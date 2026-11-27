// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_goods.h"     // BaseWatcher, Watchable
#include "common_pub.h"       // pub::AI::DirectiveGotoOp, pub::AI::DirectiveFollowOp, pub::AI::DirectiveGuideOp

#include <cstddef>

// ============================================================================
// GuideBehavior -- AI "guide/escort a point-to-point course" state (Behavior family)
// ============================================================================
//
// RTTI: COL .rdata:0x063a91e4 -> ".?AVGuideBehavior@@", numBaseClasses = 2 (self +
// Behavior): single inheritance, no secondary vtable.
//
// Constructor FUN_062cf230: FUN_062d2020, zeroes EscortWatcher, builds DirectiveGotoOp at +0x68 and
// DirectiveFollowOp at +0xd8, zeroes DestinationWatcher, installs the vptr, then +0x11c = pi/8
// (0x3ec90fdb), +0x114 = -10.0f, +0x118 = 10.0f, zeroes +0x108/+0x44/+0x34, +0x104 = 10.0f,
// +0x38 = 1. The `CMP [this+0xfc],0; JZ` after the vptr store is always taken (dead, as in
// LootBehavior).
// ============================================================================

class GuideBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (14, "Guide"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::TypeId in common_behavior.h.
    explicit GuideBehavior(int InTypeId);
    ~GuideBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639feb4, 27 slots; 0x0639ff20 is LargeShipBehavior's COL).
    //
    //   0  FUN_062cf340  scalar-deleting dtor around FUN_062cf360
    //   1  LAB_062cf3e0  SetContext (`JMP FUN_062d2090`)
    //   3  LAB_062cf3f0  OnEngage (engage)
    //   4  FUN_062cf4f0  OnDisengage (disengage)
    //   5  FUN_062cf530  OnEvaluateReadiness
    //   6  LAB_062cf5e0  Behavior's own evaluate body (`MOV EAX,2; RET 4`), inherited
    //   7  LAB_062cf5f0  OnUpdate -> Update()
    //  10  LAB_062cfc70  OnApplyDirective (apply a DirectiveGuideOp)
    //  21  LAB_062cf300  OnObjectLeftSystem (shared with BaseFollowerBehavior)
    //  24  LAB_062cf2f0  get_target
    // Every other slot is Behavior's own entry.
    // ------------------------------------------------------------------

    // Slot 1, LAB_062cf3e0: `JMP FUN_062d2090` (BindContext), shared with Flee/Cruise/
    // DrasticEvade.
    bool SetContext(IObjRW* Owner, IBehaviorManager* Manager, SystemManager* Sys) override;

    // Slot 3, LAB_062cf3f0 (Engage): re-arm, capture the manager's Goto (+0x17c) and Follow
    // (+0x1b8) behaviors, hand them GotoOp/FollowOp and engage both.
    void OnEngage() override;

    // Slot 4, FUN_062cf4f0: disengage both siblings and release their directive bit.
    void OnDisengage() override;

    // Slot 5, FUN_062cf530 (RET 4, argument unread).
    int OnEvaluateReadiness(float Value) override;

    // Slot 10, LAB_062cfc70 (RET 4): take a pub::AI::DirectiveGuideOp.
    bool OnApplyDirective(void* DirectiveOp) override;

    // ------------------------------------------------------------------
    // Slot 7, LAB_062cf5f0 (575 instructions), body in common_guidebehavior.cpp.
    //
    // The per-tick brain of the "guide/escort" behavior: an NPC leads
    // the watched object (EscortWatcher, in practice the player) along a course, hands the actual
    // flying to ONE OF TWO SIBLING Behavior objects owned by the manager, and emits a
    // numbered "guide event" to the manager's notification callback whenever the escort's
    // progress crosses a threshold.
    //
    // Structure, in retail order:
    //   1. 062cf5f0-062cf62e  Guards. No EscortWatcher -> clear Flags28 bit 0x2 and return
    //      UpdateResult = 3. No OwnerContext (or, re-tested, no EscortWatcher) -> return 3
    //      WITHOUT touching Flags28. Retail re-reads +0x2c twice more; nothing in between can
    //      change it, so the port evaluates it once.
    //   2. 062cf630-062cf6bd  Refresh the guide destination GuideDestination from GotoOp.GotoKind
    //      (retail reads it at raw offset +0x74 == GotoOp+0x0C):
    //        0 -> DestinationWatcher's world position via GetIfacePositionOrZero; if DestinationWatcher is unset,
    //             clear Flags28 bit 0x2, NotifyBehaviorDone(0xe, 1) and return 2.
    //        1 -> a straight copy of GotoOp.Destination (raw +0x78 == GotoOp+0x10).
    //        anything else -> GuideDestination is left at whatever the last tick put there.
    //   3. 062cf6c0-062cf729  ArrivalRadius = GotoOp.ArrivalRange (raw +0xb8); then three steering
    //      queries on the SysMgr+0x14fc element: Clearance (to EscortWatcher),
    //      RadialClearance (to GuideDestination) and Dist (EscortWatcher position to GuideDestination).
    //   4. 062cf74e-062cf7ef  Two independent "is the escorted object actually mine" tests,
    //      either of which clears bEscortFollowing: (a) EscortWatcher's get_behavior_id() == 10 and its
    //      get_formation_leader() has the same object id as the SysMgr element's own +0x84
    //      ship -- this one also latches bLeaderIsSelf; (b) EscortWatcher's id appears in the
    //      +0x1364 sub-object's formation-followers list (FUN_062ec520).
    //   5. 062cf7f1-062cf8b2  SampleElapsed accumulates DeltaTime until it passes SampleInterval
    //      (ctor default 10.0f). On overflow it samples: ProgressDelta = previous LastSampleDistance - Dist
    //      (distance closed since the last sample, positive == closing), ApproachAngleRad =
    //      acos(clamped dot of the escorted ship's negated forward axis with the normalized
    //      GuideDestination-minus-its-own-position delta) == how far off-course it is pointing, then
    //      LastSampleDistance = Dist and SampleElapsed = 0. Only on a sampling tick does step 6 run.
    //   6. 062cf8b4-062cfba5  The event table. Two mirror-image halves selected by bEscortFollowing:
    //        bEscortFollowing set  (following): 8 = fell behind hard (ProgressDelta < ProgressLowThreshold),
    //                                  6 = caught up hard but pointing away (ProgressDelta >
    //                                      ProgressHighThreshold and ApproachAngleRad > CourseAngleThreshold),
    //                                  5 = caught up hard and pointing the right way,
    //                                  7 = neither, but Clearance dropped below EscortMinClearance.
    //                                  8/6/7 also clear bEscortFollowing; 5 does not.
    //        bEscortFollowing clear (leading):  3 = fell behind hard, 1 = caught up and on course and
    //                                  inside EscortMaxClearance, 2 = steady but outside EscortMaxClearance,
    //                                  4 = the "leader is me" shortcut. 1 and 2 set bEscortFollowing.
    //      The Dist < RadialClearance byte gates them unevenly (retail's asymmetry):
    //      8/6/7/3/2 are suppressed when it is set, 4's second entry
    //      (via 062cfb2d) REQUIRES it, and 1, 5 and 4's bLeaderIsSelf entry ignore it entirely.
    //      Each event is a NotifyManagerGuideEvent(SelfId, WatchedId, Code, Clearance, GuideDestination).
    //   7. 062cfa15 / 062cfba7  Hand off to a sibling: bEscortFollowing set -> FollowDelegate drives and
    //      GotoDelegate is parked; bEscortFollowing clear -> the reverse. A transition disengages the
    //      outgoing one (slot 4), pushes this class's matching directive struct into the
    //      incoming one (slot 10) and engages it (slot 3); no transition just writes
    //      DeltaTime into the active one's own +0x1c and ticks it (slot 7).
    //   8. 062cfbc7-062cfc66  Termination test: if the radial clearance to GuideDestination exceeds
    //      ArrivalRadius, or the intercept clearance to EscortWatcher exceeds GotoOp.TetherNearRange (raw
    //      +0xc8), the directive is still running -> UpdateResult = 1. Otherwise clear
    //      Flags28 bit 0x2, NotifyBehaviorDone(0xe, 0) and finish with UpdateResult = 2.
    //      (GotoDelegate's slot-7 result == 2 skips straight to the intercept test.)
    //
    // Compare polarity: the pair at 062cfab4-062cfad0 runs two compares through one AH -- the
    // first FNSTSW feeds `TEST AH,0x41`/`JNZ` (strictly greater), the second `TEST AH,0x5`/`JP`
    // (not less) and, at 062cfb3d, `TEST AH,0x1` (C0 of that second compare).
    //
    // Retail oddities kept: the `TEST ECX,ECX; JNZ` at 062cf66d is dead (ECX is zero since
    // 062cf635), and event 5's arm alone skips the Dist<RadialClearance suppression and
    // leaves bEscortFollowing alone.
    int Update();

    // Slot 7: IBehaviorManager::UpdateBehaviorTick dispatches through vtable +0x1c.
    int OnUpdate() override { return Update(); }
    // ------------------------------------------------------------------

    // Slot 21, LAB_062cf300 (RET 4), the same body as BaseFollowerBehavior's slot 21 on this
    // class's own +0x2c: release EscortWatcher when its object id equals the argument.
    void OnObjectLeftSystem(void* Arg) override;

    // Slot 24, LAB_062cf2f0: EscortWatcher's object (target - 8) or null.
    IObjRW const* get_target() override;

private:
    // FUN_062d2a70, `__thiscall bool(IBehaviorManager*, uint, uint, int, float, Vector const*)`, RET 0x14 --
    // the lead-event sibling of IBehaviorManager::NotifyFollowEvent (062d29c0) and
    // NotifyTetherEvent (062d2a10). ALL FOUR of its XREFs (062cf9b3/062cfa09/062cfb1f/062cfba2)
    // are inside Update() above. Retail: false unless Sys->Targeting.AttackState (+0x1434) is
    // nonzero and the current behavior index (+0xc0) is 0xe (Guide's own dispatch slot); then
    // ContentCbPtr (+0x110) slot 2 lead_object_event with the five arguments and true, or, with
    // no callback, Sys->ExternalIface->is_player() (+0xbc) for effect and false.
    static bool NotifyManagerGuideEvent(IBehaviorManager* BehaviorMgr, unsigned SelfId, unsigned WatchedId,
                                        int EventCode, float Clearance, Vector const* Point);

    // The eight guide-event call sites in Update() are byte-identical apart from the event
    // code, so they are factored into this one helper. It reproduces retail's own evaluation
    // ORDER as well as its arguments: the SysMgr element's own +0x84 ship is asked for its
    // id FIRST (retail parks it in EBX), then the escorted object, then the manager call --
    // which matters because both are virtual calls on live game objects, not pure getters the
    // compiler may reorder.
    void FireGuideEvent(IObjRW* Watched, FAiTarget* NavElem, int EventCode, float Clearance);

public:
    // ------------------------------------------------------------------
    // Fields, ascending offset; +0x04..+0x29 is Behavior's. Retail size 0x124.
    // ------------------------------------------------------------------

    BaseWatcher EscortWatcher;       // +0x2c (ends +0x34). Zeroed by ctor; cleared by dtor. The
                                 // escorted object.
    float GuideRange;               // +0x34. Ctor 0. OnApplyDirective copies DirectiveGuideOp::GuideRange;
                                  // OnEvaluateReadiness passes it to FUN_062e4500 as the range.
    unsigned char bStartFollowing;        // +0x38. Set to 1 by ctor. OnApplyDirective overwrites it from
                                  // DirectiveGuideOp::bStartFollowing; OnEngage seeds bEscortFollowing from it.
    unsigned char Pad39[0x3c - 0x39];
    float EscortMaxClearance;                // +0x3c. OnApplyDirective: GotoParams.TetherNearRange
                                   // (062cfd62). Update() compares the intercept clearance with
                                   // it (062cfac3).
    unsigned TetherFarRangeBits;  // +0x40. OnApplyDirective: GotoParams.TetherFarRange as a raw
                                   // dword (062cfd68). No reader found.
    float EscortMinClearance;                // +0x44. Zeroed by ctor; written by OnEngage /
                                   // OnApplyDirective from a vector magnitude. Update() compares
                                   // the intercept clearance with it (062cf9be).
    Vector GuideDestination;               // +0x48..+0x54. The live "guide destination" point. Update()
                                   // refreshes it each tick (062cf637-062cf6bd) either from DestinationWatcher's
                                   // world position or from GotoOp.Destination, then uses it as the
                                   // reference point for every clearance/distance query and passes
                                   // its address as the 5th argument of every guide event.
    float ArrivalRadius;                // +0x54. Update() reloads it from GotoOp.ArrivalRange every tick
                                   // (062cf6c0, a raw dword copy from +0xb8) and compares the radial
                                   // clearance against it to decide whether the directive is done.
    float ProgressDelta;                // +0x58. Written only on a sampling tick (062cf81a): the previous
                                   // LastSampleDistance minus the current distance, i.e. how much ground was
                                   // closed since the last sample. Drives the guide event table.
    float ApproachAngleRad;                // +0x5c. Written only on a sampling tick (062cf894): the acos of
                                   // the clamped dot between the escorted ship's negated forward axis
                                   // and the normalized delta to GuideDestination -- an angle in radians,
                                   // compared against CourseAngleThreshold.
    // +0x60/+0x64: the manager's Goto (BehaviorDispatchTable entry 2, +0x17c) and Follow
    // (entry 17, +0x1b8) behaviors, captured by OnEngage. Update drives them through slots
    // 3/4/7/10 and their +0x1c delta, handing GotoDelegate &GotoOp and FollowDelegate
    // &FollowOp, alternating by bEscortFollowing.
    Behavior* GotoDelegate;
    Behavior* FollowDelegate;

    pub::AI::DirectiveGotoOp GotoOp;    // +0x68, ends +0xd8.
    pub::AI::DirectiveFollowOp FollowOp; // +0xd8, ends +0xfc.

    BaseWatcher DestinationWatcher;        // +0xfc (ends +0x104). Zeroed by ctor; cleared by dtor.
    float SampleInterval;               // +0x104. Ctor 10.0f; OnApplyDirective:
                                   // DirectiveGuideOp::SampleInterval.
    float SampleElapsed;                // +0x108. Zeroed by ctor; Update() adds DeltaTime
                                    // (062cf8a5). The sampling-period accumulator.
    float LastSampleDistance;                 // +0x10c. Not touched by ctor. Distance to
                                     // GuideDestination at the last sampling tick.
    float Field110;                  // +0x110. Not touched by ctor. OnApplyDirective:
                                      // (float)DirectiveGuideOp::Field_AC.
    float ProgressLowThreshold;                // +0x114. Ctor -10.0f; OnApplyDirective:
                                       // DirectiveGuideOp::ProgressLowThreshold.
    float ProgressHighThreshold;                // +0x118. Ctor 10.0f; OnApplyDirective:
                                        // DirectiveGuideOp::ProgressHighThreshold.
    float CourseAngleThreshold;            // +0x11c. Ctor pi/8; OnApplyDirective:
                                         // DirectiveGuideOp::CourseAngleThreshold.
    unsigned char bEscortFollowing;               // +0x120. The "escort is following me" latch that picks
                                          // which delegate drives; seeded from bStartFollowing by OnEngage.
};

static_assert(offsetof(GuideBehavior, EscortWatcher) == 0x2c, "GuideBehavior::EscortWatcher offset");
static_assert(offsetof(GuideBehavior, GuideRange) == 0x34, "GuideBehavior::GuideRange offset");
static_assert(offsetof(GuideBehavior, bStartFollowing) == 0x38, "GuideBehavior::bStartFollowing offset");
static_assert(offsetof(GuideBehavior, EscortMaxClearance) == 0x3c, "GuideBehavior::EscortMaxClearance offset");
static_assert(offsetof(GuideBehavior, TetherFarRangeBits) == 0x40, "GuideBehavior::TetherFarRangeBits offset");
static_assert(offsetof(GuideBehavior, EscortMinClearance) == 0x44, "GuideBehavior::EscortMinClearance offset");
static_assert(offsetof(GuideBehavior, GuideDestination) == 0x48, "GuideBehavior::GuideDestination offset");
static_assert(offsetof(GuideBehavior, ArrivalRadius) == 0x54, "GuideBehavior::ArrivalRadius offset");
static_assert(offsetof(GuideBehavior, ProgressDelta) == 0x58, "GuideBehavior::ProgressDelta offset");
static_assert(offsetof(GuideBehavior, ApproachAngleRad) == 0x5c, "GuideBehavior::ApproachAngleRad offset");
static_assert(offsetof(GuideBehavior, GotoDelegate) == 0x60, "GuideBehavior::GotoDelegate offset");
static_assert(offsetof(GuideBehavior, FollowDelegate) == 0x64, "GuideBehavior::FollowDelegate offset");
static_assert(offsetof(GuideBehavior, GotoOp) == 0x68, "GuideBehavior::GotoOp offset");
static_assert(offsetof(GuideBehavior, FollowOp) == 0xd8, "GuideBehavior::FollowOp offset");
static_assert(offsetof(GuideBehavior, DestinationWatcher) == 0xfc, "GuideBehavior::DestinationWatcher offset");
static_assert(offsetof(GuideBehavior, SampleInterval) == 0x104, "GuideBehavior::SampleInterval offset");
static_assert(offsetof(GuideBehavior, SampleElapsed) == 0x108, "GuideBehavior::SampleElapsed offset");
static_assert(offsetof(GuideBehavior, LastSampleDistance) == 0x10c, "GuideBehavior::LastSampleDistance offset");
static_assert(offsetof(GuideBehavior, Field110) == 0x110, "GuideBehavior::Field110 offset");
static_assert(offsetof(GuideBehavior, ProgressLowThreshold) == 0x114, "GuideBehavior::ProgressLowThreshold offset");
static_assert(offsetof(GuideBehavior, ProgressHighThreshold) == 0x118, "GuideBehavior::ProgressHighThreshold offset");
static_assert(offsetof(GuideBehavior, CourseAngleThreshold) == 0x11c, "GuideBehavior::CourseAngleThreshold offset");
static_assert(offsetof(GuideBehavior, bEscortFollowing) == 0x120, "GuideBehavior::bEscortFollowing offset");

// Retail object size from the factory's `PUSH 0x124` before operator_new.
static_assert(sizeof(GuideBehavior) == 0x124, "GuideBehavior size == retail operator_new(0x124)");

