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
    pub height: f32,
}

impl Default for Jump {
    fn default() -> Self {
        Jump { enabled: false, height: 1.0 }
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
}

/// Capture names are the game's internal ids ("Kuribo", "TRex"): letters, digits, underscore.
pub fn is_capture_name(n: &str) -> bool {
    !n.is_empty() && n.len() < 32 && n.chars().all(|c| c.is_ascii_alphanumeric() || c == '_')
}

pub const START_MIN: f32 = 0.25;
pub const START_MAX: f32 = 2.0;
pub const PER_MOON_MAX: f32 = 0.05;
/// Hard ceiling. Above this Mario starts passing through thin walls, because collision is checked per frame.
pub const MAX_CEILING: f32 = 3.0;
pub const JUMP_HEIGHT_MIN: f32 = 1.0;
/// Above 4x Mario clears most level geometry and skips whole sections.
pub const JUMP_HEIGHT_MAX: f32 = 4.0;

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
        let c = &mut self.captures;
        c.off.retain(|n| is_capture_name(n));
        c.off.sort();
        c.off.dedup();
        c.off.truncate(48);
        self
    }
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

    pub fn multiplier(&self, moons: i32) -> f32 {
        let Some(to_max) = self.moons_to_max() else { return self.start };
        let t = (moons.max(0) as f32 / to_max).clamp(0.0, 1.0);
        let shaped = match self.curve {
            Curve::Linear => t,
            Curve::FrontLoaded => 1.0 - (1.0 - t) * (1.0 - t),
            Curve::BackLoaded => t * t,
        };
        self.start + (self.max - self.start) * shaped
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
        b(s.jump.enabled), s.jump.height,
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
        assert_eq!(s.jump, Jump { enabled: false, height: 1.0 });
        s.jump.enabled = true;
        s.jump.height = 9.0;
        let ini = to_ini(&s.clone().sanitized());
        assert!(ini.contains("jump.enabled=1\n") && ini.contains("jump.height=4.000\n"));
        s.jump.height = f32::NAN;
        assert!(close(s.clone().sanitized().jump.height, 1.0));
        s.jump.height = 0.2;
        assert!(close(s.sanitized().jump.height, JUMP_HEIGHT_MIN));
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
