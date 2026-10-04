// Moonrush settings: parsing settings.ini and the Moon speed curve.
// Pure C++ with no Switch dependencies, so tests/host_test.cpp can check it on Windows.
// The curve MUST match launcher/src-tauri/src/settings.rs (both are checked against tests/curve_vectors.txt).
#pragma once

#include <cstddef>
#include <cstring>

namespace moonrush {

    enum class Curve { Linear, FrontLoaded, BackLoaded };

    enum Group { Walk, Squat, Dive, Roll, LongJump, Air, Swim, GroupCount };

    // First Person button. Never the right-stick click: SMO uses it for its own look-around view.
    enum class FpButton { LeftStick, DpadUp, DpadDown, DpadLeft, DpadRight };

    inline constexpr const char* kGroupKeys[GroupCount] = {
        "walk", "squat", "dive", "roll", "long_jump", "air", "swim",
    };

    struct Settings {
        bool found = false;       // settings.ini was read
        bool enabled = false;     // no file => stay vanilla
        bool countTotal = true;   // total save-file Moons vs Moons held now
        float start = 0.75f;
        float perMoon = 0.0025f;
        float max = 2.0f;
        Curve curve = Curve::Linear;
        bool groups[GroupCount] = { true, true, true, true, true, true, true };

        // Captures module: whatever Mario has captured moves sideways at the same multiplier.
        bool captures = true;
        // Captures to leave vanilla, by the game's internal capture name ("Kuribo", "Frog", ...).
        static constexpr int kMaxCapturesOff = 48;
        static constexpr int kNameLen = 32;
        char capturesOff[kMaxCapturesOff][kNameLen] = {};
        int capturesOffCount = 0;

        // Cappy: throw speed and reach follow the multiplier. Off unless turned on.
        bool cappy = false;

        // Jump Height (v0.4.0): `jumpHeight` is how many times higher Mario jumps. Only the launch power is
        // changed (by sqrt(height)); gravity is never touched, so the arc keeps vanilla gravity.
        bool jumpEnabled = false;
        float jumpHeight = 1.0f;

        // Moon Animation Speed (v0.6): the ordinary Moon-get demo's own updates are repeated, so it plays
        // `moonAnimSpeed` times faster. Independent of Moon Speed and Jump Height.
        bool moonAnimEnabled = false;
        float moonAnimSpeed = 2.0f;

        // First Person: camera in Mario's head, Mario hidden. Camera only, no gameplay change.
        bool firstPerson = false;
        FpButton fpButton = FpButton::LeftStick;  // tap = switch view; hold = peek (if fpPeek)
        bool fpPeek = true;
        bool fpOffCutscenes = true;               // cutscenes and cameras the stick can't turn
        bool fpOff2D = true;                      // 8-bit 2D sections
        bool fpOffCaptures = true;                // otherwise: view from the captured object

        bool isCaptureOff(const char* name) const {
            if (name == nullptr) return false;
            for (int i = 0; i < capturesOffCount; i++)
                if (strcmp(capturesOff[i], name) == 0) return true;
            return false;
        }
    };

    inline constexpr float kStartMin = 0.25f;
    inline constexpr float kStartMax = 2.0f;
    inline constexpr float kPerMoonMax = 0.05f;
    inline constexpr float kMaxCeiling = 10.0f;
    inline constexpr float kJumpHeightMin = 1.0f;
    inline constexpr float kJumpHeightMax = 5.0f;
    inline constexpr float kMoonAnimMin = 1.0f;
    inline constexpr float kMoonAnimMax = 5.0f;

    namespace detail {
        inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
        inline bool isFinite(float v) { return v == v && v - v == 0.0f; }

        inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

        // Minimal decimal parser ("1.25", "-0.5", "3"). Returns false on anything else.
        inline bool parseFloat(const char* s, size_t n, float* out) {
            size_t i = 0;
            bool neg = false;
            if (i < n && (s[i] == '-' || s[i] == '+')) neg = s[i++] == '-';
            double v = 0, scale = 1;
            bool digits = false, dot = false;
            for (; i < n; i++) {
                char c = s[i];
                if (c >= '0' && c <= '9') {
                    digits = true;
                    if (dot) { scale /= 10; v += (c - '0') * scale; }
                    else v = v * 10 + (c - '0');
                } else if (c == '.' && !dot) {
                    dot = true;
                } else {
                    return false;
                }
            }
            if (!digits) return false;
            *out = static_cast<float>(neg ? -v : v);
            return true;
        }

        inline bool eq(const char* s, size_t n, const char* lit) {
            return strlen(lit) == n && memcmp(s, lit, n) == 0;
        }

        inline bool isNameChar(char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        }

        // "Kuribo, Frog,TRex" -> list. Names with other characters, or too long, are skipped.
        inline void parseNameList(Settings& s, const char* v, size_t n) {
            s.capturesOffCount = 0;
            size_t i = 0;
            while (i < n) {
                while (i < n && (v[i] == ',' || isSpace(v[i]))) i++;
                size_t a = i;
                while (i < n && v[i] != ',') i++;
                size_t b = i;
                while (b > a && isSpace(v[b - 1])) b--;
                size_t len = b - a;
                if (len == 0 || len >= static_cast<size_t>(Settings::kNameLen)) continue;
                bool ok = true;
                for (size_t k = a; k < b; k++) ok &= isNameChar(v[k]);
                if (!ok || s.capturesOffCount >= Settings::kMaxCapturesOff) continue;
                memcpy(s.capturesOff[s.capturesOffCount], v + a, len);
                s.capturesOff[s.capturesOffCount][len] = 0;
                s.capturesOffCount++;
            }
        }
    }

    // Clamp into the safe range. Mirrors Settings::sanitized() in the launcher.
    inline void sanitize(Settings& s) {
        using namespace detail;
        Settings d;
        s.start = clampf(isFinite(s.start) ? s.start : d.start, kStartMin, kStartMax);
        s.perMoon = clampf(isFinite(s.perMoon) ? s.perMoon : d.perMoon, 0.0f, kPerMoonMax);
        s.max = clampf(isFinite(s.max) ? s.max : d.max, s.start, kMaxCeiling);
        s.jumpHeight = clampf(isFinite(s.jumpHeight) ? s.jumpHeight : d.jumpHeight, kJumpHeightMin, kJumpHeightMax);
        s.moonAnimSpeed = clampf(isFinite(s.moonAnimSpeed) ? s.moonAnimSpeed : d.moonAnimSpeed, kMoonAnimMin, kMoonAnimMax);
    }

    // Parse settings.ini. Unknown keys are ignored; bad values keep their defaults.
    inline Settings parseSettings(const char* text, size_t len) {
        using namespace detail;
        Settings s;
        s.found = true;
        size_t i = 0;
        while (i < len) {
            size_t end = i;
            while (end < len && text[end] != '\n') end++;
            size_t a = i, b = end;
            i = end + 1;
            while (a < b && isSpace(text[a])) a++;
            while (b > a && isSpace(text[b - 1])) b--;
            if (a == b || text[a] == ';' || text[a] == '#') continue;
            size_t eqPos = a;
            while (eqPos < b && text[eqPos] != '=') eqPos++;
            if (eqPos == b) continue;
            size_t kb = eqPos;
            while (kb > a && isSpace(text[kb - 1])) kb--;
            size_t va = eqPos + 1;
            while (va < b && isSpace(text[va])) va++;
            const char* key = text + a;
            size_t kn = kb - a;
            const char* val = text + va;
            size_t vn = b - va;

            bool flag = eq(val, vn, "1");
            if (eq(key, kn, "captures.enabled")) { s.captures = flag; continue; }
            if (eq(key, kn, "captures.off")) { parseNameList(s, val, vn); continue; }
            if (eq(key, kn, "cappy.enabled")) { s.cappy = flag; continue; }
            if (eq(key, kn, "jump.enabled")) { s.jumpEnabled = flag; continue; }
            if (eq(key, kn, "jump.height")) { float jf; if (parseFloat(val, vn, &jf)) s.jumpHeight = jf; continue; }
            if (eq(key, kn, "moon_anim.enabled")) { s.moonAnimEnabled = flag; continue; }
            if (eq(key, kn, "moon_anim.speed")) { float mf; if (parseFloat(val, vn, &mf)) s.moonAnimSpeed = mf; continue; }
            if (eq(key, kn, "first_person.enabled")) { s.firstPerson = flag; continue; }
            if (eq(key, kn, "first_person.peek")) { s.fpPeek = flag; continue; }
            if (eq(key, kn, "first_person.off_cutscenes")) { s.fpOffCutscenes = flag; continue; }
            if (eq(key, kn, "first_person.off_2d")) { s.fpOff2D = flag; continue; }
            if (eq(key, kn, "first_person.off_captures")) { s.fpOffCaptures = flag; continue; }
            if (eq(key, kn, "first_person.button")) {
                s.fpButton = eq(val, vn, "dpad_up") ? FpButton::DpadUp
                           : eq(val, vn, "dpad_down") ? FpButton::DpadDown
                           : eq(val, vn, "dpad_left") ? FpButton::DpadLeft
                           : eq(val, vn, "dpad_right") ? FpButton::DpadRight
                           : FpButton::LeftStick;
                continue;
            }

            static constexpr char kPrefix[] = "moon_speed.";
            constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
            if (kn <= kPrefixLen || memcmp(key, kPrefix, kPrefixLen) != 0) continue;
            key += kPrefixLen;
            kn -= kPrefixLen;

            float f;
            if (eq(key, kn, "enabled")) s.enabled = flag;
            else if (eq(key, kn, "count")) s.countTotal = !eq(val, vn, "current");
            else if (eq(key, kn, "start")) { if (parseFloat(val, vn, &f)) s.start = f; }
            else if (eq(key, kn, "per_moon")) { if (parseFloat(val, vn, &f)) s.perMoon = f; }
            else if (eq(key, kn, "max")) { if (parseFloat(val, vn, &f)) s.max = f; }
            else if (eq(key, kn, "curve")) {
                s.curve = eq(val, vn, "front") ? Curve::FrontLoaded
                        : eq(val, vn, "back") ? Curve::BackLoaded
                        : Curve::Linear;
            } else {
                for (int g = 0; g < GroupCount; g++)
                    if (eq(key, kn, kGroupKeys[g])) s.groups[g] = flag;
            }
        }
        sanitize(s);
        return s;
    }

    // Launch-power factor that gives `height` times the jump height at unchanged gravity (apex = v^2 / 2g).
    inline float jumpLaunchFactor(const Settings& s) {
        if (!s.jumpEnabled) return 1.0f;
        float x = detail::clampf(s.jumpHeight, kJumpHeightMin, kJumpHeightMax);
        float r = x;  // Newton's sqrt without <cmath> (keeps this header dependency-free)
        for (int i = 0; i < 12; i++) r = 0.5f * (r + x / r);
        return r;
    }

    // Moon Animation Speed: how many EXTRA demo updates to run this frame so the demo plays `speed` times faster.
    // `carry` holds the fraction left over, so 1.5x alternates 0 and 1 extra updates (1.5x on average).
    inline int moonAnimExtraUpdates(bool enabled, float speed, float* carry) {
        if (!enabled) { *carry = 0.0f; return 0; }
        float s = detail::clampf(detail::isFinite(speed) ? speed : 1.0f, kMoonAnimMin, kMoonAnimMax);
        float acc = *carry + (s - 1.0f);
        int n = static_cast<int>(acc);
        *carry = acc - static_cast<float>(n);
        return n;
    }

    // Speed multiplier for a Moon count. Same formula as MoonSpeed::multiplier in the launcher.
    inline float multiplier(const Settings& s, int moons) {
        float span = s.max - s.start;
        if (span <= 0.0f || s.perMoon <= 0.0f) return s.start;
        float toMax = span / s.perMoon;
        float t = detail::clampf(static_cast<float>(moons < 0 ? 0 : moons) / toMax, 0.0f, 1.0f);
        float shaped = t;
        if (s.curve == Curve::FrontLoaded) shaped = 1.0f - (1.0f - t) * (1.0f - t);
        else if (s.curve == Curve::BackLoaded) shaped = t * t;
        return s.start + span * shaped;
    }

    // ---- Live speed control (launcher "Moon & Speed" panel) ---------------------------------------------
    // The launcher may ask the RUNNING game for a manual speed. The game, not the launcher, enforces the limit:
    // a manual speed can never exceed the speed earned at the current Moon count (curve, start, per Moon, max),
    // and never goes below kLiveMin. Same rule as live_speed() in the launcher (tests/live_vectors.txt).
    inline constexpr float kLiveMin = 0.25f;

    // `manual` false => the earned speed. Non-finite requests fall back to the earned speed.
    inline float liveSpeed(float earned, bool manual, float requested) {
        if (!manual || !detail::isFinite(requested)) return earned;
        float lo = kLiveMin < earned ? kLiveMin : earned;
        return detail::clampf(requested, lo, earned);
    }

    struct LiveControl {
        bool found = false;
        unsigned long long seq = 0;  // launcher-written, strictly increasing; a new value = a new request
        bool earned = true;          // "speed=earned": return to the earned speed
        float speed = 0.0f;          // otherwise the requested manual speed
    };

    // Parses live.ini ("seq=<n>" and "speed=earned|<x>"). Unknown or malformed lines are ignored.
    inline LiveControl parseLiveControl(const char* text, size_t n) {
        using namespace detail;
        LiveControl c;
        size_t i = 0;
        while (i < n) {
            size_t end = i;
            while (end < n && text[end] != '\n') end++;
            size_t a = i, b = end;
            i = end + 1;
            while (a < b && isSpace(text[a])) a++;
            while (b > a && isSpace(text[b - 1])) b--;
            size_t eqPos = a;
            while (eqPos < b && text[eqPos] != '=') eqPos++;
            if (eqPos == b) continue;
            const char* key = text + a;
            size_t kn = eqPos - a;
            const char* val = text + eqPos + 1;
            size_t vn = b - (eqPos + 1);
            if (eq(key, kn, "seq")) {
                unsigned long long v = 0;
                bool digits = vn > 0 && vn < 20;
                for (size_t k = 0; k < vn && digits; k++) {
                    if (val[k] < '0' || val[k] > '9') digits = false;
                    else v = v * 10 + static_cast<unsigned>(val[k] - '0');
                }
                if (digits) { c.seq = v; c.found = true; }
            } else if (eq(key, kn, "speed")) {
                float f;
                if (eq(val, vn, "earned")) c.earned = true;
                else if (parseFloat(val, vn, &f)) { c.earned = false; c.speed = f; }
            }
        }
        return c;
    }

}  // namespace moonrush
