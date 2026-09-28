// Moonrush game module (exlaunch, subsdk8).
//
// Moon Speed: Mario's speed values are read through small PlayerConst getters
// (`ldr s0, [x0, #field]; ret`). Moonrush hooks those getters and returns
// `whatever the getter would have returned * Moon multiplier`, but only for the playable Mario's
// PlayerConst. Nothing in memory or on disk is overwritten.
//
// Stacking: if another mod (e.g. the "Emulator build" speed mod) hooked a getter first, Moonrush's hook chains on
// top of it, so the result is `other mod's value * Moon multiplier`.
// All offsets come from tools/gen_offsets.py (SMO 1.0.0 only) and are checked at startup.

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
    constexpr size_t kGetterCount = sizeof(off::k_getters) / sizeof(off::k_getters[0]);
    constexpr uint32_t kRet = 0xd65f03c0;

    using GetSceneObjFn = void* (*)(const void* holder, int id);
    // GameDataHolderAccessor is a single GameDataHolder* passed by value, i.e. in x0.
    using GetTotalShineNumFn = int (*)(void* gameDataHolder, int fileId);
    using GetCurrentShineNumFn = int (*)(void* gameDataHolder);
    using GetterFn = float (*)(const void* playerConst);

    struct State {
        bool settingsLoaded = false;
        mr::Settings settings;
        const void* playerConst = nullptr;  // the playable Mario's PlayerConst (others stay vanilla)
        float mult = 1.0f;
        int lastMoons = -1;
        float lastMult = -1;
    } g;

    template<typename T>
    T gameFn(uintptr_t offset) {
        return reinterpret_cast<T>(exl::util::modules::GetTargetStart() + offset);
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
            return;
        }
        g.settings = mr::parseSettings(buf, n);
        const auto& s = g.settings;
        MR_LOG("settings: moon_speed=%s count=%s start=%.3f per_moon=%.5f max=%.3f curve=%d groups=%d%d%d%d%d%d%d",
               s.enabled ? "on" : "off", s.countTotal ? "total" : "current", s.start, s.perMoon, s.max,
               static_cast<int>(s.curve), s.groups[0], s.groups[1], s.groups[2], s.groups[3], s.groups[4],
               s.groups[5], s.groups[6]);
    }

    float scaled(size_t slot, const void* self, float value) {
        if (self == nullptr || self != g.playerConst) return value;
        if (!g.settings.groups[off::k_getters[slot].group]) return value;
        return value * g.mult;
    }

    // Once per frame, before Mario's movement: work out the multiplier and which PlayerConst is his.
    void updateMoonSpeed(void* player) {
        if (!g.settingsLoaded) loadSettings();
        const auto& s = g.settings;
        if (!s.enabled) {
            g.playerConst = nullptr;
            return;
        }

        auto* playerBytes = static_cast<uint8_t*>(player);
        const void* pc = *reinterpret_cast<void**>(playerBytes + off::PlayerActorHakoniwa_mConst);
        void* holder = playerBytes + off::LiveActor_IUseSceneObjHolder;
        void* gameData = gameFn<GetSceneObjFn>(off::al_getSceneObj)(holder, off::SceneObjId_GameDataHolder);
        if (pc == nullptr || gameData == nullptr) {
            g.playerConst = nullptr;
            return;
        }
        int moons = s.countTotal
            ? gameFn<GetTotalShineNumFn>(off::GameDataFunction_getTotalShineNum)(gameData, -1)
            : gameFn<GetCurrentShineNumFn>(off::GameDataFunction_getCurrentShineNum)(gameData);

        bool newScene = pc != g.playerConst;
        g.mult = mr::multiplier(s, moons);
        g.playerConst = pc;

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
    }

    template<size_t Slot>
    struct GetterHook : exl::hook::impl::TrampolineHook<GetterHook<Slot>> {
        static float Callback(const void* self) {
            return scaled(Slot, self, GetterHook::Orig(self));
        }
    };

    template<size_t... Slots>
    void installGetterHooks(std::index_sequence<Slots...>) {
        (GetterHook<Slots>::InstallAtOffset(off::k_getters[Slots].offset), ...);
    }

}  // namespace

HOOK_DEFINE_TRAMPOLINE(PlayerMovement) {
    static void Callback(void* player) {
        updateMoonSpeed(player);
        Orig(player);
    }
};

extern "C" void exl_main(void* x0, void* x1) {
    exl::hook::Initialize();

    if (!signaturesMatch()) return;
    PlayerMovement::InstallAtOffset(off::PlayerActorHakoniwa_movement);
    installGetterHooks(std::make_index_sequence<kGetterCount>{});
    MR_LOG("loaded v0.2.0 for SMO 1.0.0 (%s); Moon Speed hooks installed (%d getters)", off::kBuildId,
           static_cast<int>(kGetterCount));
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("Default exception handler called!");
}
