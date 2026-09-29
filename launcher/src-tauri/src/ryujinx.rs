//! Everything the launcher knows about Ryujinx, SMO and installed mods, read from real files on disk.

use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

pub const SMO_TITLE_ID: &str = "0100000000010000";
/// `main` build ID of Super Mario Odyssey 1.0.0, the only version Moonrush's offsets are for.
pub const SMO_100_BUILD_ID: &str = "3CA12DFAAF9C82DA064D1698DF79CDA1";
pub const MOD_NAME: &str = "Moonrush";
pub const MOD_SLOT: &str = "subsdk8";

/// Ryujinx keeps its data next to the exe when a `portable` folder exists, otherwise in %APPDATA%.
pub fn data_dir(exe: &Path) -> PathBuf {
    if let Some(dir) = exe.parent() {
        let portable = dir.join("portable");
        if portable.is_dir() {
            return portable;
        }
    }
    appdata().join("Ryujinx")
}

pub fn appdata() -> PathBuf {
    PathBuf::from(std::env::var("APPDATA").unwrap_or_default())
}

pub fn mod_dir(data: &Path) -> PathBuf {
    data.join("mods").join("contents").join(SMO_TITLE_ID).join(MOD_NAME)
}

pub fn mods_json(data: &Path) -> PathBuf {
    data.join("games").join(SMO_TITLE_ID).join("mods.json")
}

pub fn settings_ini(data: &Path) -> PathBuf {
    data.join("sdcard").join("Moonrush").join("settings.ini")
}

pub fn now_stamp() -> String {
    let secs = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
    secs.to_string()
}

fn mtime(p: &Path) -> u64 {
    fs::metadata(p)
        .and_then(|m| m.modified())
        .ok()
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

// ---------------------------------------------------------------- Ryujinx installs

#[derive(Serialize, Clone)]
pub struct Install {
    pub exe: String,
    /// Unix seconds of the newest log, i.e. roughly when this copy was last used.
    pub last_used: u64,
}

fn find_named(dir: &Path, name: &str, depth: u32, out: &mut Vec<PathBuf>) {
    let Ok(entries) = fs::read_dir(dir) else { return };
    for e in entries.flatten() {
        let p = e.path();
        if p.is_dir() {
            if depth > 0 {
                find_named(&p, name, depth - 1, out);
            }
        } else if p.file_name().and_then(|n| n.to_str()).is_some_and(|n| n.eq_ignore_ascii_case(name)) {
            out.push(p);
        }
    }
}

pub fn newest_log(exe: &Path) -> Option<PathBuf> {
    let logs = exe.parent()?.join("Logs");
    fs::read_dir(logs)
        .ok()?
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|x| x == "log"))
        .max_by_key(|p| mtime(p))
}

pub fn find_installs(extra: &[String]) -> Vec<Install> {
    let mut found = Vec::new();
    for root in ["D:\\Emulators", "C:\\Emulators", "C:\\Program Files\\Ryujinx"] {
        find_named(Path::new(root), "Ryujinx.exe", 3, &mut found);
    }
    let local = PathBuf::from(std::env::var("LOCALAPPDATA").unwrap_or_default());
    find_named(&local.join("Ryujinx"), "Ryujinx.exe", 2, &mut found);
    for e in extra {
        let p = PathBuf::from(e);
        if p.is_file() {
            found.push(p);
        }
    }
    let mut out: Vec<Install> = Vec::new();
    for p in found {
        let exe = p.to_string_lossy().to_string();
        if out.iter().any(|i| i.exe.eq_ignore_ascii_case(&exe)) {
            continue;
        }
        let last_used = newest_log(&p).map(|l| mtime(&l)).unwrap_or(0);
        out.push(Install { exe, last_used });
    }
    out.sort_by(|a, b| b.last_used.cmp(&a.last_used));
    out
}

// ---------------------------------------------------------------- Game

#[derive(Serialize, Clone)]
pub struct Game {
    pub path: Option<String>,
    /// An update is selected in Ryujinx, so the game is not 1.0.0.
    pub update_selected: Option<String>,
}

pub fn find_game(data: &Path, preferred: Option<&str>) -> Game {
    let update_selected = fs::read_to_string(data.join("games").join(SMO_TITLE_ID).join("updates.json"))
        .ok()
        .and_then(|t| serde_json::from_str::<Value>(&t).ok())
        .and_then(|v| v.get("selected").and_then(|s| s.as_str()).map(String::from))
        .filter(|s| !s.is_empty());

    if let Some(p) = preferred.filter(|p| Path::new(p).is_file()) {
        return Game { path: Some(p.to_string()), update_selected };
    }
    let mut dirs = Vec::new();
    if let Ok(t) = fs::read_to_string(data.join("Config.json")) {
        if let Ok(v) = serde_json::from_str::<Value>(&t) {
            if let Some(arr) = v.get("game_dirs").and_then(|g| g.as_array()) {
                dirs.extend(arr.iter().filter_map(|d| d.as_str()).map(PathBuf::from));
            }
        }
    }
    let mut candidates = Vec::new();
    for d in &dirs {
        collect_games(d, 2, &mut candidates);
    }
    // The base game file carries the title ID and [v0] in its name; updates use ...0800.
    candidates.sort_by_key(|p| !p.to_string_lossy().contains("[v0]"));
    Game { path: candidates.first().map(|p| p.to_string_lossy().to_string()), update_selected }
}

fn collect_games(dir: &Path, depth: u32, out: &mut Vec<PathBuf>) {
    let Ok(entries) = fs::read_dir(dir) else { return };
    for e in entries.flatten() {
        let p = e.path();
        if p.is_dir() {
            if depth > 0 {
                collect_games(&p, depth - 1, out);
            }
            continue;
        }
        let name = p.file_name().and_then(|n| n.to_str()).unwrap_or("").to_ascii_lowercase();
        let is_game = name.ends_with(".nsp") || name.ends_with(".xci");
        if is_game && name.contains(&SMO_TITLE_ID.to_ascii_lowercase()) {
            out.push(p);
        }
    }
}

// ---------------------------------------------------------------- Mods

#[derive(Serialize, Clone)]
pub struct ModInfo {
    pub name: String,
    pub path: String,
    pub enabled: bool,
    /// exefs files this mod replaces, e.g. ["main.npdm", "subsdk9"].
    pub exefs: Vec<String>,
    pub has_romfs: bool,
}

fn read_mods_json(data: &Path) -> Value {
    fs::read_to_string(mods_json(data))
        .ok()
        .and_then(|t| serde_json::from_str(&t).ok())
        .unwrap_or_else(|| serde_json::json!({ "mods": [] }))
}

/// Ryujinx treats a mod that is not listed in mods.json as enabled.
fn listed_enabled(json: &Value, path: &Path) -> bool {
    let want = path.to_string_lossy().to_ascii_lowercase();
    json.get("mods")
        .and_then(|m| m.as_array())
        .and_then(|arr| {
            arr.iter().find(|m| {
                m.get("path").and_then(|p| p.as_str()).is_some_and(|p| p.to_ascii_lowercase() == want)
            })
        })
        .and_then(|m| m.get("enabled").and_then(|e| e.as_bool()))
        .unwrap_or(true)
}

pub fn list_mods(data: &Path) -> Vec<ModInfo> {
    let json = read_mods_json(data);
    let roots = [
        data.join("mods").join("contents").join(SMO_TITLE_ID),
        data.join("sdcard").join("atmosphere").join("contents").join(SMO_TITLE_ID),
    ];
    let mut out = Vec::new();
    for root in roots {
        let Ok(entries) = fs::read_dir(&root) else { continue };
        for e in entries.flatten() {
            let p = e.path();
            if !p.is_dir() {
                continue;
            }
            let mut exefs: Vec<String> = fs::read_dir(p.join("exefs"))
                .map(|r| r.flatten().filter_map(|f| f.file_name().to_str().map(String::from)).collect())
                .unwrap_or_default();
            exefs.sort();
            out.push(ModInfo {
                name: e.file_name().to_string_lossy().to_string(),
                enabled: listed_enabled(&json, &p),
                path: p.to_string_lossy().to_string(),
                exefs,
                has_romfs: p.join("romfs").is_dir(),
            });
        }
    }
    out
}

/// Copy mods.json aside before any change so it can be restored.
pub fn backup_file(src: &Path, backups: &Path, label: &str) -> Result<Option<PathBuf>, String> {
    if !src.is_file() {
        return Ok(None);
    }
    fs::create_dir_all(backups).map_err(|e| e.to_string())?;
    let name = src.file_name().and_then(|n| n.to_str()).unwrap_or("file");
    let dst = backups.join(format!("{}-{}-{}", now_stamp(), label, name));
    fs::copy(src, &dst).map_err(|e| format!("backup of {} failed: {e}", src.display()))?;
    Ok(Some(dst))
}

pub fn set_enabled(data: &Path, mod_path: &Path, name: &str, enabled: bool, backups: &Path) -> Result<(), String> {
    let file = mods_json(data);
    backup_file(&file, backups, "mods-json")?;
    let mut json = read_mods_json(data);
    let want = mod_path.to_string_lossy().to_string();
    let arr = json
        .as_object_mut()
        .ok_or("mods.json is not an object")?
        .entry("mods")
        .or_insert_with(|| Value::Array(vec![]))
        .as_array_mut()
        .ok_or("mods.json 'mods' is not a list")?;
    match arr.iter_mut().find(|m| {
        m.get("path").and_then(|p| p.as_str()).is_some_and(|p| p.eq_ignore_ascii_case(&want))
    }) {
        Some(entry) => {
            entry["enabled"] = Value::Bool(enabled);
        }
        None => arr.push(serde_json::json!({ "name": name, "path": want, "enabled": enabled })),
    }
    if let Some(parent) = file.parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    let text = serde_json::to_string_pretty(&json).map_err(|e| e.to_string())?;
    fs::write(&file, text).map_err(|e| format!("writing mods.json failed: {e}"))
}

// ---------------------------------------------------------------- main.npdm

/// Syscall IDs a main.npdm allows (from the ACI0 kernel capabilities), or None if unreadable.
pub fn npdm_syscalls(path: &Path) -> Option<Vec<u32>> {
    let d = fs::read(path).ok()?;
    let u32_at = |o: usize| -> Option<u32> { Some(u32::from_le_bytes(d.get(o..o + 4)?.try_into().ok()?)) };
    if d.get(0..4)? != b"META" {
        return None;
    }
    let aci = u32_at(0x70)? as usize;
    if d.get(aci..aci + 4)? != b"ACI0" {
        return None;
    }
    let kc_off = aci + u32_at(aci + 0x30)? as usize;
    let kc_size = u32_at(aci + 0x34)? as usize;
    let mut out = Vec::new();
    for i in (0..kc_size).step_by(4) {
        let v = u32_at(kc_off + i)?;
        if v & 0x1f == 0x0f {
            let index = v >> 29;
            let mask = (v >> 5) & 0x00ff_ffff;
            for bit in 0..24 {
                if mask >> bit & 1 == 1 {
                    out.push(index * 24 + bit);
                }
            }
        }
    }
    out.sort_unstable();
    out.dedup();
    Some(out)
}

// ---------------------------------------------------------------- Log evidence

#[derive(Serialize, Clone, Default)]
pub struct LogEvidence {
    pub file: Option<String>,
    pub modified: u64,
    pub build_ids: Vec<String>,
    pub moonrush_found_enabled: bool,
    pub moonrush_found_disabled: bool,
    pub slot_replaced: bool,
    /// Version check passed and the hooks went in ("[Moonrush] loaded ...").
    pub hooks_installed: bool,
    /// Version check failed ("[Moonrush] inactive: ...").
    pub inactive: bool,
    /// Getters another mod hooked first, which Moonrush stacks on.
    pub stacked: Vec<String>,
    /// A "moons=" line: the multiplier was applied while Mario was in a level.
    pub applied_in_game: bool,
    /// `[Moonrush]` lines the game module printed (newest last, capped).
    pub guest_lines: Vec<String>,
    pub warnings: Vec<String>,
}

pub fn read_log(exe: &Path) -> LogEvidence {
    let Some(file) = newest_log(exe) else { return LogEvidence::default() };
    let text = fs::read(&file).map(|b| String::from_utf8_lossy(&b).to_string()).unwrap_or_default();
    let mut ev = LogEvidence {
        file: Some(file.to_string_lossy().to_string()),
        modified: mtime(&file),
        ..Default::default()
    };
    let mut in_build_ids = false;
    let slot_line = format!("NSO '{MOD_SLOT}' replaced");
    for line in text.lines() {
        if in_build_ids {
            let t = line.trim();
            if !t.is_empty() && t.len() >= 32 && t.chars().all(|c| c.is_ascii_hexdigit()) {
                ev.build_ids.push(t[..32].to_string());
                continue;
            }
            in_build_ids = false;
        }
        if line.contains("Build ids found for application") && line.contains(SMO_TITLE_ID) {
            ev.build_ids.clear();
            in_build_ids = true;
        } else if line.contains("Found enabled mod 'Moonrush'") {
            ev.moonrush_found_enabled = true;
        } else if line.contains("Found disabled mod 'Moonrush'") {
            ev.moonrush_found_disabled = true;
        } else if line.contains(&slot_line) {
            ev.slot_replaced = true;
        } else if line.contains("[Moonrush]") {
            let at = line.find("[Moonrush]").unwrap_or(0);
            let msg = line[at..].trim_end();
            if msg.starts_with("[Moonrush] loaded ") {
                ev.hooks_installed = true;
            } else if msg.starts_with("[Moonrush] inactive:") {
                ev.inactive = true;
            } else if let Some(rest) = msg.strip_prefix("[Moonrush] stacking: ") {
                if let Some(name) = rest.split_whitespace().next() {
                    ev.stacked.push(name.to_string());
                }
            } else if msg.starts_with("[Moonrush] moons=") {
                ev.applied_in_game = true;
            }
            ev.guest_lines.push(msg.to_string());
            if ev.guest_lines.len() > 200 {
                ev.guest_lines.remove(0);
            }
        } else if line.contains("ModLoader") && (line.contains("|W|") || line.contains("|E|")) {
            ev.warnings.push(line.trim().to_string());
        }
    }
    ev
}

/// A capture the game module reported ("[Moonrush] capture: name=Kuribo ..."), and how it moves.
#[derive(Serialize, Deserialize, Clone, Debug, PartialEq, Default)]
pub struct CaptureSeen {
    pub name: String,
    /// "Collider" / "PlayerCollider" = Moonrush can speed it up. "none" = it moves some other way.
    pub path: Option<String>,
}

/// Captures reported in one log's text, first-seen order.
pub fn parse_captures(text: &str, out: &mut Vec<CaptureSeen>) {
    for line in text.lines() {
        let Some(at) = line.find("[Moonrush] capture: name=") else { continue };
        let rest = &line[at + "[Moonrush] capture: name=".len()..];
        let name = rest.split_whitespace().next().unwrap_or("");
        if !crate::settings::is_capture_name(name) {
            continue;
        }
        let path = rest.split("moves through ").nth(1).map(|p| p.trim().to_string());
        match out.iter_mut().find(|c| c.name == name) {
            Some(c) => {
                // "none" is only reported before the first move, so a real path wins.
                if path.as_deref().is_some_and(|p| p != "none") || c.path.is_none() {
                    c.path = path.or(c.path.take());
                }
            }
            None => out.push(CaptureSeen { name: name.to_string(), path }),
        }
    }
}

/// Every capture reported in any of Ryujinx's logs.
pub fn captures_in_logs(exe: &Path) -> Vec<CaptureSeen> {
    let mut out = Vec::new();
    let Some(dir) = exe.parent().map(|d| d.join("Logs")) else { return out };
    let mut files: Vec<PathBuf> = fs::read_dir(dir)
        .map(|r| r.flatten().map(|e| e.path()).filter(|p| p.extension().is_some_and(|x| x == "log")).collect())
        .unwrap_or_default();
    files.sort_by_key(|p| mtime(p));
    for f in files {
        if let Ok(b) = fs::read(&f) {
            parse_captures(&String::from_utf8_lossy(&b), &mut out);
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_capture_lines() {
        let mut out = vec![CaptureSeen { name: "Frog".into(), path: Some("PlayerCollider".into()) }];
        parse_captures(
            "1 |W| KernelSvc OutputDebugString: [Moonrush] capture: name=Kuribo mult=2.030\n\
             2 |W| KernelSvc OutputDebugString: [Moonrush] capture: name=Kuribo moves through none\n\
             3 |W| KernelSvc OutputDebugString: [Moonrush] capture: name=Kuribo moves through Collider\n\
             4 |W| KernelSvc OutputDebugString: [Moonrush] capture: name=Frog mult=1.000 (switched off)\n\
             5 |W| KernelSvc OutputDebugString: [Moonrush] capture: name=? mult=2.030\n",
            &mut out,
        );
        assert_eq!(out.len(), 2);
        assert_eq!(out[0].path.as_deref(), Some("PlayerCollider"));
        assert_eq!(out[1], CaptureSeen { name: "Kuribo".into(), path: Some("Collider".into()) });
    }

    #[test]
    fn parses_real_log_shapes() {
        let dir = std::env::temp_dir().join(format!("moonrush-test-{}", now_stamp()));
        let logs = dir.join("Logs");
        fs::create_dir_all(&logs).unwrap();
        fs::write(
            logs.join("Ryujinx_x.log"),
            "00:00:00.847 |I| ModLoader LoadCheats: Build ids found for application 0100000000010000:\n\
             \x20   A75512BE30BB2A8C880177505D7A0B3E24E9D642000000000000000000000000\n\
             \x20   3CA12DFAAF9C82DA064D1698DF79CDA100000000000000000000000000000000\n\
             00:00:00.9 |I| ModLoader AddModsFromDirectory: Found enabled mod 'Moonrush' [E]\n\
             00:00:01.0 |I| ModLoader ApplyExefsMods: NSO 'subsdk8' replaced\n\
             00:00:02.0 |W| KernelSvc OutputDebugString: [Moonrush] stacking: getNormalMaxSpeed is already hooked by another mod\n\
             00:00:02.1 |W| KernelSvc OutputDebugString: [Moonrush] loaded v0.2.0 for SMO 1.0.0\n\
             00:00:09.0 |W| KernelSvc OutputDebugString: [Moonrush] moons=12 mult=0.780\n",
        )
        .unwrap();
        let ev = read_log(&dir.join("Ryujinx.exe"));
        assert!(ev.build_ids.iter().any(|b| b == SMO_100_BUILD_ID));
        assert!(ev.moonrush_found_enabled && ev.slot_replaced);
        assert!(ev.hooks_installed && ev.applied_in_game && !ev.inactive);
        assert_eq!(ev.stacked, vec!["getNormalMaxSpeed"]);
        assert_eq!(ev.guest_lines.last().unwrap(), "[Moonrush] moons=12 mult=0.780");
        let _ = fs::remove_dir_all(dir);
    }
}
