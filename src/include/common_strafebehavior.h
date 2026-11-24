// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h" // Behavior
#include "common_idea.h"     // MoveIdea, FaceIdea, AfterburnerIdea (embedded sub-objects)

#include <cstddef>

struct IObjInspect;          // common_iface.h -- SelectStrafePoint's second argument

// ============================================================================
// StrafeBehavior -- behavior type 13 ("Strafe"), factory body at .text:062d1cc0
// (operator new(0x1bc), then FUN_062c9540). Single-inheritance child of Behavior (RTTI).
//
// Vtable .rdata:0639f9dc, 27 slots. Overrides of Behavior's table:
//    0  FUN_062c95c0  scalar-deleting dtor around FUN_062c95e0
//    1  LAB_062c9660  SetContext
//    3  LAB_062ca1e0  OnEngage
//    4  FUN_062ca310  OnDisengage
//    5  LAB_062c9720  OnEvaluateReadiness
//    7  LAB_062c9d20  OnUpdate
//   10  LAB_062c96e0  OnApplyDirective
// Every other slot is Behavior's own entry.
// ============================================================================
class StrafeBehavior : public Behavior
{
public:
    // FUN_062c9540: FUN_062d2020(TypeId), MoveIdea +0x70, FaceIdea +0xe8,
    // AfterburnerIdea +0x1a8, vptr, +0x60 = 2, +0x48 = 2.
    explicit StrafeBehavior(int InTypeId);
    // FUN_062c95e0 (slot 0 FUN_062c95c0 is the scalar-deleting thunk): the two embedded
    // ideas' BaseWatcher teardowns (+0xfc, +0x84) and ~Behavior -- the members' own dtors.
    ~StrafeBehavior() override;

    StrafeBehavior(StrafeBehavior const&) = delete;
    StrafeBehavior& operator=(StrafeBehavior const&) = delete;

    // Slot 1, LAB_062c9660 (RET 0xc): FUN_062d2090; on success the three ideas' slot 0
    // (FaceIdea, MoveIdea, AfterburnerIdea, in that order) with (Owner, Sys, Manager),
    // then +0x60 = 2, +0x48 = 2, byte +0x64 = 0, +0x6c = 0, +0x68 = 999. Returns the
    // binder's result.
    bool SetContext(void* Owner, void* Manager, void* Sys) override;

    // Slot 3, LAB_062ca1e0: FUN_062d20d0, SetAvoidanceAllowed(1, 3), the angular trigger range
    // (+0x2c), the ship's forward extent (+0x44), the strafe direction (-column 2), and the
    // personality's StrafeParams into +0x34/+0x58/+0x5c; +0x48 = 2.
    void OnEngage() override;

    // Slot 4, FUN_062ca310: MoveIdea.PublishTargetId(0), the three ideas' Commit
    // (MoveIdea, FaceIdea, AfterburnerIdea), then `JMP FUN_062d20f0`.
    void OnDisengage() override;

    // Slot 5, LAB_062c9720 (RET 4, argument unread): 1 (and +0x60 = 1) when the gun
    // convergence gate is clear, the target is a ship-class object, this ship is one too,
    // and the target's speed squared is <= 400; else 2; 3 with +0x29. Stored at +0x0c.
    int OnEvaluateReadiness(float Unused) override;

    // Slot 7, LAB_062c9d20.
    int OnUpdate() override;

    // Slot 10, LAB_062c96e0 (RET 4): Op non-null and FUN_062e74a0(Sys+0x34, &Op.TargetId) non-null
    // -> Flags28 |= 2, true; else Flags28 &= ~2, false.
    bool OnApplyDirective(void* DirectiveOp) override;

    // FUN_062c9810. Mode 0 aims at Target (or its subtarget),
    // mode 1 picks a point StrafeDistance34 off to the side; both arms hand the point to the
    // MoveIdea and FaceIdea, and every path ends with StrafeMode48 = Mode.
    void SelectStrafePoint(int Mode, IObjInspect const* Target);

    // ---- StrafeBehavior's own fields ----
    float TriggerRange2c;        // +0x2c. OnEngage: angular distance * (1 + the 0.1-step
                                 // loop counter); OnUpdate mode 0 re-picks the strafe point
                                 // once the obstacle/target distance drops below it.
    unsigned char Opaque30[0x34 - 0x30];
    float StrafeDistance34;      // +0x34. OnEngage: Personality.StrafeParams.StrafeRunAwayDistance.
    Vector StrafeDir38;          // +0x38. OnEngage: -column 2 of the ship's rotation.
    float ForwardExtent44;       // +0x44. OnEngage: the watched object's archetype Min.z.
    int StrafeMode48;            // +0x48. 0/1/2; ctor, SetContext and OnEngage set 2.
    Vector StrafePoint4c;        // +0x4c
    float ScaleNear58;           // +0x58. OnEngage: StrafeParams.StrafeAttackThrottle (MoveIdea
                                 // scale when the bearing to the target is below pi/8).
    float ScaleFar5c;            // +0x5c. OnEngage: StrafeParams.StrafeTurnThrottle.
    int Field60;                 // +0x60. Ctor/SetContext 2; OnEvaluateReadiness writes 1.
    unsigned char Field64;       // +0x64. SetContext/OnEngage clear it.
    unsigned char Pad65[0x68 - 0x65];
    int Field68;                 // +0x68. SetContext: 999.
    int Field6c;                 // +0x6c. SetContext: 0.
    MoveIdea Move;         // +0x70,  ctor FUN_062d4e60
    FaceIdea Face;         // +0xe8,  ctor FUN_062d6750
    AfterburnerIdea Afterburner;  // +0x1a8, ctor FUN_062da500
};

static_assert(offsetof(StrafeBehavior, TriggerRange2c) == 0x2c, "StrafeBehavior::TriggerRange2c offset");
static_assert(offsetof(StrafeBehavior, StrafeDistance34) == 0x34, "StrafeBehavior::StrafeDistance34 offset");
static_assert(offsetof(StrafeBehavior, StrafeDir38) == 0x38, "StrafeBehavior::StrafeDir38 offset");
static_assert(offsetof(StrafeBehavior, ForwardExtent44) == 0x44, "StrafeBehavior::ForwardExtent44 offset");
static_assert(offsetof(StrafeBehavior, StrafeMode48) == 0x48, "StrafeBehavior::StrafeMode48 offset");
static_assert(offsetof(StrafeBehavior, StrafePoint4c) == 0x4c, "StrafeBehavior::StrafePoint4c offset");
static_assert(offsetof(StrafeBehavior, ScaleNear58) == 0x58, "StrafeBehavior::ScaleNear58 offset");
static_assert(offsetof(StrafeBehavior, ScaleFar5c) == 0x5c, "StrafeBehavior::ScaleFar5c offset");
static_assert(offsetof(StrafeBehavior, Field60) == 0x60, "StrafeBehavior::Field60 offset");
static_assert(offsetof(StrafeBehavior, Field64) == 0x64, "StrafeBehavior::Field64 offset");
static_assert(offsetof(StrafeBehavior, Field68) == 0x68, "StrafeBehavior::Field68 offset");
static_assert(offsetof(StrafeBehavior, Field6c) == 0x6c, "StrafeBehavior::Field6c offset");
static_assert(offsetof(StrafeBehavior, Move) == 0x70, "StrafeBehavior::Move offset");
static_assert(offsetof(StrafeBehavior, Face) == 0xe8, "StrafeBehavior::Face offset");
static_assert(offsetof(StrafeBehavior, Afterburner) == 0x1a8, "StrafeBehavior::Afterburner offset");
static_assert(sizeof(StrafeBehavior) == 0x1bc, "StrafeBehavior size must match retail 0x1bc");
