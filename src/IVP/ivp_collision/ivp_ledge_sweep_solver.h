// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The Csys sweep-test family (Common.dll 0x0635d5b0-0x06360090 and the
// solvers at 0x06369280-0x06369720): time-of-contact estimates between ledge
// features (vertex / edge / face) and balls, reached from
// IVP_Ledge_Mindist::RescheduleCollisionEvent (FUN_0634f320, ivp_mindist_limiter.cpp).
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
//   FUN_0628bed0 == IVP_U_Point::calc_cross_product (ivp_math.h)
//   FUN_06347e60 == real_length()
//   FUN_0634d8d0 / FUN_0634d9d0 / FUN_0634d970 == vmult4 / vmult / vimult on
//                   the matrix at this+0x1c of an IVP_NarrowPhaseCacheSlot
//                   (IVP_NarrowPhaseCacheSlot::transform_slot_to_world_space etc.); spelled here as
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

#include "../ivp_physics/ivp_core.h" // IVP_Time

class IVP_Real_Object;
class IVP_Core;
class IVP_Compact_Edge;
class IVP_Compact_Ledge;
struct IVP_U_Float_Point;
class IVP_Ledge_Mindist;
class IVP_Environment;

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
    const IVP_U_Float_Point *Points;    // +0x00 -- IVP_U_Float_Point[], 0x10 stride
    const IVP_Compact_Ledge *Ledge;     // +0x04 -- IVP_Compact_Ledge *
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
    float SurfaceRotSpeedSum;   // +0x00: both cores' MaxSurfaceRotSpeed
    float RelativeSpeed;        // +0x04: closing speed along the separating normal
    float ClosingBound;         // +0x08
    float MaxSpeedSum;          // +0x0c: SurfaceRotSpeedSum + both CurrentSpeeds + 2e-10f
    IVP_Ledge_Mindist *Mindist; // +0x10
    IVP_Environment *Env;       // +0x14
    float NowBase;              // +0x18
    float NowOffset;            // +0x1c
    float NextBase;             // +0x20
    float NextOffset;           // +0x24
    int32_t OutCode;            // +0x28
    IVP_Time OutEvent;          // +0x2c

    // FUN_0635df00, thiscall, RET 0xc. From LAB_0635fd10's ShapeTypeB == 2 arm.
    void EstimateBallVsLedgeFaceContact(const IVP_Compact_Edge *FeatureEdge,
                                        IVP_NarrowPhaseCacheSlot *SourceA,
                                        FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635da70, thiscall, RET 0x10. From LAB_0635fef0 (:0636002a).
    void EstimateLedgeVertexVsLedgeFaceContact(const IVP_Compact_Edge *FeatureEdgeA,
                                               const IVP_Compact_Edge *FeatureEdgeB,
                                               FIVP_Sweep_Ledge_Side *SideA,
                                               FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635e1d0, thiscall, RET 0x10. From LAB_0635fef0 (:0635fff4). The
    // feature edges are raw tagged compact edges.
    void EstimateLedgeEdgeVsLedgeEdgeContact(const IVP_Compact_Edge *FeatureEdgeA,
                                             const IVP_Compact_Edge *FeatureEdgeB,
                                             FIVP_Sweep_Ledge_Side *SideA,
                                             FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635f180, thiscall, RET 0x10. From LAB_0635fef0 (:06360045). The
    // only ledge-ledge driver that reads +0x0c (MaxSpeedSum).
    void EstimateLedgeVertexVsLedgeEdgeContact(const IVP_Compact_Edge *FeatureEdgeA,
                                               const IVP_Compact_Edge *FeatureEdgeB,
                                               FIVP_Sweep_Ledge_Side *SideA,
                                               FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635f020, thiscall, RET 0x8. From FUN_0635fe30 (:0635fed5). Takes
    // both cache sources directly; reads only their translations (+0x4c).
    void EstimateBallVsBallContact(IVP_NarrowPhaseCacheSlot *SourceA,
                                   IVP_NarrowPhaseCacheSlot *SourceB);

    // FUN_0635e790, thiscall, RET 0x10. From LAB_0635fef0's (0, 0) arm.
    void EstimateLedgeVertexVsLedgeVertexContact(const IVP_Compact_Edge *FeatureEdgeA,
                                                 const IVP_Compact_Edge *FeatureEdgeB,
                                                 FIVP_Sweep_Ledge_Side *SideA,
                                                 FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635ec90, thiscall, RET 0x10. From LAB_0635fd10's ShapeTypeB == 0
    // arm. ObjectA is read only for ->core; SourceA is the ball's cache slot
    // passed directly; only SideB is a side record.
    void EstimateBallVsLedgeVertexContact(IVP_Real_Object *ObjectA,
                                          const IVP_Compact_Edge *FeatureEdgeB,
                                          IVP_NarrowPhaseCacheSlot *SourceA,
                                          FIVP_Sweep_Ledge_Side *SideB);

    // FUN_0635f8c0, thiscall, RET 0x10. From LAB_0635fd10's ShapeTypeB == 1
    // arm. ObjectA is unused; kept because the dispatcher pushes the same
    // four arguments as for FUN_0635ec90.
    void EstimateBallVsLedgeEdgeContact(IVP_Real_Object *ObjectA,
                                        const IVP_Compact_Edge *FeatureEdgeB,
                                        IVP_NarrowPhaseCacheSlot *SourceA,
                                        FIVP_Sweep_Ledge_Side *SideB);
};
static_assert(sizeof(IVP_Sweep_Estimator_Context) == 0x34, "IVP_Sweep_Estimator_Context size");
static_assert(offsetof(IVP_Sweep_Estimator_Context, ClosingBound) == 0x08, "IVP_Sweep_Estimator_Context::ClosingBound");
static_assert(offsetof(IVP_Sweep_Estimator_Context, Mindist) == 0x10, "IVP_Sweep_Estimator_Context::Mindist");
static_assert(offsetof(IVP_Sweep_Estimator_Context, NowBase) == 0x18, "IVP_Sweep_Estimator_Context::NowBase");
static_assert(offsetof(IVP_Sweep_Estimator_Context, NextBase) == 0x20, "IVP_Sweep_Estimator_Context::NextBase");
static_assert(offsetof(IVP_Sweep_Estimator_Context, OutCode) == 0x28, "IVP_Sweep_Estimator_Context::OutCode");
static_assert(offsetof(IVP_Sweep_Estimator_Context, OutEvent) == 0x2c, "IVP_Sweep_Estimator_Context::OutEvent");

// ============================================================================
// The dispatch layer.
//
// IVP_Ledge_Mindist::RescheduleCollisionEvent (FUN_0634f320, ivp_mindist_limiter.cpp) builds
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
