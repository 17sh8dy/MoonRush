// Moonrush game module (exlaunch, subsdk8).
//
// Moon Speed: Mario's speed values are read through small PlayerConst getters
// (`ldr s0, [x0, #field]; ret`). Moonrush hooks those getters and returns
// `whatever the getter would have returned * Moon multiplier`, but only for the playable Mario's
// PlayerConst. Nothing in memory or on disk is overwritten.
//
// Stacking: if another mod (e.g. the "Emulator build" speed mod) hooked a getter first, Moonrush's hook chains on
// top of it, so the result is `other mod's value * Moon multiplier`.
//
// Captures: while Mario is in a capture, the captured object's sideways movement is multiplied
// before it reaches the collision code (al::Collider / PlayerCollider ::collide), so walls are still
// checked over the full distance. The vertical part (jumps, falling) is untouched, and so is the
// object's own velocity, so nothing compounds frame to frame.
//
// Cappy (optional, off by default): Cappy's throw speed and reach params are multiplied in place.
// The original values are remembered and put back as soon as the toggle is off.
//
// First Person (optional, off by default): after the game has posed its normal camera, the eye is moved
// into Mario's head, looking the same way, so the right stick and movement work exactly as usual.
// Mario is hidden only while his visibility flags are applied to his model; the game's own flags
// are never changed. Camera only: nothing about gameplay changes.
//
// All offsets come from tools/gen_offsets.py (SMO 1.0.0 only) and are checked at startup.

#include <cstdio>
#include <utility>

#include "lib.hpp"
#include "nn/fs.hpp"

#include <moonrush/offsets.hpp>
#include <moonrush/settings.hpp>

namespace mr = moonrush;
namespace off = moonrush::offsets;

#define MR_LOG(fmt, ...) Logging.Log("[Moonrush] " fmt, ##__VA_ARGS__)

namespace {

    constexpr const char* kMount = "moonrush";
    constexpr const char* kSettingsPath = "moonrush:/Moonrush/settings.ini";
    // Live channel with the launcher's "Moon & Speed" panel: the launcher writes live.ini (a request), the game
    // writes status.ini (a fixed 256-byte file, because nn::fs cannot shrink a file) twice a second.
    constexpr const char* kLivePath = "moonrush:/Moonrush/live.ini";
    constexpr const char* kStatusPath = "moonrush:/Moonrush/status.ini";
    constexpr long kStatusSize = 256;
    constexpr int kLivePollFrames = 10;
    constexpr int kStatusFrames = 30;
    constexpr size_t kGetterCount = sizeof(off::k_getters) / sizeof(off::k_getters[0]);
    constexpr size_t kCappyCount = sizeof(off::k_cappy) / sizeof(off::k_cappy[0]);
    constexpr uint32_t kRet = 0xd65f03c0;

    using GetSceneObjFn = void* (*)(const void* holder, int id);
    // GameDataHolderAccessor is a single GameDataHolder* passed by value, i.e. in x0.
    using GetTotalShineNumFn = int (*)(void* gameDataHolder, int fileId);
    using GetCurrentShineNumFn = int (*)(void* gameDataHolder);
    using GetterFn = float (*)(const void* playerConst);
    using GetTransPtrFn = void* (*)(void* actor);
    struct Vec3 { float x, y, z; };  // sead::Vector3f (returned in s0-s2)
    using GetVecFn = const Vec3* (*)(const void* actor);
    using ActorCheckFn = bool (*)(const void* actor);
    using PadHoldFn = bool (*)(int port);
    using UpdaterCheckFn = bool (*)(const void* cameraPoseUpdater);

    constexpr int kPeekFrames = 18;       // hold the First Person button this long (0.3 s) to peek
    constexpr float kFpNearClip = 15.0f;  // vanilla is 100, which would cut away walls right in front of you

    enum class MovePath : uint8_t { None, Collider, PlayerCollider };
    constexpr const char* kPathNames[] = { "none", "Collider", "PlayerCollider" };

    // One Cappy throw param Moonrush has multiplied: where it lives, its original value, what was written.
    struct CappySlot { float* p = nullptr; float base = 0; float written = 0; };

    struct State {
        bool settingsLoaded = false;
        mr::Settings settings;
        const void* playerConst = nullptr;  // the playable Mario's PlayerConst (others stay vanilla)
        const void* lastPc = nullptr;       // for spotting a new scene
        float mult = 1.0f;
        float earned = 1.0f;                // the speed the Moon count has earned (before any manual choice)
        bool manual = false;                // the launcher asked for a manual speed (capped at `earned`)
        float manualReq = 0.0f;
        unsigned long long liveSeq = 0;     // last live.ini request applied; the one present at boot is ignored
        bool liveSeen = false;
        bool statusMade = false;
        unsigned beat = 0;
        int frame = 0;
        float jumpFactor = 1.0f;            // launch-power factor for Jump Height (1.0 = vanilla)
        float jumpEarned = 1.0f;            // jump height the Moon count has earned (1x = vanilla)
        float jumpApplied = 1.0f;           // jump height actually in use (earned, or a manual one capped at earned)
        bool jumpManual = false;            // the launcher asked for a manual jump height
        float jumpReq = 0.0f;
        float lastJumpApplied = -1.0f;      // for the log: only when the applied jump height changes
        bool animOk = false;                // the Moon Animation Speed hooks matched the 1.0.0 code and are installed
        float animCarry = 0.0f;             // fractional extra updates carried between frames (e.g. 1.5x)
        int animFrames = 0;                 // sped-up frames in the current demo, for the log
        int lastMoons = -1;
        float lastMult = -1;

        // Captures: the captured object's position, compared against each collider's trans pointer.
        const void* hackTrans = nullptr;
        float captureMult = 1.0f;  // 1.0 = vanilla (module off, or this capture switched off)
        const void* loggedHack = nullptr;
        MovePath hackPath = MovePath::None;
        MovePath loggedPath = MovePath::None;

        CappySlot cappy[kCappyCount];
        bool cappyLogged = false;

        // First Person.
        bool fpOn = true;              // the in-game button flips this
        int fpHeld = 0;                // frames the button has been held
        bool fpWant = false;           // wanted this frame (settings, button, auto-off rules)
        const char* fpWhy = "";        // why not, for the log
        const char* fpLoggedWhy = nullptr;
        Vec3 fpEye {};
        int fpStale = 1000;            // camera frames since Mario last moved
        bool fpCamApplied = false;     // the camera was actually moved last frame
        const void* fpChanger = nullptr;
        const void* fpPlayer = nullptr;
        float* nearPtr = nullptr;      // near-clip value First Person lowered, and its original
        float nearOrig = 0;
    } g;

    template<typename T>
    T gameFn(uintptr_t offset) {
        return reinterpret_cast<T>(exl::util::modules::GetTargetStart() + offset);
    }

    template<typename T>
    T at(const void* base, size_t offset) {
        return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(base) + offset);
    }

    bool isBranch(uint32_t word) { return (word & 0xfc000000) == 0x14000000; }

    // True if every address Moonrush relies on holds the 1.0.0 code, or (for getters) another
    // mod's single-branch hook that Moonrush can safely stack on.
    bool signaturesMatch() {
        const uintptr_t main = exl::util::modules::GetTargetStart();
        bool ok = true;
        int stacked = 0;
        for (const auto& sig : off::k_signatures) {
            const auto* code = reinterpret_cast<const uint32_t*>(main + sig.offset);
            bool exact = true;
            for (int i = 0; i < sig.count; i++) exact &= code[i] == sig.words[i];
            if (exact) continue;
            if (sig.hookable && isBranch(code[0]) && code[1] == kRet) {
                MR_LOG("stacking: %s is already hooked by another mod; Moon Speed multiplies its result", sig.what);
                stacked++;
                continue;
            }
            MR_LOG("inactive: %s at main+0x%lx is 0x%08x 0x%08x, expected 0x%08x (not SMO 1.0.0, or a patch Moonrush can't chain)",
                   sig.what, static_cast<unsigned long>(sig.offset), code[0], sig.count > 1 ? code[1] : 0, sig.words[0]);
            ok = false;
        }
        if (ok && stacked) MR_LOG("stacking on %d getter(s) hooked by another mod", stacked);
        return ok;
    }

    // Read settings.ini once, the first time Mario moves (the game's fs setup is done by then).
    void loadSettings() {
        g.settingsLoaded = true;
        Result r = nn::fs::MountSdCardForDebug(kMount);
        if (r != 0) MR_LOG("mounting sd returned 0x%x (continuing; it may already be mounted)", r);

        nn::fs::FileHandle file {};
        r = nn::fs::OpenFile(&file, kSettingsPath, nn::fs::OpenMode_Read);
        if (r != 0) {
            MR_LOG("no settings.ini (0x%x): Moonrush stays vanilla. Launch through the Moonrush launcher.", r);
            g.settings.captures = false;
            return;
        }
        long size = 0;
        nn::fs::GetFileSize(&size, file);
        static char buf[4096];
        size_t n = size < 0 ? 0 : (static_cast<size_t>(size) < sizeof(buf) ? static_cast<size_t>(size) : sizeof(buf));
        r = nn::fs::ReadFile(file, 0, buf, n);
        nn::fs::CloseFile(file);
        if (r != 0) {
            MR_LOG("reading settings.ini failed (0x%x): Moonrush stays vanilla.", r);
            g.settings.captures = false;
            return;
        }
        g.settings = mr::parseSettings(buf, n);
        const auto& s = g.settings;
        MR_LOG("settings: moon_speed=%s count=%s start=%.3f per_moon=%.5f max=%.3f curve=%d groups=%d%d%d%d%d%d%d",
               s.enabled ? "on" : "off", s.countTotal ? "total" : "current", s.start, s.perMoon, s.max,
               static_cast<int>(s.curve), s.groups[0], s.groups[1], s.groups[2], s.groups[3], s.groups[4],
               s.groups[5], s.groups[6]);
        MR_LOG("settings: captures=%s (%d switched off) cappy=%s", s.captures ? "on" : "off", s.capturesOffCount,
               s.cappy ? "on" : "off");
        MR_LOG("settings: jump=%s height=%.2fx scale_with_moons=%d (launch power x%.3f at full, gravity untouched)", s.jumpEnabled ? "on" : "off",
               s.jumpHeight, s.jumpScale ? 1 : 0, mr::jumpLaunchFactor(s));
        MR_LOG("settings: moon_anim=%s speed=%.2fx", s.moonAnimEnabled ? "on" : "off", s.moonAnimSpeed);
        MR_LOG("settings: first_person=%s button=%d peek=%d off_cutscenes=%d off_2d=%d off_captures=%d",
               s.firstPerson ? "on" : "off", static_cast<int>(s.fpButton), s.fpPeek, s.fpOffCutscenes, s.fpOff2D,
               s.fpOffCaptures);
    }

    // ------------------------------------------------------------------ Live control (launcher panel)

    // A request is applied only when its seq is newer than the last one seen, and the seq already in the file at
    // boot is never applied, so a manual speed from a previous session cannot come back by itself.
    void pollLiveControl() {
        nn::fs::FileHandle file {};
        if (nn::fs::OpenFile(&file, kLivePath, nn::fs::OpenMode_Read) != 0) {
            g.liveSeen = true;
            return;
        }
        long size = 0;
        nn::fs::GetFileSize(&size, file);
        char buf[256];
        size_t n = size < 0 ? 0 : (static_cast<size_t>(size) < sizeof(buf) ? static_cast<size_t>(size) : sizeof(buf));
        Result r = nn::fs::ReadFile(file, 0, buf, n);
        nn::fs::CloseFile(file);
        if (r != 0) return;
        const mr::LiveControl c = mr::parseLiveControl(buf, n);
        if (!c.found) return;
        if (!g.liveSeen) {
            g.liveSeen = true;
            g.liveSeq = c.seq;
            return;
        }
        if (c.seq <= g.liveSeq) return;
        g.liveSeq = c.seq;
        if (c.speedFound) {
            g.manual = !c.earned;
            g.manualReq = c.speed;
            if (g.manual)
                MR_LOG("live: manual speed requested %.3fx (earned %.3fx; the game caps it at the earned speed)", c.speed, g.earned);
            else
                MR_LOG("live: return to earned speed %.3fx", g.earned);
        }
        if (c.jumpFound) {
            g.jumpManual = !c.jumpEarned;
            g.jumpReq = c.jump;
            if (g.jumpManual)
                MR_LOG("live: manual jump height requested %.3fx (earned %.3fx; the game caps it at the earned jump)", c.jump, g.jumpEarned);
            else
                MR_LOG("live: return to earned jump height %.3fx", g.jumpEarned);
        }
    }

    void writeLiveStatus(int moons) {
        if (!g.statusMade) {
            g.statusMade = true;
            nn::fs::CreateFile(kStatusPath, kStatusSize);  // fails harmlessly if it already exists
        }
        char buf[kStatusSize];
        const float applied = g.mult;
        int len = std::snprintf(buf, sizeof(buf),
                                "v=1\nbeat=%u\nmoons=%d\nearned=%.4f\napplied=%.4f\nmode=%s\nmin=%.4f\nmax=%.4f\nenabled=%d\njenabled=%d\njearned=%.4f\njapplied=%.4f\njmode=%s\njmax=%.4f\n",
                                ++g.beat, moons, g.earned, applied,
                                (g.settings.enabled && g.manual) ? "manual" : "earned", mr::kLiveMin, g.settings.max,
                                g.settings.enabled ? 1 : 0, g.settings.jumpEnabled ? 1 : 0, g.jumpEarned, g.jumpApplied,
                                (g.settings.jumpEnabled && g.jumpManual) ? "manual" : "earned", g.settings.jumpHeight);
        if (len < 0 || len >= kStatusSize) return;
        for (int i = len; i < kStatusSize; i++) buf[i] = ' ';
        buf[kStatusSize - 1] = '\n';
        nn::fs::FileHandle file {};
        if (nn::fs::OpenFile(&file, kStatusPath, nn::fs::OpenMode_Write) != 0) return;
        nn::fs::WriteFile(file, 0, buf, kStatusSize, nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
        nn::fs::CloseFile(file);
    }

    // ------------------------------------------------------------------ Moon Animation Speed
    //
    // The ordinary Moon-get demo (StageSceneStateGetShine) plays inside three nerves (DemoGetFirst, DemoGet,
    // DemoShineCount). Each frame they run the scene's demo update triple once (Mario's demo animation, the Moon,
    // the count layout, effects) and then check whether the demo is over. To play it N times faster, the triple is
    // run N-1 extra times just before the nerve's own code. Never on the first step of a nerve: that is where the
    // one-shot work lives (Shine::get, achievement prepo, starting the layout), and it must run exactly once.
    // The End nerve is left alone because it resumes normal gameplay updates. Moon counting, the save data,
    // the fanfare/sound triggers and every state transition stay the game's own.

    using SceneUpdateFn = void (*)(void* scene);
    using IsFirstStepFn = bool (*)(const void* nerveUser);

    bool animSignaturesMatch() {
        const uintptr_t main = exl::util::modules::GetTargetStart();
        for (const auto& sig : off::k_anim_signatures) {
            const auto* code = reinterpret_cast<const uint32_t*>(main + sig.offset);
            for (int i = 0; i < sig.count; i++) {
                if (code[i] != sig.words[i]) {
                    MR_LOG("moon animation: inactive. %s at main+0x%lx is 0x%08x, expected 0x%08x (another mod changed it?)",
                           sig.what, static_cast<unsigned long>(sig.offset), code[i], sig.words[i]);
                    return false;
                }
            }
        }
        return true;
    }

    // Run before the nerve's own code, with `self` = the StageSceneStateGetShine.
    void speedUpMoonDemo(void* self, const char* nerve) {
        const auto& s = g.settings;
        if (!g.animOk || !s.moonAnimEnabled) return;
        if (gameFn<IsFirstStepFn>(off::anim_al_isFirstStep)(self)) {
            g.animCarry = 0.0f;
            return;
        }
        void* scene = at<void*>(self, off::GetShine_mScene);
        if (scene == nullptr) return;
        const int extra = mr::moonAnimExtraUpdates(true, s.moonAnimSpeed, &g.animCarry);
        for (int i = 0; i < extra; i++) {
            gameFn<SceneUpdateFn>(off::anim_al_updateKitListPrev)(scene);
            gameFn<SceneUpdateFn>(off::anim_rs_updateKitListDemoPlayerWithPauseEffect)(scene);
            gameFn<SceneUpdateFn>(off::anim_al_updateKitListPostDemoWithPauseNormalEffect)(scene);
        }
        if (g.animFrames++ == 0) MR_LOG("moon animation: x%.2f active (%s, +%d update(s) this frame)", s.moonAnimSpeed, nerve, extra);
    }

    // ------------------------------------------------------------------ First Person

    void restoreNearClip() {
        if (g.nearPtr != nullptr) *g.nearPtr = g.nearOrig;
        g.nearPtr = nullptr;
    }

    uintptr_t fpPadFn(mr::FpButton b) {
        switch (b) {
            case mr::FpButton::DpadUp: return off::al_isPadHoldUp;
            case mr::FpButton::DpadDown: return off::al_isPadHoldDown;
            case mr::FpButton::DpadLeft: return off::al_isPadHoldLeft;
            case mr::FpButton::DpadRight: return off::al_isPadHoldRight;
            default: return off::al_isPadHoldPressLeftStick;
        }
    }

    // Once per frame with Mario: read the button, apply the auto-off rules, and work out the eye position.
    void updateFirstPerson(void* player) {
        const auto& s = g.settings;
        if (player != g.fpPlayer) {
            // New scene: the old camera poser is gone, so its near clip must not be written back.
            g.fpPlayer = player;
            g.nearPtr = nullptr;
        }
        if (!s.firstPerson) {
            g.fpWant = false;
            return;
        }

        // Button: tap switches first/third person. Holding it (if peek is on) shows third person until released.
        bool held = gameFn<PadHoldFn>(fpPadFn(s.fpButton))(-1);
        bool peeking = false;
        if (held) {
            g.fpHeld++;
            if (!s.fpPeek && g.fpHeld == 1) g.fpOn = !g.fpOn;
            peeking = s.fpPeek && g.fpHeld >= kPeekFrames;
        } else {
            if (s.fpPeek && g.fpHeld > 0 && g.fpHeld < kPeekFrames) g.fpOn = !g.fpOn;
            g.fpHeld = 0;
        }

        auto* bytes = static_cast<uint8_t*>(player);
        void* keeper = *reinterpret_cast<void**>(bytes + off::PlayerActorHakoniwa_mHackKeeper);
        void* hack = keeper ? at<void*>(keeper, off::PlayerHackKeeper_mHackActor) : nullptr;
        const char* why = nullptr;
        if (!g.fpOn) why = "switched to third person";
        else if (peeking) why = "peeking";
        else if (s.fpOffCutscenes && gameFn<ActorCheckFn>(off::rs_isActiveDemo)(player)) why = "cutscene";
        else if (s.fpOff2D && gameFn<ActorCheckFn>(off::rs_isPlayer2D)(player)) why = "2D section";
        else if (s.fpOffCaptures && hack != nullptr) why = "capture";
        g.fpWant = why == nullptr;
        g.fpWhy = why ? why : "";
        g.fpChanger = *reinterpret_cast<void**>(bytes + off::PlayerActorHakoniwa_mModelChanger);

        // Eye: Mario's (or the capture's) position, raised along "up" (the opposite of gravity).
        const void* body = hack ? hack : player;
        const Vec3* p = gameFn<GetVecFn>(off::al_getTrans)(body);
        const Vec3* grav = gameFn<GetVecFn>(off::al_getGravity)(body);
        const void* pc = *reinterpret_cast<void**>(bytes + off::PlayerActorHakoniwa_mConst);
        float tall = pc ? at<float>(pc, off::PlayerConst_mTall) : 0.0f;
        if (!(tall > 50.0f && tall < 500.0f)) tall = 160.0f;
        float height = hack ? tall * 0.6f : tall * 0.85f;
        float gl = __builtin_sqrtf(grav->x * grav->x + grav->y * grav->y + grav->z * grav->z);
        float ux = 0, uy = 1, uz = 0;
        if (gl > 1e-4f) { ux = -grav->x / gl; uy = -grav->y / gl; uz = -grav->z / gl; }
        g.fpEye = { p->x + ux * height, p->y + uy * height, p->z + uz * height };
        g.fpStale = 0;
    }

    // Right after the game has posed its camera: move the eye into Mario's head, same view direction.
    void applyFirstPerson(void* updater) {
        bool apply = g.fpWant && g.fpStale++ < 5 && at<bool>(updater, off::CameraPoseUpdater_mIsMainView);
        const char* why = g.fpWhy;
        if (apply && g.settings.fpOffCutscenes &&
            !gameFn<UpdaterCheckFn>(off::CameraPoseUpdater_isCurrentCameraEnableRotateByPad)(updater)) {
            apply = false;
            why = "scripted camera";
        }
        if (!apply) {
            g.fpCamApplied = false;
            restoreNearClip();
            if (g.settings.firstPerson && why != g.fpLoggedWhy && *why) {
                g.fpLoggedWhy = why;
                MR_LOG("first person: third person (%s)", why);
            }
            return;
        }
        auto* bytes = static_cast<uint8_t*>(updater);
        auto* pos = reinterpret_cast<Vec3*>(bytes + off::CameraPoseUpdater_mPos);
        auto* look = reinterpret_cast<Vec3*>(bytes + off::CameraPoseUpdater_mAt);
        Vec3 dir { look->x - pos->x, look->y - pos->y, look->z - pos->z };
        if (dir.x * dir.x + dir.y * dir.y + dir.z * dir.z < 1e-6f) return;
        *pos = g.fpEye;
        *look = { g.fpEye.x + dir.x, g.fpEye.y + dir.y, g.fpEye.z + dir.z };
        g.fpCamApplied = true;

        // Near clip: the camera poser's value wins if it has one, otherwise the updater's own.
        void* ticket = at<void*>(updater, off::CameraPoseUpdater_mTicket);
        void* poser = ticket ? at<void*>(ticket, 0) : nullptr;
        float* nearP = poser && at<float>(poser, off::CameraPoser_mNearClip) > 0.0f
            ? reinterpret_cast<float*>(static_cast<uint8_t*>(poser) + off::CameraPoser_mNearClip)
            : reinterpret_cast<float*>(bytes + off::CameraPoseUpdater_mNearClipDistance);
        if (nearP != g.nearPtr) {
            restoreNearClip();
            g.nearPtr = nearP;
            g.nearOrig = *nearP;
        }
        *nearP = kFpNearClip;

        static constexpr const char* kOn = "on";
        if (g.fpLoggedWhy != kOn) {
            g.fpLoggedWhy = kOn;
            MR_LOG("first person: on (eye %.0f %.0f %.0f, near clip %.0f -> %.0f)", g.fpEye.x, g.fpEye.y, g.fpEye.z,
                   g.nearOrig, kFpNearClip);
        }
    }

    float scaled(size_t slot, const void* self, float value) {
        if (self == nullptr || self != g.playerConst) return value;
        const int group = off::k_getters[slot].group;
        if (group == off::kJumpGroup) return value * g.jumpFactor;
        if (!g.settings.enabled || !g.settings.groups[group]) return value;
        return value * g.mult;
    }

    // The captured object's move for this frame, with its sideways part multiplied. Vertical part unchanged.
    Vec3 scaleSideways(const Vec3& move, const Vec3* gravity, float mult) {
        float gx = 0, gy = -1, gz = 0;
        if (gravity != nullptr) {
            float len2 = gravity->x * gravity->x + gravity->y * gravity->y + gravity->z * gravity->z;
            if (len2 > 1e-6f) {
                float inv = 1.0f / __builtin_sqrtf(len2);
                gx = gravity->x * inv; gy = gravity->y * inv; gz = gravity->z * inv;
            }
        }
        float down = move.x * gx + move.y * gy + move.z * gz;
        float hx = move.x - gx * down, hy = move.y - gy * down, hz = move.z - gz * down;
        float k = mult - 1.0f;
        return { move.x + hx * k, move.y + hy * k, move.z + hz * k };
    }

    // Cappy: multiply (or restore) the throw params in place.
    void updateCappy(void* hackCap, bool on, float mult) {
        if (hackCap == nullptr) return;
        void* params = at<void*>(hackCap, off::HackCap_mThrowParam);
        if (params == nullptr) return;
        const uintptr_t missing = exl::util::modules::GetTargetStart() + off::ActorParam_missing;
        for (size_t i = 0; i < kCappyCount; i++) {
            auto& slot = g.cappy[i];
            float* p = at<float*>(params, off::k_cappy[i].offset);
            if (p == nullptr || reinterpret_cast<uintptr_t>(p) == missing) continue;
            // New object, or the game reloaded the value: what's there now is the original.
            if (p != slot.p || *p != slot.written) {
                slot.p = p;
                slot.base = *p;
                slot.written = *p;
            }
            float want = on ? slot.base * mult : slot.base;
            if (*p != want) {
                *p = want;
                slot.written = want;
            }
        }
        if (on && !g.cappyLogged) {
            g.cappyLogged = true;
            MR_LOG("cappy: throw params x%.3f (MaxSpeed %.2f -> %.2f, Reach %.1f -> %.1f)", mult, g.cappy[0].base,
                   g.cappy[0].written, g.cappy[2].base, g.cappy[2].written);
        }
    }

    // Captures: remember what Mario has captured this frame, and report it once per capture.
    void updateCapture(void* keeper) {
        const auto& s = g.settings;
        void* hack = keeper && s.captures ? at<void*>(keeper, off::PlayerHackKeeper_mHackActor) : nullptr;
        if (hack == nullptr) {
            g.hackTrans = nullptr;
            g.loggedHack = nullptr;
            g.captureMult = 1.0f;
            return;
        }
        const char* name = nullptr;
        if (at<void*>(keeper, off::PlayerHackKeeper_mHackSensor)) {
            void* info = at<void*>(keeper, off::PlayerHackKeeper_mHackObjInfo);
            if (info) name = at<const char*>(info, 0);
        }
        bool switchedOff = s.isCaptureOff(name);
        g.hackTrans = gameFn<GetTransPtrFn>(off::al_getTransPtr)(hack);
        g.captureMult = switchedOff ? 1.0f : g.mult;
        if (hack != g.loggedHack) {
            g.loggedHack = hack;
            g.hackPath = MovePath::None;
            g.loggedPath = MovePath::None;
            MR_LOG("capture: name=%s mult=%.3f%s", name ? name : "?", g.captureMult, switchedOff ? " (switched off)" : "");
        }
        // Which collision function the capture moves through (set by the collide hooks).
        if (g.hackPath != g.loggedPath) {
            g.loggedPath = g.hackPath;
            MR_LOG("capture: name=%s moves through %s", name ? name : "?", kPathNames[static_cast<int>(g.hackPath)]);
        }
    }

    // Once per frame, before Mario's movement: work out the multiplier, which PlayerConst is his,
    // and what (if anything) he has captured.
    void updateMoonSpeed(void* player) {
        if (!g.settingsLoaded) loadSettings();
        const auto& s = g.settings;
        if (!s.enabled && !s.captures && !s.cappy && !s.jumpEnabled) {
            g.playerConst = nullptr;
            g.hackTrans = nullptr;
            return;
        }

        auto* playerBytes = static_cast<uint8_t*>(player);
        const void* pc = *reinterpret_cast<void**>(playerBytes + off::PlayerActorHakoniwa_mConst);
        void* holder = playerBytes + off::LiveActor_IUseSceneObjHolder;
        void* gameData = gameFn<GetSceneObjFn>(off::al_getSceneObj)(holder, off::SceneObjId_GameDataHolder);
        if (pc == nullptr || gameData == nullptr) {
            g.playerConst = nullptr;
            g.hackTrans = nullptr;
            return;
        }
        int moons = s.countTotal
            ? gameFn<GetTotalShineNumFn>(off::GameDataFunction_getTotalShineNum)(gameData, -1)
            : gameFn<GetCurrentShineNumFn>(off::GameDataFunction_getCurrentShineNum)(gameData);

        bool newScene = pc != g.lastPc;
        g.lastPc = pc;
        g.earned = mr::multiplier(s, moons);
        if (++g.frame % kLivePollFrames == 0 && (s.enabled || s.jumpEnabled)) pollLiveControl();
        // A manual speed (launcher request) is capped at the earned speed right here, every frame.
        g.mult = mr::liveSpeed(g.earned, s.enabled && g.manual, g.manualReq);
        if (g.frame % kStatusFrames == 0) writeLiveStatus(moons);
        g.playerConst = (s.enabled || s.jumpEnabled) ? pc : nullptr;
        // Jump Height is earned like speed (1x at 0 Moons -> the setting at full progress), and a manual height from
        // the launcher is capped at the earned one here, every frame.
        g.jumpEarned = mr::earnedJump(s, moons);
        g.jumpApplied = mr::liveJump(g.jumpEarned, s.jumpEnabled && g.jumpManual, g.jumpReq);
        g.jumpFactor = s.jumpEnabled ? mr::jumpLaunchFactorFor(g.jumpApplied) : 1.0f;
        if (s.jumpEnabled && g.jumpApplied != g.lastJumpApplied) {
            g.lastJumpApplied = g.jumpApplied;
            MR_LOG("jump: earned %.3fx applied %.3fx (%s, launch power x%.3f)", g.jumpEarned, g.jumpApplied,
                   g.jumpManual ? "manual" : "earned", g.jumpFactor);
        }

        if (newScene || moons != g.lastMoons || g.mult != g.lastMult) {
            g.lastMoons = moons;
            g.lastMult = g.mult;
            // Proof from inside the game: the stored value vs what the (hooked) getter now returns.
            const auto& run = off::k_getters[1];  // NormalMaxSpeed
            float stored = *reinterpret_cast<const float*>(static_cast<const uint8_t*>(pc) + run.field);
            float effective = gameFn<GetterFn>(run.offset)(pc);
            MR_LOG("moons=%d (%s) mult=%.3f%s NormalMaxSpeed stored=%.3f effective=%.3f",
                   moons, s.countTotal ? "total" : "held", g.mult, newScene ? " new-scene" : "", stored, effective);
        }

        void* keeper = *reinterpret_cast<void**>(playerBytes + off::PlayerActorHakoniwa_mHackKeeper);
        updateCapture(keeper);
        updateCappy(keeper ? at<void*>(keeper, off::PlayerHackKeeper_mHackCap) : nullptr, s.cappy, g.mult);
    }

    // Proof per value: the first time the game reads each speed this session, log what it got.
    bool g_getterLogged[kGetterCount] = {};

    template<size_t Slot>
    struct GetterHook : exl::hook::impl::TrampolineHook<GetterHook<Slot>> {
        static float Callback(const void* self) {
            float in = GetterHook::Orig(self);
            float out = scaled(Slot, self, in);
            if (!g_getterLogged[Slot] && g.playerConst != nullptr) {
                g_getterLogged[Slot] = true;
                MR_LOG("read: %s %.3f -> %.3f (%s)", off::k_getters[Slot].name, in, out,
                       self == g.playerConst ? "Mario" : "not Mario's PlayerConst");
            }
            return out;
        }
    };

    template<size_t... Slots>
    void installGetterHooks(std::index_sequence<Slots...>) {
        (GetterHook<Slots>::InstallAtOffset(off::k_getters[Slots].offset), ...);
    }

}  // namespace

HOOK_DEFINE_TRAMPOLINE(ColliderCollide) {
    static Vec3 Callback(void* self, const Vec3& move) {
        if (g.hackTrans != nullptr && at<const void*>(self, off::Collider_mTransPtr) == g.hackTrans) {
            g.hackPath = MovePath::Collider;
            if (g.captureMult != 1.0f)
                return Orig(self, scaleSideways(move, at<const Vec3*>(self, off::Collider_mGravityPtr), g.captureMult));
        }
        return Orig(self, move);
    }
};

HOOK_DEFINE_TRAMPOLINE(PlayerColliderCollide) {
    static Vec3 Callback(void* self, const Vec3& move) {
        if (g.hackTrans != nullptr && at<const void*>(self, off::PlayerCollider_mTransPtr) == g.hackTrans) {
            g.hackPath = MovePath::PlayerCollider;
            if (g.captureMult != 1.0f)
                return Orig(self, scaleSideways(move, at<const Vec3*>(self, off::PlayerCollider_mGravityPtr), g.captureMult));
        }
        return Orig(self, move);
    }
};

// Moon Animation Speed: the ordinary Moon-get demo's three timed nerves, and its entry (for the log only).
HOOK_DEFINE_TRAMPOLINE(MoonDemoAppear) {
    static void Callback(void* self) {
        g.animFrames = 0;
        g.animCarry = 0.0f;
        MR_LOG("moon animation: ordinary Moon-get demo started (speed-up %s)",
               g.animOk && g.settings.moonAnimEnabled ? "on" : "off");
        Orig(self);
    }
};

HOOK_DEFINE_TRAMPOLINE(MoonDemoGetFirst) {
    static void Callback(void* self) {
        speedUpMoonDemo(self, "DemoGetFirst");
        Orig(self);
    }
};

HOOK_DEFINE_TRAMPOLINE(MoonDemoGet) {
    static void Callback(void* self) {
        speedUpMoonDemo(self, "DemoGet");
        Orig(self);
    }
};

HOOK_DEFINE_TRAMPOLINE(MoonDemoShineCount) {
    static void Callback(void* self) {
        speedUpMoonDemo(self, "DemoShineCount");
        Orig(self);
    }
};

HOOK_DEFINE_TRAMPOLINE(PlayerMovement) {
    static void Callback(void* player) {
        updateMoonSpeed(player);
        updateFirstPerson(player);
        Orig(player);
    }
};

// First Person: runs the game's normal camera, then moves the eye.
HOOK_DEFINE_TRAMPOLINE(CameraExeActive) {
    static void Callback(void* updater) {
        Orig(updater);
        applyFirstPerson(updater);
    }
};

// First Person: hide Mario only while his show/hide flags are applied to the model, then put the
// game's own flags back, so captures, pipes etc. keep full control of his visibility.
HOOK_DEFINE_TRAMPOLINE(ModelSyncShowHide) {
    static void Callback(void* changer, void* model) {
        if (!g.fpCamApplied || changer != g.fpChanger) {
            Orig(changer, model);
            return;
        }
        auto* flags = static_cast<uint8_t*>(changer) + off::PlayerModelChanger_mShowFlags;
        uint8_t saved[3] = { flags[0], flags[1], flags[2] };
        flags[0] = flags[1] = flags[2] = 0;
        Orig(changer, model);
        flags[0] = saved[0]; flags[1] = saved[1]; flags[2] = saved[2];
    }
};

extern "C" void exl_main(void* x0, void* x1) {
    exl::hook::Initialize();

    if (!signaturesMatch()) return;
    PlayerMovement::InstallAtOffset(off::PlayerActorHakoniwa_movement);
    installGetterHooks(std::make_index_sequence<kGetterCount>{});
    ColliderCollide::InstallAtOffset(off::al_Collider_collide);
    PlayerColliderCollide::InstallAtOffset(off::PlayerCollider_collide);
    CameraExeActive::InstallAtOffset(off::CameraPoseUpdater_exeActive);
    ModelSyncShowHide::InstallAtOffset(off::PlayerModelChangerHakoniwa_syncShowHide);
    // Optional feature: its own signature check, so a mismatch only turns Moon Animation Speed off.
    g.animOk = animSignaturesMatch();
    if (g.animOk) {
        MoonDemoAppear::InstallAtOffset(off::anim_GetShine_appear);
        MoonDemoGetFirst::InstallAtOffset(off::anim_GetShine_exeDemoGetFirst);
        MoonDemoGet::InstallAtOffset(off::anim_GetShine_exeDemoGet);
        MoonDemoShineCount::InstallAtOffset(off::anim_GetShine_exeDemoShineCount);
    }
    MR_LOG("loaded v0.4.0 for SMO 1.0.0 (%s); hooks installed: Moon Speed + Jump Height (%d getters), Captures, Cappy, First Person",
           off::kBuildId, static_cast<int>(kGetterCount));
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("Default exception handler called!");
}
