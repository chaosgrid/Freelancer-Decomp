// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_idea.h"      // IdleIdea (Watcher2c)

#include <cstddef>

// ============================================================================
// DelayBehavior -- AI "wait N seconds then notify" timer state (Behavior family)
// ============================================================================
//
// RTTI: COL .rdata:0x063a9274 -> ".?AVDelayBehavior@@", numBaseClasses = 2 (self +
// Behavior): single inheritance, like IdleBehavior/CruiseBehavior/NullBehavior.
//
// Constructor FUN_062d0a10: FUN_062d2020, then an IdleIdea at +0x2c (ctor FUN_062d9b10, as
// in IdleBehavior), then the vtable. IdleBehavior's shape plus two floats at +0x40/+0x44.
// ============================================================================

class DelayBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (20, "Delay"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::GetTypeId() in common_behavior.h.
    explicit DelayBehavior(int InTypeId);
    ~DelayBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639ff9c, 27 slots):
    //   0  (0x00) FUN_062c7090        scalar-deleting-dtor thunk, ICF-shared
    //   1  (0x04) LAB_062d0a70        SetContext()
    //   3  (0x0c) FUN_062d0ac0        ResetTickState()
    //   4  (0x10) FUN_062cd5e0        Disengage() (shared with IdleBehavior)
    //   5  (0x14) LAB_062ce3c0        SetModeFromFlags() (shared with Cruise/Idle)
    //   6  (0x18) LAB_062cf5e0        `MOV EAX,2; RET 4` (14-way fold)
    //   7  (0x1c) FUN_062d0ae0        Update()
    //  10  (0x28) LAB_062d0b40        ApplyDirective()
    // The rest are Behavior's base entries (see common_behavior.h).
    // ------------------------------------------------------------------

    // Slot 1, LAB_062d0a70. The binder FUN_062d2090; on success
    // Watcher2c.SetContext(OwnerContextPtr14, NavHelperPtr24, Param2), then zeroes
    // DelayDuration and DelayElapsed.
    bool SetContext(void* Param1, void* Param2, void* Param3);

    // Slot 3, FUN_062d0ac0: FUN_062d20d0 (Behavior::OnEngage), then DelayElapsed = 0.
    void ResetTickState();

    // Slot 4, FUN_062cd5e0 (shared with IdleBehavior::Disengage): Watcher2c.Commit(), then
    // FUN_062d20f0 (Behavior::OnDisengage).
    void Disengage();

    // Slot 5, LAB_062ce3c0 (shared): `ModeSelector0xc = Flag29 ? 3 : 1`, returned. RET 4.
    int SetModeFromFlags();

    // Slot 7, FUN_062d0ae0.
    //   if (OwnerContextPtr14 == nullptr) { UpdateResult10 = 3; return 3; }
    //   Watcher2c.Update(DeltaTime1c);
    //   DelayElapsed += DeltaTime1c;     // compared unrounded, strictly greater
    //   if (DelayElapsed > DelayDuration) {
    //       DelayElapsed = <float bit-pattern 0xccbebc20>;
    //       BehaviorMgrPtr20->FUN_062d29a0(0x14, 0);   // writes manager +0x118/+0x11c
    //   }
    //   UpdateResult10 = 1;
    //   return 1;
    int Update();

    // Slot 7: IBehaviorManager::UpdateBehaviorTick dispatches through vtable +0x1c.
    int OnUpdate() override { return Update(); }

    // Slot 10, LAB_062d0b40. Sets both DelayDuration and DelayElapsed to Param->Field_0c
    // (retail quirk: not DelayElapsed = 0 as SetContext/ResetTickState do), sets Flags28
    // bit 0x2, returns true.
    bool ApplyDirective(void const* Param);

    // Slots 3/4/5/10 of the vftable (.rdata:0639ff9c); 5 and 10 are `RET 4`.
    void OnEngage() override { ResetTickState(); }                      // 3 FUN_062d0ac0
    void OnDisengage() override { Disengage(); }                           // 4 FUN_062cd5e0
    int OnEvaluateReadiness(float) override { return SetModeFromFlags(); }         // 5 LAB_062ce3c0
    bool OnApplyDirective(void* DirectiveOp) override { return ApplyDirective(DirectiveOp); } // 10

public:
    // ------------------------------------------------------------------
    // Fields: Behavior's shared +0x04..+0x29 prefix, an IdleIdea, two floats. Retail size 0x48.
    // ------------------------------------------------------------------
    int TypeId;                // +0x04, FUN_062d2020's argument (Behavior::GetTypeId)
    unsigned CachedHandle08;   // +0x08, the binder's Param1->get_id() (an object id)
    int ModeSelector0xc;       // +0x0c. Written by ResetTickState() (=1) and
                               // SetModeFromFlags() (1 or 3).
    int UpdateResult10;        // +0x10. Update()'s own tri-state result (1=active/3=idle)
                               // and ResetTickState()'s own write (=1).
    void* OwnerContextPtr14;   // +0x14. Set by SetContext() (Param1); null-checked by Update().
    IObjDB* ObjectDb;
    float DeltaTime1c;         // +0x1c. Passed to Watcher2c's Evaluate(dt) call and
                               // accumulated into DelayElapsed by Update().
    // +0x20, the binder's Param2. An IBehaviorManager: Update() (.text:062d0b14) passes it as
    // the thiscall receiver of FUN_062d29a0, which writes +0x118/+0x11c as IBehaviorManager
    // does on its own `this` (.text:062d2ea9), and sits among the named IBehaviorManager
    // methods (between set_state_graph @062d28b0 and get_scan_response @062d2b00).
    IBehaviorManager* BehaviorMgrPtr20;
    SystemManager* NavHelperPtr24; // +0x24. Set by SetContext() (Param3).
    unsigned char Flags28;     // +0x28. Bit 0x1 cleared by ResetTickState(); bit 0x2 set by
                               // ApplyDirective().
    unsigned char Flag29;      // +0x29. Read by SetModeFromFlags(); cleared by FUN_062d20f0.
    unsigned char Pad2a[0x2c - 0x2a];

    IdleIdea Watcher2c;        // +0x2c, 0x14 bytes (ctor FUN_062d9b10 at .text:062d0a42)
    float DelayDuration;             // +0x40. Zeroed by SetContext(); set to Param->Field_0c by
                               // ApplyDirective(); compared (FCOMP) against DelayElapsed in Update().
    float DelayElapsed;             // +0x44. Zeroed by SetContext()/ResetTickState(); set to
                               // Param->Field_0c by ApplyDirective(); accumulated by Update()
                               // and reset to the bit-pattern 0xccbebc20 once it exceeds
                               // DelayDuration.
};

static_assert(offsetof(DelayBehavior, CachedHandle08) == 0x08, "DelayBehavior::CachedHandle08 offset");
static_assert(offsetof(DelayBehavior, ModeSelector0xc) == 0x0c, "DelayBehavior::ModeSelector0xc offset");
static_assert(offsetof(DelayBehavior, UpdateResult10) == 0x10, "DelayBehavior::UpdateResult10 offset");
static_assert(offsetof(DelayBehavior, OwnerContextPtr14) == 0x14, "DelayBehavior::OwnerContextPtr14 offset");
static_assert(offsetof(DelayBehavior, DeltaTime1c) == 0x1c, "DelayBehavior::DeltaTime1c offset");
static_assert(offsetof(DelayBehavior, BehaviorMgrPtr20) == 0x20, "DelayBehavior::BehaviorMgrPtr20 offset");
static_assert(offsetof(DelayBehavior, NavHelperPtr24) == 0x24, "DelayBehavior::NavHelperPtr24 offset");
static_assert(offsetof(DelayBehavior, Flags28) == 0x28, "DelayBehavior::Flags28 offset");
static_assert(offsetof(DelayBehavior, Flag29) == 0x29, "DelayBehavior::Flag29 offset");
static_assert(offsetof(DelayBehavior, Watcher2c) == 0x2c, "DelayBehavior::Watcher2c offset");
static_assert(offsetof(DelayBehavior, DelayDuration) == 0x40, "DelayBehavior::DelayDuration offset");
static_assert(offsetof(DelayBehavior, DelayElapsed) == 0x44, "DelayBehavior::DelayElapsed offset");

// Retail object size from the factory's `PUSH 0x48` before operator_new. The upper bound
// catches fields declared past the end of the retail object.
static_assert(sizeof(DelayBehavior) <= 0x48, "DelayBehavior must not exceed its retail size 0x48");
