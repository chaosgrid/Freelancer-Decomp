// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior

#include <cstddef>

// ============================================================================
// CruiseBehavior -- AI "engage cruise engine" state (Behavior family)
// ============================================================================
//
// RTTI: COL .rdata:0x063a90dc -> ".?AVCruiseBehavior@@" (.data:0x063ebfd0), numBaseClasses
// = 2 (self + Behavior): single inheritance, like IdleBehavior.
//
// Constructor FUN_062ce360: FUN_062d2020 and the vtable only; no field of its own, no
// sub-object. Slot 0 is FUN_062c7090, the scalar-deleting-dtor thunk shared by 7 vtables.
//
// Slot 1 (LAB_062cf3e0) is a bare `JMP FUN_062d2090` (the binder), also used by 0639f650,
// DrasticEvadeBehavior (0639f8f0) and 0639feb8. Slots 3/4 call ThrustManager::engage_cruise.
// Slot 5 (LAB_062ce3c0) is ICF-shared with IdleBehavior::SetModeFromFlags and 0639ffb0.
// Slot 6 (LAB_062d1730) is `return 1`, shared with 0639f5f4 and 063a0258.
// ============================================================================

class CruiseBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (12, "Cruise"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::GetTypeId() in common_behavior.h.
    explicit CruiseBehavior(int InTypeId);
    ~CruiseBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639fd4c, 27 slots):
    //   0  (0x00) FUN_062c7090        scalar-deleting-dtor thunk, ICF-shared
    //   1  (0x04) LAB_062cf3e0        SetContext() (`JMP FUN_062d2090`)
    //   3  (0x0c) FUN_062ce380        Engage()
    //   4  (0x10) FUN_062ce3a0        Disengage()
    //   5  (0x14) LAB_062ce3c0        SetModeFromFlags() (ICF-shared w/ IdleBehavior)
    //   6  (0x18) LAB_062d1730        `return 1`
    //   7  (0x1c) LAB_062ce3e0        Update()
    // The rest are Behavior's base entries (see common_behavior.h).
    // ------------------------------------------------------------------

    // Slot 1, LAB_062cf3e0: the binder FUN_062d2090 (Behavior::BindContext).
    bool SetContext(void* Param1, void* Param2, void* Param3);

    // Slot 3, FUN_062ce380: FUN_062d20d0 (Behavior::OnEngage), then
    // ThrustManager::engage_cruise(NavHelperPtr24+0x18, true).
    void Engage();

    // Slot 4, FUN_062ce3a0: ThrustManager::engage_cruise(NavHelperPtr24+0x18, false), then
    // FUN_062d20f0 (Behavior::OnDisengage).
    void Disengage();

    // Slot 5, LAB_062ce3c0: `ModeSelector0xc = Flag29 ? 3 : 1`, returned. RET 4.
    int SetModeFromFlags();

    // Slot 7, LAB_062ce3e0: `UpdateResult10 = OwnerContextPtr14 ? 1 : 3`, returned.
    int Update();

    // Slot 7: IBehaviorManager::UpdateBehaviorTick dispatches through vtable +0x1c.
    int OnUpdate() override { return Update(); }

    // Slots 3/4/5/6 of the vftable (.rdata:0639fd4c). Slot 6 is LAB_062d1730
    // `MOV EAX,1; RET 4` (shared with FreeFlightBehavior slot 6 and NullBehavior slot 5).
    void OnEngage() override { Engage(); }                              // 3 FUN_062ce380
    void OnDisengage() override { Disengage(); }                           // 4 FUN_062ce3a0
    int OnEvaluateReadiness(float) override { return SetModeFromFlags(); }         // 5 LAB_062ce3c0
    EvaluateResult evaluate(unsigned int) override { return static_cast<EvaluateResult>(1); } // 6

public:
    // ------------------------------------------------------------------
    // Fields: only Behavior's shared +0x04..+0x29 prefix; retail size 0x2c.
    // ------------------------------------------------------------------
    int TypeId;                // +0x04, FUN_062d2020's argument (Behavior::GetTypeId)
    unsigned CachedHandle08;   // +0x08, the binder's Param1->get_id() (an object id)
    int ModeSelector0xc;       // +0x0c. Written by Engage() (=1, via the shared
                               // FUN_062d20d0 effect) and SetModeFromFlags() (1 or 3).
    int UpdateResult10;        // +0x10. Update()'s own tri-state result (1=active/3=idle)
                               // and Engage()'s own write (=1, via FUN_062d20d0).
    void* OwnerContextPtr14;   // +0x14. Set by SetContext() (Param1); null-checked by Update().
    IObjDB* ObjectDb;
    float DeltaTime1c;
    // +0x20, the binder's Param2. Left void*: no CruiseBehavior method reads +0x20 (only the
    // shared FUN_062d20f0 does), so its type cannot be confirmed for this class.
    void* BehaviorMgrPtr20;
    SystemManager* NavHelperPtr24; // +0x24. Set by SetContext() (Param3); its Thrust (+0x18)
                               // receives engage_cruise.
    unsigned char Flags28;     // +0x28. Bit 0x1 cleared by FUN_062d20d0.
    unsigned char Flag29;      // +0x29. Read by SetModeFromFlags(); cleared by FUN_062d20f0.
};

static_assert(offsetof(CruiseBehavior, CachedHandle08) == 0x08, "CruiseBehavior::CachedHandle08 offset");
static_assert(offsetof(CruiseBehavior, ModeSelector0xc) == 0x0c, "CruiseBehavior::ModeSelector0xc offset");
static_assert(offsetof(CruiseBehavior, UpdateResult10) == 0x10, "CruiseBehavior::UpdateResult10 offset");
static_assert(offsetof(CruiseBehavior, OwnerContextPtr14) == 0x14, "CruiseBehavior::OwnerContextPtr14 offset");
static_assert(offsetof(CruiseBehavior, BehaviorMgrPtr20) == 0x20, "CruiseBehavior::BehaviorMgrPtr20 offset");
static_assert(offsetof(CruiseBehavior, NavHelperPtr24) == 0x24, "CruiseBehavior::NavHelperPtr24 offset");
static_assert(offsetof(CruiseBehavior, Flags28) == 0x28, "CruiseBehavior::Flags28 offset");
static_assert(offsetof(CruiseBehavior, Flag29) == 0x29, "CruiseBehavior::Flag29 offset");

// Retail object size from the factory's `PUSH 0x2c` before operator_new. The upper bound
// catches fields declared past the end of the retail object.
static_assert(sizeof(CruiseBehavior) <= 0x2c, "CruiseBehavior must not exceed its retail size 0x2c");
