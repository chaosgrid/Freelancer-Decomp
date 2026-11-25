// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_gotobehavior.h"
#include "common_iface.h"   // IObjInspect / IObjRW, ObjInspectOf, ObjRwFromWatchable
#include "common_csys.h"          // Transform, Matrix, Acquire3DMathEngine
#include "common_systemmanager.h" // SystemManager: CachedPhysicalCenter/Radius, Flags1578, ExternalIface
#include "x86math.h"              // I3DMathEngine::TransformMatrix, Vector, Matrix
#include "common_pathfind.h"      // PathfindManager::IsCruiseUnobstructed (FUN_062de160)

#include <cmath>
#include <cstdint>
#include <cstring> // std::memcpy -- the GotoOp copy in OnApplyDirective (LAB_062fe670)

// FUN_062fd620. Behavior's ctor FUN_062d2020 runs first. The sub-ideas are FUN_062d4e60 (MoveIdea), FUN_062d6750 (FaceIdea) and
// FUN_062d9bf0 (SplineMoveIdea); Watcher370 opens the FUN_062c29b0 POD.
GotoBehavior::GotoBehavior(int InTypeId)
    : Behavior(InTypeId)
    , Move()
    , Face()
    , SplineMove()
    , SteerAxis{0, 0, 0}
    , UserThrottle(0)
    , Flag2e8(0)
    , Flag2e9(1)
    , Flag2ea(0)
    , Flag2eb(0)
    , Flag2ec(0)
    , Pad2ed{}
    , Field2f0(1.5f)
    , GotoKind(3)
    , ActiveTargetPos{}    // not written by retail's ctor; value-initialised (OnApplyDirective/Update write it first)
    , Destination{}
    , Watcher310()
    , Spline()
    , ArrivalRange(200.0f)
    , MaxSpeed(-1.0f)
    , MaxThrottle(1.0f)
    , Flag364(0)    // not written by retail's ctor; Update writes it before any read
    , Flag365(1)
    , bFlyThrough(0)
    , Flag367(0)    // not written by retail's ctor; SetContext/OnApplyDirective set it before any read
    , Flag368(0)
    , Flag369(0)
    , bGotoCruise(0)
    , bGotoNoCruise(0)
    , Flag36c(1)
    , Pad36d{}
    , Watcher370()
    , ProxNearDistance(0.0f)
    , ProxFarDistance(0.0f)
    // ProxCruiseSpeedRatio/Flags384/SavedFlags388 are the tail of the shared 0x1c-byte
    // proximity-watcher struct. Deviation: retail's FUN_062c29b0 zeroes only its first four
    // dwords and leaves these three uninitialised (as DockBehavior's ctor does at
    // +0x460/+0x464/+0x468); they are zeroed here because Update reads Flags384 (062fe1c4) on
    // a path reachable before FUN_062c2af0 writes it.
    , ProxCruiseSpeedRatio(0.0f)
    , Flags384(0)
    , SavedFlags388(0)
    , GotoOp()
{
}

// FUN_062fd7a0 (scalar deleting FUN_062fd780). Retail unwatches six watchers in this order:
// +0x370, +0x310, +0x204 (SplineMove.Face), +0x18c (SplineMove.Move), +0xb8 (Face),
// +0x40 (Move). ~Behavior (FUN_062d2080) chains after.
GotoBehavior::~GotoBehavior()
{
    Watcher370.set(nullptr);
    Watcher310.set(nullptr);
    SplineMove.Face.Watcher.set(nullptr);
    SplineMove.Move.Watcher.set(nullptr);
    Face.Watcher.set(nullptr);
    Move.Watcher.set(nullptr);
}

// ============================================================================
// The shared 0x1c-byte proximity-watcher struct (+0x00 BaseWatcher, +0x08 near, +0x0c far,
// +0x10 cruise multiplier, +0x14 zone flags, +0x18 saved flags). The near/far thresholds
// are multiplied by +0x10 exactly while the watched object reports cruise engaged.
// FPU compare polarity below comes from the raw listing, not the decompilation.
// ============================================================================

// FUN_062c29f0. Only caller is GotoBehavior::OnApplyDirective.
void ProxWatcherInit(unsigned char* Watcher, SystemManager* SysMgr, void* Target, float Near, float Far,
                     float CruiseRatio)
{
    BaseWatcher* const W = reinterpret_cast<BaseWatcher*>(Watcher);
    *reinterpret_cast<float*>(Watcher + 0x08) = Near;
    *reinterpret_cast<float*>(Watcher + 0x0c) = Far;
    IObjRW* const TargetObj = static_cast<IObjRW*>(Target);
    W->set(TargetObj != nullptr ? static_cast<Watchable const*>(TargetObj) : nullptr);
    *reinterpret_cast<unsigned*>(Watcher + 0x14) = 0;
    *reinterpret_cast<unsigned*>(Watcher + 0x18) = 0;
    IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(W->target()));
    if (Watched == nullptr)
        return;
    float* const Ratio = reinterpret_cast<float*>(Watcher + 0x10);
    // DAT_0639c430 = 0.0 (double); NaN takes the speed quotient.
    if (CruiseRatio > 0.0)
    {
        *Ratio = CruiseRatio;
    }
    else
    {
        // Own cruise speed (stored to float) over the watched object's engine speed; the
        // quotient is stored to float.
        IObjRW* const Self = ObjRwFromWatchable(const_cast<Watchable*>(SysMgr->Watcher.target()));
        float const OwnSpeed = Behavior_GetSpeed(
            ObjInspectOf(Self), 1.0f, IObject::ThrustEquipCruise);
        double const WatchedSpeed = Behavior_GetSpeed(
            ObjInspectOf(Watched), 1.0f, IObject::ThrustEquipEngine);
        *Ratio = static_cast<float>(OwnSpeed / WatchedSpeed);
    }
    // Unordered keeps the value.
    if (*Ratio < 1.0f)
        *Ratio = 1.0f;
}

// FUN_062c2af0.
void ProxWatcherRecomputeZoneFlags(unsigned char* Watcher, SystemManager* SysMgr, Vector const& RefPoint)
{
    IObjRW* const Obj = ObjRwFromWatchable(*reinterpret_cast<void**>(Watcher + 0x00));
    if (Obj == nullptr)
        return;

    unsigned* const ZoneFlags = reinterpret_cast<unsigned*>(Watcher + 0x14);
    unsigned* const SavedFlags = reinterpret_cast<unsigned*>(Watcher + 0x18);

    // Snapshot last tick's zones; the hysteresis bands below OR it back in.
    *SavedFlags = *ZoneFlags;

    Vector const& Pos = ObjInspectOf(Obj)->get_position();
    float const Dx = Pos.x - RefPoint.x;
    float const Dy = Pos.y - RefPoint.y;
    float const Dz = Pos.z - RefPoint.z;

    FAiTarget* const SteerElem = &SysMgr->SelfTarget;
    // Unrounded ST0 return.
    double const RadialClearance = GetSignedRadialClearanceToPoint(SteerElem, RefPoint); // FUN_062e4ea0

    // (z*z + x*x) + y*y of the float differences, square-rooted unrounded; unordered
    // leaves bit 1 clear.
    *ZoneFlags = 0;
    double const Distance = std::sqrt((static_cast<double>(Dz) * Dz + static_cast<double>(Dx) * Dx) +
                                      static_cast<double>(Dy) * Dy);
    if (RadialClearance > Distance)
        *ZoneFlags = 1;

    // Retail re-reads the watcher before each of the next two calls and re-guards neither.
    IObjRW* const ClearanceObj = ObjRwFromWatchable(*reinterpret_cast<void**>(Watcher + 0x00));
    // Rounded to float (stored into the RefPoint argument's home slot).
    float const InterceptClearance = static_cast<float>(GetInterceptClearanceOfIface(SteerElem, ClearanceObj)); // FUN_062e4bb0

    IObjRW* const CruiseObj = ObjRwFromWatchable(*reinterpret_cast<void**>(Watcher + 0x00));
    bool bCruiseActive = false;
    ObjInspectOf(CruiseObj)->is_cruise_active(bCruiseActive);

    // The limits are re-derived on the x87 stack for each compare and never stored.
    float const CruiseScale = *reinterpret_cast<float*>(Watcher + 0x10);
    double const NearLimit = bCruiseActive
        ? static_cast<double>(*reinterpret_cast<float*>(Watcher + 0x08)) * CruiseScale
        : static_cast<double>(*reinterpret_cast<float*>(Watcher + 0x08));
    double const FarLimit = bCruiseActive
        ? static_cast<double>(*reinterpret_cast<float*>(Watcher + 0x0c)) * CruiseScale
        : static_cast<double>(*reinterpret_cast<float*>(Watcher + 0x0c));

    // DAT_0639e6d4 = 50.0f, the half-width of the hysteresis band around each limit.
    double const HysteresisBand = 50.0;
    double const Clearance = InterceptClearance;

    // Greater or unordered skips each test. Inside the band the previous tick's zone bits
    // are OR'd back in.
    if (Clearance <= NearLimit - HysteresisBand)
        *ZoneFlags |= 2;
    else if (Clearance <= NearLimit + HysteresisBand)
        *ZoneFlags |= *SavedFlags;

    // Same on the far limit, but retail returns right after setting bit 0x4.
    if (Clearance <= FarLimit - HysteresisBand)
    {
        *ZoneFlags |= 4;
        return;
    }
    if (Clearance <= FarLimit + HysteresisBand)
        *ZoneFlags |= *SavedFlags;
}

// FUN_062c2c70.
double ProxWatcherComputeSpeedCap(unsigned char* Watcher, IObjRW* OwnerContext, SystemManager* SysMgr,
                                  int Selector, float SelectorValue)
{
    IObjRW* const Obj = ObjRwFromWatchable(*reinterpret_cast<void**>(Watcher + 0x00));
    if (Obj == nullptr)
        return 0.0f;

    ThrustEquipType const ThrustType = static_cast<ThrustEquipType>(Selector);

    // The value argument is SelectorValue only for Selector 1, else 1.0f; the selector is
    // passed on unchanged.
    float const QueryValue = (Selector == 1) ? SelectorValue : 1.0f;
    float const OwnerSpeed =
        Behavior_GetSpeed(ObjInspectOf(OwnerContext), QueryValue, ThrustType); // FUN_062e5af0

    unsigned const ZoneFlags = *reinterpret_cast<unsigned*>(Watcher + 0x14);
    double ZoneSpeed;
    if ((ZoneFlags & 3) != 0)
    {
        // Retail has two identical arms (one re-reads the watcher, one passes a null
        // receiver); collapsed, since Behavior_GetSpeed returns 0.0f on null.
        ZoneSpeed = Behavior_GetSpeed(
            ObjInspectOf(ObjRwFromWatchable(*reinterpret_cast<void**>(Watcher + 0x00))), 1.0f, ThrustType);
    }
    else if ((ZoneFlags & 4) == 0)
    {
        ZoneSpeed = 0.0f;
    }
    else
    {
        // Linear blend of the clearance across the two thresholds, unscaled here (unlike
        // ProxWatcherRecomputeZoneFlags, which cruise-scales them). Only Blend is rounded.
        float const NearThreshold = *reinterpret_cast<float*>(Watcher + 0x08);
        float const FarThreshold = *reinterpret_cast<float*>(Watcher + 0x0c);
        FAiTarget* const SteerElem = &SysMgr->SelfTarget;
        double const Clearance = GetInterceptClearanceOfIface(SteerElem, Obj); // FUN_062e4bb0
        float const Blend = static_cast<float>(
            1.0 - ((Clearance - NearThreshold) / (static_cast<double>(FarThreshold) - NearThreshold))); // DAT_0639a1d0 = 1.0f

        // Retail really multiplies by Blend twice (a square), unrounded until the caller's
        // store.
        float const FullSpeed = Behavior_GetSpeed(
            ObjInspectOf(ObjRwFromWatchable(*reinterpret_cast<void**>(Watcher + 0x00))), 1.0f, ThrustType);
        ZoneSpeed = (static_cast<double>(FullSpeed) * Blend) * Blend;

        // DAT_0639e884 = 10.0f; unordered keeps the value.
        if (ZoneSpeed < 10.0)
            ZoneSpeed = 0.0;
    }

    // `!(a > b)` so NaN returns the zone speed, as retail does.
    return !(ZoneSpeed > static_cast<double>(OwnerSpeed)) ? ZoneSpeed : static_cast<double>(OwnerSpeed);
}

// FUN_062c2d90.
void ProxWatcherNotifyZoneChange(unsigned char* Watcher, SystemManager* SysMgr, IBehaviorManager* BehaviorMgr,
                                 Vector const& RefPoint)
{
    IObjRW* const Obj = ObjRwFromWatchable(*reinterpret_cast<void**>(Watcher + 0x00));
    if (Obj == nullptr)
        return;

    // Three recognised transitions between last tick's snapshot (+0x18) and this tick's
    // zones (+0x14); anything else returns without notifying.
    unsigned const Saved = *reinterpret_cast<unsigned*>(Watcher + 0x18);
    unsigned const ZoneFlags = *reinterpret_cast<unsigned*>(Watcher + 0x14);

    int Code;
    if ((Saved & 4) != 0 && (Saved & 2) == 0 && (ZoneFlags & 2) != 0)
        Code = 1;
    else if ((Saved & 4) == 0 && (ZoneFlags & 4) != 0)
        Code = 2;
    else if ((Saved & 4) != 0 && (ZoneFlags & 4) == 0)
        Code = 3;
    else
        return;

    unsigned const OwnerId = SysMgr->SelfTarget.Iface->get_id();

    unsigned const WatchedId = ObjInspectOf(Obj)->get_id();

    // FUN_062d2a10.
    (void)BehaviorMgr->NotifyTetherEvent(
        OwnerId, WatchedId, static_cast<pub::AI::ContentCallback::TetherStatusType>(Code), RefPoint);
}

// ============================================================================
// FUN_062ebf50 -- ComputeFormationFollowerEnvelope. Translated from the raw listing; the
// Ghidra decompile is unusable here (MSVC reuses several frame slots). See
// common_gotobehavior.h for the call shape and the out-struct field table.
//
// x87 compares are written as plain `<` / `>`: false on NaN, the same branch retail takes.
//
// Retail constants (.rdata):
//   DAT_0639fe30 = 0x43960000 = 300.0f   non-cruise projection divisor
//   DAT_063a0608 = 0x447a0000 = 1000.0f  cruise projection divisor
//   DAT_063a0800 = 0xc1a00000 = -20.0f   "formation has any cohesion" gate
//   DAT_0639b21c = 0x41a00000 = 20.0f    squared-blend / tightness threshold
//   DAT_0639e6b8 = 0x3c23d70a = 0.01f    blend floor-to-zero epsilon
//   DAT_06399408 = 0x00000000 = 0.0f     the family's null-receiver fallback
//   DAT_0639df88 = 0x3fe0000000000000 = 0.5  (double) speed/angular floor
//   DAT_0639fa50 = 0x3fc999999999999a = 0.2  (double) wide-angle scale
//   DAT_063a07f8 = 0x3fefd70a3d70a3d7 = 0.99 (double) alignment threshold
// The three doubles are kept as double literals rather than float ones: retail
// really does widen the float operand and compare/multiply in the wider type,
// and 0.2 and 0.99 are not representable in float.
// ============================================================================

namespace
{
    // The SystemManager's Watcher (+0x08) target, `-8` downcast. Retail re-walks this chain at
    // all seven sites that need the owner, hence a function rather than a hoisted local.
    IObjRW* OwnerObjOf(SystemManager* Sys)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()));
    }

    // Once-per-tick physical-bounds cache (latch +0x157a), inlined twice (062ec05c,
    // 062ec154). Retail sets the latch first and does not null-test the interface.
    void RefreshCachedPhysicalExtents(SystemManager* Sys)
    {
        if (Sys->SelfTarget.bPosValid != 0)
            return;
        Sys->SelfTarget.bPosValid = 1;
        Sys->SelfTarget.Iface
            ->get_physical_radius(Sys->SelfTarget.CachedRadius, Sys->SelfTarget.CachedPos);
    }
}

bool FFormationLeaderState::ComputeFormationFollowerEnvelope(FormationFollowerEnvelope* Out)
{
    SystemManager* const Sys = Owner;
    if (OwnerObjOf(Sys) == nullptr)
        return false;

    // The one-argument is_cruise_active overload (+0x60), not the (bool&, bool&) one at +0x5c.
    bool bCruise = false;
    ObjInspectOf(OwnerObjOf(Sys))->is_cruise_active(bCruise);

    // FLT_MAX sentinel for the per-follower minimum (an immediate store, not .rdata).
    float MinFollowerAngularSpeed = 3.402823466e+38f; // 0x7f7fffff

    ThrustEquipType const ThrustType = bCruise ? ThrustEquipCruise : ThrustEquipEngine;
    float MinFollowerSpeed =
        Behavior_GetSpeed(ObjInspectOf(OwnerObjOf(Sys)), 1.0f, ThrustType); // FUN_062e5af0

    // Four of the five out fields are written before the follower-count early-out, so a
    // `false` return still leaves this partial result for the caller.
    Out->Speed = MinFollowerSpeed;
    Out->Throttle = 1.0f;
    Out->AngularThrottle = 1.0f;
    Out->bCruiseAllowed = 1;

    // 0x503 is the "is a CShip" mask. Retail calls CShip::get_formation_followers
    // non-virtually even when the mask misses (this == 0), and so does the port.
    CObject const* const Obj = ObjInspectOf(OwnerObjOf(Sys))->cobject();
    CShip* const Ship = (Obj != nullptr && (Obj->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
        ? static_cast<CShip*>(const_cast<CObject*>(Obj))
        : nullptr;

    // The receiver's embedded 32-entry scratch array (+0x1c..+0x9c).
    IObjRW** const Followers = FollowerScratch;
    unsigned const Count = static_cast<unsigned>(Ship->get_formation_followers(Followers, 0x20));

    // Retail's `TEST; JBE` rejects only an exactly-zero count, hence unsigned.
    if (Count == 0)
        return false;

    RefreshCachedPhysicalExtents(Sys);

    // Retail calls get_transform once and reads through the pointer for the whole loop.
    IObjInspect const* const Ext = Sys->SelfTarget.Iface;
    Transform const* const HoistedXform = &Ext->get_transform();

    Vector OwnerFacing = { 0.0f, 0.0f, 0.0f };
    GetNegatedIfaceDirection(&OwnerFacing, OwnerObjOf(Sys));

    float MaxProjection = -10000.0f;
    float MaxDistance = -10000.0f;
    float MinAlignCos = 1.0f;

    for (unsigned Index = 0; Index < Count; ++Index)
    {
        IObjRW* const Follower = Followers[Index];
        if (Follower == nullptr)
            continue;

        // Running minimum seeded from the owner's own speed.
        float const FollowerSpeed = ObjInspectOf(Follower)->get_speed(1.0f, ThrustType);
        if (FollowerSpeed < MinFollowerSpeed)
            MinFollowerSpeed = FollowerSpeed;

        // Called directly rather than through FUN_062e5b20 (no null case in the loop).
        float const FollowerAngularSpeed = ObjInspectOf(Follower)->get_angular_speed_XY(1.0f, 0.0f);
        if (FollowerAngularSpeed < MinFollowerAngularSpeed)
            MinFollowerAngularSpeed = FollowerAngularSpeed;

        // The follower's own station, in the leader's local frame.
        Vector FormationOffset = { 0.0f, 0.0f, 0.0f };
        ObjInspectOf(Follower)->get_formation_offset(FormationOffset);

        RefreshCachedPhysicalExtents(Sys);

        // Retail passes the Transform where an Matrix is expected: rotate only.
        Vector RotatedOffset;
        Acquire3DMathEngine()->TransformMatrix(RotatedOffset, HoistedXform->Rotation, FormationOffset);

        Vector const DesiredPos = { RotatedOffset.x + Sys->SelfTarget.CachedPos.x,
                                    RotatedOffset.y + Sys->SelfTarget.CachedPos.y,
                                    RotatedOffset.z + Sys->SelfTarget.CachedPos.z };

        Vector FollowerPos = { 0.0f, 0.0f, 0.0f };
        GetIfacePositionOrZero(&FollowerPos, Follower);
        Vector const Delta = { DesiredPos.x - FollowerPos.x,
                               DesiredPos.y - FollowerPos.y,
                               DesiredPos.z - FollowerPos.z };

        // A second, fresh get_transform() call (the hoisted pointer is not reused here);
        // the owner's backward axis.
        Matrix const& R = Ext->get_transform().Rotation;
        Vector const OwnerBack = { -R.E[0][2], -R.E[1][2], -R.E[2][2] };

        // FUN_062e1680 / FUN_062e1630. VecNormalize3's port takes (Src, Out); retail pushes
        // (Out, Src).
        Vector DeltaDir;
        VecNormalize3(&Delta, &DeltaDir);
        double const Cosine = ClampedDot3(DeltaDir, OwnerBack);

        // The distance is stored rounded; the projection uses the unrounded one.
        double const Distance = std::sqrt((static_cast<double>(Delta.z) * Delta.z +
                                           static_cast<double>(Delta.y) * Delta.y) +
                                          static_cast<double>(Delta.x) * Delta.x);
        float const DistanceRounded = static_cast<float>(Distance);
        double const Projection = Cosine * Distance;

        // Running maximum of the signed longitudinal offset (positive == the follower is
        // behind its station).
        if (Projection > static_cast<double>(MaxProjection))
            MaxProjection = static_cast<float>(Projection);

        if (DistanceRounded > MaxDistance)
            MaxDistance = DistanceRounded;

        // Running minimum of how well the formation is aligned (compared unrounded).
        Vector FollowerFacing;
        GetNegatedIfaceDirection(&FollowerFacing, Follower);
        double const AlignCos = ClampedDot3(OwnerFacing, FollowerFacing);
        if (AlignCos < static_cast<double>(MinAlignCos))
            MinAlignCos = static_cast<float>(AlignCos);
    }

    // Retail computes the widest follower distance and never reads it.
    (void)MaxDistance;

    // Uses the cruise flag sampled at the top, not re-queried.
    float const ProjectionDivisor = bCruise ? 1000.0f : 300.0f; // DAT_063a0608 / DAT_0639fe30

    // <= -20.0f (or unordered): every follower is already well ahead of its station and
    // there is nothing for the leader to wait for.
    if (MaxProjection > -20.0f)
    {
        // The blend stays on the x87 stack throughout.
        double Blend = static_cast<double>(MaxProjection) / ProjectionDivisor;
        // Squared only below 20.0f; at or above it the linear ratio is used as-is.
        if (MaxProjection < 20.0f)
        {
            Blend = Blend * Blend;
            if (Blend < static_cast<double>(0.01f))
                Blend = 0.0;
        }

        // Speed - Blend*Speed, rounded, then floored at half the speed (rounded target
        // against the unrounded floor).
        float TargetSpeed = static_cast<float>(static_cast<double>(MinFollowerSpeed) -
                                               Blend * MinFollowerSpeed);
        double const SpeedFloor = static_cast<double>(MinFollowerSpeed) * 0.5;
        if (static_cast<double>(TargetSpeed) < SpeedFloor)
            TargetSpeed = static_cast<float>(SpeedFloor);
        Out->Speed = TargetSpeed;

        // FUN_062e5b70.
        Out->Throttle =
            Behavior_GetProjectedThrottle(ObjInspectOf(OwnerObjOf(Sys)), TargetSpeed, ThrustType);

        // The first compare uses the unrounded angle, the second the rounded copy.
        double const AngleExact = std::acos(static_cast<double>(MinAlignCos));
        float const Angle = static_cast<float>(AngleExact);

        // +0x9c / +0xa0; the ctor seeds them with 0.1f and 0.2f radians.
        float const NarrowAngle = FormationNarrowAngle;
        float const WideAngle = FormationWideAngle;

        float AngularTarget;
        if (AngleExact < static_cast<double>(NarrowAngle))
        {
            // The FLT_MAX sentinel: the clamp below turns it into "no angular limit".
            AngularTarget = 3.402823466e+38f;
        }
        else if (Angle < WideAngle)
        {
            // Floored at half the minimum angular speed. With retail's 0.1/0.2 seeds the ramp
            // peaks at exactly that half, so the whole band collapses to the floor; other
            // seeds make the ramp live.
            AngularTarget = static_cast<float>(
                (static_cast<double>(WideAngle) - Angle) *
                (static_cast<double>(MinFollowerAngularSpeed) / WideAngle));
            double const AngularFloor = static_cast<double>(MinFollowerAngularSpeed) * 0.5;
            if (static_cast<double>(AngularTarget) < AngularFloor)
                AngularTarget = static_cast<float>(AngularFloor);
        }
        else
        {
            // DAT_0639fa50 = 0.2 (double).
            AngularTarget = static_cast<float>(static_cast<double>(MinFollowerAngularSpeed) * 0.2);
        }

        // Clamp down to the slowest follower's turn rate.
        if (AngularTarget > MinFollowerAngularSpeed)
            AngularTarget = MinFollowerAngularSpeed;
        Out->AngularSpeed = AngularTarget;

        // FUN_062e5ba0.
        Out->AngularThrottle = Behavior_GetProjectedAxisThrottleXY(ObjInspectOf(OwnerObjOf(Sys)), AngularTarget);
    }
    else
    {
        // Nothing to hold back for: the owner's own full speed and turn rate.
        Out->Speed = Behavior_GetSpeed(ObjInspectOf(OwnerObjOf(Sys)), 1.0f, ThrustType);
        Out->Throttle = 1.0f;
        // FUN_062e5b20 with both arguments 1.0f, unlike the in-loop (1.0f, 0.0f).
        Out->AngularSpeed = Behavior_GetAngularSpeedXY(ObjInspectOf(OwnerObjOf(Sys)), 1.0f, 1.0f);
        Out->AngularThrottle = 1.0f;
    }

    // While cruising the flag keeps the 1 stored above. Otherwise it means "the formation
    // is tight"; NaN in either test writes 0.
    if (!bCruise)
        Out->bCruiseAllowed = (MaxProjection < 20.0f && static_cast<double>(MinAlignCos) > 0.99) ? 1 : 0;

    return true;
}

// ============================================================================
// GotoBehavior's vtable slots (.rdata:063a2284). Sub-objects, from the ctor's LEAs:
// +0x2c MoveIdea (0x78), +0xa4 FaceIdea (0xc0), +0x164 SplineMoveIdea (0x174, ends +0x2d8).
// FUN_062d5a10 and FUN_062d88b0 are /OPT:ICF folds of one-line setters shared with other
// BaseIdea siblings, so they do not by themselves identify the receiver's class.
// ============================================================================

// Slot 1, LAB_062fd890. The ideas are bound only when BindContext (FUN_062d2090) succeeds.
bool GotoBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    bool const bBound = BindContext(Owner, Manager, Sys);
    if (bBound)
    {
        Flag367 = 1;
        Move.SetContext(OwnerContext, SysMgr, Manager);
        Move.SetEnabled(Flag367);                             // FUN_062d5a10 (raw byte)
        Face.SetContext(OwnerContext, SysMgr, Manager);
        SplineMove.SetContext(OwnerContext, SysMgr, Manager);
    }
    return bBound;
}

// Slot 3, FUN_062fd920.
void GotoBehavior::OnEngage()
{
    Behavior::OnEngage();
    Flag369 = 0;
    // Retail does not clear Flag368 before the call.
    int const CruiseResult =
        ObjInspectOf(OwnerContext)->is_cruise_active(reinterpret_cast<bool&>(Flag368));
    // Mode 2 only when the flag came back set AND the call returned 0.
    Move.SetMode((Flag368 != 0 && CruiseResult == 0) ? 2 : 0);    // FUN_062d5980
    Flag365 = 1;
    Flag36c = 1;
    Move.SetAngle38(3.14159274f);                             // 0x40490fdb
    UserThrottle = MaxThrottle;
    Flag2ea = 0;
    Flag2ec = 0;
    SetAvoidanceAllowed(1, 3);                                            // FUN_062d21d0
    SystemManager* const Sys = SysMgr;
    if (Sys->SelfTarget.Iface->is_player())
        set_cruise(true);
}

// Slot 4, FUN_062fd9c0.
void GotoBehavior::OnDisengage()
{
    Face.Commit();
    SystemManager* const Sys = SysMgr;
    if (Sys->SelfTarget.Iface->is_player() && GotoKind == 0)
        Move.SetField44(2);                                   // FUN_062d88b0
    Move.Commit();
    SysMgr->Thrust.SetStrafeDir(StrafeNone); // FUN_062e9030(+0x18, 0)
    Behavior::OnDisengage();
}

// Slot 5, LAB_062fda20 (argument unread).
int GotoBehavior::OnEvaluateReadiness(float Value)
{
    (void)Value;
    int Result = 2;
    if ((Flags28 & 0x2) != 0)
        Result = (Flag29 != 0) ? 3 : 1;
    else if (Flag29 != 0)
        Result = 3;
    Readiness = Result;
    return Result;
}

// Slot 6, LAB_062fda60 (argument unread).
EvaluateResult GotoBehavior::evaluate(unsigned int Context)
{
    (void)Context;
    IObjInspect const* const Owner = ObjInspectOf(OwnerContext);
    IObjRW* Target = nullptr;
    Owner->get_target(Target);
    if (Target == nullptr)
        return EvalBlocked;

    // One byte for both calls; retail zeroes it only before the first.
    bool bInLane = false;
    Owner->is_using_tradelane(&bInLane);
    if (bInLane)
        return EvalSkip;
    Target->is_using_tradelane(&bInLane);
    if (bInLane)
        return EvalSkip;

    // Only a zero get_type return with a ship-class bit reaches get_behavior_id.
    unsigned int Type = 0;
    int const TypeResult = Target->get_type(Type);
    if (TypeResult == 0 && (Type & OBJ_SHIP_MASK) != 0)
    {
        int const BehaviorId = Target->get_behavior_id();
        if (BehaviorId == BEHAVIOR_GUIDE || BehaviorId == BEHAVIOR_FOLLOW || BehaviorId == BEHAVIOR_LAUNCH)
            return EvalSkip;
    }
    if ((Type & OBJ_LOOT) != 0)
        return EvalBlocked;

    // DAT_063a227c = 25.0f; unordered gives EvalBlocked.
    Vector const Velocity = Target->get_velocity();
    double const SpeedSq = static_cast<double>(Velocity.x) * Velocity.x
                         + static_cast<double>(Velocity.y) * Velocity.y
                         + static_cast<double>(Velocity.z) * Velocity.z;
    return (SpeedSq > 25.0) ? EvalSkip : EvalBlocked;
}

// Slot 21, FUN_062c6d30.
void GotoBehavior::OnObjectLeftSystem(void* Arg)
{
    unsigned int const LeftId = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Arg));
    IObjRW* const Dest = ObjRwFromWatchable(const_cast<Watchable*>(Watcher310.target()));
    if (Dest != nullptr && Dest->get_id() == LeftId)
        Watcher310.set(nullptr);
    IObjRW* const Prox = ObjRwFromWatchable(const_cast<Watchable*>(Watcher370.target()));
    if (Prox != nullptr && Prox->get_id() == LeftId)
        Watcher370.set(nullptr);
}

// Slot 24, LAB_062c6db0.
IObjRW const* GotoBehavior::get_target()
{
    return ObjRwFromWatchable(const_cast<Watchable*>(Watcher310.target()));
}

// ============================================================================
// GotoBehavior::Update -- vtable slot 7, LAB_062fdb80 (jump table PTR_LAB_062fe658).
// Both `switch` statements dispatch on GotoKind (0 watch an object, 1 a point, 2 a spline).
// Clearance holds the FUN_062e4ea0 / FUN_062e4bb0 result in cases 1 / 0, then the
// FUN_062c2c70 speed cap in the tail.
// ============================================================================

namespace
{
    // The IObjRW behind a BaseWatcher; a raw value of 8 also counts as "nothing watched".
    IObjRW* GotoWatchedObject(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }

    // The negated third column of a frame's rotation, each component stored to float.
    Vector NegatedColumn2(Matrix const& Rotation)
    {
        return Vector{ -Rotation.E[0][2], -Rotation.E[1][2], -Rotation.E[2][2] };
    }

    // Summed in double; never stored.
    double LengthSquaredXyz(Vector const& V)
    {
        return (static_cast<double>(V.x) * V.x + static_cast<double>(V.y) * V.y) +
               static_cast<double>(V.z) * V.z;
    }
}

int GotoBehavior::Update()
{
    // A pending disengage request finishes the tick with 2.
    if ((Flags28 & 0x1) != 0)
    {
        UpdateResult = 2;
        return 2;
    }

    if (OwnerContext == nullptr || (Flags28 & 0x2) == 0)
    {
        UpdateResult = 3;
        return 3;
    }

    IObjInspect const* const Owner = ObjInspectOf(OwnerContext);
    SystemManager* const Sys = SysMgr;
    FAiTarget* const Elem = &Sys->SelfTarget;

    // Nothing reads bOwnerCruise; retail issues the call anyway.
    bool bOwnerCruise = false;
    int Result = 3;
    Owner->is_cruise_active(bOwnerCruise);

    // PosValid refill (flag set before the call).
    if (Elem->bPosValid == 0)
    {
        Elem->bPosValid = 1;
        Elem->Iface->get_physical_radius(Elem->CachedRadius, Elem->CachedPos);
    }

    // The lead point starts as our own cached position.
    ActiveTargetPos = Elem->CachedPos;

    float Throttle = MaxThrottle;
    float Clearance = 0.0f;
    if (Elem->Iface->is_player())
    {
        if (Flag2e8 != 0 && bGotoNoCruise == 0)
            Throttle = 1.0f;
        else
            Throttle = UserThrottle;
    }

    // Everything but kinds 0 and 1 goes straight to the tail.
    if (GotoKind == 1)
    {
        Clearance = static_cast<float>(GetSignedRadialClearanceToPoint(Elem, Destination));

        if (Flag2ea != 0)
        {
            Vector const Dir = NegatedColumn2(Elem->Iface->get_transform().Rotation);

            if (Elem->bPosValid == 0)
            {
                Elem->bPosValid = 1;
                Elem->Iface->get_physical_radius(Elem->CachedRadius, Elem->CachedPos);
            }

            // Ahead = CachedPos + Dir * 1750.0f (DAT_063a22f0). x and y round the product
            // before the add; z adds the unrounded product.
            float const ScaledX = Dir.x * 1750.0f;
            float const ScaledY = Dir.y * 1750.0f;
            Vector const Ahead{
                ScaledX + Elem->CachedPos.x,
                ScaledY + Elem->CachedPos.y,
                static_cast<float>(static_cast<double>(Dir.z) * 1750.0f + Elem->CachedPos.z) };

            // A non-zero steering triple makes the FaceIdea face that axis, otherwise it
            // faces the point ahead.
            Vector const& Steer = SteerAxis;
            if (LengthSquaredXyz(Steer) > 0.0)
                Face.SetAxis(Steer);
            else
                Face.SetPosition(Ahead);

            Move.SetField44(2);
            Move.SetDirection(Dir);

            // bFlyThrough is stored between the load and the compare.
            bFlyThrough = 1;
            if (Clearance < ArrivalRange)
            {
                // Arrived.
                Flags28 = static_cast<unsigned char>(Flags28 | 0x1);
                Flag368 = 0;
                OnDisengage();
                Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
                BehaviorMgr->SetDirectiveResult(2, 0);
                UpdateResult = 2;
                return 2;
            }
        }
        else
        {
            Face.SetPosition(Destination);
            Move.SetPosition(Destination);
        }

        ActiveTargetPos = Destination;
    }
    else if (GotoKind == 0)
    {
        if (GotoWatchedObject(Watcher310) == nullptr)
        {
            // The watched object is gone. Retail disengages and reports twice.
            Flags28 = static_cast<unsigned char>(Flags28 | 0x1);
            OnDisengage();
            Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
            BehaviorMgr->SetDirectiveResult(2, 2);

            if (Elem->Iface->is_player())
                ObjAffectOf(OwnerContext)->slot_6c(3, 0);

            Flags28 = static_cast<unsigned char>(Flags28 | 0x1);
            Flag368 = 0;
            OnDisengage();
            Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
            BehaviorMgr->SetDirectiveResult(2, 2);
            UpdateResult = Result;
            return Result;
        }

        // For the player, a watched object moving faster than 5 m/s ends the goto
        // (DAT_063a227c = 25.0f).
        if (Elem->Iface->is_player())
        {
            Vector const Velocity = GotoWatchedObject(Watcher310)->get_velocity();
            if (LengthSquaredXyz(Velocity) > 25.0)
            {
                ObjAffectOf(OwnerContext)->slot_6c(2, 0);
                Flags28 = static_cast<unsigned char>(Flags28 | 0x1);
                Flag368 = 0;
                OnDisengage();
                Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
                BehaviorMgr->SetDirectiveResult(2, 2);
                UpdateResult = Result;
                return Result;
            }
        }

        Vector WatchedPos;
        GetIfacePositionOrZero(&WatchedPos, GotoWatchedObject(Watcher310));
        ActiveTargetPos = WatchedPos;
        Clearance = static_cast<float>(GetInterceptClearanceOfIface(Elem, GotoWatchedObject(Watcher310)));

        if (Flag2ea != 0)
        {
            // Here the FaceIdea faces the steering triple unconditionally.
            Vector const Dir = NegatedColumn2(Elem->Iface->get_transform().Rotation);
            Face.SetAxis(SteerAxis);
            Move.SetDirection(Dir);

            if (Clearance < ArrivalRange)
            {
                Flags28 = static_cast<unsigned char>(Flags28 | 0x1);
                Flag368 = 0;
                OnDisengage();
                Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
                BehaviorMgr->SetDirectiveResult(2, 0);
                UpdateResult = 2;
                return 2;
            }
        }
        else
        {
            Move.SetWatchObject(GotoWatchedObject(Watcher310));
            Face.SetWatchObject(GotoWatchedObject(Watcher310));
        }
    }

    // The proximity watcher at +0x370.
    unsigned char* const ProxWatcher = reinterpret_cast<unsigned char*>(&Watcher370);
    ProxWatcherRecomputeZoneFlags(ProxWatcher, SysMgr, ActiveTargetPos);
    ProxWatcherNotifyZoneChange(ProxWatcher, SysMgr, BehaviorMgr, ActiveTargetPos);

    if (GotoKind == 2)
    {
        // The lead point is the spline's end point (+0x330).
        ActiveTargetPos = Spline.P2;
        SplineMove.SetBankBlendCap(Throttle);

        if (GotoWatchedObject(Watcher370) != nullptr)
        {
            // Clearance is reused for the speed cap.
            Clearance = static_cast<float>(
                ProxWatcherComputeSpeedCap(ProxWatcher, OwnerContext, SysMgr, 1, MaxThrottle));
            if (Behavior_GetSpeed(Owner, Throttle, ThrustEquipEngine) > Clearance)
                SplineMove.SetThrottleScale(Clearance);
        }

        int const SplineResult = SplineMove.Update(DeltaTime);
        if (SplineResult == 0)
        {
            Result = 1;
            UpdateResult = Result;
            return Result;
        }
        if (SplineResult != 1)
        {
            Result = 3;
        }
        else
        {
            // Done with the spline: keep going while the watcher still has an object outside
            // zone 2 and bFlyThrough is clear.
            if (GotoWatchedObject(Watcher370) != nullptr && (Flags384 & 0x2) == 0 && bFlyThrough == 0)
            {
                Result = 1;
                UpdateResult = Result;
                return Result;
            }
            Result = 2;
        }
    }
    else if (GotoKind == 0 || GotoKind == 1)
    {
        Move.SetField44(bFlyThrough != 0 ? 2 : 0);

        // Once per goto (Flag365); unordered gives 0.
        if (Flag365 != 0)
        {
            Flag364 = (Clearance > 1750.0f) ? 1 : 0;
            Flag365 = 0;
        }

        // For the player, Flag364 only while Flag2e8 is set; a non-zero value is then
        // confirmed by the pathfinder when there is one (FUN_062de160). bGotoCruise forces it on,
        // bGotoNoCruise forces it off.
        unsigned char bCruise;
        if (Elem->Iface->is_player())
        {
            bCruise = Flag2e8;
            if (Flag2e8 != 0)
                bCruise = Flag364;
        }
        else
        {
            bCruise = Flag364;
        }
        if (bCruise != 0 && BehaviorMgr->Pathfind != nullptr)
            bCruise = BehaviorMgr->Pathfind->IsCruiseUnobstructed() ? 1 : 0;
        if (bGotoCruise != 0)
            bCruise = 1;
        if (bGotoNoCruise != 0)
            bCruise = 0;

        // FUN_062ebf50. Retail zeroes only the out-struct's first four dwords; the flag byte
        // is value-initialised here too (the callee writes it on every path returning true).
        FormationFollowerEnvelope Envelope{};
        if (Sys->FormationLeaders.ComputeFormationFollowerEnvelope(&Envelope))
        {
            Move.SetAngle38(3.14159274f);
            if (Envelope.Throttle < Throttle)
                Throttle = Envelope.Throttle;
            Move.SetScale40(Throttle);

            // A clear envelope flag cancels cruise.
            Face.SetThrottleScale(Envelope.AngularSpeed);
            if (Envelope.bCruiseAllowed == 0)
                bCruise = 0;
        }
        else
        {
            Face.SetScale94(1.0f);                            // FUN_062d9490
        }

        unsigned char bTargetCruise = 0;
        if (GotoWatchedObject(Watcher370) != nullptr)
        {
            bool bWatchedCruise = false;
            GotoWatchedObject(Watcher370)->is_cruise_active(bWatchedCruise);
            bTargetCruise = bWatchedCruise ? 1 : 0;
        }

        // Unless forced, a watched object that is not cruising and is not one of our formation
        // followers (FUN_062c2ab0, FUN_062ec520) cancels cruise.
        if (bGotoCruise == 0 && GotoWatchedObject(Watcher370) != nullptr && bTargetCruise == 0)
        {
            if (!Sys->FormationLeaders.IsIdAmongOwnerFormationFollowers(Watcher370.get_watched_id()))
                bCruise = 0;
        }

        if (bCruise != Flag368)
        {
            if (bCruise != 0)
            {
                Move.SetMode(2);
                Flag368 = 1;
            }
            else
            {
                // SetRange3c is the DeviceTRC::bind_to_dc ICF fold.
                Move.SetRange3c(ArrivalRange);
                Move.SetMode(0);
                Flag368 = 0;
            }
        }

        // Speed cap from the proximity watcher (selector 4 in cruise, else 1). The query type
        // is cruise only while both we and the watched object cruise.
        if (GotoWatchedObject(Watcher370) != nullptr)
        {
            Clearance = static_cast<float>(ProxWatcherComputeSpeedCap(
                ProxWatcher, OwnerContext, SysMgr, Flag368 != 0 ? 4 : 1, MaxThrottle));
            ThrustEquipType const QueryType =
                (Flag368 != 0 && bTargetCruise != 0) ? ThrustEquipCruise : ThrustEquipEngine;
            if (Behavior_GetSpeed(Owner, Throttle, QueryType) > Clearance)
                Throttle = Behavior_GetProjectedThrottle(Owner, Clearance, QueryType);
        }

        int const FaceResult = Face.Update(DeltaTime);

        // For the player, Flag2ec sets throttle -1.0f (FUN_062e89a0) and a clear Flag2e9
        // deactivates the engine (FUN_062e91d0), both dispatching index 0. Otherwise the
        // MoveIdea's own Update picks the index.
        unsigned int Index = 0;
        bool bRunMove = true;
        if (Elem->Iface->is_player())
        {
            if (Flag2ec != 0)
            {
                Sys->Thrust.SetThrottle(-1.0f, 0);
                bRunMove = false;
            }
            else if (Flag2e9 == 0)
            {
                Sys->Thrust.SetEngineActive(false);
                bRunMove = false;
            }
        }
        if (bRunMove)
        {
            Move.SetScale40(Throttle);
            Index = static_cast<unsigned int>(Move.Update(DeltaTime));
        }

        // Jump table PTR_LAB_062fe658; an index above 3 keeps Result 3.
        if (Index == 0)
        {
            Result = 1;
            UpdateResult = Result;
            return Result;
        }
        if (Index == 1)
        {
            // The face counts as done while Flag36c is clear. The player keeps going when
            // neither cruise nor the face is pending.
            bool bFaceDone = (FaceResult == 1);
            if (Flag36c == 0)
                bFaceDone = true;
            if (Owner->is_player() && bCruise == 0 && !bFaceDone)
            {
                Result = 1;
                UpdateResult = Result;
                return Result;
            }

            // Same keep-going test as the spline arm.
            if (GotoWatchedObject(Watcher370) != nullptr && (Flags384 & 0x2) == 0 && bFlyThrough == 0)
            {
                Result = 1;
                UpdateResult = Result;
                return Result;
            }
            Result = 2;
        }
        else if (Index <= 3)
        {
            Result = 3;
        }
    }

    // LAB_062fe5ea: finish and report to the manager.
    Flags28 = static_cast<unsigned char>(Flags28 | 0x1);
    Flag368 = 0;
    OnDisengage();
    Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
    if (Result == 2)
        BehaviorMgr->SetDirectiveResult(2, 0);
    else
        BehaviorMgr->SetDirectiveResult(2, 2);
    UpdateResult = Result;
    return Result;
}

// Slot 10, LAB_062fe670: apply a pub::AI::DirectiveGotoOp.
bool GotoBehavior::OnApplyDirective(void* DirectiveOp)
{
    Flags28 = static_cast<unsigned char>(Flags28 & 0xfe);
    // The whole op (0x1c dwords, vptr included).
    std::memcpy(static_cast<void*>(&GotoOp), DirectiveOp, sizeof(GotoOp));
    pub::AI::DirectiveGotoOp const& Op = *static_cast<pub::AI::DirectiveGotoOp const*>(DirectiveOp);
    SystemManagerTimerTable const* const ObjDb = &SysMgr->Timers;
    GotoKind = Op.GotoKind;

    switch (Op.GotoKind)
    {
    case 0:
    {
        // FUN_062e74a0: watch the object Op.TargetId names.
        IObjRW* const Dest = static_cast<IObjRW*>(
            ObjDb->FindVerifiedObject((unsigned const*)&GotoOp.TargetId));
        Watcher310.set(Dest != nullptr ? static_cast<Watchable const*>(Dest) : nullptr);
        Move.SetRange3c(ArrivalRange);
        IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(Watcher310.target()));
        Move.SetWatchObject(Watched);
        Face.SetWatchObject(Watched);
        break;
    }
    case 1:
        Destination = Op.Destination;
        Move.SetPosition(Destination);
        Face.SetPosition(Destination);
        break;
    case 2:
        // The spline's control points come from op+0x38/+0x20/+0x2c/+0x44, in that order.
        Spline.P0 = *reinterpret_cast<Vector const*>(&Op.SplineP0X);
        Spline.P1 = *reinterpret_cast<Vector const*>(&Op.SplineP1X);
        Spline.P2 = *reinterpret_cast<Vector const*>(&Op.SplineP2X);
        Spline.P3 = *reinterpret_cast<Vector const*>(&Op.SplineP3X);
        Spline.calculate_arclength();
        SplineMove.SetSpline(&Spline);                              // FUN_062da380
        SplineMove.SetWideMode(Op.bGotoCruise);                        // FUN_062da410 (raw byte)
        ActiveTargetPos = Spline.P2;
        break;
    default:
        Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
        return false;
    }

    ArrivalRange = Op.ArrivalRange;
    Move.SetRange3c(ArrivalRange);
    MaxSpeed = Op.MaxSpeed;
    // DAT_0639f418 = 1.0e-5 (double); greater or unordered takes the projected throttle.
    if (!(static_cast<double>(Op.MaxSpeed) <= 1.0e-5))
        MaxThrottle = Behavior_GetProjectedThrottle(ObjInspectOf(OwnerContext), Op.MaxSpeed,
                                                IObject::ThrustEquipEngine);
    else
        MaxThrottle = 1.0f;
    Flag367 = Op.Field_59;
    Move.SetEnabled(Flag367);                                 // raw byte
    bFlyThrough = Op.bFlyThrough;
    Move.SetField44(Op.bFlyThrough != 0 ? 2 : 0);
    SplineMove.SetStopStyle(Op.bFlyThrough != 0 ? 2 : 0);               // FUN_062da400
    SetAvoidanceAllowed(Op.bDisableAvoidance == 0 ? 1 : 0, 3);
    bGotoCruise = Op.bGotoCruise;
    bGotoNoCruise = Op.bGotoNoCruise;

    // Only a nonzero op+0x5c is looked up, and only then are op+0x60/+0x64/+0x68 passed;
    // otherwise all four arguments are zero.
    void* ProxTarget = nullptr;
    float ProxNear = 0.0f;
    float ProxFar = 0.0f;
    float ProxRatio = 0.0f;
    if (Op.TetherObjectId != 0)
    {
        ProxTarget = ObjDb->FindVerifiedObject((unsigned const*)&GotoOp.TetherObjectId);
        ProxNear = Op.TetherNearRange;
        ProxFar = Op.TetherFarRange;
        ProxRatio = *reinterpret_cast<float const*>(&Op.TetherCruiseRatio);
    }
    ProxWatcherInit(reinterpret_cast<unsigned char*>(&Watcher370), SysMgr, ProxTarget,
                    ProxNear, ProxFar, ProxRatio);                  // FUN_062c29f0

    Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
    Flag2ea = 0;
    return true;
}

// FUN_062fe940: copies the 0x70-byte op back out (the inverse of OnApplyDirective's copy),
// vptr included, and returns Flags28 bit 0x2.
bool GotoBehavior::CopyGotoOp(pub::AI::DirectiveGotoOp* Out) const
{
    std::memcpy(static_cast<void*>(Out), static_cast<void const*>(&GotoOp), sizeof(GotoOp));
    return ((Flags28 >> 1) & 1) != 0;
}

// Retail's clamp to [-1, 1] (062fea1a, again for +0x2dc): NaN becomes -1.0f.
static float GotoClampUnit(float Value)
{
    if (!(Value > -1.0f))
        return -1.0f;
    if (!(Value < 1.0f))
        return 1.0f;
    return Value;
}

// Slot 12, FUN_062fe970.
void GotoBehavior::set_direction(Vector const& Dir)
{
    // Kind 1 always proceeds; kind 0 only on a strictly positive |Dir|^2.
    bool bProceed = false;
    if (GotoKind == 1)
    {
        bProceed = true;
    }
    else if (GotoKind == 0)
    {
        double const LengthSq = static_cast<double>(Dir.x) * Dir.x
                              + static_cast<double>(Dir.y) * Dir.y
                              + static_cast<double>(Dir.z) * Dir.z;
        bProceed = LengthSq > 0.0;
    }
    if (!bProceed)
    {
        Flag2ea = 0;
        return;
    }

    Flag2ea = 1;
    SteerAxis.x = -Dir.y;
    SteerAxis.z = 0.0f;
    SteerAxis.y = -Dir.x;
    // Only a strictly positive factor scales; the reciprocal stays on the x87 stack and each
    // product is rounded once.
    double const Factor = BehaviorMgr->get_turn_sensitivity_factor();
    if (Factor > 0.0)
    {
        double const InvFactor = 1.0 / Factor;
        SteerAxis.x = static_cast<float>(InvFactor * SteerAxis.x);
        SteerAxis.y = static_cast<float>(InvFactor * SteerAxis.y);
    }
    SteerAxis.x = GotoClampUnit(SteerAxis.x);
    SteerAxis.y = GotoClampUnit(SteerAxis.y);
    SteerAxis.z = -Dir.z;
}

// Slot 13, LAB_062feae0.
void GotoBehavior::set_throttle(float Throttle)
{
    Flag2eb = 1;
    UserThrottle = Throttle;
    Flag2e9 = 1;
}

// Slot 14, LAB_062feb00.
void GotoBehavior::set_cruise(bool bOn)
{
    if (bOn)
    {
        Flag2e8 = 1;
        UserThrottle = MaxThrottle;
        Flag2e9 = 1;
        return;
    }
    if (Flag2e8 != 0)
        UserThrottle = MaxThrottle;
    SysMgr->Thrust.engage_cruise(false);
    Flag2e8 = 0;
    Flag2e9 = 1;
}

// Slot 18, LAB_062feb60.
void GotoBehavior::brake_reverse(bool bOn)
{
    Flag2ec = bOn ? 1 : 0;
}

