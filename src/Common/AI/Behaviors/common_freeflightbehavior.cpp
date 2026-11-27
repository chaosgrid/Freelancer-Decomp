// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_freeflightbehavior.h"
#include "common_systemmanager.h"  // SystemManager::Thrust (ThrustManager::bAutoAvoidance)

// FUN_062c6d10. Flag3fc is not written by retail; it is value-initialised here and OnEngage
// clears it before anything could read it.
FreeFlightBehavior::FreeFlightBehavior(int InTypeId)
    : GotoBehavior(InTypeId)
    , Flag3fc(0)
    , Pad3fd{}
{
    Flag2eb = 1;
}

// FUN_062c6df0 (scalar deleting FUN_062c6dd0): only the ~GotoBehavior chain.
FreeFlightBehavior::~FreeFlightBehavior()
{
}

// Slot 3, FUN_062c6ee0.
void FreeFlightBehavior::OnEngage()
{
    Behavior::OnEngage();
    Flags28 = static_cast<unsigned char>(Flags28 | kBehaviorFlagDirectiveAccepted);
    Flag365 = 1;
    Flag3fc = 0;
    Move.SetAngle38(3.14159274f);                             // 0x40490fdb
    ArrivalRange = 0.0f;
    Move.SetRange3c(0.0f);
    Move.SetEnabled(false);                                   // FUN_062d5a10
    Move.SetField44(kMoveStopFlyThrough);                                       // FUN_062d88b0
    // The lock value is ThrustManager::bAutoAvoidance, read through the ICF fold named
    // EquipDesc::get_temporary.
    SystemManager* const Sys = SysMgr;
    SetAvoidanceAllowed(Sys->Thrust.bAutoAvoidance, kAvoidanceObstacleNudge | kAvoidancePoint);                   // FUN_062d21d0
    GotoKind = kGotoKindPoint;
    Flag2ea = 1;
    SteerAxis.x = 0.0f;
    SteerAxis.y = 0.0f;
    SteerAxis.z = 0.0f;
    OwnerContext->get_throttle(UserThrottle);
    OwnerContext->is_cruise_active(reinterpret_cast<bool&>(Flag2e8));
    // Unordered keeps the throttle.
    if (UserThrottle < 0.0f)
    {
        UserThrottle = 1.0f;
        Flag2ec = 1;
    }
    else
    {
        Flag2ec = 0;
    }
    Destination.x = 1000000.0f;
    Destination.y = 1000000.0f;
    Destination.z = 1000000.0f;
}

// Slot 4, LAB_062c6fc0: jumps to Behavior's slot 4, bypassing GotoBehavior::OnDisengage.
void FreeFlightBehavior::OnDisengage()
{
    Behavior::OnDisengage();
}

// Slot 6, LAB_062d1730 (ICF-shared with CruiseBehavior's slot 6 and NullBehavior's slot 5).
EvaluateResult FreeFlightBehavior::evaluate(unsigned int Context)
{
    (void)Context;
    return EvalBlocked;
}

// Slot 10, LAB_062c6fd0 (argument unread; the op is not copied).
bool FreeFlightBehavior::OnApplyDirective(void* DirectiveOp)
{
    (void)DirectiveOp;
    Flags28 = static_cast<unsigned char>((Flags28 & ~kBehaviorFlagDisengageRequested) | kBehaviorFlagDirectiveAccepted);
    SteerAxis.x = 0.0f;
    SteerAxis.y = 0.0f;
    SteerAxis.z = 0.0f;
    UserThrottle = 0.0f;
    Flag2e8 = 0;
    return true;
}

// Slot 15, LAB_062c7000.
void FreeFlightBehavior::engage_engine(bool bOn)
{
    Flag2e9 = bOn ? 1 : 0;
    Flag2e8 = 0;
}
