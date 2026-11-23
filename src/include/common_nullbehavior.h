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
// NullBehavior -- AI "do nothing" no-op state (Behavior family)
// ============================================================================
//
// RTTI: COL .rdata:0x063a98bc -> ".?AVNullBehavior@@" (.data:0x063ec510), CHD 0x063a98ac
// numBaseClasses = 2: single inheritance from Behavior.
//
// No ctor of its own: the factory FUN_062d16d0 (descriptor .rdata:0x063a0030) does
// `operator new(0x2c)`, FUN_062d2020(newObj, TypeId), `[newObj] = PTR_FUN_063a0244`.
//
// The "no behavior" placeholder. Its own slots are constant returners; SetContext does not
// call BindContext, so the prefix fields keep FUN_062d2020's values.
// ============================================================================

class NullBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (0, "NULL"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::GetTypeId() in common_behavior.h.
    explicit NullBehavior(int InTypeId);
    ~NullBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x063a0244, 27 slots). Own entries:
    //   0  FUN_062c7090  shared scalar-deleting-dtor thunk (7 vtables)
    //   1  LAB_062d76a0  SetContext: `MOV AL,1; RET 0xc` (ICF-shared with StopIdea's slot 0)
    //   3  .text:062a8920  bare RET (OnEngage)
    //   4  .text:062a8920  bare RET (OnDisengage)
    //   5  LAB_062d1730  `MOV EAX,1; RET 4` (OnEvaluateReadiness)
    //   7  `MOV EAX,2; RET`, ICF-labelled Archetype::Solar::get_class_type (Update)
    // The rest are Behavior's entries or ICF stubs.
    // ------------------------------------------------------------------

    // Slot 1: returns true, touches nothing (no BindContext call).
    bool SetContext(void* Param1, void* Param2, void* Param3);

    // Slot 7: returns 2.
    int Update();

    // Slot 7: IBehaviorManager::UpdateBehaviorTick dispatches through vtable +0x1c.
    int OnUpdate() override { return Update(); }

    // Slots 3 and 4 are not Behavior's FUN_062d20d0 / FUN_062d20f0 but the bare `RET` at
    // .text:062a8920 (ICF-labelled Exclusion::AsteroidExclusionZone); the base bodies would
    // reset +0x0c/+0x10/+0x1c/+0x28 and clear the PathfindManager's +0x64. Base slot 5 is
    // _purecall.
    void OnEngage() override {}                                   // 3 .text:062a8920 RET
    void OnDisengage() override {}                                   // 4 .text:062a8920 RET
    int OnEvaluateReadiness(float) override { return 1; }                    // 5 LAB_062d1730

    // ---- Behavior's shared +0x04..+0x29 block. Needed for the 0x2c size: the manager's
    // SetTickDelta writes +0x1c through a bare Behavior* on every tick (.text:062d335d).
    // FUN_062d2020 (called at .text:062d170a) writes +0x04, +0x08, +0x0c (=2), +0x10 (=1),
    // +0x14, +0x18, +0x1c, +0x24 and +0x28/+0x29. See common_behavior.h for why the block is
    // re-declared per class (TODO there). -----
    int TypeId;                  // +0x04, the ctor argument (id 0, "NULL").
    unsigned CachedHandle08;     // +0x08, zeroed by FUN_062d2020.
    int ModeSelector0xc;         // +0x0c, set to 2 by FUN_062d2020.
    int UpdateResult10;          // +0x10, set to 1 by FUN_062d2020.
    void* OwnerContext;          // +0x14, zeroed.
    IObjDB* ObjectDb; // +0x18, zeroed as a dword.
    float DeltaTime;             // +0x1c, zeroed; the slot SetTickDelta writes.
    void* BehaviorMgrPtr;        // +0x20. Not written by FUN_062d2020 and SetContext does not
                                 // bind, so it stays null.
    SystemManager* NavHelper;    // +0x24, zeroed by FUN_062d2020.
    unsigned char Flags28;       // +0x28, bits 0 and 1 cleared by FUN_062d2020 (`AND CL,0xfc`).
    unsigned char Flag29;        // +0x29, zeroed.
    unsigned char Pad2a[0x2c - 0x2a]; // +0x2a, alignment only.
};
static_assert(offsetof(NullBehavior, TypeId) == 0x04, "NullBehavior::TypeId offset");
static_assert(offsetof(NullBehavior, CachedHandle08) == 0x08, "NullBehavior::CachedHandle08 offset");
static_assert(offsetof(NullBehavior, ModeSelector0xc) == 0x0c, "NullBehavior::ModeSelector0xc offset");
static_assert(offsetof(NullBehavior, UpdateResult10) == 0x10, "NullBehavior::UpdateResult10 offset");
static_assert(offsetof(NullBehavior, OwnerContext) == 0x14, "NullBehavior::OwnerContext offset");
static_assert(offsetof(NullBehavior, DeltaTime) == 0x1c, "NullBehavior::DeltaTime offset");
static_assert(offsetof(NullBehavior, BehaviorMgrPtr) == 0x20, "NullBehavior::BehaviorMgrPtr offset");
static_assert(offsetof(NullBehavior, NavHelper) == 0x24, "NullBehavior::NavHelper offset");
static_assert(offsetof(NullBehavior, Flags28) == 0x28, "NullBehavior::Flags28 offset");
static_assert(offsetof(NullBehavior, Flag29) == 0x29, "NullBehavior::Flag29 offset");
static_assert(sizeof(NullBehavior) == 0x2c, "NullBehavior size == retail operator_new argument at .text:062d16e8");

// Retail object size from the factory's `PUSH 0x2c` before operator_new.
static_assert(sizeof(NullBehavior) <= 0x2c, "NullBehavior must not exceed its retail size 0x2c");
