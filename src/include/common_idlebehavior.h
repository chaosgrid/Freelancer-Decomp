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
// IdleBehavior -- AI "idle/loiter" state (Behavior family)
// ============================================================================
//
// RTTI: COL .rdata:0x063a904c -> ".?AVIdleBehavior@@" (.data:0x063ebf94), CHD
// 0x063a903c numBaseClasses = 2: a direct child of Behavior, not of
// BaseFollowerBehavior (whose children Formation/Follow have 3).
//
// Slot 10 (LAB_062cd630, `OR byte[this+0x28],0x2; MOV AL,1; RET 4`) is the same
// address as FormationBehavior's slot 10; reproduced as RequestFlag() rather than a
// cast between the unrelated types.
//
// Constructor FUN_062cd530: FUN_062d2020, then the IdleIdea at +0x2c (FUN_062d9b10),
// then the vptr. Retail size 0x40.
// ============================================================================

class IdleBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (6, "Idle"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::TypeId in common_behavior.h.
    explicit IdleBehavior(int InTypeId);
    ~IdleBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639fc5c, 27 slots). Own entries:
    //   0  FUN_062c7090  scalar-deleting dtor (shared by 7 vtables; no own cleanup)
    //   1  LAB_062cd590  SetContext
    //   3  LAB_062cd5d0  `JMP FUN_062d20d0` (Behavior::OnEngage)
    //   4  FUN_062cd5e0  Disengage
    //   5  LAB_062ce3c0  SetModeFromFlags (ICF-shared with 0639fd60 / 0639ffb0)
    //   7  FUN_062cd600  Update
    //  10  LAB_062cd630  RequestFlag
    // The rest are Behavior's entries or ICF stubs.
    // ------------------------------------------------------------------

    // Slot 1, LAB_062cd590: BindContext (FUN_062d2090); on success
    // Watcher2c.SetContext(OwnerContext, SysMgr, Param2).
    bool SetContext(void* Param1, void* Param2, void* Param3);

    // Slot 3, LAB_062cd5d0: Behavior::OnEngage.
    void ResetTickState();

    // Slot 4, FUN_062cd5e0: Watcher2c.Commit(), then Behavior::OnDisengage.
    void Disengage();

    // Slot 5, LAB_062ce3c0: Readiness = Flag29 ? 3 : 1, returned.
    int SetModeFromFlags();

    // Slot 7, FUN_062cd600: OwnerContext null -> UpdateResult = 3; else
    // Watcher2c.Update(DeltaTime) (result discarded) and UpdateResult = 1.
    int Update();

    // Slot 7: IBehaviorManager::UpdateBehaviorTick dispatches through vtable +0x1c.
    int OnUpdate() override { return Update(); }

    // Slot 10, LAB_062cd630: Flags28 |= 0x2, return true (argument unread).
    bool RequestFlag();

    // Slots 4/5/10 as overrides; 5 and 10 are `RET 4` with the argument unread.
    void OnDisengage() override { Disengage(); }                           // 4 FUN_062cd5e0
    int OnEvaluateReadiness(float) override { return SetModeFromFlags(); }         // 5 LAB_062ce3c0
    bool OnApplyDirective(void*) override { return RequestFlag(); }            // 10 LAB_062cd630

public:
    // ------------------------------------------------------------------
    // Fields; +0x04..+0x29 is Behavior's.
    // ------------------------------------------------------------------

    IdleIdea Watcher2c;        // +0x2c, ends +0x40.
};

static_assert(offsetof(IdleBehavior, Watcher2c) == 0x2c, "IdleBehavior::Watcher2c offset");

// Retail object size from the factory's `PUSH 0x40` before operator_new.
static_assert(sizeof(IdleBehavior) <= 0x40, "IdleBehavior must not exceed its retail size 0x40");
