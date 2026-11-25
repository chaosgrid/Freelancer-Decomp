// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_dockbehavior.h"
#include "common_misc.h"   // SinglePlayer() -- UpdateApproachTail()'s case 0xb calls it
#include "common_systemmanager.h"  // SystemManagerGunGroupManager::SetShieldActive -- FUN_062e9a60
#include "common_gotobehavior.h"   // ComputeFormationFollowerEnvelope / ProxWatcherComputeSpeedCap
#include "common_aisteer.h"        // FindObjectDockingAtBase (FUN_062e7400) -- CheckClearanceProgress
#include "common_csys.h"           // Transform, Acquire3DMathEngine -- SetContext's hpmount offset
#include "x86math.h"               // I3DMathEngine::TransposeTransform, Vector, Matrix
#include "common_cobject.h"        // CObject, CShip::request_exit_tradelane -- Disengage

#include <cmath>
#include <cstdint>
#include <cstdlib>   // rand -- EnterState(2)'s spread
#include <cstring>

// Retail pushes float parameters to the MoveIdea/FaceIdea setters as raw dword
// bit patterns (`PUSH 0x3eb2b8c3`), so the call sites below reinterpret rather
// than convert -- the same convention common_idea.cpp uses.
static float BitsToFloat(unsigned int Bits)
{
    float Result;
    std::memcpy(&Result, &Bits, sizeof(Result));
    return Result;
}

// The "-8 header" idiom: a watcher's payload pointer sits 8 bytes into the object, and
// retail reaches the object by subtracting 8, treating null either side as "no object".
static void* HeaderBlockFromWatched(Watchable const* Watched)
{
    if (Watched == nullptr)
        return nullptr;
    return const_cast<void*>(
        static_cast<void const*>(reinterpret_cast<char const*>(Watched) - 8));
}

// DockBehavior; layout notes are in common_dockbehavior.h. Ghidra misreads register reuse in
// this neighbourhood, so the raw listing is the source.

// ----------------------------------------------------------------------------
// FUN_062c2e60, in retail's order:
//   FUN_062d2020(TypeId)               Behavior's field initialisation
//   FUN_062d4e60 on +0x30              MoveIdea        Move
//   FUN_062d6750 on +0xa8              FaceIdea        Face
//   FUN_062d9bf0 on +0x168             SplineMoveIdea  SplineMove
//   FUN_062da500 on +0x2dc             AfterburnerIdea GotoDirective.Idea
//   DirectiveDockOp ctor on +0x2f4     DockOp
//   +0x324 = +0x328 = 0                Watcher324
//   ICRSplineSegment ctor on +0x348    Spline
//   +0x450 = +0x454 = 0                ProxWatcher450.Watcher
//   +0x45c = +0x458 = 0                ProxWatcher450.FarDistance / NearDistance
//   +0x320 = 0, +0x32c = 0, byte +0x39b = 0
// Nothing else is written: GotoDirective.NavPilotPtr (+0x2f0) is first stored by
// RecomputeApproach, the rest by SetContext / EnterState / RefreshApproachState.
DockBehavior::DockBehavior(int InTypeId)
    : Behavior(InTypeId)
    , Move()
    , Face()
    , SplineMove()
    , DockOp()
    , ModeState320(0)
    , Watcher324()
    , StateFlags32c(0)
    , Spline()
    , Flag39b(0)
{
    ProxWatcher450.FarDistance = 0.0f;
    ProxWatcher450.NearDistance = 0.0f;
}

// FUN_062c30d0 (FUN_062c3020 is the scalar-deleting wrapper). Retail is the member teardown
// in reverse declaration order (inlined ~BaseWatcher on +0x450, +0x324, SplineMove's
// +0x208/+0x190, +0xbc, +0x44), then FUN_062d2080; the compiler-generated sequence matches.
DockBehavior::~DockBehavior()
{
}

// Slot 5, LAB_062c35c0. RET 4: the manager's float argument is not read.
int DockBehavior::SetModeFromFlags()
{
    int Mode = (Flags28 & 0x2) ? 1 : 2;
    if (Flag29 != 0)
        Mode = 3;
    Readiness = Mode;
    return Mode;
}

// Slots 13/14 (LAB_062c5650 / LAB_062c5670): forward to the nav pilot when present.
void DockBehavior::set_throttle(float Throttle)
{
    Behavior* const Pilot = static_cast<Behavior*>(GotoDirective.NavPilotPtr);
    if (Pilot != nullptr)
        Pilot->set_throttle(Throttle);
}

void DockBehavior::set_cruise(bool bOn)
{
    Behavior* const Pilot = static_cast<Behavior*>(GotoDirective.NavPilotPtr);
    if (Pilot != nullptr)
        Pilot->set_cruise(bOn);
}

// Slots 16/17 (LAB_062c5690 / LAB_062c56b0): when a nav pilot is set, Behavior's own bodies
// (FUN_062d2110 / FUN_062d21b0) on this object, not the pilot.
void DockBehavior::set_afterburner(bool bOn)
{
    if (GotoDirective.NavPilotPtr != nullptr)
        Behavior::set_afterburner(bOn);
}

void DockBehavior::slide_strafe_burst(StrafeDir Dir)
{
    if (GotoDirective.NavPilotPtr != nullptr)
        Behavior::slide_strafe_burst(Dir);
}

// Slot 11, LAB_062c3000.
bool DockBehavior::IsClearanceState() const
{
    int Mode = ModeState320;
    return Mode != 6 && Mode != 7 && Mode != 8 && Mode != 10;
}

// Slots 24/26, LAB_062c2fe0.
void* DockBehavior::GetWatchedRawBlock() const
{
    char const* Watched = reinterpret_cast<char const*>(Watcher324.target());
    if (Watched == nullptr)
        return nullptr;
    return const_cast<char*>(Watched) - 8;
}

// Slot 7, LAB_062c3b70.
int DockBehavior::Update()
{
    if (Flags28 & 0x1)
    {
        UpdateResult = 2;
        return 2;
    }

    // No owner, no watched object, or a watched pointer of exactly 8 -> result 3.
    void const* Watched = Watcher324.target();
    if (OwnerContext == nullptr || Watched == nullptr ||
        reinterpret_cast<char const*>(Watched) - 8 == nullptr)
    {
        UpdateResult = 3;
        return 3;
    }

    IObjRW* const Owner = OwnerContext;

    // Player owner with StateFlags32c bit 7: a dock target whose status is not strictly
    // positive (unordered included) fires the owner's IObjAffect slot_6c(1, 0) once.
    if (Owner->is_player() && static_cast<signed char>(StateFlags32c & 0xff) < 0)
    {
        IObjRW* const Target = ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()));
        float Health = 1.0f;
        Target->get_status(Health);
        if (Health > 0.0f)
        {
            Flag39b = 0;
        }
        else if (Flag39b == 0)
        {
            Flag39b = 1;
            static_cast<IObjAffect*>(Owner)->slot_6c(1, 0);
        }
    }

    // Target speed squared, on the x87 stack, against 0.3f (DAT_0639f484).
    {
        IObjRW* const Target = ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()));
        Vector const Velocity = Target->get_velocity();
        double const SpeedSq = (static_cast<double>(Velocity.x) * Velocity.x +
                                static_cast<double>(Velocity.y) * Velocity.y) +
                               static_cast<double>(Velocity.z) * Velocity.z;
        if (SpeedSq > static_cast<double>(0.3f))
        {
            // The target is moving: release both Ideas.
            Face.Commit();
            Move.SetField44(0);     // FUN_062d88b0
            Move.Commit();
            UpdateResult = 1;
            return 1;
        }
    }

    // The target's position is re-read for each call.
    unsigned char* const ProxWatcher = reinterpret_cast<unsigned char*>(&ProxWatcher450);
    ProxWatcherRecomputeZoneFlags(
        ProxWatcher, SysMgr,
        ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()))->get_position());
    ProxWatcherNotifyZoneChange(
        ProxWatcher, SysMgr, BehaviorMgr,
        ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()))->get_position());

    return UpdateApproachTail();   // FUN_062c4900
}

// FUN_062c3590. A raw 11-dword copy (REP MOVSD), vptr included; DirectiveDockOp's operator=
// is deleted.
bool DockBehavior::CopyDockOp(pub::AI::DirectiveDockOp* Out) const
{
    std::memcpy(static_cast<void*>(Out), static_cast<void const*>(&DockOp), 0xb * 4);
    return ((Flags28 >> 1) & 1) != 0;
}

// IObjAffect::RequestCB override on the secondary vtable, LAB_062c53f0. Retail's `this` is the
// secondary base (outer - 0x2c).
bool DockBehavior::OnResult(int Param1, int Param2, int Param3)
{
    if (Param1 == 0)
    {
        if (Param2 == 3)
            return true;

        DockPortIndex = Param3;   // outer+0x43c

        void const* Watched = Watcher324.target();
        bool const bInvalidBlock =
            (Watched == nullptr) || (reinterpret_cast<char const*>(Watched) - 8 == nullptr);

        if (bInvalidBlock || Param3 == -1)
        {
            EnterState(0xd);
            Flag39a = 0;
            return false;
        }

        RefreshApproachState();   // FUN_062c50e0

        if (StateFlags32c & (OBJ_JUMP_GATE | OBJ_JUMP_HOLE))
            EnterState(6);
        else
            EnterState(2);
        return false;
    }

    if (Param1 == 2)
    {
        if (Param2 == 4)
        {
            if (Param3 == -1)
            {
                EnterState(0xd);
                Flag39a = 0;
                return false;
            }
            DockPointRadius = 16.0f;
            EnterState(6);
            return false;
        }
        if (Param2 == 1)
        {
            EnterState(0xd);
            Flag39a = 0;
            return false;
        }
        return false;
    }

    return false;
}

// Slot 1, LAB_062c31c0.
bool DockBehavior::SetContext(void* Param1, void* Param2, void* Param3)
{
    bool const bOk = BindContext(Param1, Param2, Param3);          // FUN_062d2090
    if (!bOk)
        return false;

    AbortFlag398 = 0;
    Flag39b = 0;

    // Each Idea's slot 0 gets (owner, SysMgr, Param2); retail passes the still-live
    // Param2, not a reload of BehaviorMgr (same value).
    Move.SetContext(OwnerContext, SysMgr, Param2);
    Move.SetMode(0);                                          // FUN_062d5980
    GotoDirective.Idea.SetContext(OwnerContext, SysMgr, Param2);
    Face.SetContext(OwnerContext, SysMgr, Param2);

    SplineMove.SetContext(OwnerContext, SysMgr, Param2);

    // Zeroes +0x444..+0x44c only; TerminalType (+0x440) and RequestedDockPortIndex (+0x330)
    // are not touched here.
    DestPointOffset.x = 0.0f;
    DestPointOffset.y = 0.0f;
    DestPointOffset.z = 0.0f;
    Flags28 = static_cast<unsigned char>(Flags28 & ~0x2);

    // get_hardpoint (IObjInspect +0xc0) returns non-zero when there is no such hardpoint;
    // then the +0x430 offset is zeroed.
    Vector HpPos;   // left uninitialised by retail
    if (ObjInspectOf(OwnerContext)->get_hardpoint("hpmount", &HpPos, nullptr) != 0)
    {
        HpMountOffset.x = 0.0f;
        HpMountOffset.y = 0.0f;
        HpMountOffset.z = 0.0f;
        return bOk;
    }

    // Inlined physical-radius cache refresh (SystemManager+0x14fc block).
    SystemManager* const Sys = SysMgr;
    if (Sys->SelfTarget.bPosValid == 0)
    {
        Sys->SelfTarget.bPosValid = 1;
        Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                              Sys->SelfTarget.CachedPos);
    }

    // Hardpoint minus cached centre, stored to +0x430 and kept in a local.
    Vector Delta;
    Delta.x = HpPos.x - Sys->SelfTarget.CachedPos.x;
    Delta.y = HpPos.y - Sys->SelfTarget.CachedPos.y;
    Delta.z = HpPos.z - Sys->SelfTarget.CachedPos.z;
    HpMountOffset = Delta;

    // Into the own ship's local frame: I3DMathEngine (DAT_063fc528, via FUN_06391cf0)
    // TransposeTransform with +0x430 as source and the local as destination, then copied back.
    Transform const& OwnerXform = Sys->SelfTarget.Iface->get_transform();
    I3DMathEngine* const Math = Acquire3DMathEngine();
    Vector Source;
    Source = HpMountOffset;
    Math->TransposeTransform(Delta, OwnerXform.Rotation, Source);
    HpMountOffset = Delta;
    return bOk;
}

// Slot 3, LAB_062c3720.
void DockBehavior::RecomputeApproach()
{
    // NavPilotPtr is zeroed raw, without its release sequence.
    Flag399 = 0;
    Flag39a = 0;
    Flag39b = 0;
    GotoDirective.NavPilotPtr = nullptr;

    Behavior::OnEngage();   // FUN_062d20d0

    void const* Watched = Watcher324.target();
    bool const bInvalidBlock =
        (Watched == nullptr) || (reinterpret_cast<char const*>(Watched) - 8 == nullptr);
    if (bInvalidBlock)
    {
        EnterState(0xd);
        return;
    }

    DockPortIndex = -1;
    CombatFlag344 = 0;

    // FUN_062e5af0 (engine speed) rounded to float, FUN_062e5ab0 (coast distance), times
    // 1.4f (DAT_0639f510).
    {
        float const Speed = Behavior_GetSpeed(ObjInspectOf(OwnerContext), 1.0f,
                                                         IObject::ThrustEquipEngine);
        BrakingDistance = static_cast<float>(
            static_cast<double>(Behavior_GetCoastDistance(OwnerContext, Speed)) * 1.4f);
    }

    SetAvoidanceAllowed(1, 3);   // FUN_062d21d0

    // Bit 7 of the low byte set.
    if (static_cast<signed char>(StateFlags32c) < 0)
    {
        // IObjInspect +0x138 on the watched object (target - 8, or null).
        IObjInspect const* const WatchedObj = ObjInspectOf(Watched != nullptr
            ? static_cast<void const*>(reinterpret_cast<char const*>(Watched) - 8)
            : nullptr);
        ObjInspectOf(OwnerContext)->get_lane_start(
            WatchedObj, *static_cast<IObjInspect const**>(static_cast<void*>(&LaneNextRing)),
            DestPoint3cc, Facing38c);
        OrientMatrix3d8 = LookMatrixYup(Facing38c);
        // FUN_062d4e50 with the re-read watched object's id; on a null target retail calls
        // through a null pointer, reproduced literally.
        Watchable const* const Watched2 = Watcher324.target();
        void const* const Obj2 = Watched2 != nullptr
            ? static_cast<void const*>(reinterpret_cast<char const*>(Watched2) - 8)
            : nullptr;
        Move.PublishTargetId(ObjInspectOf(Obj2)->get_id());
    }
    else if ((StateFlags32c & OBJ_JUMP_GATE) != 0)
    {
        // Retail resets the -1 written above to 0 on this branch only.
        DockPortIndex = 0;

        RefreshApproachState();                                      // FUN_062c50e0
    }

    // DAT_063fc518 is built at startup by the CRT initializer LAB_062c2e40 as
    // DAT_0639f48c (10000.0f) squared; retail compares squared distances. Strictly closer
    // continues the approach (state 1); farther or unordered aborts (0xd).
    float const kApproachRangeSq = 10000.0f * 10000.0f;   // DAT_063fc518 == 1e8f exactly

    int NextState = 1;
    IObjInspect const* const ThreatObject = ObjInspectOf(OwnerContext);
    if (ThreatObject->is_player())
    {
        // Owner position first, then the object's (target - 8, or null, no test).
        Watchable const* const Watched3 = Watcher324.target();
        void const* const Obj3 = Watched3 != nullptr
            ? static_cast<void const*>(reinterpret_cast<char const*>(Watched3) - 8)
            : nullptr;
        Vector const& ThreatPos = ThreatObject->get_position();
        Vector const& WatchedPos = ObjInspectOf(Obj3)->get_position();

        // Kept on the x87 stack, hence double.
        double const Dx = static_cast<double>(WatchedPos.x) - ThreatPos.x;
        double const Dy = static_cast<double>(WatchedPos.y) - ThreatPos.y;
        double const Dz = static_cast<double>(WatchedPos.z) - ThreatPos.z;
        double const DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
        if (!(DistSq < kApproachRangeSq))
        {
            ObjAffectOf(OwnerContext)->slot_6c(0, 0);
            NextState = 0xd;
        }
    }
    EnterState(NextState);

    // FUN_062d5a80 (Offset4c = 0), then SetAngle38(pi).
    Vector Zero;
    Zero.x = 0.0f;
    Zero.y = 0.0f;
    Zero.z = 0.0f;
    Move.Offset4c = Zero;
    Move.SetAngle38(3.14159274f);
    MaxApproachSpeed46c = 1000.0f;   // DAT_0639f47c
}

// Slot 4, LAB_062c3910.
void DockBehavior::Disengage()
{
    // With ModeState320 == 0 retail takes a short, separate tail (LAB_062c3afc).
    if (ModeState320 == 0)
    {
        if (ApproachClearedFlag334 == 0)
            return;

        TerminalType = 0;
        RequestedDockPortIndex = -1;
        ApproachClearedFlag334 = 0;
        CancelOwnerRequest(0);
        return;
    }

    BehaviorMgr->lock_maneuvers(false);

    if (GotoDirective.NavPilotPtr != nullptr)
    {
        static_cast<Behavior*>(GotoDirective.NavPilotPtr)->OnDisengage();
        Behavior_ReleaseDirectiveTarget(GotoDirective.NavPilotPtr);  // FUN_062d21c0
        GotoDirective.NavPilotPtr = nullptr;
    }

    if (static_cast<signed char>(StateFlags32c) < 0)
    {
        Move.SetField44(2);                                    // FUN_062d88b0

        // request_exit_tradelane is called on a possibly-null CShip, as retail.
        if (AbortFlag398 != 0 && Flag399 == 0)
        {
            CObject const* const Obj = ObjInspectOf(OwnerContext)->cobject(); // +0x150
            CShip* Ship = nullptr;
            if (Obj != nullptr && (Obj->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
                Ship = const_cast<CShip*>(static_cast<CShip const*>(Obj));
            Ship->request_exit_tradelane();
        }

        if (Flag39a != 0)
            CancelOwnerRequest(2);
    }
    else
    {
        SystemManager* const Sys = SysMgr;
        int const Level = (Sys->SelfTarget.Iface->is_player() && ModeState320 == 1) ? 2 : 0;
        Move.SetField44(Level);                                // FUN_062d88b0

        if ((Flags28 & 0x1) == 0)
        {
            TerminalType = 0;
            RequestedDockPortIndex = -1;
            ApproachClearedFlag334 = 0;
            CancelOwnerRequest(0);
        }
    }

    // LAB_062c3a9e, the shared tail.
    Flag39a = 0;
    AbortFlag398 = 0;
    GotoDirective.Idea.Commit();
    Face.Commit();
    Move.Commit();
    SetAvoidanceAllowed(1, 3);                                             // FUN_062d21d0
    SysMgr->GunGroups.SetShieldActive(1);         // FUN_062e9a60
    Behavior::OnDisengage();                                               // FUN_062d20f0
    ModeState320 = 0;
}

// The three IObjAffect request_cancel (+0x60) sites in Disengage. Id is the watched object's
// get_id, or 0 when the target or its -8 is null; the re-read is not null-tested, as retail.
void DockBehavior::CancelOwnerRequest(int RequestType)
{
    unsigned int Id = 0;
    Watchable const* const Watched = Watcher324.target();
    if (Watched != nullptr && reinterpret_cast<char const*>(Watched) - 8 != nullptr)
    {
        Watchable const* const Again = Watcher324.target();
        void const* const Obj = Again != nullptr
            ? static_cast<void const*>(reinterpret_cast<char const*>(Again) - 8)
            : nullptr;
        Id = ObjInspectOf(Obj)->get_id();
    }
    ObjAffectOf(OwnerContext)->request_cancel(RequestType, &Id, 0);
}

// Slot 6 (evaluate), LAB_062c35f0. RET 4: the manager's context dword is not read.
int DockBehavior::CheckClearanceProgress()
{
    if (AbortFlag398 != 0)
        return 1;

    IObjRW* Candidate = nullptr;
    ObjInspectOf(OwnerContext)->get_target(Candidate);
    if (Candidate == nullptr)
        return 2;

    unsigned int Type = 0;   // retail leaves this unset; get_type always writes it
    Candidate->get_type(Type);
    if ((Type & 0x600de0) != 0)
    {
        if ((Type & (OBJ_JUMP_GATE | OBJ_TRADELANE_RING | OBJ_JUMP_HOLE)) == 0)
        {
            if ((Type & (OBJ_DOCKING_RING | OBJ_STATION)) == 0)
                return 2;
            if (!Candidate->has_dock_hardpoints())
                return 2;
        }
    }
    else
    {
        if ((Type & (OBJ_MOON | OBJ_PLANET)) == 0)
            return 2;
        unsigned int Base = 0;
        Candidate->get_base(Base);
        if (Base == 0)
            return 2;
        if (SysMgr->Timers.FindObjectDockingAtBase(Candidate) == nullptr)   // FUN_062e7400
            return 2;
    }

    // Go on only when strictly > 0 (unordered fails).
    float Health = 1.0f;
    Candidate->get_status(Health);
    if (!(Health > 0.0f))
        return 2;

    // Speed squared on the x87 stack, strictly below 0.3f (DAT_0639f484).
    Vector const Velocity = Candidate->get_velocity();
    double const SpeedSq = static_cast<double>(Velocity.x) * Velocity.x +
                           static_cast<double>(Velocity.y) * Velocity.y +
                           static_cast<double>(Velocity.z) * Velocity.z;
    return (SpeedSq < 0.3f) ? 1 : 2;
}

// Slot 12, LAB_062c5540. NavPilotPtr (+0x2f0) is the manager's Goto behavior (EnterState
// case 1 loads it), so its +0x30 / +0x28 calls are set_direction / OnApplyDirective.
void DockBehavior::SubmitGotoDirective(Vector const& ReferencePoint)
{
    Behavior* const Pilot = static_cast<Behavior*>(GotoDirective.NavPilotPtr);
    if (Pilot == nullptr)
        return;

    // TEST AH,0x44 / JP equality idiom: anything but exactly zero (unordered included)
    // forwards the direction unchanged.
    double const MagSq = static_cast<double>(ReferencePoint.x) * ReferencePoint.x +
                         static_cast<double>(ReferencePoint.y) * ReferencePoint.y +
                         static_cast<double>(ReferencePoint.z) * ReferencePoint.z;
    if (!(MagSq == 0.0))
    {
        Pilot->set_direction(ReferencePoint);
        return;
    }

    // A zero direction: send the pilot a goto directive instead.
    pub::AI::DirectiveGotoOp Op;
    Op.ArrivalRange = 1000.0f;                       // .data:DAT_063ebdb4
    Op.bFlyThrough = 1;
    void* const ProxObj = HeaderBlockFromWatched(ProxWatcher450.Watcher.target());
    if (ProxObj != nullptr)
        Op.TetherObjectId = ObjInspectOf(ProxObj)->get_id();
    else
        Op.TetherObjectId = 0;
    Op.TetherNearRange = ProxWatcher450.NearDistance;
    Op.TetherFarRange = ProxWatcher450.FarDistance;

    if ((StateFlags32c & (OBJ_JUMP_GATE | OBJ_TRADELANE_RING)) != 0)
    {
        Op.GotoKind = 1;
        Op.Destination = DestPoint3cc;
    }
    else
    {
        // The watched object (target - 8) is not null-tested.
        Op.GotoKind = 0;
        Watchable const* const Watched = Watcher324.target();
        void const* const Obj = Watched != nullptr
            ? static_cast<void const*>(reinterpret_cast<char const*>(Watched) - 8)
            : nullptr;
        Op.TargetId = static_cast<int32_t>(ObjInspectOf(Obj)->get_id());
    }

    Pilot->OnApplyDirective(&Op);
}

// Slot 22, LAB_062c54b0. The argument is a two-dword message {TypeCode, Payload}.
void DockBehavior::OnWatchedObjectDestroyed(void* Message)
{
    int const TypeCode = *static_cast<int const*>(Message);
    unsigned char const* const Payload =
        *reinterpret_cast<unsigned char const* const*>(static_cast<char const*>(Message) + 4);

    if (TypeCode == 4)
    {
        // No null test on the payload or on the watched object.
        if (Payload[0] != 0)
            return;
        if (ObjInspectOf(OwnerContext)->get_id() !=
            *reinterpret_cast<unsigned int const*>(Payload + 0xc))
            return;
        Watchable const* const Watched = Watcher324.target();
        void const* const Obj = Watched != nullptr
            ? static_cast<void const*>(reinterpret_cast<char const*>(Watched) - 8)
            : nullptr;
        if (ObjInspectOf(Obj)->get_id() != *reinterpret_cast<unsigned int const*>(Payload + 0x10))
            return;
        Flag399 = 1;
        EnterState(0xc);
        return;
    }

    if (TypeCode == 3)
    {
        // A null payload is ignored; no Flag399 store on this path.
        if (Payload == nullptr)
            return;
        SystemManager* const Sys = SysMgr;
        if (*reinterpret_cast<unsigned int const*>(Payload + 0x8) !=
            Sys->SelfTarget.Iface->get_id())
            return;
        EnterState(0xc);
    }
}

// FUN_062c3d10, the dock state machine's state-entry function. Non-virtual; called with an
// unadjusted `this` from RecomputeApproach(), OnWatchedObjectDestroyed() and OnResult().
// Radii set per state: 64.0f / 10.0f / 15.0f / 25.0f and DockPointRadius * DAT_0639f518;
// approach speed caps 10.0f and 18.0f (.data:DAT_063ebdb8), otherwise 1000.0f.
void DockBehavior::EnterState(int NewState)
{
    if (NewState != 1 && ModeState320 == 1)
    {
        // No null test on the pilot in retail.
        static_cast<Behavior*>(GotoDirective.NavPilotPtr)->OnDisengage();
        Behavior_ReleaseDirectiveTarget(GotoDirective.NavPilotPtr);   // FUN_062d21c0
        GotoDirective.NavPilotPtr = nullptr;
        SysMgr->Thrust.engage_cruise(false);
        GotoDirective.Idea.Commit();   // AfterburnerIdea at +0x2dc
    }

    MaxApproachSpeed46c = 1000.0f;   // .rdata:DAT_0639f47c, unconditional
    ModeState320 = NewState;

    switch (NewState)
    {
    case 1:
    {
        // Manager+0x17c is BehaviorDispatchTable[2], the Goto behavior; no null test.
        GotoDirective.NavPilotPtr = BehaviorMgr->BehaviorDispatchTable[2];

        pub::AI::DirectiveGotoOp Op;
        Op.ArrivalRange = 1000.0f;   // .data:DAT_063ebdb4
        Op.bFlyThrough = 1;

        void* ProxObj = HeaderBlockFromWatched(ProxWatcher450.Watcher.target());
        if (ProxObj != nullptr)
        {
            Op.TetherObjectId = ((IObjRW*)ProxObj)->get_id();
        }
        else
        {
            Op.TetherObjectId = 0;
        }

        // FUN_062c2ad0(this+0x450, 0) is the far threshold unscaled.
        Op.TetherNearRange = ProxWatcher450.NearDistance;
        Op.TetherFarRange = QueryHandleRange(&ProxWatcher450, 0);

        if ((StateFlags32c & (OBJ_JUMP_GATE | OBJ_TRADELANE_RING)) != 0)
        {
            Op.GotoKind = 1;
            Op.Destination = DestPoint3cc;
        }
        else
        {
            Op.GotoKind = 0;
            // The watched object is not null-tested, as retail.
            Op.TargetId = static_cast<int32_t>(
                ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()))->get_id());
        }

        // All on [this+0x2f0] with no null test.
        GotoBehavior* const Pilot = static_cast<GotoBehavior*>(
            static_cast<Behavior*>(GotoDirective.NavPilotPtr));
        Pilot->OnApplyDirective(&Op);
        Pilot->OnEngage();
        Pilot->Flag36c = 0;
        return;
    }
    case 2:
    {
        SetAvoidanceAllowed(1, 3);   // FUN_062d21d0

        if ((StateFlags32c & (OBJ_JUMP_GATE | OBJ_TRADELANE_RING)) != 0)
        {
            // Each FSTP to a float is a float below; everything kept on the x87 stack is double.
            NavHelperRefreshLeadPoint();
            SystemManager* const Sys = SysMgr;

            Vector Delta;
            Delta.x = static_cast<float>(static_cast<double>(Sys->SelfTarget.CachedPos.x) - DestPoint3cc.x);
            Delta.y = static_cast<float>(static_cast<double>(Sys->SelfTarget.CachedPos.y) - DestPoint3cc.y);
            Delta.z = static_cast<float>(static_cast<double>(Sys->SelfTarget.CachedPos.z) - DestPoint3cc.z);

            // The watched object is not null-tested; FUN_062e6010 gives its size.
            IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()));
            Matrix const& Rot0 = Watched->get_transform().Rotation;
            float const Col0x = Rot0.E[0][0];
            float const Col0y = Rot0.E[1][0];
            float const Col0z = Rot0.E[2][0];
            double const Size = Behavior_GetIfaceMaxScalarAttribute(Watched);

            // Offset = column0 * Size * 2 * (1/3) (DAT_0639f520). x stays on the x87 stack; y and
            // z are rounded after the multiply by Size, and z once more after the doubling.
            double const OneThird = 0.3333333432674408;
            Vector Offset;
            Offset.x = static_cast<float>((static_cast<double>(Col0x) * Size * 2.0) * OneThird);
            float const ScaledY = static_cast<float>(static_cast<double>(Col0y) * Size);
            Offset.y = static_cast<float>((static_cast<double>(ScaledY) * 2.0) * OneThird);
            float const ScaledZ = static_cast<float>(static_cast<double>(Col0z) * Size);
            float const DoubledZ = static_cast<float>(static_cast<double>(ScaledZ) * 2.0);
            Offset.z = static_cast<float>(static_cast<double>(DoubledZ) * OneThird);

            // Kept on greater or unordered, otherwise negated.
            double const Facing = static_cast<double>(Offset.y) * Delta.y
                                + static_cast<double>(Offset.x) * Delta.x
                                + static_cast<double>(Offset.z) * Delta.z;
            if (Facing <= 0.0)
            {
                Offset.x = -Offset.x;
                Offset.y = -Offset.y;
                Offset.z = -Offset.z;
            }

            // Spread = lerp(-128, 128, rand() / 32768) (DAT_0639f490, DAT_063997d0); the rand()
            // product is stored as a double, the result as a float.
            float const SpreadHalf = 128.0f;
            float const SpreadLow = -SpreadHalf;
            double const RandUnit = static_cast<double>(rand()) * 3.0517578125e-05;
            float const Spread = static_cast<float>(
                (static_cast<double>(SpreadHalf) - SpreadLow) * RandUnit + SpreadLow);

            // Column 1 of a second get_transform() call, times Spread, plus Offset. x goes
            // through a float temporary first; z stays on the x87 stack.
            Matrix const& Rot1 = Watched->get_transform().Rotation;
            float const Col1x = Rot1.E[0][1];
            float const Col1y = Rot1.E[1][1];
            float const Col1z = Rot1.E[2][1];
            float const SpreadX = static_cast<float>(static_cast<double>(Col1x) * Spread);
            float const JitterX = static_cast<float>(static_cast<double>(SpreadX) + Offset.x);
            float const JitterY = static_cast<float>(static_cast<double>(Col1y) * Spread + Offset.y);
            double const JitterZ = static_cast<double>(Col1z) * Spread + Offset.z;

            // InterceptPoint39c = DestPoint3cc + (64 - 200) * Facing38c + jitter (DAT_0639f480,
            // DAT_0639f488). x stays on the x87 stack; y and z are rounded after the multiply,
            // z once more after adding Dest.z.
            double const Back = 64.0 - 200.0;
            double const BackX = Back * Facing38c.x;
            float const BackY = static_cast<float>(Back * Facing38c.y);
            float const BackZ = static_cast<float>(Back * Facing38c.z);
            double const BaseX = BackX + DestPoint3cc.x;
            double const BaseY = static_cast<double>(BackY) + DestPoint3cc.y;
            float const BaseZ = static_cast<float>(static_cast<double>(BackZ) + DestPoint3cc.z);
            InterceptPoint39c.x = static_cast<float>(BaseX + JitterX);
            InterceptPoint39c.y = static_cast<float>(BaseY + JitterY);
            InterceptPoint39c.z = static_cast<float>(static_cast<double>(BaseZ) + JitterZ);

            // StandOffRadius from the watched object's size: times 0.5f (DAT_0639c13c) for
            // 0x80, times 1/3 for 0x40 only, otherwise unchanged.
            unsigned int const TypeBits = StateFlags32c;
            if ((TypeBits & OBJ_TRADELANE_RING) != 0)
                StandOffRadius = static_cast<float>(
                    static_cast<double>(Behavior_GetIfaceMaxScalarAttribute(Watched)) * 0.5);
            else if ((TypeBits & OBJ_JUMP_GATE) != 0)
                StandOffRadius = static_cast<float>(
                    static_cast<double>(Behavior_GetIfaceMaxScalarAttribute(Watched)) * OneThird);
        }

        // LAB_062c4151, reached from both branches.
        Move.SetMode(0);                           // FUN_062d5980
        Move.SetRange3c(64.0f);                    // bind_to_dc fold, .rdata:DAT_0639f480
        Move.SetPosition(InterceptPoint39c);       // FUN_062d5840
        Face.SetPosition(InterceptPoint39c);       // FUN_062d7560
        Move.SetField44(0);                        // FUN_062d88b0
        return;
    }
    case 4:
        Move.SetField44(0);                        // FUN_062d88b0
        Move.Commit();
        Face.Commit();
        return;
    case 6:
        if ((StateFlags32c & (OBJ_JUMP_GATE | OBJ_TRADELANE_RING)) != 0)
        {
            Face.SetAngleWeight8c(BitsToFloat(0x3ec90fdbu)); // FUN_062d93c0, pi/8
            Face.SetPosition(DestPoint3cc);           // FUN_062d7560
            Move.SetPosition(DestPoint3cc);           // FUN_062d5840
            Move.SetRange3c(10.0f);                   // bind_to_dc fold
            Move.SetField44(2);                       // FUN_062d88b0
            MaxApproachSpeed46c = 10.0f;
            return;
        }
        if ((StateFlags32c & OBJ_JUMP_HOLE) != 0)
            break;

        Face.SetBasis(Orient3a8);                     // FUN_062d75c0
        Move.SetPosition(InterceptPoint39c);          // FUN_062d5840
        Move.SetRange3c(10.0f);                       // bind_to_dc fold, .data:DAT_063ebdbc
        Move.SetMode(1);                              // FUN_062d5980
        MaxApproachSpeed46c = 18.0f;                   // .data:DAT_063ebdb8
        // FUN_062d5a80 copies three dwords into MoveIdea::Offset4c.
        Move.Offset4c = HpMountOffset;
        return;
    case 7:
        SetAvoidanceAllowed(0, 3);
        Move.SetField44(2);                           // FUN_062d88b0
        Move.Offset4c = HpMountOffset; // FUN_062d5a80
        if (CombatFlag344 != 0)
        {
            Face.SetPosition(SavedPosition3fc);       // FUN_062d7560
            Move.SetMode(0);                          // FUN_062d5980
            Move.SetPosition(SavedPosition3fc);       // FUN_062d5840
            Move.SetRange3c(25.0f);                   // bind_to_dc fold, .data:DAT_063ebdc4
            return;
        }
        Face.SetBasis(OrientMatrix3d8);               // FUN_062d75c0
        Move.SetPosition(DestPoint3cc);               // FUN_062d5840
        Move.SetRange3c(15.0f);                       // bind_to_dc fold, .data:DAT_063ebdc0
        Move.SetMode(1);                              // FUN_062d5980
        MaxApproachSpeed46c = 18.0f;                   // .data:DAT_063ebdb8
        return;
    case 8:
        BuildFinalApproachSpline();                         // FUN_062c56d0
        SetAvoidanceAllowed(0, 3);
        SplineMove.SetStopStyle(2);                          // FUN_062da400
        return;
    case 9:
    {
        SetAvoidanceAllowed(0, 3);
        Move.SetPosition(SavedPosition3fc);           // FUN_062d5840
        Move.Offset4c = HpMountOffset; // FUN_062d5a80
        // Float radius times double DAT_0639f518 (0.6), rounded once.
        Move.SetRange3c(static_cast<float>(
            static_cast<double>(DockPointRadius) * kEnterState9RadiusScale));

        if ((StateFlags32c & (OBJ_JUMP_GATE | OBJ_TRADELANE_RING)) != 0)
        {
            Face.SetDirection(Facing38c);             // FUN_062d7590
            Move.SetField44(2);                       // FUN_062d88b0
            return;
        }
        if ((StateFlags32c & OBJ_JUMP_HOLE) != 0)
        {
            Move.SetMode(0);                          // FUN_062d5980
            // FUN_062d5a70 writes MoveIdea+0x59, not the +0x58 SetEnabled() writes.
            Move.SetField59(0);
            Move.SetAngle38(BitsToFloat(0x3eb2b8c3u)); // set_origin_h fold
            Move.SetField44(2);                       // FUN_062d88b0
            Face.SetPosition(SavedPosition3fc);       // FUN_062d7560
            return;
        }
        // No null test on BehaviorMgr.
        BehaviorMgr->lock_maneuvers(true);
        Move.SetField44(0);                           // FUN_062d88b0
        if (CombatFlag344 != 0)
        {
            Face.SetPosition(SavedPosition3fc);       // FUN_062d7560
            Move.SetMode(0);                          // FUN_062d5980
            return;
        }
        // +0x40c is a 3x3 Matrix: FUN_062d75c0 copies nine dwords.
        Face.SetBasis(Orient40c);
        Move.SetMode(1);                              // FUN_062d5980
        MaxApproachSpeed46c = 18.0f;                   // .data:DAT_063ebdb8
        if ((StateFlags32c & OBJ_DOCKING_RING) != 0)
            break;
        SysMgr->GunGroups.SetShieldActive(0);   // FUN_062e9a60
        return;
    }
    case 0xb:
        StateTimer340 = 0.0f;
        return;
    case 0xc:
        Flags28 = static_cast<unsigned char>(Flags28 | 1);
        Disengage();
        Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
        BehaviorMgr->SetDirectiveResult(7, 0);   // FUN_062d29a0
        return;
    case 0xd:
        // Same sequence, result 2.
        Flags28 = static_cast<unsigned char>(Flags28 | 1);
        Disengage();
        Flags28 = static_cast<unsigned char>(Flags28 & 0xfd);
        BehaviorMgr->SetDirectiveResult(7, 2);   // FUN_062d29a0
        return;
    default:
        // States 3, 5, 0xa and out-of-range values only get the writes above.
        break;
    }
}

// FUN_062c50e0.
void DockBehavior::RefreshApproachState()
{
    if (DockPortIndex < -1)   // signed
        return;
    Watchable const* const Watched = Watcher324.target();
    if (Watched == nullptr || reinterpret_cast<char const*>(Watched) - 8 == nullptr)
        return;

    // get_dock_hardpoints (IObjInspect +0xc8); the transforms are left uninitialised by retail.
    IObjInspect const* const Obj = ObjInspectOf(reinterpret_cast<char const*>(Watched) - 8);
    Transform XformA;
    Transform XformB;
    Transform XformC;
    Obj->get_dock_hardpoints(DockPortIndex, reinterpret_cast<TERMINAL_TYPE*>(&TerminalType),
                             &XformA, &XformB, &XformC, &DockPointRadius);
    SavedPosition3fc = XformA.Translation;
    Orient40c = XformA.Rotation;
    DestPoint3cc = XformB.Translation;
    OrientMatrix3d8 = XformB.Rotation;
    InterceptPoint39c = XformC.Translation;
    Orient3a8 = XformC.Rotation;

    CombatFlag344 = 0;
    if ((StateFlags32c & (OBJ_STATION | OBJ_CAPITAL)) == 0)
    {
        Vector D1;
        D1.x = DestPoint3cc.x - InterceptPoint39c.x;
        D1.y = DestPoint3cc.y - InterceptPoint39c.y;
        D1.z = DestPoint3cc.z - InterceptPoint39c.z;
        Vector D2;
        D2.x = SavedPosition3fc.x - DestPoint3cc.x;
        D2.y = SavedPosition3fc.y - DestPoint3cc.y;
        D2.z = SavedPosition3fc.z - DestPoint3cc.z;

        // D2 is normalized first. Strictly below DAT_0639f53c (pi/32).
        Vector N2;
        VecNormalize3(&D2, &N2);
        Vector N1;
        VecNormalize3(&D1, &N1);
        double const Angle = std::acos(ClampedDot3(N1, N2));
        if (Angle < 3.14159274f / 32.0f)
        {
            // Sum of the three z columns, each partial sum rounded to float.
            Vector Sum1;
            Sum1.x = Orient40c.E[0][2] + OrientMatrix3d8.E[0][2];
            Sum1.y = Orient40c.E[1][2] + OrientMatrix3d8.E[1][2];
            Sum1.z = Orient40c.E[2][2] + OrientMatrix3d8.E[2][2];
            Vector Sum2;
            Sum2.x = Sum1.x + Orient3a8.E[0][2];
            Sum2.y = Sum1.y + Orient3a8.E[1][2];
            Sum2.z = Sum1.z + Orient3a8.E[2][2];
            // D2 un-normalized here; strictly above 0.9999f (DAT_0639f538).
            Vector NegSum;
            NegSum.x = -Sum2.x;
            NegSum.y = -Sum2.y;
            NegSum.z = -Sum2.z;
            if (ClampedDot3(D2, NegSum) > 0.9999f)
                CombatFlag344 = 1;
        }
    }

    if ((StateFlags32c & OBJ_JUMP_GATE) != 0)
    {
        Vector D3;
        D3.x = SavedPosition3fc.x - DestPoint3cc.x;
        D3.y = SavedPosition3fc.y - DestPoint3cc.y;
        D3.z = SavedPosition3fc.z - DestPoint3cc.z;
        Vector N3;
        VecNormalize3(&D3, &N3);
        Facing38c = N3;
    }
}

// ----------------------------------------------------------------------------
// FUN_062c4900, Update()'s non-virtual tail: the per-state tick of the dock state machine.
// Case 0xb's threshold is 3.0f, or .rdata:DAT_0639e884 (10.0f) for a player owner outside
// single player.
int DockBehavior::UpdateApproachTail()
{
    // Retail keeps the result in a stack local preset to 1 and stores it on every exit.
    UpdateResult = 1;
    AdvancePreamble();  // FUN_062c47f0

    if (ModeState320 == 1)
    {
        // Hand the tick delta to the Goto pilot, then its slot 7; anything but 2 leaves
        // through the default case. NavPilotPtr is never null-tested.
        Behavior* const Pilot = static_cast<Behavior*>(GotoDirective.NavPilotPtr);
        Pilot->DeltaTime = DeltaTime;
        if (Pilot->OnUpdate() != 2)
            return UpdateResult;

        if ((StateFlags32c & (OBJ_JUMP_GATE | OBJ_TRADELANE_RING)) != 0)
        {
            EnterState(2);
            UpdateResult = 1;
            return UpdateResult;
        }
        EnterState(3);
        UpdateResult = 1;
        return UpdateResult;
    }

    bool bAborted = false;
    if (ModeState320 != 0xc)
    {
        // Aborts on not-greater or unordered. The watched object is not null-tested.
        float Health = 1.0f;
        ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()))->get_status(Health);
        if (!(Health > 0.0f))
            bAborted = true;
    }

    {
        void* ProxWatched = const_cast<void*>(
            static_cast<void const*>(ProxWatcher450.Watcher.target()));
        if (bAborted ||
            ((ProxWatched != nullptr) && (ProxWatched != reinterpret_cast<void*>(8)) &&
             ((ProxWatcher450.Flags14 & 2) == 0)))
        {
            Move.SetField44(0);     // FUN_062d88b0
            Move.Commit();
            Face.Commit();
            UpdateResult = 1;
            return UpdateResult;
        }
    }

    if (!CheckCanProceed())
    {
        Disengage();
        RecomputeApproach();
        UpdateResult = 1;
        return UpdateResult;
    }

    switch (ModeState320)
    {
    case 2:
    {
        // In retail's order; OwnerContext is not null-tested.
        if ((LaneNextRing == nullptr) &&
            !ObjInspectOf(OwnerContext)->is_player() &&
            (static_cast<char>(StateFlags32c) < 0))
        {
            EnterState(0xc);
            UpdateResult = 1;
            return UpdateResult;
        }

        bool bPastDock = false;
        if ((StateFlags32c & (OBJ_JUMP_GATE | OBJ_TRADELANE_RING)) != 0)
        {
            // ((Dz*Fz + Dy*Fy) + Dx*Fx) on the x87 stack; >= 0 or unordered falls to
            // LAB_062c4c04, < 0 exits to LAB_062c4c35.
            NavHelperRefreshLeadPoint();
            SystemManager* const Sys = SysMgr;
            Vector Delta;
            Delta.x = Sys->SelfTarget.CachedPos.x - DestPoint3cc.x;
            Delta.y = Sys->SelfTarget.CachedPos.y - DestPoint3cc.y;
            Delta.z = Sys->SelfTarget.CachedPos.z - DestPoint3cc.z;
            double const Along = static_cast<double>(Delta.z) * Facing38c.z
                               + static_cast<double>(Delta.y) * Facing38c.y
                               + static_cast<double>(Delta.x) * Facing38c.x;
            // Retail then compares against 200.0f (DAT_0639f488) and exits on not-greater,
            // always taken for a negative value, so 062c4b0e-062c4bfe is unreachable.
            bPastDock = Along < 0.0;
        }

        if (!bPastDock)
        {
            // The FaceIdea's result is discarded; the MoveIdea's decides.
            (void)Face.Update(DeltaTime);
            if (Move.Update(DeltaTime) != 1)
                break;
        }

        EnterState(((StateFlags32c & (OBJ_JUMP_GATE | OBJ_TRADELANE_RING | OBJ_JUMP_HOLE)) == 0) ? 6 : 3);
        UpdateResult = 1;
        return UpdateResult;
    }
    case 3:
    {
        // The slot-0x5c mode and target id are filled in only on the sign-bit branch.
        int SlotMode = 0;
        int TargetQuery = 0;
        if (static_cast<char>(StateFlags32c) < 0)
        {
            SavedPosition3fc = DestPoint3cc;
            // LaneNextRing is not null-tested.
            TargetQuery = static_cast<int>(ObjInspectOf(LaneNextRing)->get_id());
            SlotMode = 2;
        }
        if (ApproachClearedFlag334 != 0)
        {
            EnterState(6);
            UpdateResult = 1;
            return UpdateResult;
        }

        EnterState(4);
        Flag39a = 1;

        // The docking object is not null-tested.
        int const EngineQuery = static_cast<int>(
            ObjInspectOf(HeaderBlockFromWatched(Watcher324.target()))->get_id());

        // IObjAffect slot 23 (+0x5c) with this+0x2c, our own RequestCB base, last; the shape
        // is fixed by Server.dll's Ship body .text:06d02670. Result unused; no null test on
        // OwnerContext.
        (void)ObjAffectOf(OwnerContext)->request_event(
            SlotMode, reinterpret_cast<unsigned const*>(&EngineQuery),
            reinterpret_cast<unsigned const*>(&TargetQuery), 0,
            static_cast<IObjAffect::RequestCB*>(this));
        UpdateResult = 1;
        return UpdateResult;
    }
    case 6:
        if ((StateFlags32c & OBJ_JUMP_HOLE) != 0)
        {
            EnterState(9);
            UpdateResult = 1;
            return UpdateResult;
        }
        {
            int MoveResult = Move.Update(DeltaTime);
            int FaceResult = Face.Update(DeltaTime);
            if (((StateFlags32c & (OBJ_JUMP_GATE | OBJ_TRADELANE_RING)) != 0) && (FaceResult == 1))
            {
                EnterState(8);
                UpdateResult = 1;
                return UpdateResult;
            }
            if (MoveResult != 1)
                break;
            EnterState((CombatFlag344 != 0) ? 9 : 7);
            UpdateResult = 1;
            return UpdateResult;
        }
    case 7:
        // Only the MoveIdea's result is tested; falls into case 10's tail either way.
        (void)Face.Update(DeltaTime);
        if (Move.Update(DeltaTime) == 1)
            EnterState(9);
        [[fallthrough]];
    case 10:
        UpdateResult = 0;
        return UpdateResult;
    case 8:
    {
        if (SplineMove.Update(DeltaTime) == 1)
        {
            SplineMove.Commit();
            EnterState(9);
            UpdateResult = 1;
            return UpdateResult;
        }
        // FUN_062da3f0 against .rdata:DAT_0639f534 (pi/16); unordered builds the spline.
        if (!(SplineMove.GetFaceAngle() < 0.19634955f))
        {
            BuildFinalApproachSpline();       // FUN_062c56d0
            UpdateResult = 1;
            return UpdateResult;
        }
        break;
    }
    case 9:
    {
        // Retail stores 0 into the result first; exits that do not set it again return 0.
        UpdateResult = 0;
        (void)Face.Update(DeltaTime);
        int MoveResult = Move.Update(DeltaTime);

        if (static_cast<char>(StateFlags32c) < 0)
        {
            if (MoveResult != 1)
            {
                // SavedPosition3fc + Facing38c * .rdata:DAT_0639f530 (-25.0f). x and y stay on
                // the x87 stack until the add; z's product is rounded first.
                Vector RetreatPoint;
                RetreatPoint.x = static_cast<float>(static_cast<double>(Facing38c.x) * -25.0 + SavedPosition3fc.x);
                RetreatPoint.y = static_cast<float>(static_cast<double>(Facing38c.y) * -25.0 + SavedPosition3fc.y);
                float const BackZ = static_cast<float>(static_cast<double>(Facing38c.z) * -25.0);
                RetreatPoint.z = static_cast<float>(static_cast<double>(BackZ) + SavedPosition3fc.z);
                // FUN_062e53e0.
                if (!IsPointOutsideHeadingCone(RetreatPoint, ObjInspectOf(OwnerContext), 0.0f))
                    break;
            }

            Move.Commit();
            Face.Commit();
            // IObjAffect slot 18.
            (void)ObjAffectOf(OwnerContext)->set_target(nullptr, SUBOBJ_ID_NONE, -1);

            // IObjAffect slot 25; a non-zero result enters state 0xd.
            void* EngineObj = HeaderBlockFromWatched(Watcher324.target());
            if (ObjAffectOf(OwnerContext)->go_tradelane(
                    static_cast<IObjInspect const*>(EngineObj),
                    static_cast<IObjInspect const*>(LaneNextRing), false, 0.0f) != 0)
            {
                EnterState(0xd);
                return UpdateResult;
            }
            // FUN_062d4520 on BehaviorMgr.
            BehaviorMgr->QueueNotification(
                OwnerContext, 0, 0.0f);
            Move.SetField44(2);   // FUN_062d88b0
            EnterState(10);
            AbortFlag398 = 1;
            Flag39a = 1;
            return UpdateResult;
        }

        if (MoveResult != 1)
            break;
        if (TerminalType != TERM_MOOR_SMALL && TerminalType != TERM_MOOR_MEDIUM && TerminalType != TERM_MOOR_LARGE)
        {
            EnterState(0xb);
            return UpdateResult;
        }
        Move.Commit();
        Face.Commit();
        (void)ObjAffectOf(OwnerContext)->set_target(nullptr, SUBOBJ_ID_NONE, -1);
        EnterState(0xc);
        return UpdateResult;
    }
    case 0xb:
    {
        StateTimer340 += DeltaTime;
        float Threshold = 3.0f;
        if (!SinglePlayer() && ObjInspectOf(OwnerContext)->is_player())
        {
            // .rdata:DAT_0639e884 (00 00 20 41); the listing mis-slices it into a string.
            // No null test on OwnerContext.
            Threshold = 10.0f;
        }
        // Strictly less; unordered stays.
        if (Threshold < StateTimer340)
        {
            Disengage();
            RecomputeApproach();
            UpdateResult = 1;
            return UpdateResult;
        }
        break;
    }
    case 0xc:
    case 0xd:
        UpdateResult = 2;
        break;
    default:
        break;
    }

    return UpdateResult;
}

// FUN_062c4530. A jump table on ModeState320 - 2:
//   mode 2       -> the range test
//   modes 3, 6   -> the facing test, then the range test
//   mode 8       -> range test only when Flag39a or ApproachClearedFlag334
//   modes 4/5/7 and anything else -> true
// The range test is FUN_062e4500 (IsWithinInterceptRangeOfIface) on SystemManager+0x14fc and
// the watched object with float(1000.0f (.data DAT_063ebdb4) * 1.05 (double DAT_0639f528)).
bool DockBehavior::CheckCanProceed()
{
    SystemManager* const Sys = SysMgr;
    switch (ModeState320)
    {
    case 3:
    case 6:
    {
        if ((StateFlags32c & (OBJ_JUMP_GATE | OBJ_TRADELANE_RING)) == 0)
            break;
        NavHelperRefreshLeadPoint();   // FUN_062c47c0
        Vector Delta;
        Delta.x = Sys->SelfTarget.CachedPos.x - DestPoint3cc.x;
        Delta.y = Sys->SelfTarget.CachedPos.y - DestPoint3cc.y;
        Delta.z = Sys->SelfTarget.CachedPos.z - DestPoint3cc.z;
        // On the x87 stack; false on >= 0 or unordered.
        double const Along = static_cast<double>(Delta.z) * Facing38c.z
                           + static_cast<double>(Delta.y) * Facing38c.y
                           + static_cast<double>(Delta.x) * Facing38c.x;
        if (!(Along < 0.0))
            return false;
        // Retail then compares against 200.0f (DAT_0639f488) and goes to the range test on
        // not-greater, always taken for a negative value, so 062c460e-062c46fc is unreachable
        // in retail and not reproduced.
        break;
    }
    case 8:
        if (Flag39a == 0 && ApproachClearedFlag334 == 0)
            return false;
        break;
    case 2:
        break;
    default:
        return true;
    }

    IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()));
    float const Range = static_cast<float>(1000.0 * 1.05);           // float times double
    return IsWithinInterceptRangeOfIface(&Sys->SelfTarget, Watched, Range);
}

// ----------------------------------------------------------------------------
// FUN_062c47f0, run every tick: this tick's linear and angular speed caps for the three Ideas.
//
//   LinearCap  = MaxApproachSpeed46c
//   AngularCap = DAT_0639f47c (1000.0f)
//   if (ComputeFormationFollowerEnvelope(SysMgr + 0x1364, &Envelope))
//   {
//       LinearCap  = min(LinearCap,  Envelope.Speed);
//       AngularCap = min(AngularCap, Envelope.AngularSpeed);
//   }
//   if (HeaderBlockFromWatched(ProxWatcher450) != null)
//       LinearCap = min(LinearCap, ProxWatcherComputeSpeedCap(...));
//   Move/C get LinearCap, Face (the FaceIdea) AngularCap.
//
// The min-clamps keep the incumbent only when it is strictly less (unordered takes the new
// value). Retail calls ProxWatcherComputeSpeedCap twice with identical arguments (MSVC's
// min() with an ST0 return that FCOMP pops): the compare uses the unrounded first result, the
// store the rounded second, and only a strictly greater cap skips the store.
void DockBehavior::AdvancePreamble()
{
    float LinearCap = MaxApproachSpeed46c;
    float AngularCap = 1000.0f;

    // Retail zeroes the first four dwords and lets the callee write the trailing byte, so
    // bCruiseAllowed is left alone.
    FormationFollowerEnvelope Envelope;
    Envelope.Speed = 0.0f;
    Envelope.Throttle = 0.0f;
    Envelope.AngularSpeed = 0.0f;
    Envelope.AngularThrottle = 0.0f;

    // This caller reads only +0x00 and +0x08; GotoBehavior::Update reads the other three.
    if (SysMgr->FormationLeaders.ComputeFormationFollowerEnvelope(&Envelope))
    {
        if (!(LinearCap < Envelope.Speed))
            LinearCap = Envelope.Speed;
        if (!(AngularCap < Envelope.AngularSpeed))
            AngularCap = Envelope.AngularSpeed;
    }

    if (HeaderBlockFromWatched(ProxWatcher450.Watcher.target()) != nullptr)
    {
        if (!(ProxWatcherComputeSpeedCap(reinterpret_cast<unsigned char*>(&ProxWatcher450),
                                         OwnerContext, SysMgr, 1, 1.0f) >
              static_cast<double>(LinearCap)))
        {
            LinearCap = static_cast<float>(ProxWatcherComputeSpeedCap(
                reinterpret_cast<unsigned char*>(&ProxWatcher450), OwnerContext, SysMgr, 1, 1.0f));
        }
    }

    Move.SetThrottleScale(LinearCap);
    Face.SetThrottleScale(AngularCap);
    SplineMove.SetThrottleScale(LinearCap);
}

// FUN_062c56d0. Builds the final-approach Catmull-Rom segment at +0x348 and gives it to
// SplineMove. Every component is one float operation (FUN_0628b030 Vec3Assign / FUN_062887d0
// Vector add); only the two lengths stay on the x87 stack through FSQRT and the scale, so double.
void DockBehavior::BuildFinalApproachSpline()
{
    SystemManager* const Sys = SysMgr;

    // Without a formation leader (FUN_062ecab0), the owner's formation offset rotated into
    // world space by the 3DMathEngine's TransformMatrix.
    Vector FormationOffset;
    FormationOffset.x = 0.0f;
    FormationOffset.y = 0.0f;
    FormationOffset.z = 0.0f;
    bool const bHasLeader = Sys->FormationLeaders.GetFormationLeader() != nullptr;
    if (!bHasLeader)
    {
        ObjInspectOf(OwnerContext)->get_formation_offset(FormationOffset);
        Transform const& ShipXform = Sys->SelfTarget.Iface->get_transform();
        Vector Rotated;
        Acquire3DMathEngine()->TransformMatrix(Rotated, ShipXform.Rotation, FormationOffset);
        FormationOffset = Rotated;
    }

    NavHelperRefreshLeadPoint();   // FUN_062c47c0
    Spline.P1.x = Sys->SelfTarget.CachedPos.x + FormationOffset.x;
    Spline.P1.y = Sys->SelfTarget.CachedPos.y + FormationOffset.y;
    Spline.P1.z = Sys->SelfTarget.CachedPos.z + FormationOffset.z;

    Vector DockPoint;
    DockPoint.x = DestPoint3cc.x + DestPointOffset.x;
    DockPoint.y = DestPoint3cc.y + DestPointOffset.y;
    DockPoint.z = DestPoint3cc.z + DestPointOffset.z;
    Spline.P2.x = DockPoint.x + FormationOffset.x;
    Spline.P2.y = DockPoint.y + FormationOffset.y;
    Spline.P2.z = DockPoint.z + FormationOffset.z;

    // Retail rebuilds the same difference twice; (z*z + y*y) + x*x, FSQRT, times 0.5f
    // (DAT_063ebdac) and 2.5f (DAT_063ebdb0).
    Vector Span;
    Span.x = Spline.P2.x - Spline.P1.x;
    Span.y = Spline.P2.y - Spline.P1.y;
    Span.z = Spline.P2.z - Spline.P1.z;
    double const SpanLength = std::sqrt(static_cast<double>(Span.z) * Span.z
                                        + static_cast<double>(Span.y) * Span.y
                                        + static_cast<double>(Span.x) * Span.x);
    float const LeadIn = static_cast<float>(SpanLength * 0.5);
    float const LeadOut = static_cast<float>(SpanLength * 2.5);

    // Rotation column 2; retail negates it (FUN_062c5a70) and back (FCHS), an exact identity.
    Transform const& Xform = Sys->SelfTarget.Iface->get_transform();
    Vector Forward;
    Forward.x = Xform.Rotation.E[0][2];
    Forward.y = Xform.Rotation.E[1][2];
    Forward.z = Xform.Rotation.E[2][2];

    Vector Ahead;
    Ahead.x = Forward.x * LeadIn;
    Ahead.y = Forward.y * LeadIn;
    Ahead.z = Forward.z * LeadIn;
    Vector Start;
    Start.x = Ahead.x + Spline.P1.x;
    Start.y = Ahead.y + Spline.P1.y;
    Start.z = Ahead.z + Spline.P1.z;
    Spline.P0.x = Start.x + FormationOffset.x;
    Spline.P0.y = Start.y + FormationOffset.y;
    Spline.P0.z = Start.z + FormationOffset.z;

    Vector Behind;
    Behind.x = LeadOut * Facing38c.x;
    Behind.y = LeadOut * Facing38c.y;
    Behind.z = LeadOut * Facing38c.z;
    Vector End;
    End.x = Behind.x + Spline.P2.x;
    End.y = Behind.y + Spline.P2.y;
    End.z = Behind.z + Spline.P2.z;
    Spline.P3.x = End.x + FormationOffset.x;
    Spline.P3.y = End.y + FormationOffset.y;
    Spline.P3.z = End.z + FormationOffset.z;

    Spline.calculate_arclength();
    SplineMove.SetSpline(&Spline);                                   // FUN_062da380
}

// Inlined in retail (062c4a72, 062c4b0e, 062c3e97): the SystemManager's physical-radius cache,
// latch +0x157a, centre +0x1508, radius +0x1514. Neither pointer is null-tested.
void DockBehavior::NavHelperRefreshLeadPoint()
{
    SystemManager* const Sys = SysMgr;
    if (Sys->SelfTarget.bPosValid != 0)
        return;
    Sys->SelfTarget.bPosValid = 1;
    Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                          Sys->SelfTarget.CachedPos);
}

// Idea setters used above (common_idea.h):
//   FUN_062d5840 -> MoveIdea::SetPosition       FUN_062d7560 -> FaceIdea::SetPosition
//   FUN_062d5980 -> MoveIdea::SetMode           FUN_062d7590 -> FaceIdea::SetDirection
//   .text:062a66b0 -> MoveIdea::SetRange3c      FUN_062d75c0 -> FaceIdea::SetBasis
//   .text:063275f0 -> MoveIdea::SetAngle38      FUN_062d5a80 -> MoveIdea::Offset4c store
//   FUN_062d93c0 -> FaceIdea::SetAngleWeight8c  FUN_062da400 -> SplineMoveIdea::SetStopStyle

// .rdata:DAT_0639f518, a double in retail.
double const DockBehavior::kEnterState9RadiusScale = 0.6;
// FUN_062c2ad0, receiver the proximity watcher: its far threshold (+0x0c), times its cruise
// ratio (+0x10) when the byte argument is set. The caller rounds the result to float.
float DockBehavior::QueryHandleRange(void* HandleField, int Mode)
{
    DockProximityWatcher const* const Watcher = static_cast<DockProximityWatcher const*>(HandleField);
    if (static_cast<unsigned char>(Mode) != 0)
        return static_cast<float>(static_cast<double>(Watcher->FarDistance) * Watcher->CruiseSpeedRatio);
    return Watcher->FarDistance;
}

// Slot 10, FUN_062c3370. The argument is a pub::AI::DirectiveDockOp. Always returns true.
// FindVerifiedObject is FUN_062e74a0.
bool DockBehavior::OnApplyDirective(void* DirectiveOp)
{
    SystemManagerTimerTable* const ObjDb = &SysMgr->Timers;
    // REP MOVSD of the whole op, vptr included.
    std::memcpy(static_cast<void*>(&DockOp), DirectiveOp, sizeof(DockOp));
    ModeState320 = 0;
    StateFlags32c = 0;
    IObjRW* const Dock = static_cast<IObjRW*>(
        ObjDb->FindVerifiedObject((unsigned const*)&DockOp.DockObjectId));
    Watcher324.set(Dock != nullptr ? static_cast<Watchable const*>(Dock) : nullptr);
    IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()));
    if (Watched == nullptr)
    {
        Flags28 = static_cast<unsigned char>((Flags28 & 0xfd) | 0x1);
        return true;
    }

    // An object docking at that base replaces it (FUN_062e7400).
    IObjRW* const Docking = ObjDb->FindObjectDockingAtBase(Watched);
    if (Docking != nullptr)
        Watcher324.set(static_cast<Watchable const*>(Docking));

    StateFlags32c = GetObjectTypeOrZero(   // FUN_062e6200
        ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target())));
    LaneNextRing = ObjDb->FindVerifiedObject((unsigned const*)&DockOp.NextRingId);   // stored raw
    ApproachClearedFlag334 = DockOp.bHasDockPortIndex;
    DockPointRadius = -1.0f;
    if (DockOp.bHasDockPortIndex != 0)
    {
        RequestedDockPortIndex = DockOp.DockPortIndex;
        DockPortIndex = DockOp.DockPortIndex;
        RefreshApproachState();                                     // FUN_062c50e0
    }
    else
    {
        TerminalType = 0;
        RequestedDockPortIndex = -1;
    }

    IObjRW* const Prox = static_cast<IObjRW*>(
        ObjDb->FindVerifiedObject((unsigned const*)&DockOp.TetherObjectId));
    ProxWatcher450.NearDistance = DockOp.TetherNearRange;
    ProxWatcher450.FarDistance = DockOp.TetherFarRange;
    ProxWatcher450.Watcher.set(Prox != nullptr ? static_cast<Watchable const*>(Prox) : nullptr);
    ProxWatcher450.Flags14 = 0;
    ProxWatcher450.SavedFlags18 = 0;
    IObjRW* const ProxObj = ObjRwFromWatchable(const_cast<Watchable*>(ProxWatcher450.Watcher.target()));
    if (ProxObj != nullptr)
    {
        // Op+0x28 as a float against double 0.0 (DAT_0639c430); not greater (or unordered)
        // derives the ratio from speeds.
        float const Ratio = *reinterpret_cast<float const*>(&DockOp.TetherCruiseRatio);
        if (Ratio > 0.0)
        {
            ProxWatcher450.CruiseSpeedRatio = Ratio;
        }
        else
        {
            // Own cruise speed rounded to float, divided by the prox object's engine speed.
            SystemManager* const Sys = SysMgr;
            IObjRW* const Self = ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()));
            float const OwnSpeed = Behavior_GetSpeed(
                ObjInspectOf(Self), 1.0f, IObject::ThrustEquipCruise);
            double const ProxSpeed = Behavior_GetSpeed(
                ObjInspectOf(ProxObj), 1.0f, IObject::ThrustEquipEngine);
            ProxWatcher450.CruiseSpeedRatio = static_cast<float>(OwnSpeed / ProxSpeed);
        }
        // Kept on >= 1.0f or unordered.
        if (ProxWatcher450.CruiseSpeedRatio < 1.0f)
            ProxWatcher450.CruiseSpeedRatio = 1.0f;
    }
    Flags28 = static_cast<unsigned char>((Flags28 & 0xfe) | 0x2);
    return true;
}

// Slot 21, FUN_062c2f60: release Watcher324, then the proximity watcher, when their object's
// id is the one leaving (passed by the manager's loop at .text:062d405c).
void DockBehavior::OnObjectLeftSystem(void* Arg)
{
    unsigned int const LeftId = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Arg));
    IObjRW* const Watched = ObjRwFromWatchable(const_cast<Watchable*>(Watcher324.target()));
    if (Watched != nullptr && Watched->get_id() == LeftId)
        Watcher324.set(nullptr);
    IObjRW* const Prox = ObjRwFromWatchable(const_cast<Watchable*>(ProxWatcher450.Watcher.target()));
    if (Prox != nullptr && Prox->get_id() == LeftId)
        ProxWatcher450.Watcher.set(nullptr);
}

// FUN_062c50c0: copies the offset into DestPointOffset (+0x444). Called only from
// BaseFollowerBehavior::OnUpdate (.text:062cbc3e) on its DockDelegate, BehaviorDispatchTable[7].
void DockBehavior::SetDestPointOffset(Vector const& Offset)
{
    DestPointOffset = Offset;
}
