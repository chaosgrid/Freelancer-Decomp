// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "ivp_great_matrix.h"
#include "ivp_mindist.h"
#include "../ivp_utility/ivp_u_vector.h"

class IVP_Environment;
class IVP_Event_Sim;

// ============================================================================
// IVP_Friction_Solver: the stack object of the friction pass. Built only by
// IVP_ListManager::DoPushesDistanceKeepers (FUN_06368f60) on its stack, whose
// frame bounds the size: 0x2c + 0x200 * 4 = 0x82c bytes. Method names follow
// the Ipion SDK roles (setup_coords_mindists, calc_solver_PSI, ...).
// ============================================================================
class IVP_Friction_Solver
{
public:
    IVP_Great_Matrix_Many_Zero DistChangeMat;               // +0x00; Columns = CtxListCount - ComplexNotNecessaryNumber, buffers from the env arena (+0xa0)
    float ImpulseScale;                                     // +0x18; set by NormizeConstraintEqu, not by the ctor
    IVP_Environment* Environment;                           // +0x1c
    const IVP_Event_Sim* EventSim;                          // +0x20
    // +0x24: every contact record (the contact point's +0x40 owner) in matrix
    // order; 0x200 inline slots at +0x2c.
    IVP_U_Vector_Fixed<IVP_Impact_Solver_Long_Term, 0x200> DistsOfSystem;   // +0x24 (inline slots +0x2c)

    // 06355380
    IVP_Friction_Solver(IVP_ListManager* FrictionSystem, const IVP_Event_Sim* EventSimIn);

    // 06368970
    void SetupCoordsMindists(IVP_ListManager* FrictionSystem);

    // 06368a30: fills the matrix and right-hand side, writes the matrix
    // indices of the "supposed active" dists to ActiveIsAtPos and returns how
    // many there are.
    int CalcSolverPSI(IVP_ListManager* FrictionSystem, int* ActiveIsAtPos);

    // 06368560
    void SolveLinearEquationAndPush(IVP_ListManager* FrictionSystem, int* ActiveIsAtPos, int TotalActives,
                                    IVP_U_Memory* Mem);

    // 06368410: scales the matrix by 1/max(diagonal) and the right-hand side
    // by 1/max|rhs|, leaving the product of the two back-scales in ImpulseScale.
    void NormizeConstraintEqu();

    // 06368300: seeds ResultVector from the Gauss sub-solution and checks it;
    // 1 = accept.
    int TestGaussSolutionSuggestion(const float* Values, const int* Indices, int Count, IVP_U_Memory* Mem);

    // 063683f0: ResultVector[i] *= ImpulseScale.
    void ScaleResultVector();

    // 063687b0 (retail returns 0, discarded by its only caller).
    void DoResultingPushes(IVP_ListManager* FrictionSystem);
};

static_assert(offsetof(IVP_Friction_Solver, ImpulseScale) == 0x18, "IVP_Friction_Solver+0x18");
static_assert(offsetof(IVP_Friction_Solver, Environment) == 0x1c, "IVP_Friction_Solver+0x1c");
static_assert(offsetof(IVP_Friction_Solver, EventSim) == 0x20, "IVP_Friction_Solver+0x20");
static_assert(offsetof(IVP_Friction_Solver, DistsOfSystem) == 0x24, "IVP_Friction_Solver+0x24");
static_assert(sizeof(IVP_Friction_Solver) == 0x82c, "IVP_Friction_Solver is 0x82c bytes");
