// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_gotobehavior.h"  // GotoBehavior

#include <cstddef>

// ============================================================================
// FreeFlightBehavior -- AI "unescorted free-roam flight" state (Behavior
// family). UNIQUE among this family's siblings: a genuine 2-level
// inheritance chain, FreeFlightBehavior : public GotoBehavior : public
// Behavior, NOT a direct Behavior child.
// ============================================================================
//
// RTTI: COL .rdata:0x063a8d74 -> ".?AVFreeFlightBehavior@@" (.data:0x063ebe58);
// BaseClassArray @ 0x063a8d54 = [ FreeFlightBehavior, GotoBehavior, Behavior ], a
// single chain. Its vtable is GotoBehavior's with six slots replaced.
//
// Constructor FUN_062c6d10: GotoBehavior's ctor (FUN_062fd620) with the type id, then
// FreeFlightBehavior's vptr (PTR_FUN_0639f5dc) and Flag2eb = 1.
//
// Destructor: the thunk FUN_062c6dd0 calls FUN_062c6df0, a second copy of GotoBehavior's
// dtor FUN_062fd7a0 (same six watcher clears, same FUN_062d2080 tail); the own byte at
// +0x3fc needs no teardown, so ~FreeFlightBehavior is empty and chains to ~GotoBehavior.
// ============================================================================

class FreeFlightBehavior : public GotoBehavior
{
public:
    // Retail's ctor argument is the behavior type id (19, "FreeFlight"); it is forwarded to GotoBehavior.
    // See Behavior::GetTypeId() in common_behavior.h.
    explicit FreeFlightBehavior(int InTypeId);
    ~FreeFlightBehavior() override;

    // ------------------------------------------------------------------
    // Vtable .rdata:0x0639f5dc (27 slots). Against GotoBehavior's 063a2284 it replaces slots
    // 0/3/4/6/10 and fills slot 15 (Behavior's in GotoBehavior's table):
    //   0 FUN_062c6dd0 -> FUN_062c6df0   ~FreeFlightBehavior (chains into ~GotoBehavior)
    //   3 FUN_062c6ee0   OnEngage            4 LAB_062c6fc0   OnDisengage (`JMP FUN_062d20f0`)
    //   6 LAB_062d1730   evaluate (1)    10 LAB_062c6fd0   OnApplyDirective   15 LAB_062c7000 engage_engine
    // ------------------------------------------------------------------
    void OnEngage() override;
    void OnDisengage() override;
    EvaluateResult evaluate(unsigned int Context) override;
    bool OnApplyDirective(void* DirectiveOp) override;
    void engage_engine(bool bOn) override;

    // +0x3fc: FreeFlightBehavior's one own byte, past GotoBehavior's 0x3fc. OnEngage clears it
    // (.text:062c6f01); no reader found. Not written by the ctor.
    unsigned char Flag3fc;
    unsigned char Pad3fd[0x400 - 0x3fd];
};
static_assert(offsetof(FreeFlightBehavior, Flag3fc) == 0x3fc, "FreeFlightBehavior::Flag3fc offset");

// Retail object size from the factory's `PUSH 0x400` before operator_new.
static_assert(sizeof(FreeFlightBehavior) == 0x400, "FreeFlightBehavior is its retail size 0x400");
