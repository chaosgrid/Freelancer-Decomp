// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_goods.h"   // BaseWatcher
#include "common_pub.h"     // pub::AI::Personality
// TargetManager (SystemManager+0x142C, by value). common_iface.h includes this header right
// after TargetManager's definition, so whichever of the two a translation unit names first,
// TargetManager is complete before SystemManager is read.
#include "common_iface.h"

#include <cstddef>          // offsetof

class SystemManager;    // forward decl: SystemManagerTimerTable::Owner back-pointer
class IBehaviorManager; // forward decl: SystemManager::Owner back-pointer (pointer-only use here)
struct IObjDB;          // common_cobjdb.h -- SystemManager::ObjectDb (pointer-only use here)
struct IObjInspect;     // common_iface.h -- FAiTarget's two interface pointers

// ============================================================================
// FAiTarget -- the 0x88-byte AI-target cache record. One instance is embedded in
// SystemManager at +0x14FC (SelfTarget, the own ship); the timer table's
// std::vector<FAiTarget> (SystemManagerTimerTable::Targets, 0x88 stride, built by
// SystemManagerTimerTable::BuildAiTargetList FUN_062e7580) holds the others. Every cached slice is filled
// lazily from Iface on first use and guarded by its own byte (+0x7C..+0x7F); the
// owner clears all four guards once per tick (SystemManager::Init / SystemManager::Update,
// FAiTarget::Init for vector elements).
// ============================================================================
struct FAiTarget
{
    // +0x00/+0x04, guarded by bAttitudeValid: AttitudeRefIface->
    // get_attitude_towards_symmetrical(CachedAttitude, Iface, CachedReverseAttitude)
    // (FAiTarget::GetCachedRange, FUN_062e6a80). Both 0.0f when either interface is null or the
    // call fails. Consumers gate on `<= -0.6f` / `<= -0.2f` (hostile enough).
    float CachedAttitude;
    float CachedReverseAttitude;

    // +0x08, guarded by bFlagsValid: Iface->get_type(CachedFlags) (GetFlags).
    unsigned CachedFlags;

    // +0x0C/+0x18, guarded by bPosValid: one Iface->get_physical_radius(CachedRadius,
    // CachedPos) call (RefreshPos; the radius out-param is pushed first).
    Vector CachedPos;
    float CachedRadius;

    // +0x1C..+0x7B, guarded by bCornersValid: world-space bounding-box corners from
    // TransformBoundingBoxCorners (FUN_062e4910) over Iface's transform and extents.
    Vector CachedCorners[8];

    unsigned char bAttitudeValid;   // +0x7C
    unsigned char bFlagsValid;      // +0x7D
    unsigned char bPosValid;        // +0x7E
    unsigned char bCornersValid;    // +0x7F

    // +0x80: the reference object the attitude pair is measured against (the list
    // owner's own ship). +0x84: this record's object; every other slice comes from it.
    IObjInspect* AttitudeRefIface;
    IObjInspect* Iface;

    // FUN_062d09b0: writes both interface pointers (attitude reference first) and clears
    // the four guards.
    void Init(IObjInspect const* NewAttitudeRef, IObjInspect const* NewIface);

    // FUN_062ca1b0. The guard is raised after the vcall.
    unsigned GetFlags();

    // FUN_062c47c0. The guard is raised before the vcall. Most retail call sites inline it.
    void RefreshPos();

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
    void GetCachedRange(float* OutLow, float* OutHigh);
};
static_assert(sizeof(FAiTarget) == 0x88, "FAiTarget must match retail's 0x88-byte AI-target cache record");
static_assert(offsetof(FAiTarget, CachedFlags) == 0x08, "FAiTarget+0x08 CachedFlags");
static_assert(offsetof(FAiTarget, CachedPos) == 0x0C, "FAiTarget+0x0C CachedPos");
static_assert(offsetof(FAiTarget, CachedRadius) == 0x18, "FAiTarget+0x18 CachedRadius");
static_assert(offsetof(FAiTarget, CachedCorners) == 0x1C, "FAiTarget+0x1C CachedCorners");
static_assert(offsetof(FAiTarget, bAttitudeValid) == 0x7C, "FAiTarget+0x7C bAttitudeValid");
static_assert(offsetof(FAiTarget, bFlagsValid) == 0x7D, "FAiTarget+0x7D bFlagsValid");
static_assert(offsetof(FAiTarget, bPosValid) == 0x7E, "FAiTarget+0x7E bPosValid");
static_assert(offsetof(FAiTarget, bCornersValid) == 0x7F, "FAiTarget+0x7F bCornersValid");
static_assert(offsetof(FAiTarget, AttitudeRefIface) == 0x80, "FAiTarget+0x80 AttitudeRefIface");
static_assert(offsetof(FAiTarget, Iface) == 0x84, "FAiTarget+0x84 Iface");

// ============================================================================
// SystemManager — ctor FUN_062e6260. Its sole caller, IBehaviorManager's ctor FUN_062d2220,
// allocates operator new(0x195C) and stores it at IBehaviorManager+0x08 (Sys). Sub-objects in
// construction order (sizes from the ctor's LEA sequence):
//
//   +0x00           int    OwnerObjectId  -- never written by the ctor
//   +0x04  IBehaviorManager* Owner         -- zeroed at the end of the ctor; set by
//                     SystemManager::Init (FUN_062e6490). ThrustManager::CheckSteeringGate
//                     reads it through ThrustManager::Owner.
//   +0x08  (0x08)   BaseWatcher Watcher    -- zeroed
//   +0x10           IObjDB* ObjectDb       -- zeroed at the end
//   +0x14           byte   bLeaderParamsBorrowed (+pad) -- zeroed at the end
//   +0x18  (0x1C)   ThrustManager Thrust   -- the ctor zeroes only its first dword;
//                     SystemManager::Init initialises the rest (see ThrustManager below).
//   +0x34  (0xF8)   SystemManagerTimerTable Timers -- ctor FUN_062e6af0; embeds 11x
//                     SustainedThresholdTimer.
//   +0x12C (0x1238) SystemManagerGunGroupManager GunGroups -- ctor FUN_062e9330. Two
//                     0x40-slot, 0x20-stride gun-group arrays plus LauncherFireCacheSlot[16]
//                     at GunGroups+0x1098.
//   +0x1364 (0xC8)  FFormationLeaderState FormationLeaders -- ctor FUN_062ebbc0.
//   +0x142C (0x6C)  TargetManager Targeting (common_iface.h) -- ctor FUN_062ed570; vftable
//                     .rdata:063a0888 (base IObjInspect::SubtargetEnumerator).
//   +0x1498         SystemManager* TractorOwner -- not written by the ctor; the loot tractor
//                     record's first dword (SystemManager::Init stores the SystemManager into it).
//   +0x149C (0x08)  BaseWatcher TractorTargetWatcher -- zeroed
//   +0x14A4         short  TractorEquipId (+pad) -- SUBOBJ_ID_NONE (0)
//   +0x14A8 (0x54)  RepairManager Repair -- ctor inline in FUN_062e6260; its +0x50
//                     CheckTimer (absolute +0x14F8) is not written by the ctor.
//   +0x14FC (0x88)  FAiTarget SelfTarget -- the own ship's AI-target record (every
//                     intercept-clearance helper's "Elem"). The ctor writes only its
//                     guards and interface pointers; SystemManager::Init and the tick fill
//                     AttitudeRefIface and Iface (the own ship's IObjInspect).
//   +0x1584 (0x3D8) pub::AI::Personality PersonalityData -- the last member.
//
// Retail has no out-of-line destructor: ~IBehaviorManager (FUN_062d2530) inlines it as
// the member unwind (see ~SystemManager in common_systemmanager.cpp).
// ============================================================================

// The Mode argument of ThrustManager::SetThrottle (FUN_062e89a0); any other value does nothing.
enum EThrottleMode
{
    kThrottleModeEngine = 0,   // the engine throttle, clamped to [-1, 1]; leaves cruise
    kThrottleModeCruise = 2,   // the cruise throttle, clamped to [0, 1.2]; may engage cruise
};

// ---------------------------------------------------------------------------
// ThrustManager — SystemManager+0x18, 0x1C bytes. A retail class: Common.dll exports
// ?engage_cruise@ThrustManager@@QAEX_N@Z (ordinal 2476, .text:062e90a0) and nothing else of
// it. Layout from SystemManager::Init's (FUN_062e6490) initialiser block plus the methods;
// +0x19..+0x1B are never touched, so the record pads to 0x1C, where Timers begins.
// SystemManager+0x18 holds the SystemManager itself because it is ThrustManager::Owner.
// Every method is __thiscall on the ThrustManager in retail (callers form
// `ECX = Sys + 0x18`). Definitions: common_thrustmanager.cpp.
// ---------------------------------------------------------------------------
struct ThrustManager
{
    // +0x00: back-pointer to the owning SystemManager. Every method resolves
    // Owner->Watcher (+0x08) through the module-wide -8 Watchable->IObjRW downcast.
    SystemManager* Owner;

    // +0x04: upper clamp applied to SetThrottle's incoming throttle. Initialised to 5.0f.
    // Retail's name is unknown.
    float MaxThrottle;

    // +0x08: SetAxisThrottle (FUN_062e8c20) refreshes it from IObjInspect::get_axis_throttle,
    // compares it against the request with a squared-delta epsilon (DAT_063a07e4) and, on
    // any differing axis, calls IObjAffect::set_axis_throttle and copies the request in.
    // Zeroed by the initialiser.
    Vector AxisThrottle;

    // +0x14: "cruise is currently commanded on". engage_cruise writes it; SetThrottle
    // reads and writes it.
    unsigned char bCruiseEngaged;

    // +0x15: "cruise is unobstructed". 1 from the initialiser, re-armed each tick from
    // PathfindManager::IsCruiseUnobstructed (FUN_062de160). engage_cruise refuses to engage
    // while it is 0; SetThrottle downgrades Mode 2 to Mode 0 on it. Name from that writer.
    unsigned char bCruiseUnobstructed;

    // +0x16: the engine activation last requested through SetEngineActive (FUN_062e91d0).
    // Zeroed by the initialiser; nothing else touches it.
    unsigned char bEngineActive;

    // +0x17: the auto-level switch. Only writer after the initialiser is
    // SetSteeringFallbackFlag, reached solely from the export
    // IBehaviorManager::update_current_behavior_auto_level(bool). CheckSteeringGate's final
    // fallback.
    unsigned char bAutoLevel;

    // +0x18: the auto-avoidance switch. 1 from the initialiser, cleared when the watched
    // object is_player() -- a player starts with it off. Written by the export
    // IBehaviorManager::update_current_behavior_auto_avoidance(bool) (tail jump into
    // SetAutoAvoidance). Read by the SystemManager tick. Retail's member name is unknown.
    unsigned char bAutoAvoidance;

    unsigned char Pad0x19[3];

    // .text:062e90a0. An ordinary member so the compiler emits retail's __thiscall.
    COMMON_API void engage_cruise(bool bEngage);

    // .text:062e92d0 / .text:062e92e0.
    void SetSteeringFallbackFlag(unsigned char Value);
    bool CheckSteeringGate();

    // 0x06263bd0, an ICF-folded `byte [ECX+0x18] = arg; return arg` body (Ghidra labels it
    // CNudgeEngine::Activate). Reached on this class only through
    // update_current_behavior_auto_avoidance with ECX = Sys+0x18.
    bool SetAutoAvoidance(bool bOn);

    // FUN_062e91d0. Unless the owner ship is missing: when the ship is the player, or
    // bActive differs from bEngineActive, finds the ship's engine; when it exists, is not
    // destroyed and its active bit differs from bActive, switches it through
    // IObjAffect::slot_4c(&Id, bActive, -1). Then bEngineActive = bActive.
    void SetEngineActive(bool bActive);

    // FUN_062e8c20. Unless the owner ship is missing or get_axis_throttle(AxisThrottle)
    // fails: when Wanted differs from the refreshed cache on some axis by more than the
    // epsilon, set_axis_throttle(Wanted) and AxisThrottle = Wanted.
    void SetAxisThrottle(Vector const& Wanted);

    // FUN_062e9030. SetEngineActive(true); then, when get_strafe_dir succeeds and answers
    // something other than Dir, set_strafe_dir(Dir).
    void SetStrafeDir(StrafeDir Dir);

    // FUN_062e9120. Nothing while the ship cruises outside a trade lane. Otherwise
    // SetEngineActive(true) when bActive, and the thrusters (afterburner) are switched
    // through IObjAffect::slot_54(bActive, -1) unless are_thrusters_active already answers
    // bActive.
    void SetThrustersActive(bool bActive);

    // FUN_062e89a0. Mode 2 sets the throttle (clamped to MaxThrottle, then [0, 1.2]) and,
    // for a positive throttle on an unobstructed path, engages cruise; Mode 0 disengages
    // cruise and sets the throttle clamped to [-1, 1]. Mode 2 falls back to 0 while
    // !bCruiseUnobstructed; any other Mode does nothing. False only when the owner ship is
    // missing.
    bool SetThrottle(float Throttle, int Mode);

    // FUN_062e8ce0. SetEngineActive(true); turns WorldDir (normalised when longer than 1)
    // into ship space and hands it to IObjAffect::set_nudge_vec unless the ship's current
    // nudge already matches it within 1e-6 on every axis.
    void SetNudgeDirection(Vector const& WorldDir);

    // FUN_062e8e60. Blends
    // the ship's current nudge (in world space, dropped unless it points within acos(0.99)
    // of WorldDir) with WorldDir scaled by 1.5 * DeltaTime * |WorldDir| when WorldDir is the
    // longer of the two, and hands the result to SetNudgeDirection.
    void BlendNudgeDirection(Vector const& WorldDir, float DeltaTime);

private:
    // The owner's watched ship: `[Owner+8] ? [Owner+8] - 8 : 0` (the -8 Watchable
    // adjustment), which every method above re-evaluates before each use.
    IObjRW* OwnerShip() const;
};

static_assert(sizeof(ThrustManager) == 0x1C,
    "ThrustManager occupies SystemManager+0x18..+0x34 exactly (Timers starts at +0x34)");
static_assert(offsetof(ThrustManager, MaxThrottle) == 0x04, "ThrustManager+0x04 MaxThrottle");
static_assert(offsetof(ThrustManager, AxisThrottle) == 0x08, "ThrustManager+0x08 AxisThrottle");
static_assert(offsetof(ThrustManager, bCruiseEngaged) == 0x14, "ThrustManager+0x14 bCruiseEngaged");
static_assert(offsetof(ThrustManager, bCruiseUnobstructed) == 0x15, "ThrustManager+0x15 bCruiseUnobstructed");
static_assert(offsetof(ThrustManager, bEngineActive) == 0x16, "ThrustManager+0x16 bEngineActive");
static_assert(offsetof(ThrustManager, bAutoLevel) == 0x17, "ThrustManager+0x17 bAutoLevel");
static_assert(offsetof(ThrustManager, bAutoAvoidance) == 0x18, "ThrustManager+0x18 bAutoAvoidance");

// SystemManager::Init's (FUN_062e6490) ThrustManager initialiser block. Separate from any
// constructor because retail runs it late, once the owner's Watcher has a live target;
// SystemManager's ctor leaves this range alone.
void InitThrustManager(ThrustManager* Self, SystemManager* Owner);

// ---------------------------------------------------------------------------
// FAiTargetVector — retail's `std::vector<FAiTarget>` layout, embedded at
// SystemManagerTimerTable+0x0C (absolute SystemManager+0x40). Defined here so the timer
// table can hold it by value; common_iustategraph.h's FAiTargetList aliases the table.
// ---------------------------------------------------------------------------
struct FAiTargetVector
{
    // +0x00: the (empty, padded) allocator member; retail's ctor writes one byte here.
    unsigned char AllocatorPad[4];
    FAiTarget* Begin;   // +0x04  _First
    FAiTarget* End;     // +0x08  _Last  (one past the live range)
    FAiTarget* Cap;     // +0x0C  _End   (one past the allocated range)

    // FUN_062ed140, __thiscall(this=the
    // vector) + 3 stack args (RET 0xc): MSVC 7.0's
    // `std::vector<FAiTarget>::insert(iterator Where, size_type Count, const
    // FAiTarget& Value)` / `_Insert_n`, all three branches. Growth, when needed,
    // is `NewCapacity = size() + max(size(), Count)`, allocated with a raw
    // `operator new` and released with `operator delete` (0x06391d5a) -- no
    // element destructors run because the element type has none. The only live
    // call site (FUN_062e7580) always uses Where == End and Count == 1, i.e.
    // push_back; the other two branches are ported too.
    void InsertAiTargets(FAiTarget* Where, unsigned Count, FAiTarget const& Value);
};
static_assert(sizeof(FAiTargetVector) == 0x10, "FAiTargetVector must be 0x10 bytes");
static_assert(offsetof(FAiTargetVector, Begin) == 0x04, "FAiTargetVector+0x04 Begin");
static_assert(offsetof(FAiTargetVector, End) == 0x08, "FAiTargetVector+0x08 End");
static_assert(offsetof(FAiTargetVector, Cap) == 0x0C, "FAiTargetVector+0x0C Cap");

// ---------------------------------------------------------------------------
// SustainedThresholdTimer -- 0x10 bytes; eleven of them sit in SystemManagerTimerTable at
// +0x28 (retail ctor FUN_062e6af0 writes {0, 100.0f, 1.0f, 0.0f} into each).
// ---------------------------------------------------------------------------
struct SustainedThresholdTimer
{
    unsigned char Active;       // +0x00 latched once Level exceeds TriggerThreshold
    float TriggerThreshold;     // +0x04
    float Duration;             // +0x08 how long the latch holds
    float Accumulator;          // +0x0C time spent latched

    // FUN_062e6a20. Latches when Level > 0 and Level > TriggerThreshold; while latched,
    // accumulates DeltaTime and answers true until the unrounded sum exceeds Duration, which
    // clears the latch and answers false. Definition: common_aifirecontrol.cpp.
    bool Update(float DeltaTime, float Level);
};
static_assert(sizeof(SustainedThresholdTimer) == 0x10, "SustainedThresholdTimer must match retail's 16-byte raw layout");

// ---------------------------------------------------------------------------
// SystemManagerTimerTable — SystemManager+0x34, ctor FUN_062e6af0. Also the AI "hostile
// candidate" target list the 0x062e7580-0x062e8400 cluster works on
// (common_iustategraph.{h,cpp}; its FAiTargetList is an alias of this struct). The ctor
// builds an empty vector at +0x0C and clears bKeepCache.
// ---------------------------------------------------------------------------
// Doubly-linked circular list node: {next, prev, embedded BaseWatcher}, 0x10 bytes (the
// size of both retail operator new calls, sentinel and node).
struct SystemManagerTimerListNode
{
    SystemManagerTimerListNode* Next;  // +0x00
    SystemManagerTimerListNode* Prev;  // +0x04
    unsigned char Watcher[8];          // +0x08, BaseWatcher-sized payload
};

struct SystemManagerTimerTable
{
    // +0x00: not written by the ctor; SystemManager::SetSharedListHead (FUN_062e68e0) stores
    // its IObjDB* here as well as in SystemManager::ObjectDb. SystemManagerTimerTable::AppendTimerListEntry
    // (FUN_062e8720) calls its FindById.
    IObjDB* ObjectDb;

    // +0x04: back-pointer to the owning SystemManager, set by SystemManagerTimerTable::Arm
    // (not by the ctor) and read directly, with no -8 adjustment.
    SystemManager* Owner;

    // +0x08: the scanner's equipment id, whose status gates the whole list rebuild.
    // SystemManagerTimerTable::Arm fills it via find_equipment(&ScannerEquipId, 1, 0x80000);
    // SystemManagerTimerTable::BuildAiTargetList passes it to get_equipment_status as `unsigned short const&`.
    unsigned short ScannerEquipId;
    unsigned char Pad_0x0A[2];

    // +0x0C (absolute SystemManager+0x40): the target list itself.
    FAiTargetVector Targets;

    // +0x1C: set once the list has been built at least once. Not written by the ctor;
    // cleared by SystemManager::SetSharedListHead and SystemManagerTimerTable::Arm.
    unsigned char bBuilt;
    // +0x1D: when clear, the list is dropped and rebuilt on every call; when
    // set (together with bBuilt) the cached list is returned untouched. Also
    // forwarded verbatim as get_scanlist's `bFlag` argument, where it means
    // "do not clear the scanner's cache first".
    unsigned char bKeepCache;
    unsigned char Pad_0x1E[2];

    // +0x20: per-tick multiplier for Level's passive decay (SystemManagerTimerTable::Update:
    // `Level -= DecayRate * DecayScale * DeltaTime`).
    float DecayRate;

    // +0x24: shared "signal level" compared against every SustainedThresholdTimer's
    // TriggerThreshold each tick (SystemManagerTimerTable::Update) and accumulated into by
    // SystemManagerTimerTable::AccumulateTimerLevel when a matching, in-range candidate is found.
    float Level;

    // +0x28..+0xD8: 11 SustainedThresholdTimer records, each constructed to {Active=0,
    // TriggerThreshold=100.0f, Duration=1.0f, Accumulator=0.0f}. Thresholds and durations are
    // loaded in SystemManagerTimerTable::RefreshTimerThresholdsFromPersonality (common_sysmgr_timertable.cpp).
    enum ETimerChannel : int
    {
        TimerEvadeBreakDamage = 0,
        TimerEvadeDodgeMoreDamage = 1,
        TimerBrakeReverseDamage = 2,
        TimerEngineKillFaceDamage = 3,
        TimerAfterburnerDamage = 4,
        TimerRollDamage = 5,
        TimerFireGunsDamage = 6,
        TimerDropMinesDamage = 7,
        TimerFireMissilesDamage = 8,
        TimerBreakFormationDamage = 9,
        TimerBreakApartFormationDamage = 10,
        TimerChannelCount = 11,
    };
    SustainedThresholdTimer Timers[TimerChannelCount];

    float DecayScale;              // +0xD8 = 0.5f, the second factor of Level's decay
    unsigned char Reserved0xDC;    // +0xDC uninitialized-in-retail artifact byte (Ghidra's `local_11`)
    unsigned char Pad0xDD[3];
    SystemManagerTimerListNode* ListHeadNode; // +0xE0: heap-allocated (operator_new(0x10)) empty
                                    // self-referential 2-pointer circular-list sentinel
    int ListCount;                 // +0xE4 = 0
    // +0xE8: a BaseWatcher (FUN_062e6d40 calls set / set_pointer on it) holding the
    // current best missile-reaction candidate. Raw bytes so
    // SystemManagerTimerTable::Construct's whole-struct memset stays valid.
    unsigned char BestCandidateWatcherBytes[0x08]; // +0xE8..+0xF0
    int Gap0xF0;                   // +0xF0 genuine gap, never written by the ctor
    float BestValueSoFar;          // +0xF4 = FLT_MAX sentinel

    // FUN_062d2700, the table's out-of-line destructor (reached from the SystemManager
    // ctor's EH unwind; IBehaviorManager's dtor inlines the same steps). Clears the +0xE8
    // watcher, tears down the candidate list (nodes, sentinel, +0xE0 / +0xE4), then frees
    // the Targets buffer and zeroes its pointers.
    ~SystemManagerTimerTable();

    // The Active byte of Timers[0..5] (+0x28 + 0x10 * N). common_sysmgr_timertable.cpp.
    bool IsTimer0Active() const;    // FUN_062e8810
    bool IsTimer1Active() const;    // FUN_062e8820
    bool IsTimer2Active() const;    // FUN_062e8860
    bool IsTimer3Active() const;    // FUN_062e8830
    bool IsTimer4Active() const;    // FUN_062e8850
    bool IsTimer5Active() const;    // FUN_062e8840

    // FUN_062e74a0. ObjectDb->FindById(Id), returned only when the found object's get_id()
    // equals *Id; null when *Id is 0, ObjectDb is null or nothing is found.
    IObjRW* FindVerifiedObject(unsigned const* Id) const;

    // FUN_062e7400: for a moon/planet base (type & 3), walks ObjectDb's docking rings (type
    // mask 0x20) and returns the first whose get_dock_target equals the base's get_base id;
    // null otherwise. Defined in common_aiintercept.cpp.
    IObjRW* FindObjectDockingAtBase(IObjInspect const* BaseIface) const;

    // FUN_062e8400. True when some target of SystemManagerTimerTable::BuildAiTargetList with a type in 0x7f0000 lies
    // ahead of the own ship (Owner's +0x14FC record) within MaxRange, moves faster than 10,
    // heads the same way (forward dot > 0.95f), is hostile (attitude <= -0.6f) and lies within
    // MaxAngle radians of the own forward axis. Definition: common_aifirecontrol.cpp.
    bool HasTargetInForwardArc(float MaxAngle, float MaxRange);

    // FUN_062e74e0. Unless the own ship (Owner->Watcher) is missing or its scanner
    // (ScannerEquipId) reports EquipStatusFlagDestroyed, OutList = the ship's
    // get_scanlist(FilterMask, bKeepCache); true when that answered 0. Callers test OutList.
    // Definition: common_obstacleavoidance.cpp.
    bool GetOwnScanList(ScanList const*& OutList, unsigned FilterMask);

    void Construct();

    // FUN_062e8720. Source is an id block {OwnerId, missile id}
    // (IBehaviorManager::Slot00 event 6, sent by Server StarSystem::create_guided).
    // Looks the missile up with Self->ObjectDb->FindById(&Source[1]) and appends a
    // node watching it onto the circular sentinel list at
    // Self->ListHeadNode/Self->ListCount.
    void AppendTimerListEntry(void* Source);

    // FUN_062e86d0. If Candidate's id (its own +8 field) matches the
    // owning SystemManager's current ExternalIface id, and Candidate's state
    // (+0x14) is in the open range (1,8), accumulates Candidate's value (+0x18)
    // into Self->Level.
    void AccumulateTimerLevel(void* Candidate);

    // FUN_062e8870. True if the current best-candidate distance
    // (Self->BestValueSoFar) falls under any of the owning SystemManager's
    // pub::AI::Personality::MissileReaction range thresholds.
    bool HasMissileReactionTarget() const;

    // FUN_062e6c60. Arms the timer table for a newly attached Owner: stores Self->Owner once
    // Owner->Watcher holds a live object, looks up the object's scanner
    // (find_equipment(&ScannerEquipId, 1, 0x80000)) and on success sets DecayRate = MaxHitPts +
    // MaxShield (the second out-params of get_status and get_shield_status).
    bool Arm(SystemManager* Owner);

    // FUN_062e72d0, thiscall on &SystemManager::Timers. Rebuilds all eleven
    // SustainedThresholdTimer slots' TriggerThreshold/Duration pairs from the owning
    // SystemManager's Personality, scaling every threshold by Timers::DecayRate. Returns true.
    bool RefreshTimerThresholdsFromPersonality();

    // FUN_062e6d40, thiscall on &Timers. Walks the candidate list
    // SystemManagerTimerTable::AppendTimerListEntry builds, garbage-collecting dead nodes, and picks the
    // nearest candidate that is (a) in front of the owner (positive dot of owner-to-candidate
    // against the candidate's velocity), (b) inside Personality.MissileReaction
    // .EvadeMissileDistance of the owner's cached centre and (c) nearer than anything so far;
    // leaves it in BestCandidateWatcher and a closing-rate estimate in BestValueSoFar
    // (FLT_MAX when nothing qualified). UnusedArg is retail's unread stack argument; the
    // caller passes its DeltaTime.
    void UpdateCandidateScan(float UnusedArg);

    // FUN_062e7010, thiscall on &Timers. Runs the candidate scan above,
    // advances all eleven SustainedThresholdTimer slots against Timers::Level, then
    // decays Level by DecayRate * DecayScale * DeltaTime, clamped at zero.
    void Update(float DeltaTime);

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
    IObjInspect* FindClosestObstacleToAvoid(unsigned ExcludeId, unsigned TypeMask);

    bool MissileWithinReaction00() const;
    bool MissileWithinReaction04() const;
    bool MissileWithinReaction08() const;

    // FUN_062e77e0, __thiscall(this = the list), no
    // arguments. `bBuilt = 0; Targets.End = Targets.Begin` -- the same inlined, never-iterating
    // erase(begin(), end()) shape, but without ClearAiTargetVector's destroy-range call. Sole
    // caller TargetManager::GetScanResponse (FUN_062eeb70, .text:062eeb89).
    void ResetAiTargetList();

    // FUN_062e7580 (194 instructions), __thiscall, no stack args, returns a pointer to
    // Self->Targets.
    //
    // Rebuilds the list of nearby objects the owner's scanner can see, unless a
    // cached list is still considered good:
    //   1. If bKeepCache is clear, ClearAiTargetVector first (0x062e7595; its shift loop is
    //      seeded from End, `MOV EAX,EDX` at 0x062e75a0, so it never runs).
    //   2. If bBuilt AND bKeepCache are both set, return the cached vector
    //      untouched (0x062e75d3-0x062e75df).
    //   3. Set bBuilt, then recover the owner's own IObjRW from
    //      SystemManager::Watcher (+0x08) by the "-8 Watchable adjustment": the
    //      watcher stores the target's Watchable sub-object, which IObjRW places
    //      at its own +0x08 (IObjRW : IObjInspect, IObjAffect, Watchable), so
    //      subtracting 8 recovers the object. Bail out returning the vector
    //      as-is if nothing is watched.
    //   4. IObjInspect::get_equipment_status(Status, ScannerEquipId) -- bail if
    //      Status.Flags has EquipStatusFlagDestroyed (0x02).
    //   5. IObjInspect::get_scanlist(List, 0x7FFDFF, bKeepCache) -- bail unless
    //      it returns 0. The 0x7FFDFF mask is the object-type set to scan for.
    //   6. Reserve exactly List->Count elements if the CURRENT CAPACITY (not the
    //      size) is smaller, by hand: operator new, CopyElemRange the live range
    //      into the new buffer, DestroyAiTargetRange the old range, operator
    //      delete the old buffer, then Cap/End/Begin.
    //   7. For each of List->Count entries, take the watched Watchable, apply the
    //      same -8 adjustment, build a fresh FAiTarget whose AttitudeRefIface is
    //      the OWNER's object and whose Iface is the candidate, prime
    //      CachedFlags via get_type, and append it if
    //      `(CachedFlags & 0x7FFDFF) != 0`.
    FAiTargetVector* BuildAiTargetList();

    // FUN_062e7820 (163 instructions), __thiscall, no stack args, returns float: the body of
    // the exported IBehaviorManager::get_closest_trailing_ship.
    //
    // Walks SystemManagerTimerTable::BuildAiTargetList's result and returns the distance to the nearest
    // candidate that passes every gate, or FLT_MAX when none does:
    //   * CachedFlags & 0x7F0000 (a ship-ish object type)
    //   * !is_player()                                        (slot +0x0BC)
    //   * get_target(Other) yields a non-null target          (slot +0x074)
    //   * that target's get_id() equals the owner's get_id()  (slot +0x020)
    //     -- i.e. the candidate is currently targeting US
    //   * IsTargetCoAlignedAndAhead(Candidate, OwnerElem, 0.0f)
    //   * CachedAttitude <= -0.2f (.rdata:063a0794) -- hostile enough
    // Distance uses retail's FastInvSqrt idiom (`FastInvSqrt(d2) * d2`), not a
    // real sqrt, and both endpoints come from the cached-position idiom. The
    // "owner element" is `SystemManager + 0x14FC`, the single embedded FAiTarget
    // the Intercept-clearance cluster already documents.
    float GetClosestTrailingShipDistance();

    // FUN_062e8280 (129 instructions), __thiscall + one float stack arg (RET 0x4), returns
    // IObjInspect* or null.
    //
    // Returns the interface of the physically nearest candidate that is hostile
    // and (optionally) close enough, or null:
    //   * CachedFlags & 0x7F0000
    //   * CachedAttitude <= -0.6f (.rdata:063a0790)
    //   * when MaxClearance >= 0.0f, also
    //     GetInterceptClearance(OwnerElem, Candidate) < MaxClearance. A negative
    //     MaxClearance skips this gate entirely (retail's own
    //     `FCOMP 0.0f; JNP` at 0x062e835b), which is how callers ask for
    //     "nearest hostile at any range".
    //   * among the survivors, minimum Distance3(OwnerPosition, CachedPos),
    //     seeded at 100000.0f (.rdata:0x47C35000) -- so a candidate farther than
    //     100km is never returned even if it is the only one.
    // OwnerPosition comes from GetIfacePositionOrZero on the owner's
    // own object, resolved through the same -8 Watchable adjustment.
    IObjInspect* FindClosestHostileTarget(float MaxClearance);
};
static_assert(sizeof(SystemManagerTimerTable) == 0xF8,
    "SystemManagerTimerTable must match retail's 0xF8-byte sub-object (SystemManager+0x34)");
static_assert(offsetof(SystemManagerTimerTable, ObjectDb) == 0x00, "SystemManagerTimerTable+0x00 ObjectDb");
static_assert(offsetof(SystemManagerTimerTable, Owner) == 0x04, "SystemManagerTimerTable+0x04 Owner");
static_assert(offsetof(SystemManagerTimerTable, ScannerEquipId) == 0x08, "SystemManagerTimerTable+0x08 ScannerEquipId");
static_assert(offsetof(SystemManagerTimerTable, Targets) == 0x0C, "SystemManagerTimerTable+0x0C Targets (the std::vector<FAiTarget> header)");
static_assert(offsetof(SystemManagerTimerTable, bBuilt) == 0x1C, "SystemManagerTimerTable+0x1C bBuilt");
static_assert(offsetof(SystemManagerTimerTable, bKeepCache) == 0x1D, "SystemManagerTimerTable+0x1D bKeepCache");
static_assert(offsetof(SystemManagerTimerTable, DecayRate) == 0x20, "SystemManagerTimerTable+0x20 DecayRate");
static_assert(offsetof(SystemManagerTimerTable, Level) == 0x24, "SystemManagerTimerTable+0x24 Level");
static_assert(offsetof(SystemManagerTimerTable, Timers) == 0x28, "SystemManagerTimerTable+0x28 Timers");
static_assert(offsetof(SystemManagerTimerTable, ListHeadNode) == 0xE0, "SystemManagerTimerTable+0xE0 ListHeadNode");
static_assert(offsetof(SystemManagerTimerTable, BestCandidateWatcherBytes) == 0xE8,
    "SystemManagerTimerTable+0xE8 BestCandidateWatcher (a real BaseWatcher)");
static_assert(offsetof(SystemManagerTimerTable, BestValueSoFar) == 0xF4, "SystemManagerTimerTable+0xF4 BestValueSoFar");

// SystemManagerTimerTable-scoped free functions (common_sysmgr_timertable.cpp).

// ---------------------------------------------------------------------------
// SystemManagerGunGroupManager -- SystemManager+0x12C, 0x1238 bytes; ctor FUN_062e9330.
// No vtable and no RTTI, so retail's class name is unknown. Every method below
// is thiscall on &SystemManager::GunGroups in retail.
//
// It runs the AI's guns and mine droppers: ResolveHardpoints fills Guns from
// find_equipment(EquipTypeGun) and MineDroppers from find_equipment(EquipTypeMineDropper);
// RefreshFromPersonality copies the +0x103C..+0x1088 tunables from the Personality's
// GunParams / MineParams / MissileParams (field names below follow the INI keys Content's
// personality loader reads into them); UpdateTimers advances every slot's burst phase; the
// Update*State methods fire. The countermeasure droppers have their own block at +0x1218.
// ---------------------------------------------------------------------------

// One gun or mine-dropper record, 0x20 bytes, natural padding. The ctor writes +0x02..+0x04 = 0,
// BurstTime 3.0f, NoBurstTime 1.5f, FireInterval 0.5f, Phase 0 and +0x1C = 0; the padding
// (+0x05..+0x07, +0x1D..+0x1F) is never written.
struct FGunGroupSlot
{
    // MunitionKind: PopulateSlots maps get_equipment_val(EquipValMunitionKind) 0 / 1 / 2 / 3 to
    // 0 / 1 / 2 / 4 and forces mine droppers to 3; 5 until a slot is classified.
    enum : int
    {
        KindPlain = 0,              // munition without a seeker
        KindSeeker = 1,             // munition with a seeker
        KindTorpedo = 2,            // munition of HpType 0xa (HpTypeTorpedo)
        KindMineDropper = 3,
        KindCruiseDisruptor = 4,
        KindUnclassified = 5,
    };

    unsigned short HardpointId;     // +0x00
    unsigned char bAutoTurret;      // +0x02 EquipValAutoTurret
    unsigned char bSeekerMunition;  // +0x03 EquipValSeeker
    unsigned char bFireTick;        // +0x04 UpdateTimers: set on the tick Phase crosses a FireInterval boundary
    int MunitionKind;               // +0x08
    float BurstTime;                // +0x0C Phase runs up to this ...
    float NoBurstTime;              // +0x10 ... then restarts at -NoBurstTime
    float FireInterval;             // +0x14
    float Phase;                    // +0x18 >= 0 inside a burst
    unsigned char Field_0x1C;       // +0x1C only the ctor's zero store is known
};
static_assert(sizeof(FGunGroupSlot) == 0x20, "FGunGroupSlot sizeof 0x20");
static_assert(offsetof(FGunGroupSlot, bAutoTurret) == 0x02, "FGunGroupSlot+0x02 bAutoTurret");
static_assert(offsetof(FGunGroupSlot, bSeekerMunition) == 0x03, "FGunGroupSlot+0x03 bSeekerMunition");
static_assert(offsetof(FGunGroupSlot, bFireTick) == 0x04, "FGunGroupSlot+0x04 bFireTick");
static_assert(offsetof(FGunGroupSlot, MunitionKind) == 0x08, "FGunGroupSlot+0x08 MunitionKind");
static_assert(offsetof(FGunGroupSlot, BurstTime) == 0x0C, "FGunGroupSlot+0x0C BurstTime");
static_assert(offsetof(FGunGroupSlot, NoBurstTime) == 0x10, "FGunGroupSlot+0x10 NoBurstTime");
static_assert(offsetof(FGunGroupSlot, FireInterval) == 0x14, "FGunGroupSlot+0x14 FireInterval");
static_assert(offsetof(FGunGroupSlot, Phase) == 0x18, "FGunGroupSlot+0x18 Phase");
static_assert(offsetof(FGunGroupSlot, Field_0x1C) == 0x1C, "FGunGroupSlot+0x1C");

// One line-of-fire cache record (GunGroups+0x1098, sixteen of them), 0x18 bytes, natural
// padding. Keyed by (TargetId, HardpointId); see IsLineOfFireBlocked and UpdateTimers.
struct FLauncherFireCacheSlot
{
    unsigned char InUse;            // +0x00
    unsigned TargetId;              // +0x04
    unsigned short HardpointId;     // +0x08
    float Age;                      // +0x0C since claimed or last recomputed; evicted past 10 s
    float RefreshTimer;             // +0x10 while fresh, counts to 1 s, then NeedsRefresh
    unsigned char NeedsRefresh;     // +0x14 stale: the next query recomputes
    unsigned char bBlocked;         // +0x15 the cached answer
};
static_assert(sizeof(FLauncherFireCacheSlot) == 0x18, "FLauncherFireCacheSlot sizeof 0x18");
static_assert(offsetof(FLauncherFireCacheSlot, TargetId) == 0x04, "FLauncherFireCacheSlot+0x04 TargetId");
static_assert(offsetof(FLauncherFireCacheSlot, HardpointId) == 0x08, "FLauncherFireCacheSlot+0x08 HardpointId");
static_assert(offsetof(FLauncherFireCacheSlot, Age) == 0x0C, "FLauncherFireCacheSlot+0x0C Age");
static_assert(offsetof(FLauncherFireCacheSlot, RefreshTimer) == 0x10, "FLauncherFireCacheSlot+0x10 RefreshTimer");
static_assert(offsetof(FLauncherFireCacheSlot, NeedsRefresh) == 0x14, "FLauncherFireCacheSlot+0x14 NeedsRefresh");
static_assert(offsetof(FLauncherFireCacheSlot, bBlocked) == 0x15, "FLauncherFireCacheSlot+0x15 bBlocked");

// SystemManager+0x1344 (GunGroups+0x1218), 0x20 bytes: switches the ship's countermeasure
// droppers on and off from Personality CMParams. Methods are thiscall on this block.
struct FCmDropperWatcher
{
    SystemManager* Owner;           // +0x00
    float Timer;                    // +0x04
    unsigned char bActive;          // +0x08 the state last applied to every dropper
    int Count;                      // +0x0C
    unsigned short Ids[8];          // +0x10 (Bind lets find_equipment write up to 0x40 ids)

    // FUN_062eb9d0. Rejects
    // a null owner or one whose Watcher holds no object; stores the owner. A player ship
    // answers true there. Otherwise clears the state, collects the ship's countermeasure
    // dropper ids through find_equipment(Ids, 0x40, EquipTypeCmDropper), counts them up to
    // the first zero and switches them on (SetActive(1)).
    bool Bind(SystemManager* NewOwner);

    // FUN_062eba70. Not for a
    // player ship: Timer cycles through CMParams.CountermeasureUnactiveTime, SetActive(1)
    // below CountermeasureActiveTime and, unless the ship is a player, SetActive(0) between
    // the two.
    void Update(float Dt);

    // FUN_062ebb10. Unless
    // bActive already equals NewState, every dropper whose active bit differs is switched
    // through IObjAffect slot_4c(Id, NewState, -1); then bActive = NewState.
    void SetActive(unsigned char NewState);
};
static_assert(sizeof(FCmDropperWatcher) == 0x20, "FCmDropperWatcher sizeof 0x20");
static_assert(offsetof(FCmDropperWatcher, Timer) == 0x04, "FCmDropperWatcher+0x04 Timer");
static_assert(offsetof(FCmDropperWatcher, bActive) == 0x08, "FCmDropperWatcher+0x08 bActive");
static_assert(offsetof(FCmDropperWatcher, Count) == 0x0C, "FCmDropperWatcher+0x0C Count");
static_assert(offsetof(FCmDropperWatcher, Ids) == 0x10, "FCmDropperWatcher+0x10 Ids");

struct SystemManagerGunGroupManager
{
    int LastGunFireResult;              // +0x00 RunFireControlTick stores UpdateGunFireState's result
    SystemManager* Owner;               // +0x04 SystemManager::Init (FUN_062e6490) writes it
    // +0x08: a BaseWatcher on the current target. Raw bytes because the ctor memsets the whole
    // block; TargetWatcher() is the typed view.
    unsigned char TargetWatcherBytes[8];
    unsigned Field_0x10;                // +0x10 bits 0x30000000 hold all fire; no writer in Common.dll
    Vector AimOffset;                   // +0x14 the aim point in the target's own frame
    float AimRefreshTimer;              // +0x20 aim point recomputed once this is <= 0 (UpdateGunFireState)
    float CruiseHeldTime;               // +0x24 how long the target has been cruising (UpdateTimers)
    float ScanTimer;                    // +0x28 ScanAutoTurretTargets
    float ScanPeriod;                   // +0x2C 1.0f
    FGunGroupSlot Guns[0x40];           // +0x30
    int GunCount;                       // +0x830
    int Field_0x834;                    // +0x834 written -1 by the ctor and SystemManager::Init; never read
    FGunGroupSlot MineDroppers[0x40];   // +0x838
    int MineDropperCount;               // +0x1038

    // +0x103C..+0x1088, refreshed by RefreshFromPersonality (ctor defaults in FUN_062e9330).
    float GunFireIntervalTime;              // +0x103C GunParams gun_fire_interval_time
    float GunFireIntervalVariance;          // +0x1040 gun_fire_interval_variance_percent
    float AutoTurretIntervalTime;           // +0x1044 auto_turret_interval_time
    float GunFireBurstIntervalTime;         // +0x1048 gun_fire_burst_interval_time
    float GunFireBurstIntervalVariance;     // +0x104C gun_fire_burst_interval_variance_percent
    float GunFireNoBurstIntervalTime;       // +0x1050 gun_fire_no_burst_interval_time
    float AutoTurretBurstIntervalTime;      // +0x1054 auto_turret_burst_interval_time
    float AutoTurretNoBurstIntervalTime;    // +0x1058 auto_turret_no_burst_interval_time
    float AutoTurretBurstIntervalVariance;  // +0x105C auto_turret_burst_interval_variance_percent
    float MissileLaunchIntervalTime;        // +0x1060 MissileParams missile_launch_interval_time
    float MissileLaunchIntervalVariance;    // +0x1064 missile_launch_interval_variance_percent
    float MissileLaunchConeAngle;           // +0x1068 missile_launch_cone_angle
    float MissileLaunchRange;               // +0x106C missile_launch_range
    float MissileLaunchAllowOutOfRange;     // +0x1070 missile_launch_allow_out_of_range, the bool as a float
    float MineLaunchInterval;               // +0x1074 MineParams mine_launch_interval
    float MineLaunchConeAngle;              // +0x1078 mine_launch_cone_angle
    float Field_0x107C;                     // +0x107C ctor 0.0f; no other access
    float GunFireAccuracyConeAngle;         // +0x1080 GunParams gun_fire_accuracy_cone_angle
    float GunRange;                         // +0x1084 longest plain-gun range * jittered gun_range_threshold
    int bFireAllGuns;                       // +0x1088 fire_style MULTIPLE -> 1, SINGLE -> 0
    float Field_0x108C;                     // +0x108C ctor pi/32; no other access
    float WideConeAngle;                    // +0x1090 ctor pi/32; UpdateMissileFireState's wide aim angle
    unsigned char bEnabled;                 // +0x1094 ctor 1; FireAutoTurrets / RunFireControlTick need it
    FLauncherFireCacheSlot LineOfFireCache[16]; // +0x1098
    FCmDropperWatcher CmDroppers;           // +0x1218

    // Inlined into ~SystemManager between the +0x1364 record and the timer table.
    ~SystemManagerGunGroupManager() { TargetWatcher().set(nullptr); }

    BaseWatcher& TargetWatcher() { return *reinterpret_cast<BaseWatcher*>(TargetWatcherBytes); }
    BaseWatcher const& TargetWatcher() const { return *reinterpret_cast<BaseWatcher const*>(TargetWatcherBytes); }

    // `[[this+4]+8] ? -8 : 0` -- the own ship (Owner->Watcher's object), or null. Retail
    // dereferences it untested at most sites; so do the callers.
    IObjRW* OwnShip() const;
    // `[this+8] ? -8 : 0` -- the current target, or null.
    IObjRW* CurrentTarget() const;

    // FUN_062e9500. Re-rolls Slot's BurstTime / NoBurstTime / FireInterval from the
    // tunables for its MunitionKind (cruise disruptors take MissileParams.AntiCruiseMissileIntervalTime), then
    // Phase = FireInterval * rand - FireInterval.
    void RandomizeSlotTiming(FGunGroupSlot* Slot);

    // FUN_062e9770. Copies
    // up to MaxCount ids (stopping at SUBOBJ_ID_NONE) into Slots, then classifies each slot
    // (MunitionKind, bAutoTurret, bSeekerMunition) and switches inactive ones on for a
    // non-player ship. Returns the count.
    int PopulateSlots(unsigned short const* SourceIds, int MaxCount, FGunGroupSlot* Slots, bool bMineDroppers);

    // FUN_062e9990. Fills Guns
    // and MineDroppers from the own ship's equipment; false only without an own ship.
    bool ResolveHardpoints();

    // FUN_062e9a60. The
    // own ship's first shield (find_equipment(&Id, 1, EquipTypeShield)) is switched on or off
    // (IObjAffect slot_4c(&Id, Mode != 0, -1)). False only without an own ship.
    bool SetShieldActive(int Mode);

    // FUN_062e9ae0. Watches
    // NewTarget, tells the own ship (set_target) and, for a non-player ship, points every gun
    // at it (slot_44). False for a null NewTarget.
    bool Retarget(IObjRW* NewTarget);

    // FUN_062e9c00: every gun's
    // target cleared (slot_44 with null).
    void ClearGunTargets();

    // FUN_062e9c50. Once per ScanPeriod each
    // auto-turret is pointed at the nearest pointable hostile in 1.2 * GunRange; then
    // Targeting.SetAttackState(0 if a hostile was seen, else 2) unless the own type is in 0x7f0000.
    void ScanAutoTurretTargets(float Dt);

    // FUN_062ea220. Ages the
    // line-of-fire cache, CruiseHeldTime and every slot's burst phase.
    void UpdateTimers(float Dt);

    // FUN_062ea050. Every auto-turret in
    // its fire tick (or all of them under the fire-guns damage reaction) fires at its own
    // gun's target when CanFireLauncherAtTarget answers 1 and it points there within pi/60.
    bool FireAutoTurrets();

    // FUN_062ea450. A cached
    // (per target id and hardpoint) answer to "is a ship between this launcher and Target";
    // a stale record is recomputed through PhySys::FindRayCollisions.
    bool IsLineOfFireBlocked(IObjRW* Target, unsigned short HardpointId);

    // FUN_062ea770. 1 = can fire; 5 = out of RangeThreshold; 7 = line of
    // fire blocked; 2 / 0 / 4 / 3 from the launcher's EquipStatus; 2 without target or ship.
    int CanFireLauncherAtTarget(unsigned short HardpointId, IObjRW* Target, float RangeThreshold);

    // FUN_062ea960. The lead position on Target (UseDirectAim: the own
    // ship's lead-fire position for SubtargetId or for the ship), scattered inside a random
    // cone of SpreadAngle shaped by gun_fire_accuracy_power(_npc). UnusedSubobjSlot is pushed
    // by both callers and never read.
    void ComputeLeadFirePosition(Vector* OutPos, IObjRW* Target, unsigned short UnusedSubobjSlot,
                                 unsigned char UseDirectAim, float SpreadAngle, unsigned short SubtargetId);

    // FUN_062eac70 (its argument is unread). Fires every plain, non-turret gun in its fire tick (or overridden) at the target's
    // refreshed aim point; returns the last CanFireLauncherAtTarget result (8 when
    // Targeting.SelectionPriority bit 0 is clear, 0 without ship or target).
    int UpdateGunFireState();

    // FUN_062eb070 (argument unread). Launches seekers, HpType-0xa munitions and cruise disruptors under their gates.
    int UpdateMissileFireState();

    // FUN_062eb390 (argument unread). Drops mines while the ship moves faster than 20 and either the drop-mines damage
    // reaction is on or a hostile follows inside the mine cone and range.
    unsigned UpdateMineDropState();

    // FUN_062eb550. Fires every mine
    // dropper once (IObjAffect slot_34(Id, 1, null, -1)).
    void DropAllMines();

    // FUN_062eb5a0. Average and longest
    // EquipValRange over the plain guns; false when there is none.
    bool ComputeGunRangeStats(float* OutAverage, float* OutMax);

    // FUN_062eb6c0. Copies the
    // tunables from the owner's Personality -- or, for a non-player ship following a formation
    // leader that has a behaviour manager, from the leader's -- and re-rolls every slot.
    bool RefreshFromPersonality();

    // FUN_062eb910. True when some gun is a plain, seeker or HpType-0xa launcher
    // that is neither destroyed nor disabled.
    bool HasReadyWeaponSlot();

    // FUN_062eb670 (two arguments unread). Retarget(Target), then the gun, missile and mine passes.
    bool RunFireControlTick(IObjRW* Target);

    // FUN_062e9330: the gun groups' constructor, run by SystemManager's ctor.
    void Construct();
};
static_assert(sizeof(SystemManagerGunGroupManager) == 0x1238,
    "SystemManagerGunGroupManager must match retail's 0x1238-byte sub-object (SystemManager+0x12C)");
static_assert(offsetof(SystemManagerGunGroupManager, TargetWatcherBytes) == 0x08, "GunGroups+0x08 TargetWatcher");
static_assert(offsetof(SystemManagerGunGroupManager, Field_0x10) == 0x10, "GunGroups+0x10");
static_assert(offsetof(SystemManagerGunGroupManager, AimOffset) == 0x14, "GunGroups+0x14 AimOffset");
static_assert(offsetof(SystemManagerGunGroupManager, AimRefreshTimer) == 0x20, "GunGroups+0x20 AimRefreshTimer");
static_assert(offsetof(SystemManagerGunGroupManager, CruiseHeldTime) == 0x24, "GunGroups+0x24 CruiseHeldTime");
static_assert(offsetof(SystemManagerGunGroupManager, ScanTimer) == 0x28, "GunGroups+0x28 ScanTimer");
static_assert(offsetof(SystemManagerGunGroupManager, ScanPeriod) == 0x2C, "GunGroups+0x2C ScanPeriod");
static_assert(offsetof(SystemManagerGunGroupManager, Guns) == 0x30, "GunGroups+0x30 Guns");
static_assert(offsetof(SystemManagerGunGroupManager, GunCount) == 0x830, "GunGroups+0x830 GunCount");
static_assert(offsetof(SystemManagerGunGroupManager, Field_0x834) == 0x834, "GunGroups+0x834");
static_assert(offsetof(SystemManagerGunGroupManager, MineDroppers) == 0x838, "GunGroups+0x838 MineDroppers");
static_assert(offsetof(SystemManagerGunGroupManager, MineDropperCount) == 0x1038, "GunGroups+0x1038 MineDropperCount");
static_assert(offsetof(SystemManagerGunGroupManager, GunFireIntervalTime) == 0x103C, "GunGroups+0x103C");
static_assert(offsetof(SystemManagerGunGroupManager, MissileLaunchRange) == 0x106C, "GunGroups+0x106C");
static_assert(offsetof(SystemManagerGunGroupManager, MineLaunchConeAngle) == 0x1078, "GunGroups+0x1078");
static_assert(offsetof(SystemManagerGunGroupManager, GunFireAccuracyConeAngle) == 0x1080, "GunGroups+0x1080");
static_assert(offsetof(SystemManagerGunGroupManager, GunRange) == 0x1084, "GunGroups+0x1084 GunRange");
static_assert(offsetof(SystemManagerGunGroupManager, bFireAllGuns) == 0x1088, "GunGroups+0x1088 bFireAllGuns");
static_assert(offsetof(SystemManagerGunGroupManager, WideConeAngle) == 0x1090, "GunGroups+0x1090 WideConeAngle");
static_assert(offsetof(SystemManagerGunGroupManager, bEnabled) == 0x1094, "GunGroups+0x1094 bEnabled");
static_assert(offsetof(SystemManagerGunGroupManager, LineOfFireCache) == 0x1098, "GunGroups+0x1098 LineOfFireCache");
static_assert(offsetof(SystemManagerGunGroupManager, CmDroppers) == 0x1218, "GunGroups+0x1218 CmDroppers");

// ---------------------------------------------------------------------------
// RepairManager / PotionHelper / HullHelper / ShieldHelper -- SystemManager+0x14A8,
// 0x54 bytes. Retail classes (RTTI):
//   RepairManager, base IObjInspect::CargoEnumerator: vtable 063a07b8 = { FUN_062ecf60 }
//   ShieldHelper (base PotionHelper): vtable 063a07c0 = { LAB_062e6430, LAB_062e6440 }
//   HullHelper   (base PotionHelper): vtable 063a07cc = { LAB_062e6400, FUN_062e6410 }
//   PotionHelper: vtable 063a07d8 = { _purecall, _purecall }
// GetRatio calls the argument's IObjInspect get_status (hull) or get_shield_status (shield).
//
// What the family does: RepairManager is the AI's nanobot / shield-battery user. Every three
// seconds SystemManager's tick (FUN_062e66c0) zeroes both helpers' Count and hands the manager
// to IObjInspect::enumerate_cargo; enumerate() tallies cargo whose archetype class is
// RepairKit (0x1a) into Hull and ShieldBattery (0x1b) into Shield, remembering the equipment
// id. Each tick the two helpers then run PotionHelper::Update (FUN_062eceb0): when the health
// ratio GetRatio() returns falls to UseAtRatio or below, wait PreUseDelay, use the item
// through IObjAffect slot 0x58 (EquipId, 1, -1), then wait PostUseDelay before re-arming.
// The three thresholds of each helper are refreshed from Personality::RepairParams by
// RepairManager::RefreshFromPersonality (FUN_062ecfb0).
//
// Layout from SystemManager's ctor and SystemManager::Init (FUN_062e6490): Owner and
// CheckTimer (SystemManager+0x14F8; advanced by FUN_062e66c0) are written only by
// SystemManager::Init.
// ---------------------------------------------------------------------------

// The values of PotionHelper::State (PotionHelper::Update, FUN_062eceb0).
enum EPotionState
{
    kPotionStateArmed = 0,         // waits until the health ratio falls to UseAtRatio
    kPotionStatePreUseDelay = 1,   // waits PreUseDelay, then uses the item
    kPotionStatePostUseDelay = 2,  // waits PostUseDelay, then re-arms
};

// PotionHelper -- abstract base; no virtual destructor (retail's table has two slots).
struct PotionHelper
{
    // Retail's ctor: State = 0, EquipId = SUBOBJ_ID_NONE, Count = 0, Timer = 0,
    // PreUseDelay = 2.0f, PostUseDelay = 5.0f, UseAtRatio = 0.35f. Type is written only by
    // slot 0; the port zeroes it.
    PotionHelper();

    // Slot 0: Type = 0x1a (hull) / 0x1b (shield). Nothing in Common.dll dispatches it.
    virtual void SetType() = 0;
    // Slot 1: the object's current health ratio for the resource this helper restores
    // (returned in ST0).
    virtual float GetRatio(IObjRW const* Object) = 0;

    // FUN_062eceb0, non-virtual.
    void Update(float Dt, IObjRW* Object);

    int State;                  // +0x04  EPotionState
    int Type;                   // +0x08  Archetype class type (0x1a / 0x1b), written by SetType
    unsigned short EquipId;     // +0x0C  id of the last matching cargo item enumerate() saw
    unsigned char Pad0E[2];
    int Count;                  // +0x10  matching cargo count, re-tallied every CheckTimer period
    float PreUseDelay;          // +0x14  state 1 -> use once Timer >= this
    float PostUseDelay;         // +0x18  state 2 -> 0 once Timer >= this
    float UseAtRatio;           // +0x1C  state 0 -> 1 once GetRatio() <= this
    float Timer;                // +0x20
};
static_assert(sizeof(PotionHelper) == 0x24, "PotionHelper sizeof 0x24 (Shield at RepairManager+0x2C)");
static_assert(offsetof(PotionHelper, State) == 0x04, "PotionHelper+0x04 State");
static_assert(offsetof(PotionHelper, Type) == 0x08, "PotionHelper+0x08 Type");
static_assert(offsetof(PotionHelper, EquipId) == 0x0C, "PotionHelper+0x0C EquipId");
static_assert(offsetof(PotionHelper, Count) == 0x10, "PotionHelper+0x10 Count");
static_assert(offsetof(PotionHelper, PreUseDelay) == 0x14, "PotionHelper+0x14 PreUseDelay");
static_assert(offsetof(PotionHelper, PostUseDelay) == 0x18, "PotionHelper+0x18 PostUseDelay");
static_assert(offsetof(PotionHelper, UseAtRatio) == 0x1C, "PotionHelper+0x1C UseAtRatio");
static_assert(offsetof(PotionHelper, Timer) == 0x20, "PotionHelper+0x20 Timer");

// HullHelper, vtable 063a07cc.
struct HullHelper : PotionHelper
{
    void SetType() override;                             // LAB_062e6400
    float GetRatio(IObjRW const* Object) override;       // FUN_062e6410
};

// ShieldHelper, vtable 063a07c0.
struct ShieldHelper : PotionHelper
{
    void SetType() override;                             // LAB_062e6430
    float GetRatio(IObjRW const* Object) override;       // LAB_062e6440
};

// RepairManager, vtable 063a07b8, base IObjInspect::CargoEnumerator.
struct RepairManager : IObjInspect::CargoEnumerator
{
    // Retail's ctor leaves Owner and CheckTimer alone (SystemManager::Init writes them); the
    // port zeroes both.
    RepairManager();

    // FUN_062ecf60, the single slot. Returns 1 on every path.
    bool enumerate(unsigned short const& Id, Archetype::Equipment const* Arch, int Count) override;

    SystemManager* Owner;       // +0x04
    HullHelper Hull;            // +0x08
    ShieldHelper Shield;        // +0x2C
    float CheckTimer;           // +0x50

    // FUN_062ecfb0, thiscall on the RepairManager. Re-seeds both helpers'
    // PreUseDelay / PostUseDelay / UseAtRatio from a pub::AI::Personality's RepairParams --
    // normally the owning SystemManager's own, but, when the owner is not player-controlled, its
    // FormationParams.bLeaderMakesMeTougher byte is set, FormationLeaders.bLeaderSuppressed is clear and the
    // FormationLeaderWatcher's object resolves to a CEqObj with a behavior
    // manager, that object's SystemManager instead (and the owner's bLeaderParamsBorrowed latch is set).
    // Always returns true.
    bool RefreshFromPersonality();
};
static_assert(sizeof(RepairManager) == 0x54, "RepairManager sizeof 0x54 (SystemManager+0x14A8..+0x14FB)");
static_assert(offsetof(RepairManager, Owner) == 0x04, "RepairManager+0x04 Owner");
static_assert(offsetof(RepairManager, Hull) == 0x08, "RepairManager+0x08 Hull");
static_assert(offsetof(RepairManager, Shield) == 0x2C, "RepairManager+0x2C Shield");
static_assert(offsetof(RepairManager, CheckTimer) == 0x50, "RepairManager+0x50 CheckTimer");

// ---------------------------------------------------------------------------
// SystemManager+0x1364, the 0xC8-byte formation / follow-leader record (its ctor is
// FUN_062ebbc0). Every function on it is reached with ECX = Sys + 0x1364; definitions in
// common_gunconvergence.cpp unless noted.
//
// Its destructor is FUN_062d27d0 (reached from the SystemManager ctor's EH unwind;
// IBehaviorManager's dtor inlines it): the BaseWatcher at +0x14, then the one at +0x0C --
// the members' reverse declaration order.
// ---------------------------------------------------------------------------
struct FormationFollowerEnvelope; // common_gotobehavior.h

struct FFormationLeaderState
{
    // +0x00: += Dt every Update while bForceAttackFormation; zeroed once the unrounded sum
    // exceeds FormationParams.ForceAttackFormationActiveTime + ...UnactiveTime. Gate A
    // compares it with the active time.
    float ForceAttackCycleTimer;
    // +0x04: += Dt every Update while bBreakingFormation, else 0. Gates A / B compare it
    // with FormationParams.FormationExitMaxTime.
    float BreakFormationTimer;
    // +0x08: the owning SystemManager (set by SystemManager::Init).
    SystemManager* Owner;
    BaseWatcher FormationLeaderWatcher;   // +0x0C SetFormationLeader
    BaseWatcher FollowLeaderWatcher;      // +0x14 SetFollowLeader
    // +0x1C: get_formation_followers' 32-entry out-array (IsIdAmongOwnerFormationFollowers,
    // ComputeFormationFollowerEnvelope).
    IObjRW* FollowerScratch[0x20];
    // +0x9C / +0xA0: the two alignment-angle thresholds (radians) of
    // ComputeFormationFollowerEnvelope; ctor 0.1f / 0.2f.
    float FormationNarrowAngle;
    float FormationWideAngle;
    float Field_0xA4;                     // +0xA4 ctor 200.0f; no reader in Common.dll
    // +0xA8..+0xB8, +0xBD, +0xC2: the order SetDirectiveOrder stores and GetDirectiveOrder
    // copies out (BaseFollowerBehavior::Slot7's FollowerDirectiveOrder).
    unsigned OrderObjectId;               // +0xA8 the dock candidate's id
    unsigned OrderSecondObjectId;         // +0xAC a second object's id
    unsigned ValueB0;                     // +0xB0 SetValueB0 / TryGetValueB0
    unsigned ValueB4;                     // +0xB4 SetValueB4 / TryGetValueB4
    unsigned OrderLaneParam;              // +0xB8 float bits, go_tradelane's 4th argument
    unsigned char Field_0xBC;             // +0xBC only ever cleared (with +0xBD..+0xC4)
    unsigned char bOrderPending;          // +0xBD; Update clears it once the leader leaves the lane
    unsigned char bValueB0Valid;          // +0xBE
    unsigned char bValueB4Valid;          // +0xBF
    unsigned char bBreakingFormation;     // +0xC0 drives BreakFormationTimer
    unsigned char bLeaderIdleFormation;   // +0xC1 SyncFormationLeader: the leader's directive is {1, 0}
    unsigned char bOrderCommitDirectly;   // +0xC2
    unsigned char bForceAttackFormation;  // +0xC3 SetFlagC3; JobParams.bForceAttackFormation copies
    unsigned char bBreakApartNotified;    // +0xC4 Update's one-shot leader notification latch
    unsigned char bLeaderSuppressed;      // +0xC5 SuppressCurrentLeader; hides both leaders
    unsigned char bReleaseRequested;      // +0xC6 RequestRelease
    unsigned char Pad_0xC7;

    // FUN_062ebbc0.
    FFormationLeaderState();
    ~FFormationLeaderState();

    // FUN_062ebcb0; called from SystemManager::Update.
    void Update(float Dt);

    // FUN_062ec870 / FUN_062ec970 / FUN_062ec6b0.
    void SetFormationLeader(IObjRW* Target);
    void SetFollowLeader(IObjRW* Target);
    void SyncFormationLeader();

    // FUN_062ecaf0. Null while
    // bLeaderSuppressed; otherwise the follow leader unless its watcher holds 0 or 8, else
    // the formation leader (Watchable - 8 = the IObjRW; 0 stays null).
    IObjRW* GetCurrentLeader() const;

    // FUN_062ec6a0 (caller IBehaviorManager::Slot00): sets the +0xC5 gate GetCurrentLeader
    // tests; SetFormationLeader / SetFollowLeader clear it again.
    void SuppressCurrentLeader();

    // FUN_062ec520: is WantedId among the owning ship's formation followers
    // (fetched into this block's 32-entry scratch array at +0x1C).
    bool IsIdAmongOwnerFormationFollowers(unsigned WantedId);

    // FUN_062ec590: +0xB4 = Value, +0xBF = Flag (stored as given).
    void SetValueB4(unsigned int Value, unsigned char Flag);

    // FUN_062ec5b0: when +0xBF is set, *Out = +0xB4; returns +0xBF.
    unsigned char TryGetValueB4(unsigned int* Out) const;

    // FUN_062ec5d0: +0xBD = P1, +0xA8 = P2, +0xAC = P3,
    // +0xC2 = P4, +0xB8 = P5 -- the order block GetDirectiveOrder copies out.
    void SetDirectiveOrder(unsigned char P1, unsigned int P2, unsigned int P3, unsigned char P4,
                           unsigned int P5);

    // FUN_062ec610: copies +0xA8 / +0xAC / +0xBD / +0xC2 / +0xB8 to
    // Out +0x00 / +0x04 / +0x08 / +0x09 / +0x0C (a 0x10-byte block).
    void GetDirectiveOrder(unsigned char* Out) const;

    // FUN_062ec650: +0xB0 = Value, +0xBE = (Value != 0).
    void SetValueB0(unsigned int Value);

    // FUN_062ec680: when +0xBE is set, *Out = +0xB0; returns +0xBE.
    bool TryGetValueB0(unsigned int* Out) const;

    // FUN_062ecab0: null while +0xC5 is set, else the formation leader.
    IObjRW* GetFormationLeader() const;

    // FUN_062ecad0: null while +0xC5 is set, else the follow leader.
    IObjRW* GetFollowLeader() const;

    // FUN_062ecb20 / FUN_062ecc00. Retail meaning unknown; the predicates are written out at
    // the definitions.
    bool FollowerDirectiveGateA() const;
    bool FollowerDirectiveGateB() const;

    // FUN_062ecc50: +0xC3 = Value.
    void SetFlagC3(unsigned char Value);

    // FUN_062ecc60 / FUN_062ecce0: the owning ship's
    // formation leader, when there is one, gets QueueNotification(Leader, 1, 0) and then
    // (Leader, 2, Delay) when Delay > 0 / only (Leader, 2, 0) from the owning IBehaviorManager.
    void NotifyFormationLeader1(float Delay);
    void NotifyFormationLeader2();

    // FUN_062ecd30: +0xC6 = 1, then the owning ship's IObjAffect slot 0x60
    // (1, &ExternalIface id, -1).
    int RequestRelease();

    // FUN_062ebf50. Folds every formation follower of the owning ship into
    // one speed / throttle / angular envelope; false (leaving *Out as written so far) when the
    // owner has no watched object or no followers. Definition: common_gotobehavior.cpp.
    bool ComputeFormationFollowerEnvelope(FormationFollowerEnvelope* Out);
};
static_assert(sizeof(FFormationLeaderState) == 0xC8, "FFormationLeaderState sizeof 0xC8");
static_assert(offsetof(FFormationLeaderState, Owner) == 0x08, "FFormationLeaderState+0x08 Owner");
static_assert(offsetof(FFormationLeaderState, FormationLeaderWatcher) == 0x0C, "FFormationLeaderState+0x0C");
static_assert(offsetof(FFormationLeaderState, FollowLeaderWatcher) == 0x14, "FFormationLeaderState+0x14");
static_assert(offsetof(FFormationLeaderState, FollowerScratch) == 0x1C, "FFormationLeaderState+0x1C");
static_assert(offsetof(FFormationLeaderState, FormationNarrowAngle) == 0x9C, "FFormationLeaderState+0x9C");
static_assert(offsetof(FFormationLeaderState, OrderObjectId) == 0xA8, "FFormationLeaderState+0xA8");
static_assert(offsetof(FFormationLeaderState, OrderLaneParam) == 0xB8, "FFormationLeaderState+0xB8");
static_assert(offsetof(FFormationLeaderState, bOrderPending) == 0xBD, "FFormationLeaderState+0xBD");
static_assert(offsetof(FFormationLeaderState, bForceAttackFormation) == 0xC3, "FFormationLeaderState+0xC3");
static_assert(offsetof(FFormationLeaderState, bReleaseRequested) == 0xC6, "FFormationLeaderState+0xC6");

// ---------------------------------------------------------------------------
// SystemManager itself.
// ---------------------------------------------------------------------------
class SystemManager
{
public:
    SystemManager();
    ~SystemManager();

    int OwnerObjectId;                                 // +0x00 the owner's get_id, cached by SystemManager::Init
    // +0x04: back-pointer to the owning IBehaviorManager; zeroed by the ctor, set by
    // SystemManager::Init (FUN_062e6490).
    IBehaviorManager* Owner;                           // +0x04
    BaseWatcher Watcher;                               // +0x08
    // +0x10: the object database the owner is connected to, written only by
    // SystemManager::SetSharedListHead (FUN_062e68e0). TargetManager::GetScanResponse calls its
    // FindById.
    IObjDB* ObjectDb;                                  // +0x10
    // +0x14: set when RepairManager::RefreshFromPersonality / RefreshWeaponFireControlConfig took
    // the formation leader's parameters; SystemManager::Update clears it (and re-reads the own
    // personality) once no live leader is left.
    unsigned char bLeaderParamsBorrowed;               // +0x14
    unsigned char Pad15[3];
    // +0x18: the embedded ThrustManager. `Thrust.Owner != nullptr` is retail's "has this
    // ThrustManager been initialised yet" test.
    ThrustManager Thrust;                              // +0x18

    SystemManagerTimerTable Timers;                    // +0x34
    SystemManagerGunGroupManager GunGroups;            // +0x12C
    FFormationLeaderState FormationLeaders;            // +0x1364 (ctor FUN_062ebbc0)
    TargetManager Targeting;                           // +0x142C (FUN_062ed570, common_iface.h)
    // +0x1498..+0x14A7: the loot tractor record LootBehavior hands to FUN_062ecd90 /
    // FUN_062ece40 as Sys+0x1498. TractorOwner is a SystemManager back-pointer written only by
    // SystemManager::Init; TractorTargetWatcher (zeroed by the ctor, cleared first by the owning
    // IBehaviorManager's dtor) watches the object being tractored; TractorEquipId is
    // SUBOBJ_ID_NONE from the ctor and filled by SystemManager::Init through
    // find_equipment(&Id, 1, 0x100000).
    SystemManager* TractorOwner;                       // +0x1498 not written by the ctor
    BaseWatcher TractorTargetWatcher;                  // +0x149C
    unsigned short TractorEquipId;                     // +0x14A4
    unsigned char Pad14A6[2];

    // +0x14A8..+0x14FB: RepairManager (see above); its CheckTimer is SystemManager+0x14F8.
    RepairManager Repair;                              // +0x14A8

    // +0x14FC..+0x1583: the own ship's FAiTarget record, every intercept-clearance helper's
    // Elem. SystemManager::Init and the tick store the owner's object into AttitudeRefIface
    // and Iface and clear the guards; the ctor zeroes only the guards and the two pointers.
    FAiTarget SelfTarget;                              // +0x14FC

    pub::AI::Personality PersonalityData;               // +0x1584

    // FUN_062e68e0. Stores NewHead into Self->ObjectDb and Timers::ObjectDb; its tail jump into
    // a TargetManager member lands on an ICF-folded `RET 4` and writes nothing. If
    // Timers::Owner names a live SystemManager whose Watcher has a target and NewHead is
    // non-null, also empties the cached AI target list (bBuilt = 0, End = Begin).
    void SetSharedListHead(IObjDB* NewHead);

    // FUN_062e6990. Clears either of FormationLeaders' two BaseWatchers
    // (SystemManager+0x1370 / +0x1378) if its watched target's id matches TargetId.
    void ClearFormationLeaderWatcherIfTarget(int TargetId);

    // FUN_062e6950. The "personality changed" notification
    // IBehaviorManager::set_personality fires: refresh the weapon fire-control
    // config, rebuild the timer thresholds above, refresh the RepairManager's
    // thresholds (RepairManager::RefreshFromPersonality), then mirror one status byte off the formation-leader
    // state's watched object.
    void NotifyPersonalityChanged();

    // FUN_062e6490, thiscall on the SystemManager, called once from IBehaviorManager's ctor
    // right after SystemManager::SystemManager. Binds Watcher to Object, stores Manager in
    // Owner, caches the object's id in OwnerObjectId, arms the timer table and -- only when Watcher
    // then holds a live object -- initialises the ThrustManager, the gun groups, the
    // formation-leader state, the +0x1498 tractor record (owner + the first equipment id of type mask
    // 0x100000) and the RepairManager, and binds the
    // hardpoint-id watcher at +0x1344. TargetManager::Attach runs unconditionally. Returns
    // true (the caller ignores it).
    bool Init(IObjRW* Object, IBehaviorManager* Manager);

    // FUN_062e66c0, thiscall on the SystemManager: its per-tick update, called from
    // IBehaviorManager::UpdateBehaviorTick.
    void Update(float Dt);
};

static_assert(sizeof(SystemManager) == 0x195C,
    "SystemManager must match retail's 0x195C-byte size (operator_new(0x195c) at the sole call site, FUN_062d2220)");
static_assert(offsetof(SystemManager, Owner) == 0x04, "SystemManager+0x04 Owner");
static_assert(offsetof(SystemManager, Watcher) == 0x08, "SystemManager+0x08 Watcher");
static_assert(offsetof(SystemManager, ObjectDb) == 0x10, "SystemManager+0x10 ObjectDb");
static_assert(offsetof(SystemManager, Thrust) == 0x18, "SystemManager+0x18 ThrustManager");
static_assert(offsetof(SystemManager, Timers) == 0x34, "SystemManager+0x34 Timers");
static_assert(offsetof(SystemManager, GunGroups) == 0x12C, "SystemManager+0x12C GunGroups");
static_assert(offsetof(SystemManager, FormationLeaders) == 0x1364, "SystemManager+0x1364 FormationLeaders");
static_assert(offsetof(SystemManager, Targeting) == 0x142C, "SystemManager+0x142C Targeting");
static_assert(offsetof(SystemManager, TractorOwner) == 0x1498, "SystemManager+0x1498 TractorOwner");
static_assert(offsetof(SystemManager, TractorTargetWatcher) == 0x149C, "SystemManager+0x149C TractorTargetWatcher");
static_assert(offsetof(SystemManager, TractorEquipId) == 0x14A4, "SystemManager+0x14A4 TractorEquipId");
static_assert(offsetof(SystemManager, Repair) == 0x14A8, "SystemManager+0x14A8 RepairManager");
static_assert(offsetof(SystemManager, SelfTarget) == 0x14FC, "SystemManager+0x14FC SelfTarget");
static_assert(offsetof(SystemManager, PersonalityData) == 0x1584, "SystemManager+0x1584 PersonalityData");

// SystemManager-scoped free functions (common_systemmanager.cpp).

// FUN_062e92d0 / FUN_062e92e0 are ThrustManager::SetSteeringFallbackFlag /
// CheckSteeringGate (declared on the struct above).

