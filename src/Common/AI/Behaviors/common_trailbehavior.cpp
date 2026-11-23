// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_trailbehavior.h"

#include "common_aisteer.h"        // FAiTarget, GetIfaceVectorMagnitude, GetIfacePositionOrZero,
                                   // ClampedDot3, BearingAngleToIface, IsAlignedAndOutsideHeadingCone,
                                   // GetInterceptClearanceOfIface, GetObjectTypeOrZero
#include "common_csys.h"           // Transform, Acquire3DMathEngine
#include "common_iface.h"          // IObjInspect, IObjRW, IBehaviorManager, ObjInspectOf, ObjRwFromWatchable
#include "common_misc.h"           // PlayerTrailAvailable
#include "common_pub.h"            // pub::AI::ContentCallback, TrailUseStruct
#include "common_systemmanager.h"  // SystemManager -- what NavHelper points at
#include "x86math.h"               // I3DMathEngine::TransformMatrix (+0x2c)

#include <cmath>
#include <cstring>

#include "x87_trig.h" // X87CosF (retail FCOS in the ctor and OnEngage)

namespace
{
    // .data:DAT_063fc538, the 3DMathEngine pointer OnUpdate fetches lazily through FUN_06391cf0.
    I3DMathEngine* GTrailMathEngine = nullptr;

    // SystemManager+0x14fc, the owner's own FAiTarget record.
    FAiTarget* NavOf(SystemManager* Sys)
    {
        return &Sys->SelfTarget;
    }

    pub::AI::Personality::TrailUseStruct const& TrailParamsOf(SystemManager* Sys)
    {
        return Sys->PersonalityData.TrailParams;
    }

    IObjRW* WatchedObject(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }

    // The inlined get_type refresh on the FAiTarget (latch +0x1579); the flag is raised after
    // the call.
    unsigned RefreshNavFlags(FAiTarget* Nav)
    {
        if (Nav->bFlagsValid == 0)
        {
            Nav->Iface->get_type(Nav->CachedFlags);
            Nav->bFlagsValid = 1;
        }
        return Nav->CachedFlags;
    }

    bool OwnShipIsPlayer(SystemManager* Sys)
    {
        return Sys->SelfTarget.Iface->is_player();
    }

    // The [-1, 1] clamp set_direction applies to both axes (062cb1d7); NaN becomes -1. Retail's
    // third compare re-tests the lower bound where it can no longer hold.
    float ClampUnit(float Value)
    {
        if (!(Value > -1.0f))
            return -1.0f;
        if (!(Value < 1.0f))
            return 1.0f;
        return Value;
    }
}

// ----------------------------------------------------------------------------
// FUN_062ca350.
// ----------------------------------------------------------------------------
TrailBehavior::TrailBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , OwnerContext(nullptr)
    , ObjectDb(nullptr)
    , DeltaTime(0.0f)
    , BehaviorMgrPtr(nullptr)
    , NavHelper(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , MatchThrottle()               // FUN_062d5aa0
    , Face()                        // FUN_062d6750
    , Move()                        // FUN_062d4e60
    , Afterburner()                 // FUN_062da500
    , DirectiveTarget()             // retail then runs a no-op set_pointer guard
    , LeaderId1dc(0)
    , bHasDirectiveTarget1e0(0)     // not written by retail's ctor; SetContext clears it
    , Pad1e1{}
    , TrailOffset1e4(0.0f)
    , TrailDistance1e8(50.0f)
    , bLagging1ec(0)                // not written by retail's ctor; OnEngage clears it
    , Pad1ed{}
    , FacingCos1f0(X87CosF(0.5235987901687622))   // FCOS, then stored to float
    , LagTimer1f4(0.0f)             // not written by retail's ctor; OnEngage clears it
    , AxisTimer1f8(0.0f)            // not written by retail's ctor; OnEngage clears it
    , LagTime1fc(2.5f)
    , AxisPause200(5.0f)
    , AxisTime204(3.0f)
    , AxisZ208(0.0f)
    , bAfterburner20c(0)
    , Pad20d{}
    , Scale210(1.0f)
    , bAxisArmed214(0)
    , Pad215{}
    , SightTimer218(0.0f)           // not written by retail's ctor; OnEngage clears it
    , SightTime21c(4.0f)
    , TrailExtent220(0.0f)          // not written by retail's ctor; OnUpdate writes it
    , Field224(0.0385f)             // overwritten below with the exact bits
    , Field228(400.0f)
    , bDirectionSet22c(0)
    , Pad22d{}
    , DirectionX230(0.0f)
    , DirectionY234(0.0f)
    , DirectionZ238(0.0f)
    , bMatchThrottle23c(0)
    , Pad23d{}
    , Field240(20.0f)
    , CatchUpRange244(1500.0f)
{
    ConstructBase(InTypeId);        // FUN_062d2020
    unsigned const Field224Bits = 0x3d1de9e7u;
    std::memcpy(&Field224, &Field224Bits, sizeof(Field224));
}

// FUN_062ca4c0 (scalar deleting FUN_062ca4a0): the four set_pointer(0) calls are the members'
// own watcher destructors, then ~Behavior (FUN_062d2080).
TrailBehavior::~TrailBehavior()
{
}

// ----------------------------------------------------------------------------
// Slot 1, LAB_062ca570.
// ----------------------------------------------------------------------------
bool TrailBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    bool const bOk = BindContext(Owner, Manager, Sys);              // FUN_062d2090
    if (!bOk)
        return bOk;

    // Every idea gets (Object = +0x14, Handle = +0x24, Context = Manager). Results unread.
    bHasDirectiveTarget1e0 = 0;
    Face.SetContext(OwnerContext, NavHelper, Manager);
    MatchThrottle.SetContext(OwnerContext, NavHelper, Manager);
    MatchThrottle.SetMaxBearing(3.14159274f);                       // ICF fold 063275d0
    MatchThrottle.SetCatchUpRange(CatchUpRange244);                 // FUN_062d6740
    if (!ObjInspectOf(OwnerContext)->is_player())
        MatchThrottle.SetFlag3b(true);                              // FUN_062d6730
    Move.SetContext(OwnerContext, NavHelper, Manager);
    Move.SetMode(1);                                                // FUN_062d5980
    Afterburner.SetContext(OwnerContext, NavHelper, Manager);
    return bOk;
}

// ----------------------------------------------------------------------------
// Slot 3, LAB_062ca640.
// ----------------------------------------------------------------------------
void TrailBehavior::OnEngage()
{
    Behavior::OnEngage();
    SetCruiseLock(1, 3);                                            // FUN_062d21d0

    // The owner's target, else the directive target.
    IObjRW* Leader = NavHelper->Targeting.GetTarget();              // FUN_062ed5a0
    if (Leader == nullptr)
        Leader = WatchedObject(DirectiveTarget);

    bLagging1ec = 0;
    LagTimer1f4 = 0.0f;
    AxisTimer1f8 = 0.0f;

    if (OwnShipIsPlayer(NavHelper))
    {
        // The owner's own cruise state into MatchThrottle's +0x38.
        bool bCruise = false;
        static_cast<IObjRW*>(OwnerContext)->is_cruise_active(bCruise);
        MatchThrottle.SetFlag38(bCruise);                           // FUN_062d6700
    }

    bDirectionSet22c = 0;
    DirectionX230 = 0.0f;
    DirectionY234 = 0.0f;
    DirectionZ238 = 0.0f;
    SightTimer218 = 0.0f;

    if (Leader != nullptr)
    {
        Move.PublishTargetId(Leader->get_id());                     // FUN_062d4e50
        Face.PublishTargetId(Leader->get_id());
        LeaderId1dc = Leader->get_id();
    }
    else
    {
        Move.PublishTargetId(0);
        LeaderId1dc = 0;
    }

    // The personality's TrailUseStruct (Sys+0x16ac).
    pub::AI::Personality::TrailUseStruct const& P = TrailParamsOf(NavHelper);
    FacingCos1f0 = X87CosF(static_cast<double>(P.TrailLockConeAngle));
    LagTime1fc = P.TrailBreakTime;
    AxisTime204 = P.TrailMinNoLockTime;
    AxisZ208 = P.TrailBreakRollThrottle;
    bAfterburner20c = P.bTrailBreakAfterburner;
    Scale210 = P.TrailMaxTurnThrottle;
    Face.SetScale94(P.TrailMaxTurnThrottle);                        // FUN_062d9490
    bAxisArmed214 = 0;
    bMatchThrottle23c = 0;
}

// ----------------------------------------------------------------------------
// Slot 4, FUN_062ca7b0.
// ----------------------------------------------------------------------------
void TrailBehavior::OnDisengage()
{
    bHasDirectiveTarget1e0 = 0;
    DirectiveTarget.set(nullptr);
    Face.Commit();
    MatchThrottle.SetFlag39(true);                                  // FUN_062d6710
    MatchThrottle.Commit();
    Move.Commit();
    Afterburner.Commit();
    Behavior::OnDisengage();
}

// ----------------------------------------------------------------------------
// Slot 5, LAB_062ca820.
// ----------------------------------------------------------------------------
int TrailBehavior::OnEvaluateReadiness(float Unused)
{
    (void)Unused;
    SystemManager* const Sys = NavHelper;
    int Result = 2;

    if (!Sys->GunConvergenceStateBytes.FollowerDirectiveGateA())     // FUN_062ecb20
    {
        IObjRW* const Leader = Sys->Targeting.GetTarget();
        if (Leader != nullptr)
        {
            // FUN_062e61a0 on a fresh GetTarget(); less or unordered leaves.
            if (GetIfaceVectorMagnitude(Sys->Targeting.GetTarget()) >= 20.0f)
            {
                // A 0x380000 leader type gives up at once.
                unsigned LeaderType = 0;                            // not pre-set in retail
                Leader->get_type(LeaderType);
                if ((LeaderType & 0x380000u) != 0)
                {
                    ModeSelector0xc = Flag29 ? 3 : Result;
                    return ModeSelector0xc;
                }
                if ((RefreshNavFlags(NavOf(Sys)) & 0x7f0000u) != 0)
                    Result = 1;
                // FUN_062e1aa0 against pi/2 (.rdata:0639f5d4); >= or unordered falls through.
                if (BearingAngleToIface(Leader, OwnerContext) < 1.5707964f)
                {
                    ModeSelector0xc = Flag29 ? 3 : 2;
                    return ModeSelector0xc;
                }
            }
        }
        if (bHasDirectiveTarget1e0 != 0)
            Result = 1;
    }

    ModeSelector0xc = Flag29 ? 3 : Result;
    return ModeSelector0xc;
}

// ----------------------------------------------------------------------------
// Slot 6, LAB_062ca940.
// ----------------------------------------------------------------------------
EvaluateResult TrailBehavior::evaluate(unsigned int Context)
{
    (void)Context;
    if (!PlayerTrailAvailable)
        return EvalSkip;

    IObjRW* const Owner = static_cast<IObjRW*>(OwnerContext);
    IObjRW* Target = nullptr;
    Owner->get_target(Target);
    if (Target == nullptr)
        return EvalSkip;

    bool bInLane = false;
    Owner->is_using_tradelane(&bInLane);
    if (bInLane)
        return EvalSkip;

    // get_type must succeed and name a ship-class type outside 0x200000.
    unsigned TargetType = 0;                                        // not pre-set in retail
    if (Target->get_type(TargetType) != 0)
        return EvalSkip;
    if ((TargetType & 0x7f0000u) == 0 || (TargetType & 0x200000u) != 0)
        return EvalSkip;

    // Behaviors 0xe, 0x11 and 8 cannot be trailed.
    int const TargetBehavior = Target->get_behavior_id();
    if (TargetBehavior == 0xe || TargetBehavior == 0x11 || TargetBehavior == 8)
        return EvalSkip;

    // FUN_062e6200 re-reads the type the null-safe way.
    unsigned const TypeAgain = GetObjectTypeOrZero(Target);
    if ((TypeAgain & 0x7f0000u) == 0 || (TypeAgain & 0x600de0u) != 0)
        return EvalSkip;

    NavHelper->Targeting.RetargetOwner(Target, SUBOBJ_ID_NONE);     // FUN_062ee940
    return EvalBlocked;
}

// ----------------------------------------------------------------------------
// Slot 8, LAB_062caa40.
// ----------------------------------------------------------------------------
void TrailBehavior::PostUpdate()
{
    if (bMatchThrottle23c != 0)
        MatchThrottle.SyncCruise(DeltaTime);                        // idea slot 2
}

// ----------------------------------------------------------------------------
// Slot 7, LAB_062caa60.
// ----------------------------------------------------------------------------
int TrailBehavior::OnUpdate()
{
    if (OwnerContext == nullptr)
    {
        UpdateResult10 = 3;
        return 3;
    }

    SystemManager* const Sys = NavHelper;
    if (Sys->Timers.IsTimer0Active())                              // FUN_062e8810
    {
        UpdateResult10 = 2;
        return 2;
    }

    // Set when the leader comes from the TargetManager rather than the directive.
    bool bTrailTarget = false;
    IObjRW* Leader = nullptr;
    if (bHasDirectiveTarget1e0 != 0 && WatchedObject(DirectiveTarget) != nullptr)
    {
        Leader = WatchedObject(DirectiveTarget);
    }
    else
    {
        if (Sys->Targeting.GetTarget() == nullptr)
        {
            UpdateResult10 = 2;
            return 2;
        }
        Leader = Sys->Targeting.GetTarget();
        // An AI owner gives up on a target slower than 20 (FUN_062e61a0; unordered stays).
        if (!OwnShipIsPlayer(Sys) && Leader != nullptr &&
            GetIfaceVectorMagnitude(Sys->Targeting.GetTarget()) < 20.0f)
        {
            UpdateResult10 = 3;
            return 3;
        }
        bTrailTarget = true;
    }
    if (Leader == nullptr)
    {
        UpdateResult10 = 2;
        return 2;
    }

    // Every idea watches the leader's id; a new leader re-engages (virtual slot 3).
    MatchThrottle.PublishTargetId(Leader->get_id());                // FUN_062d4e50
    Face.PublishTargetId(Leader->get_id());
    Move.PublishTargetId(Leader->get_id());
    if (Leader->get_id() != LeaderId1dc)
        OnEngage();
    LeaderId1dc = Leader->get_id();

    if (bTrailTarget && !OwnShipIsPlayer(Sys))
    {
        FAiTarget* const Nav = NavOf(Sys);
        Nav->RefreshPos();

        // Unit vector from the own centre to the leader's centre.
        Vector LeaderCenter;
        GetIfacePositionOrZero(&LeaderCenter, Leader);
        Vector ToLeader;
        ToLeader.x = LeaderCenter.x - Nav->CachedPos.x;
        ToLeader.y = LeaderCenter.y - Nav->CachedPos.y;
        ToLeader.z = LeaderCenter.z - Nav->CachedPos.z;
        Vector ToLeaderDir;
        VecNormalize3(&ToLeader, &ToLeaderDir);                     // FUN_062e1680

        // The own forward (column 2 negated) and the facing cosine.
        Transform const& OwnXf = Sys->SelfTarget.Iface->get_transform();
        Vector OwnFwd;
        OwnFwd.x = -OwnXf.Rotation.E[0][2];
        OwnFwd.y = -OwnXf.Rotation.E[1][2];
        OwnFwd.z = -OwnXf.Rotation.E[2][2];
        float const FacingCos = static_cast<float>(ClampedDot3(ToLeaderDir, OwnFwd));   // FUN_062e1630

        // The "face the axis" pause.
        if (bAxisArmed214 != 0 && bLagging1ec == 0 &&
            static_cast<double>(AxisPause200) + AxisTime204 > AxisTimer1f8)
        {
            if (AxisTimer1f8 > AxisTime204)
            {
                AxisTimer1f8 = static_cast<float>(static_cast<double>(AxisTimer1f8) + DeltaTime);
                bAxisArmed214 = 0;
                Vector Axis;
                Axis.x = 0.0f;
                Axis.y = 0.0f;
                Axis.z = AxisZ208;
                Face.SetAxis(Axis);                                 // FUN_062d75e0
                Face.Update(DeltaTime);
                UpdateResult10 = 1;
                return 1;
            }
            AxisTimer1f8 = static_cast<float>(static_cast<double>(DeltaTime) + AxisTimer1f8);
        }
        else
        {
            AxisTimer1f8 = 0.0f;
        }

        // Lagging behind (facing away): turn to the axis for LagTime1fc.
        if (bLagging1ec != 0 && FacingCos < FacingCos1f0 && LagTimer1f4 < LagTime1fc)
        {
            LagTimer1f4 = static_cast<float>(static_cast<double>(DeltaTime) + LagTimer1f4);
            Vector Axis;
            Axis.x = 0.0f;
            Axis.y = 0.0f;
            Axis.z = AxisZ208;
            Face.SetAxis(Axis);
            if (bAfterburner20c != 0)
                Afterburner.Update(DeltaTime);
            Face.Update(DeltaTime);
            Move.SetMode(0);                                        // FUN_062d5980
            Move.SetScale40(1.0f);                                  // ICF fold 062a33e0
            Move.SetDirectionFrom(&Sys->SelfTarget.Iface->get_transform());   // FUN_062d58f0
            Move.SetAngle38(3.14159274f);                           // ICF fold 063275f0
            Move.Update(DeltaTime);
            UpdateResult10 = 0;
            return 0;
        }

        Afterburner.Commit();
        LagTimer1f4 = 0.0f;
        if (FacingCos > FacingCos1f0)
        {
            bLagging1ec = 1;
            bAxisArmed214 = 1;
        }
        else
        {
            bLagging1ec = 0;
        }

        // Once the leader has stayed in sight SightTime21c seconds, tell the content callback
        // (IBehaviorManager+0x110, ContentCallback slot 3).
        if (bLagging1ec != 0)
        {
            bool const bWasBelow = SightTimer218 < SightTime21c;
            SightTimer218 = static_cast<float>(static_cast<double>(DeltaTime) + SightTimer218);
            if (SightTimer218 > SightTime21c && bWasBelow)
            {
                pub::AI::ContentCallback* const Callback = Sys->Owner->ContentCbPtr;
                if (Callback != nullptr)
                    Callback->ship_in_sights_event(Leader->get_id());
            }
        }
        else
        {
            SightTimer218 = 0.0f;
        }
    }

    // The trail point, TrailOffset1e4 behind the leader along its own axis.
    if (!OwnShipIsPlayer(Sys))
        TrailDistance1e8 = TrailParamsOf(Sys).TrailDistance;        // Sys+0x16c4
    TrailOffset1e4 = static_cast<float>(
        static_cast<double>(Behavior_GetIfaceMaxScalarAttribute(Leader)) + TrailDistance1e8);   // FUN_062e6010
    TrailExtent220 = static_cast<float>(
        static_cast<double>(Behavior_GetIfaceMaxScalarAttribute(Leader)) + TrailOffset1e4);

    bool bOwnCruise = false;                                        // never read again
    static_cast<IObjRW*>(OwnerContext)->is_cruise_active(bOwnCruise);

    Matrix const LeaderRotation = Leader->get_transform().Rotation; // FUN_062e61f0, the 3x3 only
    Vector LeaderCenter;
    GetIfacePositionOrZero(&LeaderCenter, Leader);

    Vector Local;
    Local.x = 0.0f;
    Local.y = 0.0f;
    Local.z = TrailOffset1e4;
    if (GTrailMathEngine == nullptr)
        GTrailMathEngine = Acquire3DMathEngine();
    Vector Offset;
    GTrailMathEngine->TransformMatrix(reinterpret_cast<FVector&>(Offset),
                                      reinterpret_cast<FMatrix const&>(LeaderRotation),
                                      reinterpret_cast<FVector const&>(Local));
    Vector TrailPoint;
    TrailPoint.x = Offset.x + LeaderCenter.x;
    TrailPoint.y = Offset.y + LeaderCenter.y;
    TrailPoint.z = Offset.z + LeaderCenter.z;

    // The MatchThrottle2Idea drives when the leader flies the same way and the own ship lies
    // outside its forward cone (FUN_062e5190 with cos 0).
    bMatchThrottle23c = IsAlignedAndOutsideHeadingCone(NavOf(Sys), Leader, 0.0f) ? 1 : 0;
    BaseIdea* Driver;
    if (bMatchThrottle23c != 0)
    {
        MatchThrottle.SetWatchAndPosition(Leader, TrailPoint);      // FUN_062d65f0
        Driver = &MatchThrottle;
    }
    else
    {
        Move.SetMode(0);
        Move.SetScale40(1.0f);
        Move.SetWatchObject(Leader);                                // FUN_062d5760
        Move.SetAngle38(3.14159274f);
        Move.SetRange3c(TrailDistance1e8);                          // ICF fold 062a66b0
        Driver = &Move;
    }
    Driver->Update(DeltaTime);

    // FUN_062e4bb0 for effect, unless a player owner has the camera-level status cleared.
    if (!OwnShipIsPlayer(Sys) || BehaviorMgrPtr->get_camera_level_status())
        (void)GetInterceptClearanceOfIface(NavOf(Sys), Leader);

    // Face the leader, upright when the timer table asks for it.
    Face.SetWatchObject(Leader);                                    // FUN_062d7510
    if (Sys->Timers.IsTimer5Active())                              // FUN_062e8840
    {
        Vector Up;
        Up.x = 0.0f;
        Up.y = 0.0f;
        Up.z = 1.0f;
        Face.SetUpVector(Up);                                       // FUN_062d7640
    }
    Face.Update(DeltaTime);
    UpdateResult10 = 1;
    return 1;
}

// ----------------------------------------------------------------------------
// Slot 10, LAB_062cb110.
// ----------------------------------------------------------------------------
bool TrailBehavior::OnApplyDirective(void* DirectiveOp)
{
    if (DirectiveOp != nullptr)
    {
        unsigned char* const Op = static_cast<unsigned char*>(DirectiveOp);
        TrailDistance1e8 = *reinterpret_cast<float const*>(Op + 0x10);   // DirectiveTrailOp+0x10
        void* const Target = NavHelper->Timers.FindVerifiedObject((unsigned const*)(Op + 0xc));   // FUN_062e74a0
        if (Target != nullptr)
        {
            DirectiveTarget.set(reinterpret_cast<Watchable const*>(static_cast<char*>(Target) + 8));
            bHasDirectiveTarget1e0 = 1;
            Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
            return true;
        }
    }
    Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
    return false;
}

// ----------------------------------------------------------------------------
// Slot 12, FUN_062cb170.
// ----------------------------------------------------------------------------
void TrailBehavior::set_direction(Vector const& Dir)
{
    bDirectionSet22c = 1;
    DirectionX230 = -Dir.y;
    DirectionZ238 = 0.0f;
    DirectionY234 = -Dir.x;

    // FUN_062d4b20. Only a strictly positive factor rescales, by 1/factor kept on the x87
    // stack.
    float const Factor = BehaviorMgrPtr->get_turn_sensitivity_factor();
    if (Factor > 0.0f)
    {
        double const Rescale = 1.0 / static_cast<double>(Factor);
        DirectionX230 = static_cast<float>(Rescale * DirectionX230);
        DirectionY234 = static_cast<float>(Rescale * DirectionY234);
    }
    DirectionX230 = ClampUnit(DirectionX230);
    DirectionY234 = ClampUnit(DirectionY234);

    // A positive z also sets the trail distance.
    if (Dir.z > 0.0f)
        TrailDistance1e8 = Dir.z;
}

// ----------------------------------------------------------------------------
// Slot 14, LAB_062cb2a0: forwards to MatchThrottle2Idea::SetFlag38 (FUN_062d6700).
// ----------------------------------------------------------------------------
void TrailBehavior::set_cruise(bool bOn)
{
    MatchThrottle.SetFlag38(bOn);
}
