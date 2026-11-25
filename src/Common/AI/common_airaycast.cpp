// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll AI ray / box / capsule / sphere tests and bearing angles (retail
// 0x062df830-0x062e1c90): RaySphereIntersect, RayAABBIntersect, RayOrientedBoxIntersect,
// RayTransformedBoxNearestHit, TransformedBoxesIntersect, BuildPerpendicularBasis, the
// capsule tests, ClampedDot3, SweptSphereCollisionTime, BearingAngleToIface/ToPoint.
// See src/include/common_aisteer.h for the per-function writeup.

#include "common_aisteer.h"
#include "AI/common_aisteer_internal.h"
#include "common_csys.h"          // Matrix, Transform, MatrixColumn0/1/2, Acquire3DMathEngine
#include "common_misc.h"          // FastInvSqrt
#include "x86math.h"              // I3DMathEngine::TransformXform

#include <cmath>
#include <cstring>

using namespace CmnAiSteer;

namespace
{
    // FUN_062e04c0, cdecl; sole caller TransformedBoxesIntersect. The 15-axis OBB
    // separating-axis test: Rij = dot(A.Axis_i, B.Axis_j), T = B.Center - A.Center. T, the Rij,
    // the |Rij| and Ta_i = dot(T, A.Axis_i) are float locals in the sum orders written below;
    // every radius and |projection| stays on the x87 stack, and a test separates only on
    // radius < |projection| (NaN goes on).
    bool OBBIntersect(OrientedBox const* A, OrientedBox const* B)
    {
        Vector const& AX = A->AxisX;
        Vector const& AY = A->AxisY;
        Vector const& AZ = A->AxisZ;
        Vector const& BX = B->AxisX;
        Vector const& BY = B->AxisY;
        Vector const& BZ = B->AxisZ;
        Vector const& AH = A->HalfExtent;
        Vector const& BH = B->HalfExtent;

        float const Tx = B->Center.x - A->Center.x;
        float const Ty = B->Center.y - A->Center.y;
        float const Tz = B->Center.z - A->Center.z;

        // Row 0 and Ta0.
        float const R00 = (float)(((double)AX.z * BX.z + (double)BX.x * AX.x) + (double)AX.y * BX.y);
        float const R01 = (float)(((double)BY.z * AX.z + (double)BY.y * AX.y) + (double)BY.x * AX.x);
        float const R02 = (float)(((double)BZ.z * AX.z + (double)BZ.y * AX.y) + (double)BZ.x * AX.x);
        float const Ta0 = (float)(((double)Ty * AX.y + (double)Tx * AX.x) + (double)Tz * AX.z);
        float const AbsR00 = std::fabs(R00);
        float const AbsR01 = std::fabs(R01);
        float const AbsR02 = std::fabs(R02);
        // Axis A.AxisX.
        if ((((double)AbsR00 * BH.x + (double)AbsR02 * BH.z) + (double)AbsR01 * BH.y) + AH.x < std::fabs(Ta0))
            return false;

        // Row 1, Ta1, axis A.AxisY.
        float const R10 = (float)(((double)AY.y * BX.y + (double)AY.z * BX.z) + (double)AY.x * BX.x);
        float const R11 = (float)(((double)AY.z * BY.z + (double)AY.y * BY.y) + (double)BY.x * AY.x);
        float const R12 = (float)(((double)AY.z * BZ.z + (double)AY.y * BZ.y) + (double)AY.x * BZ.x);
        float const Ta1 = (float)(((double)Tz * AY.z + (double)Ty * AY.y) + (double)Tx * AY.x);
        float const AbsR10 = std::fabs(R10);
        float const AbsR11 = std::fabs(R11);
        float const AbsR12 = std::fabs(R12);
        if ((((double)AbsR10 * BH.x + (double)AbsR12 * BH.z) + (double)AbsR11 * BH.y) + AH.y < std::fabs(Ta1))
            return false;

        // Row 2, Ta2, axis A.AxisZ.
        float const R20 = (float)(((double)AZ.y * BX.y + (double)AZ.z * BX.z) + (double)BX.x * AZ.x);
        float const R21 = (float)(((double)AZ.z * BY.z + (double)AZ.y * BY.y) + (double)BY.x * AZ.x);
        float const R22 = (float)(((double)AZ.z * BZ.z + (double)AZ.y * BZ.y) + (double)AZ.x * BZ.x);
        float const Ta2 = (float)(((double)Tz * AZ.z + (double)Ty * AZ.y) + (double)Tx * AZ.x);
        float const AbsR20 = std::fabs(R20);
        float const AbsR21 = std::fabs(R21);
        float const AbsR22 = std::fabs(R22);
        if ((((double)AbsR22 * BH.z + (double)AbsR21 * BH.y) + (double)AbsR20 * BH.x) + AH.z < std::fabs(Ta2))
            return false;

        // Axes B.AxisX / B.AxisY / B.AxisZ; the projections are not stored.
        if ((((double)AbsR20 * AH.z + (double)AbsR00 * AH.x) + (double)AbsR10 * AH.y) + BH.x <
            std::fabs(((double)Tz * BX.z + (double)Tx * BX.x) + (double)Ty * BX.y))
            return false;
        if ((((double)AbsR21 * AH.z + (double)AbsR01 * AH.x) + (double)AbsR11 * AH.y) + BH.y <
            std::fabs(((double)Tz * BY.z + (double)Ty * BY.y) + (double)Tx * BY.x))
            return false;
        if ((((double)AbsR22 * AH.z + (double)AbsR02 * AH.x) + (double)AbsR12 * AH.y) + BH.z <
            std::fabs(((double)Tz * BZ.z + (double)Ty * BZ.y) + (double)Tx * BZ.x))
            return false;

        // The nine cross axes, each radius summed as (p1 + p2) + (p3 + p4).
        if (((double)AbsR02 * BH.y + (double)AbsR01 * BH.z) + ((double)AbsR10 * AH.z + (double)AbsR20 * AH.y) <
            std::fabs((double)Ta2 * R10 - (double)R20 * Ta1))
            return false;
        if (((double)AbsR11 * AH.z + (double)AbsR21 * AH.y) + ((double)AbsR02 * BH.x + (double)AbsR00 * BH.z) <
            std::fabs((double)Ta2 * R11 - (double)R21 * Ta1))
            return false;
        if (((double)AbsR12 * AH.z + (double)AbsR22 * AH.y) + ((double)AbsR01 * BH.x + (double)AbsR00 * BH.y) <
            std::fabs((double)Ta2 * R12 - (double)R22 * Ta1))
            return false;
        if (((double)AbsR20 * AH.x + (double)AbsR00 * AH.z) + ((double)AbsR12 * BH.y + (double)AbsR11 * BH.z) <
            std::fabs((double)R20 * Ta0 - (double)Ta2 * R00))
            return false;
        if (((double)AbsR21 * AH.x + (double)AbsR01 * AH.z) + ((double)AbsR12 * BH.x + (double)AbsR10 * BH.z) <
            std::fabs((double)R21 * Ta0 - (double)Ta2 * R01))
            return false;
        if (((double)AbsR22 * AH.x + (double)AbsR02 * AH.z) + ((double)AbsR11 * BH.x + (double)AbsR10 * BH.y) <
            std::fabs((double)R22 * Ta0 - (double)Ta2 * R02))
            return false;
        if (((double)AbsR10 * AH.x + (double)AbsR00 * AH.y) + ((double)AbsR22 * BH.y + (double)AbsR21 * BH.z) <
            std::fabs((double)Ta1 * R00 - (double)R10 * Ta0))
            return false;
        if (((double)AbsR11 * AH.x + (double)AbsR01 * AH.y) + ((double)AbsR20 * BH.z + (double)AbsR22 * BH.x) <
            std::fabs((double)Ta1 * R01 - (double)R11 * Ta0))
            return false;
        if (((double)AbsR12 * AH.x + (double)AbsR02 * AH.y) + ((double)AbsR20 * BH.y + (double)AbsR21 * BH.x) <
            std::fabs((double)Ta1 * R02 - (double)R12 * Ta0))
            return false;

        return true;
    }

    // The per-box setup retail inlines three times: twice in TransformedBoxesIntersect and
    // once in RayTransformedBoxNearestHit. Builds an OrientedBox from a Transform and a local
    // AABB given as {MinX,MaxX,MinY,MaxY,MinZ,MaxZ}:
    //   axes       = the rotation's columns 0/1/2, raw, no sign flip;
    //   HalfExtent = |max - min| * 0.5, with the double 0.5 at DAT_0639df88, one float round;
    //   Center     = FUN_06299880(Xform, LocalMin + HalfExtent): float add (FUN_062887d0),
    //                then 3DMathEngine slot 0x28 (TransformXform).
    OrientedBox BuildOBBFromTransformAndAABB(Transform const& Xform, float const* AabbMinMax)
    {
        OrientedBox Box;
        Box.AxisX = MatrixColumn0(Xform.Rotation);
        Box.AxisY = MatrixColumn1(Xform.Rotation);
        Box.AxisZ = MatrixColumn2(Xform.Rotation);
        Box.HalfExtent.x = (float)(std::fabs((double)AabbMinMax[1] - AabbMinMax[0]) * 0.5);
        Box.HalfExtent.y = (float)(std::fabs((double)AabbMinMax[3] - AabbMinMax[2]) * 0.5);
        Box.HalfExtent.z = (float)(std::fabs((double)AabbMinMax[5] - AabbMinMax[4]) * 0.5);
        Vector const LocalMin{ AabbMinMax[0], AabbMinMax[2], AabbMinMax[4] };
        Vector const LocalCenter = Add(LocalMin, Box.HalfExtent);
        Acquire3DMathEngine()->TransformXform(Box.Center, Xform, LocalCenter);
        return Box;
    }
}

bool TransformedBoxesIntersect(Transform const& XformA, float const* AabbA, Transform const& XformB, float const* AabbB)
{
    OrientedBox const BoxA = BuildOBBFromTransformAndAABB(XformA, AabbA);
    OrientedBox const BoxB = BuildOBBFromTransformAndAABB(XformB, AabbB);
    return OBBIntersect(&BoxA, &BoxB);
}

// ----------------------------------------------------------------------------
// Ray-intersection cluster. See common_aisteer.h for the per-function writeup.
// ----------------------------------------------------------------------------

namespace
{
    // Slab-test half-space clip, inlined six times in RayAABBIntersect (once per box face):
    // the two per-axis blocks pass (Dir[i], -(Origin[i]+Half[i])) and
    // (-Dir[i], Origin[i]-Half[i]). Rejects if the ray starts on the wrong side and points
    // away; otherwise tightens whichever of TMin/TMax this plane bounds.
    // Precision: Denom and Numer are floats, the Denom*TMin / Denom*TMax products stay on the
    // x87 stack for their compare (NaN skips the store), and the quotient is rounded to float.
    bool ClipHalfSpace(float Denom, float Numer, float& TMin, float& TMax)
    {
        // Not-greater (and NaN) goes to the second test.
        if (Denom > 0.0f)
        {
            if ((double)Denom * TMax < Numer)
                return false;
            if ((double)Denom * TMin < Numer)
                TMin = (float)((double)Numer / Denom);
        }
        // Not-less (zero and NaN) goes to the parallel-face test.
        else if (Denom < 0.0f)
        {
            if ((double)Denom * TMin < Numer)
                return false;
            if ((double)Denom * TMax < Numer)
                TMax = (float)((double)Numer / Denom);
        }
        else // Denom == 0 (or NaN): ray parallel to this face's plane.
        {
            // Greater and unordered both reject.
            if (!(Numer <= 0.0f))
                return false;
        }
        return true;
    }
}

// FUN_062df830.
bool RaySphereIntersect(Vector const& RayOrigin, Vector const& RayDir, Vector const& SphereCenter, float const& Radius, float* OutT)
{
    // L = Center - Origin stays on the x87 stack; B is rounded to float, Disc is not.
    double const Lx = (double)SphereCenter.x - RayOrigin.x;
    double const Ly = (double)SphereCenter.y - RayOrigin.y;
    double const Lz = (double)SphereCenter.z - RayOrigin.z;
    float const B = (float)((Lz * RayDir.z + Ly * RayDir.y) + Lx * RayDir.x);
    double const Disc = ((double)B * B - ((Lx * Lx + Lz * Lz) + Ly * Ly)) + (double)Radius * Radius;
    // Leaves on less and on unordered.
    if (!(Disc >= 0.0))
        return false;

    double const Sq = std::sqrt(Disc);
    float const T0 = (float)(B - Sq);
    float const T1 = (float)(Sq + B);

    if (T0 < 0.0f)
    {
        // Less or NaN fails.
        if (!(T1 >= 0.0f))
            return false;
        if (OutT)
            *OutT = T1;
        return true;
    }
    if (T1 < 0.0f)
    {
        if (OutT)
            *OutT = T0;
        return true;
    }
    // Not-less or NaN writes T1.
    if (OutT)
        *OutT = (T0 < T1) ? T0 : T1;
    return true;
}

// FUN_062df940.
bool RayAABBIntersect(Vector const& LocalOrigin, Vector const& LocalDir, Vector const& HalfExtent, float& TMin, float& TMax)
{
    float const InTMin = TMin;
    float const InTMax = TMax;

    if (!ClipHalfSpace(LocalDir.x, -(LocalOrigin.x + HalfExtent.x), TMin, TMax))
        return false;
    if (!ClipHalfSpace(-LocalDir.x, LocalOrigin.x - HalfExtent.x, TMin, TMax))
        return false;
    if (!ClipHalfSpace(LocalDir.y, -(LocalOrigin.y + HalfExtent.y), TMin, TMax))
        return false;
    if (!ClipHalfSpace(-LocalDir.y, LocalOrigin.y - HalfExtent.y, TMin, TMax))
        return false;
    if (!ClipHalfSpace(LocalDir.z, -(LocalOrigin.z + HalfExtent.z), TMin, TMax))
        return false;
    if (!ClipHalfSpace(-LocalDir.z, LocalOrigin.z - HalfExtent.z, TMin, TMax))
        return false;

    // Retail's final guard: an interval unchanged from its input means no face test
    // tightened it.
    return TMin != InTMin || TMax != InTMax;
}

// FUN_062dfd80.
bool RayOrientedBoxIntersect(Vector const& RayOrigin, Vector const& RayDir, OrientedBox const& Box, int* OutHitType, Vector* OutPoints)
{
    // FUN_06288800, float per component.
    Vector const O = Sub(RayOrigin, Box.Center);

    // Each sum (z-term + y-term) + x-term on the x87 stack, rounded to float.
    Vector const LocalOrigin{
        (float)(((double)O.z * Box.AxisX.z + (double)O.y * Box.AxisX.y) + (double)O.x * Box.AxisX.x),
        (float)(((double)O.z * Box.AxisY.z + (double)O.y * Box.AxisY.y) + (double)O.x * Box.AxisY.x),
        (float)(((double)O.z * Box.AxisZ.z + (double)O.y * Box.AxisZ.y) + (double)O.x * Box.AxisZ.x),
    };
    // The direction, summed (y-term + z-term) + x-term.
    Vector const LocalDir{
        (float)(((double)Box.AxisX.y * RayDir.y + (double)Box.AxisX.z * RayDir.z) + (double)Box.AxisX.x * RayDir.x),
        (float)(((double)Box.AxisY.y * RayDir.y + (double)Box.AxisY.z * RayDir.z) + (double)Box.AxisY.x * RayDir.x),
        (float)(((double)Box.AxisZ.y * RayDir.y + (double)Box.AxisZ.z * RayDir.z) + (double)RayDir.x * Box.AxisZ.x),
    };

    float TMin = 0.0f;
    float TMax = 3.4028235e+38f; // 0x7f7fffff (FLT_MAX)
    // Retail has no null test on OutHitType or OutPoints; its one caller passes both.
    if (!RayAABBIntersect(LocalOrigin, LocalDir, Box.HalfExtent, TMin, TMax))
    {
        *OutHitType = 0;
        return false;
    }

    // The exit point both arms write: TMax*D.z is rounded before its add, x and y add the
    // origin to the unrounded product.
    float const ExitZProduct = TMax * RayDir.z;
    Vector const Exit{
        (float)((double)TMax * RayDir.x + RayOrigin.x),
        (float)((double)TMax * RayDir.y + RayOrigin.y),
        ExitZProduct + RayOrigin.z };

    // Not-greater (and NaN) is the inside arm.
    if (TMin > 0.0f)
    {
        // Entered from outside: hit type 2, entry point (TMin*D rounded into a Vector,
        // then + origin, two roundings) in [0], exit point in [1].
        *OutHitType = 2;
        Vector const Scaled{ TMin * RayDir.x, TMin * RayDir.y, TMin * RayDir.z };
        OutPoints[0] = Vector{ Scaled.x + RayOrigin.x, Scaled.y + RayOrigin.y, Scaled.z + RayOrigin.z };
        OutPoints[1] = Exit;
        return true;
    }

    // Ray origin already inside the box: hit type 1, ONLY the exit point, into
    // OutPoints[0]; OutPoints[1] is left untouched.
    *OutHitType = 1;
    OutPoints[0] = Exit;
    return true;
}

// FUN_062dffd0 (no Ghidra decompile; reconstructed from the listing). See common_aisteer.h.
bool RayTransformedBoxNearestHit(Vector const& RayOrigin, Vector const& RayDir,
                                            Transform const& Xform, float const* AabbMinMax,
                                            Vector* OutPoint, bool* OutStartsInside)
{
    // The same inlined box setup as TransformedBoxesIntersect; the 15 contiguous float locals
    // are the OrientedBox FUN_062dfd80 indexes.
    OrientedBox const Box = BuildOBBFromTransformAndAABB(Xform, AabbMinMax);

    // Cleared before any test, so it is written even on the "no intersection" exits.
    // Retail null-guards this one pointer and no other.
    if (OutStartsInside != 0)
        *OutStartsInside = false;

    Vector const Diff{ RayOrigin.x - Box.Center.x,
                       RayOrigin.y - Box.Center.y,
                       RayOrigin.z - Box.Center.z };

    // The rest is Eberly's ray-vs-OBB separating-axis test (Wild Magic Ray3Box3
    // "TestIntersection"), unrolled over the three box axes and then the three RayDir x Diff
    // cross axes. The parallel term is deliberately not part of the first three tests: this
    // is the ray form ("started outside the slab and heading away from it"), not the segment
    // form ("|DdU| > extent + |WdU|").
    // Precision (all six tests): the dots and products stay on the x87 stack; only |W.U_i| is
    // rounded to a float local for reuse. Unordered compares continue (do not reject).

    // --- Box.AxisX ---
    double const WdU0 = ((double)Box.AxisX.z * RayDir.z + (double)Box.AxisX.y * RayDir.y) + (double)Box.AxisX.x * RayDir.x;
    float const AWdU0 = (float)std::fabs(WdU0);
    double const DdU0 = ((double)Diff.z * Box.AxisX.z + (double)Diff.y * Box.AxisX.y) + (double)Diff.x * Box.AxisX.x;
    if (std::fabs(DdU0) > Box.HalfExtent.x && DdU0 * WdU0 >= 0.0)
        return false;

    // --- Box.AxisY ---
    double const WdU1 = ((double)Box.AxisY.x * RayDir.x + (double)Box.AxisY.y * RayDir.y) + (double)Box.AxisY.z * RayDir.z;
    float const AWdU1 = (float)std::fabs(WdU1);
    double const DdU1 = ((double)Box.AxisY.z * Diff.z + (double)Box.AxisY.y * Diff.y) + (double)Box.AxisY.x * Diff.x;
    if (std::fabs(DdU1) > Box.HalfExtent.y && DdU1 * WdU1 >= 0.0)
        return false;

    // --- Box.AxisZ ---
    double const WdU2 = ((double)Box.AxisZ.x * RayDir.x + (double)Box.AxisZ.y * RayDir.y) + (double)Box.AxisZ.z * RayDir.z;
    float const AWdU2 = (float)std::fabs(WdU2);
    double const DdU2 = ((double)Box.AxisZ.z * Diff.z + (double)Box.AxisZ.y * Diff.y) + (double)Box.AxisZ.x * Diff.x;
    if (std::fabs(DdU2) > Box.HalfExtent.z && DdU2 * WdU2 >= 0.0)
        return false;

    // WxD = RayDir x Diff. x and y are rounded; z is stored rounded but the first cross test
    // uses the unrounded z still in ST0.
    float const WxDx = (float)((double)Diff.z * RayDir.y - (double)Diff.y * RayDir.z);
    float const WxDy = (float)((double)Diff.x * RayDir.z - (double)Diff.z * RayDir.x);
    double const WxDzExact = (double)Diff.y * RayDir.x - (double)Diff.x * RayDir.y;
    float const WxDz = (float)WxDzExact;

    // --- RayDir x Box.AxisX --- returns 0 only on |dot| > radius.
    double const AWxDdU0 = std::fabs((WxDzExact * Box.AxisX.z + (double)WxDy * Box.AxisX.y) + (double)WxDx * Box.AxisX.x);
    if (AWxDdU0 > (double)AWdU2 * Box.HalfExtent.y + (double)Box.HalfExtent.z * AWdU1)
        return false;

    // --- RayDir x Box.AxisY ---
    double const AWxDdU1 = std::fabs(((double)Box.AxisY.z * WxDz + (double)Box.AxisY.y * WxDy) + (double)Box.AxisY.x * WxDx);
    if (AWxDdU1 > (double)AWdU2 * Box.HalfExtent.x + (double)Box.HalfExtent.z * AWdU0)
        return false;

    // --- RayDir x Box.AxisZ --- compiled the other way up (continues on radius >= |dot| and
    // NaN); same sense as the two above.
    double const AWxDdU2 = std::fabs(((double)Box.AxisZ.z * WxDz + (double)Box.AxisZ.y * WxDy) + (double)Box.AxisZ.x * WxDx);
    if ((double)Box.HalfExtent.y * AWdU0 + (double)AWdU1 * Box.HalfExtent.x < AWxDdU2)
        return false;

    // The slab clipper for the actual points. Retail seeds the hit-type slot with 0 and
    // ignores FUN_062dfd80's bool, switching on the slot.
    int HitType = 0;
    // RETAIL UB: this two-Vector scratch is uninitialised in retail. When FUN_062dfd80 reports type 0 after the SAT passed (reachable, e.g.
    // a NaN RayDir from a zero-length normalise in the caller, or a zero RayDir with
    // the origin inside the box), retail reads stack garbage in the two-point branch
    // below; the port zero-fills it instead.
    Vector HitPoints[2] = { Vector{ 0.0f, 0.0f, 0.0f }, Vector{ 0.0f, 0.0f, 0.0f } };
    RayOrientedBoxIntersect(RayOrigin, RayDir, Box, &HitType, HitPoints);

    if (HitType == 1)
    {
        // The ray origin was already inside the box.
        if (OutStartsInside != 0)
            *OutStartsInside = true;
        *OutPoint = HitPoints[0];
        return true;
    }

    // Return whichever point is nearer the ray origin, compared as squared distances. The six
    // differences stay on the x87 stack except D1.z, which is rounded before it is squared.
    double const D0x = (double)HitPoints[0].x - RayOrigin.x;
    double const D0y = (double)HitPoints[0].y - RayOrigin.y;
    double const D0z = (double)HitPoints[0].z - RayOrigin.z;
    double const D1x = (double)HitPoints[1].x - RayOrigin.x;
    double const D1y = (double)HitPoints[1].y - RayOrigin.y;
    float const D1z = (float)((double)HitPoints[1].z - RayOrigin.z);
    double const Dist0Sq = (D0z * D0z + D0y * D0y) + D0x * D0x;
    double const Dist1Sq = ((double)D1z * D1z + D1y * D1y) + D1x * D1x;

    // !(a > b) rather than (a <= b) so that the unordered case picks point 1, as retail does.
    *OutPoint = !(Dist1Sq > Dist0Sq) ? HitPoints[1] : HitPoints[0];
    return true;
}

// BuildPerpendicularBasis is FUN_062e0cf0; see common_aisteer.h.
namespace
{
    // FUN_062df7f0, cdecl, one float, result left unrounded in ST0; only called from
    // BuildPerpendicularBasis. 0.5f at DAT_063a073c, 1.5f at DAT_063a0740; the shift is
    // logical (SHR).
    double FastInvSqrtX87(float X)
    {
        double const Half = (double)X * 0.5f;
        unsigned Bits;
        std::memcpy(&Bits, &X, sizeof(Bits));
        Bits = 0x5f3759dfu - (Bits >> 1);
        float Y;
        std::memcpy(&Y, &Bits, sizeof(Y));
        return 1.5f * (double)Y - (Half * Y) * ((double)Y * Y);
    }
}

void BuildPerpendicularBasis(Vector* Out1, Vector* Out2, Vector* Dir, bool AlreadyNormalized)
{
    // A real normalise: each component times the unrounded reciprocal.
    if (!AlreadyNormalized)
    {
        double const InvLen = 1.0 / std::sqrt(((double)Dir->x * Dir->x + (double)Dir->y * Dir->y) +
                                              (double)Dir->z * Dir->z);
        Dir->x = (float)(InvLen * Dir->x);
        Dir->y = (float)(InvLen * Dir->y);
        Dir->z = (float)(InvLen * Dir->z);
    }

    // Greater and unordered take the y-dominant arm. Each arm passes the float-rounded sum of
    // squares to FUN_062df7f0 and multiplies by its unrounded result.
    if (!(std::fabs(Dir->y) <= std::fabs(Dir->x)))
    {
        double const Scale = FastInvSqrtX87((float)((double)Dir->y * Dir->y + (double)Dir->z * Dir->z));
        Out1->x = 0.0f;
        Out1->y = (float)(Scale * Dir->z);
        Out1->z = (float)(-(Scale * Dir->y));
    }
    else
    {
        double const Scale = FastInvSqrtX87((float)((double)Dir->x * Dir->x + (double)Dir->z * Dir->z));
        Out1->y = 0.0f;
        Out1->x = (float)(-(Scale * Dir->z));
        Out1->z = (float)(Scale * Dir->x);
    }

    // Out2 = Out1 x Dir, one rounding per component.
    float const X = (float)((double)Out1->z * Dir->y - (double)Out1->y * Dir->z);
    float const Y = (float)((double)Out1->x * Dir->z - (double)Out1->z * Dir->x);
    float const Z = (float)((double)Out1->y * Dir->x - (double)Out1->x * Dir->y);
    Out2->x = X;
    Out2->y = Y;
    Out2->z = Z;
}

// FUN_062e0e10, ray vs capsule. See common_aisteer.h.
int RaySegmentCapsuleIntersect(Vector const& RayOrigin, Vector const& RayDir, float Radius,
                                           Vector const& SegStart, Vector const& SegDir, float* OutT)
{
    // Retail passes SegStart and SegDir by value; the const references read the same floats.
    // Precision: a name ending in Exact stays on the x87 stack, every float is a rounded image.

    // Height = the inlined fast length (magic from L's float image, Newton step and multiply
    // on the unrounded L).
    double const AxisLenSqExact = ((double)SegDir.x * SegDir.x + (double)SegDir.y * SegDir.y) +
                                  (double)SegDir.z * SegDir.z;
    float const Height = (float)InlineFastLength(AxisLenSqExact);
    // A real sqrt here; each axis component times the unrounded reciprocal.
    double const InvAxisLen = 1.0 / std::sqrt(AxisLenSqExact);
    Vector UnitAxis{ (float)(SegDir.x * InvAxisLen), (float)(SegDir.y * InvAxisLen), (float)(SegDir.z * InvAxisLen) };

    // Already normalised, so UnitAxis is not modified.
    Vector Basis1, Basis2;
    BuildPerpendicularBasis(&Basis1, &Basis2, &UnitAxis, true);

    // The three direction projections, each summed (x-term + z-term) + y-term.
    float const d1 = (float)(((double)Basis1.x * RayDir.x + (double)Basis1.z * RayDir.z) + (double)Basis1.y * RayDir.y);
    float const d2 = (float)(((double)Basis2.x * RayDir.x + (double)Basis2.z * RayDir.z) + (double)Basis2.y * RayDir.y);
    float const d3 = (float)(((double)UnitAxis.x * RayDir.x + (double)UnitAxis.z * RayDir.z) + (double)UnitAxis.y * RayDir.y);

    // d2*d2 and d1*d1 are rounded; the length (d3*d3 + d2d2) + d1d1 stays unrounded, and its
    // inlined fast length is inverted.
    float const D2Sq = d2 * d2;
    float const D1Sq = d1 * d1;
    double const DirLenSqExact = ((double)d3 * d3 + D2Sq) + D1Sq;
    float const InvDirLen = (float)(1.0 / InlineFastLength(DirLenSqExact));

    // r = RayOrigin - SegStart on the x87 stack; o1 / o2 / o3 against b1 / b2 / the axis.
    double const Rx = (double)RayOrigin.x - SegStart.x;
    double const Ry = (double)RayOrigin.y - SegStart.y;
    double const Rz = (double)RayOrigin.z - SegStart.z;
    float const o1 = (float)(((double)Basis1.z * Rz + Ry * Basis1.y) + Rx * Basis1.x);
    float const o2 = (float)(((double)Basis2.z * Rz + Ry * Basis2.y) + Rx * Basis2.x);
    float const o3 = (float)((Rz * UnitAxis.z + Ry * UnitAxis.y) + Rx * UnitAxis.x);

    double const RadiusSqExact = (double)Radius * Radius;

    // Double 0.999999999999 at DAT_063a0750; less and unordered take the general path.
    if (std::fabs(d3) >= 0.999999999999)
    {
        // Ray parallel to the axis. Returns 0 on less and unordered.
        double const Disc = (RadiusSqExact - (double)o1 * o1) - (double)o2 * o2;
        if (!(Disc >= 0.0))
            return 0;
        double const SqrtDisc = std::sqrt(Disc);
        OutT[0] = (float)(-((o3 + SqrtDisc) * InvDirLen));
        OutT[1] = (float)((((double)Height - o3) + SqrtDisc) * InvDirLen);
        return 2;
    }

    // The lateral cylinder quadratic.
    float const A = D2Sq + D1Sq;
    float B = (float)((double)d2 * o2 + (double)d1 * o1);
    float C = (float)(((double)o2 * o2 + (double)o1 * o1) - RadiusSqExact);
    // The unrounded value is compared (NaN continues); the rounded image is used afterwards.
    double const CylDiscExact = (double)B * B - (double)C * A;
    if (CylDiscExact < 0.0)
        return 0;
    float const CylDisc = (float)CylDiscExact;

    int Count = 0;
    // Not-greater (zero, NaN) is the tangent arm.
    if (CylDisc > 0.0f)
    {
        // The axis test uses the unrounded T0 (unordered rejects); the stored root is the
        // rounded T0 times Inv.
        double const SqrtCylDisc = std::sqrt((double)CylDisc);
        double const InvA = 1.0f / (double)A;
        double const T0Exact = (-(double)B - SqrtCylDisc) * InvA;
        float const T0 = (float)T0Exact;
        double const AxisCoord0 = T0Exact * d3 + o3;
        if (AxisCoord0 >= 0.0 && AxisCoord0 <= Height)
            OutT[Count++] = T0 * InvDirLen;

        // T1 is rounded first and the rounded T1 is used for the axis test.
        float const T1 = (float)((SqrtCylDisc - B) * InvA);
        double const AxisCoord1 = (double)T1 * d3 + o3;
        if (AxisCoord1 >= 0.0 && AxisCoord1 <= Height)
            OutT[Count++] = T1 * InvDirLen;

        if (Count == 2)
            return 2;
    }
    else
    {
        // The tangent root: the unrounded T goes through the axis tests, the rounded T is
        // stored.
        double const TExact = -((double)B / A);
        float const T = (float)TExact;
        double const AxisCoord = TExact * d3 + o3;
        if (AxisCoord >= 0.0 && AxisCoord <= Height)
        {
            OutT[0] = T * InvDirLen;
            return 1;
        }
    }

    // The cap at SegStart. B' and C' overwrite B / C rounded; the discriminant stays unrounded.
    B = (float)((double)o3 * d3 + B);
    C = (float)((double)o3 * o3 + C);
    double const DiscApex = (double)B * B - C;
    // Not-greater falls to a test that keeps only the equal case.
    bool bApexRoots = false;
    double TApexNear = 0.0;
    double TApexFar = 0.0;
    if (DiscApex > 0.0)
    {
        double const SqrtDiscApex = std::sqrt(DiscApex);
        TApexNear = -(double)B - SqrtDiscApex;
        TApexFar = SqrtDiscApex - B;
        bApexRoots = true;
        // A root counts when d3*T + o3 <= 0 (unordered rejects); the stored value is the
        // unrounded T times Inv.
        if ((double)d3 * TApexNear + o3 <= 0.0)
        {
            OutT[Count++] = (float)(TApexNear * InvDirLen);
            if (Count == 2)
                return 2;
        }
    }
    else if (DiscApex == 0.0)
    {
        // The single root -B'.
        TApexFar = -(double)B;
        bApexRoots = true;
    }
    if (bApexRoots)
    {
        // Sq - B' (or -B'), same test and store.
        if ((double)d3 * TApexFar + o3 <= 0.0)
        {
            OutT[Count++] = (float)(TApexFar * InvDirLen);
            if (Count == 2)
                return 2;
        }
    }

    // The cap at SegStart + SegDir. B'' is stored rounded, and its square is the unrounded
    // B'' times the rounded image; C'' stays unrounded.
    double const BTopExact = (double)B - (double)d3 * Height;
    B = (float)BTopExact;
    double const CTop = ((double)Height - ((double)o3 + o3)) * Height + C;
    double const DiscTop = BTopExact * B - CTop;
    bool bTopRoots = false;
    double TTopFar = 0.0;
    if (DiscTop > 0.0)
    {
        double const SqrtDiscTop = std::sqrt(DiscTop);
        double const TTopNear = -(double)B - SqrtDiscTop;
        TTopFar = SqrtDiscTop - B;
        bTopRoots = true;
        // A root counts when d3*T + o3 >= H (unordered rejects).
        if ((double)d3 * TTopNear + o3 >= Height)
        {
            OutT[Count++] = (float)(TTopNear * InvDirLen);
            if (Count == 2)
                return 2;
        }
    }
    else if (DiscTop == 0.0)
    {
        // The single root -B''.
        TTopFar = -(double)B;
        bTopRoots = true;
    }
    if (bTopRoots)
    {
        if ((double)d3 * TTopFar + o3 >= Height)
        {
            OutT[Count++] = (float)(TTopFar * InvDirLen);
            if (Count == 2)
                return 2;
        }
    }

    return Count;
}

// FUN_062e1330. Returns the first hit, not the last; see common_aisteer.h.
bool RaySegmentCapsuleFirstHit(Vector const& RayOrigin, Vector const& RayDir, float Radius,
                                           Vector const& SegStart, Vector const& SegDir, Vector* OutPoint)
{
    // Retail takes a pointer to one 7-dword record {Radius, SegStart, SegDir} and copies it
    // onto the stack as FUN_062e0e10's by-value arguments; the port passes the three pieces.
    float Ts[2];
    int const HitCount = RaySegmentCapsuleIntersect(RayOrigin, RayDir, Radius, SegStart, SegDir, Ts);

    // RETAIL UB: Points[0] is copied to *OutPoint unconditionally; with no valid t retail
    // copies uninitialised stack. The port zero-fills instead.
    Vector Points[2] = { Vector{ 0.0f, 0.0f, 0.0f }, Vector{ 0.0f, 0.0f, 0.0f } };
    int ValidCount = 0;
    for (int i = 0; i < HitCount; ++i)
    {
        // Skips less and unordered.
        if (Ts[i] >= 0.0f)
        {
            // y and z products are rounded before the origin is added; x adds the origin
            // to the unrounded t*dx.
            float const T = Ts[i];
            float const Ty = T * RayDir.y;
            float const Tz = T * RayDir.z;
            Points[ValidCount] = Vector{ (float)((double)T * RayDir.x + RayOrigin.x),
                                         Ty + RayOrigin.y,
                                         Tz + RayOrigin.z };
            ++ValidCount;
        }
    }
    *OutPoint = Points[0];
    return ValidCount > 0;
}

// FUN_062e1410, point vs capsule. See common_aisteer.h.
bool PointInCapsule(Vector const& Point, float Radius, Vector const& SegStart, Vector const& SegDir)
{
    // Retail takes the same 7-dword capsule record as RaySegmentCapsuleFirstHit; the port
    // passes the pieces. The offset stays on the x87 stack.
    double Ox = (double)Point.x - SegStart.x;
    double Oy = (double)Point.y - SegStart.y;
    double Oz = (double)Point.z - SegStart.z;

    // Skips the projection on less and equal (greater and NaN project). The compare tests the
    // unrounded sum; the rounded image is what is reused below.
    double const DotExact = (Oz * SegDir.z + Oy * SegDir.y) + Ox * SegDir.x;
    float const Dot = (float)DotExact;
    if (!(DotExact <= 0.0))
    {
        // Less and NaN take the interior projection.
        float const SegLenSq = (float)(((double)SegDir.x * SegDir.x + (double)SegDir.y * SegDir.y) +
                                       (double)SegDir.z * SegDir.z);
        if (!(Dot >= SegLenSq))
        {
            // T unrounded; T*Dx and T*Dy are rounded before the subtraction, T*Dz is not.
            double const T = (double)Dot / SegLenSq;
            Ox -= (float)(T * SegDir.x);
            Oy -= (float)(T * SegDir.y);
            Oz -= T * SegDir.z;
        }
        else
        {
            // Past the far end, offset from SegStart + SegDir.
            Ox -= SegDir.x;
            Oy -= SegDir.y;
            Oz -= SegDir.z;
        }
    }

    // All unrounded; returns 0 on R*R less (and NaN).
    double const DistSq = (Oz * Oz + Ox * Ox) + Oy * Oy;
    return (double)Radius * Radius >= DistSq;
}

// FUN_062e1630, cdecl. Clamps the dot to [-1, 1]; NaN passes unchanged. The result is
// returned in ST0 without a float store, so it is a double here: a caller that rounds it to
// float rounds where retail does, and one that keeps it on the stack keeps the full value.
double ClampedDot3(Vector const& A, Vector const& B)
{
    double const Dot = (static_cast<double>(A.z) * B.z + static_cast<double>(A.y) * B.y) +
                       static_cast<double>(A.x) * B.x;
    if (Dot > 1.0)
        return 1.0;
    if (Dot < -1.0)
        return -1.0;
    return Dot;
}

// FUN_062e18f0, swept sphere vs swept sphere. See common_aisteer.h.
bool SweptSphereCollisionTime(float RadiusA, Vector const& A0, Vector const& A1,
                                          float RadiusB, Vector const& B0, Vector const& B1, float* OutT)
{
    // The float / double split of each term follows which values retail rounds to locals.
    double const DAx = (double)A1.x - A0.x;
    double const DAy = (double)A1.y - A0.y;
    float const DAz = A1.z - A0.z;
    double const DBx = (double)B1.x - B0.x;
    float const DBy = B1.y - B0.y;
    float const DBz = B1.z - B0.z;
    float const Px = B0.x - A0.x;
    float const Py = B0.y - A0.y;
    float const Pz = B0.z - A0.z;
    float const Vx = (float)(DBx - DAx);
    double const Vy = DBy - DAy;
    double const Vz = (double)DBz - DAz;
    float const RadiusSum = RadiusA + RadiusB;

    float const A = (float)((Vz * Vz + Vy * Vy) + (double)Vx * Vx);
    double const BHalf = (Vz * Pz + Vy * Py) + (double)Vx * Px;
    float const B = (float)(BHalf + BHalf);
    double const C0 = ((double)Pz * Pz + (double)Py * Py) + (double)Px * Px;
    double const CombinedRadiusSq = (double)RadiusSum * RadiusSum;

    // Already touching writes 0 and returns 1; greater and unordered go on.
    if (C0 <= CombinedRadiusSq)
    {
        *OutT = 0.0f;
        return true;
    }

    // 4.0f at DAT_063a05c0.
    float const Disc = (float)((double)B * B - ((C0 - CombinedRadiusSq) * A) * 4.0f);
    // Less and unordered both fail to *OutT = -1.
    if (Disc >= 0.0f && (double)A * A >= 0.01f)
    {
        // Root2 is rounded, Root1 stays unrounded.
        double const SqrtDisc = std::sqrt((double)Disc);
        double const InvTwoA = 1.0f / ((double)A + A);
        float const Root2 = (float)((SqrtDisc - B) * InvTwoA);
        double const Root1 = (-(double)B - SqrtDisc) * InvTwoA;
        // Root1 only when Root2 is strictly greater; equal and unordered keep Root2.
        float const T = (Root2 > Root1) ? (float)Root1 : Root2;
        *OutT = T;
        // Only a negative T returns 0 (OutT keeps it); zero, positive and NaN return 1.
        return !(T < 0.0f);
    }

    *OutT = -1.0f;
    return false;
}

namespace
{
    // The clamp + _CIacos + FABS tail both bearing helpers inline. The dot arrives rounded to
    // float; the clamps are strict (NaN passes). The result stays unrounded in ST0 for the
    // caller, hence double.
    double BearingClampedAcos(float Dot)
    {
        if (Dot > 1.0f)
        {
            Dot = 1.0f;
        }
        else if (Dot < -1.0f)
        {
            Dot = -1.0f;
        }
        return std::fabs(std::acos(static_cast<double>(Dot)));
    }
}

// FUN_062e1aa0, cdecl. Two-interface bearing: the angle between (Self's centre - Target's
// centre) and column 2 of Self's own transform.
double BearingAngleToIface(void* Self, void* Target)
{
    if (Self == nullptr || Target == nullptr)
    {
        return 0.0;   // DAT_06399408
    }

    // Retail queries Target first, then Self, each with the inlined GetIfacePositionOrZero.
    Vector PosTarget;
    GetIfacePositionOrZero(&PosTarget, ObjInspectOf(Target));
    Vector PosSelf;
    GetIfacePositionOrZero(&PosSelf, ObjInspectOf(Self));

    // The matrix is read at +8/+0x14/+0x20 (column 2).
    Matrix const& M = ObjInspectOf(Self)->get_transform().Rotation;

    float const Dx = PosSelf.x - PosTarget.x;
    float const Dy = PosSelf.y - PosTarget.y;
    float const Dz = PosSelf.z - PosTarget.z;

    double const Len = std::sqrt((static_cast<double>(Dz) * Dz + static_cast<double>(Dy) * Dy)
                                 + static_cast<double>(Dx) * Dx);

    float Nx;
    float Ny;
    double Nz;
    // Not-greater (or unordered) takes the zero path.
    if (Len > static_cast<double>(0.01f))
    {
        // The inverse stays unrounded; x and y are rounded to float, z stays on the stack.
        double const Inv = 1.0 / Len;
        Nx = static_cast<float>(Dx * Inv);
        Ny = static_cast<float>(Dy * Inv);
        Nz = Dz * Inv;
    }
    else
    {
        // FUN_0636cf40(0, 0, 0).
        Nx = 0.0f;
        Ny = 0.0f;
        Nz = 0.0;
    }

    float const Dot = static_cast<float>((Nz * M.E[2][2] + static_cast<double>(Ny) * M.E[1][2])
                                         + static_cast<double>(M.E[0][2]) * Nx);
    return BearingClampedAcos(Dot);
}

// FUN_062e1c90, cdecl(Self, Vector const* Point). Ghidra counts one parameter; all three
// callers push two dwords. Same bearing as BearingAngleToIface against a caller-supplied
// point, but its normalisation rounds differently (see below).
double BearingAngleToPoint(void* Self, Vector const& ComparisonPoint)
{
    if (Self == nullptr)
    {
        return 0.0;   // DAT_06399408
    }

    Vector PosSelf;
    GetIfacePositionOrZero(&PosSelf, ObjInspectOf(Self));

    Matrix const& M = ObjInspectOf(Self)->get_transform().Rotation;

    float const Dx = PosSelf.x - ComparisonPoint.x;
    float const Dy = PosSelf.y - ComparisonPoint.y;
    float const Dz = PosSelf.z - ComparisonPoint.z;

    double const Len = std::sqrt((static_cast<double>(Dz) * Dz + static_cast<double>(Dy) * Dy)
                                 + static_cast<double>(Dx) * Dx);

    double Nx;
    double Ny;
    double Nz;
    // Not-greater (or unordered) takes the zero path.
    if (Len > static_cast<double>(0.01f))
    {
        // Unlike the sibling above, the inverse is rounded to float here, and the three
        // products stay unrounded on the stack.
        float const Inv = static_cast<float>(1.0 / Len);
        Nx = static_cast<double>(Inv) * Dx;
        Ny = static_cast<double>(Dy) * Inv;
        Nz = static_cast<double>(Dz) * Inv;
    }
    else
    {
        Nx = 0.0;
        Ny = 0.0;
        Nz = 0.0;
    }

    float const Dot = static_cast<float>((Nz * M.E[2][2] + Ny * M.E[1][2]) + Nx * M.E[0][2]);
    return BearingClampedAcos(Dot);
}
