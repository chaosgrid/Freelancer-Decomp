// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_pub.h"       // pub::AI::DirectiveGotoOp, pub::AI::DirectiveDockOp

#include <cstddef>

// ============================================================================
// FleeBehavior -- AI "break off and run" state (Behavior family)
// ============================================================================
//
// RTTI: COL .rdata:0x063a8dbc -> ".?AVFleeBehavior@@" (.data:0x063ebe7c), numBaseClasses
// = 2 (self + Behavior): single inheritance, no secondary vtable. Retail's own debug strings in
// .rdata name three of its methods: "FleeBehavior::Perform()" (slot 7, 0639f6b8 and
// 0639f750), "FleeBehavior::deactivate()" (slots 3 and 4, 0639f7a8) and
// "FleeBehavior::event_handler()" (slot 22, 0639f810).
//
// ----------------------------------------------------------------------------
// Constructor: FUN_062c7020. FUN_062d2020 (Behavior's
// own field initialisation), DirectiveGotoOp at +0x4c, DirectiveDockOp at +0xbc,
// byte +0x2c = 0, byte +0x38 = 0, +0x34 = 0, vptr, +0x30 = 4.
// ============================================================================

class FleeBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (4, "Flee"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::TypeId in common_behavior.h.
    explicit FleeBehavior(int InTypeId);
    ~FleeBehavior() override;

    // ------------------------------------------------------------------
    // Vtable .rdata:0639f64c, 27 slots. Overrides of Behavior's table:
    //   0  FUN_062c7090  scalar-deleting dtor, ICF-shared by 7 behavior vtables
    //   1  LAB_062cf3e0  `JMP FUN_062d2090` -- Behavior::BindContext
    //   3  LAB_062c7570  OnEngage below
    //   4  LAB_062c7610  OnDisengage below
    //   5  LAB_062c70b0  OnEvaluateReadiness below
    //   7  LAB_062c72c0  Perform
    //  22  LAB_062c7710  OnEvent below
    // Every other slot is Behavior's own entry.
    // ------------------------------------------------------------------

    // Slot 1, LAB_062cf3e0: `JMP FUN_062d2090`.
    bool SetContext(IObjRW* Owner, IBehaviorManager* Manager, SystemManager* Sys) override;

    // Slot 3, LAB_062c7570. Re-reads the three delegates out
    // of the manager's dispatch table; if a flee is running (bFleeActive), re-arms whichever
    // delegate it drives through that delegate's own slot 3.
    void OnEngage() override;

    // Slot 4, LAB_062c7610. Same delegate refresh; with
    // bFollowFleeingTarget set, FormationDelegate's slot 4; otherwise the mode-selected delegate's slot 4 and, if
    // the manager's current directive is the delegate's own kind and bHasSavedDirective says Perform
    // took a copy of it, the copied op is handed back through the delegate's slot 10.
    void OnDisengage() override;

    // Slot 5, LAB_062c70b0, `RET 4`, the argument unread.
    // The flee evaluation: hull+shield ratio against the personality's flee threshold,
    // the TargetManager's hostile power ratio against a per-level table, and the "flee
    // when no ready weapon" switch. Returns 0 (flee) or 2, or 3 when +0x29 is set; the
    // value is also stored at +0x0c.
    int OnEvaluateReadiness(float Unused) override;

    // Slot 7, LAB_062c72c0, retail "FleeBehavior::Perform()".
    int Perform();
    int OnUpdate() override { return Perform(); }

    // Slot 22, LAB_062c7710, retail
    // "FleeBehavior::event_handler()". Same delegate refresh, then tail-jumps the event
    // into the active delegate's own slot 22.
    void OnEvent(void* Event) override;

private:
    // The delegate refresh all four of slots 3/4/5/22 begin with:
    // GotoDelegate = Manager->BehaviorDispatchTable[2] (+0x17c, Goto), DockDelegate = [7] (+0x190,
    // Dock), FormationDelegate = [10] (+0x19c, Formation).
    void RefreshDelegates();

public:
    // ------------------------------------------------------------------
    // Fields, ascending offset; +0x04..+0x29 is Behavior's.
    // ------------------------------------------------------------------

    unsigned char bFleeActive;     // +0x2c. "A flee is running": Perform latches it on the first
                               // tick and clears it when the delegate finishes; OnEvaluateReadiness sets it
                               // when the watched target's own manager is fleeing.
    unsigned char Pad2d[0x30 - 0x2d];
    int FleeReason;               // +0x30. The FleeReason handed to the content callback: ctor 4;
                               // OnEvaluateReadiness writes 1 (target is fleeing), 2 (hull+shield ratio),
                               // 0 (hostile power ratio) or 3 (no ready weapon).
    int FleeDestType;               // +0x34. FleeDestinationType Perform got back: 1/3 -> Goto, 2 -> Dock.
    unsigned char bFollowFleeingTarget;     // +0x38. OnEvaluateReadiness sets it when the watched target is itself
                               // fleeing; then FormationDelegate (Formation) is the delegate.
    unsigned char Pad39[0x3c - 0x39];
    Behavior* GotoDelegate;         // +0x3c. Manager->BehaviorDispatchTable[2], a GotoBehavior.
    Behavior* DockDelegate;         // +0x40. Manager->BehaviorDispatchTable[7], a DockBehavior.
    Behavior* FormationDelegate;         // +0x44. Manager->BehaviorDispatchTable[10], a FormationBehavior.
    unsigned char bHasSavedDirective;     // +0x48. Perform sets it when the current directive is the
                               // delegate's own kind and it copied that directive's op.
    unsigned char Pad49[0x4c - 0x49];

    pub::AI::DirectiveGotoOp GotoOp;  // +0x4c, 0x70 bytes.
    pub::AI::DirectiveDockOp DockOp;  // +0xbc, 0x2c bytes, ends at +0xe8.
};

static_assert(offsetof(FleeBehavior, bFleeActive) == 0x2c, "FleeBehavior::bFleeActive offset");
static_assert(offsetof(FleeBehavior, FleeReason) == 0x30, "FleeBehavior::FleeReason offset");
static_assert(offsetof(FleeBehavior, FleeDestType) == 0x34, "FleeBehavior::FleeDestType offset");
static_assert(offsetof(FleeBehavior, bFollowFleeingTarget) == 0x38, "FleeBehavior::bFollowFleeingTarget offset");
static_assert(offsetof(FleeBehavior, GotoDelegate) == 0x3c, "FleeBehavior::GotoDelegate offset");
static_assert(offsetof(FleeBehavior, DockDelegate) == 0x40, "FleeBehavior::DockDelegate offset");
static_assert(offsetof(FleeBehavior, FormationDelegate) == 0x44, "FleeBehavior::FormationDelegate offset");
static_assert(offsetof(FleeBehavior, bHasSavedDirective) == 0x48, "FleeBehavior::bHasSavedDirective offset");
static_assert(offsetof(FleeBehavior, GotoOp) == 0x4c, "FleeBehavior::GotoOp offset");
static_assert(offsetof(FleeBehavior, DockOp) == 0xbc, "FleeBehavior::DockOp offset");

// Retail object size: the factory pushes 0xe8 before operator new.
static_assert(sizeof(FleeBehavior) == 0xe8, "FleeBehavior size must match retail 0xe8");
