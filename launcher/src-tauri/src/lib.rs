mod ryujinx;
mod settings;

use ryujinx::{Game, Install, LogEvidence, ModInfo};
use serde::{Deserialize, Serialize};
use settings::Settings;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

#[cfg(windows)]
use std::os::windows::process::CommandExt;
#[cfg(windows)]
const CREATE_NO_WINDOW: u32 = 0x0800_0000;

/// Launcher-only state (which Ryujinx, which game file). Lives in %APPDATA%\Moonrush.
#[derive(Serialize, Deserialize, Default, Clone)]
struct Prefs {
    ryujinx_exe: Option<String>,
    game_path: Option<String>,
    extra_installs: Vec<String>,
}

fn home() -> PathBuf {
    ryujinx::appdata().join("Moonrush")
}

fn backups() -> PathBuf {
    home().join("backups")
}

fn read_json<T: for<'de> Deserialize<'de> + Default>(p: &Path) -> T {
    fs::read_to_string(p).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
}

fn write_json<T: Serialize>(p: &Path, v: &T) -> Result<(), String> {
    if let Some(dir) = p.parent() {
        fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    fs::write(p, serde_json::to_string_pretty(v).map_err(|e| e.to_string())?).map_err(|e| e.to_string())
}

fn prefs() -> Prefs {
    read_json(&home().join("launcher.json"))
}

/// The chosen Ryujinx, or the most recently used one found on disk.
fn pick_exe(prefs: &Prefs, installs: &[Install]) -> Option<PathBuf> {
    prefs
        .ryujinx_exe
        .as_ref()
        .filter(|p| Path::new(p).is_file())
        .map(PathBuf::from)
        .or_else(|| installs.first().map(|i| PathBuf::from(&i.exe)))
}

/// Built game module: `mod/exefs` next to a packaged launcher, or `game/deploy` in the repo.
fn mod_source() -> Option<PathBuf> {
    let exe = std::env::current_exe().ok()?;
    let mut dir = exe.parent();
    let packaged = dir?.join("mod").join("exefs");
    if packaged.join(ryujinx::MOD_SLOT).is_file() {
        return Some(packaged);
    }
    while let Some(d) = dir {
        let dev = d.join("game").join("deploy");
        if dev.join(ryujinx::MOD_SLOT).is_file() {
            return Some(dev);
        }
        dir = d.parent();
    }
    None
}

fn ryujinx_running() -> bool {
    let mut cmd = Command::new("tasklist");
    cmd.args(["/FI", "IMAGENAME eq Ryujinx.exe", "/NH"]);
    #[cfg(windows)]
    cmd.creation_flags(CREATE_NO_WINDOW);
    cmd.output()
        .map(|o| String::from_utf8_lossy(&o.stdout).to_ascii_lowercase().contains("ryujinx.exe"))
        .unwrap_or(false)
}

#[derive(Serialize)]
struct ModState {
    installed: bool,
    enabled: bool,
    path: String,
    files: Vec<String>,
    source: Option<String>,
    /// Installed files differ from the freshly built ones.
    outdated: bool,
}

#[derive(Serialize)]
struct Status {
    installs: Vec<Install>,
    exe: Option<String>,
    data_dir: Option<String>,
    game: Option<Game>,
    moonrush: Option<ModState>,
    /// Built game module ready to install, if any.
    module_source: Option<String>,
    other_mods: Vec<ModInfo>,
    conflicts: Vec<String>,
    notes: Vec<String>,
    log: LogEvidence,
    settings_file: Option<String>,
    settings_written: bool,
    ryujinx_running: bool,
    expected_build_id: &'static str,
}

fn same_files(a: &Path, b: &Path) -> bool {
    let names = |d: &Path| -> Vec<String> {
        let mut v: Vec<String> = fs::read_dir(d)
            .map(|r| r.flatten().filter_map(|e| e.file_name().to_str().map(String::from)).collect())
            .unwrap_or_default();
        v.sort();
        v
    };
    let (na, nb) = (names(a), names(b));
    na == nb && na.iter().all(|n| fs::read(a.join(n)).ok() == fs::read(b.join(n)).ok())
}

#[tauri::command]
fn get_status() -> Status {
    let p = prefs();
    let installs = ryujinx::find_installs(&p.extra_installs);
    let exe = pick_exe(&p, &installs);
    let running = ryujinx_running();
    let Some(exe) = exe else {
        return Status {
            installs,
            exe: None,
            data_dir: None,
            game: None,
            moonrush: None,
            module_source: mod_source().map(|p| p.to_string_lossy().to_string()),
            other_mods: vec![],
            conflicts: vec![],
            notes: vec![],
            log: LogEvidence::default(),
            settings_file: None,
            settings_written: false,
            ryujinx_running: running,
            expected_build_id: ryujinx::SMO_100_BUILD_ID,
        };
    };
    let data = ryujinx::data_dir(&exe);
    let game = ryujinx::find_game(&data, p.game_path.as_deref());
    let all = ryujinx::list_mods(&data);
    let mine_dir = ryujinx::mod_dir(&data);
    let source = mod_source();

    let (mine, others): (Vec<ModInfo>, Vec<ModInfo>) =
        all.into_iter().partition(|m| Path::new(&m.path) == mine_dir.as_path());
    let moonrush = mine.first().map(|m| ModState {
        installed: m.exefs.iter().any(|f| f == ryujinx::MOD_SLOT),
        enabled: m.enabled,
        path: m.path.clone(),
        files: m.exefs.clone(),
        outdated: source.as_ref().is_some_and(|s| !same_files(s, &mine_dir.join("exefs"))),
        source: source.as_ref().map(|s| s.to_string_lossy().to_string()),
    });

    let mut conflicts = Vec::new();
    let mut notes = Vec::new();
    for o in others.iter().filter(|o| o.enabled) {
        if o.exefs.iter().any(|f| f == ryujinx::MOD_SLOT) {
            conflicts.push(format!(
                "'{}' also uses {}. Only one of them can load. Disable one.",
                o.name,
                ryujinx::MOD_SLOT
            ));
        }
        if o.exefs.iter().any(|f| f == "main") {
            conflicts.push(format!("'{}' replaces the game's main code. Moonrush's offsets would no longer match.", o.name));
        }
        if o.exefs.iter().any(|f| f == "main.npdm") && moonrush.as_ref().is_some_and(|m| m.installed) {
            // Ryujinx uses only one main.npdm. Fine if each grants every syscall the other does.
            let theirs = ryujinx::npdm_syscalls(&Path::new(&o.path).join("exefs").join("main.npdm"));
            let ours = ryujinx::npdm_syscalls(&mine_dir.join("exefs").join("main.npdm"));
            match (ours, theirs) {
                (Some(a), Some(b)) => {
                    let ours_missing = b.iter().filter(|x| !a.contains(x)).count();
                    let theirs_missing = a.iter().filter(|x| !b.contains(x)).count();
                    if ours_missing > 0 {
                        conflicts.push(format!(
                            "'{}' needs {} syscall(s) Moonrush's main.npdm doesn't grant. If Ryujinx picks Moonrush's, '{}' may break.",
                            o.name, ours_missing, o.name
                        ));
                    } else if theirs_missing > 0 {
                        notes.push(format!(
                            "Both ship main.npdm and Ryujinx uses one. Moonrush's grants every syscall that '{}' asks for; theirs lacks {} newer one(s). Check the last session below.",
                            o.name, theirs_missing
                        ));
                    }
                }
                _ => notes.push(format!("'{}' also ships main.npdm, and it couldn't be read to compare.", o.name)),
            }
        }
    }
    if let Some(u) = &game.update_selected {
        conflicts.push(format!("An update is selected in Ryujinx ({u}). Moonrush only supports SMO 1.0.0."));
    }

    let ini = ryujinx::settings_ini(&data);
    Status {
        installs,
        exe: Some(exe.to_string_lossy().to_string()),
        data_dir: Some(data.to_string_lossy().to_string()),
        game: Some(game),
        moonrush,
        module_source: source.as_ref().map(|s| s.to_string_lossy().to_string()),
        other_mods: others,
        conflicts,
        notes,
        log: ryujinx::read_log(&exe),
        settings_written: ini.is_file(),
        settings_file: Some(ini.to_string_lossy().to_string()),
        ryujinx_running: running,
        expected_build_id: ryujinx::SMO_100_BUILD_ID,
    }
}

fn current_data() -> Result<(PathBuf, PathBuf), String> {
    let p = prefs();
    let installs = ryujinx::find_installs(&p.extra_installs);
    let exe = pick_exe(&p, &installs).ok_or("No Ryujinx found. Choose Ryujinx.exe in Setup.")?;
    let data = ryujinx::data_dir(&exe);
    Ok((exe, data))
}

#[tauri::command]
fn set_prefs(ryujinx_exe: Option<String>, game_path: Option<String>) -> Result<(), String> {
    let mut p = prefs();
    if let Some(exe) = ryujinx_exe {
        if !Path::new(&exe).is_file() {
            return Err(format!("Not found: {exe}"));
        }
        if !p.extra_installs.iter().any(|e| e.eq_ignore_ascii_case(&exe)) {
            p.extra_installs.push(exe.clone());
        }
        p.ryujinx_exe = Some(exe);
    }
    if let Some(g) = game_path {
        if !g.is_empty() && !Path::new(&g).is_file() {
            return Err(format!("Not found: {g}"));
        }
        p.game_path = if g.is_empty() { None } else { Some(g) };
    }
    write_json(&home().join("launcher.json"), &p)
}

#[tauri::command]
fn get_settings() -> Settings {
    let path = home().join("settings.json");
    if path.is_file() {
        let s: Option<Settings> = fs::read_to_string(&path).ok().and_then(|t| serde_json::from_str(&t).ok());
        if let Some(s) = s {
            return s.sanitized();
        }
    }
    Settings::default()
}

/// Save the launcher copy and write the INI the game reads. Returns the sanitized settings.
#[tauri::command]
fn save_settings(settings: Settings) -> Result<Settings, String> {
    let s = settings.sanitized();
    write_json(&home().join("settings.json"), &s)?;
    let (_, data) = current_data()?;
    let ini = ryujinx::settings_ini(&data);
    if let Some(dir) = ini.parent() {
        fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    fs::write(&ini, settings::to_ini(&s)).map_err(|e| format!("writing {} failed: {e}", ini.display()))?;
    Ok(s)
}

#[derive(Serialize)]
struct CurvePoint {
    moons: i32,
    mult: f32,
}

#[derive(Serialize)]
struct CurveInfo {
    points: Vec<CurvePoint>,
    moons_to_max: Option<f32>,
    vanilla_at: Option<i32>,
}

/// Curve for the chart, computed by the same code that writes the settings file.
#[tauri::command]
fn curve(settings: Settings) -> CurveInfo {
    let m = settings.sanitized().moon_speed;
    let points = (0..=1000).step_by(5).map(|n| CurvePoint { moons: n, mult: m.multiplier(n) }).collect();
    let vanilla_at = (0..=5000).find(|&n| m.multiplier(n) >= 0.99995);
    CurveInfo { points, moons_to_max: m.moons_to_max(), vanilla_at }
}

fn copy_dir(src: &Path, dst: &Path) -> Result<(), String> {
    fs::create_dir_all(dst).map_err(|e| e.to_string())?;
    for e in fs::read_dir(src).map_err(|e| e.to_string())?.flatten() {
        let to = dst.join(e.file_name());
        if e.path().is_dir() {
            copy_dir(&e.path(), &to)?;
        } else {
            fs::copy(e.path(), &to).map_err(|er| format!("copy {} failed: {er}", e.path().display()))?;
        }
    }
    Ok(())
}

/// Move an existing folder into backups instead of deleting it.
fn move_to_backups(dir: &Path, label: &str) -> Result<PathBuf, String> {
    fs::create_dir_all(backups()).map_err(|e| e.to_string())?;
    let dst = backups().join(format!("{}-{}", ryujinx::now_stamp(), label));
    if fs::rename(dir, &dst).is_err() {
        copy_dir(dir, &dst)?;
        fs::remove_dir_all(dir).map_err(|e| e.to_string())?;
    }
    Ok(dst)
}

fn refuse_while_running() -> Result<(), String> {
    if ryujinx_running() {
        Err("Close Ryujinx first. It only reads mods when a game starts, and it can overwrite mods.json.".into())
    } else {
        Ok(())
    }
}

#[tauri::command]
fn install_mod() -> Result<String, String> {
    refuse_while_running()?;
    let (_, data) = current_data()?;
    let src = mod_source().ok_or("The Moonrush game module hasn't been built yet (no subsdk8 found).")?;
    let dir = ryujinx::mod_dir(&data);
    let mut msg = String::new();
    if dir.exists() {
        let b = move_to_backups(&dir, "Moonrush")?;
        msg = format!("Previous install backed up to {}. ", b.display());
    }
    copy_dir(&src, &dir.join("exefs"))?;
    ryujinx::set_enabled(&data, &dir, ryujinx::MOD_NAME, true, &backups())?;
    Ok(format!("{msg}Installed to {}.", dir.display()))
}

#[tauri::command]
fn uninstall_mod() -> Result<String, String> {
    refuse_while_running()?;
    let (_, data) = current_data()?;
    let dir = ryujinx::mod_dir(&data);
    if !dir.exists() {
        return Ok("Moonrush is not installed.".into());
    }
    let b = move_to_backups(&dir, "Moonrush")?;
    Ok(format!("Removed from Ryujinx. The files were kept in {}.", b.display()))
}

#[tauri::command]
fn set_mod_enabled(enabled: bool) -> Result<(), String> {
    refuse_while_running()?;
    let (_, data) = current_data()?;
    let dir = ryujinx::mod_dir(&data);
    if !dir.exists() {
        return Err("Moonrush is not installed.".into());
    }
    ryujinx::set_enabled(&data, &dir, ryujinx::MOD_NAME, enabled, &backups())
}

#[tauri::command]
fn launch(settings: Settings) -> Result<String, String> {
    let (exe, data) = current_data()?;
    if ryujinx_running() {
        return Err("Ryujinx is already running. Close it so SMO starts fresh with these settings.".into());
    }
    let game = ryujinx::find_game(&data, prefs().game_path.as_deref());
    let path = game.path.ok_or("Couldn't find Super Mario Odyssey. Set the game file in Setup.")?;
    save_settings(settings)?;
    let mut cmd = Command::new(&exe);
    cmd.arg(&path);
    if let Some(dir) = exe.parent() {
        cmd.current_dir(dir);
    }
    cmd.spawn().map_err(|e| format!("Couldn't start Ryujinx: {e}"))?;
    Ok(format!("Started {}", exe.display()))
}

#[tauri::command]
fn open_folder(path: String) -> Result<(), String> {
    let p = PathBuf::from(&path);
    let target = if p.is_file() { p.parent().map(Path::to_path_buf).unwrap_or(p) } else { p };
    if !target.exists() {
        return Err(format!("Not found: {}", target.display()));
    }
    Command::new("explorer").arg(target).spawn().map(|_| ()).map_err(|e| e.to_string())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            get_status,
            set_prefs,
            get_settings,
            save_settings,
            curve,
            install_mod,
            uninstall_mod,
            set_mod_enabled,
            launch,
            open_folder
        ])
        .run(tauri::generate_context!())
        .expect("error while running Moonrush");
}
