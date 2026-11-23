// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- PathfindManager (common_pathfind.h): the per-ship AI path / obstacle state
// IBehaviorManager owns at +0x0C, together with the user-zone probe pair FUN_062db830 /
// FUN_062ddcd0 and the two oriented-box helpers FUN_062dba00 / FUN_062dbb90 that the probe
// and the zone shapes (common_zone.cpp) share.

#include "common_pathfind.h"
#include "Physics/common_geometry_internal.h" // ClampPointToBoxSurface, MathTransposeTransformVia3DMathEngine
#include "common_aisteer.h"
#include "common_behavior.h"      // GetIfacePositionOrZero
#include "common_cequip.h"        // IObject::ThrustEquipEngine (PathfindManager ctor)
#include "common_cobjdb.h"        // IObjDB::GetZoneDb (SetPathfindObjectDb, FUN_062db7c0)
#include "common_csys.h"          // Acquire3DMathEngine, Matrix
#include "common_iface.h"         // IBehaviorManager, IObjRW, ObjRwFromWatchable
#include "common_misc.h"          // FastInvSqrt, SUBOBJ_ID_NONE
#include "common_systemmanager.h" // SystemManager (Owner->Sys)
#include "common_universe.h"      // Universe::ISystem::GetSphereProperties (PathfindManager::Update's zone probe), GetId
#include "x86math.h"              // I3DMathEngine::TransposeTransform (vtable +0x38)
#include "AI/common_aisteer_internal.h" // CmnAiSteer::PathfindOwnerObject

#include <cmath>
#include <cstdlib>
#include <cstring>

using CmnAiSteer::PathfindOwnerObject;

// ============================================================================
// PathfindManager (common_pathfind.h)
// ============================================================================

void PathfindManager::ObstacleReaction::ResetState()
{
    // FUN_062db7a0, in retail's store order.
    State = 3;
    LastObstacleTag = 0;
    Heading.z = 0.0f;
    Heading.y = 0.0f;
    Heading.x = 0.0f;
    bInReaction = false;
    bCloseEncounter = false;
}

PathfindManager::PathfindManager(IBehaviorManager* NewOwner)
    // Members are built in declaration order, which is retail's order for every member with
    // a constructor: Zone (FUN_062db740), DeflectionSlots, Watcher1C8, Watcher218
    // (FUN_062de190 with a null object), Avoidance.
{
    // FUN_062db3b0. The stores follow retail's order; the member constructors have already
    // run (retail clears bUserZoneActive before Zone's ctor).
    bUserZoneActive = false;
    for (int Index = 0; Index < 8; ++Index)
    {
        RecentObstacleIds[Index] = 0;
    }
    NextDeflectionSlot = 0;
    Reaction.ResetState();

    Watcher1C8.set(nullptr);
    Timer1E0 = 0.0f;
    Period1E4 = 1.0f;
    Timer1E8 = 0.0f;
    Period1EC = 1.0f;
    Timer20C = 0.0f;
    Period210 = 1.0f;

    Field_214 = 0;
    Watcher218.set(nullptr);
    Field_220.z = 0.0f;
    Field_220.y = 0.0f;
    Field_220.x = 0.0f;
    Field_22C.z = 0.0f;
    Field_22C.y = 0.0f;
    Field_22C.x = 0.0f;
    Field_238 = false;
    Field_23C = 1.0f;
    Field_240 = 0;

    CachedObstacleId = 0;
    bHasCachedObstacle = false;

    Owner = NewOwner;
    Field_1F0 = 0;
    Flag245 = 1;
    Field_248 = -1;
    Field_24C = 1;
    Field_250 = 0x7f0000;
    Field_254 = 1;
    Field_064 = 0;
    Watcher1C8.set(nullptr);
    Field_1D0.z = 0.0f;
    Field_1D0.y = 0.0f;
    Field_1D0.x = 0.0f;
    Field_1DC = 0;
    Field_000 = 0;
    MaxEngineSpeed = 80.0f;
    Field_1F8 = 0.0f;
    Field_1FC = 0.0f;

    PathfindOwnerObject(Owner)->get_type(OwnerType);

    Avoidance.AttachOwner(Owner);

    Avoidance.PhysSystemId = 0;
    CachedTargetPosition.z = 0.0f;
    CachedTargetPosition.y = 0.0f;
    CachedTargetPosition.x = 0.0f;
    Period1E4 = 1.0f;

    // Three phase offsets, rand() * (1/32768) (DAT_063997d0) * Period, rand() called in
    // this order.
    Timer1E0 = static_cast<float>(std::rand()) * 3.0517578e-05f * Period1E4;
    Period1EC = 0.5f;
    Timer1E8 = static_cast<float>(std::rand()) * 3.0517578e-05f * Period1EC;
    Period210 = 0.1f;
    Timer20C = static_cast<float>(std::rand()) * 3.0517578e-05f * Period210;

    Field_244 = false;
    Field_004 = 0;
    Field_255 = false;
    Field_258 = 0;
    Field_25C = 0;
    BlendTimer = 0.0f;
    bBlendEnabled = false;

    // Only an exact -1.0f (DAT_06399404) zeroes both distances; unordered goes on.
    float const Speed = PathfindOwnerObject(Owner)->get_speed(1.0f, IObject::ThrustEquipEngine);
    MaxEngineSpeed = Speed;
    if (Speed == -1.0f)
    {
        Field_1F8 = 0.0f;
        Field_1FC = 0.0f;
    }
    else
    {
        // 1000.0f is DAT_063a05b4.
        Field_1F8 = PathfindOwnerObject(Owner)->get_distance_travelled(
            0.0f, Speed, 1000.0f, IObject::ThrustEquipEngine);
        Field_1FC = PathfindOwnerObject(Owner)->get_distance_travelled(
            0.0f, 300.0f, 1000.0f, IObject::ThrustEquipEngine);
    }

    BlendStart.x = 0.0f;
    BlendStart.y = 1.0f;
    BlendStart.z = 0.0f;
}

// FUN_062db7c0, thiscall. Sole caller is ConnectBehaviorManagerObjectDb (FUN_062d2830).
void SetPathfindObjectDb(PathfindManager* Self, IObjDB* ObjectDb)
{
    if (ObjectDb == nullptr)
    {
        Self->Avoidance.PhysSystemId = 0;
        return;
    }

    // Retail runs the GetZoneDb()->GetId() pair twice and stores only the second result.
    // The zone database is the system's Universe::ISystem (the same chain
    // CheckLauncherLineOfFireCached walks).
    Universe::ISystem const* ZoneDb = ObjectDb->GetZoneDb();
    (void)ZoneDb->GetId();
    ZoneDb = ObjectDb->GetZoneDb();
    Self->Avoidance.PhysSystemId = ZoneDb->GetId();
}

void PathfindManager::clear_user_zone()
{
    // FUN_062db820
    bUserZoneActive = false;
}

void PathfindManager::submit_user_zone(UserZone const& NewZone)
{
    // FUN_062db800; the 0x58-byte zone is copied whole.
    bUserZoneActive = true;
    std::memcpy(&Zone, &NewZone, sizeof(UserZone));
}

bool PathfindManager::get_user_zone(UserZone& OutZone)
{
    // FUN_062c5ac0; OutZone is untouched when no zone is set.
    if (bUserZoneActive)
    {
        std::memcpy(&OutZone, &Zone, sizeof(UserZone));
        return true;
    }
    return false;
}

void PathfindManager::SetCruiseLock(int Value, unsigned int Mask)
{
    // FUN_062dd3d0
    unsigned char const Byte = static_cast<unsigned char>(Value);
    if ((Mask & 2) != 0)
        Field_254 = Byte;
    unsigned int const Bit0 = Mask & 1;
    if (Bit0 != 0)
        Field_24C = Byte;
    if (Byte != 0)
        return;

    // FUN_062e8ce0 on the owner's ThrustManager.
    if (Bit0 != 0 && (Field_214 & 1) != 0)
    {
        Vector Zero;
        Zero.x = 0.0f;
        Zero.y = 0.0f;
        Zero.z = 0.0f;
        Owner->Sys->Thrust.SetNudgeDirection(Zero);
    }

    // Retail assigns the +0x214 record from a default-constructed temporary; field by field
    // that is the following (the temporary's watcher branches are never taken).
    Field_214 = 0;
    Watcher218.set(nullptr);
    Field_220.x = 0.0f;
    Field_220.y = 0.0f;
    Field_220.z = 0.0f;
    Field_22C.x = 0.0f;
    Field_22C.y = 0.0f;
    Field_22C.z = 0.0f;
    Field_238 = false;
    Field_23C = 1.0f;
    Field_240 = 0;
}

void PathfindManager::Update(float Dt)
{
    // FUN_062dd760. Clock sums stay on the x87 stack for the compare (double here); the
    // stored timers are floats.
    if ((OwnerType & 0x7f0000) == 0)
    {
        return;
    }

    // The behaviour index is read before the probe.
    int const BehaviorIndex = Owner->CurrentBehaviorIndex;
    double const ZoneClock = static_cast<double>(Dt) + Timer1E0;
    float const ZoneClockStored = static_cast<float>(ZoneClock);
    Timer1E0 = ZoneClockStored;
    if (ZoneClock > Period1E4)
    {
        // fmod of the stored (rounded) clock.
        Timer1E0 = static_cast<float>(std::fmod(static_cast<double>(ZoneClockStored),
                                                static_cast<double>(Period1E4)));

        // Zone properties of a 20.0f sphere around the owner.
        Vector const& Position = PathfindOwnerObject(Owner)->get_position();
        Universe::ISystem const* const System = Owner->ObjectDb->GetZoneDb();
        Geometry::Sphere Probe;
        Probe.Center = Position;
        Probe.Radius = 20.0f;
        unsigned const ZoneProps = System->GetSphereProperties(&Probe);

        Field_004 = static_cast<int>(ZoneProps);
        Field_000 = 0;
        if ((ZoneProps & 0x7000) != 0)
        {
            Field_000 = 1;
        }
        if (Avoidance.HasContact == 1)
        {
            Field_000 |= 8;
        }
    }

    // Behaviours 6, 8, 0x13 and none drop the obstacle state (FUN_062db780 inlined).
    if (BehaviorIndex == 6 || BehaviorIndex == 8 || BehaviorIndex == 0x13 || BehaviorIndex == -1)
    {
        Avoidance.ResetContact();
        Reaction.ResetState();
        return;
    }

    // Field_214 as it was before the refresh.
    int const PreviousRecord = Field_214;
    double const RecordClock = static_cast<double>(Dt) + Timer20C;
    float const RecordClockStored = static_cast<float>(RecordClock);
    Timer20C = RecordClockStored;
    if (RecordClock > Period210)
    {
        Timer20C = static_cast<float>(std::fmod(static_cast<double>(RecordClockStored),
                                                static_cast<double>(Period210)));
        RefreshAvoidance();
    }

    // A live nudge record is blended into the ThrustManager (FUN_062e8e60) when Field_244
    // allows; one that has just ended hands it a zero vector (FUN_062e8ce0).
    if ((Field_214 & 1) != 0)
    {
        if (Field_244)
        {
            Owner->Sys->Thrust.BlendNudgeDirection(Field_220, Dt);
        }
    }
    else if ((PreviousRecord & 1) != 0)
    {
        Vector Zero;
        Zero.x = 0.0f;
        Zero.y = 0.0f;
        Zero.z = 0.0f;
        Owner->Sys->Thrust.SetNudgeDirection(Zero);
    }

    if (Flag245 == 0 || Field_254 == 0)
    {
        return;
    }

    UpdateHeadingBlend(Dt);

    // The avoidance runs on its own clock and is handed its period as its Dt; the period
    // becomes 0.1f (DAT_0639d20c) while it is active or has a contact, 0.5f otherwise.
    double const AvoidClock = static_cast<double>(Dt) + Timer1E8;
    float const AvoidClockStored = static_cast<float>(AvoidClock);
    Timer1E8 = AvoidClockStored;
    if (AvoidClock > Period1EC)
    {
        Timer1E8 = static_cast<float>(std::fmod(static_cast<double>(AvoidClockStored),
                                                static_cast<double>(Period1EC)));
        Avoidance.Update(Period1EC);
        if (Avoidance.Active != 0 || Avoidance.HasContact != 0)
        {
            Period1EC = 0.1f;
        }
        else
        {
            Period1EC = 0.5f;
        }
    }

    // Retail calls is_player and drops the result; no null test.
    (void)PathfindOwnerObject(Owner)->is_player();
}

void PathfindManager::RefreshAvoidance()
{
    // FUN_062dd580
    Watcher218.set(nullptr);
    Field_214 = 0;
    Field_22C.z = 0.0f;
    Field_22C.y = 0.0f;
    Field_22C.x = 0.0f;
    Field_220.z = 0.0f;
    Field_220.y = 0.0f;
    Field_220.x = 0.0f;
    Field_23C = 1.0f;
    Field_240 = 0;
    int Kind;
    if (SelectAvoidancePoint(&Field_22C, &Field_23C, &Kind) == 0)
    {
        // A point record.
        Field_238 = Avoidance.HasContact;
        Watcher218.set(nullptr);
        Field_214 = 2;
        Field_240 = Kind;
        // Retail reads get_center_of_mass into a local that is never used.
        (void)PathfindOwnerObject(Owner)->get_center_of_mass();
        return;
    }

    // Only owner types in 0x130000 look for an obstacle to nudge around.
    if ((OwnerType & 0x130000) == 0)
    {
        return;
    }
    IObjRW* Obstacle = nullptr;
    Vector Direction;
    if (SelectObstacleDirection(&Direction, &Obstacle, static_cast<unsigned>(Field_064)) != 0)
    {
        return;
    }

    // Watch the obstacle (its Watchable at +8).
    Watcher218.set(Obstacle != nullptr ? static_cast<Watchable const*>(Obstacle) : nullptr);
    Field_240 = 4;

    bool bCruise = false;
    PathfindOwnerObject(Owner)->is_cruise_active(bCruise);
    unsigned ObstacleType = 0;
    if (Watcher218.target() != nullptr
        && ObjRwFromWatchable(const_cast<Watchable*>(Watcher218.target())) != nullptr)
    {
        ObjRwFromWatchable(const_cast<Watchable*>(Watcher218.target()))->get_type(ObstacleType);
    }

    // Without blending the direction goes straight to the ThrustManager (FUN_062e8ce0).
    SystemManager* const Sys = Owner->Sys;
    bool const bBlend = (!bCruise || (ObstacleType & 0x7f0000) != 0);
    Field_244 = bBlend;
    if (!bBlend)
    {
        Sys->Thrust.SetNudgeDirection(Direction);
    }
    Field_220 = Direction;
    Field_214 = 1;
}

int PathfindManager::SelectAvoidancePoint(Vector* OutPoint, float* OutScale, int* OutKind)
{
    // FUN_062dda10
    if (Flag245 == 0 || Field_254 == 0)
    {
        return 2;
    }

    // The user-zone exit direction first.
    IObjRW* const Ship = PathfindOwnerObject(Owner);
    Vector ExitDirection;
    if (GetUserZoneApproachDirection(&ExitDirection) == 0)
    {
        // Kind 2: 100 units along the exit direction; *OutScale untouched. Retail calls
        // get_orientation, folded with get_transform.
        *OutKind = 2;
        Field_25C = 2;
        Transform const& Xform = Ship->get_transform();
        Vector const Reach{ ExitDirection.x * 100.0f, ExitDirection.y * 100.0f, ExitDirection.z * 100.0f };
        *OutPoint = Vector{ Reach.x + Xform.Translation.x, Reach.y + Xform.Translation.y,
                            Reach.z + Xform.Translation.z };
        return 0;
    }

    if (Reaction.State != 3)
    {
        // Kind 3: 100 units along the reaction heading; x and y are summed unrounded, z is
        // rounded first.
        IObjRW* const Self = PathfindOwnerObject(Owner);
        Transform const& Xform = Self->get_transform();
        double const ReachX = static_cast<double>(Reaction.Heading.x) * 100.0f;
        double const ReachY = static_cast<double>(Reaction.Heading.y) * 100.0f;
        float const ReachZ = static_cast<float>(static_cast<double>(Reaction.Heading.z) * 100.0f);
        OutPoint->x = static_cast<float>(ReachX + Xform.Translation.x);
        OutPoint->y = static_cast<float>(ReachY + Xform.Translation.y);
        OutPoint->z = ReachZ + Xform.Translation.z;

        // FUN_062e5c00
        *OutScale = static_cast<float>(EstimateTurnCircleApproachCost(Self, *OutPoint, 0.0f, 0.0f));

        // In a close encounter, a heading off the owner's forward by more than the 0.93f
        // (DAT_063a05d0) dot zeroes the scale.
        if (Reaction.bCloseEncounter)
        {
            float const ForwardX = -Xform.Rotation.E[0][2];
            double const Dot = (static_cast<double>(-Xform.Rotation.E[2][2]) * Reaction.Heading.z
                                + static_cast<double>(-Xform.Rotation.E[1][2]) * Reaction.Heading.y)
                               + static_cast<double>(ForwardX) * Reaction.Heading.x;
            if (Dot < 0.93f)
            {
                *OutScale = 0.0f;
            }
        }

        // A point within 1 of the world origin still answers 2 (unordered too).
        *OutKind = 3;
        Field_25C = 3;
        double const LengthSq = static_cast<double>(OutPoint->x) * OutPoint->x
                                + static_cast<double>(OutPoint->y) * OutPoint->y
                                + static_cast<double>(OutPoint->z) * OutPoint->z;
        if (!(LengthSq > 1.0))
        {
            return 2;
        }
        return 0;
    }

    // The avoidance point, unless its contact is the excluded Field_064.
    Vector Point;
    unsigned ObjectId;
    if (Avoidance.GetAvoidPoint(&Point, &ObjectId) && ObjectId != static_cast<unsigned>(Field_064))
    {
        *OutPoint = Point;
        *OutScale = Avoidance.GetCloseRateScale();
        *OutKind = 1;
        Field_25C = 1;
        return 0;
    }
    *OutKind = 0;
    Field_25C = 0;
    return 2;
}

int PathfindManager::SelectObstacleDirection(Vector* OutDirection, IObjRW** OutObstacle, unsigned ExcludeId)
{
    // FUN_062de050
    if (Flag245 == 0 || Field_24C == 0)
    {
        return 2;
    }

    // The closest obstacle first (FUN_062e7a30, type mask Field_248).
    SystemManager* const Sys = Owner->Sys;
    OutDirection->z = 0.0f;
    OutDirection->y = 0.0f;
    OutDirection->x = 0.0f;
    Field_255 = false;
    IObjInspect* const Obstacle =
        FindClosestObstacleToAvoid(&Sys->Timers, ExcludeId, static_cast<unsigned>(Field_248));
    if (Obstacle != nullptr)
    {
        // The shape-biased direction around it (FUN_062e55b0).
        Vector Direction;
        *OutDirection = *ComputeShapeBiasedAvoidDirection(&Direction, &Sys->SelfTarget, Obstacle);
        *OutObstacle = static_cast<IObjRW*>(Obstacle);
        Field_255 = true;
        Field_258 = static_cast<int>(Obstacle->get_id());
        return 0;
    }

    // Otherwise the avoidance direction and its contact, looked up by id.
    unsigned ObjectId = 0;
    if (Avoidance.GetAvoidDirection(OutDirection, &ObjectId))
    {
        *OutObstacle = Owner->ObjectDb->FindById(&ObjectId);
        Field_255 = true;
        Field_258 = static_cast<int>(ObjectId);
        return 0;
    }
    return 2;
}

// ============================================================================
// The AI proximity-volume probe family: FUN_062dbb90, FUN_062dba00, FUN_062db830,
// FUN_062ddcd0. The receiver is `*(IBehaviorManager+0x0c)`, the PathfindManager (both
// FUN_062ed780 call sites pass it). UserZone layout, as these bodies read it:
//   +0x008 bool   Active        -- probe disabled when clear
//   +0x00C int    Mode          -- 0 = point+radius sphere, 1 = oriented box,
//                                  2 = live-interface target, >=3 = inert
//   +0x010 int    Invert        -- dword, tested as a whole (not a byte)
//   +0x014 Vector TargetPoint   -- mode 0's target; mode 2 caches into it
//   +0x020 float  Radius        -- mode 0's sphere radius / distance bias
//   +0x024 Matrix BoxRotation, +0x048 Vector BoxCenter, +0x054 Vector BoxHalfExtent --
//                                  a 0x3C-byte oriented box in matrix-first order (not
//                                  common_ini.h's OrientedBox, which puts Center first)
//   +0x060 Zone.TargetKey       -- an object id; its address goes to IObjDB::FindById
//   +0x1C0 IBehaviorManager* Owner
// FUN_06288800 (Vec3Sub) and FUN_0628b030 (Vec3Assign) are written out inline.
// ============================================================================

// FUN_062dbb90, cdecl (Out, M, Src). Rotates a world-space delta into the box's local frame
// via I3DMathEngine::TransposeTransform (+0x38, stdcall), cached per call site in
// DAT_063FC994. The bounce through Scratch is retail's, so Out/Src aliasing is harmless.
// Also called directly by the zone shapes in common_zone.cpp. Retail has no null guard.
void MathTransposeTransformVia3DMathEngine(Vector* Out, Matrix const* M, Vector const* Src)
{
    I3DMathEngine* const Engine = Acquire3DMathEngine();

    Vector Scratch;
    Engine->TransposeTransform(*reinterpret_cast<FVector*>(&Scratch),
                               *reinterpret_cast<FMatrix const*>(M),
                               *reinterpret_cast<FVector const*>(Src));
    *Out = Scratch;
}

// FUN_062dba00, thiscall (Box, Src, Dst), RET 8. Box is the 0x3C-byte block at UserZone+0x24
// (half extents at Box+0x30). Each axis with |Src| - HalfExtent >= 0 (equality included) is
// clamped to the face and latches "outside". A point strictly inside still has its nearest
// face axis (largest overshoot; ties resolve as in the chain below) snapped onto the surface,
// but returns false. So the result means "the input was outside", while Dst always lies on
// the surface; both callers rely on that. Also called by the zone box shape (common_zone.cpp).
bool ClampPointToBoxSurface(unsigned char* Box, Vector const* Src, Vector* Dst)
{
    float const* const HalfExtent = reinterpret_cast<float const*>(Box + 0x30);
    float const* const In = reinterpret_cast<float const*>(Src);
    float* const OutAxis = reinterpret_cast<float*>(Dst);

    float Overshoot[3];
    bool bWasOutside = false;

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Overshoot[Axis] = std::fabs(In[Axis]) - HalfExtent[Axis];
        if (Overshoot[Axis] >= 0.0f)
        {
            OutAxis[Axis] = static_cast<float>(_copysign(HalfExtent[Axis], In[Axis]));
            bWasOutside = true;
        }
        else
        {
            OutAxis[Axis] = In[Axis];
        }
    }

    if (!bWasOutside)
    {
        int NearestAxis = 2;
        if (Overshoot[0] >= Overshoot[1])
        {
            if (Overshoot[0] >= Overshoot[2])
                NearestAxis = 0;
        }
        else if (Overshoot[1] >= Overshoot[2])
        {
            NearestAxis = 1;
        }
        OutAxis[NearestAxis] = static_cast<float>(_copysign(HalfExtent[NearestAxis], In[NearestAxis]));
    }

    return bWasOutside;
}

// Both user-zone probes start by fetching the owner position (GetIfacePositionOrZero,
// FUN_062e60e0) on every mode. Mode 2 resolves Zone.TargetKey through
// IBehaviorManager::ObjectDb->FindById and takes the found object's position.

// FUN_062db830: is ProbePoint inside the volume? Retail is thiscall(This, ProbePoint, float*)
// with a float out-param it never touches; the dead argument is dropped.
//   * A disabled probe admits everything (retail, not a stub).
//   * Mode 0 / 2: sphere at TargetPoint (mode 2 does not write the cache); a missing
//     database or target returns false outright, ignoring Invert.
//   * Mode 1: oriented box; the squared distance to the clamped point is negated when the
//     point was not outside, and the point is inside iff that is < 0 (an interior point
//     still moves one axis, so the test is not degenerate).
//   * Mode >= 3: result stays false.
//   * Invert flips the result.
bool PathfindManager::UserZoneContainsPoint(Vector const* ProbePoint)
{
    if (!bUserZoneActive)
        return true;

    Vector OwnerPos;
    GetIfacePositionOrZero(&OwnerPos, PathfindOwnerObject(Owner));

    bool bInside = false;

    // Set by mode 0 and mode 2; consumed by the shared sphere test.
    Vector SphereTarget;
    bool bHaveSphereTarget = false;

    if (Zone.Mode == 0)
    {
        SphereTarget = Zone.TargetPoint;
        bHaveSphereTarget = true;
    }
    else if (Zone.Mode == 1)
    {
        unsigned char* const Box = reinterpret_cast<unsigned char*>(&Zone.BoxRotation);
        Vector const* const BoxCenter = &Zone.BoxCenter;

        Vector Delta;
        Delta.x = ProbePoint->x - BoxCenter->x;
        Delta.y = ProbePoint->y - BoxCenter->y;
        Delta.z = ProbePoint->z - BoxCenter->z;

        Vector Local;
        MathTransposeTransformVia3DMathEngine(&Local, &Zone.BoxRotation, &Delta);

        Vector Clamped;
        bool const bWasOutside = ClampPointToBoxSurface(Box, &Local, &Clamped);

        // The differences are rounded to float; the sum of squares stays unrounded.
        float const Dx = Local.x - Clamped.x;
        float const Dy = Local.y - Clamped.y;
        float const Dz = Local.z - Clamped.z;
        double SignedSq = ((double)Dy * Dy + (double)Dz * Dz) + (double)Dx * Dx;
        if (!bWasOutside)
            SignedSq = -SignedSq;

        bInside = (SignedSq < 0.0f);
    }
    else if (Zone.Mode == 2)
    {
        IObjDB* const ObjectDb = Owner->ObjectDb;
        if (ObjectDb == nullptr)
            return false;

        IObjRW* const Found = ObjectDb->FindById(&Zone.TargetKey);
        if (Found == nullptr)
            return false;

        SphereTarget = Found->get_position();
        bHaveSphereTarget = true;
    }

    if (bHaveSphereTarget)
    {
        // Strict R^2 > DistSq, both unrounded; equality and unordered count as outside.
        double const Dx = (double)OwnerPos.x - SphereTarget.x;
        double const Dy = (double)OwnerPos.y - SphereTarget.y;
        double const Dz = (double)OwnerPos.z - SphereTarget.z;
        double const Radius = Zone.Radius;
        bInside = (Radius * Radius > (Dz * Dz + Dy * Dy) + Dx * Dx);
    }

    if (Zone.Invert != 0)
        return !bInside;
    return bInside;
}

// FUN_062ddcd0 (thunk 0x062de040), thiscall, RET 4. "How far am I from the volume": returns 0
// with a unit approach direction in *OutDir, or 2 ("not applicable") on every rejection.
//   * Disabled -> 2.
//   * Mode 0: gate = |OwnerPos - TargetPoint| - Radius, Dir = normalize(TargetPoint - OwnerPos).
//   * Mode 1: the owner's own position is clamped to the box (retail runs the chain twice with
//     identical inputs; computed once here); gate = sqrt of the signed squared distance when
//     it is > 0, else 0. Dir = normalize(BoxCenter - OwnerPos).
//   * Mode 2: refreshes (writes) TargetPoint from the live object, then as mode 0; a missing
//     database or target -> 2.
//   * Mode >= 3: the 0.0 gate fails both tests, so always 2; OutDir untouched.
//   * Gate: Invert == 0 needs gate > 0 and writes Dir; Invert != 0 needs gate < 0 and writes
//     -Dir.
// Callers: SelectAvoidancePoint, DodgeIdea, BreakAwayIdea, DodgeMissileIdea and
// TargetManager::UpdateAttackTargetOverride.
int PathfindManager::GetUserZoneApproachDirection(Vector* OutDir)
{
    if (!bUserZoneActive)
        return 2;

    Vector OwnerPos;
    GetIfacePositionOrZero(&OwnerPos, PathfindOwnerObject(Owner));

    int const Mode = Zone.Mode;

    // The gate value lives on the x87 stack (double here) until the final compare.
    double GateValue = 0.0f;
    Vector Dir;
    Dir.x = 0.0f;
    Dir.y = 0.0f;
    Dir.z = 0.0f;

    if (Mode == 2)
    {
        IObjDB* const ObjectDb = Owner->ObjectDb;
        if (ObjectDb == nullptr)
            return 2;

        IObjRW* const Found = ObjectDb->FindById(&Zone.TargetKey);
        if (Found == nullptr)
            return 2;

        Zone.TargetPoint = Found->get_position();
    }

    if (Mode == 0 || Mode == 2)
    {
        Vector const* const Target = &Zone.TargetPoint;

        // The gate is left unrounded.
        float const Bx = OwnerPos.x - Target->x;
        float const By = OwnerPos.y - Target->y;
        float const Bz = OwnerPos.z - Target->z;
        float const Radius = Zone.Radius;
        GateValue = std::sqrt(((double)By * By + (double)Bz * Bz) + (double)Bx * Bx) - Radius;

        // No zero-length guard, as in retail.
        Dir.x = Target->x - OwnerPos.x;
        Dir.y = Target->y - OwnerPos.y;
        Dir.z = Target->z - OwnerPos.z;
        double const InvLen = 1.0 / std::sqrt(((double)Dir.z * Dir.z + (double)Dir.y * Dir.y) + (double)Dir.x * Dir.x);
        Dir.x = (float)(Dir.x * InvLen);
        Dir.y = (float)(Dir.y * InvLen);
        Dir.z = (float)(Dir.z * InvLen);
    }
    else if (Mode == 1)
    {
        unsigned char* const Box = reinterpret_cast<unsigned char*>(&Zone.BoxRotation);
        Vector const* const BoxCenter = &Zone.BoxCenter;

        Vector Delta;
        Delta.x = OwnerPos.x - BoxCenter->x;
        Delta.y = OwnerPos.y - BoxCenter->y;
        Delta.z = OwnerPos.z - BoxCenter->z;

        Vector Local;
        MathTransposeTransformVia3DMathEngine(&Local, &Zone.BoxRotation, &Delta);

        Vector Clamped;
        bool const bWasOutside = ClampPointToBoxSurface(Box, &Local, &Clamped);

        // Negated when the point was inside.
        float const Dx = Local.x - Clamped.x;
        float const Dy = Local.y - Clamped.y;
        float const Dz = Local.z - Clamped.z;
        double SignedSq = ((double)Dy * Dy + (double)Dz * Dz) + (double)Dx * Dx;
        if (!bWasOutside)
            SignedSq = -SignedSq;

        // A NaN gate is rejected below either way. Retail evaluates the chain twice with
        // identical results; only the second run's sqrt, rounded to float, is kept.
        GateValue = (SignedSq > 0.0f) ? (float)std::sqrt(SignedSq) : 0.0f;

        // No zero-length guard, as in retail.
        Dir.x = BoxCenter->x - OwnerPos.x;
        Dir.y = BoxCenter->y - OwnerPos.y;
        Dir.z = BoxCenter->z - OwnerPos.z;
        double const InvLen = 1.0 / std::sqrt(((double)Dir.x * Dir.x + (double)Dir.y * Dir.y) + (double)Dir.z * Dir.z);
        Dir.x = (float)(Dir.x * InvLen);
        Dir.y = (float)(Dir.y * InvLen);
        Dir.z = (float)(Dir.z * InvLen);
    }

    if (Zone.Invert == 0)
    {
        if (!(GateValue > 0.0f))
            return 2;
        *OutDir = Dir;
        return 0;
    }

    if (!(GateValue < 0.0f))
        return 2;
    OutDir->x = -Dir.x;
    OutDir->y = -Dir.y;
    OutDir->z = -Dir.z;
    return 0;
}

