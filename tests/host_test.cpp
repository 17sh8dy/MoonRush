// Host-side test of the game's settings parser and speed curve (compiled with MSVC on Windows).
// Checks the same vectors as the launcher, and parses the exact INI the launcher writes.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include "../game/source/moonrush/settings.hpp"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::string readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : ".";
    std::string vectors = readFile((std::string(dir) + "/curve_vectors.txt").c_str());
    std::istringstream in(vectors);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        float start, per, max, expected; std::string curve; int moons;
        ls >> start >> per >> max >> curve >> moons >> expected;
        moonrush::Settings s;
        s.start = start; s.perMoon = per; s.max = max;
        s.curve = curve == "front" ? moonrush::Curve::FrontLoaded : curve == "back" ? moonrush::Curve::BackLoaded : moonrush::Curve::Linear;
        float got = moonrush::multiplier(s, moons);
        CHECK(std::fabs(got - expected) < 1e-4f, "%s -> %f, expected %f", line.c_str(), got, expected);
        n++;
    }
    CHECK(n >= 10, "only %d vectors", n);

    // Earned jump + live jump cap: the same vectors the launcher checks.
    {
        std::string jv = readFile((std::string(dir) + "/jump_vectors.txt").c_str());
        std::istringstream jin(jv);
        std::string jl;
        int en = 0, ln2 = 0;
        while (std::getline(jin, jl)) {
            if (jl.empty() || jl[0] == '#') continue;
            std::istringstream ls(jl);
            std::string kind; ls >> kind;
            if (kind == "earned") {
                float start, per, max, jh, expected; std::string curve; int moons, scale;
                ls >> start >> per >> max >> curve >> moons >> jh >> scale >> expected;
                moonrush::Settings s;
                s.start = start; s.perMoon = per; s.max = max;
                s.curve = curve == "front" ? moonrush::Curve::FrontLoaded : curve == "back" ? moonrush::Curve::BackLoaded : moonrush::Curve::Linear;
                s.jumpEnabled = true; s.jumpHeight = jh; s.jumpScale = scale != 0;
                float got = moonrush::earnedJump(s, moons);
                CHECK(std::fabs(got - expected) < 1e-4f, "earned jump %s -> %f", jl.c_str(), got);
                en++;
            } else {
                float earned, req, expected; std::string mode;
                ls >> earned >> mode >> req >> expected;
                float got = moonrush::liveJump(earned, mode == "manual", req);
                CHECK(std::fabs(got - expected) < 1e-4f, "live jump %s -> %f", jl.c_str(), got);
                ln2++;
            }
        }
        CHECK(en >= 8 && ln2 >= 5, "jump vectors: %d earned, %d live", en, ln2);
        moonrush::Settings off;
        CHECK(std::fabs(moonrush::earnedJump(off, 100) - 1.0f) < 1e-6f, "jump off = 1x");
        CHECK(std::fabs(moonrush::liveJump(2.5f, true, std::nanf("")) - 2.5f) < 1e-6f, "NaN jump request -> earned");
        const char* sc = "jump.enabled=1\njump.height=3\njump.scale=0\n";
        auto jsx = moonrush::parseSettings(sc, std::strlen(sc));
        CHECK(jsx.jumpEnabled && !jsx.jumpScale, "jump.scale parsed");
        const char* dflt = "jump.enabled=1\n";
        CHECK(moonrush::parseSettings(dflt, std::strlen(dflt)).jumpScale, "jump.scale defaults on");
    }

    // Moon Animation Speed: extra demo updates per frame (fractional speeds carry over; off or NaN = none).
    {
        float carry = 0;
        CHECK(moonrush::moonAnimExtraUpdates(false, 3.0f, &carry) == 0, "off -> 0");
        carry = 0; int t2 = 0; for (int i = 0; i < 10; i++) t2 += moonrush::moonAnimExtraUpdates(true, 2.0f, &carry);
        CHECK(t2 == 10, "2x = 1 extra per frame (%d)", t2);
        carry = 0; int t15 = 0; for (int i = 0; i < 10; i++) t15 += moonrush::moonAnimExtraUpdates(true, 1.5f, &carry);
        CHECK(t15 == 5, "1.5x = 5 extra in 10 frames (%d)", t15);
        carry = 0; CHECK(moonrush::moonAnimExtraUpdates(true, 9.0f, &carry) == 4, "capped at 5x = 4 extra");
        carry = 0; CHECK(moonrush::moonAnimExtraUpdates(true, 1.0f, &carry) == 0, "1x = none");
        carry = 0; CHECK(moonrush::moonAnimExtraUpdates(true, std::nanf(""), &carry) == 0, "NaN = none");
        const char* on = "moon_anim.enabled=1\nmoon_anim.speed=9\n";
        auto ma = moonrush::parseSettings(on, std::strlen(on));
        CHECK(ma.moonAnimEnabled && std::fabs(ma.moonAnimSpeed - 5.0f) < 1e-6f, "parsed + clamped to 5x");
        const char* lo = "moon_anim.speed=0.1\n";
        auto md = moonrush::parseSettings(lo, std::strlen(lo));
        CHECK(!md.moonAnimEnabled && std::fabs(md.moonAnimSpeed - 1.0f) < 1e-6f, "default off, clamped to 1x");
    }

    // Live speed: the same vectors the launcher checks. A manual speed never exceeds the earned speed.
    {
        std::string lv = readFile((std::string(dir) + "/live_vectors.txt").c_str());
        std::istringstream lin(lv);
        std::string l2;
        int ln = 0;
        while (std::getline(lin, l2)) {
            if (l2.empty() || l2[0] == '#') continue;
            std::istringstream ls(l2);
            float earned, requested, expected; std::string mode;
            ls >> earned >> mode >> requested >> expected;
            float got = moonrush::liveSpeed(earned, mode == "manual", requested);
            CHECK(std::fabs(got - expected) < 1e-4f, "live %s -> %f, expected %f", l2.c_str(), got, expected);
            ln++;
        }
        CHECK(ln >= 10, "only %d live vectors", ln);
        CHECK(std::fabs(moonrush::liveSpeed(2.2f, true, std::nanf("")) - 2.2f) < 1e-6f, "NaN request -> earned");
        CHECK(std::fabs(moonrush::liveSpeed(2.2f, true, INFINITY) - 2.2f) < 1e-6f, "inf request -> earned");

        // The file the launcher writes (see live_control_text in settings.rs) parses back.
        const char* req = "seq=1790000000123\nspeed=1.5000\njump=2.2500\n";
        auto c = moonrush::parseLiveControl(req, std::strlen(req));
        CHECK(c.found && c.seq == 1790000000123ULL && c.speedFound && !c.earned && std::fabs(c.speed - 1.5f) < 1e-6f, "manual request");
        CHECK(c.jumpFound && !c.jumpEarned && std::fabs(c.jump - 2.25f) < 1e-6f, "manual jump request");
        const char* ret = "seq=1790000000456\r\nspeed=earned\r\n";
        c = moonrush::parseLiveControl(ret, std::strlen(ret));
        CHECK(c.found && c.seq == 1790000000456ULL && c.earned, "return-to-earned request (CRLF ok)");
        const char* junk = "seq=abc\nspeed=\n=\nfoo\n";
        CHECK(!moonrush::parseLiveControl(junk, std::strlen(junk)).found, "junk is ignored");
    }

    // The launcher's default INI, byte for byte (pinned by the launcher's own test).
    std::string ini = readFile((std::string(dir) + "/default_settings.ini").c_str());
    auto s = moonrush::parseSettings(ini.data(), ini.size());
    CHECK(s.found && s.enabled && s.countTotal, "flags");
    CHECK(std::fabs(s.start - 0.75f) < 1e-6f && std::fabs(s.perMoon - 0.0025f) < 1e-6f && std::fabs(s.max - 2.0f) < 1e-6f, "numbers");
    CHECK(s.curve == moonrush::Curve::Linear, "curve");
    for (int g = 0; g < moonrush::GroupCount; g++) CHECK(s.groups[g], "group %d", g);
    CHECK(s.captures && !s.cappy && s.capturesOffCount == 0, "captures on, cappy off, nothing switched off");

    // Capture switches: spaces, CRLF, empty entries and invalid names.
    const char* caps = "captures.enabled=1\ncaptures.off= Kuribo, TRex ,bad name,,Frog\r\ncappy.enabled=1\n";
    auto cs = moonrush::parseSettings(caps, std::strlen(caps));
    CHECK(cs.capturesOffCount == 3, "3 valid names (%d)", cs.capturesOffCount);
    CHECK(cs.isCaptureOff("Kuribo") && cs.isCaptureOff("TRex") && cs.isCaptureOff("Frog"), "names parsed");
    CHECK(!cs.isCaptureOff("Killer") && !cs.isCaptureOff(nullptr), "others stay on");
    CHECK(cs.cappy, "cappy=1");
    auto capsOff = moonrush::parseSettings("captures.enabled=0\n", 19);
    CHECK(!capsOff.captures, "captures.enabled=0");
    // Settings written before v0.3.0 have no captures keys: Captures defaults on, Cappy off.
    auto old = moonrush::parseSettings("moon_speed.enabled=1\n", 21);
    CHECK(old.captures && !old.cappy, "old file defaults");
    CHECK(!old.firstPerson && old.fpPeek && old.fpOffCutscenes && old.fpOff2D && old.fpOffCaptures &&
          old.fpButton == moonrush::FpButton::LeftStick, "first person defaults");

    // First Person keys.
    const char* fp = "first_person.enabled=1\nfirst_person.button=dpad_down\nfirst_person.peek=0\n"
                     "first_person.off_cutscenes=0\nfirst_person.off_2d=1\nfirst_person.off_captures=0\n";
    auto f = moonrush::parseSettings(fp, std::strlen(fp));
    CHECK(f.firstPerson && !f.fpPeek && !f.fpOffCutscenes && f.fpOff2D && !f.fpOffCaptures, "fp flags");
    CHECK(f.fpButton == moonrush::FpButton::DpadDown, "fp button");
    auto rs = moonrush::parseSettings("first_person.button=rstick\n", 27);
    CHECK(rs.fpButton == moonrush::FpButton::LeftStick, "right stick is never used (falls back to left)");

    // Jump Height: default off at 1x; clamped to 1..5x; launch factor = sqrt(height) (gravity is never touched).
    CHECK(!s.jumpEnabled && std::fabs(s.jumpHeight - 1.0f) < 1e-6f && std::fabs(moonrush::jumpLaunchFactor(s) - 1.0f) < 1e-6f, "jump default");
    const char* jmp = "jump.enabled=1\njump.height=4\n";
    auto j = moonrush::parseSettings(jmp, std::strlen(jmp));
    CHECK(j.jumpEnabled && std::fabs(moonrush::jumpLaunchFactor(j) - 2.0f) < 1e-4f, "4x height = 2x launch (%f)", moonrush::jumpLaunchFactor(j));
    const char* jmp2 = "jump.enabled=1\njump.height=2.5\n";
    auto j2 = moonrush::parseSettings(jmp2, std::strlen(jmp2));
    CHECK(std::fabs(moonrush::jumpLaunchFactor(j2) - std::sqrt(2.5f)) < 1e-4f, "2.5x (%f)", moonrush::jumpLaunchFactor(j2));
    const char* jmp3 = "jump.enabled=1\njump.height=99\n";
    auto j3 = moonrush::parseSettings(jmp3, std::strlen(jmp3));
    CHECK(std::fabs(j3.jumpHeight - 10.0f) < 1e-6f, "height clamped to 10x (%f)", j3.jumpHeight);
    const char* jmp4 = "jump.enabled=0\njump.height=3\n";
    auto j4 = moonrush::parseSettings(jmp4, std::strlen(jmp4));
    CHECK(std::fabs(moonrush::jumpLaunchFactor(j4) - 1.0f) < 1e-6f, "disabled = vanilla");

    // Toggles, curve names, current count, CRLF, junk, and unsafe values.
    const char* custom =
        "; comment\r\n"
        "moon_speed.enabled=1\r\n"
        "moon_speed.count=current\r\n"
        "moon_speed.curve=back\r\n"
        "moon_speed.swim=0\r\n"
        "moon_speed.dive = 0 \r\n"
        "moon_speed.start=abc\r\n"
        "moon_speed.max=50\r\n"
        "garbage line\r\n"
        "other.key=1\r\n";
    auto c = moonrush::parseSettings(custom, std::strlen(custom));
    CHECK(!c.countTotal, "count=current");
    CHECK(c.curve == moonrush::Curve::BackLoaded, "curve=back");
    CHECK(!c.groups[moonrush::Swim] && !c.groups[moonrush::Dive] && c.groups[moonrush::Walk], "group toggles");
    CHECK(std::fabs(c.start - 0.75f) < 1e-6f, "bad start keeps default (%f)", c.start);
    CHECK(std::fabs(c.max - moonrush::kMaxCeiling) < 1e-6f, "max clamped (%f)", c.max);

    // Disabled / empty => vanilla.
    auto off = moonrush::parseSettings("moon_speed.enabled=0\n", 21);
    CHECK(!off.enabled, "enabled=0");
    auto empty = moonrush::parseSettings("", 0);
    CHECK(!empty.enabled, "empty file stays vanilla");

    std::printf("%s: %d curve vectors, %d failure(s)\n", failures ? "FAILED" : "OK", n, failures);
    return failures ? 1 : 0;
}
