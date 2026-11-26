// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// pub::AI namespace types and interfaces: the contract layer used across DLL boundaries
// (Content/Server/RPC).

#include "common_ini.h" // Vector
#include "common_csys.h" // Matrix (DirectiveFaceOp)
#include <cstddef>
#include <cstdint>

typedef unsigned int uint;

// FORMATION_RTYPE is a global enum in retail (mangled without scope, e.g.
// ?add_formation_follower@CShip@@QAE?AW4FORMATION_RTYPE@@PAUIObjRW@@@Z). Enumerator names
// are the port's; the values are those CShip::add/remove_formation_follower return.
enum FORMATION_RTYPE
{
    FORMATION_RTYPE_OK = 0,
    FORMATION_RTYPE_SELF = -1,
    FORMATION_RTYPE_NOT_LEADER = -2,
    FORMATION_RTYPE_NOT_FOUND = -3
};

// DirectiveGotoOp::GotoKind (+0x0C), mirrored into GotoBehavior::GotoKind (+0x2f4):
// GotoBehavior::OnApplyDirective (LAB_062fe670) switches on it; Content's [ObjList] Goto
// directives write object / point / spline. The ctor leaves 3, which no directive carries.
enum EGotoKind
{
    kGotoKindObject = 0,   // watch the object DirectiveGotoOp::TargetId names
    kGotoKindPoint = 1,    // fly to DirectiveGotoOp::Destination
    kGotoKindSpline = 2,   // follow the spline of DirectiveGotoOp::SplineP0..P3
    kGotoKindNone = 3,     // the ctor value; OnApplyDirective rejects it
};

// pub::AI::Personality::FormationUseStruct::FormationExitMode (+0x44), the [FormationBlock]
// formation_exit_mode keyword Content's pilot-personality loader maps (default 5);
// BaseFollowerBehavior::UpdateTickPayload switches on it.
enum EFormationExitMode
{
    kFormationExitBreakAwayFromCenter = 0,             // BREAK_AWAY_FROM_CENTER
    kFormationExitBreakAwayFromCenterAfterburner = 1,  // BREAK_AWAY_FROM_CENTER_AFTERBURNER
    kFormationExitBrakeReverse = 2,                    // BRAKE_REVERSE
    kFormationExitOutrun = 3,                          // OUTRUN
    kFormationExitNothing = 5,                         // NOTHING
};

namespace pub { namespace AI {

// ============================================================================
// Enums
// ============================================================================

// OP_TYPE: the literal each BaseOp-derived default ctor stores at +4 (e.g.
// DirectiveGotoOp::DirectiveGotoOp @ 0x7AE20 sets 3); 0-21, no gaps.
enum OP_TYPE
{
    OpType_SetPersonalityParams          = 0,
    OpType_SetZoneBehaviorParams         = 1,
    OpType_SetFlagParams                 = 2,
    OpType_DirectiveGotoOp                = 3,
    OpType_DirectiveFollowOp              = 4,
    OpType_DirectiveTrailOp               = 5,
    OpType_DirectiveStrafeOp              = 6,
    OpType_DirectiveFaceOp                = 7,
    OpType_DirectiveRamOp                 = 8,
    OpType_DirectiveTractorBeamOp         = 9,
    OpType_DirectiveEvadeOp               = 10,
    OpType_DirectiveDrasticEvadeOp        = 11,
    OpType_DirectiveDelayOp               = 12,
    OpType_DirectiveDockOp                = 13,
    OpType_DirectiveLaunchOp              = 14,
    OpType_DirectiveInstantTradelaneOp    = 15,
    OpType_DirectiveFormationOp           = 16,
    OpType_DirectiveGuideOp               = 17,
    OpType_DirectiveWaitForShip           = 18,
    OpType_DirectiveWaitForPlayerManeuverOp = 19,
    OpType_DirectiveIdleOp                = 20,
    OpType_DirectiveCancelOp              = 21,
};

// OP_RTYPE: enumerator names are the port's, from the retail return sites (the values are
// consumed only by Content.dll):
//   0  Server SubmitState: personality applied / user zone cleared (mode 2).
//   1  Server SubmitDirective: directive accepted (forwarded to the player's client or set
//      on the AI); Common IBehaviorManager::set_directive_priority (0x062d3e80): priority
//      stored on the current directive. Not a failure.
//   2  Server SubmitDirective: the AI already has a current directive.
//   3  Common set_directive_priority: there is no current directive to change.
//   4  bad id in both Server functions, and every SubmitState zone / flag / unknown-op
//      exit -- mixed meaning, so unnamed.
//   5  SubmitDirective: op failed validate(), no object, not a CSimple, or no behavior
//      for this op.
enum OP_RTYPE
{
    OpRTypeSuccess = 0,
    OpRTypeAccepted = 1,
    OpRTypeDirectiveBusy = 2,
    OpRTypeNoDirective = 3,
    OpRTypeInvalid = 5,
};

enum ScanResponse
{
    ScanRespNone = 0,
    ScanRespFriendly = 1,
    ScanRespHostile = 2,
    ScanRespNeutral = 3,
};

enum DirectivePriority
{
    DirPriNone = 0,
    DirPriLow = 1,
    DirPriNormal = 2,
    DirPriHigh = 3,
    DirPriCritical = 4,
};

// The eight ContentCallback enums (CSNType, FollowStatusType, ...) are nested in
// pub::AI::ContentCallback below: retail mangles them `W4CSNType@123@`, i.e.
// CSNType in ContentCallback in AI in pub.

// ============================================================================
// Forward declarations for nested Personality structs
// ============================================================================

class BaseOp;
struct ContentCallback;
struct DirectiveCallback;

// ============================================================================
// GSplinePath — spline path for AI navigation
// ============================================================================

// Retail exports only two members, both ICF-folded:
//   ctor      .text:06261260  `MOV EAX,this; RET` -- initialises nothing.
//   operator= .text:06274100  shared with Csys::operator=: a flat 12-dword copy, so size 0x30.
// No retail module imports either, so the field layout is not recovered.
struct COMMON_API GSplinePath
{
    uint32_t Opaque[12];   // +0x00 .. +0x2F  meaning not recovered

    GSplinePath();
    GSplinePath& operator=(GSplinePath const& Other);
};

static_assert(sizeof(GSplinePath) == 0x30, "GSplinePath size (operator= copies 0xC dwords)");

// ============================================================================
// Personality — AI personality configuration
// ============================================================================

// Personality — Common.dll.c 0x7AA30. Exactly these 16 sub-structs back to back, no gaps
// and no top-level scalars: operator= (0x488B0) flat-copies 0xF6 dwords = 0x3D8 bytes.
// Several sub-structs are inlined into the outer ctor; their offsets were matched against
// their standalone ctors. A class in retail (mangled `V`); its nested *Struct members are
// structs. No copy ctor is exported (implicit bitwise copy); operator= is.
class COMMON_API Personality
{
public:
    // Nested in retail: get_toughness_ratio takes `W4TOUGHNESS_TYPE@123@`. Named after the
    // EASIEST..HARDEST tokens Content's [JobBlock] loader maps to 0..4.
    enum TOUGHNESS_TYPE
    {
        ToughnessEasiest = 0,
        ToughnessEasy = 1,
        ToughnessEqual = 2,
        ToughnessHard = 3,
        ToughnessHardest = 4,
    };

    // Field names of the *UseStruct / *ReactionStruct members are the [<Name>Block] keys
    // Content's pilot-personality loaders (FUN_06f3a2d0 .. FUN_06f3bb00) store at each
    // offset; the second column is the ctor default. Angles are radians (the loader
    // multiplies INI degrees by GDegToRad) unless noted. "*Weight<Dir>" fields are the
    // per-name weights of one "<key>, <NAME>, <weight>" key.

    // EvadeDodgeUseStruct — Common.dll.c 0x7A5B0; [EvadeDodgeBlock] (Content 06f3a2d0)
    struct COMMON_API EvadeDodgeUseStruct
    {
        float   EvadeActivateRange;                      // +0x00  500.0
        float   EvadeDodgeStyleWeightWaggle;             // +0x04  0.0
        float   EvadeDodgeStyleWeightWaggleRandom;       // +0x08  0.0
        float   EvadeDodgeStyleWeightSlide;              // +0x0C  0.0
        float   EvadeDodgeStyleWeightCorkscrew;          // +0x10  0.0
        float   EvadeDodgeConeAngle;                     // +0x14  pi/4
        float   EvadeDodgeConeAngleVariancePercent;      // +0x18  0.0
        float   EvadeDodgeWaggleAxisConeAngle;           // +0x1C  0.0
        float   EvadeDodgeRollAngle;                     // +0x20  0.0
        float   EvadeDodgeIntervalTime;                  // +0x24  1.0
        float   EvadeDodgeIntervalTimeVariancePercent;   // +0x28  0.0
        float   EvadeDodgeDistance;                      // +0x2C  100.0
        float   EvadeDodgeTime;                          // +0x30  10.0
        float   EvadeDodgeSlideThrottle;                 // +0x34  0.6
        float   EvadeDodgeTurnThrottle;                  // +0x38  0.6
        float   EvadeDodgeCorkscrewTurnThrottle;         // +0x3C  0.15
        float   EvadeDodgeCorkscrewRollThrottle;         // +0x40  1.0
        uint8_t bEvadeDodgeCorkscrewRollFlipDirection;   // +0x44  0
        // +0x45..+0x47 alignment padding: the ctor never writes it, operator= copies it.
        float   EvadeDodgeDirectionWeightLeft;           // +0x48  0.0
        float   EvadeDodgeDirectionWeightRight;          // +0x4C  0.0
        float   EvadeDodgeDirectionWeightUp;             // +0x50  0.0
        float   EvadeDodgeDirectionWeightDown;           // +0x54  0.0

        EvadeDodgeUseStruct();
        EvadeDodgeUseStruct& operator=(EvadeDodgeUseStruct const& Other);
    };

    static_assert(offsetof(EvadeDodgeUseStruct, bEvadeDodgeCorkscrewRollFlipDirection) == 0x44, "EvadeDodgeUseStruct +0x44");
    static_assert(offsetof(EvadeDodgeUseStruct, EvadeDodgeDirectionWeightDown) == 0x54, "EvadeDodgeUseStruct +0x54");
    static_assert(sizeof(EvadeDodgeUseStruct) == 0x58, "EvadeDodgeUseStruct size");

    // EvadeBreakUseStruct — Common.dll.c 0x7A620; also inlined at Personality +0x58;
    // [EvadeBreakBlock] (Content 06f3a690)
    struct COMMON_API EvadeBreakUseStruct
    {
        float EvadeBreakTime;                             // +0x00  3.0
        float EvadeBreakIntervalTime;                     // +0x04  100000.0
        float EvadeBreakAfterburnerDelay;                 // +0x08  0.0
        float EvadeBreakAfterburnerDelayVariancePercent;  // +0x0C  0.0
        float EvadeBreakDirectionWeightLeft;              // +0x10  0.0
        float EvadeBreakDirectionWeightRight;             // +0x14  0.0
        float EvadeBreakDirectionWeightUp;                // +0x18  0.0
        float EvadeBreakDirectionWeightDown;              // +0x1C  0.0
        float EvadeBreakRollThrottle;                     // +0x20  0.0
        float EvadeBreakTurnThrottle;                     // +0x24  1.0
        float EvadeBreakStyleWeightSideways;              // +0x28  0.0
        float EvadeBreakStyleWeightOutrun;                // +0x2C  0.0
        float EvadeBreakStyleWeightReverse;               // +0x30  0.0
        float EvadeBreakAttemptReverseTime;               // +0x34  4.0
        float EvadeBreakReverseDistance;                  // +0x38  150.0

        EvadeBreakUseStruct();
        EvadeBreakUseStruct& operator=(EvadeBreakUseStruct const& Other);
    };

    static_assert(sizeof(EvadeBreakUseStruct) == 0x3C, "EvadeBreakUseStruct size");

    // BuzzHeadTowardUseStruct — Common.dll.c 0x7A670; also inlined at Personality +0x94;
    // [BuzzHeadTowardBlock] (Content 06f3a940)
    struct COMMON_API BuzzHeadTowardUseStruct
    {
        float   BuzzMaxTimeToHeadAway;                       // +0x00  4.0
        float   BuzzHeadTowardStyleWeightStraightTo;         // +0x04  0.0
        float   BuzzHeadTowardStyleWeightSlide;              // +0x08  0.0
        float   BuzzHeadTowardStyleWeightWaggle;             // +0x0C  0.0
        float   BuzzMinDistanceToHeadToward;                 // +0x10  200.0
        float   BuzzMinDistanceToHeadTowardVariancePercent;  // +0x14  1.0
        float   BuzzHeadTowardEngineThrottle;                // +0x18  1.0
        float   BuzzHeadTowardTurnThrottle;                  // +0x1C  1.0
        float   BuzzHeadTowardRollThrottle;                  // +0x20  0.0
        uint8_t bBuzzHeadTowardRollFlipDirection;            // +0x24  0
        // +0x25..+0x27 alignment padding
        float   BuzzDodgeDirectionWeightLeft;                // +0x28  0.0
        float   BuzzDodgeDirectionWeightRight;               // +0x2C  0.0
        float   BuzzDodgeDirectionWeightUp;                  // +0x30  0.0
        float   BuzzDodgeDirectionWeightDown;                // +0x34  0.0
        float   BuzzDodgeTurnThrottle;                       // +0x38  1.0
        float   BuzzDodgeConeAngle;                          // +0x3C  pi/4
        float   BuzzDodgeConeAngleVariancePercent;           // +0x40  0.0
        float   BuzzDodgeWaggleAxisConeAngle;                // +0x44  0.0
        float   BuzzDodgeRollAngle;                          // +0x48  0.0
        float   BuzzDodgeIntervalTime;                       // +0x4C  2.0
        float   BuzzDodgeIntervalTimeVariancePercent;        // +0x50  0.0
        float   BuzzSlideThrottle;                           // +0x54  1.0
        float   BuzzSlideIntervalTime;                       // +0x58  2.0
        float   BuzzSlideIntervalTimeVariancePercent;        // +0x5C  0.4

        BuzzHeadTowardUseStruct();
        BuzzHeadTowardUseStruct& operator=(BuzzHeadTowardUseStruct const& Other);
    };

    static_assert(offsetof(BuzzHeadTowardUseStruct, bBuzzHeadTowardRollFlipDirection) == 0x24, "BuzzHeadTowardUseStruct +0x24");
    static_assert(sizeof(BuzzHeadTowardUseStruct) == 0x60, "BuzzHeadTowardUseStruct size");

    // BuzzPassByUseStruct — Common.dll.c 0x7A6E0; also inlined at Personality +0xF4;
    // [BuzzPassByBlock] (Content 06f3ad40)
    struct COMMON_API BuzzPassByUseStruct
    {
        float   BuzzPassByStyleWeightStraightBy;   // +0x00  0.0
        float   BuzzPassByStyleWeightBreakAway;    // +0x04  0.0
        float   BuzzPassByStyleWeightEngineKill;   // +0x08  0.0
        float   BuzzDistanceToPassBy;              // +0x0C  100.0
        float   BuzzPassByTime;                    // +0x10  3.0
        uint8_t bBuzzDropBombOnPassBy;             // +0x14  0
        // +0x15..+0x17 alignment padding
        float   BuzzBreakDirectionWeightLeft;      // +0x18  0.0
        float   BuzzBreakDirectionWeightRight;     // +0x1C  0.0
        float   BuzzBreakDirectionWeightUp;        // +0x20  0.0
        float   BuzzBreakDirectionWeightDown;      // +0x24  0.0
        float   BuzzBreakDirectionConeAngle;       // +0x28  0.0    INI value as is (no GDegToRad)
        float   BuzzBreakTurnThrottle;             // +0x2C  1.0
        float   BuzzPassByRollThrottle;            // +0x30  0.0

        BuzzPassByUseStruct();
        BuzzPassByUseStruct& operator=(BuzzPassByUseStruct const& Other);
    };

    static_assert(offsetof(BuzzPassByUseStruct, bBuzzDropBombOnPassBy) == 0x14, "BuzzPassByUseStruct +0x14");
    static_assert(sizeof(BuzzPassByUseStruct) == 0x34, "BuzzPassByUseStruct size");

    // TrailUseStruct — Common.dll.c 0x7A720; also inlined at Personality +0x128;
    // [TrailBlock] (Content 06f3b030)
    struct COMMON_API TrailUseStruct
    {
        float   TrailLockConeAngle;       // +0x00  pi/6
        float   TrailBreakTime;           // +0x04  2.5
        float   TrailMinNoLockTime;       // +0x08  4.0
        float   TrailBreakRollThrottle;   // +0x0C  0.0
        uint8_t bTrailBreakAfterburner;   // +0x10  0
        // +0x11..+0x13 alignment padding
        float   TrailMaxTurnThrottle;     // +0x14  1.0
        float   TrailDistance;            // +0x18  150.0

        TrailUseStruct();
        TrailUseStruct& operator=(TrailUseStruct const& Other);
    };

    static_assert(offsetof(TrailUseStruct, bTrailBreakAfterburner) == 0x10, "TrailUseStruct +0x10");
    static_assert(sizeof(TrailUseStruct) == 0x1C, "TrailUseStruct size");

    // StrafeUseStruct — Common.dll.c 0x7A750; also inlined at Personality +0x144;
    // [StrafeBlock] (Content 06f3b190)
    struct COMMON_API StrafeUseStruct
    {
        float StrafeRunAwayDistance;   // +0x00  150.0
        float StrafeAttackThrottle;    // +0x04  1.0
        float StrafeTurnThrottle;      // +0x08  1.0

        StrafeUseStruct();
        StrafeUseStruct& operator=(StrafeUseStruct const& Other);
    };

    static_assert(sizeof(StrafeUseStruct) == 0x0C, "StrafeUseStruct size");

    // EngineKillUseStruct — Common.dll.c 0x7A770; also inlined at Personality +0x150;
    // [EngineKillBlock] (Content 06f3b240)
    struct COMMON_API EngineKillUseStruct
    {
        float EngineKillSearchTime;          // +0x00  6.0
        float EngineKillFaceTime;            // +0x04  3.0
        float EngineKillUseAfterburner;      // +0x08  0.0    TRUE/FALSE stored as 1.0f/0.0f (FILD)
        float EngineKillAfterburnerTime;     // +0x0C  0.5
        float EngineKillMaxTargetDistance;   // +0x10  700.0

        EngineKillUseStruct();
        EngineKillUseStruct& operator=(EngineKillUseStruct const& Other);
    };

    static_assert(sizeof(EngineKillUseStruct) == 0x14, "EngineKillUseStruct size");

    // RepairUseStruct — Common.dll.c 0x7A7A0; also inlined at Personality +0x164;
    // [RepairBlock] (Content 06f3b370)
    struct COMMON_API RepairUseStruct
    {
        float UseShieldRepairPreDelay;          // +0x00  2.0
        float UseShieldRepairAtDamagePercent;   // +0x04  0.5
        float UseShieldRepairPostDelay;         // +0x08  3.0
        float UseHullRepairPreDelay;            // +0x0C  1.0
        float UseHullRepairAtDamagePercent;     // +0x10  0.5
        float UseHullRepairPostDelay;           // +0x14  1.0

        RepairUseStruct();
        RepairUseStruct& operator=(RepairUseStruct const& Other);
    };

    static_assert(sizeof(RepairUseStruct) == 0x18, "RepairUseStruct size");

    // GunUseStruct — Common.dll.c 0x7A7D0; also inlined at Personality +0x17C;
    // [GunBlock] (Content 06f3b470)
    struct COMMON_API GunUseStruct
    {
        // The values of FireStyle, the "fire_style" keywords.
        enum EFireStyle
        {
            kFireStyleMultiple = 0,
            kFireStyleSingle = 1,
        };
        int32_t FireStyle;                                // +0x00  0    "fire_style": 0 MULTIPLE, 1 anything else
        float   GunFireIntervalTime;                      // +0x04  0.0
        float   GunFireIntervalVariancePercent;           // +0x08  0.5
        float   GunFireBurstIntervalTime;                 // +0x0C  1.5
        float   GunFireBurstIntervalVariancePercent;      // +0x10  0.1
        float   GunFireNoBurstIntervalTime;               // +0x14  0.1
        float   GunFireAccuracyConeAngle;                 // +0x18  0.0
        float   GunFireAccuracyPower;                     // +0x1C  1.0
        float   GunFireAccuracyPowerNpc;                  // +0x20  6.0
        float   GunRangeThreshold;                        // +0x24  1.0
        float   GunRangeThresholdVariancePercent;         // +0x28  0.2
        float   GunTargetPointSwitchTime;                 // +0x2C  1.5
        float   AutoTurretIntervalTime;                   // +0x30  0.0
        float   AutoTurretBurstIntervalTime;              // +0x34  1.0
        float   AutoTurretNoBurstIntervalTime;            // +0x38  0.0
        float   AutoTurretBurstIntervalVariancePercent;   // +0x3C  0.1

        GunUseStruct();
        GunUseStruct& operator=(GunUseStruct const& Other);
    };

    static_assert(sizeof(GunUseStruct) == 0x40, "GunUseStruct size");

    // MineUseStruct — Common.dll.c 0x7A830; also inlined at Personality +0x1BC;
    // [MineBlock] (Content 06f3b6c0)
    struct COMMON_API MineUseStruct
    {
        float MineLaunchInterval;    // +0x00  0.0
        float MineLaunchConeAngle;   // +0x04  pi/8
        float MineLaunchRange;       // +0x08  400.0

        MineUseStruct();
        MineUseStruct& operator=(MineUseStruct const& Other);
    };

    static_assert(sizeof(MineUseStruct) == 0x0C, "MineUseStruct size");

    // MissileUseStruct — Common.dll.c 0x7A850; also inlined at Personality +0x1C8;
    // [MissileBlock] (Content 06f3b770)
    struct COMMON_API MissileUseStruct
    {
        float   MissileLaunchRange;                     // +0x00  -1.0
        uint8_t bMissileLaunchAllowOutOfRange;          // +0x04  0
        // +0x05..+0x07 alignment padding
        float   MissileLaunchIntervalTime;              // +0x08  5.0
        float   MissileLaunchIntervalVariancePercent;   // +0x0C  0.4
        float   MissileLaunchConeAngle;                 // +0x10  pi/8
        float   AntiCruiseMissileMinDistance;           // +0x14  150.0
        float   AntiCruiseMissileMaxDistance;           // +0x18  2500.0
        float   AntiCruiseMissilePreFireDelay;          // +0x1C  2.0
        float   AntiCruiseMissileIntervalTime;          // +0x20  5.0

        MissileUseStruct();
        MissileUseStruct& operator=(MissileUseStruct const& Other);
    };

    static_assert(offsetof(MissileUseStruct, bMissileLaunchAllowOutOfRange) == 0x04, "MissileUseStruct +0x04");
    static_assert(sizeof(MissileUseStruct) == 0x24, "MissileUseStruct size");

    // DamageReactionStruct — Common.dll.c 0x7A890; also inlined at Personality +0x1EC;
    // [DamageReactionBlock] (Content 06f3b900). operator= (0x48850) copies 0x11 dwords but
    // the retail ctor writes only the first 15, leaving +0x3C/+0x40 uninitialized; the port
    // zero-initializes them. No loader key writes them (TODO: meaning).
    struct COMMON_API DamageReactionStruct
    {
        float EvadeBreakDamageTriggerPercent;       // +0x00  1.0
        float EvadeDodgeMoreDamageTriggerPercent;   // +0x04  0.2
        float DropMinesDamageTriggerPercent;        // +0x08  1.0
        float DropMinesDamageTriggerTime;           // +0x0C  1.0
        float EngineKillFaceDamageTriggerPercent;   // +0x10  1.0
        float EngineKillFaceDamageTriggerTime;      // +0x14  2.0
        float RollDamageTriggerPercent;             // +0x18  1.0
        float RollDamageTriggerTime;                // +0x1C  1.0
        float AfterburnerDamageTriggerPercent;      // +0x20  1.0
        float AfterburnerDamageTriggerTime;         // +0x24  1.5
        float BrakeReverseDamageTriggerPercent;     // +0x28  1.0
        float FireMissilesDamageTriggerPercent;     // +0x2C  1.0
        float FireMissilesDamageTriggerTime;        // +0x30  0.5
        float FireGunsDamageTriggerPercent;         // +0x34  1.0
        float FireGunsDamageTriggerTime;            // +0x38  1.0
        float Field_3C;   // +0x3C  not written by retail ctor
        float Field_40;   // +0x40  not written by retail ctor

        DamageReactionStruct();
        DamageReactionStruct& operator=(DamageReactionStruct const& Other);
    };

    static_assert(sizeof(DamageReactionStruct) == 0x44, "DamageReactionStruct size");

    // MissileReactionStruct — Common.dll.c 0x7A8E0; also inlined at Personality +0x230;
    // [MissileReactionBlock] (Content 06f3bb00)
    struct COMMON_API MissileReactionStruct
    {
        float EvadeBreakMissileReactionTime;       // +0x00  0.0
        float EvadeSlideMissileReactionTime;       // +0x04  0.0
        float EvadeAfterburnMissileReactionTime;   // +0x08  0.0
        float EvadeMissileDistance;                // +0x0C  250.0

        MissileReactionStruct();
        MissileReactionStruct& operator=(MissileReactionStruct const& Other);
    };

    static_assert(sizeof(MissileReactionStruct) == 0x10, "MissileReactionStruct size");

    // CountermeasureUseStruct — Common.dll.c 0x7A900; also inlined at Personality +0x240;
    // [CountermeasureBlock] (read inline by Content at 06f3db9e / 06f3dbb9)
    struct COMMON_API CountermeasureUseStruct
    {
        float CountermeasureActiveTime;     // +0x00  1.0
        float CountermeasureUnactiveTime;   // +0x04  0.0

        CountermeasureUseStruct();
        CountermeasureUseStruct& operator=(CountermeasureUseStruct const& Other);
    };

    static_assert(sizeof(CountermeasureUseStruct) == 0x08, "CountermeasureUseStruct size");

    // FormationUseStruct — ctor .text:062da910; also constructed at Personality +0x248.
    // [FormationBlock] (Content FUN_06f3bbd0).
    struct COMMON_API FormationUseStruct
    {
        float    ForceAttackFormationActiveTime;            // +0x00  1.0
        float    ForceAttackFormationUnactiveTime;          // +0x04  0.0
        float    BreakFormationDamageTriggerPercent;        // +0x08  1.0
        float    BreakFormationDamageTriggerTime;           // +0x0C  4.0
        float    BreakApartFormationDamageTriggerPercent;   // +0x10  1.0
        float    BreakApartFormationDamageTriggerTime;      // +0x14  4.0
        float    BreakFormationMissileReactionTime;         // +0x18  0.0
        float    BreakApartFormationMissileReactionTime;    // +0x1C  4.0
        // The bytes between the uint8_t fields and the next float are alignment
        // padding: the ctor never writes them, operator= copies them.
        uint8_t  bBreakApartFormationOnBuzzHeadToward;      // +0x20  0
        float    BreakFormationOnBuzzHeadTowardTime;        // +0x24  4.0
        uint8_t  bRegroupFormationOnBuzzHeadToward;         // +0x28  0
        uint8_t  bBreakApartFormationOnBuzzPassBy;          // +0x29  0
        float    BreakFormationOnBuzzPassByTime;            // +0x2C  4.0
        uint8_t  bRegroupFormationOnBuzzPassBy;             // +0x30  0
        uint8_t  bBreakApartFormationOnEvadeDodge;          // +0x31  0
        float    BreakFormationOnEvadeDodgeTime;            // +0x34  4.0
        uint8_t  bRegroupFormationOnEvadeDodge;             // +0x38  0
        uint8_t  bBreakApartFormationOnEvadeBreak;          // +0x39  0
        float    BreakFormationOnEvadeBreakTime;            // +0x3C  4.0
        uint8_t  bRegroupFormationOnEvadeBreak;             // +0x40  0
        // 0 BREAK_AWAY_FROM_CENTER, 1 ..._AFTERBURNER, 2 BRAKE_REVERSE, 3 OUTRUN, 5 NOTHING.
        int32_t  FormationExitMode;                         // +0x44  5
        float    FormationExitTopTurnBreakAwayThrottle;     // +0x48  1.0
        float    FormationExitRollOutrunThrottle;           // +0x4C  0
        float    FormationExitMaxTime;                      // +0x50  0
        uint8_t  bLeaderMakesMeTougher;                     // +0x54  0

        FormationUseStruct();
        FormationUseStruct& operator=(FormationUseStruct const& Other);
    };

    static_assert(offsetof(FormationUseStruct, FormationExitMode) == 0x44, "FormationUseStruct +0x44");
    static_assert(offsetof(FormationUseStruct, bLeaderMakesMeTougher) == 0x54, "FormationUseStruct +0x54");
    static_assert(sizeof(FormationUseStruct) == 0x58, "FormationUseStruct size");

    // JobStruct — ctor .text:062da980; also constructed at Personality +0x2A0; operator=
    // .text:062a8890 copies 0x4E dwords. [JobBlock] (Content FUN_06f3c190). The toughness
    // fields hold 0..4 (EASIEST..HARDEST, unknown text -> 2).
    struct COMMON_API JobStruct
    {
        // The bits of LootPreference, the "loot_preference" keywords LT_COMMODITIES /
        // LT_EQUIPMENT / LT_POTIONS (LT_ALL sets all three, LT_NONE none).
        enum ELootPreference
        {
            kLootPrefCommodities = 1,
            kLootPrefEquipment = 2,
            kLootPrefPotions = 4,
            kLootPrefAll = 7,
        };

        // The values of FAttackPreference::ShipClass, named as the [JobBlock] class keywords
        // (Content's table): 12 ends the record list.
        enum EAttackClass
        {
            kAttackClassFighter = 0,
            kAttackClassFreighter = 1,
            kAttackClassTransport = 2,
            kAttackClassGunboat = 3,
            kAttackClassCruiser = 4,
            kAttackClassCapital = 5,
            kAttackClassTradelane = 6,
            kAttackClassJumpgate = 7,
            kAttackClassWeaponsPlatform = 8,
            kAttackClassDestroyableDepot = 9,
            kAttackClassSolar = 10,
            kAttackClassAnything = 11,
            kAttackClassEnd = 12,
        };

        // FAttackPreference::WeaponFlags, the weapon keywords of "attack_preference".
        enum EAttackWeapon
        {
            kAttackWeaponGuns = 1,
            kAttackWeaponGuided = 2,
            kAttackWeaponUnguided = 4,
            kAttackWeaponTorpedo = 8,
        };

        // AttackSubtargetOrder entries, named as the "attack_subtarget_order" keywords; 7 ends the
        // list.
        enum EAttackSubtarget
        {
            kAttackSubtargetGuns = 0,
            kAttackSubtargetTurrets = 1,
            kAttackSubtargetLaunchers = 2,
            kAttackSubtargetTowers = 3,
            kAttackSubtargetEngines = 4,
            kAttackSubtargetHull = 5,
            kAttackSubtargetAnything = 6,
            kAttackSubtargetEnd = 7,
        };
        // One "attack_preference" / "attack_order" record (Content 06f3c9d8): ship class
        // (11 = ANYTHING; 12 terminates the list), range and weapon-type mask (1 GUNS,
        // 2 GUIDED, 4 UNGUIDED, 8 TORPEDO). The type name is the port's.
        struct FAttackPreference
        {
            int32_t  ShipClass;   // +0x0
            float    Range;       // +0x4
            uint32_t WeaponFlags; // +0x8
        };

        // +0x000  0. The selector get_range_threshold_value (062a87e0) maps to a range: 1 ->
        // 3333.33, 2 -> 1666.67, else 5000; TargetManager applies the same 5000 x 2/3 / 1/3
        // tiers to the target proximity limit (Sys+0x1824). No [JobBlock] key writes it.
        int32_t  RangeThreshold;
        int32_t  SceneToughnessThreshold;   // +0x004  2
        int32_t  TargetToughnessPreference; // +0x008  2
        // +0x00C: TargetManager fires combat_drift_event once the owner is this far from its
        // attack anchor. Content recognises "combat_drift_distance" in [JobBlock] but never
        // stores it, so the ctor default always applies.
        float    CombatDriftDistance;       // +0x00C  10000.0
        // +0x010: 12 records plus the terminator record. The ctor writes record 0 =
        // {11, 5000.0, 0xF} and record 1's class = 12 only; the port zeroes the rest.
        FAttackPreference AttackPreferences[13]; // +0x010
        // +0x0AC: "attack_subtarget_order" (0 GUNS .. 5 HULL, 6 ANYTHING), terminated by 7.
        // The ctor writes {6, 7}; the port zeroes the rest.
        int32_t  AttackSubtargetOrder[8];   // +0x0AC
        uint8_t  bWaitForLeaderTarget;      // +0x0CC  never written by the ctor; the port zeroes it
        float    MaximumLeaderTargetDistance; // +0x0D0  5000.0
        int32_t  FieldTargeting;            // +0x0D4  3   NEVER/LOW_DENSITY/HIGH_DENSITY/ALWAYS
        int32_t  LootPreference;            // +0x0D8  0   ELootPreference mask
        int32_t  LootFleeThreshold;         // +0x0DC  2
        // +0x0E0 / +0x0E4: the faction's "scan_for_cargo" archetype ids (Content NpcShip
        // 06f33ae1); LootBehavior matches a commodity's good id against them.
        int32_t  ScanForCargoCount;         // +0x0E0  0
        uint32_t ScanForCargo[16];          // +0x0E4  never written by the ctor; the port zeroes it
        int32_t  FleeSceneThreatStyle;      // +0x124  3
        float    FleeWhenHullDamagedPercent; // +0x128 0
        uint8_t  bFleeWhenLeaderFleesStyle; // +0x12C  1
        uint8_t  bFleeNoWeaponsStyle;       // +0x12D  1
        uint8_t  bAllowPlayerTargeting;     // +0x12E  1
        // +0x130: seeded from SCAN_CHANCE_DEFAULT; Content's PatrolGuides compares it
        // against that sentinel before using it as the scan chance.
        float    ScanChance;                // +0x130  -1.0
        uint8_t  bForceAttackFormation;     // +0x134  0
        uint8_t  bForceAttackFormationSet;  // +0x135  0   set to 1 with the key

        JobStruct();
        JobStruct& operator=(JobStruct const& Other);
    };

    static_assert(sizeof(JobStruct::FAttackPreference) == 0x0C, "attack preference stride 0xC");
    static_assert(offsetof(JobStruct, AttackPreferences) == 0x10, "JobStruct +0x10");
    static_assert(offsetof(JobStruct, AttackSubtargetOrder) == 0xAC, "JobStruct +0xAC");
    static_assert(offsetof(JobStruct, bWaitForLeaderTarget) == 0xCC, "JobStruct +0xCC");
    static_assert(offsetof(JobStruct, MaximumLeaderTargetDistance) == 0xD0, "JobStruct +0xD0");
    static_assert(offsetof(JobStruct, ScanForCargo) == 0xE4, "JobStruct +0xE4");
    static_assert(offsetof(JobStruct, FleeSceneThreatStyle) == 0x124, "JobStruct +0x124");
    static_assert(offsetof(JobStruct, ScanChance) == 0x130, "JobStruct +0x130");
    static_assert(offsetof(JobStruct, bForceAttackFormationSet) == 0x135, "JobStruct +0x135");
    static_assert(sizeof(JobStruct) == 0x138, "JobStruct size");

    EvadeDodgeUseStruct     EvadeDodgeParams;    // +0x000  size 0x58
    EvadeBreakUseStruct     EvadeBreakParams;    // +0x058  size 0x3C
    BuzzHeadTowardUseStruct BuzzHeadParams;      // +0x094  size 0x60
    BuzzPassByUseStruct     BuzzPassParams;      // +0x0F4  size 0x34
    TrailUseStruct          TrailParams;         // +0x128  size 0x1C
    StrafeUseStruct         StrafeParams;        // +0x144  size 0x0C
    EngineKillUseStruct     EngineKillParams;    // +0x150  size 0x14
    RepairUseStruct         RepairParams;        // +0x164  size 0x18
    GunUseStruct            GunParams;           // +0x17C  size 0x40
    MineUseStruct           MineParams;          // +0x1BC  size 0x0C
    MissileUseStruct        MissileParams;       // +0x1C8  size 0x24
    DamageReactionStruct    DamageReaction;      // +0x1EC  size 0x44
    MissileReactionStruct   MissileReaction;     // +0x230  size 0x10
    CountermeasureUseStruct CMParams;            // +0x240  size 0x08
    FormationUseStruct      FormationParams;     // +0x248  size 0x58
    JobStruct               JobParams;           // +0x2A0  size 0x138

    Personality();
    Personality& operator=(Personality const& Other);

    // Common.dll.c 0x48780 / 0x487E0; both are hardcoded switches in retail.
    float get_toughness_ratio(TOUGHNESS_TYPE Type);
    // Deviation: returns the unrounded ST0 value as double (retail ?..@@QAEMXZ returns
    // float; no retail module calls it).
    double get_range_threshold_value();

    // Export ordinal 1947, .rdata 0x063a05a8 = -1.0f: the "not set" sentinel the JobStruct
    // ctor stores into JobStruct::ScanChance.
    static float const SCAN_CHANCE_DEFAULT;
};

static_assert(offsetof(Personality, EvadeBreakParams) == 0x058, "Personality +0x058");
static_assert(offsetof(Personality, BuzzHeadParams)   == 0x094, "Personality +0x094");
static_assert(offsetof(Personality, BuzzPassParams)   == 0x0F4, "Personality +0x0F4");
static_assert(offsetof(Personality, TrailParams)      == 0x128, "Personality +0x128");
static_assert(offsetof(Personality, StrafeParams)     == 0x144, "Personality +0x144");
static_assert(offsetof(Personality, EngineKillParams) == 0x150, "Personality +0x150");
static_assert(offsetof(Personality, RepairParams)     == 0x164, "Personality +0x164");
static_assert(offsetof(Personality, GunParams)        == 0x17C, "Personality +0x17C");
static_assert(offsetof(Personality, MineParams)       == 0x1BC, "Personality +0x1BC");
static_assert(offsetof(Personality, MissileParams)    == 0x1C8, "Personality +0x1C8");
static_assert(offsetof(Personality, DamageReaction)   == 0x1EC, "Personality +0x1EC");
static_assert(offsetof(Personality, MissileReaction)  == 0x230, "Personality +0x230");
static_assert(offsetof(Personality, CMParams)         == 0x240, "Personality +0x240");
static_assert(offsetof(Personality, FormationParams)  == 0x248, "Personality +0x248");
static_assert(offsetof(Personality, JobParams)        == 0x2A0, "Personality +0x2A0");
static_assert(sizeof(Personality) == 0x3D8, "Personality size");

// ============================================================================
// BaseOp — base class for directive operations
// ============================================================================

// Retail exports only the OP_TYPE ctor (.text:062a8740), the copy ctor (.text:062a8760)
// and the vftable (.rdata:0639e0d0), whose single slot is _purecall: validate() is pure and
// there is no virtual destructor. operator= is deleted so the compiler does not emit and
// export one (retail has none).
class COMMON_API BaseOp
{
public:
    BaseOp(OP_TYPE Type);
    BaseOp(BaseOp const& Other);
    BaseOp& operator=(BaseOp const& Other) = delete;

    virtual bool validate() = 0;   // vftable slot 0 (+0x00)

    OP_TYPE  OpType;               // +0x04  operation type
    // +0x08  0. The directive's priority (DirectivePriority values): Server.dll
    // pub::AI::SubmitDirective copies it into word 1 of the IDirectiveInfo it hands to
    // IBehaviorManager::set_current_directive -- the word set_directive_priority rewrites --
    // and Content's [ObjList] directives default a 0 to the ship's own priority.
    // A raw dword because every producer stores plain numbers.
    uint32_t Priority;
};

static_assert(offsetof(BaseOp, Priority) == 0x08, "BaseOp +0x08");
static_assert(sizeof(BaseOp) == 0x0C, "BaseOp size");

// ============================================================================
// Directive operations
// ============================================================================
//
// Layouts from each op's default ctor (Common.dll.c 0x7AE20-0x7B2A0), copy ctor
// (0x48A60-0x48EE0) and validate() (0x7AE60-0x7B360); the comment column is the ctor
// default. Unnamed fields are Field_XX by offset. Names come from the consumers
// (GotoBehavior / DockBehavior / GuideBehavior / ... OnApplyDirective), Content's [ObjList]
// parsers and Server's SubmitDirective.
//
// "Tether" fields: an optional object the ship keeps company with. Its id is watched by the
// behaviour's 0x1c-byte proximity watcher (ProxWatcherInit, FUN_062c29f0), which caps the
// speed by the near / far ranges and reports zone changes through
// ContentCallback::tether_object_event (Content's Cnd_TetherBroke). The cruise ratio is a
// float stored in an integer field (0 = derive it from the two ships' speeds).
//
// Retail exports per op: default ctor, copy ctor, vftable and the virtual validate(). The
// Directive* ops are classes, the three Set*Params structs. No operator= or destructor is
// exported, so operator= is deleted (see BaseOp).

class COMMON_API DirectiveGotoOp : public BaseOp
{
public:
    // +0x0C: 0 = fly to TargetId, 1 = to Destination, 2 = along the spline (Content's
    // GotoShip / GotoVec / GotoSpline); GotoBehavior::OnApplyDirective fails any other value.
    int32_t GotoKind;              // +0x0C  3
    Vector  Destination;           // +0x10  0,0,0 (validate rejects NaN components)
    int32_t TargetId;              // +0x1C  0     object to fly to (validate: no Destination with it)
    // +0x20..+0x4C: the spline's four control points as float bits, in memory order P1, P2,
    // P0, P3 (GotoBehavior builds the ICRSplineSegment from +0x38 / +0x20 / +0x2c / +0x44).
    uint32_t SplineP1X;            // +0x20  0
    uint32_t SplineP1Y;            // +0x24  0
    uint32_t SplineP1Z;            // +0x28  0
    uint32_t SplineP2X;            // +0x2C  0
    uint32_t SplineP2Y;            // +0x30  0
    uint32_t SplineP2Z;            // +0x34  0
    uint32_t SplineP0X;            // +0x38  0
    uint32_t SplineP0Y;            // +0x3C  0
    uint32_t SplineP0Z;            // +0x40  0
    uint32_t SplineP3X;            // +0x44  0
    uint32_t SplineP3Y;            // +0x48  0
    uint32_t SplineP3Z;            // +0x4C  0
    float   ArrivalRange;          // +0x50  500.0  (validate: must be >= 0)
    // +0x54: speed limit; above 1e-5 GotoBehavior turns it into its MaxThrottle through
    // get_projected_throttle, otherwise full throttle.
    float   MaxSpeed;              // +0x54  -1.0
    // +0x58: set = no stop at the destination (the Move / SplineMove Ideas get StopStyle 2,
    // "no slow-down"); Content's patrols and guides set it, the player's HUD goto clears it.
    uint8_t bFlyThrough;           // +0x58  0
    // +0x59  1. GotoBehavior::OnApplyDirective copies it to GotoBehavior+0x367 (.text:062fe86c)
    // and through FUN_062d5a10 to MoveIdea+0x58; no instruction in common.dll reads either copy.
    uint8_t Field_59;
    uint8_t bGotoCruise;           // +0x5A  0     "goto_cruise": force cruise on
    uint8_t bGotoNoCruise;         // +0x5B  0     "goto_no_cruise": force cruise off (validate: not both)
    uint32_t TetherObjectId;       // +0x5C  0
    float   TetherNearRange;       // +0x60  200.0
    float   TetherFarRange;        // +0x64  500.0  (validate: must be >= TetherNearRange)
    uint32_t TetherCruiseRatio;    // +0x68  0     float bits
    // +0x6C: set = the pathfinder's obstacle avoidance is switched off for this goto
    // (Behavior::SetAvoidanceAllowed(bDisableAvoidance == 0, 3)).
    uint8_t bDisableAvoidance;     // +0x6C  0

    DirectiveGotoOp();
    DirectiveGotoOp(DirectiveGotoOp const& Other);
    DirectiveGotoOp& operator=(DirectiveGotoOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveGotoOp, GotoKind) == 0x0C, "DirectiveGotoOp +0x0C");
static_assert(offsetof(DirectiveGotoOp, Destination) == 0x10, "DirectiveGotoOp +0x10");
static_assert(offsetof(DirectiveGotoOp, ArrivalRange) == 0x50, "DirectiveGotoOp +0x50");
static_assert(offsetof(DirectiveGotoOp, bDisableAvoidance) == 0x6C, "DirectiveGotoOp +0x6C");
static_assert(sizeof(DirectiveGotoOp) == 0x70, "DirectiveGotoOp size");

class COMMON_API DirectiveFollowOp : public BaseOp
{
public:
    uint   TargetId;               // +0x0C  0      object to follow (validate: != 0)
    float  FollowDistance;         // +0x10  150.0
    // +0x14..+0x1C: the offset from the leader FollowBehavior hands to set_follow_offset
    // (Content's Follow / Guide "offset" values, Server's FollowOffset).
    float  FollowOffsetX;          // +0x14  0.0
    float  FollowOffsetY;          // +0x18  0.0
    float  FollowOffsetZ;          // +0x1C  0.0
    float  EngageRange;            // +0x20  400.0  FollowBehavior's readiness range

    DirectiveFollowOp();
    DirectiveFollowOp(DirectiveFollowOp const& Other);
    DirectiveFollowOp& operator=(DirectiveFollowOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveFollowOp, TargetId) == 0x0C, "DirectiveFollowOp +0x0C");
static_assert(sizeof(DirectiveFollowOp) == 0x24, "DirectiveFollowOp size");

// Field meanings from FaceBehavior's directive handler (Common .text:062c8b30): Mode 1
// watches TargetId, 2 faces FacePoint, 3 takes Orientation, 4 has no target; any other
// mode fails. The copy ctor (.text:062a8b70) copies FacePoint as a Vector and Orientation
// as a Matrix (9 dwords). Content's [ObjList] "Face" parser writes OBJECT -> 1 / POINT -> 2.
class COMMON_API DirectiveFaceOp : public BaseOp
{
public:
    int32_t  Mode;                 // +0x0C  0  (validate: != 0)
    uint     TargetId;             // +0x10  0  mode 1 object (validate: != 0)
    Vector   FacePoint;            // +0x14  0,0,0  mode 2 point
    Matrix   Orientation;          // +0x20  mode 3; not written by retail default ctor
    uint8_t  bKillEngine;          // +0x44  1  FaceBehavior: SetEngineActive(bKillEngine == 0) on engage

    DirectiveFaceOp();
    DirectiveFaceOp(DirectiveFaceOp const& Other);
    DirectiveFaceOp& operator=(DirectiveFaceOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveFaceOp, Mode) == 0x0C, "DirectiveFaceOp +0x0C");
static_assert(offsetof(DirectiveFaceOp, FacePoint) == 0x14, "DirectiveFaceOp +0x14");
static_assert(offsetof(DirectiveFaceOp, Orientation) == 0x20, "DirectiveFaceOp +0x20");
static_assert(offsetof(DirectiveFaceOp, bKillEngine) == 0x44, "DirectiveFaceOp +0x44");
static_assert(sizeof(DirectiveFaceOp) == 0x48, "DirectiveFaceOp size");

class COMMON_API DirectiveStrafeOp : public BaseOp
{
public:
    uint   TargetId;               // +0x0C  0      object to strafe past (validate: != 0)

    DirectiveStrafeOp();
    DirectiveStrafeOp(DirectiveStrafeOp const& Other);
    DirectiveStrafeOp& operator=(DirectiveStrafeOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveStrafeOp, TargetId) == 0x0C, "DirectiveStrafeOp +0x0C");
static_assert(sizeof(DirectiveStrafeOp) == 0x10, "DirectiveStrafeOp size");

class COMMON_API DirectiveTrailOp : public BaseOp
{
public:
    uint   TargetId;               // +0x0C  0      object to trail (validate: != 0)
    float  TrailDistance;          // +0x10  150.0
    // +0x14 / +0x18  0.0: written by the ctor and the client's HUD trail request (0); copied by
    // the copy ctor. TrailBehavior::OnApplyDirective reads only +0x0c / +0x10.
    float  Field_14;
    float  Field_18;

    DirectiveTrailOp();
    DirectiveTrailOp(DirectiveTrailOp const& Other);
    DirectiveTrailOp& operator=(DirectiveTrailOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveTrailOp, TargetId) == 0x0C, "DirectiveTrailOp +0x0C");
static_assert(sizeof(DirectiveTrailOp) == 0x1C, "DirectiveTrailOp size");

class COMMON_API DirectiveRamOp : public BaseOp
{
public:
    uint   TargetId;               // +0x0C  0      object to ram

    DirectiveRamOp();
    DirectiveRamOp(DirectiveRamOp const& Other);
    DirectiveRamOp& operator=(DirectiveRamOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveRamOp, TargetId) == 0x0C, "DirectiveRamOp +0x0C");
static_assert(sizeof(DirectiveRamOp) == 0x10, "DirectiveRamOp size");

class COMMON_API DirectiveTractorBeamOp : public BaseOp
{
public:
    uint    TargetId;              // +0x0C  0      object to tractor (validate: != 0)
    // +0x10  1 (+0x11..+0x13 padding). Content's FObjListTractorBeam::SetTarget (FUN_06f28df0)
    // writes its bool argument (PatrolGuides passes false); LootBehavior::OnApplyDirective
    // copies it to LootBehavior+0x16c (.text:062cee35), which nothing reads.
    uint8_t Field_10;

    DirectiveTractorBeamOp();
    DirectiveTractorBeamOp(DirectiveTractorBeamOp const& Other);
    DirectiveTractorBeamOp& operator=(DirectiveTractorBeamOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveTractorBeamOp, TargetId) == 0x0C, "DirectiveTractorBeamOp +0x0C");
static_assert(offsetof(DirectiveTractorBeamOp, Field_10) == 0x10, "DirectiveTractorBeamOp +0x10");
static_assert(sizeof(DirectiveTractorBeamOp) == 0x14, "DirectiveTractorBeamOp size");

class COMMON_API DirectiveFormationOp : public BaseOp
{
public:
    // No fields beyond BaseOp.

    DirectiveFormationOp();
    DirectiveFormationOp(DirectiveFormationOp const& Other);
    DirectiveFormationOp& operator=(DirectiveFormationOp const& Other) = delete;

    bool validate() override;
};

static_assert(sizeof(DirectiveFormationOp) == 0x0C, "DirectiveFormationOp size");

class COMMON_API DirectiveEvadeOp : public BaseOp
{
public:
    uint   ThreatId;               // +0x0C  0      threat to evade

    DirectiveEvadeOp();
    DirectiveEvadeOp(DirectiveEvadeOp const& Other);
    DirectiveEvadeOp& operator=(DirectiveEvadeOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveEvadeOp, ThreatId) == 0x0C, "DirectiveEvadeOp +0x0C");
static_assert(sizeof(DirectiveEvadeOp) == 0x10, "DirectiveEvadeOp size");

class COMMON_API DirectiveDrasticEvadeOp : public BaseOp
{
public:
    // +0x0C: DrasticEvadeBehavior's mode (LAB_062c87c0 / LAB_062c8800): 0 = nudge sideways
    // along NudgeX/NudgeY for Duration seconds, 1 = full reverse throttle for Duration, other
    // values end at once. validate() always returns true.
    uint     Mode;                 // +0x0C  0
    uint32_t NudgeX;               // +0x10  0     float bits; copied with Y/Z only in mode 0
    uint32_t NudgeY;               // +0x14  0
    uint32_t NudgeZ;               // +0x18  0     copied but not used (the nudge's z is 0)
    float    Duration;             // +0x1C  1.0
    // +0x20..+0x2C  0: written by the ctor, copied by the copy ctor; DrasticEvadeBehavior
    // reads only +0x0c..+0x1c.
    uint32_t Field_20;             // +0x20  0
    uint32_t Field_24;             // +0x24  0
    uint32_t Field_28;             // +0x28  0
    uint32_t Field_2C;             // +0x2C  0

    DirectiveDrasticEvadeOp();
    DirectiveDrasticEvadeOp(DirectiveDrasticEvadeOp const& Other);
    DirectiveDrasticEvadeOp& operator=(DirectiveDrasticEvadeOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveDrasticEvadeOp, Mode) == 0x0C, "DirectiveDrasticEvadeOp +0x0C");
static_assert(offsetof(DirectiveDrasticEvadeOp, Duration) == 0x1C, "DirectiveDrasticEvadeOp +0x1C");
static_assert(sizeof(DirectiveDrasticEvadeOp) == 0x30, "DirectiveDrasticEvadeOp size");

class COMMON_API DirectiveDockOp : public BaseOp
{
public:
    uint     DockObjectId;         // +0x0C  0      object to dock with
    // +0x10: with a trade-lane ring as DockObjectId, the ring the ship continues to (Content's
    // Dock second value, "NONE" = 0); DockBehavior keeps it as LaneNextRing.
    int32_t  NextRingId;           // +0x10  0
    // +0x14: DockPortIndex is given (every writer sets both; validate: not with index -1);
    // DockBehavior otherwise picks its own port.
    uint8_t  bHasDockPortIndex;    // +0x14  0     (+0x15..+0x17 padding)
    int32_t  DockPortIndex;        // +0x18  -1
    int32_t  TetherObjectId;       // +0x1C  0
    float    TetherNearRange;      // +0x20  200.0
    float    TetherFarRange;       // +0x24  500.0  (validate: must be >= TetherNearRange)
    int32_t  TetherCruiseRatio;    // +0x28  0     float bits

    DirectiveDockOp();
    DirectiveDockOp(DirectiveDockOp const& Other);
    DirectiveDockOp& operator=(DirectiveDockOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveDockOp, DockObjectId) == 0x0C, "DirectiveDockOp +0x0C");
static_assert(offsetof(DirectiveDockOp, DockPortIndex) == 0x18, "DirectiveDockOp +0x18");
static_assert(offsetof(DirectiveDockOp, TetherNearRange) == 0x20, "DirectiveDockOp +0x20");
static_assert(sizeof(DirectiveDockOp) == 0x2C, "DirectiveDockOp size");

class COMMON_API DirectiveLaunchOp : public BaseOp
{
public:
    uint    LaunchObjectId;        // +0x0C  0      object to launch from (validate: != 0)
    int32_t DockPortIndex;         // +0x10  -1     (validate: != -1); LaunchBehavior's DockPortIndex
    // +0x14: Content's Launch terminal kind, NONE / BERTH / PAD / MOOR_SMALL / MOOR_MEDIUM /
    // MOOR_LARGE / RING / JUMP_GATE = 0..7 (validate: != 0).
    int32_t TerminalType;          // +0x14  0

    DirectiveLaunchOp();
    DirectiveLaunchOp(DirectiveLaunchOp const& Other);
    DirectiveLaunchOp& operator=(DirectiveLaunchOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveLaunchOp, LaunchObjectId) == 0x0C, "DirectiveLaunchOp +0x0C");
static_assert(sizeof(DirectiveLaunchOp) == 0x18, "DirectiveLaunchOp size");

class COMMON_API DirectiveInstantTradelaneOp : public BaseOp
{
public:
    uint    TradeLaneId;           // +0x0C  0      tradelane to use (validate: != 0)
    // +0x10: InstantTradeLaneBehavior's exit ring (go_tradelane's second ring); 0 lets
    // get_lane_start find it.
    int32_t ExitRingId;            // +0x10  0
    // +0x14: float bits, go_tradelane's Speed (InstantTradeLaneBehavior::LaneSpeed).
    int32_t LaneSpeed;             // +0x14  0

    DirectiveInstantTradelaneOp();
    DirectiveInstantTradelaneOp(DirectiveInstantTradelaneOp const& Other);
    DirectiveInstantTradelaneOp& operator=(DirectiveInstantTradelaneOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveInstantTradelaneOp, TradeLaneId) == 0x0C, "DirectiveInstantTradelaneOp +0x0C");
static_assert(sizeof(DirectiveInstantTradelaneOp) == 0x18, "DirectiveInstantTradelaneOp size");

class COMMON_API DirectiveDelayOp : public BaseOp
{
public:
    float  DelaySeconds;           // +0x0C  0.0    (validate: != 0.0)

    DirectiveDelayOp();
    DirectiveDelayOp(DirectiveDelayOp const& Other);
    DirectiveDelayOp& operator=(DirectiveDelayOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveDelayOp, DelaySeconds) == 0x0C, "DirectiveDelayOp +0x0C");
static_assert(sizeof(DirectiveDelayOp) == 0x10, "DirectiveDelayOp size");

class COMMON_API DirectiveCancelOp : public BaseOp
{
public:
    // No fields beyond BaseOp.

    DirectiveCancelOp();
    DirectiveCancelOp(DirectiveCancelOp const& Other);
    DirectiveCancelOp& operator=(DirectiveCancelOp const& Other) = delete;

    bool validate() override;
};

static_assert(sizeof(DirectiveCancelOp) == 0x0C, "DirectiveCancelOp size");

class COMMON_API DirectiveWaitForShip : public BaseOp
{
public:
    uint     ShipId;               // +0x0C  0      ship to wait for
    uint32_t Field_10;             // +0x10  not written by retail default ctor

    DirectiveWaitForShip();
    DirectiveWaitForShip(DirectiveWaitForShip const& Other);
    DirectiveWaitForShip& operator=(DirectiveWaitForShip const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveWaitForShip, ShipId) == 0x0C, "DirectiveWaitForShip +0x0C");
static_assert(sizeof(DirectiveWaitForShip) == 0x14, "DirectiveWaitForShip size");

class COMMON_API DirectiveWaitForPlayerManeuverOp : public BaseOp
{
public:
    // Passed to BehaviorTypes::get_behavior_id() in validate().
    char const* BehaviorName;      // +0x0C  not written by retail default ctor

    DirectiveWaitForPlayerManeuverOp();
    DirectiveWaitForPlayerManeuverOp(DirectiveWaitForPlayerManeuverOp const& Other);
    DirectiveWaitForPlayerManeuverOp& operator=(DirectiveWaitForPlayerManeuverOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveWaitForPlayerManeuverOp, BehaviorName) == 0x0C, "DirectiveWaitForPlayerManeuverOp +0x0C");
static_assert(sizeof(DirectiveWaitForPlayerManeuverOp) == 0x10, "DirectiveWaitForPlayerManeuverOp size");

class COMMON_API DirectiveIdleOp : public BaseOp
{
public:
    // No fields beyond BaseOp.

    DirectiveIdleOp();
    DirectiveIdleOp(DirectiveIdleOp const& Other);
    DirectiveIdleOp& operator=(DirectiveIdleOp const& Other) = delete;

    bool validate() override;
};

static_assert(sizeof(DirectiveIdleOp) == 0x0C, "DirectiveIdleOp size");

// DirectiveGuideOp — Common.dll.c 0x7B060 (default ctor). Embeds a DirectiveGotoOp and a
// DirectiveFollowOp by value, each with its own vtable; validate() calls both embedded
// validate()s through their vtables.
class COMMON_API DirectiveGuideOp : public BaseOp
{
public:
    DirectiveGotoOp   GotoParams;      // +0x0C  size 0x70
    DirectiveFollowOp FollowParams;    // +0x7C  size 0x24
    // GuideBehavior::OnApplyDirective (LAB_062cfc70) copies each of these into its own
    // member of the same name. GotoParams.TetherObjectId is the escorted ship.
    uint8_t bStartFollowing;       // +0xA0  1     the escort starts out following (Content's SetGuide)
    float   SampleInterval;        // +0xA4  10.0  seconds between progress samples
    float   CourseAngleThreshold;  // +0xA8  0.3927  (pi/8) escort heading error for "on course"
    // +0xAC  3. Content's Guide parsers write value 14 / 12 of the [ObjList] line
    // (MissionObjList.cpp); GuideBehavior::OnApplyDirective FILDs it into GuideBehavior+0x110,
    // which nothing reads.
    int32_t Field_AC;
    float   ProgressHighThreshold; // +0xB0  10.0  distance closed per sample that counts as catching up
    float   ProgressLowThreshold;  // +0xB4  -10.0 below this the escort counts as falling behind
    float   GuideRange;            // +0xB8  400.0 OnEvaluateReadiness's range to the escort

    DirectiveGuideOp();
    DirectiveGuideOp(DirectiveGuideOp const& Other);
    DirectiveGuideOp& operator=(DirectiveGuideOp const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(DirectiveGuideOp, GotoParams) == 0x0C, "DirectiveGuideOp +0x0C");
static_assert(offsetof(DirectiveGuideOp, FollowParams) == 0x7C, "DirectiveGuideOp +0x7C");
static_assert(offsetof(DirectiveGuideOp, bStartFollowing) == 0xA0, "DirectiveGuideOp +0xA0");
static_assert(offsetof(DirectiveGuideOp, GuideRange) == 0xB8, "DirectiveGuideOp +0xB8");
static_assert(sizeof(DirectiveGuideOp) == 0xBC, "DirectiveGuideOp size");

// ============================================================================
// Parameter structs for IBehaviorManager methods
// ============================================================================
// These derive from BaseOp: their copy ctors (Common.dll.c 0x48DA0 / 0x48D10) set
// BaseOp's vftable and then their own. Default ctors 0x7B3C0 / 0x7B330, validate()
// 0x7B3F0 / 0x7B360.

struct COMMON_API SetFlagParams : public BaseOp
{
    int32_t  FlagId;               // +0x0C  2      (validate: != 2)
    uint8_t  bValue;               // +0x10  0     (+0x11..+0x13 padding)

    SetFlagParams();
    SetFlagParams(SetFlagParams const& Other);
    SetFlagParams& operator=(SetFlagParams const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(SetFlagParams, FlagId) == 0x0C, "SetFlagParams +0x0C");
static_assert(offsetof(SetFlagParams, bValue) == 0x10, "SetFlagParams +0x10");
static_assert(sizeof(SetFlagParams) == 0x14, "SetFlagParams size");

// SetPersonalityParams — Common.dll.c 0x7AD10 (default ctor) / 0x48930 (copy ctor, flat
// copy of the embedded Personality's 0xF6 dwords). Embeds a Personality by value.
// Field meanings from the one consumer, Server.dll pub::AI::SubmitState: +0x0C ->
// IBehaviorManager::set_state_graph(id, false); +0x14 / +0x10 -> set_directive_callback /
// set_content_callback unless -1; byte +0x18 gates set_personality(Value). Content fills
// the callbacks with its NPC objects' callback subobjects or 0.
struct COMMON_API SetPersonalityParams : public BaseOp
{
    int32_t            StateGraphId;             // +0x0C  -1 (validate: != -1)
    ContentCallback*   ContentCallbackObject;    // +0x10  (ContentCallback*)-1 = keep
    DirectiveCallback* DirectiveCallbackObject;  // +0x14  (DirectiveCallback*)-1 = keep
    uint8_t            bSetPersonality;          // +0x18  0  (+0x19..+0x1B padding)
    Personality Value;              // +0x1C  size 0x3D8

    SetPersonalityParams();
    SetPersonalityParams(SetPersonalityParams const& Other);
    SetPersonalityParams& operator=(SetPersonalityParams const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(SetPersonalityParams, StateGraphId) == 0x0C, "SetPersonalityParams +0x0C");
static_assert(offsetof(SetPersonalityParams, DirectiveCallbackObject) == 0x14, "SetPersonalityParams +0x14");
static_assert(offsetof(SetPersonalityParams, bSetPersonality) == 0x18, "SetPersonalityParams +0x18");
static_assert(offsetof(SetPersonalityParams, Value) == 0x1C, "SetPersonalityParams +0x1C");
static_assert(sizeof(SetPersonalityParams) == 0x3F4, "SetPersonalityParams size");

// Field names follow PathfindManager::UserZone (common_pathfind.h), which Server.dll
// pub::AI::SubmitState builds from these fields; Content's mission [ObjList]
// "StayInRange" parser fills them.
struct COMMON_API SetZoneBehaviorParams : public BaseOp
{
    // The values of InvertMode.
    enum EInvertMode
    {
        kInvertNormal = 0,
        kInvertInverted = 1,
        kInvertClearUserZone = 2,
    };
    // The values of Mode (also PathfindManager::UserZone::Mode).
    enum EZoneMode
    {
        kZoneSphere = 0,   // sphere at TargetPoint / Radius
        kZoneBox = 1,      // oriented box BoxMin..BoxMax
        kZoneObject = 2,   // sphere around the live position of the object TargetId names
    };
    int32_t Mode;                  // +0x0C  0 sphere (TargetPoint, Radius), 1 box (BoxMin..BoxMax),
                                   //        2 live object (TargetId, Radius).
                                   //        Not written by the retail default ctor.
    int32_t InvertMode;            // +0x10  2      0 normal, 1 inverted, 2 clear the user zone
                                   //        (validate skips its checks)
    Vector  TargetPoint;           // +0x14  0,0,0  sphere centre
    uint    TargetId;              // +0x20  0      object of mode 2
    Vector  BoxMin;                // +0x24  box corners of mode 1; not written by the
    Vector  BoxMax;                // +0x30  retail default ctor
    float   Radius;                // +0x3C  0.0    (validate: != 0.0)

    SetZoneBehaviorParams();
    SetZoneBehaviorParams(SetZoneBehaviorParams const& Other);
    SetZoneBehaviorParams& operator=(SetZoneBehaviorParams const& Other) = delete;

    bool validate() override;
};

static_assert(offsetof(SetZoneBehaviorParams, Mode) == 0x0C, "SetZoneBehaviorParams +0x0C");
static_assert(offsetof(SetZoneBehaviorParams, TargetPoint) == 0x14, "SetZoneBehaviorParams +0x14");
static_assert(offsetof(SetZoneBehaviorParams, TargetId) == 0x20, "SetZoneBehaviorParams +0x20");
static_assert(offsetof(SetZoneBehaviorParams, BoxMin) == 0x24, "SetZoneBehaviorParams +0x24");
static_assert(offsetof(SetZoneBehaviorParams, BoxMax) == 0x30, "SetZoneBehaviorParams +0x30");
static_assert(offsetof(SetZoneBehaviorParams, Radius) == 0x3C, "SetZoneBehaviorParams +0x3C");
static_assert(sizeof(SetZoneBehaviorParams) == 0x40, "SetZoneBehaviorParams size");

// ============================================================================
// ContentCallback — interface for content-driven AI events
// ============================================================================

// Retail compiled these ten virtuals inline into every module that saw the header, and
// each module exports its own copy (Common.dll, e.g. get_creation_type .text:0626c9a0;
// Server.dll .text:06ce1030..06ce10a0; FreeLancer.exe) -- member-level export only, no
// ctor, dtor, operator= or vftable. Default bodies are empty / return false; Common /
// Server addresses:
//   follow_event 062a88d0 / 06ce1030 (shared with tether_object_event)
//   lead_object_event 062a88e0 / 06ce1040    request_flee_destination 062a88f0 / 06ce1060
//   information_request 062a8900 / 06ce1070  combat_state_notification 062a8910 / 06ce1080
//   combat_drift_event 062a8920 / 06ce1090   get_creation_type 0626c9a0 / 06ce10a0 (returns 3)
//   scan_state_change 062ed730 / 06ce1050 (shared with ship_in_sights_event)
//
// Slot order from Common.dll's calls through IBehaviorManager+0x110 (argument count
// matched to each body's RET n):
//   +0x00 follow_event              .text:062d29f6 (FUN_062d29c0)
//   +0x04 tether_object_event       .text:062d2a4d
//   +0x08 lead_object_event         .text:062d2aab
//   +0x0c ship_in_sights_event      .text:062d2cc8 (an id from the object's [vptr+0x20])
//   +0x10 request_flee_destination  .text:062d2ade
//   +0x14 information_request       the one 3-argument method left; no Common call site
//   +0x18 scan_state_change         .text:062d2dc0
//   +0x1c combat_state_notification .text:062ee2d3 / 062ee2ff / 062ee35a
//   +0x20 combat_drift_event        .text:062ed96e
//   +0x24 get_creation_type         .text:062d3f03 (set_content_callback)
// Ten slots, one per exported virtual: there is no virtual destructor.
struct ContentCallback
{
    // Values the engine sends: 0, 2, 4 (TargetManager::SetAttackState, 062ee280). Content's
    // Gcs handler also accepts 1 as a second "no engage" and 3 as a second "combat over".
    enum CSNType // Combat State Notification type
    {
        CSNEngage = 0,       // a hostile target was acquired (Gcs engage comm)
        CSNNoEngage = 2,     // Gcs no-engage comm
        CSNCombatOver = 4,   // all clear (Gcs combat-over comm)
    };

    // FollowBehavior reports the leader crossing the follow range (UpdateInterceptEngageState).
    enum FollowStatusType
    {
        FollowEnterRange = 1,   // the leader came within FollowDistance (+ FollowRangeBias)
        FollowLeaveRange = 2,   // the leader left it
    };

    // GuideBehavior's guide events; the retail voice-line names (Content NpcShip's
    // lead_object_event) are the bc_* strings noted per value.
    enum LeadProgressType
    {
        LeadGotoPos = 1,           // "bc_goto_pos": leading, caught up and on course
        LeadGotoFlat = 2,          // "bc_goto_flat": leading, steady
        LeadGotoNeg = 3,           // "bc_goto_neg": leading, fell behind
        LeadSelf = 4,              // the "leader is me" shortcut (timer only, no line)
        LeadFollowPos = 5,         // following, caught up and pointing the right way (timer only)
        LeadFollowPosAppOff = 6,   // "bc_follow_pos_app_off": caught up but pointing away
        LeadFollowFlat = 7,        // "bc_follow_flat"
        LeadFollowNeg = 8,         // "bc_follow_neg": fell behind
    };

    // GotoBehavior's tether zone transitions ("bc_enter_near_teth", "bc_enter_far_teth",
    // "bc_exit_far_teth").
    enum TetherStatusType
    {
        TetherEnterNear = 1,
        TetherEnterFar = 2,
        TetherExitFar = 3,
    };

    // FleeBehavior::FleeReason; Content's MakeFleeReasonName picks the voice line
    // gcs_combat_fleereason_tough / _damaged / _noweapons / _other.
    enum FleeReason
    {
        FleeReasonTough = 0,           // the aggression ratio is above the threat threshold
        FleeReasonLeaderFleeing = 1,   // follows a fleeing leader
        FleeReasonDamaged = 2,         // hull below FleeWhenHullDamagedPercent
        FleeReasonNoWeapons = 3,
        FleeReasonOther = 4,           // the ctor value
    };

    // What request_flee_destination answers and FleeBehavior::FleeDestType holds.
    enum FleeDestinationType
    {
        FleeDestNone = 0,     // nothing answered yet
        FleeDestObject = 1,   // fly to the object OutParam names under the Goto behavior (kGotoKindObject)
        FleeDestDock = 2,     // dock with the object OutParam names under the Dock behavior
        FleeDestPoint = 3,    // fly to OutPosition under the Goto behavior (kGotoKindPoint); also the callback-less fallback
    };

    enum InformationType
    {
        InfoType0 = 0,
        InfoType1 = 1,
        InfoType2 = 2,
    };

    // What created the ship (Content's ENpcCreationKind mapped by FContentNpcShip::
    // get_creation_type); 3 is what the base get_creation_type returns. Gun groups skip a
    // Mission ship as a candidate unless the shooter may engage it.
    enum CreationType
    {
        CreationEncounter = 0,
        CreationRandomMission = 1,
        CreationMission = 2,
        CreationNone = 3,
    };

    SHARED_INLINE_API virtual void follow_event(uint ObjectId1, uint ObjectId2, FollowStatusType Status, float Distance) {}                                                           // +0x00
    SHARED_INLINE_API virtual void tether_object_event(uint ObjectId1, uint ObjectId2, TetherStatusType Status, Vector const& Position) {}                                             // +0x04
    SHARED_INLINE_API virtual void lead_object_event(uint ObjectId1, uint ObjectId2, LeadProgressType Progress, float Value, Vector const& Position) {}                                // +0x08
    SHARED_INLINE_API virtual void ship_in_sights_event(uint ObjectId) {}                                                                                                               // +0x0c
    SHARED_INLINE_API virtual bool request_flee_destination(uint ObjectId, FleeReason Reason, FleeDestinationType& OutType, Vector& OutPosition, uint& OutParam, float& OutFloat) { return false; } // +0x10
    SHARED_INLINE_API virtual bool information_request(InformationType Type, int Param, char* Buffer) { return false; }                                                                 // +0x14
    SHARED_INLINE_API virtual void scan_state_change(bool bScanning) {}                                                                                                                 // +0x18
    SHARED_INLINE_API virtual void combat_state_notification(CSNType Type, uint Param1, uint Param2) {}                                                                                 // +0x1c
    SHARED_INLINE_API virtual void combat_drift_event() {}                                                                                                                              // +0x20
    SHARED_INLINE_API virtual CreationType get_creation_type() { return CreationNone; }                                                                                                // +0x24
};

// ============================================================================
// DirectiveCallback — interface for directive-related callbacks
// ============================================================================

// One virtual, no destructor: IBehaviorManager::UpdateBehaviorTick calls slot 0 of each of
// its five DirectiveCallbacks with two arguments (.text:062d2ef7, again at 062d33ca), and
// LaunchAction's vtable (PTR_FUN_0639e57c) has exactly one slot. Arguments are manager
// +0x11c then +0x118, the directive-result payload (meaning unrecovered, hence the offset
// names). Nothing deletes through a DirectiveCallback*; callbacks are registered by pointer
// and never owned by the manager.
// A struct in retail and not exported (RTTI .data:063ebca0); there is no vftable of its own,
// so the slot is pure.
struct DirectiveCallback
{
    // Retail override: LaunchAction::OnDirectiveComplete (0x62b01f0). Dispatched when a
    // queued AI directive this callback was registered for finishes.
    virtual void OnDirectiveComplete(int CompletedBehavior, int DirectiveResult) = 0;
};

}} // namespace pub::AI
