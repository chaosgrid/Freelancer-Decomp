// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cmath>
#include <cstring>

#include "x87_trig.h"

struct IVP_U_Matrix3;
struct IVP_U_Matrix;

// Precision convention for this file (retail is VC6 x87 code): a value retail keeps on the
// x87 stack is `double` here, a value retail spills with FSTP dword is `float`. Sums are
// written in retail's evaluation order (it changes the rounding). Every function below
// names the retail address it ports.

// 16-byte 3D point/vector (4th float used for hesse distance or padding)
struct IVP_U_Point
{
    union
    {
        struct
        {
            float x;
            float y;
            float z;
            float hesse;
        };
        float k[4];
    };

    void set(float _x, float _y, float _z)
    {
        k[0] = _x;
        k[1] = _y;
        k[2] = _z;
    }

    // FUN_0628bdf0: copies k[0..2].
    void set(const IVP_U_Point *p)
    {
        k[0] = p->k[0];
        k[1] = p->k[1];
        k[2] = p->k[2];
    }

    // FUN_0628bea0: this = v1 - v2, one rounding per component; every operand is read
    // before the first store.
    void subtract(const IVP_U_Point *v1, const IVP_U_Point *v2)
    {
        const float Y = v1->k[1] - v2->k[1];
        const float Z = v1->k[2] - v2->k[2];
        const float X = v1->k[0] - v2->k[0];
        k[0] = X;
        k[1] = Y;
        k[2] = Z;
    }

    // FUN_06347e60: sqrt((x*x + y*y) + z*z); the result is returned on the x87 stack unrounded.
    double real_length() const
    {
        const double X = k[0];
        const double Y = k[1];
        const double Z = k[2];
        return sqrt((X * X + Y * Y) + Z * Z);
    }

    // FUN_06347e90. Normalises when q >= 1e-10f (DAT_063a53d8); less or NaN
    // leaves the vector alone and returns 0. Returns q * inv unrounded.
    double real_length_plus_normize()
    {
        const double X = k[0];
        const double Y = k[1];
        const double Z = k[2];
        const double QuadLength = (X * X + Y * Y) + Z * Z;
        if (!(QuadLength >= (double)(1e-10f)))
            return 0.0;
        const double InvLength = 1.0 / sqrt(QuadLength);
        k[0] = (float)(InvLength * k[0]);
        k[1] = (float)(InvLength * k[1]);
        k[2] = (float)(InvLength * k[2]);
        return QuadLength * InvLength;
    }

    // FUN_06347ef0. Normalises when |v|^2 >= 1e-10f and returns 1; less or NaN
    // leaves v alone and returns 0.
    int normize()
    {
        const double QuadLength = ((double)(k[0]) * k[0] + (double)(k[1]) * k[1]) +
                                  (double)(k[2]) * k[2];
        if (!(QuadLength >= (double)(1e-10f)))
            return 0;
        const double InvLength = 1.0 / sqrt(QuadLength);
        k[0] = (float)(InvLength * k[0]);
        k[1] = (float)(InvLength * k[1]);
        k[2] = (float)(InvLength * k[2]);
        return 1;
    }

    // FUN_06347e00: this = p1 * (1 - t) + p2 * t. Both z products are rounded
    // to float before their sum; x and y are not.
    void set_interpolate(const IVP_U_Point *p1, const IVP_U_Point *p2, float t)
    {
        const double S = 1.0 - (double)(t);
        const float Z1 = (float)(S * p1->k[2]);
        const float Z2 = (float)((double)(t) * p2->k[2]);
        const double Y = S * p1->k[1] + (double)(t) * p2->k[1];
        const double X = S * p1->k[0] + (double)(t) * p2->k[0];
        k[0] = (float)(X);
        k[1] = (float)(Y);
        k[2] = (float)((double)(Z2) + Z1);
    }

    // FUN_06347f50: this = v - dot(v, dir) * dir. All reads precede the first
    // store; the stores go z, x, y.
    void set_perpendicular_component(const IVP_U_Point *v, const IVP_U_Point *dir)
    {
        const double NegDot = -(((double)(v->k[0]) * dir->k[0] + (double)(dir->k[2]) * v->k[2]) +
                                (double)(v->k[1]) * dir->k[1]);
        const double Y = NegDot * dir->k[1] + v->k[1];
        const float Z = (float)(NegDot * dir->k[2] + v->k[2]);
        const double X = NegDot * dir->k[0] + v->k[0];
        k[2] = Z;
        k[0] = (float)(X);
        k[1] = (float)(Y);
    }

    // FUN_06347fa0: {disc, root0, root1} of a*x^2 + b*x + c = 0 (coeffs =
    // {a, b, c}). |a| < 1e-10f or NaN goes linear (k[0] = -1 when b is also
    // tiny). The sign test uses the unrounded disc, sqrt the float-rounded one;
    // the lower root uses the float-rounded sqrt.
    void set_quadratic_roots(const IVP_U_Point *coeffs)
    {
        const double Epsilon = (double)(1e-10f); // DAT_063a53d8
        const float a = coeffs->k[0];
        const float b = coeffs->k[1];
        const float c = coeffs->k[2];

        if (fabs((double)(a)) >= Epsilon)
        {
            const double Disc = (double)(b) * b - ((double)(c) * a) * 4.0;
            const float DiscF = (float)(Disc);
            k[0] = DiscF;
            if (!(Disc >= 0.0))
                return;
            const double HalfInvA = 0.5 / (double)(a);
            const double Root = sqrt((double)(DiscF));
            const float RootF = (float)(Root);
            const double Upper = (Root - b) * HalfInvA;
            const double Lower = (-(double)(b) - RootF) * HalfInvA;
            k[1] = (float)(Lower);
            k[2] = (float)(Upper);
            return;
        }

        k[0] = 0.0f;
        if (!(fabs((double)(b)) >= Epsilon))
        {
            k[0] = -1.0f;
            return;
        }
        const double Root = -((double)(c) / b);
        k[2] = (float)(Root);
        k[1] = (float)(Root);
    }

    // FUN_063480a0: the unnormalised Hesse plane through three points, with
    // n = b x a (not a x b) for a = p1 - p0, b = p2 - p0. The hesse term uses
    // n.z unrounded but n.x / n.y as stored floats.
    void set_plane_from_3_points(const IVP_U_Point *p0, const IVP_U_Point *p1, const IVP_U_Point *p2)
    {
        const double X0 = p0->k[0];
        const double Y0 = p0->k[1];
        const double Z0 = p0->k[2];
        const double Ax = p1->k[0] - X0;
        const double Ay = p1->k[1] - Y0;
        const double Az = p1->k[2] - Z0;
        const float Bx = (float)(p2->k[0] - X0);
        const float By = (float)(p2->k[1] - Y0);
        const float Bz = (float)(p2->k[2] - Z0);

        const float Nx = (float)(By * Az - Bz * Ay);
        k[0] = Nx;
        const float Ny = (float)(Bz * Ax - Bx * Az);
        k[1] = Ny;
        const double Nz = Bx * Ay - By * Ax;
        k[2] = (float)(Nz);
        k[3] = (float)(-((Nz * Z0 + (double)(Ny) * Y0) + (double)(Nx) * X0));
    }

    // FUN_06348150. No zero-length guard in retail.
    void normize_plane()
    {
        const double InvLength = 1.0 / real_length();
        k[0] = (float)(InvLength * k[0]);
        k[1] = (float)(InvLength * k[1]);
        k[2] = (float)(InvLength * k[2]);
        k[3] = (float)(InvLength * k[3]);
    }

    // FUN_06348180: this = -v; all reads before the first store.
    void set_negated(const IVP_U_Point *v)
    {
        const float y = v->k[1];
        const float z = v->k[2];
        k[0] = -v->k[0];
        k[2] = -z;
        k[1] = -y;
    }

    // FUN_063485e0, defined after IVP_U_Point::calc_cross_product below.
    void set_arbitrary_perpendicular_to(const IVP_U_Point *v);

    // FUN_0628bed0 (thiscall this = Out, RET 8), defined below.
    void calc_cross_product(const IVP_U_Point* A, const IVP_U_Point* B);
};

// FUN_06347df0: 1 / sqrt(x), returned unrounded on the x87 stack.
inline double IVP_InvSqrtFloat(float Value)
{
    return 1.0 / sqrt((double)(Value));
}

// FUN_0628bed0: Out = A x B, one rounding per component; every operand is read before
// the first store (alias-safe). The PhySys copy of the same retail function is
// PhySys_Vec3Cross (common_physys.cpp, on Vector).
inline void IVP_U_Point::calc_cross_product(const IVP_U_Point* A, const IVP_U_Point* B)
{
    const double Ax = A->k[0], Ay = A->k[1], Az = A->k[2];
    const double Bx = B->k[0], By = B->k[1], Bz = B->k[2];
    const double Y = Bx * Az - Ax * Bz;
    const double X = Ay * Bz - Az * By;
    const double Z = Ax * By - Ay * Bx;
    k[0] = (float)(X);
    k[1] = (float)(Y);
    k[2] = (float)(Z);
}

// FUN_063485e0: a vector perpendicular to v. The largest |component| is found scanning
// from index 2 down (ties keep the higher index); it is swapped with its predecessor
// (wrapping) and one of them negated, then this = this x v.
inline void IVP_U_Point::set_arbitrary_perpendicular_to(const IVP_U_Point *v)
{
    k[0] = v->k[0];
    k[1] = v->k[1];
    k[2] = v->k[2];

    int MaxAxis = 0;
    float MaxAbs = 0.0f;
    for (int Axis = 2; Axis >= 0; --Axis)
    {
        const double Abs = fabs((double)(k[Axis]));
        if (Abs > MaxAbs)
        {
            MaxAbs = (float)(Abs);
            MaxAxis = Axis;
        }
    }

    const float Negated = (float)((double)(k[MaxAxis]) * -1.0);
    int PrevAxis = MaxAxis - 1;
    if (PrevAxis < 0)
        PrevAxis = 2;
    k[MaxAxis] = k[PrevAxis];
    k[PrevAxis] = Negated;

    calc_cross_product(this, v);
}

// 48-byte 3x3 rotation matrix (3 rows of 16-byte IVP_U_Point)
struct IVP_U_Matrix3
{
    IVP_U_Point rows[3];

    IVP_U_Point *get_row(int index) { return &rows[index]; }
    const IVP_U_Point *get_row(int index) const { return &rows[index]; }

    // FUN_06348580: out = M * in. Every read precedes the first store.
    void vmult(const IVP_U_Point *p_in, IVP_U_Point *p_out) const
    {
        const double X = p_in->k[0];
        const double Y = p_in->k[1];
        const double Z = p_in->k[2];
        const double Ry = (rows[1].k[0] * X + rows[1].k[2] * Z) + rows[1].k[1] * Y;
        const float Rz = (float)((rows[2].k[0] * X + rows[2].k[2] * Z) + rows[2].k[1] * Y);
        const double Rx = (rows[0].k[2] * Z + rows[0].k[1] * Y) + rows[0].k[0] * X;
        p_out->k[2] = Rz;
        p_out->k[0] = (float)(Rx);
        p_out->k[1] = (float)(Ry);
    }

    // FUN_06366010: out = M^T * in. Every read precedes the first store.
    void vimult(const IVP_U_Point *p_in, IVP_U_Point *p_out) const
    {
        const double X = p_in->k[0];
        const double Y = p_in->k[1];
        const double Z = p_in->k[2];
        const double Ry = (rows[0].k[1] * X + rows[2].k[1] * Z) + rows[1].k[1] * Y;
        const float Rz = (float)((rows[0].k[2] * X + rows[2].k[2] * Z) + rows[1].k[2] * Y);
        const double Rx = (rows[2].k[0] * Z + rows[1].k[0] * Y) + rows[0].k[0] * X;
        p_out->k[2] = Rz;
        p_out->k[0] = (float)(Rx);
        p_out->k[1] = (float)(Ry);
    }
};

// 64-byte 3x4 transformation matrix
struct IVP_U_Matrix : public IVP_U_Matrix3
{
    IVP_U_Point vv; // translation offset at +0x30

    // FUN_063481b0: zeroes all 0x10 dwords (4th floats included), then the identity.
    void init()
    {
        memset(this, 0, sizeof(IVP_U_Matrix));
        rows[0].k[0] = 1.0f;
        rows[1].k[1] = 1.0f;
        rows[2].k[2] = 1.0f;
    }

    // FUN_063484e0: out = M * in + vv. Retail rounds the x and y partial sums to
    // float after every term; z stays extended.
    void vmult4(const IVP_U_Point *p_in, IVP_U_Point *p_out) const
    {
        const double X = p_in->k[0];
        float Tx = (float)(rows[0].k[0] * X);
        float Ty = (float)(rows[1].k[0] * X);
        double Ez = X * rows[2].k[0];
        const double Y = p_in->k[1];
        const float Z = p_in->k[2];
        Tx = (float)(rows[0].k[1] * Y + Tx);
        Ty = (float)(rows[1].k[1] * Y + Ty);
        Ez = Ez + rows[2].k[1] * Y;
        Tx = (float)((double)(rows[0].k[2]) * Z + Tx);
        const double Ry = ((double)(rows[1].k[2]) * Z + Ty) + vv.k[1];
        const float Rz = (float)(((double)(rows[2].k[2]) * Z + Ez) + vv.k[2]);
        const double Rx = (double)(Tx) + vv.k[0];
        p_out->k[2] = Rz;
        p_out->k[0] = (float)(Rx);
        p_out->k[1] = (float)(Ry);
    }

    // FUN_06348470: out = M^T * (in - vv), the difference kept unrounded.
    void vimult4(const IVP_U_Point *p_in, IVP_U_Point *p_out) const
    {
        const double Dx = (double)(p_in->k[0]) - vv.k[0];
        const double Dy = (double)(p_in->k[1]) - vv.k[1];
        const double Dz = (double)(p_in->k[2]) - vv.k[2];
        p_out->k[0] = (float)((rows[2].k[0] * Dz + rows[1].k[0] * Dy) + rows[0].k[0] * Dx);
        p_out->k[1] = (float)((rows[2].k[1] * Dz + rows[1].k[1] * Dy) + rows[0].k[1] * Dx);
        p_out->k[2] = (float)((rows[2].k[2] * Dz + rows[1].k[2] * Dy) + rows[0].k[2] * Dx);
    }

    // FUN_063481d0: result = this * mb (rotation), result.vv = this.rot * mb.vv +
    // this.vv. this.vv is read only after the rows are written; retail callers pass
    // result == this (06347503) or a separate local (06343da4).
    void mmult4(const IVP_U_Matrix *mb, IVP_U_Matrix *result) const
    {
        IVP_U_Point Translation;
        vmult(&mb->vv, &Translation);

        float C[3][3];
        for (int Row = 0; Row < 3; ++Row)
        {
            const double Ax = rows[Row].k[0];
            const double Ay = rows[Row].k[1];
            const double Az = rows[Row].k[2];
            C[Row][0] = (float)((mb->rows[0].k[0] * Ax + mb->rows[2].k[0] * Az) + mb->rows[1].k[0] * Ay);
            C[Row][1] = (float)((mb->rows[2].k[1] * Az + mb->rows[1].k[1] * Ay) + mb->rows[0].k[1] * Ax);
            C[Row][2] = (float)((mb->rows[2].k[2] * Az + mb->rows[1].k[2] * Ay) + mb->rows[0].k[2] * Ax);
        }
        for (int Row = 0; Row < 3; ++Row)
        {
            result->rows[Row].k[0] = C[Row][0];
            result->rows[Row].k[1] = C[Row][1];
            result->rows[Row].k[2] = C[Row][2];
        }

        const double Ty = (double)(Translation.k[1]) + vv.k[1];
        const float Tz = (float)((double)(Translation.k[2]) + vv.k[2]);
        const double Tx = (double)(Translation.k[0]) + vv.k[0];
        result->vv.k[2] = Tz;
        result->vv.k[0] = (float)(Tx);
        result->vv.k[1] = (float)(Ty);
    }

    // FUN_063482c0: result = this^-1 * mb for a rigid this: rotation this^T * mb.rot,
    // translation this^T * float(mb.vv - this.vv), the translation written first.
    void mimult4(const IVP_U_Matrix *mb, IVP_U_Matrix *result) const
    {
        IVP_U_Point Delta;
        Delta.k[0] = (float)((double)(mb->vv.k[0]) - vv.k[0]);
        Delta.k[1] = (float)((double)(mb->vv.k[1]) - vv.k[1]);
        Delta.k[2] = (float)((double)(mb->vv.k[2]) - vv.k[2]);
        vimult(&Delta, &result->vv);

        float T[3][3];
        for (int Row = 0; Row < 3; ++Row)
        {
            T[Row][0] = rows[0].k[Row];
            T[Row][1] = rows[1].k[Row];
            T[Row][2] = rows[2].k[Row];
        }
        for (int Row = 0; Row < 3; ++Row)
        {
            const double Mx = T[Row][0];
            const double My = T[Row][1];
            const double Mz = T[Row][2];
            const double C1 = (mb->rows[2].k[1] * Mz + mb->rows[0].k[1] * Mx) + mb->rows[1].k[1] * My;
            const float C2 = (float)((mb->rows[1].k[2] * My + mb->rows[0].k[2] * Mx) + mb->rows[2].k[2] * Mz);
            const double C0 = (mb->rows[0].k[0] * Mx + mb->rows[2].k[0] * Mz) + mb->rows[1].k[0] * My;
            T[Row][2] = C2;
            T[Row][0] = (float)(C0);
            T[Row][1] = (float)(C1);
        }
        for (int Row = 0; Row < 3; ++Row)
        {
            result->rows[Row].k[0] = T[Row][0];
            result->rows[Row].k[1] = T[Row][1];
            result->rows[Row].k[2] = T[Row][2];
        }
    }

    // FUN_063483d0: this = source^-1 for a rigid source (this == source works). Only
    // k[0..2] of each row are written.
    void set_inverse(const IVP_U_Matrix *Source)
    {
        rows[0].k[0] = Source->rows[0].k[0];
        rows[1].k[1] = Source->rows[1].k[1];
        rows[2].k[2] = Source->rows[2].k[2];
        for (int Row = 1; Row < 3; ++Row)
        {
            for (int Col = 0; Col < Row; ++Col)
            {
                const float Lower = Source->rows[Row].k[Col];
                const float Upper = Source->rows[Col].k[Row];
                rows[Row].k[Col] = Upper;
                rows[Col].k[Row] = Lower;
            }
        }

        IVP_U_Point RotatedTranslation;
        vmult(&Source->vv, &RotatedTranslation);
        vv.k[0] = -RotatedTranslation.k[0];
        vv.k[1] = -RotatedTranslation.k[1];
        vv.k[2] = -RotatedTranslation.k[2];
    }
};

// 16-byte Quaternion
struct IVP_U_Quat
{
    float x;
    float y;
    float z;
    float w;

    void set_matrix(IVP_U_Matrix3 *out_matrix) const;
    void set_from_matrix(const IVP_U_Matrix3 *m);
    void set_interpolate_quat(const IVP_U_Quat *q0, const IVP_U_Quat *q1, float alpha);
    void normize();
    void fast_normize();
    void set_invert_unit(const IVP_U_Quat *q);
    void set_mult_quat(const IVP_U_Quat *q1, const IVP_U_Quat *q2);
    void set_mult_inv_quat(const IVP_U_Quat *q1, const IVP_U_Quat *q2);
    void set_inv_mult_quat(const IVP_U_Quat *q1, const IVP_U_Quat *q2);
    void set_fast_rot_vector(const IVP_U_Point *v, float dt);
    void set_rot_vector(const IVP_U_Point *v, float dt);
};

// FUN_06350bc0: rotation matrix of the quat. Rows' k[3] untouched.
inline void IVP_U_Quat::set_matrix(IVP_U_Matrix3 *out_matrix) const
{
    const double X2 = (double)(x) + x;
    const float Y2 = (float)((double)(y) + y);
    const float Z2 = (float)((double)(z) + z);

    const float XX = (float)(X2 * x);
    const float XY = (float)((double)(Y2) * x);
    const float XZ = (float)((double)(Z2) * x);
    const float YY = (float)((double)(Y2) * y);
    const float YZ = (float)((double)(Z2) * y);
    const float ZZ = (float)((double)(Z2) * z);

    const double WX = X2 * w;
    const double WY = (double)(Y2) * w;
    const double WZ = (double)(Z2) * w;

    out_matrix->rows[0].k[0] = (float)(1.0 - ((double)(ZZ) + YY));
    out_matrix->rows[0].k[1] = (float)((double)(XY) - WZ);
    out_matrix->rows[0].k[2] = (float)(WY + XZ);
    out_matrix->rows[1].k[0] = (float)(WZ + XY);
    out_matrix->rows[1].k[1] = (float)(1.0 - ((double)(ZZ) + XX));
    out_matrix->rows[1].k[2] = (float)((double)(YZ) - WX);
    out_matrix->rows[2].k[0] = (float)((double)(XZ) - WY);
    out_matrix->rows[2].k[1] = (float)(WX + YZ);
    out_matrix->rows[2].k[2] = (float)(1.0 - ((double)(YY) + XX));
}

// FUN_06350df0. dot <= 0 or NaN flips q1's sign; dot >= 0.999f (DAT_063a5630) lerps and
// renormalises with two Newton steps, less or NaN slerps. this == q1 is safe.
inline void IVP_U_Quat::set_interpolate_quat(const IVP_U_Quat *q0, const IVP_U_Quat *q1, float alpha)
{
    double Dot = (((double)(q0->z) * q1->z + (double)(q0->y) * q1->y) +
                  (double)(q1->w) * q0->w) +
                 (double)(q0->x) * q1->x;
    float DotF = (float)(Dot);
    float Sign;
    if (Dot > 0.0)
    {
        Sign = 1.0f;
    }
    else
    {
        Dot = -Dot;
        Sign = -1.0f;
        DotF = (float)(Dot);
    }

    if (!(Dot >= (double)(0.999f)))
    {
        const double Omega = acos((double)(DotF));
        const float InvSin = (float)(1.0 / sqrt(1.0 - (double)(DotF) * DotF));
        const float Scale0 = (float)(X87Sin((1.0 - (double)(alpha)) * Omega) * InvSin);
        const double Scale1 = (X87Sin(Omega * alpha) * InvSin) * Sign;

        x = (float)((double)(Scale0) * q0->x + Scale1 * q1->x);
        y = (float)((double)(Scale0) * q0->y + Scale1 * q1->y);
        z = (float)((double)(Scale0) * q0->z + Scale1 * q1->z);
        w = (float)(Scale1 * q1->w + (double)(Scale0) * q0->w);
        return;
    }

    x = (float)(((double)(Sign) * q1->x - q0->x) * alpha + q0->x);
    y = (float)(((double)(Sign) * q1->y - q0->y) * alpha + q0->y);
    z = (float)(((double)(Sign) * q1->z - q0->z) * alpha + q0->z);
    const double W = ((double)(Sign) * q1->w - q0->w) * alpha + q0->w;
    w = (float)(W);

    const double Half = ((((double)(x) * x + (double)(y) * y) + (double)(z) * z) + W * W) * 0.5;
    double G = 1.5 - Half;
    G = G + (0.5 - (G * G) * Half);
    G = (0.5 - Half * (G * G)) + G;

    x = (float)(G * x);
    y = (float)(G * y);
    z = (float)(G * z);
    w = (float)(G * w);
}

// FUN_06350ca0: quat from a rotation matrix. Trace <= 0 or NaN takes the diagonal
// branch (NaN comparisons pick the later axis). Both branches end in normize.
inline void IVP_U_Quat::set_from_matrix(const IVP_U_Matrix3 *m)
{
    const double Trace = ((double)(m->rows[1].k[1]) + m->rows[0].k[0]) + m->rows[2].k[2];
    if (Trace > 0.0)
    {
        double S = sqrt(Trace + 1.0);
        w = (float)(S * 0.5);
        S = 0.5 / S;
        x = (float)(((double)(m->rows[2].k[1]) - m->rows[1].k[2]) * S);
        y = (float)(((double)(m->rows[0].k[2]) - m->rows[2].k[0]) * S);
        z = (float)(((double)(m->rows[1].k[0]) - m->rows[0].k[1]) * S);
        normize();
        return;
    }

    const int Next[3] = { 1, 2, 0 };
    int I = 0;
    if (!(m->rows[0].k[0] >= m->rows[1].k[1]))
        I = 1;
    if (!(m->rows[I].k[I] >= m->rows[2].k[2]))
        I = 2;
    const int J = Next[I];
    const int K = Next[J];

    double S = sqrt(((double)(m->rows[I].k[I]) - ((double)(m->rows[K].k[K]) + m->rows[J].k[J])) + 1.0);
    float Q[4];
    Q[I] = (float)(S * 0.5);
    // Zero or NaN skips the division.
    if (S > 0.0 || S < 0.0)
        S = 0.5 / S;

    Q[3] = (float)(((double)(m->rows[K].k[J]) - m->rows[J].k[K]) * S);
    Q[J] = (float)(((double)(m->rows[J].k[I]) + m->rows[I].k[J]) * S);
    Q[K] = (float)(((double)(m->rows[K].k[I]) + m->rows[I].k[K]) * S);

    x = Q[0];
    y = Q[1];
    z = Q[2];
    w = Q[3];
    normize();
}

// FUN_06350fb0. len <= 1e-10f or NaN leaves the quat alone.
inline void IVP_U_Quat::normize()
{
    const double LenSq = (((double)(x) * x + (double)(y) * y) + (double)(z) * z) +
                         (double)(w) * w;
    if (!(LenSq > (double)(1e-10f)))
        return;
    const double InvLen = 1.0 / sqrt(LenSq);
    x = (float)(InvLen * x);
    y = (float)(InvLen * y);
    z = (float)(InvLen * z);
    w = (float)(InvLen * w);
}

// FUN_06351010: Newton iteration on the float-rounded squared length until
// |1 - f*f*len| <= 1e-6f (DAT_063a5610); already within (or NaN) leaves the quat alone.
inline void IVP_U_Quat::fast_normize()
{
    const float LenSq = (float)((((double)(x) * x + (double)(y) * y) +
                                            (double)(z) * z) +
                                           (double)(w) * w);
    const double Epsilon = (double)(1e-6f);
    if (!(fabs(1.0 - LenSq) > Epsilon))
        return;

    double F = 1.5 - (double)(LenSq) * 0.5;
    do
    {
        F = F + (1.0 - (F * F) * LenSq) * 0.5;
    } while (fabs(1.0 - (F * F) * LenSq) > Epsilon);

    x = (float)(F * x);
    y = (float)(F * y);
    z = (float)(F * z);
    w = (float)(F * w);
}

// FUN_063510c0: (-x, -y, -z, w).
inline void IVP_U_Quat::set_invert_unit(const IVP_U_Quat *q)
{
    x = -q->x;
    y = -q->y;
    z = -q->z;
    w = q->w;
}

// FUN_063510f0: this = q1 * q2. All reads precede the first store.
inline void IVP_U_Quat::set_mult_quat(const IVP_U_Quat *q1, const IVP_U_Quat *q2)
{
    const double Ry = (((double)(q1->w) * q2->y + (double)(q2->w) * q1->y) +
                       (double)(q1->z) * q2->x) -
                      (double)(q2->z) * q1->x;
    const float Rz = (float)((((double)(q1->w) * q2->z + (double)(q1->x) * q2->y) +
                                         (double)(q1->z) * q2->w) -
                                        (double)(q2->x) * q1->y);
    const float Rw = (float)((((double)(q1->w) * q2->w - (double)(q1->x) * q2->x) -
                                         (double)(q1->y) * q2->y) -
                                        (double)(q1->z) * q2->z);
    const double Rx = (((double)(q1->x) * q2->w + (double)(q2->z) * q1->y) +
                       (double)(q1->w) * q2->x) -
                      (double)(q1->z) * q2->y;
    z = Rz;
    w = Rw;
    x = (float)(Rx);
    y = (float)(Ry);
}

// FUN_06351190: this = q1 * q2^-1, with its own summation order.
inline void IVP_U_Quat::set_mult_inv_quat(const IVP_U_Quat *q1, const IVP_U_Quat *q2)
{
    IVP_U_Quat L;
    L.set_invert_unit(q2);
    const double Ry = (((double)(L.x) * q1->z + (double)(L.y) * q1->w) +
                       (double)(L.w) * q1->y) -
                      (double)(L.z) * q1->x;
    const float Rz = (float)((((double)(L.y) * q1->x + (double)(L.w) * q1->z) +
                                         (double)(L.z) * q1->w) -
                                        (double)(L.x) * q1->y);
    const float Rw = (float)((((double)(L.w) * q1->w - (double)(L.x) * q1->x) -
                                         (double)(L.y) * q1->y) -
                                        (double)(L.z) * q1->z);
    const double Rx = (((double)(L.w) * q1->x + (double)(L.z) * q1->y) +
                       (double)(L.x) * q1->w) -
                      (double)(L.y) * q1->z;
    z = Rz;
    w = Rw;
    x = (float)(Rx);
    y = (float)(Ry);
}

// FUN_06351250: this = q1^-1 * q2.
inline void IVP_U_Quat::set_inv_mult_quat(const IVP_U_Quat *q1, const IVP_U_Quat *q2)
{
    IVP_U_Quat L;
    L.set_invert_unit(q1);
    const double Ry = (((double)(L.z) * q2->x + (double)(L.y) * q2->w) +
                       (double)(L.w) * q2->y) -
                      (double)(L.x) * q2->z;
    const float Rz = (float)((((double)(L.w) * q2->z + (double)(L.z) * q2->w) +
                                         (double)(L.x) * q2->y) -
                                        (double)(L.y) * q2->x);
    const float Rw = (float)((((double)(L.w) * q2->w - (double)(L.x) * q2->x) -
                                         (double)(L.y) * q2->y) -
                                        (double)(L.z) * q2->z);
    const double Rx = (((double)(L.y) * q2->z + (double)(L.w) * q2->x) +
                       (double)(L.x) * q2->w) -
                      (double)(L.z) * q2->y;
    z = Rz;
    w = Rw;
    x = (float)(Rx);
    y = (float)(Ry);
}

// FUN_06350aa0: per axis sin approximated as s - s^3/6 (DAT_063a5624); w uses the
// rounded x, y but the unrounded z.
inline void IVP_U_Quat::set_fast_rot_vector(const IVP_U_Point *v, float dt)
{
    const double SixthF = (double)(0.16666667f);
    const double Half = (double)(dt) * 0.5;

    double S = Half * v->k[0];
    S = S - ((S * S) * S) * SixthF;
    const float Sx = (float)(S);
    x = Sx;

    S = Half * v->k[1];
    S = S - ((S * S) * S) * SixthF;
    const float Sy = (float)(S);
    y = Sy;

    S = Half * v->k[2];
    S = S - ((S * S) * S) * SixthF;
    z = (float)(S);

    w = (float)(sqrt(1.0 - ((S * S + (double)(Sy) * Sy) + (double)(Sx) * Sx)));
}

// FUN_06350b20: x87 FSIN per axis; a squared length > 1 rescales xyz by
// 0.997f (DAT_063a5628) / sqrt(len) before w = sqrt(1 - len).
inline void IVP_U_Quat::set_rot_vector(const IVP_U_Point *v, float dt)
{
    const double Half = (double)(dt) * 0.5;
    x = X87SinF(Half * v->k[0]);
    y = X87SinF(Half * v->k[1]);
    // z is stored rounded but its unrounded value feeds the length.
    const double Sz = X87Sin(Half * v->k[2]);
    z = X87SinF(Half * v->k[2]);

    double LenSq = ((double)(x) * x + (double)(y) * y) + Sz * Sz;
    if (LenSq > 1.0)
    {
        const double F = (double)(0.997f) / sqrt(LenSq);
        const float Rx = (float)(F * x);
        x = Rx;
        const float Ry = (float)(F * y);
        y = Ry;
        const double Rz = F * z;
        z = (float)(Rz);
        LenSq = (Rz * Rz + (double)(Ry) * Ry) + (double)(Rx) * Rx;
    }
    w = (float)(sqrt(1.0 - LenSq));
}

static_assert(sizeof(IVP_U_Point) == 0x10, "IVP_U_Point size mismatch");
static_assert(sizeof(IVP_U_Matrix3) == 0x30, "IVP_U_Matrix3 size mismatch");
static_assert(sizeof(IVP_U_Matrix) == 0x40, "IVP_U_Matrix size mismatch");
static_assert(sizeof(IVP_U_Quat) == 0x10, "IVP_U_Quat size mismatch");

// FUN_06350a60: out = 2 * asin(in) per component, each stored before the next is read.
// CRT asin matches retail _CIasin (same SSE2 core, see x87_trig.h).
inline void IVP_DoubleAsinVector(const IVP_U_Point *In, IVP_U_Point *Out)
{
    double A = asin((double)(In->k[0]));
    Out->k[0] = (float)(A + A);
    A = asin((double)(In->k[1]));
    Out->k[1] = (float)(A + A);
    A = asin((double)(In->k[2]));
    Out->k[2] = (float)(A + A);
}
