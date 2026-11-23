// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP friction pass (retail 06355380-0635558b and 06368300-063691e4):
// IVP_Friction_Solver (the stack object built by the pass) and the friction
// system's own methods (IVP_ListManager = Ipion's IVP_Friction_System). The
// linear algebra is in ivp_great_matrix.cpp.
//
// x87: retail keeps intermediates on the FPU stack (53-bit precision under the
// VC6 runtime). Such values are `double` here and cast to float where retail
// stores a float; comparisons are written so NaN takes retail's branch.

#include "ivp_friction_solver.h"
#include "ivp_environment.h"
#include "ivp_controller.h"
#include "ivp_mindist_settings.h"
#include "ivp_simulation_unit.h"
#include "../ivp_utility/ivp_core_trap.h"
#include "../ivp_collision/ivp_listmanager_topology.h" // IVP_UnlinkPairRecordFromObjects (FUN_06355200), IVP_UpdatePairRecordContactFrame

#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
    // DAT_0658ab8c: incremented once per friction pass; never read.
    int32_t G_IVP_FrictionPassCount = 0;

    unsigned IVP_FrictionKey(const IVP_ListManager* FrictionSystem)
    {
        return static_cast<unsigned>(reinterpret_cast<uintptr_t>(FrictionSystem));
    }

    // FUN_0628be40 (this = A; B): (A.z*B.z + A.y*B.y) + A.x*B.x, left in ST0.
    double IVP_Dot(const Vector& A, const Vector& B)
    {
        const double Z = (double)(B.z) * (double)(A.z);
        const double Y = (double)(B.y) * (double)(A.y);
        const double X = (double)(B.x) * (double)(A.x);
        return (Z + Y) + X;
    }

    // FUN_0628be10 (this = Dst; Src, Scale): Dst = Src * Scale (Scale is a float argument).
    void IVP_SetMultiple(Vector& Dst, const Vector& Src, float Scale)
    {
        const float Y = (float)((double)(Scale) * (double)(Src.y));
        const float Z = (float)((double)(Scale) * (double)(Src.z));
        const float X = (float)((double)(Scale) * (double)(Src.x));
        Dst.x = X;
        Dst.y = Y;
        Dst.z = Z;
    }

    // FUN_0628be60 (this = Dst; A, B, Scale): Dst = A + B * Scale, all three
    // components loaded before the first store.
    void IVP_AddMultiple(Vector& Dst, const Vector& A, const Vector& B, float Scale)
    {
        const float Y = (float)((double)(Scale) * (double)(B.y) + (double)(A.y));
        const float Z = (float)((double)(Scale) * (double)(B.z) + (double)(A.z));
        const float X = (float)((double)(Scale) * (double)(B.x) + (double)(A.x));
        Dst.x = X;
        Dst.y = Y;
        Dst.z = Z;
    }

    // FUN_06368780 (this = Out; A, B, RET 8): Out = A (x) B componentwise.
    void IVP_SetPairwiseMult(Vector& Out, const Vector& A, const Vector& B)
    {
        const float Y = (float)((double)(A.y) * (double)(B.y));
        const float Z = (float)((double)(A.z) * (double)(B.z));
        const float X = (float)((double)(A.x) * (double)(B.x));
        Out.z = Z;
        Out.x = X;
        Out.y = Y;
    }

    // FUN_06348180 (this = Dst; V): Dst = -V, all loads before the stores.
    void IVP_SetNegative(Vector& Dst, const Vector& V)
    {
        const float Y = -V.y;
        const float Z = -V.z;
        const float X = -V.x;
        Dst.x = X;
        Dst.z = Z;
        Dst.y = Y;
    }

    // FUN_06368d30 (cdecl, 4 args): dot(LongTerm->CoordOSCrossNormal[Side], RotArm)
    // + dot(LongTerm->ContactNormalWS, LinArm), in retail's association order:
    //   ((Cz*Rz + Cy*Ry) + Cx*Rx) + ((Nz*Lz + Ny*Ly) + Nx*Lx)
    double IVP_CrossAndNormalDot(const IVP_Impact_Solver_Long_Term* LongTerm, int Side, const Vector& RotArm, const Vector& LinArm)
    {
        const Vector& Cross = LongTerm->CoordOSCrossNormal[Side];
        const Vector& Normal = LongTerm->ContactNormalWS;
        const double CrossPart = ((double)(Cross.z) * (double)(RotArm.z) +
                                  (double)(Cross.y) * (double)(RotArm.y)) +
                                 (double)(Cross.x) * (double)(RotArm.x);
        const double NormalPart = ((double)(Normal.z) * (double)(LinArm.z) +
                                   (double)(Normal.y) * (double)(LinArm.y)) +
                                  (double)(Normal.x) * (double)(LinArm.x);
        return CrossPart + NormalPart;
    }
}

// ============================================================================
// IVP_Friction_Solver
// ============================================================================

// 06355380. DistChangeMat (06366400) and DistsOfSystem (inline buffer at
// +0x2c, memsize 0x200) are member constructors.
IVP_Friction_Solver::IVP_Friction_Solver(IVP_ListManager* FrictionSystem, const IVP_Event_Sim* EventSimIn)
{
    Environment = FrictionSystem->Environment;
    EventSim = EventSimIn;

    // Both counts sign-extended.
    const int Columns = static_cast<int>(static_cast<int16_t>(FrictionSystem->CtxListCount)) -
                        static_cast<int>(FrictionSystem->ComplexNotNecessaryNumber);
    DistChangeMat.Columns = Columns;
    DistChangeMat.AlignedRowLen = (Columns + 3) & ~3;

    // Three arena allocations, in this order.
    IVP_U_Memory* Mem = Environment->vector_cores_active;
    DistChangeMat.MatrixValues = static_cast<float*>(Mem->GetMem(DistChangeMat.Columns * DistChangeMat.AlignedRowLen * 4 + 0xc));
    DistChangeMat.DesiredVector = static_cast<float*>(Mem->GetMem(DistChangeMat.AlignedRowLen * 4));
    DistChangeMat.ResultVector = static_cast<float*>(Mem->GetMem(DistChangeMat.AlignedRowLen * 4));
    DistChangeMat.AlignMatrixValues(); // 063661a0
}

// 06368970
void IVP_Friction_Solver::SetupCoordsMindists(IVP_ListManager* FrictionSystem)
{
    int Position = 0;
    int MatrixIndex = 0;

    for (IVP_Contact_Point* Dist = FrictionSystem->CtxListHead; Dist != nullptr; Dist = Dist->ListManagerListNext)
    {
        IVP_Impact_Solver_Long_Term* LongTerm = Dist->Owner;
        // The skip branch does not advance Position.
        if (Position < static_cast<int>(FrictionSystem->ComplexNotNecessaryNumber))
        {
            LongTerm->IndexInFs = -1;
            continue;
        }

        ++Position;
        LongTerm->FrictionInfos[0] = static_cast<IVP_Friction_Info_For_Core*>(
            Dist->Object1->core->find_object_hash_entry(IVP_FrictionKey(FrictionSystem)));
        LongTerm->FrictionInfos[1] = static_cast<IVP_Friction_Info_For_Core*>(
            Dist->Object2->core->find_object_hash_entry(IVP_FrictionKey(FrictionSystem)));
        LongTerm->GapDistance = Dist->GapDistance;
        LongTerm->HasNegativePullSince = Dist->HasNegativePullSince;
        DistsOfSystem.Add(LongTerm);
        LongTerm->IndexInFs = static_cast<int16_t>(MatrixIndex);
        ++MatrixIndex;
    }
}

// 06368a30
int IVP_Friction_Solver::CalcSolverPSI(IVP_ListManager* FrictionSystem, int* ActiveIsAtPos)
{
    // Zeroed from the last element down.
    for (int K = DistChangeMat.Columns * DistChangeMat.AlignedRowLen - 1; K >= 0; --K)
    {
        DistChangeMat.MatrixValues[K] = 0.0f;
    }

    int TotalActives = 0;
    for (int I = 0; I < static_cast<int>(DistsOfSystem.n_elems); ++I)
    {
        IVP_Impact_Solver_Long_Term* LongTerm = DistsOfSystem.elems[I];

        // Relative normal speed of the two cores at the contact.
        float RelSpeed = 0.0f;
        IVP_Core* CoreA = LongTerm->ChildCandidate[0];
        if (CoreA != nullptr)
        {
            const float RotPart = (float)(IVP_Dot(CoreA->RotSpeed, LongTerm->CoordOSCrossNormal[0]));
            RelSpeed = (float)(IVP_Dot(CoreA->Speed, LongTerm->ContactNormalWS) + (double)(RotPart));
        }
        IVP_Core* CoreB = LongTerm->ChildCandidate[1];
        if (CoreB != nullptr)
        {
            const float RotPart = (float)(IVP_Dot(CoreB->RotSpeed, LongTerm->CoordOSCrossNormal[1]));
            const double Part = IVP_Dot(CoreB->Speed, LongTerm->ContactNormalWS) + (double)(RotPart);
            RelSpeed = (float)((double)(RelSpeed) - Part);
        }

        // Factor 20.0f (DAT_063a543c) when the gap error is negative or NaN.
        const double GapError = (double)(G_IVP_MindistSettings.friction_dist) -
                                (double)(LongTerm->GapDistance);
        double Factor = (double)(1.0f);
        if (!(GapError >= 0.0))
        {
            Factor = (double)(20.0f);
        }
        DistChangeMat.DesiredVector[I] = (float)(Factor * GapError + (double)(RelSpeed));

        if (LongTerm->HasNegativePullSince != 0)
        {
            *ActiveIsAtPos = I;
            ++ActiveIsAtPos;
            ++TotalActives;
        }

        // Core A's column entries, with the arms negated.
        CoreA = LongTerm->ChildCandidate[0];
        if (CoreA != nullptr)
        {
            Vector RotArm;
            IVP_SetPairwiseMult(RotArm, LongTerm->CoordOSCrossNormal[0], CoreA->InvRotInertia);
            IVP_SetNegative(RotArm, RotArm);
            Vector LinArm;
            const float LinScale = (float)((double)(CoreA->InvMass) * (double)(-1.0f)); // DAT_063a5498
            IVP_SetMultiple(LinArm, LongTerm->ContactNormalWS, LinScale);

            IVP_Friction_Info_For_Core* Info = LongTerm->FrictionInfos[0];
            IVP_Core* ThisCore = LongTerm->ChildCandidate[0];
            for (int J = 0; J < static_cast<int>(Info->FrDists.n_elems); ++J)
            {
                const int Row = static_cast<int>(Info->FrDists.elems[J]->Owner->IndexInFs);
                if (Row < 0)
                {
                    continue;
                }
                IVP_Impact_Solver_Long_Term* Other = DistsOfSystem.elems[Row];
                int Side = 1;
                float Sign = 1.0f;
                if (Other->ChildCandidate[0] == ThisCore)
                {
                    Side = 0;
                    Sign = -1.0f;
                }
                if (Other->ChildCandidate[Side] == nullptr)
                {
                    continue;
                }
                const float RotDot = (float)(IVP_Dot(RotArm, Other->CoordOSCrossNormal[Side]));
                const double Sum = IVP_Dot(LinArm, Other->ContactNormalWS) + (double)(RotDot);
                float* Cell = &DistChangeMat.MatrixValues[Row * DistChangeMat.AlignedRowLen + I];
                *Cell = (float)(Sum * (double)(Sign) + (double)(*Cell));
            }
        }

        // Core B's column entries.
        CoreB = LongTerm->ChildCandidate[1];
        if (CoreB != nullptr)
        {
            Vector RotArm;
            IVP_SetPairwiseMult(RotArm, LongTerm->CoordOSCrossNormal[1], CoreB->InvRotInertia);
            Vector LinArm;
            IVP_SetMultiple(LinArm, LongTerm->ContactNormalWS, CoreB->InvMass);

            IVP_Friction_Info_For_Core* Info = LongTerm->FrictionInfos[1];
            IVP_Core* ThisCore = LongTerm->ChildCandidate[1];
            for (int J = 0; J < static_cast<int>(Info->FrDists.n_elems); ++J)
            {
                const int Row = static_cast<int>(Info->FrDists.elems[J]->Owner->IndexInFs);
                if (Row < 0)
                {
                    continue;
                }
                IVP_Impact_Solver_Long_Term* Other = DistsOfSystem.elems[Row];
                int Side = 1;
                float Sign = 1.0f;
                if (Other->ChildCandidate[0] == ThisCore)
                {
                    Side = 0;
                    Sign = -1.0f;
                }
                if (Other->ChildCandidate[Side] == nullptr)
                {
                    continue;
                }
                const double Value = IVP_CrossAndNormalDot(Other, Side, RotArm, LinArm); // 06368d30
                float* Cell = &DistChangeMat.MatrixValues[Row * DistChangeMat.AlignedRowLen + I];
                *Cell = (float)(Value * (double)(Sign) + (double)(*Cell));
            }
        }
    }

    // Every contact's matrix index must equal its list position.
    int Position = 0;
    for (IVP_Contact_Point* Dist = FrictionSystem->CtxListHead; Dist != nullptr; Dist = Dist->ListManagerListNext)
    {
        const int16_t Index = Dist->Owner->IndexInFs;
        if (Index >= 0 && static_cast<int>(Index) != Position)
        {
            IVP_CoreTrap(); // write to address 0
        }
        ++Position;
    }
    return TotalActives;
}

// 06368410
void IVP_Friction_Solver::NormizeConstraintEqu()
{
    const int Count = DistChangeMat.Columns;
    const int Last = Count - 1;

    // Largest diagonal element; only a strictly greater, ordered value replaces it.
    float MaxDiag = 0.0f;
    double DiagScale = (double)(1.0f);
    if (Last >= 0)
    {
        const float* Diag = &DistChangeMat.MatrixValues[(DistChangeMat.AlignedRowLen + 1) * Last];
        const int Stride = -1 - DistChangeMat.AlignedRowLen;
        for (int K = Last + 1; K != 0; --K)
        {
            if (*Diag > MaxDiag)
            {
                MaxDiag = *Diag;
            }
            Diag += Stride;
        }
        // 1e-10f is DAT_063a53d8; not greater or unordered keeps 1.0.
        if (MaxDiag > 1.000000013351432e-10f)
        {
            DiagScale = (double)(1.0f) / (double)(MaxDiag);
        }
    }
    ImpulseScale = (float)(DiagScale); // overwritten below, but retail stores it

    // Largest |rhs|, each candidate rounded to float.
    float RhsScale = 1.0f;
    double BackScale = (double)(1.0f);
    if (Last >= 0)
    {
        double MaxRhs = (double)(0.0f);
        for (int I = Last; I >= 0; --I)
        {
            const float AbsValue = std::fabs(DistChangeMat.DesiredVector[I]);
            if ((double)(AbsValue) > MaxRhs)
            {
                MaxRhs = (double)(AbsValue);
            }
        }
        if (MaxRhs > (double)(1.000000013351432e-10f))
        {
            RhsScale = (float)((double)(1.0f) / MaxRhs);
            BackScale = MaxRhs;                                                 // stays unrounded
        }
    }

    ImpulseScale = (float)(DiagScale * BackScale);

    // Every row, whole aligned width.
    for (int Row = 0; Row < DistChangeMat.Columns; ++Row)
    {
        float* RowValues = &DistChangeMat.MatrixValues[DistChangeMat.AlignedRowLen * Row];
        for (int Col = 0; Col < DistChangeMat.AlignedRowLen; ++Col)
        {
            RowValues[Col] = (float)(DiagScale * (double)(RowValues[Col]));
        }
    }

    for (int I = DistChangeMat.Columns - 1; I >= 0; --I)
    {
        DistChangeMat.DesiredVector[I] = (float)((double)(RhsScale) *
                                                            (double)(DistChangeMat.DesiredVector[I]));
    }
}

// 06368300
int IVP_Friction_Solver::TestGaussSolutionSuggestion(const float* Values, const int* Indices, int Count, IVP_U_Memory* Mem)
{
    int* Visited = static_cast<int*>(Mem->GetMem(DistChangeMat.Columns * 4));
    std::memset(Visited, 0, static_cast<size_t>(DistChangeMat.Columns * 4));

    int bPushTooSmall = 0;
    for (int I = 0; I < Count; ++I)
    {
        const int Index = Indices[I];
        const double Value = (double)(Values[I]);
        const double Threshold = (double)(Environment->field_c4) * (double)(0.01f); // DAT_063a5404
        const double Push = (double)(DistsOfSystem.elems[Index]->EffectiveMassDenom) *
                            (double)(ImpulseScale) * Value;
        // Below threshold or unordered flags it.
        if (!(Push >= Threshold))
        {
            bPushTooSmall = 1;
        }
        Visited[Index] = 1;
        DistChangeMat.ResultVector[Index] = (float)(Value);
    }

    // Every row the Gauss sub-solution did not cover must hold.
    for (int J = 0; J < DistChangeMat.Columns; ++J)
    {
        if (Visited[J] == 0 && DistChangeMat.TestResultRow(J) == 0)
        {
            return 0;
        }
    }
    return bPushTooSmall == 0 ? 1 : 0;
}

// 063683f0
void IVP_Friction_Solver::ScaleResultVector()
{
    for (int I = DistChangeMat.Columns - 1; I >= 0; --I)
    {
        DistChangeMat.ResultVector[I] = (float)((double)(ImpulseScale) *
                                                           (double)(DistChangeMat.ResultVector[I]));
    }
}

// 06368560
void IVP_Friction_Solver::SolveLinearEquationAndPush(IVP_ListManager* FrictionSystem, int* ActiveIsAtPos, int TotalActives,
                                                     IVP_U_Memory* Mem)
{
    // The actives-only matrix, on the stack.
    IVP_Great_Matrix_Many_Zero ActiveMat;
    ActiveMat.Columns = TotalActives;
    ActiveMat.AlignedRowLen = (TotalActives + 3) & ~3;

    NormizeConstraintEqu();

    // rhs, result, matrix: in this order.
    ActiveMat.DesiredVector = static_cast<float*>(Mem->GetMem(ActiveMat.AlignedRowLen * 4));
    ActiveMat.ResultVector = static_cast<float*>(Mem->GetMem(ActiveMat.AlignedRowLen * 4));
    ActiveMat.MatrixValues = static_cast<float*>(Mem->GetMem(ActiveMat.AlignedRowLen * TotalActives * 4 + 0xc));
    ActiveMat.AlignMatrixValues();

    std::memset(DistChangeMat.ResultVector, 0, static_cast<size_t>(DistChangeMat.AlignedRowLen * 4));

    ActiveMat.FillFromBiggerMatrix(&DistChangeMat, ActiveIsAtPos, TotalActives);

    IVP_Linear_Constraint_Solver LinearSolver;

    // Diagonal += 1e-5f (DAT_063a5608).
    for (int I = 0; I < DistChangeMat.Columns; ++I)
    {
        float* Diag = &DistChangeMat.MatrixValues[(DistChangeMat.AlignedRowLen + 1) * I];
        *Diag = (float)((double)(*Diag) + (double)(9.99999974737875e-06f));
    }

    // Try the Gauss solution of the actives first, then the full LCP.
    bool bSolved = false;
    if (ActiveMat.SolveGreatMatrixManyZero() == 1 &&
        TestGaussSolutionSuggestion(ActiveMat.ResultVector, ActiveIsAtPos, TotalActives, Mem) == 1)
    {
        bSolved = true;
    }
    if (!bSolved)
    {
        const int NumActives = FrictionSystem->GetNumSupposedActiveFrDists();
        if (LinearSolver.InitAndSolveLc(DistChangeMat.MatrixValues, DistChangeMat.DesiredVector, DistChangeMat.ResultVector,
                                        DistChangeMat.Columns, NumActives, Mem) != 1)
        {
            return;
        }
    }

    ScaleResultVector();
    const float EnergyBefore = (float)(FrictionSystem->KineticEnergyOfSystem());
    DoResultingPushes(FrictionSystem);
    const float EnergyAfter = (float)(FrictionSystem->KineticEnergyOfSystem());
    const double Allowed = FrictionSystem->MaxEnergyGain() + (double)(EnergyBefore);
    // Undo when the energy grew too much, or unordered.
    if (!(Allowed >= (double)(EnergyAfter)))
    {
        FrictionSystem->UndoComplexPushes();
        return;
    }
    FrictionSystem->ConfirmComplexPushes();
}

// 063687b0
void IVP_Friction_Solver::DoResultingPushes(IVP_ListManager* FrictionSystem)
{
    int Index = 0; // list position, also the ResultVector index
    for (IVP_Contact_Point* Dist = FrictionSystem->CtxListHead; Dist != nullptr; Dist = Dist->ListManagerListNext)
    {
        double Pressure = (double)(0.0f); // what the tail multiplies by i_delta_time
        if (Index >= static_cast<int>(FrictionSystem->ComplexNotNecessaryNumber))
        {
            const float ImpulseF = DistChangeMat.ResultVector[Index];
            const double Impulse = (double)(ImpulseF);
            bool bPush = true;

            // Positive (ordered), zero-or-NaN, negative.
            if (Impulse > 0.0)
            {
                if (Dist->HasNegativePullSince >= 0)
                {
                    Dist->HasNegativePullSince = -1;
                }
                else
                {
                    --Dist->HasNegativePullSince;
                }
            }
            else if (!(Impulse < 0.0))
            {
                Dist->HasNegativePullSince = 0; // then straight to the 0.0 tail
                bPush = false;
            }
            else
            {
                if (Dist->HasNegativePullSince < 0)
                {
                    Dist->HasNegativePullSince = 0;
                }
                const int Count = Dist->HasNegativePullSince + 1;
                Dist->HasNegativePullSince = Count;
                if (Count > 9)
                {
                    Dist->HasNegativePullSince = 0;
                }
            }

            if (bPush)
            {
                IVP_Impact_Solver_Long_Term* LongTerm = Dist->Owner;

                // Core A gets the negative push.
                IVP_Core* CoreA = LongTerm->ChildCandidate[0];
                if (CoreA != nullptr)
                {
                    Vector RotArm;
                    RotArm.x = (float)((double)(LongTerm->CoordOSCrossNormal[0].x) * (double)(CoreA->InvRotInertia.x));
                    RotArm.y = (float)((double)(LongTerm->CoordOSCrossNormal[0].y) * (double)(CoreA->InvRotInertia.y));
                    RotArm.z = (float)((double)(LongTerm->CoordOSCrossNormal[0].z) * (double)(CoreA->InvRotInertia.z));
                    IVP_AddMultiple(CoreA->RotSpeedChange, CoreA->RotSpeedChange, RotArm, (float)(-Impulse));
                    const float LinScale = (float)(-((double)(CoreA->InvMass) * (double)(ImpulseF)));
                    IVP_AddMultiple(CoreA->SpeedChange, CoreA->SpeedChange, LongTerm->ContactNormalWS, LinScale);
                }

                // Core B gets the positive push, inline. The x product stays
                // unrounded; y and z go through float locals.
                IVP_Core* CoreB = LongTerm->ChildCandidate[1];
                if (CoreB != nullptr)
                {
                    const double ArmX = (double)(LongTerm->CoordOSCrossNormal[1].x) * (double)(CoreB->InvRotInertia.x);
                    const float ArmY = (float)((double)(LongTerm->CoordOSCrossNormal[1].y) * (double)(CoreB->InvRotInertia.y));
                    const float ArmZ = (float)((double)(LongTerm->CoordOSCrossNormal[1].z) * (double)(CoreB->InvRotInertia.z));
                    CoreB->RotSpeedChange.x = (float)(ArmX * Impulse + (double)(CoreB->RotSpeedChange.x));
                    CoreB->RotSpeedChange.y = (float)((double)(ArmY) * Impulse + (double)(CoreB->RotSpeedChange.y));
                    CoreB->RotSpeedChange.z = (float)((double)(ArmZ) * Impulse + (double)(CoreB->RotSpeedChange.z));

                    const double MassImpulse = (double)(CoreB->InvMass) * Impulse;
                    const double NewY = MassImpulse * (double)(LongTerm->ContactNormalWS.y) + (double)(CoreB->SpeedChange.y);
                    const float NewZ = (float)(MassImpulse * (double)(LongTerm->ContactNormalWS.z) + (double)(CoreB->SpeedChange.z));
                    const double NewX = MassImpulse * (double)(LongTerm->ContactNormalWS.x);
                    CoreB->SpeedChange.z = NewZ;
                    CoreB->SpeedChange.x = (float)(NewX + (double)(CoreB->SpeedChange.x));
                    CoreB->SpeedChange.y = (float)(NewY);
                }

                Pressure = (Impulse >= 0.0) ? Impulse : (double)(0.0f);
            }
        }

        Dist->NowFrictionPressure = (float)(Pressure * (double)(EventSim->i_delta_time));
        ++Index;
    }
}

// ============================================================================
// IVP_ListManager friction methods (Ipion's IVP_Friction_System)
// ============================================================================

// 06355490
void IVP_ListManager::RemoveDistFromSystem(IVP_Contact_Point* Dist)
{
    IVP_Core* Core1 = Dist->Object1->core;
    IVP_Core* Core2 = Dist->Object2->core;
    Core1->update_time_of_last_motion();   // 063470e0
    Core2->update_time_of_last_motion();
    RemoveCtx(Dist);                       // 0634b8d0
    if (RetirePairRecordBookkeeping(Dist) == 1) // 0634b850
    {
        union_find_necessary = 1;
    }

    IVP_Friction_Info_For_Core* Info1 = static_cast<IVP_Friction_Info_For_Core*>(
        Core1->find_object_hash_entry(IVP_FrictionKey(this)));
    IVP_Friction_Info_For_Core* Info2 = static_cast<IVP_Friction_Info_For_Core*>(
        Core2->find_object_hash_entry(IVP_FrictionKey(this)));

    Info1->FrDists.RemoveElement(Dist); // 06355310
    if (Info1->FrDists.n_elems == 0)    // 06355370
    {
        Core1->release_object_hash_entry(Info1); // 06347d50
        UnregisterCore(Core1);                    // 0634ba50
        IVP_Simulation_Unit* Unit = Core1->OwningSimUnit;
        Unit->State = (Unit->State & 0xfffffdffu) | 0x100u;
    }

    Info2->FrDists.RemoveElement(Dist);
    if (Info2->FrDists.n_elems == 0)
    {
        Core2->release_object_hash_entry(Info2);
        UnregisterCore(Core2);
        IVP_Simulation_Unit* Unit = Core2->OwningSimUnit;
        Unit->State = (Unit->State & 0xfffffdffu) | 0x100u;
    }

    // Destructor FUN_06355200, then operator delete.
    if (Dist != nullptr)
    {
        IVP_UnlinkPairRecordFromObjects(Dist);
        delete Dist;
    }
}

// 06368540
int IVP_ListManager::GetNumSupposedActiveFrDists() const
{
    int Count = 0;
    for (const IVP_Contact_Point* Dist = CtxListHead; Dist != nullptr; Dist = Dist->ListManagerListNext)
    {
        if (Dist->HasNegativePullSince >= 0)
        {
            break;
        }
        ++Count;
    }
    return Count;
}

// 06368e20. First is always the earlier node (FUN_06368e60's argument order).
void IVP_ListManager::ExchangeFrDists(IVP_Contact_Point* First, IVP_Contact_Point* Second)
{
    if (CtxListHead == First)
    {
        CtxListHead = Second;
    }
    if (First->ListManagerListPrev != nullptr)
    {
        First->ListManagerListPrev->ListManagerListNext = Second;
    }
    if (Second->ListManagerListNext != nullptr)
    {
        Second->ListManagerListNext->ListManagerListPrev = First;
    }
    First->ListManagerListNext = Second->ListManagerListNext;
    Second->ListManagerListPrev = First->ListManagerListPrev;
    First->ListManagerListPrev = Second;
    Second->ListManagerListNext = First;
}

// 06368e60
void IVP_ListManager::ReorderMindistsForComplex()
{
    IVP_Contact_Point* Current = CtxListHead;
    while (Current != nullptr)
    {
        IVP_Contact_Point* Next = Current->ListManagerListNext;
        if (Next == nullptr)
        {
            return;
        }
        if (Current->HasNegativePullSince > Next->HasNegativePullSince)
        {
            // Sink Next backwards past every larger key.
            ExchangeFrDists(Current, Next);
            for (;;)
            {
                IVP_Contact_Point* Before = Next->ListManagerListPrev;
                if (Before == nullptr || Before->HasNegativePullSince <= Next->HasNegativePullSince)
                {
                    break;
                }
                ExchangeFrDists(Before, Next);
            }
            Next = Current; // continue after the node that was overtaken
        }
        Current = Next;
    }
}

// 06368d80
void IVP_ListManager::RecheckFrDistsBeforeSolve()
{
    IVP_Contact_Point* Dist = CtxListHead;
    while (Dist != nullptr)
    {
        IVP_Contact_Point* Next = Dist->ListManagerListNext; // read first

        // Remove when the gap is not below max_distance_for_friction (ordered).
        // Below it (or unordered), remove only when the owner's +0x54 dword has
        // (bits & 0x300) == 0x100.
        bool bRemove = true;
        if (!(Dist->GapDistance >= G_IVP_MindistSettings.max_distance_for_friction))
        {
            bRemove = (Dist->Owner->FrictionIsBroken & 0x3) == 0x1;
        }

        if (bRemove)
        {
            RemoveDistFromSystem(Dist); // 06355490
        }
        else
        {
            // Acts when the sum is below the gap, or unordered.
            const double Limit = (double)(G_IVP_MindistSettings.distance_keepers_safety) +
                                 (double)(G_IVP_MindistSettings.friction_dist);
            if (!(Limit >= (double)(Dist->GapDistance)))
            {
                // Both original cores share Flags bit 0 or 1: move to the head.
                const unsigned Shared = Dist->Object2->original_core->Flags & Dist->Object1->original_core->Flags;
                if ((Shared & 0x3) != 0)
                {
                    RemoveCtx(Dist); // 0634b8d0
                    InsertCtx(Dist); // 0634b9a0
                }
            }
        }
        Dist = Next;
    }
    ComplexNotNecessaryNumber = 0;
}

// 06368ea0
int IVP_ListManager::CoreIsInAtMostOneDynamicPair(IVP_Core* Core) const
{
    int Count = 0;
    for (int I = static_cast<int>(CorePairEntries.n_elems) - 1; I >= 0; --I)
    {
        const IVP_ListManager_CorePairEntry* Pair = CorePairEntries.elems[I];
        if (Pair->CoreA == Core || Pair->CoreB == Core)
        {
            if (((Pair->CoreB->Flags | Pair->CoreA->Flags) & 0xc) == 0)
            {
                ++Count;
            }
        }
    }
    return Count > 1 ? 0 : 1;
}

// 06368f00
void IVP_ListManager::StaticFrOversizedMatrixPanic()
{
    for (int I = static_cast<int>(RegisteredDynamicCores.n_elems) - 1; I >= 0; --I)
    {
        IVP_Core* Core = RegisteredDynamicCores.elems[I];
        Core->Flags = (Core->Flags & 0xfffffffdu) | 1u;
        if (CoreIsInAtMostOneDynamicPair(Core) == 0)
        {
            Core->Speed.x = 0.0f;
            Core->Speed.y = 0.0f;
            Core->Speed.z = 0.0f;
            Core->RotSpeed.x = 0.0f;
            Core->RotSpeed.y = 0.0f;
            Core->RotSpeed.z = 0.0f;
        }
    }
}

// 06368f60
void IVP_ListManager::DoPushesDistanceKeepers(const IVP_Event_Sim* EventSim)
{
    IVP_U_Memory* Mem = Environment->vector_cores_active;
    ++Mem->State;                                         // transaction depth (word +0x10)
    ++G_IVP_FrictionPassCount;

    ReorderMindistsForComplex();
    RecheckFrDistsBeforeSolve();

    // More than 0x96 dists: fallback.
    if (static_cast<int>(static_cast<int16_t>(CtxListCount)) - static_cast<int>(ComplexNotNecessaryNumber) > 0x96)
    {
        StaticFrOversizedMatrixPanic();
        --Mem->State;
        Mem->reset_chunks(); // 06344070
        return;
    }

    IVP_Friction_Solver Solver(this, EventSim);
    Solver.SetupCoordsMindists(this);
    int* ActiveIsAtPos = static_cast<int*>(Mem->GetMem(static_cast<int>(static_cast<int16_t>(CtxListCount)) * 4));
    const int TotalActives = Solver.CalcSolverPSI(this, ActiveIsAtPos);
    Solver.SolveLinearEquationAndPush(this, ActiveIsAtPos, TotalActives, Mem);
    --Mem->State;
    Mem->reset_chunks();
}

// 063690b0
double IVP_ListManager::KineticEnergyOfSystem() const
{
    float Sum = 0.0f;
    for (int I = static_cast<int>(RegisteredCores.n_elems) - 1; I >= 0; --I)
    {
        const IVP_Core* Core = RegisteredCores.elems[I];
        Vector Rot;
        Rot.x = Core->RotSpeed.x + Core->RotSpeedChange.x;
        Rot.y = Core->RotSpeed.y + Core->RotSpeedChange.y;
        Rot.z = Core->RotSpeed.z + Core->RotSpeedChange.z;
        Vector Lin;
        Lin.x = Core->Speed.x + Core->SpeedChange.x;
        Lin.y = Core->Speed.y + Core->SpeedChange.y;
        Lin.z = Core->Speed.z + Core->SpeedChange.z;
        Sum = (float)(Core->calc_kinetic_energy(&Lin, &Rot) + (double)(Sum));
    }
    return (double)(Sum);
}

// 06369170
void IVP_ListManager::ConfirmComplexPushes()
{
    for (int I = static_cast<int>(RegisteredDynamicCores.n_elems) - 1; I >= 0; --I)
    {
        RegisteredDynamicCores.elems[I]->commit_speed_changes(); // 06346dd0
    }
}

// 06369190
void IVP_ListManager::UndoComplexPushes()
{
    for (int I = static_cast<int>(RegisteredDynamicCores.n_elems) - 1; I >= 0; --I)
    {
        RegisteredDynamicCores.elems[I]->clear_speed_change(); // 06346960
    }
}

// 063691b0
double IVP_ListManager::MaxEnergyGain() const
{
    double Sum = (double)(0.0f); // kept on the x87 stack
    for (int I = static_cast<int>(RegisteredCores.n_elems) - 1; I >= 0; --I)
    {
        const IVP_Core* Core = RegisteredCores.elems[I];
        if ((Core->Flags & 0xc) == 0)
        {
            Sum = (double)(Core->Mass) * (double)(9.81000041961670f) * (double)(0.1f) + Sum;
        }
    }
    return Sum;
}

// ============================================================================
// The friction system as a controller, and its two handles. Vtables and
// layout: ivp_mindist.h, above IVP_ListManager.
// ============================================================================

// FUN_06347d50: IVP_Core's member, defined here because it needs
// IVP_Friction_Info_For_Core complete. Unregisters (FUN_06347d20), then, when
// non-null, runs the FrDists destructor (FUN_063449d0) and operator delete.
void IVP_Core::release_object_hash_entry(IVP_Friction_Info_For_Core* Candidate)
{
    unregister_object_hash_entry(Candidate);
    if (Candidate != nullptr)
    {
        delete Candidate;
    }
}

// LAB_06355bb0, slot 3: rebase every dist's IVP_Time stamp (+0x68 / +0x6c)
// by Delta, pairs and dists both walked backward; pairs re-read every pass.
void IVP_ListManager::reset_time(IVP_Time Delta)
{
    for (int I = static_cast<int>(CorePairEntries.n_elems) - 1; I >= 0; --I)
    {
        IVP_ListManager_CorePairEntry* Pair = CorePairEntries.elems[I];
        for (int J = static_cast<int>(Pair->AttachedCtx.n_elems) - 1; J >= 0; --J)
        {
            IVP_Contact_Point* Dist = Pair->AttachedCtx.elems[J];
            Dist->ShiftTimeStamp(Delta.time_base, Delta.time_offset); // FUN_0635a160
        }
    }
}

// FUN_0634b7d0. Per pair: sum += (old gap - new gap) * pressure over its
// dists (FUN_0635a1e0 recomputes the gap), rounded to float per dist; only a
// positive, ordered sum is added to the pair's +0x24 budget.
void IVP_ListManager::RecalcAllContactPoints()
{
    for (int I = static_cast<int>(CorePairEntries.n_elems) - 1; I >= 0; --I)
    {
        IVP_ListManager_CorePairEntry* Pair = CorePairEntries.elems[I];
        float Sum = 0.0f;
        for (int J = static_cast<int>(Pair->AttachedCtx.n_elems) - 1; J >= 0; --J)
        {
            IVP_Contact_Point* Dist = Pair->AttachedCtx.elems[J];
            const float OldGap = Dist->GapDistance;
            IVP_UpdatePairRecordContactFrame(Dist);
            Sum = (float)(((double)(OldGap) - (double)(Dist->GapDistance)) *
                                         (double)(Dist->NowFrictionPressure) +
                                     (double)(Sum));
        }
        if (Sum > 0.0f)
        {
            Pair->_field24 = (float)((double)(Sum) + (double)(Pair->_field24));
        }
    }
}

// LAB_063568a0, the static handle's slot 0: a core is being deleted. Every
// dist of a pair involving Core is removed (FUN_06355490); the system is
// deleted once it holds no dist. Removing a pair's last (index-0) dist deletes
// the pair (FUN_0634b850), after which the inner loop ends without touching
// it again, as in retail.
void IVP_Friction_Sys_Static::core_is_going_to_be_deleted_event(IVP_Core* Core)
{
    for (int I = static_cast<int>(FrictionSystem->CorePairEntries.n_elems) - 1; I >= 0; --I)
    {
        IVP_ListManager_CorePairEntry* Pair = FrictionSystem->CorePairEntries.elems[I];
        if (Pair->CoreA == Core || Pair->CoreB == Core)
        {
            for (int J = static_cast<int>(Pair->AttachedCtx.n_elems) - 1; J >= 0; --J)
            {
                FrictionSystem->RemoveDistFromSystem(Pair->AttachedCtx.elems[J]);
            }
        }
    }

    IVP_ListManager* Fs = FrictionSystem;
    if (static_cast<int16_t>(Fs->CtxListCount) == 0)
    {
        delete Fs;
    }
}

// FUN_063565c0, the static handle's do_simulation_controller. At most one
// dist (signed count): FUN_06356560; else DoPushesDistanceKeepers. An empty
// system is deleted. Otherwise, when union_find_necessary is set, a core cut
// off from the first one (FUN_063557e0) has its group split into a new system
// (FUN_06355890). `this` is a member of Fs: nothing touches it after
// `delete Fs`, and the split may delete Fs too, after which only SplitCore is
// used, as in retail.
void IVP_Friction_Sys_Static::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* /*CoreList*/)
{
    if (static_cast<int16_t>(FrictionSystem->CtxListCount) <= 1)
    {
        DoSimulationSingleFriction(EventSim);
    }
    else
    {
        FrictionSystem->DoPushesDistanceKeepers(EventSim);
    }

    IVP_ListManager* Fs = FrictionSystem;
    if (static_cast<int16_t>(Fs->CtxListCount) == 0)
    {
        FrictionSystem = nullptr;
        delete Fs;
        IVP_Simulation_Unit* Unit = EventSim->sim_unit;
        Unit->State = (Unit->State & 0xfffffdffu) | 0x100u;
        return;
    }

    if (Fs->union_find_necessary == 0)
    {
        return;
    }
    Fs->union_find_necessary = 0;

    IVP_Core* SplitCore = IVP_ListManager_FindDisconnectedCore(FrictionSystem);
    if (SplitCore == nullptr)
    {
        return;
    }
    IVP_ListManager_SplitOffCoreGroup(FrictionSystem, SplitCore);
    IVP_Simulation_Unit* Unit = SplitCore->OwningSimUnit;
    Unit->State = (Unit->State & 0xfffffdffu) | 0x100u;
}

// FUN_06356560: FUN_06353f10 on the single dist with friction_dist
// (DAT_0658ab1c) and 1.0f. A gap >= max_distance_for_friction (ordered) is
// removed outright; otherwise only when ([Owner+0x54] & 0x300) == 0x100.
void IVP_Friction_Sys_Static::DoSimulationSingleFriction(IVP_Event_Sim* EventSim)
{
    IVP_Contact_Point* Dist = FrictionSystem->CtxListHead;
    Dist->SolveNormalImpulse(EventSim, G_IVP_MindistSettings.friction_dist, 1.0f);

    if (!(Dist->GapDistance >= G_IVP_MindistSettings.max_distance_for_friction))
    {
        const uint32_t OwnerState = *reinterpret_cast<const uint32_t*>(
            reinterpret_cast<const char*>(Dist->Owner) + offsetof(IVP_Impact_Solver_Long_Term, StateProcessed));
        if ((OwnerState & 0x300u) != 0x100u)
        {
            return;
        }
    }
    FrictionSystem->RemoveDistFromSystem(Dist);
}

// FUN_06356510, the energy handle's do_simulation_controller. With at most one
// dist: FUN_0635a1e0 on the head and return (a null head is not tested; the
// static handle, priority 0, deletes an empty system before this one, priority
// 2000, runs). Otherwise RecalcAllContactPoints, then FUN_06355ca0 (zero every
// pair's budget) when State & 0x3000, and FUN_06355c80 (tick every budget)
// when State & 0x0c00 is clear.
void IVP_Friction_Sys_Energy::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* /*CoreList*/)
{
    IVP_ListManager* Fs = FrictionSystem;
    if (static_cast<int16_t>(Fs->CtxListCount) <= 1)
    {
        IVP_UpdatePairRecordContactFrame(Fs->CtxListHead);
        return;
    }

    Fs->RecalcAllContactPoints();
    if ((EventSim->sim_unit->State & 0x3000u) != 0)
    {
        IVP_ListManager_ClearAllCorePairBudgets(FrictionSystem);
    }
    if ((EventSim->sim_unit->State & 0x0c00u) == 0)
    {
        IVP_ListManager_TickAllCorePairBudgets(FrictionSystem);
    }
}
