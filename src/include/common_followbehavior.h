// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior, BaseFollowerBehavior
#include "common_goods.h"     // BaseWatcher
#include "common_ini.h"       // Vector
#include "common_idea.h"      // MatchThrottle2Idea, FaceIdea, MoveIdea

#include <cstddef>

// ============================================================================
// FollowBehavior -- RTTI ".?AVFollowBehavior@@" (COL .rdata:063a9004, TypeDescriptor
// .data:063ebf74; BaseClassArray {FollowBehavior, BaseFollowerBehavior, Behavior}, a single
// inheritance chain). Behavior type 17 ("Follow"); factory `PUSH 0x2bc` before the ctor
// FUN_062ccaf0, so sizeof is 0x2bc.
//
// The +0x2c..+0x2a0 block (LeadWatcher, the five Ideas, the delegates, the flags, the
// +0x290 offset) is BaseFollowerBehavior's: FUN_062cb2b0 builds it for both subclasses.
//
// Vtable .rdata:0639fbe4 (29 slots). Own slots; the rest are BaseFollowerBehavior's:
//    3  FUN_062ccc80  OnEngage            4  FUN_062ccce0  OnDisengage
//    5  FUN_062ccb70  OnEvaluateReadiness            6  LAB_062ccc10  evaluate
//   10  FUN_062cd3d0  OnApplyDirective          27  LAB_062ccd30  UpdateFollowerSteering
//   28  LAB_062cd4c0  GetRotatedOffset
// ============================================================================
class FollowBehavior : public BaseFollowerBehavior
{
public:
    // FUN_062ccaf0 (RET 4): FUN_062cb2b0(TypeId); +0x281 = +0x280 = 0; vptr 0639fbe4;
    // +0x2a0 = 150.0f; +0x290/+0x294/+0x298 = 0; +0x285 = 0; +0x2ac = 150.0f;
    // +0x2b0 = 2.0f; +0x2b4 = 400.0f; +0x2b8 = 100.0f; +0x2a4 = 1.
    explicit FollowBehavior(int InTypeId);

    // Slot 3, FUN_062ccc80.
    void OnEngage() override;
    // Slot 4, FUN_062ccce0.
    void OnDisengage() override;
    // Slot 5, FUN_062ccb70 (RET 4, argument unread).
    int OnEvaluateReadiness(float Value) override;
    // Slot 6, LAB_062ccc10 (RET 4, argument unread).
    EvaluateResult evaluate(unsigned int Context) override;
    // Slot 10, FUN_062cd3d0 (RET 4): take a pub::AI::DirectiveFollowOp.
    bool OnApplyDirective(void* DirectiveOp) override;
    // Slot 27, LAB_062ccd30 (536 instructions): the per-tick follow work.
    void UpdateFollowerSteering() override;
    // Slot 28, LAB_062cd4c0: Offset290 rotated out of the leader's frame, by value.
    Vector GetRotatedOffset() override;

private:
    // Not a retail function: the engage toggle UpdateFollowerSteering inlines twice (062ccf83-062cd043 and
    // 062cd0a3-062cd164) -- Enabled2a4 follows IsWithinInterceptRangeOfIface and each
    // transition goes to IBehaviorManager::NotifyFollowEvent (FUN_062d29c0).
    void UpdateInterceptEngageState();

public:
    // ---- FollowBehavior's own fields (+0x2a0..+0x2bc) ----
    float SpeedParam2a0;        // +0x2a0. Ctor 150.0f; OnApplyDirective copies op+0x10.
    unsigned char Enabled2a4;   // +0x2a4. Ctor 1; UpdateFollowerSteering toggles it on range transitions.
    unsigned char Pad2a5[3];
    float Distance2a8;          // +0x2a8. Not ctor-set; OnApplyDirective: |Offset290|.
    float Field2ac;             // +0x2ac. Ctor 150.0f.
    float LeadClearanceScale;             // +0x2b0. Ctor 2.0f; UpdateFollowerSteering's weight factor.
    float EngageRange;             // +0x2b4. Ctor 400.0f; OnEvaluateReadiness's range; OnApplyDirective's failure path
                                // copies op+0x20.
    float FollowRangeBias;             // +0x2b8. Ctor 100.0f; UpdateFollowerSteering's range bias.
};

static_assert(offsetof(FollowBehavior, SpeedParam2a0) == 0x2a0, "FollowBehavior::SpeedParam2a0 offset");
static_assert(offsetof(FollowBehavior, Enabled2a4) == 0x2a4, "FollowBehavior::Enabled2a4 offset");
static_assert(offsetof(FollowBehavior, Distance2a8) == 0x2a8, "FollowBehavior::Distance2a8 offset");
static_assert(offsetof(FollowBehavior, Field2ac) == 0x2ac, "FollowBehavior::Field2ac offset");
static_assert(offsetof(FollowBehavior, LeadClearanceScale) == 0x2b0, "FollowBehavior::LeadClearanceScale offset");
static_assert(offsetof(FollowBehavior, EngageRange) == 0x2b4, "FollowBehavior::EngageRange offset");
static_assert(offsetof(FollowBehavior, FollowRangeBias) == 0x2b8, "FollowBehavior::FollowRangeBias offset");
static_assert(sizeof(FollowBehavior) == 0x2bc, "FollowBehavior size == retail operator_new(0x2bc)");
