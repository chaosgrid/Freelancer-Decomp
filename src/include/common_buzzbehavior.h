// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"
#include "common_behavior.h"  // Behavior
#include "common_idea.h"      // MoveIdea, FaceIdea, AfterburnerIdea, DodgeIdea, BreakAwayIdea
#include "common_iface.h"     // IObjRW, IObjInspect, ObjInspectOf, ObjRwFromWatchable
#include "common_aisteer.h"   // FAiTarget + the three intercept probes Update() calls

#include <cstddef>

// ============================================================================
// BuzzBehavior -- AI "buzz" maneuver state (Behavior family, TractorArm named-
// mode dispatch table row "Buzz")
// ============================================================================
//
// RTTI: COL .rdata:063a8d10 -> ".?AVBuzzBehavior@@", numBaseClasses = 2 (self +
// Behavior) -- single inheritance like IdleBehavior, not BaseFollowerBehavior.
// Vtable .rdata:0639f564 (dtor thunk FUN_062c5c00 at slot 0), ctor FUN_062c5af0.
//
// +0x04..+0x29 is Behavior's (common_behavior.h); slot 3 opens with
// FUN_062d20d0 and SetContext calls the binder FUN_062d2090.
//
// Five embedded Ideas follow, sizes from the ctor's LEA gaps:
//   +0x2c  Move   size 0x78   ctor FUN_062d4e60  MoveIdea
//   +0xa4  Face   size 0xc0   ctor FUN_062d6750  FaceIdea
//   +0x164 Afterburner   size 0x14   ctor FUN_062da500  AfterburnerIdea
//   +0x178 Dodge   size 0xe0   ctor FUN_062d77d0  DodgeIdea (vtable 063a04c8)
//   +0x258 BreakAway   size 0x9c   ctor FUN_062d8970  BreakAwayIdea (vtable 063a04f8)
// Identities come from each ctor's vtable literal and RTTI (common_idea.h). FUN_062d5980 is
// called on Move by address but it is a MoveIdea, not a MatchThrottle2Idea.
//
// ----------------------------------------------------------------------------
// /OPT:ICF folds: several calls carry Ghidra names of unrelated classes; each is a
// one-line field setter whose effect depends on the receiver:
//
//   .text:062a33e0  `MOV [ECX+0x40],EAX; RET 4`  "CAsteroidCube::set_state"
//   .text:062a66b0  `MOV [ECX+0x3c],EAX; RET 4`  "DeviceTRC::bind_to_dc"
//   .text:063275f0  `MOV [ECX+0x38],EAX; RET 4`  "TextRenderContext::set_origin_h"
//   .text:063275d0  `MOV [ECX+0x34],EAX; RET 4`  "CAttachedEquip::SetFate"
//   .text:062d8920  `MOV [ECX+0xb4],EAX; RET 4`  "CSimple::SetOwnerPlayer"
//   .text:062d88b0  `MOV [ECX+0x44],EAX; RET 4`  FUN_062d88b0
//   .text:062d9490  `MOV [ECX+0x94],EAX; RET 4`  FUN_062d9490
//   .text:062d93c0  `MOV [ECX+0x8c],EAX; RET 4`  FUN_062d93c0
//
// On Move (MoveIdea) the first three and FUN_062d88b0 are SetScale40 / SetRange3c /
// SetAngle38 / SetField44; on Face (FaceIdea) FUN_062d9490 / FUN_062d93c0 are
// ScaleDefault1_94 / AngleWeight8c. On Dodge/BreakAway see the setter map below the
// private helpers. The port reproduces each effect on the correct receiver.
// ----------------------------------------------------------------------------
//
// SysMgr (+0x24) is the SystemManager (common_systemmanager.h): +0x14fc its own
// FAiTarget, +0x1364 the formation-leader block, +0x1584.. its pub::AI::Personality.
// ============================================================================

// BuzzBehavior::BuzzState (+0x318), the buzz stage.
enum EBuzzState
{
    kBuzzStateHeadAway = 0,
    kBuzzStateHeadToward = 1,
    kBuzzStatePassBy = 2,
};

// BuzzBehavior::HeadTowardStyle (+0x31c), drawn from the BuzzHeadTowardStyleWeight* keys.
enum EBuzzHeadTowardStyle
{
    kBuzzHeadStraightTo = 0,
    kBuzzHeadSlide = 1,
    kBuzzHeadWaggle = 2,
};

// BuzzBehavior::PassByStyle (+0x320), drawn from the BuzzPassByStyleWeight* keys.
enum EBuzzPassByStyle
{
    kBuzzPassBreakAway = 0,   // ticks the BreakAwayIdea
    kBuzzPassStraightBy = 1,
    kBuzzPassEngineKill = 2,  // ThrustManager::SetEngineActive(false)
};

class BuzzBehavior : public Behavior
{
public:
    // Retail's ctor argument is the behavior type id (1, "Buzz"), pushed by the factory out of
    // the descriptor's +0x04 -- see Behavior::TypeId in common_behavior.h.
    explicit BuzzBehavior(int InTypeId);
    ~BuzzBehavior();

    // ------------------------------------------------------------------
    // Vtable (.rdata:0x0639f564):
    //   0  (0x00) FUN_062c5c00   scalar-deleting-dtor thunk (real dtor FUN_062c5c40)
    //   1  (0x04) LAB_062c5cf0   SetContext()
    //   3  (0x0c) LAB_062c5dd0   RecomputeApproach()
    //   4  (0x10) FUN_062c60a0   Disengage()
    //   5  (0x14) FUN_062c60f0   EvaluateReadiness()
    //   7  (0x1c) LAB_062c62a0   Update()
    // ------------------------------------------------------------------

    // Slot 1, LAB_062c5cf0. The binder, then SetContext on all five Ideas, interleaved with
    // Move's configuration (SetField44(2) / SetAngle38(PI) / SetMode(0) /
    // SetRange3c(0.0f)).
    bool SetContext(IObjRW* Param1, IBehaviorManager* Param2, SystemManager* Param3);

    // Slot 3, LAB_062c5dd0. Resets Behavior's tick state (FUN_062d20d0), re-rolls
    // Move/B/C/D/E's per-tick tunables from SysMgr's large tunable
    // block, and re-derives HeadTowardStyle/PassByStyle/MinDistanceToHeadToward/FastPathRange via
    // RandomVector()/rand()-driven weighted picks.
    void RecomputeApproach();

    // Slot 4, FUN_062c60a0. Commit() on all five Ideas in order Face/A/C/D/E, then the
    // shared FUN_062d20f0 tail -- see IdleBehavior::Disengage() for why only its
    // unconditional Flag29 clear is reproduced.
    void Disengage();

    // Slot 5, FUN_062c60f0. While HeadAwayElapsed < MaxTimeToHeadAway, checks the SysMgr's
    // follower gate / target / type (FUN_062ecb20 / FUN_062ed5a0 / FUN_062e6200) to pick
    // mode 1 (ready) or 2; otherwise resets the timer. Flag29 forces mode 3.
    int EvaluateReadiness();

    // ------------------------------------------------------------------
    // Slot 7, LAB_062c62a0 (741 instructions, no Ghidra decompile). Points that are easy to
    // get wrong (all matched in the .cpp):
    //  (1) `HeadAwayElapsed += DeltaTime` (LAB_062c6705) is reached only by falling out of
    //      the first HeadTowardStyle switch; BuzzState != 0 (062c646f) and the 0 -> 1 commit
    //      (062c64fa/062c650b) jump past it to 062c672b.
    //  (2) The first switch's case-0 gate at 062c659b is `TEST AH,0x44; JP`, an equality
    //      test: `SysMgr[0x1638] != 0.0f` (the second switch's case 0 at 062c6b07
    //      agrees).
    //  (3) The "immediate direction" fast path is not an early return: both exits
    //      (062c6415/062c6425) reach the shared tail LAB_062c6cda, so the SysMgr timeout
    //      check still runs.
    //  (4) PassByStyle case 1's second vector is `{0, 0, PassByRollThrottle}` (FSTP at
    //      062c6bed into the third float).
    // No case body of either HeadTowardStyle switch writes a BuzzBehavior field; every
    // effect lands on the Ideas or on SysMgr.
    //
    // Both target records are FAiTargets: SysMgr+0x14fc and a stack record retail builds
    // at [ESP+0x2c] by inlining FUN_062d09b0. Their lazy refreshes are the inlined
    // FUN_062c47c0 (FAiTarget::RefreshPos, guard byte +0x7E). FUN_062ed5a0 is
    // TargetManager::GetTarget; FUN_062d9450 at .text:062c6caf is
    // BreakAwayIdea::SetWatchObject. The candidate-angle metric is AngleToBasisZColumn3
    // (FUN_062e1e20).
    int Update();

    // Slot 7: IBehaviorManager::UpdateBehaviorTick stores the tick delta to +0x1c and
    // dispatches through vtable +0x1c.
    int OnUpdate() override { return Update(); }

    // Slots 3/4/5 of the vftable (.rdata:0639f564). Slot 5 is `RET 4` (float timer unused).
    void OnEngage() override { RecomputeApproach(); }             // 3 +0x0c LAB_062c5dd0
    void OnDisengage() override { Disengage(); }                     // 4 +0x10 FUN_062c60a0
    int OnEvaluateReadiness(float) override { return EvaluateReadiness(); }  // 5 +0x14 FUN_062c60f0

private:
    // FUN_062d75c0 and FUN_062d93c0 are plain FaceIdea field writes (a 9-dword basis copy
    // into ZeroBlock34 with FieldA0 = 4, and AngleWeight8c = Value), done at the call sites.

    // LAB_062c66cb/LAB_062c66f2 -- retail's own shared "commit a fate to
    // Dodge" epilogue, reached from five call sites across both HeadTowardStyle
    // switches. Writes +0xb4/+0x34/+0x38 then ticks Dodge. Interval / IntervalVariance
    // are the personality's BuzzDodgeIntervalTime pair (+0x1664) or BuzzSlideIntervalTime
    // pair (+0x1670).
    void ApplySteeringFate(int Fate, float Interval, float IntervalVariance);

    // FUN_062c6170, called
    // twice from Update() (062c682d, 062c6b49). Points Face (face) and Move
    // (move) at the current target (TargetManager::GetTarget), ticks Face, ticks or
    // commits Afterburner by IsAlignedAndOutsideHeadingCone, and steers Move along the
    // own ship's -z axis. Writes no BuzzBehavior-own field.
    void AcquireLockEffects();

    // The three SysMgr+0x34 byte probes: the timer table's IsTimer0/5/4Active
    // (FUN_062e8810 / 062e8840 / 062e8850, each `MOV AL,byte ptr [ECX+disp]; RET`).
    static bool CheckNavHelperTimeout(SystemManager* SysMgr);    // FUN_062e8810(SysMgr+0x34)
    static bool CheckNavHelperFlagA(SystemManager* SysMgr);      // FUN_062e8840(SysMgr+0x34)
    static bool CheckNavHelperFlagB(SystemManager* SysMgr);      // FUN_062e8850(SysMgr+0x34)

    // The two "fetch a 3x3 basis" vcalls -- slot 3, get_transform(), whose
    // Transform begins with its 3x3 Matrix. Retail reads the result at +0x8,
    // +0x14 and +0x20 (column 2) and also block-copies all nine floats.
    static Matrix const& FetchHandleBasis(IObjInspect const* Handle);           // Record.Iface->vtbl[0xc]()
    static Matrix const& FetchNavCandidateBasis(SystemManager* SysMgr);      // SysMgr->SelfTarget.Iface->vtbl[0xc]()

    // ---- The fourteen retail setter addresses on Dodge/BreakAway, each a bare store
    // ---- at a fixed offset, written as plain assignments at the call sites:
    // ----
    // ----   .text:062a66b0  [+0x3c]  -> DodgeIdea::ConeAngle
    // ----   .text:062a33e0  [+0x40]  -> DodgeIdea::ConeAngleVariancePercent
    // ----   FUN_062d88b0    [+0x44]  -> DodgeIdea::WaggleAxisConeAngle
    // ----   .text:062a33c0  [+0x48]  -> DodgeIdea::RollAngle
    // ----   FUN_062d8910    [+0x90]  -> DodgeIdea::TurnThrottle
    // ----   FUN_062d8950    [+0xd4]  -> DodgeIdea::VectorCc.z   (0xcc + 8)
    // ----   FUN_062d8960    [+0xd8]  -> DodgeIdea::bRollFlipDirection (the byte)
    // ----   FUN_062d8930    [+0xc8]  -> DodgeIdea::SlideThrottle
    // ----   .text:062d8920  [+0xb4]  -> DodgeIdea::DodgeStyle
    // ----   .text:063275d0  [+0x34]  -> DodgeIdea::IntervalTime / BreakAwayIdea::BreakStyle
    // ----   .text:063275f0  [+0x38]  -> DodgeIdea::IntervalTimeVariancePercent
    // ----   FUN_062d76b0    [+0x4/8/c/10 of the passed sub-object, which is
    // ----                    <Idea>+0x14] -> FDirectionPicker::WeightLeft/Right/Up/Down
    // ----                    on Dodge's and on BreakAway's own Picker.
    // ----   FUN_062d9490    [+0x94]  -> DodgeIdea::Throttle / BreakAwayIdea::TurnThrottle
    // ----   FUN_062d88c0    [+0x58]  -> DodgeIdea::SetField58 (Vector + Field64 = 1)

    // Candidate-relative angular metric, .text:062c68bf-062c68de and its two
    // twins: retail's `FUN_062e1e20(Basis, &NavRecord.CachedPos, &CandidatePos)`
    // is common_ini.h's AngleToBasisZColumn3; Basis is NavRecord.Iface->
    // get_transform(), fetched after both pointer pushes. Returns the unrounded
    // ST0 value the three FCOMP sites compare.
    static double ReadCandidateMetric(SystemManager* SysMgr, Vector const& CandidatePos);

public:
    // ------------------------------------------------------------------
    // Fields, ascending offset. Retail size 0x354.
    // ------------------------------------------------------------------

    MoveIdea Move;  // +0x2c.  Ctor FUN_062d4e60 (DockBehavior & co.'s SubObjectA).
    FaceIdea Face;  // +0xa4.  Ctor FUN_062d6750 (their SubObjectB).
    AfterburnerIdea Afterburner;  // +0x164. Ctor FUN_062da500; sizeof 0x14 == BaseIdea.
    // +0x178, size 0xe0. Ctor FUN_062d77d0, vtable PTR_LAB_063a04c8 (COL 063a9af4).
    // Update LAB_062d7950 (common_dodgeidea.cpp); FDirectionPicker base at +0x14.
    DodgeIdea Dodge;

    // +0x258, size 0x9c. Ctor FUN_062d8970, vtable PTR_LAB_063a04f8 (COL 063a9b40).
    // The ctor's highest write is +0x98 (.text:062d89e0) and Field2f4 bounds it above.
    BreakAwayIdea BreakAway;

    // +0x2f4 (ends +0x300). RandomVector(1.0f) result, written by
    // RecomputeApproach(). No other reader found.
    Vector Field2f4;
    float Field300;             // +0x300. Set to 300.0f by SetContext().
    unsigned char Field304;     // +0x304. Set to 0 by both SetContext() and RecomputeApproach().
    unsigned char Pad305[0x308 - 0x305];
    float FastPathElapsed;              // +0x308. Ctor-uninitialized (first real write is RecomputeApproach()'s
                                  // reset to 0); per-tick accumulator compared against FastPathMaxDuration in Update().
    float FastPathMaxDuration;               // +0x30c. Set to 5.0f by RecomputeApproach(). Threshold for FastPathElapsed.
    float AlignedElapsed;                // +0x310. Reset to 0 by RecomputeApproach(); accumulator compared
                                    // against AfterburnerDelay in Update().

    float AfterburnerDelay;             // +0x314. Ctor 1.5f. Threshold for AlignedElapsed.

    // Names below +0x314 come from the personality keys RecomputeApproach() (.text:062c5dd0)
    // copies in: SysMgr+0x1584 is the SystemManager's pub::AI::Personality, so +0x1618.. is
    // its [BuzzHeadTowardBlock] (BuzzHeadParams) and +0x1678.. its [BuzzPassByBlock]
    // (BuzzPassParams) -- the key names are Content's loader keys (common_pub.h).

    // +0x318. The buzz stage, 0/1/2. Ctor 0; RecomputeApproach() and the
    // pass-by timeout reset it to 0. Proof of the three meanings (Update(), .text:062c62a0):
    //   0 = head away: its timer (HeadAwayElapsed) runs against MaxTimeToHeadAway and it is left
    //       when IsWithinInterceptRange(MinDistanceToHeadToward) fails;
    //   1 = head toward: entered at .text:062c64cd, where the formation notifications are gated
    //       by bBreakApartFormationOnBuzzHeadToward / bRegroupFormationOnBuzzHeadToward;
    //   2 = pass by: entered at .text:062c679f under the ...OnBuzzPassBy flags and
    //       bBuzzDropBombOnPassBy; its timer (PassByElapsed) runs against PassByTime.
    int BuzzState;
    // +0x31c. Weighted pick (.text:062c5e50-062c5eb6) over the
    // BuzzHeadTowardStyleWeight* keys: 0 = StraightTo, 1 = Slide, 2 = Waggle. Case 1 uses the
    // BuzzSlideIntervalTime pair, cases 0/2 the BuzzDodgeIntervalTime pair.
    int HeadTowardStyle;
    // +0x320. Weighted pick (.text:062c5ebc-062c5f1b) over the
    // BuzzPassByStyleWeight* keys: 0 = BreakAway (ticks the BreakAwayIdea BreakAway),
    // 1 = StraightBy, 2 = EngineKill (ThrustManager::SetEngineActive(false)).
    int PassByStyle;
    // +0x324. Ctor 500.0f; RecomputeApproach(): BuzzMinDistanceToHeadToward
    // scaled by (1 + r * BuzzMinDistanceToHeadTowardVariancePercent), r in [-1, 1).
    float MinDistanceToHeadToward;
    float Field328;                  // +0x328. Ctor 1.5f. No reader in .text:062c5af0..062c6d30.
    float DistanceToPassBy;          // +0x32c. Ctor 100.0f; BuzzDistanceToPassBy.
    float Field330;                  // +0x330. Ctor 5.0f. No reader in .text:062c5af0..062c6d30.
    float PassByTime;                // +0x334. Ctor 3.0f; BuzzPassByTime.
    float PassByElapsed;             // +0x338. Ctor 0. DeltaTime sum in stage 2,
                                     // compared against PassByTime (.text:062c6b5d-062c6b6c).
    float MaxTimeToHeadAway;         // +0x33c. Ctor 6.0f; BuzzMaxTimeToHeadAway.
    float HeadAwayElapsed;           // +0x340. Ctor 0. DeltaTime sum in stage 0,
                                     // compared against MaxTimeToHeadAway (.text:062c670d,
                                     // EvaluateReadiness .text:062c60f3).
    float FastPathRange;                  // +0x344. Set to 300.0f by RecomputeApproach(). The fast path runs
                                     // only while GetInterceptClearance(...) < FastPathRange (.text:062c6340).
    float PassByRollThrottle;        // +0x348. Ctor 0; BuzzPassByRollThrottle (the
                                     // z of FaceIdea::SetAxis in PassByStyle 1).
    float HeadTowardEngineThrottle;  // +0x34c. Ctor 1.0f; BuzzHeadTowardEngineThrottle
                                     // (MoveIdea::SetScale40 in AcquireLockEffects()).
    float HeadTowardTurnThrottle;    // +0x350. Ctor 1.0f; BuzzHeadTowardTurnThrottle
                                     // (FaceIdea::SetScale94 in AcquireLockEffects()).
};

static_assert(offsetof(BuzzBehavior, Move) == 0x2c, "BuzzBehavior::Move offset");
static_assert(offsetof(BuzzBehavior, Face) == 0xa4, "BuzzBehavior::Face offset");
static_assert(offsetof(BuzzBehavior, Afterburner) == 0x164, "BuzzBehavior::Afterburner offset");
static_assert(offsetof(BuzzBehavior, Dodge) == 0x178, "BuzzBehavior::Dodge offset");
static_assert(offsetof(BuzzBehavior, BreakAway) == 0x258, "BuzzBehavior::BreakAway offset");
static_assert(offsetof(BuzzBehavior, Field2f4) == 0x2f4, "BuzzBehavior::Field2f4 offset");
static_assert(offsetof(BuzzBehavior, Field300) == 0x300, "BuzzBehavior::Field300 offset");
static_assert(offsetof(BuzzBehavior, Field304) == 0x304, "BuzzBehavior::Field304 offset");
static_assert(offsetof(BuzzBehavior, FastPathElapsed) == 0x308, "BuzzBehavior::FastPathElapsed offset");
static_assert(offsetof(BuzzBehavior, FastPathMaxDuration) == 0x30c, "BuzzBehavior::FastPathMaxDuration offset");
static_assert(offsetof(BuzzBehavior, AlignedElapsed) == 0x310, "BuzzBehavior::AlignedElapsed offset");
static_assert(offsetof(BuzzBehavior, AfterburnerDelay) == 0x314, "BuzzBehavior::AfterburnerDelay offset");
static_assert(offsetof(BuzzBehavior, BuzzState) == 0x318, "BuzzBehavior::BuzzState offset");
static_assert(offsetof(BuzzBehavior, HeadTowardStyle) == 0x31c, "BuzzBehavior::HeadTowardStyle offset");
static_assert(offsetof(BuzzBehavior, PassByStyle) == 0x320, "BuzzBehavior::PassByStyle offset");
static_assert(offsetof(BuzzBehavior, MinDistanceToHeadToward) == 0x324, "BuzzBehavior::MinDistanceToHeadToward offset");
static_assert(offsetof(BuzzBehavior, Field328) == 0x328, "BuzzBehavior::Field328 offset");
static_assert(offsetof(BuzzBehavior, DistanceToPassBy) == 0x32c, "BuzzBehavior::DistanceToPassBy offset");
static_assert(offsetof(BuzzBehavior, Field330) == 0x330, "BuzzBehavior::Field330 offset");
static_assert(offsetof(BuzzBehavior, PassByTime) == 0x334, "BuzzBehavior::PassByTime offset");
static_assert(offsetof(BuzzBehavior, PassByElapsed) == 0x338, "BuzzBehavior::PassByElapsed offset");
static_assert(offsetof(BuzzBehavior, MaxTimeToHeadAway) == 0x33c, "BuzzBehavior::MaxTimeToHeadAway offset");
static_assert(offsetof(BuzzBehavior, HeadAwayElapsed) == 0x340, "BuzzBehavior::HeadAwayElapsed offset");
static_assert(offsetof(BuzzBehavior, FastPathRange) == 0x344, "BuzzBehavior::FastPathRange offset");
static_assert(offsetof(BuzzBehavior, PassByRollThrottle) == 0x348, "BuzzBehavior::PassByRollThrottle offset");
static_assert(offsetof(BuzzBehavior, HeadTowardEngineThrottle) == 0x34c, "BuzzBehavior::HeadTowardEngineThrottle offset");
static_assert(offsetof(BuzzBehavior, HeadTowardTurnThrottle) == 0x350, "BuzzBehavior::HeadTowardTurnThrottle offset");
static_assert(sizeof(BuzzBehavior) >= 0x354, "BuzzBehavior minimum size (largest field actually read/written)");

// Retail object size from the factory's `PUSH 0x354` before operator_new. The upper bound
// catches fields declared past the end of the retail object.
static_assert(sizeof(BuzzBehavior) <= 0x354, "BuzzBehavior must not exceed its retail size 0x354");
