// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_idea.h"      // MoveIdea, FaceIdea
#include "common_iface.h"     // IObjRW, IObjInspect, ObjInspectOf, ObjRwFromWatchable

#include <cstddef>

// ============================================================================
// LargeShipBehavior -- AI "large ship (move)" state (Behavior family)
// ============================================================================
//
// RTTI: COL .rdata:0x063a922c -> ".?AVLargeShipBehavior@@", numBaseClasses = 2: single
// inheritance from Behavior.
//
// Constructor FUN_062cfe20: FUN_062d2020, MoveIdea at +0x30 (FUN_062d4e60), FaceIdea at
// +0xa8 (FUN_062d6750), the vptr, +0x2c = 1500.0f (0x44bb8000), zeroes +0x168/+0x16c.
// Retail size 0x180.
// ============================================================================

class LargeShipBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (11, "Large Ship (move)"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::TypeId in common_behavior.h.
    explicit LargeShipBehavior(int InTypeId);
    ~LargeShipBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639ff24, 27 slots). Own entries:
    //   0  FUN_062cfea0  dtor wrapper -> FUN_062cfec0 (unwatches Face/Move)
    //   1  LAB_062cff40  SetContext
    //   3  LAB_062cffa0  OnEngage
    //   4  FUN_062d09e0  OnDisengage
    //   5  LAB_062cfff0  OnEvaluateReadiness
    //   7  LAB_062d0050  Update
    // The rest are Behavior's entries or ICF stubs.
    // ------------------------------------------------------------------
    //
    // Slot 7, LAB_062d0050 (`SUB ESP,0xd4`, 684 instructions, no Ghidra decompile).
    // Entry guard: Handle = Sys->Targeting.GetTarget() (FUN_062ed5a0); Handle is also the
    // second argument of the immediate-threat probe. Then two cooldown gates
    // (ThreatCooldown, AvoidanceScanCooldown) and the avoidance scan, which runs when
    // AvoidanceScanCooldown < 0.0f and walks SystemManagerTimerTable::BuildAiTargetList's FAiTarget vector (stride
    // 0x88, `ADD ESI,0x88` at .text:062d0255 / 062d0459 / 062d088a); the ship's position comes
    // from SystemManager::Watcher -> -8 -> get_position (.text:062d0192-062d01b5):
    //   Pass 1 (.text:062d0200-062d025d) averages the world positions of every candidate whose
    //     cached attitude is <= -0.6f (DAT_0639f540) -- the hostile contacts' centre of mass.
    //   Pass 2 (.text:062d02a6-062d0553), which runs ONLY when ThreatCooldown has run down this tick,
    //     averages the positions of every candidate of type 0x80000/0x100000/0x200000 within
    //     1500 units, and offsets the goal 500 units AWAY from that crowd's bearing.
    //   The goal is then clamped to within +/-50 units of our own altitude, abandoned if it is
    //     already within 100 units, and finally
    //   Pass 3 (.text:062d0692-062d0892) sweeps a ray from us to the goal against every
    //     candidate's (radius + 500) sphere via RaySphereIntersect and pulls the
    //     goal back to the nearest hit -- or latches "blocked" and commits both ideas instead
    //     if we are already inside somebody's envelope.
    // Move.SetRange3c(250.0f) / SetMode(0) / SetScale40(0.6f) / SetPosition(AvoidanceGoalPos)
    // and Face.SetPosition(AvoidanceGoalPos) then steer toward the goal.
    //
    // The byte the ThreatCooldown clamp writes at .text:062d0098/062d00b6 (`[ESP+0x27]`) is
    // read at .text:062d0274 as `[ESP+0x2b]` (`PUSH EBP` at .text:062d0177 shifts the frame by
    // four) and gates pass 2 onward (.text:062d02a0 `JZ LAB_062d095a`).
    //
    // Constants: DAT_0639c430 = 0.0 (double), DAT_06399408 = 0.0f, DAT_0639f54c = 5.0f,
    // DAT_0639f550 = 30.0f, DAT_0639f540 = -0.6f, DAT_0639f554 = 0.6f, DAT_0639f558 = 250.0f,
    // DAT_0639f55c = 500.0f, DAT_0639ff90 = 500.0f, DAT_0639ff94 = 1500.0f, DAT_0639fb58 =
    // 100.0f, DAT_0639e6d4 = 50.0f, DAT_0639e6d0 = 1.5f, DAT_0639c13c = 0.5f,
    // DAT_0639a1d0 = 1.0f.
    // ------------------------------------------------------------------

    // Slot 1, LAB_062cff40 (RET 0xc).
    bool SetContext(void* Owner, void* Manager, void* Sys) override;

    // Slot 3, LAB_062cffa0.
    void OnEngage() override;

    // Slot 4, FUN_062d09e0.
    void OnDisengage() override;

    // Slot 5, LAB_062cfff0:
    //   uint Type = 0; OwnerContext->get_type(Type);       // .text:062d0004
    //   int Mode = 2;
    //   if (Type & 0x80000)                                      // .text:062d000a
    //       if (Sys->Targeting.GetTarget()) Mode = 1;
    //   if (Flag29) { Readiness = 3; return 3; }
    //   Readiness = Mode; return Mode;
    // Note the 0x80000 test is on get_type's OUT-PARAMETER, not on its int return value --
    // retail discards the return entirely (`TEST dword ptr [ESP+8], 0x80000`).
    int OnEvaluateReadiness(float Value) override;

    // Slot 7 body; see the comment above and the .cpp.
    int Update();

    // Slot 7: IBehaviorManager::UpdateBehaviorTick dispatches through vtable +0x1c.
    int OnUpdate() override { return Update(); }

private:
    // The immediate-threat probe in Update() uses one on-stack FAiTarget: the
    // `LEA ECX,[ESP+0x60]` at .text:062d0086 (after two PUSHes) and `LEA ECX,[ESP+0x58]` at
    // .text:062d00f0 name the same address.

public:
    // ------------------------------------------------------------------
    // Fields; +0x04..+0x29 is Behavior's. Retail size 0x180.
    // ------------------------------------------------------------------

    float AverageGunRange;       // +0x2c. Ctor 1500.0f; OnEngage refreshes it from
                                  // GunGroups.ComputeGunRangeStats's average when that succeeds.
                                  // No reader found.
    MoveIdea Move;             // +0x30, ends +0xa8.
    FaceIdea Face;             // +0xa8, ends +0x168.

    // +0x168. Ctor-zeroed cooldown: Update() subtracts DeltaTime; once negative the
    // avoidance scan runs and resets it to 5.0f (DAT_0639f54c).
    float AvoidanceScanCooldown;
    // +0x16c. Ctor-zeroed cooldown: decremented and clamped to 0.0f; the immediate-threat
    // branch sets it to 30.0f (DAT_0639f550).
    float ThreatCooldown;
    // +0x170 (ends +0x17c). The avoidance steering goal the scan computes (see Update);
    // read by no other method.
    Vector AvoidanceGoalPos;
};

static_assert(offsetof(LargeShipBehavior, AverageGunRange) == 0x2c, "LargeShipBehavior::AverageGunRange offset");
static_assert(offsetof(LargeShipBehavior, Move) == 0x30, "LargeShipBehavior::Move offset");
static_assert(offsetof(LargeShipBehavior, Face) == 0xa8, "LargeShipBehavior::Face offset");
static_assert(offsetof(LargeShipBehavior, AvoidanceScanCooldown) == 0x168, "LargeShipBehavior::AvoidanceScanCooldown offset");
static_assert(offsetof(LargeShipBehavior, ThreatCooldown) == 0x16c, "LargeShipBehavior::ThreatCooldown offset");
static_assert(offsetof(LargeShipBehavior, AvoidanceGoalPos) == 0x170, "LargeShipBehavior::AvoidanceGoalPos offset");
static_assert(sizeof(LargeShipBehavior) >= 0x17c, "LargeShipBehavior minimum size (largest field actually read/written)");

// Retail object size from the factory's `PUSH 0x180` before operator_new.
static_assert(sizeof(LargeShipBehavior) <= 0x180, "LargeShipBehavior must not exceed its retail size 0x180");
