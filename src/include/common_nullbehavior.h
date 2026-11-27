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
// call BindContext, so Behavior's fields keep FUN_062d2020's values.
// ============================================================================

class NullBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (0, "NULL"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::TypeId in common_behavior.h.
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
    bool SetContext(IObjRW* Param1, IBehaviorManager* Param2, SystemManager* Param3);

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

    // No fields of its own: Behavior's +0x04..+0x29, as FUN_062d2020 (called at
    // .text:062d170a) leaves them.
};
static_assert(sizeof(NullBehavior) == 0x2c, "NullBehavior size == retail operator_new argument at .text:062d16e8");
