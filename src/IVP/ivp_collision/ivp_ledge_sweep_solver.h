// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The Csys sweep-test family (Common.dll 0x0635d5b0-0x06360090 and the
// solvers at 0x06369280-0x06369720): time-of-contact estimates between ledge
// features (vertex / edge / face) and balls, reached from
// IVP_RescheduleCollisionEvent (FUN_0634f320, ivp_mindist_limiter.cpp).
// The neighbouring range 0x0635cbd0-0x0635d360 is the IVP broadphase, ported
// behaviourally in src/Common/common_physys.cpp, not here.
//
// Layers, bottom up:
//   * evaluators: nine one-slot vtables (.rdata:063a56d8..063a5700), each a
//     DISTINCT retail class with its own field layout. Evaluate takes the two
//     candidate frames, pushes the evaluator's local points through them
//     (vmult4 for positions, vmult for directions) and reduces to one scalar:
//       point-vs-plane separation       (0635d5b0, 0635d7b0, 0635d6d0, 0635da20)
//       point-vs-point, clamped to a projection (0635d720, 0635d950)
//       edge-vs-edge and its parallel guard (0635d830, 0635e080, 0635e160)
//   * solvers: IVP_Ledge_Sweep_Evaluator's three methods -- conservative
//     advancement plus regula falsi. They sit in the LCP solver's address
//     range but are not part of it.
//   * drivers: IVP_Sweep_Estimator_Context's eight Estimate* methods.
//   * dispatch: four __cdecl handlers and the 16-slot table DAT_0658af9c.
//
// "Csys::Csys" is retail's demangled name for the 0x40-byte frame whose
// vector-ctor runs over the caches; it matches IVP_U_Matrix and is a retail
// name collision with this project's own 0x30-byte Csys (common_csys.h).
//
// Shared callees:
//   FUN_063484e0 == IVP_U_Matrix::vmult4 (rows dot p + vv; NOT vimult4 --
//                   that is FUN_06348470)
//   FUN_06348580 == IVP_U_Matrix3::vmult
//   FUN_06366010 == IVP_U_Matrix3::vimult
//   FUN_06347df0(x) == 1.0f / sqrtf(x)
//   FUN_06347ef0 == IVP_U_Point::normize, but retail's returns a bool (0 when
//                   too small to normalize); every caller here ignores it
//   FUN_0628bed0 == IVP_CalcCrossProduct (ivp_math.h)
//   FUN_06347e60 == real_length()
//   FUN_0634d8d0 / FUN_0634d9d0 / FUN_0634d970 == vmult4 / vmult / vimult on
//                   the matrix at this+0x1c of an IVP_NarrowPhaseCacheSlot
//                   (IVP_TransformSlotToWorldSpace etc.); spelled here as
//                   Source->WorldTransform.<op>()
//
// Edge stepping: the `(uint)ptr & 0xc` -> DAT_063ee4e0 / DAT_063ee4f0 idiom is
// IVP_Compact_Edge::GetNextEdge / GetPrevEdge (ivp_compact_ledge.h). The
// tables are indexed in bytes by the edge's offset inside its 16-byte triangle:
//     .data:063ee4e0  { 0, +4, +4, -8 }   next: 4 -> 8, 8 -> c, c -> 4
//     .data:063ee4f0  { 0, +8, -4, -4 }   prev: 4 -> c, 8 -> 4, c -> 8

#include <cstddef>
#include <cstdint>

#include "../ivp_utility/ivp_math.h"

class IVP_Real_Object;
class IVP_Core;
class IVP_Time;

// ============================================================================
// IVP_Sweep_Frame_Cache -- port-local name (retail's is not recovered; it is
// a 0x5a8-byte stack local in every driver). Layout from its initializer
// FUN_0635de90 and the solvers' indexing:
//   +0x00  Object           receiver of the solvers' FUN_06345850 calls
//                           (IVP_Real_Object::get_m_world_f_object_at_transform)
//   +0x04  Core             Object->core (+0x98)
//   +0x08/+0x0c             never touched by the ctor or either solver
//   +0x10  LastRefreshTick  copied from Source+0x00
//   +0x14  FrameAtSlot[21]  lazily-filled pointer table; slot 0 is always
//                           valid on entry (see Init)
//   +0x68  FrameStorage[21] 21 * sizeof(IVP_U_Matrix); retail runs the vector
//                           ctor iterator over it just before Init
// ============================================================================
// IVP_NarrowPhaseCacheSlot: the library's IVP_Cache_Object (common_ivp_real_object.h).
struct IVP_NarrowPhaseCacheSlot;

struct IVP_Sweep_Frame_Cache
{
    IVP_Real_Object *Object;            // +0x00
    IVP_Core *Core;                     // +0x04
    int Field_0x08;                     // +0x08 -- purpose unknown, never written by the ctor
    int Field_0x0c;                     // +0x0c -- purpose unknown, never written by the ctor
    int32_t LastRefreshTick;            // +0x10 -- ctor: `= Source->LastRefreshTick`
    IVP_U_Matrix *FrameAtSlot[21];      // +0x14
    IVP_U_Matrix FrameStorage[21];      // +0x68

    // FUN_0635de90, thiscall. Never reads `this`, so the drivers hand it raw
    // stack memory. Source is non-const because the cache stores
    // `Source + 0x1c` into FrameAtSlot[] and aliases the source's storage.
    void Init(IVP_NarrowPhaseCacheSlot *Source);
};
static_assert(sizeof(IVP_Sweep_Frame_Cache) == 0x5a8, "IVP_Sweep_Frame_Cache size");
static_assert(offsetof(IVP_Sweep_Frame_Cache, FrameAtSlot) == 0x14, "IVP_Sweep_Frame_Cache::FrameAtSlot");
static_assert(offsetof(IVP_Sweep_Frame_Cache, FrameStorage) == 0x68, "IVP_Sweep_Frame_Cache::FrameStorage");

// ============================================================================
// IVP_Ledge_Sweep_Evaluator -- port-local name for the abstract base of the
// one-slot evaluator classes.
//   +0x00  vfptr
//   +0x04  MaxRelativeSpeed
//   +0x08  IMaxRelativeSpeed  1.0f / MaxRelativeSpeed, set by every driver;
//                             FUN_06369720 divides by +0x04, FUN_063694a0
//                             multiplies by +0x08, so both take the same step
//   +0x0c/+0x10               unidentified; derived classes start at +0x14
//
// Evaluate takes (frame of the first cache argument, frame of the second).
// ============================================================================
class IVP_Ledge_Sweep_Evaluator
{
public:
    // Deliberately NO virtual destructor: retail's evaluator vtables are one
    // slot wide and that slot is Evaluate. The evaluators are stack locals and
    // never deleted through a base pointer.

    // The one vtable slot (thiscall, RET 0x8). Every body leaves its result
    // unrounded in ST0 and the solvers compare it unrounded, hence double.
    virtual double Evaluate(const IVP_U_Matrix *FrameA, const IVP_U_Matrix *FrameB) const = 0;

    // FUN_063694a0.
    int SearchContactTime(float Threshold, float CurTimeBase, float CurTimeOffset,
                          float EndTimeBase, float EndTimeOffset, int StartSlot,
                          IVP_Sweep_Frame_Cache *CacheA, IVP_Sweep_Frame_Cache *CacheB,
                          const float *KnownDistance, IVP_Time *OutTime);

    // FUN_06369720.
    int SearchContactTimeFromContact(float Threshold, float InnerThreshold,
                                     float CurTimeBase, float CurTimeOffset,
                                     float EndTimeBase, float EndTimeOffset,
                                     IVP_Sweep_Frame_Cache *CacheA, IVP_Sweep_Frame_Cache *CacheB,
                                     const float *KnownDistance, IVP_Time *OutTime);

    // FUN_06369280.
    void RefineContactTime(IVP_Time *OutTime, float LoTimeBase, float LoTimeOffset,
                           float HiTimeBase, float HiTimeOffset, float Threshold,
                           float LoDistance, float HiDistance,
                           IVP_Real_Object *ObjectA, IVP_Real_Object *ObjectB);

    float MaxRelativeSpeed;   // +0x04
    float IMaxRelativeSpeed;  // +0x08
    int Field_0x0c;           // +0x0c -- unidentified, see comment above
    int Field_0x10;           // +0x10 -- unidentified, see comment above
};

namespace IVP_LedgeSweepEvaluatorsDeferred
{
    // LAB_0635d6d0: dot(FrameA->vmult(P14), FrameB->vmult(P24)), both through
    // FUN_06348580; retail's association order, returned unrounded.
    inline double Evaluate_0635d6d0(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                     const IVP_U_Matrix *FrameB)
    {
        const IVP_U_Point *P0 =
            reinterpret_cast<const IVP_U_Point *>(static_cast<const char *>(EvaluatorThis) + 0x14);
        const IVP_U_Point *P1 =
            reinterpret_cast<const IVP_U_Point *>(static_cast<const char *>(EvaluatorThis) + 0x24);
        IVP_U_Point Local1, Local2;
        FrameA->vmult(P0, &Local1);
        FrameB->vmult(P1, &Local2);
        return ((double)(Local1.k[2]) * Local2.k[2] + (double)(Local1.k[1]) * Local2.k[1]) +
               (double)(Local2.k[0]) * Local1.k[0];
    }

    // LAB_0635da20: P24 rotated by FrameB, re-expressed in FrameA's local
    // space (vimult), then dotted with the fixed local direction P14.
    // Retail's association order, returned unrounded.
    inline double Evaluate_0635da20(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                     const IVP_U_Matrix *FrameB)
    {
        const IVP_U_Point *P0 =
            reinterpret_cast<const IVP_U_Point *>(static_cast<const char *>(EvaluatorThis) + 0x14);
        const IVP_U_Point *P1 =
            reinterpret_cast<const IVP_U_Point *>(static_cast<const char *>(EvaluatorThis) + 0x24);
        IVP_U_Point Local1, Local2;
        FrameB->vmult(P1, &Local1);
        FrameA->vimult(&Local1, &Local2);
        return ((double)(Local2.k[1]) * P0->k[1] + (double)(Local2.k[2]) * P0->k[2]) +
               (double)(Local2.k[0]) * P0->k[0];
    }

    // ------------------------------------------------------------------------
    // The evaluator bodies are free functions taking `const void *` with
    // measured offsets because the nine are distinct retail classes with
    // different layouts (0635d950 puts a float at +0x14 and its points at
    // +0x18). The .cpp's concrete classes forward here.
    // ------------------------------------------------------------------------
    inline const IVP_U_Point *EvaluatorPoint(const void *EvaluatorThis, int Offset)
    {
        return reinterpret_cast<const IVP_U_Point *>(
            static_cast<const char *>(EvaluatorThis) + Offset);
    }

    inline float EvaluatorFloat(const void *EvaluatorThis, int Offset)
    {
        return *reinterpret_cast<const float *>(
            static_cast<const char *>(EvaluatorThis) + Offset);
    }

    // LAB_0635d5b0, the largest of the nine and installed by every driver but
    // FUN_0635e1d0. Signed distance of a point on A from a plane carried by B:
    //   Position = FrameA->vmult4(P14)              // CALL FUN_063484e0
    //   Anchor   = FrameB->vmult4(P34)              // inlined
    //   Normal   = FrameB->vmult (P24)              // inlined
    //   return dot(Position - Anchor, Normal)
    // The inlined transforms do NOT round like FUN_063484e0: Anchor x/y are
    // spilled as floats after two terms, Anchor.z stays extended to the final
    // subtraction; each Normal component has its own association order.
    inline double Evaluate_0635d5b0(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                    const IVP_U_Matrix *FrameB)
    {
        IVP_U_Point Position;
        FrameA->vmult4(EvaluatorPoint(EvaluatorThis, 0x14), &Position); // 0635d5c3

        const IVP_U_Point *P34 = EvaluatorPoint(EvaluatorThis, 0x34);
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

        const IVP_U_Point *P24 = EvaluatorPoint(EvaluatorThis, 0x24);
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

    // LAB_0635d720.
    //   Delta     = FrameB->vmult4(P24) - FrameA->vmult4(P14)
    //   Projected = dot(Delta, P34) * 1.2f        // P34 read RAW, untransformed
    //   return (fabsf(Projected) * Projected < Delta.quad_length())
    //              ? Projected : sqrtf(Delta.quad_length());
    // i.e. min(Projected, |Delta|) without a branch on the sign: a negative
    // Projected is always returned. Unordered takes the square root. 1.2f is
    // DAT_063a5668 (9a 99 99 3f). Q is rounded to float, the rest extended.
    inline double Evaluate_0635d720(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                    const IVP_U_Matrix *FrameB)
    {
        IVP_U_Point PositionA;
        IVP_U_Point PositionB;
        FrameA->vmult4(EvaluatorPoint(EvaluatorThis, 0x14), &PositionA);
        FrameB->vmult4(EvaluatorPoint(EvaluatorThis, 0x24), &PositionB);

        const double DeltaX = (double)(PositionB.k[0]) - PositionA.k[0];
        const double DeltaY = (double)(PositionB.k[1]) - PositionA.k[1];
        const double DeltaZ = (double)(PositionB.k[2]) - PositionA.k[2];
        const float QuadLength = (float)((DeltaY * DeltaY + DeltaZ * DeltaZ) + DeltaX * DeltaX);

        const IVP_U_Point *Direction = EvaluatorPoint(EvaluatorThis, 0x34);
        const double Projected = ((DeltaY * Direction->k[1] + DeltaZ * Direction->k[2]) + DeltaX * Direction->k[0]) *
                                 (double)(1.2f); // DAT_063a5668
        if (std::fabs(Projected) * Projected < QuadLength)
        {
            return Projected;
        }
        return std::sqrt((double)(QuadLength));
    }

    // LAB_0635d7b0: dot(FrameB->vmult4(P24) - FrameA->vmult4(P14),
    // FrameB->vmult(P34)) -- Evaluate_0635d5b0 with the difference reversed.
    // Delta extended, result unrounded.
    inline double Evaluate_0635d7b0(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                    const IVP_U_Matrix *FrameB)
    {
        IVP_U_Point PositionA;
        IVP_U_Point PositionB;
        IVP_U_Point Normal;
        FrameA->vmult4(EvaluatorPoint(EvaluatorThis, 0x14), &PositionA);
        FrameB->vmult4(EvaluatorPoint(EvaluatorThis, 0x24), &PositionB);
        FrameB->vmult(EvaluatorPoint(EvaluatorThis, 0x34), &Normal);

        const double DeltaX = (double)(PositionB.k[0]) - PositionA.k[0];
        const double DeltaY = (double)(PositionB.k[1]) - PositionA.k[1];
        const double DeltaZ = (double)(PositionB.k[2]) - PositionA.k[2];
        return (DeltaZ * Normal.k[2] + DeltaY * Normal.k[1]) + DeltaX * Normal.k[0];
    }

    // LAB_0635d830.
    //   Delta = FrameA->vmult4(P14) - FrameB->vmult4(P34)
    //   Perp  = normize(cross(cross(DirectionB, Delta), DirectionB))
    //   return dot(DirectionA, Perp)      // DirectionA/B = vmult of P24/P44
    // Perp is the unit direction from B's edge towards A's. Both crosses are
    // inlined in retail with mixed float/extended intermediates; normize
    // (FUN_06347ef0, :0635d91b) leaves a below-epsilon Perp unchanged.
    inline double Evaluate_0635d830(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                    const IVP_U_Matrix *FrameB)
    {
        IVP_U_Point PositionA;
        IVP_U_Point DirectionA;
        IVP_U_Point PositionB;
        IVP_U_Point DirectionB;
        FrameA->vmult4(EvaluatorPoint(EvaluatorThis, 0x14), &PositionA);
        FrameA->vmult(EvaluatorPoint(EvaluatorThis, 0x24), &DirectionA);
        FrameB->vmult4(EvaluatorPoint(EvaluatorThis, 0x34), &PositionB);
        FrameB->vmult(EvaluatorPoint(EvaluatorThis, 0x44), &DirectionB);

        const double Dx = (double)(PositionA.k[0]) - PositionB.k[0];
        const double Dy = (double)(PositionA.k[1]) - PositionB.k[1];
        const double Dz = (double)(PositionA.k[2]) - PositionB.k[2];
        const double Bx = DirectionB.k[0];
        const double By = DirectionB.k[1];
        const double Bz = DirectionB.k[2];

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

        return ((double)(DirectionA.k[2]) * Perpendicular.k[2] +
                (double)(DirectionA.k[1]) * Perpendicular.k[1]) +
               (double)(Perpendicular.k[0]) * DirectionA.k[0];
    }

    // LAB_0635d950.
    //   Delta  = FrameB->vmult4(P28) - FrameA->vmult4(P18)
    //   Moment = cross(Delta, DirectionB)       // FUN_0628bed0; DirectionB = vmult(P38)
    //   Limit  = Moment.real_length()           // FUN_06347e60
    //   Value  = dot(Moment, MeasureB) + Float14   // MeasureB = vmult(P48)
    //   return (Value < Limit) ? Value : Limit;
    // The odd one out: +0x14 is a float bias and the points start at +0x18.
    inline double Evaluate_0635d950(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                    const IVP_U_Matrix *FrameB)
    {
        IVP_U_Point PositionA;
        IVP_U_Point PositionB;
        IVP_U_Point DirectionB;
        IVP_U_Point MeasureB;
        FrameA->vmult4(EvaluatorPoint(EvaluatorThis, 0x18), &PositionA);
        FrameB->vmult4(EvaluatorPoint(EvaluatorThis, 0x28), &PositionB);
        FrameB->vmult(EvaluatorPoint(EvaluatorThis, 0x38), &DirectionB);
        FrameB->vmult(EvaluatorPoint(EvaluatorThis, 0x48), &MeasureB);

        IVP_U_Point Delta;
        Delta.subtract(&PositionB, &PositionA);

        // Limit stays unrounded; Value is compared extended but returned as
        // its float copy.
        IVP_U_Point Moment;
        IVP_CalcCrossProduct(&Moment, &Delta, &DirectionB);

        const double Limit = Moment.real_length();
        const double Value = (((double)(Moment.k[2]) * MeasureB.k[2] +
                               (double)(Moment.k[1]) * MeasureB.k[1]) +
                              (double)(MeasureB.k[0]) * Moment.k[0]) +
                             EvaluatorFloat(EvaluatorThis, 0x14);
        if (Value < Limit)
        {
            return (float)(Value);
        }
        return Limit;
    }

    // LAB_0635e080: the signed distance between two skew lines along their
    // common perpendicular, times the stored scale at +0x54.
    //   Axis = cross(DirectionA, DirectionB)      // FUN_0628bed0
    //   return (dot(Axis, PositionA) - dot(Axis, PositionB))
    //              * (1.0f / sqrtf(Axis.quad_length())) * Float54;
    // Two dots subtracted, not one dot of the difference (they differ in
    // float). Numerator and Q rounded to float, the result unrounded.
    inline double Evaluate_0635e080(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                    const IVP_U_Matrix *FrameB)
    {
        IVP_U_Point PositionA;
        IVP_U_Point DirectionA;
        IVP_U_Point PositionB;
        IVP_U_Point DirectionB;
        FrameA->vmult(EvaluatorPoint(EvaluatorThis, 0x24), &DirectionA);
        FrameA->vmult4(EvaluatorPoint(EvaluatorThis, 0x14), &PositionA);
        FrameB->vmult(EvaluatorPoint(EvaluatorThis, 0x44), &DirectionB);
        FrameB->vmult4(EvaluatorPoint(EvaluatorThis, 0x34), &PositionB);

        IVP_U_Point Axis;
        IVP_CalcCrossProduct(&Axis, &DirectionA, &DirectionB);

        const double DotA = ((double)(Axis.k[2]) * PositionA.k[2] +
                             (double)(Axis.k[1]) * PositionA.k[1]) +
                            (double)(PositionA.k[0]) * Axis.k[0];
        const double DotB = ((double)(Axis.k[2]) * PositionB.k[2] +
                             (double)(Axis.k[1]) * PositionB.k[1]) +
                            (double)(PositionB.k[0]) * Axis.k[0];
        const float Numerator = (float)(DotA - DotB);
        const float QuadLength = (float)(
            ((double)(Axis.k[1]) * Axis.k[1] + (double)(Axis.k[2]) * Axis.k[2]) +
            (double)(Axis.k[0]) * Axis.k[0]);
        return (IVP_InvSqrtFloat(QuadLength) * Numerator) * EvaluatorFloat(EvaluatorThis, 0x54);
    }

    // LAB_0635e160: |cross(FrameA->vmult(P14), FrameB->vmult(P24))|^2, the
    // parallel-edge guard for Evaluate_0635e080 (the quantity it divides by).
    // Rotation-only transforms; result unrounded.
    inline double Evaluate_0635e160(const void *EvaluatorThis, const IVP_U_Matrix *FrameA,
                                    const IVP_U_Matrix *FrameB)
    {
        IVP_U_Point DirectionA;
        IVP_U_Point DirectionB;
        FrameA->vmult(EvaluatorPoint(EvaluatorThis, 0x14), &DirectionA);
        FrameB->vmult(EvaluatorPoint(EvaluatorThis, 0x24), &DirectionB);

        IVP_U_Point Axis;
        IVP_CalcCrossProduct(&Axis, &DirectionA, &DirectionB);
        return ((double)(Axis.k[2]) * Axis.k[2] + (double)(Axis.k[1]) * Axis.k[1]) +
               (double)(Axis.k[0]) * Axis.k[0];
    }
}

// ============================================================================
// The driver layer.
// ============================================================================

// ----------------------------------------------------------------------------
// FIVP_Sweep_Ledge_Side -- port-local name. The 0x10-byte "resolved side"
// record a dispatcher builds on its frame and passes to a driver by address.
//   +0x00 Points -- `Ledge + Ledge->CPointOffset`, already resolved, hence the
//                   drivers' bare `index*0x10 + Points` vertex fetch
//   +0x04 Ledge  -- the containing compact ledge (GetCompactLedge of the
//                   feature edge)
//   +0x08 Source -- the object's cache slot (FUN_06353510), fed to
//                   IVP_Sweep_Frame_Cache::Init
//   +0x0c Object -- the object
// ----------------------------------------------------------------------------
struct FIVP_Sweep_Ledge_Side
{
    const void *Points;                 // +0x00 -- IVP_U_Float_Point[], 0x10 stride
    const void *Ledge;                  // +0x04 -- IVP_Compact_Ledge *
    IVP_NarrowPhaseCacheSlot *Source;    // +0x08
    IVP_Real_Object *Object;            // +0x0c
};
static_assert(offsetof(FIVP_Sweep_Ledge_Side, Source) == 0x08, "FIVP_Sweep_Ledge_Side::Source");
static_assert(offsetof(FIVP_Sweep_Ledge_Side, Object) == 0x0c, "FIVP_Sweep_Ledge_Side::Object");

// ----------------------------------------------------------------------------
// IVP_Sweep_Estimator_Context -- the drivers' `this`: retail's 0x34-byte
// collision-time estimator context, built by FUN_0634f320 and passed through
// `CALL [idx*4 + DAT_0658af9c]`.
//     +0x08 ClosingBound  -> evaluators' MaxRelativeSpeed
//     +0x10 Mindist       -> read at +0x14/+0x50/+0x54/+0x60
//     +0x18/+0x1c Now     -> interval start
//     +0x20/+0x24 Next    -> interval end
//     +0x28 OutCode       -> event code on a hit (0x10..0x42)
//     +0x2c/+0x30 OutEvent-> the drivers' OutTime
// ----------------------------------------------------------------------------
struct IVP_Sweep_Estimator_Context
{
    float RadiusSum;            // +0x00
    float RelativeSpeed;        // +0x04
    float ClosingBound;         // +0x08
    float MarginSpan;           // +0x0c
    void *Mindist;              // +0x10 -- IVP_Ledge_Mindist *
    void *Env;                  // +0x14 -- IVP_Environment *
    float NowBase;              // +0x18
    float NowOffset;            // +0x1c
    float NextBase;             // +0x20
    float NextOffset;           // +0x24
    int32_t OutCode;            // +0x28
    float OutEventBase;         // +0x2c
    float OutEventOffset;       // +0x30

    // FUN_0635df00, thiscall, RET 0xc. From LAB_0635fd10's ShapeTypeB == 2 arm.
    void EstimateBallVsLedgeFaceContact(const void *FeatureEdge,
                                        IVP_NarrowPhaseCacheSlot *SourceA,
                                        FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635da70, thiscall, RET 0x10. From LAB_0635fef0 (:0636002a).
    void EstimateLedgeVertexVsLedgeFaceContact(const void *FeatureEdgeA,
                                               const void *FeatureEdgeB,
                                               FIVP_Sweep_Ledge_Side *SideA,
                                               FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635e1d0, thiscall, RET 0x10. From LAB_0635fef0 (:0635fff4). The
    // feature edges are raw tagged compact edges.
    void EstimateLedgeEdgeVsLedgeEdgeContact(const void *FeatureEdgeA,
                                             const void *FeatureEdgeB,
                                             FIVP_Sweep_Ledge_Side *SideA,
                                             FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635f180, thiscall, RET 0x10. From LAB_0635fef0 (:06360045). The
    // only ledge-ledge driver that reads +0x0c (MarginSpan).
    void EstimateLedgeVertexVsLedgeEdgeContact(const void *FeatureEdgeA,
                                               const void *FeatureEdgeB,
                                               FIVP_Sweep_Ledge_Side *SideA,
                                               FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635f020, thiscall, RET 0x8. From FUN_0635fe30 (:0635fed5). Takes
    // both cache sources directly; reads only their translations (+0x4c).
    void EstimateBallVsBallContact(IVP_NarrowPhaseCacheSlot *SourceA,
                                   IVP_NarrowPhaseCacheSlot *SourceB);

    // FUN_0635e790, thiscall, RET 0x10. From LAB_0635fef0's (0, 0) arm.
    void EstimateLedgeVertexVsLedgeVertexContact(const void *FeatureEdgeA,
                                                 const void *FeatureEdgeB,
                                                 FIVP_Sweep_Ledge_Side *SideA,
                                                 FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635ec90, thiscall, RET 0x10. From LAB_0635fd10's ShapeTypeB == 0
    // arm. ObjectA is read only for ->core; SourceA is the ball's cache slot
    // passed directly; only SideB is a side record.
    void EstimateBallVsLedgeVertexContact(IVP_Real_Object *ObjectA,
                                          const void *FeatureEdgeB,
                                          IVP_NarrowPhaseCacheSlot *SourceA,
                                          FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635f8c0, thiscall, RET 0x10. From LAB_0635fd10's ShapeTypeB == 1
    // arm. ObjectA is unused; kept because the dispatcher pushes the same
    // four arguments as for FUN_0635ec90.
    void EstimateBallVsLedgeEdgeContact(IVP_Real_Object *ObjectA,
                                        const void *FeatureEdgeB,
                                        IVP_NarrowPhaseCacheSlot *SourceA,
                                        FIVP_Sweep_Ledge_Side *SideB);
};
static_assert(sizeof(IVP_Sweep_Estimator_Context) == 0x34, "IVP_Sweep_Estimator_Context size");
static_assert(offsetof(IVP_Sweep_Estimator_Context, ClosingBound) == 0x08, "IVP_Sweep_Estimator_Context::ClosingBound");
static_assert(offsetof(IVP_Sweep_Estimator_Context, Mindist) == 0x10, "IVP_Sweep_Estimator_Context::Mindist");
static_assert(offsetof(IVP_Sweep_Estimator_Context, NowBase) == 0x18, "IVP_Sweep_Estimator_Context::NowBase");
static_assert(offsetof(IVP_Sweep_Estimator_Context, NextBase) == 0x20, "IVP_Sweep_Estimator_Context::NextBase");
static_assert(offsetof(IVP_Sweep_Estimator_Context, OutCode) == 0x28, "IVP_Sweep_Estimator_Context::OutCode");
static_assert(offsetof(IVP_Sweep_Estimator_Context, OutEventBase) == 0x2c, "IVP_Sweep_Estimator_Context::OutEventBase");

// ============================================================================
// The dispatch layer.
//
// IVP_RescheduleCollisionEvent (FUN_0634f320, ivp_mindist_limiter.cpp) builds
// the 0x34-byte context and calls `[Index*4 + DAT_0658af9c]` with
// `Index = ShapeType[j] + ShapeType[i] * 4` (:0634f5ea).
//
// All four handlers are __cdecl with one stack argument and a plain `RET`;
// each loads ECX for its thiscall driver from that argument.
//
// THE TABLE (FUN_06360090): REP STOSD of the trap over all sixteen slots,
// then eleven explicit stores (eight re-storing the trap, kept anyway):
//     slots 0, 1, 2, 5    -> LAB_0635fef0   (ledge-vs-ledge)
//     slots 12, 13, 14    -> LAB_0635fd10   (ball-vs-ledge)
//     slot 15             -> FUN_0635fe30   (ball-vs-ball)
//     the other eight     -> LAB_06360080   (the trap)
// With Index = ShapeB + ShapeA*4 that reads: ShapeA in {0,1} (ledge vertex or
// ledge edge) paired with ShapeB in {0,1,2} goes to the ledge-vs-ledge
// handler; ShapeA == 3 (ball) with ShapeB in {0,1,2} goes to ball-vs-ledge;
// (3,3) goes to ball-vs-ball. Slots 3, 7, 11 are "ShapeB == 3 with a non-ball
// A", which retail never generates because it normalizes the ball onto side A.
// The table is sixteen dwords, 0658af9c..0658afd8 (the last store is slot 15).
// ivp_environment.cpp's IVP_InitSimUnitDispatchTable forwards to
// IVP_InitSweepEstimatorTable.
// ============================================================================

typedef void (*FIVP_Sweep_Estimator_Handler)(IVP_Sweep_Estimator_Context *Ctx);

// LAB_0635fd10.
void IVP_SweepEstimate_BallVsLedge(IVP_Sweep_Estimator_Context *Ctx);

// FUN_0635fe30.
void IVP_SweepEstimate_BallVsBall(IVP_Sweep_Estimator_Context *Ctx);

// LAB_0635fef0.
void IVP_SweepEstimate_LedgeVsLedge(IVP_Sweep_Estimator_Context *Ctx);

// LAB_06360080: a deliberate null store, asserting that this shape-pair
// index is never dispatched.
void IVP_SweepEstimate_Unreachable(IVP_Sweep_Estimator_Context *Ctx);

// FUN_06360090. Fills the 16-slot table.
void IVP_InitSweepEstimatorTable();

// `CALL dword ptr [Index*4 + DAT_0658af9c]`, FUN_0634f320 at :0634f5ea.
void IVP_InvokeSweepEstimator(int TableIndex, IVP_Sweep_Estimator_Context *Ctx);
