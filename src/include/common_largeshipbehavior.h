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
    // the descriptor's +0x04 -- see Behavior::GetTypeId() in common_behavior.h.
    explicit LargeShipBehavior(int InTypeId);
    ~LargeShipBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639ff24, 27 slots). Own entries:
    //   0  FUN_062cfea0  dtor wrapper -> FUN_062cfec0 (unwatches SubObjectB/SubObjectA)
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
    // AvoidanceScanCooldown < 0.0f and walks BuildAiTargetList's FAiTarget vector (stride
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
    // SubObjectA.SetRange3c(250.0f) / SetMode(0) / SetScale40(0.6f) / SetPosition(AvoidanceGoalPos)
    // and SubObjectB.SetPosition(AvoidanceGoalPos) then steer toward the goal.
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
    //   uint Type = 0; ThreatSourcePtr14->get_type(Type);       // .text:062d0004
    //   int Mode = 2;
    //   if (Type & 0x80000)                                      // .text:062d000a
    //       if (Sys->Targeting.GetTarget()) Mode = 1;
    //   if (Flag29) { ModeSelector0xc = 3; return 3; }
    //   ModeSelector0xc = Mode; return Mode;
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
    // Fields; +0x04..+0x29 is Behavior's shared prefix. Retail size 0x180.
    // ------------------------------------------------------------------
    int TypeId;                 // +0x04, FUN_062d2020's argument (Behavior::GetTypeId)
    unsigned CachedHandle08;    // +0x08, the binder's Owner->get_id() (an object id)
    int ModeSelector0xc;        // +0x0c. OnEngage (=1), OnEvaluateReadiness (1/2/3).
    int UpdateResult10;         // +0x10. Update()'s result (1/2/3).
    void* ThreatSourcePtr14;    // +0x14. SetContext() Param1; get_type is called on it.
    IObjDB* ObjectDb;
    float DeltaTime1c;           // +0x1c. Zeroed by Behavior::OnEngage (FUN_062d20d0).
    // +0x20. SetContext() Param2. Left void*: no LargeShipBehavior method
    // (.text:062cfe20..062d0a10) reads it back.
    void* BehaviorMgrPtr20;
    SystemManager* NavHelperPtr24; // +0x24. SetContext() Param3; its +0x142c (Targeting) and
                                 // +0x12c are read.
    unsigned char Flags28;      // +0x28. Bit 0x1 cleared by OnEngage.
    unsigned char Flag29;       // +0x29. Read by OnEvaluateReadiness().
    unsigned char Pad2a[0x2c - 0x2a];

    float Field2c;               // +0x2c. Ctor 1500.0f; OnEngage refreshes it from
                                  // ComputeWeaponSlotStat's OutAverage when that succeeds.
    MoveIdea SubObjectA;             // +0x30, ends +0xa8.
    FaceIdea SubObjectB;             // +0xa8, ends +0x168.

    // +0x168. Ctor-zeroed cooldown: Update() subtracts DeltaTime1c; once negative the
    // avoidance scan runs and resets it to 5.0f (DAT_0639f54c).
    float AvoidanceScanCooldown;
    // +0x16c. Ctor-zeroed cooldown: decremented and clamped to 0.0f; the immediate-threat
    // branch sets it to 30.0f (DAT_0639f550).
    float ThreatCooldown;
    // +0x170 (ends +0x17c). The avoidance steering goal the scan computes (see Update);
    // read by no other method.
    Vector AvoidanceGoalPos;
};

static_assert(offsetof(LargeShipBehavior, CachedHandle08) == 0x08, "LargeShipBehavior::CachedHandle08 offset");
static_assert(offsetof(LargeShipBehavior, ModeSelector0xc) == 0x0c, "LargeShipBehavior::ModeSelector0xc offset");
static_assert(offsetof(LargeShipBehavior, UpdateResult10) == 0x10, "LargeShipBehavior::UpdateResult10 offset");
static_assert(offsetof(LargeShipBehavior, ThreatSourcePtr14) == 0x14, "LargeShipBehavior::ThreatSourcePtr14 offset");
static_assert(offsetof(LargeShipBehavior, DeltaTime1c) == 0x1c, "LargeShipBehavior::DeltaTime1c offset");
static_assert(offsetof(LargeShipBehavior, BehaviorMgrPtr20) == 0x20, "LargeShipBehavior::BehaviorMgrPtr20 offset");
static_assert(offsetof(LargeShipBehavior, NavHelperPtr24) == 0x24, "LargeShipBehavior::NavHelperPtr24 offset");
static_assert(offsetof(LargeShipBehavior, Flags28) == 0x28, "LargeShipBehavior::Flags28 offset");
static_assert(offsetof(LargeShipBehavior, Flag29) == 0x29, "LargeShipBehavior::Flag29 offset");
static_assert(offsetof(LargeShipBehavior, Field2c) == 0x2c, "LargeShipBehavior::Field2c offset");
static_assert(offsetof(LargeShipBehavior, SubObjectA) == 0x30, "LargeShipBehavior::SubObjectA offset");
static_assert(offsetof(LargeShipBehavior, SubObjectB) == 0xa8, "LargeShipBehavior::SubObjectB offset");
static_assert(offsetof(LargeShipBehavior, AvoidanceScanCooldown) == 0x168, "LargeShipBehavior::AvoidanceScanCooldown offset");
static_assert(offsetof(LargeShipBehavior, ThreatCooldown) == 0x16c, "LargeShipBehavior::ThreatCooldown offset");
static_assert(offsetof(LargeShipBehavior, AvoidanceGoalPos) == 0x170, "LargeShipBehavior::AvoidanceGoalPos offset");
static_assert(sizeof(LargeShipBehavior) >= 0x17c, "LargeShipBehavior minimum size (largest field actually read/written)");

// Retail object size from the factory's `PUSH 0x180` before operator_new.
static_assert(sizeof(LargeShipBehavior) <= 0x180, "LargeShipBehavior must not exceed its retail size 0x180");
