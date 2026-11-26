// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// EffectTypes.cpp — PTR_s_EFT_NONE_00612a68 (stride 0x28), FUN_004fd500/560/5a0.
// EffectLOD screen-size table — DAT_00667d90 (FUN_0042dae0 / LAB_0042dac0).

// The [EffectType] rows, in the order of G_EffectTypeTable; the names are the retail strings
// (PTR_s_EFT_NONE_00612a68 .. PTR_s_EFT_NUMTYPES_006131c0).
enum EEffectType
{
    EFT_NONE = 0x0,
    EFT_EXPLOSION_SMALL = 0x1,
    EFT_EXPLOSION_MEDIUM = 0x2,
    EFT_EXPLOSION_LARGE = 0x3,
    EFT_EXPLOSION_DEBRIS = 0x4,
    EFT_EXPLOSION_SHIP = 0x5,
    EFT_EXPLOSION_MISSILE = 0x6,
    EFT_EXPLOSION_MINE = 0x7,
    EFT_MUZZLE_FLASH = 0x8,
    EFT_MUZZLE_LARGE_FLASH = 0x9,
    EFT_MISSILE_DRIVE = 0xa,
    EFT_WEAPON_PROJ = 0xb,
    EFT_WEAPON_LARGE_PROJ = 0xc,
    EFT_WEAPON_IMPACT = 0xd,
    EFT_WEAPON_LARGE_IMPACT = 0xe,
    EFT_WEAPON_COUNTER_MEASURE = 0xf,
    EFT_WEAPON_MINE = 0x10,
    EFT_DAMAGE_HULL = 0x11,
    EFT_DAMAGE_LARGE_SHIELD = 0x12,
    EFT_DAMAGE_SHIELD = 0x13,
    EFT_DAMAGE_CRUISE = 0x14,
    EFT_DAMAGE_SMOKE = 0x15,
    EFT_DAMAGE_LARGE_SMOKE = 0x16,
    EFT_DEBRIS_TRAIL = 0x17,
    EFT_ENGINE_CONTRAIL = 0x18,
    EFT_ENGINE_TRAIL = 0x19,
    EFT_ENGINE_LARGE_TRAIL = 0x1a,
    EFT_ENGINE_FIRE = 0x1b,
    EFT_ENGINE_LARGE_FIRE = 0x1c,
    EFT_ENGINE_THRUSTER = 0x1d,
    EFT_EQUIP_CLOAK = 0x1e,
    EFT_EQUIP_LARGE_CLOAK = 0x1f,
    EFT_EQUIP_ATTACHED = 0x20,
    EFT_EQUIP_ATTACHED_LARGE = 0x21,
    EFT_EQUIP_ATTACHED_INSANELY_LARGE = 0x22,
    EFT_MISC_DUST = 0x23,
    EFT_MOTION_DUST = 0x24,
    EFT_JUMP_EFFECTS = 0x25,
    EFT_JUMP_GATE_EFFECTS = 0x26,
    EFT_LANE_ACTIVE = 0x27,
    EFT_LANE_TUBE = 0x28,
    EFT_LANE_SHIP = 0x29,
    EFT_LANE_SHIP_TRAVEL = 0x2a,
    EFT_CUSTOM_EFFECTS = 0x2b,
    EFT_CODE_PROJ_EFFECT = 0x2c,
    EFT_TRACTORBEAM = 0x2d,
    EFT_FLASHLIGHT = 0x2e,
    EFT_NUMTYPES = 0x2f,
};

constexpr int kEffectTypeCount = EFT_NUMTYPES; // 0..0x2e; 0x2f = unknown
constexpr int kEffectLodTypeCount = 7; // 0=none .. 6=small_distant (names FUN_0042dae0 matches)
// DAT_00667d90 holds 8 rows: the static initializer LAB_0042dac0 seeds 8 x (-1,-1,-1); only
// rows 1..6 are ever filled by FUN_0042dae0.
constexpr int kEffectLodTableRows = 8;

struct FEffectTypeInfo
{
    char const* Nickname;     // +0x00  "EFT_*"
    float Priority;           // +0x04
    float GenericPriority;    // +0x08
    int LodType;              // +0x0C  index into EFT_LOD_* / EffectLOD row
    float Radius;             // +0x10
    unsigned char VisibilityOffscreen; // +0x14  EXIST_OFFSCREEN
    unsigned char UpdateOffscreen;     // +0x15  UPDATE_OFFSCREEN
    unsigned char Pad16[2];
    float RunTime;            // +0x18  <0 → EffectArch flags bit0
    float Fps;                // +0x1C
    float PbubbleNear;        // +0x20
    float PbubbleFar;         // +0x24
};
static_assert(sizeof(FEffectTypeInfo) == 0x28, "EffectTypeInfo 0x28");

// One EffectLOD row: max_lod_screen_size / min_lod_screen_size / min_screen_size.
struct FEffectLodSizes
{
    float MaxLodScreenSize;
    float MinLodScreenSize;
    float MinScreenSize;
};

extern FEffectTypeInfo G_EffectTypeTable[kEffectTypeCount];
extern FEffectLodSizes G_EffectLodTable[kEffectLodTableRows];

// FUN_004fd500 — name → index; returns kEffectTypeCount (0x2f) if unknown.
int EffectTypeIndexFromName(char const* Nickname);

// FUN_004fd560 — index → row (0x00612a68 + Index * 0x28), no range test. It never returns null;
// the null tests at its call sites (0x0040e3b8, 0x004f81f3, 0x004f820b, 0x004f9629) are retail's.
FEffectTypeInfo* EffectTypeInfoByIndex(int Index);

// FUN_004fd570 -- the perf level (DAT_006129c8) is at or below the type's Priority. No range
// test on Index, as retail. Caller: the splash-hit one-shot FUN_0053a230 (0x0053a268).
bool EffectTypeWithinPerf(int Index);

// FUN_004fd5a0 — parse one [EffectType] section into G_EffectTypeTable.
bool LoadEffectTypeSection(class INI_Reader* Reader);

// FUN_0042dae0 — parse one [EffectLOD] section into G_EffectLodTable.
bool LoadEffectLodSection(class INI_Reader* Reader);

struct ICamera;

// FUN_0042da00 (on the row) — screen size = Radius / Distance * ZNear *
// XScaleOverNear (camera slots +0x20 / +0x2c); at or below min_screen_size -> *Out = 0, false;
// else the blend (size - min_lod) / (max_lod - min_lod) clamped to [0, 1], true. A distance
// below 1e-5 (double DAT_005c75f0) gives 1.0 without asking the camera.
bool EffectLodRowBlend(FEffectLodSizes const* Row, ICamera* Cam, float Distance, float Radius,
                       float* OutBlend);

// FUN_0042dcd0 — the type's lod_type 1..6 selects the G_EffectLodTable row for
// FUN_0042da00 with the type's radius; any other type (or no row) gives *Out = 1.0, true.
// Caller: the particle visibility test FUN_004f86c0 (0x004f897f) under ALCHEMY_LOD.
bool EffectTypeLodBlend(int TypeIndex, ICamera* Cam, float Distance, float* OutBlend);

