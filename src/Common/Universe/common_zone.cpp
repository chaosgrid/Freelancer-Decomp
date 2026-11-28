// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll ZONE -- Universe::IZone's implementation (RTTI .?AUZONE@@, vftable
// .rdata:063a5094, 37 slots) and the zone shape geometry its slots call (Systems.cpp).
//
// Floating point: retail evaluates on the x87 stack at 53-bit precision and rounds only where
// it stores a float (FSTP dword). The bodies below keep that split: a value retail keeps on
// the stack is a double here, a value retail stores to a float local is a float here, and
// the slots whose result other slots consume in-register (+0x00, +0x3c, +0x40, +0x48) return
// double, so the caller sees the unrounded value, as in retail. A single float operation
// followed by a float store is written as plain float arithmetic (the 53-bit intermediate
// rounds to the same float).
//
// Comparisons are written (`!(a >= b)` and the like) so that an unordered (NaN) operand takes
// the same branch as retail's FCOM / TEST AH / Jcc sequence.

#include "common_zone.h"
#include "common_archetype.h" // CacheString

#include "common_csys.h"  // Acquire3DMathEngine (FUN_06391cf0)
#include "common_misc.h"  // Geometry::Frustum / Sphere, FrustumCull, SphereCull
#include "Physics/common_geometry_internal.h" // MathTransposeTransformVia3DMathEngine, ClampPointToBoxSurface
#include "x86math.h"

#include "DALib.h"
#include "dacom.h"

#include "x87_trig.h" // retail's inline FSIN / FCOS / FPTAN (CullQuatFromAxisAngle, FrustumCull)

#include <math.h>
#include <stdlib.h>

namespace
{
    char const kSystemsSource[] = "E:\\FL\\Scratch\\Source\\Common\\Systems.cpp";
    char const kZoneTypeNotSupported[] = "PCI - zone type not supported!\n";

    // FUN_06334ac0's first argument: a sphere {centre, radius}.
    struct FZoneSphere
    {
        Vector Center; // +0x00
        float Radius;  // +0x0c
    };
    static_assert(sizeof(FZoneSphere) == 0x10, "FZoneSphere is 0x10 bytes");

    // The segment every clip function takes: {start, end}.
    struct FZoneSegment
    {
        Vector Start; // +0x00
        Vector End;   // +0x0c
    };
    static_assert(sizeof(FZoneSegment) == 0x18, "FZoneSegment is 0x18 bytes");

    // The oriented frame the box / cylinder / ring code builds on the stack: the zone's
    // transform followed by three dimensions. ClampPointToBoxSurface (FUN_062dba00) reads the
    // dimensions at +0x30 as half extents; the clip functions read them as their own sizes.
    struct FZoneFrame
    {
        Transform Xform;    // +0x00
        float Dim[3];       // +0x30
    };
    static_assert(sizeof(FZoneFrame) == 0x3c, "FZoneFrame is 0x3c bytes");

    // The ellipsoid object FUN_06339f20 / FUN_06339fb0 / FUN_0633dc90 work on.
    struct FZoneEllipsoid
    {
        Matrix Orientation; // +0x00
        Vector Pos;         // +0x24
        float Radius[3];    // +0x30
        Matrix Scaled;      // +0x3c
    };
    static_assert(sizeof(FZoneEllipsoid) == 0x60, "FZoneEllipsoid is 0x60 bytes");

    // .data:DAT_0658a9b0 -- Systems.cpp's own 3DMathEngine pointer, fetched through
    // FUN_06391cf0 the first time a zone function needs it (every use site tests it first).
    I3DMathEngine* GZoneMathEngine = nullptr;

    I3DMathEngine* ZoneMathEngine()
    {
        if (GZoneMathEngine == nullptr)
            GZoneMathEngine = Acquire3DMathEngine();
        return GZoneMathEngine;
    }

    // FUN_0628b030: this = {X, Y, Z}.
    Vector MakeVector(float X, float Y, float Z)
    {
        Vector Result;
        Result.x = X;
        Result.y = Y;
        Result.z = Z;
        return Result;
    }

    // FUN_06288800: Out = A - B, per component, float stores.
    Vector Vec3Sub(Vector const& A, Vector const& B)
    {
        return MakeVector(A.x - B.x, A.y - B.y, A.z - B.z);
    }

    // FUN_062887d0: Out = A + B, per component, float stores.
    Vector Vec3Add(Vector const& A, Vector const& B)
    {
        return MakeVector(A.x + B.x, A.y + B.y, A.z + B.z);
    }

    // FUN_06288830: Out = V * S, per component, float stores.
    Vector Vec3Scale(Vector const& V, float S)
    {
        return MakeVector(S * V.x, S * V.y, S * V.z);
    }

    // FUN_06347e60: sqrt((x*x + y*y) + z*z), left on the stack.
    double VecLength(Vector const& V)
    {
        return sqrt(((double)V.x * V.x + (double)(V.y) * V.y) +
                    (double)V.z * V.z);
    }

    // FUN_062c4780: (x*x + y*y) + z*z, left on the stack.
    double VecLengthSq(Vector const& V)
    {
        return ((double)V.x * V.x + (double)(V.y) * V.y) +
               (double)V.z * V.z;
    }

    // FUN_062e25c0 (thiscall, RET 4): each component *= S, float stores.
    void VecScaleInPlace(Vector& V, float S)
    {
        V.x = S * V.x;
        V.y = S * V.y;
        V.z = S * V.z;
    }

    // 3DMathEngine vt+0x38 through DAT_0658a9b0: Dst = transpose(M) * Src.
    Vector EngineTransposeTransform(Matrix const& M, Vector const& Src)
    {
        Vector Dst;
        ZoneMathEngine()->TransposeTransform(Dst, M, Src);
        return Dst;
    }

    // 3DMathEngine vt+0x2c through DAT_0658a9b0: Dst = M * Src.
    Vector EngineTransformMatrix(Matrix const& M, Vector const& Src)
    {
        Vector Dst;
        ZoneMathEngine()->TransformMatrix(Dst, M, Src);
        return Dst;
    }

    // 3DMathEngine vt+0x28 through DAT_0658a9b0: Dst = Frame.Xform.Rotation * Src + Frame.Xform.Translation.
    Vector EngineTransformXform(FZoneFrame const& Frame, Vector const& Src)
    {
        Vector Dst;
        ZoneMathEngine()->TransformXform(Dst, Frame.Xform, Src);
        return Dst;
    }

    // FUN_062dbb90: Out = transpose(M) * Src through the shared 3DMathEngine.
    Vector MathTransposeTransform(Matrix const& M, Vector const& Src)
    {
        Vector Out;
        MathTransposeTransformVia3DMathEngine(&Out, &M, &Src);
        return Out;
    }

    // FUN_062dba00 on a frame whose Dim holds the half extents.
    bool ClampToBoxSurface(FZoneFrame& Frame, Vector const& Src, Vector& Dst)
    {
        return ClampPointToBoxSurface(reinterpret_cast<FOrientedBox const*>(&Frame), &Src, &Dst);
    }

    // The frame every shape builds from the zone (its 12 dwords at +0x0c); retail's identity
    // initialisation before the copy does not survive.
    FZoneFrame ZoneFrame(Universe::IZone const& Zone)
    {
        FZoneFrame Frame;
        Frame.Xform.Rotation = Zone.Orientation;
        Frame.Xform.Translation = Zone.Pos;
        Frame.Dim[0] = 0.0f;
        Frame.Dim[1] = 0.0f;
        Frame.Dim[2] = 0.0f;
        return Frame;
    }

    void ZoneTypeNotSupported(int Line)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kSystemsSource, Line, kZoneTypeNotSupported);
    }

    // FUN_06339f20 (thiscall). Each column of the matrix copy is scaled by that axis's squared
    // radius (r*r kept on the x87 stack).
    void EllipsoidSetRadii(FZoneEllipsoid& Ellipsoid, Vector const& Radii)
    {
        Ellipsoid.Scaled = Ellipsoid.Orientation;
        Ellipsoid.Radius[0] = Radii.x;
        Ellipsoid.Radius[1] = Radii.y;
        Ellipsoid.Radius[2] = Radii.z;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            double const Square = (double)(Ellipsoid.Radius[Axis]) * Ellipsoid.Radius[Axis];
            for (int Row = 0; Row < 3; ++Row)
                Ellipsoid.Scaled.E[Row][Axis] =
                    (float)(Square * Ellipsoid.Scaled.E[Row][Axis]);
        }
    }

    // FUN_06339fb0 (thiscall): the smallest radius.
    float EllipsoidMinRadius(FZoneEllipsoid const& Ellipsoid)
    {
        float const R0 = Ellipsoid.Radius[0];
        float const R1 = Ellipsoid.Radius[1];
        float const R2 = Ellipsoid.Radius[2];
        if (R0 < R1)
        {
            if (R0 < R2)
                return R0;
            return R2;
        }
        if (R1 < R2)
            return R1;
        return R2;
    }

    // FUN_0633dc90 (thiscall): the ellipsoid's radius along a unit local direction. 1e-4 is the
    // double .rdata:0639c2b8.
    double EllipsoidRadiusAlong(FZoneEllipsoid const& Ellipsoid, Vector const& Dir)
    {
        double const A = (double)(Ellipsoid.Radius[0]) * Ellipsoid.Radius[0];
        double const B = (double)(Ellipsoid.Radius[1]) * Ellipsoid.Radius[1];
        double const C = (double)(Ellipsoid.Radius[2]) * Ellipsoid.Radius[2];
        double const X = Dir.x;
        double const Y = Dir.y;
        double const Z = Dir.z;
        double const Sum = ((((X * X) * C) * B) + (((Z * Z) * B) * A)) + (((Y * Y) * C) * A);
        double const Product = (A * (C * B)) * Sum;
        if (!(Product > 1.0e-4))
            return 0.0;
        return sqrt(Product) / Sum;
    }

    // The ring's surface scale along a unit tube direction W, inlined in slots +0x44 and +0x48.
    double RingSurfaceScale(Vector const& Size, Vector const& W)
    {
        double const HalfH = (double)Size.z * 0.5;
        double const AbsY = fabs((double)(W.y));
        double const Cutoff = HalfH / sqrt((double)Size.x * Size.x + HalfH * HalfH);
        if (AbsY > Cutoff)
            return HalfH / AbsY;
        return (((double)Size.x - Size.y) * 0.5) /
               sqrt((double)(W.z) * W.z + (double)(W.x) * W.x);
    }

    // The clip functions' shared tail (cylinder, ring, box): Start + (End - Start) * T. The x
    // product is stored as a float before the add, y stays on the stack, and z's difference is
    // stored as a float before the multiply.
    void WriteClipPoint(FZoneSegment const& Segment, float T, Vector& Out)
    {
        double const DeltaX = (double)(Segment.End.x) - Segment.Start.x;
        double const DeltaY = (double)(Segment.End.y) - Segment.Start.y;
        float const DeltaZ = (float)((double)(Segment.End.z) - Segment.Start.z);
        float const OffsetX = (float)(DeltaX * T);
        Out.x = OffsetX + Segment.Start.x;
        Out.y = (float)(DeltaY * T + Segment.Start.y);
        Out.z = (float)((double)(DeltaZ) * T + Segment.Start.z);
    }

    // FUN_06334ac0 (cdecl): where the segment enters the sphere.
    bool ClipSegmentSphere(FZoneSphere const& Sphere, FZoneSegment const& Segment, Vector& Out)
    {
        Vector const D = MakeVector(Segment.End.x - Segment.Start.x, Segment.End.y - Segment.Start.y,
                                    Segment.End.z - Segment.Start.z);
        Vector const F = MakeVector(Segment.Start.x - Sphere.Center.x, Segment.Start.y - Sphere.Center.y,
                                    Segment.Start.z - Sphere.Center.z);
        float const A = (float)(((double)(D.z) * D.z + (double)D.y * D.y) +
                                           (double)D.x * D.x);
        double const B = ((double)(D.z) * F.z + (double)D.y * F.y) +
                         (double)(F.x) * D.x;
        double const C0 = ((double)(F.z) * F.z + (double)(F.y) * F.y) +
                          (double)(F.x) * F.x;
        double const R = Sphere.Radius;
        float const Disc = (float)(B * B - (C0 - R * R) * A);

        if (!(Disc >= 0.0f))
            return false;

        if (!(Disc > 0.0f))
        {
            // Tangent. The 0 test uses the unrounded T, the 1 test the float; z's product is
            // stored as a float.
            double const T = -(B / A);
            float const TF = (float)T;
            if (!(T >= 0.0))
                return false;
            if (!(TF <= 1.0f))
                return false;
            float const OffsetZ = D.z * TF;
            Out.x = (float)((double)D.x * TF + Segment.Start.x);
            Out.y = (float)((double)D.y * TF + Segment.Start.y);
            Out.z = OffsetZ + Segment.Start.z;
            return true;
        }

        double const Root = sqrt((double)(Disc));
        double const InvA = 1.0 / A;
        float const T1 = (float)((-B - Root) * InvA);
        float const T2 = (float)((Root - B) * InvA);
        if (T1 > 1.0f)
            return false;
        if (T2 < 0.0f)
            return false;
        float const T = (T1 >= 0.0f) ? T1 : T2;
        float const OffsetY = D.y * T;
        float const OffsetZ = D.z * T;
        Out.x = (float)((double)D.x * T + Segment.Start.x);
        Out.y = OffsetY + Segment.Start.y;
        Out.z = OffsetZ + Segment.Start.z;
        return true;
    }

    // FUN_06334d10 (cdecl): the ellipsoid clip. Both ends go into the ellipsoid's local frame
    // (engine vt+0x38), are divided per axis by the radii, clipped against the unit sphere,
    // and a hit is scaled back, rotated (engine vt+0x2c) and moved to the ellipsoid.
    bool ClipSegmentEllipsoid(FZoneFrame const& Ellipsoid, FZoneSegment const& Segment, Vector& Out)
    {
        Vector const StartRel = Vec3Sub(Segment.Start, Ellipsoid.Xform.Translation);
        Vector const LocalStart = EngineTransposeTransform(Ellipsoid.Xform.Rotation, StartRel);
        Vector const EndRel = MakeVector(Segment.End.x - Ellipsoid.Xform.Translation.x, Segment.End.y - Ellipsoid.Xform.Translation.y,
                                         Segment.End.z - Ellipsoid.Xform.Translation.z);
        Vector const LocalEnd = EngineTransposeTransform(Ellipsoid.Xform.Rotation, EndRel);

        // The inverses stay on the stack; only the end's z uses the float-rounded inverse.
        double const InvX = 1.0 / Ellipsoid.Dim[0];
        double const InvY = 1.0 / Ellipsoid.Dim[1];
        double const InvZ = 1.0 / Ellipsoid.Dim[2];
        float const InvZF = (float)(InvZ);

        FZoneSegment Unit;
        Unit.Start = MakeVector((float)(LocalStart.x * InvX), (float)(LocalStart.y * InvY),
                                (float)(LocalStart.z * InvZ));
        Unit.End = MakeVector((float)(LocalEnd.x * InvX), (float)(InvY * LocalEnd.y),
                              (float)((double)(InvZF) * LocalEnd.z));

        FZoneSphere UnitSphere;
        UnitSphere.Center = MakeVector(0.0f, 0.0f, 0.0f);
        UnitSphere.Radius = 1.0f;

        bool const bHit = ClipSegmentSphere(UnitSphere, Unit, Out);
        if (bHit)
        {
            Out = MakeVector(Out.x * Ellipsoid.Dim[0], Ellipsoid.Dim[1] * Out.y, Out.z * Ellipsoid.Dim[2]);
            Out = EngineTransformMatrix(Ellipsoid.Xform.Rotation, Out);
            Out = MakeVector(Ellipsoid.Xform.Translation.x + Out.x, Ellipsoid.Xform.Translation.y + Out.y, Ellipsoid.Xform.Translation.z + Out.z);
        }
        return bHit;
    }

    // One side-wall pass of the cylinder and ring clips: the roots of the circle equation in
    // the local XZ plane, kept only when inside the height band. TBest is the best parameter
    // so far (2.0f, .rdata:063a0498, on entry to the first pass) and is also the upper limit
    // here. Dy is kept on the stack for the T1 height test and rounded to float for the
    // following T2 test; when T1 does not count, the T2 test recomputes it unrounded.
    void ClipSideWall(float A, double B, double Disc, Vector const& LS, Vector const& LE, float HalfH,
                      float& TBest)
    {
        if (Disc > 0.0)
        {
            double const Root = sqrt(Disc);
            double const TwoA = (double)(A) + A;
            float const T1 = (float)((-B - Root) / TwoA);
            float const T2 = (float)((Root - B) / TwoA);

            if (T1 >= 0.0f && T1 <= TBest)
            {
                double const Dy = (double)LE.y - LS.y;
                float const DyF = (float)(Dy);
                if (HalfH >= fabs(Dy * T1 + LS.y))
                {
                    if (T2 >= 0.0f && T2 <= T1 && HalfH >= fabs((double)(DyF) * T2 + LS.y))
                        TBest = T2;
                    else
                        TBest = T1;
                    return;
                }
            }

            if (T2 >= 0.0f && T2 <= TBest &&
                HalfH >= fabs(((double)LE.y - LS.y) * T2 + LS.y))
                TBest = T2;
            return;
        }

        if (Disc == 0.0)
        {
            double const T = -B / ((double)(A) + A);
            float const TF = (float)T;
            if (T >= 0.0 && TF <= TBest && HalfH >= fabs(((double)LE.y - LS.y) * TF + LS.y))
                TBest = TF;
        }
    }

    // A cap plane of the cylinder / ring at local height where Num = +-HalfH - LS.y. The > 0
    // test uses the unrounded T, the TBest test its float.
    bool CapCrossing(float Num, float DirY, Vector const& LS, float DxF, float DzF, float TBest, float& OutT,
                     double& OutRadialSq)
    {
        if (!((DirY > 0.0f && Num > 0.0f) || (DirY < 0.0f && Num < 0.0f)))
            return false;
        double const T = (double)(Num) / DirY;
        float const TF = (float)T;
        if (!(T > 0.0))
            return false;
        if (!(TF < TBest))
            return false;
        float const OffsetX = DxF * TF;
        double const X = (double)LS.x + OffsetX;
        double const Z = (double)LS.z + (double)(DzF) * TF;
        OutT = TF;
        OutRadialSq = Z * Z + X * X;
        return true;
    }

    // FUN_06334f30 (cdecl): the cylinder clip, Dim = {radius, height}.
    bool ClipSegmentCylinder(FZoneFrame const& Cylinder, FZoneSegment const& Segment, Vector& Out)
    {
        Vector const StartRel = MakeVector(Segment.Start.x - Cylinder.Xform.Translation.x, Segment.Start.y - Cylinder.Xform.Translation.y,
                                           Segment.Start.z - Cylinder.Xform.Translation.z);
        Vector const LS = EngineTransposeTransform(Cylinder.Xform.Rotation, StartRel);
        Vector const EndRel = MakeVector(Segment.End.x - Cylinder.Xform.Translation.x, Segment.End.y - Cylinder.Xform.Translation.y,
                                         Segment.End.z - Cylinder.Xform.Translation.z);
        Vector const LE = EngineTransposeTransform(Cylinder.Xform.Rotation, EndRel);

        float const HalfH = (float)((double)(Cylinder.Dim[1]) * 0.5);
        float TBest = 2.0f;
        double const Dx = (double)(LE.x) - LS.x;
        float const DxF = (float)(Dx);
        double const Dz = (double)(LE.z) - LS.z;
        float const DzF = (float)(Dz);
        float const A = (float)(Dz * Dz + Dx * Dx);
        double B = (double)LS.z * Dz + (double)LS.x * Dx;
        B = B + B;
        double const Radius = Cylinder.Dim[0];
        double const C0 = (double)LS.z * LS.z + (double)LS.x * LS.x;
        double const Disc = B * B - ((C0 - Radius * Radius) * A) * 4.0f;

        ClipSideWall(A, B, Disc, LS, LE, HalfH, TBest);

        // The two caps, with the segment direction in local space.
        Vector const Along = MakeVector(Segment.End.x - Segment.Start.x, Segment.End.y - Segment.Start.y,
                                        Segment.End.z - Segment.Start.z);
        Vector const LD = EngineTransposeTransform(Cylinder.Xform.Rotation, Along);
        double const RadiusSq = Radius * Radius;

        float CapT = 0.0f;
        double RadialSq = 0.0;
        float const Num1 = (float)(-(double)HalfH - LS.y);
        if (CapCrossing(Num1, LD.y, LS, DxF, DzF, TBest, CapT, RadialSq) && RadiusSq > RadialSq)
            TBest = CapT;
        float const Num2 = (float)((double)HalfH - LS.y);
        if (CapCrossing(Num2, LD.y, LS, DxF, DzF, TBest, CapT, RadialSq) && RadiusSq > RadialSq)
            TBest = CapT;

        if (!(TBest >= 0.0f))
            return false;
        if (!(TBest <= 1.0f))
            return false;
        WriteClipPoint(Segment, TBest, Out);
        return true;
    }

    // FUN_06335440 (cdecl): the ring clip, Dim = {outer radius, inner radius, height}.
    bool ClipSegmentRing(FZoneFrame const& Ring, FZoneSegment const& Segment, Vector& Out)
    {
        Vector const StartRel = MakeVector(Segment.Start.x - Ring.Xform.Translation.x, Segment.Start.y - Ring.Xform.Translation.y,
                                           Segment.Start.z - Ring.Xform.Translation.z);
        Vector const LS = EngineTransposeTransform(Ring.Xform.Rotation, StartRel);
        Vector const EndRel = MakeVector(Segment.End.x - Ring.Xform.Translation.x, Segment.End.y - Ring.Xform.Translation.y,
                                         Segment.End.z - Ring.Xform.Translation.z);
        Vector const LE = EngineTransposeTransform(Ring.Xform.Rotation, EndRel);

        // The outer wall, as in the cylinder.
        float const HalfH = (float)((double)(Ring.Dim[2]) * 0.5);
        float TBest = 2.0f;
        double const Dx = (double)(LE.x) - LS.x;
        float const DxF = (float)(Dx);
        double const Dz = (double)(LE.z) - LS.z;
        float const DzF = (float)(Dz);
        float const A = (float)(Dz * Dz + Dx * Dx);
        double B = (double)LS.z * Dz + (double)LS.x * Dx;
        B = B + B;
        double const C0 = (double)LS.z * LS.z + (double)LS.x * LS.x;
        double const Outer = Ring.Dim[0];
        double const OuterDisc = B * B - ((C0 - Outer * Outer) * A) * 4.0f;
        ClipSideWall(A, B, OuterDisc, LS, LE, HalfH, TBest);

        // The inner wall with the same B and C0, limited by TBest.
        double const Inner = Ring.Dim[1];
        double const InnerDisc = B * B - ((C0 - Inner * Inner) * A) * 4.0f;
        ClipSideWall(A, B, InnerDisc, LS, LE, HalfH, TBest);

        // The caps count between the two radii.
        Vector const Along = MakeVector(Segment.End.x - Segment.Start.x, Segment.End.y - Segment.Start.y,
                                        Segment.End.z - Segment.Start.z);
        Vector const LD = EngineTransposeTransform(Ring.Xform.Rotation, Along);
        double const OuterSq = Outer * Outer;
        double const InnerSq = Inner * Inner;

        float CapT = 0.0f;
        double RadialSq = 0.0;
        float const Num1 = (float)(-(double)HalfH - LS.y);
        if (CapCrossing(Num1, LD.y, LS, DxF, DzF, TBest, CapT, RadialSq) && RadialSq < OuterSq &&
            RadialSq > InnerSq)
            TBest = CapT;
        float const Num2 = (float)((double)HalfH - LS.y);
        if (CapCrossing(Num2, LD.y, LS, DxF, DzF, TBest, CapT, RadialSq) && RadialSq < OuterSq &&
            RadialSq > InnerSq)
            TBest = CapT;

        if (!(TBest >= 0.0f))
            return false;
        if (!(TBest <= 1.0f))
            return false;
        WriteClipPoint(Segment, TBest, Out);
        return true;
    }

    // The face point the box clip builds with three FUN_0628b030 calls: Q = LE - LS,
    // Q = Q * T, P = LS + Q, each a float store.
    Vector BoxFacePoint(Vector const& LS, Vector const& LE, float T)
    {
        Vector const Q = MakeVector(LE.x - LS.x, LE.y - LS.y, LE.z - LS.z);
        Vector const Scaled = MakeVector(Q.x * T, Q.y * T, Q.z * T);
        return MakeVector(LS.x + Scaled.x, LS.y + Scaled.y, LS.z + Scaled.z);
    }

    // FUN_06335b10 (cdecl): the box clip, Dim = the full size.
    bool ClipSegmentBox(FZoneFrame const& Box, FZoneSegment const& Segment, Vector& Out)
    {
        Vector const StartRel = MakeVector(Segment.Start.x - Box.Xform.Translation.x, Segment.Start.y - Box.Xform.Translation.y,
                                           Segment.Start.z - Box.Xform.Translation.z);
        Vector const LS = EngineTransposeTransform(Box.Xform.Rotation, StartRel);
        Vector const EndRel = MakeVector(Segment.End.x - Box.Xform.Translation.x, Segment.End.y - Box.Xform.Translation.y,
                                         Segment.End.z - Box.Xform.Translation.z);
        Vector const LE = EngineTransposeTransform(Box.Xform.Rotation, EndRel);

        // Retail quirk kept: a start inside the box returns the start in the box's LOCAL frame,
        // not the world. x compares the unrounded half extent.
        double const HalfX = (double)(Box.Dim[0]) * 0.5;
        float const HalfXF = (float)(HalfX);
        float const HalfYF = (float)((double)(Box.Dim[1]) * 0.5);
        float const HalfZF = (float)((double)(Box.Dim[2]) * 0.5);
        if (HalfX > fabs((double)LS.x) && HalfYF > fabs(LS.y) && HalfZF > fabs(LS.z))
        {
            Out = LS;
            return true;
        }

        Vector const Along = MakeVector(Segment.End.x - Segment.Start.x, Segment.End.y - Segment.Start.y,
                                        Segment.End.z - Segment.Start.z);
        Vector const LD = EngineTransposeTransform(Box.Xform.Rotation, Along);
        float TBest = 2.0f;

        // X faces. T is NOT compared with TBest.
        {
            double Num = 0.0;
            bool bFace = false;
            if (LD.x > 0.0f)
            {
                Num = -(double)(HalfXF) - LS.x;
                bFace = Num > 0.0;
            }
            else if (LD.x < 0.0f)
            {
                Num = (double)(HalfXF) - LS.x;
                bFace = Num < 0.0;
            }
            if (bFace)
            {
                float const TF = (float)(Num / LD.x);
                Vector const P = BoxFacePoint(LS, LE, TF);
                if (HalfYF > fabs(P.y) && HalfZF > fabs(P.z))
                    TBest = TF;
            }
        }

        // Y faces: as X, but the unrounded T must be below TBest.
        {
            double Num = 0.0;
            bool bFace = false;
            if (LD.y > 0.0f)
            {
                Num = -(double)(HalfYF) - LS.y;
                bFace = Num > 0.0;
            }
            else if (LD.y < 0.0f)
            {
                Num = (double)(HalfYF) - LS.y;
                bFace = Num < 0.0;
            }
            if (bFace)
            {
                double const T = Num / LD.y;
                float const TF = (float)T;
                if (T < TBest)
                {
                    Vector const P = BoxFacePoint(LS, LE, TF);
                    if (HalfXF > fabs(P.x) && HalfZF > fabs(P.z))
                        TBest = TF;
                }
            }
        }

        // Z faces. The +Z face adds the Q * T products on the stack to LS and stores the point;
        // the -Z face stores x and keeps y on the stack.
        if (LD.z > 0.0f)
        {
            double const Num = -(double)(HalfZF) - LS.z;
            if (Num > 0.0)
            {
                double const T = Num / LD.z;
                float const TF = (float)T;
                if (T < TBest)
                {
                    Vector const Q = MakeVector(LE.x - LS.x, LE.y - LS.y, LE.z - LS.z);
                    Vector const P = MakeVector((float)(LS.x + (double)(Q.x) * TF),
                                                (float)(LS.y + (double)(Q.y) * TF),
                                                (float)(LS.z + (double)(Q.z) * TF));
                    if (HalfXF > fabs(P.x) && HalfYF > fabs(P.y))
                        TBest = TF;
                }
            }
        }
        else if (LD.z < 0.0f)
        {
            double const Num = (double)(HalfZF) - LS.z;
            if (Num < 0.0)
            {
                double const T = Num / LD.z;
                float const TF = (float)T;
                if (T < TBest)
                {
                    Vector const Q = MakeVector(LE.x - LS.x, LE.y - LS.y, LE.z - LS.z);
                    float const PX = (float)(LS.x + (double)(Q.x) * TF);
                    double const PY = (double)(LS.y) + (double)(Q.y) * TF;
                    if (HalfXF > fabs(PX) && HalfYF > fabs(PY))
                        TBest = TF;
                }
            }
        }

        // Only the upper bound: there is no lower-bound test in the box clip.
        if (!(TBest <= 1.0f))
            return false;
        WriteClipPoint(Segment, TBest, Out);
        return true;
    }

    // ------------------------------------------------------------------------------------
    // Frustum / sphere culling: FrustumCull (0633c6d0), SphereCull (0633d770) and their
    // helpers 0633cb50 / 0633cc20 / 0633d040 / 0633d150 / 0633d1e0 / 0633d5d0 / 0633daa0 /
    // 0633dd10.
    // ------------------------------------------------------------------------------------

    float const kCullHalf = 0.5f;          // .rdata:0639c13c (float)
    float const kCullHalfPi = 1.5707964f;  // .rdata:0639f5d4 (float)
    float const kCullPi = 3.1415927f;      // .rdata:0639d208 (float)

    // .data:DAT_063edc78: the number of steps of FrustumCull's far-cap sweep, re-read on every
    // iteration.
    int GFrustumCapSteps = 5;

    // The plane FUN_0632f8b0 builds: unit normal, then the offset along it.
    struct FCullPlane
    {
        Vector Normal; // +0x00
        float D;       // +0x0c
    };

    // FUN_062e0480 / FUN_062e04a0 / FUN_062d3af0: column 0 / 1 / 2 of the frustum matrix.
    Vector FrustumColumn(Geometry::Frustum const& Frustum, int Column)
    {
        Matrix const& Rotation = Frustum.Xform.Rotation;
        return MakeVector(Rotation.E[0][Column], Rotation.E[1][Column], Rotation.E[2][Column]);
    }

    // FUN_062c5a70
    Vector VecNegate(Vector const& V)
    {
        return MakeVector(-V.x, -V.y, -V.z);
    }

    // FUN_0632f8b0 (thiscall). D is computed from the unrounded normalised components. No guard:
    // a zero normal gives inf * 0, as in retail.
    void CullPlaneSet(FCullPlane& Plane, Vector const& Point, Vector const& N)
    {
        Plane.Normal = N;
        double const Inverse = 1.0 / sqrt(((double)Plane.Normal.x * Plane.Normal.x +
                                           (double)(Plane.Normal.y) * Plane.Normal.y) +
                                          (double)(Plane.Normal.z) * Plane.Normal.z);
        double const NX = Inverse * Plane.Normal.x;
        Plane.Normal.x = (float)(NX);
        double const NY = Inverse * Plane.Normal.y;
        Plane.Normal.y = (float)(NY);
        double const NZ = Inverse * Plane.Normal.z;
        Plane.Normal.z = (float)(NZ);
        Plane.D = (float)((NZ * Point.z + NY * Point.y) + NX * Point.x);
    }

    // FUN_0633d150 (thiscall).
    void CullQuatFromAxisAngle(Quaternion& Quat, Vector const& Axis, float Angle)
    {
        double const Half = (double)(Angle) * kCullHalf;
        double const Length = sqrt(((double)(Axis.x) * Axis.x + (double)(Axis.y) * Axis.y) +
                                   (double)(Axis.z) * Axis.z);
        float const Scale = (float)(X87Sin(Half) / Length);
        Quat.w = X87CosF(Half);
        Quat.x = Scale * Axis.x;
        Quat.y = Scale * Axis.y;
        Quat.z = Scale * Axis.z;
    }

    // FUN_0633d040 (cdecl, Out / Quat / V): V rotated by Quat,
    //   Out = (2W^2 - 1) V + 2 (q . V) q - 2W (V x q)
    // with retail's float stores: (V x q).z, 2W, the x term of 2W (V x q), the y and z terms
    // of 2 (q . V) q, the three (2W^2 - 1) V products and the z sum are floats; the rest stays
    // on the stack.
    Vector CullQuatRotate(Quaternion const& Quat, Vector const& V)
    {
        double const CrossX = (double)Quat.z * V.y - (double)V.z * Quat.y;
        double const CrossY = (double)V.z * Quat.x - (double)Quat.z * V.x;
        float const CrossZ = (float)((double)V.x * Quat.y - (double)Quat.x * V.y);
        float const TwoW = (float)((double)Quat.w + Quat.w);
        float const SpinX = (float)(CrossX * TwoW);
        double const SpinY = CrossY * TwoW;
        double const SpinZ = (double)(CrossZ) * TwoW;

        double const TwoDot = (((double)Quat.z * V.z + (double)Quat.y * V.y) +
                               (double)Quat.x * V.x) * 2.0;
        double const AlongX = TwoDot * Quat.x;
        float const AlongY = (float)(TwoDot * Quat.y);
        float const AlongZ = (float)(TwoDot * Quat.z);

        double const Cos2 = ((double)Quat.w * Quat.w) * 2.0 - 1.0;
        float const KeepX = (float)(Cos2 * V.x);
        float const KeepY = (float)(Cos2 * V.y);
        float const KeepZ = (float)(Cos2 * V.z);

        double const SumX = (double)(KeepX) + AlongX;
        double const SumY = (double)(KeepY) + AlongY;
        float const SumZ = (float)((double)(KeepZ) + AlongZ);

        return MakeVector((float)(SumX - SpinX), (float)(SumY - SpinY),
                          (float)((double)(SumZ) - SpinZ));
    }

    // FUN_0633d5d0 (cdecl): FUN_0633d150 and FUN_0633d040 in one body but with its own float
    // stores -- each term vector is rounded to float, and the dot is summed x, y, z
    // (FUN_0633d040 sums z, y, x).
    Vector CullRotateAboutAxis(Vector const& V, Vector const& Axis, float Angle)
    {
        Quaternion Quat;
        CullQuatFromAxisAngle(Quat, Axis, Angle);

        double const CrossX = (double)Quat.z * V.y - (double)Quat.y * V.z;
        double const CrossY = (double)Quat.x * V.z - (double)Quat.z * V.x;
        double const CrossZ = (double)Quat.y * V.x - (double)Quat.x * V.y;
        float const TwoW = (float)((double)Quat.w + Quat.w);
        Vector const Spin = MakeVector((float)(CrossX * TwoW), (float)(CrossY * TwoW),
                                       (float)(CrossZ * TwoW));

        double const TwoDot = (((double)Quat.x * V.x + (double)Quat.y * V.y) +
                               (double)Quat.z * V.z) * 2.0;
        Vector const Along = MakeVector((float)(TwoDot * Quat.x), (float)(Quat.y * TwoDot),
                                        (float)(Quat.z * TwoDot));

        double const Cos2 = ((double)Quat.w * Quat.w) * 2.0 - 1.0;
        Vector const Keep = MakeVector((float)(Cos2 * V.x), (float)(Cos2 * V.y),
                                       (float)(Cos2 * V.z));

        double const SumX = (double)(Along.x) + Keep.x;
        double const SumY = (double)(Along.y) + Keep.y;
        float const SumZ = (float)((double)(Along.z) + Keep.z);

        return MakeVector((float)(SumX - Spin.x), (float)(SumY - Spin.y),
                          (float)((double)(SumZ) - Spin.z));
    }

    // FUN_0633cc20 (thiscall on the frustum): side Index of the frustum as a plane. 0 near,
    // 1 far, 2 / 3 the sides turned about column 1 by AngleX, 4 / 5 about column 0 by AngleY;
    // above 5 the point and normal are zero.
    FCullPlane FrustumPlane(Geometry::Frustum const& Frustum, unsigned Index)
    {
        Vector const& Apex = Frustum.Xform.Translation;
        Vector Point;
        Vector Normal;
        switch (Index)
        {
        case 0:
            Point = Apex;
            Normal = VecNegate(FrustumColumn(Frustum, 2));
            break;
        case 1:
        {
            Vector const Forward = FrustumColumn(Frustum, 2);
            Point = Vec3Sub(Apex, Vec3Scale(Forward, Frustum.Distance));
            Normal = FrustumColumn(Frustum, 2);
            break;
        }
        case 2:
        case 3:
        case 4:
        case 5:
        {
            Point = Apex;
            bool const bHorizontal = (Index == 2 || Index == 3);
            Vector const Axis = FrustumColumn(Frustum, bHorizontal ? 1 : 0);
            Vector const Back = VecNegate(FrustumColumn(Frustum, 2));
            double const HalfAngle = (double)(bHorizontal ? Frustum.AngleX : Frustum.AngleY) * kCullHalf;
            float const Angle = (Index == 2 || Index == 4)
                ? (float)(HalfAngle - kCullHalfPi)
                : (float)(kCullHalfPi - HalfAngle);
            Quaternion Quat;
            CullQuatFromAxisAngle(Quat, Axis, Angle);
            Normal = CullQuatRotate(Quat, Back);
            break;
        }
        default:
            Point = MakeVector(0.0f, 0.0f, 0.0f);
            Normal = Point;
            break;
        }

        FCullPlane Plane;
        CullPlaneSet(Plane, Point, Normal);
        return Plane;
    }

    // The result of EllipsoidPlaneSide, and the side a sphere is classified on in
    // FrustumCullsSphere.
    enum EPlaneSide
    {
        kPlaneSideStraddle = 0,
        kPlaneSideFront = 1,
        kPlaneSideBehind = 2,
    };
    // FUN_0633cb50 (thiscall on the ellipsoid): which side of Plane the ellipsoid lies -- 0 when
    // it straddles (or unordered), 2 behind, 1 in front. The transpose is built with
    // FUN_06284f70.
    int EllipsoidPlaneSide(FZoneEllipsoid const& Ellipsoid, FCullPlane const& Plane)
    {
        Matrix Transposed;
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Column = 0; Column < 3; ++Column)
                Transposed.E[Row][Column] = Ellipsoid.Scaled.E[Column][Row];
        }
        Vector const Spread = EngineTransformMatrix(Transposed, Plane.Normal);

        double const Dist = (((double)(Ellipsoid.Pos.y) * Plane.Normal.y +
                              (double)(Ellipsoid.Pos.z) * Plane.Normal.z) +
                             (double)Plane.Normal.x * Ellipsoid.Pos.x) - Plane.D;
        double const Extent = ((double)(Spread.z) * Plane.Normal.z +
                               (double)(Spread.x) * Plane.Normal.x) +
                              (double)(Spread.y) * Plane.Normal.y;
        if (!(Extent <= Dist * Dist))
            return kPlaneSideStraddle;
        if (Dist < 0.0f)
            return kPlaneSideBehind;
        return kPlaneSideFront;
    }

    // FUN_0633d1e0 (cdecl): true when the sphere lies wholly on the outer side of one of the six
    // sides. The sides are built as FUN_0633cc20 builds them, except that cases 2 / 3 / 5 turn
    // the normal with FUN_0633d5d0 (case 4 keeps FUN_0633d150 + FUN_0633d040). Retail keeps the
    // six results in a local array nobody reads.
    bool FrustumCullsSphere(Geometry::Frustum const& Frustum, FZoneSphere const& Sphere)
    {
        Vector const& Apex = Frustum.Xform.Translation;
        for (unsigned Index = 0; Index < 6; ++Index)
        {
            Vector Point;
            Vector Normal;
            switch (Index)
            {
            case 0:
                Point = Apex;
                Normal = VecNegate(FrustumColumn(Frustum, 2));
                break;
            case 1:
                Point = Vec3Sub(Apex, Vec3Scale(FrustumColumn(Frustum, 2), Frustum.Distance));
                Normal = FrustumColumn(Frustum, 2);
                break;
            case 2:
                Point = Apex;
                Normal = CullRotateAboutAxis(VecNegate(FrustumColumn(Frustum, 2)), FrustumColumn(Frustum, 1),
                                             (float)((double)Frustum.AngleX * kCullHalf - kCullHalfPi));
                break;
            case 3:
                Point = Apex;
                Normal = CullRotateAboutAxis(VecNegate(FrustumColumn(Frustum, 2)), FrustumColumn(Frustum, 1),
                                             (float)(kCullHalfPi - (double)Frustum.AngleX * kCullHalf));
                break;
            case 4:
            {
                Point = Apex;
                Quaternion Quat;
                CullQuatFromAxisAngle(Quat, FrustumColumn(Frustum, 0),
                                      (float)((double)Frustum.AngleY * kCullHalf - kCullHalfPi));
                Normal = CullQuatRotate(Quat, VecNegate(FrustumColumn(Frustum, 2)));
                break;
            }
            case 5:
                Point = Apex;
                Normal = CullRotateAboutAxis(VecNegate(FrustumColumn(Frustum, 2)), FrustumColumn(Frustum, 0),
                                             (float)(kCullHalfPi - (double)Frustum.AngleY * kCullHalf));
                break;
            default:
                Point = MakeVector(0.0f, 0.0f, 0.0f);
                Normal = Point;
                break;
            }

            FCullPlane Plane;
            CullPlaneSet(Plane, Point, Normal);

            double const Dist = (((double)Plane.Normal.x * Sphere.Center.x +
                                  (double)(Plane.Normal.z) * Sphere.Center.z) +
                                 (double)(Plane.Normal.y) * Sphere.Center.y) - Plane.D;
            double const Radius = Sphere.Radius;
            int Side = kPlaneSideStraddle;
            if (Dist > Radius)
                Side = kPlaneSideFront;
            else if (Dist < -Radius)
                Side = kPlaneSideBehind;
            if (Side == kPlaneSideBehind)
                return true;
        }
        return false;
    }

    // FUN_0633daa0 (thiscall on a frame whose Dim holds half extents): signed squared distance
    // from Point to the box surface, negative inside. Inside on all three axes, the axis with
    // the largest excess is snapped to the surface. The z coordinate stays on the stack.
    double BoxSignedDistanceSq(FZoneFrame const& Frame, Vector const& Point)
    {
        Vector const Delta = MakeVector(Point.x - Frame.Xform.Translation.x, Point.y - Frame.Xform.Translation.y, Point.z - Frame.Xform.Translation.z);
        Vector const Local = EngineTransposeTransform(Frame.Xform.Rotation, Delta);

        bool bOutside = false;
        float ClosestX = Local.x;
        float ClosestY = Local.y;
        double ClosestZ = Local.z;

        double const RawX = fabs((double)Local.x) - Frame.Dim[0];
        float const ExcessX = (float)(RawX);
        if (RawX >= 0.0)
        {
            ClosestX = (float)(copysign((double)(Frame.Dim[0]), (double)Local.x));
            bOutside = true;
        }

        double const RawY = fabs((double)Local.y) - Frame.Dim[1];
        float const ExcessY = (float)(RawY);
        if (RawY >= 0.0)
        {
            ClosestY = (float)(copysign((double)(Frame.Dim[1]), (double)Local.y));
            bOutside = true;
        }

        double const RawZ = fabs((double)Local.z) - Frame.Dim[2];
        float const ExcessZ = (float)(RawZ);
        if (RawZ >= 0.0)
        {
            ClosestZ = copysign((double)Frame.Dim[2], (double)Local.z);
            bOutside = true;
        }
        else if (!bOutside)
        {
            if (ExcessX >= ExcessY)
            {
                if (ExcessX >= ExcessZ)
                    ClosestX = (float)(copysign((double)(Frame.Dim[0]), (double)Local.x));
                else
                    ClosestZ = copysign((double)Frame.Dim[2], (double)Local.z);
            }
            else
            {
                if (ExcessY >= ExcessZ)
                    ClosestY = (float)(copysign((double)(Frame.Dim[1]), (double)Local.y));
                else
                    ClosestZ = copysign((double)Frame.Dim[2], (double)Local.z);
            }
        }

        float const DX = (float)((double)Local.x - ClosestX);
        float const DY = (float)((double)Local.y - ClosestY);
        double const DZ = (double)Local.z - ClosestZ;
        double Value = ((DZ * DZ) + (double)(DY) * DY) + (double)(DX) * DX;
        if (!bOutside)
            Value = -Value;
        return Value;
    }

    // FUN_0633dd10 (thiscall on the ellipsoid): Point's distance outside the surface. dy is
    // rounded to float, dx / dz are not. Within 1e-4 of the centre the result is -(smallest
    // radius).
    double EllipsoidSignedDistance(FZoneEllipsoid const& Ellipsoid, Vector const& Point)
    {
        double const DX = (double)(Point.x) - Ellipsoid.Pos.x;
        float const DY = (float)((double)(Point.y) - Ellipsoid.Pos.y);
        double const DZ = (double)(Point.z) - Ellipsoid.Pos.z;
        double const Length = sqrt((DX * DX + DZ * DZ) + (double)(DY) * DY);
        float const LengthF = (float)Length;
        if (Length < 1.0e-4)
            return -EllipsoidMinRadius(Ellipsoid);

        double const Inverse = 1.0 / LengthF;
        Vector const Dir = MakeVector((float)(DX * Inverse), (float)(DY * Inverse),
                                      (float)(DZ * Inverse));
        Vector const Local = EngineTransposeTransform(Ellipsoid.Orientation, Dir);
        return LengthF - EllipsoidRadiusAlong(Ellipsoid, Local);
    }

    // The ellipsoid FrustumCull and SphereCull build from a zone.
    FZoneEllipsoid CullEllipsoid(Universe::IZone const& Zone)
    {
        FZoneEllipsoid Ellipsoid;
        Vector const Size = Zone.GetSize();
        Transform const& Xform = Zone.GetTransform();
        Ellipsoid.Orientation = Xform.Rotation;
        Ellipsoid.Pos = Xform.Translation;
        EllipsoidSetRadii(Ellipsoid, Size);
        return Ellipsoid;
    }

    // One step of FrustumCull's far-cap sweep: true when the ellipsoid lies in front of the
    // step's plane.
    bool CapStepCulls(FZoneEllipsoid const& Ellipsoid, Vector const& Center, float Sweep, int Step, bool bHorizontal)
    {
        double const Angle = ((double)(Step) * Sweep) / GFrustumCapSteps;
        // One rounding each (negation is exact).
        float const Sin = X87SinF(Angle);
        float const NegCos = -X87CosF(Angle);
        Vector const Dir = bHorizontal ? MakeVector(Sin, 0.0f, NegCos) : MakeVector(0.0f, Sin, NegCos);
        FCullPlane Plane;
        CullPlaneSet(Plane, Center, Dir);
        return EllipsoidPlaneSide(Ellipsoid, Plane) == kPlaneSideFront;
    }
}

// Inlined in FUN_06337250 (the stack ZONE, .text:063375cd).
ZONE::ZONE()
{
    NicknameId = 0;
    SystemId = 0;
    Orientation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    Pos = MakeVector(0.0f, 0.0f, 0.0f);
    Shape = 0;
    PropertyFlags = 0;
    Size = MakeVector(0.0f, 0.0f, 0.0f);
    MeshHandle = -1;
    EdgeFraction = 0.0f;
    PhysicalEffect = -1;
    DragModifier = 1.0f;
    Damage = 0.0f;
    Interference = 0.0f;
    CachedRadius = -1.0f;
    MusicId = 0;
    reinterpret_cast<CacheString*>(&Spacedust)->clear(); // +0x70
    SpacedustMaxParticles = 0;
    FogColor = MakeVector(1.0f, 1.0f, 1.0f);
    OwnedLootEntry = nullptr;
    bPathLabel = false;
    Visit = 0x20;
    IdsName = 0;
    IdsInfo = 0;
    // (Retail also calls CreateID(0) and drops the result: no effect.)
}

// FUN_0633e3f0: memberwise, the mesh handle and loot pointer included -- so every copy's
// destructor releases them again, as in retail (see InsertZone).
ZONE::ZONE(ZONE const& Other) = default;

// FUN_06339010.
ZONE::~ZONE()
{
    if (MeshHandle != -1)
    {
        DALib::Engine->ReleaseHandle(MeshHandle);
        MeshHandle = -1;
    }
    // FUN_06391d5a is free().
    if (OwnedLootEntry != nullptr)
        free(OwnedLootEntry);
}

// LAB_0633ac30.
double ZONE::GetImmersion(Vector const& Point) const
{
    // Outside the bounding sphere the answer is 0.
    float const Dx = Point.x - Pos.x;
    float const Dy = Point.y - Pos.y;
    float const Dz = Point.z - Pos.z;
    double const Radius = GetRadius();
    double const DistSq = ((double)Dx * Dx + (double)(Dz) * Dz) +
                          (double)(Dy) * Dy;
    if (Radius * Radius < DistSq)
        return 0.0;

    // The 1.0 test uses the unrounded value, the ramp its float.
    double const Value = ClassifyPoint(Point);
    float const ValueF = (float)(Value);
    if (Value >= 1.0)
        return 0.0;

    if ((1.0 - EdgeFraction) > ValueF)
        return 1.0;
    if (EdgeFraction < 1.0e-4)
        return 1.0;
    return (1.0 - ValueF) / EdgeFraction; // left on the stack
}

// 062d1ad0 (ICF) [+0x4]
unsigned ZONE::GetId() const
{
    return NicknameId;
}

// 062a66a0 (ICF) [+0x8]
unsigned ZONE::GetSystem() const
{
    return SystemId;
}

// 062d1ae0 (ICF, also slot +0x14) [+0xc]: the orientation and position as one transform.
Transform const& ZONE::GetTransform() const
{
    return *reinterpret_cast<Transform const*>(&Orientation);
}

// 063390a0 [+0x30]
Vector const& ZONE::GetPos() const
{
    return Pos;
}

// 062d1ae0 (ICF with slot +0x0c)
Matrix const& ZONE::GetOrientation() const
{
    return Orientation;
}

// 0626c6b0 (ICF) [+0x3c]
unsigned ZONE::GetShape() const
{
    return Shape;
}

// 062a6690 (ICF) [+0x40]
unsigned long ZONE::GetProperties() const
{
    return PropertyFlags;
}

// 063390b0
bool ZONE::Slot20() const
{
    return (PropertyFlags & 0x18000) != 0;
}

// 063390c0 [+0x78]
Vector const& ZONE::GetFogColor() const
{
    return FogColor;
}

// 063390d0 [+0x44]
Vector ZONE::GetSize() const
{
    return Size;
}

// 063390f0 (ICF) [+0x50]
long ZONE::GetMeshHandle() const
{
    return MeshHandle;
}

// LAB_06339100.
float ZONE::GetRadius() const
{
    // The cached value (+0x68) is returned unless it is below 0.0f (a NaN is returned too).
    if (!(CachedRadius < 0.0f))
        return CachedRadius;

    // The sums and square roots stay on the stack and are rounded once when stored to +0x68.
    double const X = Size.x;
    double const Y = Size.y;
    double const Z = Size.z;
    switch (Shape)
    {
    case Universe::ZoneShapeSphere:
        CachedRadius = Size.x;
        break;

    case Universe::ZoneShapeEllipsoid:
    {
        float Largest = (Size.y < Size.x) ? Size.x : Size.y;
        if (Largest < Size.z)
            Largest = Size.z;
        CachedRadius = Largest;
        break;
    }

    case Universe::ZoneShapeBox:
        // 0.5 is the double .rdata:0639df88.
        CachedRadius = (float)(sqrt((X * X + Y * Y) + Z * Z) * 0.5);
        break;

    case Universe::ZoneShapeCylinder:
        // 0.25 is the double .rdata:063a0618.
        CachedRadius = (float)(sqrt(X * X + (Y * Y) * 0.25));
        break;

    case Universe::ZoneShapeRing:
        CachedRadius = (float)(sqrt(X * X + (Z * Z) * 0.25));
        break;

    default:
        // Retail caches a stack slot zeroed before the switch.
        ZoneTypeNotSupported(0x35c);
        CachedRadius = 0.0f;
        break;
    }
    return CachedRadius;
}

// LAB_06339220.
bool ZONE::InZone(Vector const& Point, float Margin) const
{
    // z's difference is multiplied unrounded by its own float rounding. 1e-4f is
    // .rdata:063a4ab4.
    float const Dx = Point.x - Pos.x;
    float const Dy = Point.y - Pos.y;
    double const DzExact = (double)(Point.z) - Pos.z;
    float const Dz = (float)(DzExact);
    double const DistSq = (DzExact * Dz + (double)Dx * Dx) + (double)(Dy) * Dy;
    float const DistSqF = (float)(DistSq);
    if (DistSq < 1.0e-4f)
        return true;

    // The bounding-sphere test uses the float-rounded distance.
    double const Reach = (double)(GetRadius()) + Margin;
    if (!(DistSqF < Reach * Reach))
        return false;

    switch (Shape)
    {
    case Universe::ZoneShapeSphere:
        return true;

    case Universe::ZoneShapeEllipsoid:
    {
        float const Length = (float)(sqrt((double)(DistSqF)));
        double const Inv = 1.0 / Length;
        Vector const Dir = MakeVector((float)(Dx * Inv), (float)(Dy * Inv),
                                      (float)(Dz * Inv));
        if (!((GetRadiusAlong(Dir) + Margin) >= Length))
            return false;
        return true;
    }

    case Universe::ZoneShapeBox:
    {
        // Signed squared distance to the surface (negative inside) against Margin^2.
        FZoneFrame Frame = ZoneFrame(*this);
        Frame.Dim[0] = Size.x * 0.5f;
        Frame.Dim[1] = Size.y * 0.5f;
        Frame.Dim[2] = Size.z * 0.5f;
        Vector const Delta = Vec3Sub(Point, Frame.Xform.Translation);
        Vector const Local = MathTransposeTransform(Frame.Xform.Rotation, Delta);
        Vector Clamped;
        bool const bOutside = ClampToBoxSurface(Frame, Local, Clamped);
        Vector const Diff = MakeVector(Local.x - Clamped.x, Local.y - Clamped.y, Local.z - Clamped.z);
        double Value = ((double)Diff.x * Diff.x + (double)Diff.y * Diff.y) +
                       (double)Diff.z * Diff.z;
        if (!bOutside)
            Value = -Value;
        if (!(Value < (double)Margin * Margin))
            return false;
        return true;
    }

    case Universe::ZoneShapeCylinder:
    {
        Vector const Delta = MakeVector(Point.x - Pos.x, Point.y - Pos.y, Point.z - Pos.z);
        Vector const Local = EngineTransposeTransform(Orientation, Delta);
        if (!((double)Size.y * 0.5 >= fabs((double)Local.y) - Margin))
            return false;
        double const RadialSq = (double)Local.x * Local.x + (double)Local.z * Local.z;
        double const Reach = ((double)Margin + Margin + Size.x) * Size.x +
                             (double)Margin * Margin;
        if (!(RadialSq - Reach < 0.0))
            return false;
        return true;
    }

    case Universe::ZoneShapeRing:
    {
        Vector const Delta = MakeVector(Point.x - Pos.x, Point.y - Pos.y, Point.z - Pos.z);
        Vector const Local = EngineTransposeTransform(Orientation, Delta);
        float const Inner = (float)(sqrt((double)Size.y * Size.y +
                                                    ((double)Size.z * Size.z) * 0.25));
        if (!((double)Size.z * 0.5 >= fabs((double)Local.y) - Margin))
            return false;
        double const RadialSq = (double)Local.x * Local.x + (double)Local.z * Local.z;
        double const OuterReach = ((double)Margin + Margin + Size.x) * Size.x +
                                  (double)Margin * Margin;
        if (!(RadialSq - OuterReach <= 0.0))
            return false;
        double const InnerReach = ((double)Inner + Inner + Size.y) * Size.y +
                                  (double)Inner * Inner;
        if (!(RadialSq - InnerReach >= 0.0))
            return false;
        return true;
    }

    case Universe::ZoneShapeMesh:
        // Both outcomes of retail's mesh-handle test return false.
        return false;

    default:
        ZoneTypeNotSupported(0x3bc);
        return false;
    }
}

// LAB_06339670: slot +0x34 in the zone's XZ plane -- the y difference is 0.0f throughout.
bool ZONE::InZoneXZ(Vector const& Point, float Margin) const
{
    float const Dx = Point.x - Pos.x;
    float const Dy = 0.0f;
    float const Dz = Point.z - Pos.z;
    double const DistSq = (double)Dx * Dx + (double)(Dz) * Dz;
    float const DistSqF = (float)(DistSq);
    if (DistSq < 1.0e-4f)
        return true;

    double const Reach = (double)(GetRadius()) + Margin;
    if (!(DistSqF < Reach * Reach))
        return false;

    switch (Shape)
    {
    case Universe::ZoneShapeSphere:
        return true;

    case Universe::ZoneShapeEllipsoid:
    {
        float const Length = (float)(sqrt((double)(DistSqF)));
        double const Inv = 1.0 / Length;
        Vector const Dir = MakeVector((float)(Dx * Inv), (float)(Dy * Inv),
                                      (float)(Inv * Dz));
        if (!((GetRadiusAlong(Dir) + Margin) >= Length))
            return false;
        return true;
    }

    case Universe::ZoneShapeBox:
    {
        // As slot +0x34, but the frame's position has y = 0 and the squared distance sums
        // (dy^2 + dz^2) + dx^2.
        FZoneFrame Frame = ZoneFrame(*this);
        Frame.Xform.Translation.y = 0.0f;
        Frame.Dim[0] = Size.x * 0.5f;
        Frame.Dim[1] = Size.y * 0.5f;
        Frame.Dim[2] = Size.z * 0.5f;
        Vector const Delta = Vec3Sub(Point, Frame.Xform.Translation);
        Vector const Local = MathTransposeTransform(Frame.Xform.Rotation, Delta);
        Vector Clamped;
        bool const bOutside = ClampToBoxSurface(Frame, Local, Clamped);
        Vector const Diff = MakeVector(Local.x - Clamped.x, Local.y - Clamped.y, Local.z - Clamped.z);
        double Value = ((double)Diff.y * Diff.y + (double)Diff.z * Diff.z) +
                       (double)Diff.x * Diff.x;
        if (!bOutside)
            Value = -Value;
        if (!(Value < (double)Margin * Margin))
            return false;
        return true;
    }

    case Universe::ZoneShapeCylinder:
    {
        // The height test is |0.0| - Margin: the height never excludes the point.
        Vector const Delta = MakeVector(Point.x - Pos.x, Point.y - Pos.y, Point.z - Pos.z);
        Vector const Local = EngineTransposeTransform(Orientation, Delta);
        if (!((double)Size.y * 0.5 >= fabs(0.0) - Margin))
            return false;
        double const RadialSq = (double)Local.x * Local.x + (double)Local.z * Local.z;
        double const Reach = ((double)Margin + Margin + Size.x) * Size.x +
                             (double)Margin * Margin;
        if (!(RadialSq - Reach < 0.0))
            return false;
        return true;
    }

    case Universe::ZoneShapeRing:
    {
        Vector const Delta = MakeVector(Point.x - Pos.x, Point.y - Pos.y, Point.z - Pos.z);
        Vector const Local = EngineTransposeTransform(Orientation, Delta);
        float const Inner = (float)(sqrt((double)Size.y * Size.y +
                                                    ((double)Size.z * Size.z) * 0.25));
        if (!((double)Size.z * 0.5 >= fabs(0.0) - Margin))
            return false;
        double const RadialSq = (double)Local.x * Local.x + (double)Local.z * Local.z;
        double const OuterReach = ((double)Margin + Margin + Size.x) * Size.x +
                                  (double)Margin * Margin;
        if (!(RadialSq - OuterReach <= 0.0))
            return false;
        double const InnerReach = ((double)Inner + Inner + Size.y) * Size.y +
                                  (double)Inner * Inner;
        if (!(RadialSq - InnerReach >= 0.0))
            return false;
        return true;
    }

    case Universe::ZoneShapeMesh:
        return false;

    default:
        ZoneTypeNotSupported(0x421);
        return false;
    }
}

// LAB_06339b00. Ring, mesh and invalid shapes take the general path.
double ZONE::GetDistance(Vector const& Point) const
{
    switch (Shape)
    {
    case Universe::ZoneShapeSphere:
    {
        Vector const Delta = Vec3Sub(Point, Pos);
        return sqrt(((double)Delta.z * Delta.z + (double)Delta.y * Delta.y) +
                    (double)Delta.x * Delta.x) -
               Size.x;
    }

    case Universe::ZoneShapeCylinder:
    case Universe::ZoneShapeBox:
    {
        // The cylinder is measured as a box of half extents {Size.x, Size.y * 0.5, Size.x}.
        // Retail runs the clamp twice: the first pass feeds TransformXform, whose result is
        // overwritten unread.
        FZoneFrame Frame = ZoneFrame(*this);
        if (Shape == Universe::ZoneShapeCylinder)
        {
            Frame.Dim[0] = Size.x;
            Frame.Dim[1] = (float)((double)Size.y * 0.5);
            Frame.Dim[2] = Size.x;
        }
        else
        {
            Vector const Half = Vec3Scale(Size, 0.5f);
            Frame.Dim[0] = Half.x;
            Frame.Dim[1] = Half.y;
            Frame.Dim[2] = Half.z;
        }

        Vector const Delta = Vec3Sub(Point, Frame.Xform.Translation);
        Vector const Local = MathTransposeTransform(Frame.Xform.Rotation, Delta);
        Vector Clamped;
        ClampToBoxSurface(Frame, Local, Clamped);
        EngineTransformXform(Frame, Clamped);

        Vector const Delta2 = Vec3Sub(Point, Frame.Xform.Translation);
        Vector const Local2 = MathTransposeTransform(Frame.Xform.Rotation, Delta2);
        Vector Clamped2;
        bool const bOutside = ClampToBoxSurface(Frame, Local2, Clamped2);
        Vector const Diff = Vec3Sub(Local2, Clamped2);

        double Value = VecLengthSq(Diff);
        if (!bOutside)
            Value = -Value;
        if (Value < 0.0)
            return -sqrt(-Value);
        return sqrt(Value);
    }

    case Universe::ZoneShapeEllipsoid:
    {
        FZoneEllipsoid Ellipsoid;
        Ellipsoid.Orientation = Orientation;
        Ellipsoid.Pos = Pos;
        EllipsoidSetRadii(Ellipsoid, Size);
        Vector Delta = Vec3Sub(Point, Ellipsoid.Pos);
        double const Length = VecLength(Delta);
        float const LengthF = (float)Length;
        if (Length < 1.0e-4)
            return -EllipsoidMinRadius(Ellipsoid);
        VecScaleInPlace(Delta, (float)(1.0 / LengthF));
        Vector const Local = MathTransposeTransform(Ellipsoid.Orientation, Delta);
        return LengthF - EllipsoidRadiusAlong(Ellipsoid, Local); // left on the stack
    }

    default:
    {
        // The length of vt+0x44's surface point relative to Pos, negative when vt+0x34 says
        // the point is inside. Retail: measured from the zone centre, not from Point.
        Vector const Surface = GetSurfacePoint(Point);
        Vector const Delta = Vec3Sub(Surface, Pos);
        float const Length = (float)(sqrt(((double)(Delta.z) * Delta.z +
                                                      (double)(Delta.y) * Delta.y) +
                                                     (double)(Delta.x) * Delta.x));
        if (InZone(Point, 0.0f))
            return -Length;
        return Length;
    }
    }
}

// LAB_06339ff0. The radius along a world direction, left on the x87 stack (callers inside ZONE
// consume it in-register through vt+0x40).
double ZONE::GetRadiusAlong(Vector const& Direction) const
{
    switch (Shape)
    {
    case Universe::ZoneShapeSphere:
        return Size.x;

    case Universe::ZoneShapeEllipsoid:
    {
        FZoneEllipsoid Ellipsoid;
        Ellipsoid.Orientation = Orientation;
        Ellipsoid.Pos = Pos;
        EllipsoidSetRadii(Ellipsoid, Size);
        Vector const Local = EngineTransposeTransform(Orientation, Direction);
        return EllipsoidRadiusAlong(Ellipsoid, Local);
    }

    case Universe::ZoneShapeBox:
    {
        // x and z are stored as floats, y stays on the stack.
        Vector const Local = EngineTransposeTransform(Orientation, Direction);
        double const AbsX = fabs((double)Local.x);
        float const AlongX = (AbsX > 1.0e-4f) ? (float)(Size.x / AbsX) : 1.0e10f;
        double const AbsY = fabs((double)Local.y);
        double const AlongY = (AbsY > 1.0e-4f) ? (Size.y / AbsY) : (double)(1.0e10f);
        double const AbsZ = fabs((double)Local.z);
        float const AlongZ = (AbsZ > 1.0e-4f) ? (float)(Size.z / AbsZ) : 1.0e10f;
        double Smallest = (AlongX < AlongY) ? (double)(AlongX) : AlongY;
        if (!(Smallest < AlongZ))
            Smallest = AlongZ;
        return Smallest * 0.5;
    }

    case Universe::ZoneShapeCylinder:
    case Universe::ZoneShapeRing:
    {
        Vector const Local = EngineTransposeTransform(Orientation, Direction);
        double const HalfH = (double)((Shape == Universe::ZoneShapeCylinder) ? Size.y : Size.z) * 0.5;
        double const AbsY = fabs((double)Local.y);
        double const Cutoff = HalfH / sqrt((double)Size.x * Size.x + HalfH * HalfH);
        if (AbsY > Cutoff)
            return HalfH / AbsY;
        return Size.x / sqrt((double)Local.x * Local.x + (double)Local.z * Local.z);
    }

    default:
        ZoneTypeNotSupported(0x4be);
        return GetRadius();
    }
}

// LAB_0633a290.
Vector ZONE::GetSurfacePoint(Vector const& Point) const
{
    if (Shape == Universe::ZoneShapeBox)
    {
        FZoneFrame Frame = ZoneFrame(*this);
        Frame.Dim[0] = Size.x * 0.5f;
        Frame.Dim[1] = Size.y * 0.5f;
        Frame.Dim[2] = Size.z * 0.5f;
        Vector const Delta = Vec3Sub(Point, Frame.Xform.Translation);
        Vector const Local = MathTransposeTransform(Frame.Xform.Rotation, Delta);
        Vector Clamped;
        ClampToBoxSurface(Frame, Local, Clamped);
        return EngineTransformXform(Frame, Clamped);
    }

    if (Shape == Universe::ZoneShapeRing)
    {
        // The nearest point on the ring's middle circle plus the tube direction scaled to the
        // surface.
        Vector const Delta = Vec3Sub(Point, Pos);
        Vector const Local = EngineTransposeTransform(Orientation, Delta);
        Vector Rim = MakeVector(Local.x, 0.0f, Local.z);
        float const MidRadius = (float)(((double)Size.y + Size.x) * 0.5);
        double const Scale = MidRadius / VecLength(Rim);
        Rim = MakeVector((float)(Rim.x * Scale), (float)(0.0f * Scale),
                         (float)(Rim.z * Scale));
        Vector Tube = Vec3Sub(Local, Rim);
        double const TubeLength = VecLength(Tube);
        if (!(TubeLength < 1.0e-4f))
        {
            double const Inv = 1.0 / TubeLength;
            Tube = MakeVector((float)(Tube.x * Inv), (float)(Tube.y * Inv),
                              (float)(Inv * Tube.z));
        }
        double const Surface = RingSurfaceScale(Size, Tube);
        Vector const Offset = MakeVector((float)(Tube.x * Surface), (float)(Tube.y * Surface),
                                         (float)(Tube.z * Surface));
        Vector const LocalSurface = Vec3Add(Rim, Offset);
        Vector const World = EngineTransformMatrix(Orientation, LocalSurface);
        return Vec3Add(Pos, World);
    }

    // Every other shape: the centre when the point is within 1e-4f of it.
    Vector Delta = MakeVector(Point.x - Pos.x, Point.y - Pos.y, Point.z - Pos.z);
    double const Length = VecLength(Delta);
    if (Length < 1.0e-4f)
        return Pos;
    double const Inv = 1.0 / Length;
    Delta = MakeVector((float)(Delta.x * Inv), (float)(Delta.y * Inv),
                       (float)(Delta.z * Inv));
    float const Radius = (float)GetRadiusAlong(Delta);
    Vector const Offset = Vec3Scale(Delta, Radius);
    return MakeVector(Offset.x + Pos.x, Offset.y + Pos.y, Offset.z + Pos.z);
}

// LAB_0633a6e0. The small-radius tail (LAB_0633aa25) returns a local zeroed at entry, not the
// float length: 0.0.
double ZONE::ClassifyPoint(Vector const& Point) const
{
    if (Shape == Universe::ZoneShapeBox)
        return ClassifyBox(Point);

    float LengthF = 0.0f;
    double Radius = 0.0;
    if (Shape == Universe::ZoneShapeRing)
    {
        // The offset from the ring's middle circle and the surface scale along it.
        Vector const Delta = Vec3Sub(Point, Pos);
        Vector const Local = EngineTransposeTransform(Orientation, Delta);
        double const MidRadius = ((double)Size.y + Size.x) * 0.5;
        double const Scale = MidRadius / sqrt((double)Local.z * Local.z +
                                              (double)Local.x * Local.x);
        float const RimX = (float)(Scale * Local.x);
        float const RimY = (float)(0.0f * Scale);
        double const RimZ = Local.z * Scale;
        Vector Tube = MakeVector(Local.x - RimX, Local.y - RimY,
                                 (float)((double)Local.z - RimZ));
        // Retail sums the length twice (same value): once stored as float, once kept for the
        // test.
        double const TubeLength = sqrt(((double)(Tube.y) * Tube.y + (double)(Tube.z) * Tube.z) +
                                       (double)(Tube.x) * Tube.x);
        LengthF = (float)(TubeLength);
        if (!(TubeLength < 1.0e-4f))
            VecScaleInPlace(Tube, (float)(1.0 / TubeLength));
        Radius = RingSurfaceScale(Size, Tube);
    }
    else
    {
        Vector Delta = Vec3Sub(Point, Pos);
        double const Length = sqrt(((double)(Delta.y) * Delta.y + (double)(Delta.z) * Delta.z) +
                                   (double)(Delta.x) * Delta.x);
        LengthF = (float)Length;
        if (Length < 1.0e-4f)
            return 0.0;
        double const Inv = 1.0 / LengthF;
        Delta = MakeVector((float)(Delta.x * Inv), (float)(Delta.y * Inv),
                           (float)(Delta.z * Inv));
        Radius = GetRadiusAlong(Delta);
    }

    if (!(Radius > 1.0e-4f))
        return 0.0;
    return LengthF / Radius;
}

// FUN_0633a917 (a tail of slot +0x48 for the box): the squared distance to the surface
// (negative inside) as a float, then (v + r^2) / r^2 with r = vt+0x30.
double ZONE::ClassifyBox(Vector const& Point) const
{
    FZoneFrame Frame = ZoneFrame(*this);
    Frame.Dim[0] = Size.x * 0.5f;
    Frame.Dim[1] = Size.y * 0.5f;
    Frame.Dim[2] = Size.z * 0.5f;
    Vector const Delta = Vec3Sub(Point, Frame.Xform.Translation);
    Vector const Local = MathTransposeTransform(Frame.Xform.Rotation, Delta);
    Vector Clamped;
    bool const bOutside = ClampToBoxSurface(Frame, Local, Clamped);
    Vector const Diff = Vec3Sub(Local, Clamped);
    double Value = ((double)Diff.y * Diff.y + (double)Diff.z * Diff.z) +
                   (double)Diff.x * Diff.x;
    if (!bOutside)
        Value = -Value;
    float const ValueF = (float)(Value);
    double const Radius = GetRadius();
    double const RadiusSq = Radius * Radius;
    return (ValueF + RadiusSq) / RadiusSq;
}

// LAB_0633aa40.
bool ZONE::Intersect(Vector const& Start, Vector const& End, Vector& OutHit) const
{
    FZoneSegment Segment;
    Segment.Start = Start;
    Segment.End = End;

    FZoneFrame Frame = ZoneFrame(*this);
    switch (Shape)
    {
    case Universe::ZoneShapeEllipsoid:
        Frame.Dim[0] = Size.x;
        Frame.Dim[1] = Size.y;
        Frame.Dim[2] = Size.z;
        return ClipSegmentEllipsoid(Frame, Segment, OutHit);

    case Universe::ZoneShapeBox:
        Frame.Dim[0] = Size.x;
        Frame.Dim[1] = Size.y;
        Frame.Dim[2] = Size.z;
        return ClipSegmentBox(Frame, Segment, OutHit);

    case Universe::ZoneShapeCylinder:
        // The third dimension is left as it was; the cylinder clip does not read it.
        Frame.Dim[0] = Size.x;
        Frame.Dim[1] = Size.y;
        return ClipSegmentCylinder(Frame, Segment, OutHit);

    case Universe::ZoneShapeRing:
        Frame.Dim[0] = Size.x;
        Frame.Dim[1] = Size.y;
        Frame.Dim[2] = Size.z;
        return ClipSegmentRing(Frame, Segment, OutHit);

    case Universe::ZoneShapeSphere:
        break;

    default:
        // Reported, then treated as the sphere case.
        ZoneTypeNotSupported(0x55a);
        break;
    }

    FZoneSphere Sphere;
    Sphere.Radius = GetRadius();
    Sphere.Center = Pos;
    return ClipSegmentSphere(Sphere, Segment, OutHit);
}

// LAB_0633ac20 [+0x54]
float ZONE::GetEdgeFraction() const
{
    return EdgeFraction;
}

// LAB_0633ad10: +0x58 is computed once and cached.
bool ZONE::HasPhysicalEffect() const
{
    if (PhysicalEffect < 0)
    {
        if (DragModifier < 1.0f || Interference > 0.0f || Damage > 0.0f)
            PhysicalEffect = 1;
        else
        {
            PhysicalEffect = 0;
            return false;
        }
    }
    return PhysicalEffect != 0;
}

// 0633ad70 (ICF with Archetype::Asteroid::get_detect_radius) [+0x5c]
float ZONE::GetDragModifier() const
{
    return DragModifier;
}

// 0633ad80 (ICF with Archetype::Asteroid::get_offset_dist) [+0x60]
float ZONE::GetDamage() const
{
    return Damage;
}

// 0633ad90 (ICF with Archetype::Asteroid::get_recharge_time) [+0x64]
float ZONE::GetInterference() const
{
    return Interference;
}

// LAB_0633add0
int ZONE::GetPopulationCount() const
{
    if (Population.Items.data() == nullptr)
        return 0;
    return (int)Population.Items.size();
}

// FUN_0633adf0
bool ZONE::GetPopulationEntry(int Index, unsigned& OutId, float& OutWeight) const
{
    if (Index < 0)
        return false;
    if (Population.Items.data() == nullptr)
        return false;
    if ((unsigned)Index >= Population.Items.size())
        return false;
    OutId = Population.Items[Index].first;
    OutWeight = Population.Items[Index].second;
    return true;
}

// LAB_0633ae40 [+0x8c]
weighted_vector<unsigned int> const& ZONE::GetPopulation() const
{
    return Population;
}

// LAB_0633ae50 [+0x88]
bool ZONE::HasPathLabel() const
{
    return bPathLabel;
}

// LAB_0633ada0 [+0x6c]
ID_String ZONE::GetMusic() const
{
    ID_String Result;
    Result.Id = MusicId;
    return Result;
}

// LAB_0633adb0 [+0x70 / +0x74]
bool ZONE::GetSpacedust(char const*& OutSpacedust, int& OutMaxParticles) const
{
    OutSpacedust = Spacedust;
    OutMaxParticles = SpacedustMaxParticles;
    return ((PropertyFlags >> 0x15) & 1) != 0;
}

// 06262dd0 (ICF with File::tell) [+0x84]
CmnAsteroid::LootableZoneEntry* ZONE::GetLootableInfo() const
{
    return OwnedLootEntry;
}

// LAB_0633ae60 [+0x9c]
unsigned char ZONE::GetVisit() const
{
    return Visit;
}

// LAB_0633ae80 [+0xa0]
int ZONE::GetName() const
{
    return IdsName;
}

// 0633ae90 [+0xa4]
int ZONE::GetIdsInfo() const
{
    return IdsInfo;
}

// FUN_0633aea0
void ZONE::SetLootableInfo(CmnAsteroid::LootableZoneEntry* LootEntry)
{
    if (OwnedLootEntry != nullptr)
    {
        free(OwnedLootEntry);
        OwnedLootEntry = nullptr;
    }
    OwnedLootEntry = LootEntry;
}

// FUN_0633ae70
void ZONE::SetVisit(unsigned char NewVisit)
{
    Visit = (unsigned char)(NewVisit | 0x20);
}

// .text:0633c6d0: true when the zone lies outside the frustum. A non-ellipsoid is culled as its
// bounding sphere (FUN_0633d1e0). An ellipsoid behind any side culls; otherwise, unless it
// straddles the far side, it stays. When exactly one of the left / right sides straddles,
// planes turned step by step around the far edge on that side are tried; the same for top /
// bottom afterwards.
bool __cdecl FrustumCull(Geometry::Frustum const& Frustum, Universe::IZone const* Zone)
{
    if (Zone == nullptr)
        return false;

    if (Zone->GetShape() != Universe::ZoneShapeEllipsoid)
    {
        FZoneSphere Sphere;
        float const Radius = Zone->GetRadius();
        Sphere.Center = Zone->GetPos();
        Sphere.Radius = Radius;
        return FrustumCullsSphere(Frustum, Sphere);
    }

    FZoneEllipsoid const Ellipsoid = CullEllipsoid(*Zone);

    int Sides[6];
    for (unsigned Index = 0; Index < 6; ++Index)
    {
        FCullPlane const Plane = FrustumPlane(Frustum, Index);
        Sides[Index] = EllipsoidPlaneSide(Ellipsoid, Plane);
        if (Sides[Index] == kPlaneSideBehind)
            return true;
    }
    if (Sides[1] != kPlaneSideStraddle)
        return false;

    // Bit 0..3 = side 2..5 straddles.
    unsigned const Straddle = (Sides[2] == kPlaneSideStraddle ? 1u : 0u) |
                              (Sides[3] == kPlaneSideStraddle ? 2u : 0u) |
                              (Sides[4] == kPlaneSideStraddle ? 4u : 0u) |
                              (Sides[5] == kPlaneSideStraddle ? 8u : 0u);
    float const Distance = Frustum.Distance;
    Vector const Back = VecNegate(FrustumColumn(Frustum, 2));

    unsigned const Horizontal = Straddle & 3u;
    if (Horizontal == 1u || Horizontal == 2u)
    {
        float const Spread = (float)(X87Tan((double)Frustum.AngleX * 0.5) * Distance);
        Vector const Right = FrustumColumn(Frustum, 0);
        Vector Center = Vec3Add(Frustum.Xform.Translation, Vec3Scale(Back, Distance));
        float Sweep;
        if ((Straddle & 1u) != 0)
        {
            Vector const Shift = Vec3Scale(Right, -Spread);
            Center = MakeVector(Shift.x + Center.x, Shift.y + Center.y, Shift.z + Center.z);
            Sweep = (float)(((double)Frustum.AngleX + kCullPi) * -0.5);
        }
        else
        {
            Vector const Shift = Vec3Scale(Right, Spread);
            Center = MakeVector(Shift.x + Center.x, Shift.y + Center.y, Shift.z + Center.z);
            Sweep = (float)(((double)Frustum.AngleX + kCullPi) * 0.5);
        }
        for (int Step = 1; Step < GFrustumCapSteps; ++Step)
        {
            if (CapStepCulls(Ellipsoid, Center, Sweep, Step, true))
                return true;
        }
    }

    unsigned const Vertical = Straddle & 0xcu;
    if (Vertical == 4u || Vertical == 8u)
    {
        float const Spread = (float)(X87Tan((double)Frustum.AngleY * 0.5) * Distance);
        Vector const Up = FrustumColumn(Frustum, 1);
        Vector Center = Vec3Add(Frustum.Xform.Translation, Vec3Scale(Back, Distance));
        float Sweep;
        if ((Straddle & 4u) != 0)
        {
            Vector const Shift = Vec3Scale(Up, Spread);
            Center = MakeVector(Shift.x + Center.x, Shift.y + Center.y, Shift.z + Center.z);
            Sweep = (float)(((double)Frustum.AngleY + kCullPi) * 0.5);
        }
        else
        {
            Vector const Shift = Vec3Scale(Up, -Spread);
            Center = MakeVector(Shift.x + Center.x, Shift.y + Center.y, Shift.z + Center.z);
            Sweep = (float)(((double)Frustum.AngleY + kCullPi) * -0.5);
        }
        for (int Step = 1; Step < GFrustumCapSteps; ++Step)
        {
            if (CapStepCulls(Ellipsoid, Center, Sweep, Step, false))
                return true;
        }
    }
    return false;
}

// .text:0633d770: true when the sphere does not reach the zone. The shape is asked again before
// each test, as retail does.
bool __cdecl SphereCull(Geometry::Sphere const& Sphere, Universe::IZone const* Zone)
{
    if (Zone == nullptr)
        return false;

    double const Radius = Sphere.Radius;
    if (Zone->GetShape() == Universe::ZoneShapeEllipsoid)
    {
        FZoneEllipsoid const Ellipsoid = CullEllipsoid(*Zone);
        return !(EllipsoidSignedDistance(Ellipsoid, Sphere.Center) < Radius);
    }

    if (Zone->GetShape() == Universe::ZoneShapeBox)
    {
        Vector const Size = Zone->GetSize();
        Vector const Half = MakeVector(Size.x * kCullHalf, Size.y * kCullHalf, Size.z * kCullHalf);
        Transform const& Xform = Zone->GetTransform();
        FZoneFrame Frame;
        Frame.Xform.Rotation = Xform.Rotation;
        Frame.Xform.Translation = Xform.Translation;
        Frame.Dim[0] = Half.x;
        Frame.Dim[1] = Half.y;
        Frame.Dim[2] = Half.z;
        return !(BoxSignedDistanceSq(Frame, Sphere.Center) < Radius * Radius);
    }

    if (Zone->GetShape() == Universe::ZoneShapeCylinder)
    {
        // Measured as a box of half extents {Size.x, Size.y * 0.5, Size.x}; GetSize is called
        // three times, as retail does.
        float const DimX = Zone->GetSize().x;
        float const DimZ = Zone->GetSize().x;
        float const DimY = (float)((double)(Zone->GetSize().y) * 0.5);
        Transform const& Xform = Zone->GetTransform();
        FZoneFrame Frame;
        Frame.Xform.Rotation = Xform.Rotation;
        Frame.Xform.Translation = Xform.Translation;
        Frame.Dim[0] = DimX;
        Frame.Dim[1] = DimY;
        Frame.Dim[2] = DimZ;
        Vector const Delta = Vec3Sub(Sphere.Center, Frame.Xform.Translation);
        Vector const Local = MathTransposeTransform(Frame.Xform.Rotation, Delta);
        Vector Clamped;
        bool const bOutside = ClampToBoxSurface(Frame, Local, Clamped);
        Vector const Diff = Vec3Sub(Local, Clamped);
        double Value = VecLengthSq(Diff);
        if (!bOutside)
            Value = -Value;
        return !(Value < Radius * Radius);
    }

    // Every other shape: the zone's bounding sphere.
    float const ZoneRadius = Zone->GetRadius();
    Vector const ZonePos = Zone->GetPos();
    double const DX = (double)(Sphere.Center.x) - ZonePos.x;
    double const DY = (double)(Sphere.Center.y) - ZonePos.y;
    double const DZ = (double)(Sphere.Center.z) - ZonePos.z;
    double const Reach = (double)(ZoneRadius) + Sphere.Radius;
    double const Sum = (DX * DX + DZ * DZ) + DY * DY;
    return !(Reach * Reach > Sum);
}
