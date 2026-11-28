// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_itlbehavior.h"
#include "common_cobject.h"  // CObject::ObjectClass, CShip::request_exit_tradelane
#include "common_systemmanager.h"  // SystemManager::Thrust -- TurnOff

// FUN_062cee50. Behavior's ctor FUN_062d2020 runs first. Deviation: retail then installs the
// two vtables and writes nothing else; the port zero-initialises every field.
InstantTradeLaneBehavior::InstantTradeLaneBehavior(int InTypeId)
    : Behavior(InTypeId)
    , State30(kItlStateStart)
    , EntranceRing(nullptr)
    , ExitRing(nullptr)
    , LaneStart3c()
    , LaneEnd48()
    , Flags54(0)
    , Flag55(0)
    , Flag56(0)
    , Pad57(0)
    , LaneSpeed(0)
{
}

// Scalar deleting FUN_062c7090 (ICF-shared) only chains to ~Behavior; this class owns no
// watchers.
InstantTradeLaneBehavior::~InstantTradeLaneBehavior()
{
}

// Slot 1, LAB_062cee70. The fields are cleared only when BindContext (FUN_062d2090) succeeds.
bool InstantTradeLaneBehavior::SetContext(IObjRW* Param1, IBehaviorManager* Param2, SystemManager* Param3)
{
    bool const bInstalled = BindContext(Param1, Param2, Param3);

    if (bInstalled)
    {
        Flag55 = 0;
        Flag56 = 0;
        Flags54 = 0;
        EntranceRing = nullptr;
        ExitRing = nullptr;
        LaneSpeed = 0;
    }

    return bInstalled;
}

// Slot 3, FUN_062cef10.
void InstantTradeLaneBehavior::TurnOff()
{
    if (OwnerContext == nullptr)
        return;
    SetAvoidanceAllowed(0, kAvoidanceObstacleNudge | kAvoidancePoint);                                                    // FUN_062d21d0
    State30 = kItlStateStart;
    Behavior::OnEngage();
    SysMgr->Thrust.engage_cruise(false);
}

// Slot 4, FUN_062cef40.
void InstantTradeLaneBehavior::RequestExitTradeLane()
{
    // The null test gates the whole body, tail included.
    if (OwnerContext == nullptr)
        return;

    bool const bWasStatusSet = (Flags54 != 0);
    Flag55 = 0;
    Flag56 = 0;
    Flags28 |= kBehaviorFlagDisengageRequested;

    if (bWasStatusSet)
    {
        Flags54 = 0;

        CObject const* const Obj = OwnerContext->cobject();

        CShip* Ship = nullptr;
        if (Obj != nullptr && (Obj->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
            Ship = (CShip*)Obj;

        // Retail calls CShip::request_exit_tradelane on both paths, i.e. with a null `this`
        // whenever cobject() is null or fails the class test; reproduced without a guard.
        Ship->request_exit_tradelane();
    }

    Behavior::OnDisengage();
    EntranceRing = nullptr;
    SetAvoidanceAllowed(0, kAvoidanceObstacleNudge | kAvoidancePoint);
}

// Slot 5, LAB_062ceeb0.
int InstantTradeLaneBehavior::UpdateMode()
{
    int Mode = 2;

    if (OwnerContext != nullptr)
    {
        if (Flags54 != 0)
            Mode = 1;

        if (EntranceRing != nullptr)
        {
            // Retail tests only the sign bit of the out-parameter's low byte (`TEST AL,AL;
            // JNS`); get_type's return value is discarded.
            unsigned SubObjectType = 0;
            EntranceRing->get_type(SubObjectType);
            if ((SubObjectType & OBJ_TRADELANE_RING) != 0)
                Mode = 1;
        }
    }

    if (Flag29 != 0)
        Mode = 3;

    Readiness = Mode;
    return Mode;
}

// Slot 7, LAB_062cefb0: a 7-way switch on State30.
int InstantTradeLaneBehavior::Update()
{
    // The exit-tradelane bit is already set.
    if (Flags28 & kBehaviorFlagDisengageRequested)
    {
        UpdateResult = kBehaviorUpdateFinished;
        return UpdateResult;
    }

    if (OwnerContext == nullptr)
    {
        UpdateResult = kBehaviorUpdateAborted;
        return UpdateResult;
    }

    // States above 6 join case 1. The vtable calls below are on OwnerContext (ECX is never
    // reloaded after the guard), not on this object.
    switch (State30)
    {
    case kItlStateStart:
    {
        if (ExitRing == nullptr)
        {
            IObjInspect const* OutLane = nullptr;
            int const LaneResult = OwnerContext->get_lane_start(
                EntranceRing, OutLane, LaneStart3c, LaneEnd48);

            // Zero is success.
            if (LaneResult != 0)
            {
                UpdateResult = kBehaviorUpdateAborted;
                return kBehaviorUpdateAborted;
            }
            ExitRing = OutLane;
        }

        UpdateResult = kBehaviorUpdateRunning;
        State30 = kItlStateEnter;
        Flag56 = 1;
        Flag55 = 0;
        return kBehaviorUpdateRunning;
    }

    case kItlStateEnter:
    {
        // IObjAffect slot 25 on OwnerContext+4.
        int const AffectResult = OwnerContext->go_tradelane(EntranceRing, ExitRing, true, LaneSpeed);

        // Zero is success; the failure path still returns 1.
        if (AffectResult != 0)
        {
            UpdateResult = kBehaviorUpdateRunning;
            State30 = kItlStateFailed;
            return kBehaviorUpdateRunning;
        }

        // FUN_062d4520 with Extra 0 and Duration 0.0f: OwnerContext's formation followers get
        // notification 0 at once. No null test on the manager.
        BehaviorMgr->QueueNotification(OwnerContext, 0, 0.0f);

        UpdateResult = kBehaviorUpdateBusy;
        Flags54 = 1;
        State30 = kItlStateInLane;
        return kBehaviorUpdateBusy;
    }

    case kItlStateLeftLane:
    case kItlStateFailed:
    {
        // Retail picks the constant at the case label, before the virtual OnDisengage call,
        // so it must not be re-derived from State30 afterwards.
        int const ManagerArg118 = (State30 == kItlStateFailed) ? 2 : 0;

        Flags28 = static_cast<unsigned char>(Flags28 | kBehaviorFlagDisengageRequested);
        OnDisengage();
        BehaviorMgr->SetDirectiveResult(BEHAVIOR_INSTANT_TRADE_LANE, ManagerArg118);    // FUN_062d29a0
        UpdateResult = kBehaviorUpdateFinished;
        return kBehaviorUpdateFinished;
    }

    case 1:
    case 2:
    case kItlStateInLane:
    default:
        // Still running.
        UpdateResult = kBehaviorUpdateRunning;
        return kBehaviorUpdateRunning;
    }
}

// Slot 10, LAB_062cf0f0. Both lookups (FUN_062e74a0) go through the SysMgr (+0x24), not
// the manager at +0x20.
bool InstantTradeLaneBehavior::AttachDirective(void* Directive)
{
    Flags28 &= ~kBehaviorFlagDisengageRequested;

    pub::AI::DirectiveInstantTradelaneOp const* const Op =
        static_cast<pub::AI::DirectiveInstantTradelaneOp const*>(Directive);
    SystemManagerTimerTable const* const LookupThis = &SysMgr->Timers;

    EntranceRing = LookupThis->FindVerifiedObject(&Op->TradeLaneId);
    ExitRing = LookupThis->FindVerifiedObject(reinterpret_cast<unsigned const*>(&Op->ExitRingId));

    LaneSpeed = *reinterpret_cast<float const*>(&Op->LaneSpeed);   // float bits

    if (EntranceRing != nullptr)
        Flags28 |= kBehaviorFlagDirectiveAccepted;
    else
        Flags28 &= ~kBehaviorFlagDirectiveAccepted;

    return EntranceRing != nullptr;
}

// Slot 22, LAB_062cf1d0. Proceeds only for Tag 4 with a zero first payload byte.
void InstantTradeLaneBehavior::HandleTargetEvent(void* Event)
{
    FBehaviorManagerEvent const* Ev = static_cast<FBehaviorManagerEvent const*>(Event);

    if (Ev->Type != FBehaviorManagerEvent::kEventTradelane ||
        static_cast<FBehaviorTradelaneEvent const*>(Ev->Data)->bEnteringLane != 0)                         // no null test
        return;

    if (OwnerContext->get_id() != static_cast<FBehaviorTradelaneEvent const*>(Ev->Data)->ShipId)
        return;

    // Set regardless of EntranceRing; its null test gates only the cancel below.
    State30 = kItlStateLeftLane;

    if (EntranceRing == nullptr)
        return;

    // Retail uses its own incoming parameter slot as the id scratch.
    unsigned SavedId = EntranceRing->get_id();
    (void)OwnerContext->request_cancel(kRequestEventTradelane, &SavedId, 0);
}

// IObjAffect::RequestCB override, the secondary vtable's single slot (LAB_062cf150); retail
// recovers the outer object from the adjusted `this`.
bool InstantTradeLaneBehavior::OnResult(int /*Param1*/, int /*Param2*/, int Param3)
{
    Flag55 = 0;

    if (Param3 != -1)
    {
        Flag56 = 1;
        State30 = kItlStateEnter;
        return false;
    }

    State30 = kItlStateFailed;

    // Retail also re-tests Flag55 here, which is dead since it was just zeroed; only Flag56
    // gates the block.
    if (Flag56 != 0)
    {
        // Same (2, &SavedId, 0) shape as HandleTargetEvent; both calls unguarded in retail.
        unsigned SavedId = EntranceRing->get_id();
        (void)OwnerContext->request_cancel(kRequestEventTradelane, &SavedId, 0);

        Flag55 = 0;
        Flag56 = 0;
    }

    if (Flags54 != 0)
    {
        Flags54 = 0;
        State30 = kItlStateFailed;
    }

    return false;
}
