// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_buzzbehavior.h"
#include "common_misc.h"  // RandomVector
#include "common_aisteer.h"  // FAiTarget + IsTargetCoAlignedAndAhead / GetInterceptClearance /
                              // IsWithinInterceptRange -- Update()'s three intercept probes.
#include "common_csys.h"   // Matrix / Transform -- FetchNavCandidateBasis's real result type
#include "common_iface.h"  // IObjInspect -- what SysMgr[+0x14fc][+0x84] actually is

#include <cstdlib>  // rand()

// BuzzBehavior; layout and RTTI notes are in common_buzzbehavior.h. Update() has no Ghidra
// decompilation and is read off the raw listing.

// ----------------------------------------------------------------------------
// FUN_062c5af0. FUN_062d2020 is Behavior's ctor; the five Ideas are constructed in
// declaration order (FUN_062d4e60 / 062d6750 / 062da500 / 062d77d0 / 062d8970).
// ----------------------------------------------------------------------------
BuzzBehavior::BuzzBehavior(int InTypeId)
    : Behavior(InTypeId)
    , Move()
    , Face()
    , Afterburner()
    , Dodge()
    , BreakAway()
    , Field2f4{}
    , Field300(0.0f)
    , Field304(0)
    , Pad305{}
    , FastPathElapsed(0.0f)
    , FastPathMaxDuration(0.0f)
    , AlignedElapsed(0.0f)
    , AfterburnerDelay(1.5f)
    , BuzzState(kBuzzStateHeadAway)
    , HeadTowardStyle(kBuzzHeadStraightTo)
    , PassByStyle(kBuzzPassBreakAway)
    , MinDistanceToHeadToward(500.0f)
    , Field328(1.5f)
    , DistanceToPassBy(100.0f)
    , Field330(5.0f)
    , PassByTime(3.0f)
    , PassByElapsed(0.0f)
    , MaxTimeToHeadAway(6.0f)
    , HeadAwayElapsed(0.0f)
    , FastPathRange(0.0f)
    , PassByRollThrottle(0.0f)
    , HeadTowardEngineThrottle(1.0f)
    , HeadTowardTurnThrottle(1.0f)
{
    // Retail's explicit zeroing of the state fields is covered by the initializer list;
    // SetContext()/RecomputeApproach() re-zero most of them at runtime anyway.
}

// FUN_062c5c40 (FUN_062c5c00 is the scalar deleting destructor).
BuzzBehavior::~BuzzBehavior()
{
    // Retail clears the Ideas' watchers in this order: +0x280 (BreakAway picker), +0x1a0
    // (Dodge picker), +0xb8 (Face), +0x40 (Move). Afterburner (AfterburnerIdea) has
    // no watcher.
    BreakAway.Picker.Watcher.set(nullptr);
    Dodge.Picker.Watcher.set(nullptr);
    Face.Watcher.set(nullptr);
    Move.Watcher.set(nullptr);

    // FUN_062d2080 (~Behavior) runs as the base destructor.
}

// ----------------------------------------------------------------------------
// Slot 1, LAB_062c5cf0.
// ----------------------------------------------------------------------------
bool BuzzBehavior::SetContext(IObjRW* Param1, IBehaviorManager* Param2, SystemManager* Param3)
{
    if (!BindContext(Param1, Param2, Param3))   // FUN_062d2090
        return false;

    Field300 = 300.0f;
    Field304 = 0;

    // MoveIdea::SetContext (LAB_062d4f20).
    Move.SetContext(OwnerContext, SysMgr, Param2);

    // Four ICF-folded one-store setters on Move; retail calls them by borrowed names:
    // FUN_062d88b0 (+0x44), TextRenderContext::set_origin_h (.text:063275f0, +0x38),
    // FUN_062d5980 (MoveIdea::SetMode), DeviceTRC::bind_to_dc (.text:062a66b0, +0x3c).
    Move.SetField44(kMoveStopFlyThrough);

    Move.SetAngle38(3.14159274f);  // 0x40490fdb, MoveIdea's own ctor default

    Move.SetMode(kMoveModeEngine);

    Move.SetRange3c(0.0f);   // raw dword 0

    // FaceIdea slot 0 (LAB_062d6820).
    Face.SetContext(OwnerContext, SysMgr, Param2);

    // AfterburnerIdea, DodgeIdea and BreakAwayIdea inherit BaseIdea's slot 0 (LAB_062d7920).
    Afterburner.SetContext(OwnerContext, SysMgr, Param2);

    Dodge.SetContext(OwnerContext, SysMgr, Param2);
    BreakAway.SetContext(OwnerContext, SysMgr, Param2);

    return true;
}

// ----------------------------------------------------------------------------
// Slot 3 (OnEngage), LAB_062c5dd0. Restarts the state machine at "head away" (state 0) and
// rerolls the approach/pass-by styles from the Personality.
// ----------------------------------------------------------------------------
void BuzzBehavior::RecomputeApproach()
{
    Behavior::OnEngage();   // FUN_062d20d0

    SetAvoidanceAllowed(1, kAvoidanceObstacleNudge | kAvoidancePoint);   // FUN_062d21d0

    FastPathRange = 300.0f;
    FastPathElapsed = 0.0f;
    Field304 = 0;
    Field2f4 = RandomVector(1.0f);

    FastPathMaxDuration = 5.0f;
    AlignedElapsed = 0.0f;
    BuzzState = kBuzzStateHeadAway;
    PassByElapsed = 0.0f;
    HeadAwayElapsed = 0.0f;

    // SysMgr+0x1584 is the SystemManager's pub::AI::Personality: +0x1618..+0x1677 is its
    // BuzzHeadParams (Personality+0x94), +0x1678..+0x16ab its BuzzPassParams (+0xF4).
    pub::AI::Personality::BuzzHeadTowardUseStruct const& Head = SysMgr->PersonalityData.BuzzHeadParams;
    pub::AI::Personality::BuzzPassByUseStruct const& Pass = SysMgr->PersonalityData.BuzzPassParams;

    // Weighted pick of HeadTowardStyle. The weight sum is rounded to a float local; the
    // pick R = rand() * Sum * (1/32768) (DAT_063997d0) stays on the x87 stack. Both tests
    // are strict less-than (unordered fails): R < Slide -> 1, R < Waggle + Slide -> 2, else 0.
    {
        float const Sum = (float)((double)Head.BuzzHeadTowardStyleWeightSlide +
                                  Head.BuzzHeadTowardStyleWeightWaggle +
                                  Head.BuzzHeadTowardStyleWeightStraightTo);
        double const Pick = (double)std::rand() * Sum * 3.0517578125e-05;
        if (Pick < Head.BuzzHeadTowardStyleWeightSlide)
            HeadTowardStyle = kBuzzHeadSlide;
        else if (Pick < (double)Head.BuzzHeadTowardStyleWeightWaggle + Head.BuzzHeadTowardStyleWeightSlide)
            HeadTowardStyle = kBuzzHeadWaggle;
        else
            HeadTowardStyle = kBuzzHeadStraightTo;
    }

    // Same shape for PassByStyle over StraightBy / EngineKill / BreakAway.
    {
        float const Sum = (float)((double)Pass.BuzzPassByStyleWeightStraightBy +
                                  Pass.BuzzPassByStyleWeightEngineKill +
                                  Pass.BuzzPassByStyleWeightBreakAway);
        double const Pick = (double)std::rand() * Sum * 3.0517578125e-05;
        if (Pick < Pass.BuzzPassByStyleWeightStraightBy)
            PassByStyle = kBuzzPassStraightBy;
        else if (Pick < (double)Pass.BuzzPassByStyleWeightEngineKill + Pass.BuzzPassByStyleWeightStraightBy)
            PassByStyle = kBuzzPassEngineKill;
        else
            PassByStyle = kBuzzPassBreakAway;
    }

    // Variance factor (2R - 1) * pct + 1 computed on the x87 stack, one float store.
    MinDistanceToHeadToward = Head.BuzzMinDistanceToHeadToward;
    {
        double const Rand = (double)std::rand() * 3.0517578125e-05;
        MinDistanceToHeadToward = (float)(((Rand + Rand - 1.0) * Head.BuzzMinDistanceToHeadTowardVariancePercent + 1.0) *
                           MinDistanceToHeadToward);
    }

    MaxTimeToHeadAway = Head.BuzzMaxTimeToHeadAway;
    DistanceToPassBy = Pass.BuzzDistanceToPassBy;
    PassByTime = Pass.BuzzPassByTime;
    HeadTowardEngineThrottle = Head.BuzzHeadTowardEngineThrottle;
    HeadTowardTurnThrottle = Head.BuzzHeadTowardTurnThrottle;

    // Retail configures Dodge/BreakAway through direct calls to ICF-folded one-store setters
    // (some carry borrowed names such as DeviceTRC::bind_to_dc, CAsteroidCube::set_state,
    // CAsteroidField::set_fill_dist); each is a plain store at the offset noted. Sources are
    // copied as raw dwords, never through the x87 stack.
    Dodge.Throttle = Head.BuzzHeadTowardEngineThrottle;               // [0x1630] FUN_062d9490, +0x94
    Dodge.ConeAngle = Head.BuzzDodgeConeAngle;                        // [0x1654] .text:062a66b0, +0x3c
    Dodge.ConeAngleVariancePercent = Head.BuzzDodgeConeAngleVariancePercent;  // [0x1658] .text:062a33e0, +0x40
    Dodge.WaggleAxisConeAngle = Head.BuzzDodgeWaggleAxisConeAngle;    // [0x165c] FUN_062d88b0,  +0x44
    Dodge.RollAngle = Head.BuzzDodgeRollAngle;                        // [0x1660] .text:062a33c0, +0x48
    Dodge.TurnThrottle = Head.BuzzDodgeTurnThrottle;                  // [0x1650] FUN_062d8910, +0x90
    // FUN_062d76b0 on <Idea>+0x14 writes FDirectionPicker::WeightLeft/Right/Up/Down.
    Dodge.Picker.WeightLeft = Head.BuzzDodgeDirectionWeightLeft;     // [0x1640]
    Dodge.Picker.WeightRight = Head.BuzzDodgeDirectionWeightRight;   // [0x1644]
    Dodge.Picker.WeightUp = Head.BuzzDodgeDirectionWeightUp;         // [0x1648]
    Dodge.Picker.WeightDown = Head.BuzzDodgeDirectionWeightDown;     // [0x164c]
    // FUN_062d8950 writes +0xd4, which is VectorCc.z, not a field of its own.
    Dodge.VectorCc.z = Head.BuzzHeadTowardRollThrottle;              // [0x1638] FUN_062d8950, +0xd4
    // The only byte-sized copy in the chain.
    Dodge.bRollFlipDirection = Head.bBuzzHeadTowardRollFlipDirection; // [0x163c] FUN_062d8960, +0xd8
    Dodge.SlideThrottle = Head.BuzzSlideThrottle;                    // [0x166c] FUN_062d8930, +0xc8
    // The second FUN_062d76b0, on BreakAway's picker.
    BreakAway.Picker.WeightLeft = Pass.BuzzBreakDirectionWeightLeft;     // [0x1690]
    BreakAway.Picker.WeightRight = Pass.BuzzBreakDirectionWeightRight;   // [0x1694]
    BreakAway.Picker.WeightUp = Pass.BuzzBreakDirectionWeightUp;         // [0x1698]
    BreakAway.Picker.WeightDown = Pass.BuzzBreakDirectionWeightDown;     // [0x169c]

    PassByRollThrottle = Pass.BuzzPassByRollThrottle;                             // [0x16a8]
}

// ----------------------------------------------------------------------------
// Slot 4, FUN_062c60a0.
// ----------------------------------------------------------------------------
void BuzzBehavior::Disengage()
{
    // Retail's order: Face, A, C, D, E.
    Face.Commit();
    Move.Commit();
    Afterburner.Commit();
    Dodge.Commit();
    BreakAway.Commit();

    Behavior::OnDisengage();   // FUN_062d20f0
}

// ----------------------------------------------------------------------------
// Slot 5, FUN_062c60f0.
// ----------------------------------------------------------------------------
int BuzzBehavior::EvaluateReadiness()
{
    // Retail's JNZ is taken on less-or-unordered, so the reset runs only on a true >=.
    int Mode = 2;
    if (HeadAwayElapsed >= MaxTimeToHeadAway)
    {
        HeadAwayElapsed = 0.0f;
    }
    else
    {
        // FUN_062ecb20 (gate A), then TargetManager::GetTarget (FUN_062ed5a0) and
        // GetObjectTypeOrZero (FUN_062e6200).
        if (!SysMgr->FormationLeaders.FollowerDirectiveGateA())
        {
            IObjRW* const Target = SysMgr->Targeting.GetTarget();
            if (Target != nullptr && (GetObjectTypeOrZero(Target) & (OBJ_FIGHTER | OBJ_FREIGHTER)) != 0)
                Mode = 1;
        }
    }

    if (Flag29 != 0)
    {
        Readiness = 3;
        return 3;
    }
    Readiness = Mode;
    return Mode;
}

// ----------------------------------------------------------------------------
// Internal helper, FUN_062c6170; called twice from Update().
// ----------------------------------------------------------------------------
void BuzzBehavior::AcquireLockEffects()
{
    SystemManager* const Sys = SysMgr;

    // FUN_062ed5a0; retail uses the result unchecked.
    IObjRW* const Target = Sys->Targeting.GetTarget();

    Face.PublishTargetId(Target->get_id());   // FUN_062d4e50
    Face.SetScale94(HeadTowardTurnThrottle);   // FUN_062d9490
    Face.SetWatchObject(Target);   // FUN_062d7510

    // FUN_062e8840 gates FUN_062d7640.
    if (CheckNavHelperFlagA(SysMgr))
    {
        Vector const WorldUp{ 0.0f, 0.0f, 1.0f };
        Face.SetUpVector(WorldUp);
    }

    Face.Update(DeltaTime);

    // FUN_062e5190. MoveIdea::SetScale40 is the .text:062a33e0 fold.
    float Scale40;
    if (IsAlignedAndOutsideHeadingCone(&Sys->SelfTarget, Target, 0.0f))
    {
        Afterburner.Update(DeltaTime);
        Scale40 = 1.0f;
    }
    else
    {
        Afterburner.Commit();
        Scale40 = HeadTowardEngineThrottle;
    }
    Move.SetScale40(Scale40);

    // get_id() is re-read, not reused.
    Move.PublishTargetId(Target->get_id());

    // Negated z column of the own ship's transform; FUN_062d5890 = MoveIdea::SetDirection.
    Matrix const& Rotation = Sys->SelfTarget.Iface->get_transform().Rotation;
    Vector Dir;
    Dir.x = -Rotation.E[0][2];
    Dir.y = -Rotation.E[1][2];
    Dir.z = -Rotation.E[2][2];
    Move.SetDirection(Dir);

    Move.Update(DeltaTime);
}

// ----------------------------------------------------------------------------
// LAB_062c66cb + LAB_062c66f2: retail's shared "commit a fate to Dodge" epilogue, reached
// from five sites across both HeadTowardStyle switches. Per site only the fate and the
// interval pair differ (BuzzDodgeInterval* for fates 0 and 3, BuzzSlideInterval* for fate 2).
// The fate is the DodgeIdea style: 0 Waggle, 2 Slide, 3 Corkscrew. The three stores are ICF
// folds called by borrowed names (CSimple::SetOwnerPlayer, CAttachedEquip::SetFate,
// TextRenderContext::set_origin_h).
// ----------------------------------------------------------------------------
void BuzzBehavior::ApplySteeringFate(int Fate, float Interval, float IntervalVariance)
{
    Dodge.DodgeStyle = Fate;                                           // .text:062d8920, +0xb4
    Dodge.IntervalTime = Interval;                                     // .text:063275d0, +0x34
    Dodge.IntervalTimeVariancePercent = IntervalVariance;              // .text:063275f0, +0x38

    Dodge.Update(DeltaTime);   // DodgeIdea::Update, LAB_062d7950
}

// ----------------------------------------------------------------------------
// Slot 7, LAB_062c62a0 (no Ghidra function; read off the listing). BuzzState: 0 head away,
// 1 head toward, 2 pass by.
// ----------------------------------------------------------------------------
int BuzzBehavior::Update()
{
    IObjRW* const Handle = SysMgr->Targeting.GetTarget();   // FUN_062ed5a0

    // Retail inlines FUN_062d09b0 (FAiTarget::Init) into a stack FAiTarget, the same 0x88-byte
    // record LargeShipBehavior::Update builds. Only Init's stores happen; every other slice is
    // behind a cache guard Init clears.
    FAiTarget Record;
    Record.Init(OwnerContext, Handle);

    // Retail writes the record's interface pointers before testing either.
    if (OwnerContext == nullptr || Handle == nullptr)
    {
        UpdateResult = kBehaviorUpdateAborted;
        return kBehaviorUpdateAborted;
    }

    // SysMgr+0x14fc, the first argument to all four intercept probes.
    FAiTarget* const NavRecord = &SysMgr->SelfTarget;

    // SysMgr+0x1584, the personality: BuzzHeadParams at +0x1618, BuzzPassParams at
    // +0x1678, FormationParams at +0x17cc.
    pub::AI::Personality const& Pers = SysMgr->PersonalityData;

    int Result = 1;  // EDI throughout retail; becomes UpdateResult.

    Vector& CandidatePos = Record.CachedPos;

    // ------------------------------------------------------------------
    // "Immediate direction" fast path, only when BuzzState != 2. Both exits go to the shared
    // tail (LAB_062c6cda), so the timeout check below still runs.
    // ------------------------------------------------------------------
    bool bFastPathTaken = false;
    if (BuzzState != kBuzzStatePassBy)
    {
        // FUN_062e4f60 / FUN_062e4d00, arguments in retail's push order. Strict less-than.
        if (IsTargetCoAlignedAndAhead(NavRecord, &Record, 0.0f) &&
            GetInterceptClearance(NavRecord, &Record) < FastPathRange)
        {
            // Handle->vtbl[0xc]() returns the 3x3 basis.
            Matrix const& Basis = FetchHandleBasis(Record.Iface);
            {
                Face.SetBasis(Basis);   // FUN_062d75c0

                // FUN_062d93c0; pi/8, wider than the pi/32 ctor default.
                Face.SetAngleWeight8c(0.3926991f);  // 0x3ec90fdb

                Face.SetScale94(1.0f);   // FUN_062d9490

                // The return value is used here (CMP EAX,1).
                int const FaceTick = Face.Update(DeltaTime);
                if (FaceTick == 1)
                {
                    // Strictly greater.
                    if (AlignedElapsed > AfterburnerDelay)
                    {
                        Afterburner.Update(DeltaTime);
                    }
                    AlignedElapsed += DeltaTime;
                }
                else
                {
                    AlignedElapsed = 0.0f;
                }

                Move.SetDirectionFrom(&Basis);   // FUN_062d58f0

                // CAsteroidCube::set_state, the .text:062a33e0 fold.
                Move.SetScale40(1.0f);

                Move.Update(DeltaTime);
            }

            // Retail compares the unrounded x87 sum (FST, not FSTP); less-equal and unordered
            // take the reset-and-1 arm.
            double const Sum308 = (double)(FastPathElapsed) + (double)(DeltaTime);
            FastPathElapsed = (float)(Sum308);
            if (Sum308 > (double)(FastPathMaxDuration))
            {
                Result = 0;
            }
            else
            {
                FastPathElapsed = 0.0f;
                Result = 1;
            }
            bFastPathTaken = true;
        }
    }

    if (!bFastPathTaken)
    {
        // Shared landing pad: the BuzzState == 2 entry skips the bPosValid test, but there the
        // flag is still Init's 0, so the guarded RefreshPos covers both entries.
        Record.RefreshPos();

        // ------------------------------------------------------------------
        // BuzzState == 0, the head-away stage.
        // ------------------------------------------------------------------
        if (BuzzState == kBuzzStateHeadAway)
        {
            // IObjAffect slot 8 (named from Server.dll's Ship forwarder .text:06d02a70);
            // retail discards the result.
            (void)ObjAffectOf(OwnerContext)->get_formation_follower_count();

            // FUN_062e4350. True enters the HeadTowardStyle switch; false moves to head-toward.
            if (IsWithinInterceptRange(NavRecord, &Record, MinDistanceToHeadToward))
            {
                // Any other style goes straight to the timer. No own field is written here.
                switch (HeadTowardStyle)
                {
                case kBuzzHeadStraightTo:
                {
                    // Retail pushes the argument before the branch and both arms tick Afterburner,
                    // so the tick is hoisted.
                    Afterburner.Update(DeltaTime);

                    // TEST AH,0x44 / JP is MSVC's equality idiom: taken on not-equal or
                    // unordered. The second switch gates the same field with JNP.
                    if (Pers.BuzzHeadParams.BuzzHeadTowardRollThrottle != 0.0f)   // [0x1638]
                    {
                        ApplySteeringFate(kDodgeStyleCorkscrew, Pers.BuzzHeadParams.BuzzDodgeIntervalTime,
                                          Pers.BuzzHeadParams.BuzzDodgeIntervalTimeVariancePercent);
                    }
                    else
                    {
                        Move.SetScale40(1.0f);

                        // SysMgr[+0x14fc][+0x84]->vtbl[0xc](); retail inlines the negated
                        // z column instead of calling MoveIdea::SetDirectionFrom.
                        Matrix const& Basis = FetchNavCandidateBasis(SysMgr);
                        {
                            Vector Dir;
                            Dir.x = -Basis.E[0][2];
                            Dir.y = -Basis.E[1][2];
                            Dir.z = -Basis.E[2][2];
                            Move.SetDirection(Dir);
                        }

                        // retail 062c6612-062c661a: `Move->vtbl[4](dt)`.
                        Move.Update(DeltaTime);

                        // FAiTarget::RefreshPos inlined on SysMgr+0x14fc (guard +0x157a).
                        NavRecord->RefreshPos();

                        // FUN_062d7590 = FaceIdea::SetDirection.
                        Vector const LeadMinusCandidate{
                            NavRecord->CachedPos.x - CandidatePos.x,
                            NavRecord->CachedPos.y - CandidatePos.y,
                            NavRecord->CachedPos.z - CandidatePos.z };
                        Face.SetDirection(LeadMinusCandidate);

                        Face.SetScale94(1.0f);   // FUN_062d9490

                        // FUN_062e8840 gates FUN_062d7640 (FaceIdea::SetUpVector).
                        if (CheckNavHelperFlagA(SysMgr))
                        {
                            Vector const WorldUp{ 0.0f, 0.0f, 1.0f };
                            Face.SetUpVector(WorldUp);
                        }

                        Face.Update(DeltaTime);
                    }
                    break;
                }
                case kBuzzHeadSlide:
                    // Retail enters the epilogue at LAB_062c66f2, past the push sequence, but
                    // performs the same three stores.
                    Afterburner.Update(DeltaTime);
                    ApplySteeringFate(kDodgeStyleSlide, Pers.BuzzHeadParams.BuzzSlideIntervalTime,
                                      Pers.BuzzHeadParams.BuzzSlideIntervalTimeVariancePercent);
                    break;
                case kBuzzHeadWaggle:
                    Afterburner.Update(DeltaTime);
                    ApplySteeringFate(kDodgeStyleWaggle, Pers.BuzzHeadParams.BuzzDodgeIntervalTime,
                                      Pers.BuzzHeadParams.BuzzDodgeIntervalTimeVariancePercent);
                    break;
                default:
                    break;
                }

                // Timer, LAB_062c6705: reached only by falling out of the switch; the
                // BuzzState != 0 and head-toward-commit paths jump past it. Retail re-seeds
                // Result (EDI was scratch in the cases) and compares the unrounded sum.
                Result = 1;
                double const Sum340 = (double)(DeltaTime) + (double)(HeadAwayElapsed);
                HeadAwayElapsed = (float)(Sum340);
                if (!(Sum340 < (double)(MaxTimeToHeadAway)))
                {
                    Result = 2;
                }
            }
            else
            {
                // Head-toward commit, in retail's order C, B, A.
                Afterburner.Commit();
                Face.Commit();
                Move.Commit();
                BuzzState = kBuzzStateHeadToward;

                // FUN_062ecc60 / FUN_062ecce0 on the SysMgr+0x1364 record.
                FFormationLeaderState& LeaderRec = SysMgr->FormationLeaders;
                if (Pers.FormationParams.bBreakApartFormationOnBuzzHeadToward != 0)     // [0x17ec]
                {
                    LeaderRec.NotifyFormationLeader1(Pers.FormationParams.BreakFormationOnBuzzHeadTowardTime);
                }
                if (Pers.FormationParams.bRegroupFormationOnBuzzHeadToward != 0)        // [0x17f4]
                {
                    LeaderRec.NotifyFormationLeader2();
                }
            }
        }

        // ------------------------------------------------------------------
        // LAB_062c672b: BuzzState == 1, the head-toward stage. Runs in the same tick as the
        // head-toward commit above.
        // ------------------------------------------------------------------
        if (BuzzState == kBuzzStateHeadToward)
        {
            if (IsWithinInterceptRange(NavRecord, &Record, DistanceToPassBy))
            {
                // Transition to pass-by. This path skips the shared `Result = 0` merge, so
                // Result keeps its entry seed of 1.
                Face.Commit();
                Move.Commit();

                // FUN_062e8850.
                if (CheckNavHelperFlagB(SysMgr))
                {
                    Afterburner.Update(DeltaTime);
                }
                else
                {
                    Afterburner.Commit();
                }
                BuzzState = kBuzzStatePassBy;

                if (Pers.FormationParams.bBreakApartFormationOnBuzzPassBy != 0)
                {
                    SysMgr->FormationLeaders.NotifyFormationLeader1(
                        Pers.FormationParams.BreakFormationOnBuzzPassByTime);
                }
                if (Pers.FormationParams.bRegroupFormationOnBuzzPassBy != 0)
                {
                    SysMgr->FormationLeaders.NotifyFormationLeader2();
                }
                if (Pers.BuzzPassParams.bBuzzDropBombOnPassBy != 0)
                {
                    SysMgr->GunGroups.DropAllMines();   // FUN_062eb550
                }
            }
            else
            {
                if (IsWithinInterceptRange(NavRecord, &Record, MinDistanceToHeadToward))
                {
                    // A second, independent HeadTowardStyle switch. Each case refreshes both
                    // records, then compares the candidate angle strictly below its threshold:
                    // DAT_0639f5d0 pi/8, DAT_0639f534 pi/16, DAT_0639f5d4 pi/2.
                    switch (HeadTowardStyle)
                    {
                    case kBuzzHeadStraightTo:
                    {
                        Record.RefreshPos();
                        NavRecord->RefreshPos();

                        // The second gate tests the same [0x1638] as the first switch, with JNP:
                        // equal goes to AcquireLockEffects().
                        bool const bWithinAngle =
                            ReadCandidateMetric(SysMgr, CandidatePos) < 0.3926991f;  // DAT_0639f5d0
                        if (bWithinAngle && Pers.BuzzHeadParams.BuzzHeadTowardRollThrottle != 0.0f)
                        {
                            ApplySteeringFate(kDodgeStyleCorkscrew, Pers.BuzzHeadParams.BuzzDodgeIntervalTime,
                                              Pers.BuzzHeadParams.BuzzDodgeIntervalTimeVariancePercent);
                        }
                        else
                        {
                            AcquireLockEffects();  // LAB_062c6b47
                        }
                        break;
                    }
                    case kBuzzHeadSlide:
                        Record.RefreshPos();
                        NavRecord->RefreshPos();
                        if (ReadCandidateMetric(SysMgr, CandidatePos) < 0.19634955f)  // DAT_0639f534
                        {
                            ApplySteeringFate(kDodgeStyleSlide, Pers.BuzzHeadParams.BuzzSlideIntervalTime,
                                              Pers.BuzzHeadParams.BuzzSlideIntervalTimeVariancePercent);
                        }
                        else
                        {
                            AcquireLockEffects();
                        }
                        break;
                    case kBuzzHeadWaggle:
                    {
                        // Not the shared epilogue: a guide-vector write sits between the fate
                        // stores and the Dodge tick.
                        Record.RefreshPos();
                        NavRecord->RefreshPos();
                        if (ReadCandidateMetric(SysMgr, CandidatePos) < 1.5707964f)  // DAT_0639f5d4
                        {
                            Dodge.DodgeStyle = kDodgeStyleWaggle;                                 // .text:062d8920
                            Dodge.IntervalTime = Pers.BuzzHeadParams.BuzzDodgeIntervalTime;                // [0x1664] .text:063275d0
                            Dodge.IntervalTimeVariancePercent = Pers.BuzzHeadParams.BuzzDodgeIntervalTimeVariancePercent; // [0x1668] .text:063275f0

                            // Retail repeats both flag-gated refreshes; they are no-ops by now.
                            NavRecord->RefreshPos();
                            Record.RefreshPos();

                            // Operand order is reversed relative to the first switch's case 0
                            // (candidate minus lead), as in retail.
                            Vector Guide;
                            Guide.x = CandidatePos.x - NavRecord->CachedPos.x;
                            Guide.y = CandidatePos.y - NavRecord->CachedPos.y;
                            Guide.z = CandidatePos.z - NavRecord->CachedPos.z;
                            Dodge.SetPreferredForward(Guide);   // FUN_062d88c0

                            Dodge.Update(DeltaTime);
                        }
                        else
                        {
                            AcquireLockEffects();
                        }
                        break;
                    }
                    default:
                        // Calls nothing, unlike the cases' own fallback.
                        break;
                    }
                }
                else
                {
                    // Lost lock: run AcquireLockEffects() at full intensity, then restore the
                    // Personality values.
                    HeadTowardEngineThrottle = 1.0f;
                    HeadTowardTurnThrottle = 1.0f;
                    AcquireLockEffects();
                    HeadTowardEngineThrottle = Pers.BuzzHeadParams.BuzzHeadTowardEngineThrottle;   // [0x1630]
                    HeadTowardTurnThrottle = Pers.BuzzHeadParams.BuzzHeadTowardTurnThrottle;     // [0x1634]
                }

                // LAB_062c6b4e: merge point for the second switch and the lost-lock path,
                // not the pass-by transition.
                Result = 0;
            }
        }

        // ------------------------------------------------------------------
        // LAB_062c6b50: BuzzState == 2, the pass-by stage. Also runs in the same tick as the
        // transition above.
        // ------------------------------------------------------------------
        if (BuzzState == kBuzzStatePassBy)
        {
            // Compare on the unrounded sum, strictly less.
            double const Sum338 = (double)(PassByElapsed) + (double)(DeltaTime);
            PassByElapsed = (float)(Sum338);
            if (Sum338 < (double)(PassByTime))
            {
                switch (PassByStyle)
                {
                case kBuzzPassBreakAway:
                    // All four on BreakAway (BreakAwayIdea).
                    BreakAway.TurnThrottle = Pers.BuzzPassParams.BuzzBreakTurnThrottle;   // FUN_062d9490, +0x94
                    BreakAway.BreakStyle = kBreakStyleSideways;   // .text:063275d0 fold, +0x34
                    BreakAway.SetWatchObject(Record.Iface);   // FUN_062d9450
                    BreakAway.Update(DeltaTime);   // LAB_062d8a50
                    break;
                case kBuzzPassStraightBy:
                    Face.PublishTargetId(0);   // FUN_062d4e50
                    // FUN_062d75e0 (FaceIdea::SetAxis); the roll throttle goes in .z.
                    {
                        Vector Axis;
                        Axis.x = 0.0f;
                        Axis.y = 0.0f;
                        Axis.z = PassByRollThrottle;
                        Face.SetAxis(Axis);
                    }
                    Face.Update(DeltaTime);

                    Move.PublishTargetId(0);   // FUN_062d4e50
                    Move.SetScale40(1.0f);
                    {
                        // Same inlined negated-z-column sequence as the first switch's case 0.
                        Matrix const& Basis = FetchNavCandidateBasis(SysMgr);
                        Vector Dir;
                        Dir.x = -Basis.E[0][2];
                        Dir.y = -Basis.E[1][2];
                        Dir.z = -Basis.E[2][2];
                        Move.SetDirection(Dir);
                    }
                    Move.Update(DeltaTime);
                    break;
                default:
                    // PassByStyle == 2 only; other values just zero Result (LAB_062c6cbf).
                    if (PassByStyle == kBuzzPassEngineKill)
                    {
                        SysMgr->Thrust.SetEngineActive(false);   // FUN_062e91d0

                        Face.SetWatchObject(Handle);   // FUN_062d7510; Handle is non-null here

                        Face.SetScale94(1.0f);
                        Face.Update(DeltaTime);
                    }
                    break;
                }
                Result = 0;
            }
            else
            {
                // Give up the maneuver and restart at head away.
                PassByElapsed = 0.0f;
                HeadAwayElapsed = 0.0f;
                BuzzState = kBuzzStateHeadAway;
                Result = 2;
            }
        }
    }

    // ------------------------------------------------------------------
    // LAB_062c6cda, the shared tail of every path including the fast path.
    // FUN_062e8810 reads SysMgr's byte at +0x5c.
    // ------------------------------------------------------------------
    if (CheckNavHelperTimeout(SysMgr))
    {
        Result = 2;
    }

    UpdateResult = Result;
    return Result;
}

// Handle->vtbl[0xc]() (get_transform) on the stack record's Iface (Record+0x84), no null test.
Matrix const& BuzzBehavior::FetchHandleBasis(IObjInspect const* Handle)
{
    return Handle->get_transform().Rotation;
}

// SysMgr[+0x14fc][+0x84]->vtbl[0xc](): the own ship's orientation; never null-tested.
Matrix const& BuzzBehavior::FetchNavCandidateBasis(SystemManager* SysMgr)
{
    return SysMgr->SelfTarget.Iface->get_transform().Rotation;
}

// The three SysMgr+0x34 byte probes (the timer table's getters).
bool BuzzBehavior::CheckNavHelperTimeout(SystemManager* SysMgr)
{
    return SysMgr->Timers.IsTimer0Active();   // FUN_062e8810
}

bool BuzzBehavior::CheckNavHelperFlagA(SystemManager* SysMgr)
{
    return SysMgr->Timers.IsTimer5Active();   // FUN_062e8840
}

bool BuzzBehavior::CheckNavHelperFlagB(SystemManager* SysMgr)
{
    return SysMgr->Timers.IsTimer4Active();   // FUN_062e8850
}

// FUN_062e1e20 = AngleToBasisZColumn3 (common_ini.h), cdecl. The lead point is the own
// SelfTarget.CachedPos itself (SysMgr+0x1508), not a copy.
double BuzzBehavior::ReadCandidateMetric(SystemManager* SysMgr, Vector const& CandidatePos)
{
    Matrix const* const Basis = &FetchNavCandidateBasis(SysMgr);
    return AngleToBasisZColumn3(Basis, &SysMgr->SelfTarget.CachedPos, &CandidatePos);
}
