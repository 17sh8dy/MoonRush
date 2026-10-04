//! Live channel with the running game: reading status.ini (written by the game twice a second) and
//! writing live.ini (a request the game polls). Both live on Ryujinx's emulated SD card next to settings.ini.
//!
//! The launcher only ASKS. The game module caps every manual speed at the speed earned from the Moon count, so a
//! stale or hand-edited live.ini can never push Mario past what the progression has unlocked.

use serde::Serialize;

/// What the game reported in status.ini.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct GameStatus {
    pub beat: u64,
    pub moons: i32,
    pub earned: f32,
    pub applied: f32,
    pub manual: bool,
    pub min: f32,
    pub max: f32,
    pub enabled: bool,
}

/// Parse status.ini. None unless every field the panel needs is present and sane.
pub fn parse_status(text: &str) -> Option<GameStatus> {
    let mut s = GameStatus::default();
    let mut seen = 0u32;
    for line in text.lines() {
        let Some((k, v)) = line.split_once('=') else { continue };
        let (k, v) = (k.trim(), v.trim());
        match k {
            "beat" => { s.beat = v.parse().ok()?; seen |= 1; }
            "moons" => { s.moons = v.parse().ok()?; seen |= 2; }
            "earned" => { s.earned = v.parse().ok()?; seen |= 4; }
            "applied" => { s.applied = v.parse().ok()?; seen |= 8; }
            "mode" => { s.manual = v == "manual"; seen |= 16; }
            "min" => { s.min = v.parse().ok()?; seen |= 32; }
            "max" => { s.max = v.parse().ok()?; seen |= 64; }
            "enabled" => { s.enabled = v == "1"; seen |= 128; }
            _ => {}
        }
    }
    let finite = s.earned.is_finite() && s.applied.is_finite() && s.min.is_finite() && s.max.is_finite();
    (seen == 255 && finite).then_some(s)
}

/// What the panel shows. `connected: false` means every live control must be disabled.
#[derive(Serialize, Default)]
pub struct LiveStatus {
    pub connected: bool,
    /// Why not connected (shown under the panel title).
    pub reason: String,
    pub moons: i32,
    /// Earned speed as the GAME computed it (what it enforces).
    pub earned: f32,
    /// The same, recomputed here from the saved settings with the Rust formula.
    pub earned_launcher: f32,
    /// False if those two differ: settings were changed after the game started.
    pub settings_match: bool,
    pub applied: f32,
    pub manual: bool,
    pub min: f32,
    pub max: f32,
    pub moon_speed_enabled: bool,
}

#[cfg(test)]
mod tests {
    use super::*;

    const SAMPLE: &str = "v=1\nbeat=12\nmoons=10\nearned=2.2000\napplied=1.5000\nmode=manual\nmin=0.2500\nmax=5.0000\nenabled=1\n      \n";

    #[test]
    fn parses_what_the_game_writes() {
        let s = parse_status(SAMPLE).unwrap();
        assert_eq!((s.beat, s.moons, s.manual, s.enabled), (12, 10, true, true));
        assert!((s.earned - 2.2).abs() < 1e-6 && (s.applied - 1.5).abs() < 1e-6);
    }

    #[test]
    fn rejects_partial_or_garbled_files() {
        assert!(parse_status("").is_none());
        assert!(parse_status("beat=1\nmoons=2\n").is_none());
        assert!(parse_status(&SAMPLE.replace("earned=2.2000", "earned=nan")).is_none());
        assert!(parse_status(&SAMPLE.replace("moons=10", "moons=abc")).is_none());
    }
}
