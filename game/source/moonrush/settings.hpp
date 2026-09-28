// Moonrush settings: parsing settings.ini and the Moon speed curve.
// Pure C++ with no Switch dependencies, so tests/host_test.cpp can check it on Windows.
// The curve MUST match launcher/src-tauri/src/settings.rs (both are checked against tests/curve_vectors.txt).
#pragma once

#include <cstddef>
#include <cstring>

namespace moonrush {

    enum class Curve { Linear, FrontLoaded, BackLoaded };

    enum Group { Walk, Squat, Dive, Roll, LongJump, Air, Swim, GroupCount };

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
    };

    inline constexpr float kStartMin = 0.25f;
    inline constexpr float kStartMax = 2.0f;
    inline constexpr float kPerMoonMax = 0.05f;
    inline constexpr float kMaxCeiling = 3.0f;

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
    }

    // Clamp into the safe range. Mirrors Settings::sanitized() in the launcher.
    inline void sanitize(Settings& s) {
        using namespace detail;
        Settings d;
        s.start = clampf(isFinite(s.start) ? s.start : d.start, kStartMin, kStartMax);
        s.perMoon = clampf(isFinite(s.perMoon) ? s.perMoon : d.perMoon, 0.0f, kPerMoonMax);
        s.max = clampf(isFinite(s.max) ? s.max : d.max, s.start, kMaxCeiling);
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

            static constexpr char kPrefix[] = "moon_speed.";
            constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
            if (kn <= kPrefixLen || memcmp(key, kPrefix, kPrefixLen) != 0) continue;
            key += kPrefixLen;
            kn -= kPrefixLen;

            bool flag = eq(val, vn, "1");
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

}  // namespace moonrush
