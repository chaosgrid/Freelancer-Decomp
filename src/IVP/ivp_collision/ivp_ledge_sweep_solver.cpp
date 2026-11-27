// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// ============================================================================
// ivp_ledge_sweep_solver.cpp
//
// The generic time-of-contact solvers behind the Csys sweep-test driver family:
//
//   FUN_06369280  ->  IVP_Ledge_Sweep_Evaluator::RefineContactTime
//   FUN_063694a0  ->  IVP_Ledge_Sweep_Evaluator::SearchContactTime
//   FUN_06369720  ->  IVP_Ledge_Sweep_Evaluator::SearchContactTimeFromContact
//
// Conservative advancement: a distance functor called through a one-slot vtable,
// a step of (distance / relative speed), two lazily-filled 21-entry transform
// caches on a 1/200 s grid, and a regula-falsi bracket refinement. (They sit at
// LCP-adjacent addresses but have nothing to do with the LCP solver.)
//
// TIME REPRESENTATION. Every "time" is a split IVP_Time {time_base, time_offset}
// passed as two floats, as retail passes it. time_offset is normalized into
// (-inf, 1.0] with an explicit `while (offset > 1.0f)` loop (:063695df,
// :06369882, :06369302), kept as a loop rather than fmod because its float
// rounding is observable.
//
// .rdata constants (only DAT_063a58b0 is an 8-byte double):
//   DAT_063a53d0 = 00 00 80 3f              -> 1.0f
//   DAT_063a53d4 = 00 00 00 00              -> 0.0f
//   DAT_063a545c = a6 9b 44 3b              -> 0.003f (0x3b449ba6)
//   DAT_063a5654 = 0a d7 a3 3b              -> 0.005f (0x3ba3d70a)
//   DAT_063a58a8 = 00 00 c0 3e              -> 0.375f
//   DAT_063a58ac = 00 00 48 43              -> 200.0f  (== 1.0f / 0.005f)
//   DAT_063a58b0 = 00 00 00 00 00 00 00 00  -> (double)0.0
//
// x87 BRANCH POLARITY. :063694e5 and :06369764 test the SAME comparison with
// OPPOSITE jumps -- see SearchContactTimeFromContact.
//
// x87 PRECISION. Values retail keeps on the x87 stack are `double` here and are
// rounded to float only at retail's dword stores; compares on the unrounded ST0
// compare the double. Every test sends the unordered (NaN) case to the same side
// as retail.
// ============================================================================

#include "ivp_ledge_sweep_solver.h"

#include "../ivp_physics/ivp_real_object.h"
#include "../ivp_physics/ivp_mindist.h"
#include "../ivp_compact_builder/ivp_compact_ledge.h"
#include "ivp_compact_ledge_solver.h" // calc_hesse_vec_object_not_normized (FUN_0635b940)
#include "../ivp_utility/ivp_signed_byte.h" // IVP_SignedLowByte (IVP_Sweep_Frame_Cache::Init)

#include <cmath>

// ============================================================================
// FUN_06369280 -- IVP_Ledge_Sweep_Evaluator::RefineContactTime
// Sole caller: SearchContactTime (:063696d9).
//
// Regula falsi on the bracket [Lo -> LoDistance, Hi -> HiDistance], solving
// Evaluate(...) == Threshold, with a pull toward the bracket midpoint on every
// 4th iteration against false-position stagnation. The receiver is used only
// for the slot-0 virtual call.
// ============================================================================
void IVP_Ledge_Sweep_Evaluator::RefineContactTime(IVP_Time *OutTime,
                                                  float LoTimeBase, float LoTimeOffset,
                                                  float HiTimeBase, float HiTimeOffset,
                                                  float Threshold,
                                                  float LoDistance, float HiDistance,
                                                  IVP_Real_Object *ObjectA,
                                                  IVP_Real_Object *ObjectB)
{
    // Counts only the iterations that did NOT converge.
    int Iteration = 0;

    // Retail keeps the low end of the bracket both in EBX/EBP and in the
    // param_2/param_3 slots; they never diverge, so one pair models both.
    float ResultBase = LoTimeBase;
    float ResultOffset = LoTimeOffset;

    for (;;)
    {
        float MidBase = ResultBase;

        // The whole expression stays on the x87 stack; the stored copy is
        // rounded, the > 1.0 test below compares the unrounded value.
        double MidOffsetExt =
            ((((double)(HiTimeBase) - LoTimeBase) + HiTimeOffset) - LoTimeOffset)
                * ((double)(Threshold) - LoDistance)
                / ((double)(HiDistance) - LoDistance)
            + LoTimeOffset;
        float MidOffset = (float)(MidOffsetExt);

        // Unordered skips the normalization; each pass compares the
        // unrounded difference (:06369302).
        while (MidOffsetExt > 1.0)
        {
            MidBase += 1.0f;
            MidOffsetExt = (double)(MidOffset) - 1.0;
            MidOffset = (float)(MidOffsetExt);
        }

        // Fires on iterations 3, 7, 11, ...
        if ((Iteration & 3) == 3)
        {
            // The give-up exit reports the LOW end of the bracket, not the
            // current midpoint.
            if (Iteration > 0x40)
            {
                OutTime->time_base = ResultBase;
                OutTime->time_offset = ResultOffset;
                return;
            }

            // Pull toward the bracket midpoint: ((Hi - Mid) + (Lo - Mid)) *
            // 0.375f + Mid, on the x87 stack (:06369346).
            MidOffsetExt =
                ((((((double)(HiTimeBase) - MidBase) + HiTimeOffset) - MidOffset)
                  + ((((double)(LoTimeBase) - MidBase) + LoTimeOffset) - MidOffset))
                 * (double)(0.375f))
                + MidOffset;
            MidOffset = (float)(MidOffsetExt);

            while (MidOffsetExt > 1.0)
            {
                MidBase += 1.0f;
                MidOffsetExt = (double)(MidOffset) - 1.0;
                MidOffset = (float)(MidOffsetExt);
            }
        }

        IVP_Time MidTime;
        MidTime.time_base = MidBase;
        MidTime.time_offset = MidOffset;

        IVP_U_Matrix FrameA;
        IVP_U_Matrix FrameB;
        ObjectA->get_m_world_f_object_at_transform(MidTime, &FrameA);
        ObjectB->get_m_world_f_object_at_transform(MidTime, &FrameB);

        // The evaluator's result stays on the x87 stack for both tests and is
        // rounded only when stored into the moved bracket end.
        const double MidDistanceExt = Evaluate(&FrameA, &FrameB);
        const float MidDistance = (float)(MidDistanceExt);

        // Converged when strictly less OR unordered, hence the negated >=.
        if (!(std::fabs(MidDistanceExt - Threshold) >= (double)(0.003f)))
        {
            OutTime->time_base = MidBase;
            OutTime->time_offset = MidOffset;
            return;
        }

        Iteration = Iteration + 1;

        // Below Threshold (or unordered) moves the HIGH end of the bracket.
        if (!(MidDistanceExt >= Threshold))
        {
            HiDistance = MidDistance;
            HiTimeBase = MidBase;
            HiTimeOffset = MidOffset;
        }
        else
        {
            ResultBase = MidBase;
            ResultOffset = MidOffset;
            LoDistance = MidDistance;
            LoTimeBase = MidBase;
            LoTimeOffset = MidOffset;
        }
    }
}

// ============================================================================
// FUN_063694a0 -- IVP_Ledge_Sweep_Evaluator::SearchContactTime
//
// Conservative advancement from (CurTimeBase, CurTimeOffset) toward
// (EndTimeBase, EndTimeOffset): while the evaluator's distance is above
// Threshold, advance by up to twice (distance / relative speed), quantized
// down onto a 1/200 s grid, filling both caches' transform slots on demand;
// as soon as the distance drops to or below Threshold, hand the bracket to
// RefineContactTime.
//
// Returns 1 and writes *OutTime when a contact time at or before the end of
// the interval was found, 0 otherwise. The receiver's +0x08 and vtable are
// both used (:06369523, :0636965e).
//
// CACHE-SLOT BOUND. Retail's only guard is an exact `== 20` test (:0636967b)
// made AFTER the slot has been indexed. Safe only because a step never exceeds
// the remaining interval; a caller passing an interval longer than 21/200 s
// would walk off FrameAtSlot[] into FrameStorage[]. Kept unclamped.
// ============================================================================
int IVP_Ledge_Sweep_Evaluator::SearchContactTime(float Threshold,
                                                 float CurTimeBase, float CurTimeOffset,
                                                 float EndTimeBase, float EndTimeOffset,
                                                 int StartSlot,
                                                 IVP_Sweep_Frame_Cache *CacheA,
                                                 IVP_Sweep_Frame_Cache *CacheB,
                                                 const float *KnownDistance,
                                                 IVP_Time *OutTime)
{
    // KnownDistance is an optional already-computed distance at the interval
    // start. Slot 0 of both caches is always non-null (IVP_Sweep_Frame_Cache ctor).
    float Distance;
    if (KnownDistance != nullptr)
    {
        Distance = *KnownDistance;
    }
    else
    {
        Distance = (float)(Evaluate(CacheA->FrameAtSlot[0], CacheB->FrameAtSlot[0]));
    }

    // Distance <= Threshold (or unordered) is an immediate hit at the start.
    if (!(Distance > Threshold))
    {
        OutTime->time_base = CurTimeBase;
        OutTime->time_offset = CurTimeOffset;
        return 1;
    }

    int Slot = StartSlot;
    float PrevTimeBase = CurTimeBase;
    float PrevTimeOffset = CurTimeOffset;

    for (;;)
    {
        // Multiplies by the reciprocal speed (+0x08), not divides by +0x04.
        const double Step = ((double)(Distance) - Threshold) * IMaxRelativeSpeed;

        // Negative while before the end of the interval. The first test uses
        // the unrounded value, the second the rounded copy (:06369536).
        const double RemainingExt =
            (((double)(PrevTimeBase) - EndTimeBase) + PrevTimeOffset) - EndTimeOffset;
        const float Remaining = (float)(RemainingExt);

        // One conservative step already overshoots the end: no contact inside.
        if (RemainingExt + Step > 0.0)
        {
            return 0;
        }

        const double DoubledStep = Step + Step;

        double Advance;

        // Two steps would overshoot: jump to the end of the interval plus 0.003.
        if ((double)(Remaining) + DoubledStep > 0.0)
        {
            Advance = ((((double)(EndTimeBase) - PrevTimeBase) + EndTimeOffset) - PrevTimeOffset) +
                      (double)(0.003f);
        }
        else
        {
            Advance = DoubledStep;

            // Compared against the 8-byte DAT_063a58b0 (0.0); negative or
            // unordered clamps to 0.
            if (!(Advance >= 0.0))
            {
                Advance = 0.0;
            }
        }

        // Quantize onto the 1/200 s grid, at least one grid step (ftol truncates).
        int StepCount = static_cast<int>(Advance * (double)(200.0f));
        if (StepCount < 1)
        {
            StepCount = 1;
        }

        Slot = Slot + StepCount;

        // The offset sum is rounded when stored but compared unrounded.
        float NewTimeBase = PrevTimeBase;
        double NewTimeOffsetExt =
            (double)(StepCount) * (double)(0.005f) + PrevTimeOffset;
        float NewTimeOffset = (float)(NewTimeOffsetExt);
        while (NewTimeOffsetExt > 1.0)
        {
            NewTimeBase += 1.0f;
            NewTimeOffsetExt = (double)(NewTimeOffset) - 1.0;
            NewTimeOffset = (float)(NewTimeOffsetExt);
        }

        IVP_Time SlotTime;
        SlotTime.time_base = NewTimeBase;
        SlotTime.time_offset = NewTimeOffset;

        // CacheA is filled first; the stored pointer is always the cache's own
        // inline storage for that slot.
        if (CacheA->FrameAtSlot[Slot] == nullptr)
        {
            CacheA->FrameAtSlot[Slot] = &CacheA->FrameStorage[Slot];
            CacheA->Object->get_m_world_f_object_at_transform(SlotTime,
                                                             &CacheA->FrameStorage[Slot]);
        }

        // Retail latches CacheA's pointer BEFORE possibly creating CacheB's.
        IVP_U_Matrix *FrameA = CacheA->FrameAtSlot[Slot];

        if (CacheB->FrameAtSlot[Slot] == nullptr)
        {
            CacheB->FrameAtSlot[Slot] = &CacheB->FrameStorage[Slot];
            CacheB->Object->get_m_world_f_object_at_transform(SlotTime,
                                                             &CacheB->FrameStorage[Slot]);
        }

        const double NewDistanceExt = Evaluate(FrameA, CacheB->FrameAtSlot[Slot]);
        const float NewDistance = (float)(NewDistanceExt);

        // NewDistance <= Threshold (or unordered): [Prev, New] straddles the contact.
        if (!(NewDistanceExt > Threshold))
        {
            // Retail refines into its own param_2 slot, overwriting
            // (CurTimeBase, CurTimeOffset).
            IVP_Time Refined;
            RefineContactTime(&Refined, PrevTimeBase, PrevTimeOffset,
                              NewTimeBase, NewTimeOffset, Threshold,
                              Distance, NewDistance,
                              CacheA->Object, CacheB->Object);
            CurTimeBase = Refined.time_base;
            CurTimeOffset = Refined.time_offset;

            // The refined time landed strictly past the end.
            if ((((double)(CurTimeBase) - EndTimeBase) + CurTimeOffset) - EndTimeOffset > 0.0)
            {
                return 0;
            }

            OutTime->time_base = CurTimeBase;
            OutTime->time_offset = CurTimeOffset;
            return 1;
        }

        // Exact equality with 20 -- see CACHE-SLOT BOUND above.
        if (Slot == 0x14)
        {
            return 0;
        }

        PrevTimeBase = NewTimeBase;
        PrevTimeOffset = NewTimeOffset;
        Distance = NewDistance;
    }
}

// ============================================================================
// FUN_06369720 -- IVP_Ledge_Sweep_Evaluator::SearchContactTimeFromContact
//
// The entry point the drivers call (8 call sites in 0x0635dc22-0x0635faff).
// It handles bodies ALREADY within Threshold at the interval start, where
// SearchContactTime would report an immediate hit at t0.
//
// The entry test is the mirror of SearchContactTime's (:06369764 vs
// :063694e5): bodies that start APART are delegated to SearchContactTime;
// bodies that start TOUCHING run the loop below.
//
// The loop walks forward while the separation keeps growing inside Threshold
// and stops once it stops growing (NewDistance <= the distance at t0),
// reporting the PREVIOUS grid time. If the separation leaves the Threshold
// band, the rest of the interval goes to SearchContactTime from the slot
// reached, re-using the measured distance.
//
// Differences from SearchContactTime's stepping:
//   * the step is measured against InnerThreshold, not Threshold;
//   * the step is NOT doubled, and an overshoot jumps to the end of the
//     interval rather than returning 0;
//   * it divides by MaxRelativeSpeed (+0x04) instead of multiplying by
//     IMaxRelativeSpeed (+0x08); the drivers set +0x08 = 1.0f / +0x04
//     (:0635db9d), so the two agree.
// ============================================================================
int IVP_Ledge_Sweep_Evaluator::SearchContactTimeFromContact(float Threshold, float InnerThreshold,
                                                            float CurTimeBase, float CurTimeOffset,
                                                            float EndTimeBase, float EndTimeOffset,
                                                            IVP_Sweep_Frame_Cache *CacheA,
                                                            IVP_Sweep_Frame_Cache *CacheB,
                                                            const float *KnownDistance,
                                                            IVP_Time *OutTime)
{
    float Distance;
    if (KnownDistance != nullptr)
    {
        Distance = *KnownDistance;
    }
    else
    {
        Distance = (float)(Evaluate(CacheA->FrameAtSlot[0], CacheB->FrameAtSlot[0]));
    }

    if (Distance > Threshold)
    {
        return SearchContactTime(Threshold, CurTimeBase, CurTimeOffset,
                                 EndTimeBase, EndTimeOffset, 0,
                                 CacheA, CacheB, &Distance, OutTime);
    }

    // The reciprocal is rounded to float before use.
    float InvSpeed = 1.0f / MaxRelativeSpeed;
    int Slot = 0;
    float PrevTimeBase = CurTimeBase;
    float PrevTimeOffset = CurTimeOffset;

    // Strictly negative OR unordered enters the loop.
    if ((((double)(CurTimeBase) - EndTimeBase) + CurTimeOffset) - EndTimeOffset >= 0.0)
    {
        return 0;
    }

    // From the second iteration on this is the evaluator's unrounded result
    // left on the x87 stack (:0636994e), hence double.
    double CurDistance = Distance;

    for (;;)
    {
        // Step, Remaining and their sum all stay on the x87 stack.
        const double Step = (CurDistance - InnerThreshold) * InvSpeed;
        const double Remaining = (((double)(PrevTimeBase) - EndTimeBase) + PrevTimeOffset) - EndTimeOffset;

        double Advance;

        // Overshoot jumps to the end of the interval.
        if (Remaining + Step > 0.0)
        {
            Advance = ((((double)(EndTimeBase) - PrevTimeBase) + EndTimeOffset) - PrevTimeOffset) +
                      (double)(0.003f);
        }
        else
        {
            Advance = Step;

            // Compared against the 4-byte DAT_063a53d4 (0.0f), where
            // SearchContactTime uses the 8-byte DAT_063a58b0; same value.
            if (!(Advance >= 0.0))
            {
                Advance = 0.0;
            }
        }

        int StepCount = static_cast<int>(Advance * (double)(200.0f));
        if (StepCount < 1)
        {
            StepCount = 1;
        }

        // Unlike SearchContactTime, base and offset both stay on the x87 stack
        // through the normalization loop and are rounded only after it.
        double WorkTimeBaseExt = PrevTimeBase;
        double WorkTimeOffsetExt =
            (double)(StepCount) * (double)(0.005f) + PrevTimeOffset;
        while (WorkTimeOffsetExt > 1.0)
        {
            WorkTimeBaseExt += 1.0;
            WorkTimeOffsetExt -= 1.0;
        }
        const float WorkTimeBase = (float)(WorkTimeBaseExt);
        const float WorkTimeOffset = (float)(WorkTimeOffsetExt);

        // No `Slot == 20` guard in this function; the bound rests on Advance
        // never exceeding the remaining interval.
        Slot = Slot + StepCount;

        IVP_Time SlotTime;
        SlotTime.time_base = WorkTimeBase;
        SlotTime.time_offset = WorkTimeOffset;

        if (CacheA->FrameAtSlot[Slot] == nullptr)
        {
            CacheA->FrameAtSlot[Slot] = &CacheA->FrameStorage[Slot];
            CacheA->Object->get_m_world_f_object_at_transform(SlotTime,
                                                             &CacheA->FrameStorage[Slot]);
        }

        IVP_U_Matrix *FrameA = CacheA->FrameAtSlot[Slot];

        if (CacheB->FrameAtSlot[Slot] == nullptr)
        {
            CacheB->FrameAtSlot[Slot] = &CacheB->FrameStorage[Slot];
            CacheB->Object->get_m_world_f_object_at_transform(SlotTime,
                                                             &CacheB->FrameStorage[Slot]);
        }

        // Kept unrounded for both tests and for the next iteration.
        const double NewDistanceExt = Evaluate(FrameA, CacheB->FrameAtSlot[Slot]);

        // Separated past the band: the rest is an ordinary approach search.
        if (NewDistanceExt > Threshold)
        {
            if ((((double)(WorkTimeBase) - EndTimeBase) + WorkTimeOffset) - EndTimeOffset > 0.0)
            {
                return 0;
            }

            // The handed-over distance is the float.
            float NewDistance = (float)(NewDistanceExt);

            // Continues from the slot already reached, not from 0.
            return SearchContactTime(Threshold, WorkTimeBase, WorkTimeOffset,
                                     EndTimeBase, EndTimeOffset, Slot,
                                     CacheA, CacheB, &NewDistance, OutTime);
        }

        // Separation stopped growing: report the PREVIOUS grid time.
        if (!(NewDistanceExt > Distance)) // <= or unordered
        {
            OutTime->time_base = PrevTimeBase;
            OutTime->time_offset = PrevTimeOffset;
            return 1;
        }

        PrevTimeBase = WorkTimeBase;
        PrevTimeOffset = WorkTimeOffset;

        // Loop while strictly before the end OR unordered.
        if ((((double)(WorkTimeBase) - EndTimeBase) + WorkTimeOffset) - EndTimeOffset >= 0.0)
        {
            return 0;
        }

        CurDistance = NewDistanceExt;
    }
}

// ============================================================================
// FUN_0635de90 -- IVP_Sweep_Frame_Cache::Init
//
// Sixteen call sites, all sweep drivers, each seeding one cache per object.
//
//   flags low byte >= 8 (non-moving): all 21 slots point at the SAME prepared
//        frame, Source->WorldTransform, so the lazy-fill path never fires.
//   <  8 (moving): slots 20..1 are nulled and only slot 0 is seeded; the
//        solvers fill the rest on demand into FrameStorage.
//
// Either way slot 0 is valid, which is why the solvers read FrameAtSlot[0]
// with no null check.
//
// Not reproduced: retail precedes each call with the MSVC vector ctor
// iterator FUN_06273bc0(FrameStorage, 0x40, 0x15, Csys::Csys); Csys::Csys is
// an empty `MOV EAX,ECX; RET`, so it is a no-op. (0x40 = sizeof(IVP_U_Matrix);
// see the header's note on the retail Csys name collision.)
// ============================================================================
void IVP_Sweep_Frame_Cache::Init(IVP_NarrowPhaseCacheSlot *Source)
{
    this->Object = Source->Owner;
    this->Core = this->Object->core;

    // Retail tests `(int32_t)(flags << 24) < 0x08000000`. Written as
    // IVP_SignedLowByte, not as the shift: MSVC folded the shift form into a
    // whole-dword compare, so a moving ship (flags 0x101) took the static
    // branch and the sweep never found the contact (ivp_signed_byte.h).
    const int32_t MovementByte = IVP_SignedLowByte(this->Object->flags);

    IVP_U_Matrix *const PreparedFrame = &Source->WorldTransform;

    if (MovementByte < 8)
    {
        for (int SlotIndex = 20; SlotIndex >= 1; --SlotIndex)
        {
            this->FrameAtSlot[SlotIndex] = 0;
        }
        this->FrameAtSlot[0] = PreparedFrame;
    }
    else
    {
        for (int SlotIndex = 20; SlotIndex >= 0; --SlotIndex)
        {
            this->FrameAtSlot[SlotIndex] = PreparedFrame;
        }
    }

    // Retail emits this store separately at the end of both paths.
    this->LastRefreshTick = Source->LastRefreshTick;
}

// ============================================================================
// THE DRIVER LAYER
// ============================================================================
//
// The tolerances come from the global mindist settings block G_IVP_MindistSettings
// (.data:0658aa10, ivp_mindist_settings.h):
//   DAT_0658aa10 = +0x000             -- the tolerance base
//   DAT_0658aa18 = +0x008             -- the ramp's base, hence retail's
//                                        `FLD [idx*4 + DAT_0658aa18]`
#include "../ivp_physics/ivp_mindist_settings.h"

namespace
{
    float SweepToleranceRamp(uint32_t Index) { return IVP_MindistSettingsRamp(Index); }
    float SweepToleranceBase() { return G_IVP_MindistSettings.real_coll_dist; }            // DAT_0658aa10
    float SweepToleranceBias() { return G_IVP_MindistSettings.mindist_change_force_dist; } // DAT_0658ab34

    // FUN_0635b940 (unnormalised face normal) is
    // IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized.

    // ------------------------------------------------------------------------
    // The evaluator EstimateBallVsLedgeFaceContact (FUN_0635df00) installs:
    // vfptr PTR_LAB_0635d5b0 (:0635df83), Evaluate = LAB_0635d5b0.
    //
    // Field offsets are the driver's own writes (there is no shared base
    // class beyond IVP_Ledge_Sweep_Evaluator; see the header):
    //   +0x04 / +0x08  MaxRelativeSpeed, IMaxRelativeSpeed  (:0635dfdb/:0635dff7)
    //   +0x14 PointA   x, y, z zeroed explicitly; the 4th float at +0x20 is
    //                  never written and never read.
    //   +0x24 Normal   face normal, normalized in place (:0635dfc7 / :0635dfd3).
    //   +0x34 Anchor   the feature edge's own start vertex.
    // sizeof: 4 (vfptr) + 4*4 (base fields) + 3*0x10 = 0x44.
    // ------------------------------------------------------------------------
    class IVP_Sweep_PointPlane_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point PointA;   // +0x14
        IVP_U_Point Normal;   // +0x24
        IVP_U_Point Anchor;   // +0x34

        // LAB_0635d5b0, the largest of the nine and installed by every driver but
        // FUN_0635e1d0. Signed distance of a point on A from a plane carried by B:
        //   Position = FrameA->vmult4(P14)              // CALL FUN_063484e0
        //   Anchor   = FrameB->vmult4(P34)              // inlined
        //   Normal   = FrameB->vmult (P24)              // inlined
        //   return dot(Position - Anchor, Normal)
        // The inlined transforms do NOT round like FUN_063484e0: Anchor x/y are
        // spilled as floats after two terms, Anchor.z stays extended to the final
        // subtraction; each Normal component has its own association order.
        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            IVP_U_Point Position;
            FrameA->vmult4(&PointA, &Position); // 0635d5c3

            const IVP_U_Point *P34 = &Anchor;
            const double Px = P34->k[0];
            const double Py = P34->k[1];
            const double Pz = P34->k[2];
            float Tx = (float)(FrameB->rows[0].k[0] * Px);
            float Ty = (float)(FrameB->rows[1].k[0] * Px);
            double Ez = Px * FrameB->rows[2].k[0];
            Tx = (float)(FrameB->rows[0].k[1] * Py + Tx);
            Ty = (float)(FrameB->rows[1].k[1] * Py + Ty);
            Ez = Ez + FrameB->rows[2].k[1] * Py;
            const float AnchorX = (float)((FrameB->rows[0].k[2] * Pz + Tx) + FrameB->vv.k[0]);
            const float AnchorY = (float)((FrameB->rows[1].k[2] * Pz + Ty) + FrameB->vv.k[1]);
            const double AnchorZ = (Ez + FrameB->rows[2].k[2] * Pz) + FrameB->vv.k[2];

            const IVP_U_Point *P24 = &Normal;
            const float NormalX = (float)(((double)(FrameB->rows[0].k[0]) * P24->k[0] +
                                                      (double)(FrameB->rows[0].k[2]) * P24->k[2]) +
                                                     (double)(FrameB->rows[0].k[1]) * P24->k[1]);
            const float NormalY = (float)(((double)(FrameB->rows[1].k[0]) * P24->k[0] +
                                                      (double)(FrameB->rows[1].k[2]) * P24->k[2]) +
                                                     (double)(FrameB->rows[1].k[1]) * P24->k[1]);
            const float NormalZ = (float)(((double)(FrameB->rows[2].k[1]) * P24->k[1] +
                                                      (double)(FrameB->rows[2].k[0]) * P24->k[0]) +
                                                     (double)(FrameB->rows[2].k[2]) * P24->k[2]);

            const float DeltaX = Position.k[0] - AnchorX;
            const float DeltaY = Position.k[1] - AnchorY;
            const double DeltaZ = (double)(Position.k[2]) - AnchorZ;
            return (DeltaZ * NormalZ + (double)(DeltaY) * NormalY) + (double)(DeltaX) * NormalX;
        }
    };
    static_assert(sizeof(IVP_Sweep_PointPlane_Evaluator) == 0x44,
                  "evaluator must be retail's 0x44-byte frame local");

    // ------------------------------------------------------------------------
    // The second evaluator EstimateLedgeVertexVsLedgeFaceContact (FUN_0635da70)
    // installs: vfptr PTR_LAB_0635da20 (:0635dc61), Evaluate = LAB_0635da20. Two
    // points, and the base fields come from a different speed bound.
    //   +0x04 / +0x08  :0635dd05 / :0635dd2b
    //   +0x14 Direction  the unit edge direction
    //   +0x24 Normal     a 4-dword copy of the first evaluator's normal
    // sizeof: 4 + 4*4 + 2*0x10 = 0x34.
    // ------------------------------------------------------------------------
    class IVP_Sweep_EdgeNormal_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point Direction;   // +0x14
        IVP_U_Point Normal;      // +0x24

        // LAB_0635da20: P24 rotated by FrameB, re-expressed in FrameA's local
        // space (vimult), then dotted with the fixed local direction P14.
        // Retail's association order, returned unrounded.
        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            const IVP_U_Point *P0 = &Direction;
            const IVP_U_Point *P1 = &Normal;
            IVP_U_Point Local1, Local2;
            FrameB->vmult(P1, &Local1);
            FrameA->vimult(&Local1, &Local2);
            return ((double)(Local2.k[1]) * P0->k[1] + (double)(Local2.k[2]) * P0->k[2]) +
                   (double)(Local2.k[0]) * P0->k[0];
        }
    };
    static_assert(sizeof(IVP_Sweep_EdgeNormal_Evaluator) == 0x34,
                  "evaluator must be retail's 0x34-byte frame local");

    // ------------------------------------------------------------------------
    // The three evaluators EstimateLedgeEdgeVsLedgeEdgeContact (FUN_0635e1d0)
    // installs. Offsets are the driver's own stores; its stack frame (0xc7c
    // bytes) tiles exactly, which pins the sizes:
    //
    //   frame+0x6c .. +0xc4   evaluator 1 (0x58 bytes), vfptr PTR_LAB_0635e080
    //   frame+0xc4 .. +0xf8   evaluator 3 (0x34 bytes), vfptr PTR_LAB_0635d6d0
    //   frame+0xf8 .. +0x12c  evaluator 2 (0x34 bytes), vfptr PTR_LAB_0635e160
    //   frame+0x12c.. +0x6d4  CacheB      (0x5a8 bytes)
    //   frame+0x6d4.. +0xc7c  CacheA      (0x5a8 bytes)
    // ------------------------------------------------------------------------

    // Evaluator 1, frame+0x6c, vfptr stored at :0635e263; slot
    // LAB_0635e080 (skew-line edge-edge separation).
    //   +0x04 / +0x08  :0635e3b2 / :0635e3d5
    //   +0x14 PositionA  frame+0x80
    //   +0x24 DirectionA frame+0x90
    //   +0x34 PositionB  frame+0xa0
    //   +0x44 DirectionB frame+0xb0
    //   +0x54 Scale      frame+0xc0, +-1.0f (:0635e443/:0635e458)
    // sizeof: 0x14 base + 4*0x10 + 4 = 0x58.
    class IVP_Sweep_EdgeEdge_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point PositionA;    // +0x14
        IVP_U_Point DirectionA;   // +0x24
        IVP_U_Point PositionB;    // +0x34
        IVP_U_Point DirectionB;   // +0x44
        float Scale;              // +0x54

        // LAB_0635e080: the signed distance between two skew lines along their
        // common perpendicular, times the stored scale at +0x54.
        //   Axis = cross(WorldDirectionA, WorldDirectionB)      // FUN_0628bed0
        //   return (dot(Axis, WorldPositionA) - dot(Axis, WorldPositionB))
        //              * (1.0f / sqrtf(Axis.quad_length())) * Float54;
        // Two dots subtracted, not one dot of the difference (they differ in
        // float). Numerator and Q rounded to float, the result unrounded.
        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            IVP_U_Point WorldPositionA;
            IVP_U_Point WorldDirectionA;
            IVP_U_Point WorldPositionB;
            IVP_U_Point WorldDirectionB;
            FrameA->vmult(&DirectionA, &WorldDirectionA);
            FrameA->vmult4(&PositionA, &WorldPositionA);
            FrameB->vmult(&DirectionB, &WorldDirectionB);
            FrameB->vmult4(&PositionB, &WorldPositionB);

            IVP_U_Point Axis;
            Axis.calc_cross_product(&WorldDirectionA, &WorldDirectionB);

            const double DotA = ((double)(Axis.k[2]) * WorldPositionA.k[2] +
                                 (double)(Axis.k[1]) * WorldPositionA.k[1]) +
                                (double)(WorldPositionA.k[0]) * Axis.k[0];
            const double DotB = ((double)(Axis.k[2]) * WorldPositionB.k[2] +
                                 (double)(Axis.k[1]) * WorldPositionB.k[1]) +
                                (double)(WorldPositionB.k[0]) * Axis.k[0];
            const float Numerator = (float)(DotA - DotB);
            const float QuadLength = (float)(
                ((double)(Axis.k[1]) * Axis.k[1] + (double)(Axis.k[2]) * Axis.k[2]) +
                (double)(Axis.k[0]) * Axis.k[0]);
            return (IVP_InvSqrtFloat(QuadLength) * Numerator) * Scale;
        }
    };
    // offsetof is not used: offsetof on a polymorphic type is only
    // conditionally supported.
    static_assert(sizeof(IVP_Sweep_EdgeEdge_Evaluator) == 0x58,
                  "evaluator must be retail's 0x58-byte frame local");

    // Evaluator 2, frame+0xf8, vfptr stored at :0635e576; slot
    // LAB_0635e160, the parallel-edge guard (the squared cross-product
    // length whose reciprocal square root evaluator 1 divides by).
    //   +0x04 / +0x08  :0635e553 / :0635e581
    //   +0x14 DirectionA frame+0x10c, copied from evaluator 1's +0x24
    //   +0x24 DirectionB frame+0x11c, copied from evaluator 1's +0x44
    // A different retail class from IVP_Sweep_EdgeNormal_Evaluator despite
    // the same size.
    class IVP_Sweep_EdgeParallel_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point DirectionA;   // +0x14
        IVP_U_Point DirectionB;   // +0x24

        // LAB_0635e160: |cross(FrameA->vmult(P14), FrameB->vmult(P24))|^2, the
        // parallel-edge guard for LAB_0635e080 (the quantity it divides by).
        // Rotation-only transforms; result unrounded.
        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            IVP_U_Point WorldDirectionA;
            IVP_U_Point WorldDirectionB;
            FrameA->vmult(&DirectionA, &WorldDirectionA);
            FrameB->vmult(&DirectionB, &WorldDirectionB);

            IVP_U_Point Axis;
            Axis.calc_cross_product(&WorldDirectionA, &WorldDirectionB);
            return ((double)(Axis.k[2]) * Axis.k[2] + (double)(Axis.k[1]) * Axis.k[1]) +
                   (double)(Axis.k[0]) * Axis.k[0];
        }
    };
    static_assert(sizeof(IVP_Sweep_EdgeParallel_Evaluator) == 0x34,
                  "evaluator must be retail's 0x34-byte frame local");

    // Evaluator 3, frame+0xc4, vfptr stored at :0635e67e; slot
    // LAB_0635d6d0, `dot(FrameA->vmult(P14), FrameB->vmult(P24))`.
    //   +0x04 / +0x08  :0635e5c1 / :0635e689
    //   +0x14 Direction  frame+0xd8, rewritten every loop iteration
    //   +0x24 Normal     frame+0xe8, face normal, normalized (:0635e714)
    // Another distinct retail class of the same size.
    class IVP_Sweep_EdgeFace_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point Direction;   // +0x14
        IVP_U_Point Normal;      // +0x24

        // LAB_0635d6d0: dot(FrameA->vmult(P14), FrameB->vmult(P24)), both through
        // FUN_06348580; retail's association order, returned unrounded.
        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            const IVP_U_Point *P0 = &Direction;
            const IVP_U_Point *P1 = &Normal;
            IVP_U_Point Local1, Local2;
            FrameA->vmult(P0, &Local1);
            FrameB->vmult(P1, &Local2);
            return ((double)(Local1.k[2]) * Local2.k[2] + (double)(Local1.k[1]) * Local2.k[1]) +
                   (double)(Local2.k[0]) * Local1.k[0];
        }
    };
    static_assert(sizeof(IVP_Sweep_EdgeFace_Evaluator) == 0x34,
                  "evaluator must be retail's 0x34-byte frame local");

    // ------------------------------------------------------------------------
    // The two further evaluators EstimateLedgeVertexVsLedgeEdgeContact
    // (FUN_0635f180) installs; its middle pass reuses
    // IVP_Sweep_PointPlane_Evaluator unchanged.
    //
    // DEVIATION: retail builds ONE object at frame+0x34 and rebinds its vfptr
    // per pass (PTR_LAB_0635d950 :0635f26b, PTR_LAB_0635d5b0 :0635f471,
    // PTR_LAB_0635d830 :0635f6ea). C++ cannot rebind a vptr, so the port uses
    // three locals; every field a successor reads is freshly written before
    // use, so nothing survives a rebind.
    // ------------------------------------------------------------------------

    // Pass 1's evaluator, frame+0x34, vfptr PTR_LAB_0635d950 (Evaluate = LAB_0635d950).
    // The odd one out: +0x14 is a float bias and the points start at +0x18
    // (the driver writes a scalar to evaluator +0x14 at :0635f32d).
    //   +0x14 Bias       (Ramp + CombinedRadius) * 0.5f
    //   +0x18 PointA     side A's feature vertex, A-local
    //   +0x28 PointB     side B's feature vertex, B-local
    //   +0x38 DirectionB side B's unit edge direction, B-local
    //   +0x48 MeasureB   the moment direction, B-local
    // sizeof: 0x14 base + 4 (bias) + 4*0x10 = 0x58.
    class IVP_Sweep_VertexEdgeMoment_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        float Bias;                // +0x14
        IVP_U_Point PointA;        // +0x18
        IVP_U_Point PointB;        // +0x28
        IVP_U_Point DirectionB;    // +0x38
        IVP_U_Point MeasureB;      // +0x48

        // LAB_0635d950.
        //   Delta  = FrameB->vmult4(P28) - FrameA->vmult4(P18)
        //   Moment = cross(Delta, WorldDirectionB)       // FUN_0628bed0; WorldDirectionB = vmult(P38)
        //   Limit  = Moment.real_length()           // FUN_06347e60
        //   Value  = dot(Moment, WorldMeasureB) + Float14   // WorldMeasureB = vmult(P48)
        //   return (Value < Limit) ? Value : Limit;
        // The odd one out: +0x14 is a float bias and the points start at +0x18.
        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            IVP_U_Point PositionA;
            IVP_U_Point PositionB;
            IVP_U_Point WorldDirectionB;
            IVP_U_Point WorldMeasureB;
            FrameA->vmult4(&PointA, &PositionA);
            FrameB->vmult4(&PointB, &PositionB);
            FrameB->vmult(&DirectionB, &WorldDirectionB);
            FrameB->vmult(&MeasureB, &WorldMeasureB);

            IVP_U_Point Delta;
            Delta.subtract(&PositionB, &PositionA);

            // Limit stays unrounded; Value is compared extended but returned as
            // its float copy.
            IVP_U_Point Moment;
            Moment.calc_cross_product(&Delta, &WorldDirectionB);

            const double Limit = Moment.real_length();
            const double Value = (((double)(Moment.k[2]) * WorldMeasureB.k[2] +
                                   (double)(Moment.k[1]) * WorldMeasureB.k[1]) +
                                  (double)(WorldMeasureB.k[0]) * Moment.k[0]) +
                                 Bias;
            if (Value < Limit)
            {
                return (float)(Value);
            }
            return Limit;
        }
    };
    static_assert(sizeof(IVP_Sweep_VertexEdgeMoment_Evaluator) == 0x58,
                  "evaluator must be retail's 0x58-byte frame local");

    // Pass 3's evaluator, frame+0x34, vfptr PTR_LAB_0635d830 (Evaluate = LAB_0635d830).
    //   +0x14 PositionA  side A's feature vertex, A-local
    //   +0x24 DirectionA the current fan edge's unit direction, A-local
    //   +0x34 PositionB  side B's feature vertex, B-local
    //   +0x44 DirectionB side B's unit edge direction, B-local
    // sizeof: 0x14 base + 4*0x10 = 0x54. Not IVP_Sweep_EdgeEdge_Evaluator:
    // different vtable, and no +0x54 scale field.
    class IVP_Sweep_EdgeFanEdge_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point PositionA;     // +0x14
        IVP_U_Point DirectionA;    // +0x24
        IVP_U_Point PositionB;     // +0x34
        IVP_U_Point DirectionB;    // +0x44

        // LAB_0635d830.
        //   Delta = FrameA->vmult4(P14) - FrameB->vmult4(P34)
        //   Perp  = normize(cross(cross(WorldDirectionB, Delta), WorldDirectionB))
        //   return dot(WorldDirectionA, Perp)      // WorldDirectionA/B = vmult of P24/P44
        // Perp is the unit direction from B's edge towards A's. Both crosses are
        // inlined in retail with mixed float/extended intermediates; normize
        // (FUN_06347ef0, :0635d91b) leaves a below-epsilon Perp unchanged.
        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            IVP_U_Point WorldPositionA;
            IVP_U_Point WorldDirectionA;
            IVP_U_Point WorldPositionB;
            IVP_U_Point WorldDirectionB;
            FrameA->vmult4(&PositionA, &WorldPositionA);
            FrameA->vmult(&DirectionA, &WorldDirectionA);
            FrameB->vmult4(&PositionB, &WorldPositionB);
            FrameB->vmult(&DirectionB, &WorldDirectionB);

            const double Dx = (double)(WorldPositionA.k[0]) - WorldPositionB.k[0];
            const double Dy = (double)(WorldPositionA.k[1]) - WorldPositionB.k[1];
            const double Dz = (double)(WorldPositionA.k[2]) - WorldPositionB.k[2];
            const double Bx = WorldDirectionB.k[0];
            const double By = WorldDirectionB.k[1];
            const double Bz = WorldDirectionB.k[2];

            const float Cx = (float)(Dz * By - Bz * Dy);
            const float Cy = (float)(Bz * Dx - Dz * Bx);
            const double Cz = Dy * Bx - By * Dx;

            const float T1 = (float)(By * Cx);
            const float T2 = (float)((double)(Cy) * Bx);

            IVP_U_Point Perpendicular;
            Perpendicular.k[0] = (float)((double)(Cy) * Bz - Cz * By);
            Perpendicular.k[1] = (float)(Cz * Bx - Bz * Cx);
            Perpendicular.k[2] = (float)((double)(T1) - T2);
            Perpendicular.normize();

            return ((double)(WorldDirectionA.k[2]) * Perpendicular.k[2] +
                    (double)(WorldDirectionA.k[1]) * Perpendicular.k[1]) +
                   (double)(Perpendicular.k[0]) * WorldDirectionA.k[0];
        }
    };
    static_assert(sizeof(IVP_Sweep_EdgeFanEdge_Evaluator) == 0x54,
                  "evaluator must be retail's 0x54-byte frame local");

    // ------------------------------------------------------------------------
    // FUN_06347e60, thiscall, result unrounded in ST0. The (x*x + y*y) + z*z
    // association order is retail's.
    // ------------------------------------------------------------------------
    double SweepPointLength(const IVP_U_Float_Point *Point)
    {
        return std::sqrt(((double)(Point->X) * Point->X
                          + (double)(Point->Y) * Point->Y)
                         + (double)(Point->Z) * Point->Z);
    }

    // ------------------------------------------------------------------------
    // The single evaluator EstimateBallVsBallContact (FUN_0635f020) installs:
    // vfptr PTR_LAB_0635d720 (:0635f09c), Evaluate = LAB_0635d720. Frame tiling:
    // evaluator at frame+0x10, CacheB at +0x54, CacheA at +0x5fc (0xba4 total).
    //   +0x04 / +0x08  :0635f0f4 / :0635f10f
    //   +0x14 PointA   frame+0x24, zeroed
    //   +0x24 PointB   frame+0x34, zeroed
    //   +0x34 Axis     frame+0x44, the normalized centre-to-centre direction
    // sizeof: 0x14 + 3*0x10 = 0x44. Not IVP_Sweep_PointPlane_Evaluator:
    // LAB_0635d720 reads +0x34 as an UNTRANSFORMED direction, where
    // LAB_0635d5b0 pushes it through FrameB as a point.
    // ------------------------------------------------------------------------
    class IVP_Sweep_CenterDistance_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point PointA;   // +0x14
        IVP_U_Point PointB;   // +0x24
        IVP_U_Point Axis;     // +0x34

        // LAB_0635d720.
        //   Delta     = FrameB->vmult4(P24) - FrameA->vmult4(P14)
        //   Projected = dot(Delta, P34) * 1.2f        // P34 read RAW, untransformed
        //   return (fabsf(Projected) * Projected < Delta.quad_length())
        //              ? Projected : sqrtf(Delta.quad_length());
        // i.e. min(Projected, |Delta|) without a branch on the sign: a negative
        // Projected is always returned. Unordered takes the square root. 1.2f is
        // DAT_063a5668 (9a 99 99 3f). Q is rounded to float, the rest extended.
        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            IVP_U_Point PositionA;
            IVP_U_Point PositionB;
            FrameA->vmult4(&PointA, &PositionA);
            FrameB->vmult4(&PointB, &PositionB);

            const double DeltaX = (double)(PositionB.k[0]) - PositionA.k[0];
            const double DeltaY = (double)(PositionB.k[1]) - PositionA.k[1];
            const double DeltaZ = (double)(PositionB.k[2]) - PositionA.k[2];
            const float QuadLength = (float)((DeltaY * DeltaY + DeltaZ * DeltaZ) + DeltaX * DeltaX);

            const IVP_U_Point *Direction = &Axis;
            const double Projected = ((DeltaY * Direction->k[1] + DeltaZ * Direction->k[2]) + DeltaX * Direction->k[0]) *
                                     (double)(1.2f); // DAT_063a5668
            if (std::fabs(Projected) * Projected < QuadLength)
            {
                return Projected;
            }
            return std::sqrt((double)(QuadLength));
        }
    };
    static_assert(sizeof(IVP_Sweep_CenterDistance_Evaluator) == 0x44,
                  "evaluator must be retail's 0x44-byte frame local");

    // ------------------------------------------------------------------------
    // The fan-walk evaluator EstimateLedgeVertexVsLedgeVertexContact
    // (FUN_0635e790) and EstimateBallVsLedgeVertexContact (FUN_0635ec90)
    // install for their second pass: vfptr PTR_LAB_0635d7b0 (:0635eac4,
    // :0635eebd), Evaluate = LAB_0635d7b0. Same size and offsets as PointPlane
    // and CenterDistance, but its +0x34 goes through FrameB with vmult (a
    // direction) where PointPlane uses vmult4 and CenterDistance none.
    //
    //   +0x14 PointOther  the OTHER side's feature vertex, in that side's local
    //                     frame, transformed by FrameA
    //   +0x24 PointSelf   this side's own feature vertex, transformed by FrameB
    //   +0x34 Direction   the current fan edge's unit direction, this side's
    //                     local frame, rotated by FrameB
    // Evaluate returns dot(PointSelf_world - PointOther_world, Direction_world).
    // Size pinned by FUN_0635e790's frame (evaluator at frame+0x68..0xac,
    // caches at +0xac and +0x654).
    // ------------------------------------------------------------------------
    class IVP_Sweep_VertexFanEdge_Evaluator : public IVP_Ledge_Sweep_Evaluator
    {
    public:
        IVP_U_Point PointOther;   // +0x14
        IVP_U_Point PointSelf;    // +0x24
        IVP_U_Point Direction;    // +0x34

        // LAB_0635d7b0: dot(FrameB->vmult4(P24) - FrameA->vmult4(P14),
        // FrameB->vmult(P34)) -- LAB_0635d5b0 with the difference reversed.
        // Delta extended, result unrounded.
        double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const override
        {
            IVP_U_Point PositionA;
            IVP_U_Point PositionB;
            IVP_U_Point Normal;
            FrameA->vmult4(&PointOther, &PositionA);
            FrameB->vmult4(&PointSelf, &PositionB);
            FrameB->vmult(&Direction, &Normal);

            const double DeltaX = (double)(PositionB.k[0]) - PositionA.k[0];
            const double DeltaY = (double)(PositionB.k[1]) - PositionA.k[1];
            const double DeltaZ = (double)(PositionB.k[2]) - PositionA.k[2];
            return (DeltaZ * Normal.k[2] + DeltaY * Normal.k[1]) + DeltaX * Normal.k[0];
        }
    };
    static_assert(sizeof(IVP_Sweep_VertexFanEdge_Evaluator) == 0x44,
                  "evaluator must be retail's 0x44-byte frame local");
}

// ============================================================================
// FUN_0635df00 -- IVP_Sweep_Estimator_Context::EstimateBallVsLedgeFaceContact
// Smallest of the drivers; sole caller LAB_0635fd10's ShapeTypeB == 2 arm
// (:0635fdd9).
//
// Contact-time estimate between object A treated as a POINT (its frame
// origin: the evaluator's local point is literal (0,0,0)) and one triangle
// face of B's compact ledge (anchor = the feature edge's start vertex,
// normal = the face normal, both B-local).
//
// FeatureEdge is the RAW tagged edge pointer (the dispatcher masks a separate
// temporary only to find the ledge). CacheA is seeded from SourceA directly,
// CacheB from SideB->Source -- the asymmetry is retail's. The vector-ctor
// iterator over each cache's matrix tail is a no-op and not reproduced (see
// IVP_Sweep_Frame_Cache::Init).
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateBallVsLedgeFaceContact(
    const void *FeatureEdge, IVP_NarrowPhaseCacheSlot *SourceA, FIVP_Sweep_Ledge_Side *SideB)
{
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SourceA);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SideB->Source);

    // Retail indexes SideB->Points (the already-resolved point-array base),
    // not the ledge; equal by construction.
    const IVP_Compact_Edge *const Edge = static_cast<const IVP_Compact_Edge *>(FeatureEdge);
    const IVP_U_Float_Point *const PointArray =
        static_cast<const IVP_U_Float_Point *>(SideB->Points);
    const IVP_U_Float_Point *const Vertex = PointArray + Edge->GetStartPointIndex();

    IVP_Sweep_PointPlane_Evaluator Evaluator;

    // +0x20 is left unwritten.
    Evaluator.PointA.set(0.0f, 0.0f, 0.0f);

    Evaluator.Anchor.set(Vertex->X, Vertex->Y, Vertex->Z);

    // normize's return is ignored, so a zero-area triangle leaves the normal
    // unnormalized rather than zeroed.
    IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(
        Edge, static_cast<const IVP_Compact_Ledge *>(SideB->Ledge), &Evaluator.Normal); // FUN_0635b940
    Evaluator.Normal.normize();

    // A zero ClosingBound yields an unguarded infinity, as in retail.
    Evaluator.MaxRelativeSpeed = this->ClosingBound;
    Evaluator.IMaxRelativeSpeed = 1.0f / Evaluator.MaxRelativeSpeed;

    const IVP_Ledge_Mindist *const LedgeMindist = this->Mindist;

    const float CombinedRadius = LedgeMindist->CombinedRadius; // +0x50

    // Same ramp index as FUN_0634f320 (ivp_mindist_limiter.cpp).
    const uint32_t RampIndex = (LedgeMindist->StateFlags >> 22) & 0xff;
    const float Ramp = SweepToleranceRamp(RampIndex);

    // DAT_063a5460 = 0.5f.
    const float InnerThreshold = CombinedRadius * 0.5f + SweepToleranceBase();

    // The distance known at the interval start: SeparationGap is |Delta| minus
    // the combined radius, so this restores the raw separation.
    float KnownDistance = CombinedRadius + LedgeMindist->SeparationGap;

    const float Threshold = Ramp + CombinedRadius;

    const int Hit = Evaluator.SearchContactTimeFromContact(
        Threshold, InnerThreshold,
        this->NowBase, this->NowOffset,
        this->NextBase, this->NextOffset,
        &CacheA, &CacheB, &KnownDistance,
        &this->OutEvent);

    // The dispatcher zeroed OutCode before calling (:0635fda8).
    if (Hit != 0)
    {
        this->OutCode = 0x20;
    }
}

// ============================================================================
// FUN_0635da70 -- IVP_Sweep_Estimator_Context::EstimateLedgeVertexVsLedgeFaceContact
// Called from LAB_0635fef0 (:0636002a).
//
// Two sweep tests run back to back:
//   Pass 1: the point-vs-plane test of EstimateBallVsLedgeFaceContact
//   (PTR_LAB_0635d5b0), with a real ledge VERTEX of A instead of the origin.
//   Pass 2: a second evaluator (PTR_LAB_0635da20) over the fan of edges
//   around that vertex, testing each edge DIRECTION against the face normal
//   -- which edge leaving the vertex closes fast enough before pass 1's time.
//
// THE OUT-TIME IS ALSO THE INTERVAL END: OutEvent* is seeded from Next*, and
// every solver call reads its interval end from OutEvent* and writes its
// result there, so each hit narrows the window and the function returns the
// earliest contact. Passing Next* every time would still find contacts, just
// not the earliest.
//
// THE FAN WALK is `Cursor = Cursor->GetPrevEdge()->GetOppositeEdge()`,
// terminated on `Cursor == FeatureEdgeA` AFTER the body, so the hub edge is
// processed last.
//
// .rdata constants:
//   DAT_063a5460 = 00 00 00 3f              -> 0.5f
//   DAT_063a5464 = cd cc cc 3d              -> 0.1f
//   DAT_063a53d8 = ff e6 db 2e              -> 1e-10f
//   DAT_063a53d0 = 00 00 80 3f              -> 1.0f
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateLedgeVertexVsLedgeFaceContact(
    const void *FeatureEdgeA, const void *FeatureEdgeB,
    FIVP_Sweep_Ledge_Side *SideA, FIVP_Sweep_Ledge_Side *SideB)
{
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SideA->Source);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SideB->Source);

    // The summed angular speeds (IVP_Core+0x148); B is loaded first and A is added.
    const float SpeedBound =
        CacheB.Core->AbsOmega + CacheA.Core->AbsOmega;

    this->OutEvent.time_base = this->NextBase;
    this->OutEvent.time_offset = this->NextOffset;

    const IVP_Compact_Edge *const EdgeA = static_cast<const IVP_Compact_Edge *>(FeatureEdgeA);
    const IVP_Compact_Edge *const EdgeB = static_cast<const IVP_Compact_Edge *>(FeatureEdgeB);
    const IVP_U_Float_Point *const PointsA =
        static_cast<const IVP_U_Float_Point *>(SideA->Points);
    const IVP_U_Float_Point *const PointsB =
        static_cast<const IVP_U_Float_Point *>(SideB->Points);
    const IVP_U_Float_Point *const VertexA = PointsA + EdgeA->GetStartPointIndex();
    const IVP_U_Float_Point *const VertexB = PointsB + EdgeB->GetStartPointIndex();

    // ---- Pass 1: vertex of A against the face carrying edge B ----------------
    IVP_Sweep_PointPlane_Evaluator VertexFace;

    VertexFace.PointA.set(VertexA->X, VertexA->Y, VertexA->Z);
    VertexFace.Anchor.set(VertexB->X, VertexB->Y, VertexB->Z);

    IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(
        EdgeB, static_cast<const IVP_Compact_Ledge *>(SideB->Ledge), &VertexFace.Normal); // FUN_0635b940
    VertexFace.Normal.normize();

    VertexFace.MaxRelativeSpeed = this->ClosingBound;
    VertexFace.IMaxRelativeSpeed = 1.0f / VertexFace.MaxRelativeSpeed;

    // The same scalar block as EstimateBallVsLedgeFaceContact.
    const IVP_Ledge_Mindist *const LedgeMindist = this->Mindist;
    const float CombinedRadius = LedgeMindist->CombinedRadius;
    const uint32_t RampIndex = (LedgeMindist->StateFlags >> 22) & 0xff;
    const float Ramp = SweepToleranceRamp(RampIndex);
    const float InnerThreshold = CombinedRadius * 0.5f + SweepToleranceBase();
    float KnownDistance = CombinedRadius + LedgeMindist->SeparationGap;
    const float Threshold = Ramp + CombinedRadius;

    // Interval end from OutEvent* (still equal to Next* here).
    if (VertexFace.SearchContactTimeFromContact(
            Threshold, InnerThreshold,
            this->NowBase, this->NowOffset,
            this->OutEvent.time_base, this->OutEvent.time_offset,
            &CacheA, &CacheB, &KnownDistance,
            &this->OutEvent) != 0)
    {
        this->OutCode = 0x20;
    }

    // ---- Pass 2: the edge fan around that same vertex of A -------------------
    IVP_Sweep_EdgeNormal_Evaluator EdgeNormal;

    // Retail copies all four dwords; the 4th has no reader, so copying three
    // is observably identical.
    EdgeNormal.Normal.set(&VertexFace.Normal);

    // min(Gap, Ramp), with Gap winning ties and NaN.
    const float SeparationGap = LedgeMindist->SeparationGap;
    const float GapOrRamp = !(SeparationGap > Ramp) ? SeparationGap : Ramp;

    // Rounded once at the end. The negation (FCHS) applies to the bias term
    // alone, before the multiply.
    const IVP_Core *const CoreB = SideB->Object->core;
    const double BiasTerm = -((double)(SweepToleranceBias()) * CoreB->DAngularLimitFactor);
    const float ApproachThreshold = (float)(
        BiasTerm * ((double)(CombinedRadius) * (double)(0.1f) + GapOrRamp) / Ramp);

    // The face normal, B-local -> world -> A-local, rotation only, through the
    // caches' SOURCE frames rather than any frame the solver fills later.
    IVP_U_Point WorldNormal;
    SideB->Source->WorldTransform.vmult(&EdgeNormal.Normal, &WorldNormal);

    IVP_U_Point LocalNormalA;
    SideA->Source->WorldTransform.vimult(&WorldNormal, &LocalNormalA);

    // A different speed bound from pass 1's ClosingBound.
    EdgeNormal.MaxRelativeSpeed = SpeedBound + 1e-10f;
    EdgeNormal.IMaxRelativeSpeed = 1.0f / EdgeNormal.MaxRelativeSpeed;

    // Computed AFTER pass 1, so bounded by any event time pass 1 wrote.
    const float MaxTravel = (float)(
        ((((double)(this->OutEvent.time_base) - this->NowBase) + this->OutEvent.time_offset)
         - this->NowOffset)
        * EdgeNormal.MaxRelativeSpeed);

    const IVP_Compact_Edge *Cursor = EdgeA->GetPrevEdge()->GetOppositeEdge();

    for (;;)
    {
        // The far end of this fan edge is the NEXT edge's start point.
        const IVP_U_Float_Point *const FarPoint =
            PointsA + Cursor->GetNextEdge()->GetStartPointIndex();

        // x and y are rounded to float; z's unrounded difference is what the
        // projection and squared length multiply.
        const float DeltaX = FarPoint->X - VertexA->X;
        const float DeltaY = FarPoint->Y - VertexA->Y;
        const double DeltaZExt = (double)(FarPoint->Z) - VertexA->Z;
        const float DeltaZ = (float)(DeltaZExt);

        // Association order is retail's.
        const float Projection = (float)(
            ((double)(LocalNormalA.k[2]) * DeltaZExt
             + (double)(LocalNormalA.k[1]) * DeltaY)
            + (double)(DeltaX) * LocalNormalA.k[0]);

        const float QuadLength = (float)(
            ((double)(DeltaX) * DeltaX + (double)(DeltaY) * DeltaY)
            + DeltaZExt * DeltaZExt);

        // FUN_06347df0, result kept unrounded.
        const double InverseLength = IVP_InvSqrtFloat(QuadLength);

        // The rounded copy is the solver's known distance; the compare uses
        // the unrounded product.
        const double AlignmentExt = InverseLength * Projection;
        float Alignment = (float)(AlignmentExt);

        // Strictly less OR unordered runs the sweep.
        if (!(AlignmentExt >= MaxTravel))
        {
            // Rounded deltas times the unrounded InverseLength.
            EdgeNormal.Direction.set((float)(DeltaX * InverseLength),
                                     (float)(DeltaY * InverseLength),
                                     (float)(DeltaZ * InverseLength));

            // SearchContactTime here, not ...FromContact.
            if (EdgeNormal.SearchContactTime(
                    ApproachThreshold,
                    this->NowBase, this->NowOffset,
                    this->OutEvent.time_base, this->OutEvent.time_offset,
                    0,
                    &CacheA, &CacheB, &Alignment,
                    &this->OutEvent) != 0)
            {
                this->OutCode = 0x21;
            }
        }

        if (Cursor == EdgeA)
        {
            break;
        }

        Cursor = Cursor->GetPrevEdge()->GetOppositeEdge();
    }
}

// ============================================================================
// FUN_0635e1d0 -- IVP_Sweep_Estimator_Context::EstimateLedgeEdgeVsLedgeEdgeContact
// Sole caller dispatcher LAB_0635fef0 (:0635fff4).
//
// The EDGE-versus-EDGE arm: one edge of A against one edge of B, in order
//   1. skew-line edge-edge separation (PTR_LAB_0635e080), event code 0x40;
//   2. parallel-edge guard (PTR_LAB_0635e160), measuring |dirA x dirB|^2 --
//      the quantity test 1 divides by -- event code 0x41;
//   3. four edge-direction-into-face tests (PTR_LAB_0635d6d0) over
//      {edge, opposite} on each side, event code 0x42.
//
// Unlike the vertex drivers, this one never reads CombinedRadius (+0x50) or
// SeparationGap (+0x54): the outer threshold is the RAW ramp entry, the
// inner the RAW tolerance base, and every solver call passes a NULL known
// distance. Its only matrix transform is FUN_06348580 (IVP_U_Matrix3::vmult).
//
// The out-time is again the interval end (see
// EstimateLedgeVertexVsLedgeFaceContact): all six solver calls narrow it.
//
// .rdata constants:
//   DAT_063a53d0 = 00 00 80 3f  -> 1.0f
//   DAT_063a53d4 = 00 00 00 00  -> 0.0f
//   DAT_063a53d8 = ff e6 db 2e  -> 1e-10f
//   DAT_063a5498 = 00 00 80 bf  -> -1.0f
// .data: DAT_0658aa10 == block[0x00], DAT_0658aa18 == &block[2],
// DAT_0658ab34 == block[0x49] of the tolerance block.
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateLedgeEdgeVsLedgeEdgeContact(
    const void *FeatureEdgeA, const void *FeatureEdgeB,
    FIVP_Sweep_Ledge_Side *SideA, FIVP_Sweep_Ledge_Side *SideB)
{
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SideA->Source);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SideB->Source);

    IVP_Sweep_EdgeEdge_Evaluator EdgeEdge;

    // A's core first, B's added (the reverse of FUN_0635da70's sum). Same
    // naming caveat on IVP_Core+0x148 as there.
    const float SpeedBound =
        SideA->Object->core->AbsOmega + SideB->Object->core->AbsOmega;

    this->OutEvent.time_base = this->NextBase;
    this->OutEvent.time_offset = this->NextOffset;

    // The far vertex is the NEXT edge's start point.
    const IVP_Compact_Edge *const EdgeA = static_cast<const IVP_Compact_Edge *>(FeatureEdgeA);
    const IVP_Compact_Edge *const EdgeB = static_cast<const IVP_Compact_Edge *>(FeatureEdgeB);
    const IVP_U_Float_Point *const PointsA =
        static_cast<const IVP_U_Float_Point *>(SideA->Points);
    const IVP_U_Float_Point *const PointsB =
        static_cast<const IVP_U_Float_Point *>(SideB->Points);

    const IVP_U_Float_Point *const VertexA = PointsA + EdgeA->GetStartPointIndex();
    const IVP_U_Float_Point *const FarVertexA =
        PointsA + EdgeA->GetNextEdge()->GetStartPointIndex();
    const IVP_U_Float_Point *const VertexB = PointsB + EdgeB->GetStartPointIndex();
    const IVP_U_Float_Point *const FarVertexB =
        PointsB + EdgeB->GetNextEdge()->GetStartPointIndex();

    // normize's return is ignored: a degenerate edge stays unnormalized.
    EdgeEdge.DirectionA.set(FarVertexA->X - VertexA->X,
                            FarVertexA->Y - VertexA->Y,
                            FarVertexA->Z - VertexA->Z);
    EdgeEdge.DirectionA.normize();

    EdgeEdge.DirectionB.set(FarVertexB->X - VertexB->X,
                            FarVertexB->Y - VertexB->Y,
                            FarVertexB->Z - VertexB->Z);
    EdgeEdge.DirectionB.normize();

    EdgeEdge.PositionA.set(VertexA->X, VertexA->Y, VertexA->Z);
    EdgeEdge.PositionB.set(VertexB->X, VertexB->Y, VertexB->Z);

    // No zero guard, as in the other drivers.
    EdgeEdge.MaxRelativeSpeed = this->ClosingBound;
    EdgeEdge.IMaxRelativeSpeed = 1.0f / EdgeEdge.MaxRelativeSpeed;

    // Rotation only, on the caches' SOURCE frames.
    IVP_U_Point WorldDirectionB;
    SideB->Source->WorldTransform.vmult(&EdgeEdge.DirectionB, &WorldDirectionB);

    IVP_U_Point WorldDirectionA;
    SideA->Source->WorldTransform.vmult(&EdgeEdge.DirectionA, &WorldDirectionA);

    // FUN_0628bed0: Axis = WorldDirectionA x WorldDirectionB.
    IVP_U_Point Axis;
    Axis.calc_cross_product(&WorldDirectionA, &WorldDirectionB);

    const IVP_Ledge_Mindist *const LedgeMindist = this->Mindist;

    // Retail's association order; negated whole and compared unrounded.
    // Mindist +0x60 is SeparatingNormal.
    const double NegatedAlignment =
        -(((double)(Axis.k[1]) * LedgeMindist->SeparatingNormal.y
           + (double)(Axis.k[2]) * LedgeMindist->SeparatingNormal.z)
          + (double)(Axis.k[0]) * LedgeMindist->SeparatingNormal.x);

    // SwapFlag and Scale follow the sign of dot(Axis, SeparatingNormal):
    // negative gives (0, -1.0f), non-negative or unordered (1, +1.0f). Scale
    // makes the skew-line separation positive on the side the mindist believes
    // the objects separate towards; SwapFlag orients the four face tests.
    int SwapFlag;
    if (NegatedAlignment > 0.0)
    {
        SwapFlag = 0;
        EdgeEdge.Scale = -1.0f;
    }
    else
    {
        SwapFlag = 1;
        EdgeEdge.Scale = 1.0f;
    }

    // The same ramp index, used on its own as the outer threshold.
    const uint32_t RampIndex = (LedgeMindist->StateFlags >> 22) & 0xff;
    const float Ramp = SweepToleranceRamp(RampIndex);

    if (EdgeEdge.SearchContactTimeFromContact(
            Ramp, SweepToleranceBase(),
            this->NowBase, this->NowOffset,
            this->OutEvent.time_base, this->OutEvent.time_offset,
            &CacheA, &CacheB, 0,
            &this->OutEvent) != 0)
    {
        this->OutCode = 0x40;
    }

    // ---- Test 2: the parallel-edge degeneracy guard --------------------------
    IVP_Sweep_EdgeParallel_Evaluator Parallel;

    Parallel.DirectionA.set(&EdgeEdge.DirectionA);
    Parallel.DirectionB.set(&EdgeEdge.DirectionB);

    // The core-deviation sum is doubled before the epsilon is added.
    Parallel.MaxRelativeSpeed = (SpeedBound + SpeedBound) + 1e-10f;
    Parallel.IMaxRelativeSpeed = 1.0f / Parallel.MaxRelativeSpeed;

    // Threshold pushed as the immediate 0x2edbe6ff (1e-10f).
    if (Parallel.SearchContactTime(
            1e-10f,
            this->NowBase, this->NowOffset,
            this->OutEvent.time_base, this->OutEvent.time_offset,
            0,
            &CacheA, &CacheB, 0,
            &this->OutEvent) != 0)
    {
        this->OutCode = 0x41;
    }

    // ---- Test 3: the four edge-direction-versus-face tests -------------------
    IVP_Sweep_EdgeFace_Evaluator EdgeFace;

    // The SINGLE core-deviation sum here, not test 2's doubled one.
    EdgeFace.MaxRelativeSpeed = SpeedBound + 1e-10f;
    EdgeFace.IMaxRelativeSpeed = 1.0f / EdgeFace.MaxRelativeSpeed;

    const IVP_Compact_Edge *const OppositeA = EdgeA->GetOppositeEdge();
    const IVP_Compact_Edge *const OppositeB = EdgeB->GetOppositeEdge();

    // Retail builds six four-entry tables on its frame and walks four of them
    // upward and two downward (:0635e765); they are written here in the order
    // the loop consumes them. On each iteration the direction (+0x14) belongs
    // to the FIRST cache's object and the face normal (+0x24) to the SECOND's
    // ledge, matching LAB_0635d6d0's FrameA / FrameB use.
    const int UseRawDirection[4] = { SwapFlag, 1 - SwapFlag, SwapFlag, 1 - SwapFlag };
    const IVP_U_Point *const FanDirection[4] = {
        &EdgeEdge.DirectionB, &EdgeEdge.DirectionB,
        &EdgeEdge.DirectionA, &EdgeEdge.DirectionA };
    const IVP_Compact_Edge *const FanEdge[4] = { OppositeA, EdgeA, OppositeB, EdgeB };
    FIVP_Sweep_Ledge_Side *const FanSide[4] = { SideA, SideA, SideB, SideB };
    IVP_Sweep_Frame_Cache *const FanFirstCache[4] = { &CacheB, &CacheB, &CacheA, &CacheA };
    IVP_Sweep_Frame_Cache *const FanSecondCache[4] = { &CacheA, &CacheA, &CacheB, &CacheB };

    for (int FanIndex = 0; FanIndex < 4; ++FanIndex)
    {
        const IVP_U_Point *const Direction = FanDirection[FanIndex];

        // The negation is a multiply by DAT_063a5498 (-1.0f), not a sign flip
        // (they differ on negative zero).
        if (UseRawDirection[FanIndex] != 0)
        {
            EdgeFace.Direction.set(Direction->k[0], Direction->k[1], Direction->k[2]);
        }
        else
        {
            EdgeFace.Direction.set(Direction->k[0] * -1.0f,
                                   Direction->k[1] * -1.0f,
                                   Direction->k[2] * -1.0f);
        }

        // On the first two iterations the ledge is A's while the direction is
        // B's -- that asymmetry is the point of the test.
        IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(
            FanEdge[FanIndex], static_cast<const IVP_Compact_Ledge *>(FanSide[FanIndex]->Ledge),
            &EdgeFace.Normal); // FUN_0635b940
        EdgeFace.Normal.normize();

        // Core is the FIRST cache's. Unlike FUN_0635da70's similar term,
        // nothing further is multiplied or divided in.
        const float Threshold =
            -(SweepToleranceBias() * FanFirstCache[FanIndex]->Core->DAngularLimitFactor);

        if (EdgeFace.SearchContactTime(
                Threshold,
                this->NowBase, this->NowOffset,
                this->OutEvent.time_base, this->OutEvent.time_offset,
                0,
                FanFirstCache[FanIndex], FanSecondCache[FanIndex], 0,
                &this->OutEvent) != 0)
        {
            this->OutCode = 0x42;
        }
    }
}

// ============================================================================
// FUN_0635f180 -- IVP_Sweep_Estimator_Context::EstimateLedgeVertexVsLedgeEdgeContact
// The largest driver; sole caller dispatcher LAB_0635fef0 (:06360045).
//
// One VERTEX of side A against one EDGE of side B, in three passes:
//   Pass 1, PTR_LAB_0635d950: the clamped moment test. Event code 0x30.
//   Pass 2, PTR_LAB_0635d5b0, run over B's edge and its opposite: the vertex
//     against the plane standing on that edge, perpendicular to its face.
//     Code 0x31.
//   Pass 3, PTR_LAB_0635d830, once per edge of the fan around vertex A:
//     edge-vs-edge separation against B's edge. Code 0x32.
// Each pass's interval end is narrowed by the previous one's result.
//
// Notes:
//   1. FUN_0634d8d0 (vmult4), FUN_0634d9d0 (vmult) and FUN_0634d970 (vimult)
//      take the enclosing cache object and read the matrix at this+0x1c, so
//      they are spelled `Source->WorldTransform.vmult4/vmult/vimult`. (This
//      supports reading IVP_NarrowPhaseCacheSlot as IVP_Cache_Object.)
//   2. DAT_063a56f8 is -0.3f (9a 99 99 be), not +1.2f (DAT_063a5668,
//      9a 99 99 3f).
//   3. Pass 1 passes Next* as the interval end, passes 2 and 3 OutEvent*.
//      Equal when pass 1 runs; kept as written.
//   4. The two cores are measured differently in pass 3: A uses the LIVE
//      length of its feature vertex (FUN_06347e60) times AbsOmega, B the
//      cached IVP_Core::MaxSurfaceRotSpeed (+0x14c) times AbsOmega again
//      (0635f6c7 / 0635f6d1, both B's core). Retail: B's term carries AbsOmega
//      twice. Symmetrizing would change which contact is found first.
//   5. Context +0x0c (MaxSpeedSum) is read here as pass 2's speed bound and
//      as the speed that turns pass 3's elapsed time into a distance.
//
// .rdata constants:
//   DAT_063a53d0 = 00 00 80 3f -> 1.0f
//   DAT_063a53fc = 66 66 66 3f -> 0.9f
//   DAT_063a545c = a6 9b 44 3b -> 0.003f
//   DAT_063a5460 = 00 00 00 3f -> 0.5f
//   DAT_063a56f8 = 9a 99 99 be -> -0.3f
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateLedgeVertexVsLedgeEdgeContact(
    const void *FeatureEdgeA, const void *FeatureEdgeB,
    FIVP_Sweep_Ledge_Side *SideA, FIVP_Sweep_Ledge_Side *SideB)
{
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SideA->Source);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SideB->Source);

    // Both cores are spilled and read back in pass 3.
    const IVP_Core *const CoreA = SideA->Object->core;
    const IVP_Core *const CoreB = SideB->Object->core;
    const float SpeedBound = CoreA->AbsOmega + CoreB->AbsOmega;

    this->OutEvent.time_base = this->NextBase;
    this->OutEvent.time_offset = this->NextOffset;

    const IVP_Compact_Edge *const EdgeA = static_cast<const IVP_Compact_Edge *>(FeatureEdgeA);
    const IVP_Compact_Edge *const EdgeB = static_cast<const IVP_Compact_Edge *>(FeatureEdgeB);
    const IVP_U_Float_Point *const PointsA =
        static_cast<const IVP_U_Float_Point *>(SideA->Points);
    const IVP_U_Float_Point *const PointsB =
        static_cast<const IVP_U_Float_Point *>(SideB->Points);

    const IVP_U_Float_Point *const VertexA = PointsA + EdgeA->GetStartPointIndex();
    const IVP_U_Float_Point *const VertexB = PointsB + EdgeB->GetStartPointIndex();
    const IVP_U_Float_Point *const FarVertexB =
        PointsB + EdgeB->GetNextEdge()->GetStartPointIndex();

    // ---- Pass 1: the clamped moment test -------------------------------------
    IVP_Sweep_VertexEdgeMoment_Evaluator Moment;

    Moment.PointA.set(VertexA->X, VertexA->Y, VertexA->Z);
    Moment.PointB.set(VertexB->X, VertexB->Y, VertexB->Z);

    // Unit edge direction of B, B-local.
    Moment.DirectionB.set(FarVertexB->X - VertexB->X,
                          FarVertexB->Y - VertexB->Y,
                          FarVertexB->Z - VertexB->Z);
    Moment.DirectionB.normize();

    Moment.MaxRelativeSpeed = this->ClosingBound;
    Moment.IMaxRelativeSpeed = 1.0f / Moment.MaxRelativeSpeed;

    const IVP_Ledge_Mindist *const LedgeMindist = this->Mindist;
    const float CombinedRadius = LedgeMindist->CombinedRadius;
    const uint32_t RampIndex = (LedgeMindist->StateFlags >> 22) & 0xff;
    const float Ramp = SweepToleranceRamp(RampIndex);

    // DAT_063a5460 = 0.5f.
    Moment.Bias = (Ramp + CombinedRadius) * 0.5f;

    // The moment of the world-space separation about B's world edge direction.
    IVP_U_Point WorldVertexA;
    SideA->Source->WorldTransform.vmult4(&Moment.PointA, &WorldVertexA);

    IVP_U_Point WorldVertexB;
    SideB->Source->WorldTransform.vmult4(&Moment.PointB, &WorldVertexB);

    IVP_U_Point WorldDirectionB;
    SideB->Source->WorldTransform.vmult(&Moment.DirectionB, &WorldDirectionB);

    IVP_U_Point Separation;
    Separation.set(WorldVertexB.k[0] - WorldVertexA.k[0],
                   WorldVertexB.k[1] - WorldVertexA.k[1],
                   WorldVertexB.k[2] - WorldVertexA.k[2]);

    IVP_U_Point WorldMoment;
    WorldMoment.calc_cross_product(&Separation, &WorldDirectionB); // FUN_0628bed0

    // Back into B's frame (FUN_0634d970, transposed rotation).
    SideB->Source->WorldTransform.vimult(&WorldMoment, &Moment.MeasureB);
    Moment.MeasureB.normize();

    // DAT_063a53fc = 0.9f, not the 0.5f the other drivers use. Next*, not
    // OutEvent* (note 3).
    if (Moment.SearchContactTimeFromContact(
            Ramp + CombinedRadius,
            CombinedRadius * 0.9f + SweepToleranceBase(),
            this->NowBase, this->NowOffset,
            this->NextBase, this->NextOffset,
            &CacheA, &CacheB, 0,
            &this->OutEvent) != 0)
    {
        this->OutCode = 0x30;
    }

    // ---- Pass 2: the vertex against B's two edge-side planes -----------------
    IVP_Sweep_PointPlane_Evaluator EdgePlane;

    // The speed bound is the context's +0x0c, not ClosingBound.
    EdgePlane.MaxRelativeSpeed = this->MaxSpeedSum;
    EdgePlane.IMaxRelativeSpeed = 1.0f / EdgePlane.MaxRelativeSpeed;

    EdgePlane.PointA.set(VertexA->X, VertexA->Y, VertexA->Z);

    const IVP_Compact_Edge *const PlaneEdge[2] = { EdgeB, EdgeB->GetOppositeEdge() };

    for (int PlaneIndex = 0; PlaneIndex < 2; ++PlaneIndex)
    {
        const IVP_Compact_Edge *const Edge = PlaneEdge[PlaneIndex];

        // All three points of this edge's own triangle.
        const IVP_U_Float_Point *const Cur = PointsB + Edge->GetStartPointIndex();
        const IVP_U_Float_Point *const Next =
            PointsB + Edge->GetNextEdge()->GetStartPointIndex();
        const IVP_U_Float_Point *const Prev =
            PointsB + Edge->GetPrevEdge()->GetStartPointIndex();

        // UNNORMALIZED, unlike every other direction in this family.
        IVP_U_Point EdgeVector;
        EdgeVector.set(Next->X - Cur->X, Next->Y - Cur->Y, Next->Z - Cur->Z);

        // FUN_0635b940 inlined: retail recomputes Next - Cur unrounded on the
        // x87 stack rather than reusing EdgeVector; Prev - Cur is rounded.
        const float PrevX = Prev->X - Cur->X;
        const float PrevY = Prev->Y - Cur->Y;
        const float PrevZ = Prev->Z - Cur->Z;
        const double EdgeXExt = (double)(Next->X) - Cur->X;
        const double EdgeYExt = (double)(Next->Y) - Cur->Y;
        const double EdgeZExt = (double)(Next->Z) - Cur->Z;

        IVP_U_Point FaceNormal;
        FaceNormal.set((float)(EdgeYExt * PrevZ - EdgeZExt * PrevY),
                       (float)(EdgeZExt * PrevX - EdgeXExt * PrevZ),
                       (float)(EdgeXExt * PrevY - EdgeYExt * PrevX));

        // The in-plane outward normal of this edge (FUN_0628bed0).
        EdgePlane.Normal.calc_cross_product(&EdgeVector, &FaceNormal);
        EdgePlane.Normal.normize();

        // Anchored at this edge's own start vertex.
        EdgePlane.Anchor.set(Cur->X, Cur->Y, Cur->Z);

        if (EdgePlane.SearchContactTime(
                -SweepToleranceBias(),
                this->NowBase, this->NowOffset,
                this->OutEvent.time_base, this->OutEvent.time_offset,
                0,
                &CacheA, &CacheB, 0,
                &this->OutEvent) != 0)
        {
            this->OutCode = 0x31;
        }
    }

    // ---- Pass 3: the fan of edges at vertex A against B's edge ----------------
    IVP_Sweep_EdgeFanEdge_Evaluator FanEdge;

    // Separation still unspent at the current interval end, clamped below to
    // 0.003f; the clamp tests the unrounded value and also fires on unordered.
    const float SeparationGap = LedgeMindist->SeparationGap;
    const double SpanExt =
        (double)(SeparationGap)
        - ((((double)(this->OutEvent.time_base) - this->NowBase) + this->OutEvent.time_offset)
           - this->NowOffset)
              * this->MaxSpeedSum;
    float Span = (float)(SpanExt);
    if (!(SpanExt >= (double)(0.003f)))
    {
        Span = 0.003f;
    }

    // The asymmetric speed bound (note 4); rounded once.
    const double TermA = SweepPointLength(VertexA) * CoreA->AbsOmega + CoreA->CurrentSpeed;
    const double TermB =
        (double)(CoreB->MaxSurfaceRotSpeed) * CoreB->AbsOmega + CoreB->CurrentSpeed;
    const float FanSpeedBound = (float)((TermB + TermA) / Span + SpeedBound);

    FanEdge.PositionA.set(VertexA->X, VertexA->Y, VertexA->Z);
    FanEdge.PositionB.set(VertexB->X, VertexB->Y, VertexB->Z);

    // Recomputed rather than copied from pass 1's evaluator.
    FanEdge.DirectionB.set(FarVertexB->X - VertexB->X,
                           FarVertexB->Y - VertexB->Y,
                           FarVertexB->Z - VertexB->Z);
    FanEdge.DirectionB.normize();

    FanEdge.MaxRelativeSpeed = FanSpeedBound;
    FanEdge.IMaxRelativeSpeed = 1.0f / FanSpeedBound;

    // min(Gap, Ramp) with RAMP winning ties -- the opposite tie-break from
    // FUN_0635da70's min (:0635dc69). Both as retail wrote them.
    const float FanRamp = SweepToleranceRamp((LedgeMindist->StateFlags >> 22) & 0xff);
    const float GapOrRamp = (SeparationGap >= FanRamp) ? FanRamp : SeparationGap;

    // Rounded once; the threshold is NEGATIVE (note 2).
    const float FanThreshold = (float)(
        (((double)(CoreB->DAngularLimitFactor) + CoreB->DAngularLimitFactor)
         * GapOrRamp)
        * (double)(-0.3f));

    // Same post-tested fan walk as FUN_0635da70.
    const IVP_Compact_Edge *Cursor = EdgeA->GetPrevEdge()->GetOppositeEdge();

    for (;;)
    {
        const IVP_U_Float_Point *const FarPoint =
            PointsA + Cursor->GetNextEdge()->GetStartPointIndex();

        FanEdge.DirectionA.set(FarPoint->X - VertexA->X,
                               FarPoint->Y - VertexA->Y,
                               FarPoint->Z - VertexA->Z);
        FanEdge.DirectionA.normize();

        if (FanEdge.SearchContactTime(
                FanThreshold,
                this->NowBase, this->NowOffset,
                this->OutEvent.time_base, this->OutEvent.time_offset,
                0,
                &CacheA, &CacheB, 0,
                &this->OutEvent) != 0)
        {
            this->OutCode = 0x32;
        }

        if (Cursor == EdgeA)
        {
            break;
        }

        Cursor = Cursor->GetPrevEdge()->GetOppositeEdge();
    }
}

// ============================================================================
// FUN_0635f020 -- IVP_Sweep_Estimator_Context::EstimateBallVsBallContact
// The family's base case: no feature geometry, just the two objects' frame
// origins (the evaluator's two zeroed local points go through vmult4). +0x34
// is read untransformed and holds the unit direction between the two origins
// at the interval start; the evaluator's clamp (`min(Projected, |Delta|)`)
// keeps the answer honest once the objects rotate away from it.
//
// The shared scalar block (Threshold = Ramp + CombinedRadius, InnerThreshold
// = CombinedRadius * 0.5f + ToleranceBase), but a NULL known distance
// (:0635f106); only FUN_0635da70 and FUN_0635df00 pass a real one.
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateBallVsBallContact(
    IVP_NarrowPhaseCacheSlot *SourceA, IVP_NarrowPhaseCacheSlot *SourceB)
{
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SourceA);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SourceB);

    // The single solver call below gets Next*, so the seeding only leaves a
    // usable time behind on a miss.
    this->OutEvent.time_base = this->NextBase;
    this->OutEvent.time_offset = this->NextOffset;

    IVP_Sweep_CenterDistance_Evaluator CenterDistance;

    // The 4th floats (+0x20, +0x30) are never written or read.
    CenterDistance.PointA.set(0.0f, 0.0f, 0.0f);
    CenterDistance.PointB.set(0.0f, 0.0f, 0.0f);

    // B minus A of the world-space origins ([Source+0x4c]); coincident origins
    // leave it unnormalized rather than zeroed.
    CenterDistance.Axis.set(SourceB->WorldTransform.vv.k[0] - SourceA->WorldTransform.vv.k[0],
                            SourceB->WorldTransform.vv.k[1] - SourceA->WorldTransform.vv.k[1],
                            SourceB->WorldTransform.vv.k[2] - SourceA->WorldTransform.vv.k[2]);
    CenterDistance.Axis.normize();

    CenterDistance.MaxRelativeSpeed = this->ClosingBound;
    CenterDistance.IMaxRelativeSpeed = 1.0f / CenterDistance.MaxRelativeSpeed;

    const IVP_Ledge_Mindist *const LedgeMindist = this->Mindist;
    const float CombinedRadius = LedgeMindist->CombinedRadius;
    const float Ramp = SweepToleranceRamp((LedgeMindist->StateFlags >> 22) & 0xff);

    if (CenterDistance.SearchContactTimeFromContact(
            Ramp + CombinedRadius,
            CombinedRadius * 0.5f + SweepToleranceBase(),
            this->NowBase, this->NowOffset,
            this->NextBase, this->NextOffset,
            &CacheA, &CacheB, 0,
            &this->OutEvent) != 0)
    {
        // The lowest event code in the family (0x2x ledge-face, 0x3x
        // vertex-edge, 0x4x edge-edge).
        this->OutCode = 0x10;
    }
}

// ============================================================================
// FUN_0635e790 -- IVP_Sweep_Estimator_Context::EstimateLedgeVertexVsLedgeVertexContact
// Sole caller LAB_0635fef0's (ShapeTypeA, ShapeTypeB) == (0, 0) arm: both
// sides a ledge vertex.
//
// Frame: evaluator at frame+0x24 (0x44 bytes), 17 scalar dwords at
// +0x68..0xac, CacheA at +0xac, CacheB at +0x654 (0xbfc total).
//
//   Pass 1: vertex-to-vertex distance along the NEGATED SeparatingNormal,
//   using EstimateBallVsBallContact's evaluator class (PTR_LAB_0635d720) with
//   the real vertices in place of the zeroes. Event code 0x10.
//   Pass 2: for each edge of the fan around ONE side's vertex, when that edge
//   direction stops separating it from the OTHER side's vertex
//   (PTR_LAB_0635d7b0). Event code 0x11. Run twice, once per side.
//
// Retail runs pass 2 as one loop over pairs of adjacent frame dwords indexed
// by a (0, 0) then (-4, +4) cursor pair (:0635ec5f); iteration 0 takes B as
// "self", iteration 1 takes A. Written here as a plain two-pass loop.
//
// Pass 2 uses OutEvent* as the interval end; pass 1 reads Next* (equal at
// that point), as retail writes it.
//
// .rdata constants:
//   DAT_063a5460 = 00 00 00 3f  ->  0.5f
//   DAT_063a5498 = 00 00 80 bf  -> -1.0f
//   DAT_063a53d0 = 00 00 80 3f  ->  1.0f
//   DAT_063a56f0 = 00 00 00 bf  -> -0.5f   (next to -0.3f at 063a56f8)
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateLedgeVertexVsLedgeVertexContact(
    const void *FeatureEdgeA, const void *FeatureEdgeB,
    FIVP_Sweep_Ledge_Side *SideA, FIVP_Sweep_Ledge_Side *SideB)
{
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SideA->Source);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SideB->Source);

    // Through each side's Object, not the cache's Core field.
    const IVP_Core *const CoreA = SideA->Object->core;
    const IVP_Core *const CoreB = SideB->Object->core;

    this->OutEvent.time_base = this->NextBase;
    this->OutEvent.time_offset = this->NextOffset;

    const IVP_Compact_Edge *const EdgeA = static_cast<const IVP_Compact_Edge *>(FeatureEdgeA);
    const IVP_Compact_Edge *const EdgeB = static_cast<const IVP_Compact_Edge *>(FeatureEdgeB);
    const IVP_U_Float_Point *const PointsA =
        static_cast<const IVP_U_Float_Point *>(SideA->Points);
    const IVP_U_Float_Point *const PointsB =
        static_cast<const IVP_U_Float_Point *>(SideB->Points);
    const IVP_U_Float_Point *const VertexA = PointsA + EdgeA->GetStartPointIndex();
    const IVP_U_Float_Point *const VertexB = PointsB + EdgeB->GetStartPointIndex();

    const IVP_Ledge_Mindist *const LedgeMindist = this->Mindist;

    // ---- Pass 1: the two vertices along the separating normal ---------------
    IVP_Sweep_CenterDistance_Evaluator VertexVertex;

    VertexVertex.PointA.set(VertexA->X, VertexA->Y, VertexA->Z);
    VertexVertex.PointB.set(VertexB->X, VertexB->Y, VertexB->Z);

    // Multiplied by DAT_063a5498 (-1.0f); no normalize, SeparatingNormal is
    // already unit. Evaluate reads it untransformed, so the sign decides which
    // way "closing" points.
    VertexVertex.Axis.set(LedgeMindist->SeparatingNormal.x * -1.0f,
                          LedgeMindist->SeparatingNormal.y * -1.0f,
                          LedgeMindist->SeparatingNormal.z * -1.0f);

    VertexVertex.MaxRelativeSpeed = this->ClosingBound;
    VertexVertex.IMaxRelativeSpeed = 1.0f / VertexVertex.MaxRelativeSpeed;

    // The shared scalar block, with a NULL known distance.
    const float CombinedRadius = LedgeMindist->CombinedRadius;
    const uint32_t RampIndex = (LedgeMindist->StateFlags >> 22) & 0xff;
    const float Ramp = SweepToleranceRamp(RampIndex);

    if (VertexVertex.SearchContactTimeFromContact(
            Ramp + CombinedRadius,
            CombinedRadius * 0.5f + SweepToleranceBase(),
            this->NowBase, this->NowOffset,
            this->NextBase, this->NextOffset,
            &CacheA, &CacheB, 0,
            &this->OutEvent) != 0)
    {
        this->OutCode = 0x10;
    }

    // ---- The two pass-2 speed bounds ---------------------------------------
    // MaxSpeedSum (+0x0c) is read here too. Retail's association order; one
    // rounding.
    const float Span = (float)(
        (((((double)(this->OutEvent.time_base) - this->NowBase) + this->OutEvent.time_offset)
          - this->NowOffset)
         * this->MaxSpeedSum)
        + LedgeMindist->SeparationGap);

    // FUN_06347e60: each vertex's distance from its own frame origin. TermA is
    // rounded, TermB stays unrounded for both sums below.
    const float TermA = (float)(
        SweepPointLength(VertexA) * CoreA->AbsOmega + CoreA->CurrentSpeed);
    const double TermB =
        SweepPointLength(VertexB) * CoreB->AbsOmega + CoreB->CurrentSpeed;

    // Two bounds, differing only in whose AbsOmega scales the span;
    // the CoreB-weighted one goes with the B-side pass (frame+0x9c at j == 0).
    const float SpeedForSideB = (float)(
        ((double)(Span) * CoreB->AbsOmega + TermB) + TermA);
    const float SpeedForSideA = (float)(
        ((double)(Span) * CoreA->AbsOmega + TermB) + TermA);

    // max(A, B), B winning ties; unordered gives A's.
    const float LargerRadius = (CoreB->UpperLimitRadius >= CoreA->UpperLimitRadius)
                                   ? CoreB->UpperLimitRadius
                                   : CoreA->UpperLimitRadius;

    // Retail recomputes the ramp index; the square stays unrounded.
    const float RampAgain = SweepToleranceRamp((LedgeMindist->StateFlags >> 22) & 0xff);
    const double RampSquared = (double)(RampAgain) * RampAgain;

    // min(Span^2, Ramp^2), Ramp^2 winning ties; the compare uses the unrounded
    // square, the result the rounded one.
    const double SpanSquaredExt = (double)(Span) * Span;
    const float SpanSquared = (float)(SpanSquaredExt);
    const double BoundedSquare = (SpanSquaredExt >= RampSquared) ? RampSquared
                                                                 : (double)(SpanSquared);

    // NEGATIVE on purpose: SearchContactTime marches while distance > Threshold,
    // so this keeps marching until the fan edge actually crosses.
    // -0.5f / LargerRadius is -DAngularLimitFactor (+0x48) of the larger core;
    // EstimateBallVsLedgeVertexContact reads +0x48 directly for the same value.
    const float ThresholdScale =
        (float)((BoundedSquare * (double)(-0.5f)) / LargerRadius);

    // ---- Pass 2, twice: the edge fan around each side's feature vertex ------
    for (int SideIndex = 0; SideIndex < 2; ++SideIndex)
    {
        // Iteration 0 is the B side, iteration 1 the A side.
        const IVP_Compact_Edge *const EdgeSelf = (SideIndex == 0) ? EdgeB : EdgeA;
        const IVP_U_Float_Point *const VertexSelf = (SideIndex == 0) ? VertexB : VertexA;
        const IVP_U_Float_Point *const VertexOther = (SideIndex == 0) ? VertexA : VertexB;
        const IVP_U_Float_Point *const PointsSelf = (SideIndex == 0) ? PointsB : PointsA;
        IVP_Sweep_Frame_Cache *const CacheSelf = (SideIndex == 0) ? &CacheB : &CacheA;
        IVP_Sweep_Frame_Cache *const CacheOther = (SideIndex == 0) ? &CacheA : &CacheB;
        const float SpeedBound = (SideIndex == 0) ? SpeedForSideB : SpeedForSideA;

        // Retail rebinds pass 1's evaluator vptr (:0635eac4); a separate local
        // here, every field written before use.
        IVP_Sweep_VertexFanEdge_Evaluator FanEdge;

        FanEdge.PointOther.set(VertexOther->X, VertexOther->Y, VertexOther->Z);
        FanEdge.PointSelf.set(VertexSelf->X, VertexSelf->Y, VertexSelf->Z);

        FanEdge.MaxRelativeSpeed = SpeedBound;
        FanEdge.IMaxRelativeSpeed = 1.0f / FanEdge.MaxRelativeSpeed;

        const IVP_Compact_Edge *Cursor = EdgeSelf->GetPrevEdge()->GetOppositeEdge();

        for (;;)
        {
            const IVP_U_Float_Point *const FarPoint =
                PointsSelf + Cursor->GetNextEdge()->GetStartPointIndex();

            // x is rounded; y and z are stored rounded but squared unrounded.
            const float DeltaX = FarPoint->X - VertexSelf->X;
            const double DeltaYExt = (double)(FarPoint->Y) - VertexSelf->Y;
            const double DeltaZExt = (double)(FarPoint->Z) - VertexSelf->Z;
            const float DeltaY = (float)(DeltaYExt);
            const float DeltaZ = (float)(DeltaZExt);

            // Retail's (x + z) + y association order.
            const float QuadLength = (float)(
                ((double)(DeltaX) * DeltaX + DeltaZExt * DeltaZExt)
                + DeltaYExt * DeltaYExt);

            // FUN_06347df0, returned unrounded.
            const double InverseLength = IVP_InvSqrtFloat(QuadLength);

            // Rounded deltas times the unrounded InverseLength.
            FanEdge.Direction.set((float)(DeltaX * InverseLength),
                                  (float)(DeltaY * InverseLength),
                                  (float)(DeltaZ * InverseLength));

            // `InverseLength * QuadLength` is retail's sqrt(QuadLength); kept,
            // the two are not bit-equal.
            const float Threshold = (float)((InverseLength * QuadLength) * ThresholdScale);

            // The OTHER side's cache goes first (FrameA for PointOther), this
            // side's second (FrameB for PointSelf and Direction).
            if (FanEdge.SearchContactTime(
                    Threshold,
                    this->NowBase, this->NowOffset,
                    this->OutEvent.time_base, this->OutEvent.time_offset,
                    0,
                    CacheOther, CacheSelf, 0,
                    &this->OutEvent) != 0)
            {
                this->OutCode = 0x11;
            }

            if (Cursor == EdgeSelf)
            {
                break;
            }

            Cursor = Cursor->GetPrevEdge()->GetOppositeEdge();
        }
    }
}

// ============================================================================
// FUN_0635ec90 -- IVP_Sweep_Estimator_Context::EstimateBallVsLedgeVertexContact
// Sole caller LAB_0635fd10's ShapeTypeB == 0 arm (:0635fe07).
//
// EstimateLedgeVertexVsLedgeVertexContact with side A collapsed to a point
// (its frame origin): pass 1 is the same with a zeroed +0x14 (code 0x10);
// pass 2 runs ONCE, on B only (code 0x11); the speed bound drops A's
// vertex-length term (|VertexA| = 0), so the two agree exactly.
//
// ObjectA is read only to reach ->core; SourceA is a cache source passed
// directly (as in EstimateBallVsBallContact).
//
// The threshold scale is NOT the vertex-vertex one: here it is
// -(min(Span^2, Ramp^2) * CoreB->DAngularLimitFactor), with no max and no
// division. Equal whenever B is the larger side, but a different expression.
//
// CacheA is at frame+0x60c, CacheB at frame+0x64 (the reverse of
// FUN_0635e790's layout); CacheA is still argument seven.
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateBallVsLedgeVertexContact(
    IVP_Real_Object *ObjectA, const void *FeatureEdgeB,
    IVP_NarrowPhaseCacheSlot *SourceA, FIVP_Sweep_Ledge_Side *SideB)
{
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SourceA);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SideB->Source);

    const IVP_Core *const CoreA = ObjectA->core;
    const IVP_Core *const CoreB = SideB->Object->core;

    this->OutEvent.time_base = this->NextBase;
    this->OutEvent.time_offset = this->NextOffset;

    const IVP_Compact_Edge *const EdgeB = static_cast<const IVP_Compact_Edge *>(FeatureEdgeB);
    const IVP_U_Float_Point *const PointsB =
        static_cast<const IVP_U_Float_Point *>(SideB->Points);
    const IVP_U_Float_Point *const VertexB = PointsB + EdgeB->GetStartPointIndex();

    const IVP_Ledge_Mindist *const LedgeMindist = this->Mindist;

    // ---- Pass 1: A's origin against B's feature vertex ----------------------
    IVP_Sweep_CenterDistance_Evaluator OriginVertex;

    OriginVertex.PointA.set(0.0f, 0.0f, 0.0f);
    OriginVertex.PointB.set(VertexB->X, VertexB->Y, VertexB->Z);

    OriginVertex.Axis.set(LedgeMindist->SeparatingNormal.x * -1.0f,
                          LedgeMindist->SeparatingNormal.y * -1.0f,
                          LedgeMindist->SeparatingNormal.z * -1.0f);

    OriginVertex.MaxRelativeSpeed = this->ClosingBound;
    OriginVertex.IMaxRelativeSpeed = 1.0f / OriginVertex.MaxRelativeSpeed;

    // The shared scalar block, NULL known distance.
    const float CombinedRadius = LedgeMindist->CombinedRadius;
    const float Ramp = SweepToleranceRamp((LedgeMindist->StateFlags >> 22) & 0xff);

    if (OriginVertex.SearchContactTimeFromContact(
            Ramp + CombinedRadius,
            CombinedRadius * 0.5f + SweepToleranceBase(),
            this->NowBase, this->NowOffset,
            this->NextBase, this->NextOffset,
            &CacheA, &CacheB, 0,
            &this->OutEvent) != 0)
    {
        this->OutCode = 0x10;
    }

    // ---- The pass-2 speed bound --------------------------------------------
    const float Span = (float)(
        (((((double)(this->OutEvent.time_base) - this->NowBase) + this->OutEvent.time_offset)
          - this->NowOffset)
         * this->MaxSpeedSum)
        + LedgeMindist->SeparationGap);

    // CoreA contributes only its CurrentSpeed. The whole bound stays unrounded.
    const double SpeedBoundExt =
        ((SweepPointLength(VertexB) * CoreB->AbsOmega + CoreB->CurrentSpeed)
         + (double)(Span) * CoreB->AbsOmega)
        + CoreA->CurrentSpeed;

    // Same min(Span^2, Ramp^2) as FUN_0635e790, then scaled by CoreB's
    // DAngularLimitFactor and negated.
    const float RampAgain = SweepToleranceRamp((LedgeMindist->StateFlags >> 22) & 0xff);
    const double RampSquared = (double)(RampAgain) * RampAgain;
    const double SpanSquaredExt = (double)(Span) * Span;
    const float SpanSquared = (float)(SpanSquaredExt);
    const double BoundedSquare = (SpanSquaredExt >= RampSquared) ? RampSquared
                                                                 : (double)(SpanSquared);
    const float ThresholdScale =
        (float)(-(BoundedSquare * CoreB->DAngularLimitFactor));

    // ---- Pass 2: the edge fan around B's feature vertex ---------------------
    IVP_Sweep_VertexFanEdge_Evaluator FanEdge;

    // Retail re-zeroes PointOther rather than relying on pass 1's.
    FanEdge.PointOther.set(0.0f, 0.0f, 0.0f);
    FanEdge.PointSelf.set(VertexB->X, VertexB->Y, VertexB->Z);

    // Unlike every other driver, the reciprocal divides the UNROUNDED bound.
    FanEdge.MaxRelativeSpeed = (float)(SpeedBoundExt);
    FanEdge.IMaxRelativeSpeed = (float)(1.0 / SpeedBoundExt);

    const IVP_Compact_Edge *Cursor = EdgeB->GetPrevEdge()->GetOppositeEdge();

    for (;;)
    {
        const IVP_U_Float_Point *const FarPoint =
            PointsB + Cursor->GetNextEdge()->GetStartPointIndex();

        // Same precision shape as FUN_0635e790's fan loop.
        const float DeltaX = FarPoint->X - VertexB->X;
        const double DeltaYExt = (double)(FarPoint->Y) - VertexB->Y;
        const double DeltaZExt = (double)(FarPoint->Z) - VertexB->Z;
        const float DeltaY = (float)(DeltaYExt);
        const float DeltaZ = (float)(DeltaZExt);

        const float QuadLength = (float)(
            ((double)(DeltaX) * DeltaX + DeltaZExt * DeltaZExt)
            + DeltaYExt * DeltaYExt);

        const double InverseLength = IVP_InvSqrtFloat(QuadLength); // FUN_06347df0

        FanEdge.Direction.set((float)(DeltaX * InverseLength),
                              (float)(DeltaY * InverseLength),
                              (float)(DeltaZ * InverseLength));

        const float Threshold = (float)((InverseLength * QuadLength) * ThresholdScale);

        if (FanEdge.SearchContactTime(
                Threshold,
                this->NowBase, this->NowOffset,
                this->OutEvent.time_base, this->OutEvent.time_offset,
                0,
                &CacheA, &CacheB, 0,
                &this->OutEvent) != 0)
        {
            this->OutCode = 0x11;
        }

        if (Cursor == EdgeB)
        {
            break;
        }

        Cursor = Cursor->GetPrevEdge()->GetOppositeEdge();
    }
}

// ============================================================================
// FUN_0635f8c0 -- IVP_Sweep_Estimator_Context::EstimateBallVsLedgeEdgeContact
// Sole caller LAB_0635fd10's ShapeTypeB == 1 arm (:0635fdf0).
//
// Frame: evaluator at frame+0x60 (0x58 bytes), 0x10-byte cross-product
// scratch at +0xb8, CacheB at +0xc8, CacheA at +0x670 (0xc18 total).
//
// Two passes over one ledge edge with a ball on side A:
//   Pass 1, PTR_LAB_0635d950: the MOMENT test (A's origin, B's edge start,
//     B's unit edge direction, a measure direction built in world space and
//     carried back into B's frame). Event code 0x30.
//   Pass 2, PTR_LAB_0635d5b0, over the edge and its twin: when the ball
//     crosses each triangle's side plane through that edge, normal
//     `(PNext - PCur) x FaceNormal`. Event code 0x31.
//
// Departures from the family pattern:
//   * reads no IVP_Core at all; speed bounds are Ctx->ClosingBound (pass 1)
//     and Ctx->MaxSpeedSum (pass 2);
//   * inner threshold is CombinedRadius * 0.9f (DAT_063a53fc), as in
//     FUN_0635f180, not * 0.5f;
//   * pass 2's threshold is the bare -DAT_0658ab34 (SweepToleranceBias).
//
// ObjectA is unused; kept because the dispatcher pushes the same four
// arguments as for FUN_0635ec90.
//
// FUN_0634d8d0 / FUN_0634d9d0 / FUN_0634d970 (IVP_NarrowPhaseCacheSlot::transform_slot_to_world_space,
// IVP_NarrowPhaseCacheSlot::rotate_slot_to_world_space, IVP_NarrowPhaseCacheSlot::rotate_world_to_slot_space in
// ivp_real_object.h) take the cache source and read the matrix at +0x1c;
// spelled here as Source->WorldTransform.vmult4/vmult/vimult.
// ============================================================================
void IVP_Sweep_Estimator_Context::EstimateBallVsLedgeEdgeContact(
    IVP_Real_Object * /*ObjectA*/, const void *FeatureEdgeB,
    IVP_NarrowPhaseCacheSlot *SourceA, FIVP_Sweep_Ledge_Side *SideB)
{
    IVP_Sweep_Frame_Cache CacheA;
    CacheA.Init(SourceA);

    IVP_Sweep_Frame_Cache CacheB;
    CacheB.Init(SideB->Source);

    this->OutEvent.time_base = this->NextBase;
    this->OutEvent.time_offset = this->NextOffset;

    const IVP_Compact_Edge *const EdgeB = static_cast<const IVP_Compact_Edge *>(FeatureEdgeB);
    const IVP_U_Float_Point *const PointsB =
        static_cast<const IVP_U_Float_Point *>(SideB->Points);
    const IVP_U_Float_Point *const VertexB = PointsB + EdgeB->GetStartPointIndex();
    const IVP_U_Float_Point *const VertexBNext =
        PointsB + EdgeB->GetNextEdge()->GetStartPointIndex();

    const IVP_Ledge_Mindist *const LedgeMindist = this->Mindist;

    // ---- Pass 1: the moment test -------------------------------------------
    IVP_Sweep_VertexEdgeMoment_Evaluator EdgeMoment;

    // A's own frame origin.
    EdgeMoment.PointA.set(0.0f, 0.0f, 0.0f);

    EdgeMoment.PointB.set(VertexB->X, VertexB->Y, VertexB->Z);

    EdgeMoment.DirectionB.set(VertexBNext->X - VertexB->X,
                              VertexBNext->Y - VertexB->Y,
                              VertexBNext->Z - VertexB->Z);
    EdgeMoment.DirectionB.normize();

    EdgeMoment.MaxRelativeSpeed = this->ClosingBound;
    EdgeMoment.IMaxRelativeSpeed = 1.0f / EdgeMoment.MaxRelativeSpeed;

    // LAB_0635d950 adds the bias to its moment dot product before the min
    // against the moment's own length.
    const float CombinedRadius = LedgeMindist->CombinedRadius;
    const float Ramp = SweepToleranceRamp((LedgeMindist->StateFlags >> 22) & 0xff);
    EdgeMoment.Bias = (Ramp + CombinedRadius) * 0.5f;

    IVP_U_Point WorldVertexB;
    SideB->Source->WorldTransform.vmult4(&EdgeMoment.PointB, &WorldVertexB);

    IVP_U_Point WorldDirectionB;
    SideB->Source->WorldTransform.vmult(&EdgeMoment.DirectionB, &WorldDirectionB);

    // Vertex minus the ball's world origin ([SourceA+0x4c]).
    IVP_U_Point WorldDelta;
    WorldDelta.set(WorldVertexB.k[0] - SourceA->WorldTransform.vv.k[0],
                   WorldVertexB.k[1] - SourceA->WorldTransform.vv.k[1],
                   WorldVertexB.k[2] - SourceA->WorldTransform.vv.k[2]);

    // FUN_0628bed0: cross(WorldDelta, WorldDirectionB), in that order.
    IVP_U_Point WorldMoment;
    WorldMoment.calc_cross_product(&WorldDelta, &WorldDirectionB);

    SideB->Source->WorldTransform.vimult(&WorldMoment, &EdgeMoment.MeasureB);
    EdgeMoment.MeasureB.normize();

    // Retail recomputes the ramp index here.
    const float RampAgain = SweepToleranceRamp((LedgeMindist->StateFlags >> 22) & 0xff);

    if (EdgeMoment.SearchContactTimeFromContact(
            RampAgain + CombinedRadius,
            CombinedRadius * 0.9f + SweepToleranceBase(),
            this->NowBase, this->NowOffset,
            this->NextBase, this->NextOffset,
            &CacheA, &CacheB, 0,
            &this->OutEvent) != 0)
    {
        this->OutCode = 0x30;
    }

    // ---- Pass 2: the two side planes through this edge ----------------------
    const IVP_Compact_Edge *const EdgePair[2] = { EdgeB, EdgeB->GetOppositeEdge() };

    for (int PairIndex = 0; PairIndex < 2; ++PairIndex)
    {
        const IVP_Compact_Edge *const Edge = EdgePair[PairIndex];

        IVP_Sweep_PointPlane_Evaluator SidePlane;

        SidePlane.PointA.set(0.0f, 0.0f, 0.0f);

        // Retail sets these once before the loop on the single rebound
        // evaluator; written per local here.
        SidePlane.MaxRelativeSpeed = this->MaxSpeedSum;
        SidePlane.IMaxRelativeSpeed = 1.0f / SidePlane.MaxRelativeSpeed;

        const IVP_U_Float_Point *const PointCur = PointsB + Edge->GetStartPointIndex();
        const IVP_U_Float_Point *const PointNext =
            PointsB + Edge->GetNextEdge()->GetStartPointIndex();
        const IVP_U_Float_Point *const PointPrev =
            PointsB + Edge->GetPrevEdge()->GetStartPointIndex();

        const float EdgeX = PointNext->X - PointCur->X;
        const float EdgeY = PointNext->Y - PointCur->Y;
        const float EdgeZ = PointNext->Z - PointCur->Z;

        // FUN_0635b940 inlined, on SideB->Points rather than the ledge:
        // Prev - Cur rounded, Next - Cur recomputed unrounded (not EdgeX/Y/Z).
        const float PrevX = PointPrev->X - PointCur->X;
        const float PrevY = PointPrev->Y - PointCur->Y;
        const float PrevZ = PointPrev->Z - PointCur->Z;
        const double EdgeXExt = (double)(PointNext->X) - PointCur->X;
        const double EdgeYExt = (double)(PointNext->Y) - PointCur->Y;
        const double EdgeZExt = (double)(PointNext->Z) - PointCur->Z;

        IVP_U_Point FaceNormal;
        FaceNormal.set((float)(EdgeYExt * PrevZ - EdgeZExt * PrevY),
                       (float)(EdgeZExt * PrevX - EdgeXExt * PrevZ),
                       (float)(EdgeXExt * PrevY - EdgeYExt * PrevX));

        IVP_U_Point EdgeDirection;
        EdgeDirection.set(EdgeX, EdgeY, EdgeZ);

        // The in-plane normal of the edge's side plane (FUN_0628bed0).
        SidePlane.Normal.calc_cross_product(&EdgeDirection, &FaceNormal);
        SidePlane.Normal.normize();

        SidePlane.Anchor.set(PointCur->X, PointCur->Y, PointCur->Z);

        if (SidePlane.SearchContactTime(
                -SweepToleranceBias(),
                this->NowBase, this->NowOffset,
                this->OutEvent.time_base, this->OutEvent.time_offset,
                0,
                &CacheA, &CacheB, 0,
                &this->OutEvent) != 0)
        {
            this->OutCode = 0x31;
        }
    }
}

// ============================================================================
// THE DISPATCH LAYER
//
// Four handlers and a table between IVP_Ledge_Mindist::RescheduleCollisionEvent
// (FUN_0634f320) and the drivers above; see the header for the table layout
// and why these are __cdecl.
//
// The dispatchers switch on IVP_Ledge_Mindist's ShapeType tags, which is what
// names the drivers:
//     LAB_0635fd10, on ShapeTypeB alone (side A is a ball):
//         0 -> FUN_0635ec90   ball vs ledge VERTEX
//         1 -> FUN_0635f8c0   ball vs ledge EDGE
//         2 -> FUN_0635df00   ball vs ledge FACE
//     LAB_0635fef0, on both tags:
//         (0,0) -> FUN_0635e790   vertex vs vertex
//         (0,1) -> FUN_0635f180   vertex vs edge
//         (0,2) -> FUN_0635da70   vertex vs face
//         (1,1) -> FUN_0635e1d0   edge   vs edge
// So tag 0 == vertex, 1 == edge, 2 == face, 3 == ball. (1,0), (1,2) and (2,*)
// are retail null-store traps: the mindist keeps the lower-dimensional
// feature on side A.
// ============================================================================

namespace
{
    // Retail's "cannot happen" null store, reproduced as a real null
    // dereference: an assert, not an error path.
    void SweepEstimatorUnreachable()
    {
        *reinterpret_cast<volatile int32_t *>(0) = 0;
    }

    // DAT_0658af9c, the 16-slot table FUN_06360090 fills.
    FIVP_Sweep_Estimator_Handler G_SweepEstimatorTable[16] = {};
}

// ============================================================================
// LAB_0635fd10 -- IVP_SweepEstimate_BallVsLedge
// __cdecl. Side A is a ball, side B a ledge; the arm is chosen by
// ShapeTypeB alone.
// ============================================================================
void IVP_SweepEstimate_BallVsLedge(IVP_Sweep_Estimator_Context *Ctx)
{
    IVP_Ledge_Mindist *const Mindist = Ctx->Mindist;
    IVP_Real_Object *const ObjectA = Mindist->synapse[0].l_obj;
    IVP_Real_Object *const ObjectB = Mindist->synapse[1].l_obj;
    IVP_Compact_Edge *const FeatureB = Mindist->synapse[1].edge;

    // Retail inlines ResolveNarrowPhaseCacheSlot for A and calls it for B.
    IVP_NarrowPhaseCacheSlot *const SourceA =
        ObjectA->ResolveNarrowPhaseCacheSlot();

    IVP_Compact_Ledge *const LedgeB = FeatureB->GetCompactLedge();

    // Points is already resolved to an absolute pointer, which is why the
    // drivers index it with a bare `index*0x10 + Points`.
    FIVP_Sweep_Ledge_Side SideB;
    SideB.Points = LedgeB->GetPointArray();
    SideB.Ledge = LedgeB;
    SideB.Source =
        ObjectB->ResolveNarrowPhaseCacheSlot();
    SideB.Object = ObjectB;

    // Cleared before dispatch; the drivers only write nonzero codes.
    Ctx->OutCode = 0;

    // All arms receive the UNMASKED feature edge.
    switch (Mindist->synapse[1].status)
    {
    case IVP_SYNAPSE_POINT:
        Ctx->EstimateBallVsLedgeVertexContact(ObjectA, FeatureB, SourceA, &SideB);
        break;
    case IVP_SYNAPSE_EDGE:
        Ctx->EstimateBallVsLedgeEdgeContact(ObjectA, FeatureB, SourceA, &SideB);
        break;
    case IVP_SYNAPSE_POLY:
        Ctx->EstimateBallVsLedgeFaceContact(FeatureB, SourceA, &SideB);
        break;
    default:
        SweepEstimatorUnreachable(); // :0635fdbe
        break;
    }

    // ObjectA's refcount released first.
    ObjectA->ReleaseNarrowPhaseCacheSlot(
        SourceA);
    ObjectB->ReleaseNarrowPhaseCacheSlot(
        SideB.Source);
}

// ============================================================================
// FUN_0635fe30 -- IVP_SweepEstimate_BallVsBall
// No ledge, no feature edge, no shape switch.
// ============================================================================
void IVP_SweepEstimate_BallVsBall(IVP_Sweep_Estimator_Context *Ctx)
{
    IVP_Ledge_Mindist *const Mindist = Ctx->Mindist;
    IVP_Real_Object *const ObjectA = Mindist->synapse[0].l_obj;
    IVP_Real_Object *const ObjectB = Mindist->synapse[1].l_obj;

    // Both inlined in retail.
    IVP_NarrowPhaseCacheSlot *const SourceA =
        ObjectA->ResolveNarrowPhaseCacheSlot();
    IVP_NarrowPhaseCacheSlot *const SourceB =
        ObjectB->ResolveNarrowPhaseCacheSlot();

    Ctx->OutCode = 0;

    Ctx->EstimateBallVsBallContact(SourceA, SourceB);

    // Retail inlines these as `DEC [Slot+4]`.
    ObjectA->ReleaseNarrowPhaseCacheSlot(
        SourceA);
    ObjectB->ReleaseNarrowPhaseCacheSlot(
        SourceB);
}

// ============================================================================
// LAB_0635fef0 -- IVP_SweepEstimate_LedgeVsLedge
// Both sides are ledges; which synapse is "A" is chosen at run time by
// StateFlags bit 8 (TagB is TagA with bit 0 toggled). The `& 3` is kept
// rather than `& 1` because it is what retail computes, so a StateFlags with
// bit 9 set indexes the same place as in retail.
// ============================================================================
void IVP_SweepEstimate_LedgeVsLedge(IVP_Sweep_Estimator_Context *Ctx)
{
    IVP_Ledge_Mindist *const Mindist = Ctx->Mindist;

    const uint32_t TagA = (Mindist->StateFlags >> 8) & 3;
    const uint32_t TagB = ((Mindist->StateFlags ^ 0x100) >> 8) & 3;

    IVP_Synapse_Real *const SynapseA = &Mindist->synapse[TagA];
    IVP_Synapse_Real *const SynapseB = &Mindist->synapse[TagB];

    IVP_Real_Object *const ObjectA = SynapseA->l_obj;
    IVP_Compact_Edge *const FeatureA = SynapseA->edge;

    IVP_Compact_Ledge *const LedgeA = FeatureA->GetCompactLedge();

    FIVP_Sweep_Ledge_Side SideA;
    SideA.Points = LedgeA->GetPointArray();
    SideA.Ledge = LedgeA;
    SideA.Source =
        ObjectA->ResolveNarrowPhaseCacheSlot();
    SideA.Object = ObjectA;

    IVP_Real_Object *const ObjectB = SynapseB->l_obj;
    IVP_Compact_Edge *const FeatureB = SynapseB->edge;
    IVP_Compact_Ledge *const LedgeB = FeatureB->GetCompactLedge();

    FIVP_Sweep_Ledge_Side SideB;
    SideB.Points = LedgeB->GetPointArray();
    SideB.Ledge = LedgeB;
    SideB.Source =
        ObjectB->ResolveNarrowPhaseCacheSlot();
    SideB.Object = ObjectB;

    Ctx->OutCode = 0;

    // Unhandled combinations are retail null-store traps: tag 1 on side A
    // accepts only tag 1 on B, tags 2/3 on side A are rejected.
    switch (SynapseA->status)
    {
    case IVP_SYNAPSE_POINT:
        switch (SynapseB->status)
        {
        case IVP_SYNAPSE_POINT:
            Ctx->EstimateLedgeVertexVsLedgeVertexContact(FeatureA, FeatureB, &SideA, &SideB);
            break;
        case IVP_SYNAPSE_EDGE:
            Ctx->EstimateLedgeVertexVsLedgeEdgeContact(FeatureA, FeatureB, &SideA, &SideB);
            break;
        case IVP_SYNAPSE_POLY:
            Ctx->EstimateLedgeVertexVsLedgeFaceContact(FeatureA, FeatureB, &SideA, &SideB);
            break;
        default:
            SweepEstimatorUnreachable(); // :0636000a
            break;
        }
        break;
    case IVP_SYNAPSE_EDGE:
        if (SynapseB->status == IVP_SYNAPSE_EDGE)
        {
            Ctx->EstimateLedgeEdgeVsLedgeEdgeContact(FeatureA, FeatureB, &SideA, &SideB);
        }
        else
        {
            SweepEstimatorUnreachable(); // :0635ffd1
        }
        break;
    default:
        SweepEstimatorUnreachable(); // :0635ffbb
        break;
    }

    ObjectA->ReleaseNarrowPhaseCacheSlot(
        SideA.Source);
    ObjectB->ReleaseNarrowPhaseCacheSlot(
        SideB.Source);
}

// ============================================================================
// LAB_06360080 -- IVP_SweepEstimate_Unreachable: a null store, then RET.
// ============================================================================
void IVP_SweepEstimate_Unreachable(IVP_Sweep_Estimator_Context * /*Ctx*/)
{
    SweepEstimatorUnreachable();
}

// ============================================================================
// FUN_06360090 -- IVP_InitSweepEstimatorTable
// ============================================================================
void IVP_InitSweepEstimatorTable()
{
    // REP STOSD of the trap over all sixteen slots. The last explicit store
    // (0658afd8, :063600fb) is slot 15, not a seventeenth.
    for (int Index = 0; Index < 16; ++Index)
    {
        G_SweepEstimatorTable[Index] = &IVP_SweepEstimate_Unreachable;
    }

    // Retail's store order. Eight of these eleven re-store the trap; kept to
    // show that retail writes the array exhaustively.
    G_SweepEstimatorTable[3]  = &IVP_SweepEstimate_Unreachable;  // :063600a7
    G_SweepEstimatorTable[4]  = &IVP_SweepEstimate_Unreachable;  // :063600ac
    G_SweepEstimatorTable[6]  = &IVP_SweepEstimate_Unreachable;  // :063600b1
    G_SweepEstimatorTable[7]  = &IVP_SweepEstimate_Unreachable;  // :063600b6
    G_SweepEstimatorTable[8]  = &IVP_SweepEstimate_Unreachable;  // :063600bb
    G_SweepEstimatorTable[9]  = &IVP_SweepEstimate_Unreachable;  // :063600c0
    G_SweepEstimatorTable[10] = &IVP_SweepEstimate_Unreachable;  // :063600c5
    G_SweepEstimatorTable[11] = &IVP_SweepEstimate_Unreachable;  // :063600ca

    G_SweepEstimatorTable[0]  = &IVP_SweepEstimate_LedgeVsLedge; // :063600d4
    G_SweepEstimatorTable[1]  = &IVP_SweepEstimate_LedgeVsLedge; // :063600da
    G_SweepEstimatorTable[2]  = &IVP_SweepEstimate_LedgeVsLedge; // :063600e0
    G_SweepEstimatorTable[5]  = &IVP_SweepEstimate_LedgeVsLedge; // :063600e6

    G_SweepEstimatorTable[12] = &IVP_SweepEstimate_BallVsLedge;  // :063600ec
    G_SweepEstimatorTable[13] = &IVP_SweepEstimate_BallVsLedge;  // :063600f1
    G_SweepEstimatorTable[14] = &IVP_SweepEstimate_BallVsLedge;  // :063600f6

    G_SweepEstimatorTable[15] = &IVP_SweepEstimate_BallVsBall;   // :063600fb
}

// ============================================================================
// `CALL dword ptr [TableIndex*4 + DAT_0658af9c]`, FUN_0634f320 at :0634f5ea.
//
// Unguarded, as retail: both tags are in 0..3, and the table is filled by
// FUN_06360090 from the environment ctor (:0634c5c5) before any mindist exists.
// ============================================================================
void IVP_InvokeSweepEstimator(int TableIndex, IVP_Sweep_Estimator_Context *Ctx)
{
    G_SweepEstimatorTable[TableIndex](Ctx);
}
