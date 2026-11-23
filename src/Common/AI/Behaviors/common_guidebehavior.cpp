// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_guidebehavior.h"
#include "common_pathfind.h" // PathfindManager (IBehaviorManager+0x0C)
#include "common_systemmanager.h" // SystemManager::Targeting / ExternalIface (NotifyManagerGuideEvent)

// ClampedDot3 (FUN_062e1630), GetIfacePositionOrZero (FUN_062e60e0),
// GetInterceptClearanceOfIface (FUN_062e4bb0), GetSignedRadialClearanceToPoint (FUN_062e4ea0).
#include "common_aisteer.h"

#include <cmath>
#include <cstdint>
#include <cstring>

// FUN_062cf230. Embedded ops: GotoOp at +0x68, FollowOp at +0xd8 (common_pub.h).
GuideBehavior::GuideBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , OwnerContextPtr14(nullptr)
    , ObjectDb(nullptr)
    , DeltaTime1c(0.0f)
    , BehaviorMgrPtr20(nullptr)
    , NavHelperPtr24(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , WatcherA()
    , GuideRange(0.0f)
    , Flag38(1)
    , Pad39{}
    , EscortMaxClearance(0.0f)
    , Field40(0)
    , EscortMinClearance(0.0f)
    , GuideDestination{}
    , ArrivalRadius(0.0f)
    , ProgressDelta(0.0f)
    , ApproachAngleRad(0.0f)
    , GotoDelegate(nullptr)
    , FollowDelegate(nullptr)
    , GotoOp()
    , FollowOp()
    , WatcherB()
    , SampleInterval(10.0f)
    , SampleElapsed(0.0f)
    , LastSampleDistance(0.0f)
    , Field110(0.0f)
    , FieldLow114(-10.0f)
    , FieldHigh118(10.0f)
    , AngleThreshold11c(0.39269909f)
    , Flag120(0)
{
    ConstructBase(InTypeId);   // FUN_062d2020
    // Not reproduced: retail then tests WatcherB (+0xfc) and set_pointer(nullptr)s it, which
    // is dead because the ctor has just zeroed it.
}

// FUN_062cf360 (scalar deleting FUN_062cf340): WatcherB (+0xfc) is cleared before WatcherA
// (+0x2c); ~Behavior (FUN_062d2080) chains after.
GuideBehavior::~GuideBehavior()
{
    WatcherB.set(nullptr);
    WatcherA.set(nullptr);
}

// Slot 1, LAB_062cf3e0: jumps to FUN_062d2090.
bool GuideBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    return BindContext(Owner, Manager, Sys);
}

// Slot 3, LAB_062cf3f0.
void GuideBehavior::OnEngage()
{
    Behavior::OnEngage();
    GotoDelegate = BehaviorMgrPtr20->BehaviorDispatchTable[2];       // Manager+0x17c (Goto)
    FollowDelegate = BehaviorMgrPtr20->BehaviorDispatchTable[17];      // Manager+0x1b8 (Follow)
    (void)GotoDelegate->OnApplyDirective(&GotoOp);
    GotoDelegate->OnEngage();
    (void)FollowDelegate->OnApplyDirective(&FollowOp);
    // Retail writes the follow behavior's fields directly.
    BaseFollowerBehavior* const Follow = static_cast<BaseFollowerBehavior*>(FollowDelegate);
    Follow->Flag28c = 1;
    Follow->MatchThrottle.SetFlag39(true);
    Follow->Move.SetField44(2);
    FollowDelegate->OnEngage();
    GuideDestination = GotoOp.Destination;
    ArrivalRadius = GotoOp.Field_50;
    SampleElapsed = 0.0f;
    // Summed (x, z, y) and square-rooted in double, rounded once.
    Vector Pos{ 0.0f, 0.0f, 0.0f };
    GetIfacePositionOrZero(&Pos, ObjRwFromWatchable(const_cast<Watchable*>(WatcherA.target())));
    double const Dx = static_cast<double>(Pos.x) - GuideDestination.x;
    double const Dy = static_cast<double>(Pos.y) - GuideDestination.y;
    double const Dz = static_cast<double>(Pos.z) - GuideDestination.z;
    Flag120 = Flag38;
    ProgressDelta = 0.0f;
    ApproachAngleRad = 0.0f;
    LastSampleDistance = static_cast<float>(std::sqrt((Dx * Dx + Dz * Dz) + Dy * Dy));
}

// Slot 4, FUN_062cf4f0.
void GuideBehavior::OnDisengage()
{
    if (GotoDelegate != nullptr)
    {
        GotoDelegate->OnDisengage();
        Behavior_ReleaseDirectiveTarget(GotoDelegate);               // FUN_062d21c0
    }
    if (FollowDelegate != nullptr)
    {
        FollowDelegate->OnDisengage();
        Behavior_ReleaseDirectiveTarget(FollowDelegate);
    }
    Behavior::OnDisengage();
}

// Slot 5, FUN_062cf530 (argument unread).
int GuideBehavior::OnEvaluateReadiness(float Value)
{
    (void)Value;
    if ((Flags28 & 0x2) != 0)
    {
        IObjRW* const Lead = ObjRwFromWatchable(const_cast<Watchable*>(WatcherA.target()));
        if (Lead != nullptr)
        {
            if (IsWithinInterceptRangeOfIface(&NavHelperPtr24->SelfTarget, Lead, GuideRange))
                ModeSelector0xc = (Flag29 != 0) ? 3 : 1;
            else
                ModeSelector0xc = (Flag29 != 0) ? 3 : 0;
            return ModeSelector0xc;
        }
        BehaviorMgrPtr20->SetDirectiveResult(0xe, 1);
    }
    ModeSelector0xc = (Flag29 != 0) ? 3 : 2;
    return ModeSelector0xc;
}

// Slot 10, LAB_062cfc70. The argument is a pub::AI::DirectiveGuideOp; the FollowParams fields
// it reads (+0x8c/+0x90..+0x98) and the GotoParams fields (+0x68/+0x6c/+0x70) are taken by
// raw offset off the op.
bool GuideBehavior::OnApplyDirective(void* DirectiveOp)
{
    pub::AI::DirectiveGuideOp const* const Op = static_cast<pub::AI::DirectiveGuideOp const*>(DirectiveOp);
    unsigned char const* const OpBytes = static_cast<unsigned char const*>(DirectiveOp);
    GotoDelegate = BehaviorMgrPtr20->BehaviorDispatchTable[2];
    FollowDelegate = BehaviorMgrPtr20->BehaviorDispatchTable[17];
    // Whole-object copies (0x1c and 9 dwords), vptr included.
    std::memcpy(static_cast<void*>(&GotoOp), &Op->GotoParams, sizeof(GotoOp));
    std::memcpy(static_cast<void*>(&FollowOp), &Op->FollowParams, sizeof(FollowOp));
    bool bResult = true;
    // WatcherB = the object GotoOp+0x1c names (FUN_062e74a0).
    IObjRW* const GotoTarget = NavHelperPtr24->Timers.FindVerifiedObject(
        (unsigned const*)((unsigned char*)this + 0x84));
    WatcherB.set(GotoTarget != nullptr ? static_cast<Watchable const*>(GotoTarget) : nullptr);
    SampleInterval = Op->Field_A4;
    AngleThreshold11c = Op->Field_A8;
    Field110 = static_cast<float>(Op->Field_AC);                    // int -> float (FILD)
    FieldHigh118 = Op->Field_B0;
    FieldLow114 = Op->Field_B4;
    if (*reinterpret_cast<int const*>(OpBytes + 0x68) != 0)
    {
        IObjRW* const Escort = NavHelperPtr24->Timers.FindVerifiedObject(
            (unsigned const*)(OpBytes + 0x68));
        WatcherA.set(Escort != nullptr ? static_cast<Watchable const*>(Escort) : nullptr);
        if (ObjRwFromWatchable(const_cast<Watchable*>(WatcherA.target())) != nullptr)
        {
            EscortMaxClearance = *reinterpret_cast<float const*>(OpBytes + 0x6c);
            Field40 = *reinterpret_cast<unsigned const*>(OpBytes + 0x70);
            // A positive |op+0x90| gives len + 100.0f (DAT_0639fb58), rounded once; otherwise
            // op+0x8c.
            float const* const V = reinterpret_cast<float const*>(OpBytes + 0x90);
            double const Len = std::sqrt((static_cast<double>(V[0]) * V[0]
                                          + static_cast<double>(V[1]) * V[1])
                                         + static_cast<double>(V[2]) * V[2]);
            if (Len > 0.0)
                EscortMinClearance = static_cast<float>(Len + 100.0);
            else
                EscortMinClearance = *reinterpret_cast<float const*>(OpBytes + 0x8c);
            if (!GotoDelegate->OnApplyDirective(&GotoOp))
                bResult = false;
            if (!FollowDelegate->OnApplyDirective(&FollowOp))
                bResult = false;
        }
        else
        {
            bResult = false;
        }
    }
    GuideRange = Op->Field_B8;
    Flag38 = Op->Field_A0;
    Flags28 = static_cast<unsigned char>((Flags28 & ~0x2u) | (bResult ? 0x2u : 0u));
    return bResult;
}

// Slot 21, LAB_062cf300.
void GuideBehavior::OnObjectLeftSystem(void* Arg)
{
    unsigned int const LeftId = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Arg));
    IObjRW* const Lead = ObjRwFromWatchable(const_cast<Watchable*>(WatcherA.target()));
    if (Lead == nullptr)
        return;
    if (Lead->get_id() == LeftId)
        WatcherA.set(nullptr);
}

// Slot 24, LAB_062cf2f0.
IObjRW const* GuideBehavior::get_target()
{
    return ObjRwFromWatchable(const_cast<Watchable*>(WatcherA.target()));
}

// ===========================================================================
// Slot 7 Update() (LAB_062cf5f0) and its notification path NotifyManagerGuideEvent
// (FUN_062d2a70). The layout and per-step reasoning are in common_guidebehavior.h.
// ===========================================================================

// FUN_062d2a70. All four callers are in Update(); the receiver is the IBehaviorManager.
bool GuideBehavior::NotifyManagerGuideEvent(IBehaviorManager* BehaviorMgr, unsigned SelfId, unsigned WatchedId,
                                           int EventCode, float Clearance, Vector const* Point)
{
    // Only while attacking (+0x1434) and Guide (0xe) is the current behavior.
    if (BehaviorMgr->Sys->Targeting.AttackState == 0 || BehaviorMgr->CurrentBehaviorIndex != 0xe)
        return false;

    // ContentCbPtr (+0x110) slot 2.
    if (BehaviorMgr->ContentCbPtr != nullptr)
    {
        BehaviorMgr->ContentCbPtr->lead_object_event(
            SelfId, WatchedId, (pub::AI::ContentCallback::LeadProgressType)EventCode, Clearance, *Point);
        return true;
    }

    // Retail still calls is_player() for effect before returning false.
    (void)BehaviorMgr->Sys->SelfTarget.Iface->is_player();
    return false;
}

// Shared body of Update()'s eight guide-event sites; only the event code differs.
void GuideBehavior::FireGuideEvent(IObjRW* Watched, FAiTarget* NavElem, int EventCode,
                                   float Clearance)
{
    unsigned const SelfId = NavElem->Iface->get_id();   // fetched first
    unsigned const WatchedId = ObjInspectOf(Watched)->get_id();
    NotifyManagerGuideEvent(BehaviorMgrPtr20, SelfId, WatchedId, EventCode, Clearance, &GuideDestination);
}

int GuideBehavior::Update()
{
    // ---- entry guards ----
    IObjRW* const ObjA = ObjRwFromWatchable(const_cast<Watchable*>(WatcherA.target()));
    if (ObjA == nullptr)
    {
        // Only this exit clears the flag bit.
        Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
        UpdateResult10 = 3;
        return 3;
    }
    if (OwnerContextPtr14 == nullptr)
    {
        UpdateResult10 = 3;
        return 3;
    }
    // Retail re-reads WatcherA and repeats both tests here; nothing in between can change it.

    FAiTarget* const NavElem = &NavHelperPtr24->SelfTarget;

    // ---- refresh the guide destination point ----
    int const GotoMode = GotoOp.Field_0C;
    if (GotoMode == 0)
    {
        IObjRW* const ObjB = ObjRwFromWatchable(const_cast<Watchable*>(WatcherB.target()));
        if (ObjB == nullptr)
        {
            Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
            BehaviorMgrPtr20->SetDirectiveResult(0xe, 1);
            UpdateResult10 = 2;
            return 2;
        }
        // Retail re-tests the mode here (062cf66d), which is always zero: dead, not reproduced.
        Vector WatchedBPos;
        GetIfacePositionOrZero(&WatchedBPos, ObjB);
        GuideDestination = WatchedBPos;
    }
    else if (GotoMode == 1)
    {
        GuideDestination = GotoOp.Destination;
    }
    // Any other mode keeps last tick's destination.

    ArrivalRadius = GotoOp.Field_50;

    // ---- this tick's three steering measurements, each rounded to float ----
    Vector WatchedAPos;
    GetIfacePositionOrZero(&WatchedAPos, ObjA);
    float const Clearance = static_cast<float>(GetInterceptClearanceOfIface(NavElem, ObjA));            // FUN_062e4bb0
    float const RadialClearance = static_cast<float>(GetSignedRadialClearanceToPoint(NavElem, GuideDestination)); // FUN_062e4ea0
    float const Dist = (float)Distance3(&WatchedAPos, &GuideDestination);                 // FUN_062e4f20

    // Both flags gate the event table further down.
    bool const bInsideRadial = (Dist < RadialClearance);
    bool bLeaderIsSelf = false;

    // ---- is the escorted object formed up on me? ----
    if (ObjInspectOf(ObjA)->get_behavior_id() == 0xa)
    {
        IObjRW* Leader = nullptr;
        ObjInspectOf(ObjA)->get_formation_leader(Leader);
        if (Leader != nullptr)
        {
            unsigned const NavSelfId = NavElem->Iface->get_id();
            if (ObjInspectOf(Leader)->get_id() == NavSelfId)
            {
                Flag120 = 0;
                bLeaderIsSelf = true;
            }
        }
    }

    // ---- ... or listed among my own formation followers ----
    if (NavHelperPtr24->GunConvergenceStateBytes.IsIdAmongOwnerFormationFollowers(
            ObjInspectOf(ObjA)->get_id())) // FUN_062ec520
    {
        Flag120 = 0;
    }

    // ---- sample the escort's progress, at most once per SampleInterval ----
    bool bSampled = false;
    if (SampleElapsed > SampleInterval)
    {
        ProgressDelta = LastSampleDistance - Dist;
        SampleElapsed = 0.0f;

        // From the escorted ship's own position to the guide point.
        Vector Delta;
        Delta.x = GuideDestination.x - WatchedAPos.x;
        Delta.y = GuideDestination.y - WatchedAPos.y;
        Delta.z = GuideDestination.z - WatchedAPos.z;

        Matrix const& Rotation = ObjInspectOf(ObjA)->get_transform().Rotation;
        Vector NegForward;
        NegForward.x = -Rotation.E[0][2];
        NegForward.y = -Rotation.E[1][2];
        NegForward.z = -Rotation.E[2][2];

        // VecNormalize3's port takes (Src, Out); retail pushes (Out, Src).
        Vector NormDelta;
        VecNormalize3(&Delta, &NormDelta);                                // FUN_062e1680
        ApproachAngleRad = std::acos(ClampedDot3(NegForward, NormDelta));          // FUN_062e1630

        LastSampleDistance = Dist;
        bSampled = true;
    }
    else
    {
        SampleElapsed = SampleElapsed + DeltaTime1c;
    }

    bool bSkipRadialTermination = false;

    if (Flag120 != 0)
    {
        // ---- event table for the "escort is following me" state ----
        if (bSampled)
        {
            if (ProgressDelta < FieldLow114)
            {
                if (!bInsideRadial)
                    FireGuideEvent(ObjA, NavElem, 8, Clearance);
                Flag120 = 0; // whether or not the event fired
            }
            else if (ProgressDelta > FieldHigh118)
            {
                if (ApproachAngleRad > AngleThreshold11c)
                {
                    if (!bInsideRadial)
                        FireGuideEvent(ObjA, NavElem, 6, Clearance);
                    Flag120 = 0;
                }
                else
                {
                    // The one arm that neither checks bInsideRadial nor clears Flag120.
                    FireGuideEvent(ObjA, NavElem, 5, Clearance);
                }
            }
            else if (Clearance < EscortMinClearance)
            {
                if (!bInsideRadial)
                    FireGuideEvent(ObjA, NavElem, 7, Clearance);
                Flag120 = 0;
            }
        }

        // ---- hand the flying to the right sibling ----
        if (Flag120 != 0)
        {
            // Still following. Retail discards the slot-7 result here (the mirror branch below
            // tests it against 2).
            *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(FollowDelegate) + 0x1c) = DeltaTime1c;
            FollowDelegate->OnUpdate();
        }
        else
        {
            // Transition to leading.
            FollowDelegate->OnDisengage();
            GotoDelegate->OnApplyDirective(&GotoOp);
            GotoDelegate->OnEngage();
        }
    }
    else
    {
        // ---- event table for the "escort is not following me" state ----
        if (bSampled)
        {
            if (bLeaderIsSelf)
            {
                FireGuideEvent(ObjA, NavElem, 4, Clearance);
            }
            else if (ProgressDelta < FieldLow114 && !bInsideRadial)
            {
                FireGuideEvent(ObjA, NavElem, 3, Clearance);
            }
            else
            {
                // Retail evaluates both compares up front and reads the Clearance one twice:
                // once as "less" (TEST AH,0x5) and once as C0 alone (TEST AH,0x1, also set on
                // unordered).
                bool const bFarAhead = (ProgressDelta > FieldHigh118);
                bool const bInsideField3c = (Clearance < EscortMaxClearance);
                bool const bField3cC0 = !(Clearance >= EscortMaxClearance);

                if (bFarAhead)
                {
                    if (bInsideField3c)
                    {
                        // Unordered skips the event.
                        if (ApproachAngleRad <= AngleThreshold11c)
                        {
                            FireGuideEvent(ObjA, NavElem, 1, Clearance);
                            Flag120 = 1;
                        }
                    }
                    else if (bInsideRadial)
                    {
                        FireGuideEvent(ObjA, NavElem, 4, Clearance);
                    }
                }
                else if (!bField3cC0 && !bInsideRadial)
                {
                    FireGuideEvent(ObjA, NavElem, 2, Clearance);
                    Flag120 = 1;
                }
            }
        }

        // ---- hand the flying to the right sibling ----
        if (Flag120 != 0)
        {
            // Transition to following.
            GotoDelegate->OnDisengage();
            FollowDelegate->OnApplyDirective(&FollowOp);
            FollowDelegate->OnEngage();
        }
        else
        {
            // Still leading: tick GotoDelegate. A result of 2 skips the radial termination test.
            *reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(GotoDelegate) + 0x1c) = DeltaTime1c;
            if (GotoDelegate->OnUpdate() == 2)
                bSkipRadialTermination = true;
        }
    }

    // ---- is the directive still running? ----
    // Both tests compare the unrounded return and keep going on greater OR unordered, hence
    // `!(a <= b)`.
    if (!bSkipRadialTermination)
    {
        if (!(GetSignedRadialClearanceToPoint(NavElem, GuideDestination) <= ArrivalRadius))
        {
            UpdateResult10 = 1;
            return 1;
        }
    }
    if (!(GetInterceptClearanceOfIface(NavElem, ObjA) <= GotoOp.Field_60))
    {
        UpdateResult10 = 1;
        return 1;
    }

    Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
    BehaviorMgrPtr20->SetDirectiveResult(0xe, 0);
    UpdateResult10 = 2;
    return 2;
}
