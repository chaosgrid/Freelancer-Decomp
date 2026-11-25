// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_Mindist_Minimize_Solver -- see ivp_mindist_minimize.h. Retail's x87
// rounding points are kept: values held on the FPU stack are double, every
// float store is a float.

#include "ivp_mindist_minimize.h"

#include <cmath>
#include <cstring>

#include "ivp_compact_ledge_solver.h"
#include "../ivp_compact_builder/ivp_compact_ledge.h"
#include "../ivp_physics/ivp_mindist.h"
#include "../ivp_physics/ivp_real_object.h"
#include "../../include/common_ini.h" // Vector (IVP_Ledge_Mindist::SeparatingNormal)
#include "../ivp_utility/ivp_core_trap.h" // IVP_CoreTrap: retail's `MOV dword ptr [0],0`

IVP_MRC_TYPE (*IVP_Mindist_Minimize_Solver::mms_function_table[4][4])(IVP_Mindist_Minimize_Solver* Solver) = {};

namespace
{
    uint32_t IVP_MinimizeFloatBits(float Value)
    {
        uint32_t Bits;
        std::memcpy(&Bits, &Value, sizeof(Bits));
        return Bits;
    }

    // 0x60ad78ec: the "no candidate yet" start of the minimum searches.
    constexpr float MinimizeSearchStart = 1.0e20f;
}

// ---------------------------------------------------------------------------
// FUN_06353940: REP STOSD fills all 16 slots of
// DAT_0658ab4c with LAB_06353930, then [status0][status1]:
//   row 0 (point):    default_poly_poly x3, swapped_B_POLY
//   row 1 (edge):     swapped_poly_poly, default_poly_poly, illegal, swapped_B_POLY
//   row 2 (triangle): swapped_poly_poly, illegal, default_poly_poly, swapped_B_POLY
//   row 3 (ball):     B_POLY x3, BB
// ---------------------------------------------------------------------------
void IVP_Mindist_Minimize_Solver::init_mms_function_table()
{
    for (int32_t I = 0; I < 4; ++I)
    {
        for (int32_t J = 0; J < 4; ++J)
            mms_function_table[I][J] = &minimize_illegal;
    }

    mms_function_table[0][0] = &minimize_default_poly_poly; // DAT_0658ab4c
    mms_function_table[0][1] = &minimize_default_poly_poly; // ab50
    mms_function_table[0][2] = &minimize_default_poly_poly; // ab54
    mms_function_table[0][3] = &minimize_swapped_B_POLY;    // ab58
    mms_function_table[1][0] = &minimize_swapped_poly_poly; // ab5c
    mms_function_table[1][1] = &minimize_default_poly_poly; // ab60
    mms_function_table[1][2] = &minimize_illegal;           // ab64
    mms_function_table[1][3] = &minimize_swapped_B_POLY;    // ab68
    mms_function_table[2][0] = &minimize_swapped_poly_poly; // ab6c
    mms_function_table[2][1] = &minimize_illegal;           // ab70
    mms_function_table[2][2] = &minimize_default_poly_poly; // ab74
    mms_function_table[2][3] = &minimize_swapped_B_POLY;    // ab78
    mms_function_table[3][0] = &minimize_B_POLY;            // ab7c
    mms_function_table[3][1] = &minimize_B_POLY;            // ab80
    mms_function_table[3][2] = &minimize_B_POLY;            // ab84
    mms_function_table[3][3] = &minimize_BB;                // ab88
}

// ---------------------------------------------------------------------------
// FUN_06352a90, thiscall, RET 0x10. Keys are status | edge pointer, larger
// (signed) first. A known pair or a full table (0x100) returns 1; otherwise
// the pair is recorded and 0 returned.
// ---------------------------------------------------------------------------
int32_t IVP_Mindist_Minimize_Solver::check_loop_hash(int32_t Status0, const IVP_Compact_Edge* Edge0, int32_t Status1,
                                                     const IVP_Compact_Edge* Edge1)
{
    int32_t Key0 = Status0 | static_cast<int32_t>(reinterpret_cast<intptr_t>(Edge0));
    int32_t Key1 = Status1 | static_cast<int32_t>(reinterpret_cast<intptr_t>(Edge1));
    if (Key0 < Key1)
    {
        const int32_t Swap = Key0;
        Key0 = Key1;
        Key1 = Swap;
    }

    for (int32_t I = loop_hash_size - 1; I >= 0; --I)
    {
        if (loop_hash[I].Key0 == Key0 && loop_hash[I].Key1 == Key1)
            return 1;
    }
    if (loop_hash_size >= 0x100)
        return 1;

    loop_hash[loop_hash_size].Key0 = Key0;
    loop_hash[loop_hash_size].Key1 = Key1;
    ++loop_hash_size;
    return 0;
}

// ---------------------------------------------------------------------------
// FUN_06352b30, thiscall. Sorted synapse 0 if its status is 5 (backside),
// else synapse 1: edge = FUN_06353840(edge, &BacksidePosition), status = 2.
// ---------------------------------------------------------------------------
void IVP_Mindist_Minimize_Solver::proove_polypoly()
{
    IVP_Ledge_Mindist* const Mindist = mindist;
    IVP_Synapse_Real* Synapse = Mindist->get_sorted_synapse(0);
    if (Synapse->status != IVP_ST_BACKSIDE)
        Synapse = Mindist->get_sorted_synapse(1);
    Synapse->edge = IVP_WalkPiercingTriangleChain(Synapse->edge, &BacksidePosition);
    Synapse->status = IVP_SYNAPSE_POLY;
}

// ---------------------------------------------------------------------------
// FUN_06353370, cdecl. Caches for the sorted synapses (resolved 0 then 1),
// then by status1 + status0 * 4: 0 PP 06364890, 1 PK 063651c0, 2 PF 06363350,
// 5 KK 06363ab0, everything else FF 06352e40; both cache refcounts dropped.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::minimize_default_poly_poly(IVP_Mindist_Minimize_Solver* Solver)
{
    IVP_Ledge_Mindist* const Mindist = Solver->mindist;
    IVP_Synapse_Real* const Synapse0 = Mindist->get_sorted_synapse(0);
    IVP_Synapse_Real* const Synapse1 = Mindist->get_sorted_synapse(1);
    IVP_Compact_Edge* const Edge0 = Synapse0->edge;
    IVP_Compact_Edge* const Edge1 = Synapse1->edge;

    IVP_Cache_Ledge_Point Cache0;
    Cache0.compact_ledge = Edge0->GetCompactLedge();
    Cache0.cache_object = Synapse0->l_obj->ResolveNarrowPhaseCacheSlot(); // FUN_06353510
    Cache0.object = Synapse0->l_obj;
    Cache0.compact_poly_points = Cache0.compact_ledge->GetPointArray();
    Cache0.synapse = Synapse0;

    IVP_Cache_Ledge_Point Cache1;
    Cache1.compact_ledge = Edge1->GetCompactLedge();
    Cache1.cache_object = Synapse1->l_obj->ResolveNarrowPhaseCacheSlot();
    Cache1.object = Synapse1->l_obj;
    Cache1.compact_poly_points = Cache1.compact_ledge->GetPointArray();
    Cache1.synapse = Synapse1;

    IVP_MRC_TYPE Result;
    switch (static_cast<uint32_t>(Synapse1->status + Synapse0->status * 4))
    {
    case 0:
        Result = Solver->p_minimize_PP(Edge0, Edge1, &Cache0, &Cache1);
        break;
    case 1:
        Result = Solver->p_minimize_PK(Edge0, Edge1, &Cache0, &Cache1);
        break;
    case 2:
        Result = Solver->p_minimize_PF(Edge0, Edge1, &Cache0, &Cache1);
        break;
    case 5:
        Result = Solver->p_minimize_KK(Edge0, Edge1, &Cache0, &Cache1);
        break;
    default: // 3, 4 and above 5 (switchdataD_063534f0 / JA caseD_3)
        Result = Solver->p_minimize_FF(Edge0, Edge1, &Cache0, &Cache1);
        break;
    }

    --Cache0.cache_object->RefCount;
    --Cache1.cache_object->RefCount;
    return Result;
}

// ---------------------------------------------------------------------------
// FUN_06353560, cdecl. The ball is always PHYSICAL synapse 0, the polygon
// physical synapse 1 (FUN_06353510 inlined for the ball). By the polygon
// status: 0 BP 063645d0, 1 BK 06364ce0, 2 BF 06363430, else 0 + CORE trap.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::minimize_B_POLY(IVP_Mindist_Minimize_Solver* Solver)
{
    IVP_Ledge_Mindist* const Mindist = Solver->mindist;

    IVP_Cache_Ball Ball;
    Ball.object = Mindist->synapse[0].l_obj;
    Ball.cache_object = Ball.object->ResolveNarrowPhaseCacheSlot();
    Ball.synapse = &Mindist->synapse[0];

    IVP_Synapse_Real* const SynapseP = &Mindist->synapse[1];
    IVP_Compact_Edge* const Edge = SynapseP->edge;
    IVP_Cache_Ledge_Point CacheP;
    CacheP.compact_ledge = Edge->GetCompactLedge();
    CacheP.cache_object = SynapseP->l_obj->ResolveNarrowPhaseCacheSlot();
    CacheP.object = SynapseP->l_obj;
    CacheP.compact_poly_points = CacheP.compact_ledge->GetPointArray();
    CacheP.synapse = SynapseP;

    IVP_MRC_TYPE Result;
    switch (SynapseP->status)
    {
    case 0:
        Result = Solver->p_minimize_BP(&Ball, Edge, &CacheP);
        break;
    case 1:
        Result = Solver->p_minimize_BK(&Ball, Edge, &CacheP);
        break;
    case 2:
        Result = Solver->p_minimize_BF(&Ball, Edge, &CacheP);
        break;
    default:
        Result = IVP_MRC_UNINITIALIZED;
        IVP_CoreTrap();
        break;
    }

    --CacheP.cache_object->RefCount;
    --Ball.cache_object->RefCount;
    return Result;
}

// LAB_06353680: CORE first, then flags ^= 0x100 and minimize_B_POLY.
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::minimize_swapped_B_POLY(IVP_Mindist_Minimize_Solver* Solver)
{
    IVP_CoreTrap();
    Solver->mindist->StateFlags ^= 0x100u;
    return minimize_B_POLY(Solver);
}

// LAB_063536b0: flags ^= 0x100, then minimize_default_poly_poly.
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::minimize_swapped_poly_poly(IVP_Mindist_Minimize_Solver* Solver)
{
    Solver->mindist->StateFlags ^= 0x100u;
    return minimize_default_poly_poly(Solver);
}

// LAB_06353930: CORE, return 5.
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::minimize_illegal(IVP_Mindist_Minimize_Solver* /*Solver*/)
{
    IVP_CoreTrap();
    return IVP_MRC_ILLEGAL;
}

// ---------------------------------------------------------------------------
// FUN_063536d0, cdecl: ball vs ball. FUN_06353510 inlined twice (physical
// order). The normal is the rounded centre difference, scaled by
// 1/sqrt(q) unless |q| <= 1e-10f or unordered (then 1.0f); the gap is
// k*q - CombinedRadius. Returns OK.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::minimize_BB(IVP_Mindist_Minimize_Solver* Solver)
{
    IVP_Ledge_Mindist* const Mindist = Solver->mindist;
    IVP_NarrowPhaseCacheSlot* const Cache0 = Mindist->synapse[0].l_obj->ResolveNarrowPhaseCacheSlot();
    IVP_NarrowPhaseCacheSlot* const Cache1 = Mindist->synapse[1].l_obj->ResolveNarrowPhaseCacheSlot();

    Vector& Normal = Mindist->SeparatingNormal;
    Normal.y = (float)((double)(Cache0->WorldTransform.vv.k[1]) - Cache1->WorldTransform.vv.k[1]);
    Normal.z = (float)((double)(Cache0->WorldTransform.vv.k[2]) - Cache1->WorldTransform.vv.k[2]);
    Normal.x = (float)((double)(Cache0->WorldTransform.vv.k[0]) - Cache1->WorldTransform.vv.k[0]);

    const double QuadLength = ((double)(Normal.x) * Normal.x + (double)(Normal.y) * Normal.y) +
                              (double)(Normal.z) * Normal.z;
    double Factor;
    if (std::fabs(QuadLength) > (double)(1.0e-10f)) // DAT_063a53d8
        Factor = 1.0 / std::sqrt(QuadLength);
    else
        Factor = 1.0; // DAT_063a53d0

    Mindist->SeparationGap = (float)(Factor * QuadLength - Mindist->CombinedRadius);
    Normal.x = (float)(Factor * Normal.x);
    Normal.y = (float)(Factor * Normal.y);
    Normal.z = (float)(Factor * Normal.z);

    const double Dx = (double)(Cache0->Secondary.x) - Cache1->Secondary.x;
    const double Dy = (double)(Cache0->Secondary.y) - Cache1->Secondary.y;
    const double Dz = (double)(Cache0->Secondary.z) - Cache1->Secondary.z;
    Mindist->SecondaryNormalDot = (float)((Dy * Normal.y + Dz * Normal.z) + Dx * Normal.x);

    --Cache0->RefCount;
    --Cache1->RefCount;
    return IVP_MRC_OK;
}

// ---------------------------------------------------------------------------
// FUN_06363350, thiscall, RET 0x10. P inside F (no QR sign bit) -> 06363620;
// else the nearest of F's three edges (less or unordered replaces) -> PK.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_PF(IVP_Compact_Edge* P, IVP_Compact_Edge* F,
                                                        IVP_Cache_Ledge_Point* CacheP, IVP_Cache_Ledge_Point* CacheF)
{
    IVP_U_Point PosPInFSpace;
    IVP_Compact_Ledge_Solver::calc_pos_other_space(P, CacheP, CacheF, &PosPInFSpace);
    IVP_Unscaled_QR_Result QR;
    IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(CacheF->compact_ledge, F, &PosPInFSpace, &QR);

    if (((IVP_MinimizeFloatBits(QR.Checks[1]) | IVP_MinimizeFloatBits(QR.Checks[2]) | IVP_MinimizeFloatBits(QR.Checks[0])) &
         0x80000000u) == 0)
        return p_minimize_Leave_PF(P, &PosPInFSpace, F, CacheP, CacheF);

    float MinQuadDist = MinimizeSearchStart;
    IVP_Compact_Edge* BestEdge = nullptr;
    IVP_Compact_Edge* Edge = F;
    for (int32_t I = 3; I != 0; --I)
    {
        const double QuadDist = IVP_Compact_Ledge_Solver::calc_qlen_PK_K_space(&PosPInFSpace, CacheF->compact_ledge, Edge);
        if (!(QuadDist >= MinQuadDist))
        {
            MinQuadDist = (float)(QuadDist);
            BestEdge = Edge;
        }
        Edge = Edge->GetNextEdge();
    }
    return p_minimize_PK(P, BestEdge, CacheP, CacheF);
}

// ---------------------------------------------------------------------------
// FUN_06363620, thiscall, RET 0x14. P projects inside F.
//   Loop guard (P_Finish_Counter / check_loop_hash) -> ENDLESS_LOOP.
//   Gap / normal from F's normized Hesse vector (world normal into +0x60); a
//   negative (or unordered) gap flips P's copy of the normal.
//   Walk P's vertex fan for the neighbour that descends most steeply against
//   the normal (dot < 0, value = dot / |v|, smallest wins; unordered counts).
//   None: settle on {P, point} / {F, poly}; a negative raw gap (or unordered)
//   records BacksidePosition, marks F's synapse status 5 and returns BACKSIDE.
//   Otherwise move to the best neighbour: still inside F -> recurse; exactly
//   one negative QR check -> KK against that edge; else KK against the
//   nearest non-positive edge (FUN_0635bdd0).
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_Leave_PF(IVP_Compact_Edge* P, const IVP_U_Point* PosPInFSpace,
                                                              IVP_Compact_Edge* F, IVP_Cache_Ledge_Point* CacheP,
                                                              IVP_Cache_Ledge_Point* CacheF)
{
    if (--P_Finish_Counter < 0)
    {
        if (check_loop_hash(IVP_SYNAPSE_POINT, P, IVP_SYNAPSE_POLY, F) != 0)
            return IVP_MRC_ENDLESS_LOOP;
    }

    IVP_U_Point HesseF;
    IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(F, CacheF->compact_ledge, &HesseF);
    HesseF.normize();
    IVP_U_Point HesseWorld;
    IVP_RotateSlotToWorldSpace(CacheF->cache_object, &HesseF, &HesseWorld); // 0634d9d0
    IVP_U_Point HesseP;
    IVP_RotateWorldToSlotSpace(CacheP->cache_object, &HesseWorld, &HesseP); // 0634d970

    IVP_Ledge_Mindist* const Mindist = mindist;
    const IVP_U_Float_Point* const PointOfF = CacheF->compact_poly_points + F->GetStartPointIndex();
    const double DistP = ((double)(HesseF.k[2]) * PosPInFSpace->k[2] + (double)(HesseF.k[0]) * PosPInFSpace->k[0]) +
                         (double)(HesseF.k[1]) * PosPInFSpace->k[1];
    const double DistF = ((double)(HesseF.k[1]) * PointOfF->Y + (double)(HesseF.k[2]) * PointOfF->Z) +
                         (double)(HesseF.k[0]) * PointOfF->X;
    Mindist->SeparationGap = (float)(DistP - DistF);
    Mindist->SeparatingNormal.x = HesseWorld.k[0];
    Mindist->SeparatingNormal.y = HesseWorld.k[1];
    Mindist->SeparatingNormal.z = HesseWorld.k[2];

    if (!(Mindist->SeparationGap >= 0.0f)) // DAT_063a53d4
    {
        HesseP.k[0] = (float)((double)(HesseP.k[0]) * -1.0f); // DAT_063a5498
        HesseP.k[1] = (float)((double)(HesseP.k[1]) * -1.0f);
        HesseP.k[2] = (float)((double)(HesseP.k[2]) * -1.0f);
    }
    Mindist->SeparationGap = (float)((double)(Mindist->SeparationGap) - Mindist->CombinedRadius);

    {
        const IVP_NarrowPhaseCacheSlot* const CacheObjectP = CacheP->cache_object;
        const IVP_NarrowPhaseCacheSlot* const CacheObjectF = CacheF->cache_object;
        const double Dx = (double)(CacheObjectP->Secondary.x) - CacheObjectF->Secondary.x;
        const double Dy = (double)(CacheObjectP->Secondary.y) - CacheObjectF->Secondary.y;
        const double Dz = (double)(CacheObjectP->Secondary.z) - CacheObjectF->Secondary.z;
        Mindist->SecondaryNormalDot = (float)((Dy * Mindist->SeparatingNormal.y + Dz * Mindist->SeparatingNormal.z) +
                                                         Dx * Mindist->SeparatingNormal.x);
    }

    // P's vertex fan.
    const IVP_U_Float_Point* const PointOfP = CacheP->compact_poly_points + P->GetStartPointIndex();
    IVP_Compact_Edge* const StopEdge = P->GetPrevEdge();
    IVP_Compact_Edge* Neighbour = StopEdge->GetOppositeEdge()->GetPrevEdge();
    float MinValue = 0.0f;
    IVP_Compact_Edge* BestEdge = nullptr;
    for (;;)
    {
        const IVP_U_Float_Point* const Q = CacheP->compact_poly_points + Neighbour->GetStartPointIndex();
        const double Vx = (double)(Q->X) - PointOfP->X;
        const float Vy = (float)((double)(Q->Y) - PointOfP->Y);
        const double Vz = (double)(Q->Z) - PointOfP->Z;
        const double DotExtended = ((double)(HesseP.k[2]) * Vz + (double)(HesseP.k[1]) * Vy) +
                                   (double)(HesseP.k[0]) * Vx;
        const float Dot = (float)(DotExtended);
        if (!(DotExtended >= 0.0))
        {
            const float QuadLength = (float)((Vx * Vx + (double)(Vy) * Vy) + Vz * Vz);
            const double Value = IVP_InvSqrtFloat(QuadLength) * Dot;
            if (!(Value >= MinValue))
            {
                MinValue = (float)(Value);
                BestEdge = Neighbour;
            }
        }
        if (Neighbour == StopEdge)
            break;
        Neighbour = Neighbour->GetOppositeEdge()->GetPrevEdge();
    }

    if (BestEdge == nullptr)
    {
        CacheP->synapse->edge = P;
        CacheP->synapse->status = IVP_SYNAPSE_POINT;
        CacheF->synapse->edge = F;
        CacheF->synapse->status = IVP_SYNAPSE_POLY;
        if (!((double)(Mindist->SeparationGap) + Mindist->CombinedRadius >= 0.0))
        {
            BacksidePosition.k[0] = PosPInFSpace->k[0];
            BacksidePosition.k[1] = PosPInFSpace->k[1];
            BacksidePosition.k[2] = PosPInFSpace->k[2];
            CacheF->synapse->edge = F;
            CacheF->synapse->status = IVP_ST_BACKSIDE;
            return IVP_MRC_BACKSIDE;
        }
        return IVP_MRC_OK;
    }

    IVP_U_Point PosBestInFSpace;
    IVP_Compact_Ledge_Solver::calc_pos_other_space(BestEdge, CacheP, CacheF, &PosBestInFSpace);
    IVP_Unscaled_QR_Result QR;
    IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(F->GetCompactLedge(), F, &PosBestInFSpace, &QR);
    if (((IVP_MinimizeFloatBits(QR.Checks[1]) | IVP_MinimizeFloatBits(QR.Checks[2]) | IVP_MinimizeFloatBits(QR.Checks[0])) &
         0x80000000u) == 0)
        return p_minimize_Leave_PF(BestEdge, &PosBestInFSpace, F, CacheP, CacheF);

    int32_t NegativeCount = 0;
    for (int32_t I = 0; I < 3; ++I)
    {
        if (!(QR.Checks[I] >= 0.0f))
            ++NegativeCount;
    }
    if (NegativeCount == 1)
    {
        IVP_Compact_Edge* Edge = F;
        for (int32_t I = 0; I < 3; ++I)
        {
            if (!(QR.Checks[I] >= 0.0f))
                return p_minimize_KK(BestEdge, Edge, CacheP, CacheF);
            Edge = Edge->GetNextEdge();
        }
    }

    IVP_Compact_Edge* BestOfF = nullptr;
    float MinDist = MinimizeSearchStart;
    IVP_Compact_Edge* Edge = F;
    for (int32_t I = 0; I < 3; ++I)
    {
        if (!(QR.Checks[I] > 0.0f))
        {
            const double Dist = IVP_Compact_Ledge_Solver::calc_qlen_KK(BestEdge, Edge, CacheP, CacheF);
            if (!(Dist >= MinDist))
            {
                MinDist = (float)(Dist);
                BestOfF = Edge;
            }
        }
        Edge = Edge->GetNextEdge();
    }
    return p_minimize_KK(BestEdge, BestOfF, CacheP, CacheF);
}

// ---------------------------------------------------------------------------
// FUN_063651c0, thiscall, RET 0x10. s0 < 0 -> PP with K's start, s1 < 0 ->
// PP with its end (unordered counts as negative), else Leave_PK.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_PK(IVP_Compact_Edge* P, IVP_Compact_Edge* K,
                                                        IVP_Cache_Ledge_Point* CacheP, IVP_Cache_Ledge_Point* CacheK)
{
    IVP_U_Point PosPInKSpace;
    IVP_Compact_Ledge_Solver::calc_pos_other_space(P, CacheP, CacheK, &PosPInKSpace);
    IVP_Unscaled_S_Result S;
    IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(CacheK->compact_ledge, K, &PosPInKSpace, &S);
    if (!(S.Checks[0] >= 0.0f))
        return p_minimize_PP(P, K, CacheP, CacheK);
    if (!(S.Checks[1] >= 0.0f))
        return p_minimize_PP(P, K->GetNextEdge(), CacheP, CacheK);
    return p_minimize_Leave_PK(P, K, CacheP, CacheK);
}

// ---------------------------------------------------------------------------
// FUN_06364890, thiscall, RET 0x10.
//   Loop guard -> ENDLESS_LOOP; a squared distance <= 1e-6f (DAT_063a5610) or
//   unordered also returns ENDLESS_LOOP.
//   Gap / normal from the world-space difference.
//   For each side, walk its vertex fan for the neighbour edge the OTHER point
//   projects onto most positively (value = p / |w|, largest wins).
//   No such edge, or s1 <= 0 on it: settle on both points, OK. s0 < 0 (or
//   unordered): recurse PP with the neighbour; else Leave_PK. In both cases
//   StateFlags bit 0x100 is flipped so the sorted synapse 0 is the first
//   argument's side.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_PP(IVP_Compact_Edge* P0, IVP_Compact_Edge* P1,
                                                        IVP_Cache_Ledge_Point* Cache0, IVP_Cache_Ledge_Point* Cache1)
{
    if (--P_Finish_Counter < 0)
    {
        if (check_loop_hash(IVP_SYNAPSE_POINT, P0, IVP_SYNAPSE_POINT, P1) != 0)
            return IVP_MRC_ENDLESS_LOOP;
    }

    IVP_Compact_Edge* const Edges[2] = { P0, P1 };            // local_5c / local_58
    IVP_Cache_Ledge_Point* const Caches[2] = { Cache0, Cache1 }; // local_50 / local_4c

    IVP_U_Point World0; // local_48
    IVP_Compact_Ledge_Solver::give_world_coords_AT(P0, Cache0, &World0);
    IVP_U_Point World1; // local_38
    IVP_Compact_Ledge_Solver::give_world_coords_AT(P1, Cache1, &World1);

    // [0]: P1 in cache0's space (local_10), [1]: P0 in cache1's space (local_20); side i
    // compares the OTHER point against its own vertex fan.
    IVP_U_Point PosOther[2];
    IVP_TransformWorldToSlotSpace(Cache1->cache_object, &World0, &PosOther[1]);
    IVP_TransformWorldToSlotSpace(Cache0->cache_object, &World1, &PosOther[0]);

    const double Dx = (double)(World0.k[0]) - World1.k[0];
    const double Dy = (double)(World0.k[1]) - World1.k[1];
    const double Dz = (double)(World0.k[2]) - World1.k[2];
    const float QuadLength = (float)((Dz * Dz + Dy * Dy) + Dx * Dx);
    if (!(QuadLength > 1.0e-6f)) // DAT_063a5610
        return IVP_MRC_ENDLESS_LOOP;

    IVP_Ledge_Mindist* const Mindist = mindist;
    const double InvLength = 1.0 / std::sqrt((double)(QuadLength)); // FSQRT; FDIVR 1.0f
    Mindist->SeparationGap = (float)(InvLength * QuadLength - Mindist->CombinedRadius);
    Mindist->SeparatingNormal.z = (float)(Dz * InvLength);
    Mindist->SeparatingNormal.x = (float)(Dx * InvLength);
    Mindist->SeparatingNormal.y = (float)(Dy * InvLength);

    {
        const IVP_NarrowPhaseCacheSlot* const CacheObject0 = Cache0->cache_object;
        const IVP_NarrowPhaseCacheSlot* const CacheObject1 = Cache1->cache_object;
        const double Sx = (double)(CacheObject0->Secondary.x) - CacheObject1->Secondary.x;
        const double Sy = (double)(CacheObject0->Secondary.y) - CacheObject1->Secondary.y;
        const double Sz = (double)(CacheObject0->Secondary.z) - CacheObject1->Secondary.z;
        Mindist->SecondaryNormalDot = (float)((Sy * Mindist->SeparatingNormal.y + Sz * Mindist->SeparatingNormal.z) +
                                                         Sx * Mindist->SeparatingNormal.x);
    }

    float MaxValue = 0.0f;                              // local_78
    IVP_Compact_Edge* BestEdge = nullptr;               // local_98
    IVP_U_Point* BestPos = nullptr;                     // local_94
    IVP_Compact_Edge* BestOtherEdge = nullptr;          // local_88
    IVP_Cache_Ledge_Point* BestCache = nullptr;         // local_90
    IVP_Cache_Ledge_Point* BestOtherCache = nullptr;    // local_8c
    for (int32_t Side = 0; Side < 2; ++Side)
    {
        IVP_Compact_Edge* const P = Edges[Side];
        IVP_Cache_Ledge_Point* const Cache = Caches[Side];
        IVP_U_Point* const Pos = &PosOther[Side];
        const IVP_U_Float_Point* const V = Cache->compact_poly_points + P->GetStartPointIndex();

        const float Vx = (float)((double)(Pos->k[0]) - V->X); // local_6c
        const float Vy = (float)((double)(Pos->k[1]) - V->Y); // local_68
        const float Vz = (float)((double)(Pos->k[2]) - V->Z); // local_64
        const float DotV = (float)(((double)(Vx) * V->X + (double)(Vy) * V->Y) +
                                              (double)(Vz) * V->Z); // local_54

        IVP_Compact_Edge* const StopEdge = P->GetPrevEdge();
        IVP_Compact_Edge* Neighbour = StopEdge->GetOppositeEdge()->GetPrevEdge();
        for (;;)
        {
            const IVP_U_Float_Point* const Q = Cache->compact_poly_points + Neighbour->GetStartPointIndex();
            const double ProjectionExtended = (((double)(Vx) * Q->X + (double)(Vy) * Q->Y) +
                                               (double)(Vz) * Q->Z) - DotV;
            const float Projection = (float)(ProjectionExtended); // local_70
            if (ProjectionExtended > 0.0)
            {
                const double Wx = (double)(Q->X) - V->X;
                const double Wy = (double)(Q->Y) - V->Y;
                const double Wz = (double)(Q->Z) - V->Z;
                const float QuadEdge = (float)((Wz * Wz + Wy * Wy) + Wx * Wx);
                const double Value = IVP_InvSqrtFloat(QuadEdge) * Projection;
                if (Value > MaxValue)
                {
                    MaxValue = (float)(Value);
                    BestOtherEdge = Edges[1 - Side];
                    BestOtherCache = Caches[1 - Side];
                    BestPos = Pos;
                    BestEdge = Neighbour;
                    BestCache = Cache;
                }
            }
            if (Neighbour == StopEdge)
                break;
            Neighbour = Neighbour->GetOppositeEdge()->GetPrevEdge();
        }
    }

    if (BestEdge != nullptr)
    {
        IVP_Unscaled_S_Result S; // local_28
        IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(BestCache->compact_ledge, BestEdge, BestPos, &S);
        if (S.Checks[1] > 0.0f)
        {
            if (!(S.Checks[0] >= 0.0f))
            {
                if (BestCache->synapse != Mindist->get_sorted_synapse(0))
                    Mindist->StateFlags ^= 0x100u;
                return p_minimize_PP(BestEdge, BestOtherEdge, BestCache, BestOtherCache);
            }
            if (BestOtherCache->synapse != Mindist->get_sorted_synapse(0))
                Mindist->StateFlags ^= 0x100u;
            return p_minimize_Leave_PK(BestOtherEdge, BestEdge, BestOtherCache, BestCache);
        }
    }

    Cache0->synapse->edge = P0;
    Cache0->synapse->status = IVP_SYNAPSE_POINT;
    Cache1->synapse->status = IVP_SYNAPSE_POINT;
    Cache1->synapse->edge = P1;
    return IVP_MRC_OK;
}

// ---------------------------------------------------------------------------
// FUN_06365280, thiscall, RET 0x10. P projects onto the inside of edge K.
//   Loop guard -> ENDLESS_LOOP.
//   The two triangles sharing K (K's and the opposite edge's): P inside
//   either (QR check 0 > 0) -> PF, preferring the opposite face when both
//   qualify and P is above it. Below both faces (or unordered) -> {P, point},
//   {K, status 5}, BacksidePosition, BACKSIDE.
//   Gap / normal from c = e x d: when |c|^2 / |e|^2 > 1e-10f (DAT_063a53d8)
//   the normal is e x c rotated to world and scaled by -(i*ie); otherwise the
//   gap is -CombinedRadius and the normal is a normized perpendicular of e
//   (FUN_063485e0) LEFT IN K'S OBJECT SPACE, as retail.
//   c is then rotated into P's space (in the degenerate case it is the
//   unrotated e x d) and P's vertex fan searched for a neighbour above
//   threshold t = q * 1e-6f. A best neighbour goes to KK when t >= 0.003f
//   (DAT_063a545c), or when the KK values (FUN_0635b570) succeed with
//   checks_L[0] >= 0; otherwise settle on {P, point} / {K, edge}, OK.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_Leave_PK(IVP_Compact_Edge* P, IVP_Compact_Edge* K,
                                                              IVP_Cache_Ledge_Point* CacheP, IVP_Cache_Ledge_Point* CacheK)
{
    if (--P_Finish_Counter < 0)
    {
        if (check_loop_hash(IVP_SYNAPSE_POINT, P, IVP_SYNAPSE_EDGE, K) != 0)
            return IVP_MRC_ENDLESS_LOOP;
    }

    IVP_U_Point PosP; // local_d8
    IVP_Compact_Ledge_Solver::calc_pos_other_space(P, CacheP, CacheK, &PosP);

    const IVP_U_Float_Point* const PointsK = CacheK->compact_poly_points;
    IVP_Compact_Edge* const OppositeK = K->GetOppositeEdge();
    const IVP_U_Float_Point* const K0 = PointsK + K->GetStartPointIndex();
    const IVP_U_Float_Point* const K1 = PointsK + K->GetNextEdge()->GetStartPointIndex();
    const IVP_U_Float_Point* const R = PointsK + K->GetPrevEdge()->GetStartPointIndex();
    const IVP_U_Float_Point* const S = PointsK + OppositeK->GetPrevEdge()->GetStartPointIndex();

    IVP_U_Point EdgeVec; // local_f8
    EdgeVec.k[1] = (float)((double)(K1->Y) - K0->Y);
    EdgeVec.k[2] = (float)((double)(K1->Z) - K0->Z);
    EdgeVec.k[0] = (float)((double)(K1->X) - K0->X);
    IVP_U_Point Delta; // local_e8
    Delta.k[1] = (float)((double)(PosP.k[1]) - K0->Y);
    Delta.k[2] = (float)((double)(PosP.k[2]) - K0->Z);
    Delta.k[0] = (float)((double)(PosP.k[0]) - K0->X);
    IVP_U_Point ToThird; // local_a8
    ToThird.k[1] = (float)((double)(R->Y) - K0->Y);
    ToThird.k[2] = (float)((double)(R->Z) - K0->Z);
    ToThird.k[0] = (float)((double)(R->X) - K0->X);
    IVP_U_Point ToOpposite; // local_98
    ToOpposite.k[1] = (float)((double)(S->Y) - K0->Y);
    ToOpposite.k[2] = (float)((double)(S->Z) - K0->Z);
    ToOpposite.k[0] = (float)((double)(S->X) - K0->X);

    IVP_U_Point NormalK; // local_c8
    NormalK.calc_cross_product(&EdgeVec, &ToThird);
    IVP_U_Point NormalOpposite; // local_b8
    NormalOpposite.calc_cross_product(&ToOpposite, &EdgeVec);

    const float HeightK = (float)(
        ((double)(Delta.k[2]) * NormalK.k[2] + (double)(Delta.k[1]) * NormalK.k[1]) +
        (double)(Delta.k[0]) * NormalK.k[0]); // local_108
    const float HeightOpposite = (float)(
        ((double)(Delta.k[2]) * NormalOpposite.k[2] + (double)(Delta.k[1]) * NormalOpposite.k[1]) +
        (double)(NormalOpposite.k[0]) * Delta.k[0]); // local_104

    IVP_Unscaled_QR_Result QRK; // local_78
    IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(CacheK->compact_ledge, K, &PosP, &QRK);
    IVP_Unscaled_QR_Result QROpposite; // local_88
    IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(CacheK->compact_ledge, OppositeK, &PosP, &QROpposite);

    if (QRK.Checks[0] > 0.0f)
    {
        if (QROpposite.Checks[0] > 0.0f && HeightOpposite > 0.0f)
            return p_minimize_PF(P, OppositeK, CacheP, CacheK);
        return p_minimize_PF(P, K, CacheP, CacheK);
    }
    if (QROpposite.Checks[0] > 0.0f)
        return p_minimize_PF(P, OppositeK, CacheP, CacheK);

    if (!(HeightK >= 0.0f) && !(HeightOpposite >= 0.0f))
    {
        CacheP->synapse->edge = P;
        CacheP->synapse->status = IVP_SYNAPSE_POINT;
        CacheK->synapse->edge = K;
        CacheK->synapse->status = IVP_ST_BACKSIDE;
        BacksidePosition.k[0] = PosP.k[0];
        BacksidePosition.k[1] = PosP.k[1];
        BacksidePosition.k[2] = PosP.k[2];
        return IVP_MRC_BACKSIDE;
    }

    IVP_U_Point Perpendicular; // local_118
    Perpendicular.calc_cross_product(&EdgeVec, &Delta);
    const float InvQuadEdge = (float)(
        1.0 / (((double)(EdgeVec.k[0]) * EdgeVec.k[0] + (double)(EdgeVec.k[2]) * EdgeVec.k[2]) +
               (double)(EdgeVec.k[1]) * EdgeVec.k[1])); // local_128
    const double QuadDistExtended =
        (((double)(Perpendicular.k[2]) * Perpendicular.k[2] + (double)(Perpendicular.k[1]) * Perpendicular.k[1]) +
         (double)(Perpendicular.k[0]) * Perpendicular.k[0]) *
        InvQuadEdge;
    const float QuadDist = (float)(QuadDistExtended); // local_120

    IVP_Ledge_Mindist* const Mindist = mindist;
    if (QuadDistExtended > (double)(1.0e-10f)) // DAT_063a53d8
    {
        const float InvDist = (float)(IVP_InvSqrtFloat(QuadDist)); // local_11c
        Mindist->SeparationGap = (float)((double)(InvDist) * QuadDist - Mindist->CombinedRadius);
        Perpendicular.calc_cross_product(&EdgeVec, &Perpendicular);
        IVP_RotateSlotToWorldSpace(CacheK->cache_object, &Perpendicular, &Perpendicular); // 0634d9d0, in place
        const double Scale = -((double)(InvDist) * InvQuadEdge);
        Mindist->SeparatingNormal.y = (float)(Perpendicular.k[1] * Scale);
        Mindist->SeparatingNormal.z = (float)(Perpendicular.k[2] * Scale);
        Mindist->SeparatingNormal.x = (float)(Scale * Perpendicular.k[0]);
    }
    else
    {
        Mindist->SeparationGap = -Mindist->CombinedRadius;
        IVP_U_Point Fallback; // local_108
        Fallback.set_arbitrary_perpendicular_to(&EdgeVec); // 063485e0
        Fallback.normize();                                // 06347ef0
        Mindist->SeparatingNormal.x = Fallback.k[0];
        Mindist->SeparatingNormal.y = Fallback.k[1];
        Mindist->SeparatingNormal.z = Fallback.k[2];
    }

    {
        const IVP_NarrowPhaseCacheSlot* const CacheObjectP = CacheP->cache_object;
        const IVP_NarrowPhaseCacheSlot* const CacheObjectK = CacheK->cache_object;
        const double Sx = (double)(CacheObjectP->Secondary.x) - CacheObjectK->Secondary.x;
        const double Sy = (double)(CacheObjectP->Secondary.y) - CacheObjectK->Secondary.y;
        const double Sz = (double)(CacheObjectP->Secondary.z) - CacheObjectK->Secondary.z;
        Mindist->SecondaryNormalDot = (float)((Sy * Mindist->SeparatingNormal.y + Sz * Mindist->SeparatingNormal.z) +
                                                         Sx * Mindist->SeparatingNormal.x);
    }

    IVP_RotateWorldToSlotSpace(CacheP->cache_object, &Perpendicular, &Perpendicular); // 0634d970, in place

    float MaxValue = (float)((double)(QuadDist) * 1.0e-6f); // DAT_063a5610, local_120
    const IVP_U_Float_Point* const V = CacheP->compact_poly_points + P->GetStartPointIndex();
    IVP_Compact_Edge* const StopEdge = P->GetPrevEdge();
    IVP_Compact_Edge* Neighbour = StopEdge->GetOppositeEdge()->GetPrevEdge();
    IVP_Compact_Edge* BestEdge = nullptr; // local_11c
    for (;;)
    {
        const IVP_U_Float_Point* const Q = CacheP->compact_poly_points + Neighbour->GetStartPointIndex();
        const double Wx = (double)(Q->X) - V->X;
        const double Wy = (double)(Q->Y) - V->Y;
        const double Wz = (double)(Q->Z) - V->Z;
        const double DotExtended = (Wz * Perpendicular.k[2] + Wy * Perpendicular.k[1]) + Wx * Perpendicular.k[0];
        const float Dot = (float)(DotExtended); // local_108
        if (DotExtended > 0.0)
        {
            const float QuadEdge = (float)((Wy * Wy + Wz * Wz) + Wx * Wx);
            const double Value = IVP_InvSqrtFloat(QuadEdge) * Dot;
            if (Value > MaxValue)
            {
                MaxValue = (float)(Value);
                BestEdge = Neighbour;
            }
        }
        if (Neighbour == StopEdge)
            break;
        Neighbour = Neighbour->GetOppositeEdge()->GetPrevEdge();
    }

    bool bTakeEdgeEdge = false;
    if (BestEdge != nullptr)
    {
        if (MaxValue >= 0.003f) // DAT_063a545c
        {
            bTakeEdgeEdge = true;
        }
        else
        {
            const IVP_KK_Input KKInput(K, BestEdge, CacheK, CacheP); // local_68
            IVP_Unscaled_KK_Result KKResult;                        // local_108
            if (IVP_Compact_Ledge_Solver::calc_unscaled_KK_vals(&KKInput, &KKResult) != 0 &&
                KKResult.checks_L[0] >= 0.0f)
                bTakeEdgeEdge = true;
        }
    }
    if (bTakeEdgeEdge)
        return p_minimize_KK(BestEdge, K, CacheP, CacheK);

    CacheP->synapse->edge = P;
    CacheP->synapse->status = IVP_SYNAPSE_POINT;
    CacheK->synapse->edge = K;
    CacheK->synapse->status = IVP_SYNAPSE_EDGE;
    return IVP_MRC_OK;
}

// ---------------------------------------------------------------------------
// FUN_06363ab0, thiscall, RET 0x10. From the KK values (FUN_0635b570):
//   L inside: K's start/end outside (negative or unordered) -> PK, else Leave_KK.
//   K inside: PK with L's start or end (sorted synapse 0 made L's side).
//   Both out: the nearer endpoint of each edge is tested against the other
//   edge (PK when it projects inside), then the PP combinations by the sign
//   of s0 * r. Whenever L's side becomes the first argument, StateFlags bit
//   0x100 is flipped so it is the sorted synapse 0.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_KK(IVP_Compact_Edge* K, IVP_Compact_Edge* L,
                                                        IVP_Cache_Ledge_Point* CacheK, IVP_Cache_Ledge_Point* CacheL)
{
    const IVP_KK_Input KKInput(K, L, CacheK, CacheL); // local_68
    IVP_Unscaled_KK_Result Result;                   // local_b4
    IVP_Compact_Ledge_Solver::calc_unscaled_KK_vals(&KKInput, &Result);

    if (((IVP_MinimizeFloatBits(Result.checks_L[0]) | IVP_MinimizeFloatBits(Result.checks_L[1])) & 0x80000000u) == 0)
    {
        if (!(Result.checks_K[0] >= 0.0f))
            return p_minimize_PK(K, L, CacheK, CacheL);
        if (!(Result.checks_K[1] >= 0.0f))
            return p_minimize_PK(K->GetNextEdge(), L, CacheK, CacheL);
        return p_minimize_Leave_KK(K, L, &KKInput, &Result, CacheK, CacheL);
    }

    IVP_Ledge_Mindist* const Mindist = mindist;
    if (((IVP_MinimizeFloatBits(Result.checks_K[1]) | IVP_MinimizeFloatBits(Result.checks_K[0])) & 0x80000000u) == 0)
    {
        if (CacheL->synapse != Mindist->get_sorted_synapse(0))
            Mindist->StateFlags ^= 0x100u;
        if (!(Result.checks_L[0] >= 0.0f))
            return p_minimize_PK(L, K, CacheL, CacheK);
        return p_minimize_PK(L->GetNextEdge(), K, CacheL, CacheK);
    }

    IVP_Compact_Edge* PointK;      // local_b8
    IVP_Compact_Edge* OtherPointK; // local_a0
    if (!(Result.checks_K[0] >= Result.checks_K[1]))
    {
        PointK = K;
        OtherPointK = K->GetNextEdge();
    }
    else
    {
        PointK = K->GetNextEdge();
        OtherPointK = K;
    }
    IVP_Compact_Edge* PointL;      // local_a4
    IVP_Compact_Edge* OtherPointL; // local_9c
    if (!(Result.checks_L[0] >= Result.checks_L[1]))
    {
        PointL = L;
        OtherPointL = L->GetNextEdge();
    }
    else
    {
        PointL = L->GetNextEdge();
        OtherPointL = L;
    }

    IVP_U_Point PosKInL; // local_88
    IVP_Compact_Ledge_Solver::calc_pos_other_space(PointK, CacheK, CacheL, &PosKInL);
    IVP_Unscaled_S_Result SK; // local_98
    IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(CacheL->compact_ledge, L, &PosKInL, &SK);
    if (((IVP_MinimizeFloatBits(SK.Checks[1]) | IVP_MinimizeFloatBits(SK.Checks[0])) & 0x80000000u) == 0)
        return p_minimize_PK(PointK, L, CacheK, CacheL);

    IVP_U_Point PosLInK; // local_78
    IVP_Compact_Ledge_Solver::calc_pos_other_space(PointL, CacheL, CacheK, &PosLInK);
    IVP_Unscaled_S_Result SL; // local_90
    IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(CacheK->compact_ledge, K, &PosLInK, &SL);
    if (((IVP_MinimizeFloatBits(SL.Checks[1]) | IVP_MinimizeFloatBits(SL.Checks[0])) & 0x80000000u) == 0)
    {
        if (CacheL->synapse != Mindist->get_sorted_synapse(0))
            Mindist->StateFlags ^= 0x100u;
        return p_minimize_PK(PointL, K, CacheL, CacheK);
    }

    if (!((double)(SK.Checks[0]) * Result.checks_L[0] >= 0.0))
        return p_minimize_PP(PointK, OtherPointL, CacheK, CacheL);
    if (!((double)(SL.Checks[0]) * Result.checks_K[0] >= 0.0))
    {
        if (CacheL->synapse != Mindist->get_sorted_synapse(0))
            Mindist->StateFlags ^= 0x100u;
        return p_minimize_PP(PointL, OtherPointK, CacheL, CacheK);
    }
    return p_minimize_PP(PointK, PointL, CacheK, CacheL);
}

// ---------------------------------------------------------------------------
// FUN_06363e00, thiscall, RET 0x18: the closest points lie inside both edges.
//   Loop guard -> ENDLESS_LOOP. Gap = |dist| / |c| - CombinedRadius with
//   c = cross_KL_Los; normal = c (L-local -> world) scaled by
//   2 * (sign - 0.5f) / |c|.
//   Four candidate records (each edge's two adjacent triangles, with the
//   other edge's endpoint in that triangle's space); flag_i = which side of
//   triangle i the normal points to, xor (i >> 1) xor sign.
//   The steepest descending move (value < -4e-6f, 0xb68637bd) whose moved
//   point lands inside the triangle (qr0 > 0) wins.
//   None: settle on both edges; both flags of one edge set -> that edge goes
//   status 5 (backside) with BacksidePosition interpolated along the other
//   edge by the KK values, BACKSIDE; else OK.
//   Best: sorted synapse 0 := the moving point's side; inside the triangle ->
//   Leave_PF; else KK against the next or prev edge of the triangle by QR
//   checks, ties broken by FUN_0635bdd0 distance (unordered -> prev).
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_Leave_KK(IVP_Compact_Edge* K, IVP_Compact_Edge* L,
                                                              const IVP_KK_Input* KKInput,
                                                              const IVP_Unscaled_KK_Result* Result,
                                                              IVP_Cache_Ledge_Point* CacheK, IVP_Cache_Ledge_Point* CacheL)
{
    if (--P_Finish_Counter < 0)
    {
        if (check_loop_hash(IVP_SYNAPSE_EDGE, K, IVP_SYNAPSE_EDGE, L) != 0)
            return IVP_MRC_ENDLESS_LOOP;
    }

    const IVP_U_Point* const Cross = &KKInput->cross_KL_Los;
    const IVP_U_Float_Point* const L0 = KKInput->L_Los[0];
    const IVP_U_Point* const KStart = &KKInput->K_Los[0]; // local_140
    const double Dx = (double)(L0->X) - KStart->k[0];
    const double Dy = (double)(L0->Y) - KStart->k[1];
    const double Dz = (double)(L0->Z) - KStart->k[2];
    const float Dist = (float)((Dy * Cross->k[1] + Dz * Cross->k[2]) + Dx * Cross->k[0]); // local_160
    const int32_t DistSign = static_cast<int32_t>(IVP_MinimizeFloatBits(Dist) >> 31);               // EBP
    const float InvLength = (float)(
        1.0 / std::sqrt(((double)(Cross->k[0]) * Cross->k[0] + (double)(Cross->k[1]) * Cross->k[1]) +
                        (double)(Cross->k[2]) * Cross->k[2])); // local_164

    IVP_U_Point CrossCopy; // local_100
    CrossCopy.k[0] = Cross->k[0];
    CrossCopy.k[1] = Cross->k[1];
    CrossCopy.k[2] = Cross->k[2];
    IVP_U_Point NormalWorld; // local_e0
    IVP_RotateSlotToWorldSpace(CacheL->cache_object, &CrossCopy, &NormalWorld); // 0634d9d0
    IVP_U_Point NormalK; // local_d0
    IVP_RotateWorldToSlotSpace(CacheK->cache_object, &NormalWorld, &NormalK); // 0634d970

    IVP_Ledge_Mindist* const Mindist = mindist;
    Mindist->SeparationGap = (float)(std::fabs((double)(InvLength) * Dist));
    Mindist->SeparationGap = (float)((double)(Mindist->SeparationGap) - Mindist->CombinedRadius);
    double Scale = ((double)(DistSign) - 0.5f) * InvLength; // FILD; FSUB DAT_063a5460; FMUL
    Scale = Scale + Scale;
    Mindist->SeparatingNormal.z = (float)(NormalWorld.k[2] * Scale);
    Mindist->SeparatingNormal.x = (float)(NormalWorld.k[0] * Scale);
    Mindist->SeparatingNormal.y = (float)(NormalWorld.k[1] * Scale);

    {
        const IVP_NarrowPhaseCacheSlot* const CacheObjectK = CacheK->cache_object;
        const IVP_NarrowPhaseCacheSlot* const CacheObjectL = CacheL->cache_object;
        const double Sx = (double)(CacheObjectK->Secondary.x) - CacheObjectL->Secondary.x;
        const double Sy = (double)(CacheObjectK->Secondary.y) - CacheObjectL->Secondary.y;
        const double Sz = (double)(CacheObjectK->Secondary.z) - CacheObjectL->Secondary.z;
        Mindist->SecondaryNormalDot = (float)((Sy * Mindist->SeparatingNormal.y + Sz * Mindist->SeparatingNormal.z) +
                                                         Sx * Mindist->SeparatingNormal.x);
    }

    IVP_U_Point L0InK; // local_c0
    IVP_Compact_Ledge_Solver::transform_pos_other_space(KKInput->L_Los[0], CacheL, CacheK, &L0InK);
    IVP_U_Point L1InK; // local_b0
    IVP_Compact_Ledge_Solver::transform_pos_other_space(KKInput->L_Los[1], CacheL, CacheK, &L1InK);

    // The four stack records (local_a0 + 0x28 * i).
    struct FLeaveKKRecord
    {
        IVP_Compact_Edge* Triangle;            // +0x00
        IVP_Compact_Edge* PointEdge;           // +0x04
        const IVP_U_Point* Point;              // +0x08
        IVP_Cache_Ledge_Point* CachePoint;     // +0x0c
        IVP_Cache_Ledge_Point* CacheTriangle;  // +0x10
        const IVP_U_Point* Normal;             // +0x14
        IVP_U_Point Hesse;                     // +0x18
    };
    IVP_Compact_Edge* const InK = const_cast<IVP_Compact_Edge*>(KKInput->K);
    IVP_Compact_Edge* const InL = const_cast<IVP_Compact_Edge*>(KKInput->L);
    FLeaveKKRecord Records[4];
    Records[0] = { K->GetOppositeEdge(), InL, &L0InK, CacheL, CacheK, &NormalK, {} };
    Records[1] = { K, InL->GetOppositeEdge(), &L1InK, CacheL, CacheK, &NormalK, {} };
    Records[2] = { L->GetOppositeEdge(), InK, &KKInput->K_Los[0], CacheK, CacheL, &CrossCopy, {} };
    Records[3] = { L, InK->GetOppositeEdge(), &KKInput->K_Los[1], CacheK, CacheL, &CrossCopy, {} };

    int32_t Flags[4]; // local_110
    for (int32_t I = 0; I <= 3; ++I)
    {
        FLeaveKKRecord& Record = Records[I];
        IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(Record.Triangle, Record.Triangle->GetCompactLedge(),
                                                                    &Record.Hesse);
        const IVP_U_Point* const N = Record.Normal;
        const float Side = (float)(
            ((double)(Record.Hesse.k[2]) * N->k[2] + (double)(N->k[1]) * Record.Hesse.k[1]) +
            (double)(N->k[0]) * Record.Hesse.k[0]);
        Flags[I] = static_cast<int32_t>(IVP_MinimizeFloatBits(Side) >> 31) ^ (I >> 1) ^ DistSign;
    }

    float MaxValue = -4.0e-6f;                            // 0xb68637bd, local_13c
    IVP_Compact_Edge* BestPointEdge = nullptr;            // local_164
    IVP_Cache_Ledge_Point* BestCachePoint = nullptr;      // local_150
    IVP_Compact_Edge* BestTriangle = nullptr;             // local_158
    IVP_Cache_Ledge_Point* BestCacheTriangle = nullptr;   // local_15c
    IVP_Unscaled_QR_Result BestQR = {};                   // local_124
    for (int32_t J = 0; J <= 3; ++J)
    {
        const int32_t Side = J ^ DistSign;
        const FLeaveKKRecord& Moving = Records[Side ^ Flags[J]];
        const IVP_U_Point* const A = Records[Side].Point;
        const IVP_U_Point* const B = Records[Side ^ 1].Point;
        const IVP_U_Point& H = Records[J].Hesse;

        const float Vx = (float)((double)(A->k[0]) - B->k[0]); // local_134
        const float Vy = (float)((double)(A->k[1]) - B->k[1]); // local_130
        const double VzExtended = (double)(A->k[2]) - B->k[2];
        const float Vz = (float)(VzExtended);                             // local_12c (FST)
        const double DotExtended = (VzExtended * H.k[2] + (double)(Vx) * H.k[0]) + (double)(Vy) * H.k[1];
        const float Dot = (float)(DotExtended); // local_14c (FST)
        if (DotExtended >= 0.0)
            continue;

        const float InvLengthH = (float)(IVP_InvSqrtFloat((float)(
            ((double)(H.k[0]) * H.k[0] + (double)(H.k[2]) * H.k[2]) + (double)(H.k[1]) * H.k[1])));
        const double Value = (IVP_InvSqrtFloat((float)(
                                  ((double)(Vx) * Vx + (double)(Vz) * Vz) + (double)(Vy) * Vy)) *
                              Dot) *
                             InvLengthH;
        const float ValueRounded = (float)(Value); // FST local_14c
        if (Value >= MaxValue)
            continue;

        IVP_Unscaled_QR_Result QR; // local_f0
        IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(Records[J].CacheTriangle->compact_ledge, Records[J].Triangle,
                                                               Moving.Point, &QR);
        if (!(QR.Checks[0] > 0.0f))
            continue;

        MaxValue = ValueRounded;
        BestPointEdge = Moving.PointEdge;
        BestCachePoint = Moving.CachePoint;
        BestTriangle = Records[J].Triangle;
        BestCacheTriangle = Records[J].CacheTriangle;
        BestQR = QR;
    }

    if (BestPointEdge == nullptr)
    {
        CacheK->synapse->edge = K;
        CacheK->synapse->status = IVP_SYNAPSE_EDGE;
        CacheL->synapse->status = IVP_SYNAPSE_EDGE;
        CacheL->synapse->edge = L;
        if (Flags[0] + Flags[1] == 2)
        {
            CacheK->synapse->edge = K;
            CacheK->synapse->status = IVP_ST_BACKSIDE;
            CacheL->synapse->status = IVP_SYNAPSE_POLY;
            CacheL->synapse->edge = L;
            const double CheckL0 = Result->checks_L[0];
            const float T = (float)(CheckL0 / (CheckL0 + Result->checks_L[1]));
            BacksidePosition.set_interpolate(&L0InK, &L1InK, T);
            return IVP_MRC_BACKSIDE;
        }
        if (Flags[2] + Flags[3] == 2)
        {
            CacheK->synapse->status = IVP_SYNAPSE_POLY;
            CacheK->synapse->edge = K;
            const double CheckK0 = Result->checks_K[0];
            const float T = (float)(CheckK0 / (CheckK0 + Result->checks_K[1]));
            BacksidePosition.set_interpolate(&KKInput->K_Los[0], &KKInput->K_Los[1], T);
            CacheL->synapse->edge = L;
            CacheL->synapse->status = IVP_ST_BACKSIDE;
            return IVP_MRC_BACKSIDE;
        }
        return IVP_MRC_OK;
    }

    if (BestCachePoint->synapse != Mindist->get_sorted_synapse(0))
        Mindist->StateFlags ^= 0x100u;

    if (((IVP_MinimizeFloatBits(BestQR.Checks[1]) | IVP_MinimizeFloatBits(BestQR.Checks[2]) |
          IVP_MinimizeFloatBits(BestQR.Checks[0])) & 0x80000000u) == 0)
    {
        IVP_U_Point PosInF; // local_110
        IVP_Compact_Ledge_Solver::calc_pos_other_space(BestPointEdge, BestCachePoint, BestCacheTriangle, &PosInF);
        return p_minimize_Leave_PF(BestPointEdge, &PosInF, BestTriangle, BestCachePoint, BestCacheTriangle);
    }
    if (BestQR.Checks[2] >= 0.0f)
        return p_minimize_KK(BestPointEdge, BestTriangle->GetNextEdge(), BestCachePoint, BestCacheTriangle);
    if (BestQR.Checks[1] >= 0.0f)
        return p_minimize_KK(BestPointEdge, BestTriangle->GetPrevEdge(), BestCachePoint, BestCacheTriangle);

    const float QuadDistNext = (float)(IVP_Compact_Ledge_Solver::calc_qlen_KK(
        BestPointEdge, BestTriangle->GetNextEdge(), BestCachePoint, BestCacheTriangle));
    const double QuadDistPrev = IVP_Compact_Ledge_Solver::calc_qlen_KK(
        BestPointEdge, BestTriangle->GetPrevEdge()->GetOppositeEdge(), BestCachePoint, BestCacheTriangle);
    if (!(QuadDistPrev >= QuadDistNext))
        return p_minimize_KK(BestPointEdge, BestTriangle->GetPrevEdge(), BestCachePoint, BestCacheTriangle);
    return p_minimize_KK(BestPointEdge, BestTriangle->GetNextEdge(), BestCachePoint, BestCacheTriangle);
}

// ---------------------------------------------------------------------------
// FUN_06352e40, thiscall, RET 0x10: the exhaustive feature search between two
// triangles. In order, each candidate replacing the minimum when smaller (or
// unordered): vertex pairs; points over the other triangle; points over the
// other triangle's edges; edge pairs. All but the first compare
// q * 1.000001f (DAT_063a5650), so a later feature type must win by a margin.
// Then dispatch on the winning feature pair (a point side goes first, sorted
// synapse 0 made first's): PP / PK / PF / KK, anything else CORE and 0.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_FF(IVP_Compact_Edge* F0, IVP_Compact_Edge* F1,
                                                        IVP_Cache_Ledge_Point* Cache0, IVP_Cache_Ledge_Point* Cache1)
{
    float MinQuadDist = MinimizeSearchStart; // local_c8

    {
        IVP_Compact_Edge* E0 = F0;
        IVP_Compact_Edge* E1 = F1;
        for (int32_t I = 3; I != 0; --I)
        {
            for (int32_t J = 3; J != 0; --J)
            {
                IVP_U_Point World1; // local_ac
                IVP_Compact_Ledge_Solver::give_world_coords_AT(E1, Cache1, &World1);
                IVP_U_Point World0; // local_bc
                IVP_Compact_Ledge_Solver::give_world_coords_AT(E0, Cache0, &World0);
                const double Dx = (double)(World0.k[0]) - World1.k[0];
                const double Dy = (double)(World0.k[1]) - World1.k[1];
                const double Dz = (double)(World0.k[2]) - World1.k[2];
                const double QuadDist = (Dz * Dz + Dy * Dy) + Dx * Dx;
                if (!(QuadDist >= MinQuadDist))
                {
                    MinQuadDist = (float)(QuadDist);
                    Cache0->synapse->edge = E0;
                    Cache0->synapse->status = IVP_SYNAPSE_POINT;
                    Cache1->synapse->edge = E1;
                    Cache1->synapse->status = IVP_SYNAPSE_POINT;
                }
                E1 = E1->GetNextEdge();
            }
            E0 = E0->GetNextEdge();
        }
    }

    IVP_Cache_Ledge_Point* const TriangleCaches[2] = { Cache1, Cache0 }; // local_90
    IVP_Cache_Ledge_Point* const PointCaches[2] = { Cache0, Cache1 };    // local_88
    IVP_Compact_Edge* const PointEdges[2] = { F0, F1 };                  // local_80
    IVP_Compact_Edge* const TriangleEdges[2] = { F1, F0 };               // local_9c

    for (int32_t Side = 0; Side < 2; ++Side)
    {
        IVP_Cache_Ledge_Point* const TriangleCache = TriangleCaches[Side];
        IVP_Compact_Edge* const Triangle = TriangleEdges[Side];
        IVP_U_Point Hesse; // local_bc
        IVP_Compact_Ledge_Solver::calc_hesse_object(Triangle, TriangleCache->compact_ledge, &Hesse);
        IVP_Compact_Edge* Point = PointEdges[Side];
        IVP_Cache_Ledge_Point* const PointCache = PointCaches[Side];
        for (int32_t I = 3; I != 0; --I)
        {
            IVP_U_Point Pos; // local_ac
            IVP_Compact_Ledge_Solver::calc_pos_other_space(Point, PointCache, TriangleCache, &Pos);
            IVP_Unscaled_QR_Result QR; // local_78
            IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(TriangleCache->compact_ledge, Triangle, &Pos, &QR);
            if (((IVP_MinimizeFloatBits(QR.Checks[1]) | IVP_MinimizeFloatBits(QR.Checks[2]) |
                  IVP_MinimizeFloatBits(QR.Checks[0])) & 0x80000000u) == 0)
            {
                const double Dist = (((double)(Hesse.k[2]) * Pos.k[2] + (double)(Hesse.k[1]) * Pos.k[1]) +
                                     (double)(Pos.k[0]) * Hesse.k[0]) +
                                    Hesse.k[3];
                const double QuadDist = Dist * Dist;
                if (!(QuadDist * 1.000001f >= MinQuadDist)) // DAT_063a5650
                {
                    MinQuadDist = (float)(QuadDist);
                    PointCache->synapse->edge = Point;
                    PointCache->synapse->status = IVP_SYNAPSE_POINT;
                    TriangleCache->synapse->edge = Triangle;
                    TriangleCache->synapse->status = IVP_SYNAPSE_POLY;
                }
            }
            Point = Point->GetNextEdge();
        }
    }

    for (int32_t Side = 0; Side < 2; ++Side)
    {
        IVP_Compact_Edge* Point = TriangleEdges[Side];                   // EDI
        IVP_Cache_Ledge_Point* const PointCache = TriangleCaches[Side];  // EBP
        IVP_Cache_Ledge_Point* const EdgeCache = PointCaches[Side];      // EBX
        IVP_Compact_Edge* const FirstEdge = PointEdges[Side];            // local_94
        for (int32_t I = 3; I != 0; --I)
        {
            IVP_U_Point Pos; // local_78
            IVP_Compact_Ledge_Solver::calc_pos_other_space(Point, PointCache, EdgeCache, &Pos);
            IVP_Compact_Edge* Edge = FirstEdge;
            for (int32_t J = 3; J != 0; --J)
            {
                IVP_Unscaled_S_Result S; // local_ac
                IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(EdgeCache->compact_ledge, Edge, &Pos, &S);
                if (((IVP_MinimizeFloatBits(S.Checks[1]) | IVP_MinimizeFloatBits(S.Checks[0])) & 0x80000000u) == 0)
                {
                    const double QuadDist =
                        IVP_Compact_Ledge_Solver::quad_dist_edge_to_point_K_space(EdgeCache->compact_ledge, Edge, &Pos);
                    if (!(QuadDist * 1.000001f >= MinQuadDist))
                    {
                        MinQuadDist = (float)(QuadDist);
                        EdgeCache->synapse->edge = Edge;
                        EdgeCache->synapse->status = IVP_SYNAPSE_EDGE;
                        PointCache->synapse->edge = Point;
                        PointCache->synapse->status = IVP_SYNAPSE_POINT;
                    }
                }
                Edge = Edge->GetNextEdge();
            }
            Point = Point->GetNextEdge();
        }
    }

    {
        IVP_Compact_Edge* K = F0;
        for (int32_t I = 3; I != 0; --I)
        {
            IVP_Compact_Edge* L = F1;
            for (int32_t J = 3; J != 0; --J)
            {
                const IVP_KK_Input KKInput(K, L, Cache0, Cache1); // local_68
                IVP_Unscaled_KK_Result Result;                   // local_bc
                IVP_Compact_Ledge_Solver::calc_unscaled_KK_vals(&KKInput, &Result);
                if (((IVP_MinimizeFloatBits(Result.checks_K[1]) | IVP_MinimizeFloatBits(Result.checks_K[0])) & 0x80000000u) == 0 &&
                    ((IVP_MinimizeFloatBits(Result.checks_L[0]) | IVP_MinimizeFloatBits(Result.checks_L[1])) & 0x80000000u) == 0)
                {
                    const double QuadDist = KKInput.calc_quad_distance_edge_edge();
                    if (!(QuadDist * 1.000001f >= MinQuadDist))
                    {
                        MinQuadDist = (float)(QuadDist);
                        Cache0->synapse->edge = K;
                        Cache0->synapse->status = IVP_SYNAPSE_EDGE;
                        Cache1->synapse->edge = L;
                        Cache1->synapse->status = IVP_SYNAPSE_EDGE;
                    }
                }
                L = L->GetNextEdge();
            }
            K = K->GetNextEdge();
        }
    }

    IVP_Cache_Ledge_Point* FirstCache = Cache0;
    IVP_Cache_Ledge_Point* SecondCache = Cache1;
    if (Cache1->synapse->status == IVP_SYNAPSE_POINT && Cache0->synapse->status != IVP_SYNAPSE_POINT)
    {
        FirstCache = Cache1;
        SecondCache = Cache0;
    }
    IVP_Synapse_Real* const FirstSynapse = FirstCache->synapse;
    IVP_Synapse_Real* const SecondSynapse = SecondCache->synapse;

    IVP_Ledge_Mindist* const Mindist = mindist;
    if (FirstCache->synapse != Mindist->get_sorted_synapse(0))
        Mindist->StateFlags ^= 0x100u;

    IVP_Compact_Edge* const FirstEdge = FirstSynapse->edge;
    IVP_Compact_Edge* const SecondEdge = SecondSynapse->edge;
    if (FirstSynapse->status == IVP_SYNAPSE_POINT)
    {
        if (SecondSynapse->status == IVP_SYNAPSE_POINT)
            return p_minimize_PP(FirstEdge, SecondEdge, FirstCache, SecondCache);
        if (SecondSynapse->status == IVP_SYNAPSE_EDGE)
            return p_minimize_PK(FirstEdge, SecondEdge, FirstCache, SecondCache);
        if (SecondSynapse->status == IVP_SYNAPSE_POLY)
            return p_minimize_PF(FirstEdge, SecondEdge, FirstCache, SecondCache);
        IVP_CoreTrap();
        return IVP_MRC_UNINITIALIZED;
    }
    if (FirstSynapse->status == IVP_SYNAPSE_EDGE)
    {
        if (SecondSynapse->status == IVP_SYNAPSE_EDGE)
            return p_minimize_KK(FirstEdge, SecondEdge, FirstCache, SecondCache);
        IVP_CoreTrap();
        return IVP_MRC_UNINITIALIZED;
    }
    IVP_CoreTrap();
    return IVP_MRC_UNINITIALIZED;
}

// ---------------------------------------------------------------------------
// FUN_063645d0, thiscall, RET 0xc. Loop guard -> ENDLESS_LOOP. Gap / normal
// from the ball centre (cache object +0x4c) to P. Then P's vertex fan for
// the edge the ball centre projects onto most positively: with s1 > 0 on
// it, s0 < 0 (or unordered) recurses BP on the neighbour, else BK; otherwise
// settle on {P, point}, OK.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_BP(IVP_Cache_Ball* Ball, IVP_Compact_Edge* P, IVP_Cache_Ledge_Point* CacheP)
{
    if (--P_Finish_Counter < 0)
    {
        if (check_loop_hash(IVP_SYNAPSE_BALL, nullptr, IVP_SYNAPSE_POINT, P) != 0)
            return IVP_MRC_ENDLESS_LOOP;
    }

    IVP_U_Point WorldP; // local_10
    IVP_Compact_Ledge_Solver::give_world_coords_AT(P, CacheP, &WorldP);
    const IVP_U_Point* const BallCenter = &Ball->cache_object->WorldTransform.vv;
    IVP_U_Point BallInP; // local_20
    IVP_TransformWorldToSlotSpace(CacheP->cache_object, BallCenter, &BallInP);

    IVP_U_Point Normal; // local_30
    Normal.k[1] = (float)((double)(BallCenter->k[1]) - WorldP.k[1]);
    Normal.k[2] = (float)((double)(BallCenter->k[2]) - WorldP.k[2]);
    Normal.k[0] = (float)((double)(BallCenter->k[0]) - WorldP.k[0]);
    const double Length = Normal.real_length_plus_normize(); // FUN_06347e90

    IVP_Ledge_Mindist* const Mindist = mindist;
    Mindist->SeparatingNormal.z = Normal.k[2];
    Mindist->SeparatingNormal.x = Normal.k[0];
    Mindist->SeparatingNormal.y = Normal.k[1];
    Mindist->SeparationGap = (float)(Length - Mindist->CombinedRadius);

    {
        const IVP_NarrowPhaseCacheSlot* const CacheObjectBall = Ball->cache_object;
        const IVP_NarrowPhaseCacheSlot* const CacheObjectP = CacheP->cache_object;
        const double Sx = (double)(CacheObjectBall->Secondary.x) - CacheObjectP->Secondary.x;
        const double Sy = (double)(CacheObjectBall->Secondary.y) - CacheObjectP->Secondary.y;
        const double Sz = (double)(CacheObjectBall->Secondary.z) - CacheObjectP->Secondary.z;
        Mindist->SecondaryNormalDot = (float)((Sy * Mindist->SeparatingNormal.y + Sz * Mindist->SeparatingNormal.z) +
                                                         Sx * Mindist->SeparatingNormal.x);
    }

    const IVP_U_Float_Point* const V = CacheP->compact_poly_points + P->GetStartPointIndex();
    const float Vx = (float)((double)(BallInP.k[0]) - V->X); // local_40
    const float Vy = (float)((double)(BallInP.k[1]) - V->Y); // local_3c
    const double VzExtended = (double)(BallInP.k[2]) - V->Z;
    const float Vz = (float)(VzExtended); // local_38 (FST)
    const float DotV = (float)((VzExtended * V->Z + (double)(Vx) * V->X) + (double)(Vy) * V->Y); // local_50

    IVP_Compact_Edge* const StopEdge = P->GetPrevEdge();
    IVP_Compact_Edge* Neighbour = StopEdge->GetOppositeEdge()->GetPrevEdge();
    float MaxValue = 0.0f;                // local_58
    IVP_Compact_Edge* BestEdge = nullptr; // param_3 slot
    for (;;)
    {
        const IVP_U_Float_Point* const Q = CacheP->compact_poly_points + Neighbour->GetStartPointIndex();
        const double ProjectionExtended =
            (((double)(Vx) * Q->X + (double)(Vy) * Q->Y) + (double)(Vz) * Q->Z) - DotV;
        const float Projection = (float)(ProjectionExtended); // local_4c
        if (ProjectionExtended > 0.0)
        {
            const double Wx = (double)(Q->X) - V->X;
            const double Wy = (double)(Q->Y) - V->Y;
            const double Wz = (double)(Q->Z) - V->Z;
            const float QuadEdge = (float)((Wz * Wz + Wy * Wy) + Wx * Wx);
            const double Value = IVP_InvSqrtFloat(QuadEdge) * Projection;
            if (Value > MaxValue)
            {
                MaxValue = (float)(Value);
                BestEdge = Neighbour;
            }
        }
        if (Neighbour == StopEdge)
            break;
        Neighbour = Neighbour->GetOppositeEdge()->GetPrevEdge();
    }

    if (BestEdge != nullptr)
    {
        IVP_Unscaled_S_Result S; // local_48
        IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(CacheP->compact_ledge, BestEdge, &BallInP, &S);
        if (S.Checks[1] > 0.0f)
        {
            if (!(S.Checks[0] >= 0.0f))
                return p_minimize_BP(Ball, BestEdge, CacheP);
            return p_minimize_BK(Ball, BestEdge, CacheP);
        }
    }

    CacheP->synapse->edge = P;
    CacheP->synapse->status = IVP_SYNAPSE_POINT;
    return IVP_MRC_OK;
}

// ---------------------------------------------------------------------------
// FUN_06364ce0, thiscall, RET 0xc. s0 < 0 -> BP with K's start, s1 < 0 -> BP
// with its end (unordered counts as negative), else Leave_BK.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_BK(IVP_Cache_Ball* Ball, IVP_Compact_Edge* K, IVP_Cache_Ledge_Point* CacheK)
{
    IVP_U_Point BallInK; // local_10
    IVP_TransformWorldToSlotSpace(CacheK->cache_object, &Ball->cache_object->WorldTransform.vv, &BallInK);
    IVP_Unscaled_S_Result S; // local_18
    IVP_Compact_Ledge_Solver::calc_unscaled_s_val_K_space(CacheK->compact_ledge, K, &BallInK, &S);
    if (!(S.Checks[0] >= 0.0f))
        return p_minimize_BP(Ball, K, CacheK);
    if (!(S.Checks[1] >= 0.0f))
        return p_minimize_BP(Ball, K->GetNextEdge(), CacheK);
    return p_minimize_Leave_BK(Ball, K, CacheK);
}

// ---------------------------------------------------------------------------
// FUN_06364d90, thiscall, RET 0xc: the ball centre projects onto the inside of
// edge K. As Leave_PK: inside either adjacent face -> BF (preferring the
// opposite face when the ball is above it); below both faces by more than
// 1e-10f (DAT_063a589c; unordered too) -> {K, status 5}, BacksidePosition,
// BACKSIDE; otherwise gap / normal from e x (e x d), no degenerate branch,
// and {K, edge}, OK.
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_Leave_BK(IVP_Cache_Ball* Ball, IVP_Compact_Edge* K,
                                                              IVP_Cache_Ledge_Point* CacheK)
{
    if (--P_Finish_Counter < 0)
    {
        if (check_loop_hash(IVP_SYNAPSE_BALL, nullptr, IVP_SYNAPSE_EDGE, K) != 0)
            return IVP_MRC_ENDLESS_LOOP;
    }

    IVP_U_Point BallInK; // local_70
    IVP_TransformWorldToSlotSpace(CacheK->cache_object, &Ball->cache_object->WorldTransform.vv, &BallInK);

    const IVP_U_Float_Point* const PointsK = CacheK->compact_poly_points;
    IVP_Compact_Edge* const OppositeK = K->GetOppositeEdge();
    const IVP_U_Float_Point* const K0 = PointsK + K->GetStartPointIndex();
    const IVP_U_Float_Point* const K1 = PointsK + K->GetNextEdge()->GetStartPointIndex();
    const IVP_U_Float_Point* const R = PointsK + K->GetPrevEdge()->GetStartPointIndex();
    const IVP_U_Float_Point* const S = PointsK + OppositeK->GetPrevEdge()->GetStartPointIndex();

    IVP_U_Point EdgeVec; // local_98
    EdgeVec.k[1] = (float)((double)(K1->Y) - K0->Y);
    EdgeVec.k[2] = (float)((double)(K1->Z) - K0->Z);
    EdgeVec.k[0] = (float)((double)(K1->X) - K0->X);
    IVP_U_Point Delta; // local_80
    Delta.k[1] = (float)((double)(BallInK.k[1]) - K0->Y);
    Delta.k[2] = (float)((double)(BallInK.k[2]) - K0->Z);
    Delta.k[0] = (float)((double)(BallInK.k[0]) - K0->X);
    IVP_U_Point ToThird; // local_30
    ToThird.k[1] = (float)((double)(R->Y) - K0->Y);
    ToThird.k[2] = (float)((double)(R->Z) - K0->Z);
    ToThird.k[0] = (float)((double)(R->X) - K0->X);
    IVP_U_Point ToOpposite; // local_40
    ToOpposite.k[1] = (float)((double)(S->Y) - K0->Y);
    ToOpposite.k[2] = (float)((double)(S->Z) - K0->Z);
    ToOpposite.k[0] = (float)((double)(S->X) - K0->X);

    IVP_U_Point NormalK; // local_60
    NormalK.calc_cross_product(&EdgeVec, &ToThird);
    IVP_U_Point NormalOpposite; // local_50
    NormalOpposite.calc_cross_product(&ToOpposite, &EdgeVec);

    const float HeightK = (float)(
        ((double)(Delta.k[2]) * NormalK.k[2] + (double)(Delta.k[1]) * NormalK.k[1]) +
        (double)(Delta.k[0]) * NormalK.k[0]); // local_b4
    const float HeightOpposite = (float)(
        ((double)(Delta.k[2]) * NormalOpposite.k[2] + (double)(Delta.k[1]) * NormalOpposite.k[1]) +
        (double)(NormalOpposite.k[0]) * Delta.k[0]); // local_b0

    IVP_Unscaled_QR_Result QRK; // local_10
    IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(CacheK->compact_ledge, K, &BallInK, &QRK);
    IVP_Unscaled_QR_Result QROpposite; // local_20
    IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(CacheK->compact_ledge, OppositeK, &BallInK, &QROpposite);

    if (QRK.Checks[0] > 0.0f)
    {
        if (QROpposite.Checks[0] > 0.0f && HeightOpposite > 0.0f)
            return p_minimize_BF(Ball, OppositeK, CacheK);
        return p_minimize_BF(Ball, K, CacheK);
    }
    if (QROpposite.Checks[0] > 0.0f)
        return p_minimize_BF(Ball, OppositeK, CacheK);

    if (!(HeightK >= -1.0e-10f) && !(HeightOpposite >= -1.0e-10f)) // DAT_063a589c
    {
        BacksidePosition.k[0] = BallInK.k[0];
        BacksidePosition.k[1] = BallInK.k[1];
        BacksidePosition.k[2] = BallInK.k[2];
        CacheK->synapse->edge = K;
        CacheK->synapse->status = IVP_ST_BACKSIDE;
        return IVP_MRC_BACKSIDE;
    }

    IVP_U_Point Perpendicular; // local_a8
    Perpendicular.calc_cross_product(&EdgeVec, &Delta);
    const float InvQuadEdge = (float)(
        1.0 / (((double)(EdgeVec.k[2]) * EdgeVec.k[2] + (double)(EdgeVec.k[1]) * EdgeVec.k[1]) +
               (double)(EdgeVec.k[0]) * EdgeVec.k[0])); // local_b4
    const float QuadDist = (float)(
        (((double)(Perpendicular.k[2]) * Perpendicular.k[2] + (double)(Perpendicular.k[1]) * Perpendicular.k[1]) +
         (double)(Perpendicular.k[0]) * Perpendicular.k[0]) *
        InvQuadEdge); // local_84
    const float InvDist = (float)(IVP_InvSqrtFloat(QuadDist)); // local_88

    IVP_Ledge_Mindist* const Mindist = mindist;
    Mindist->SeparationGap = (float)((double)(InvDist) * QuadDist - Mindist->CombinedRadius);
    Perpendicular.calc_cross_product(&EdgeVec, &Perpendicular);
    IVP_RotateSlotToWorldSpace(CacheK->cache_object, &Perpendicular, &Perpendicular); // 0634d9d0, in place
    const double Scale = -((double)(InvDist) * InvQuadEdge);
    Mindist->SeparatingNormal.y = (float)(Perpendicular.k[1] * Scale);
    Mindist->SeparatingNormal.z = (float)(Perpendicular.k[2] * Scale);
    Mindist->SeparatingNormal.x = (float)(Scale * Perpendicular.k[0]);

    {
        const IVP_NarrowPhaseCacheSlot* const CacheObjectBall = Ball->cache_object;
        const IVP_NarrowPhaseCacheSlot* const CacheObjectK = CacheK->cache_object;
        const double Sx = (double)(CacheObjectBall->Secondary.x) - CacheObjectK->Secondary.x;
        const double Sy = (double)(CacheObjectBall->Secondary.y) - CacheObjectK->Secondary.y;
        const double Sz = (double)(CacheObjectBall->Secondary.z) - CacheObjectK->Secondary.z;
        Mindist->SecondaryNormalDot = (float)((Sy * Mindist->SeparatingNormal.y + Sz * Mindist->SeparatingNormal.z) +
                                                         Sx * Mindist->SeparatingNormal.x);
    }

    CacheK->synapse->edge = K;
    CacheK->synapse->status = IVP_SYNAPSE_EDGE;
    return IVP_MRC_OK;
}

// ---------------------------------------------------------------------------
// FUN_06363430, thiscall, RET 0xc. Ball centre over F: normal = F's normized
// Hesse vector in world space, {F, poly}; a negative (or unordered) height ->
// {F, status 5}, BacksidePosition, BACKSIDE; else gap, OK. Outside F: BK on
// the nearest of F's edges (FUN_0635bd10; unordered replaces).
// ---------------------------------------------------------------------------
IVP_MRC_TYPE IVP_Mindist_Minimize_Solver::p_minimize_BF(IVP_Cache_Ball* Ball, IVP_Compact_Edge* F, IVP_Cache_Ledge_Point* CacheF)
{
    IVP_U_Point BallInF; // local_30
    IVP_TransformWorldToSlotSpace(CacheF->cache_object, &Ball->cache_object->WorldTransform.vv, &BallInF);
    IVP_Unscaled_QR_Result QR; // local_20
    IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(CacheF->compact_ledge, F, &BallInF, &QR);

    if (((IVP_MinimizeFloatBits(QR.Checks[1]) | IVP_MinimizeFloatBits(QR.Checks[2]) | IVP_MinimizeFloatBits(QR.Checks[0])) &
         0x80000000u) == 0)
    {
        const IVP_U_Float_Point* const V = CacheF->compact_poly_points + F->GetStartPointIndex();
        IVP_U_Point Hesse; // local_40
        IVP_Compact_Ledge_Solver::calc_hesse_vec_object_not_normized(F, CacheF->compact_ledge, &Hesse);
        Hesse.normize();
        IVP_U_Point HesseWorld; // local_10
        IVP_RotateSlotToWorldSpace(CacheF->cache_object, &Hesse, &HesseWorld); // 0634d9d0

        IVP_Ledge_Mindist* const Mindist = mindist;
        Mindist->SeparatingNormal.x = HesseWorld.k[0];
        Mindist->SeparatingNormal.y = HesseWorld.k[1];
        Mindist->SeparatingNormal.z = HesseWorld.k[2];

        const double DistBall = ((double)(BallInF.k[2]) * Hesse.k[2] + (double)(BallInF.k[1]) * Hesse.k[1]) +
                                (double)(Hesse.k[0]) * BallInF.k[0];
        const double DistV = ((double)(Hesse.k[2]) * V->Z + (double)(Hesse.k[1]) * V->Y) +
                             (double)(Hesse.k[0]) * V->X;
        CacheF->synapse->edge = F;
        CacheF->synapse->status = IVP_SYNAPSE_POLY;
        const double Dist = DistBall - DistV;
        if (!(Dist >= 0.0))
        {
            CacheF->synapse->edge = F;
            CacheF->synapse->status = IVP_ST_BACKSIDE;
            BacksidePosition.k[0] = BallInF.k[0];
            BacksidePosition.k[1] = BallInF.k[1];
            BacksidePosition.k[2] = BallInF.k[2];
            return IVP_MRC_BACKSIDE;
        }

        Mindist->SeparationGap = (float)(Dist - Mindist->CombinedRadius);
        const IVP_NarrowPhaseCacheSlot* const CacheObjectBall = Ball->cache_object;
        const IVP_NarrowPhaseCacheSlot* const CacheObjectF = CacheF->cache_object;
        const double Sx = (double)(CacheObjectBall->Secondary.x) - CacheObjectF->Secondary.x;
        const double Sy = (double)(CacheObjectBall->Secondary.y) - CacheObjectF->Secondary.y;
        const double Sz = (double)(CacheObjectBall->Secondary.z) - CacheObjectF->Secondary.z;
        Mindist->SecondaryNormalDot = (float)((Sy * Mindist->SeparatingNormal.y + Sz * Mindist->SeparatingNormal.z) +
                                                         Sx * Mindist->SeparatingNormal.x);
        return IVP_MRC_OK;
    }

    float MinQuadDist = MinimizeSearchStart; // param_3 slot
    IVP_Compact_Edge* BestEdge = nullptr;    // param_2 slot
    IVP_Compact_Edge* Edge = F;
    for (int32_t I = 3; I != 0; --I)
    {
        const double QuadDist = IVP_Compact_Ledge_Solver::calc_qlen_PK_K_space(&BallInF, CacheF->compact_ledge, Edge);
        if (!(QuadDist >= MinQuadDist))
        {
            MinQuadDist = (float)(QuadDist);
            BestEdge = Edge;
        }
        Edge = Edge->GetNextEdge();
    }
    return p_minimize_BK(Ball, BestEdge, CacheF);
}
