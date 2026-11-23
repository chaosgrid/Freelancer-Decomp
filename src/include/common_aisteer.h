// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_ini.h"      // Vector, OrientedBox
#include "common_csys.h"     // Matrix, Transform, MatrixColumn0/1/2
#include "common_iface.h"    // IBehaviorManager
#include "common_cobject.h"  // CObject

#include <cstddef>

// ============================================================================
// Common.dll AI steering helpers, address range 0x062d6000-0x062ee000.
//
// This header declares the shared geometry and steering helpers the AI behaviors and
// ideas use: ObstacleAvoidance (PathfindManager+0x268), ICRSplineSegment, the ray / box /
// capsule tests, the intercept / clearance / heading-cone predicates on an FAiTarget,
// the steering-frame builders (BuildOrientationBasis, ComputeBankedSteeringFrame,
// TurnHelper) and GenericWeightedPick. The idea classes themselves (BaseIdea and its
// eleven subclasses) are in common_idea.h; SystemManager, its ThrustManager, FAiTarget
// and the timer table are in common_systemmanager.h.
//
// Definitions (src/Common/AI/): common_obstacleavoidance.cpp (ObstacleAvoidance, obstacle
// candidates / heading blend, FindClosestObstacleToAvoid), common_crspline.cpp
// (ICRSplineSegment), common_aiintercept.cpp (intercept / clearance / heading cones /
// GetIfacePositionOrZero), common_aifirecontrol.cpp (SustainedThresholdTimer, weapon fire
// control, the CM-dropper watcher), common_airaycast.cpp (ray / box / capsule tests,
// bearings), common_aisteer.cpp (steering frames, TurnHelper), common_aitarget.cpp
// (FAiTarget), common_gunconvergence.cpp (the SystemManager+0x1364 block), the idea
// Update bodies in common_faceidea.cpp / common_dodgeidea.cpp / common_splinemoveidea.cpp,
// and common_idea.cpp (GenericWeightedPick). Shared file-local helpers:
// common_aisteer_internal.h.
// ============================================================================

// ----------------------------------------------------------------------------
// ObstacleAvoidance -- per-ship forward-raycast obstacle steering.
//
// PathfindManager embeds it at +0x268 (common_pathfind.h; 0x268 + 0x318 == 0x580,
// PathfindManager's size). Its ctor is inlined into PathfindManager's (FUN_062db3b0),
// which also calls AttachOwner with the owning IBehaviorManager.
// ----------------------------------------------------------------------------
struct ObstacleAvoidance
{
    // Inlined into FUN_062db3b0: the vector ctor iterator over Slots (ICF-folded
    // Watchable::Watchable) only zeroes each Slot.Timer; then CursorIndex = 0 and
    // ResetContact. Nothing else is written; Owner/NoseDistance/Breadth/ElapsedTime come
    // from AttachOwner and PathfindManager's ctor zeroes PhysSystemId afterwards.
    ObstacleAvoidance();

    // FUN_062de220, __thiscall, RET 4. Owner = NewOwner, ElapsedTime = 0; then the owner's
    // get_archetype_extents(Min, Max) (IObjInspect slot 10) sets NoseDistance = -Min.z and
    // Breadth = max(|Max.x - Min.x|, |Max.y - Min.y|).
    void AttachOwner(IBehaviorManager* NewOwner);

    // FUN_062db780, called on this+0x2D4: Active, HasContact, ContactObjectId,
    // ContactCloseRate, ContactTimer = 0, in that order.
    void ResetContact();

    // One candidate deflection direction. Timer counts down to 0 every tick
    // (Update's decay loop); a slot is only overwritten once its timer has
    // expired, so a heading that was just tried and failed isn't immediately
    // retried on the very next attempt.
    struct Slot
    {
        float Timer;        // +0x00  cooldown; > 0 while "reserved"
        Vector Point;        // +0x04  candidate deflection point (world space)
        Vector Direction;     // +0x10  candidate deflection direction (unit-ish)
    };

    static_assert(sizeof(Slot) == 0x1c, "ObstacleAvoidance::Slot stride 7 floats (Common.dll.c 0x062de490)");

    // Update reads Owner+0xf0 (IBehaviorManager::WatchablePtr) and Owner+0xc
    // (IBehaviorManager::Pathfind, the PathfindManager this object is embedded in).
    IBehaviorManager* Owner;          // +0x00
    unsigned PhysSystemId;             // +0x04  PhySys system id (FindRayCollisions)
    // Both written only by AttachOwner from the owner's archetype extents. Update moves the
    // ray start from the ship's position NoseDistance units along -Z (the nose), sizes the
    // look-ahead from NoseDistance * 50 and the probe spiral radius from Breadth.
    float NoseDistance;                  // +0x08  -Min.z
    float Breadth;                       // +0x0C  max(|Max.x - Min.x|, |Max.y - Min.y|)
    float ElapsedTime;                   // +0x10  running dt accumulator (drives the probe spiral angle)

    Slot Slots[25];                        // +0x14 .. +0x2CF

    int CursorIndex;                         // +0x2D0  round-robin slot cursor for replacement
    unsigned char Active;                     // +0x2D4  true while actively steering around a contact
    unsigned char PadD5[3];

    // Point and Normal of the PhySys::RayHit that triggered avoidance (hit+0x04 and
    // hit+0x10; the hit's Object is not kept).
    Vector ContactPoint;                        // +0x2D8
    Vector ContactNormal;                        // +0x2E4
    unsigned ContactObjectId;                     // +0x2F0  hit CSimple::Id (hit.Object+0xb0)
    Vector AvoidDirection;                          // +0x2F4
    Vector AvoidPoint;                                // +0x300
    unsigned char HasContact;                          // +0x30C
    unsigned char PadE[3];
    float ContactTimer;                                  // +0x310
    float ContactCloseRate;                                // +0x314

    // FUN_062de490, __thiscall, RET 4. Per-frame tick: ages the 25 slot timers; with a
    // physics system bound and the ship moving, casts a ray from the nose along a slowly
    // spiralling probe ahead of the ship. A close enough blocking hit sends three random
    // rays into a cone around -Z, every blocked one recording its hit point / normal into
    // a slot, and the active slots are averaged into the escape direction.
    void Update(float Dt);

    // FUN_062df2d0, __thiscall, RET 0, float on the x87 stack (its one caller FSTPs it).
    // 1.0f unless Active with a watched owner; then by ContactCloseRate: > 5.0 -> 0.8f,
    // < 1.0 -> 0.4f, otherwise ((rate - 1.0) * 0.25)^2 * 0.4f + 0.4f.
    float GetCloseRateScale() const;

    // FUN_062df360, __thiscall, RET 8. While Active: *OutDirection = AvoidDirection,
    // *OutObjectId = ContactObjectId, true; otherwise false with both untouched.
    bool GetAvoidDirection(Vector* OutDirection, unsigned* OutObjectId) const;

    // FUN_062df3a0, __thiscall, RET 8. GetAvoidDirection's twin over AvoidPoint.
    bool GetAvoidPoint(Vector* OutPoint, unsigned* OutObjectId) const;
};

static_assert(offsetof(ObstacleAvoidance, NoseDistance) == 0x08, "ObstacleAvoidance+0x08 NoseDistance");
static_assert(offsetof(ObstacleAvoidance, Breadth) == 0x0c, "ObstacleAvoidance+0x0c Breadth");
static_assert(offsetof(ObstacleAvoidance, ElapsedTime) == 0x10, "ObstacleAvoidance+0x10 ElapsedTime");
static_assert(offsetof(ObstacleAvoidance, Slots) == 0x14, "ObstacleAvoidance+0x14 Slots");
static_assert(offsetof(ObstacleAvoidance, CursorIndex) == 0x2d0, "ObstacleAvoidance+0x2d0 CursorIndex");
static_assert(offsetof(ObstacleAvoidance, ContactPoint) == 0x2d8, "ObstacleAvoidance+0x2d8 ContactPoint");
static_assert(offsetof(ObstacleAvoidance, ContactNormal) == 0x2e4, "ObstacleAvoidance+0x2e4 ContactNormal");
static_assert(offsetof(ObstacleAvoidance, ContactObjectId) == 0x2f0, "ObstacleAvoidance+0x2f0 ContactObjectId");
static_assert(offsetof(ObstacleAvoidance, AvoidDirection) == 0x2f4, "ObstacleAvoidance+0x2f4 AvoidDirection");
static_assert(offsetof(ObstacleAvoidance, AvoidPoint) == 0x300, "ObstacleAvoidance+0x300 AvoidPoint");
static_assert(offsetof(ObstacleAvoidance, ContactTimer) == 0x310, "ObstacleAvoidance+0x310 ContactTimer");
static_assert(sizeof(ObstacleAvoidance) == 0x318, "ObstacleAvoidance sizeof 0x318");

// FUN_062de2a0, __thiscall on &Slots[0], RET 8. For every active (Timer > 0) slot,
// accumulates `2 * normalize(Ship.Translation - Slot.Point) + Slot.Direction`, then
// averages; false when no slot is active. The average is normalised unless its squared
// length is <= 1e-5, and true is returned either way. Ship is the Transform the caller
// got from the owner's get_orientation (slot 4, folded with get_transform).
bool ObstacleAvoidance_AverageActiveDirection(ObstacleAvoidance::Slot const* Slots,
                                              Transform const& Ship,
                                              Vector& OutDirection);

// FUN_062dc440. Rotates From toward To by a fixed, caller-supplied Angle (not the angle
// between them) around the shortest-arc axis cross(From, To). Returns To unchanged when
// the two are within ~2.56 degrees (dot > 0.999), avoiding a degenerate axis. Ghidra's
// decompile of the two math-engine calls is corrupt; the real shapes are QuaternionToMatrix
// (vtable +0x54) and TransformMatrix (+0x2C). Sole caller NudgeObstaclePathPoint passes
// Angle = 0.3f.
Vector RotateTowardBySlerp(Vector const& From, Vector const& To, float Angle);

// ----------------------------------------------------------------------------
// Ship heading-blend classifier pair (FUN_062dc590 / FUN_062dc840), now
// PathfindManager::UpdateHeadingBlend / ClassifyObstaclePhase. PathfindManager layout used
// by them:
//   +0x068 unsigned                   NextDeflectionSlot -- round-robin cursor
//   +0x06C unsigned[8]                RecentObstacleIds  -- recent-candidate cache
//   +0x08C ObstacleAvoidance::Slot[8] DeflectionSlots (stride 0x1c, tiles exactly to +0x16C)
//   +0x16C int    State       -- 0/1 while actively deflecting, 3 = idle
//                                (this classifier's own default each tick)
//   +0x170 unsigned  LastObstacleTag -- cached vtable+0x20 id of whichever
//                                obstacle drove the most recent decision
//   +0x174..+0x17C Vector Heading -- current computed heading direction,
//                                read+blended by UpdateHeadingBlend
//   +0x180 bool   InReaction
//   +0x181 bool   CloseEncounterFlag
//   +0x184..+0x19F ObstacleAvoidance::Slot ActiveDeflectionSlot -- cached
//                                copy of whichever DeflectionSlots[] entry
//                                drove the current reaction
//   +0x1A0 float  BlendTimer  -- owned by UpdateHeadingBlend, not
//                                touched by ClassifyObstaclePhase
//   +0x1B0 bool   BlendEnabled
//   +0x1B4..+0x1BC Vector TargetPosition -- effective steering target, refreshed
//                                every tick from *(Owner+0xc)+0x200
//   +0x1C0 IBehaviorManager* Owner
//   +0x260 unsigned CachedObstacleId
//   +0x264 bool     HasCachedObstacle
// ----------------------------------------------------------------------------

// FUN_062dbfb0. Candidate is a DeflectionSlots[] entry with Timer reused as the clearance
// Radius (Point = segment start, Direction = segment vector); Obstacle is a Transform
// (backward = -MatrixColumn2).
//
// Algorithm: builds Midpoint = Candidate.SegStart + 0.5*Candidate.SegDir,
// then DirToObstacle = normalize(Obstacle.Translation - Midpoint) (a real
// reciprocal sqrt via FUN_06347e60, not the fast-invsqrt idiom used later); bails
// with 3 ("path irrelevant to this obstacle") if a ray from Midpoint along
// DirToObstacle doesn't even intersect the candidate's own swept-capsule volume
// (RaySegmentCapsuleFirstHit). Three more re-derived unit directions
// (ObstacleDir, PointDir, ObstacleToPointDir -- all bailing with 1, "too
// close/urgent", if their source distance is under 1 unit) feed a
// "roughly-aligned" gate (dot(ObstacleDir,PointDir) > 0.9, i.e. the obstacle
// sits within ~25.8 degrees of the straight line to Point) that returns 1
// UNLESS the caller opted into the extra orientation check (ExtraCheck) AND
// the obstacle's own -MatrixColumn2 (its "backward" direction) is more than
// ~31.8 degrees off ObstacleToPointDir (dot < 0.85) -- i.e. the obstacle is
// facing roughly away from Point, making the straight-line proximity less
// concerning. Past that gate, a fast-invsqrt-approximated-distance compare
// (RayCapsuleHitPoint-to-Midpoint * 1.2 vs Obstacle-to-Midpoint) returns 2
// ("close enough already, no reroute needed") if the capsule's own surface
// is closer than 1.2x the obstacle itself. Otherwise, if Point is already
// within a fast-invsqrt-approximated 100 units of the obstacle, *OutPoint =
// Point unchanged (0, "use Point as-is"); else RotateTowardBySlerp(From=
// ObstacleDir, To=PointDir, Angle=0.3f) computes a nudged direction, and a
// second RaySegmentCapsuleFirstHit along it produces the final *OutPoint (0
// on success, 3 if even the nudged direction misses the capsule).
int NudgeObstaclePathPoint(ObstacleAvoidance::Slot const& Candidate, Vector const& Point,
                                       Transform const& Obstacle, Vector* OutPoint, bool ExtraCheck);

// FUN_062dbbe0. A PathfindManager thiscall
// in retail that never reads `this`; fills a deflection slot (Timer = capsule radius,
// Point = segment start, Direction = segment vector) for an obstacle candidate:
// from get_physical_radius / get_atmosphere_range when get_archetype_extents
// reports a sphere, otherwise a capsule along the longest extent of the archetype
// box, transformed to world space. Always returns true.
bool ResolveObstacleCandidateSlot(IObjRW* Candidate, ObstacleAvoidance::Slot& OutSlot);

// FUN_062e74e0 is SystemManagerTimerTable::GetOwnScanList (common_systemmanager.h).
// FUN_062dc840 / FUN_062dc590 are PathfindManager::ClassifyObstaclePhase /
// UpdateHeadingBlend (common_pathfind.h). All three are defined in
// common_obstacleavoidance.cpp.

// ----------------------------------------------------------------------------
// ICRSplineSegment -- 0x063327e0-0x06332eab. A 4-point uniform Catmull-Rom
// spline segment: P1..P2 is the interpolated curve, P0/P3 are the neighboring
// control points that only shape the incoming/outgoing tangent.
//
// Retail embeds it at outer+0xc6, +0xd2 and +0x348 of three unrelated objects; its six
// methods touch only its own fields.
//
// Size 0x40: operator= (0x062830a0) is COMDAT-folded with
// pub::AI::Personality::GunUseStruct::operator= (sizeof 0x40, a 16-dword copy), so the
// type has 8 bytes past SampleCount that no method touches (Reserved38).
struct COMMON_API ICRSplineSegment
{
    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the copy assignment.
    ICRSplineSegment& operator=(ICRSplineSegment const&) = default;

    Vector P0;              // +0x00  neighbor control point before the segment
    Vector P1;              // +0x0C  segment start -- calculate_spline_vector(0) == P1
    Vector P2;              // +0x18  segment end   -- calculate_spline_vector(1) == P2
    Vector P3;              // +0x24  neighbor control point after the segment
    float ArcLength;        // +0x30  running accumulated length; written by calculate_arclength()
    unsigned SampleCount;    // +0x34  numerical-integration sample count; ctor sets it to 20

    // +0x38 .. +0x3F: implied by the operator= fold, never touched by the six methods.
    // Meaning unknown (possibly list linkage).
    unsigned char Reserved38[0x08];

    // 0x063327e0. Retail only sets SampleCount; P0..P3/ArcLength/Reserved38 stay
    // uninitialized, so callers must fill in P0..P3 before evaluating the curve.
    ICRSplineSegment() : SampleCount(20) {}

    // 0x063327f0. Uniform Catmull-Rom basis at T (0 == P1, 1 == P2):
    //   0.5 * ( 2*P1 + T*(P2-P0) + T^2*(2*P0-5*P1+4*P2-P3) + T^3*(-P0+3*P1-3*P2+P3) )
    // Deviation: evaluated left to right; retail's x87 instruction order was not reproduced.
    Vector calculate_spline_vector(float T);

    // 0x06332bb0. Central-difference tangent: normalize(calculate_spline_vector(T+eps) -
    // calculate_spline_vector(T-eps)), eps == 0.001 (double at .rdata:0639c9f0).
    Vector calculate_spline_tangent(float T);

    // 0x06332c60. Sums SampleCount chord lengths (T = i/SampleCount, i in [1, SampleCount])
    // into ArcLength. Retail quirk kept: the first chord starts from the raw control point
    // P0, not from calculate_spline_vector(0) == P1 (compare get_spline_length).
    void calculate_arclength();

    // 0x06332d30. Same integration as calculate_arclength, but the running chord starts
    // from P1 and the sum is returned rather than stored.
    float get_spline_length();

    // 0x06332df0. A coarse one-directional line search, not a closest-point solve:
    // from T = StartT step by StepT while the squared distance to Point does not get
    // worse, and return the last accepted T. Like retail, it can loop indefinitely if
    // the distance never stops decreasing.
    float get_t_from_point(Vector const& Point, float StartT, float StepT);
};

// ----------------------------------------------------------------------------
// Ray-intersection cluster (0x062df830-0x062dfd80+): pure-math free functions.
// ----------------------------------------------------------------------------

// FUN_062df830. Classic analytic
// ray-sphere intersection: L = SphereCenter - RayOrigin; b = dot(L, RayDir)
// (RayDir assumed unit-length, per retail -- no normalization here);
// disc = Radius^2 + b^2 - dot(L,L); t0 = b - sqrt(disc), t1 = b + sqrt(disc).
// Returns false if disc < 0 (no real root) or both t0 and t1 are negative
// (sphere entirely behind the ray origin). Otherwise returns true and writes
// the nearest non-negative root to *OutT (t0 if t0 >= 0, else t1) -- OutT may
// be null (retail checks before writing). The `t0 >= 0 && t1 < 0` branch is
// unreachable (t1 >= t0) but kept from retail's if-chain.
// Radius is passed by address in retail (callee does FLD [EAX]; callers at
// .text:062d083e / 062dd23c push an address), hence the reference.
bool RaySphereIntersect(Vector const& RayOrigin, Vector const& RayDir, Vector const& SphereCenter, float const& Radius, float* OutT);

// FUN_062df940. Ray-vs-axis-aligned-box slab test in LOCAL space (box spans
// [-HalfExtent, +HalfExtent] around the local origin) -- the classic
// Kay/Kajiya slab-clipping algorithm, one half-space test per box face (6
// total: +-X, +-Y, +-Z), narrowing [TMin, TMax] in place. TMin/TMax are
// in/out by reference (caller seeds them, e.g. [0, FLT_MAX] for a plain ray).
// Returns false the instant any face test proves the interval empty or the
// ray is parallel to a slab and starts outside it; true otherwise, but ONLY
// if TMin or TMax actually changed from their input values (retail's final
// `local_out == in` guard -- an untouched interval means no face test ever
// tightened it, i.e. degenerate input, not a real hit).
bool RayAABBIntersect(Vector const& LocalOrigin, Vector const& LocalDir, Vector const& HalfExtent, float& TMin, float& TMax);

// FUN_062dfd80. Ray-vs-oriented-box (Box is common_ini.h's OrientedBox; Ghidra
// mistypes it as float). Transforms
// RayOrigin (recentered on the box) and RayDir into box-local space via dot
// products against each axis row, then defers to RayAABBIntersect with
// TMin=0/TMax=FLT_MAX. On a hit, writes:
//   *OutHitType = 2 (entered from outside, TMin > 0): OutPoints[0] = entry
//     point (RayOrigin + TMin*RayDir, WORLD-space RayDir), OutPoints[1] =
//     exit point (RayOrigin + TMax*RayDir).
//   *OutHitType = 1 (ray origin already inside the box, TMin <= 0): retail
//     writes only the exit point, into OutPoints[0]; OutPoints[1] stays stale.
// *OutHitType = 0 and false on a miss. OutPoints must have room for 2
// Vector3s (6 floats) even though only one is guaranteed written.
bool RayOrientedBoxIntersect(Vector const& RayOrigin, Vector const& RayDir, OrientedBox const& Box, int* OutHitType, Vector* OutPoints);

// FUN_062dffd0 (no Ghidra decompile; read from the listing). The helper calls are
// MatrixColumn0/1/2 (FUN_062e0480/FUN_062e04a0/FUN_062d3af0), and local_64..local_2c
// are one OrientedBox plus dot products.
//
// Takes a ray, a Transform and a local-space AABB laid out
// {MinX,MaxX,MinY,MaxY,MinZ,MaxZ} (the same 6-float shape
// TransformedBoxesIntersect takes; the caller at 0x062e47b0 de-interleaves a
// get_bounding_box(Min,Max) pair into it), builds the
// world-space OrientedBox, and runs Eberly's ray-vs-OBB separating-axis test
// (Wild Magic Ray3Box3): three box-axis tests of the form
// `|dot(Diff,A_i)| > Extent_i && dot(Diff,A_i)*dot(Dir,A_i) >= 0`, then three
// cross-axis tests `|dot(Dir x Diff, A_i)| > E_j*|dot(Dir,A_k)| +
// E_k*|dot(Dir,A_j)|`. Any one of them returns false. The box-axis tests
// are the ray form (start outside the slab, heading away), not the segment
// form: there is no `+ |dot(Dir,A_i)|` term.
//
// On a hit it defers to RayOrientedBoxIntersect for the real entry/exit points
// and returns the one nearer the ray origin; a hit-type of 1 (origin already
// inside the box) sets *OutStartsInside and returns that single point.
// *OutStartsInside is cleared up front, so it is written even on false. Retail
// null-checks only that pointer; OutPoint is written unconditionally.
bool RayTransformedBoxNearestHit(Vector const& RayOrigin, Vector const& RayDir,
                                            Transform const& Xform, float const* AabbMinMax,
                                            Vector* OutPoint, bool* OutStartsInside);

// FUN_062e0cf0. Builds an orthonormal basis {Out1, Out2,
// Dir} from a single direction vector Dir (in/out -- normalized in place
// first unless AlreadyNormalized is set), using the classic "zero out
// whichever of X/Y has the LARGER absolute value" single-vector basis
// construction: Out1's other two components are built from
// FastInvSqrt(sum-of-squares) of Z plus whichever of X/Y was KEPT, making
// Out1 unit-length and perpendicular to Dir; Out2 = cross(Dir, Out1)
// completes the basis. Ghidra's extraout_EDX locals are just Dir. Only caller is
// RaySegmentCapsuleIntersect.
void BuildPerpendicularBasis(Vector* Out1, Vector* Out2, Vector* Dir, bool AlreadyNormalized);

// FUN_062e0e10. Ray vs capsule: a constant-radius cylinder over axis range [0, |SegDir|]
// plus a sphere at each end (same shape as PointInCapsule).
//
// Builds an orthonormal (Basis1, Basis2, UnitAxis) frame via
// BuildPerpendicularBasis above, projects RayDir and (RayOrigin-SegStart)
// into it, and:
//   * If the ray is (near-)parallel to the axis (|dot(UnitAxis,RayDir)| >= 1):
//     degenerates to testing whether the ray's (fixed) lateral offset is
//     within Radius, writing both axis-crossing t's (0/2 hits only).
//   * Otherwise solves the constant-radius lateral (infinite-cylinder)
//     quadratic; each root is kept only if its axis-coordinate falls in
//     [0, Height] (Height = |SegDir| approximated as FastInvSqrt(x)*x).
//   * Falls through (whether the lateral test produced 0 or 1 valid roots --
//     2 already returns immediately) to test both end-cap spheres (centered
//     at SegStart and SegStart+SegDir, both radius Radius), each root
//     accepted only on the correct outward-facing side (axis-coordinate <= 0
//     for the SegStart cap, >= Height for the far cap) so it doesn't overlap
//     the cylinder body's own already-covered range.
// Returns the total hit count (0/1/2), writing that many t-values (ray
// parameter, i.e. HitPoint = RayOrigin + t*RayDir) into OutT in the exact
// order retail produces them (lateral roots first, then end-cap roots) --
// this order matters, see RaySegmentCapsuleFirstHit below.
int RaySegmentCapsuleIntersect(Vector const& RayOrigin, Vector const& RayDir, float Radius,
                                           Vector const& SegStart, Vector const& SegDir, float* OutT);

// FUN_062e1330. Thin wrapper: calls RaySegmentCapsuleIntersect, then walks its OutT[]
// results in order and keeps RayOrigin + t*RayDir for the first t >= 0.0. Retail
// computes later hits too but writes them to a dead stack slot. Returns true iff at
// least one t >= 0.0 was found.
bool RaySegmentCapsuleFirstHit(Vector const& RayOrigin, Vector const& RayDir, float Radius,
                                           Vector const& SegStart, Vector const& SegDir, Vector* OutPoint);

// FUN_062e1410. Classic point-vs-capsule test: Capsule is a
// 7-float record {Radius, SegStart(3), SegDir(3), SegDir NOT unit -- its own
// squared length is the segment's parameterization denominator}. Projects
// Point onto the segment, clamping to the near endpoint (t<0, i.e. the dot
// product gate fails) or far endpoint (t>1, i.e. dot >= SegDir's squared
// length) exactly like a standard closest-point-on-segment routine, then
// compares the squared distance from Point to that closest point against
// Radius^2.
bool PointInCapsule(Vector const& Point, float Radius, Vector const& SegStart, Vector const& SegDir);

// FUN_062e1630. Dot product of two Vector3s clamped to [-1, 1] (the guard before an
// acos). Returns double: retail leaves the value in ST0 unrounded.
double ClampedDot3(Vector const& A, Vector const& B);

// FUN_062e18f0. Classic continuous (swept) sphere-vs-sphere
// earliest-time-of-impact test over a single linear time step: sphere A
// moves from A0 to A1 (radius RadiusA), sphere B moves from B0 to B1 (radius
// RadiusB). RelPos0 = B0-A0, RelVel = (B1-B0)-(A1-A0) (relative displacement
// over the step); solves |RelPos0 + t*RelVel|^2 = (RadiusA+RadiusB)^2 for the
// smaller root of the resulting quadratic. Already-overlapping-at-t=0 short-
// circuits to *OutT=0/true. A near-zero relative-velocity-squared (<0.01) or
// negative discriminant reports *OutT=-1/false (no collision this step).
// Returns false (with a valid *OutT) if the computed earliest-impact t itself
// is negative (the collision, if any, already happened before this step).
bool SweptSphereCollisionTime(float RadiusA, Vector const& A0, Vector const& A1,
                                          float RadiusB, Vector const& B0, Vector const& B1, float* OutT);

// ----------------------------------------------------------------------------
// BearingAngleToIface / BearingAngleToPoint -- FUN_062e1aa0 / FUN_062e1c90.
//
// Both return fabs(acos(clamp(Dot, -1, 1))) where Dot is the normalised
// (Self centre - comparison point) dotted with column 2 of Self's get_transform()
// (+8/+0x14/+0x20, not negated). The centre is IObjInspect slot 0x2c's Vector
// out-parameter, inlined in the GetIfacePositionOrZero shape. A null Self (or, for
// the Iface variant, a null Target) returns 0.0 (DAT_06399408).
//
// Both return DOUBLE: the FABS result is left unrounded in ST0, and every retail
// caller FCOMPs it directly (062c8e96, 062c8fae, 062c9f3b, 062ca8dd, 062d5422,
// 062d55fe, 062d631e).
//
// The two inline normalisations are NOT the same and neither is VecNormalize3:
//   * Iface: inverse = 1.0f/Len kept unrounded; x and y FSTP'd to float, z left on
//     the stack.
//   * Point: inverse FSTP'd to float, all three products left on the stack.
// The dot is (z*m22 + y*m12) + x*m02 in both, FSTP'd to float before the clamp.
//
// BearingAngleToIface additionally queries Target's centre (first, before Self's).
double BearingAngleToIface(void* Self, void* Target);

// Ghidra counts one parameter; the point is read at 062e1d02 (entry ESP + 8), and all
// three callers push two dwords.
double BearingAngleToPoint(void* Self, Vector const& ComparisonPoint);

// ----------------------------------------------------------------------------
// BuildOrientationBasis / TransformedBoxesIntersect (0x062e0000-0x06301280).
// OBBIntersect, OrientedBox and the Vec*3 primitives they use are in common_ini.h.
// ----------------------------------------------------------------------------

// FUN_062e1f40. "Look-at" orthonormal basis builder: given a Forward
// direction and an optional Up hint, builds Out as [Right | Up' | Z] (column 0 =
// Right, column 1 = the re-orthogonalized Up, column 2 = Z), where Z =
// normalize(-Forward).
//
//   * If Forward is near-zero (|Forward|^2 < 0.0001, DAT_0639c2b8), Out stays
//     the all-zero matrix retail fills up front.
//   * If UpHint is supplied and is not within ~2.56 degrees of parallel to Z
//     (|dot(UpHint,Z)| < 0.999): Right = normalize(cross(UpHint, Z)), and
//     Up' = cross(Z, Right), not re-normalized.
//   * Otherwise (UpHint is null, or too close to parallel with Z): retail falls
//     back to Reference's own column 0/1 (C0/C1, via MatrixColumn0/1) to rebuild
//     an orthogonal basis via Gram-Schmidt, picking C0 or C1 depending on which
//     is further from parallel with Z, then sign-flipping the result to stay
//     consistent with Reference's own column 1 (near-parallel-to-C1 branch,
//     rebuilt from C0) or column 0 (otherwise, rebuilt from C1); the .cpp has the
//     per-branch details.
//
// Reference is dereferenced without a null check in the fallback; UpHint is the only
// nullable input.
void BuildOrientationBasis(Matrix& Out, Matrix const& Reference, Vector const& Forward, Vector const* UpHint);

// FUN_062e2630. Ghidra's unaff_EBX / unaff_retaddr are Reference / SpreadAngle; all five
// callers clean 0x10 bytes (Out is the hidden return pointer, returned in EAX).
// Observed arguments: SpreadAngle is the literal 0x3e490fdb (pi/16) at three of
// the five sites and a caller variable at the other two; BankAngle is 0.0f at
// three sites and a caller value at two.
//
// Algorithm:
//   1. Quat0 = MatrixToQuaternion(*Reference)                    [+0x50]
//   2. Quat1 = {cos(SpreadAngle/2), RandomVector(1.0f) * (sin(SpreadAngle/2) /
//      |RandomVector|)} -- a rotation of SpreadAngle about a uniformly random
//      axis. The 0.5f is DAT_0639c13c; the magnitude uses a real FSQRT, not the
//      clearance cluster's FastInvSqrt idiom, and the divide is unguarded.
//   3. Frame = QuaternionToMatrix(MulQuat(Quat0, Quat1))         [+0x18, +0x54]
//   4. Forward = -column2(Frame) (Frame[0][2]/[1][2]/[2][2], negated).
//   5. BuildOrientationBasis(Frame, WorkingCopyOfFrame, Forward, &UpHint), then
//      the returned frame is copied back over the working copy. Retail needs the
//      copy only because it aims Out at Frame's own storage.
//   6. Finally *Out = the working frame, and EAX = Out.
//
// The branch gate is `BankAngle > 0.0f` (DAT_06399408 == 0.0f). When it holds, UpHint
// is Reference's column 1 and a random-signed roll of BankAngle follows; otherwise
// UpHint is (0, 1, 0) and no roll happens. UpHint is never null here.
// The roll loops over Frame's three rows rotating each (col0, col1) pair -- a roll
// about local Z that leaves column 2 (Forward) untouched.
//
// Retail dead code kept: the sign draw is `if (rand() * (1/32768) < 0.0f) BankAngle =
// -BankAngle` (DAT_063997d0 == 1/32768). rand() is never negative, so the roll is
// always positive (the intent was likely "< 0.5f").
Matrix* BuildRandomSpreadFrame(Matrix& Out, Matrix const& Reference,
                                          float SpreadAngle, float BankAngle);

// FUN_062e0a60. Builds two OrientedBox values, one per (Transform, local-space AABB)
// pair --
//   Center     = TransformXform(Xform, AabbMin + AabbHalfExtent)   (world space)
//   AxisX/Y/Z  = Xform.Rotation's raw columns 0/1/2 (MatrixColumn0/1/2, no flip)
//   HalfExtent = per-axis |max-min|/2 of the AABB
// -- then calls OBBIntersect on the pair. AabbMinMax is 6 floats laid out
// {MinX,MaxX,MinY,MaxY,MinZ,MaxZ} (retail reads exactly this pattern: fields
// 0/2/4 as the min corner, |field[2k+1]-field[2k]| as the half-extent).
// TransformXform is FUN_06299880 (thiscall on the Transform, hidden Dst, Vector const&).
bool TransformedBoxesIntersect(Transform const& XformA, float const* AabbA, Transform const& XformB, float const* AabbB);

// ---------------------------------------------------------------------------
// Intercept-clearance cluster (0x062e4350-0x062e4ea0), common_aiintercept.cpp. Every
// "Elem" is an FAiTarget (common_systemmanager.h): the
// own ship's SystemManager::SelfTarget or an element of the timer table's target
// vector. The clearance is the distance between two cached centres minus the sum of
// the cached radii, clamped to >= 0, through retail's inlined fast-length idiom; when
// the 0x30000 type bits are missing retail falls back to EstimateLeadInterceptDistance.
bool IsWithinInterceptRange(FAiTarget* Elem, FAiTarget* Target, float Threshold); // FUN_062e4350
double GetInterceptClearance(FAiTarget* Elem, FAiTarget* Target); // FUN_062e4d00 (same body minus the threshold compare; ST0 return)

// FUN_062e4500. Sibling of IsWithinInterceptRange for callers that hold only the
// target's interface: it re-queries TargetIface's flags every call (no 0x7d cache), and
// TargetFlags&0x207 alone also passes the gate. Details in common_aiintercept.cpp.
bool IsWithinInterceptRangeOfIface(FAiTarget* Elem, IObjInspect const* TargetIface, float Threshold); // FUN_062e4500

// FUN_062e4bb0. Same body as IsWithinInterceptRangeOfIface, returning the clamped
// clearance instead of comparing it (Ghidra shows both parameters "<UNASSIGNED>"
// because their home slots are reused as scratch). MoveIdea::Update reaches it through
// NavInterceptClearanceOfIface. Returns double: retail leaves the clearance in ST0.
double GetInterceptClearanceOfIface(FAiTarget* Elem, IObjInspect const* TargetIface); // FUN_062e4bb0

// FUN_062e53e0, __cdecl, nine call sites.
//
// The whole body is one predicate: take the unit direction from `Iface`'s own
// physical centre to `Point`, dot it against `Iface`'s heading (the NEGATED
// column 2 of its get_transform(), i.e. the axis this module's LookMatrix calls
// "forward"), clamp the dot into [-1, 1], and return whether it is strictly
// below `CosThreshold`. In other words: is `Point` outside the cone of half-
// angle acos(CosThreshold) around where `Iface` is pointing.
// Retail reuses the Iface argument slot as get_physical_radius's Radius out-param.
//
// Two retail shapes kept as read:
//   * get_physical_radius returning NON-zero makes retail call get_id()
//     (slot 0x20) and DISCARD the result, then use a zero centre -- the filled
//     centre is used only on the ZERO return (062e53fd/062e5423).
//   * The near-zero guard is on the length AFTER the sqrt, against
//     .rdata:DAT_0639e6b8 == 0x3c23d70a == 0.01f, and a shorter separation
//     yields a zero direction (hence a zero dot), not an early out.
bool IsPointOutsideHeadingCone(Vector const& Point, IObjInspect const* Iface,
                                          float CosThreshold); // FUN_062e53e0

// FUN_062e5190. All four callers clean 0xc, so it takes three arguments (Ghidra lists
// two; see the .cpp for where the third is read).
//
// The two-object sibling of IsPointOutsideHeadingCone: `Elem` is the nav block
// (its owner at +0x84, its refresh byte at +0x7e and its cached centre/radius at
// +0x0c/+0x18, exactly as in IsWithinInterceptRangeOfIface), and the cone
// belongs to `TargetIface`. Returns true only when BOTH hold:
//   * the two objects' headings point the same way (their dot is > 0), and
//   * the unit direction from TargetIface's centre to Elem's centre, dotted
//     against TargetIface's heading and clamped into [-1, 1], is strictly below
//     CosThreshold.
// A null TargetIface degenerates both terms to zero, which fails the first test.
bool IsAlignedAndOutsideHeadingCone(FAiTarget* Elem, IObjInspect const* TargetIface,
                                    float CosThreshold); // FUN_062e5190

// FUN_062e4660. Elem is an FAiTarget, TargetIface the other
// object's IObjInspect. Refreshes Elem's bounding-box corner cache (+0x7f,
// FUN_062e4910) and its PosValid cache, casts a ray from Elem's surface point
// (frame translation + unit direction * cached radius) towards the target's
// position and intersects it with the target's oriented archetype box
// (FUN_062dffd0). Returns, in ST0 (double): 0 when the ray starts inside the
// box, the fast length to the nearest hit, or on a miss the fast length from
// Elem's frame translation to the target's physical centre (radius unused).
double EstimateLeadInterceptDistance(FAiTarget* Elem, IObjInspect const* TargetIface); // FUN_062e4660

// FUN_062e4ea0. Distance from Elem's cached position to an arbitrary world
// Point, minus Elem's cached radius -- unclamped (may be negative), and using
// a real sqrtf rather than the FastInvSqrt idiom of IsWithinInterceptRange.
// Retail re-checks the PosValid guard a second time (dead, kept).
// Returns the ST0 value unrounded (double); callers FCOMP / FSTP it themselves.
double GetSignedRadialClearanceToPoint(FAiTarget* Elem, Vector const& Point); // FUN_062e4ea0

// FUN_062e4f60. Argument order from the caller at 0x062e78df (PUSH 0; PUSH SelfElem;
// PUSH CandidateElem): the first argument is the candidate, the second the reference.
// Retail reuses Reference's stack slot as scratch for the heading dot.
//
// Returns true iff BOTH of:
//   1. dot(RefForward, TargetForward) > 0.0f -- the two objects' facings
//      point into the same half-space. Retail builds each forward vector as
//      MatrixColumn2 of the object's own get_transform (slot +0x0C) and runs
//      it through VecNegate3 (0x062c5a70) first; negating both sides leaves
//      the dot product unchanged, so the port skips the two negations.
//   2. dot(normalize(TargetPos - RefPos), -RefForward) < CosThreshold, with
//      the cosine clamped to [-1, +1]. At the one live call site
//      CosThreshold is 0.0f, i.e. "the target is in the reference's forward
//      half-space". A direction shorter than 0.01f degenerates to the zero
//      vector (retail's own epsilon branch at 0x062e5079), which makes the
//      dot product 0.0f and therefore fails a 0.0f threshold.
// Both objects' cached positions are refreshed through the usual bPosValid /
// get_physical_radius idiom before step 2.
bool IsTargetCoAlignedAndAhead(FAiTarget* Target, FAiTarget* Reference, float CosThreshold); // FUN_062e4f60

// FUN_062e6a80. Its seven callers are in the IUStateGraph cluster; it lives here beside
// the other FAiTarget accessors.
//
// Lazily-caches a {Low, High} float pair (Elem+0x00/+0x04) behind a RangeValid
// guard (Elem+0x7C), refreshed through a SECOND, independent vtable pointer
// (Elem+0x80, distinct from the Intercept-clearance cluster's Elem+0x84
// Iface) at slot 0x6C: `bool __thiscall RangeIface->vt[0x6C](RangeIface,
// float* OutLow, void* Iface, float* OutHigh)` -- Iface (Elem+0x84) is passed
// through unchanged as the query's own middle argument. If RangeIface or
// Iface is null, or the vtable call returns false, CachedLow/CachedHigh are
// forced to 0.0f/0.0f instead.
void GetCachedRange(FAiTarget* Elem, float* OutLow, float* OutHigh); // FUN_062e6a80

// FUN_062e6070, __cdecl. Ghidra's unaff_ESI / unaff_retaddr are the CachedRadius local
// and the Target argument slot, which retail reuses as get_atmosphere_range's out-param.
//
// The FAiTarget sibling of Behavior_GetIfaceMaxScalarAttribute (FUN_062e6010,
// common_behavior.cpp): where that one re-queries the interface
// every call, this one goes through the record's bPosValid cache. Returns the
// larger of the object's physical radius (+0x18, refreshed through the same
// slot-11 get_physical_radius guard RefreshPos uses) and its atmosphere range
// (slot 80, +0x140). The compare is strictly-greater, so a tie -- and an
// unordered compare -- yields the radius.
float GetAiTargetEffectiveRadius(FAiTarget* Target); // FUN_062e6070

// FAiTargetList, a.k.a. SystemManagerTimerTable (common_systemmanager.h) --
// SystemManager+0x34. Forward-declared rather than included: the type carries
// an 0xB0-byte blob that is really this file's own SustainedThresholdTimer, so
// common_systemmanager.h deliberately does not include common_aisteer.h and
// the dependency must not be closed in the other direction either.
struct SystemManagerTimerTable;

// FUN_062e7a30. "Pick the obstacle worth dodging": the owner sweeps its own bounding volume
// forward along its heading for a look-ahead time T and returns the NEAREST
// scanner contact that volume would run into. The result feeds straight into
// ComputeShapeBiasedAvoidDirection (FUN_062e55b0) as its `Target`.
//
// Retail is __thiscall (RET 8) on SystemManagerTimerTable (sole caller .text:062de098
// passes EBX+0x34), exposed here as a free function taking the receiver explicitly.
// It reads this+0x04 Owner (the SystemManager; +0x14FC its FAiTarget, +0x08 its
// Watcher), +0x08 ScannerEquipId and +0x1D bKeepCache (get_scanlist's bFlag).
//
// ExcludeId is compared against each candidate's get_id() and rejects on a
// match; TypeMask is ANDed with the candidate's cached get_type() flags.
// Returns the winning candidate's IObjInspect*, or null.
//
// Reject chain keeps candidates with heading dot >= 0 (ahead), LenSq <= Reach*Reach
// and CachedRadius <= 1000.0f. Type flag bit 29 set selects TransformedBoxesIntersect,
// clear selects SweptSphereCollisionTime.
IObjInspect* FindClosestObstacleToAvoid(SystemManagerTimerTable* Self,
                                                   unsigned ExcludeId,
                                                   unsigned TypeMask); // FUN_062e7a30

// FUN_062e6150. Negated direction/basis-row vector fetched through
// Iface's vtable slot 0xc; zero-fills on null Iface.
void GetNegatedIfaceDirection(Vector* OutDir, IObjInspect const* Iface);

// FUN_062e61a0: |IObjInspect::get_velocity()| of Iface, 0.0f on null Iface.
// Returns double: retail leaves the FSQRT result on the x87 stack (no FSTP to a float
// before the RET), and every caller compares that unrounded value.
double GetIfaceVectorMagnitude(IObjInspect const* Iface);

// FUN_062e4910, __cdecl. Ghidra recovered two of four parameters (unaff_EBX is Xform,
// unaff_retaddr is OutCorners). Sole caller is EstimateLeadInterceptDistance, which
// passes Elem+0x1C as OutCorners. EAX at return is incidental.
//
// It writes the eight corners of the axis-aligned box [Min, Max], each pushed
// through I3DMathEngine::TransformXform (slot +0x28), in a plain 3-bit order with
// X as the high bit and Z as the low one. Retail inlines the DAT_063fca48 singleton
// init in each of the eight blocks.
//
// So FAiTarget+0x1C..+0x7C is an eight-Vector cache of world-space bounding-box
// corners, guarded by +0x7F (alongside +0x7C RangeValid, +0x7D FlagsValid, +0x7E
// PosValid).
void TransformBoundingBoxCorners(Transform const& Xform, Vector const& Min,
                                            Vector const& Max, Vector* OutCorners); // FUN_062e4910

// FUN_062e55b0, __cdecl (Out is the hidden struct return). Ghidra recovered one of three
// parameters: unaff_EBP is Elem (used only as *(Elem+0x84)), unaff_ESI is Target, and
// unaff_retaddr is Target's slot reused as scratch. Sole caller passes
// SystemManager+0x14FC as Elem and FindClosestObstacleToAvoid's result as Target.
//
// What it computes -- a lateral dodge direction, in world space:
//   1. Target->get_physical_radius(Radius, Centre) [+0x2c]. The radius is never
//      read again: retail dead code.
//   2. LocalPos = InverseTransform(Elem's own Transform, Centre) [+0x3c], i.e.
//      the obstacle's centre in the Elem's local frame.
//   3. If |LocalPos| < 1e-5 (the DOUBLE at .rdata:0639f418), give up and answer
//      with the local +X axis, (1,0,0).
//   4. Target->get_archetype_extents(Min, Max) [+0x28], then classify the box:
//      HorizontalHalf = ((Max.x-Min.x) + (Max.z-Min.z)) * 0.5 in DOUBLE
//      precision (.rdata:0639df88), VerticalExtent = Max.y-Min.y.
//      bFlatObstacle = HorizontalHalf/VerticalExtent > 2.0; bTallObstacle is the
//      reciprocal test. Mutually exclusive by construction.
//   5. bFlatObstacle squashes the local X (right) component by 0.3
//      (.rdata:063a0788) before renormalising, which biases the dodge towards
//      going over/under a wide flat obstacle; bTallObstacle squashes the local Y
//      (up) component instead, biasing towards going around a tall one. On the
//      tall path retail rescales only Y and X and leaves Z alone -- an asymmetry
//      that is reproduced as-is (it cannot change the answer, since Z is
//      discarded in step 6).
//   6. Direction = normalize2D(-X, -Y) with Z forced to exactly zero (retail
//      multiplies the reciprocal by .rdata:06399408 == 0.0f to get it).
//   7. Result = Rotate(Elem's Transform, Direction) [+0x30] -- rotation only, no
//      translation, so this stays a pure direction.
// Freelancer's local frame is X=right, Y=up, Z=forward, so step 6 is a purely
// lateral sidestep away from the obstacle -- consistent with the caller being on
// the obstacle-avoidance path.
Vector* ComputeShapeBiasedAvoidDirection(Vector* Out, FAiTarget* Elem, IObjInspect* Target); // FUN_062e55b0

// FUN_062e5c00, __cdecl. Ghidra recovered one of four parameters; its unaff_ESI is the
// default-1.0f return slot, unaff_EBX the Radius out-param, unaff_retaddr / (float)param_1
// the third / fourth parameters, and -(float)pfVar2 is -TurnRadius. Callers 062d563f
// (passes cached *(Obj+0x60) / *(Obj+0x5c) as the speeds) and 062ddb91.
//
// What it computes -- a steering cost in radians for reaching Point:
//   1. Frame = Iface->get_transform(), with its translation REPLACED by the
//      object's centre of mass from get_physical_radius. (Retail dispatches slot
//      +0x10, get_orientation, but copies a full Transform; sound only because
//      slots 3 and 4 fold to one implementation. The port calls get_transform().)
//   2. Bail (returning 1.0f) unless 0.01f < |Point - Centre| < max(Radius*5,100)
//      (.rdata:0639e6b8 = 0.01f, 063a078c = 5.0f, 0639fb58 = 100.0f).
//   3. TurnRadius = LinearSpeed / AngularSpeed, where a zero argument means "not
//      cached" and is refilled from get_angular_speed_XY(0,1) [+0xf0] and
//      get_speed(1.0f, ThrustEquipEngine) [+0xd8] respectively. Bail unless
//      TurnRadius > 0.01f.
//   4. The two turn-circle centres: TransformXform(Frame, (+/-TurnRadius,0,0))
//      through the 3DMathEngine singleton (slot +0x28).
//   5. Bail unless Point lies INSIDE the nearer circle (min centre distance <
//      TurnRadius) -- i.e. only unreachable-without-turning-away targets cost
//      anything.
//   6. Result = max(dot(normalize(Point-Centre), -column2(Frame)), 0)^2
//               * MinCentreDist * AngularSpeed / LinearSpeed.
//      The exponent at .rdata:0639c420 is the double 2.0, so retail's _CIpow is
//      a square. The trailing factor has units of radians.
// Retail dead code kept: get_throttle's out-parameter is written and never read.
// The result stays in ST0 unrounded in retail and the MoveIdea caller compares it
// before rounding (062d5644), so the port returns double.
double EstimateTurnCircleApproachCost(IObjInspect* Iface, Vector const& Point,
                                      float AngularSpeed, float LinearSpeed); // FUN_062e5c00

// FUN_062e6200. Iface->get_type() (IObjInspect slot +0x88), or 0 on null Iface or
// failure. Retail reuses its own argument slot as the out-param (Ghidra's unaff_retaddr).
// Callers mask the result: 0x3 in FindObjectDockingAtBase, 0x30000 in
// BuzzBehavior::EvaluateReadiness, 0x7f0000 in DockBehavior/FollowBehavior::
// CheckClearanceProgress.
unsigned GetObjectTypeOrZero(IObjInspect const* Iface); // FUN_062e6200

// FUN_062e7400 is SystemManagerTimerTable::FindObjectDockingAtBase (common_systemmanager.h):
// given a moon/planet (type & 0x3), walks the IObjDB for docking rings (TypeMask 0x20)
// and returns the one whose get_dock_target() equals the base's get_base() id -- the
// fallback DockBehavior::RecomputeApproach uses when a planet has no dock hardpoints.

// FUN_062e3460. cdecl, four arguments, returns
// bool in AL. "Build a perpendicular frame with angle-threshold rejection":
// false if |Direction|^2 < 1e-5 (double DAT_0639f418); NegDir =
// normalize(-Direction) (zero when its length is not above 0.01f); false if
// OutFrame is null; Reference defaults to (0,1,0); false if
// |clamp(dot(NegDir, Reference), -1, 1)| is not below DotThreshold. Otherwise
// Right = normalize(cross(Reference, NegDir)), Up = cross(NegDir, Right), and
// OutFrame = [Right | Up | NegDir] (column 0 at +0/+0xc/+0x18, column 1 at
// +4/+0x10/+0x1c, column 2 through MatrixSetColumn2 / FUN_062e3710).
// OutFrame is left untouched on every rejection.
bool BuildPerpendicularFrame(Vector const& Direction, Matrix* OutFrame, float DotThreshold, Vector const* Reference);

// FUN_062e2c00. 3DMathEngine-singleton wrapper (DAT_063fca48 lazy init): returns
// MathEngine->vtable[0x2c](&Result, &M, &V), i.e. TransformMatrix. M is the Matrix*
// from the iface's slot 0xc at ComputeBankedSteeringFrame's call site.
Vector TransformVectorByMatrix(Matrix const& M, Vector const& V);

// FUN_062e2c70 (no Ghidra pseudocode; common_aisteer.cpp carries the hand trace,
// .rdata constants and the nine __cdecl arguments).
//
// Out = (P0 + P1) * Ratio + P2 * ZFactor, where P0/P1/P2 are Command's scalar
// projections onto Obj's own orientation columns, Ratio scales the pitch/yaw
// plane by how far Obj still has to turn (clamped by the throttle it can
// actually deliver there), and ZFactor scales the roll axis. With Obj == null
// the whole body collapses to Out = Command. Returns &Out, matching retail's
// EAX-on-exit, which every call site consumes.
//
// Obj is the caller's own bound Object (this->0x4), an IObjInspect; the six slots it
// touches (0x8, 0xc, 0x50, 0xe4, 0xec, 0xf8) are ordinary IObjInspect getters.
//
// ReferenceAngle and the bank flag come straight from the call sites (0.35f and
// true at 062d72ee, this->0x98 and true at 062d731b). TurnAngle is an acos
// result on the angle path and a literal 0.0f on the other, and the body
// special-cases exactly that 0.0f.
Vector* LimitAxisThrottleForTurn(Vector& Out, IObjInspect const* Obj, Vector Command,
                                            Matrix const* TargetFrame, float TurnAngle,
                                            float ReferenceAngle, bool bBankBlend);

// ----------------------------------------------------------------------------
// FUN_062e8e60 is ThrustManager::BlendNudgeDirection (common_systemmanager.h,
// common_thrustmanager.cpp). Retail callers: DodgeIdea::Update (062d849e) and
// PathfindManager (062dd8f7).

// ----------------------------------------------------------------------------
// ComputeBankedSteeringFrame -- FUN_062e37e0, cdecl, caller cleans 0x14.
//
// Elem is the SystemManager+0x14FC view (IObjInspect at Elem+0x84, cached
// get_type flags at Elem+8, their valid byte at Elem+0x7d). TargetDir is the
// desired facing, WorkingFrame (in/out) the ship's orientation, BankVector
// (in/out) the bank state, Mode selects whether BankVector is first taken
// into the iface's local frame (TransposeTransform) or used as is.
//
// Returns false without touching WorkingFrame / BankVector when the flags
// lack bit 0x30000 or (Mode != 0) the frame is already within 0.98 of the
// iface forward. Otherwise WorkingFrame is always rebuilt by
// BuildOrientationBasis and BankVector re-expressed through the iface
// transform; the return value says whether the steer branch ran (TargetDir
// not within 0.999 of the iface forward, and not behind the iface up axis
// unless the object is the player). The .cpp has the section-by-section addresses.
bool ComputeBankedSteeringFrame(FAiTarget* Elem, Vector const& TargetDir, Matrix& WorkingFrame, Vector& BankVector, unsigned char Mode);

// LAB_062d9d00 is SplineMoveIdea::Update (common_idea.h, common_splinemoveidea.cpp).

// SustainedThresholdTimer (FUN_062e6a20) is declared in common_systemmanager.h, next to the
// timer table that holds eleven of them.

// FUN_062e60e0 (Ghidra's unaff_ESI is an artifact). Leaf helper with ~21 callers:
// fetches a position through Iface's vtable slot 0x2c (this, void** UnusedOut,
// Vector* Out); the middle out-param lands in the stack copy of Iface and is never
// read. If Iface is null, or the call returns non-zero (after which slot 0x20 is also
// called), *OutPos is zero-filled.
void GetIfacePositionOrZero(Vector* OutPos, IObjInspect const* Iface);

// The AI fire-control functions FUN_062ea050..FUN_062ebb10 are members of
// SystemManagerGunGroupManager / FCmDropperWatcher (common_systemmanager.h); their
// definitions are in common_aifirecontrol.cpp. FUN_062e8400 is
// SystemManagerTimerTable::HasTargetInForwardArc.

// FUN_062eb020. A thiscall helper on a Transform, RET 8: *Out = the math engine's
// InverseTransform (+0x3c) of *In by *Self (through a 12-byte local); returns Out.
Vector* TransformInversePoint(Transform const* Self, Vector* Out, Vector const* In);

// The SystemManager+0x1364 block's ctor / Update / SetFormationLeader / SetFollowLeader /
// SyncFormationLeader (FUN_062ebbc0 / 062ebcb0 / 062ec870 / 062ec970 / 062ec6b0) are
// FGunConvergenceStateBlock members (common_systemmanager.h, common_gunconvergence.cpp).

static_assert(offsetof(ICRSplineSegment, P0) == 0x00, "ICRSplineSegment+0x00 P0");
static_assert(offsetof(ICRSplineSegment, P1) == 0x0c, "ICRSplineSegment+0x0c P1");
static_assert(offsetof(ICRSplineSegment, P2) == 0x18, "ICRSplineSegment+0x18 P2");
static_assert(offsetof(ICRSplineSegment, P3) == 0x24, "ICRSplineSegment+0x24 P3");
static_assert(offsetof(ICRSplineSegment, ArcLength) == 0x30, "ICRSplineSegment+0x30 ArcLength");
static_assert(offsetof(ICRSplineSegment, SampleCount) == 0x34, "ICRSplineSegment+0x34 SampleCount");
static_assert(offsetof(ICRSplineSegment, Reserved38) == 0x38, "ICRSplineSegment+0x38 Reserved38");
static_assert(sizeof(ICRSplineSegment) == 0x40, "ICRSplineSegment sizeof 0x40 (proven by operator= COMDAT fold with GunUseStruct)");

// LAB_062d68b0 and LAB_062d7950 are FaceIdea::Update and DodgeIdea::Update
// (common_faceidea.cpp / common_dodgeidea.cpp); the steering helpers they share are in
// common_aisteer.cpp (common_aisteer_internal.h).

// ----------------------------------------------------------------------------
// TurnHelper::get_angular_throttle -- retail 0x062e28f0,
// `?get_angular_throttle@TurnHelper@@YA?AVVector@@ABVMatrix@@0PAM@Z`.
// A namespace in retail, not a class: YA marks a free __cdecl function, and nothing
// else of TurnHelper is exported. Four stack dwords (hidden Vector return, A, B,
// OutAngle); Ghidra drops OutAngle.
//
// Pipeline:
//   1. TransposedA = Transpose(A)                       [Matrix ctor, retail FUN_06284f70]
//   2. RelativeMat = MulMatrix(A=B, B=TransposedA)       [Acquire3DMathEngine +0x20]
//   3. Quat(W,X,Y,Z) = MatrixToQuaternion(RelativeMat)   [+0x50]
//   4. Angle2 = 2*acos(clamp(W,-1,1))                    (candidate *OutAngle)
//   5. Axis = (asin(X),asin(Y),asin(Z)), normalized when |Axis|^2 > 1e-5f
//   6. LocalAxis = TransposeTransform(A, Axis)           [+0x38] (world->local)
//   7. dominant-plane scale: if max(|LocalAxis.x|,|LocalAxis.y|) > 0.001,
//      scale both by 1/max (FDIVR double 1.0, no square root); Z snapped to
//      +1 (z > 0) or -1 when |Z| > 0.1, otherwise kept
//   8. WorldThrottle = TransformMatrix(A, ScaledLocalAxis) [+0x2c] (local->world)
//   9. if Angle2 > +pi:      WorldThrottle = -WorldThrottle; Angle2 = 2*pi - Angle2
//      else if Angle2 < -pi: WorldThrottle = -WorldThrottle; Angle2 = Angle2 + 2*pi
//  10. if OutAngle != null: *OutAngle = Angle2 (post step-9 adjustment)
//  return WorldThrottle
//
// Constants: DAT_06399404=-1.0f, DAT_0639a1d0=1.0f, DAT_0639b984=1e-5f,
// DAT_06399410=1.0 (double), DAT_0639c9f0=0.001 (double), DAT_063a0460=0.1 (double),
// DAT_0639c430=0.0 (double), DAT_0639d208=+pi, DAT_0639d2a0=-pi, DAT_0639c9e8=2*pi (floats).
namespace TurnHelper
{
    COMMON_API Vector __cdecl get_angular_throttle(Matrix const& A, Matrix const& B, float* OutAngle);
}

// FUN_062d76d0. rand()-weighted 4-way state picker: State[0] is
// the current 1..4 tag (the entry matching it is treated as 0 rather than
// re-read -- "don't re-pick the current state"), State[1..4] are 4 float
// weights. Draws `rand() * (1/32768) * (sum of the masked weights)` and walks
// a cumulative-threshold ladder to land on a new 1..4 tag, writes it back to
// State[0] and returns it. Used by DodgeIdea::Update's case-0 axis picker.
int GenericWeightedPick(int* State);
