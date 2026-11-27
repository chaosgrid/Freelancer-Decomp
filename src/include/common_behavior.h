// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_ini.h"    // Vector
#include "common_goods.h"  // BaseWatcher
#include "common_iface.h"  // IBehaviorManager
#include "common_pub.h"    // pub::AI::DirectiveGotoOp
#include "common_idea.h"   // FaceIdea, MoveIdea -- FormationBehavior's own real embedded sub-objects

#include <cstddef>

struct IObjDB; // common_cobjdb.h -- Behavior::ObjectDb (pointer-only use here)
class SystemManager; // common_systemmanager.h -- Behavior::SysMgr (pointer-only use here)

// Common.dll AI behavior family (Behavior and its subclasses), from the RTTI at each
// vtable's [-4] slot:
// Behavior (root, RTTI_Type_Descriptor @ .data:063ebdec)
// +-- BaseFollowerBehavior   RTTI_Type_Descriptor @ .data:063ebf30, base of the two below
// +-- FormationBehavior      COL @ 063a8fb8, vtable .rdata:0639fb64, ctor
// |                          FUN_062cbfe0, size 0x2b8 (factory .text:062d1b67).
// |                          Mostly approach/rejoin/break-off maneuvering relative
// |                          to the leader.
// +-- FollowBehavior         COL @ 063a9004, vtable .rdata:0639fbe4, ctor
//                            FUN_062ccaf0 (common_followbehavior.h).
//
// IdleBehavior (COL @ 063a904c, vtable .rdata:0639fc5c, ctor FUN_062cd530) is not a
// BaseFollowerBehavior subclass: its ClassHierarchyDescriptor has numBaseClasses=2
// (self + Behavior). It still shares the +0x08..+0x29 field shape and the FUN_062d2090
// context initializer -- a family layout convention, not a shared base
// (common_idlebehavior.h).
//
// FormationBehavior/FollowBehavior share one ~29-slot vtable shape and both ctors call
// the FUN_062cb2b0 base ctor. FUN_062c4900 / FUN_062c3d10 belong to DockBehavior
// (size 0x470, factory .text:062d1a07), not FormationBehavior; see common_dockbehavior.h.

// FormationMoveFx (ctor FUN_062d5aa0, vtable .rdata:063a047c) is retail's
// MatchThrottle2Idea (common_idea.h).

// ============================================================================
// Null-guarded IObjInspect vtable trampolines (0x062e5850-0x062e6010), used by
// MatchThrottle2Idea::Update and the behaviors. Each returns 0.0f on a null Obj.
// ============================================================================

// FUN_062e5b70. Returns Obj->get_projected_throttle(Value, Type) (slot 0xd4), or
// DAT_06399408 (0.0f) on null Obj. Retail pushes the ThrustEquipType as an integer (4 or 1).
// Callers: FUN_062d5a20 and MatchThrottle2Idea::Update.
float Behavior_GetProjectedThrottle(IObjInspect const* Obj, float Value, ThrustEquipType Type);

// FUN_062d21c0: `Target->Flags28 = (Target->Flags28 & ~0x02) | 0x01`.
//
// The receiver is the pointed-to object, not the behavior: at all five retail call sites
// ECX is a pointer field's value ([ESI+0x2f0] DockBehavior's NavPilotPtr at 062c393d /
// 062c3d3f, [ESI+0x40] at 062c7695, [ESI+0x60] / [ESI+0x64] at 062cf4ff / 062cf513),
// each preceded by that pointer's OnDisengage (vtable +0x10). So the target is a
// Behavior-family object and +0x28 the Flags28 byte FormationBehavior::OnApplyDirective
// (LAB_062cd630) sets bit 0x2 of.
class Behavior;

// FUN_062e5af0. Same shape as Behavior_GetProjectedThrottle on slot 0xd8,
// IObjInspect::get_speed(float, ThrustEquipType).
float Behavior_GetSpeed(IObjInspect const* Obj, float Value, ThrustEquipType Type);

// FUN_062e5950, cdecl. Null Obj -> 0.0f;
// MaxSpeed < 0 or == 0 -> 0.0f (`TEST AH,0x41; JP` lets only > and NaN through);
// Dt < 0.001 (double DAT_0639c9f0) -> Dt = 0; then tail-returns
// Obj->get_angular_distance_travelled(From, To, Dt, MaxSpeed) (slot 59, +0xec).
float Behavior_GetAngularDistanceTravelled(IObjInspect const* Obj, float From, float To, float Dt,
                                           float MaxSpeed);

// FUN_062e6010. No null check (all callers pre-validate). Returns the larger of
// Iface->get_physical_radius (slot 0x2c, V1 initialised 1.0f) and
// get_atmosphere_range (slot 0x140, V2 initialised 0.0f): `(V2 > V1) ? V2 : V1`, so V1
// on a tie or unordered compare. Slot 0x2c's second out-param is a 12-byte Vector.
float Behavior_GetIfaceMaxScalarAttribute(IObjInspect const* Iface);

// FUN_062e5850. Slot 0xcc, IObjInspect::get_time_to_accelerate; the parameters are named after
// CShip's implementation (FUN_062b37f0: Throttle, FromSpeed, ToSpeed). FromSpeed and ToSpeed
// are clamped to >= 0.0 (NaN clamps too); if FromSpeed and Throttle are both exactly 0.0,
// returns 0.0f without the call. Otherwise FromSpeed below 0.001 (DAT_0639c9f0) snaps to 0.0
// before the call. Type is pushed as an integer.
float Behavior_GetTimeToAccelerate(IObjInspect const* Obj, float Throttle, float FromSpeed, float ToSpeed,
                                   ThrustEquipType Type);

// FUN_062e58f0. Slot 0xd0, IObjInspect::get_distance_travelled (CShip FUN_062b3900: Throttle,
// FromSpeed, Time). One gate only: Time < 0.0 returns 0.0f without the call (no
// clamp-and-continue; NaN continues). Otherwise FromSpeed below 0.001 snaps to 0.0 before the
// call. MatchThrottle2Idea chains it after Behavior_GetTimeToAccelerate, passing that result
// as Time.
float Behavior_GetDistanceTravelled(IObjInspect const* Obj, float Throttle, float FromSpeed, float Time,
                                    ThrustEquipType Type);

// FUN_062e59b0. Chains Behavior_GetSpeed with the GetTimeToAccelerate /
// GetDistanceTravelled pair (DAT_06399410 = 1.0, DAT_0639c430 = 0.0, DAT_0639c9f0 = 0.001).
// Ghidra's unaff_retaddr is the Radius parameter (entry ESP+8).
float Behavior_QueryChainedRadiusEffect(IObjInspect const* Obj, float Radius, int Selector);

// FUN_062e5ab0. Null-guards Obj, returns 0.0f if Speed < 0.001f (DAT_0639e6c8); otherwise
// get_distance_travelled(0.0f, Speed, 1000.0f (DAT_063a0760), engine): the distance the ship
// coasts from Speed (SplineMoveIdea's CoastDistance, PathfindManager's coast distances).
float Behavior_GetCoastDistance(IObjInspect const* Obj, float Speed);

// FUN_062e5bc0. Null-guards Obj, returns 0.0f if Value is exactly 0.0f, else
// Obj->get_initial_speed_to_coast_distance(Value, true) (slot 0xdc).
float Behavior_GetInitialSpeedToCoastDistance(IObjInspect const* Obj, float Value);

// FUN_062e5b20. Null Obj -> DAT_06399408 (0.0f), else
// Obj->get_angular_speed_XY(Speed, MaxSpeed) (slot +0xf0).
float Behavior_GetAngularSpeedXY(IObjInspect const* Obj, float Speed, float MaxSpeed);

// FUN_062e5b50: null Obj -> 0.0f, else Obj->get_angular_speed_Z(Speed) (slot +0xf4).
// Sole caller FaceIdea::SetContext (.text:062d6881), which discards the result.
float Behavior_GetAngularSpeedZ(IObjInspect const* Obj, float Speed);

// FUN_062e5ba0: null Obj -> 0.0f, else Obj->get_projected_axis_throttle_XY(Value)
// (slot +0xf8). Callers: FaceIdea's ctor / SetThrottleScale, ComputeFormationFollowerEnvelope.
float Behavior_GetProjectedAxisThrottleXY(IObjInspect const* Obj, float Value);

// ============================================================================
// FUN_062e89a0 / 062e8c20 / 062e8ce0 / 062e8e60 / 062e9030 / 062e9120 / 062e91d0 are
// ThrustManager members (every retail caller passes SystemManager + 0x18;
// common_systemmanager.h, common_thrustmanager.cpp).
// ============================================================================

// FUN_062e61f0. Tail-thunk `Node->get_transform()` (IObjInspect slot 0xc), no null test.
// Sole caller ThrustManager::SetNudgeDirection (FUN_062e8ce0).
Transform const* Behavior_GetTransform(IObjInspect const* Node);

// FUN_062ecb20 (FollowerDirectiveGateA), FUN_062ecc60 / FUN_062ecce0 (NotifyFormationLeader1 /
// 2) and FUN_062ecc50 (SetFlagC3) are thiscall on the 0xC8-byte block at SystemManager+0x1364:
// FFormationLeaderState members (common_systemmanager.h, common_gunconvergence.cpp).
// Notify callers: EvadeBehavior::OnUpdate (.text:062c7e80/062c7ebc, 062c7e98/062c7ed4) and
// BuzzBehavior::Update (.text:062c64ec/062c67be, 062c6506/062c67d4); SetFlagC3's:
// IBehaviorManager::QueueNotification cases 1 / 2 (.text:062d4949 / 062d496c).

// The SystemManagerTimerTable (SystemManager+0x34) byte getters FUN_062e8810..8860 are
// SystemManagerTimerTable::IsTimer0Active..IsTimer5Active (common_systemmanager.h,
// common_sysmgr_timertable.cpp).

// FUN_062e8910 / FUN_062e8940 / FUN_062e8970 (.text:062e8910-062e899e, RET 0 each), three
// byte-identical bodies on the SystemManagerTimerTable (SystemManager+0x34) differing only in
// the threshold: BestValueSoFar (+0xf4) > 0.0f (`TEST AH,0x41; JNZ false`) and BestValueSoFar
// <= Owner->PersonalityData.MissileReaction.EvadeBreak / EvadeSlide / EvadeAfterburn
// MissileReactionTime (Sys+0x17b4 /
// +0x17b8 / +0x17bc; `TEST AH,0x41; JP false`, so equality is true). Owner (+4) is
// dereferenced unconditionally. Only caller: EvadeBehavior::OnUpdate mode 4. Retail names
// unknown.
struct SystemManagerTimerTable;
   // FUN_062e8910
   // FUN_062e8940
   // FUN_062e8970

// ============================================================================
// Behavior::Flags28 (+0x28) bits.
enum EBehaviorFlags28
{
    // Set by Behavior::ReleaseDirectiveTarget (FUN_062d21c0) and by the behaviors' own
    // disengage paths (`Flags28 |= 1; OnDisengage(); Flags28 &= ~2`); cleared by OnEngage
    // (FUN_062d20d0). A behavior that sees it on its next OnUpdate finishes with result 2.
    kBehaviorFlagDisengageRequested = 0x1,
    // Set by the slot-10 (OnApplyDirective) overrides that accept a directive; cleared by
    // cancel_notify (LAB_062c2f50) and when a behavior finishes.
    kBehaviorFlagDirectiveAccepted = 0x2,
};

// Behavior::UpdateResult (+0x10), the value OnUpdate stores and returns and GetStatus
// (slot 9) reports. IBehaviorManager::UpdateBehaviorTick reads it as the active behavior's
// status: 0 forbids a re-evaluation of the current behavior, 2 and 3 force one (and on the
// player's own ship drop back to DefaultBehaviorIndex), anything else leaves its verdict alone.
enum EBehaviorUpdateResult
{
    kBehaviorUpdateBusy = 0,      // mid-maneuver: no re-evaluation
    kBehaviorUpdateRunning = 1,   // the ctor / OnEngage value; the normal running result
    kBehaviorUpdateFinished = 2,  // directive completed (arrived, disengaged, ...)
    kBehaviorUpdateAborted = 3,   // cannot run: no owner, target gone, timeout
};

// The Mask argument of Behavior::SetAvoidanceAllowed / PathfindManager::SetAvoidanceAllowed
// (FUN_062d21d0 / FUN_062dd3d0): which of the pathfinder's two latches the Value is stored in.
// Every caller passes both.
enum EAvoidanceMask
{
    kAvoidanceObstacleNudge = 0x1,   // PathfindManager::bObstacleNudgeAllowed
    kAvoidancePoint = 0x2,           // PathfindManager::bAvoidPointAllowed
};

// ============================================================================
// Behavior -- root of the family.
// ============================================================================
// The interface IBehaviorManager fans its per-tick ship commands out over. Every slot is
// __thiscall -- retail loads the behavior into ECX and tail-jumps into it, so the behavior
// inherits the manager method's own stack frame (e.g. allow_head_tracking at 062d43e0:
// `MOV ECX,[this + EAX*0x4 + 0x174]; MOV EAX,[ECX]; JMP [EAX + 0x4c]`). Signatures of the
// manager-driven slots come from the exported manager methods' mangled names.
//
// Vtable .rdata:063a02c4 (COL at [vtable-4] = 063a98e8, ClassHierarchyDescriptor
// numBases = 1). Slot 0 LAB_062d2060 is the scalar-deleting destructor around
// FUN_062d2080, the plain `~Behavior()` (vptr reset only). FUN_062d2020 is
// `Behavior::Behavior(int)`, which every derived behavior constructor calls first.
//
// FUN_062d2020 writes +0x04..+0x29, so all of that block belongs to Behavior itself (even
// TrailBehavior, whose BaseClassArray is [TrailBehavior, Behavior], has it).
//
// The table is 27 slots (.rdata:063a02c4..063a032c = 0x6c bytes). The next dword,
// DAT_063a0330 = 0xBF19999A (-0.6f), is not a COL pointer; NullBehavior's table
// (PTR_FUN_063a0244) has the same 0x6c extent.
//
// "folds" = how many vtable slots DLL-wide point at that address; anything in the double
// digits is /OPT:ICF collapsing a trivial body, so Ghidra's label names some class that
// shares the code, never the receiver.
//
//  #  off   target                              retail body                                RET n  folds
//  0  0x00  LAB_062d2060                        scalar-deleting dtor                       RET 4      1
//  1  0x04  LAB_06391fb0 = _purecall            PURE VIRTUAL                                  --    268
//  2  0x08  0629afd0 "CEquip::ConnectServerEquip" MOV EAX,[ESP+4]; MOV [ECX+0x18],EAX       RET 4     23
//  3  0x0c  FUN_062d20d0                        +0xc=1; +0x10=1; +0x28&=~1; +0x1c=0         RET 0      1
//  4  0x10  FUN_062d20f0                        t=[[ECX+0x20]+0xc]; if(t)[t+0x64]=0;
//                                               [ECX+0x29]=0                                RET 0      1
//  5  0x14  LAB_06391fb0 = _purecall            PURE VIRTUAL                                  --    268
//  6  0x18  LAB_062cf5e0                        MOV EAX,2                                   RET 4     14
//  7  0x1c  LAB_06391fb0 = _purecall            PURE VIRTUAL                                  --    268
//  8  0x20  062a8920 "Exclusion::AsteroidExclusionZone..."  bare RET                        RET 0     50
//  9  0x24  06263a70 "IObjInspectImpl::cobject" MOV EAX,[ECX+0x10]                          RET 0     25
// 10  0x28  LAB_062c2f40                        MOV AL,1                                    RET 4     21
// 11  0x2c  0629ba00 "CEquip::DisableController" XOR AL,AL                                  RET 0    130
// 12  0x30  062ed730 "MetaList<GoodDesc>::op"   empty                                       RET 4    214
// 13  0x34  062ed730                            empty                                       RET 4    214
// 14  0x38  062ed730                            empty                                       RET 4    214
// 15  0x3c  062ed730                            empty                                       RET 4    214
// 16  0x40  FUN_062d2110                        real body; slot-0x5c query on [+0x24]-8,
//                                               then FUN_062e9120                           RET 4     18
// 17  0x44  FUN_062d21b0 -> FUN_062e9030        real body on [+0x24]+0x18                   RET 4     19
// 18  0x48  062ed730                            empty                                       RET 4    214
// 19  0x4c  0627a710 "Archetype::AttachedEquipment::i..." MOV AL,1                          RET 0     36
// 20  0x50  LAB_062c2f40                        MOV AL,1                                    RET 4     21
// 21  0x54  062ed730                            empty                                       RET 4    214
// 22  0x58  062ed730                            empty                                       RET 4    214
// 23  0x5c  LAB_062c2f50                        AND byte [ECX+0x28],0xfd                    RET 0     21
// 24  0x60  06325390 "RDLNode::operator..."     XOR EAX,EAX                                 RET 0    374
// 25  0x64  0629ba10 "CEShield::GetEquipDesc"   XOR AL,AL                                   RET 4     25
// 26  0x68  06325390 "RDLNode::operator..."     XOR EAX,EAX                                 RET 0    374
//
// Only slots 0, 3, 4 (fold count 1) are Behavior's alone; 16/17 are shared inside the
// behavior family. The rest are ICF folds named by position and `RET n`. Slots 2, 9, 10 and
// 23 are still Behavior's semantics by the field they touch: slot 2 writes +0x18, slot 9
// reads +0x10, slots 10/23 set and clear bit 0x2 of +0x28.
//
// Slots 1, 5, 7 are _purecall (LAB_06391fb0), so retail's Behavior is abstract. NullBehavior's
// overrides give their shapes: slot 1 LAB_062d76a0 `MOV AL,1; RET 0xc`, slot 5 LAB_062d1730
// `MOV EAX,1; RET 4`, slot 7 LAB_062d1740 `MOV EAX,2; RET 0`.
//
// Slots 8, 9, 11, 21, 22, 25 keep port-chosen names (retail gives no name evidence); their
// signatures come from `RET n` and the return register.
class Behavior
{
public:
    // 0 +0x00. LAB_062d2060, the scalar-deleting destructor around FUN_062d2080 -- the pair
    // MSVC emits for `virtual ~Behavior() {}`.
    virtual ~Behavior() {}
    // 1 +0x04. _purecall in retail. The one caller is IBehaviorManager's ctor (.text:062d2220),
    // which runs it on each of the 21 CREATE_BEHAVIOR results with (owner IObjRW*, the
    // manager, its SystemManager). Every retail override starts with the binder FUN_062d2090
    // (BindContext) and returns its result, except NullBehavior's LAB_062d76a0
    // (`MOV AL,1; RET 0xc`).
    virtual bool SetContext(IObjRW* Owner, IBehaviorManager* Manager, SystemManager* Sys) = 0;
    // 2 +0x08. `MOV [ECX+0x18],EAX; RET 4` (0x0629afd0, ICF fold labelled
    // CEquip::ConnectServerEquip): setter for +0x18, the behavior's IObjDB. Sole caller
    // ConnectBehaviorManagerObjectDb (FUN_062d2830) hands every dispatch-table entry the
    // manager's IObjDB. No subclass overrides it.
    virtual void SetObjectDb(IObjDB* ObjectDb);
    // 3 +0x0c. FUN_062d20d0: `+0xc = 1; +0x10 = 1; +0x28 &= 0xfe; +0x1c = 0` (the ctor leaves
    // +0xc = 2). Derived slot-3 bodies that begin with `CALL FUN_062d20d0`
    // call it as Behavior::OnEngage().
    virtual void OnEngage();
    // 4 +0x10. FUN_062d20f0: `P = [[this+0x20]+0xc]; if (P) [P+0x64] = 0; byte [this+0x29] = 0`
    // -- this+0x20 is the IBehaviorManager, +0x0C its PathfindManager, whose AvoidExcludeId is
    // cleared. Derived slot-4 bodies that end in `CALL/JMP FUN_062d20f0` call it as
    // Behavior::OnDisengage().
    virtual void OnDisengage();
    // 5 +0x14. _purecall in retail. The argument is a float: both callers in
    // IBehaviorManager::UpdateBehaviorTick (.text:062d2e7f, .text:062d31b8) push the
    // manager's float accumulator at +0xdc (FADD/FSTP at .text:062d2df2) as a plain dword.
    virtual int OnEvaluateReadiness(float Value) = 0;
    // 6 +0x18 -- ?external_player_evaluate@IBehaviorManager@@QAE?AW4EvaluateResult@@H@Z
    // tail-jumps here. Retail's base body is LAB_062cf5e0 `MOV EAX,2; RET 4` (EvalSkip == 2).
    virtual EvaluateResult evaluate(unsigned int Context) { (void)Context; return EvalSkip; }
    // 7 +0x1c. _purecall in retail; every concrete subclass overrides it.
    virtual int OnUpdate() = 0;
    // 8 +0x20. Bare `RET` -- a real, empty, zero-argument base implementation.
    virtual void PostUpdate() {}
    // 9 +0x24. `MOV EAX,[ECX+0x10]; RET` (ICF fold labelled IObjInspectImpl::cobject):
    // getter for +0x10, UpdateResult.
    virtual int GetStatus();
    // 10 +0x28. `MOV AL,1; RET 4`. The base body ignores the argument, but callers pass a
    // directive op: IBehaviorManager::SetCurrentBehavior skips the call when it is null
    // (.text:062d3560) and consumes the bool; BaseFollowerBehavior::OnUpdate passes a
    // pub::AI::DirectiveGotoOp (.text:062cba08) or DirectiveDockOp (.text:062cbc28), and
    // FollowBehavior's override (FUN_062cd3d0) reads a DirectiveFollowOp. The pointee type
    // varies per subclass, hence void*.
    virtual bool OnApplyDirective(void* DirectiveOp) { (void)DirectiveOp; return true; }
    // 11 +0x2c. `XOR AL,AL; RET` -- returns false, no arguments.
    virtual bool allow_user_steering() { return false; }
    virtual void set_direction(Vector const& Dir) { (void)Dir; }             // 12 +0x30
    virtual void set_throttle(float Throttle) { (void)Throttle; }            // 13 +0x34
    virtual void set_cruise(bool bOn) { (void)bOn; }                        // 14 +0x38
    virtual void engage_engine(bool bOn) { (void)bOn; }                     // 15 +0x3C
    // 16 +0x40. FUN_062d2110. Driven by IBehaviorManager::update_current_behavior_set_afterburner.
    // Queries IObjInspect::is_cruise_active (slot 0x5c) on the SysMgr's owner
    // (`[this+0x24] + 0x8` -> list head - 8), then calls FUN_062e9120 on SysMgr+0x18.
    // See common_behavior.cpp.
    virtual void set_afterburner(bool bOn);                                 // 16 +0x40
    // 17 +0x44. FUN_062d21b0: `MOV ECX,[ECX+0x24]; ADD ECX,0x18; JMP FUN_062e9030` --
    // forwards to the SysMgr's ThrustManager. The player's strafe burst reaches it via
    // IBehaviorManager::update_current_behavior_slide_strafe_burst.
    virtual void slide_strafe_burst(StrafeDir Dir);                         // 17 +0x44
    virtual void brake_reverse(bool bOn) { (void)bOn; }                     // 18 +0x48
    // 19 +0x4C. Retail's base body is `MOV AL,1; RET` (true). The manager's own
    // no-current-behavior path returning false is a separate null guard.
    virtual bool allow_head_tracking() { return true; }
    // 20 +0x50. 062d43c0 tail-jumps here with the manager's CancelRequestType. Base body
    // LAB_062c2f40, `MOV AL,1; RET 4`.
    virtual bool cancel_behavior(CancelRequestType Request) { (void)Request; return true; }
    // 21 +0x54, 22 +0x58. Empty `RET 4` bodies, one dword argument each.
    virtual void OnObjectLeftSystem(void* Arg) { (void)Arg; }
    virtual void OnEvent(void* Arg) { (void)Arg; }
    // 23 +0x5C. LAB_062c2f50, `AND byte ptr [ECX+0x28],0xfd; RET` -- clears the +0x28 bit
    // slot 10's derived override sets. All 21 Behavior vtables point here; no subclass
    // overrides it.
    virtual void cancel_notify();
    virtual IObjRW const* get_target() { return nullptr; }                   // 24 +0x60
    // 25 +0x64. Retail body 0629ba10, `XOR AL,AL; RET 4`: returns false, pops one dword.
    virtual bool Slot25(void* Arg) { (void)Arg; return false; }
    // 26 +0x68. Retail body 06325390, `XOR EAX,EAX; RET` (the same ICF body as slot 24, so
    // typed after it).
    virtual IObjRW* GetDockTarget() { return nullptr; }

    // FUN_062d2020 (RET 4), `Behavior::Behavior(int)`, which every derived constructor calls
    // first: the stores listed with the data block below, in retail order.
    explicit Behavior(int InTypeId);

    Behavior(Behavior const&) = delete;
    Behavior& operator=(Behavior const&) = delete;

    // FUN_062d2090, non-virtual, thiscall, RET 0xc, called by the slot-1 SetContext
    // overrides: Owner && Sys required, else false; then OwnerContext = Owner,
    // OwnerId = Owner->get_id(), SysMgr = Sys, BehaviorMgr = Manager, true.
    bool BindContext(IObjRW* Owner, IBehaviorManager* Manager, SystemManager* Sys);

    // FUN_062d21d0, non-virtual, thiscall: forwards both stack arguments to the manager's
    // PathfindManager::SetAvoidanceAllowed (FUN_062dd3d0) via [[this+0x20]+0xc]: Value (low byte)
    // goes to the pathfinder's two obstacle-avoidance latches selected by Mask (bit 0
    // bObstacleNudgeAllowed, bit 1 bAvoidPointAllowed). FreeFlightBehavior passes the thrust
    // manager's bAutoAvoidance, GotoBehavior `bDisableAvoidance == 0`.
    void SetAvoidanceAllowed(int Value, int Mask);

    // ---- Behavior's own data block, +0x04..+0x29 ------------------------------------------
    // FUN_062d2020 writes +0x04 = its argument, +0x08/+0x14/+0x24/+0x18/+0x1c = 0,
    // +0x29 = 0, +0x28 &= 0xfc, the vptr (.rdata:063a02c4), +0x0c = 2, +0x10 = 1; the binder
    // FUN_062d2090 writes +0x08, +0x14, +0x20, +0x24.

    // +0x04: the behavior type id. The twenty-one factory bodies (.text:062d16d0..062d1f90)
    // pass the BehaviorTypeDesc's Id (+0x04) to each constructor, which forwards it here.
    // Nothing in Common.dll reads it back.
    int TypeId;                     // +0x04
    unsigned OwnerId;               // +0x08, the binder's Owner->get_id()
    // +0x0c: OnEvaluateReadiness's result, stored and returned by every slot-5 override;
    // the ctor leaves 2, OnEngage 1.
    int Readiness;                  // +0x0c
    int UpdateResult;               // +0x10, OnUpdate's result; slot 9 (GetStatus) returns it
    IObjRW* OwnerContext;           // +0x14, the owner (binder's first argument)
    IObjDB* ObjectDb;               // +0x18, slot 2 (SetObjectDb)
    // +0x1c: the per-tick delta. IBehaviorManager::UpdateBehaviorTick stores its float
    // accumulator (+0xd4) here right before OnUpdate (.text:062d335d); OnEngage zeroes it.
    float DeltaTime;                // +0x1c
    IBehaviorManager* BehaviorMgr;  // +0x20, the owning manager (binder); not written by the ctor
    SystemManager* SysMgr;          // +0x24, the owner's SystemManager (binder)
    // +0x28: bit 0x1 is cleared by OnEngage and set by Behavior::ReleaseDirectiveTarget; bit 0x2
    // is set by the slot-10 overrides that accept a directive and cleared by cancel_notify.
    unsigned char Flags28;          // +0x28
    unsigned char Flag29;           // +0x29, cleared by OnDisengage; slot 5 answers 3 while set

    void ReleaseDirectiveTarget();
};
static_assert(offsetof(Behavior, TypeId) == 0x04, "Behavior::TypeId offset");
static_assert(offsetof(Behavior, OwnerId) == 0x08, "Behavior::OwnerId offset");
static_assert(offsetof(Behavior, Readiness) == 0x0c, "Behavior::Readiness offset");
static_assert(offsetof(Behavior, UpdateResult) == 0x10, "Behavior::UpdateResult offset");
static_assert(offsetof(Behavior, OwnerContext) == 0x14, "Behavior::OwnerContext offset");
static_assert(offsetof(Behavior, ObjectDb) == 0x18, "Behavior::ObjectDb offset");
static_assert(offsetof(Behavior, DeltaTime) == 0x1c, "Behavior::DeltaTime offset");
static_assert(offsetof(Behavior, BehaviorMgr) == 0x20, "Behavior::BehaviorMgr offset");
static_assert(offsetof(Behavior, SysMgr) == 0x24, "Behavior::SysMgr offset");
static_assert(offsetof(Behavior, Flags28) == 0x28, "Behavior::Flags28 offset");
static_assert(offsetof(Behavior, Flag29) == 0x29, "Behavior::Flag29 offset");
static_assert(sizeof(Behavior) == 0x2c, "Behavior size (CruiseBehavior, which adds nothing, is 0x2c)");

// ============================================================================
// BaseFollowerBehavior -- RTTI ".?AVBaseFollowerBehavior@@" (TypeDescriptor .data:063ebf30),
// the shared single-inheritance base of FormationBehavior (vftable 0639fb64) and
// FollowBehavior (vftable 0639fbe4). Own vftable .rdata:0639fae4, 29 slots (0639fae4..
// 0639fb54; 0639fb58 is the float constant DAT_0639fb58 and 0639fb60 the next COL).
// Constructor FUN_062cb2b0, destructor FUN_062cb3b0 (scalar-deleting 062cb390).
//
// The +0x2c..+0x2a0 block is declared here because FUN_062cb2b0 builds it and the base's
// slots 3/4/7/8/21/22 use it. sizeof is 0x2a0: base slot 4 (062cb5ee) writes +0x29c, so the
// Vector at +0x290 is a base member too.
//
// Vtable 0639fae4, overrides of Behavior's 27 plus the two this class adds:
//    0  062cb390  scalar-deleting dtor around FUN_062cb3b0
//    1  062cb470  SetContext
//    3  062cb540  OnEngage
//    4  062cb5e0  OnDisengage
//    5  06391fb0  _purecall
//    6  06391fb0  _purecall (evaluate)
//    7  062cb700  OnUpdate, the subtree's per-tick body (no subclass overrides it)
//    8  062cb6d0  PostUpdate
//   10  06391fb0  _purecall -- Behavior's concrete slot 10 is re-pured here
//   16  062ed730  set_afterburner: bare `RET 4`
//   17  062cbfc0  slide_strafe_burst: FUN_062e9030(Sys+0x18, 0), the argument replaced by 0
//   19  0629ba00  allow_head_tracking: `XOR AL,AL; RET`
//   21  062cf300  OnObjectLeftSystem
//   22  062cbea0  OnEvent
//   27  06391fb0  _purecall, UpdateFollowerSteering (void, no arguments: the one call site .text:062cbcbd
//                 pushes nothing and discards EAX)
//   28  06391fb0  _purecall, GetRotatedOffset (Vector by value: the `RET 4` pops MSVC's hidden
//                 return-buffer pointer -- .text:062cbc2d pushes a bare local and hands the
//                 returned EAX to FUN_062c50c0, which copies three dwords out of it)
// All four _purecall slots are `= 0` below; both subclasses override every one of them.
// ============================================================================
class BaseFollowerBehavior : public Behavior
{
public:
    // FUN_062cb2b0 (RET 4): FUN_062d2020(TypeId); LeadWatcher zeroed; the five Ideas at
    // +0x34/+0x90/+0x150/+0x1c8/+0x264 (EH states 2..6); bytes +0x280..+0x285 and +0x28c and
    // dwords +0x278/+0x27c zeroed; vptr 0639fae4. +0x288/+0x290/+0x29c are not written.
    explicit BaseFollowerBehavior(int InTypeId);
    // FUN_062cb3b0: guarded set_pointer(0) on +0x1f0, +0x164, +0xa4, +0x48, +0x2c (the
    // members' own BaseWatcher destructors, in reverse order), then FUN_062d2080.
    ~BaseFollowerBehavior() override;

    BaseFollowerBehavior(BaseFollowerBehavior const&) = delete;
    BaseFollowerBehavior& operator=(BaseFollowerBehavior const&) = delete;

    // Slot 1, LAB_062cb470 (RET 0xc).
    bool SetContext(IObjRW* Owner, IBehaviorManager* Manager, SystemManager* Sys) override;
    // Slot 3, FUN_062cb540.
    void OnEngage() override;
    // Slot 4, FUN_062cb5e0.
    void OnDisengage() override;
    // Slots 5, 6 and 10 are _purecall in this table.
    int OnEvaluateReadiness(float Value) override = 0;
    EvaluateResult evaluate(unsigned int Context) override = 0;
    // Slot 7, LAB_062cb700 (488 instructions).
    int OnUpdate() override;
    // Slot 8, LAB_062cb6d0.
    void PostUpdate() override;
    bool OnApplyDirective(void* DirectiveOp) override = 0;
    // Slot 16, 062ed730: a bare `RET 4`.
    void set_afterburner(bool bOn) override { (void)bOn; }
    // Slot 17, LAB_062cbfc0.
    void slide_strafe_burst(StrafeDir Dir) override;
    // Slot 19, 0629ba00: `XOR AL,AL; RET`.
    bool allow_head_tracking() override { return false; }
    // Slot 21, LAB_062cf300 (RET 4). The argument is an object id (the manager's loop at
    // .text:062d405c hands every entry the dword at [Data+8]).
    void OnObjectLeftSystem(void* Arg) override;
    // Slot 22, FUN_062cbea0 (RET 4). The argument is the manager's FBehaviorManagerEvent.
    void OnEvent(void* Arg) override;
    // Slots 27 and 28, _purecall here.
    virtual void UpdateFollowerSteering() = 0;
    virtual Vector GetRotatedOffset() = 0;

protected:
    // FUN_062cbcf0 (RET 4), OnUpdate's "final leg" payload, called at .text:062cbca4 with
    // DeltaTime. A switch on SystemManager+0x1810 (0..3; anything above 3 does nothing).
    void UpdateTickPayload(float Delta);

public:
    // ---- BaseFollowerBehavior's own block, built by FUN_062cb2b0 ----
    // +0x2c: the ship being followed. OnUpdate re-points it every tick from the SystemManager's
    // +0x1364 directive record (FUN_062ecaf0); FormationBehavior's slots 5/27 from
    // FUN_062ecab0 / FUN_062ecad0, FollowBehavior's slot 10 from the directive op.
    BaseWatcher LeadWatcher;        // +0x2c
    MatchThrottle2Idea MatchThrottle; // +0x34 (ctor FUN_062d5aa0)
    FaceIdea Face;                  // +0x90 (ctor FUN_062d6750)
    MoveIdea Move;                  // +0x150 (ctor FUN_062d4e60)
    BreakAwayIdea BreakAway;        // +0x1c8 (ctor FUN_062d8970)
    AfterburnerIdea Afterburner;    // +0x264 (ctor FUN_062da500)
    // +0x278 / +0x27c: the manager's own Dock (BehaviorDispatchTable[7], Manager+0x190) and
    // Goto (BehaviorDispatchTable[2], Manager+0x17c) behaviors, captured by OnEngage
    // (062cb58f / 062cb5c7). OnUpdate drives them as child behaviors: it stores its DeltaTime
    // to their +0x1c and dispatches slots 3/4/7/10, the manager's own protocol.
    Behavior* DockDelegate;         // +0x278
    Behavior* GotoDelegate;         // +0x27c
    unsigned char bLeftTradeLane;      // +0x280: OnEvent sets it when the owner left a trade lane
    unsigned char bInTradeLane;        // +0x281: go_tradelane accepted the record's lane order (returned 0)
    unsigned char bDockDelegateActive; // +0x282: DockDelegate is running
    // +0x283: OnEngage sets it for a player owner; OnUpdate's one catch-up goto to a distant,
    // slow leader runs while it is set and clears it.
    unsigned char bPlayerCatchUp;
    unsigned char bGotoDelegateActive; // +0x284: GotoDelegate is running
    unsigned char Flag285;          // +0x285: the record's +0xb0 order was consumed
    unsigned char Pad286[2];
    int Field288;                   // +0x288: OnEngage zeroes it; no reader found in this class
    unsigned char Flag28c;          // +0x28c: FormationBehavior::OnEngage sets it from is_player()
    unsigned char Pad28d[3];
    // +0x290: FormationBehavior: the owner's get_formation_offset (OnEngage, UpdateFollowerSteering);
    // FollowBehavior: the follow offset its slot 10 copies from the directive op.
    Vector Offset290;               // +0x290
    int Field29c;                   // +0x29c: OnDisengage zeroes it; no reader found in this class
};
static_assert(offsetof(BaseFollowerBehavior, LeadWatcher) == 0x2c, "BaseFollowerBehavior::LeadWatcher offset");
static_assert(offsetof(BaseFollowerBehavior, MatchThrottle) == 0x34, "BaseFollowerBehavior::MatchThrottle offset");
static_assert(offsetof(BaseFollowerBehavior, Face) == 0x90, "BaseFollowerBehavior::Face offset");
static_assert(offsetof(BaseFollowerBehavior, Move) == 0x150, "BaseFollowerBehavior::Move offset");
static_assert(offsetof(BaseFollowerBehavior, BreakAway) == 0x1c8, "BaseFollowerBehavior::BreakAway offset");
static_assert(offsetof(BaseFollowerBehavior, Afterburner) == 0x264, "BaseFollowerBehavior::Afterburner offset");
static_assert(offsetof(BaseFollowerBehavior, DockDelegate) == 0x278, "BaseFollowerBehavior::DockDelegate offset");
static_assert(offsetof(BaseFollowerBehavior, GotoDelegate) == 0x27c, "BaseFollowerBehavior::GotoDelegate offset");
static_assert(offsetof(BaseFollowerBehavior, bLeftTradeLane) == 0x280, "BaseFollowerBehavior::bLeftTradeLane offset");
static_assert(offsetof(BaseFollowerBehavior, Flag285) == 0x285, "BaseFollowerBehavior::Flag285 offset");
static_assert(offsetof(BaseFollowerBehavior, Field288) == 0x288, "BaseFollowerBehavior::Field288 offset");
static_assert(offsetof(BaseFollowerBehavior, Flag28c) == 0x28c, "BaseFollowerBehavior::Flag28c offset");
static_assert(offsetof(BaseFollowerBehavior, Offset290) == 0x290, "BaseFollowerBehavior::Offset290 offset");
static_assert(offsetof(BaseFollowerBehavior, Field29c) == 0x29c, "BaseFollowerBehavior::Field29c offset");
static_assert(sizeof(BaseFollowerBehavior) == 0x2a0, "BaseFollowerBehavior size (base slot 4 writes +0x29c)");

// ============================================================================
// FormationBehavior -- RTTI ".?AVFormationBehavior@@" (COL 0639fb60 -> 063a8fb8), behavior
// type 10 ("Formation"). vftable .rdata:0639fb64, 29 slots; factory .text:062d1b67
// `PUSH 0x2b8` before the ctor FUN_062cbfe0, so sizeof is 0x2b8.
//
// Own slots (the rest are BaseFollowerBehavior's):
//    3  FUN_062cc3b0  OnEngage          4  LAB_062cc410  OnDisengage
//    5  FUN_062cc030  OnEvaluateReadiness          6  LAB_062cc0d0  evaluate
//   10  LAB_062cd630  OnApplyDirective        12  LAB_062cc990  set_direction
//   27  LAB_062cc470  UpdateFollowerSteering        28  LAB_062cc8f0  GetRotatedOffset
// ============================================================================
class FormationBehavior : public BaseFollowerBehavior
{
public:
    // FUN_062cbfe0 (RET 4): FUN_062cb2b0(TypeId); +0x280 = +0x281 = +0x2a8 = 0; vptr;
    // +0x2a0 = 200.0f; +0x2a4 = 500.0f.
    explicit FormationBehavior(int InTypeId);

    // Slot 3, FUN_062cc3b0.
    void OnEngage() override;
    // Slot 4, LAB_062cc410.
    void OnDisengage() override;
    // Slot 5, FUN_062cc030 (RET 4, argument unread).
    int OnEvaluateReadiness(float Value) override;
    // Slot 6, LAB_062cc0d0 (RET 4, argument unread): 1 when the owner's target is a valid
    // formation candidate, else 2.
    EvaluateResult evaluate(unsigned int Context) override;
    // Slot 10, LAB_062cd630: `OR byte [ECX+0x28],0x2; MOV AL,1; RET 4` (shared with
    // IdleBehavior's slot 10).
    bool OnApplyDirective(void* DirectiveOp) override;
    // Slot 12, LAB_062cc990 (RET 4).
    void set_direction(Vector const& Dir) override;

    // Slot 27, LAB_062cc470 (362 instructions): place the ship in the leader's formation slot.
    void UpdateFollowerSteering() override;
    // Slot 28, LAB_062cc8f0 (`RET 4` = hidden return buffer): the owner's formation offset
    // rotated into the leader's frame.
    Vector GetRotatedOffset() override;

    // ---- FormationBehavior's own fields (+0x2a0..+0x2b8) ----
    // +0x2a0 / +0x2a4: the standoff UpdateFollowerSteering adds to the own physical radius -- +0x2a4 when
    // cruising outside a trade lane, +0x2a0 otherwise. Ctor 200.0f / 500.0f (062cc007/062cc011).
    float StandoffDistance2a0;       // +0x2a0
    float CruiseStandoffDistance2a4; // +0x2a4
    // +0x2a8: set_direction writes 1 (non-zero direction) or 0; OnEngage clears it; UpdateFollowerSteering
    // steers along FaceAxis2ac while it is set. Ctor 0.
    unsigned char bDirectionSet2a8;  // +0x2a8
    unsigned char Pad2a9[3];
    // +0x2ac: set_direction's (-y*k, -x*k, -z), the first two clamped to [-1, 1]; UpdateFollowerSteering hands
    // it to FaceIdea::SetAxis. Not ctor-initialised in retail.
    Vector FaceAxis2ac;              // +0x2ac
};

static_assert(offsetof(FormationBehavior, StandoffDistance2a0) == 0x2a0, "FormationBehavior::StandoffDistance2a0 offset");
static_assert(offsetof(FormationBehavior, CruiseStandoffDistance2a4) == 0x2a4, "FormationBehavior::CruiseStandoffDistance2a4 offset");
static_assert(offsetof(FormationBehavior, bDirectionSet2a8) == 0x2a8, "FormationBehavior::bDirectionSet2a8 offset");
static_assert(offsetof(FormationBehavior, FaceAxis2ac) == 0x2ac, "FormationBehavior::FaceAxis2ac offset");
static_assert(sizeof(FormationBehavior) == 0x2b8, "FormationBehavior size == retail operator_new argument at .text:062d1b67");

// ============================================================================
// FUN_062ec520 ("is WantedId among the owner's 32 formation-follower ids") is
// FFormationLeaderState::IsIdAmongOwnerFormationFollowers (common_systemmanager.h,
// common_gunconvergence.cpp): both callers (GotoBehavior::Update, GuideBehavior::Update)
// pass SysMgr+0x1364 as the receiver.
// ============================================================================
