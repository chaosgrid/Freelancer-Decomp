// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll pub::AI namespace implementations.

#include "common_pub.h"
#include "common_iface.h" // BehaviorTypes::get_behavior_id
#include <windows.h> // OutputDebugStringA
#include <cstdio>    // snprintf
#include <cstring>   // memcpy

namespace pub { namespace AI {

// ============================================================================
// GSplinePath
// ============================================================================

GSplinePath::GSplinePath()
{
    // .text:06261260: nothing is initialised.
}

GSplinePath& GSplinePath::operator=(GSplinePath const& Other)
{
    // .text:06274100: 12 dwords, no self-check.
    std::memcpy(this, &Other, sizeof(GSplinePath));
    return *this;
}

// ============================================================================
// Personality nested structs
// ============================================================================

// The default values are retail's default ctors (per-struct addresses in common_pub.h);
// operator= is a raw copy, as retail's dword-copy loops (all plain data, no pointers).

Personality::EvadeDodgeUseStruct::EvadeDodgeUseStruct()
    : EvadeActivateRange(500.0f)
    , EvadeDodgeStyleWeightWaggle(0.0f)
    , EvadeDodgeStyleWeightWaggleRandom(0.0f)
    , EvadeDodgeStyleWeightSlide(0.0f)
    , EvadeDodgeStyleWeightCorkscrew(0.0f)
    , EvadeDodgeConeAngle(0.7853981852531433f)
    , EvadeDodgeConeAngleVariancePercent(0.0f)
    , EvadeDodgeWaggleAxisConeAngle(0.0f)
    , EvadeDodgeRollAngle(0.0f)
    , EvadeDodgeIntervalTime(1.0f)
    , EvadeDodgeIntervalTimeVariancePercent(0.0f)
    , EvadeDodgeDistance(100.0f)
    , EvadeDodgeTime(10.0f)
    , EvadeDodgeSlideThrottle(0.6f)
    , EvadeDodgeTurnThrottle(0.6f)
    , EvadeDodgeCorkscrewTurnThrottle(0.15f)
    , EvadeDodgeCorkscrewRollThrottle(1.0f)
    , bEvadeDodgeCorkscrewRollFlipDirection(0)
    , EvadeDodgeDirectionWeightLeft(0.0f)
    , EvadeDodgeDirectionWeightRight(0.0f)
    , EvadeDodgeDirectionWeightUp(0.0f)
    , EvadeDodgeDirectionWeightDown(0.0f)
{
}

Personality::EvadeDodgeUseStruct& Personality::EvadeDodgeUseStruct::operator=(EvadeDodgeUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::EvadeBreakUseStruct::EvadeBreakUseStruct()
    : EvadeBreakTime(3.0f)
    , EvadeBreakIntervalTime(100000.0f)
    , EvadeBreakAfterburnerDelay(0.0f)
    , EvadeBreakAfterburnerDelayVariancePercent(0.0f)
    , EvadeBreakDirectionWeightLeft(0.0f)
    , EvadeBreakDirectionWeightRight(0.0f)
    , EvadeBreakDirectionWeightUp(0.0f)
    , EvadeBreakDirectionWeightDown(0.0f)
    , EvadeBreakRollThrottle(0.0f)
    , EvadeBreakTurnThrottle(1.0f)
    , EvadeBreakStyleWeightSideways(0.0f)
    , EvadeBreakStyleWeightOutrun(0.0f)
    , EvadeBreakStyleWeightReverse(0.0f)
    , EvadeBreakAttemptReverseTime(4.0f)
    , EvadeBreakReverseDistance(150.0f)
{
}

Personality::EvadeBreakUseStruct& Personality::EvadeBreakUseStruct::operator=(EvadeBreakUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::BuzzHeadTowardUseStruct::BuzzHeadTowardUseStruct()
    : BuzzMaxTimeToHeadAway(4.0f)
    , BuzzHeadTowardStyleWeightStraightTo(0.0f)
    , BuzzHeadTowardStyleWeightSlide(0.0f)
    , BuzzHeadTowardStyleWeightWaggle(0.0f)
    , BuzzMinDistanceToHeadToward(200.0f)
    , BuzzMinDistanceToHeadTowardVariancePercent(1.0f)
    , BuzzHeadTowardEngineThrottle(1.0f)
    , BuzzHeadTowardTurnThrottle(1.0f)
    , BuzzHeadTowardRollThrottle(0.0f)
    , bBuzzHeadTowardRollFlipDirection(0)
    , BuzzDodgeDirectionWeightLeft(0.0f)
    , BuzzDodgeDirectionWeightRight(0.0f)
    , BuzzDodgeDirectionWeightUp(0.0f)
    , BuzzDodgeDirectionWeightDown(0.0f)
    , BuzzDodgeTurnThrottle(1.0f)
    , BuzzDodgeConeAngle(0.7853981852531433f)
    , BuzzDodgeConeAngleVariancePercent(0.0f)
    , BuzzDodgeWaggleAxisConeAngle(0.0f)
    , BuzzDodgeRollAngle(0.0f)
    , BuzzDodgeIntervalTime(2.0f)
    , BuzzDodgeIntervalTimeVariancePercent(0.0f)
    , BuzzSlideThrottle(1.0f)
    , BuzzSlideIntervalTime(2.0f)
    , BuzzSlideIntervalTimeVariancePercent(0.4000000059604645f)
{
}

Personality::BuzzHeadTowardUseStruct& Personality::BuzzHeadTowardUseStruct::operator=(BuzzHeadTowardUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::BuzzPassByUseStruct::BuzzPassByUseStruct()
    : BuzzPassByStyleWeightStraightBy(0.0f)
    , BuzzPassByStyleWeightBreakAway(0.0f)
    , BuzzPassByStyleWeightEngineKill(0.0f)
    , BuzzDistanceToPassBy(100.0f)
    , BuzzPassByTime(3.0f)
    , bBuzzDropBombOnPassBy(0)
    , BuzzBreakDirectionWeightLeft(0.0f)
    , BuzzBreakDirectionWeightRight(0.0f)
    , BuzzBreakDirectionWeightUp(0.0f)
    , BuzzBreakDirectionWeightDown(0.0f)
    , BuzzBreakDirectionConeAngle(0.0f)
    , BuzzBreakTurnThrottle(1.0f)
    , BuzzPassByRollThrottle(0.0f)
{
}

Personality::BuzzPassByUseStruct& Personality::BuzzPassByUseStruct::operator=(BuzzPassByUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::TrailUseStruct::TrailUseStruct()
    : TrailLockConeAngle(0.5235987901687622f)
    , TrailBreakTime(2.5f)
    , TrailMinNoLockTime(4.0f)
    , TrailBreakRollThrottle(0.0f)
    , bTrailBreakAfterburner(0)
    , TrailMaxTurnThrottle(1.0f)
    , TrailDistance(150.0f)
{
}

Personality::TrailUseStruct& Personality::TrailUseStruct::operator=(TrailUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::StrafeUseStruct::StrafeUseStruct()
    : StrafeRunAwayDistance(150.0f)
    , StrafeAttackThrottle(1.0f)
    , StrafeTurnThrottle(1.0f)
{
}

Personality::StrafeUseStruct& Personality::StrafeUseStruct::operator=(StrafeUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::EngineKillUseStruct::EngineKillUseStruct()
    : EngineKillSearchTime(6.0f)
    , EngineKillFaceTime(3.0f)
    , EngineKillUseAfterburner(0.0f)
    , EngineKillAfterburnerTime(0.5f)
    , EngineKillMaxTargetDistance(700.0f)
{
}

Personality::EngineKillUseStruct& Personality::EngineKillUseStruct::operator=(EngineKillUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::RepairUseStruct::RepairUseStruct()
    : UseShieldRepairPreDelay(2.0f)
    , UseShieldRepairAtDamagePercent(0.5f)
    , UseShieldRepairPostDelay(3.0f)
    , UseHullRepairPreDelay(1.0f)
    , UseHullRepairAtDamagePercent(0.5f)
    , UseHullRepairPostDelay(1.0f)
{
}

Personality::RepairUseStruct& Personality::RepairUseStruct::operator=(RepairUseStruct const& Other)
{
    // FUN_06263b40 (ICF-shared with MarshalBuffer::operator=), no self test.
    UseShieldRepairPreDelay = Other.UseShieldRepairPreDelay;
    UseShieldRepairAtDamagePercent = Other.UseShieldRepairAtDamagePercent;
    UseShieldRepairPostDelay = Other.UseShieldRepairPostDelay;
    UseHullRepairPreDelay = Other.UseHullRepairPreDelay;
    UseHullRepairAtDamagePercent = Other.UseHullRepairAtDamagePercent;
    UseHullRepairPostDelay = Other.UseHullRepairPostDelay;
    return *this;
}

Personality::GunUseStruct::GunUseStruct()
    : FireStyle(0)
    , GunFireIntervalTime(0.0f)
    , GunFireIntervalVariancePercent(0.5f)
    , GunFireBurstIntervalTime(1.5f)
    , GunFireBurstIntervalVariancePercent(0.1f)
    , GunFireNoBurstIntervalTime(0.1f)
    , GunFireAccuracyConeAngle(0.0f)
    , GunFireAccuracyPower(1.0f)
    , GunFireAccuracyPowerNpc(6.0f)
    , GunRangeThreshold(1.0f)
    , GunRangeThresholdVariancePercent(0.20000000298023224f)
    , GunTargetPointSwitchTime(1.5f)
    , AutoTurretIntervalTime(0.0f)
    , AutoTurretBurstIntervalTime(1.0f)
    , AutoTurretNoBurstIntervalTime(0.0f)
    , AutoTurretBurstIntervalVariancePercent(0.1f)
{
}

Personality::GunUseStruct& Personality::GunUseStruct::operator=(GunUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::MineUseStruct::MineUseStruct()
    : MineLaunchInterval(0.0f)
    , MineLaunchConeAngle(0.39269909262657166f)
    , MineLaunchRange(400.0f)
{
}

Personality::MineUseStruct& Personality::MineUseStruct::operator=(MineUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::MissileUseStruct::MissileUseStruct()
    : MissileLaunchRange(-1.0f)
    , bMissileLaunchAllowOutOfRange(0)
    , MissileLaunchIntervalTime(5.0f)
    , MissileLaunchIntervalVariancePercent(0.4000000059604645f)
    , MissileLaunchConeAngle(0.39269909262657166f)
    , AntiCruiseMissileMinDistance(150.0f)
    , AntiCruiseMissileMaxDistance(2500.0f)
    , AntiCruiseMissilePreFireDelay(2.0f)
    , AntiCruiseMissileIntervalTime(5.0f)
{
}

Personality::MissileUseStruct& Personality::MissileUseStruct::operator=(MissileUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::DamageReactionStruct::DamageReactionStruct()
    : EvadeBreakDamageTriggerPercent(1.0f)
    , EvadeDodgeMoreDamageTriggerPercent(0.20000000298023224f)
    , DropMinesDamageTriggerPercent(1.0f)
    , DropMinesDamageTriggerTime(1.0f)
    , EngineKillFaceDamageTriggerPercent(1.0f)
    , EngineKillFaceDamageTriggerTime(2.0f)
    , RollDamageTriggerPercent(1.0f)
    , RollDamageTriggerTime(1.0f)
    , AfterburnerDamageTriggerPercent(1.0f)
    , AfterburnerDamageTriggerTime(1.5f)
    , BrakeReverseDamageTriggerPercent(1.0f)
    , FireMissilesDamageTriggerPercent(1.0f)
    , FireMissilesDamageTriggerTime(0.5f)
    , FireGunsDamageTriggerPercent(1.0f)
    , FireGunsDamageTriggerTime(1.0f)
    , Field_3C(0.0f) // not written by retail ctor
    , Field_40(0.0f) // not written by retail ctor
{
}

Personality::DamageReactionStruct& Personality::DamageReactionStruct::operator=(DamageReactionStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::MissileReactionStruct::MissileReactionStruct()
    : EvadeBreakMissileReactionTime(0.0f)
    , EvadeSlideMissileReactionTime(0.0f)
    , EvadeAfterburnMissileReactionTime(0.0f)
    , EvadeMissileDistance(250.0f)
{
}

Personality::MissileReactionStruct& Personality::MissileReactionStruct::operator=(MissileReactionStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::CountermeasureUseStruct::CountermeasureUseStruct()
    : CountermeasureActiveTime(1.0f)
    , CountermeasureUnactiveTime(0.0f)
{
}

Personality::CountermeasureUseStruct& Personality::CountermeasureUseStruct::operator=(CountermeasureUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

Personality::FormationUseStruct::FormationUseStruct()
    : ForceAttackFormationActiveTime(1.0f)
    , ForceAttackFormationUnactiveTime(0.0f)
    , BreakFormationDamageTriggerPercent(1.0f)
    , BreakFormationDamageTriggerTime(4.0f)
    , BreakApartFormationDamageTriggerPercent(1.0f)
    , BreakApartFormationDamageTriggerTime(4.0f)
    , BreakFormationMissileReactionTime(0.0f)
    , BreakApartFormationMissileReactionTime(4.0f)
    , bBreakApartFormationOnBuzzHeadToward(0)
    , BreakFormationOnBuzzHeadTowardTime(4.0f)
    , bRegroupFormationOnBuzzHeadToward(0)
    , bBreakApartFormationOnBuzzPassBy(0)
    , BreakFormationOnBuzzPassByTime(4.0f)
    , bRegroupFormationOnBuzzPassBy(0)
    , bBreakApartFormationOnEvadeDodge(0)
    , BreakFormationOnEvadeDodgeTime(4.0f)
    , bRegroupFormationOnEvadeDodge(0)
    , bBreakApartFormationOnEvadeBreak(0)
    , BreakFormationOnEvadeBreakTime(4.0f)
    , bRegroupFormationOnEvadeBreak(0)
    , FormationExitMode(5)
    , FormationExitTopTurnBreakAwayThrottle(1.0f)
    , FormationExitRollOutrunThrottle(0.0f)
    , FormationExitMaxTime(0.0f)
    , bLeaderMakesMeTougher(0)
{
}

Personality::FormationUseStruct& Personality::FormationUseStruct::operator=(FormationUseStruct const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

// .text:062da980. Retail writes AttackPreferences[0], AttackPreferences[1].ShipClass and
// AttackSubtargetOrder[0..1] only; the rest of both arrays, bWaitForLeaderTarget and
// ScanForCargo are left as they were. The port zeroes them (over-initialisation).
Personality::JobStruct::JobStruct()
    : RangeThreshold(0)
    , SceneToughnessThreshold(2)
    , TargetToughnessPreference(2)
    , CombatDriftDistance(10000.0f)
    , AttackPreferences()
    , AttackSubtargetOrder()
    , bWaitForLeaderTarget(0)
    , MaximumLeaderTargetDistance(5000.0f)
    , FieldTargeting(3)
    , LootPreference(0)
    , LootFleeThreshold(2)
    , ScanForCargoCount(0)
    , ScanForCargo()
    , FleeSceneThreatStyle(3)
    , FleeWhenHullDamagedPercent(0.0f)
    , bFleeWhenLeaderFleesStyle(1)
    , bFleeNoWeaponsStyle(1)
    , bAllowPlayerTargeting(1)
    , ScanChance(SCAN_CHANCE_DEFAULT)
    , bForceAttackFormation(0)
    , bForceAttackFormationSet(0)
{
    AttackPreferences[0].ShipClass = 11;      // ANYTHING
    AttackPreferences[0].Range = 5000.0f;
    AttackPreferences[0].WeaponFlags = 0xF;   // every weapon type
    AttackPreferences[1].ShipClass = 12;      // list terminator
    AttackSubtargetOrder[0] = 6;              // ANYTHING
    AttackSubtargetOrder[1] = 7;              // list terminator
}

Personality::JobStruct& Personality::JobStruct::operator=(JobStruct const& Other)
{
    // .text:062a8890: REP MOVSD of 0x4E dwords, no self test.
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

// ============================================================================
// Personality
// ============================================================================

// Export ordinal 1947, retail .rdata 0x063a05a8 = -1.0f. Seed for JobStruct::ScanChance
// (loaded at 062da9c1 and stored to JobStruct+0x130); see common_pub.h for the write-up.
float const Personality::SCAN_CHANCE_DEFAULT = -1.0f;

Personality::Personality()
    : EvadeDodgeParams()
    , EvadeBreakParams()
    , BuzzHeadParams()
    , BuzzPassParams()
    , TrailParams()
    , StrafeParams()
    , EngineKillParams()
    , RepairParams()
    , GunParams()
    , MineParams()
    , MissileParams()
    , DamageReaction()
    , MissileReaction()
    , CMParams()
    , FormationParams()
    , JobParams()
{
}

// No user copy constructor: retail exports none (see common_pub.h); the implicit one is the
// same bitwise copy.

Personality& Personality::operator=(Personality const& Other)
{
    std::memcpy(this, &Other, sizeof(*this));
    return *this;
}

float Personality::get_toughness_ratio(TOUGHNESS_TYPE Type)
{
    // RVA 0x48780: a hardcoded switch, not a table lookup.
    switch (Type)
    {
    case ToughnessEasiest: return 0.15f;
    case ToughnessEasy:    return 0.35f;
    case ToughnessEqual:   return 0.65f;
    case ToughnessHard:    return 0.85f;
    case ToughnessHardest: return 1.0f;
    default:               return 0.5f;
    }
}

double Personality::get_range_threshold_value()
{
    // .text:062a87e0: 5000.0f (DAT_0639e0c8) times the doubles 2/3 (DAT_0639e0f0) / 1/3
    // (DAT_0639e0e8), returned unrounded.
    double const Base = 5000.0f;
    if (JobParams.RangeThreshold == 1)
        return Base * 0.66666666666666663;
    if (JobParams.RangeThreshold == 2)
        return Base * 0.33333333333333331;
    return Base;
}

// ============================================================================
// BaseOp
// ============================================================================

BaseOp::BaseOp(OP_TYPE Type)
    : OpType(Type)
    , Priority(0)
{
}

BaseOp::BaseOp(BaseOp const& Other)
    : OpType(Other.OpType)
    , Priority(Other.Priority)
{
}

// ============================================================================
// Directive operations
// ============================================================================

DirectiveGotoOp::DirectiveGotoOp()
    : BaseOp(OpType_DirectiveGotoOp)
    , GotoKind(3)
    , Destination{0.0f, 0.0f, 0.0f}
    , TargetId(0)
    , SplineP1X(0), SplineP1Y(0), SplineP1Z(0), SplineP2X(0)
    , SplineP2Y(0), SplineP2Z(0), SplineP0X(0), SplineP0Y(0)
    , SplineP0Z(0), SplineP3X(0), SplineP3Y(0), SplineP3Z(0)
    , ArrivalRange(500.0f)
    , MaxSpeed(-1.0f)
    , bFlyThrough(0), Field_59(1), bGotoCruise(0), bGotoNoCruise(0)
    , TetherObjectId(0)
    , TetherNearRange(200.0f)
    , TetherFarRange(500.0f)
    , TetherCruiseRatio(0)
    , bDisableAvoidance(0)
{
}

DirectiveGotoOp::DirectiveGotoOp(DirectiveGotoOp const& Other)
    : BaseOp(Other)
    , GotoKind(Other.GotoKind)
    , Destination(Other.Destination)
    , TargetId(Other.TargetId)
    , SplineP1X(Other.SplineP1X), SplineP1Y(Other.SplineP1Y), SplineP1Z(Other.SplineP1Z), SplineP2X(Other.SplineP2X)
    , SplineP2Y(Other.SplineP2Y), SplineP2Z(Other.SplineP2Z), SplineP0X(Other.SplineP0X), SplineP0Y(Other.SplineP0Y)
    , SplineP0Z(Other.SplineP0Z), SplineP3X(Other.SplineP3X), SplineP3Y(Other.SplineP3Y), SplineP3Z(Other.SplineP3Z)
    , ArrivalRange(Other.ArrivalRange)
    , MaxSpeed(Other.MaxSpeed)
    , bFlyThrough(Other.bFlyThrough), Field_59(Other.Field_59), bGotoCruise(Other.bGotoCruise), bGotoNoCruise(Other.bGotoNoCruise)
    , TetherObjectId(Other.TetherObjectId)
    , TetherNearRange(Other.TetherNearRange)
    , TetherFarRange(Other.TetherFarRange)
    , TetherCruiseRatio(Other.TetherCruiseRatio)
    , bDisableAvoidance(Other.bDisableAvoidance)
{
}

bool DirectiveGotoOp::validate()
{
    // Common.dll.c 0x7aea0 — the NaN-safe-squared-magnitude checks in retail
    // collapse to plain "!= 0.0f" comparisons under IEEE-754 (NaN compares
    // unequal to everything, including itself).
    if (TargetId != 0 &&
        (Destination.x != 0.0f || Destination.y != 0.0f || Destination.z != 0.0f))
        return false;
    if (ArrivalRange < 0.0f)
        return false;
    if (bGotoCruise != 0 && bGotoNoCruise != 0)
        return false;
    if (TetherFarRange < TetherNearRange)
        return false;
    return true;
}

DirectiveFollowOp::DirectiveFollowOp()
    : BaseOp(OpType_DirectiveFollowOp)
    , TargetId(0)
    , FollowDistance(150.0f)
    , FollowOffsetX(0.0f)
    , FollowOffsetY(0.0f)
    , FollowOffsetZ(0.0f)
    , EngageRange(400.0f)
{
}

DirectiveFollowOp::DirectiveFollowOp(DirectiveFollowOp const& Other)
    : BaseOp(Other)
    , TargetId(Other.TargetId)
    , FollowDistance(Other.FollowDistance)
    , FollowOffsetX(Other.FollowOffsetX)
    , FollowOffsetY(Other.FollowOffsetY)
    , FollowOffsetZ(Other.FollowOffsetZ)
    , EngageRange(Other.EngageRange)
{
}

bool DirectiveFollowOp::validate()
{
    return TargetId != 0;
}

DirectiveFaceOp::DirectiveFaceOp()
    // .text:062dafb0 stores Mode, TargetId, FacePoint (z, y, x) and bKillEngine = 1 only;
    // Orientation stays as it was. The port zeroes it (over-initialisation).
    : BaseOp(OpType_DirectiveFaceOp)
    , Mode(0)
    , TargetId(0)
    , FacePoint{0.0f, 0.0f, 0.0f}
    , Orientation(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f)
    , bKillEngine(1)
{
}

DirectiveFaceOp::DirectiveFaceOp(DirectiveFaceOp const& Other)
    // .text:062a8b70: BaseOp words, Mode, TargetId, FacePoint (3 dwords), Orientation
    // (REP MOVSD 9), the byte at +0x44.
    : BaseOp(Other)
    , Mode(Other.Mode)
    , TargetId(Other.TargetId)
    , FacePoint(Other.FacePoint)
    , Orientation(Other.Orientation)
    , bKillEngine(Other.bKillEngine)
{
}

bool DirectiveFaceOp::validate()
{
    // Common.dll.c 0x7afe0
    if (Mode == 0)
        return false;
    if (Mode == 1 && TargetId == 0)
        return false;
    return true;
}

DirectiveStrafeOp::DirectiveStrafeOp()
    : BaseOp(OpType_DirectiveStrafeOp)
    , TargetId(0)
{
}

DirectiveStrafeOp::DirectiveStrafeOp(DirectiveStrafeOp const& Other)
    : BaseOp(Other)
    , TargetId(Other.TargetId)
{
}

bool DirectiveStrafeOp::validate()
{
    return TargetId != 0;
}

DirectiveTrailOp::DirectiveTrailOp()
    : BaseOp(OpType_DirectiveTrailOp)
    , TargetId(0)
    , TrailDistance(150.0f)
    , Field_14(0.0f)
    , Field_18(0.0f)
{
}

DirectiveTrailOp::DirectiveTrailOp(DirectiveTrailOp const& Other)
    : BaseOp(Other)
    , TargetId(Other.TargetId)
    , TrailDistance(Other.TrailDistance)
    , Field_14(Other.Field_14)
    , Field_18(Other.Field_18)
{
}

bool DirectiveTrailOp::validate()
{
    return TargetId != 0;
}

DirectiveRamOp::DirectiveRamOp()
    : BaseOp(OpType_DirectiveRamOp)
    , TargetId(0)
{
}

DirectiveRamOp::DirectiveRamOp(DirectiveRamOp const& Other)
    : BaseOp(Other)
    , TargetId(Other.TargetId)
{
}

bool DirectiveRamOp::validate()
{
    return TargetId != 0;
}

DirectiveTractorBeamOp::DirectiveTractorBeamOp()
    : BaseOp(OpType_DirectiveTractorBeamOp)
    , TargetId(0)
    , Field_10(1){
}

DirectiveTractorBeamOp::DirectiveTractorBeamOp(DirectiveTractorBeamOp const& Other)
    : BaseOp(Other)
    , TargetId(Other.TargetId)
    , Field_10(Other.Field_10){
}

bool DirectiveTractorBeamOp::validate()
{
    return TargetId != 0;
}

DirectiveFormationOp::DirectiveFormationOp()
    : BaseOp(OpType_DirectiveFormationOp)
{
}

DirectiveFormationOp::DirectiveFormationOp(DirectiveFormationOp const& Other)
    : BaseOp(Other)
{
}

bool DirectiveFormationOp::validate()
{
    return true; // Common.dll.c 0x1a710 (shared with Cancel/DrasticEvade/Idle)
}

DirectiveEvadeOp::DirectiveEvadeOp()
    : BaseOp(OpType_DirectiveEvadeOp)
    , ThreatId(0)
{
}

DirectiveEvadeOp::DirectiveEvadeOp(DirectiveEvadeOp const& Other)
    : BaseOp(Other)
    , ThreatId(Other.ThreatId)
{
}

bool DirectiveEvadeOp::validate()
{
    return ThreatId != 0;
}

DirectiveDrasticEvadeOp::DirectiveDrasticEvadeOp()
    : BaseOp(OpType_DirectiveDrasticEvadeOp)
    , Mode(0)
    , NudgeX(0)
    , NudgeY(0)
    , NudgeZ(0)
    , Duration(1.0f)
    , Field_20(0)
    , Field_24(0)
    , Field_28(0)
    , Field_2C(0)
{
}

DirectiveDrasticEvadeOp::DirectiveDrasticEvadeOp(DirectiveDrasticEvadeOp const& Other)
    : BaseOp(Other)
    , Mode(Other.Mode)
    , NudgeX(Other.NudgeX)
    , NudgeY(Other.NudgeY)
    , NudgeZ(Other.NudgeZ)
    , Duration(Other.Duration)
    , Field_20(Other.Field_20)
    , Field_24(Other.Field_24)
    , Field_28(Other.Field_28)
    , Field_2C(Other.Field_2C)
{
}

bool DirectiveDrasticEvadeOp::validate()
{
    return true; // Common.dll.c 0x1a710 (shared with Cancel/Formation/Idle)
}

DirectiveDockOp::DirectiveDockOp()
    : BaseOp(OpType_DirectiveDockOp)
    , DockObjectId(0)
    , NextRingId(0)
    , bHasDockPortIndex(0)    , DockPortIndex(-1)
    , TetherObjectId(0)
    , TetherNearRange(200.0f)
    , TetherFarRange(500.0f)
    , TetherCruiseRatio(0)
{
}

DirectiveDockOp::DirectiveDockOp(DirectiveDockOp const& Other)
    : BaseOp(Other)
    , DockObjectId(Other.DockObjectId)
    , NextRingId(Other.NextRingId)
    , bHasDockPortIndex(Other.bHasDockPortIndex)    , DockPortIndex(Other.DockPortIndex)
    , TetherObjectId(Other.TetherObjectId)
    , TetherNearRange(Other.TetherNearRange)
    , TetherFarRange(Other.TetherFarRange)
    , TetherCruiseRatio(Other.TetherCruiseRatio)
{
}

bool DirectiveDockOp::validate()
{
    // Common.dll.c 0x7b100
    if (DockObjectId == 0)
        return false;
    if (bHasDockPortIndex != 0 && DockPortIndex == -1)
        return false;
    if (TetherFarRange < TetherNearRange)
        return false;
    return true;
}

DirectiveLaunchOp::DirectiveLaunchOp()
    : BaseOp(OpType_DirectiveLaunchOp)
    , LaunchObjectId(0)
    , DockPortIndex(-1)
    , TerminalType(0)
{
}

DirectiveLaunchOp::DirectiveLaunchOp(DirectiveLaunchOp const& Other)
    : BaseOp(Other)
    , LaunchObjectId(Other.LaunchObjectId)
    , DockPortIndex(Other.DockPortIndex)
    , TerminalType(Other.TerminalType)
{
}

bool DirectiveLaunchOp::validate()
{
    // Common.dll.c 0x7b160
    if (LaunchObjectId == 0)
        return false;
    if (DockPortIndex == -1)
        return false;
    return TerminalType != 0;
}

DirectiveInstantTradelaneOp::DirectiveInstantTradelaneOp()
    : BaseOp(OpType_DirectiveInstantTradelaneOp)
    , TradeLaneId(0)
    , ExitRingId(0)
    , LaneSpeed(0)
{
}

DirectiveInstantTradelaneOp::DirectiveInstantTradelaneOp(DirectiveInstantTradelaneOp const& Other)
    : BaseOp(Other)
    , TradeLaneId(Other.TradeLaneId)
    , ExitRingId(Other.ExitRingId)
    , LaneSpeed(Other.LaneSpeed)
{
}

bool DirectiveInstantTradelaneOp::validate()
{
    return TradeLaneId != 0;
}

DirectiveDelayOp::DirectiveDelayOp()
    : BaseOp(OpType_DirectiveDelayOp)
    , DelaySeconds(0.0f)
{
}

DirectiveDelayOp::DirectiveDelayOp(DirectiveDelayOp const& Other)
    : BaseOp(Other)
    , DelaySeconds(Other.DelaySeconds)
{
}

bool DirectiveDelayOp::validate()
{
    return DelaySeconds != 0.0f;
}

DirectiveCancelOp::DirectiveCancelOp()
    : BaseOp(OpType_DirectiveCancelOp)
{
}

DirectiveCancelOp::DirectiveCancelOp(DirectiveCancelOp const& Other)
    : BaseOp(Other)
{
}

bool DirectiveCancelOp::validate()
{
    return true; // Common.dll.c 0x1a710 (shared with DrasticEvade/Formation/Idle)
}

DirectiveWaitForShip::DirectiveWaitForShip()
    : BaseOp(OpType_DirectiveWaitForShip)
    , ShipId(0)
    , Field_10(0)
{
}

DirectiveWaitForShip::DirectiveWaitForShip(DirectiveWaitForShip const& Other)
    : BaseOp(Other)
    , ShipId(Other.ShipId)
    , Field_10(Other.Field_10)
{
}

bool DirectiveWaitForShip::validate()
{
    return ShipId != 0;
}

DirectiveWaitForPlayerManeuverOp::DirectiveWaitForPlayerManeuverOp()
    : BaseOp(OpType_DirectiveWaitForPlayerManeuverOp)
    , BehaviorName(nullptr)
{
}

DirectiveWaitForPlayerManeuverOp::DirectiveWaitForPlayerManeuverOp(DirectiveWaitForPlayerManeuverOp const& Other)
    : BaseOp(Other)
    , BehaviorName(Other.BehaviorName)
{
}

bool DirectiveWaitForPlayerManeuverOp::validate()
{
    // Common.dll.c 0x7b230
    int id = BehaviorTypes::get_behavior_id(BehaviorName);
    return id < 0x15 && id != -1;
}

DirectiveIdleOp::DirectiveIdleOp()
    : BaseOp(OpType_DirectiveIdleOp)
{
}

DirectiveIdleOp::DirectiveIdleOp(DirectiveIdleOp const& Other)
    : BaseOp(Other)
{
}

bool DirectiveIdleOp::validate()
{
    return true; // Common.dll.c 0x1a710 (shared with Cancel/DrasticEvade/Formation)
}

// DirectiveGuideOp — Common.dll.c 0x7b2a0. Embeds a default-constructed
// DirectiveGotoOp at +0x0C and a default-constructed DirectiveFollowOp at
// +0x7C (retail writes the latter's fields inline rather than calling
// DirectiveFollowOp's ctor, but the values it writes are byte-for-byte
// DirectiveFollowOp's own defaults).
DirectiveGuideOp::DirectiveGuideOp()
    : BaseOp(OpType_DirectiveGuideOp)
    , GotoParams()
    , FollowParams()
    , bStartFollowing(1)    , SampleInterval(10.0f)
    , CourseAngleThreshold(0.3926991f)
    , Field_AC(3)
    , ProgressHighThreshold(10.0f)
    , ProgressLowThreshold(-10.0f)
    , GuideRange(400.0f)
{
}

DirectiveGuideOp::DirectiveGuideOp(DirectiveGuideOp const& Other)
    : BaseOp(Other)
    , GotoParams(Other.GotoParams)
    , FollowParams(Other.FollowParams)
    , bStartFollowing(Other.bStartFollowing)    , SampleInterval(Other.SampleInterval)
    , CourseAngleThreshold(Other.CourseAngleThreshold)
    , Field_AC(Other.Field_AC)
    , ProgressHighThreshold(Other.ProgressHighThreshold)
    , ProgressLowThreshold(Other.ProgressLowThreshold)
    , GuideRange(Other.GuideRange)
{
}

bool DirectiveGuideOp::validate()
{
    // Common.dll.c 0x7b350 — dispatches through both embedded objects' own
    // (real) vtables; calling their validate() directly is equivalent.
    return GotoParams.validate() && FollowParams.validate();
}

// ============================================================================
// Parameter structs
// ============================================================================
// SetFlagParams and SetZoneBehaviorParams derive from BaseOp too: their copy ctors
// (RVA 0x48DA0 / 0x48D10) patch BaseOp's vftable, then their own. See common_pub.h.

SetFlagParams::SetFlagParams()
    : BaseOp(OpType_SetFlagParams)
    , FlagId(2)
    , bValue(0){
}

SetFlagParams::SetFlagParams(SetFlagParams const& Other)
    : BaseOp(Other)
    , FlagId(Other.FlagId)
    , bValue(Other.bValue){
}

bool SetFlagParams::validate()
{
    return FlagId != 2;
}

SetPersonalityParams::SetPersonalityParams()
    : BaseOp(OpType_SetPersonalityParams)
    , StateGraphId(-1)
    , ContentCallbackObject((ContentCallback*)-1)
    , DirectiveCallbackObject((DirectiveCallback*)-1)
    , bSetPersonality(0)
    , Value()
{
}

SetPersonalityParams::SetPersonalityParams(SetPersonalityParams const& Other)
    : BaseOp(Other)
    , StateGraphId(Other.StateGraphId)
    , ContentCallbackObject(Other.ContentCallbackObject)
    , DirectiveCallbackObject(Other.DirectiveCallbackObject)
    , bSetPersonality(Other.bSetPersonality)
    , Value(Other.Value)
{
}

bool SetPersonalityParams::validate()
{
    // RVA 0x7ad50
    return StateGraphId != -1;
}

SetZoneBehaviorParams::SetZoneBehaviorParams()
    // .text:062dad60 writes neither Mode nor the box; the port zeroes them (over-initialisation).
    : BaseOp(OpType_SetZoneBehaviorParams)
    , Mode(0)
    , InvertMode(2)
    , TargetPoint{0.0f, 0.0f, 0.0f}
    , TargetId(0)
    , BoxMin{0.0f, 0.0f, 0.0f}
    , BoxMax{0.0f, 0.0f, 0.0f}
    , Radius(0.0f)
{
}

SetZoneBehaviorParams::SetZoneBehaviorParams(SetZoneBehaviorParams const& Other)
    : BaseOp(Other)
    , Mode(Other.Mode)
    , InvertMode(Other.InvertMode)
    , TargetPoint(Other.TargetPoint)
    , TargetId(Other.TargetId)
    , BoxMin(Other.BoxMin)
    , BoxMax(Other.BoxMax)
    , Radius(Other.Radius)
{
}

bool SetZoneBehaviorParams::validate()
{
    // .text:062dad90. A NaN radius passes; a NaN target sum fails.
    if (InvertMode != 2)
    {
        if (Radius == 0.0f)
            return false;
        if (TargetId == 0)
        {
            return ((double)TargetPoint.x * TargetPoint.x + (double)TargetPoint.y * TargetPoint.y) +
                   (double)TargetPoint.z * TargetPoint.z > 0.0;
        }
    }
    return true;
}


// ============================================================================
// ContentCallback -- the ten virtuals are header-inline (common_pub.h), as in
// retail, which compiled and exported them in each module that saw the header.
// ============================================================================

// ============================================================================
// DirectiveCallback
// ============================================================================

// Pure in retail (no DirectiveCallback vftable exists); nothing to define here.

}} // namespace pub::AI
