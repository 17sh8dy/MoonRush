"""Generate game/src/offsets.hpp from the real SMO 1.0.0 code.

Every PlayerConst field offset is read from its getter (`ldr s0, [x0, #imm]; ret`), so nothing
is guessed from headers. Re-run after changing GROUPS.
"""
import os, struct, smo_disasm as s

GROUPS = {
    'walk': ['NormalMinSpeed', 'NormalMaxSpeed', 'RunAfterTurnSpeedMax', 'DashJudgeSpeed'],
    'squat': ['SquatWalkSpeed'],
    'dive': ['HeadSlidingSpeed', 'HeadSlidingSpeedMin'],
    'roll': ['SlopeRollingSpeedStart', 'SlopeRollingSpeedBoost', 'SlopeRollingMaxSpeed', 'SlopeRollingReStartMaxAdd'],
    'long_jump': ['LongJumpInitSpeed', 'LongJumpSpeed', 'LongJumpSpeedMin'],
    'air': ['JumpBaseSpeedMax', 'JumpMoveSpeedMin', 'JumpMoveSpeedMax'],
    'swim': ['SwimSurfaceSpeedMaxH', 'SwimHighSpeedMaxH', 'SwimLowSpeedMaxH', 'SwimFloorSpeedMaxH', 'SwimWalkMaxSpeed'],
    # v0.4.0 Jump Height: launch power only. Gravity getters (JumpGravity, GravityAir, ...) are NEVER hooked.
    # MUST stay last: index 7 = off::kJumpGroup, which is not one of the Moon Speed groups.
    'jump': ['JumpPowerMin', 'JumpPowerMax', 'JumpPowerMax2nd', 'JumpPowerMax3rd', 'ContinuousJumpPowerMin',
             'SquatJumpPower', 'SquatJumpBackPower', 'TurnJumpPower', 'WallJumpPower',
             'JumpPowerMin2DArea', 'JumpPowerMax2DArea'],
}
FUNCS = {
    'PlayerActorHakoniwa_movement': '_ZN19PlayerActorHakoniwa8movementEv',
    'GameDataFunction_getTotalShineNum': '_ZN16GameDataFunction16getTotalShineNumE22GameDataHolderAccessori',
    'GameDataFunction_getCurrentShineNum': '_ZN16GameDataFunction18getCurrentShineNumE22GameDataHolderAccessor',
    'al_getSceneObj': '_ZN2al11getSceneObjEPKNS_18IUseSceneObjHolderEi',
    # Captures: every captured object moves through one of these two collision functions.
    'al_Collider_collide': '_ZN2al8Collider7collideERKN4sead7Vector3IfEE',
    'PlayerCollider_collide': '_ZN14PlayerCollider7collideERKN4sead7Vector3IfEE',
    'al_getTransPtr': '_ZN2al11getTransPtrEPNS_9LiveActorE',
    'al_findActorParamF32': '_ZN2al17findActorParamF32EPKNS_9LiveActorEPKc',
    # First Person
    'CameraPoseUpdater_exeActive': '_ZN2al17CameraPoseUpdater9exeActiveEv',
    'CameraPoseUpdater_isCurrentCameraEnableRotateByPad': '_ZNK2al17CameraPoseUpdater32isCurrentCameraEnableRotateByPadEv',
    'PlayerModelChangerHakoniwa_syncShowHide': '_ZN26PlayerModelChangerHakoniwa12syncShowHideEPN2al9LiveActorE',
    'rs_isActiveDemo': '_ZN2rs12isActiveDemoEPKN2al9LiveActorE',
    'rs_isPlayer2D': '_ZN2rs10isPlayer2DEPKN2al9LiveActorE',
    'al_getTrans': '_ZN2al8getTransEPKNS_9LiveActorE',
    'al_getGravity': '_ZN2al10getGravityEPKNS_9LiveActorE',
    'al_isPadHoldPressLeftStick': '_ZN2al23isPadHoldPressLeftStickEi',
    'al_isPadHoldUp': '_ZN2al11isPadHoldUpEi',
    'al_isPadHoldDown': '_ZN2al13isPadHoldDownEi',
    'al_isPadHoldLeft': '_ZN2al13isPadHoldLeftEi',
    'al_isPadHoldRight': '_ZN2al14isPadHoldRightEi',
}

syms = s.symbols(); img = s.flat_image()

def field_offset(name):
    mangled = f'_ZNK11PlayerConst{3 + len(name)}get{name}Ev'
    off, size, _ = syms[mangled]
    ldr, ret = struct.unpack_from('<II', img, off)
    assert ret == 0xd65f03c0, f'{name}: getter is not a plain load'
    assert (ldr & 0xffc003ff) == 0xbd400000, f'{name}: not "ldr s0, [x0, #imm]" ({ldr:08x})'
    return ((ldr >> 10) & 0xfff) * 4, off

# Verified by hand from disassembly (see docs/RESEARCH.md):
#   PlayerActorHakoniwa::initPlayer @0x41b704: bl createMarioConst; str x0, [x19, #304]
#   GameDataHolderAccessor(IUseSceneObjHolder*) @0x5316ec: al::getSceneObj(holder, 18)
#   LiveActor -> IUseSceneObjHolder: add x1, x0, #0x20 (153 call sites)
# Captures / Cappy (2026-09-28):
#   PlayerActorHakoniwa::getPlayerHackKeeper @0x4298f0: ldr x0, [x0, #520]
#   PlayerHackKeeper ctor @0x448f30: stp x1, x2, [x19] (parent, HackCap)
#   PlayerHackKeeper::startHack @0x449138: stp actor, sensor, [x19, #104]; str HackObjInfo, [x19, #120]
#   PlayerHackKeeper::getCurrentHackName @0x449c7c: sensor at #112 ? *(char**)[#120] : null
#   HackCap::init @0x3fa1e4: new HackCapThrowParam (0xc0) -> str [x19, #544]; its ctor stores the 24
#     ActorParam pointers in declaration order (checked against the param-name strings)
#   al::Collider ctor @0x844740: trans ptr -> #40, gravity ptr -> #48
#   PlayerCollider ctor @0x42f784: trans ptr -> #24, gravity ptr -> #32
#   al::findActorParamF32 @0x8ec3e8: actor without params -> returns main+0x1923d60 (shared, never write)
# First Person (2026-09-28):
#   CameraPoseUpdater::exeActive @0x837f90: first step -> strb [x19, #56] (mIsMainView); final pose stored to
#     #120/#132/#144 (sead::LookAtCamera pos/at/up; the LookAtCamera object starts at #64)
#   CameraPoseUpdater::isCurrentCameraEnableRotateByPad @0x838600: ticket at #160, poser = *ticket
#   CameraPoseUpdater::update @0x83794c: near clip = poser[#100] if > 0 else this[#184]
#   PlayerModelChangerHakoniwa::syncShowHide @0x45e22c: applies show flags at #65 (model) #66 #67 to the model
#     actor; hideModel writes 0x00000001 to #64, showModel 0x01010101. Called every frame via update/syncHost.
#   PlayerActorHakoniwa: mModelChanger at #352 (header order after mConst #304), PlayerConst::getTall -> #84
#   PlayerInputFunction::isTriggerCameraSubjective @0x450640 uses the right-stick click, so First Person
#     never uses that button.
out = ['// GENERATED by tools/gen_offsets.py from SMO 1.0.0 (build 3CA12DFAAF9C82DA064D1698DF79CDA1). Do not edit.',
       '#pragma once', '#include <cstddef>', '#include <cstdint>', '', 'namespace moonrush::offsets {', '',
       'constexpr const char* kBuildId = "3CA12DFAAF9C82DA064D1698DF79CDA1";', '',
       '// Offsets into the main module.']
for k, sym in FUNCS.items():
    out.append(f'constexpr uintptr_t {k} = 0x{syms[sym][0]:x};  // {sym}')
out += ['', '// Object layout (verified in disassembly).',
        'constexpr size_t PlayerActorHakoniwa_mConst = 0x130;',
        'constexpr size_t LiveActor_IUseSceneObjHolder = 0x20;',
        'constexpr int SceneObjId_GameDataHolder = 18;',
        'constexpr size_t PlayerConst_size = 0x9a8;',
        'constexpr int kJumpGroup = 7;  // group index of the Jump Height getters (not a Moon Speed group)',
        'constexpr size_t PlayerActorHakoniwa_mHackKeeper = 0x208;',
        'constexpr size_t PlayerHackKeeper_mHackCap = 0x8;',
        'constexpr size_t PlayerHackKeeper_mHackActor = 0x68;',
        'constexpr size_t PlayerHackKeeper_mHackSensor = 0x70;',
        'constexpr size_t PlayerHackKeeper_mHackObjInfo = 0x78;  // HackObjInfo: first field is the capture name',
        'constexpr size_t HackCap_mThrowParam = 0x220;',
        'constexpr size_t Collider_mTransPtr = 0x28;',
        'constexpr size_t Collider_mGravityPtr = 0x30;',
        'constexpr size_t PlayerCollider_mTransPtr = 0x18;',
        'constexpr size_t PlayerCollider_mGravityPtr = 0x20;',
        'constexpr uintptr_t ActorParam_missing = 0x1923d60;  // what findActorParamF32 returns when there is no param',
        'constexpr size_t CameraPoseUpdater_mIsMainView = 0x38;',
        'constexpr size_t CameraPoseUpdater_mPos = 0x78;',
        'constexpr size_t CameraPoseUpdater_mAt = 0x84;',
        'constexpr size_t CameraPoseUpdater_mUp = 0x90;',
        'constexpr size_t CameraPoseUpdater_mTicket = 0xa0;',
        'constexpr size_t CameraPoseUpdater_mNearClipDistance = 0xb8;',
        'constexpr size_t CameraPoser_mNearClip = 0x64;',
        'constexpr size_t PlayerModelChanger_mShowFlags = 0x41;  // model, silhouette, shadow (1 byte each)',
        'constexpr size_t PlayerConst_mTall = 0x54;',
        'constexpr size_t PlayerActorHakoniwa_mModelChanger = 0x160;  // syncHost call sites: ldr x0, [x19, #352]',
        '',
        '// HackCapThrowParam fields Cappy scaling multiplies (ActorParamF32* each). Heights, times, angles stay vanilla.',
        'struct CappyParam { uint16_t offset; const char* what; };',
        'constexpr CappyParam k_cappy[] = {',
        '    {0x08, "MaxSpeed"}, {0x10, "ContinuousThrowSpeed"}, {0x20, "Reach"}, {0x40, "ReturnMaxSpeed"},',
        '    {0x50, "WaterMaxSpeed"}, {0x58, "WaterReach"}, {0x68, "WaterReturnMaxSpeed"},',
        '    {0x70, "TornadoReach"}, {0x78, "TornadoMaxReach"},',
        '    {0x88, "RollSpeed"}, {0x90, "RollReachUp"}, {0x98, "RollReachDown"},',
        '};', '',
        '// offset: field inside PlayerConst. getter: the 8-byte `ldr s0, [x0, #offset]; ret` function Moonrush hooks.',
        'struct Field { const char* name; uint16_t offset; uintptr_t getter; };', '']
for g, names in GROUPS.items():
    out.append(f'constexpr Field k_{g}[] = {{')
    for n in names:
        fo, go = field_offset(n)
        assert fo < 0x9a8
        out.append(f'    {{"{n}", 0x{fo:x}, 0x{go:x}}},')
    out.append('};')
# Flat list of every getter, tagged with its group index (same order as moonrush::Group).
out += ['', 'struct Getter { int group; const char* name; uint16_t field; uintptr_t offset; };', 'constexpr Getter k_getters[] = {']
for gi, (g, names) in enumerate(GROUPS.items()):
    for n in names:
        fo, go = field_offset(n)
        out.append(f'    {{{gi}, "{n}", 0x{fo:x}, 0x{go:x}}},  // {g}')
out.append('};')

# Signatures: exact instruction words at every address Moonrush depends on. Checked at startup.
# Getters may instead start with a `b` (another mod hooked them first); Moonrush then stacks on top.
# Any other mismatch (other game version, or a patch Moonrush can't chain) means it stays inactive.
sigs = []
for k, sym in FUNCS.items():
    o = syms[sym][0]
    sigs.append((o, struct.unpack_from('<4I', img, o), k, False))
for g, names in GROUPS.items():
    for n in names:
        _, go = field_offset(n)
        sigs.append((go, struct.unpack_from('<2I', img, go) + (None, None), 'get' + n, True))
out += ['', 'struct Signature { uintptr_t offset; uint32_t words[4]; uint8_t count; bool hookable; const char* what; };',
        'constexpr Signature k_signatures[] = {']
for o, words, what, hookable in sigs:
    ws = [w for w in words if w is not None]
    out.append(f'    {{0x{o:x}, {{{", ".join(f"0x{w:08x}" for w in ws)}}}, {len(ws)}, {"true" if hookable else "false"}, "{what}"}},')
out.append('};')
out += ['', '}  // namespace moonrush::offsets', '']
dst = os.path.join(os.path.dirname(__file__), '..', 'game', 'source', 'moonrush', 'offsets.hpp')
os.makedirs(os.path.dirname(dst), exist_ok=True)
open(dst, 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
print('\n'.join(out))
