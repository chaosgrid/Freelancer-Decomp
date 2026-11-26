// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- IBehaviorManager (ctor/dtor, the exported behavior-control API, the
// notification queue, UpdateBehaviorTick and the one virtual OnEvent) and
// ConnectBehaviorManagerObjectDb.

#include "common_iface.h"
#include "common_behavior.h" // class Behavior -- what BehaviorDispatchTable holds
#include "common_dockbehavior.h" // DockBehavior -- BehaviorDispatchTable[7] (get_docking_port)

#include "common_aisteer.h" // IsWithinInterceptRangeOfIface -- UpdateBehaviorTick real-body pass
#include "common_cobjdb.h"
#include "common_csys.h"   // Acquire3DMathEngine, MatrixColumn2 (update_current_behavior_direction)
#include "x86math.h"       // I3DMathEngine::TransposeTransform, Vector, Matrix
#include "common_goods.h"
#include "common_iustategraph.h" // GetAiTargetList / GetClosestTrailingShipDistance --
                                  // the SystemManager+0x34 AI target list that
                                  // get_closest_trailing_ship tail-calls into

#include "common_misc.h" // SinglePlayer() -- UpdateBehaviorTick's docking-target probe
#include "common_newchar.h"
#include "common_pub.h"
#include "common_pathfind.h" // PathfindManager -- IBehaviorManager+0x0C
#include "common_universe.h"
#include "dacom.h"

namespace
{
    // The one local with a destructor in IBehaviorManager::UpdateBehaviorTick (FuncInfo
    // 063aeb40): it holds &Sys->Timers after the tick has set Timers.bKeepCache = 1. Its
    // destructor is FUN_062d3410 (clears byte +0x1d), reached out of line only from
    // Unwind@06395120; both normal exits inline it. Retail gives it no name.
    struct FTimerTableKeepCacheScope
    {
        explicit FTimerTableKeepCacheScope(SystemManagerTimerTable* InTable)
            : Table(InTable)
        {
        }

        ~FTimerTableKeepCacheScope()
        {
            Table->bKeepCache = 0;
        }

        FTimerTableKeepCacheScope(FTimerTableKeepCacheScope const&) = delete;
        FTimerTableKeepCacheScope& operator=(FTimerTableKeepCacheScope const&) = delete;

        SystemManagerTimerTable* Table;
    };
}

#include <intrin.h> // __debugbreak -- UpdateBehaviorTick's retail INT3 (.text:062d32fd)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

// Behavior::OnEvaluateReadiness takes a float: the value UpdateBehaviorTick pushes is the
// float accumulator at +0xdc (FADD/FSTP float ptr [ESI+0xdc] earlier in the same function).

// ============================================================================
// IBehaviorManager — constructor / destructor (not exported). FUN_062d2220 and
// FUN_062d2530.
// ============================================================================

IBehaviorManager::IBehaviorManager(IObjRW* Owner)
{
    // PORT: fields retail's ctor never writes (operator_new garbage in retail) are zeroed first
    // so the port is deterministic; every store retail does make follows, in retail order.
    // Retail vptr 063a037c; the compiler installs the port's one-slot vftable (OnEvent).
    memset(BehaviorScores, 0, sizeof(BehaviorScores));
    memset(PadBd, 0, sizeof(PadBd));
    memset(OpaqueD9, 0, sizeof(OpaqueD9));
    BehaviorTimerC = 0.0f;
    bScanState = 0;
    memset(OpaqueFa, 0, sizeof(OpaqueFa));
    memset(Pad131, 0, sizeof(Pad131));
    memset(Pad151, 0, sizeof(Pad151));
    memset(Opaque16a, 0, sizeof(Opaque16a));
    memset(Opaque171, 0, sizeof(Opaque171));
    for (int Index = 0; Index < 21; ++Index)
        BehaviorDispatchTable[Index] = nullptr;
    Flags1c8 = 0;
    memset(Pad1c9, 0, sizeof(Pad1c9));

    // The current-directive storage (two zero dwords and a -1), the attack-override timer pair.
    int const MinusOne = -1;
    memset(DirectiveStorage, 0, 8);
    memcpy(DirectiveStorage + 8, &MinusOne, sizeof(int));
    AttackOverrideTimer = 0.0f;
    AttackOverridePeriod = 1.0f;

    // The +0xF0 BaseWatcher is zeroed (Watched, Next).
    static_assert(offsetof(IBehaviorManager, WatchablePtr) == 0xF0, "IBehaviorManager+0xF0 watcher");
    static_assert(offsetof(IBehaviorManager, OpaqueF4) == 0xF4, "IBehaviorManager+0xF4 watcher link");
    static_assert(sizeof(BaseWatcher) == 8, "BaseWatcher is Watched + Next");
    WatchablePtr = nullptr;
    memset(OpaqueF4, 0, sizeof(OpaqueF4));

    // The notification queue (a VC6 std::list). +0x158 is the list's empty allocator object --
    // retail copies the low byte of the ctor argument into it, a byte nothing ever reads; the
    // port zeroes it. Then the sentinel node operator_new(0x24), self-linked, and a zero count.
    memset(Pad158, 0, sizeof(Pad158));
    InitNotificationQueue();

    bAllowUserSteering = 1;
    DockingPortFlag = 0;
    DockingPortStatus = 0;
    TickSuppressedFlag = 0;
    StateGraph = nullptr;

    // Bind the +0xF0 watcher to the owner's Watchable sub-object (Owner+8).
    reinterpret_cast<BaseWatcher*>(&WatchablePtr)->set(
        Owner != nullptr ? static_cast<Watchable const*>(Owner) : nullptr);

    // Retail ORs bit 0 into a byte it never initialised; the port zeroed it above, so the
    // result is 1.
    ObjectDb = nullptr;
    Pathfind = nullptr;
    BehaviorDirty = 0;
    bScanState = 1;
    Flags1c8 = static_cast<unsigned char>(Flags1c8 | 1);
    for (int Index = 0; Index < 5; ++Index)
        DirectiveCallbacks[Index] = nullptr;
    ContentCbPtr = nullptr;
    PendingDirectiveResult = 0;
    PendingCompletedBehavior = -1;

    // operator_new(0x580) + FUN_062db3b0(this). PathfindManager's ctor queries the watched
    // object without a null test.
    Pathfind = new PathfindManager(this);

    // operator_new(0x195c) + FUN_062e6260, then FUN_062e6490(Owner, this).
    Sys = new SystemManager();
    (void)InitSystemManager(Sys, Owner, this);

    // Build all 21 behaviours and bind each one (vtbl[1]); no null test on the
    // CREATE_BEHAVIOR result.
    for (int Index = 0; Index < 21; ++Index)
    {
        BehaviorDispatchTable[Index] = CREATE_BEHAVIOR(Index);
        (void)BehaviorDispatchTable[Index]->SetContext(Owner, this, Sys);
    }

    CurrentBehaviorIndex = 0;

    // Three phase offsets, each `(float)rand() * (1/32768) (DAT_063997d0) * Period`, rand()
    // called in this order. Periods: 3.0f (DAT_063a0340), 0.2f (DAT_063a0344), 1.0f.
    // BehaviorTimerC and the attack period are rewritten between the draws; the period ends
    // as DAT_063a0348 == 1.0f.
    BehaviorTimerA = static_cast<float>(std::rand()) * (1.0f / 32768.0f) * 3.0f;
    BehaviorTimerC = 0.0f;
    AttackOverridePeriod = 1.0f;
    BehaviorTimerB = static_cast<float>(std::rand()) * (1.0f / 32768.0f) * 0.2f;
    AttackOverrideTimer = static_cast<float>(std::rand()) * (1.0f / 32768.0f) * 1.0f;
    AttackOverridePeriod = 1.0f;

    // IObjInspect::is_player (+0xBC) on the owner.
    if (Owner != nullptr && static_cast<IObjInspect*>(Owner)->is_player())
        DefaultBehaviorIndex = 0x13;
    else
        DefaultBehaviorIndex = 6;

    LevelCameraFlag = 1;
    LastCommandedDirection.x = 0.0f;
    LastCommandedDirection.y = 0.0f;
    LastCommandedDirection.z = 0.0f;
    ShipUpDirFlag = 0;
    ShipUpDirection.x = 0.0f;
    ShipUpDirection.y = 1.0f;
    ShipUpDirection.z = 0.0f;
    CameraUp.x = 0.0f;
    CameraUp.y = 1.0f;
    CameraUp.z = 0.0f;
    UserTurningInputState = 0;
    memset(Opaque14c, 0, sizeof(Opaque14c));
    CameraLevelStatus = 0;
    ManeuverMask = 0;
    LockManeuversByte = 0;
    InSightsTargetId = 0;
    TurnSensitivity = 0.75f;                                 // 0x3f400000
    ContentCbCreationType = pub::AI::ContentCallback::CreationNone;
}

IBehaviorManager::~IBehaviorManager()
{
    if (CurrentBehaviorIndex != -1)
        CurrentBehaviorIndex = -1;

    // PathfindManager's destructor is inlined (its two BaseWatchers, +0x218 then +0x1C8).
    delete Pathfind;
    Pathfind = nullptr;

    // SystemManager's destructor is inlined.
    delete Sys;
    Sys = nullptr;

    // Scalar deleting destructor (vtbl[0](1)).
    for (int Index = 0; Index < 21; ++Index)
    {
        if (BehaviorDispatchTable[Index] != nullptr)
        {
            delete BehaviorDispatchTable[Index];
            BehaviorDispatchTable[Index] = nullptr;
        }
    }

    // Each queued node goes through FUN_062d4c50, then the sentinel is freed.
    ClearNotificationQueue();

    // The +0xF0 BaseWatcher's destructor, unlinking this manager from the owner's chain.
    reinterpret_cast<BaseWatcher*>(&WatchablePtr)->set(nullptr);
}

// FUN_062d2830, RET 4.
void ConnectBehaviorManagerObjectDb(IBehaviorManager* Self, IObjDB* ObjectDb)
{
    Self->ObjectDb = ObjectDb;

    // Stops at the first null entry; the database is re-read from +0xEC on every pass.
    for (int Index = 0; Index < 21; ++Index)
    {
        if (Self->BehaviorDispatchTable[Index] == nullptr)
            break;
        Self->BehaviorDispatchTable[Index]->SetObjectDb(Self->ObjectDb);
    }

    SetSystemManagerSharedListHead(Self->Sys, ObjectDb);
    SetPathfindObjectDb(Self->Pathfind, ObjectDb);
}

// ============================================================================
// IBehaviorManager — exported methods
// ============================================================================

bool IBehaviorManager::allow_head_tracking()
{
    // 0x062d43e0: tail-jumps to the current behavior's slot 0x4c with no null test on the
    // entry; the ctor fills all 21 slots.
    if (CurrentBehaviorIndex == -1)
        return false;
    return BehaviorDispatchTable[CurrentBehaviorIndex]->allow_head_tracking();
}

bool IBehaviorManager::cancel_behavior(CancelRequestType Request)
{
    // 0x062d43c0: tail-jumps into the current behavior's slot 0x50 with the same
    // CancelRequestType; no null test on the entry.
    if (CurrentBehaviorIndex == -1)
        return true;
    Behavior* const Current = BehaviorDispatchTable[CurrentBehaviorIndex];
    return Current->cancel_behavior(Request);
}

bool IBehaviorManager::enable_all_maneuvers()
{
    // 0x062d4a90.
    ManeuverMask = 0;
    return true;
}

bool IBehaviorManager::enable_maneuver(int Maneuver, bool bEnable)
{
    // 0x062d4a40: only Maneuver == -1 is rejected; the bit is `2 << (Maneuver & 0x1f)` (x86
    // SHL masks the count), i.e. bit (Maneuver+1) mod 32, not bit Maneuver -- the same scheme
    // external_player_evaluate reads.
    if (Maneuver == -1)
        return false;
    unsigned Bit = 2u << (static_cast<unsigned>(Maneuver) & 0x1f);
    if (bEnable) {
        ManeuverMask &= ~Bit;
    } else {
        ManeuverMask |= Bit;
    }
    return true;
}

EvaluateResult IBehaviorManager::external_player_evaluate(int PlayerIndex)
{
    // 0x062d3f40, RET 4. Otherwise tail-jumps into BehaviorDispatchTable[PlayerIndex]'s
    // evaluate (+0x18) with the raw dword at +0xdc (BehaviorTimerC) replacing the argument; no
    // null test on the entry.
    if ((LockManeuversByte == 0 || PlayerIndex == CurrentBehaviorIndex) &&
        ((ManeuverMask & (2u << (static_cast<unsigned int>(PlayerIndex) & 0x1f))) == 0))
    {
        Behavior* const Current = BehaviorDispatchTable[PlayerIndex];
        unsigned int ContextParam = 0;
        std::memcpy(&ContextParam, &BehaviorTimerC, sizeof(ContextParam));
        return Current->evaluate(ContextParam);
    }
    return EvalSkip;
}

bool IBehaviorManager::get_camera_level_status()
{
    // 0x062d4310.
    if (CameraLevelStatus == 1)
        return LevelCameraFlag != 0;
    return false;
}

float IBehaviorManager::get_closest_trailing_ship() const
{
    // 0x062d4a30: tail-call to FUN_062e7820 (GetClosestTrailingShipDistance) on the AI target
    // list at Sys+0x34. No null test; the ctor always builds Sys.
    return GetClosestTrailingShipDistance(GetAiTargetList(Sys));
}

IDirectiveInfo const* IBehaviorManager::get_current_directive()
{
    // 0x062d3e70: returns this+0xc4.
    return reinterpret_cast<IDirectiveInfo const*>(DirectiveStorage);
}

IObjRW const* IBehaviorManager::get_debugger_target() const
{
    // 0x062d49d0. Behavior slot 24 (get_target), no null test on the entry; the fallback is
    // FUN_062ed5a0 (TargetManager::GetTarget on Sys+0x142c).
    if (CurrentBehaviorIndex == -1)
        return nullptr;
    IObjRW const* const Target = BehaviorDispatchTable[CurrentBehaviorIndex]->get_target();
    if (Target != nullptr)
        return Target;
    return Sys->Targeting.GetTarget();
}

bool IBehaviorManager::get_docking_port(IObjRW const*& OutPort, int& OutIndex)
{
    // 0x062d4b30, RET 8. Every failure path leaves OutPort / OutIndex untouched.
    if (CurrentBehaviorIndex != BEHAVIOR_DOCK || DockingPortFlag == 0 || DockingPortStatus != 1)
        return false;

    // [this+0x190] is BehaviorDispatchTable[7], the DockBehavior; retail does null-test it.
    DockBehavior const* const Dock = static_cast<DockBehavior const*>(BehaviorDispatchTable[7]);
    if (Dock == nullptr)
        return false;

    // Watcher324 (+0x324) minus 8 (the Watchable base inside IObjRW); DockPortIndex is +0x43c.
    Watchable const* const Watched = Dock->Watcher324.target();
    OutPort = Watched != nullptr ? static_cast<IObjRW const*>(Watched) : nullptr;
    OutIndex = Dock->DockPortIndex;
    return true;
}

unsigned IBehaviorManager::get_parent_id()
{
    // 0x062d4a10: tail call through Sys+0x1580 (SelfTarget.Iface) to get_id (+0x20). No null
    // test.
    return Sys->SelfTarget.Iface->get_id();
}

void IBehaviorManager::get_personality(pub::AI::Personality& OutPersonality)
{
    // 0x062d4430: REP MOVSD of 0xf6 dwords (0x3d8 bytes) from Sys+0x1584. No Sys test; the
    // ctor always builds it.
    OutPersonality = Sys->PersonalityData;
}

pub::AI::ScanResponse IBehaviorManager::get_scan_response(uint Type)
{
    // 0x062d2b00: tail-jump to FUN_062eeb70 on Sys+0x142c.
    return Sys->Targeting.GetScanResponse(Type);
}

const Vector IBehaviorManager::get_ship_up_direction()
{
    // 0x062d4370: this+0x134.
    return ShipUpDirection;
}

IStateGraph const* IBehaviorManager::get_state_graph()
{
    // ICF-folded @ 0x71AD0.
    return StateGraph;
}

SystemManager* IBehaviorManager::get_sys()
{
    // ICF-folded @ 0x466A0.
    return Sys;
}

bool IBehaviorManager::get_user_turning_input_state()
{
    // 0x062d43b0: byte at this+0x150.
    return UserTurningInputState != 0;
}

bool IBehaviorManager::lock_maneuvers(bool bLock)
{
    // 0x062d4b90: writes LockManeuversByte only; ManeuverMask (+0xb8) is untouched.
    LockManeuversByte = bLock;
    return true;
}

void IBehaviorManager::refresh_state_graph()
{
    // 0x062d2890: re-applies the graph by its id (the graph's first field), forced.
    if (StateGraph != nullptr)
    {
        int GraphId = *reinterpret_cast<int*>(StateGraph);
        set_state_graph(GraphId, true);
    }
}

void IBehaviorManager::reset_current_behavior_direction()
{
    // 0x062d3b10.
    BehaviorDirty = 1;
    // No -1 test here, unlike every sibling: with no current behavior retail reads the dword
    // at +0x170 (TickSuppressedFlag and its padding) as the entry. Only the loaded pointer is
    // null-tested.
    Behavior* const BehaviorObj = *(Behavior* const*)((char const*)BehaviorDispatchTable + CurrentBehaviorIndex * 4);
    if (BehaviorObj != nullptr)
    {
        Vector ZeroDir = {0.0f, 0.0f, 0.0f};
        BehaviorObj->set_direction(ZeroDir);
    }
    LastCommandedDirection.x = 0.0f;
    LastCommandedDirection.y = 0.0f;
    LastCommandedDirection.z = 0.0f;
}

void IBehaviorManager::set_camera_mode(IBehaviorManager::IBehaviorCameraMode Mode)
{
    // 0x062da4d0: this+0x154.
    CameraLevelStatus = static_cast<int>(Mode);
}

void IBehaviorManager::set_content_callback(pub::AI::ContentCallback* Callback)
{
    // 0x062d3ef0. get_creation_type is a __thiscall virtual at +0x24 with no stack arguments.
    ContentCbPtr = Callback;
    if (Callback != nullptr)
    {
        ContentCbCreationType = static_cast<unsigned>(Callback->get_creation_type());
    }
}

bool IBehaviorManager::set_current_directive(IDirectiveInfo const& Dir,
                                              pub::AI::BaseOp const* Op)
{
    // 0x062d3d70, RET 8. FUN_062d3420 is SetCurrentBehavior, FUN_062ec970 SetFollowLeader.
    // The three dwords go to +0xC4 first; every branch reads them back.
    std::memcpy(DirectiveStorage, &Dir, sizeof(DirectiveStorage));
    int* const Stored = reinterpret_cast<int*>(DirectiveStorage);
    int const* const Incoming = reinterpret_cast<int const*>(&Dir);

    if (Incoming[0] == 0)
    {
        // No directive: cancel the current behavior (no null test on the table entry), fall
        // back to DefaultBehaviorIndex, then drop the follow leader.
        Stored[2] = 0;
        if (CurrentBehaviorIndex != -1)
            BehaviorDispatchTable[CurrentBehaviorIndex]->cancel_notify();
        SetCurrentBehavior(static_cast<int>(DefaultBehaviorIndex), false, nullptr);
        Sys->FormationLeaders.SetFollowLeader(nullptr);
        return true;
    }

    // Switch straight to the behavior index the directive carries (Dir+8); the result is
    // discarded.
    if (Stored[1] == 1 && Incoming[0] == 1)
    {
        SetCurrentBehavior(Incoming[2], false, const_cast<pub::AI::BaseOp*>(Op));
        return true;
    }

    // No null test.
    for (int Index = 0; Index < 21; ++Index)
        BehaviorDispatchTable[Index]->cancel_notify();

    // A type-1 directive forces the behavior at +0xCC and returns SetCurrentBehavior's own
    // result.
    if (Stored[0] == 1)
        return SetCurrentBehavior(Stored[2], true, const_cast<pub::AI::BaseOp*>(Op));

    SetCurrentBehavior(static_cast<int>(DefaultBehaviorIndex), false, nullptr);
    return true;
}

void IBehaviorManager::set_directive_callback(pub::AI::DirectiveCallback* Callback)
{
    // 0x062d3eb0. With all 5 slots full and none matching, the callback is dropped.
    for (int I = 0; I < 5; ++I)
    {
        if (DirectiveCallbacks[I] == nullptr || DirectiveCallbacks[I] == Callback)
        {
            DirectiveCallbacks[I] = Callback;
            return;
        }
    }
}

void IBehaviorManager::clear_directive_callback(pub::AI::DirectiveCallback* Callback)
{
    // 0x062d3f10. Unlike set_directive_callback this does not stop at the first match; all 5
    // slots are scanned.
    for (int I = 0; I < 5; ++I)
    {
        if (DirectiveCallbacks[I] == Callback)
        {
            DirectiveCallbacks[I] = nullptr;
        }
    }
}

pub::AI::OP_RTYPE IBehaviorManager::set_directive_priority(
    pub::AI::DirectivePriority Priority)
{
    // 0x062d3e80.
    if (*reinterpret_cast<int*>(DirectiveStorage) != 0)
    {
        *reinterpret_cast<pub::AI::DirectivePriority*>(DirectiveStorage + 4) = Priority;
        return pub::AI::OpRTypeAccepted;
    }
    return pub::AI::OpRTypeNoDirective;
}

void IBehaviorManager::set_personality(pub::AI::Personality const& Personality)
{
    // 0x062d4400: 0x3d8 bytes to Sys+0x1584. No Sys test; the ctor always builds it.
    Sys->PersonalityData = Personality;
    // FUN_062e6950: rebuilds the weapon fire-control config, the eleven Personality-derived
    // timer thresholds and the RepairManager's helper thresholds.
    NotifySystemManagerPersonalityChanged(Sys);
}

void IBehaviorManager::set_ship_up_direction(Vector const& Dir)
{
    // 0x062d4350: this+0x134.
    ShipUpDirection = Dir;
}

void IBehaviorManager::set_state_graph(int GraphId, bool bForce)
{
    // 0x062d28b0.
    if (StateGraph != nullptr)
    {
        int CurrentGraphId = *reinterpret_cast<int*>(StateGraph);
        if (CurrentGraphId == GraphId && !bForce)
            return;
    }

    // The watched object (called with no null test): for the player the graph id becomes 0
    // and Flags1c8 bit 0 is cleared.
    int EffectiveGraphId = GraphId;
    IObjInspect const* const Self = ObjInspectOf(ObjRwFromWatchable(WatchablePtr));
    if (Self->is_player())
    {
        Flags1c8 &= 0xFE;
        EffectiveGraphId = 0;
    }

    // get_type (+0x88) writes into bForce's own stack slot; any type bit other than 0x200
    // clears Flags1c8 bit 0 too.
    unsigned int Type = static_cast<unsigned int>(bForce);   // the reused param_2 dword
    ObjInspectOf(ObjRwFromWatchable(WatchablePtr))->get_type(Type);
    if ((Type & OBJ_SOLAR_NO_WAYPOINT_MASK) != 0)
        Flags1c8 &= 0xFE;

    IStateGraph const* NewGraph = pub::StateGraph::get_state_graph_internal(EffectiveGraphId);
    if (NewGraph == StateGraph)
        return;
    StateGraph = const_cast<IStateGraph*>(NewGraph);
    if (NewGraph == nullptr)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s",
              "E:\\FL\\Scratch\\Source\\Common\\Ai\\IBehaviorManager.cpp", 0xce,
              "IBehaviorManager: state graph not found in database!");
        return;
    }

    IDirectiveInfo Dir;
    int const DirDwords[3] = { 0, 0, -1 };
    std::memcpy(Dir.Data, DirDwords, sizeof(DirDwords));
    set_current_directive(Dir, nullptr);
}

void IBehaviorManager::set_turn_sensitivity(float Sensitivity)
{
    // 0x062d4aa0: clamp to [0, 0.9] (unordered gives 0; 0.9f at DAT_0639c470), then store
    // 1.0 - clamped: the stored field is an inverted factor, read by
    // get_turn_sensitivity_factor. Retail's third (`<= 0` again) test is dead.
    TurnSensitivity = Sensitivity;
    if (!(TurnSensitivity > 0.0f)) {
        TurnSensitivity = 0.0f;
    } else if (!(TurnSensitivity < 0.9f)) {
        TurnSensitivity = 0.9f;
    }
    TurnSensitivity = 1.0f - TurnSensitivity;
}

float IBehaviorManager::get_turn_sensitivity_factor() const
{
    // 0x062d4b20: +0x164.
    return TurnSensitivity;
}

void IBehaviorManager::set_user_turning_input_state(bool bState)
{
    // 0x062d43a0: byte at this+0x150.
    UserTurningInputState = bState ? 1 : 0;
}

void IBehaviorManager::submit_camera_up(Vector const& Up)
{
    // 0x062d4330: this+0x140.
    CameraUp = Up;
}

void IBehaviorManager::update_current_behavior_afterburner(bool bOn)
{
    // 0x062d3c60: tail-jump to the current behavior's slot 0x40.
    if (CurrentBehaviorIndex == -1)
        return;
    Behavior* const Current = BehaviorDispatchTable[CurrentBehaviorIndex];
    if (Current == nullptr)
        return;
    BehaviorDirty = 1;
    Current->set_afterburner(bOn);
}

void IBehaviorManager::update_current_behavior_auto_avoidance(bool bOn)
{
    // 0x062d3d40: tail-jump to 06263bd0 on &Sys->Thrust. 06263bd0 is the ICF-folded byte
    // setter Ghidra names CNudgeEngine::Activate; on this receiver it writes
    // ThrustManager::bAutoAvoidance.
    Sys->Thrust.SetAutoAvoidance(bOn);
}

void IBehaviorManager::update_current_behavior_auto_level(bool bOn)
{
    // 0x062d3cf0. FUN_062e92d0 on &Sys->Thrust.
    Sys->Thrust.SetSteeringFallbackFlag(bOn ? 1 : 0);

    Vector ZeroDir = {0.0f, 0.0f, 0.0f};
    update_current_behavior_direction(ZeroDir);
}

void IBehaviorManager::update_current_behavior_brake_reverse(bool bOn)
{
    // 0x062d3cc0: the current behavior's slot 0x48.
    if (CurrentBehaviorIndex == -1)
        return;
    Behavior* const Current = BehaviorDispatchTable[CurrentBehaviorIndex];
    if (Current == nullptr)
        return;
    BehaviorDirty = 1;
    Current->brake_reverse(bOn);
}

void IBehaviorManager::update_current_behavior_cruise(bool bOn)
{
    // 0x062d3be0.
    if (CurrentBehaviorIndex == -1)
        return;
    Behavior* const Current = BehaviorDispatchTable[CurrentBehaviorIndex];
    if (Current == nullptr)
        return;

    // FUN_062de160; set_cruise is slot 0x38.
    if (Pathfind != nullptr && Pathfind->IsCruiseUnobstructed())
    {
        BehaviorDirty = 1;
        Current->set_cruise(bOn);
        return;
    }

    // Fallback through the watched owner, IObjAffect slot 0x6c(4, 0); no null test.
    ObjAffectOf(ObjRwFromWatchable(WatchablePtr))->slot_6c(4, 0);
}

void IBehaviorManager::update_current_behavior_direction(Vector const& Dir)
{
    // FUN_062d3690 (exported), RET 4.

    // x and y clamped to [-1, 1] against the doubles DAT_063a03f0 (-1.0) and DAT_06399410
    // (1.0), NaN kept. z is not clamped.
    Vector Clamped = Dir;
    if (Clamped.x < -1.0) Clamped.x = -1.0f;
    if (Clamped.x > 1.0) Clamped.x = 1.0f;
    if (Clamped.y < -1.0) Clamped.y = -1.0f;
    if (Clamped.y > 1.0) Clamped.y = 1.0f;

    // Unordered counts as not equal.
    bool const bIsZero = ((static_cast<double>(Clamped.z) * Clamped.z + static_cast<double>(Clamped.y) * Clamped.y) +
                          static_cast<double>(Clamped.x) * Clamped.x) == 0.0;
    UserTurningInputState = bIsZero ? 0 : 1;

    // DAT_063a033c (0x3f34fdf4, 0.707f) and DAT_063a0350 (0x3dcccccd, 0.1f).
    unsigned int const LevelThresholdBits = 0x3f34fdf4u;
    float LevelThreshold;
    std::memcpy(&LevelThreshold, &LevelThresholdBits, sizeof(LevelThreshold));
    unsigned int const BlendBits = 0x3dcccccdu;
    float Blend;
    std::memcpy(&Blend, &BlendBits, sizeof(Blend));

    if (CurrentBehaviorIndex != -1 && BehaviorDispatchTable[CurrentBehaviorIndex] != nullptr)
    {
        Behavior* const BehaviorObj = BehaviorDispatchTable[CurrentBehaviorIndex];
        BehaviorDirty = 1;   // stored before the CameraLevelStatus branch

        if (CameraLevelStatus != 1 || LevelCameraFlag == 0)
        {
            ShipUpDirFlag = 0;
            BehaviorObj->set_direction(Clamped);   // slot +0x30
        }
        else
        {
            // The watched ship (unguarded).
            IObjInspect const* const Ship = ObjInspectOf(ObjRwFromWatchable(WatchablePtr));
            Matrix const ShipRotation = Ship->get_transform().Rotation;

            Matrix Frame;
            Frame.E[0][0] = 1.0f;
            Frame.E[1][1] = 1.0f;
            Frame.E[2][2] = 1.0f;

            if (LevelCameraFlag != 0)
            {
                // Retail takes a second get_transform copy.
                Matrix const ShipRotation2 = Ship->get_transform().Rotation;
                if (bIsZero)
                {
                    // FUN_062d3af0 + FUN_062c47a0, unrounded; skips unless the threshold is
                    // strictly greater.
                    Vector const WorldUp{ 0.0f, 1.0f, 0.0f };
                    Vector const Column2 = MatrixColumn2(ShipRotation2);
                    double const DotUp = (static_cast<double>(Column2.z) * WorldUp.z +
                                          static_cast<double>(Column2.y) * WorldUp.y) +
                                         static_cast<double>(Column2.x) * WorldUp.x;
                    if (LevelThreshold > std::fabs(DotUp))
                    {
                        // The same re-level RelevelShipUpDirection does. Retail builds the
                        // (0, 1, 0) temporary in Frame's first three floats, so they are
                        // clobbered too.
                        LevelCameraFlag = 0;
                        if (Sys->Thrust.CheckSteeringGate())
                        {
                            Frame.E[0][0] = 0.0f;
                            Frame.E[0][1] = 1.0f;
                            Frame.E[0][2] = 0.0f;
                            ShipUpDirection = Vector{ 0.0f, 1.0f, 0.0f };
                            ShipUpDirFlag = 0;
                        }
                        LevelCameraFlag = 1;
                    }
                }
            }
            // With the stick deflected, the up hint is the camera's up (+0x140).
            if (!bIsZero)
                ShipUpDirection = CameraUp;

            // Threshold = (float)(0.1f + 0.707f).
            Vector const Forward{ -ShipRotation.E[0][2], -ShipRotation.E[1][2], -ShipRotation.E[2][2] };
            Vector const UpHint = ShipUpDirection;
            float const FrameThreshold = static_cast<float>(static_cast<double>(Blend) + LevelThreshold);
            Vector Steer;
            if (BuildPerpendicularFrame(Forward, &Frame, FrameThreshold, &UpHint))
            {
                // ShipRotation^T applied to Frame's columns 0 and 1 (engine slot +0x38).
                I3DMathEngine* const Math = Acquire3DMathEngine();
                Vector const Column0{ Frame.E[0][0], Frame.E[1][0], Frame.E[2][0] };
                Vector Local0;
                Math->TransposeTransform(Local0, ShipRotation, Column0);
                Vector const Column1{ Frame.E[0][1], Frame.E[1][1], Frame.E[2][1] };
                Vector Local1;
                Math->TransposeTransform(Local1, ShipRotation, Column1);

                // Retail also computes z = L1.z + L0.z and then overwrites it with the
                // clamped z.
                Steer.x = static_cast<float>(static_cast<double>(Local0.x) * Clamped.x -
                                             static_cast<double>(Local1.x) * Clamped.y);
                Steer.y = static_cast<float>(static_cast<double>(Local1.y) * Clamped.y -
                                             static_cast<double>(Local0.y) * Clamped.x);
                Steer.z = Clamped.z;
            }
            else
            {
                Steer = Vector{ 0.0f, 0.0f, 0.0f };
            }
            BehaviorObj->set_direction(Steer);   // slot +0x30
        }
    }

    // Always stored, even on early-out.
    LastCommandedDirection = Clamped;
}

void IBehaviorManager::update_current_behavior_engage_engine(bool bOn)
{
    // 0x062d3bb0: the current behavior's slot 0x3c. Unlike every sibling
    // update_current_behavior_* function, retail does not set BehaviorDirty here.
    if (CurrentBehaviorIndex == -1)
        return;
    Behavior* const Current = BehaviorDispatchTable[CurrentBehaviorIndex];
    if (Current == nullptr)
        return;
    Current->engage_engine(bOn);
}

void IBehaviorManager::update_current_behavior_slide_strafe_burst(StrafeDir Dir)
{
    // 0x062d3c90: the current behavior's slot 0x44.
    if (CurrentBehaviorIndex == -1)
        return;
    Behavior* const Current = BehaviorDispatchTable[CurrentBehaviorIndex];
    if (Current == nullptr)
        return;
    BehaviorDirty = 1;
    Current->slide_strafe_burst(Dir);
}

void IBehaviorManager::update_current_behavior_throttle(float Throttle)
{
    // 0x062d3b80: the current behavior's slot 0x34.
    if (CurrentBehaviorIndex == -1)
        return;
    Behavior* const Current = BehaviorDispatchTable[CurrentBehaviorIndex];
    if (Current == nullptr)
        return;
    BehaviorDirty = 1;
    Current->set_throttle(Throttle);
}

void IBehaviorManager::update_level_camera(bool bEnable)
{
    // 0x062d3d60: byte at this+0xF9.
    LevelCameraFlag = bEnable ? 1 : 0;
}

// ============================================================================
// IBehaviorManager — notification / delayed-callback queue
// ============================================================================
//
// See NotificationNode (common_iface.h) for the layout. The queue's alloc/link/unlink
// primitives, the per-tick expiry walk, and the two delayed-op kinds
// (QueueNotification/InsertNotification) a node's Mode selects.

void IBehaviorManager::InitNotificationQueue()
{
    // A self-referencing sentinel node, as FUN_062d4cf0(0, 0) produces. The teardown loops
    // (FUN_062d4bf0/FUN_062d4c90) free() it at the end, so it is a separate heap node, not data
    // inline at +0x158. Head is always dereferenceable and self-pointing when empty.
    NotificationQueueHead = AllocNotificationNode(nullptr, nullptr);
    NotificationCount = 0;
}

NotificationNode* IBehaviorManager::AllocNotificationNode(
    NotificationNode* NextIfAny, NotificationNode* PrevIfAny)
{
    // FUN_062d4cf0: raw operator_new(0x24). Retail leaves the payload uninitialized for the
    // caller to fill; placement-new keeps BaseWatcher's null invariant, which is behaviorally
    // identical. malloc so the teardown can use free() (FUN_06391d5a is a free() wrapper).
    void* Mem = malloc(sizeof(NotificationNode));
    NotificationNode* Node = new (Mem) NotificationNode();
    Node->Next = (NextIfAny != nullptr) ? NextIfAny : Node;
    Node->Prev = (PrevIfAny != nullptr) ? PrevIfAny : Node;
    return Node;
}

NotificationNode* IBehaviorManager::InsertNotificationNodeBefore(NotificationNode* Anchor)
{
    // FUN_062d4cf0 plus its caller-side linking, which retail inlines in FUN_062d4520 and
    // spells out after a FUN_062d4cf0 call in FUN_062d4750: insert immediately before Anchor.
    NotificationNode* OldPrev = Anchor->Prev;
    NotificationNode* Node = AllocNotificationNode(Anchor, OldPrev);
    Anchor->Prev = Node;
    OldPrev->Next = Node;
    ++NotificationCount;
    return Node;
}

void IBehaviorManager::ReleaseNotificationWatchers(NotificationNode* Node)
{
    // FUN_062d46f0: set_pointer(nullptr) on each non-null watcher, via the public set().
    // Reverse member order, as retail's inlined payload dtor: WatcherB (node+0x14) first, then
    // WatcherA (node+0xC).
    Node->WatcherB.set(nullptr);
    Node->WatcherA.set(nullptr);
}

void IBehaviorManager::ClearNotificationQueue()
{
    // FUN_062d4bf0 and FUN_062d4c90 are the same full-queue teardown, differing only in how
    // each retires a node's watchers (FUN_062d46f0 vs ~BaseWatcher, both set(nullptr)), so they
    // collapse to one body.
    NotificationNode* Sentinel = NotificationQueueHead;
    NotificationNode* Cur = Sentinel->Next;
    while (Cur != Sentinel)
    {
        NotificationNode* Next = Cur->Next;
        Cur->Prev->Next = Cur->Next;
        Cur->Next->Prev = Cur->Prev;
        ReleaseNotificationWatchers(Cur);
        free(Cur); // FUN_06391d5a
        --NotificationCount;
        Cur = Next;
    }
    free(Sentinel); // FUN_06391d5a
    NotificationQueueHead = nullptr;
    NotificationCount = 0;
}

// FUN_062d4c50 (std::list::erase) has no body of its own here: its only caller is
// ~IBehaviorManager's teardown loop, and the loop body above is that erase.

void IBehaviorManager::UpdateNotificationQueue(float Dt)
{
    // FUN_062d4450. Walks every real node once, decrementing its Timer by Dt; an expired node
    // fires the op its Mode selected, then is unlinked and freed. The sentinel is dereferenced
    // unconditionally.
    NotificationNode* Cur = NotificationQueueHead->Next;
    while (Cur != NotificationQueueHead)
    {
        // Positive or unordered timers stay queued; < 0 and == 0 fire.
        Cur->Timer -= Dt;
        if (Cur->Timer > 0.0f || Cur->Timer != Cur->Timer)
        {
            Cur = Cur->Next;
            continue;
        }

        // Expired: pass the raw IObjRW (Watched - 8) and Extra as Extra with Duration 0,
        // which in both callees is the immediate path, never another queue insert.
        if (Cur->Mode == 0)
        {
            Watchable const* WatchedA = Cur->WatcherA.target();
            IObjRW* RawA = (WatchedA != nullptr)
                ? reinterpret_cast<IObjRW*>(const_cast<char*>(
                      reinterpret_cast<char const*>(WatchedA) - 8))
                : nullptr;
            QueueNotification(RawA, Cur->Extra, 0.0f);
        }
        else if (Cur->Mode == 1)
        {
            Watchable const* WatchedB = Cur->WatcherB.target();
            IObjRW* RawB = (WatchedB != nullptr)
                ? reinterpret_cast<IObjRW*>(const_cast<char*>(
                      reinterpret_cast<char const*>(WatchedB) - 8))
                : nullptr;
            InsertNotification(RawB, Cur->Extra, 0.0f);
        }
        // Mode >= 2: the node is simply retired.

        // The successor is read after the dispatch, not before.
        NotificationNode* Next = Cur->Next;
        Cur->Prev->Next = Cur->Next;
        Cur->Next->Prev = Cur->Prev;
        ReleaseNotificationWatchers(Cur);
        free(Cur); // FUN_06391d5a
        --NotificationCount;
        Cur = Next;
    }
}

void IBehaviorManager::QueueNotification(IObjRW* Target, unsigned Extra, float Duration)
{
    // FUN_062d4520: notify Target's formation followers, now or after Duration seconds.
    // Duration <= 0 or unordered is the immediate fan-out; only Duration > 0 inserts a node,
    // which re-enters here with Duration 0 once its timer runs out (Mode 0). Inverting this
    // gate makes a zero-timer node re-insert itself forever.
    if (Duration > 0.0f)
    {
        // Retail builds the payload in on-stack watchers, inlines operator_new(0x24) and copies
        // it with FUN_062d4d60.
        NotificationNode* Node = InsertNotificationNodeBefore(NotificationQueueHead);
        Node->Mode = 0;
        Node->WatcherA.set((Target != nullptr)
            ? reinterpret_cast<Watchable const*>(reinterpret_cast<char const*>(Target) + 8)
            : nullptr);
        Node->WatcherB.set(nullptr);
        Node->Extra = Extra;
        Node->Timer = Duration;
        return;
    }

    if (Target == nullptr)
        return;
    // IObjInspect +0x14c get_formation_followers.
    IObjRW* FollowerBuf[16];
    int const Count = ObjInspectOf(Target)->get_formation_followers(FollowerBuf, 16);
    // Duration 0: each follower's own manager runs the Extra switch immediately.
    for (int I = 0; I < Count; ++I)
    {
        if (FollowerBuf[I] != nullptr)
            InsertNotification(FollowerBuf[I], Extra, 0.0f);
    }
}

void IBehaviorManager::InsertNotification(IObjRW* Target, unsigned Extra, float Duration)
{
    // FUN_062d4750. Same dual-mode gate as QueueNotification. The immediate path resolves
    // Target's owner (cobject, +0x150) to a CEqObj, fetches its IBehaviorManager and switches
    // on Extra; Duration > 0 inserts a Mode 1 node that re-enters here with Duration 0.
    if (Duration > 0.0f)
    {
        NotificationNode* Node = InsertNotificationNodeBefore(NotificationQueueHead);
        Node->Mode = 1;
        // WatcherA is this manager's own WatchablePtr (+0xf0), checked against the literal-8
        // sentinel (`W != nullptr && (W - 8) != nullptr`).
        Watchable const* SelfWatched = reinterpret_cast<Watchable const*>(WatchablePtr);
        bool SelfValid = (SelfWatched != nullptr)
            && (reinterpret_cast<char const*>(SelfWatched) - 8 != nullptr);
        Node->WatcherA.set(SelfValid ? SelfWatched : nullptr);
        Node->WatcherB.set((Target != nullptr)
            ? reinterpret_cast<Watchable const*>(reinterpret_cast<char const*>(Target) + 8)
            : nullptr);
        Node->Extra = Extra;
        Node->Timer = Duration;
        return;
    }

    if (Target == nullptr)
        return;
    CObject const* Owner = ObjInspectOf(Target)->cobject();
    if (Owner == nullptr)
        return;
    // ObjectClass & 0x103 == 0x103 -> CEqObj-derived.
    CObject* OwnerObj = const_cast<CObject*>(Owner);
    if ((static_cast<unsigned>(OwnerObj->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        return;
    IBehaviorManager* Mgr = static_cast<CEqObj*>(OwnerObj)->get_behavior_interface();
    if (Mgr == nullptr)
        return;

    switch (Extra)
    {
    case 0:
        // Only when the target manager's current behavior is 10: [Mgr+0x19c] is
        // BehaviorDispatchTable[10] (index constant-folded) and slot +0x1c is OnUpdate. Falls
        // through into case 1 as retail does.
        if (Mgr->CurrentBehaviorIndex == BEHAVIOR_FORMATION && Mgr->BehaviorDispatchTable[0xa] != nullptr)
        {
            Mgr->BehaviorDispatchTable[0xa]->OnUpdate();
        }
        [[fallthrough]];
    case 1:
        // FUN_062ecc50 on Sys+0x1364: the record's +0xc3 byte.
        Mgr->Sys->FormationLeaders.SetFlagC3(0);
        break;
    case 2:
        Mgr->Sys->FormationLeaders.SetFlagC3(1);
        break;
    case 3:
        {
            // Re-enter QueueNotification on Mgr with its own object (no literal-8 sentinel
            // re-check on this path).
            Watchable const* SelfWatched = reinterpret_cast<Watchable const*>(Mgr->WatchablePtr);
            IObjRW* Raw = (SelfWatched != nullptr)
                ? reinterpret_cast<IObjRW*>(const_cast<char*>(
                      reinterpret_cast<char const*>(SelfWatched) - 8))
                : nullptr;
            Mgr->QueueNotification(Raw, 1, 0.0f);
        }
        break;
    default:
        break;
    }
}

void IBehaviorManager::UpdateBehaviorTick(float Dt)
{
    // FUN_062d2b10.
    if (TickSuppressedFlag != 0)
        return;
    if (WatchablePtr == nullptr)
        return;
    if (reinterpret_cast<char*>(WatchablePtr) - 8 == nullptr)
        return;
    // Sys is dereferenced unconditionally past this point, as in retail: the ctor always
    // builds it and only the dtor clears it.

    // Sys->Timers+0x1d is SystemManagerTimerTable::bKeepCache, the AI target list's "return
    // the cached list" flag. Held set for the tick body (retail clears it at both exits via
    // FUN_062d3410), so the list is built at most once per tick.
    Sys->Timers.bKeepCache = 1;
    FTimerTableKeepCacheScope const KeepCacheScope(&Sys->Timers);

    UpdateNotificationQueue(Dt);

    if (StateGraph == nullptr)
    {
        return;
    }

    // FUN_062e66c0, then FUN_062ec6b0 on Sys+0x1364.
    UpdateSystemManager(Sys, Dt);
    Sys->FormationLeaders.SyncFormationLeader();

    // No null test, as in retail: UpdateSystemManager has just re-resolved the iface from the
    // live watched object.
    bool const ExternalIfaceFlag = Sys->SelfTarget.Iface->is_player();

    // Flags1c8 bit 0 gates both the attack-override timer and the target branch after it.
    if ((Flags1c8 & 1) != 0)
    {
        // The sum is stored rounded but compared unrounded; fmod runs on the rounded value.
        // Only a strictly greater sum wraps and fires FUN_062ed780 (Sys+0x142C).
        double const SumD = static_cast<double>(Dt) + static_cast<double>(AttackOverrideTimer);
        AttackOverrideTimer = static_cast<float>(SumD);
        if (SumD > static_cast<double>(AttackOverridePeriod))
        {
            AttackOverrideTimer = static_cast<float>(std::fmod(static_cast<double>(AttackOverrideTimer),
                                                               static_cast<double>(AttackOverridePeriod)));
            Sys->Targeting.UpdateAttackTargetOverride();
        }

        // FUN_062ed5a0. Null skips the rest without clearing InSightsTargetId.
        IObjRW* const Target = Sys->Targeting.GetTarget();
        if (Target != nullptr)
        {
            // FUN_062ed5e0's result and Dt are pushed to FUN_062eb670, which reads only the
            // Target argument.
            (void)Sys->Targeting.GetSubTarget();
            Sys->GunGroups.RunFireControlTick(Target);

            // Report the target to the content callback once it is in sights: FUN_062e5190
            // (-0.7f) and FUN_062e4500 (600.0f at DAT_063a034c). Any failed test zeroes +0xE8.
            FAiTarget* const SelfRecord = &Sys->SelfTarget;
            bool bInSights = false;
            if (ContentCbPtr != nullptr
                && !Sys->SelfTarget.Iface->is_player()
                && IsAlignedAndOutsideHeadingCone(SelfRecord, Target, -0.7f)
                && IsWithinInterceptRangeOfIface(SelfRecord, Target, 600.0f))
            {
                bInSights = true;
            }

            if (bInSights)
            {
                // get_id is called twice, as retail; ship_in_sights_event is ContentCallback
                // slot +0x0C.
                if (InSightsTargetId != Target->get_id())
                {
                    unsigned int const TargetId = Target->get_id();
                    InSightsTargetId = TargetId;
                    ContentCbPtr->ship_in_sights_event(TargetId);
                }
            }
            else
            {
                InSightsTargetId = 0;
            }
        }
    }

    // Non-player ships run the auto-turret scan and fire (FUN_062e9c50, FUN_062ea050).
    if (!ExternalIfaceFlag)
    {
        Sys->GunGroups.ScanAutoTurretTargets(Dt);
        Sys->GunGroups.FireAutoTurrets();
    }

    // FUN_062dd760.
    Pathfind->Update(Dt);

    // The watched object is re-read with no null test (the null arm reads address 0, as in
    // retail). For the player a nonzero LastCommandedDirection is re-dispatched; NaN still
    // dispatches.
    if (ObjRwFromWatchable(WatchablePtr)->is_player())
    {
        // Unrounded for the compare.
        double const MagSq = static_cast<double>(LastCommandedDirection.x) * LastCommandedDirection.x
                           + static_cast<double>(LastCommandedDirection.y) * LastCommandedDirection.y
                           + static_cast<double>(LastCommandedDirection.z) * LastCommandedDirection.z;
        if (MagSq != 0.0)
            update_current_behavior_direction(LastCommandedDirection);
    }

    // Scan-state reconciliation: Sys+0x1434 is Targeting.AttackState. On a change the flag is
    // stored in +0xF8 and reported through scan_state_change (ContentCallback slot +0x18).
    if (ContentCbPtr != nullptr)
    {
        int const* DirDwords = reinterpret_cast<int const*>(DirectiveStorage);
        bool NewContentFlag = (Sys->Targeting.AttackState == TargetManager::kAttackStateCombatOver) && (CurrentBehaviorIndex == BEHAVIOR_GOTO)
                             && (DirDwords[0] == 0 || DirDwords[1] == 1);
        if (NewContentFlag != (bScanState != 0))
        {
            bScanState = NewContentFlag ? 1 : 0;
            ContentCbPtr->scan_state_change(NewContentFlag);
        }
    }

    // ========================================================================
    // The behavior dispatch tail. Behavior slots used: OnEvaluateReadiness +0x14, GetStatus
    // +0x24, allow_user_steering +0x2c, GetDockTarget +0x68. FUN_062d3420 is
    // SetCurrentBehavior.
    // ========================================================================

    // BehaviorTimerA/B/C accumulators (see common_iface.h). A's sum is stored first, then
    // wrapped with fmod once it reaches 3.0f (DAT_063a0340). The new C is kept in a local for
    // OnEvaluateReadiness. Unordered does not wrap; fmod runs on the rounded value.
    bool bTimerAWrapped = false;
    bool bReevaluate = false;
    float const NewTimerC = Dt + BehaviorTimerC;
    {
        double const NewTimerAExt = static_cast<double>(Dt) + BehaviorTimerA;
        float const NewTimerA = static_cast<float>(NewTimerAExt);
        BehaviorTimerA = NewTimerA;
        BehaviorTimerB = Dt + BehaviorTimerB;
        BehaviorTimerC = NewTimerC;
        if (NewTimerAExt >= 3.0)
        {
            BehaviorTimerA = static_cast<float>(std::fmod(static_cast<double>(NewTimerA), 3.0));
            bTimerAWrapped = true;
            bReevaluate = true;
        }
    }

    // Retail keeps this in the dead Dt parameter slot.
    int* const DirDwords = reinterpret_cast<int*>(DirectiveStorage);
    bool const bDirectiveArmed = (DirDwords[0] != 0) && (DirDwords[1] == 0);

    // Behavior 6 always resets BehaviorTimerA and counts as a wrap.
    bool bCurrentIsIndex6 = false;
    if (CurrentBehaviorIndex == BEHAVIOR_IDLE)
    {
        bTimerAWrapped = true;
        BehaviorTimerA = 0.0f;
        bReevaluate = true;
        bCurrentIsIndex6 = true;
    }

    // Both guards exit through KeepCacheScope.
    if (CurrentBehaviorIndex == -1)
    {
        return;
    }
    Behavior* CurrentBehavior = BehaviorDispatchTable[CurrentBehaviorIndex];
    if (CurrentBehavior == nullptr)
    {
        return;
    }

    // On a timer wrap, poll the active behavior. A result of 4 means "this behavior is done":
    // back to DefaultBehaviorIndex plus a DirectiveCallbacks[] fan-out.
    if (bTimerAWrapped)
    {
        int const Polled = CurrentBehavior->OnEvaluateReadiness(NewTimerC);

        // An armed directive suppresses the wrap-driven re-evaluation.
        if (!bCurrentIsIndex6 && bDirectiveArmed)
            bReevaluate = false;

        if (Polled == 4)
        {
            // Re-read (and re-tested against -1) because OnEvaluateReadiness may have changed it.
            int const FinishedIndex = CurrentBehaviorIndex;
            PendingDirectiveResult = 2;
            PendingCompletedBehavior = FinishedIndex;
            if (FinishedIndex != -1)
            {
                DirDwords[0] = 0;
                SetCurrentBehavior(static_cast<int>(DefaultBehaviorIndex), true, nullptr);
                NotifyDirectiveCallbacks();
            }
            PendingCompletedBehavior = -1;
            return;
        }
    }

    // The active behavior's status: 2 and 3 force a re-evaluation, 0 forbids one, anything
    // else leaves the wrap's verdict alone.
    int const BehaviorStatus = CurrentBehavior->GetStatus();
    if (BehaviorStatus == kBehaviorUpdateFinished || BehaviorStatus == kBehaviorUpdateAborted)
        bReevaluate = true;
    if (BehaviorStatus == kBehaviorUpdateBusy)
        bReevaluate = false;

    // Two AI-target-list probes on Sys->Timers, each forcing a re-evaluation unless the
    // behavior they would switch to is already current. Skipped while a directive is armed.
    if (!bDirectiveArmed)
    {
        if (Sys->Timers.IsTimer3Active() && CurrentBehaviorIndex != BEHAVIOR_FACE)   // FUN_062e8830
            bReevaluate = true;
        if (SystemManagerTimerTableHasMissileReactionTarget(&Sys->Timers) && CurrentBehaviorIndex != BEHAVIOR_EVADE)
            bReevaluate = true;
    }

    // The rest is gated on the watched object being the player. Retail re-derives
    // WatchablePtr - 8 and calls is_player with no null test (the null arm reads address 0).
    IObjRW* const WatchedObj = ObjRwFromWatchable(WatchablePtr);
    if (WatchedObj->is_player())
    {
        // On the player's own ship a status 2 or 3 behavior is abandoned right away.
        if (BehaviorStatus == kBehaviorUpdateFinished || BehaviorStatus == kBehaviorUpdateAborted)
            SetCurrentBehavior(static_cast<int>(DefaultBehaviorIndex), true, nullptr);

        // Defaults, overwritten below.
        bAllowUserSteering = 1;
        DockingPortFlag = 0;
        DockingPortStatus = 0;
        bReevaluate = false;

        // Behavior 19 has no docking-probe handling.
        if (CurrentBehaviorIndex != -1 && CurrentBehaviorIndex != BEHAVIOR_FREE_FLIGHT)
        {
            // The entry is called (+0x2c) with no null test.
            Behavior* const Active = BehaviorDispatchTable[CurrentBehaviorIndex];
            {
                bAllowUserSteering = Active->allow_user_steering() ? 1 : 0;
                if (BehaviorStatus == kBehaviorUpdateBusy || LockManeuversByte != 0)
                    DockingPortFlag = 1;

                // Resolve the docking target; the source depends on the running behavior.
                IObjRW* Target = nullptr;
                bool bProximityGated = false;
                if (CurrentBehaviorIndex == BEHAVIOR_DOCK)
                {
                    Target = static_cast<IObjRW*>(Active->GetDockTarget());
                }
                else if (CurrentBehaviorIndex == BEHAVIOR_FORMATION)
                {
                    // FUN_062ecaf0 on Sys+0x1364 returns the leader, already -8 adjusted.
                    bProximityGated = true;
                    IObjRW* const Partner =
                        Sys->FormationLeaders.GetCurrentLeader();
                    if (Partner != nullptr)
                    {
                        if (SinglePlayer())
                        {
                            // Single player reaches through the partner ship's own behavior
                            // manager and asks its dock behavior ([+0x190], table[7]).
                            CObject const* const PartnerObj = ObjInspectOf(Partner)->cobject();
                            if (PartnerObj != nullptr
                                && (static_cast<unsigned>(PartnerObj->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                            {
                                IBehaviorManager* const PartnerMgr =
                                    static_cast<CEqObj*>(const_cast<CObject*>(PartnerObj))->get_behavior_interface();
                                // The manager is null-tested, its table[7] is not.
                                if (PartnerMgr != nullptr && PartnerMgr->CurrentBehaviorIndex == BEHAVIOR_DOCK)
                                {
                                    Target = static_cast<IObjRW*>(
                                        PartnerMgr->BehaviorDispatchTable[7]->GetDockTarget());
                                }
                            }
                        }
                        else
                        {
                            // Multiplayer asks the partner for its own current target.
                            if (ObjInspectOf(Partner)->get_behavior_id() == BEHAVIOR_DOCK)
                                ObjInspectOf(Partner)->get_target(Target);
                        }
                    }
                }

                if (Target != nullptr)
                {
                    unsigned TargetType = 0;
                    ObjInspectOf(Target)->get_type(TargetType);

                    // The leader path also requires the target within 1000 units (squared
                    // 1.0e6f at DAT_063a03ec); strict `<`, unordered fails.
                    bool bInRange = true;
                    if (bProximityGated)
                    {
                        // Both get_position calls in this order; the sum stays on the x87 stack.
                        Vector const& SelfPos = ObjRwFromWatchable(WatchablePtr)->get_position();
                        Vector const& TgtPos = ObjInspectOf(Target)->get_position();
                        double const Dx = static_cast<double>(TgtPos.x) - SelfPos.x;
                        double const Dy = static_cast<double>(TgtPos.y) - SelfPos.y;
                        double const Dz = static_cast<double>(TgtPos.z) - SelfPos.z;
                        bInRange = ((Dz * Dz + Dy * Dy) + Dx * Dx) < 1000000.0f;
                    }

                    if (bInRange)
                    {
                        // Bit 7 of the target's type word picks status 2 over 1.
                        DockingPortStatus = ((TargetType & OBJ_TRADELANE_RING) != 0) ? 2 : 1;
                    }
                }
            }
        }
    }

    // A locked-maneuvers manager never re-evaluates.
    if (LockManeuversByte != 0)
        bReevaluate = false;

    if (bReevaluate)
    {
        // --------------------------------------------------------------------
        // Re-evaluation: score all 21 candidates, then pick one -- deterministically if any
        // scored exactly 1.0, otherwise by a weighted random draw over the prefix sums. The
        // state graph's transition matrix at StateGraph+0x48 is 21x21.
        // --------------------------------------------------------------------
        int ScoreCount = 0;
        bool bFoundUnityWeight = false;
        for (int Index = 0; Index < 21; ++Index)
        {
            Behavior* const Candidate = BehaviorDispatchTable[Index];

            // Retail re-reads StateGraph and CurrentBehaviorIndex every iteration;
            // OnEvaluateReadiness can change either.
            float const* const TransitionWeights =
                reinterpret_cast<float const*>(reinterpret_cast<char const*>(StateGraph) + 0x48);
            float Weight = TransitionWeights[CurrentBehaviorIndex * 21 + Index];

            // An armed directive naming this behavior gets a token weight of 0.001f: enough to
            // stay in the running, not enough to beat a real transition.
            if (DirDwords[0] == 1 && DirDwords[2] == Index)
                Weight = 0.001f;

            // A zero-weight transition is not allowed; the candidate is not polled.
            if (Weight == 0.0)
                continue;

            // Retail polls without a null check. Verdict 0 means "definitely me" (1.0f), 2..4
            // "definitely not me" (0.0f, rejected below); anything else keeps the weight.
            int const Verdict = Candidate->OnEvaluateReadiness(BehaviorTimerC);
            float Value;
            if (Verdict == 0)
                Value = 1.0f;
            else if (Verdict >= 2 && Verdict <= 4)
                Value = 0.0f;
            else
                Value = Weight;

            // Strictly positive only.
            if (!(Value > 0.0))
                continue;

            BehaviorScores[ScoreCount].Weight = Value;
            BehaviorScores[ScoreCount].BehaviorIndex = Index;
            if (Value == 1.0)
                bFoundUnityWeight = true;
            ++ScoreCount;
        }

        if (ScoreCount == 0)
        {
            // Nothing is eligible: retire any armed directive and fall back to
            // DefaultBehaviorIndex unless it is already current.
            if (DirDwords[0] == 1)
            {
                PendingDirectiveResult = 2;
                PendingCompletedBehavior = DirDwords[2];
                DirDwords[0] = 0;
            }
            if (CurrentBehaviorIndex != static_cast<int>(DefaultBehaviorIndex))
                SetCurrentBehavior(static_cast<int>(DefaultBehaviorIndex), false, nullptr);
        }
        else
        {
            int Chosen = -1;
            if (bFoundUnityWeight)
            {
                // The draw is skipped and the last entry scoring exactly 1.0 wins.
                for (int Index = 0; Index < ScoreCount; ++Index)
                {
                    if (BehaviorScores[Index].Weight == 1.0)
                        Chosen = BehaviorScores[Index].BehaviorIndex;
                }
                SetCurrentBehavior(Chosen, false, nullptr);
            }
            else
            {
                // Prefix sums in place, then draw. DAT_063997d0 = 1/32768, DAT_0639b984 =
                // 1.0e-5f, DAT_0639c3f0 = the double 1.0e-5. The running total stays
                // unrounded while each prefix sum is stored as a float; the total is rounded
                // once.
                double Total = 0.0;
                for (int Index = 0; Index < ScoreCount; ++Index)
                {
                    float const Own = BehaviorScores[Index].Weight;
                    BehaviorScores[Index].Weight = static_cast<float>(Total + static_cast<double>(Own));
                    Total = static_cast<double>(Own) + Total;
                }
                float const TotalF = static_cast<float>(Total);

                // One rounding, at the end.
                double const Unit = static_cast<double>(rand()) * static_cast<double>(1.0f / 32768.0f);
                float const Pick = static_cast<float>(
                    (static_cast<double>(TotalF) - static_cast<double>(1.0e-5f)) * Unit + 1.0e-5);

                // Walk to the first prefix sum strictly greater than the draw, bounded by the
                // table size 21 like retail, not by ScoreCount: stale entries past ScoreCount
                // are read, and falling off the end reads the dword at
                // BehaviorScores+21*8+4 (+0xBC).
                int Picked = 0;
                while (Picked < 21 && !(BehaviorScores[Picked].Weight > Pick))
                    ++Picked;
                Chosen = *reinterpret_cast<int const*>(
                    reinterpret_cast<char const*>(BehaviorScores) + Picked * 8 + 4);

                // .text:062d32f4: a chosen index of -1 hits INT3 (an assertion); execution past
                // it still makes the SetCurrentBehavior call with the -1.
                if (Chosen == -1)
                    __debugbreak();
                SetCurrentBehavior(Chosen, false, nullptr);
            }
        }
    }

    // Hand the accumulated BehaviorTimerB to the active behavior every 0.2s (DAT_063a0344),
    // or at once when BehaviorDirty is set or this tick re-evaluated. Retail reads the table
    // entry with no range or null test.
    Behavior* const TickBehavior = BehaviorDispatchTable[CurrentBehaviorIndex];
    {
        if (BehaviorTimerB >= 0.2f || BehaviorDirty != 0 || bReevaluate)
        {
            TickBehavior->DeltaTime = BehaviorTimerB;
            TickBehavior->OnUpdate();
            BehaviorTimerB = 0.0f;
            BehaviorDirty = 0;
        }

        // Unconditional end-of-tick call (the base body for this slot is a bare RET).
        TickBehavior->PostUpdate();
    }

    // A pending directive result whose behavior is still running, or is 8, completes now.
    if (PendingCompletedBehavior != -1 && (PendingCompletedBehavior == CurrentBehaviorIndex || PendingCompletedBehavior == BEHAVIOR_LAUNCH))
    {
        DirDwords[0] = 0;
        SetCurrentBehavior(static_cast<int>(DefaultBehaviorIndex), true, nullptr);
        NotifyDirectiveCallbacks();
    }
    PendingCompletedBehavior = -1;

    // KeepCacheScope's destructor releases the keep-cache flag on the way out.
}

void IBehaviorManager::NotifyDirectiveCallbacks()
{
    // The DirectiveCallbacks[5] fan-out UpdateBehaviorTick runs at both transition-completed
    // sites (two identical unrolled loops in retail). DirectiveCallback slot 0
    // (OnDirectiveComplete) gets PendingCompletedBehavior (+0x11c) first and
    // PendingDirectiveResult (+0x118) second.
    for (int Index = 0; Index < 5; ++Index)
    {
        pub::AI::DirectiveCallback* const Cb = DirectiveCallbacks[Index];
        if (Cb != nullptr)
            Cb->OnDirectiveComplete(PendingCompletedBehavior, PendingDirectiveResult);
    }
}

// FUN_062d29a0, RET 8.
void IBehaviorManager::SetDirectiveResult(int CompletedBehavior, int DirectiveResult)
{
    PendingDirectiveResult = DirectiveResult;
    PendingCompletedBehavior = CompletedBehavior;
}

// FUN_062d35c0.
void IBehaviorManager::RelevelShipUpDirection(bool bApply)
{
    if (LevelCameraFlag == 0)
        return;

    // The watched ship (unguarded); its rotation is copied before bApply is tested.
    Matrix const Rotation = ObjInspectOf(ObjRwFromWatchable(WatchablePtr))->get_transform().Rotation;
    if (!bApply)
        return;

    // The forward axis's y, compiled as a dot product with (0, 1, 0), unrounded; returns
    // unless the threshold is strictly greater.
    double const ForwardY =
        std::fabs((static_cast<double>(Rotation.E[2][2]) + Rotation.E[0][2]) * 0.0f + Rotation.E[1][2]);
    unsigned int const LevelThresholdBits = 0x3f34fdf4u;      // DAT_063a033c, 0.707f
    float LevelThreshold;
    std::memcpy(&LevelThreshold, &LevelThresholdBits, sizeof(LevelThreshold));
    if (!(LevelThreshold > ForwardY))
        return;

    // LevelCameraFlag is cleared around the gate query (FUN_062e92e0 reads it back through
    // get_camera_level_status).
    LevelCameraFlag = 0;
    if (Sys->Thrust.CheckSteeringGate())
    {
        ShipUpDirection = Vector{ 0.0f, 1.0f, 0.0f };
        ShipUpDirFlag = 0;
    }
    LevelCameraFlag = 1;
}

// FUN_062d2ad0.
bool IBehaviorManager::RequestFleeDestination(uint ObjectId,
                                              pub::AI::ContentCallback::FleeReason Reason,
                                              pub::AI::ContentCallback::FleeDestinationType& OutType,
                                              Vector& OutPosition, uint& OutParam, float& OutRadius)
{
    // Tail-jumps to the callback's slot 4 with the six stack arguments unchanged.
    if (ContentCbPtr != nullptr)
        return ContentCbPtr->request_flee_destination(ObjectId, Reason, OutType, OutPosition,
                                                      OutParam, OutRadius);
    // is_player for effect, result discarded. No null tests in retail.
    (void)Sys->SelfTarget.Iface->is_player();
    return false;
}

// FUN_062d29c0.
bool IBehaviorManager::NotifyFollowEvent(uint ObjectId1, uint ObjectId2,
                                         pub::AI::ContentCallback::FollowStatusType Status,
                                         float Distance)
{
    if (Sys->Targeting.AttackState == TargetManager::kAttackStateEngaged || CurrentBehaviorIndex != BEHAVIOR_FOLLOW)
        return false;
    // The callback's slot 0.
    if (ContentCbPtr != nullptr)
    {
        ContentCbPtr->follow_event(ObjectId1, ObjectId2, Status, Distance);
        return true;
    }
    // is_player for effect.
    (void)Sys->SelfTarget.Iface->is_player();
    return false;
}

// FUN_062d2a10.
bool IBehaviorManager::NotifyTetherEvent(uint ObjectId1, uint ObjectId2,
                                         pub::AI::ContentCallback::TetherStatusType Status,
                                         Vector const& Position)
{
    if (Sys->Targeting.AttackState == TargetManager::kAttackStateEngaged)
        return false;
    if (CurrentBehaviorIndex != BEHAVIOR_GOTO && CurrentBehaviorIndex != BEHAVIOR_DOCK)
        return false;
    // The callback's slot 1 (+0x04).
    if (ContentCbPtr != nullptr)
    {
        ContentCbPtr->tether_object_event(ObjectId1, ObjectId2, Status, Position);
        return true;
    }
    // is_player for effect.
    (void)Sys->SelfTarget.Iface->is_player();
    return false;
}

bool IBehaviorManager::SetCurrentBehavior(int NewIndex, bool bForce, void* DirectiveArg)
{
    // FUN_062d3420, RET 0xc. No null test on the iface.
    int EffectiveIndex = NewIndex;

    if (Sys->SelfTarget.Iface->is_player())
    {
        // ManeuverMask bit (NewIndex + 1), then LockManeuversByte; either refuses.
        if ((ManeuverMask & (2u << (static_cast<unsigned>(NewIndex) & 31))) != 0)
            return false;
        if (LockManeuversByte != 0)
            return false;

        // Behavior 6 as the player becomes DefaultBehaviorIndex instead, and every steering
        // input on the ThrustManager (Sys+0x18) is released on the way.
        if (NewIndex == BEHAVIOR_IDLE)
        {
            EffectiveIndex = static_cast<int>(DefaultBehaviorIndex);
            ThrustManager* const Thrust = &Sys->Thrust;
            Thrust->SetThrottle(0.0f, kThrottleModeCruise);                           // FUN_062e89a0(0, 2)
            Thrust->SetThrottle(0.0f, kThrottleModeEngine);                           // FUN_062e89a0(0, 0)
            // Retail re-zeroes the same stack scratch before each of the next two calls.
            Vector Zero;
            Zero.x = 0.0f;
            Zero.y = 0.0f;
            Zero.z = 0.0f;
            Thrust->SetAxisThrottle(Zero);                           // FUN_062e8c20
            Zero.x = 0.0f;
            Zero.y = 0.0f;
            Zero.z = 0.0f;
            Thrust->SetNudgeDirection(Zero);                         // FUN_062e8ce0
            Thrust->SetStrafeDir(StrafeNone);                        // FUN_062e9030(0)
            Thrust->SetThrustersActive(false);                       // FUN_062e9120(0)
        }
    }

    if (CurrentBehaviorIndex == EffectiveIndex && !bForce)
        return true;

    // An index of -1 leaves its pointer at 0; no other bound is checked.
    Behavior* const Outgoing = (CurrentBehaviorIndex != -1)
                                   ? BehaviorDispatchTable[CurrentBehaviorIndex]
                                   : nullptr;
    Behavior* const Incoming = (EffectiveIndex != -1)
                                   ? BehaviorDispatchTable[EffectiveIndex]
                                   : nullptr;

    bool bResult = true;

    if (Outgoing != nullptr)
        Outgoing->OnDisengage();

    // With a directive, the incoming entry's slot 10 (+0x28) answers; OnEngage runs
    // regardless.
    if (Incoming != nullptr)
    {
        if (DirectiveArg != nullptr)
            bResult = Incoming->OnApplyDirective(DirectiveArg);
        Incoming->OnEngage();
    }

    CurrentBehaviorIndex = EffectiveIndex;

    // No guard on the watched object; a CEqObj owner gets the new index in CEqObj+0x1a0.
    IObjRW* const WatchedObj = ObjRwFromWatchable(WatchablePtr);
    CObject const* const Owned = WatchedObj->cobject();
    if (Owned != nullptr && (static_cast<unsigned>(Owned->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
    {
        static_cast<CEqObj*>(const_cast<CObject*>(Owned))->BehaviorIndex = EffectiveIndex;
    }

    return bResult;
}

// LAB_062d3f90 -- vftable 063a037c slot 0, the only virtual of retail IBehaviorManager.
// thiscall, RET 4; jump table at .text:062d42f0 (8 entries). `Rec` is Sys+0x1364 and `Data`
// is [Event+4]. The Rec helpers are FFormationLeaderState members: FUN_062ec590
// SetValueB4, FUN_062ec5d0 SetDirectiveOrder, FUN_062ec650 SetValueB0, FUN_062ec6a0
// SuppressCurrentLeader.
void IBehaviorManager::OnEvent(FBehaviorManagerEvent const* Event)
{
    if (CurrentBehaviorIndex == -1)
        return;
    if (static_cast<unsigned int>(Event->Type) > 7u)
        return;

    unsigned char* const Data = static_cast<unsigned char*>(Event->Data);
    FFormationLeaderState* const Rec = &Sys->FormationLeaders;  // Sys + 0x1364
    switch (Event->Type)
    {
    case FBehaviorManagerEvent::kEventDamageTaken:
        // FUN_062e86d0(Sys+0x34, Data).
        AccumulateSystemManagerTimerLevel(&Sys->Timers, Data);
        return;

    case FBehaviorManagerEvent::kEventMunitionHit:
        // The callee 062ed730 is the ICF-folded `RET 4` Ghidra labels
        // MetaList<GoodDesc>::operator=, so the case only reads [Data] (kept: it faults on a
        // bad pointer in retail too).
        {
            unsigned int const volatile Unused = *reinterpret_cast<unsigned int const*>(Data);
            (void)Unused;
        }
        return;

    case FBehaviorManagerEvent::kEventSetManeuver:
    {
        // A type-2 directive whose target is [Data+8] is completed.
        int* const Dir = reinterpret_cast<int*>(DirectiveStorage);
        if (Dir[0] != 2)
            return;
        int const OldTarget = Dir[2];
        if (*reinterpret_cast<int const*>(Data + 8) != OldTarget)
            return;
        Dir[0] = 0;
        Dir[2] = -1;
        PendingDirectiveResult = 0;
        PendingCompletedBehavior = OldTarget;
        return;
    }

    case FBehaviorManagerEvent::kEventObjectLeftSystem:
    {
        // [Data+4] / [Data+8] are re-read from memory at every use, as retail does.
        unsigned int& LeftId = *reinterpret_cast<unsigned int*>(Data + 8);
        unsigned int const& LeftFlags = *reinterpret_cast<unsigned int const*>(Data + 4);

        Sys->GunGroups.ClearGunTargets();

        // No null test on the watcher; a CShip (& 0x503) gets leaving_system_update.
        CObject const* const WatchedCobj =
            ObjInspectOf(ObjRwFromWatchable(WatchablePtr))->cobject();
        if (WatchedCobj != nullptr &&
            (static_cast<unsigned int>(WatchedCobj->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            static_cast<CShip*>(const_cast<CObject*>(WatchedCobj))->leaving_system_update(LeftId);

        if (LeftId == Sys->SelfTarget.Iface->get_id())
            Rec->SetValueB0(0);

        ClearFormationLeaderWatcherIfTarget(Sys, static_cast<int>(LeftId));

        // Behavior slot 21 (+0x54) with the dword [Data+8].
        for (int Index = 0; Index < 21; ++Index)
        {
            Behavior* const Entry = BehaviorDispatchTable[Index];
            if (Entry != nullptr)
                Entry->OnObjectLeftSystem(reinterpret_cast<void*>(static_cast<uintptr_t>(LeftId)));
        }

        if ((LeftFlags & (OBJ_JUMP_GATE | OBJ_AIRLOCK_GATE | OBJ_JUMP_HOLE)) != 0)
        {
            // get_formation_leader (+0xb0), return value ignored; the watcher is not tested.
            IObjRW* Leader = nullptr;
            ObjInspectOf(ObjRwFromWatchable(WatchablePtr))->get_formation_leader(Leader);
            if (Leader != nullptr &&
                ObjInspectOf(Leader)->get_id() == LeftId)
            {
                // The leader's id is read first.
                IObjRW* const Self = ObjRwFromWatchable(WatchablePtr);
                unsigned int const LeaderId = ObjInspectOf(Leader)->get_id();
                if (LeaderId != ObjInspectOf(Self)->get_id())
                    Rec->SetValueB0(*reinterpret_cast<unsigned int const*>(Data));
            }
            // Behavior slot 22 (+0x58); the entry is not null-tested.
            BehaviorDispatchTable[CurrentBehaviorIndex]->OnEvent(
                const_cast<FBehaviorManagerEvent*>(Event));
        }

        if ((LeftFlags & (OBJ_JUMP_GATE | OBJ_JUMP_HOLE)) == 0)
            return;
        if (!Sys->SelfTarget.Iface->is_player())
            return;
        if (CurrentBehaviorIndex != BEHAVIOR_FORMATION)
            return;
        if (LeftId != Sys->SelfTarget.Iface->get_id())
            return;
        SetCurrentBehavior(static_cast<int>(DefaultBehaviorIndex), true, nullptr);
        return;
    }

    case FBehaviorManagerEvent::kEventTradelane:
    {
        // FUN_062ecaf0 returns the leader (already -8 adjusted) or 0. The behavior slot 22 call
        // is unconditional (entry not null-tested).
        IObjRW* const Watched = Sys->FormationLeaders.GetCurrentLeader();
        if (Watched != nullptr)
        {
            if (Watched->get_id() ==
                *reinterpret_cast<unsigned int const*>(Data + 0xc))
                Rec->SetDirectiveOrder(Data[0],
                                       *reinterpret_cast<unsigned int const*>(Data + 0x10),
                                       *reinterpret_cast<unsigned int const*>(Data + 0x14),
                                       Data[2],
                                       *reinterpret_cast<unsigned int const*>(Data + 4));
        }
        BehaviorDispatchTable[CurrentBehaviorIndex]->OnEvent(
            const_cast<FBehaviorManagerEvent*>(Event));
        return;
    }

    case FBehaviorManagerEvent::kEventDockRequest:
    {
        IObjRW* const Watched = Sys->FormationLeaders.GetCurrentLeader();
        if (Watched == nullptr)
            return;
        if (Watched->get_id() !=
            *reinterpret_cast<unsigned int const*>(Data + 8))
            return;
        if ((*reinterpret_cast<unsigned int const*>(Data + 4) & 0x120u) == 0)
            return;
        Rec->SetValueB4(*reinterpret_cast<unsigned int const*>(Data),
                        Data[0xc]);
        return;
    }

    case FBehaviorManagerEvent::kEventMissileLaunched:
        // FUN_062e8720(Sys+0x34, Data).
        AppendSystemManagerTimerListEntry(&Sys->Timers, Data);
        return;

    case FBehaviorManagerEvent::kEventObjectDying:
        // Otherwise FUN_062ec6a0 (sets Rec+0xc5).
        if (*reinterpret_cast<unsigned int const*>(Data) ==
            Sys->SelfTarget.Iface->get_id())
        {
            SetCurrentBehavior(static_cast<int>(DefaultBehaviorIndex), false, nullptr);
            TickSuppressedFlag = 1;
            return;
        }
        Sys->FormationLeaders.SuppressCurrentLeader();
        return;
    }
}
