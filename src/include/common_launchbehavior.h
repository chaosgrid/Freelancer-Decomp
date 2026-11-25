// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_goods.h"     // BaseWatcher
#include "common_idea.h"      // MoveIdea, FaceIdea -- real classes behind Move/B (see below)

#include <cstddef>

// ============================================================================
// LaunchBehavior -- AI "launch/catapult" state (Behavior family)
// ============================================================================
//
// RTTI: COL .rdata:0x063a9094 -> ".?AVLaunchBehavior@@" (.data:0x063ebfb0), CHD 0x063a9084
// numBaseClasses = 2: single inheritance from Behavior (BCD 0x063a8c78).
//
// The row of the behavior descriptor table (.rdata 0x063a002c-0x063a0238) whose ctor is
// FUN_062cd640 has an empty name: launches are never issued as named directives.
//
// Constructor FUN_062cd640: FUN_062d2020, MoveIdea at +0x2c (FUN_062d4e60), FaceIdea at
// +0xa4 (FUN_062d6750), zeroes several fields, LaunchDwellDuration = 3.5f,
// LaunchThrottleScale = 15.0f. Retail size 0x234.
// ============================================================================

class LaunchBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (8, "Launch"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::TypeId in common_behavior.h.
    explicit LaunchBehavior(int InTypeId);
    ~LaunchBehavior() override;

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639fccc, 27 slots). Own entries:
    //   0  FUN_062cd6e0  dtor wrapper -> FUN_062cd700
    //   1  LAB_062cd790  SetContext
    //   3  FUN_062cd830  Engage
    //   4  FUN_062cd8b0  Disengage
    //   5  LAB_062cd910  SetModeFromFlags
    //   7  LAB_062cd950  Update (457 instructions; 6-way ModeState164 state machine)
    //  10  FUN_062cdf30  ApplyDirective
    // The rest are Behavior's entries or ICF stubs.
    // ------------------------------------------------------------------

    // Destructor FUN_062cd700 (called by the thunk FUN_062cd6e0): clears Watcher168 and the
    // Ideas' watchers at +0xb8 / +0x40, then FUN_062d2080.

    // Slot 1, LAB_062cd790 (45 instructions): BindContext (FUN_062d2090); false -> false.
    // Otherwise Move.SetContext(OwnerContext, SysMgr, Param2) (Param2 is
    // kept in a register across the bind), FUN_062d5980(&Move, 0), the same SetContext
    // on Face, and LaunchTimeout = NestedNavQuery0xbc() ? 15.0f : 60.0f
    // (0x41700000 / 0x42700000).
    bool SetContext(void* Param1, void* Param2, void* Param3);

    // Slot 3, FUN_062cd830: Behavior::OnEngage (FUN_062d20d0); CachedSystemId170 =
    // FetchSystemId(); TargetType174 = GetObjectTypeOrZero (FUN_062e6200) of Watcher168's
    // target; FUN_062e9a60(SysMgr+0x12c, 0); ModeState164 = 1, bStraightLaunch = 0,
    // LaunchDwellElapsed = TotalLaunchElapsed = 0.0f; SetAvoidanceAllowed(0, 3) (FUN_062d21d0).
    void Engage();

    // Slot 4, FUN_062cd8b0: Move.Commit(), Face.Commit(), Behavior::OnDisengage
    // (FUN_062d20f0), FUN_062e9a60(SysMgr+0x12c, 1), SetAvoidanceAllowed(1, 3), Flags28
    // bit 0x2 cleared / bit 0x1 set, SetDirectiveResult(8, 0) on BehaviorMgr
    // (FUN_062d29a0).
    void Disengage();

    // Slot 5, LAB_062cd910:
    //   int mode = (Flags28 & 0x2) ? 0 : 2;
    //   if (UpdateResult == 2 || UpdateResult == 3) mode = 2;
    //   if (Flag29 != 0) { Readiness = 3; return 3; }
    //   Readiness = mode;
    //   return mode;
    int SetModeFromFlags();

    // Slot 10, FUN_062cdf30 (46 instructions). FUN_062e74a0(SysMgr+0x34,
    // &Op.LaunchObjectId) (FindVerifiedObject), adjusted by -8; if that differs from the
    // current Watcher168.Watched, re-points Watcher168 via
    // `BaseWatcher::set_pointer` (already-ported, common_goods.h); copies
    // Param->+0x10 into DockPortIndex; then re-derives the (possibly
    // just-updated) raw block from Watcher168 and, if non-null, sets Flags28
    // bit 0x2 and UpdateResult=1 and returns true, otherwise clears Flags28
    // bit 0x2 and returns false.
    bool ApplyDirective(void const* Param);

    // Slot 7, LAB_062cd950 (457 instructions): guards (CachedSystemId170 change -> 2,
    // TotalLaunchElapsed > LaunchTimeout -> 3), DeltaTime accumulation and the
    // ModeState164 dispatch, whose case payloads are the helpers below. Case 5 rotates
    // LaunchBasis by pi in the (x,z) plane (DAT_0639e2d0) behind a dot-vs-zero gate. See the
    // .cpp.
    int Update();

    // Slot 7: IBehaviorManager::UpdateBehaviorTick dispatches through vtable +0x1c.
    int OnUpdate() override { return Update(); }

    // Slots 3/4/5/10 as overrides. Slot 5 is `RET 4`: the float argument is ignored.
    void OnEngage() override { Engage(); }                        // 3 +0x0c FUN_062cd830
    void OnDisengage() override { Disengage(); }                     // 4 +0x10 FUN_062cd8b0
    int OnEvaluateReadiness(float) override { return SetModeFromFlags(); }   // 5 +0x14 LAB_062cd910
    bool OnApplyDirective(void* DirectiveOp) override { return ApplyDirective(DirectiveOp); } // 10 +0x28 FUN_062cdf30

private:
    // .text:062cd838-062cd846 / 062cd980-062cd98c: `ECX=[this+0x18]; CALL [[ECX]+8]` (IObjDB::
    // GetZoneDb, a Universe::ISystem*), then `CALL [[EAX]+0]` (ISystem slot 0, the system id).
    unsigned FetchSystemId() const;
    static void* FindLaunchObject(SystemManager* SysMgr, void const* Param); // FUN_062e74a0, ApplyDirective()'s own lookup

    // ---- Update() (slot 7) payload helpers. SysMgr is the SystemManager. ----

    // is_player() (vt+0xbc) on SystemManager::ExternalIface (+0x1580).
    bool NestedNavQuery0xbc();

    // Case 2 payload, .text:062cda2f-062cdbdd:
    //   * Watcher168's object -> get_dock_hardpoints (vt+0xc8): six pushes at
    //     .text:062cda42-062cda5f give (DockPortIndex, null, XformA = ESP+0x38,
    //     XformB = ESP+0x94, XformC = ESP+0x60, null).
    //   * OwnerContext->get_hardpoint("hpmount", ...) (vt+0xc0, .text:062cdb0d).
    //   * FUN_06391cf0 = Acquire3DMathEngine (cached in DAT_063fc538); its vt+0x38
    //     TransposeTransform yields LaunchOffset (.text:062cdbbe-062cdbd1).
    // Populates
    // DockPointA/ApproachBasis3 from XformA, DockPointB/ApproachBasis2 from XformB and
    // DockPointC/LaunchBasis from XformC, then (only when get_hardpoint FAILS) LaunchOffset
    // and MoveIdea::Offset4c via FUN_062d5a80.
    void ComputeLaunchApproachTransform();
    // case 3: FUN_062cdfa0(this) -- re-reads the dock hardpoints, sets bStraightLaunch
    // from three alignment dot products and special-cases the terminal type (bHalfThrottleOnExit,
    // bFlipHeadingOnExit, pi rotation of the three bases). Returns retail's AL (false only when the
    // watcher target is gone); the only caller ignores it.
    bool StartLaunchSequence();
    // MoveIdea::SetRange3c on Move (fold address .text:062a66b0, ICF-labelled
    // DeviceTRC::bind_to_dc).
    void SetApproachSpeedParam(float Value);
    // FUN_062d5980(Move, Flag) = MoveIdea::SetMode (.text:062cdc6a / 062cdcc9).
    void ReleaseApproachStep(int Flag);
    // FUN_062d5840(Move, &Vector) = MoveIdea::SetPosition.
    void SetApproachOffset(Vector const* OffsetVector);
    // FUN_062d75c0 on Face = FaceIdea::SetBasis: tag 4 and the 9-dword basis copied
    // in from the caller (LaunchBasis). Cases 4/5/6.
    void PushApproachBasisToSubObjectB(void const* SrcVectorBlock);
    // FUN_062d5a20(Move, LaunchThrottleScale) = MoveIdea::SetThrottleScale.
    void SetApproachThrottleScale(float Scale);
    // FUN_062d88b0(Move, Mode): the folded `MOV [ECX+0x44],EAX` setter,
    // MoveIdea::StopStyle.
    void SetApproachMode(int Mode);

    // BaseIdea slot 1 (Update) / slot 4 (Commit) on either Idea.
    int TickSubObject(BaseIdea* SubObject, float DeltaTime);
    void ReleaseSubObject(BaseIdea* SubObject);
    // Case 5: FUN_062e6d40's cache refresh, inlined -- SystemManager+0x14fc's +0x84
    // get_physical_radius (vt+0x2c), gated by +0x14fc+0x7e.
    void EnsureNestedNavPositionCached();
    // FUN_062d93c0(Face, pi/8), case 6: the folded `MOV [ECX+0x8c],EAX` setter,
    // FaceIdea::FacingTolerance.
    void SetHardpointCone(float Angle);
    // FUN_062e89a0(SysMgr+0x18, Value, 0) -- case 5, gated by bHalfThrottleOnExit:
    // ThrustManager::SetThrottle(Value, 0) on SystemManager::Thrust.
    void AdjustNavHelperThrottle(float Value);


public:
    // ------------------------------------------------------------------
    // Fields; +0x04..+0x29 is Behavior's. Retail size 0x234.
    // ------------------------------------------------------------------

    MoveIdea Move;    // +0x2c, ends +0xa4.
    FaceIdea Face;    // +0xa4, ends +0x164.

    int ModeState164;           // +0x164. Zeroed by ctor; set to 1 by Engage().
    BaseWatcher Watcher168;     // +0x168 (ends +0x170). Zeroed by ctor; cleared by dtor. The
                                 // launch target.
    unsigned CachedSystemId170; // +0x170. Engage(): ObjectDb->GetZoneDb()->GetId() (the current
                                 // system's id); Update() aborts with 2 when it changes.
    unsigned TargetType174;     // +0x174. Engage(): FUN_062e6200 == GetObjectTypeOrZero on the
                                 // watched launch target (target - 8).
    int DockPortIndex;      // +0x178. Zeroed by ctor; copied from the DirectiveLaunchOp's DockPortIndex.
    unsigned char bStraightLaunch;      // +0x17c. Cleared by Engage().
    unsigned char Pad17d[0x180 - 0x17d];
    float LaunchDwellDuration;             // +0x180. Ctor 3.5f (0x40600000). Update()'s case 1
                                 // leaves mode 1 once LaunchDwellElapsed > LaunchDwellDuration.
    float LaunchDwellElapsed;             // +0x184. Mode-1 dwell accumulator. Cleared by Engage()
                                 // only (not the ctor).
    float TotalLaunchElapsed;             // +0x188. Whole-launch elapsed time. Cleared by ctor and
                                 // Engage(); Update() returns 3 once it exceeds LaunchTimeout.
    float LaunchTimeout;             // +0x18c. 15.0f or 60.0f, set by every successful
                                 // SetContext(). Not ctor-initialized in retail.
    // ------------------------------------------------------------------
    // +0x190..+0x22c: Update()'s case 2/3/5 workspace, filled by
    // ComputeLaunchApproachTransform() (and StartLaunchSequence()).
    // ------------------------------------------------------------------
    Vector LaunchOffset;             // +0x190. The TransposeTransform result.
    Vector DockPointC;             // +0x19c. From XformC; also written by case 5.
    Vector LaunchBasis[3];       // +0x1a8. XformC's basis (9-dword REP MOVSD); case 5 rotates it
                                  // by pi in the (x,z) plane.
    Vector DockPointB;             // +0x1cc. From XformB.
    Vector ApproachBasis2[3];    // +0x1d8. XformB's basis.
    Vector DockPointA;             // +0x1fc. From XformA.
    Vector ApproachBasis3[3];    // +0x208. XformA's basis; ends at LaunchThrottleScale.
    float LaunchThrottleScale;              // +0x22c. Ctor 15.0f; case 4 passes it to
                                  // SetApproachThrottleScale().
    unsigned char bHalfThrottleOnExit;       // +0x230. Zeroed by ctor.
    unsigned char bFlipHeadingOnExit;       // +0x231. Zeroed by ctor.
};

static_assert(offsetof(LaunchBehavior, Move) == 0x2c, "LaunchBehavior::Move offset");
static_assert(offsetof(LaunchBehavior, Face) == 0xa4, "LaunchBehavior::Face offset");
static_assert(offsetof(LaunchBehavior, ModeState164) == 0x164, "LaunchBehavior::ModeState164 offset");
static_assert(offsetof(LaunchBehavior, Watcher168) == 0x168, "LaunchBehavior::Watcher168 offset");
static_assert(offsetof(LaunchBehavior, CachedSystemId170) == 0x170, "LaunchBehavior::CachedSystemId170 offset");
static_assert(offsetof(LaunchBehavior, TargetType174) == 0x174, "LaunchBehavior::TargetType174 offset");
static_assert(offsetof(LaunchBehavior, DockPortIndex) == 0x178, "LaunchBehavior::DockPortIndex offset");
static_assert(offsetof(LaunchBehavior, bStraightLaunch) == 0x17c, "LaunchBehavior::bStraightLaunch offset");
static_assert(offsetof(LaunchBehavior, LaunchDwellDuration) == 0x180, "LaunchBehavior::LaunchDwellDuration offset");
static_assert(offsetof(LaunchBehavior, LaunchDwellElapsed) == 0x184, "LaunchBehavior::LaunchDwellElapsed offset");
static_assert(offsetof(LaunchBehavior, TotalLaunchElapsed) == 0x188, "LaunchBehavior::TotalLaunchElapsed offset");
static_assert(offsetof(LaunchBehavior, LaunchTimeout) == 0x18c, "LaunchBehavior::LaunchTimeout offset");
static_assert(offsetof(LaunchBehavior, LaunchOffset) == 0x190, "LaunchBehavior::LaunchOffset offset");
static_assert(offsetof(LaunchBehavior, DockPointC) == 0x19c, "LaunchBehavior::DockPointC offset");
static_assert(offsetof(LaunchBehavior, LaunchBasis) == 0x1a8, "LaunchBehavior::LaunchBasis offset");
static_assert(offsetof(LaunchBehavior, DockPointB) == 0x1cc, "LaunchBehavior::DockPointB offset");
static_assert(offsetof(LaunchBehavior, ApproachBasis2) == 0x1d8, "LaunchBehavior::ApproachBasis2 offset");
static_assert(offsetof(LaunchBehavior, DockPointA) == 0x1fc, "LaunchBehavior::DockPointA offset");
static_assert(offsetof(LaunchBehavior, ApproachBasis3) == 0x208, "LaunchBehavior::ApproachBasis3 offset");
static_assert(offsetof(LaunchBehavior, LaunchThrottleScale) == 0x22c, "LaunchBehavior::LaunchThrottleScale offset");
static_assert(offsetof(LaunchBehavior, bHalfThrottleOnExit) == 0x230, "LaunchBehavior::bHalfThrottleOnExit offset");
static_assert(offsetof(LaunchBehavior, bFlipHeadingOnExit) == 0x231, "LaunchBehavior::bFlipHeadingOnExit offset");

// Retail object size from the factory's `PUSH 0x234` before operator_new.
static_assert(sizeof(LaunchBehavior) <= 0x234, "LaunchBehavior must not exceed its retail size 0x234");
