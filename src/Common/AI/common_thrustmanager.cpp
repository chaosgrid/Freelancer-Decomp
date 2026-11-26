// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- ThrustManager (SystemManager+0x18).

#include "common_systemmanager.h"
#include "common_aisteer.h"   // SustainedThresholdTimer, RefreshWeaponFireControlConfig
#include "common_behavior.h"  // Behavior_GetTransform (FUN_062e61f0)
#include "common_csys.h"      // Transform, Acquire3DMathEngine
#include "x86math.h"          // I3DMathEngine::TransposeTransform / TransformMatrix, Vector, Matrix
#include "common_misc.h"      // SUBOBJ_ID_NONE
#include "common_iface.h"     // IBehaviorManager::Pathfind, IObjInspect, ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager::IsCruiseUnobstructed (FUN_062de160)
#include "common_iustategraph.h" // ClearAiTargetVector, BuildAiTargetList (ScanGunGroupTargets)
#include "common_cobject.h"    // CEqObj::EquipManager / get_behavior_interface (ScanGunGroupTargets)
#include "common_cequip.h"     // CELauncher::cast / GetBarrelPosWS (ScanGunGroupTargets)
#include "common_cobjdb.h"     // IObjDB::FindById (AppendSystemManagerTimerListEntry)
#include "dacom.h"             // DACOM_API

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <new>

// ============================================================================
// ThrustManager -- SystemManager+0x18. See common_systemmanager.h for the
// layout derivation.
// ============================================================================

// FUN_062e6490's ThrustManager initialiser block.
void InitThrustManager(ThrustManager* Self, SystemManager* Owner)
{
    Self->Owner = Owner;
    Self->MaxThrottle = 5.0f;
    Self->AxisThrottle.x = 0.0f;
    Self->AxisThrottle.y = 0.0f;
    Self->AxisThrottle.z = 0.0f;
    Self->bCruiseEngaged = 0;
    Self->bCruiseUnobstructed = 1;
    Self->bEngineActive = 0;
    Self->bAutoLevel = 1;
    Self->bAutoAvoidance = 1;

    // A player ship gets no auto-avoidance.
    IObjRW* const Target =
        ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()));
    if (Target != nullptr && ObjInspectOf(Target)->is_player())
        Self->bAutoAvoidance = 0;
}

// ThrustManager::engage_cruise, .text:062e90a0 (`?engage_cruise@ThrustManager@@QAEX_N@Z`).
// A disengage always runs the body, an engage only while bCruiseUnobstructed. Retail passes
// its own argument slot as is_cruise_active's first out-param (harmless: the argument was
// already copied to EBX); an explicit local here.
void ThrustManager::engage_cruise(bool bEngage)
{
    unsigned char const EngageByte = bEngage ? 1 : 0;

    if (EngageByte == 1 && bCruiseUnobstructed == 0)
        return;

    // FUN_062e91d0. No Owner test, as in retail.
    SetEngineActive(true);

    // The owner ship is not null-tested, as in retail.
    bool CruiseActive = false;
    bool TradeLaneActive = false;  // retail's `local_1` scratch byte, never read again
    (void)OwnerShip()->is_cruise_active(CruiseActive, TradeLaneActive);

    unsigned char const ActiveByte = CruiseActive ? 1 : 0;
    if (ActiveByte == EngageByte && bCruiseEngaged == EngageByte)
        return;

    ObjAffectOf(OwnerShip())->toggle_cruise(EngageByte != 0, false, -1);

    bCruiseEngaged = EngageByte;
}

// FUN_062e92d0
void ThrustManager::SetSteeringFallbackFlag(unsigned char Value)
{
    bAutoLevel = Value;
}

// 0x06263bd0, an ICF fold shared by CNudgeEngine::Activate and three unrelated setters; on a
// ThrustManager receiver (from update_current_behavior_auto_avoidance) it writes
// bAutoAvoidance and returns the argument.
bool ThrustManager::SetAutoAvoidance(bool bOn)
{
    bAutoAvoidance = bOn ? 1 : 0;
    return bOn;
}

// FUN_062e92e0. No null tests anywhere, as in retail.
bool ThrustManager::CheckSteeringGate()
{
    if (Owner->Owner->get_camera_level_status())
        return true;

    if (Owner->SelfTarget.Iface->is_player())
    {
        int const BehaviorIndex = Owner->Owner->CurrentBehaviorIndex;
        if (BehaviorIndex == BEHAVIOR_FORMATION || BehaviorIndex == BEHAVIOR_DOCK)
            return true;
    }

    return bAutoLevel != 0;
}

// ============================================================================
// The throttle / nudge / engine / thruster setters. Each re-resolves the owner ship before
// every interface call, as retail does; none null-tests it beyond the entry gates noted.
// ============================================================================

IObjRW* ThrustManager::OwnerShip() const
{
    return ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()));
}

// FUN_062e91d0, RET 4.
void ThrustManager::SetEngineActive(bool bActive)
{
    if (OwnerShip() == nullptr)
        return;

    // A non-player whose cached byte already equals bActive returns.
    bool const bPlayer = Owner->SelfTarget.Iface->is_player();
    if (!bPlayer && bEngineActive == (unsigned char)bActive)
        return;

    // SUBOBJ_ID_NONE is .rdata:063a4aa4.
    unsigned short Id = SUBOBJ_ID_NONE;
    ObjInspectOf(OwnerShip())->find_equipment(&Id, 1, EquipTypeEngine);

    if (Id != SUBOBJ_ID_NONE)
    {
        EquipStatus Status{};
        ObjInspectOf(OwnerShip())->get_equipment_status(Status, Id);
        unsigned char const StatusByte = Status.Flags;

        // Gate on the status byte just written (Ghidra conflates it with bActive): not
        // destroyed and the active bit differs from the request.
        if ((StatusByte & EquipStatusFlagDestroyed) == 0 &&
            ((StatusByte & EquipStatusFlagActive) != 0) != bActive)
        {
            ObjAffectOf(OwnerShip())->toggle_item(&Id, bActive, -1);
        }
    }

    bEngineActive = (unsigned char)bActive;
}

// FUN_062e8c20, RET 4.
void ThrustManager::SetAxisThrottle(Vector const& Wanted)
{
    if (OwnerShip() == nullptr)
        return;

    if (ObjInspectOf(OwnerShip())->get_axis_throttle(AxisThrottle) != 0)
        return;

    // Epsilon = DAT_063a07e4 raw bits (~1.049e-6f).
    unsigned int const EpsilonBits = 0x358637BEu;
    float Epsilon;
    std::memcpy(&Epsilon, &EpsilonBits, sizeof(Epsilon));

    // The squares stay unrounded; greater or unordered counts as moved.
    double const DeltaX = (double)Wanted.x - AxisThrottle.x;
    double const DeltaY = (double)Wanted.y - AxisThrottle.y;
    double const DeltaZ = (double)Wanted.z - AxisThrottle.z;
    if (DeltaX * DeltaX <= Epsilon && DeltaY * DeltaY <= Epsilon && DeltaZ * DeltaZ <= Epsilon)
        return;

    ObjAffectOf(OwnerShip())->set_axis_throttle(Wanted);

    AxisThrottle = Wanted;
}

// FUN_062e9030, RET 4.
void ThrustManager::SetStrafeDir(StrafeDir Dir)
{
    SetEngineActive(true);

    StrafeDir Current = StrafeNone;
    if (ObjInspectOf(OwnerShip())->get_strafe_dir(Current) != 0)
        return;
    if (Current == Dir)
        return;

    ObjAffectOf(OwnerShip())->set_strafe_dir(Dir);
}

// FUN_062e9120, RET 4.
void ThrustManager::SetThrustersActive(bool bActive)
{
    bool bCruise = false;
    bool bTradeLane = false;
    (void)ObjInspectOf(OwnerShip())->is_cruise_active(bCruise, bTradeLane);
    if (bCruise && !bTradeLane)
        return;

    if (bActive)
        SetEngineActive(true);

    bool bThrusters = false;
    (void)ObjInspectOf(OwnerShip())->are_thrusters_active(bThrusters);
    if (bThrusters == bActive)
        return;

    ObjAffectOf(OwnerShip())->toggle_thrusters(bActive, -1);
}

// FUN_062e89a0, RET 8.
bool ThrustManager::SetThrottle(float Throttle, int Mode)
{
    if (OwnerShip() == nullptr)
        return false;

    // Retail asks is_player and discards the answer.
    (void)ObjInspectOf(OwnerShip())->is_player();

    float Clamped = (Throttle > MaxThrottle) ? MaxThrottle : Throttle;
    int const EffectiveMode = (bCruiseUnobstructed == 0 && Mode == kThrottleModeCruise) ? kThrottleModeEngine : Mode;

    if (EffectiveMode != kThrottleModeEngine && EffectiveMode != kThrottleModeCruise)
        return true;

    if (EffectiveMode == kThrottleModeCruise)
    {
        // DAT_063a07a0 / DAT_06399408.
        if (Clamped > 1.2f) Clamped = 1.2f;
        if (Clamped < 0.0f) Clamped = 0.0f;

        bool bCruise = false;
        bool bTradeLane = false;
        (void)ObjInspectOf(OwnerShip())->is_cruise_active(bCruise, bTradeLane);

        // Cruising outside a trade lane: the set_throttle commit moves to the tail.
        bool const bDeferCommitToTail = (bCruise && !bTradeLane);
        if (!bDeferCommitToTail)
            ObjAffectOf(OwnerShip())->set_throttle(Clamped);

        if (Clamped > 0.0f && bCruiseUnobstructed != 0)
        {
            SetEngineActive(true);

            bool bCruiseNow = false;
            bool bTradeLaneNow = false;
            (void)ObjInspectOf(OwnerShip())->is_cruise_active(bCruiseNow, bTradeLaneNow);

            if (!(bCruiseNow && bCruiseEngaged == 1))
            {
                ObjAffectOf(OwnerShip())->toggle_cruise(true, false, -1);
                bCruiseEngaged = 1;
            }
        }

        if (bDeferCommitToTail)
            ObjAffectOf(OwnerShip())->set_throttle(Clamped);
        return true;
    }

    // Mode 0: DAT_063a03f0 / DAT_06399410.
    if (Clamped < -1.0f) Clamped = -1.0f;
    else if (Clamped > 1.0f) Clamped = 1.0f;

    SetEngineActive(true);

    bool bCruise = false;
    bool bTradeLane = false;
    (void)ObjInspectOf(OwnerShip())->is_cruise_active(bCruise, bTradeLane);

    if (!(!bCruise && bCruiseEngaged == 0))
    {
        ObjAffectOf(OwnerShip())->toggle_cruise(false, false, -1);
        bCruiseEngaged = 0;
    }

    SetEngineActive(true);

    ObjAffectOf(OwnerShip())->set_throttle(Clamped);
    return true;
}

// FUN_062e8ce0, RET 4.
void ThrustManager::SetNudgeDirection(Vector const& WorldDir)
{
    SetEngineActive(true);

    // Normalised (FUN_062e1680) only when longer than 1 (unordered is kept).
    Vector Local = WorldDir;
    if ((double)Local.y * Local.y + (double)Local.z * Local.z + (double)Local.x * Local.x > 1.0)
    {
        Vector Normalized;
        Local = *VecNormalize3(&Local, &Normalized);
    }

    // Local into ship space (FUN_062e61f0 for the frame; DAT_063fca58 is this function's
    // engine cache) -- the nudge vector to apply.
    Transform const* const Frame = Behavior_GetTransform(OwnerShip());
    Vector Nudge;
    Acquire3DMathEngine()->TransposeTransform(Nudge, Frame->Rotation, Local);

    // A failed query returns without committing.
    Vector Current;
    if (ObjInspectOf(OwnerShip())->get_nudge_vec(Current) != 0)
        return;

    // Commit when any axis differs by more than 1e-6f (DAT_063a07e4) squared; unordered
    // commits.
    double const DeltaX = (double)Nudge.x - Current.x;
    double const DeltaY = (double)Nudge.y - Current.y;
    double const DeltaZ = (double)Nudge.z - Current.z;
    float const Epsilon = 1.0000001111620804e-06f;                  // 0x358637be
    if (!(DeltaX * DeltaX <= Epsilon) || !(DeltaY * DeltaY <= Epsilon) || !(DeltaZ * DeltaZ <= Epsilon))
    {
        ObjAffectOf(OwnerShip())->set_nudge_vec(Nudge);
    }
}

// FUN_062e8e60 (Ghidra's decompile of it has bogus `unaff_ESI`/`unaff_EDI`).

// .data:063ec6ac: in .data, but only ever read (three times, all here) -- a fixed tuning
// constant.
static float const NudgeBlendScale = 1.5f;

// .rdata:0639e6cc
static float const NudgeAlignmentThreshold = 0.99f;

void ThrustManager::BlendNudgeDirection(Vector const& WorldDir, float DeltaTime)
{
    Vector Working = WorldDir;

    // No null test on the owner ship, as in retail.
    Vector NudgeLocal;
    ObjInspectOf(OwnerShip())->get_nudge_vec(NudgeLocal);

    // The nudge into world space by the own ship's transform (DAT_063fca58 engine cache).
    Vector NudgeWorld;
    {
        Matrix const& Orientation = Owner->SelfTarget.Iface->get_transform().Rotation;
        Acquire3DMathEngine()->TransformMatrix(NudgeWorld, Orientation, NudgeLocal);
    }

    // A current nudge not aligned with the wanted direction (dot < 0.99f) is dropped.
    {
        Vector NormWorking;
        Vector NormNudge;
        VecNormalize3(&Working, &NormWorking);
        VecNormalize3(&NudgeWorld, &NormNudge);
        if (ClampedDot3(NormNudge, NormWorking) < (double)NudgeAlignmentThreshold)
            NudgeWorld = Vector{ 0.0f, 0.0f, 0.0f };
    }

    // Both lengths stay unrounded in retail's sum order; blend only when |Working| > |Nudge|.
    double const LenWorking = std::sqrt(((double)Working.y * Working.y +
                                         (double)Working.x * Working.x) +
                                        (double)Working.z * Working.z);
    double const LenNudge = std::sqrt(((double)NudgeWorld.y * NudgeWorld.y +
                                       (double)NudgeWorld.z * NudgeWorld.z) +
                                      (double)NudgeWorld.x * NudgeWorld.x);
    if (LenWorking > LenNudge)
    {
        // Roundings exactly where retail stores to float (they differ per axis).
        double const K = NudgeBlendScale;
        float const XScaled = (float)((K * Working.x) * DeltaTime);
        float const XTerm = (float)(XScaled * LenWorking);
        float const YTerm = (float)(((K * Working.y) * DeltaTime) * LenWorking);
        float const ZScaled = (float)(K * Working.z);
        double const ZTerm = ((double)ZScaled * DeltaTime) * LenWorking;
        Working.x = (float)((double)XTerm + NudgeWorld.x);
        Working.y = (float)((double)NudgeWorld.y + YTerm);
        Working.z = (float)((double)NudgeWorld.z + ZTerm);
    }

    SetNudgeDirection(Working);
}
