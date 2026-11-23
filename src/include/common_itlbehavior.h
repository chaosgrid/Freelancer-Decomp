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

// Intermediate base holding the +0x04..+0x2c prefix so that the RequestCB vptr lands at
// +0x2c and State30 at +0x30 (MSVC places a derived class's own members after all its
// bases; same arrangement as DockBehaviorFields).
class InstantTradeLaneBehaviorFields : public Behavior
{
public:
    explicit InstantTradeLaneBehaviorFields(int InTypeId);

    int TypeId;  // +0x04, FUN_062d2020's argument (Behavior::GetTypeId); not read back

    unsigned CachedHandle08;    // +0x08, the binder's Owner->get_id() (an object id)

    int ModeSelector0xc;        // +0x0c. Written by UpdateMode() (1/2/3).
    int UpdateResult10;         // +0x10. Update()'s result (0/1/2/3).

    // +0x14, an IObjRW*: Update calls get_lane_start (vt+0x138) on it, and go_tradelane /
    // request_cancel through its IObjAffect sub-object at +4; get_id (vt+0x20) and cobject
    // (vt+0x150) also. (Update loads ECX = [ESI+0x14] once at .text:062cefcc, so the
    // `[ECX+0x4]` there is this pointer's +4, not this object's.)
    IObjRW* TargetPtr14;
    IObjDB* ObjectDb;
    float DeltaTime1c; // +0x1c, not read by this class

    // +0x20, the IBehaviorManager (QueueNotification FUN_062d4520, SetDirectiveResult
    // FUN_062d29a0).
    IBehaviorManager* BehaviorMgrPtr20;

    // +0x24, the SystemManager: AttachDirective looks objects up through NavHelperPtr24+0x34
    // (FUN_062e74a0).
    SystemManager* NavHelperPtr24;
    unsigned char Flags28;       // +0x28. RequestExitTradeLane() ORs bit 0x1; Update()'s
                                 // first check tests bit 0x1; UpdateMode() tests bit 0x2.
    unsigned char Flag29;       // +0x29. UpdateMode(): nonzero => mode 3.
    unsigned char Pad2a[0x2c - 0x2a]; // +0x2a, alignment only
};
static_assert(offsetof(InstantTradeLaneBehaviorFields, CachedHandle08) == 0x08, "InstantTradeLaneBehaviorFields::CachedHandle08 offset");
static_assert(offsetof(InstantTradeLaneBehaviorFields, ModeSelector0xc) == 0x0c, "InstantTradeLaneBehaviorFields::ModeSelector0xc offset");
static_assert(offsetof(InstantTradeLaneBehaviorFields, UpdateResult10) == 0x10, "InstantTradeLaneBehaviorFields::UpdateResult10 offset");
static_assert(offsetof(InstantTradeLaneBehaviorFields, TargetPtr14) == 0x14, "InstantTradeLaneBehaviorFields::TargetPtr14 offset");
static_assert(offsetof(InstantTradeLaneBehaviorFields, BehaviorMgrPtr20) == 0x20, "InstantTradeLaneBehaviorFields::BehaviorMgrPtr20 offset");
static_assert(offsetof(InstantTradeLaneBehaviorFields, NavHelperPtr24) == 0x24, "InstantTradeLaneBehaviorFields::NavHelperPtr24 offset");
static_assert(offsetof(InstantTradeLaneBehaviorFields, Flags28) == 0x28, "InstantTradeLaneBehaviorFields::Flags28 offset");
static_assert(offsetof(InstantTradeLaneBehaviorFields, Flag29) == 0x29, "InstantTradeLaneBehaviorFields::Flag29 offset");
static_assert(sizeof(InstantTradeLaneBehaviorFields) == 0x2c, "InstantTradeLaneBehaviorFields size (own data ends right before the RequestCB secondary vtable ptr at +0x2c)");

class InstantTradeLaneBehavior : public InstantTradeLaneBehaviorFields, public IObjAffect::RequestCB
{
public:
    // Retail's ctor argument is the behavior type id (9, "InstantTradeLane"); it is forwarded to InstantTradeLaneBehaviorFields.
    // See Behavior::GetTypeId() in common_behavior.h.
    explicit InstantTradeLaneBehavior(int InTypeId);

    // Slot 0 is the shared thunk; FUN_062d2080 (~Behavior) runs after this body.
    ~InstantTradeLaneBehavior() override;

    // Slot 1, LAB_062cee70, RET 0xc (.text:062ceea2): BindContext (FUN_062d2090) with all
    // three parameters; on success (`CMP AL,CL; JZ` skips otherwise) clears the cached lane
    // state. Returns BindContext's bool.
    bool SetContext(void* Param1, void* Param2, void* Param3);

    // Slot 3, FUN_062cef10: with a target, SetCruiseLock(0, 3).
    void TurnOff();

    // Slot 4, FUN_062cef40 (.text:062cef66-062cef87):
    //   CObject const* Obj = TargetPtr14->cobject();        // vtbl+0x150,
    //                                                       // IObjInspect slot 84
    //   CShip* Ship = (Obj && (Obj->ObjectClass & 0x503) == 0x503)
    //                     ? (CShip*)Obj : nullptr;          // +0x4c IS CObject::
    //                                                       // ObjectClass, common_cobject.h
    //   Ship->request_exit_tradelane();                     // called unconditionally
    void RequestExitTradeLane();

    // Slot 5, LAB_062ceeb0:
    //   mode = 2;
    //   if (TargetPtr14) {
    //       if (Flags54) mode = 1;
    //       if (SubObjPtr34) {
    //           uint Type = 0; SubObjPtr34->get_type(Type);   // vtbl+0x88, slot 34
    //           if (Type & 0x80) mode = 1;                     // low byte's sign bit
    //       }
    //   }
    //   if (Flag29) mode = 3;
    //   ModeSelector0xc = mode;
    // Retail reads only the low byte of get_type's result and branches on JNS
    // (.text:062ceee1-062ceee7), so the predicate is `Type & 0x80`.
    int UpdateMode();   // returns the mode in EAX, RET 4

    // Slot 7, LAB_062cefb0 (124 instructions), switch on State30:
    //   state 0: `TargetPtr14->get_lane_start(SubObjPtr34, OutLane, LaneStart3c, LaneEnd48)`.
    //            Nonzero -> UpdateResult10 = 3, return 3. Zero (`JZ` is the success path) ->
    //            LanePtr38 = OutLane, State30 = 3, Flag56 = 1, Flag55 = 0, UpdateResult10 = 1,
    //            return 1. Skipped when LanePtr38 is already set.
    //   state 3: go_tradelane(SubObjPtr34, LanePtr38, 1, LaneSpeed) on TargetPtr14's
    //            IObjAffect. Zero -> BehaviorMgrPtr20->QueueNotification(TargetPtr14, 0, 0.0f),
    //            UpdateResult10 = 0, Flags54 = 1, State30 = 4, return 0. Nonzero -> State30 = 6,
    //            UpdateResult10 = 1, return 1.
    //   states 5/6: Flags28 |= 1, RequestExitTradeLane(), SetDirectiveResult(9,
    //            state 6 ? 2 : 0); UpdateResult10 = 2.
    //   states 1/2/4 and above 6: UpdateResult10 = 1, no other effect.
    int Update();

    // Slot 7: IBehaviorManager::UpdateBehaviorTick dispatches through vtable +0x1c.
    int OnUpdate() override { return Update(); }

    // The other own slots of .rdata:0639fe44.
    void OnEngage() override { TurnOff(); }                                   // 3  FUN_062cef10
    void OnDisengage() override { RequestExitTradeLane(); }                      // 4  FUN_062cef40
    int OnEvaluateReadiness(float) override { return UpdateMode(); }                     // 5  LAB_062ceeb0 (RET 4)
    bool OnApplyDirective(void* DirectiveOp) override { return AttachDirective(DirectiveOp); } // 10 LAB_062cf0f0
    void OnEvent(void* Event) override { HandleTargetEvent(Event); }        // 22 LAB_062cf1d0

    // Slot 10, LAB_062cf0f0: resolves SubObjPtr34 / LanePtr38 through FUN_062e74a0
    // (SystemManagerTimerTable::FindVerifiedObject) on NavHelperPtr24+0x34, twice.
    bool AttachDirective(void* Directive);

    // Slot 22, LAB_062cf1d0: if the event's tag == 4, its first payload byte == 0 and
    // TargetPtr14->get_id() equals payload +0xc, State30 = 5 (regardless of SubObjPtr34);
    // then, with SubObjPtr34 set, TargetPtr14->request_cancel(2, &SubObjPtr34->get_id(), 0).
    void HandleTargetEvent(void* Event);

    // RequestCB slot 0, LAB_062cf150 (47 instructions): `this` arrives adjusted to +0x2c.
    // Param3 != -1 -> Flag56 = 1, State30 = 3; else State30 = 6 and, with Flag56, the same
    // request_cancel as HandleTargetEvent.
    bool OnResult(int Param1, int Param2, int Param3) override;

    // ------------------------------------------------------------------
    // Fields; retail size 0x5c. +0x04..+0x2b is on InstantTradeLaneBehaviorFields; +0x2c is
    // the RequestCB vptr.
    // ------------------------------------------------------------------

    int State30;                // +0x30. Not zeroed by retail's ctor. Set to 3/4/5/6 by
                                 // Update()/OnResult()/HandleTargetEvent(); Update() switches
                                 // on it.

    // +0x34, an IObjInspect const*: get_lane_start's `Self`, and get_type / get_id are
    // called on it. Resolved by AttachDirective() from Directive + 0xc. Role unknown.
    IObjInspect const* SubObjPtr34;

    // +0x38: get_lane_start's OutLane (.text:062cf02f), the resolved trade lane; Update's
    // state 0 skips when set, state 3 passes it to go_tradelane. AttachDirective() also seeds
    // it from Directive + 0x10.
    IObjInspect const* LanePtr38;

    // +0x3c / +0x48: get_lane_start's OutStart / OutEnd (arguments 3 / 4 by the push order at
    // .text:062ceffe-062cf005).
    Vector LaneStart3c;
    Vector LaneEnd48;
    unsigned char Flags54;      // +0x54. SetContext()/RequestExitTradeLane()/
                                 // UpdateMode() all read or clear this bit; AttachDirective()
                                 // sets it.
    unsigned char Flag55;       // +0x55. Cleared alongside Flags54/Flag56 by
                                 // SetContext()/RequestExitTradeLane();
                                 // read/cleared by OnResult().
    unsigned char Flag56;       // +0x56. Same clearing group as Flag55; OnResult() sets it
                                 // to 1 on one path.
    unsigned char Pad57;        // +0x57, alignment only.
    float LaneSpeed;              // +0x58. Set by AttachDirective() from Directive + 0x14;
                                 // go_tradelane's fourth argument (Server's Ship body
                                 // .text:06d028c0 forwards it as CShip::go_tradelane's Speed).
};

static_assert(offsetof(InstantTradeLaneBehavior, State30) == 0x30, "InstantTradeLaneBehavior::State30 offset");
static_assert(offsetof(InstantTradeLaneBehavior, SubObjPtr34) == 0x34, "InstantTradeLaneBehavior::SubObjPtr34 offset");
static_assert(offsetof(InstantTradeLaneBehavior, LanePtr38) == 0x38, "InstantTradeLaneBehavior::LanePtr38 offset");
static_assert(offsetof(InstantTradeLaneBehavior, LaneStart3c) == 0x3c, "InstantTradeLaneBehavior::LaneStart3c offset");
static_assert(offsetof(InstantTradeLaneBehavior, LaneEnd48) == 0x48, "InstantTradeLaneBehavior::LaneEnd48 offset");
static_assert(offsetof(InstantTradeLaneBehavior, Flags54) == 0x54, "InstantTradeLaneBehavior::Flags54 offset");
static_assert(offsetof(InstantTradeLaneBehavior, Flag55) == 0x55, "InstantTradeLaneBehavior::Flag55 offset");
static_assert(offsetof(InstantTradeLaneBehavior, Flag56) == 0x56, "InstantTradeLaneBehavior::Flag56 offset");
static_assert(offsetof(InstantTradeLaneBehavior, LaneSpeed) == 0x58, "InstantTradeLaneBehavior::LaneSpeed offset");
static_assert(sizeof(InstantTradeLaneBehavior) == 0x5c, "InstantTradeLaneBehavior size -- CONFIRMED via operator_new(0x5c) in the factory (item 1514), not just the largest field referenced");
