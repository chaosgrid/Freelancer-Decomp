// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll MatchThrottle2Idea (ctor 0x062d5aa0). See src/include/common_idea.h.

#include "common_idea.h"
#include "common_aisteer.h"   // BearingAngleToPoint, GetIfacePositionOrZero
#include "common_behavior.h"  // Behavior_InvokeManagerSlotD4 / D8, Behavior_QueryRadiusEffect / ...
#include "common_cequip.h"    // ThrustEquipType (IObjInspect::get_speed's 2nd argument)
#include "common_csys.h"      // Matrix / Transform
#include "common_iface.h"     // IObjInspect / IObjRW / IBehaviorManager / ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager (IBehaviorManager::Pathfind)
#include "common_systemmanager.h" // SystemManager / ThrustManager (BaseIdea::Handle)
#include "x86math.h"          // I3DMathEngine::TransformMatrix

#include <cmath>
#include <cstring>

// ----------------------------------------------------------------------------
// MatchThrottle2Idea. Handle is the SystemManager, Context the IBehaviorManager,
// Object the owner IObjRW.
// ----------------------------------------------------------------------------

namespace
{
    PathfindManager* MatchPathfindOf(void* Context)
    {
        return static_cast<IBehaviorManager*>(Context)->Pathfind;
    }

    IObjRW* MatchWatched(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }

    // The shared tail of FUN_062d65f0 / FUN_062d6670: the watcher is re-read with no
    // null test (a null watch faults in retail) and its position copied to
    // Pathfind+0x200.
    void MirrorWatchedPosition(void* Context, BaseWatcher const& Watcher)
    {
        PathfindManager* const Pathfind = MatchPathfindOf(Context);
        Pathfind->CachedTargetPosition = MatchWatched(Watcher)->get_position();
    }
}

// FUN_062d5aa0. Distance28 (+0x28) and Speed4c/50 (+0x4c/+0x50) are not written by
// retail's ctor (they are set by SetWatchAnd* / SetContext); the port zeroes them.
MatchThrottle2Idea::MatchThrottle2Idea()
    : BaseIdea()
    , Watcher()
    , Position{ 0.0f, 0.0f, 0.0f }
    , Distance28(0.0f)              // not written by retail
    , Speed2c(0.0f)
    , ThrottleBias30(0.0f)
    , MaxBearing34(0.78539819f)     // 0x3f490fdb
    , Flag38(1)
    , Flag39(0)
    , Flag3a(0)
    , Flag3b(0)
    , Flag3c(1)
    , Flag3d(0)
    , Pad3e{}
    , ClosingFloor40(-200.0f)
    , ClosingBias44(-0.5f)
    , Field48(0.05f)                // 0x3d4ccccd
    , Speed4c(0.0f)                 // not written by retail
    , Speed50(0.0f)                 // not written by retail
    , ClosingWindow54(5.0f)
    , CatchUpRange58(500.0f)
{
}

// Slot 0, LAB_062d5b50 (RET 0xc).
bool MatchThrottle2Idea::SetContext(void* InObject, void* InHandle, void* InContext)
{
    if (InObject == nullptr || InHandle == nullptr)
        return false;

    Handle = (SystemManager*)InHandle;
    Object = InObject;
    Context = InContext;
    Attached = 1;

    IObjInspect const* const Owner = static_cast<IObjInspect const*>(static_cast<IObjRW*>(Object));

    // Retail shares one argument frame between the two calls: get_speed's result is
    // stored over a pushed dword and reused as FUN_062e5850's third argument.
    // 300.0f is .rdata:0639fe30.
    float const CruiseSpeed = Behavior_InvokeManagerSlotD8(Owner, 1.0f, ThrustEquipCruise);
    Speed4c = static_cast<float>(
        static_cast<double>(Behavior_QueryRadiusEffect(Owner, 0.0f, CruiseSpeed, 0.0f, ThrustEquipCruise)) + 300.0f);

    // Same pair for Engine; 50.0f is .rdata:0639e6d4.
    float const EngineSpeed = Behavior_InvokeManagerSlotD8(Owner, 1.0f, ThrustEquipEngine);
    Speed50 = static_cast<float>(
        static_cast<double>(Behavior_QueryRadiusEffect(Owner, 0.0f, EngineSpeed, 0.0f, ThrustEquipEngine)) + 50.0f);
    return true;
}

// Slot 2, LAB_062d5bf0 (RET 4). The argument is never read.
void MatchThrottle2Idea::SyncCruise(float DeltaTime)
{
    (void)DeltaTime;
    IObjRW* const Watched = MatchWatched(Watcher);
    if (Watched == nullptr || Object == nullptr)
        return;

    ThrustManager& Thrust = Handle->Thrust;
    if (Flag38 == 0)
    {
        Thrust.engage_cruise(false);
        return;
    }

    bool bCruise = false;
    Watched->is_cruise_active(bCruise);
    Vector const Velocity = Watched->get_velocity();
    unsigned char const bEngaged = Thrust.bCruiseEngaged;           // Handle+0x2c

    if (bCruise)
    {
        // 900.0f is .rdata:063a0494; unordered leaves.
        double const SpeedSq = (static_cast<double>(Velocity.y) * Velocity.y +
                                static_cast<double>(Velocity.z) * Velocity.z) +
                               static_cast<double>(Velocity.x) * Velocity.x;
        if (SpeedSq > 900.0f && bEngaged == 0)
            Thrust.engage_cruise(true);
        return;
    }

    if (bEngaged != 0 && Flag3d == 0)
        Thrust.engage_cruise(true);
}

// Slot 4, LAB_062d6550.
void MatchThrottle2Idea::Commit()
{
    SystemManager* const Sys = Handle;
    Vector const Zero{ 0.0f, 0.0f, 0.0f };
    Sys->Thrust.SetNudgeDirection(Zero);
    if (Flag39 == 0)
    {
        Sys->Thrust.SetThrottle(0.0f, 0);
        Sys->Thrust.engage_cruise(false);
    }
    Sys->Thrust.SetThrustersActive(false);
    MatchPathfindOf(Context)->CachedTargetPosition = Vector{ 0.0f, 0.0f, 0.0f };
}

// FUN_062d65f0 (RET 8).
void MatchThrottle2Idea::SetWatchAndPosition(void const* Object2, Vector const& Point)
{
    Watcher.set(Object2 != nullptr
        ? reinterpret_cast<Watchable const*>(static_cast<unsigned char const*>(Object2) + 8)
        : nullptr);
    Position = Point;
    Distance28 = 0.0f;
    MirrorWatchedPosition(Context, Watcher);
}

// FUN_062d6670 (RET 8). As FUN_062d65f0, but the point is zeroed and the float
// lands in +0x28.
void MatchThrottle2Idea::SetWatchAndDistance(void const* Object2, float Distance)
{
    Watcher.set(Object2 != nullptr
        ? reinterpret_cast<Watchable const*>(static_cast<unsigned char const*>(Object2) + 8)
        : nullptr);
    Position = Vector{ 0.0f, 0.0f, 0.0f };
    Distance28 = Distance;
    MirrorWatchedPosition(Context, Watcher);
}

// Slot 1, LAB_062d5ce0 (RET 4). DeltaTime is never read.
int MatchThrottle2Idea::Update(float DeltaTime)
{
    (void)DeltaTime;
    Flag3d = 0;
    IObjRW* const Watched = MatchWatched(Watcher);
    if (Watched == nullptr || Object == nullptr)
        return 3;

    IObjRW* const Owner = static_cast<IObjRW*>(Object);
    SystemManager* const Sys = Handle;
    FAiTarget* const Nav = &Handle->SelfTarget;

    // Own cached centre minus the watched position, each component rounded.
    Nav->RefreshPos();
    Vector const& WatchedPos = Watched->get_position();
    Vector Delta;
    Delta.z = Nav->CachedPos.z - WatchedPos.z;
    Delta.y = Nav->CachedPos.y - WatchedPos.y;
    Delta.x = Nav->CachedPos.x - WatchedPos.x;

    // Distance by the 0x5f3759df trick: DistSq stays double except for the rounded
    // copy that supplies the bit pattern; DistSq * s * (1.5 - 0.5 * s * s * DistSq).
    double const DistSq = (static_cast<double>(Delta.x) * Delta.x + static_cast<double>(Delta.y) * Delta.y) +
                          static_cast<double>(Delta.z) * Delta.z;
    float const DistSqF = static_cast<float>(DistSq);
    int DistSqBits;
    std::memcpy(&DistSqBits, &DistSqF, sizeof(DistSqBits));
    int const SeedBits = 0x5f3759df - (DistSqBits >> 1);           // arithmetic shift
    float Seed;
    std::memcpy(&Seed, &SeedBits, sizeof(Seed));
    float const Distance = static_cast<float>(
        ((1.5f - ((static_cast<double>(Seed) * Seed) * DistSq) * 0.5f) * Seed) * DistSq);

    // Flag38: ask whether the watched ship cruises / rides a lane (slot 23).
    bool bWatchedCruise = false;
    bool bWatchedLane = false;
    if (Flag38 != 0)
        Watched->is_cruise_active(bWatchedCruise, bWatchedLane);

    Vector const WatchedVel = Watched->get_velocity();
    float const WatchedSpeed = static_cast<float>(std::sqrt(
        (static_cast<double>(WatchedVel.x) * WatchedVel.x + static_cast<double>(WatchedVel.y) * WatchedVel.y) +
        static_cast<double>(WatchedVel.z) * WatchedVel.z));
    Vector const OwnVel = Owner->get_velocity();
    float const OwnSpeed = static_cast<float>(std::sqrt(
        (static_cast<double>(OwnVel.x) * OwnVel.x + static_cast<double>(OwnVel.y) * OwnVel.y) +
        static_cast<double>(OwnVel.z) * OwnVel.z));

    // The throttle that matches the watched speed: cruise only when cruising and not
    // in a lane, engine otherwise.
    ThrustEquipType const MatchType = (bWatchedCruise && !bWatchedLane) ? ThrustEquipCruise : ThrustEquipEngine;
    float const MatchThrottle = Behavior_InvokeManagerSlotD4(Owner, WatchedSpeed, MatchType);

    // The aim vector, relative to the own centre.
    Vector Aim;
    if (Distance28 > 0.0f)
    {
        Vector WatchedCenter;
        GetIfacePositionOrZero(&WatchedCenter, Watched);   // FUN_062e60e0

        // Inside 95% of the standoff (0.95 is a double, .rdata:063a04a0) the aim is
        // zero.
        if (static_cast<double>(Distance28) * 0.95 > Distance)
        {
            Aim = Vector{ 0.0f, 0.0f, 0.0f };
        }
        else
        {
            // Standoff = max(radius, atmosphere) of the watched object (FUN_062e6010)
            // + own cached radius + Distance28.
            Nav->RefreshPos();
            float const OwnRadius = Nav->CachedRadius;
            float const Standoff = static_cast<float>(
                (static_cast<double>(Behavior_GetIfaceMaxScalarAttribute(Watched)) + OwnRadius) + Distance28);
            // Unit vector from the watched centre to the own centre.
            Nav->RefreshPos();
            Vector Away;
            Away.z = Nav->CachedPos.z - WatchedCenter.z;
            Away.y = Nav->CachedPos.y - WatchedCenter.y;
            Away.x = Nav->CachedPos.x - WatchedCenter.x;
            Vector AwayDir;
            VecNormalize3(&Away, &AwayDir);
            // (Standoff * dir) rounded, then + the watched centre.
            Aim.x = Standoff * AwayDir.x;
            Aim.y = Standoff * AwayDir.y;
            Aim.z = Standoff * AwayDir.z;
            Aim.x = Aim.x + WatchedCenter.x;
            Aim.y = Aim.y + WatchedCenter.y;
            Aim.z = Aim.z + WatchedCenter.z;
            Nav->RefreshPos();
            Aim.x = Aim.x - Nav->CachedPos.x;
            Aim.y = Aim.y - Nav->CachedPos.y;
            Aim.z = Aim.z - Nav->CachedPos.z;
        }
        Position = WatchedCenter;
    }
    else
    {
        // The fixed point relative to the own centre.
        Nav->RefreshPos();
        Aim.x = Position.x - Nav->CachedPos.x;
        Aim.y = Position.y - Nav->CachedPos.y;
        Aim.z = Position.z - Nav->CachedPos.z;
    }

    // Both forward vectors: the watched one by FUN_062e6150, the own one as -column 2.
    Vector WatchedFwd;
    GetNegatedIfaceDirection(&WatchedFwd, Watched);
    Transform const& OwnXf = Sys->SelfTarget.Iface->get_transform();
    Vector OwnFwd;
    OwnFwd.x = -OwnXf.Rotation.E[0][2];
    OwnFwd.y = -OwnXf.Rotation.E[1][2];
    OwnFwd.z = -OwnXf.Rotation.E[2][2];

    float Throttle;
    int Mode;
    // Dot accumulated x, z, y.
    double const Facing = (static_cast<double>(OwnFwd.x) * WatchedFwd.x + static_cast<double>(OwnFwd.z) * WatchedFwd.z) +
                          static_cast<double>(OwnFwd.y) * WatchedFwd.y;
    if (!(Facing > 0.0) && !(Distance28 > 0.0f))
    {
        Throttle = 1.0f;
    }
    else
    {
        // Closing = |Aim| * clamp(dot(WatchedFwd, unit(Aim))): how far ahead of the
        // owner, along the watched heading, the aim point lies.
        Vector AimDir;
        VecNormalize3(&Aim, &AimDir);
        double const Cos = ClampedDot3(WatchedFwd, AimDir);         // FUN_062e1630
        double const AimLen = std::sqrt((static_cast<double>(Aim.x) * Aim.x + static_cast<double>(Aim.z) * Aim.z) +
                                        static_cast<double>(Aim.y) * Aim.y);
        float const Closing = static_cast<float>(AimLen * Cos);

        // Retail calls get_speed(1.0f, type) and discards the result; Speed2c takes
        // the matching cached speed.
        if (bWatchedCruise)
        {
            (void)Behavior_InvokeManagerSlotD8(Owner, 1.0f, ThrustEquipCruise);
            Speed2c = Speed4c;
            Mode = 4;
        }
        else
        {
            (void)Behavior_InvokeManagerSlotD8(Owner, 1.0f, ThrustEquipEngine);
            Speed2c = Speed50;
            Mode = 1;
        }

        ThrottleBias30 = 0.0f;
        // Unordered skips the bias block.
        if (ClosingWindow54 < std::fabs(Closing))
        {
            ThrottleBias30 = static_cast<float>(static_cast<double>(Closing) / Speed2c);
            if (static_cast<double>(WatchedSpeed) > 0.01 && OwnSpeed > WatchedSpeed && Closing > 0.0f)
            {
                // Time to slow from the own speed to the watched one, and the
                // distance covered meanwhile (stored to +0x2c).
                ThrustEquipType const Type = static_cast<ThrustEquipType>(Mode);
                float const SlowTime = Behavior_QueryRadiusEffect(Owner, 0.0f, OwnSpeed, WatchedSpeed, Type);
                float const SlowDistance = Behavior_QueryFalloffEffect(Owner, 0.0f, OwnSpeed, SlowTime, Type);
                Speed2c = SlowDistance;
                if (Closing <= SlowDistance && static_cast<double>(SlowDistance) > 0.01)
                    ThrottleBias30 = 0.0f;
            }
            if (Closing < ClosingFloor40)
                ThrottleBias30 = ClosingBias44;
        }

        Throttle = static_cast<float>(static_cast<double>(MatchThrottle) + ThrottleBias30);
        if (Throttle < 0.0f)
            Throttle = 0.0f;
    }

    if (bWatchedCruise)
    {
        Mode = 2;
        if (MatchThrottle > 0.8f && Throttle < 0.8f)
            Throttle = 0.8f;                                        // 0x3f4ccccd
    }
    else
    {
        Mode = 0;
    }

    // Target outside the bearing cone and more than 200 out: set the throttle
    // directly (Mode 2) or 0, skipping catch-up and the thruster notify.
    if (BearingAngleToPoint(Owner, Position) > MaxBearing34 &&                 // FUN_062e1c90
        GetSignedRadialClearanceToPoint(Nav, Position) > 200.0f)              // FUN_062e4ea0
    {
        Sys->Thrust.SetThrottle((Mode == 2) ? Throttle : 0.0f, Mode);
    }
    else
    {
        // Catch up when too far behind.
        if (Flag3c != 0 && Distance > CatchUpRange58)
        {
            Flag3d = 1;
            Throttle = 1.2f;                                        // 0x3f99999a
            Mode = 2;
        }
        if (Flag3b != 0 || Sys->Timers.IsTimer4Active())   // FUN_062e8850
        {
            if (!Sys->SelfTarget.Iface->is_player() && Mode == 0)
                Sys->Thrust.SetThrustersActive(Throttle > 2.0f);
        }
        else
        {
            Sys->Thrust.SetThrustersActive(false);
        }
        Sys->Thrust.SetThrottle(Throttle, Mode);
    }

    // No nudge matching while PathfindManager::Field_214 bit 0 is set or Flag3a is
    // clear.
    if ((MatchPathfindOf(Context)->Field_214 & 1) != 0 || Flag3a == 0)
        return 0;

    // The watched ship's nudge vector (slot 21); its length is compared unrounded.
    Vector Nudge{ 0.0f, 0.0f, 0.0f };
    Watched->get_nudge_vec(Nudge);
    double const NudgeLenD = std::sqrt((static_cast<double>(Nudge.z) * Nudge.z + static_cast<double>(Nudge.y) * Nudge.y) +
                                       static_cast<double>(Nudge.x) * Nudge.x);
    float const NudgeLen = static_cast<float>(NudgeLenD);
    if (!(NudgeLenD > 0.1f))
    {
        Vector const Zero{ 0.0f, 0.0f, 0.0f };
        Sys->Thrust.SetNudgeDirection(Zero);
        return 0;
    }

    // Watched speed for that nudge, the own throttle for that speed, along the unit
    // nudge, into IObjAffect::set_nudge_vec (Object+4, slot 2). Direct vcalls, no
    // null tests.
    float const NudgeSpeed = Watched->get_speed(NudgeLen, ThrustEquipTradelane);
    float const NudgeThrottle = Owner->get_projected_throttle(NudgeSpeed, ThrustEquipTradelane);
    Vector NudgeDir;
    VecNormalize3(&Nudge, &NudgeDir);
    Vector Out;
    Out.x = NudgeThrottle * NudgeDir.x;
    Out.y = NudgeThrottle * NudgeDir.y;
    Out.z = NudgeThrottle * NudgeDir.z;
    static_cast<IObjAffect*>(Owner)->set_nudge_vec(Out);
    return 0;
}
