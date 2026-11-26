// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_pub.h"       // pub::AI::DirectiveGotoOp
#include "common_aisteer.h"   // ICRSplineSegment
#include "common_goods.h"     // BaseWatcher
#include "common_idea.h"      // MoveIdea, FaceIdea, SplineMoveIdea

#include <cstddef>

// ============================================================================
// GotoBehavior -- AI "fly to a point" state machine, intermediate base of
// FreeFlightBehavior (Behavior family)
// ============================================================================
//
// RTTI: TypeDescriptor .data:0x063ebe3c ".?AVGotoBehavior@@", the middle entry of
// FreeFlightBehavior's BaseClassArray (0x063a8d54): FreeFlightBehavior : GotoBehavior :
// Behavior. Own vtable .rdata:0x063a2284; FreeFlightBehavior's (0x0639f5dc) differs only in
// slots 0/3/4/6/10/15.
//
// Constructor FUN_062fd620: FUN_062d2020, then
//   +0x2c  FUN_062d4e60  MoveIdea       (0x78 bytes)
//   +0xa4  FUN_062d6750  FaceIdea       (0xc0 bytes)
//   +0x164 FUN_062d9bf0  SplineMoveIdea (0x174 bytes, ends +0x2d8)
// then zeroes Watcher310, builds the ICRSplineSegment at +0x318, the proximity watcher at
// +0x370 (FUN_062c29b0, zeroes 4 dwords) and the DirectiveGotoOp at +0x38c (ends +0x3fc,
// operator_new(0x3fc) at every allocation site), installs the vptr (PTR_FUN_063a2284) and
// sets the own fields +0x2d8..+0x36c. The `CMP [this+0x310],0` that follows is always taken
// (dead, as in LootBehavior).
//
// Destructor FUN_062fd7a0 (called by the thunk FUN_062fd780; FreeFlightBehavior's
// FUN_062c6df0 is a copy): set_pointer(nullptr) on +0x370, +0x310, +0x204
// (SplineMove.Face.Watcher), +0x18c (SplineMove.Move.Watcher), +0xb8 (Face.Watcher),
// +0x40 (Move.Watcher), then FUN_062d2080.
// ============================================================================

class GotoBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (2, "Goto"), forwarded to Behavior's ctor.
    explicit GotoBehavior(int InTypeId);
    ~GotoBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x063a2284, 27 slots, COL [-4] 063a2280 -> 063a9f94). Own slots versus
    // Behavior::`vftable' 063a02c4: 0, 1, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 18, 21, 24 (the
    // rest are Behavior's ICF-folded entries):
    //   0 ~GotoBehavior (062fd780 / 062fd7a0)   1 SetContext 062fd890   3 OnEngage 062fd920
    //   4 OnDisengage 062fd9c0   5 OnEvaluateReadiness 062fda20   6 evaluate 062fda60   7 OnUpdate -> Update 062fdb80
    //  10 OnApplyDirective 062fe670  11 allow_user_steering 0627a710  12 set_direction 062fe970
    //  13 set_throttle 062feae0  14 set_cruise 062feb00  18 brake_reverse 062feb60
    //  21 OnObjectLeftSystem 062c6d30  24 get_target 062c6db0
    // FreeFlightBehavior (0639f5dc) replaces 0/3/4/6/10 and adds 15; see its header.
    // ------------------------------------------------------------------

public:
    // ------------------------------------------------------------------
    // Fields, ascending offset; retail size exactly 0x3fc.
    // ------------------------------------------------------------------

    MoveIdea Move;               // +0x2c, ends +0xa4 (watcher at +0x40)
    FaceIdea Face;               // +0xa4, ends +0x164 (watcher at +0xb8)
    SplineMoveIdea SplineMove;        // +0x164, ends +0x2d8 (watchers at +0x18c / +0x204)

    // +0x2d8..+0x2e0: the steering triple set_direction (FUN_062fe970) writes -- -Dir.y and
    // -Dir.x scaled by 1/turn-sensitivity and clamped to [-1,1], then -Dir.z
    // (FSTP dword at .text:062fe9cb/062fe9de/062feaca).
    Vector SteerAxis;             // +0x2d8. Zeroed by ctor.
    // +0x2e4: a throttle. set_throttle (LAB_062feae0) stores its float argument here and
    // set_cruise / OnEngage copy MaxThrottle into it.
    float UserThrottle;                // +0x2e4. Zeroed by ctor.
    unsigned char Flag2e8;          // +0x2e8. Zeroed by ctor.
    unsigned char Flag2e9;           // +0x2e9. Set to 1 by ctor.
    unsigned char Flag2ea;            // +0x2ea. Zeroed by ctor.
    unsigned char Flag2eb;             // +0x2eb. Zeroed by ctor; FreeFlightBehavior's ctor
                                        // sets it to 1.
    unsigned char Flag2ec;              // +0x2ec. Zeroed by ctor.
    unsigned char Pad2ed[0x2f0 - 0x2ed]; // unconfirmed span
    float Field2f0;               // +0x2f0. Set to 1.5f by ctor (retail: 0x3fc00000).
    int GotoKind;                  // +0x2f4. Set to 3 by ctor. OnApplyDirective mirrors GotoOp's
                                   // GotoKind (0/1/2) into it.
    // +0x2f8: lead point. OnApplyDirective's spline case copies Spline.P2 here (.text:062fe750-062fe763);
    // Update mirrors other points into it. Not written by the ctor (value-initialised here).
    Vector ActiveTargetPos;
    // +0x304..+0x30c: the goto destination point. OnApplyDirective's case 1 copies GotoOp.Destination
    // here (.text:062fe76b-062fe783); FreeFlightBehavior::OnEngage sets all three to 1e6f.
    // The ctor zeroes them.
    Vector Destination;
    BaseWatcher Watcher310;      // +0x310 (8 bytes, ends +0x318). Zeroed by ctor;
                                  // unwatched via set(nullptr) by the real destructor.
    ICRSplineSegment Spline;    // +0x318 (ends +0x358). Real, already-ported type
                                  // (common_aisteer.h).
    float ArrivalRange;             // +0x358. Set to 200.0f by ctor (retail: 0x43480000).
    float MaxSpeed;               // +0x35c. Set to -1.0f by ctor (retail: 0xbf800000); OnApplyDirective: GotoOp.MaxSpeed.
    float MaxThrottle;                // +0x360. Set to 1.0f by ctor (retail: 0x3f800000).
    // +0x364: Update()'s "far enough to cruise" byte -- written only at .text:062fe237 as
    // (clearance > 1750.0f) while Flag365 is set, read at 062fe26e / 062fe27a. Not written by
    // retail's ctor (value-initialised here).
    unsigned char Flag364;
    unsigned char Flag365;         // +0x365. Set to 1 by ctor.
    unsigned char bFlyThrough;          // +0x366. Zeroed by ctor. OnApplyDirective: GotoOp.bFlyThrough.
    unsigned char Flag367;          // +0x367. Not ctor-written. SetContext sets 1, OnApplyDirective copies
                                    // GotoOp.Field_59; both feed MoveIdea::SetEnabled.
    unsigned char Flag368;         // +0x368. Zeroed by ctor.
    unsigned char Flag369;          // +0x369. Zeroed by ctor.
    unsigned char bGotoCruise;     // +0x36a. Zeroed by ctor; GotoOp.bGotoCruise, forces cruise on.
    unsigned char bGotoNoCruise;   // +0x36b. Zeroed by ctor; GotoOp.bGotoNoCruise, forces cruise off.
    unsigned char Flag36c;              // +0x36c. Set to 1 by ctor.
    unsigned char Pad36d[0x370 - 0x36d]; // unconfirmed span

    // +0x370 (ends +0x38c): the same 0x1c-byte proximity watcher DockBehavior embeds at +0x450
    // (DockBehavior::DockProximityWatcher): built by the shared FUN_062c29b0, and Update calls
    // FUN_062c2af0 / FUN_062c2c70 / FUN_062c2d90 on this+0x370 (.text:062fe0dd ...).
    // TODO: the type is declared twice (here as members, there as a struct); hoist it.
    BaseWatcher Watcher370;        // +0x370 (ends +0x378). Struct-relative +0x00.
    float ProxNearDistance;        // +0x378. Struct +0x08. Zeroed by the shared ctor; near
                                   // threshold.
    float ProxFarDistance;         // +0x37c. Struct +0x0c. Zeroed by the shared ctor; far
                                   // threshold.
    float ProxCruiseSpeedRatio;    // +0x380. Struct +0x10. Written by ProxWatcherInit
                                   // (FUN_062c29f0); not zeroed by the shared ctor.
    unsigned Flags384;             // +0x384. Struct +0x14. Distance-zone flags 0x1/0x2/0x4,
                                   // recomputed by FUN_062c2af0. Bit 0x2 is read by Update's
                                   // "clear to proceed" guard (.text:062fe1c4, 062fe5b6).
    unsigned SavedFlags388;        // +0x388. Struct +0x18. Previous-tick snapshot of Flags384.
    pub::AI::DirectiveGotoOp GotoOp; // +0x38c (ends +0x3fc).

    // ------------------------------------------------------------------
    // Methods
    // ------------------------------------------------------------------

    // Slot 7 body, .text:062fdb80 (796 instructions). Returns the tri-state result (1/2/3)
    // also stored in UpdateResult. See common_gotobehavior.cpp.
    int Update();

    // Slot 7: IBehaviorManager::UpdateBehaviorTick dispatches through vtable +0x1c.
    int OnUpdate() override { return Update(); }

    // ---- Retail's own slots of .rdata:063a2284 (bodies in common_gotobehavior.cpp).
    bool SetContext(void* Owner, void* Manager, void* Sys) override;   //  1 LAB_062fd890 (RET 0xc)
    void OnEngage() override;                                             //  3 FUN_062fd920
    void OnDisengage() override;                                             //  4 FUN_062fd9c0
    int OnEvaluateReadiness(float Value) override;                                   //  5 LAB_062fda20 (RET 4)
    EvaluateResult evaluate(unsigned int Context) override;            //  6 LAB_062fda60 (RET 4)
    // 10 LAB_062fe670 (RET 4): applies a pub::AI::DirectiveGotoOp (0x70 bytes, copied into
    // GotoOp) and dispatches on its GotoKind.
    bool OnApplyDirective(void* DirectiveOp) override;
    // FUN_062fe940 (thiscall, RET 4), not virtual: `REP MOVSD` of 0x1c dwords from GotoOp into
    // *Out, then `(Flags28 >> 1) & 1`. Sole caller FleeBehavior's perform (.text:062c748f).
    bool CopyGotoOp(pub::AI::DirectiveGotoOp* Out) const;
    bool allow_user_steering() override { return true; }                            // 11 0627a710 `MOV AL,1; RET`
    void set_direction(Vector const& Dir) override;                    // 12 FUN_062fe970 (RET 4)
    void set_throttle(float Throttle) override;                        // 13 LAB_062feae0 (RET 4)
    void set_cruise(bool bOn) override;                                // 14 LAB_062feb00 (RET 4)
    void brake_reverse(bool bOn) override;                             // 18 LAB_062feb60 (RET 4)
    void OnObjectLeftSystem(void* Arg) override;                                   // 21 FUN_062c6d30 (RET 4)
    IObjRW const* get_target() override;                               // 24 LAB_062c6db0
};

static_assert(offsetof(GotoBehavior, Move) == 0x2c, "GotoBehavior::Move offset");
static_assert(offsetof(GotoBehavior, Face) == 0xa4, "GotoBehavior::Face offset");
static_assert(offsetof(GotoBehavior, SplineMove) == 0x164, "GotoBehavior::SplineMove offset");
static_assert(offsetof(GotoBehavior, SteerAxis) == 0x2d8, "GotoBehavior::SteerAxis offset");
static_assert(offsetof(GotoBehavior, UserThrottle) == 0x2e4, "GotoBehavior::UserThrottle offset");
static_assert(offsetof(GotoBehavior, Flag2e8) == 0x2e8, "GotoBehavior::Flag2e8 offset");
static_assert(offsetof(GotoBehavior, Flag2e9) == 0x2e9, "GotoBehavior::Flag2e9 offset");
static_assert(offsetof(GotoBehavior, Flag2ea) == 0x2ea, "GotoBehavior::Flag2ea offset");
static_assert(offsetof(GotoBehavior, Flag2eb) == 0x2eb, "GotoBehavior::Flag2eb offset");
static_assert(offsetof(GotoBehavior, Flag2ec) == 0x2ec, "GotoBehavior::Flag2ec offset");
static_assert(offsetof(GotoBehavior, Field2f0) == 0x2f0, "GotoBehavior::Field2f0 offset");
static_assert(offsetof(GotoBehavior, GotoKind) == 0x2f4, "GotoBehavior::GotoKind offset");
static_assert(offsetof(GotoBehavior, ActiveTargetPos) == 0x2f8, "GotoBehavior::ActiveTargetPos offset");
static_assert(offsetof(GotoBehavior, Destination) == 0x304, "GotoBehavior::Destination offset");
static_assert(offsetof(GotoBehavior, Flag367) == 0x367, "GotoBehavior::Flag367 offset");
static_assert(offsetof(GotoBehavior, Watcher310) == 0x310, "GotoBehavior::Watcher310 offset");
static_assert(offsetof(GotoBehavior, Spline) == 0x318, "GotoBehavior::Spline offset");
static_assert(offsetof(GotoBehavior, ArrivalRange) == 0x358, "GotoBehavior::ArrivalRange offset");
static_assert(offsetof(GotoBehavior, MaxSpeed) == 0x35c, "GotoBehavior::MaxSpeed offset");
static_assert(offsetof(GotoBehavior, MaxThrottle) == 0x360, "GotoBehavior::MaxThrottle offset");
static_assert(offsetof(GotoBehavior, Flag364) == 0x364, "GotoBehavior::Flag364 offset");
static_assert(offsetof(GotoBehavior, Flag365) == 0x365, "GotoBehavior::Flag365 offset");
static_assert(offsetof(GotoBehavior, bFlyThrough) == 0x366, "GotoBehavior::bFlyThrough offset");
static_assert(offsetof(GotoBehavior, Flag368) == 0x368, "GotoBehavior::Flag368 offset");
static_assert(offsetof(GotoBehavior, Flag369) == 0x369, "GotoBehavior::Flag369 offset");
static_assert(offsetof(GotoBehavior, bGotoCruise) == 0x36a, "GotoBehavior::bGotoCruise offset");
static_assert(offsetof(GotoBehavior, bGotoNoCruise) == 0x36b, "GotoBehavior::bGotoNoCruise offset");
static_assert(offsetof(GotoBehavior, Flag36c) == 0x36c, "GotoBehavior::Flag36c offset");
static_assert(offsetof(GotoBehavior, Watcher370) == 0x370, "GotoBehavior::Watcher370 offset");
static_assert(offsetof(GotoBehavior, ProxNearDistance) == 0x378, "GotoBehavior::ProxNearDistance offset");
static_assert(offsetof(GotoBehavior, ProxFarDistance) == 0x37c, "GotoBehavior::ProxFarDistance offset");
static_assert(offsetof(GotoBehavior, ProxCruiseSpeedRatio) == 0x380, "GotoBehavior::ProxCruiseSpeedRatio offset");
static_assert(offsetof(GotoBehavior, Flags384) == 0x384, "GotoBehavior::Flags384 offset (== Watcher370 + 0x14, the DockProximityWatcher::Flags14 slot read at .text:062fe1c4)");
static_assert(offsetof(GotoBehavior, SavedFlags388) == 0x388, "GotoBehavior::SavedFlags388 offset");
static_assert(offsetof(GotoBehavior, GotoOp) == 0x38c, "GotoBehavior::GotoOp offset");
static_assert(sizeof(GotoBehavior) == 0x3fc, "GotoBehavior size == retail operator_new(0x3fc)");

// ============================================================================
// The shared 0x1c-byte proximity watcher's methods, called on both GotoBehavior+0x370 and
// DockBehavior+0x450. They take the struct's base address as unsigned char* because the
// type is still declared twice. Struct-relative offsets:
//   +0x00 BaseWatcher (Watchable* + chain link)
//   +0x08 float  near threshold   (cruise-scaled by +0x10 when the watched
//                                  object reports IObjInspect::is_cruise_active)
//   +0x0c float  far threshold    (same scaling)
//   +0x10 float  cruise multiplier
//   +0x14 unsigned  3-bit distance-zone flags (0x1 / 0x2 / 0x4)
//   +0x18 unsigned  previous-tick snapshot of +0x14
// TODO: hoist the struct and make these member functions.
// ============================================================================

// The proximity watcher's zone flags (struct +0x14, DockProximityWatcher::Flags14; +0x18 holds
// last tick's copy), set by ProxWatcherRecomputeZoneFlags (FUN_062c2af0).
enum EProxWatcherZone
{
    kProxZoneOverlap = 0x1,   // the intercept clearance of the reference point exceeds its distance to the watched object
    kProxZoneNear = 0x2,      // intercept clearance within the near limit (minus the 50.0f hysteresis band)
    kProxZoneFar = 0x4,       // intercept clearance within the far limit; its entry / exit raise tether events 2 / 3
};

// FUN_062c29f0 (RET 0x14). Re-arms the watcher:
// +0x08 = Near, +0x0c = Far, watch Target (Target+8, deduplicated), clear +0x14/+0x18; then,
// when something is watched, +0x10 = CruiseRatio if it is > 0.0, else own ship's
// get_speed(1.0f, cruise) / Target's get_speed(1.0f, engine), clamped up to 1.0f.
// `SysMgr` is the SystemManager whose +0x08 Watcher names the own ship. GotoBehavior's
// OnApplyDirective calls it; DockBehavior::OnApplyDirective inlines the same sequence.
void ProxWatcherInit(unsigned char* Watcher, SystemManager* SysMgr, void* Target, float Near, float Far,
                     float CruiseRatio);

// FUN_062c2af0. Recomputes the +0x14 zone flags for this tick. `SysMgr` is the Behavior
// family's SysMgr; retail only ever uses `SysMgr + 0x14FC`, the
// AI steering element common_aisteer.h's GetSignedRadialClearanceToPoint /
// GetInterceptClearanceOfIface operate on. `RefPoint` is the world point the
// zones are measured against (GotoBehavior passes &this->ActiveTargetPos).
void ProxWatcherRecomputeZoneFlags(unsigned char* Watcher, SystemManager* SysMgr, Vector const& RefPoint);

// FUN_062c2c70. Returns a
// speed cap: the lesser of the zone-derived speed and the owner's own
// IObjInspect::get_speed(SelectorValue-or-1.0f, Selector). `OwnerContext` is
// the Behavior family's OwnerContext, `SysMgr` its SysMgr.
// Retail returns 0.0f when nothing is watched. The result is left unrounded in ST0
// (DockBehavior::AdvancePreamble FCOMPs it directly at .text:062c489d), hence double.
double ProxWatcherComputeSpeedCap(unsigned char* Watcher, IObjRW* OwnerContext, SystemManager* SysMgr,
                                  int Selector, float SelectorValue);

// FUN_062c2d90. Compares
// this tick's +0x14 flags against the +0x18 snapshot and, on one of three
// recognised transitions, raises notification code 1/2/3 through the behavior
// manager's own dispatcher (FUN_062d2a10). Does nothing otherwise.
void ProxWatcherNotifyZoneChange(unsigned char* Watcher, SystemManager* SysMgr, IBehaviorManager* BehaviorMgr,
                                            Vector const& RefPoint);

// ============================================================================
// The formation-follower envelope, filled by FUN_062ebf50 (RET 4):
//   `bool __thiscall FUN_062ebf50(This, Envelope* Out)`
// Ghidra's unaff_retaddr is the Out argument. Callers .text:062c4839, 062d9d5e, 062fe2eb.
// `This` is the SystemManager+0x1364 block (FFormationLeaderState): +0x08 is the owning
// SystemManager, +0x1C..+0x9C its 32-entry IObjRW* follower array, +0x9C/+0xA0 the two angle
// thresholds (0.1f / 0.2f).
//
// The out-struct's fields:
//   +0x00 Speed            -- get_speed(...) directly on the fallback path
//   +0x04 Throttle         -- get_projected_throttle(Speed, Type) (slot 0xD4)
//   +0x08 AngularSpeed     -- folded from get_angular_speed_XY (slot 0xF0)
//   +0x0C AngularThrottle  -- get_projected_axis_throttle_XY(...) (slot 0xF8)
//   +0x10 bCruiseAllowed   -- one byte. Retail sets it to 1 up front, and on the
//                             non-cruise path rewrites it to
//                             `MaxProjection < 20.0f && MinAlignCos > 0.99`; its
//                             only reader, GotoBehavior::Update, drops cruise on 0.
// Callers zero only the first four dwords before the call and let the callee
// write the byte; GotoBehavior::Update reads +0x04/+0x08/+0x10, FUN_062c47f0
// reads +0x00/+0x08, SplineMoveIdea::Update (LAB_062d9d00) reads +0x04 twice.
// TODO: its natural home is common_systemmanager.h, next to FFormationLeaderState.
// ============================================================================
struct FormationFollowerEnvelope
{
    float Speed;                 // +0x00
    float Throttle;              // +0x04
    float AngularSpeed;          // +0x08
    float AngularThrottle;       // +0x0c
    unsigned char bCruiseAllowed; // +0x10  FUN_062ebf50 writes it, GotoBehavior::Update reads it
};

static_assert(offsetof(FormationFollowerEnvelope, Speed) == 0x00, "FormationFollowerEnvelope::Speed offset");
static_assert(offsetof(FormationFollowerEnvelope, Throttle) == 0x04, "FormationFollowerEnvelope::Throttle offset");
static_assert(offsetof(FormationFollowerEnvelope, AngularSpeed) == 0x08, "FormationFollowerEnvelope::AngularSpeed offset");
static_assert(offsetof(FormationFollowerEnvelope, AngularThrottle) == 0x0c, "FormationFollowerEnvelope::AngularThrottle offset");
static_assert(offsetof(FormationFollowerEnvelope, bCruiseAllowed) == 0x10, "FormationFollowerEnvelope::bCruiseAllowed offset");
static_assert(sizeof(FormationFollowerEnvelope) == 0x14, "FormationFollowerEnvelope is four floats plus a byte, 4-aligned");

// The envelope's producer, FUN_062ebf50, is
// FFormationLeaderState::ComputeFormationFollowerEnvelope (common_systemmanager.h);
// its definition stays in common_gotobehavior.cpp next to its file-local helpers.
