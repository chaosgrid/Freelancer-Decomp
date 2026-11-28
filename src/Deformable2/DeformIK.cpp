// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Deformable2.h"
#include "EyeMaterial.h"
#include "common_misc.h"
#include "msvcrt_compat.h"

#include <cstddef>
#include <cstdarg>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <vector>

static double IkLen3(const Vector* Vec);
static void IkCross3(Vector* Dest, const Vector* Left, const Vector* Right);
static void IkColsToMat(Matrix* Out, const Vector* Col0, const Vector* Col1, const Vector* Col2);
static Matrix* IkEulerClamp(Matrix* Out, const float* Bone, const Matrix* Mat);

static void IkEulerExtract(float* OutYaw, float* OutPitch, float* OutRoll, const Matrix* Mat,
                           bool bAlt);

static void IkMathMulMatrix(Matrix* Out, const Matrix* Left, const Matrix* Right);


// IK current and trial poses; row zero holds the start joint.
static Matrix G_IkCurRot[G_IkMaxJoints + 1];
static Vector G_IkCurPos[G_IkMaxJoints + 1];
static Vector G_IkNewPos[G_IkMaxJoints + 1];
static Matrix G_IkNewRot[G_IkMaxJoints + 1];

// FUN_065f9d70 transpose; FUN_065faef0 A*B; FUN_065fafc0 A*x.
void FSvdMatrix::Transpose(FSvdMatrix* Out) const
{
    Out->RowCount = ColCount;
    Out->ColCount = RowCount;
    const int Rows = RowCount;
    const int Cols = ColCount;
    for (int Row = 0; Row < Rows; ++Row)
    {
        for (int Col = 0; Col < Cols; ++Col)
            Out->Cell[Col][Row] = Cell[Row][Col];
    }
}

// Shared helpers for the two damped-least-squares solvers.

// FSvdMatrix-shaped vectors (FUN_065fafc0 writes its row count into the thirteenth dword).
struct FIkVec
{
    float Cell[12];
    int Count;
};

static_assert(sizeof(FIkVec) == 0x34, "IK vector is 13 dwords");

// Keep Rest9 adjacent: a locked first joint writes into the preceding cells.
struct FIkDlsJacobian
{
    Matrix Rest9;
    FSvdMatrix J;
};

static_assert(offsetof(FIkDlsJacobian, J) == 0x24, "the Jacobian sits right after the scratch");

// The locals both solvers share, by their frame slots (FUN_065fc000 / FUN_065fce60). Nothing
// in the frame is cleared: J, the vectors, TargetPos / TargetMat when their flag is clear and
// Held3 before the first unlocked joint hold whatever was there, as in retail.
struct FIkDlsFrame
{
    bool bPos;               // [0x13] / [0x13]  AimFlags bit 5
    bool bOri;               // [0x11] / [0x12]  AimFlags bit 6
    int PosDim;              // [0x48] / [0x54]  bPos ? 3 : 0
    int TaskDim;             // [0x58] / [0x58]  PosDim + (bOri ? 3 : 0)
    int Dof;                 // [0x38] / [0x40]
    int JointIters;          // [0x30] / [0x44]
    int EndIndex;            // [0x64] / [0x70]  JointCount, the chain end's scratch row
    int Counter;             // [0x24] / [0x20]
    int CounterFloor;        // [0xa8] / [0x9c]
    float Alpha;             // [0x20] / [0x28]
    float Accum;             // [0x1c] / [0x24]
    Vector TargetPos;        // [0x68] / [0x5c]  TargetA, copied when bPos
    Matrix TargetMat;        // [0x2d0] / [0x2c4] TargetB, copied when bOri
    Quaternion TargetQ;      // [0x124] / [0x118]
    Vector Held3;            // [0xac] / [0xac]
    FIkDlsJacobian Block;    // [0x2f4] / [0x2e8]
    FSvdMatrix JtJ;          // [0x560] / [0x554]
    FSvdMatrix Jt;           // [0x7a8] / [0x79c]
    FSvdMatrix Pinv;         // [0x9f0] / [0x9e4]
    FIkVec Error;            // [0x134] / [0x128]
    FIkVec Achieved;         // [0x168] / [0x15c]
    FIkVec JtE;              // [0x19c] / [0x190]
    FIkVec Delta;            // [0x268] / [0x25c]
    FIkVec Pull;             // [0x29c] / [0x290]
};

// The dimension stores before the first round. The J counts are the only ones read as stored;
// FUN_065f9d70, FUN_065faef0 and FUN_065fafc0 rewrite the others.
static void IkDlsSetup(FIkDlsFrame* Frame)
{
    Frame->Block.J.RowCount = Frame->TaskDim;
    Frame->Block.J.ColCount = Frame->Dof;
    Frame->Jt.RowCount = Frame->Dof;
    Frame->Jt.ColCount = Frame->TaskDim;
    Frame->JtJ.RowCount = Frame->TaskDim + Frame->Dof;
    Frame->JtJ.ColCount = Frame->Dof;
    Frame->Delta.Count = Frame->Dof;
    Frame->Achieved.Count = Frame->TaskDim;
    Frame->Counter = 1;
    Frame->CounterFloor = 1;
    Frame->Alpha = 1.0f;
    Frame->Accum = 0.0f;
}

// Shared scratch used by the SVD scalar operations.
static float G_SvdSqrArg = 0.0f;

// FUN_065f9e00 (two floats; ECX unused).
static double SvdPythag(float Left, float Right)
{
    const double AbsLeft = std::fabs((double)Left);
    const float AbsRight = std::fabs(Right);
    if (AbsLeft > (double)AbsRight)
    {
        const double Ratio = (double)AbsRight / AbsLeft;
        G_SvdSqrArg = (float)Ratio;
        if (Ratio == 0.0)
            return AbsLeft * std::sqrt(0.0 + 1.0);
        const double RatioF = (double)G_SvdSqrArg;
        return AbsLeft * std::sqrt(RatioF * RatioF + 1.0);
    }
    if ((double)AbsRight == 0.0)
        return 0.0;
    const double Ratio = AbsLeft / (double)AbsRight;
    G_SvdSqrArg = (float)Ratio;
    if (Ratio == 0.0)
        return std::sqrt(0.0 + 1.0) * (double)AbsRight;
    const double RatioF = (double)G_SvdSqrArg;
    return std::sqrt(RatioF * RatioF + 1.0) * (double)AbsRight;
}

// FUN_065f9ed0. Retail rounds the sum to float once before the compare, as SSE float addition
// does.
static bool SvdIsNegligible(float Value, float Anorm)
{
    return (Value + Anorm) == Anorm;
}

static float SvdAbs(float Value)
{
    return (Value < 0.0f) ? -Value : Value;
}

static float G_SvdMaxArg1 = 0.0f;
static float G_SvdMaxArg2 = 0.0f;
static int G_SvdMinArg1 = 0;
static int G_SvdMinArg2 = 0;

static float& SvdCell(float* Base, int Row, int Col)
{
    return Base[Row * G_SvdMaxDim + Col];
}

// Retail's sign test before each FSQRT / FABS sets C0 when below zero OR unordered (a NaN
// counts as negative).
static bool SvdSignC0(float Value)
{
    return !((double)Value >= 0.0);
}

// |Value| negated unless the sign test set C0: the FABS / (C0 ? FCHS) / FCHS tail both
// Householder halves use.
static double SvdNegSignOf(double Value, bool bSignNegative)
{
    double Magnitude = std::fabs(Value);
    if (bSignNegative)
        Magnitude = -Magnitude;
    return -Magnitude;
}

// FUN_065f9f00 (thiscall on the source matrix: A, W, V): Golub-Reinsch SVD.
unsigned char FSvdMatrix::SvdCmp(float* A, float* W, float* V)
{
    float Rv1[12];
    float G = 0.0f;
    float Anorm = 0.0f;
    double Scale = 0.0;
    int L = 0;
    int I = 0;
    while (I < ColCount)
    {
        L = I + 1;
        Rv1[I] = (float)(Scale * (double)G);
        G = 0.0f;
        Scale = 0.0;
        double Sum = 0.0;
        if (I < RowCount)
        {
            for (int K = I; K < RowCount; ++K)
                Scale += std::fabs((double)(SvdCell(A, K, I)));
            if (Scale != 0.0)
            {
                const double Inv = 1.0 / Scale;
                for (int K = I; K < RowCount; ++K)
                {
                    const double Scaled = Inv * (double)(SvdCell(A, K, I));
                    SvdCell(A, K, I) = (float)Scaled;
                    Sum += Scaled * Scaled;
                }
                const float F = SvdCell(A, I, I);
                G = (float)(SvdNegSignOf(std::sqrt(Sum), SvdSignC0(F)));
                const double H = (double)F * (double)G - Sum;
                SvdCell(A, I, I) = (float)((double)F - (double)G);
                if (L < ColCount)
                {
                    const double HInv = 1.0 / H;
                    for (int J = L; J < ColCount; ++J)
                    {
                        double Dot = 0.0;
                        if (I < RowCount)
                        {
                            for (int K = I; K < RowCount; ++K)
                                Dot += (double)(SvdCell(A, K, J)) *
                                       (double)(SvdCell(A, K, I));
                        }
                        const float Factor = (float)(HInv * Dot);
                        if (I >= RowCount)
                            continue;

                        for (int K = I; K < RowCount; ++K)
                            SvdCell(A, K, J) = (float)(
                                (double)Factor * SvdCell(A, K, I) +
                                (double)(SvdCell(A, K, J)));
                    }
                }
                for (int K = I; K < RowCount; ++K)
                    SvdCell(A, K, I) = (float)(Scale * (double)(SvdCell(A, K, I)));
            }
        }
        W[I] = (float)(Scale * (double)G);

        G = 0.0f;
        Scale = 0.0;
        Sum = 0.0;
        if (I < RowCount && I != ColCount && L < ColCount)
        {
            for (int K = L; K < ColCount; ++K)
                Scale += std::fabs((double)(SvdCell(A, I, K)));
            if (Scale != 0.0)
            {
                const double Inv = 1.0 / Scale;
                for (int K = L; K < ColCount; ++K)
                {
                    const double Scaled = Inv * (double)(SvdCell(A, I, K));
                    SvdCell(A, I, K) = (float)Scaled;
                    Sum += Scaled * Scaled;
                }
                const float SumF = (float)Sum;
                const float F = SvdCell(A, I, L);
                const double Gd = SvdNegSignOf(std::sqrt((double)SumF), SvdSignC0(F));
                G = (float)Gd;
                const float H = (float)(Gd * (double)F -
                                                   (double)SumF);
                SvdCell(A, I, L) = (float)((double)F - (double)G);
                if (L < ColCount)
                {
                    const double HInv = 1.0 / (double)H;
                    for (int K = L; K < ColCount; ++K)
                        Rv1[K] = (float)(HInv * (double)(SvdCell(A, I, K)));
                }
                if (L < RowCount)
                {
                    for (int J = L; J < RowCount; ++J)
                    {
                        double Dot = 0.0;
                        if (L < ColCount)
                        {
                            for (int K = L; K < ColCount; ++K)
                                Dot += (double)(SvdCell(A, I, K)) *
                                       (double)(SvdCell(A, J, K));
                        }
                        if (L >= ColCount)
                            continue;

                        for (int K = L; K < ColCount; ++K)
                            SvdCell(A, J, K) = (float)(
                                Dot * (double)Rv1[K] +
                                (double)(SvdCell(A, J, K)));
                    }
                }
                if (L < ColCount)
                {
                    for (int K = L; K < ColCount; ++K)
                        SvdCell(A, I, K) = (float)(Scale * (double)(SvdCell(A, I, K)));
                }
            }
        }
        G_SvdMaxArg1 = Anorm;
        G_SvdMaxArg2 = (float)(std::fabs((double)W[I]) +
                                          std::fabs((double)Rv1[I]));
        if (!(Anorm > G_SvdMaxArg2))
            Anorm = G_SvdMaxArg2;
        I = L;
    }

    // Right-hand transforms into V, i from n-1 down; g (float) and l carry.
    I = ColCount - 1;
    while (I >= 0)
    {
        if (I < ColCount)
        {
            if (G != 0.0f)
            {
                if (L < ColCount)
                {
                    const double GInv = 1.0 / (double)G;
                    for (int J = L; J < ColCount; ++J)
                        SvdCell(V, J, I) = (float)(
                            (double)(SvdCell(A, I, J)) /
                            (double)(SvdCell(A, I, L)) * GInv);
                }
                if (L < ColCount)
                {
                    for (int J = L; J < ColCount; ++J)
                    {
                        double Dot = 0.0;
                        if (L < ColCount)
                        {
                            for (int K = L; K < ColCount; ++K)
                                Dot += (double)(SvdCell(V, K, J)) *
                                       (double)(SvdCell(A, I, K));
                        }
                        if (L >= ColCount)
                            continue;

                        for (int K = L; K < ColCount; ++K)
                            SvdCell(V, K, J) = (float)(
                                Dot * (double)(SvdCell(V, K, I)) +
                                (double)(SvdCell(V, K, J)));
                    }
                }
            }
            if (L < ColCount)
            {
                for (int J = L; J < ColCount; ++J)
                {
                    SvdCell(V, J, I) = 0.0f;
                    SvdCell(V, I, J) = 0.0f;
                }
            }
        }
        SvdCell(V, I, I) = 1.0f;
        G = Rv1[I];
        L = I;
        I -= 1;
    }

    // Left-hand transforms into A, i from min(m, n)-1 down (IMIN through DAT_066032a4 /
    // DAT_066032a0).
    G_SvdMinArg1 = RowCount;
    G_SvdMinArg2 = ColCount;
    const int MinDim = (G_SvdMinArg1 < G_SvdMinArg2) ? G_SvdMinArg1 : G_SvdMinArg2;
    I = MinDim - 1;
    while (I >= 0)
    {
        L = I + 1;
        const float WVal = W[I];
        if (L < ColCount)
        {
            for (int J = L; J < ColCount; ++J)
                SvdCell(A, I, J) = 0.0f;
        }
        if (WVal != 0.0f)
        {
            const float GInv = (float)(1.0 / (double)WVal);
            if (L < ColCount)
            {
                for (int J = L; J < ColCount; ++J)
                {
                    double Dot = 0.0;
                    if (L < RowCount)
                    {
                        for (int K = L; K < RowCount; ++K)
                            Dot += (double)(SvdCell(A, K, I)) *
                                   (double)(SvdCell(A, K, J));
                    }
                    const float Factor = (float)(
                        Dot / (double)(SvdCell(A, I, I)) * (double)GInv);
                    if (I >= RowCount)
                        continue;

                    for (int K = I; K < RowCount; ++K)
                        SvdCell(A, K, J) = (float)(
                            (double)Factor * SvdCell(A, K, I) +
                            (double)(SvdCell(A, K, J)));
                }
            }
            if (I < RowCount)
            {
                for (int K = I; K < RowCount; ++K)
                    SvdCell(A, K, I) = (float)((double)GInv *
                                                          (double)(SvdCell(A, K, I)));
            }
        }
        else
        {
            if (I < RowCount)
            {
                for (int K = I; K < RowCount; ++K)
                    SvdCell(A, K, I) = 0.0f;
            }
        }
        SvdCell(A, I, I) = (float)((double)(SvdCell(A, I, I)) + 1.0);
        I -= 1;
    }

    // Diagonalisation, k from n-1 down, its from 1.
    int K = ColCount - 1;
    while (K >= 0)
    {
        int Its = 1;
        for (;;)
        {
            // l from k down. |rv1[l]| negligible -> no cancellation;
            // |W[l-1]| negligible (or l ran below 0) -> cancellation with this l.
            bool bCancel = true;
            int Nm = K - 1;
            L = K;
            while (L >= 0)
            {
                Nm = L - 1;
                if (SvdIsNegligible(SvdAbs(Rv1[L]), Anorm))
                {
                    bCancel = false;
                    break;
                }
                if (SvdIsNegligible(SvdAbs(W[Nm]), Anorm))
                    break;
                L = Nm;
            }
            if (bCancel && L <= K)
            {
                // c is stored as float, s stays double on the FPU.
                float Cos = 0.0f;
                double Sin = 1.0;
                for (int Row = L; Row <= K; ++Row)
                {
                    const float F = (float)(Sin * (double)Rv1[Row]);
                    Rv1[Row] = (float)((double)Cos * Rv1[Row]);
                    if (SvdIsNegligible(SvdAbs(F), Anorm))
                        break;
                    const float Gw = W[Row];
                    const double Hyp = SvdPythag(F, Gw);
                    W[Row] = (float)Hyp;
                    const double HInv = 1.0 / Hyp;
                    Cos = (float)((double)Gw * HInv);
                    Sin = -(HInv * (double)F);
                    for (int J = 0; J < RowCount; ++J)
                    {
                        const double Y = (double)(SvdCell(A, J, Nm));
                        const double Z = (double)(SvdCell(A, J, Row));
                        SvdCell(A, J, Nm) = (float)(Y * Cos + Z * Sin);
                        SvdCell(A, J, Row) = (float)(Z * Cos - Y * Sin);
                    }
                }
            }

            const float Z = W[K];
            if (L == K)
            {
                // Only "below zero" flips; an unordered compare (NaN) does not.
                if ((double)Z < 0.0)
                {
                    W[K] = -Z;
                    for (int J = 0; J < ColCount; ++J)
                        SvdCell(V, J, K) = -SvdCell(V, J, K);
                }
                break;
            }
            if (Its == G_SvdMaxIter)
                return 0;

            // Shift from the bottom 2x2 minor.
            float X = W[L];
            Nm = K - 1;
            float Y = W[Nm];
            float Gv = Rv1[Nm];
            float H = Rv1[K];
            float F = (float)(
                (((double)Y - Z) * ((double)Z + Y) +
                 ((double)Gv - H) * ((double)H + Gv)) /
                ((double)Y * H + (double)Y * H));
            const double GSigned = SvdSignC0(F) ? -std::fabs(SvdPythag(F, 1.0f))
                                                : std::fabs(SvdPythag(F, 1.0f));
            F = (float)(
                (((double)Y / ((double)F + GSigned) - H) * H +
                 ((double)X - Z) * ((double)X + Z)) /
                (double)X);
            float Cos = 1.0f;
            float Sin = 1.0f;

            // The QR sweep; every quantity is stored as float between steps.
            for (int J = L; J <= Nm; ++J)
            {
                const int Next = J + 1;
                const float GNext = Rv1[Next];
                Y = W[Next];
                H = (float)((double)Sin * GNext);
                Gv = (float)((double)Cos * GNext);
                double Zd = SvdPythag(F, H);
                Rv1[J] = (float)Zd;
                double ZInv = 1.0 / Zd;
                Cos = (float)((double)F * ZInv);
                Sin = (float)(ZInv * (double)H);
                F = (float)((double)X * Cos + (double)Gv * Sin);
                Gv = (float)((double)Cos * Gv - (double)X * Sin);
                H = (float)((double)Y * Sin);
                Y = (float)((double)Y * Cos);
                for (int Jj = 0; Jj < ColCount; ++Jj)
                {
                    const double Vx = (double)(SvdCell(V, Jj, J));
                    const double Vz = (double)(SvdCell(V, Jj, Next));
                    SvdCell(V, Jj, J) = (float)(Vx * Cos + Vz * Sin);
                    SvdCell(V, Jj, Next) = (float)(Vz * Cos - Vx * Sin);
                }
                Zd = SvdPythag(F, H);
                W[J] = (float)Zd;
                if (Zd != 0.0)
                {
                    ZInv = 1.0 / Zd;
                    Cos = (float)((double)F * ZInv);
                    Sin = (float)(ZInv * (double)H);
                }
                F = (float)((double)Y * Sin + (double)Cos * Gv);
                X = (float)((double)Y * Cos - (double)Sin * Gv);
                for (int Jj = 0; Jj < RowCount; ++Jj)
                {
                    const double Ay = (double)(SvdCell(A, Jj, J));
                    const double Az = (double)(SvdCell(A, Jj, Next));
                    SvdCell(A, Jj, J) = (float)(Ay * Cos + Az * Sin);
                    SvdCell(A, Jj, Next) = (float)(Az * Cos - Ay * Sin);
                }
            }
            W[K] = X;
            Rv1[L] = 0.0f;
            Rv1[K] = F;
            Its += 1;
            if (Its > G_SvdMaxIter)
                break;
        }
        K -= 1;
    }
    return 1;
}

static const char* const G_FdumpFileMat =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\Mat.h";

static void SvdScaleColumns(const FSvdMatrix* V, const float* W, FSvdMatrix* Out)
{
    Out->RowCount = V->RowCount;
    Out->ColCount = V->ColCount;
    const int Rows = V->RowCount;
    const int Cols = V->ColCount;
    for (int Row = 0; Row < Rows; ++Row)
    {
        for (int Col = 0; Col < Cols; ++Col)
            Out->Cell[Row][Col] = W[Col] * V->Cell[Row][Col];
    }
}

static void SvdMulABt(const FSvdMatrix* Left, const FSvdMatrix* Right, FSvdMatrix* Out)
{
    Out->RowCount = Left->RowCount;
    Out->ColCount = Right->RowCount;
    const int Rows = Left->RowCount;
    // Retail quirk: Out.n = Right.m, but the loop bound is Right.n (+0x244).
    const int OutCols = Right->ColCount;
    const int Inner = Left->ColCount;
    // The cell is accumulated in place, so each step rounds to float once (the product never
    // does on its own).
    for (int Row = 0; Row < Rows; ++Row)
    {
        for (int Col = 0; Col < OutCols; ++Col)
        {
            float Sum = 0.0f;
            for (int K = 0; K < Inner; ++K)
                Sum = (float)((double)Right->Cell[Col][K] *
                                             (double)Left->Cell[Row][K] +
                                         (double)Sum);
            Out->Cell[Row][Col] = Sum;
        }
    }
}

// FUN_065fadb0 (thiscall on the source matrix: Out).
void FSvdMatrix::PseudoInverse(FSvdMatrix* Out)
{
    Out->RowCount = ColCount;
    Out->ColCount = RowCount;
    const int N = ColCount;
    float W[12];
    int Index = 0;
    while (Index < N)
    {
        W[Index] = 0.0f;
        Index += 1;
    }
    FSvdMatrix V;
    V.RowCount = ColCount;
    V.ColCount = ColCount;
    FSvdMatrix Work;
    std::memcpy(&Work, this, sizeof(FSvdMatrix));

    if (SvdCmp(&Work.Cell[0][0], W, &V.Cell[0][0]) == 0)
        FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", G_FdumpFileMat, 0x2E8,
              "svdcmp: didnt approach a finite soln.");

    Index = 0;
    while (Index < N)
    {
        if ((double)W[Index] < 1.0e-6)
            W[Index] = 0.0f;
        else
            W[Index] = (float)(1.0 / (double)W[Index]);
        Index += 1;
    }

    FSvdMatrix Scaled;
    Scaled.RowCount = V.RowCount;
    Scaled.ColCount = V.ColCount;
    SvdScaleColumns(&V, W, &Scaled);
    SvdMulABt(&Scaled, &Work, Out);
}

// .data:066029e8: the __FILE__ FUN_065fb030 pushes at line 0x15.
static const char* const G_FdumpFileDeformIk =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\Deformable2\\deformIK.cpp";

// FUN_065fb030 (thiscall on the event: JointCount).
bool FIkEvent::SetJointCount(unsigned int JointCount)
{
    if (JointCount > (unsigned int)G_IkMaxJoints)
    {
        char Message[0x2000];
        FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", G_FdumpFileDeformIk, 0x15,
              FormatDeformMessage(Message, "too many joints specified for ik event. %d / %d",
                                  (int)JointCount, G_IkMaxJoints));
        return false;
    }
    this->JointCount = (int)JointCount;
    return true;
}

// FUN_065fb090: count of yaw/pitch/roll outside [min,max] on Relative.
static int IkEulerOutOfRange(const float* Bone, float Yaw, float Pitch, float Roll)
{
    int Count = 0;
    if (Yaw < Bone[0x4C / 4] || Bone[0x50 / 4] < Yaw)
        Count += 1;
    if (Pitch < Bone[0x54 / 4] || Bone[0x58 / 4] < Pitch)
        Count += 1;
    if (Roll < Bone[0x40 / 4] || Bone[0x44 / 4] < Roll)
        Count += 1;
    return Count;
}

static void IkCopy3(Vector* Dest, const Vector* Src)
{
    Dest->x = Src->x;
    Dest->y = Src->y;
    Dest->z = Src->z;
}

// FUN_065fb100 (Out, Dir, Parent 3x3, flags byte).
static void IkLookAt(Matrix* Out, const Vector* Dir, const Matrix* Parent, unsigned char Flags)
{
    Vector Col0 = {0.0f, 0.0f, 0.0f};
    Vector Col1 = {0.0f, 0.0f, 0.0f};
    Vector Col2 = {0.0f, 0.0f, 0.0f};
    if ((Flags & kIkAimAxisX) != 0)
    {
        IkCopy3(&Col0, Dir);
        const double P2 = (double)Parent->E[0][2];
        const double P5 = (double)Parent->E[1][2];
        const double P8 = (double)Parent->E[2][2];
        Col2.x = (float)((double)Col0.z * P5 - (double)Col0.y * P8);
        Col2.y = (float)(P8 * (double)Col0.x - (double)Col0.z * P2);
        Col2.z = (float)((double)Col0.y * P2 - P5 * (double)Col0.x);
        const double Y0 = (double)Col2.x;
        const double Y1 = (double)Col2.y;
        const double Y2 = (double)Col2.z;
        const double Inv = 1.0 / std::sqrt(Y2 * Y2 + Y1 * Y1 + Y0 * Y0);
        Col1.x = (float)(Y0 * Inv);
        Col1.y = (float)(Y1 * Inv);
        Col1.z = (float)(Y2 * Inv);
        IkCross3(&Col2, &Col0, &Col1);
    }
    else if ((Flags & kIkAimAxisY) != 0)
    {
        IkCopy3(&Col1, Dir);
        Vector Temp;
        Temp.x = Parent->E[0][0];
        Temp.y = Parent->E[1][0];
        Temp.z = Parent->E[2][0];
        IkCross3(&Col2, &Temp, &Col1);
        const double Inv = 1.0 / IkLen3(&Col2);
        Col2.x = (float)((double)Col2.x * Inv);
        Col2.y = (float)((double)Col2.y * Inv);
        Col2.z = (float)((double)Col2.z * Inv);
        IkCross3(&Col0, &Col1, &Col2);
    }
    else if ((Flags & kIkAimAxisZ) != 0)
    {
        IkCopy3(&Col2, Dir);
        Col1.x = Parent->E[0][1];
        Col1.y = Parent->E[1][1];
        Col1.z = Parent->E[2][1];
        IkCross3(&Col0, &Col1, &Col2);
        const double Inv = 1.0 / IkLen3(&Col0);
        Col0.x = (float)((double)Col0.x * Inv);
        Col0.y = (float)((double)Col0.y * Inv);
        Col0.z = (float)((double)Col0.z * Inv);
        IkCross3(&Col1, &Col2, &Col0);
    }
    IkColsToMat(Out, &Col0, &Col1, &Col2);
}

// FUN_065fb480 (ECX = vec): returned in ST0 as a double, never rounded to float.
static double IkLen3(const Vector* Vec)
{
    const double X = (double)Vec->x;
    const double Y = (double)Vec->y;
    const double Z = (double)Vec->z;
    return std::sqrt(X * X + Y * Y + Z * Z);
}

// FUN_065fb4b0: each component is a difference of two double products, stored once as float.
static void IkCross3(Vector* Dest, const Vector* Left, const Vector* Right)
{
    const double Z = (double)Left->x * Right->y - (double)Left->y * Right->x;
    const double Y = (double)Left->z * Right->x - (double)Right->z * Left->x;
    const double X = (double)Left->y * Right->z - (double)Left->z * Right->y;
    Dest->x = (float)X;
    Dest->y = (float)Y;
    Dest->z = (float)Z;
}

// FUN_065fb4f0: columns → 3×3.
static void IkColsToMat(Matrix* Out, const Vector* Col0, const Vector* Col1, const Vector* Col2)
{
    Out->E[0][0] = Col0->x;
    Out->E[1][0] = Col0->y;
    Out->E[2][0] = Col0->z;
    Out->E[0][1] = Col1->x;
    Out->E[1][1] = Col1->y;
    Out->E[2][1] = Col1->z;
    Out->E[0][2] = Col2->x;
    Out->E[1][2] = Col2->y;
    Out->E[2][2] = Col2->z;
}

static void IkCopy4(Quaternion* Dest, const Quaternion* Src)
{
    Dest->w = Src->w;
    Dest->x = Src->x;
    Dest->y = Src->y;
    Dest->z = Src->z;
}

static void IkCopy9(Matrix* Dest, const Matrix* Src)
{
    float* const DestFloats = &Dest->E[0][0];
    const float* const SrcFloats = &Src->E[0][0];
    int Index = 0;
    while (Index < 9)
    {
        DestFloats[Index] = SrcFloats[Index];
        Index += 1;
    }
}

// The single-joint body LAB_065fb540 inlines; Bone is the IEngine +0xC4 result.
static void IkSolveSingleJoint(FIkEvent* Event, FIkApplyGate* Gate, const float* Bone)
{
    const InstanceIndex Joint = Gate->Joint;
    const Matrix* ParentRot = G_Engine->GetInstanceOrientation(G_Engine->GetInstanceParent(Joint));
    Matrix Local;
    IkCopy9(&Local, ParentRot);
    Matrix Trans;
    if ((Event->AimFlags & kIkAimOrientationTarget) != 0)
    {
        Trans.E[0][0] = Local.E[0][0];
        Trans.E[0][1] = Local.E[1][0];
        Trans.E[0][2] = Local.E[2][0];
        Trans.E[1][0] = Local.E[0][1];
        Trans.E[1][1] = Local.E[1][1];
        Trans.E[1][2] = Local.E[2][1];
        Trans.E[2][0] = Local.E[0][2];
        Trans.E[2][1] = Local.E[1][2];
        Trans.E[2][2] = Local.E[2][2];
        AcquireMathEngine()->MulMatrix(Local,
                                       Trans,
                                       *Event->TargetB);
    }
    else
    {
        const Vector* Pos = G_Engine->GetInstancePosition(Joint);
        Vector P;
        IkCopy3(&P, Pos);
        const Vector* Target = Event->TargetA;
        Vector Diff;
        const Vector* Src = Target;
        if ((Event->AimFlags & kIkAimTargetIsPoint) != 0)
        {
            Diff.x = (float)((double)Target->x - (double)P.x);
            Diff.y = (float)((double)Target->y - (double)P.y);
            Diff.z = (float)((double)Target->z - (double)P.z);
            Src = &Diff;
        }
        Vector Dir;
        IkCopy3(&Dir, Src);
        const double X = (double)Dir.x;
        const double Y = (double)Dir.y;
        const double Z = (double)Dir.z;
        const double Inv = 1.0 / std::sqrt(Y * Y + Z * Z + X * X);
        Dir.x = (float)(X * Inv);
        Dir.y = (float)(Y * Inv);
        Dir.z = (float)(Z * Inv);
        Matrix Look;
        IkLookAt(&Look, &Dir, &Local, (unsigned char)Event->AimFlags);
        Trans.E[0][0] = Local.E[0][0];
        Trans.E[0][1] = Local.E[1][0];
        Trans.E[0][2] = Local.E[2][0];
        Trans.E[1][0] = Local.E[0][1];
        Trans.E[1][1] = Local.E[1][1];
        Trans.E[1][2] = Local.E[2][1];
        Trans.E[2][0] = Local.E[0][2];
        Trans.E[2][1] = Local.E[1][2];
        Trans.E[2][2] = Local.E[2][2];
        AcquireMathEngine()->MulMatrix(Local,
                                       Trans,
                                       Look);
    }
    Matrix Result;
    IkCopy9(&Result, &Local);
    if ((Event->AimFlags & kIkAimNoClamp) == 0)
    {
        Matrix Clamp;
        const Matrix* Clamped = IkEulerClamp(&Clamp, Bone, &Result);
        IkCopy9(&Result, Clamped);
    }
    Quaternion Q;
    AcquireMathEngine()->MatrixToQuaternion(Q,
                                            Result); // +0x50
    IkCopy4(Event->Quats, &Q);
}

// LAB_065fb540, the IK event's only table slot (06601340): EngBase's FUN_0661b420 calls it on
// the channel owner with (sampled value, channel key, bind descriptor, time).
// LAB_065fb540 (IK event vtable slot 0: OutQuat, MatchId, Gate, Time).
int DACOM_API FIkEvent::Apply(float* OutQuat, unsigned long ChannelKey, void* BindDesc, float Time)
{
    int MatchId = (int)ChannelKey;
    FIkApplyGate* Gate = static_cast<FIkApplyGate*>(BindDesc);
    (void)Time;
    if ((StateFlags & kIkStateStopped) != 0)
    {
        for (int Index = 0; Index < JointCount; ++Index)
        {
            if ((int)Channels[Index] != MatchId)
                continue;

            G_Engine->GetJointParams(
                JointIds[Index], 0,
                OutQuat);
            return 1;
        }
        return 1;
    }

    const int OldTick = ApplyTick;
    ApplyTick = OldTick + 1;
    if (OldTick == 0 && Gate->SkipFlag == 0)
    {
        const float* Bone = (const float*)(G_Engine->GetInstanceRelative(
            Gate->Joint));
        if (JointCount == 1)
            IkSolveSingleJoint(this, Gate, Bone);
        else if ((StateFlags & kIkStateSphereAxis) != 0)
            SolveDlsAlt();
        else
            SolveDls();
    }

    if (ApplyTick == JointCount)
        ApplyTick = 0;

    for (int Index = 0; Index < JointCount; ++Index)
    {
        if ((int)Channels[Index] == MatchId)
        {
            IkCopy4(reinterpret_cast<Quaternion*>(OutQuat), &Quats[Index]);
            return 0;
        }
    }
    return 0;
}

// FUN_065fb900 (Out9, Bone, Mat9). Returns Out9.
static Matrix* IkEulerClamp(Matrix* Out, const float* Bone, const Matrix* Mat)
{
    float Yaw = 0.0f;
    float Pitch = 0.0f;
    float Roll = 0.0f;
    IkEulerExtract(&Yaw, &Pitch, &Roll, Mat, false);
    const int Score = IkEulerOutOfRange(Bone, Yaw, Pitch, Roll);
    if (Score != 0)
    {
        float AltYaw = 0.0f;
        float AltPitch = 0.0f;
        float AltRoll = 0.0f;
        IkEulerExtract(&AltYaw, &AltPitch, &AltRoll, Mat, true);
        if (!(IkEulerOutOfRange(Bone, AltYaw, AltPitch, AltRoll) > Score))
        {
            Yaw = AltYaw;
            Pitch = AltPitch;
            Roll = AltRoll;
        }
    }

    bool bChanged = false;
    if (Yaw < Bone[0x4C / 4])
    {
        Yaw = Bone[0x4C / 4];
        bChanged = true;
    }
    if (Yaw > Bone[0x50 / 4])
    {
        Yaw = Bone[0x50 / 4];
        bChanged = true;
    }
    if (Pitch < Bone[0x54 / 4])
    {
        Pitch = Bone[0x54 / 4];
        bChanged = true;
    }
    if (Pitch > Bone[0x58 / 4])
    {
        Pitch = Bone[0x58 / 4];
        bChanged = true;
    }
    if (Roll < Bone[0x40 / 4])
    {
        Roll = Bone[0x40 / 4];
        bChanged = true;
    }
    if (Roll > Bone[0x44 / 4])
    {
        Roll = Bone[0x44 / 4];
        bChanged = true;
    }

    Matrix Local;
    if (!bChanged)
        IkCopy9(&Local, Mat);
    else
        IkEulerRebuild(&Local, Yaw, Pitch, Roll);
    IkCopy9(Out, &Local);
    return Out;
}

// DAT_066013a8: the float 0x36000000 = 2^-19, written out exactly.
static const float G_IkGimbalEps = 1.9073486328125e-06f;

// FUN_065fbb90 (OutYaw, OutPitch, OutRoll, Mat9, bAlt byte).
static void IkEulerExtract(float* OutYaw, float* OutPitch, float* OutRoll, const Matrix* Mat,
                           bool bAlt)
{
    const double M0 = (double)Mat->E[0][0];
    const double M3 = (double)Mat->E[1][0];
    const double M6 = (double)Mat->E[2][0];
    const double Hyp = std::sqrt(M0 * M0 + M3 * M3);
    if (!(Hyp > (double)G_IkGimbalEps))
    {
        if (bAlt)
        {
            *OutYaw = (float)(std::atan2((double)Mat->E[1][2], -(double)Mat->E[1][1]));
            *OutPitch = (float)(std::atan2(M6, -Hyp));
        }
        else
        {
            *OutYaw = (float)(std::atan2(-(double)Mat->E[1][2], (double)Mat->E[1][1]));
            *OutPitch = (float)(std::atan2(-M6, Hyp));
        }
        *OutRoll = 0.0f;
        return;
    }
    if (bAlt)
    {
        *OutYaw = (float)(std::atan2((double)Mat->E[2][1], -(double)Mat->E[2][2]));
        *OutPitch = (float)(std::atan2(-M6, -Hyp));
        *OutRoll = (float)(std::atan2(M3, -M0));
        return;
    }
    *OutYaw = (float)(std::atan2((double)Mat->E[2][1], (double)Mat->E[2][2]));
    *OutPitch = (float)(std::atan2(-M6, Hyp));
    *OutRoll = (float)(std::atan2(M3, M0));
}

// DAT_066013b0: the double widening of 1e-5f (bytes 00000080b5f8e43e).
static const double G_IkLogEps = (double)1.0e-5f;

// FUN_065fbc90 (Out3, Quat wxyz): quat -> log axis (acos / sin).
static void IkQuatLogVec(Vector* Out, const Quaternion* Quat)
{
    const double W = (double)Quat->w;
    if (std::fabs(1.0 - W) < G_IkLogEps)
    {
        Out->x = 0.0f;
        Out->y = 0.0f;
        Out->z = 0.0f;
        return;
    }
    const double Scale = std::acos(W) / std::sqrt(1.0 - W * W);
    Out->x = (float)(Scale * (double)Quat->x);
    Out->y = (float)(Scale * (double)Quat->y);
    Out->z = (float)(Scale * (double)Quat->z);
}

// DAT_06601348: the double 1e-5 (bytes f168e388b5f8e43e).
static const double G_IkExpEps = 1.0e-5;

// FUN_065fbd30 (Out4 wxyz, Axis).
static void IkQuatExp(Quaternion* Out, const Vector* Axis)
{
    const double Len = IkLen3(Axis);
    if (!(Len > G_IkExpEps))
    {
        Out->w = 1.0f;
        Out->x = 0.0f;
        Out->y = 0.0f;
        Out->z = 0.0f;
        return;
    }
    const float W = (float)(std::cos(Len));
    const double Scale = std::sin(Len) / Len;
    Out->w = W;
    Out->x = (float)(Scale * (double)Axis->x);
    Out->y = (float)(Scale * (double)Axis->y);
    Out->z = (float)(Scale * (double)Axis->z);
}

// FUN_065fbdf0 (Out3, A, B -- wxyz).
static void IkQuatLog(Vector* Out, const Quaternion* QuatA, const Quaternion* QuatB)
{
    const double Dot = (double)QuatA->z * QuatB->z +
                       (double)QuatA->y * QuatB->y +
                       (double)QuatA->x * QuatB->x +
                       (double)QuatA->w * QuatB->w;
    Quaternion UseB;
    if (!(Dot >= 0.0))
    {
        UseB.w = -QuatB->w;
        UseB.x = -QuatB->x;
        UseB.y = -QuatB->y;
        UseB.z = -QuatB->z;
    }
    else
    {
        IkCopy4(&UseB, QuatB);
    }
    Quaternion ConjA;
    ConjA.w = QuatA->w;
    ConjA.x = -QuatA->x;
    ConjA.y = -QuatA->y;
    ConjA.z = -QuatA->z;
    Quaternion Prod;
    AcquireMathEngine()->MulQuat(Prod,
                                 UseB,
                                 ConjA); // +0x18
    Vector Log3;
    IkQuatLogVec(&Log3, &Prod);
    Out->x = Log3.x + Log3.x;
    Out->y = Log3.y + Log3.y;
    Out->z = Log3.z + Log3.z;
}


// FUN_065fbf40; vtable 06601340.
FIkEvent::~FIkEvent()
{
    int Index = 0;
    while (Index < JointCount)
    {
        G_Channel->Destroy(
            (unsigned long)((ptrdiff_t)Channels[Index]));
        Index += 1;
    }
    JointCount = 0;
    delete[] (unsigned char*)JointIds;
    JointIds = nullptr;
    delete[] (unsigned char*)Channels;
    Channels = nullptr;
    delete[] (unsigned char*)Quats;
    Quats = nullptr;
    delete[] (unsigned char*)BindMats;
    BindMats = nullptr;
    delete[] LockBytes;
    LockBytes = nullptr;
}

// The Jacobian's cells as the listing addresses them: flat, 12 per row, from Cell[0][0].
static float* IkDlsCells(FIkDlsJacobian* Block)
{
    return (float*)Block + 9;
}

// An unlocked joint's position rows: the skew matrix of the joint's lever arm (X, Y, Z) in columns Col..Col+2 of rows 0..2.
static void IkDlsWriteSkew(float* Cells, int Col, float AxisX, float AxisY, float AxisZ)
{
    Cells[Col] = 0.0f;
    Cells[Col + 1] = AxisZ;
    Cells[Col + 2] = -AxisY;
    Cells[Col + 12] = -AxisZ;
    Cells[Col + 13] = 0.0f;
    Cells[Col + 14] = AxisX;
    Cells[Col + 24] = AxisY;
    Cells[Col + 25] = -AxisX;
    Cells[Col + 26] = 0.0f;
}

// A locked joint's position rows: its skew terms are ADDED into the previous three columns (the
// joint moves with the last unlocked one). Each add rounds to float once. No test of Col.
static void IkDlsAddSkew(float* Cells, int Col, float AxisX, float AxisY, float AxisZ)
{
    Cells[Col - 2] = AxisZ + Cells[Col - 2];
    Cells[Col - 1] = Cells[Col - 1] - AxisY;
    Cells[Col + 9] = Cells[Col + 9] - AxisZ;
    Cells[Col + 11] = AxisX + Cells[Col + 11];
    Cells[Col + 21] = AxisY + Cells[Col + 21];
    Cells[Col + 22] = Cells[Col + 22] - AxisX;
}

// An unlocked joint's orientation rows: identity in the 3x3
// block at row PosDim, column Col (First = Col + 12 * PosDim).
static void IkDlsWriteIdentity(float* Cells, int First)
{
    Cells[First] = 1.0f;
    Cells[First + 1] = 0.0f;
    Cells[First + 2] = 0.0f;
    Cells[First + 12] = 0.0f;
    Cells[First + 13] = 1.0f;
    Cells[First + 14] = 0.0f;
    Cells[First + 24] = 0.0f;
    Cells[First + 25] = 0.0f;
    Cells[First + 26] = 1.0f;
}

static void IkMathMatrixToQuat(Quaternion* Out, const Matrix* Mat)
{
    AcquireMathEngine()->MatrixToQuaternion(*Out,
                                            *Mat); // +0x50
}

// The write-back: for each unlocked joint among the first JointIters, the local rotation
// transpose(NewRot[i]) x NewRot[i + 1] goes to Quats[i].
static void IkDlsWriteQuats(FIkEvent* Event, int JointIters)
{
    int Joint = 0;
    while (Joint < JointIters)
    {
        if (Event->LockBytes[Joint] == 0)
        {
            const Matrix* Parent = &G_IkNewRot[Joint];
            Matrix ParentT;
            ParentT.E[0][0] = Parent->E[0][0];
            ParentT.E[0][1] = Parent->E[1][0];
            ParentT.E[0][2] = Parent->E[2][0];
            ParentT.E[1][0] = Parent->E[0][1];
            ParentT.E[1][1] = Parent->E[1][1];
            ParentT.E[1][2] = Parent->E[2][1];
            ParentT.E[2][0] = Parent->E[0][2];
            ParentT.E[2][1] = Parent->E[1][2];
            ParentT.E[2][2] = Parent->E[2][2];
            Matrix Product;
            IkMathMulMatrix(&Product, &ParentT, &G_IkNewRot[Joint + 1]);
            Matrix Local;
            IkCopy9(&Local, &Product);
            Quaternion Q;
            IkMathMatrixToQuat(&Q, &Local);
            IkCopy4(&Event->Quats[Joint], &Q);
        }
        Joint += 1;
    }
}

// The entry reads of the chain's start: IEngine +0x90 (position) and +0x98 (orientation).
static void IkDlsReadStart(FIkEvent* Event)
{
    const Vector* StartPos = G_Engine->GetInstancePosition(Event->StartJoint);
    IkCopy3(&G_IkNewPos[0], StartPos);
    IkCopy3(&G_IkCurPos[0], StartPos);
    IkCopy9(&G_IkNewRot[0], G_Engine->GetInstanceOrientation(Event->StartJoint));
    IkCopy9(&G_IkCurRot[0], &G_IkNewRot[0]);
}

// Whether the chain end's orientation is off target.
static bool IkDlsOriOff(const FIkDlsFrame* Frame)
{
    Quaternion CurQ;
    IkMathMatrixToQuat(&CurQ, &G_IkCurRot[Frame->EndIndex]);
    Vector Log3;
    IkQuatLog(&Log3, &CurQ, &Frame->TargetQ);
    const double X = (double)Log3.x;
    const double Y = (double)Log3.y;
    const double Z = (double)Log3.z;
    return std::sqrt((Y * Y + Z * Z) + X * X) > 0.01;
}

static void SvdMulAB(const FSvdMatrix* Left, const FSvdMatrix* Right, FSvdMatrix* Out)
{
    Out->RowCount = Left->RowCount;
    Out->ColCount = Right->ColCount;
    const int Rows = Left->RowCount;
    const int Cols = Right->ColCount;
    const int Inner = Left->ColCount;
    for (int Row = 0; Row < Rows; ++Row)
    {
        for (int Col = 0; Col < Cols; ++Col)
        {
            // Each step rounds to float once.
            float Sum = 0.0f;
            for (int K = 0; K < Inner; ++K)
                Sum = (float)((double)Left->Cell[Row][K] *
                                             (double)Right->Cell[K][Col] +
                                         (double)Sum);
            Out->Cell[Row][Col] = Sum;
        }
    }
}

static void SvdMulVec(const FSvdMatrix* Left, const float* Vec, float* Out)
{
    const int Rows = Left->RowCount;
    const int Cols = Left->ColCount;
    // Retail stores the integer m bits, not float(m).
    *(int*)(Out + 12) = Rows;
    for (int Row = 0; Row < Rows; ++Row)
    {
        // Each step rounds to float once.
        float Sum = 0.0f;
        for (int Col = 0; Col < Cols; ++Col)
            Sum = (float)((double)Vec[Col] *
                                         (double)Left->Cell[Row][Col] +
                                     (double)Sum);
        Out[Row] = Sum;
    }
}

// The damping: Lam2 = float(Damping * Damping); every diagonal cell of JtJ becomes
// float(Lam2 + cell).
static void IkDlsDamp(FSvdMatrix* JtJ, float Damp)
{
    const float Lam2 = (float)((double)Damp * (double)Damp);
    int Row = 0;
    while (Row < JtJ->RowCount)
    {
        int Col = 0;
        while (Col < JtJ->ColCount)
        {
            if (Row == Col)
                JtJ->Cell[Row][Col] = (float)((double)Lam2 +
                                                         (double)JtJ->Cell[Row][Col]);
            Col += 1;
        }
        Row += 1;
    }
}

// The rest pull, run when AimFlags bit 7 is clear.
static void IkDlsFillRestPull(FIkEvent* Event, int JointIters, Matrix* Rest9, float* Pull)
{
    float* Cursor = Pull + 1;
    int Joint = 0;
    while (Joint < JointIters)
    {
        if (Event->LockBytes[Joint] == 0)
        {
            Matrix Product;
            IkMathMulMatrix(&Product, &G_IkCurRot[Joint], &Event->BindMats[Joint]);
            IkCopy9(Rest9, &Product);
            Quaternion CurQ;
            IkMathMatrixToQuat(&CurQ, &G_IkCurRot[Joint + 1]);
            Quaternion RestQ;
            IkMathMatrixToQuat(&RestQ, Rest9);
            Vector Log3;
            IkQuatLog(&Log3, &CurQ, &RestQ);
            Cursor[-1] = Log3.x;
            Cursor[0] = Log3.y;
            Cursor[1] = Log3.z;
            Cursor += 3;
        }
        Joint += 1;
    }
}

static void IkMathTransform(Vector* Out, const Matrix* Mat, const Vector* Vec)
{
    AcquireMathEngine()->TransformMatrix(*Out,
                                         *Mat,
                                         *Vec); // +0x2C
}

// The trial step over the first JointIters joints.
static void IkDlsStepJoints(FIkEvent* Event, int JointIters, const float* Delta, Vector* Held3)
{
    const float* Cursor = Delta + 2;
    int Joint = 0;
    while (Joint < JointIters)
    {
        const FEngineJointBlock* Bone = G_Engine->GetInstanceRelative(Event->JointIds[Joint]);
        if (Event->LockBytes[Joint] == 0)
        {
            Held3->x = Cursor[-2];
            Held3->y = Cursor[-1];
            Held3->z = Cursor[0];
            Cursor += 3;
        }
        Vector Axis;
        Axis.x = (float)((double)Held3->x * 0.5);
        Axis.y = (float)((double)Held3->y * 0.5);
        Axis.z = (float)((double)Held3->z * 0.5);
        Quaternion StepQ;
        IkQuatExp(&StepQ, &Axis);
        Matrix StepM;
        IkMathQuatToMatrix(&StepM, &StepQ);
        Matrix Rotated;
        IkMathMulMatrix(&Rotated, &StepM, &G_IkCurRot[Joint + 1]);
        IkCopy9(&G_IkNewRot[Joint + 1], &Rotated);
        Vector Arm;
        IkMathTransform(&Arm, &G_IkNewRot[Joint], &Bone->ParentPos);
        G_IkNewPos[Joint + 1].x = (float)((double)Arm.x +
                                                      (double)G_IkNewPos[Joint].x);
        G_IkNewPos[Joint + 1].y = (float)((double)Arm.y +
                                                      (double)G_IkNewPos[Joint].y);
        G_IkNewPos[Joint + 1].z = (float)((double)Arm.z +
                                                      (double)G_IkNewPos[Joint].z);
        Joint += 1;
    }
}

// The squared residual, the difference and its square in double (0.0 is DAT_06601318).
static double IkDlsResidual(const FIkVec* Error, const FIkVec* Achieved, int TaskDim)
{
    double Sum = 0.0;
    int Index = 0;
    while (Index < TaskDim)
    {
        const double Diff = (double)Error->Cell[Index] -
                            (double)Achieved->Cell[Index];
        Sum = Sum + Diff * Diff;
        Index += 1;
    }
    return Sum;
}

// One round after the Jacobian is built; returns whether to run another.
static bool IkDlsRound(FIkDlsFrame* Frame, FIkEvent* Event)
{
    Frame->Block.J.Transpose(&Frame->Jt);
    SvdMulAB(&Frame->Jt, &Frame->Block.J, &Frame->JtJ);
    IkDlsDamp(&Frame->JtJ, Event->Damping);
    Frame->Pinv.RowCount = Frame->JtJ.ColCount;
    Frame->Pinv.ColCount = Frame->JtJ.RowCount;
    Frame->JtJ.PseudoInverse(&Frame->Pinv);

    const bool bPull = ((Event->AimFlags >> 7) & 1u) == 0;
    Frame->Pull.Count = Frame->Dof;
    if (bPull)
        IkDlsFillRestPull(Event, Frame->JointIters, &Frame->Block.Rest9, Frame->Pull.Cell);

    float Step = 2.0f;
    float PrevErr = FLT_MAX;
    float ErrDelta = 0.0f;
    for (;;)
    {
        Step = (float)((double)Step * 0.5);
        const Vector* const End = &G_IkCurPos[Frame->EndIndex];
        Vector Diff;
        Diff.x = (float)((double)Frame->TargetPos.x - (double)End->x);
        Diff.y = (float)((double)Frame->TargetPos.y - (double)End->y);
        Diff.z = (float)((double)Frame->TargetPos.z - (double)End->z);
        const int Divisor = Frame->Counter >= 1 ? Frame->Counter : Frame->CounterFloor;
        const double Scale = 1.0 / (double)Divisor;
        Frame->Counter -= 1;
        Frame->Error.Count = Frame->TaskDim;
        const float ScaledX = (float)((double)Diff.x * Scale);
        const float ScaledY = (float)((double)Diff.y * Scale);
        const double ScaledZ = Scale * (double)Diff.z;
        if (Frame->bPos)
        {
            Frame->Error.Cell[0] = ScaledX;
            Frame->Error.Cell[1] = ScaledY;
            Frame->Error.Cell[2] = (float)ScaledZ;
        }
        if (Frame->bOri)
        {
            Quaternion CurQ;
            IkMathMatrixToQuat(&CurQ, &G_IkCurRot[Frame->EndIndex]);
            Vector Log3;
            IkQuatLog(&Log3, &CurQ, &Frame->TargetQ);
            Frame->Error.Cell[Frame->PosDim] = Log3.x;
            Frame->Error.Cell[Frame->PosDim + 1] = Log3.y;
            Frame->Error.Cell[Frame->PosDim + 2] = Log3.z;
        }

        Frame->JtE.Count = Frame->Error.Count;
        SvdMulVec(&Frame->Jt, Frame->Error.Cell, Frame->JtE.Cell);
        if (bPull)
        {
            const float Gain = (float)((double)Event->Damping * 0.25);
            int Index = 0;
            while (Index < Frame->JtE.Count)
            {
                Frame->JtE.Cell[Index] = (float)(
                    (double)Gain * (double)(Frame->Pull.Cell[Index]) +
                    (double)(Frame->JtE.Cell[Index]));
                Index += 1;
            }
        }
        SvdMulVec(&Frame->Pinv, Frame->JtE.Cell, Frame->Delta.Cell);

        Vector OldEnd;
        IkCopy3(&OldEnd, End);
        IkDlsStepJoints(Event, Frame->JointIters, Frame->Delta.Cell, &Frame->Held3);
        if (Frame->bPos)
        {
            const Vector* const NewEnd = &G_IkNewPos[Frame->EndIndex];
            Frame->Achieved.Cell[0] = (float)((double)NewEnd->x - (double)OldEnd.x);
            Frame->Achieved.Cell[1] = (float)((double)NewEnd->y - (double)OldEnd.y);
            Frame->Achieved.Cell[2] = (float)((double)NewEnd->z - (double)OldEnd.z);
        }
        if (Frame->bOri)
        {
            Quaternion NewQ;
            IkMathMatrixToQuat(&NewQ, &G_IkNewRot[Frame->EndIndex]);
            Vector Log3;
            IkQuatLog(&Log3, &NewQ, &Frame->TargetQ);
            Frame->Achieved.Cell[Frame->PosDim] = Log3.x;
            Frame->Achieved.Cell[Frame->PosDim + 1] = Log3.y;
            Frame->Achieved.Cell[Frame->PosDim + 2] = Log3.z;
        }

        const double Sum = IkDlsResidual(&Frame->Error, &Frame->Achieved, Frame->TaskDim);
        ErrDelta = (float)(Sum - (double)PrevErr);
        PrevErr = (float)Sum;
        if (!(Sum > 0.01))
            break;
        if (!((double)ErrDelta < -1.0e-5))
            break;
    }

    Frame->Accum = (float)((double)Step * (double)Frame->Alpha +
                                      (double)Frame->Accum);
    const double Remain = 1.0 - (double)Frame->Accum;
    const float RemainF = (float)Remain;
    if (Remain < (double)Frame->Alpha)
        Frame->Alpha = RemainF;
    if (std::fabs((double)Frame->Accum - 1.0) <= 0.01)
        return false;
    return !(ErrDelta >= 0.0f);
}

// FUN_065fc000 (thiscall on the event): LAB_065fb540 calls it for a chain of two or more joints
// when StateFlags bit 1 is clear.
void FIkEvent::SolveDls()
{
    FIkDlsFrame Frame;
    const unsigned long Flags = AimFlags;
    Frame.bPos = ((Flags >> 5) & 1u) != 0;
    Frame.bOri = ((Flags >> 6) & 1u) != 0;
    const int Count = JointCount;
    Frame.Dof = (Frame.bOri ? Count : Count - 1) * 3;
    Frame.PosDim = Frame.bPos ? 3 : 0;
    Frame.TaskDim = Frame.PosDim + (Frame.bOri ? 3 : 0);
    if (Frame.bOri)
        Frame.Dof += LockCount * -3;
    else if (Frame.bPos)
    {
        int Locked = LockCount;
        if (Locked > 0 && LockBytes[Count - 1] != 0)
            Locked -= 1;
        Frame.Dof += Locked * -3;
    }

    IkDlsReadStart(this);
    int Joint = 0;
    while (Joint < JointCount)
    {
        G_Engine->GetJointParams(JointIds[Joint], 0, &Quats[Joint].w);
        G_Engine->GetInstanceRelative(JointIds[Joint]);
        IkCopy3(&G_IkCurPos[Joint + 1],
                G_Engine->GetInstancePosition(JointIds[Joint]));
        IkCopy9(&G_IkCurRot[Joint + 1],
                G_Engine->GetInstanceOrientation(JointIds[Joint]));
        Joint += 1;
    }

    Frame.EndIndex = JointCount;
    if (Frame.bPos)
        IkCopy3(&Frame.TargetPos, TargetA);
    if (Frame.bOri)
        IkCopy9(&Frame.TargetMat, TargetB);
    bool bNeed = false;
    if (Frame.bPos)
    {
        const Vector* const End = &G_IkCurPos[Frame.EndIndex];
        const double Dx = (double)Frame.TargetPos.x - (double)End->x;
        const double Dy = (double)Frame.TargetPos.y - (double)End->y;
        const double Dz = (double)Frame.TargetPos.z - (double)End->z;
        bNeed = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx) > 0.001;
    }
    IkMathMatrixToQuat(&Frame.TargetQ, &Frame.TargetMat);
    if (!bNeed)
    {
        if (!Frame.bOri)
            return;
        if (!IkDlsOriOff(&Frame))
            return;
    }

    IkDlsSetup(&Frame);
    Frame.JointIters = Frame.bOri ? JointCount : JointCount - 1;
    do
    {
        float* const Cells = IkDlsCells(&Frame.Block);
        int Limit = JointCount;
        if (Frame.bOri)
            Limit += 1;
        int Col = 0;
        int Link = 1;
        while (Link < Limit)
        {
            if (Frame.bPos)
            {
                Vector Arm;
                if (Link == JointCount)
                {
                    Arm.x = 0.0f;
                    Arm.y = 0.0f;
                    Arm.z = 0.0f;
                }
                else
                {
                    const FEngineJointBlock* Bone = G_Engine->GetInstanceRelative(JointIds[Link]);
                    IkMathTransform(&Arm, &G_IkCurRot[Link], &Bone->ParentPos);
                }
                if (LockBytes[Link - 1] != 0)
                    IkDlsAddSkew(Cells, Col, Arm.x, Arm.y, Arm.z);
                else
                    IkDlsWriteSkew(Cells, Col, Arm.x, Arm.y, Arm.z);
            }
            if (Frame.bOri && LockBytes[Link - 1] == 0)
                IkDlsWriteIdentity(Cells, Col + 12 * Frame.PosDim);
            if (LockBytes[Link - 1] == 0)
                Col += 3;
            Link += 1;
        }
    } while (IkDlsRound(&Frame, this));
    IkDlsWriteQuats(this, Frame.JointIters);
}

// FUN_065fce60 (thiscall on the event): LAB_065fb540 calls it for a chain of two or more joints
// when StateFlags bit 1 is set.
void FIkEvent::SolveDlsAlt()
{
    FIkDlsFrame Frame;
    const unsigned long Flags = AimFlags;
    Frame.bPos = ((Flags >> 5) & 1u) != 0;
    Frame.bOri = ((Flags >> 6) & 1u) != 0;
    Frame.Dof = JointCount * 3;
    Frame.PosDim = Frame.bPos ? 3 : 0;
    Frame.TaskDim = Frame.PosDim + (Frame.bOri ? 3 : 0);
    if (Frame.bOri)
        Frame.Dof += LockCount * -3;
    else if (Frame.bPos)
        Frame.Dof += LockCount * -3;

    IkDlsReadStart(this);
    int Joint = 0;
    while (Joint < JointCount)
    {
        G_Engine->GetJointParams(JointIds[Joint], 0, &Quats[Joint].w);
        IkCopy3(&G_IkCurPos[Joint + 1],
                G_Engine->GetInstancePosition(JointIds[Joint]));
        IkCopy9(&G_IkCurRot[Joint + 1],
                G_Engine->GetInstanceOrientation(JointIds[Joint]));
        Joint += 1;
    }

    Frame.EndIndex = JointCount;
    if (Frame.bPos)
        IkCopy3(&Frame.TargetPos, TargetA);
    if (Frame.bOri)
        IkCopy9(&Frame.TargetMat, TargetB);
    bool bNeed = false;
    if (Frame.bPos)
    {
        const Vector* const End = &G_IkCurPos[Frame.EndIndex];
        const float Fx = (float)((double)Frame.TargetPos.x - (double)End->x);
        const float Fy = (float)((double)Frame.TargetPos.y - (double)End->y);
        const float Fz = (float)((double)Frame.TargetPos.z - (double)End->z);
        const double X = (double)Fx;
        const double Y = (double)Fy;
        const double Z = (double)Fz;
        bNeed = std::sqrt((Y * Y + Z * Z) + X * X) > 0.001;
    }
    IkMathMatrixToQuat(&Frame.TargetQ, &Frame.TargetMat);
    if (!bNeed)
    {
        if (!Frame.bOri)
            return;
        if (!IkDlsOriOff(&Frame))
            return;
    }

    IkDlsSetup(&Frame);
    Frame.JointIters = JointCount;
    do
    {
        float* const Cells = IkDlsCells(&Frame.Block);
        const int Limit = JointCount + 1;
        int Col = 0;
        int Link = 1;
        while (Link < Limit)
        {
            if (Frame.bPos)
            {
                const FEngineJointBlock* Bone = G_Engine->GetInstanceRelative(JointIds[Link]);
                Vector Arm;
                IkMathTransform(&Arm, &G_IkCurRot[Link], &Bone->ParentPos);
                if (LockBytes[Link - 1] != 0)
                    IkDlsAddSkew(Cells, Col, Arm.x, Arm.y, Arm.z);
                else
                    IkDlsWriteSkew(Cells, Col, Arm.x, Arm.y, Arm.z);
            }
            if (Frame.bOri && LockBytes[Link - 1] == 0)
                IkDlsWriteIdentity(Cells, Col + 12 * Frame.PosDim);
            if (LockBytes[Link - 1] == 0)
                Col += 3;
            Link += 1;
        }
    } while (IkDlsRound(&Frame, this));
    IkDlsWriteQuats(this, Frame.JointIters);
}

// Float-array views of the math engine slots the IK solvers call (retail: DAT_066033a8, filled
// by FUN_06600de0 on first use). They add no checks.
static void IkMathMulMatrix(Matrix* Out, const Matrix* Left, const Matrix* Right)
{
    AcquireMathEngine()->MulMatrix(*Out,
                                   *Left,
                                   *Right); // +0x20
}
