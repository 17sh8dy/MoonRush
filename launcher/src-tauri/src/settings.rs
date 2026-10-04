//! Moonrush settings: the launcher's model, the speed curve, and the INI file the game module reads.
//!
//! The curve here MUST match `game/src/moon_speed.cpp` exactly. Both sides are checked against
//! the same vectors in `docs/curve-vectors.txt`.

use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CountSource {
    /// Every Moon ever collected on the save file. Only goes up.
    Total,
    /// Moons held right now. Drops when Moons are paid into the Odyssey.
    Current,
}

#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Curve {
    Linear,
    /// Big gains early, flattening out near the cap.
    FrontLoaded,
    /// Slow start, big gains late.
    BackLoaded,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Groups {
    pub walk: bool,
    pub squat: bool,
    pub dive: bool,
    pub roll: bool,
    pub long_jump: bool,
    pub air: bool,
    pub swim: bool,
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct MoonSpeed {
    pub enabled: bool,
    pub count: CountSource,
    pub start: f32,
    pub per_moon: f32,
    pub max: f32,
    pub curve: Curve,
    pub groups: Groups,
}

/// Captures follow the same multiplier as Mario. `off` = internal capture names left vanilla.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Captures {
    pub enabled: bool,
    pub off: Vec<String>,
    /// Cappy's throw speed and reach follow the multiplier too. Off unless turned on.
    pub cappy: bool,
}

impl Default for Captures {
    fn default() -> Self {
        Captures { enabled: true, off: vec![], cappy: false }
    }
}

/// Jump Height: how many times higher Mario jumps. Only launch power changes (by sqrt of this), so gravity is vanilla.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Jump {
    pub enabled: bool,
    /// The jump height reached at full Moon progress (or always, with `scale` off).
    pub height: f32,
    /// v0.7: jump height is EARNED like speed: 1x at 0 Moons, rising to `height` along the Moon Speed curve.
    pub scale: bool,
}

impl Default for Jump {
    fn default() -> Self {
        Jump { enabled: false, height: 1.0, scale: true }
    }
}

/// Moon Animation Speed (v0.6): how many times faster the ordinary Moon-get demo plays. Off by default.
/// Only the demo's own per-frame updates are repeated; Moon counting, saving and the fanfare logic are untouched.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct MoonAnim {
    pub enabled: bool,
    pub speed: f32,
}

impl Default for MoonAnim {
    fn default() -> Self {
        MoonAnim { enabled: false, speed: 2.0 }
    }
}

/// First Person button. Never the right-stick click: SMO uses it for its own look-around view.
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum FpButton {
    Lstick,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
}

/// Camera in Mario's head. Camera only: no gameplay change.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct FirstPerson {
    pub enabled: bool,
    /// Tap = switch first/third person in game.
    pub button: FpButton,
    /// Hold the button to peek at third person.
    pub peek: bool,
    pub off_cutscenes: bool,
    pub off_2d: bool,
    pub off_captures: bool,
}

impl Default for FirstPerson {
    fn default() -> Self {
        FirstPerson {
            enabled: false,
            button: FpButton::Lstick,
            peek: true,
            off_cutscenes: true,
            off_2d: true,
            off_captures: true,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Settings {
    pub moon_speed: MoonSpeed,
    /// Missing in settings saved before v0.3.0, so these fall back to the defaults.
    #[serde(default)]
    pub captures: Captures,
    #[serde(default)]
    pub first_person: FirstPerson,
    /// Missing in settings saved before v0.4.0.
    #[serde(default)]
    pub jump: Jump,
    /// Missing in settings saved before v0.6.
    #[serde(default)]
    pub moon_anim: MoonAnim,
}

/// Capture names are the game's internal ids ("Kuribo", "TRex"): letters, digits, underscore.
pub fn is_capture_name(n: &str) -> bool {
    !n.is_empty() && n.len() < 32 && n.chars().all(|c| c.is_ascii_alphanumeric() || c == '_')
}

pub const START_MIN: f32 = 0.25;
pub const START_MAX: f32 = 2.0;
pub const PER_MOON_MAX: f32 = 0.05;
/// Hard ceiling. Raised 5 -> 10 on 2026-10-04 as an OPTION to test; clipping at high speed is UNTESTED above 3x.
pub const MAX_CEILING: f32 = 10.0;
pub const JUMP_HEIGHT_MIN: f32 = 1.0;
/// Raised 5 -> 10 on 2026-10-04 (nothing tested above 5x was ever clipped or skipped); the launcher warns above 5x.
pub const JUMP_HEIGHT_MAX: f32 = 10.0;
pub const MOON_ANIM_MIN: f32 = 1.0;
pub const MOON_ANIM_MAX: f32 = 5.0;

impl Default for Settings {
    fn default() -> Self {
        Settings {
            moon_speed: MoonSpeed {
                enabled: true,
                count: CountSource::Total,
                start: 0.75,
                per_moon: 0.0025,
                max: 2.0,
                curve: Curve::Linear,
                groups: Groups {
                    walk: true,
                    squat: true,
                    dive: true,
                    roll: true,
                    long_jump: true,
                    air: true,
                    swim: true,
                },
            },
            captures: Captures::default(),
            first_person: FirstPerson::default(),
            jump: Jump::default(),
            moon_anim: MoonAnim::default(),
        }
    }
}

fn finite_or(v: f32, fallback: f32) -> f32 {
    if v.is_finite() { v } else { fallback }
}

impl Settings {
    /// Clamp everything into the safe range. Applied before every save.
    pub fn sanitized(mut self) -> Self {
        let d = Settings::default().moon_speed;
        let m = &mut self.moon_speed;
        m.start = finite_or(m.start, d.start).clamp(START_MIN, START_MAX);
        m.per_moon = finite_or(m.per_moon, d.per_moon).clamp(0.0, PER_MOON_MAX);
        m.max = finite_or(m.max, d.max).clamp(m.start, MAX_CEILING);
        self.jump.height = finite_or(self.jump.height, 1.0).clamp(JUMP_HEIGHT_MIN, JUMP_HEIGHT_MAX);
        self.moon_anim.speed = finite_or(self.moon_anim.speed, 2.0).clamp(MOON_ANIM_MIN, MOON_ANIM_MAX);
        let c = &mut self.captures;
        c.off.retain(|n| is_capture_name(n));
        c.off.sort();
        c.off.dedup();
        c.off.truncate(48);
        self
    }
}

/// Lowest manual speed the live panel can ask for.
pub const LIVE_MIN: f32 = 0.25;

/// The live-speed rule: a manual speed never exceeds the speed earned at the current Moon count, and never goes
/// below `LIVE_MIN`. `None` (or a non-finite request) means the earned speed. The game module enforces the same
/// rule itself (`moonrush::liveSpeed`); both are checked against tests/live_vectors.txt.
pub fn live_speed(earned: f32, manual: Option<f32>) -> f32 {
    match manual {
        Some(x) if x.is_finite() => x.clamp(LIVE_MIN.min(earned), earned),
        _ => earned,
    }
}

/// The jump height the Moon count has earned: 1x at 0 Moons, rising to the Jump Height setting along the same curve
/// as Moon Speed (or fixed at the setting with "Scale with Moons" off). 1x when Jump Height is off.
/// The game computes the same thing (`moonrush::earnedJump`); both are checked against tests/jump_vectors.txt.
pub fn earned_jump(s: &Settings, moons: i32) -> f32 {
    if !s.jump.enabled {
        return 1.0;
    }
    if !s.jump.scale {
        return s.jump.height;
    }
    1.0 + (s.jump.height - 1.0) * s.moon_speed.progress(moons)
}

/// A manual jump height never exceeds the earned jump and never goes below 1x. None = the earned jump.
pub fn live_jump(earned: f32, manual: Option<f32>) -> f32 {
    match manual {
        Some(x) if x.is_finite() => x.clamp(JUMP_HEIGHT_MIN.min(earned), earned),
        _ => earned,
    }
}

/// Text of live.ini, the request the game polls. `seq` must increase with every request.
pub fn live_control_text(seq: u64, speed: Option<f32>, jump: Option<f32>) -> String {
    let one = |v: Option<f32>| match v {
        Some(x) if x.is_finite() => format!("{x:.4}"),
        _ => "earned".to_string(),
    };
    format!("seq={seq}\nspeed={}\njump={}\n", one(speed), one(jump))
}

impl MoonSpeed {
    /// Moons needed to reach `max`, or None if the multiplier never moves.
    pub fn moons_to_max(&self) -> Option<f32> {
        let span = self.max - self.start;
        if span <= 0.0 || self.per_moon <= 0.0 {
            None
        } else {
            Some(span / self.per_moon)
        }
    }

    /// How far along the progression the Moon count is, 0..1, after the curve. 0 if it never moves.
    /// Speed and the earned jump both use this, so they rise together.
    pub fn progress(&self, moons: i32) -> f32 {
        let Some(to_max) = self.moons_to_max() else { return 0.0 };
        let t = (moons.max(0) as f32 / to_max).clamp(0.0, 1.0);
        match self.curve {
            Curve::Linear => t,
            Curve::FrontLoaded => 1.0 - (1.0 - t) * (1.0 - t),
            Curve::BackLoaded => t * t,
        }
    }

    pub fn multiplier(&self, moons: i32) -> f32 {
        let Some(_) = self.moons_to_max() else { return self.start };
        self.start + (self.max - self.start) * self.progress(moons)
    }
}

fn b(v: bool) -> &'static str {
    if v { "1" } else { "0" }
}

/// The file the game module parses: flat `key=value` lines, `;` comments.
pub fn to_ini(s: &Settings) -> String {
    let m = &s.moon_speed;
    let g = &m.groups;
    let fp = &s.first_person;
    let count = match m.count {
        CountSource::Total => "total",
        CountSource::Current => "current",
    };
    let curve = match m.curve {
        Curve::Linear => "linear",
        Curve::FrontLoaded => "front",
        Curve::BackLoaded => "back",
    };
    format!(
        "; Moonrush settings. Written by the Moonrush launcher; read by the game at startup.\n\
         version=1\n\
         moon_speed.enabled={}\n\
         moon_speed.count={}\n\
         moon_speed.start={:.4}\n\
         moon_speed.per_moon={:.5}\n\
         moon_speed.max={:.4}\n\
         moon_speed.curve={}\n\
         moon_speed.walk={}\n\
         moon_speed.squat={}\n\
         moon_speed.dive={}\n\
         moon_speed.roll={}\n\
         moon_speed.long_jump={}\n\
         moon_speed.air={}\n\
         moon_speed.swim={}\n\
         captures.enabled={}\n\
         captures.off={}\n\
         cappy.enabled={}\n\
         jump.enabled={}\n\
         jump.height={:.3}\n\
         jump.scale={}\n\
         moon_anim.enabled={}\n\
         moon_anim.speed={:.3}\n\
         first_person.enabled={}\n\
         first_person.button={}\n\
         first_person.peek={}\n\
         first_person.off_cutscenes={}\n\
         first_person.off_2d={}\n\
         first_person.off_captures={}\n",
        b(m.enabled), count, m.start, m.per_moon, m.max, curve,
        b(g.walk), b(g.squat), b(g.dive), b(g.roll), b(g.long_jump), b(g.air), b(g.swim),
        b(s.captures.enabled),
        s.captures.off.iter().filter(|n| is_capture_name(n)).cloned().collect::<Vec<_>>().join(","),
        b(s.captures.cappy),
        b(s.jump.enabled), s.jump.height, b(s.jump.scale),
        b(s.moon_anim.enabled), s.moon_anim.speed,
        b(fp.enabled),
        match fp.button {
            FpButton::Lstick => "lstick",
            FpButton::DpadUp => "dpad_up",
            FpButton::DpadDown => "dpad_down",
            FpButton::DpadLeft => "dpad_left",
            FpButton::DpadRight => "dpad_right",
        },
        b(fp.peek), b(fp.off_cutscenes), b(fp.off_2d), b(fp.off_captures),
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn close(a: f32, b: f32) -> bool {
        (a - b).abs() < 1e-4
    }

    #[test]
    fn default_curve_hits_vanilla_at_100_and_caps_at_500() {
        let m = Settings::default().moon_speed;
        assert!(close(m.multiplier(0), 0.75));
        assert!(close(m.multiplier(100), 1.0));
        assert!(close(m.multiplier(500), 2.0));
        assert!(close(m.multiplier(999), 2.0));
        assert!(close(m.multiplier(-5), 0.75));
    }

    #[test]
    fn shaped_curves_share_endpoints() {
        for curve in [Curve::FrontLoaded, Curve::BackLoaded] {
            let mut m = Settings::default().moon_speed;
            m.curve = curve;
            assert!(close(m.multiplier(0), 0.75));
            assert!(close(m.multiplier(500), 2.0));
        }
        let mut m = Settings::default().moon_speed;
        m.curve = Curve::FrontLoaded;
        assert!(close(m.multiplier(250), 0.75 + 1.25 * 0.75));
        m.curve = Curve::BackLoaded;
        assert!(close(m.multiplier(250), 0.75 + 1.25 * 0.25));
    }

    #[test]
    fn flat_when_no_growth() {
        let mut m = Settings::default().moon_speed;
        m.per_moon = 0.0;
        assert!(close(m.multiplier(300), 0.75));
    }

    #[test]
    fn sanitize_clamps_unsafe_values() {
        let mut s = Settings::default();
        s.moon_speed.start = 9.0;
        s.moon_speed.max = 50.0;
        s.moon_speed.per_moon = f32::NAN;
        let s = s.sanitized();
        assert!(close(s.moon_speed.start, START_MAX));
        assert!(close(s.moon_speed.max, MAX_CEILING));
        assert!(close(s.moon_speed.per_moon, 0.0025));
    }

    #[test]
    fn capture_switches_are_cleaned_and_written() {
        let mut s = Settings::default();
        s.captures.off = vec!["TRex".into(), "Kuribo".into(), "bad name".into(), "Kuribo".into(), "a,b".into()];
        s.captures.cappy = true;
        let s = s.sanitized();
        assert_eq!(s.captures.off, vec!["Kuribo", "TRex"]);
        let ini = to_ini(&s);
        assert!(ini.contains("captures.off=Kuribo,TRex\n"));
        assert!(ini.contains("cappy.enabled=1\n"));
    }

    #[test]
    fn old_settings_json_without_captures_still_loads() {
        let old = r#"{"moon_speed":{"enabled":true,"count":"total","start":0.75,"per_moon":0.0025,"max":2.0,
            "curve":"linear","groups":{"walk":true,"squat":true,"dive":true,"roll":true,"long_jump":true,"air":true,"swim":true}}}"#;
        let s: Settings = serde_json::from_str(old).unwrap();
        assert_eq!(s.captures, Captures::default());
        assert_eq!(s.first_person, FirstPerson::default());
    }

    #[test]
    fn jump_height_is_clamped_and_written() {
        let mut s = Settings::default();
        assert_eq!(s.jump, Jump { enabled: false, height: 1.0, scale: true });
        s.jump.enabled = true;
        s.jump.height = 99.0;
        let ini = to_ini(&s.clone().sanitized());
        assert!(ini.contains("jump.enabled=1\n") && ini.contains("jump.height=10.000\n"));
        s.jump.height = f32::NAN;
        assert!(close(s.clone().sanitized().jump.height, 1.0));
        s.jump.height = 0.2;
        assert!(close(s.sanitized().jump.height, JUMP_HEIGHT_MIN));
    }

    #[test]
    fn moon_anim_speed_is_clamped_and_written() {
        let mut s = Settings::default();
        assert_eq!(s.moon_anim, MoonAnim { enabled: false, speed: 2.0 });
        s.moon_anim.enabled = true;
        s.moon_anim.speed = 9.0;
        let ini = to_ini(&s.clone().sanitized());
        assert!(ini.contains("moon_anim.enabled=1\n") && ini.contains("moon_anim.speed=5.000\n"));
        s.moon_anim.speed = f32::NAN;
        assert!(close(s.clone().sanitized().moon_anim.speed, 2.0));
        s.moon_anim.speed = 0.2;
        assert!(close(s.sanitized().moon_anim.speed, MOON_ANIM_MIN));
    }

    #[test]
    fn old_settings_json_without_moon_anim_still_loads() {
        let old = r#"{"moon_speed":{"enabled":true,"count":"total","start":0.75,"per_moon":0.0025,"max":2.0,
            "curve":"linear","groups":{"walk":true,"squat":true,"dive":true,"roll":true,"long_jump":true,"air":true,"swim":true}}}"#;
        let s: Settings = serde_json::from_str(old).unwrap();
        assert_eq!(s.moon_anim, MoonAnim::default());
    }

    #[test]
    fn old_settings_json_without_jump_still_loads() {
        let old = r#"{"moon_speed":{"enabled":true,"count":"total","start":0.75,"per_moon":0.0025,"max":2.0,
            "curve":"linear","groups":{"walk":true,"squat":true,"dive":true,"roll":true,"long_jump":true,"air":true,"swim":true}}}"#;
        let s: Settings = serde_json::from_str(old).unwrap();
        assert_eq!(s.jump, Jump::default());
    }

    #[test]
    fn first_person_keys() {
        let mut s = Settings::default();
        s.first_person.enabled = true;
        s.first_person.button = FpButton::DpadDown;
        s.first_person.off_captures = false;
        let ini = to_ini(&s);
        for key in ["first_person.enabled=1\n", "first_person.button=dpad_down\n", "first_person.peek=1\n",
                    "first_person.off_cutscenes=1\n", "first_person.off_2d=1\n", "first_person.off_captures=0\n"] {
            assert!(ini.contains(key), "missing {key}");
        }
    }

    #[test]
    fn shared_live_vectors() {
        let text = include_str!("../../../tests/live_vectors.txt");
        let mut n = 0;
        for line in text.lines().filter(|l| !l.is_empty() && !l.starts_with('#')) {
            let f: Vec<&str> = line.split_whitespace().collect();
            let earned: f32 = f[0].parse().unwrap();
            let requested: f32 = f[2].parse().unwrap();
            let want: f32 = f[3].parse().unwrap();
            let got = live_speed(earned, if f[1] == "manual" { Some(requested) } else { None });
            assert!(close(got, want), "{line} -> {got}");
            n += 1;
        }
        assert!(n >= 10);
        assert!(close(live_speed(2.2, Some(f32::NAN)), 2.2));
        assert!(close(live_speed(2.2, Some(f32::INFINITY)), 2.2));
    }

    #[test]
    fn live_control_text_format() {
        assert_eq!(live_control_text(7, Some(1.5), None), "seq=7\nspeed=1.5000\njump=earned\n");
        assert_eq!(live_control_text(8, None, Some(2.25)), "seq=8\nspeed=earned\njump=2.2500\n");
    }

    #[test]
    fn shared_jump_vectors() {
        let text = include_str!("../../../tests/jump_vectors.txt");
        let (mut earned_n, mut live_n) = (0, 0);
        for line in text.lines().filter(|l| !l.is_empty() && !l.starts_with('#')) {
            let f: Vec<&str> = line.split_whitespace().collect();
            if f[0] == "earned" {
                let mut s = Settings::default();
                s.moon_speed.start = f[1].parse().unwrap();
                s.moon_speed.per_moon = f[2].parse().unwrap();
                s.moon_speed.max = f[3].parse().unwrap();
                s.moon_speed.curve = match f[4] { "front" => Curve::FrontLoaded, "back" => Curve::BackLoaded, _ => Curve::Linear };
                let moons: i32 = f[5].parse().unwrap();
                s.jump = Jump { enabled: true, height: f[6].parse().unwrap(), scale: f[7] == "1" };
                let want: f32 = f[8].parse().unwrap();
                let got = earned_jump(&s, moons);
                assert!(close(got, want), "{line} -> {got}");
                earned_n += 1;
            } else {
                let earned: f32 = f[1].parse().unwrap();
                let req: f32 = f[3].parse().unwrap();
                let want: f32 = f[4].parse().unwrap();
                let got = live_jump(earned, if f[2] == "manual" { Some(req) } else { None });
                assert!(close(got, want), "{line} -> {got}");
                live_n += 1;
            }
        }
        assert!(earned_n >= 8 && live_n >= 5);
        let off = Settings::default();
        assert!(close(earned_jump(&off, 100), 1.0), "jump off = 1x");
        assert!(close(live_jump(2.5, Some(f32::NAN)), 2.5));
    }

    #[test]
    fn shared_curve_vectors() {
        let text = include_str!("../../../tests/curve_vectors.txt");
        let mut n = 0;
        for line in text.lines().filter(|l| !l.is_empty() && !l.starts_with('#')) {
            let f: Vec<&str> = line.split_whitespace().collect();
            let mut m = Settings::default().moon_speed;
            m.start = f[0].parse().unwrap();
            m.per_moon = f[1].parse().unwrap();
            m.max = f[2].parse().unwrap();
            m.curve = match f[3] {
                "front" => Curve::FrontLoaded,
                "back" => Curve::BackLoaded,
                _ => Curve::Linear,
            };
            let moons: i32 = f[4].parse().unwrap();
            let want: f32 = f[5].parse().unwrap();
            let got = m.multiplier(moons);
            assert!(close(got, want), "{line} -> {got}");
            n += 1;
        }
        assert!(n >= 10);
    }

    #[test]
    fn default_ini_matches_the_file_the_game_is_tested_with() {
        let fixture = include_str!("../../../tests/default_settings.ini").replace("\r\n", "\n");
        assert_eq!(to_ini(&Settings::default()), fixture);
    }

    #[test]
    fn ini_has_every_key() {
        let ini = to_ini(&Settings::default());
        for key in [
            "moon_speed.enabled=1", "moon_speed.count=total", "moon_speed.start=0.7500",
            "moon_speed.per_moon=0.00250", "moon_speed.max=2.0000", "moon_speed.curve=linear",
            "moon_speed.walk=1", "moon_speed.swim=1", "captures.enabled=1", "captures.off=\n",
            "cappy.enabled=0",
        ] {
            assert!(ini.contains(key), "missing {key}");
        }
    }
}
