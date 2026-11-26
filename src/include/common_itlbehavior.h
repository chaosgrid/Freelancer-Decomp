// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_iface.h"     // IObjAffect::RequestCB
#include "common_ini.h"       // Vector

#include <cstddef>

// ============================================================================
// InstantTradeLaneBehavior -- AI trade-lane state machine (Behavior family)
// ============================================================================
//
// RTTI: COL .rdata:0x063a9138 -> ".?AVInstantTradeLaneBehavior@@" (.data:0x063ec00c), CHD
// 0x063a918c numBaseClasses = 3: [self, Behavior (BCD 0x063a8c78, COMDAT-shared with
// DockBehavior's), IObjAffect::RequestCB (BCD 0x063a914c, mdisp 0x2c, attributes 9)].
// Secondary COL .rdata:0x063a919c (offset 0x2c).
//
// Constructor FUN_062cee50: FUN_062d2020, then the vptrs (PTR_FUN_0639fe44 at +0,
// PTR_LAB_0639fe38 at +0x2c) and nothing else; the port zero-initialises the fields anyway.
// Sole factory FUN_062d1af0 allocates 0x5c.
//
// Destructor: slot 0 is the shared thunk FUN_062c7090 (7 vtables), which goes straight to
// FUN_062d2080; no own cleanup.
//
// Primary vtable .rdata:0x0639fe44 (27 slots). Own entries:
//   1  LAB_062cee70  SetContext (RET 0xc)
//   3  FUN_062cef10  TurnOff
//   4  FUN_062cef40  RequestExitTradeLane
//   5  LAB_062ceeb0  UpdateMode
//   7  LAB_062cefb0  Update
//  10  LAB_062cf0f0  AttachDirective
//  22  LAB_062cf1d0  HandleTargetEvent
// The rest are Behavior's entries or ICF stubs.
// Secondary vtable .rdata:0x0639fe38 (RequestCB, 1 slot): LAB_062cf150 OnResult.
// ============================================================================

// InstantTradeLaneBehavior::State30 (+0x30), the state Update (LAB_062cefb0) switches on. States
// 1 and 2 are never entered; anything above 6 runs like 1.
enum EItlState
{
    kItlStateStart = 0,     // OnEngage's value: looks up the lane (get_lane_start)
    kItlStateEnter = 3,     // lane found / request granted: go_tradelane, then directive result BEHAVIOR_INSTANT_TRADE_LANE
    kItlStateInLane = 4,    // go_tradelane succeeded; travelling
    kItlStateLeftLane = 5,  // the ship left the lane (event 4, bEnteringLane == 0): finishes with result 0
    kItlStateFailed = 6,    // request refused or go_tradelane failed: finishes with result 2
};
// Behavior (0x2c bytes) comes first, so the RequestCB vptr lands at +0x2c and State30 at +0x30.
class InstantTradeLaneBehavior : public Behavior, public IObjAffect::RequestCB
{
public:
    // Retail's ctor argument is the behavior type id (9, "InstantTradeLane"), forwarded to Behavior's ctor.
    explicit InstantTradeLaneBehavior(int InTypeId);

    // Slot 0 is the shared thunk; FUN_062d2080 (~Behavior) runs after this body.
    ~InstantTradeLaneBehavior() override;

    // Slot 1, LAB_062cee70, RET 0xc (.text:062ceea2): BindContext (FUN_062d2090) with all
    // three parameters; on success (`CMP AL,CL; JZ` skips otherwise) clears the cached lane
    // state. Returns BindContext's bool.
    bool SetContext(void* Param1, void* Param2, void* Param3);

    // Slot 3, FUN_062cef10: with a target, SetAvoidanceAllowed(0, 3).
    void TurnOff();

    // Slot 4, FUN_062cef40 (.text:062cef66-062cef87):
    //   CObject const* Obj = OwnerContext->cobject();        // vtbl+0x150,
    //                                                       // IObjInspect slot 84
    //   CShip* Ship = (Obj && (Obj->ObjectClass & 0x503) == 0x503)
    //                     ? (CShip*)Obj : nullptr;          // +0x4c IS CObject::
    //                                                       // ObjectClass, common_cobject.h
    //   Ship->request_exit_tradelane();                     // called unconditionally
    void RequestExitTradeLane();

    // Slot 5, LAB_062ceeb0:
    //   mode = 2;
    //   if (OwnerContext) {
    //       if (Flags54) mode = 1;
    //       if (EntranceRing) {
    //           uint Type = 0; EntranceRing->get_type(Type);   // vtbl+0x88, slot 34
    //           if (Type & 0x80) mode = 1;                     // low byte's sign bit
    //       }
    //   }
    //   if (Flag29) mode = 3;
    //   Readiness = mode;
    // Retail reads only the low byte of get_type's result and branches on JNS
    // (.text:062ceee1-062ceee7), so the predicate is `Type & 0x80`.
    int UpdateMode();   // returns the mode in EAX, RET 4

    // Slot 7, LAB_062cefb0 (124 instructions), switch on State30:
    //   state 0: `OwnerContext->get_lane_start(EntranceRing, OutLane, LaneStart3c, LaneEnd48)`.
    //            Nonzero -> UpdateResult = 3, return 3. Zero (`JZ` is the success path) ->
    //            ExitRing = OutLane, State30 = 3, Flag56 = 1, Flag55 = 0, UpdateResult = 1,
    //            return 1. Skipped when ExitRing is already set.
    //   state 3: go_tradelane(EntranceRing, ExitRing, 1, LaneSpeed) on OwnerContext's
    //            IObjAffect. Zero -> BehaviorMgr->QueueNotification(OwnerContext, 0, 0.0f),
    //            UpdateResult = 0, Flags54 = 1, State30 = 4, return 0. Nonzero -> State30 = 6,
    //            UpdateResult = 1, return 1.
    //   states 5/6: Flags28 |= 1, RequestExitTradeLane(), SetDirectiveResult(9,
    //            state 6 ? 2 : 0); UpdateResult = 2.
    //   states 1/2/4 and above 6: UpdateResult = 1, no other effect.
    int Update();

    // Slot 7: IBehaviorManager::UpdateBehaviorTick dispatches through vtable +0x1c.
    int OnUpdate() override { return Update(); }

    // The other own slots of .rdata:0639fe44.
    void OnEngage() override { TurnOff(); }                                   // 3  FUN_062cef10
    void OnDisengage() override { RequestExitTradeLane(); }                      // 4  FUN_062cef40
    int OnEvaluateReadiness(float) override { return UpdateMode(); }                     // 5  LAB_062ceeb0 (RET 4)
    bool OnApplyDirective(void* DirectiveOp) override { return AttachDirective(DirectiveOp); } // 10 LAB_062cf0f0
    void OnEvent(void* Event) override { HandleTargetEvent(Event); }        // 22 LAB_062cf1d0

    // Slot 10, LAB_062cf0f0: resolves EntranceRing / ExitRing through FUN_062e74a0
    // (SystemManagerTimerTable::FindVerifiedObject) on SysMgr+0x34, twice.
    bool AttachDirective(void* Directive);

    // Slot 22, LAB_062cf1d0: if the event's tag == 4, its first payload byte == 0 and
    // OwnerContext->get_id() equals payload +0xc, State30 = 5 (regardless of EntranceRing);
    // then, with EntranceRing set, OwnerContext->request_cancel(2, &EntranceRing->get_id(), 0).
    void HandleTargetEvent(void* Event);

    // RequestCB slot 0, LAB_062cf150 (47 instructions): `this` arrives adjusted to +0x2c.
    // Param3 != -1 -> Flag56 = 1, State30 = 3; else State30 = 6 and, with Flag56, the same
    // request_cancel as HandleTargetEvent.
    bool OnResult(int Param1, int Param2, int Param3) override;

    // ------------------------------------------------------------------
    // Fields; retail size 0x5c. +0x04..+0x29 is Behavior's; +0x2c is
    // the RequestCB vptr.
    // ------------------------------------------------------------------

    int State30;                // +0x30. Not zeroed by retail's ctor. Set to 3/4/5/6 by
                                 // Update()/OnResult()/HandleTargetEvent(); Update() switches
                                 // on it.

    // +0x34: the ring the ship enters the lane at -- get_lane_start's `Self` and go_tradelane's
    // EntranceRing; get_type / get_id are called on it. AttachDirective() resolves it from the
    // op's TradeLaneId.
    IObjInspect const* EntranceRing;

    // +0x38: go_tradelane's ExitRing. AttachDirective() resolves it from the op's ExitRingId;
    // when that is 0, Update's state 0 fills it from get_lane_start's OutLane (.text:062cf02f).
    IObjInspect const* ExitRing;

    // +0x3c / +0x48: get_lane_start's OutStart / OutEnd (arguments 3 / 4 by the push order at
    // .text:062ceffe-062cf005).
    Vector LaneStart3c;
    Vector LaneEnd48;
    unsigned char Flags54;      // +0x54. SetContext()/RequestExitTradeLane()/
                                 // UpdateMode() all read or clear this bit; Update()'s
                                 // state 3 sets it.
    unsigned char Flag55;       // +0x55. Cleared alongside Flags54/Flag56 by
                                 // SetContext()/RequestExitTradeLane();
                                 // read/cleared by OnResult().
    unsigned char Flag56;       // +0x56. Same clearing group as Flag55; OnResult() sets it
                                 // to 1 on one path.
    unsigned char Pad57;        // +0x57, alignment only.
    float LaneSpeed;              // +0x58. Set by AttachDirective() from the op's LaneSpeed;
                                 // go_tradelane's fourth argument (Server's Ship body
                                 // .text:06d028c0 forwards it as CShip::go_tradelane's Speed).
};

static_assert(offsetof(InstantTradeLaneBehavior, State30) == 0x30, "InstantTradeLaneBehavior::State30 offset");
static_assert(offsetof(InstantTradeLaneBehavior, EntranceRing) == 0x34, "InstantTradeLaneBehavior::EntranceRing offset");
static_assert(offsetof(InstantTradeLaneBehavior, ExitRing) == 0x38, "InstantTradeLaneBehavior::ExitRing offset");
static_assert(offsetof(InstantTradeLaneBehavior, LaneStart3c) == 0x3c, "InstantTradeLaneBehavior::LaneStart3c offset");
static_assert(offsetof(InstantTradeLaneBehavior, LaneEnd48) == 0x48, "InstantTradeLaneBehavior::LaneEnd48 offset");
static_assert(offsetof(InstantTradeLaneBehavior, Flags54) == 0x54, "InstantTradeLaneBehavior::Flags54 offset");
static_assert(offsetof(InstantTradeLaneBehavior, Flag55) == 0x55, "InstantTradeLaneBehavior::Flag55 offset");
static_assert(offsetof(InstantTradeLaneBehavior, Flag56) == 0x56, "InstantTradeLaneBehavior::Flag56 offset");
static_assert(offsetof(InstantTradeLaneBehavior, LaneSpeed) == 0x58, "InstantTradeLaneBehavior::LaneSpeed offset");
static_assert(sizeof(InstantTradeLaneBehavior) == 0x5c, "InstantTradeLaneBehavior size (operator_new(0x5c) in the factory)");
