// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_ini.h"       // Vector

#include <cstddef>

// ============================================================================
// DrasticEvadeBehavior -- AI "drastic evade" directive/behavior (Behavior family)
// ============================================================================
//
// RTTI: COL .rdata:063a8e4c (offset 0), TypeDescriptor .data:063ebeb4
// ".?AVDrasticEvadeBehavior@@", ClassHierarchyDescriptor 063a8e3c with two bases
// (DrasticEvadeBehavior, Behavior) -- a single-inheritance child of Behavior.
//
// Constructor FUN_062c8730: FUN_062d2020(TypeId),
// +0x40 = 0, vptr 0639f8ec, +0x30 = 4.0f, +0x34/+0x38/+0x3c = 0.
//
// Vtable .rdata:0639f8ec, 27 slots. Overrides of Behavior's table:
//    0  FUN_062c7090  scalar-deleting dtor, ICF-shared by 7 behavior vtables
//    1  LAB_062cf3e0  `JMP FUN_062d2090` -- Behavior::BindContext
//    3  FUN_062c8790  OnEngage
//    4  FUN_062c87b0  OnDisengage
//    5  LAB_062c8760  OnEvaluateReadiness
//    7  LAB_062c8800  OnUpdate
//   10  LAB_062c87c0  OnApplyDirective
// Every other slot is Behavior's own entry.
class DrasticEvadeBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (18, "DrasticEvade"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::TypeId in common_behavior.h.
    explicit DrasticEvadeBehavior(int InTypeId);
    ~DrasticEvadeBehavior() override;

    // Slot 1, LAB_062cf3e0: `JMP FUN_062d2090`.
    bool SetContext(void* Owner, void* Manager, void* Sys) override;

    // Slot 3, FUN_062c8790: FUN_062d20d0, then +0x2c = 0.
    void OnEngage() override;

    // Slot 4, FUN_062c87b0: FUN_062d20f0, then Flags28 |= 0x2.
    void OnDisengage() override;

    // Slot 5, LAB_062c8760, `RET 4`, argument unread.
    // With Flags28 bit 0x2: 3 when Flag29 is set, else 0. Without it: 3 when Flag29 is
    // set, else 2. Stored at +0x0c and returned.
    int OnEvaluateReadiness(float Unused) override;

    // Slot 7, LAB_062c8800. Switch on EvadeMode:
    //   0 -> timer += delta; owner's IObjAffect::set_nudge_vec({Offset.x, Offset.y, 0})
    //        while timer < duration (result 1), else set_nudge_vec({0,0,0}) (result 2);
    //   1 -> timer += delta; Behavior_UpdateCruiseGate(Sys+0x18, -1.0f or 0.0f, 0),
    //        result 1 / 2 the same way;
    //   other -> result 3.
    int OnUpdate() override;

    // Slot 10, LAB_062c87c0, `RET 4`: takes a
    // pub::AI::DirectiveDrasticEvadeOp: EvadeMode = Op.Mode; when it is 0, NudgeOffset =
    // Op.NudgeX..Z; Flags28 |= 0x2; DurationThreshold30 = Op.Duration; returns true.
    bool OnApplyDirective(void* DirectiveOp) override;

public:
    float StateTimer2c;        // +0x2c. OnUpdate accumulates the delta; OnEngage zeroes it. Not
                               // written by the ctor (retail leaves it to OnEngage).
    float DurationThreshold30; // +0x30. Ctor 4.0f; OnApplyDirective copies the op's Duration.
    Vector NudgeOffset;       // +0x34. Ctor zero; OnApplyDirective copies the op's NudgeX..Z when the mode is 0.
    int EvadeMode;          // +0x40. Ctor 0; OnApplyDirective copies the op's Mode; OnUpdate switches on it.
};

static_assert(offsetof(DrasticEvadeBehavior, StateTimer2c) == 0x2c, "DrasticEvadeBehavior::StateTimer2c offset");
static_assert(offsetof(DrasticEvadeBehavior, DurationThreshold30) == 0x30, "DrasticEvadeBehavior::DurationThreshold30 offset");
static_assert(offsetof(DrasticEvadeBehavior, NudgeOffset) == 0x34, "DrasticEvadeBehavior::NudgeOffset offset");
static_assert(offsetof(DrasticEvadeBehavior, EvadeMode) == 0x40, "DrasticEvadeBehavior::EvadeMode offset");
// Retail object size: the factory pushes 0x44 before operator new.
static_assert(sizeof(DrasticEvadeBehavior) == 0x44, "DrasticEvadeBehavior size must match retail 0x44");
