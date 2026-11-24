// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_evadebehavior.h"

#include "common_aisteer.h"        // IsPointOutsideHeadingCone, IsWithinInterceptRangeOfIface,
                                   // GetIfaceVectorMagnitude
#include "common_csys.h"           // Transform
#include "common_iface.h"          // IObjInspect, IObjRW, ObjInspectOf, ObjRwFromWatchable
#include "common_iustategraph.h"   // FindClosestHostileTarget
#include "common_systemmanager.h"  // SystemManager -- what SysMgr points at

#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "x87_trig.h" // X87CosF (OnEngage's FCOS)

// EvadeBehavior; see common_evadebehavior.h. FUN_062e74a0 is
// SystemManagerTimerTable::FindVerifiedObject.

namespace
{
    // .rdata:063997d0, 1/32768, the rand() scale every pick below uses.
    float const GRandScale = 3.0517578125e-05f;

    // The family's `Ptr ? Ptr - 8 : 0` downcast.
    IObjRW* WatchedObject(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }

    // Watched and not the 8 sentinel.
    bool IsWatching(BaseWatcher const& Watcher)
    {
        return WatchedObject(Watcher) != nullptr;
    }

    // FUN_062e53e0 on the watched object's position against the owner's heading cone.
    bool WatchedOutsideCone(BaseWatcher const& Watcher, void* Owner, float Cos)
    {
        IObjRW* const Watched = WatchedObject(Watcher);
        return IsPointOutsideHeadingCone(Watched->get_position(),
                                         static_cast<IObjInspect const*>(static_cast<IObjRW*>(Owner)),
                                         Cos);
    }
}

// ----------------------------------------------------------------------------
// FUN_062c7790.
// ----------------------------------------------------------------------------
EvadeBehavior::EvadeBehavior(int InTypeId)
    : Behavior(InTypeId)
    , EvadeMode(0)
    , Dodge()                    // FUN_062d77d0
    , BreakAway()                // FUN_062d8970
    , DodgeMissile()             // FUN_062d94b0
    , Face()                     // FUN_062d6750
    , Afterburner()              // FUN_062da500
    , Watcher2a0()
    , DodgeTime2a8(0.0f)         // not written by the ctor; SetContext sets it
    , BreakTime2ac(3.0f)
    , Elapsed2b0(0.0f)           // not written by the ctor; SetContext sets it
    , Flag2b4(0)
    , Flag2b5(0)
    , Flag2b6(0)
    , Flag2b7(0)
    , ConeCos2b8(0.0f)
    , ConeCos2bc(0.707f)         // 0x3f34fdf4 (0.707f, not cos 45)
{
}

// FUN_062c7900 (FUN_062c78e0 is the scalar-deleting thunk): the five set_pointer(0)
// calls are the members' own BaseWatcher destructors (Watcher2a0, Face, DodgeMissile,
// BreakAway.Picker, Dodge.Picker -- reverse member order), then FUN_062d2080 (~Behavior).
EvadeBehavior::~EvadeBehavior()
{
}

// ----------------------------------------------------------------------------
// Slot 1, LAB_062c79d0.
// ----------------------------------------------------------------------------
bool EvadeBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    bool const bOk = BindContext(Owner, Manager, Sys);              // FUN_062d2090
    if (!bOk)
        return bOk;

    // Every Idea gets (owner, SysMgr, Manager); results unread.
    Dodge.SetContext(OwnerContext, SysMgr, Manager);
    Dodge.ConeAngle = 1.5707964f;                                   // .text:062a66b0
    Dodge.DodgeDistance = 50.0f;                                    // .text:062a33d0
    Dodge.IntervalTime = 6.0f;                                      // .text:063275d0
    BreakAway.SetContext(OwnerContext, SysMgr, Manager);
    DodgeMissile.SetContext(OwnerContext, SysMgr, Manager);
    Face.SetContext(OwnerContext, SysMgr, Manager);
    Afterburner.SetContext(OwnerContext, SysMgr, Manager);

    DodgeTime2a8 = 10.0f;
    Elapsed2b0 = 0.0f;
    return bOk;
}

// ----------------------------------------------------------------------------
// FUN_062c8690. The weight sum is rounded to a float local; r = rand() * Sum / 32768 stays
// on the x87 stack. Each test is strict less-than (unordered falls through to 3).
// ----------------------------------------------------------------------------
int EvadeBehavior::PickDodgeStyle()
{
    pub::AI::Personality::EvadeDodgeUseStruct const& P = SysMgr->PersonalityData.EvadeDodgeParams;
    float const Sum = static_cast<float>(((static_cast<double>(P.EvadeDodgeStyleWeightWaggle) +
                                            P.EvadeDodgeStyleWeightWaggleRandom) +
                                           P.EvadeDodgeStyleWeightSlide) +
                                          P.EvadeDodgeStyleWeightCorkscrew);
    double const R = static_cast<double>(std::rand()) * Sum * GRandScale;

    double Bound = P.EvadeDodgeStyleWeightWaggle;
    if (R < Bound)
        return 0;
    Bound = Bound + P.EvadeDodgeStyleWeightWaggleRandom;
    if (R < Bound)
        return 1;
    Bound = Bound + P.EvadeDodgeStyleWeightSlide;
    if (R < Bound)
        return 2;
    return 3;
}

// ----------------------------------------------------------------------------
// Slot 3, LAB_062c8260.
// ----------------------------------------------------------------------------
void EvadeBehavior::OnEngage()
{
    Behavior::OnEngage();                                              // FUN_062d20d0
    EvadeMode = 0;
    SetAvoidanceAllowed(1, 3);                                            // FUN_062d21d0
    Elapsed2b0 = 0.0f;

    Dodge.Picker.SetWatchObject(WatchedObject(Watcher2a0));         // FUN_062d88e0
    BreakAway.Picker.SetWatchObject(WatchedObject(Watcher2a0));     // FUN_062d88e0

    if (SysMgr->SelfTarget.Iface->is_player())
    {
        // The owner's get_throttle result is unread; its out-param (preset 1.0f) is used.
        float Throttle = 1.0f;
        static_cast<IObjInspect const*>(OwnerContext)->get_throttle(Throttle);
        Dodge.Throttle = Throttle;
        BreakAway.Throttle = Throttle;
    }

    pub::AI::Personality const& Pers = SysMgr->PersonalityData;
    pub::AI::Personality::EvadeDodgeUseStruct const& D = Pers.EvadeDodgeParams;
    pub::AI::Personality::EvadeBreakUseStruct const& B = Pers.EvadeBreakParams;

    // One-store setters on Dodge, each a raw dword copy (setter, offset, Personality source).
    Dodge.ConeAngle = D.EvadeDodgeConeAngle;                        // .text:062a66b0 +0x3c <- Sys+0x1598
    Dodge.ConeAngleVariancePercent = D.EvadeDodgeConeAngleVariancePercent;       // .text:062a33e0 +0x40 <- 0x159c
    Dodge.WaggleAxisConeAngle = D.EvadeDodgeWaggleAxisConeAngle;    // FUN_062d88b0  +0x44 <- 0x15a0
    Dodge.RollAngle = D.EvadeDodgeRollAngle;                        // .text:062a33c0 +0x48 <- 0x15a4
    Dodge.IntervalTime = D.EvadeDodgeIntervalTime;                  // .text:063275d0 +0x34 <- 0x15a8
    Dodge.IntervalTimeVariancePercent = D.EvadeDodgeIntervalTimeVariancePercent; // .text:063275f0 +0x38 <- 0x15ac
    Dodge.DodgeDistance = D.EvadeDodgeDistance;                     // .text:062a33d0 +0x4c <- 0x15b0

    DodgeTime2a8 = D.EvadeDodgeTime;                                // <- 0x15b4
    BreakTime2ac = B.EvadeBreakTime;                                // <- 0x15dc

    Dodge.TurnThrottle = D.EvadeDodgeTurnThrottle;                  // FUN_062d8910 +0x90 <- 0x15bc
    Dodge.SlideThrottle = D.EvadeDodgeSlideThrottle;                // FUN_062d8930 +0xc8 <- 0x15b8
    Dodge.VectorCc.x = D.EvadeDodgeCorkscrewTurnThrottle;           // FUN_062d8940 +0xcc <- 0x15c0
    Dodge.VectorCc.z = D.EvadeDodgeCorkscrewRollThrottle;           // FUN_062d8950 +0xd4 <- 0x15c4
    Dodge.bRollFlipDirection = D.bEvadeDodgeCorkscrewRollFlipDirection; // FUN_062d8960 +0xd8 <- byte 0x15c8
    Dodge.Picker.SetWeights(D.EvadeDodgeDirectionWeightLeft, D.EvadeDodgeDirectionWeightRight,
                            D.EvadeDodgeDirectionWeightUp, D.EvadeDodgeDirectionWeightDown);  // FUN_062d76b0 on +0x44

    int const Style = PickDodgeStyle();                             // FUN_062c8690
    Dodge.DodgeStyle = Style;                                       // .text:062d8920 +0xb4
    if (Style == 2)
    {
        // Double .rdata:0639f8e0 through FCOS, negated, rounded to float.
        ConeCos2b8 = -X87CosF(1.0471975803375244);
    }

    // The break-away fate: same weighted pick, strict less-than: r < Reverse -> 2,
    // r < Outrun + Reverse -> 0, otherwise 1.
    int Fate;
    {
        float const Sum = static_cast<float>((static_cast<double>(B.EvadeBreakStyleWeightOutrun) +
                                              B.EvadeBreakStyleWeightSideways) +
                                             B.EvadeBreakStyleWeightReverse);
        double const R = static_cast<double>(std::rand()) * Sum * GRandScale;
        if (R < B.EvadeBreakStyleWeightReverse)
            Fate = 2;
        else if (R < static_cast<double>(B.EvadeBreakStyleWeightOutrun) + B.EvadeBreakStyleWeightReverse)
            Fate = 0;
        else
            Fate = 1;
    }
    BreakAway.BreakStyle = Fate;                                    // .text:063275d0 +0x34
    BreakAway.AttemptReverseTime = B.EvadeBreakAttemptReverseTime;  // .text:062a33d0 +0x4c <- 0x1610
    BreakAway.ReverseDistance = B.EvadeBreakReverseDistance;        // FUN_062d93b0  +0x54 <- 0x1614
    BreakAway.TurnThrottle = B.EvadeBreakTurnThrottle;              // FUN_062d9490  +0x94 <- 0x1600
    BreakAway.RollThrottle = B.EvadeBreakRollThrottle;              // FUN_062d94a0  +0x98 <- 0x15fc
    BreakAway.IntervalTime = B.EvadeBreakIntervalTime;              // FUN_062d9480  +0x88 <- 0x15e0
    BreakAway.Picker.SetWeights(B.EvadeBreakDirectionWeightLeft, B.EvadeBreakDirectionWeightRight,
                                B.EvadeBreakDirectionWeightUp, B.EvadeBreakDirectionWeightDown);  // on +0x124

    // Delay * variance rounded to a float local; Delay - rand() * local / 32768 -> FUN_062d93c0.
    {
        float const Spread = static_cast<float>(static_cast<double>(B.EvadeBreakAfterburnerDelay) *
                                                B.EvadeBreakAfterburnerDelayVariancePercent);
        double const R = static_cast<double>(std::rand()) * Spread * GRandScale;
        BreakAway.AfterburnerDelay = static_cast<float>(static_cast<double>(B.EvadeBreakAfterburnerDelay) - R);
    }

    if (SysMgr->SelfTarget.Iface->is_player())
    {
        // Negated rotation column 2 -> FUN_062d88c0.
        Transform const& Xf = SysMgr->SelfTarget.Iface->get_transform();
        Vector Forward;
        Forward.x = -Xf.Rotation.E[0][2];
        Forward.y = -Xf.Rotation.E[1][2];
        Forward.z = -Xf.Rotation.E[2][2];
        Dodge.SetPreferredForward(Forward);
    }
}

// ----------------------------------------------------------------------------
// Slot 4, FUN_062c85e0.
// ----------------------------------------------------------------------------
void EvadeBehavior::OnDisengage()
{
    Flag2b5 = 0;
    Dodge.Commit();
    BreakAway.Commit();
    DodgeMissile.Commit();
    Afterburner.Commit();
    Behavior::OnDisengage();                                              // FUN_062d20f0
}

// ----------------------------------------------------------------------------
// Slot 5, LAB_062c7ab0.
// ----------------------------------------------------------------------------
int EvadeBehavior::OnEvaluateReadiness(float Unused)
{
    (void)Unused;
    SystemManager* const Sys = SysMgr;
    int Result = 2;

    // JobParams.bForceAttackFormation is Sys+0x1958.
    if (Sys->PersonalityData.JobParams.bForceAttackFormation != 0 && SysMgr->Timers.IsTimer0Active())
    {
        Flag2b4 = 1;
        Elapsed2b0 = DodgeTime2a8;
        Result = 0;
    }

    if (SystemManagerTimerTableHasMissileReactionTarget(&Sys->Timers))   // FUN_062e8870
    {
        Readiness = Flag29 ? 3 : 0;
        return Readiness;
    }
    if (Sys->FormationLeaders.FollowerDirectiveGateA())       // FUN_062ecb20
    {
        Readiness = Flag29 ? 3 : 2;
        return Readiness;
    }

    IObjInspect* const Closest = FindClosestHostileTarget(&Sys->Timers,
                                                          Sys->PersonalityData.EvadeDodgeParams.EvadeActivateRange);
    Watcher2a0.set(static_cast<Watchable const*>(static_cast<IObjRW*>(Closest)));

    if (IsWatching(Watcher2a0))
    {
        IObjRW* Victim = nullptr;
        WatchedObject(Watcher2a0)->get_target(Victim);
        if (Victim != nullptr)
        {
            // FUN_062e61a0, strictly greater.
            if (GetIfaceVectorMagnitude(WatchedObject(Watcher2a0)) > 1.0f &&
                WatchedOutsideCone(Watcher2a0, OwnerContext, ConeCos2bc))
            {
                // Is the watched object's target this very ship?
                unsigned int const SelfId = Sys->SelfTarget.Iface->get_id();
                if (Victim->get_id() == SelfId)
                {
                    Readiness = Flag29 ? 3 : 1;
                    return Readiness;
                }
            }

            // ... or one of this ship's formation followers.
            IObjRW* Followers[0x10];
            int const Count = OwnerContext->get_formation_followers(Followers, 0x10);
            for (int Index = 0; Index < Count; ++Index)
            {
                IObjRW* const Follower = Followers[Index];
                if (Follower == nullptr)
                    continue;
                // The cone test is against the follower's heading.
                if (!IsPointOutsideHeadingCone(WatchedObject(Watcher2a0)->get_position(), Follower, ConeCos2bc))
                    continue;
                if (Victim->get_id() == Follower->get_id())
                {
                    Readiness = Flag29 ? 3 : 1;
                    return Readiness;
                }
            }
        }
    }

    Readiness = Flag29 ? 3 : Result;
    return Readiness;
}

// ----------------------------------------------------------------------------
// Slot 8, FUN_062c7d00.
// ----------------------------------------------------------------------------
void EvadeBehavior::PostUpdate()
{
    SystemManager* const Sys = SysMgr;
    Flag2b4 = 0;
    if (Sys->PersonalityData.JobParams.bForceAttackFormation != 0)
        Flag2b4 = SysMgr->Timers.IsTimer0Active() ? 1 : 0;    // FUN_062e8810
    if (Flag2b4 != 0)
        Elapsed2b0 = DodgeTime2a8;
    if (Flag2b5 == 0)
        Flag2b5 = SysMgr->Timers.IsTimer1Active() ? 1 : 0;    // FUN_062e8820
}

// ----------------------------------------------------------------------------
// Slot 7, LAB_062c7d60. EvadeMode 1..4 dispatch through a jump table; anything else returns 2.
// ----------------------------------------------------------------------------
int EvadeBehavior::OnUpdate()
{
    SystemManager* const Sys = SysMgr;
    pub::AI::Personality const& Pers = Sys->PersonalityData;

    // A non-player ship evades only something it is watching, and only while that object
    // lies outside its own heading cone.
    if (!SysMgr->SelfTarget.Iface->is_player())
    {
        if (!IsWatching(Watcher2a0) || !WatchedOutsideCone(Watcher2a0, OwnerContext, ConeCos2bc))
        {
            UpdateResult = 2;
            return 2;
        }
    }

    int const OldMode = EvadeMode;
    if (SystemManagerTimerTableHasMissileReactionTarget(&Sys->Timers))
    {
        EvadeMode = 4;
    }
    else if (SysMgr->Timers.IsTimer2Active())                 // FUN_062e8860
    {
        EvadeMode = 3;
    }
    // Strictly less (unordered fails).
    else if (Flag2b6 == 0 && Elapsed2b0 < DodgeTime2a8)
    {
        EvadeMode = 1;
    }
    else
    {
        if (Flag2b7 != 0)
        {
            UpdateResult = 2;
            return 2;
        }
        // Strictly greater, the sum unrounded.
        if (!(static_cast<double>(BreakTime2ac) + DodgeTime2a8 > Elapsed2b0))
        {
            UpdateResult = 2;
            return 2;
        }
        EvadeMode = 2;
    }

    if (OldMode != EvadeMode)
    {
        if (EvadeMode == 1)
        {
            if (Pers.FormationParams.bBreakApartFormationOnEvadeDodge != 0)   // Sys+0x17fd
                SysMgr->FormationLeaders.NotifyFormationLeader1(Pers.FormationParams.BreakFormationOnEvadeDodgeTime);
            if (Pers.FormationParams.bRegroupFormationOnEvadeDodge != 0)      // Sys+0x1804
                SysMgr->FormationLeaders.NotifyFormationLeader2();
        }
        if (EvadeMode == 2)
        {
            if (Pers.FormationParams.bBreakApartFormationOnEvadeBreak != 0)   // Sys+0x1805
                SysMgr->FormationLeaders.NotifyFormationLeader1(Pers.FormationParams.BreakFormationOnEvadeBreakTime);
            if (Pers.FormationParams.bRegroupFormationOnEvadeBreak != 0)      // Sys+0x180c
                SysMgr->FormationLeaders.NotifyFormationLeader2();
        }
        OnDisengage();                                                    // virtual, slot 4
    }

    Elapsed2b0 = Elapsed2b0 + DeltaTime;

    switch (EvadeMode)
    {
    case 1:
    {
        if (IsWatching(Watcher2a0) && !WatchedOutsideCone(Watcher2a0, OwnerContext, ConeCos2b8))
        {
            // The threat is back inside the narrower cone: stop steering (FUN_062e8c20,
            // FUN_062e8ce0, FUN_062e89a0 on the ThrustManager).
            Vector Zero1;
            Zero1.x = 0.0f;
            Zero1.y = 0.0f;
            Zero1.z = 0.0f;
            Sys->Thrust.SetAxisThrottle(Zero1);
            Vector Zero2;
            Zero2.x = 0.0f;
            Zero2.y = 0.0f;
            Zero2.z = 0.0f;
            Sys->Thrust.SetNudgeDirection(Zero2);
            Sys->Thrust.SetThrottle(1.0f, 0);
            UpdateResult = 0;
            return 0;
        }

        if (Flag2b5 != 0)
        {
            pub::AI::Personality::EvadeDodgeUseStruct const& D = Pers.EvadeDodgeParams;
            // Angle * 3.0f (DAT_0639dfd4) clamped to pi/2 (DAT_0639f5d4); the compare uses the
            // unrounded product.
            double const Angle = static_cast<double>(D.EvadeDodgeConeAngle) * 3.0f;
            Dodge.ConeAngle = (Angle > 1.5707964f) ? 1.5707964f : static_cast<float>(Angle);   // .text:062a66b0
            // Same clamp -> FUN_062d88b0 (+0x44).
            double const Turn = static_cast<double>(D.EvadeDodgeWaggleAxisConeAngle) * 3.0f;
            Dodge.WaggleAxisConeAngle = (Turn > 1.5707964f) ? 1.5707964f : static_cast<float>(Turn);
            // Greater than 1 halves it, otherwise (unordered included) doubles it.
            double Interval = D.EvadeDodgeIntervalTime;
            if (Interval > 1.0)
                Interval = Interval * 0.5f;
            else
                Interval = Interval + Interval;
            Dodge.IntervalTime = static_cast<float>(Interval);      // .text:063275d0
            Dodge.TurnThrottle = 1.0f;                              // FUN_062d8910
            Dodge.SlideThrottle = 1.0f;                             // FUN_062d8930
        }

        Dodge.Update(DeltaTime);

        // FUN_062e4500.
        if (IsWithinInterceptRangeOfIface(&Sys->SelfTarget,WatchedObject(Watcher2a0), 50.0f))
            Afterburner.Update(DeltaTime);
        else
            Afterburner.Commit();
        UpdateResult = 0;
        return 0;
    }

    case 2:
    {
        int const R = BreakAway.Update(DeltaTime);
        if (R != 0)
        {
            UpdateResult = 2;
            return 2;
        }
        UpdateResult = R;
        return R;
    }

    case 3:
    {
        if (IsWatching(Watcher2a0) && !WatchedOutsideCone(Watcher2a0, OwnerContext, 0.7f))  // 0x3f333333
        {
            UpdateResult = 2;
            return 2;
        }
        BreakAway.BreakStyle = 2;                                   // .text:063275d0
        int const R = BreakAway.Update(DeltaTime);
        if (R != 0)
        {
            UpdateResult = 2;
            return 2;
        }
        UpdateResult = R;
        return R;
    }

    case 4:
    {
        // The missile the timer table tracks (Timers+0xe8, Sys+0x11c); the dodge mode comes from
        // the Personality::MissileReaction range it falls inside.
        void* const MissileRaw = *reinterpret_cast<void* const*>(Sys->Timers.BestCandidateWatcherBytes);
        int Mode;
        if (TimerTableMissileWithinReaction00(&Sys->Timers))        // FUN_062e8910
            Mode = 1;
        else if (TimerTableMissileWithinReaction04(&Sys->Timers))   // FUN_062e8940
            Mode = 2;
        else if (TimerTableMissileWithinReaction08(&Sys->Timers))   // FUN_062e8970
            Mode = 0;
        else
        {
            UpdateResult = 1;
            return 1;
        }
        DodgeMissile.SetWatchObject(ObjRwFromWatchable(MissileRaw));    // FUN_062d9ae0
        DodgeMissile.Mode1c = Mode;                                 // .text:0631d950
        DodgeMissile.Update(DeltaTime);
        UpdateResult = 1;
        return 1;
    }

    default:
        UpdateResult = 2;
        return 2;
    }
}

// ----------------------------------------------------------------------------
// Slot 10, LAB_062c8630.
// ----------------------------------------------------------------------------
bool EvadeBehavior::OnApplyDirective(void* DirectiveOp)
{
    SystemManager* const Sys = SysMgr;
    void* Target;
    if (DirectiveOp != nullptr)
    {
        Target = Sys->Timers.FindVerifiedObject(
            &static_cast<pub::AI::DirectiveEvadeOp const*>(DirectiveOp)->ThreatId);
    }
    else
    {
        Target = FindClosestHostileTarget(&Sys->Timers, Sys->PersonalityData.EvadeDodgeParams.EvadeActivateRange);
    }
    Watcher2a0.set(Target != nullptr
                       ? reinterpret_cast<Watchable const*>(static_cast<char*>(Target) + 8)
                       : nullptr);
    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
    return true;
}

// ----------------------------------------------------------------------------
// Slot 21, LAB_062c7870.
// ----------------------------------------------------------------------------
void EvadeBehavior::OnObjectLeftSystem(void* Arg)
{
    if (!IsWatching(Watcher2a0))
        return;
    if (WatchedObject(Watcher2a0)->get_id() != static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Arg)))
        return;
    Watcher2a0.set(nullptr);
}

// ----------------------------------------------------------------------------
// Slot 24, LAB_062c78c0: `Watched ? Watched - 8 : 0`.
// ----------------------------------------------------------------------------
IObjRW const* EvadeBehavior::get_target()
{
    return WatchedObject(Watcher2a0);
}
