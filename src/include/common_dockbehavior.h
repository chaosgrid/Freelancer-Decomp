// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_iface.h"     // IObjAffect::RequestCB
#include "common_pub.h"       // pub::AI::DirectiveDockOp, pub::AI::DirectiveGotoOp
#include "common_aisteer.h"   // ICRSplineSegment
#include "common_goods.h"     // BaseWatcher
#include "common_csys.h"      // Matrix (LookMatrixYup output, see Orient3d8 below)
#include "common_idea.h"      // MoveIdea, FaceIdea -- real classes behind Move/B (see below)

#include <cstddef>

// ============================================================================
// DockBehavior -- AI docking-maneuver state machine (Behavior family)
// ============================================================================
//
// RTTI: COL .rdata:0x063a8c4c -> ".?AVDockBehavior@@" (.data:0x063ebe04); BaseClassArray
// @ 0x063a8ca8 = [ DockBehavior, Behavior (mdisp 0), IObjAffect::RequestCB (mdisp 0x2c) ]:
// multiple inheritance, not BaseFollowerBehavior. The second COL (.rdata:0x063a8cc8,
// offset 0x2c) belongs to the RequestCB vtable, which the ctor FUN_062c2e60 stamps at +0x2c
// (PTR_LAB_0639f498) after the primary at +0 (PTR_FUN_0639f4a4). InstantTradeLaneBehavior
// has the same Behavior + RequestCB(0x2c) shape.
//
// Size 0x470 (factory .text:062d1a07). FUN_062c4900 (UpdateApproachTail, called from
// Update's tail at .text:062c3cea) and FUN_062c3d10 (the ten-case EnterState; also called
// from the RequestCB slot LAB_062c53f0 with this-0x2c and from slots 3/4/22) are this
// class's own members: they write up to +0x46c, past FormationBehavior's 0x2b8.
// ============================================================================

// Behavior (0x2c bytes) comes first, so the RequestCB vptr lands at +0x2c and the
// sub-objects start at +0x30.
class DockBehavior : public Behavior, public IObjAffect::RequestCB
{
public:
    // Retail's ctor argument is the behavior type id (7, "Dock"), forwarded to Behavior's ctor.
    explicit DockBehavior(int InTypeId);

    // ------------------------------------------------------------------
    // Primary vtable (.rdata:0x0639f4a4, 27 slots); unlisted slots are Behavior's ICF-folded
    // base entries:
    //   0  (0x00) FUN_062c3020        scalar-deleting-dtor thunk (real dtor FUN_062c30d0)
    //   1  (0x04) LAB_062c31c0        SetContext()
    //   3  (0x0c) LAB_062c3720        RecomputeApproach()
    //   4  (0x10) LAB_062c3910        Disengage()
    //   5  (0x14) LAB_062c35c0        SetModeFromFlags()
    //   6  (0x18) LAB_062c35f0        CheckClearanceProgress()
    //   7  (0x1c) LAB_062c3b70        Update()
    //  10  (0x28) FUN_062c3370        OnApplyDirective() (apply DirectiveDockOp)
    //  11  (0x2c) LAB_062c3000        IsClearanceState()
    //  12  (0x30) LAB_062c5540        SubmitGotoDirective()
    //  13  (0x34) LAB_062c5650        set_throttle
    //  14  (0x38) LAB_062c5670        set_cruise
    //  16  (0x40) LAB_062c5690        set_afterburner
    //  17  (0x44) LAB_062c56b0        slide_strafe_burst
    //  21  (0x54) FUN_062c2f60        OnObjectLeftSystem() (drop watchers on id)
    //  22  (0x58) LAB_062c54b0        OnWatchedObjectDestroyed()
    //  24  (0x60) LAB_062c2fe0        GetWatchedRawBlock()
    //  26  (0x68) LAB_062c2fe0        same target as slot 24
    //
    // Secondary vtable (.rdata:0x0639f498, IObjAffect::RequestCB at +0x2c), 1 slot:
    //   0  (0x00) LAB_062c53f0        OnResult()
    // ------------------------------------------------------------------

    // FUN_062c30d0: member teardown in reverse declaration order (six
    // BaseWatcher destructors) and FUN_062d2080 (~Behavior) -- all compiler-generated.
    ~DockBehavior() override;

    // Slot 5, LAB_062c35c0: mode = (Flags28 & 0x2) ? 1 : 2, 3 if Flag29; stored in
    // Readiness and returned (RET 4).
    int SetModeFromFlags();

    // Slot 11, LAB_062c3000: ModeState320 (+0x320, not Readiness) is none of
    // 6/7/8/10.
    bool IsClearanceState() const;

    // Slots 24 and 26, LAB_062c2fe0: Watcher324's watched pointer minus 8, or null.
    void* GetWatchedRawBlock() const;

    // Slot 7, LAB_062c3b70 (RET 0): early outs 2/3, the player-owner docking-target
    // health latch (Flag39b, IObjAffect slot_6c), release of both Ideas while the target
    // moves faster than sqrt(0.3), else the +0x450 proximity watcher (FUN_062c2af0 /
    // FUN_062c2d90) and UpdateApproachTail.
    int Update();

    // FUN_062c4900, not a vtable slot: the tail half of Update(), its only caller
    // (.text:062c3cea, unadjusted this). Returns the tri-state left in UpdateResult
    // (0 = done/idle, 1 = continue, 2 = give up).
    int UpdateApproachTail();

    // Slot 7: IBehaviorManager::UpdateBehaviorTick dispatches through vtable +0x1c.
    int OnUpdate() override { return Update(); }

    // The other slots of the vftable (.rdata:0639f4a4).
    bool OnApplyDirective(void* DirectiveOp) override;                              // 10 FUN_062c3370 (RET 4)
    void OnObjectLeftSystem(void* Arg) override;                                      // 21 FUN_062c2f60 (RET 4)
    void OnEngage() override { RecomputeApproach(); }                       // 3  LAB_062c3720
    void OnDisengage() override { Disengage(); }                               // 4  LAB_062c3910
    int OnEvaluateReadiness(float) override { return SetModeFromFlags(); }             // 5  LAB_062c35c0 (RET 4)
    EvaluateResult evaluate(unsigned int) override                       // 6  LAB_062c35f0 (RET 4)
    { return static_cast<EvaluateResult>(CheckClearanceProgress()); }
    bool allow_user_steering() override { return IsClearanceState(); }                // 11 LAB_062c3000
    void set_direction(Vector const& Dir) override { SubmitGotoDirective(Dir); } // 12 LAB_062c5540
    // 13 LAB_062c5650 / 14 LAB_062c5670: `ECX = [this+0x2f0]; if (ECX) JMP [[ECX]+0x34/+0x38]`
    // with the argument passed through -- forwarded to the Goto pilot when there is one.
    void set_throttle(float Throttle) override;
    void set_cruise(bool bOn) override;
    // 16 LAB_062c5690 / 17 LAB_062c56b0: `if ([this+0x2f0]) JMP FUN_062d2110 / FUN_062d21b0`
    // with this unchanged -- Behavior's own bodies, only while a pilot is attached.
    void set_afterburner(bool bOn) override;
    void slide_strafe_burst(StrafeDir Dir) override;
    void OnEvent(void* Message) override { OnWatchedObjectDestroyed(Message); } // 22 LAB_062c54b0

    // FUN_062c3590, `RET 4`, sole caller FleeBehavior::Perform (.text:062c7403):
    // `REP MOVSD` of 0xb dwords from this+0x2f4 (DockOp) into *Out, then
    // `(Flags28 >> 1) & 1`.
    bool CopyDockOp(pub::AI::DirectiveDockOp* Out) const;
    // 24 / 26: both LAB_062c2fe0 -- Watcher324's object (target - 8) or null.
    IObjRW const* get_target() override { return static_cast<IObjRW const*>(GetWatchedRawBlock()); }
    void* GetDockTarget() override { return GetWatchedRawBlock(); }

    // IObjAffect::RequestCB override, the secondary vtable's only slot (LAB_062c53f0);
    // reaches EnterState and FUN_062c50e0 with this-0x2c.
    bool OnResult(int Param1, int Param2, int Param3) override;

    // Slot 1, LAB_062c31c0. The binder FUN_062d2090 (false when P1 or P3 is null) gates the
    // rest: resets AbortFlag398/Flag39b, SetContext on Move/B/C with
    // (OwnerContext, SysMgr, raw Param2), zeroes Opaque444 (+0x444..+0x44c) and
    // Flags28 bit 0x2, and when the "hpmount" hardpoint lookup fails rebuilds
    // OrientMatrix3d8 from the SysMgr's own target. Returns the binder's result.
    bool SetContext(void* Param1, void* Param2, void* Param3) override;

    // Slot 3, LAB_062c3720. Early-outs to `EnterState(0xd)` if
    // Watcher324's watched block is gone; otherwise resets several per-tick
    // fields (DockPortIndex=-1, CombatFlag344=0, BrakingDistance via FUN_062e5af0/
    // FUN_062e5ab0*3.0, SetAvoidanceAllowed(1,3) via FUN_062d21d0), then branches on
    // StateFlags32c's sign bit: if clear, calls OwnerContext's vtbl+0x138
    // "get transform" (writes Opaque388/Facing38c/an intercept-point out-param),
    // builds OrientMatrix3d8 via LookMatrixYup, and updates
    // Move via FUN_062d4e50 with the watched block's vtbl+0x20 result;
    // if set and bit 0x40 set, tail-calls FUN_062c50e0.
    // Finally checks OwnerContext's vtbl+0xbc "is threatening" bool and a
    // distance-squared-vs-DAT_063fc518 test on the watched block's position to
    // decide `EnterState(0xd)` vs `EnterState(1)`, then FUN_062d5a80 on Move and a
    // PI-shaped dword store to +0x46c.
    void RecomputeApproach();

    // Slot 4, LAB_062c3910, the "give up the maneuver" routine.
    //   * ModeState320 == 0: short tail LAB_062c3afc -- return unless ApproachClearedFlag334;
    //     else reset TerminalType/RequestedDockPortIndex/ApproachClearedFlag334 and cancel
    //     the owner's request.
    //   * Otherwise lock_maneuvers(false) and release NavPilotPtr, then by StateFlags32c's
    //     sign bit: set -> Move.SetField44(2) (FUN_062d88b0), the AbortFlag398/Flag399
    //     gated CShip::request_exit_tradelane and the Flag39a gated cancel; clear ->
    //     SetField44(2 when the owner is the player and ModeState320 == 1, else 0) and,
    //     unless Flags28 bit 0, the resets.
    //   * Shared tail LAB_062c3a9e: clear Flag39a/AbortFlag398, GotoDirective vtbl+0x10,
    //     Face.Commit(), Move.Commit(), SetAvoidanceAllowed(1,3),
    //     GunGroups.SetShieldActive(1) (FUN_062e9a60), FUN_062d20f0, ModeState320 = 0.
    //     GotoWatcherPtr is not cleared here.
    void Disengage();

    // Slot 6, LAB_062c35f0. Returns 1 or 2 (an EvaluateResult, hence int). 1 if
    // AbortFlag398; 2 without a target. Otherwise the target's type bits decide
    // (0x600de0 / 0x8c0 / 0x120, or for bases FindObjectDockingAtBase, FUN_062e7400)
    // whether a speed/facing check follows (vtbl+0x40 against DAT_06399408, vtbl+0x4
    // squared against DAT_0639f484): 1 if both pass, else 2.
    int CheckClearanceProgress();

    // Slot 12, LAB_062c5540. No-op without NavPilotPtr. When the squared magnitude of
    // ReferencePoint compares unordered against DAT_06399408, forwards it to NavPilotPtr's
    // vtbl+0x30. Otherwise builds a pub::AI::DirectiveGotoOp (ArrivalRange from DAT_063ebdb4,
    // arrival radii from ProxWatcher450, destination from the intercept point at +0x3cc when
    // StateFlags32c & 0xc0, else the watched object's position) and submits it via
    // NavPilotPtr vtbl+0x28.
    void SubmitGotoDirective(Vector const& ReferencePoint);

    // Slot 22, LAB_062c54b0. A notification-message handler:
    // `param->TypeCode` (+0) selects between two "is this about MY watched
    // object" identity checks against `param->Payload` (+4) -- type 4 checks
    // a flag byte plus two vtbl+0x20 "get id" calls (OwnerContext's and
    // Watcher324's watched-block's) against Payload+0xc/+0x10; type 3 checks
    // SysMgr's nested +0x14fc/+0x84 sub-object's own vtbl+0x20 result
    // against Payload+8. On a match, sets Flag399=1 and calls
    // `EnterState(0xc)`. Any other TypeCode, or a non-matching identity check, is a
    // silent no-op.
    void OnWatchedObjectDestroyed(void* Message);

    // FUN_062c3d10, the ten-case state entry; not a vtable slot. Callers: RecomputeApproach
    // (.text:062c375b, 062c38c4), OnResult (.text:062c5440..062c552e),
    // OnWatchedObjectDestroyed, all with an unadjusted this.
    void EnterState(int NewState);

private:
    // Helpers of UpdateApproachTail()/EnterState().
    bool CheckCanProceed();                                  // FUN_062c4530
    // FUN_062c56d0: builds the final-approach spline (+0x348) and hands it to SplineMove.
    void BuildFinalApproachSpline();

    // FUN_062c47f0 (thiscall). Computes this tick's linear and angular speed caps and
    // pushes them into the three Idea sub-objects; see the .cpp.
    void AdvancePreamble();
    // Retail's inlined "refresh the SysMgr's lead point unless already recomputed this
    // frame" (062c4a72, 062c4b0e, 062c3e97): if SysMgr+0x157a is clear, set
    // +0x14fc+0x7e and call slot 0x2c of +0x14fc+0x84 with (+0x14fc+0x18, +0x14fc+0xc).
    void NavHelperRefreshLeadPoint();
    // .rdata:DAT_0639f518, the double 0.6, the only multiplier EnterState()'s case 9 applies
    // to DockPointRadius (.text:062c439f).
    static double const kEnterState9RadiusScale;   // DAT_0639f518, a double

    // The retail setters these bodies call are Idea members (common_idea.h):
    //   FUN_062d5840 MoveIdea::SetPosition, FUN_062d5a80 MoveIdea::Offset4c,
    //   FUN_062d7560 FaceIdea::SetPosition, FUN_062d7590 FaceIdea::SetDirection,
    //   FUN_062d75c0 FaceIdea::SetBasis, FUN_062d5980 MoveIdea::SetMode,
    //   .text:062a66b0 MoveIdea::SetRange3c, .text:063275f0 MoveIdea::SetAngle38,
    //   FUN_062d5a70 MoveIdea::SetField59 (+0x59; SetEnabled at 062d5a10 writes +0x58),
    //   FUN_062d88b0 MoveIdea::SetField44 (all eleven callers pass Move).
    static float QueryHandleRange(void* HandleField, int Mode);            // FUN_062c2ad0


    // Disengage's three owner IObjAffect slot-0x60 sites (RequestType, &Watcher324 id, 0).
    void CancelOwnerRequest(int RequestType);
    // FUN_062c50e0, thiscall, RET 0: re-reads the dock hardpoints of Watcher324's
    // object for DockPortIndex into the approach fields and sets CombatFlag344 / Facing38c.
    // Called from RecomputeApproach() and OnResult()'s Param1==0 path.
    void RefreshApproachState();

public:
    // ------------------------------------------------------------------
    // Fields, ascending offset; retail size exactly 0x470.
    // +0x04..+0x29 is Behavior's; +0x2c is the RequestCB vptr.
    // ------------------------------------------------------------------

    MoveIdea Move;    // +0x30 (ctor FUN_062d4e60, 0x78 bytes)
    FaceIdea Face;    // +0xa8 (ctor FUN_062d6750, 0xc0 bytes)
    // +0x168, ends +0x2dc (ctor FUN_062d9bf0, vtable .rdata:063a0568, 0x174 bytes). Its
    // Move/Face watchers at +0x190 / +0x208 are two of the dtor's six watcher clears.
    SplineMoveIdea SplineMove;

    // +0x2dc (ends +0x2f4): an AfterburnerIdea by value (ctor FUN_062da500) followed by the
    // manager's Goto behavior pointer. EnterState (.text:062c3d6a) and Disengage
    // (062c3ab6) call Commit with this+0x2dc as the receiver; SetContext (062c3229) calls
    // SetContext.
    struct GotoDirectiveHandle
    {
        AfterburnerIdea Idea;               // +0x00 (absolute +0x2dc)
        Behavior* NavPilotPtr;              // +0x14 (absolute +0x2f0), the manager's Goto behavior
    };
    GotoDirectiveHandle GotoDirective; // +0x2dc

    pub::AI::DirectiveDockOp DockOp;   // +0x2f4. Real, already-ported type (common_pub.h). Ends at +0x320.

    int ModeState320;          // +0x320. Zeroed by ctor; the EnterState() state.
    BaseWatcher Watcher324;    // +0x324 (ends +0x32c). Ctor zeroes both dwords; the dtor
                                // (FUN_062c30d0) clears it via set_pointer(nullptr).
                                // GetWatchedRawBlock() reads its Watched pointer.
    unsigned StateFlags32c;    // +0x32c. Zeroed by ctor. OnResult() tests it against mask 0x840.
    int RequestedDockPortIndex;               // +0x330. Set to -1 (sentinel) by RecomputeApproach()/Disengage().
    unsigned char ApproachClearedFlag334; // +0x334. Cleared by RecomputeApproach()/Disengage().
    unsigned char Pad335[0x338 - 0x335];
    float StandOffRadius;              // +0x338. EnterState(2) (.text:062c4148, FSTP dword): the
                                  // watched object's FUN_062e6010 attribute times 0.5f
                                  // (StateFlags32c bit 0x80) or 1/3 (bit 0x40). Retail's ctor
                                  // (062c2e60) leaves it unwritten, and so does the port.
    float BrakingDistance;              // +0x33c. Written by RecomputeApproach() from
                                  // FUN_062e5af0/FUN_062e5ab0.
    // +0x340: the cooldown-state elapsed seconds UpdateApproachTail()'s case 0xb accumulates
    // (.text:062c5019 FADD / 062c5049 FCOMP) and EnterState()'s case 0xb clears.
    float StateTimer340;
    unsigned char CombatFlag344; // +0x344. Cleared by RecomputeApproach().
    unsigned char Pad345[0x348 - 0x345];
    ICRSplineSegment Spline;   // +0x348, ends at +0x388.

    // +0x388 (4 bytes, a pointer to a polymorphic object): out-param 1 of
    // OwnerContext->vtbl[0x138] in RecomputeApproach (out-params +0x388 / +0x3cc /
    // +0x38c); UpdateApproachTail null-tests it (.text:062c4a2c) and calls its vtbl+0x20
    // (062c4c8b). Pointee class not identified.
    void* LaneNextRing;
    Vector Facing38c;          // +0x38c (ends +0x398). Out-param 3 of the same call; input to
                                // LookMatrixYup for OrientMatrix3d8.
    unsigned char AbortFlag398; // +0x398
    unsigned char Flag399;      // +0x399. Cleared by RecomputeApproach()/Disengage(); set to 1 by
                                 // OnWatchedObjectDestroyed()'s type-4 match branch.
    unsigned char Flag39a;      // +0x39a. Read/cleared by RecomputeApproach()/Disengage().
    unsigned char Flag39b;     // +0x39b. Zeroed by ctor.
    // +0x39c (ends +0x3a8): a computed lead/intercept point. EnterState()'s cases 2 and 6
    // pass its address to the MoveIdea/FaceIdea setters (.text:062c416a / 062c4244 /
    // 062c4028).
    Vector InterceptPoint39c;
    // +0x3a8 (ends +0x3cc): RefreshApproachState copies the third dock-hardpoint rotation
    // here (.text:062c51d4); EnterState case 6 passes it to FaceIdea::SetBasis
    // (FUN_062d75c0).
    Matrix Orient3a8;
    // +0x3cc (ends +0x3d8): the destination point -- EnterState case 1 copies it into
    // DirectiveGotoOp::Destination, cases 6/7 feed it to the Move/Face setters, and
    // .text:062c4c6f copies it into SavedPosition3fc.
    Vector DestPoint3cc;
    Matrix OrientMatrix3d8;    // +0x3d8 (ends +0x3fc). Built by RecomputeApproach() via
                                // LookMatrixYup and copied in (9-dword MOVSD.REP).
    // +0x3fc (ends +0x408): the copy of DestPoint3cc; case 9 builds its retreat point from it
    // (.text:062c4ebb).
    Vector SavedPosition3fc;
    float DockPointRadius;            // +0x408. Set to 16.0f by OnResult() under one branch (retail:
                                // `MOV dword ptr [outer+0x408],0x41800000`). Also the float*
                                // out-param of get_dock_hardpoints in RefreshApproachState.
    // +0x40c (ends +0x430). A Matrix: RefreshApproachState copies the first dock-hardpoint
    // transform's rotation here (9-dword MOVSD at .text:062c5175-062c5184) and reads its
    // column z (+0x414/+0x420/+0x42c).
    Matrix Orient40c;
    // +0x430: the own ship's "hpmount" hardpoint relative to its centre, in its local frame
    // (SetContext, .text:062c31c0..); zeroed when there is no such hardpoint. EnterState hands
    // it to MoveIdea's Offset4c.
    Vector HpMountOffset;
    int DockPortIndex;              // +0x43c. Set by OnResult() to its own 3rd int parameter; also
                                // reset to -1 by RecomputeApproach().
    int TerminalType;       // +0x440. Cleared by RecomputeApproach()/Disengage().
    // +0x444: a Vector offset from DestPoint3cc. SetContext zeroes its three dwords;
    // BuildFinalApproachSpline (.text:062c57a4) adds it to DestPoint3cc for the spline's P2.
    Vector DestPointOffset;

    // FUN_062c50c0 (RET 4): DestPointOffset = Offset. Sole caller BaseFollowerBehavior
    // (.text:062cbc3e) on its DockDelegate with its GetRotatedOffset result.
    void SetDestPointOffset(Vector const& Offset);

    // +0x450 (ends +0x46c): the proximity watcher FUN_062c2af0/FUN_062c2d90 operate on
    // (ECX = this+0x450). A BaseWatcher (ctor zeroes it, dtor clears it) followed by two
    // distances, a speed ratio and two flag dwords; OnApplyDirective rewrites the tail.
    struct DockProximityWatcher
    {
        BaseWatcher Watcher;   // +0x00 (absolute +0x450)
        float NearDistance;       // +0x08 (absolute +0x458). Ctor zeroes as a raw dword.
        float FarDistance;       // +0x0c (absolute +0x45c). Ctor zeroes as a raw dword.
        float CruiseSpeedRatio;       // +0x10 (absolute +0x460). OnApplyDirective (FUN_062c3370) writes it:
                                // the op's +0x28 when > 0, else own get_speed(1, 4) over the
                                // docking target's get_speed(1, 1), clamped up to 1.0f
                                // (FCOMP float at 062c3547). Not ctor-zeroed.
        unsigned Flags14;      // +0x14 (absolute +0x464). Flags accumulator.
        unsigned SavedFlags18; // +0x18 (absolute +0x468). Previous-flags snapshot.
    };
    DockProximityWatcher ProxWatcher450; // +0x450, ends +0x46c

    // +0x46c (ends +0x470): the approach speed cap. EnterState() sets it on entry to
    // DAT_0639f47c (1000.0f, "no limit", .text:062c3d76), then 10.0f (case 6, 062c4213) or
    // DAT_063ebdb8 (18.0f, 062c4270 / 062c4333 / 062c4486). AdvancePreamble (062c47fc)
    // min-clamps it against FormationFollowerEnvelope::Speed and ProxWatcherComputeSpeedCap
    // and feeds it to MoveIdea/SplineMoveIdea::SetThrottleScale.
    float MaxApproachSpeed46c;
};

static_assert(offsetof(DockBehavior, Move) == 0x30, "DockBehavior::Move offset");
static_assert(offsetof(DockBehavior, Face) == 0xa8, "DockBehavior::Face offset");
static_assert(offsetof(DockBehavior, SplineMove) == 0x168, "DockBehavior::SplineMove offset");
static_assert(offsetof(DockBehavior, GotoDirective) == 0x2dc, "DockBehavior::GotoDirective offset");
static_assert(sizeof(AfterburnerIdea) == 0x14, "GotoDirectiveHandle::Idea is an AfterburnerIdea (0x14 bytes, ends at NavPilotPtr)");
static_assert(offsetof(DockBehavior::GotoDirectiveHandle, NavPilotPtr) == 0x14, "GotoDirectiveHandle::NavPilotPtr offset (absolute +0x2f0)");
static_assert(sizeof(DockBehavior::GotoDirectiveHandle) == 0x18, "GotoDirectiveHandle size");
static_assert(offsetof(DockBehavior, DockOp) == 0x2f4, "DockBehavior::DockOp offset");
static_assert(offsetof(DockBehavior, ModeState320) == 0x320, "DockBehavior::ModeState320 offset");
static_assert(offsetof(DockBehavior, Watcher324) == 0x324, "DockBehavior::Watcher324 offset");
static_assert(offsetof(DockBehavior, StateFlags32c) == 0x32c, "DockBehavior::StateFlags32c offset");
static_assert(offsetof(DockBehavior, RequestedDockPortIndex) == 0x330, "DockBehavior::RequestedDockPortIndex offset");
static_assert(offsetof(DockBehavior, ApproachClearedFlag334) == 0x334, "DockBehavior::ApproachClearedFlag334 offset");
static_assert(offsetof(DockBehavior, BrakingDistance) == 0x33c, "DockBehavior::BrakingDistance offset");
static_assert(offsetof(DockBehavior, StateTimer340) == 0x340, "DockBehavior::StateTimer340 offset");
static_assert(offsetof(DockBehavior, CombatFlag344) == 0x344, "DockBehavior::CombatFlag344 offset");
static_assert(offsetof(DockBehavior, Spline) == 0x348, "DockBehavior::Spline offset");
static_assert(offsetof(DockBehavior, LaneNextRing) == 0x388, "DockBehavior::LaneNextRing offset");
static_assert(offsetof(DockBehavior, Facing38c) == 0x38c, "DockBehavior::Facing38c offset");
static_assert(offsetof(DockBehavior, AbortFlag398) == 0x398, "DockBehavior::AbortFlag398 offset");
static_assert(offsetof(DockBehavior, Flag399) == 0x399, "DockBehavior::Flag399 offset");
static_assert(offsetof(DockBehavior, Flag39a) == 0x39a, "DockBehavior::Flag39a offset");
static_assert(offsetof(DockBehavior, Flag39b) == 0x39b, "DockBehavior::Flag39b offset");
static_assert(offsetof(DockBehavior, InterceptPoint39c) == 0x39c, "DockBehavior::InterceptPoint39c offset");
static_assert(offsetof(DockBehavior, DestPoint3cc) == 0x3cc, "DockBehavior::DestPoint3cc offset");
static_assert(offsetof(DockBehavior, OrientMatrix3d8) == 0x3d8, "DockBehavior::OrientMatrix3d8 offset");
static_assert(offsetof(DockBehavior, SavedPosition3fc) == 0x3fc, "DockBehavior::SavedPosition3fc offset");
static_assert(offsetof(DockBehavior, DockPointRadius) == 0x408, "DockBehavior::DockPointRadius offset");
static_assert(offsetof(DockBehavior, Orient3a8) == 0x3a8, "DockBehavior::Orient3a8 offset (.text:062c51d4)");
static_assert(offsetof(DockBehavior, Orient40c) == 0x40c, "DockBehavior::Orient40c offset (.text:062c5175)");
static_assert(offsetof(DockBehavior, HpMountOffset) == 0x430, "DockBehavior::HpMountOffset offset");
static_assert(offsetof(DockBehavior, DockPortIndex) == 0x43c, "DockBehavior::DockPortIndex offset");
static_assert(offsetof(DockBehavior, TerminalType) == 0x440, "DockBehavior::TerminalType offset");
static_assert(offsetof(DockBehavior, ProxWatcher450) == 0x450, "DockBehavior::ProxWatcher450 offset");
static_assert(sizeof(DockBehavior::DockProximityWatcher) == 0x1c, "DockProximityWatcher size (0x450..0x46c)");
static_assert(offsetof(DockBehavior, MaxApproachSpeed46c) == 0x46c, "DockBehavior::MaxApproachSpeed46c offset");
static_assert(sizeof(DockBehavior) == 0x470, "DockBehavior size == retail operator_new argument at .text:062d1a07");
