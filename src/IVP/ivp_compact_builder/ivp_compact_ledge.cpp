// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_compact_ledge.h"

#include <cstdint>
#include <vector>

#include "../ivp_collision/ivp_compact_ledge_solver.h"

// Retail FUN_06353840, cdecl. A weight that compares unordered counts as
// not positive. Retail keeps the visited flags in an alloca'd byte array
// (FUN_06391dc0); the port uses a std::vector.
IVP_Compact_Edge* IVP_WalkPiercingTriangleChain(IVP_Compact_Edge* StartEdge, const IVP_U_Point* Position)
{
    IVP_Compact_Ledge* Ledge = StartEdge->GetCompactTriangle()->GetCompactLedge();
    const uint32_t PierceIndex = StartEdge->GetCompactTriangle()->GetPierceIndex();

    std::vector<uint8_t> Visited(static_cast<size_t>(Ledge->GetNTriangles()), 0);

    IVP_Compact_Edge* Edge = Ledge->GetFirstTriangle()[PierceIndex].GetFirstEdge();
    for (;;)
    {
        Visited[Edge->GetCompactTriangle()->GetTriIndex()] = 1;

        IVP_Unscaled_QR_Result Weights;
        IVP_Compact_Ledge_Solver::calc_unscaled_qr_vals_F_space(Ledge, Edge, Position, &Weights);

        IVP_Compact_Edge* Candidate = Edge;
        int32_t Count = 0;
        for (;;)
        {
            const bool bPositive = Weights.Checks[Count] > 0.0f;
            if (!bPositive && Visited[Candidate->GetOppositeEdge()->GetCompactTriangle()->GetTriIndex()] == 0)
                break;

            Candidate = Candidate->GetNextEdge();
            ++Count;
            if (Count >= 3)
                return Edge;
        }

        Edge = Candidate->GetOppositeEdge();
    }
}
