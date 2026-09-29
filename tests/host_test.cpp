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
