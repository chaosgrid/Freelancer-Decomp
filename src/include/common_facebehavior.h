// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_goods.h"     // BaseWatcher, Watchable
#include "common_csys.h"      // Matrix
#include "common_ini.h"       // Vector
#include "common_misc.h"      // PlayerTrailAvailable
#include "common_idea.h"      // MoveIdea, FaceIdea, AfterburnerIdea

#include <cstddef>

class SystemManager;

// ============================================================================
// FaceBehavior -- AI "point at target" state machine (Behavior family): the engine-kill /
// coast-and-face manoeuvre, tuned by the personality's EngineKillParams.
// ============================================================================
//
// RTTI: COL .rdata:0x063a8e94 -> ".?AVFaceBehavior@@", numBaseClasses = 2 (self +
// Behavior): single inheritance, no secondary vtable.
//
// Constructor FUN_062c88e0: FUN_062d2020, zeroes TargetKind2c/Watcher30, then
//   +0x68  MoveIdea         FUN_062d4e60 (0x78 bytes)
//   +0xe0  FaceIdea         FUN_062d6750 (0xc0 bytes)
//   +0x1a0 AfterburnerIdea  FUN_062da500 (0x14 bytes, ends at Flag1b4)
// then the vptr (PTR_FUN_0639f95c) and the remaining own fields (see the field list). The
// `CMP [this+0x30],0; JZ` right after the vptr store is always taken (dead, as in
// LootBehavior).
// ============================================================================

class FaceBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (15, "Face"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::GetTypeId() in common_behavior.h.
    explicit FaceBehavior(int InTypeId);

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639f95c, 27 slots); unlisted slots are Behavior's base entries:
    //   0  (0x00) FUN_062c8a10        scalar-deleting-dtor thunk -> real dtor FUN_062c8a30
    //   1  (0x04) LAB_062c8ac0        SetContext()
    //   3  (0x0c) FUN_062c9320        OnEngage()
    //   4  (0x10) FUN_062c9380        OnDisengage()
    //   5  (0x14) LAB_062c8c10        OnEvaluateReadiness()
    //   6  (0x18) LAB_062c8d80        evaluate()
    //   7  (0x1c) LAB_062c8de0        Update()
    //  10  (0x28) LAB_062c8b30        OnApplyDirective()
    //  12  (0x30) FUN_062c93c0        set_direction()
    //  13  (0x34) LAB_062c9530        set_throttle()
    //  16  (0x40) 062ed730            set_afterburner(): empty
    //  21  (0x54) LAB_062c89c0        OnObjectLeftSystem()
    //  24  (0x60) LAB_062c8a00        get_target()
    // ------------------------------------------------------------------

    // FUN_062c8a30 (called by the slot-0 thunk FUN_062c8a10): set_pointer(nullptr) on
    // +0xf4 (SubObjectB's watcher), +0x7c (SubObjectA's), +0x30 (Watcher30), then
    // FUN_062d2080.
    ~FaceBehavior() override;

    // Slot 1, LAB_062c8ac0. The binder; on success SetContext(Param1, Param3, Param2) on
    // SubObjectB and SubObjectC (not SubObjectA), then
    // Field1cc = Behavior_InvokeManagerSlotF0(ThreatSourcePtr14, 0, 1.0f) (FUN_062e5b20).
    bool SetContext(void* Param1, void* Param2, void* Param3) override;

    // Slot 3, FUN_062c9320: Behavior::OnEngage (FUN_062d20d0), zeroes
    // Flag1c5/EngineKillStateElapsed/Flag1b4/FaceAxisX/Y/Z/AfterburnerElapsed,
    // SetCruiseLock(0, 3), and FUN_062e91d0(NavHelperPtr24+0x18, Flag1c4 == 0).
    void OnEngage() override;

    // Slot 4, FUN_062c9380. Clears Watcher30 unless Flags28 bit 0x2; Commit() on SubObjectB
    // and SubObjectC (afterburner off); then FUN_062d20f0 (Behavior::OnDisengage).
    void OnDisengage() override;

    // Slot 5, LAB_062c8c10 (RET 4, argument unread). Fast path: Flags28 bit 0x2,
    // TargetKind2c == 1 and a live Watcher30 give Flag29 ? 3 : 1. Otherwise re-derives
    // Watcher30 (FUN_062e74a0) and runs the gate checks (+0x1364 FUN_062ecb20, the manager's
    // Pathfind +0x260/+0x264, +0x16e4 FUN_062e8280, a distance test against DAT_0639f9c8 and
    // FUN_062e8830); see the .cpp.
    int OnEvaluateReadiness(float Unused) override;

    // Slot 6, LAB_062c8d80 (RET 4). 2 when PlayerTrailAvailable is false; otherwise
    // TargetManager::RetargetOwner (FUN_062ee940) with the owner's get_target(), returning 1.
    EvaluateResult evaluate(unsigned int Context) override;

    // Slot 7, LAB_062c8de0 (410 instructions); see the .cpp for the per-branch trace.
    int Update();

    // Slot 7: IBehaviorManager::UpdateBehaviorTick dispatches through vtable +0x1c.
    int OnUpdate() override { return Update(); }

    // Slot 10, LAB_062c8b30. Reads Message->+0x44 into Flag1c4 and Message->+0xc (kind 1..4)
    // into TargetKind2c: kind 1 re-points Watcher30 at FUN_062e74a0's result; kind 2 copies
    // a Vector from +0x14 into TargetPosition5c; kind 3 a Matrix from +0x20 into
    // OrientMatrix38; kind 4 only sets the flag. Reached cases set Flags28 bit 0x2 and return
    // true; a null Message or an out-of-range kind clears it and returns false.
    bool OnApplyDirective(void* DirectiveOp) override;

    // Slot 12, FUN_062c93c0. The gate is `Dot(Offset, Offset) > 0.0f` (DAT_06399408 == 0.0f,
    // strictly greater). On the set path Flag1b4=1,
    // FaceAxisX=-Offset.y, FaceAxisY=-Offset.x, FaceAxisZ=Offset.z; then, when
    // IBehaviorManager::get_turn_sensitivity_factor() (FUN_062d4b20, manager +0x164) is
    // strictly greater than 0.0 (DAT_0639c430
    // is an all-zero double), both FaceAxisX and FaceAxisY are multiplied by
    // 1.0f/Sensitivity (DAT_0639a1d0 == 1.0f); then each is clamped into
    // [-1.0f, +1.0f] (DAT_06399404 == -1.0f, DAT_0639a1d0 == +1.0f).
    void set_direction(Vector const& Offset) override;

    // Slot 13, LAB_062c9530: `MOV byte [ECX+0x1d4],1; RET 4` -- an abort request Update()'s
    // entry guard consumes; the throttle argument is unread.
    void set_throttle(float Throttle) override;

    // Slot 16 is 062ed730, an empty `RET 4`, replacing Behavior's FUN_062d2110.
    void set_afterburner(bool bOn) override { (void)bOn; }

    // Slot 21, LAB_062c89c0: clears Watcher30 when its object's get_id() is Id.
    void OnObjectLeftSystem(void* Arg) override;

    // Slot 24, LAB_062c8a00: Watcher30's watched pointer minus 8, or null.
    IObjRW const* get_target() override;

    // The same `Ptr ? Ptr - 8 : 0` the slot-24 body computes, kept as the non-virtual helper
    // Update() uses (retail inlines it there).
    IObjRW* GetWatchedRawBlock() const;

public:
    // ------------------------------------------------------------------
    // Fields, ascending offset; +0x04..+0x29 is Behavior's shared prefix. Retail size 0x1dc.
    // ------------------------------------------------------------------
    int TypeId;                  // +0x04, FUN_062d2020's argument (Behavior::GetTypeId)
    unsigned CachedHandle08;     // +0x08, the binder's Param1->get_id() (an object id)
    int ModeSelector0xc;         // +0x0c, OnEvaluateReadiness's result.
    int UpdateResult10;          // +0x10. Update()'s own tri-state result.
    void* ThreatSourcePtr14;     // +0x14. Set by SetContext() (Param1).
    IObjDB* ObjectDb;
    float DeltaTime1c;            // +0x1c. Per-tick delta stored by the manager.
    // +0x20, the binder's Param2. An IBehaviorManager: set_direction passes it as the
    // receiver of FUN_062d4b20 (get_turn_sensitivity_factor, .text:062c9432), and
    // .text:062c8c77 walks its +0x0c (Pathfind).
    IBehaviorManager* BehaviorMgrPtr20;
    // +0x24, the binder's Param3, the SystemManager. Update() reads its ExternalIface
    // (+0x1580, .text:062c8e0a), passes its Thrust (+0x18) to FUN_062e92e0, refreshes its
    // SelfTarget cache (+0x14fc+0xc/+0x18/+0x7e) and reads PersonalityData.EngineKillParams
    // (+0x16d4..+0x16e4).
    SystemManager* NavHelperPtr24;
    unsigned char Flags28;       // +0x28. Bit 0x2 = "have a valid target" (set/cleared by
                                  // ApplyTargetMessage(), tested by SetModeFromFlags()/Disengage()).
    unsigned char Flag29;        // +0x29. Tested by SetModeFromFlags() (forces mode 3 either path).
    unsigned char Pad2a[0x2c - 0x2a];

    int TargetKind2c;             // +0x2c. Zeroed by ctor. "Target kind" selector (1=watched-object
                                   // candidate, 2=fixed Vector, 3=fixed Matrix, 4=flag-only) --
                                   // written by OnApplyDirective() from Message->+0xc.
    BaseWatcher Watcher30;        // +0x30 (ends +0x38). Zeroed by ctor; the dtor clears it.
    Matrix OrientMatrix38;        // +0x38 (ends +0x5c). OnApplyDirective kind 3 copies 9 dwords
                                   // from Message->+0x20.
    Vector TargetPosition5c;      // +0x5c (ends +0x68). OnApplyDirective kind 2 copies 3 dwords
                                   // from Message->+0x14.

    // +0x68. Unlike the sibling behaviors, SetContext/OnDisengage never call it; only the
    // dtor clears its watcher (+0x7c).
    MoveIdea SubObjectA;    // +0x68.
    FaceIdea SubObjectB;    // +0xe0.
    // +0x1a0 (ctor .text:062c8934). Slot 0 in SetContext (.text:062c8b08), slot 4 in
    // OnDisengage (062c93b5) and Update (062c8f16), slot 1 in Update (062c8ec5).
    AfterburnerIdea SubObjectC;
    unsigned char Flag1b4;          // +0x1b4. Zeroed by ctor. Read/written directly by Update()
                                     // and set_direction() (1 on the set path, cleared by
                                     // OnEngage()/the clear path).
    unsigned char Pad1b5[0x1b8 - 0x1b5];
    float FaceAxisX;                  // +0x1b8. Zeroed by ctor/OnEngage(). set_direction()
                                      // writes -Offset.y (pre-clamp) on the set path.
    float FaceAxisY;                  // +0x1bc. Zeroed by ctor/OnEngage(). set_direction()
                                      // writes -Offset.x (pre-clamp) on the set path.
    float FaceAxisZ;                  // +0x1c0. Zeroed by ctor/OnEngage(). set_direction()
                                      // writes Offset.z (unclamped, unnegated) on the set path.
    unsigned char Flag1c4;           // +0x1c4. Set to 1 by ctor. OnApplyDirective() overwrites it
                                      // from Message->+0x44; OnEngage() passes Flag1c4 == 0 to
                                      // FUN_062e91d0.
    unsigned char Flag1c5;           // +0x1c5. Zeroed by ctor/OnEngage(). Update()'s "resolved
                                      // this tick" flag.
    unsigned char Pad1c6[0x1c8 - 0x1c6];
    // +0x1c8: seconds the current engine-kill state has been held (x87 accesses at
    // .text:062c8f26 / 062c8fd8 / 062c8fbe), compared against EngineKillParams'
    // EngineKillSearchTime / EngineKillFaceTime.
    float EngineKillStateElapsed;                  // +0x1c8. Zeroed by ctor/OnEngage().
    float Field1cc;                  // +0x1cc. Zeroed by ctor; SetContext() stores
                                      // Behavior_InvokeManagerSlotF0(ThreatSourcePtr14, 0, 1.0f).
    float Field1d0;                  // +0x1d0. Set to 0.9f by ctor. No other access found.
    unsigned char Flag1d4;            // +0x1d4. Zeroed by ctor. Update()'s entry guard checks and
                                       // clears it (early-out -> UpdateResult10=2); set_throttle()
                                       // (slot 13) sets it.
    unsigned char Pad1d5[0x1d8 - 0x1d5];
    // +0x1d8: seconds spent turning toward the target while the bearing is still wide
    // (.text:062c8ea6 / 062c8e68), capped by EngineKillParams.EngineKillAfterburnerTime.
    float AfterburnerElapsed;                   // +0x1d8. Zeroed by ctor/OnEngage().
};

static_assert(offsetof(FaceBehavior, CachedHandle08) == 0x08, "FaceBehavior::CachedHandle08 offset");
static_assert(offsetof(FaceBehavior, ModeSelector0xc) == 0x0c, "FaceBehavior::ModeSelector0xc offset");
static_assert(offsetof(FaceBehavior, UpdateResult10) == 0x10, "FaceBehavior::UpdateResult10 offset");
static_assert(offsetof(FaceBehavior, ThreatSourcePtr14) == 0x14, "FaceBehavior::ThreatSourcePtr14 offset");
static_assert(offsetof(FaceBehavior, DeltaTime1c) == 0x1c, "FaceBehavior::DeltaTime1c offset");
static_assert(offsetof(FaceBehavior, BehaviorMgrPtr20) == 0x20, "FaceBehavior::BehaviorMgrPtr20 offset");
static_assert(offsetof(FaceBehavior, NavHelperPtr24) == 0x24, "FaceBehavior::NavHelperPtr24 offset");
static_assert(offsetof(FaceBehavior, Flags28) == 0x28, "FaceBehavior::Flags28 offset");
static_assert(offsetof(FaceBehavior, Flag29) == 0x29, "FaceBehavior::Flag29 offset");
static_assert(offsetof(FaceBehavior, TargetKind2c) == 0x2c, "FaceBehavior::TargetKind2c offset");
static_assert(offsetof(FaceBehavior, Watcher30) == 0x30, "FaceBehavior::Watcher30 offset");
static_assert(offsetof(FaceBehavior, OrientMatrix38) == 0x38, "FaceBehavior::OrientMatrix38 offset");
static_assert(offsetof(FaceBehavior, TargetPosition5c) == 0x5c, "FaceBehavior::TargetPosition5c offset");
static_assert(offsetof(FaceBehavior, SubObjectA) == 0x68, "FaceBehavior::SubObjectA offset");
static_assert(offsetof(FaceBehavior, SubObjectB) == 0xe0, "FaceBehavior::SubObjectB offset");
static_assert(offsetof(FaceBehavior, SubObjectC) == 0x1a0, "FaceBehavior::SubObjectC offset");
static_assert(offsetof(FaceBehavior, Flag1b4) == 0x1b4, "FaceBehavior::Flag1b4 offset");
static_assert(offsetof(FaceBehavior, FaceAxisX) == 0x1b8, "FaceBehavior::FaceAxisX offset");
static_assert(offsetof(FaceBehavior, FaceAxisY) == 0x1bc, "FaceBehavior::FaceAxisY offset");
static_assert(offsetof(FaceBehavior, FaceAxisZ) == 0x1c0, "FaceBehavior::FaceAxisZ offset");
static_assert(offsetof(FaceBehavior, Flag1c4) == 0x1c4, "FaceBehavior::Flag1c4 offset");
static_assert(offsetof(FaceBehavior, Flag1c5) == 0x1c5, "FaceBehavior::Flag1c5 offset");
static_assert(offsetof(FaceBehavior, EngineKillStateElapsed) == 0x1c8, "FaceBehavior::EngineKillStateElapsed offset");
static_assert(offsetof(FaceBehavior, Field1cc) == 0x1cc, "FaceBehavior::Field1cc offset");
static_assert(offsetof(FaceBehavior, Field1d0) == 0x1d0, "FaceBehavior::Field1d0 offset");
static_assert(offsetof(FaceBehavior, Flag1d4) == 0x1d4, "FaceBehavior::Flag1d4 offset");
static_assert(offsetof(FaceBehavior, AfterburnerElapsed) == 0x1d8, "FaceBehavior::AfterburnerElapsed offset");
static_assert(sizeof(FaceBehavior) >= 0x1dc, "FaceBehavior minimum size (largest field actually read/written this pass)");

// Retail object size from the factory's `PUSH 0x1dc` before operator_new. The upper bound
// catches fields declared past the end of the retail object.
static_assert(sizeof(FaceBehavior) <= 0x1dc, "FaceBehavior must not exceed its retail size 0x1dc");
