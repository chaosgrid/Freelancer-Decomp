// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_csys.h"
#include "x86math.h" // I3DMathEngine

#include "DALib.h"   // DALib::Engine, for DALib_Engine_get_csys
#include "dacom.h"   // DACOM_Acquire, AGGDESC
#include "x87_trig.h" // retail's inline FSIN / FCOS (EulerMatrix, RotateMatrix)

#include <math.h>
#include <stdlib.h>  // abs(int) -- retail truncates the dot product through it

namespace
{
    void CopyVector(Vector& Dst, Vector const& Src)
    {
        Dst.x = Src.x;
        Dst.y = Src.y;
        Dst.z = Src.z;
    }

    void CopyMatrix(Matrix& Dst, Matrix const& Src)
    {
        unsigned Row = 0;
        for (; Row < 3; ++Row)
        {
            unsigned Col = 0;
            for (; Col < 3; ++Col)
                Dst.E[Row][Col] = Src.E[Row][Col];
        }
    }

    void TransposeMatrix(Matrix& Dst, Matrix const& Src)
    {
        unsigned Row = 0;
        for (; Row < 3; ++Row)
        {
            unsigned Col = 0;
            for (; Col < 3; ++Col)
                Dst.E[Row][Col] = Src.E[Col][Row];
        }
    }

}

void CsysFromPosOrient(Csys& Out, Vector const& Pos, Matrix const& Orient)
{
    Out.Pos.x = Pos.x;
    Out.Pos.y = Pos.y;
    Out.Pos.z = Pos.z;
    unsigned Cell = 0;
    for (; Cell < 9; ++Cell)
        Out.Orient.E[Cell / 3][Cell % 3] = Orient.E[Cell / 3][Cell % 3];
}

Csys::Csys()
{
}

void Csys::set_identity()
{
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;
    MatrixSetIdentity(Orient);
}

void Csys::init(Vector const& NewPos, Matrix const& NewOrient)
{
    CopyVector(Pos, NewPos);
    CopyMatrix(Orient, NewOrient);
}

Transform Csys::get_transform() const
{
    Transform Out;
    CopyMatrix(Out.Rotation, Orient);
    CopyVector(Out.Translation, Pos);
    return Out;
}

void Csys::set_transform(Transform const& Xform)
{
    CopyVector(Pos, Xform.Translation);
    CopyMatrix(Orient, Xform.Rotation);
}

// The Csys transforms go through the 3DMathEngine (retail's TU cache DAT_063fceb8, lazily
// filled by FUN_06391cf0 = Acquire3DMathEngine); its return codes are ignored, as in retail.

// FUN_06307490: TransformMatrix (slot +0x2c), then + Pos.
Vector Csys::trans_from_frame(Vector const& Local) const
{
    Vector Rotated;
    Acquire3DMathEngine()->TransformMatrix(Rotated, Orient, Local);
    Vector Out;
    Out.x = Rotated.x + Pos.x;
    Out.y = Rotated.y + Pos.y;
    Out.z = Rotated.z + Pos.z;
    return Out;
}

// FUN_06307500: MulMatrix (slot +0x20).
Matrix Csys::trans_from_frame(Matrix const& Local) const
{
    Matrix Out;
    Acquire3DMathEngine()->MulMatrix(Out, Orient, Local);
    return Out;
}

// FUN_06307550: the two members above, in this order.
Csys Csys::trans_from_frame(Csys const& Local) const
{
    Csys Out;
    Out.Pos = trans_from_frame(Local.Pos);
    Out.Orient = trans_from_frame(Local.Orient);
    return Out;
}

// FUN_06307610: World - Pos, then TransposeTransform (slot +0x38).
Vector Csys::trans_to_frame(Vector const& World) const
{
    Vector Delta;
    Delta.x = World.x - Pos.x;
    Delta.y = World.y - Pos.y;
    Delta.z = World.z - Pos.z;
    Vector Out;
    Acquire3DMathEngine()->TransposeTransform(Out, Orient, Delta);
    return Out;
}

// FUN_06307680: transpose Orient, then MulMatrix (slot +0x20).
Matrix Csys::trans_to_frame(Matrix const& World) const
{
    Matrix Transposed;
    TransposeMatrix(Transposed, Orient);
    Matrix Out;
    Acquire3DMathEngine()->MulMatrix(Out, Transposed, World);
    return Out;
}

// FUN_06307710: the two members above, in this order.
Csys Csys::trans_to_frame(Csys const& World) const
{
    Csys Out;
    Out.Pos = trans_to_frame(World.Pos);
    Out.Orient = trans_to_frame(World.Orient);
    return Out;
}

// FUN_063079b0: Pos' = -TransposeTransform(Orient, Pos); Orient' = transpose(Orient).
Csys Csys::get_inverse() const
{
    Vector Back;
    Acquire3DMathEngine()->TransposeTransform(Back, Orient, Pos);
    Csys Out;
    Out.Pos.x = -Back.x;
    Out.Pos.y = -Back.y;
    Out.Pos.z = -Back.z;
    TransposeMatrix(Out.Orient, Orient);
    return Out;
}

Csys __cdecl Csys_identity()
{
    Csys Out;
    Out.set_identity();
    return Out;
}

Csys __cdecl Csys_init(Vector const& Pos, Matrix const& Orient)
{
    Csys Out;
    Out.init(Pos, Orient);
    return Out;
}

// FUN_06307a80: trans_from_frame(Csys) with Left as `this`.
Csys __cdecl operator*(Csys const& Left, Csys const& Right)
{
    return Left.trans_from_frame(Right);
}

// FUN_063038b0. x87 stack values are double here, stored floats are float.
// - Degenerate look (1, 0, 0) unless the length is strictly greater than 1e-4 (unordered takes
//   the degenerate arm).
// - Side = (0, 1) only when |LookX| and |LookY| are both strictly below 1e-4; otherwise
//   (-LookX, LookY), NaN included.
// - The *0.0 terms are real multiplies (they carry NaN / infinity), and the normalisation has
//   no length guard.
Matrix __cdecl LookMatrix(Vector const& Direction)
{
    Matrix Out;

    const double X = Direction.x;
    const double Y = Direction.y;
    const double Z = Direction.z;
    const double Length = sqrt((X * X + Y * Y) + Z * Z);

    float LookX;
    float LookY;
    float LookZ;
    if (Length > 1e-4)
    {
        const double Inv = -1.0 / Length;
        LookX = (float)(Inv * Direction.x);
        LookY = (float)(Inv * Direction.y);
        LookZ = (float)(Inv * Direction.z);
    }
    else
    {
        LookX = 1.0f;
        LookY = 0.0f;
        LookZ = 0.0f;
    }

    double SideX;
    double SideY;
    if (!(fabs((double)(LookX)) < 1e-4) || !(fabs((double)(LookY)) < 1e-4))
    {
        SideX = -(double)(LookX);
        SideY = LookY;
    }
    else
    {
        SideX = 0.0;
        SideY = 1.0;
    }

    const float C0 = (float)((double)(LookY) * 0.0 - (double)(LookZ) * SideX);
    const float C1 = (float)((double)(LookZ) * SideY - (double)(LookX) * 0.0);
    const float C2 = (float)(SideX * LookX - SideY * LookY);

    const float InvN = (float)(1.0 / sqrt(((double)(C2) * C2 + (double)(C1) * C1) +
                                                     (double)(C0) * C0));
    const double C0n = (double)(C0) * InvN;
    const double C1n = (double)(C1) * InvN;
    const float C2n = (float)((double)(C2) * InvN);

    Out.E[2][2] = LookZ;
    Out.E[2][1] = C2n;
    Out.E[0][0] = (float)((double)(LookZ) * C1n - (double)(C2n) * LookY);
    Out.E[1][0] = (float)((double)(C2n) * LookX - (double)(LookZ) * C0n);
    Out.E[2][0] = (float)(C0n * LookY - C1n * LookX);
    Out.E[0][1] = (float)(C0n);
    Out.E[1][1] = (float)(C1n);
    Out.E[0][2] = LookX;
    Out.E[1][2] = LookY;
    return Out;
}

Matrix __cdecl LookMatrixYup(Vector const& Direction)
{
    // FUN_06303a60. x87 stack values are double here, stored floats are float.
    Matrix Out;

    // Note the test is on the squared length, unlike LookMatrix. Degenerate look (0, 0, -1)
    // unless strictly greater (unordered takes the degenerate arm).
    const double X = Direction.x;
    const double Y = Direction.y;
    const double Z = Direction.z;
    const double LenSq = (X * X + Y * Y) + Z * Z;
    float LookX;
    float LookY;
    float LookZ;
    if (LenSq > 1e-4)
    {
        const double Inv = -1.0 / sqrt(LenSq);
        LookX = (float)(Inv * X);
        LookY = (float)(Inv * Y);
        LookZ = (float)(Inv * Z);
    }
    else
    {
        LookX = 0.0f;
        LookY = 0.0f;
        LookZ = -1.0f;
    }

    // Ref = (0, 0, 1) only when |LookX| and |LookZ| are both strictly below 1e-4; otherwise
    // (-LookX, 0, LookZ), NaN included.
    const double Lx = LookX;
    const double Ly = LookY;
    const double Lz = LookZ;
    double RefX;
    double RefZ;
    if (!(fabs(Lx) < 1e-4) || !(fabs(Lz) < 1e-4))
    {
        RefX = -Lx;
        RefZ = Lz;
    }
    else
    {
        RefX = 0.0;
        RefZ = 1.0;
    }

    // X and Z are the cross product of (RefX, 0, RefZ) with Look; the middle one is
    // RefZ*LookZ - RefX*LookX (still perpendicular to Look). The *0.0f terms are real
    // multiplies (they carry NaN / infinity).
    const double UpX = (float)(RefX * Ly - Lz * 0.0f);
    const double UpY = (float)(RefZ * Lz - RefX * Lx);
    const double UpZ = (float)(Lx * 0.0f - Ly * RefZ);

    // No length guard. UpX and UpY scaled stay on the stack; UpZ is stored.
    const float UpInv = (float)(1.0f / sqrt((UpZ * UpZ + UpY * UpY) + UpX * UpX));
    const double UpXn = UpX * UpInv;
    const double UpYn = UpY * UpInv;
    const double UpZn = (float)(UpZ * UpInv);

    // Column 2 = Look, column 1 = Up, column 0 = Up x Look.
    Out.E[2][2] = LookZ;
    Out.E[2][1] = (float)UpZn;
    Out.E[0][0] = (float)(Lz * UpYn - UpZn * Ly);
    Out.E[1][0] = (float)(UpZn * Lx - UpXn * Lz);
    Out.E[2][0] = (float)(Ly * UpXn - UpYn * Lx);
    Out.E[0][1] = (float)UpXn;
    Out.E[1][1] = (float)UpYn;
    Out.E[0][2] = LookX;
    Out.E[1][2] = LookY;
    return Out;
}

Matrix __cdecl EulerMatrix(Vector const& AnglesDegrees)
{
    // FUN_06304230. The angles are stored as floats (degrees * DAT_0639d210); FSIN / FCOS
    // results stay on the x87 stack (cos(yaw) is spilled as a double). Only SinR*CosP and
    // CosR*CosP are rounded to float; each element is one double expression rounded at its
    // store.
    static const float kDegToRad = 0.017453292f;

    const float Pitch = AnglesDegrees.x * kDegToRad;
    const float Yaw = AnglesDegrees.y * kDegToRad;
    const float Roll = AnglesDegrees.z * kDegToRad;

    const double SinP = X87Sin((double)Pitch);
    const double CosP = X87Cos((double)Pitch);
    const double SinY = X87Sin((double)Yaw);
    const double CosY = X87Cos((double)Yaw);
    const double SinR = X87Sin((double)Roll);
    const double CosR = X87Cos((double)Roll);

    const double SinYSinP = SinY * SinP;
    const double SinRCosP = (float)(SinR * CosP);
    const double CosRCosP = (float)(CosR * CosP);

    Matrix Out;
    Out.E[0][0] = (float)(CosR * CosY);
    Out.E[0][1] = (float)(SinYSinP * CosR - SinRCosP);
    Out.E[0][2] = (float)(CosRCosP * SinY + SinR * SinP);
    Out.E[1][0] = (float)(SinR * CosY);
    Out.E[1][1] = (float)(SinYSinP * SinR + CosRCosP);
    Out.E[1][2] = (float)(SinRCosP * SinY - CosR * SinP);
    Out.E[2][0] = (float)(-SinY);
    Out.E[2][1] = (float)(CosY * SinP);
    Out.E[2][2] = (float)(CosY * CosP);
    return Out;
}

Matrix __cdecl RotateMatrix(Vector const& AnglesDegrees)
{
    // FUN_06303fe0. Starting from identity, the axes are folded in Y, X, Z, each by a MulMatrix
    // (engine slot +0x20) with the engine's return code ignored. An axis whose |angle| is not
    // strictly greater than 0.01 degrees (unordered included) is skipped.
    // Y (FUN_063041b0) and Z (FUN_063041f0) take cos/sin of the float-rounded angle. X is
    // inlined: FCOS uses the unrounded x*k still on the stack, FSIN the float-rounded copy.
    static const float kDegToRad = 0.017453292f;   // .rdata:0639d210
    static const double kAngleEpsilon = 0.01;      // .rdata:0639e6b0, compared against fabs()

    Matrix Accum;
    MatrixSetIdentity(Accum);
    Matrix Tmp;

    if (fabs((double)(AnglesDegrees.y)) > kAngleEpsilon)
    {
        const float Angle = AnglesDegrees.y * kDegToRad;
        const float Cos = X87CosF((double)(Angle));
        const float Sin = X87SinF((double)(Angle));
        Matrix Rot;
        Rot.E[0][0] = Cos;  Rot.E[0][1] = 0.0f; Rot.E[0][2] = Sin;
        Rot.E[1][0] = 0.0f; Rot.E[1][1] = 1.0f; Rot.E[1][2] = 0.0f;
        Rot.E[2][0] = -Sin; Rot.E[2][1] = 0.0f; Rot.E[2][2] = Cos;
        Acquire3DMathEngine()->MulMatrix(Tmp, Accum, Rot);
        CopyMatrix(Accum, Tmp);
    }

    if (fabs((double)(AnglesDegrees.x)) > kAngleEpsilon)
    {
        const double AngleExact = (double)(AnglesDegrees.x) * kDegToRad;
        const float AngleRounded = (float)(AngleExact);
        const float Cos = X87CosF(AngleExact);
        const float Sin = X87SinF((double)(AngleRounded));
        Matrix Rot;
        Rot.E[0][0] = 1.0f; Rot.E[0][1] = 0.0f; Rot.E[0][2] = 0.0f;
        Rot.E[1][0] = 0.0f; Rot.E[1][1] = Cos;  Rot.E[1][2] = -Sin;
        Rot.E[2][0] = 0.0f; Rot.E[2][1] = Sin;  Rot.E[2][2] = Cos;
        Acquire3DMathEngine()->MulMatrix(Tmp, Accum, Rot);
        CopyMatrix(Accum, Tmp);
    }

    if (fabs((double)(AnglesDegrees.z)) > kAngleEpsilon)
    {
        const float Angle = AnglesDegrees.z * kDegToRad;
        const float Cos = X87CosF((double)(Angle));
        const float Sin = X87SinF((double)(Angle));
        Matrix Rot;
        Rot.E[0][0] = Cos;  Rot.E[0][1] = -Sin; Rot.E[0][2] = 0.0f;
        Rot.E[1][0] = Sin;  Rot.E[1][1] = Cos;  Rot.E[1][2] = 0.0f;
        Rot.E[2][0] = 0.0f; Rot.E[2][1] = 0.0f; Rot.E[2][2] = 1.0f;
        Acquire3DMathEngine()->MulMatrix(Tmp, Accum, Rot);
        CopyMatrix(Accum, Tmp);
    }

    return Accum;
}

Matrix __cdecl GetDirectionMatrix(Transform const& Xform, Vector const& Target)
{
    // FUN_06303c10: aim the transform at Target. Column 2 becomes the normalised vector from
    // Target back to the transform's position, and the other two columns are rebuilt around it.
    Matrix Base = Xform.Rotation;

    Vector Delta;
    Delta.x = Xform.Translation.x - Target.x;
    Delta.y = Xform.Translation.y - Target.y;
    Delta.z = Xform.Translation.z - Target.z;

    // Only three exact zeros return the rotation unchanged (NaN rebuilds).
    if (Delta.x == 0.0f && Delta.y == 0.0f && Delta.z == 0.0f)
        return Base;

    // x87 stack values are double below, stored floats are float.
    const double Dx = Delta.x;
    const double Dy = Delta.y;
    const double Dz = Delta.z;
    const double InvLen = 1.0 / sqrt((Dz * Dz + Dy * Dy) + Dx * Dx);
    Vector Dir;
    Dir.x = (float)(Dx * InvLen);
    Dir.y = (float)(Dy * InvLen);
    Dir.z = (float)(InvLen * Dz);

    // How parallel column 1 already is to Dir. Retail truncates the dot product to an integer
    // (_ftol) and takes the integer abs before the 0.98 compare, so only |dot| >= 1 picks
    // column 0.
    const double Dot = ((double)Base.E[1][1] * Dir.y + (double)Base.E[2][1] * Dir.z) +
                       (double)Base.E[0][1] * Dir.x;
    const int Truncated = abs((int)(long long)Dot);

    Vector Col0;
    Vector Col1;
    if ((double)Truncated <= 0.98)
    {
        // Reference axis: column 1 of the incoming rotation.
        Vector Ref;
        Ref.x = Base.E[0][1];
        Ref.y = Base.E[1][1];
        Ref.z = Base.E[2][1];

        // Ortho = Ref - Dir * Along, one float product each.
        const float Along = (float)(((double)Ref.z * Dir.z + (double)Ref.y * Dir.y) + (double)Ref.x * Dir.x);
        Vector Ortho;
        Ortho.x = Ref.x - Along * Dir.x;
        Ortho.y = Ref.y - Along * Dir.y;
        Ortho.z = Ref.z - Along * Dir.z;

        // The reciprocal stays on the stack.
        const double Ox = Ortho.x;
        const double Oy = Ortho.y;
        const double Oz = Ortho.z;
        const double Inv = 1.0f / sqrt((Ox * Ox + Oy * Oy) + Oz * Oz);
        Col1.x = (float)(Ox * Inv);
        Col1.y = (float)(Oy * Inv);
        Col1.z = (float)(Oz * Inv);

        // Col0 = Col1 x Dir.
        Col0.z = (float)((double)Col1.x * Dir.y - (double)Col1.y * Dir.x);
        Col0.y = (float)((double)Col1.z * Dir.x - (double)Col1.x * Dir.z);
        Col0.x = (float)((double)Col1.y * Dir.z - (double)Col1.z * Dir.y);
    }
    else
    {
        // Reference axis: column 0 instead.
        Vector Ref;
        Ref.x = Base.E[0][0];
        Ref.y = Base.E[1][0];
        Ref.z = Base.E[2][0];

        const float Along = (float)(((double)Ref.z * Dir.z + (double)Ref.y * Dir.y) + (double)Ref.x * Dir.x);
        Vector Ortho;
        Ortho.x = Ref.x - Along * Dir.x;
        Ortho.y = Ref.y - Along * Dir.y;
        Ortho.z = Ref.z - Along * Dir.z;

        const double Ox = Ortho.x;
        const double Oy = Ortho.y;
        const double Oz = Ortho.z;
        const double Inv = 1.0f / sqrt((Ox * Ox + Oy * Oy) + Oz * Oz);
        Col0.x = (float)(Ox * Inv);
        Col0.y = (float)(Oy * Inv);
        Col0.z = (float)(Oz * Inv);

        // Dir x Col0, the opposite order from the other arm, so the basis stays right-handed.
        Col1.x = (float)((double)Col0.z * Dir.y - (double)Col0.y * Dir.z);
        Col1.y = (float)((double)Col0.x * Dir.z - (double)Col0.z * Dir.x);
        Col1.z = (float)((double)Col0.y * Dir.x - (double)Col0.x * Dir.y);
    }

    // FUN_06275ee0 writes its three arguments as columns.
    Matrix Out;
    Out.E[0][0] = Col0.x; Out.E[1][0] = Col0.y; Out.E[2][0] = Col0.z;
    Out.E[0][1] = Col1.x; Out.E[1][1] = Col1.y; Out.E[2][1] = Col1.z;
    Out.E[0][2] = Dir.x;  Out.E[1][2] = Dir.y;  Out.E[2][2] = Dir.z;
    return Out;
}

Vector __cdecl MatrixColumn2(Matrix const& M)
{
    // FUN_062d3af0 (thiscall in retail). Raw column 2, no sign flip -- unlike the inlined
    // "Forward = -E[*][2]" call sites elsewhere.
    Vector Out;
    Out.x = M.E[0][2];
    Out.y = M.E[1][2];
    Out.z = M.E[2][2];
    return Out;
}

Vector __cdecl MatrixColumn0(Matrix const& M)
{
    // FUN_062e0480 (thiscall in retail). Raw column 0, no sign flip.
    Vector Out;
    Out.x = M.E[0][0];
    Out.y = M.E[1][0];
    Out.z = M.E[2][0];
    return Out;
}

Vector __cdecl MatrixColumn1(Matrix const& M)
{
    // FUN_062e04a0 (thiscall in retail). Raw column 1, no sign flip.
    Vector Out;
    Out.x = M.E[0][1];
    Out.y = M.E[1][1];
    Out.z = M.E[2][1];
    return Out;
}

void __cdecl MatrixSetColumn2(Matrix& M, Vector const& Src)
{
    // FUN_062e3710, the setter mirror of MatrixColumn2.
    M.E[0][2] = Src.x;
    M.E[1][2] = Src.y;
    M.E[2][2] = Src.z;
}

Csys __cdecl DALib_Engine_get_csys(long Handle)
{
    // FUN_06307900: GetInstanceTransform (engine slot +0xa0) repacked as a Csys. Nothing is
    // tested.
    Transform const* const Xform =
        DALib::Engine->GetInstanceTransform(Handle);

    Csys Out;
    Out.Pos = Xform->Translation;
    Out.Orient = Xform->Rotation;
    return Out;
}


// FUN_06307cc0: the largest |A - B| over the position and the nine matrix elements. A NaN
// difference is never taken.
float __cdecl Csys_error(Csys const& A, Csys const& B)
{
    float Error = 0.0f;
    float const DeltaX = fabs(A.Pos.x - B.Pos.x);
    float const DeltaY = fabs(A.Pos.y - B.Pos.y);
    float const DeltaZ = fabs(A.Pos.z - B.Pos.z);

    if (Error < DeltaX) Error = DeltaX;
    if (Error < DeltaY) Error = DeltaY;
    if (Error < DeltaZ) Error = DeltaZ;

    unsigned Row = 0;
    for (; Row < 3; ++Row)
    {
        unsigned Col = 0;
        for (; Col < 3; ++Col)
        {
            float const Delta = fabs(A.Orient.E[Row][Col] - B.Orient.E[Row][Col]);
            if (Error < Delta) Error = Delta;
        }
    }
    return Error;
}

// FUN_06307b40 (ordinal 1332). x87: dx, dy, dy*T and dz*T stay on the stack; dz and dx*T are
// spilled as floats. Orientation: MatrixToQuaternion of B then A, QuatSlerp, a temporary copy,
// QuaternionToMatrix; return codes are ignored.
Csys __cdecl Csys_interpolate(Csys A, Csys B, float T)
{
    double const Dx = (double)(B.Pos.x) - (double)(A.Pos.x);
    double const Dy = (double)(B.Pos.y) - (double)(A.Pos.y);
    float const DzF = (float)((double)(B.Pos.z) - (double)(A.Pos.z));
    float const DxTF = (float)(Dx * (double)(T));
    double const DyT = Dy * (double)(T);
    double const DzT = (double)(DzF) * (double)(T);

    Csys Result;
    Result.Pos.x = (float)((double)(DxTF) + (double)(A.Pos.x));
    Result.Pos.y = (float)(DyT + (double)(A.Pos.y));
    Result.Pos.z = (float)(DzT + (double)(A.Pos.z));

    Quaternion QuatB;
    Quaternion QuatA;
    Quaternion QuatOut;
    Acquire3DMathEngine()->MatrixToQuaternion(QuatB, B.Orient);
    Acquire3DMathEngine()->MatrixToQuaternion(QuatA, A.Orient);
    Acquire3DMathEngine()->QuatSlerp(QuatOut, QuatA, QuatB, T);
    Quaternion const QuatTmp = QuatOut;
    Acquire3DMathEngine()->QuaternionToMatrix(Result.Orient, QuatTmp);
    return Result;
}

// FUN_06307960: the Csys as a Transform, to SetInstanceMatrix (engine slot +0x9c). The engine
// is not tested.
void __cdecl DALib_Engine_set_csys(long Handle, Csys const& InCsys)
{
    Transform Xform = InCsys.get_transform();
    DALib::Engine->SetInstanceMatrix(Handle, &Xform);
}

// FUN_06391cf0. DEVIATION: retail's body does not cache; it acquires and calls CreateInstance
// every time, and the caching lives in about 25 per-translation-unit statics at the call
// sites. The port's call sites do not cache, so the cache is collapsed into this one static.
// Retail may hold up to 25 references to the engine, the port holds one; no call site ever
// releases its reference. If CreateInstance ever stops returning a shared instance, move the
// cache back out to the call sites.
I3DMathEngine* __cdecl Acquire3DMathEngine()
{
    static I3DMathEngine* CachedEngine = nullptr;
    if (CachedEngine != nullptr)
        return CachedEngine;

    // A 12-byte descriptor: AGGDESC's {size, interface name} plus a null third dword.
    struct FAGGDESC12
    {
        AGGDESC Base;
        void* Reserved08;
    };

    FAGGDESC12 Desc;
    Desc.Base.size = 12;
    Desc.Base.interface_name = "3DMathEngine";
    Desc.Reserved08 = nullptr;

    // CreateInstance (slot +0x0c) with no test of the manager.
    void* Engine = nullptr;
    ICOManager* const Manager = DACOM_Acquire();
    Manager->CreateInstance(&Desc.Base, &Engine);

    CachedEngine = (I3DMathEngine*)Engine;
    return CachedEngine;
}
