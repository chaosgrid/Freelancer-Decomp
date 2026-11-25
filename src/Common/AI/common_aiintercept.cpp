// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll AI intercept / clearance queries (retail 0x062e4350-0x062e6200, 0x062e6a80,
// 0x062e7400): intercept range and clearance, heading cones, cached range, shape-biased
// avoid direction, turn-circle approach cost, docking lookup, GetIfacePositionOrZero.
// See src/include/common_aisteer.h for the "Elem" raw-layout note.

#include "common_aisteer.h"
#include "AI/common_aisteer_internal.h"
#include "x86math.h"
#include "common_csys.h"          // Acquire3DMathEngine, Matrix, Transform
#include "common_cobjdb.h"        // IObjDB::GetNext (FindObjectDockingAtBase)

#include <cmath>

using namespace CmnAiSteer;

// ============================================================================
// Intercept-clearance cluster. See common_aisteer.h for the shared "Elem"
// raw-layout note.
// ============================================================================

namespace
{
    // Shared body of FUN_062e4350 / FUN_062e4d00 when both CachedFlags have bit 0x30000:
    // distance between cached positions minus the sum of the cached radii, clamped to
    // >= 0, returned unrounded. Each RefreshPos is retail's inlined guard, re-checks
    // included.
    double ComputeCachedRadialClearance(FAiTarget* Elem, FAiTarget* Target)
    {
        Target->RefreshPos();
        Vector const TargetPos = Target->CachedPos;
        Target->RefreshPos();
        float const TargetRadius = Target->CachedRadius;

        Elem->RefreshPos();
        Vector const& ElemPos = Elem->CachedPos;

        double const Dx = (double)ElemPos.x - TargetPos.x;
        double const Dy = (double)ElemPos.y - TargetPos.y;
        double const Dz = (double)ElemPos.z - TargetPos.z;
        double const DistSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
        // 0x5f3759df fast length (0.5f DAT_0639c13c, 1.5f DAT_0639e6d0); the distance
        // is the only rounding.
        float const Dist = (float)InlineFastLength(DistSq);

        Elem->RefreshPos();
        // Unrounded; NaN is kept.
        double const Clearance = Dist - ((double)Elem->CachedRadius + TargetRadius);
        if (Clearance < 0.0)
            return 0.0;
        return Clearance;
    }
}

// FUN_062e4660, __cdecl, result in ST0 (double).
double EstimateLeadInterceptDistance(FAiTarget* Elem, IObjInspect const* TargetIface)
{
    FAiTarget* const Self = Elem;
    IObjInspect const* const Target = TargetIface;

    // Bounding-box corner cache, guarded by +0x7f (set before the calls). Retail uses
    // slot +0x10 (get_orientation), ICF-folded with get_transform; the callee reads a
    // whole Transform, so the port calls get_transform().
    if (Self->bCornersValid == 0)
    {
        Self->bCornersValid = 1;
        Vector Min;
        Vector Max;
        Self->Iface->get_archetype_extents(Min, Max);
        TransformBoundingBoxCorners(Self->Iface->get_transform(), Min, Max, Self->CachedCorners);
    }

    Vector const& ElemPos = Self->Iface->get_transform().Translation;
    Vector const& TargetPos = Target->get_position();

    Vector Dir{ TargetPos.x - ElemPos.x, TargetPos.y - ElemPos.y, TargetPos.z - ElemPos.z };

    // Normalise in place with the unrounded reciprocal (1.0f is DAT_0639a1d0).
    double const InvLen = 1.0f / std::sqrt((static_cast<double>(Dir.z) * Dir.z +
                                            static_cast<double>(Dir.y) * Dir.y) +
                                           static_cast<double>(Dir.x) * Dir.x);
    Dir.x = static_cast<float>(Dir.x * InvLen);
    Dir.y = static_cast<float>(Dir.y * InvLen);
    Dir.z = static_cast<float>(Dir.z * InvLen);

    Self->RefreshPos();

    // The ray starts on Elem's bounding sphere, facing the target.
    float const Radius = Self->CachedRadius;
    Vector const Offset{ Dir.x * Radius, Dir.y * Radius, Dir.z * Radius };
    Vector const RayOrigin{ ElemPos.x + Offset.x, ElemPos.y + Offset.y, ElemPos.z + Offset.z };

    // De-interleaved into the {MinX, MaxX, MinY, MaxY, MinZ, MaxZ} layout FUN_062dffd0
    // takes.
    Vector BoxMin;
    Vector BoxMax;
    Target->get_archetype_extents(BoxMin, BoxMax);
    float const Aabb[6] = { BoxMin.x, BoxMax.x, BoxMin.y, BoxMax.y, BoxMin.z, BoxMax.z };

    bool bStartsInside = false;
    Vector HitPoint;
    if (!RayTransformedBoxNearestHit(RayOrigin, Dir, Target->get_transform(), Aabb, &HitPoint, &bStartsInside))
    {
        // Miss: fast length of (Centre - ElemPos). The radius is fetched and never read.
        float UnusedRadius;
        Vector Centre;
        Target->get_physical_radius(UnusedRadius, Centre);
        double const Dx = static_cast<double>(Centre.x) - ElemPos.x;
        double const Dy = static_cast<double>(Centre.y) - ElemPos.y;
        double const Dz = static_cast<double>(Centre.z) - ElemPos.z;
        return InlineFastLength((Dz * Dz + Dy * Dy) + Dx * Dx);
    }

    // Hit: 0.0f (DAT_06399408) when the ray origin is already inside the box, else the
    // fast length of (HitPoint - RayOrigin).
    double const Dx = static_cast<double>(HitPoint.x) - RayOrigin.x;
    double const Dy = static_cast<double>(HitPoint.y) - RayOrigin.y;
    double const Dz = static_cast<double>(HitPoint.z) - RayOrigin.z;
    double const HitDistSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
    if (bStartsInside)
        return 0.0;
    return InlineFastLength(HitDistSq);
}

// FUN_062e4350.
bool IsWithinInterceptRange(FAiTarget* Elem, FAiTarget* Target, float Threshold)
{
    // FAiTarget::GetFlags inlined twice by retail; the target's flags are read only
    // when Elem's have 0x30000.
    bool HaveBoth = false;
    if ((Elem->GetFlags() & 0x30000) != 0)
        HaveBoth = (Target->GetFlags() & 0x30000) != 0;

    // Unrounded clearance; less or equal is in range, unordered is not.
    double const Clearance = HaveBoth
        ? ComputeCachedRadialClearance(Elem, Target)
        : EstimateLeadInterceptDistance(Elem, Target->Iface);
    return Clearance <= Threshold;
}

// FUN_062e4d00. IsWithinInterceptRange minus the final threshold compare.
double GetInterceptClearance(FAiTarget* Elem, FAiTarget* Target)
{
    bool HaveBoth = false;
    if ((Elem->GetFlags() & 0x30000) != 0)
        HaveBoth = (Target->GetFlags() & 0x30000) != 0;

    return HaveBoth
        ? ComputeCachedRadialClearance(Elem, Target)
        : EstimateLeadInterceptDistance(Elem, Target->Iface);
}

// FUN_062e4500 (Ghidra misses all three parameters). The overload of
// IsWithinInterceptRange for callers holding only the target's interface: the
// target's flags, position and radius are fetched fresh every call (no cache byte),
// and the direct path is also taken when the target's flags have any of 0x207 set,
// regardless of Elem's own 0x30000 bit.
bool IsWithinInterceptRangeOfIface(FAiTarget* Elem, IObjInspect const* TargetIface, float Threshold)
{
    unsigned TargetFlags = 0;
    TargetIface->get_type(TargetFlags);

    bool const ElemHasFlag = (Elem->GetFlags() & 0x30000) != 0;

    bool const UseCached = (ElemHasFlag && (TargetFlags & 0x30000) != 0) || (TargetFlags & 0x207) != 0;

    // Unrounded from here to the final compare.
    double Clearance;
    if (UseCached)
    {
        // Retail leaves both uninitialised; the zeroes only matter if the callee
        // writes nothing.
        float TargetRadius = 0.0f;
        Vector TargetPos{};
        TargetIface->get_physical_radius(TargetRadius, TargetPos);

        Elem->RefreshPos();
        Vector const& ElemPos = Elem->CachedPos;

        double const Dx = (double)ElemPos.x - TargetPos.x;
        double const Dy = (double)ElemPos.y - TargetPos.y;
        double const Dz = (double)ElemPos.z - TargetPos.z;
        double const DistSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
        float const Dist = (float)InlineFastLength(DistSq);

        // Retail re-checks Elem's guard here.
        Elem->RefreshPos();

        // NaN is kept.
        Clearance = Dist - ((double)Elem->CachedRadius + TargetRadius);
        if (Clearance < 0.0)
            Clearance = 0.0;
    }
    else
    {
        Clearance = EstimateLeadInterceptDistance(Elem, TargetIface);
    }

    // Less or equal is in range, unordered is not.
    return Clearance <= Threshold;
}

// FUN_062e4bb0, __cdecl (Elem, TargetIface). Ghidra cannot assign either parameter
// because both home slots are reused as float scratch. IsWithinInterceptRangeOfIface
// minus the final compare: the direct path returns the clearance clamped at zero,
// the fallback returns EstimateLeadInterceptDistance unclamped.
double GetInterceptClearanceOfIface(FAiTarget* Elem, IObjInspect const* TargetIface)
{
    unsigned TargetFlags = 0;
    TargetIface->get_type(TargetFlags);

    bool const ElemHasFlag = (Elem->GetFlags() & 0x30000) != 0;
    bool const UseCached = (ElemHasFlag && (TargetFlags & 0x30000) != 0) || (TargetFlags & 0x207) != 0;

    if (!UseCached)
    {
        // Not clamped in retail.
        return EstimateLeadInterceptDistance(Elem, TargetIface);
    }

    float TargetRadius = 0.0f;
    Vector TargetPos{};
    TargetIface->get_physical_radius(TargetRadius, TargetPos);

    Elem->RefreshPos();
    Vector const& ElemPos = Elem->CachedPos;

    double const Dx = (double)ElemPos.x - TargetPos.x;
    double const Dy = (double)ElemPos.y - TargetPos.y;
    double const Dz = (double)ElemPos.z - TargetPos.z;
    double const DistSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
    float const Dist = (float)InlineFastLength(DistSq);

    // Retail re-checks Elem's guard here.
    Elem->RefreshPos();

    // Unrounded; NaN is returned as is.
    double const Clearance = Dist - ((double)Elem->CachedRadius + TargetRadius);
    if (Clearance < 0.0)
        return 0.0;
    return Clearance;
}

// FUN_062e53e0, __cdecl, three arguments. See common_aisteer.h for the predicate's
// meaning.
bool IsPointOutsideHeadingCone(Vector const& Point, IObjInspect const* Iface,
                                          float CosThreshold)
{
    // Retail passes the param_2 home slot as the scratch Radius.
    Vector Centre{ 0.0f, 0.0f, 0.0f };
    if (Iface != nullptr)
    {
        float Radius = 0.0f;
        if (Iface->get_physical_radius(Radius, Centre) != 0)
        {
            // Retail quirk: a non-zero return calls get_id(), discards it, and uses the
            // origin as the centre, as for a null Iface. Only a zero return uses the
            // filled centre.
            (void)Iface->get_id();
            Centre = Vector{ 0.0f, 0.0f, 0.0f };
        }
    }

    // From the iface's centre to Point.
    Vector Dir{ Point.x - Centre.x, Point.y - Centre.y, Point.z - Centre.z };

    // Unrounded length against 0.01f (DAT_0639e6b8); not-greater zeroes the direction.
    double const Length = std::sqrt(((double)Dir.z * Dir.z + (double)Dir.y * Dir.y) + (double)Dir.x * Dir.x);
    if (Length > 0.01f)
    {
        // Unrounded reciprocal; only the products round.
        double const Inv = 1.0 / Length;
        Dir.x = (float)(Dir.x * Inv);
        Dir.y = (float)(Dir.y * Inv);
        Dir.z = (float)(Dir.z * Inv);
    }
    else
    {
        Dir = Vector{ 0.0f, 0.0f, 0.0f };
    }

    Vector Heading{ 0.0f, 0.0f, 0.0f };
    if (Iface != nullptr)
    {
        Vector const Col2 = MatrixColumn2(Iface->get_transform().Rotation);
        Vec3Assign(&Heading, -Col2.x, -Col2.y, -Col2.z);
    }

    // Unrounded dot clamped into [-1, 1] (DAT_0639a1d0 / DAT_06399404).
    double const Dot = ClampCos11(DotZyx(Dir, Heading));

    // Only strictly less returns true.
    return Dot < CosThreshold;
}

// FUN_062e5190, __cdecl, three arguments (Ghidra lists two; every call site cleans
// 0xc, e.g. 062d9384). The two-object sibling of IsPointOutsideHeadingCone: same
// clamp and final compare, but the point is Elem's cached centre, the cone belongs
// to TargetIface, and the two headings must also agree (dot > 0).
bool IsAlignedAndOutsideHeadingCone(FAiTarget* Elem, IObjInspect const* TargetIface, float CosThreshold)
{
    IObjInspect const* const Target = TargetIface;

    Vector TargetHeading{ 0.0f, 0.0f, 0.0f };
    if (Target != nullptr)
    {
        Vector const Col2 = MatrixColumn2(Target->get_transform().Rotation);
        VecNegate3(&Col2, &TargetHeading);
    }

    Vector OwnerHeading{ 0.0f, 0.0f, 0.0f };
    {
        Vector const Col2 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
        VecNegate3(&Col2, &OwnerHeading);
    }

    // Rounded once to float.
    float const HeadingAlignment = (float)DotZyx(TargetHeading, OwnerHeading);

    // Same quirk as IsPointOutsideHeadingCone: only a zero return uses the filled
    // centre; a non-zero return calls get_id() and keeps the origin.
    Vector TargetCentre{ 0.0f, 0.0f, 0.0f };
    if (Target != nullptr)
    {
        float Radius = 0.0f;
        Vector Centre{ 0.0f, 0.0f, 0.0f };
        if (Target->get_physical_radius(Radius, Centre) == 0)
        {
            TargetCentre = Centre;
        }
        else
        {
            (void)Target->get_id();
        }
    }

    Elem->RefreshPos();
    Vector const& ElemCentre = Elem->CachedPos;

    // From the target to Elem.
    Vector Dir{ ElemCentre.x - TargetCentre.x,
                ElemCentre.y - TargetCentre.y,
                ElemCentre.z - TargetCentre.z };
    // The unrounded length is compared against 0.01f (DAT_0639e6b8); FUN_062dc410
    // gets a rounded copy.
    double const Distance = std::sqrt(((double)Dir.z * Dir.z + (double)Dir.y * Dir.y) + (double)Dir.x * Dir.x);
    if (Distance > 0.01f)
    {
        VecNormalizeInPlaceByLength3(&Dir, (float)Distance);
    }
    else
    {
        Vec3Assign(&Dir, 0.0f, 0.0f, 0.0f);
    }

    // Retail rebuilds TargetIface's heading a second time rather than reusing it.
    Vector Heading{ 0.0f, 0.0f, 0.0f };
    if (Target != nullptr)
    {
        Vector const Col2 = MatrixColumn2(Target->get_transform().Rotation);
        VecNegate3(&Col2, &Heading);
    }
    else
    {
        Vec3Assign(&Heading, 0.0f, 0.0f, 0.0f);
    }

    double const Dot = ClampCos11(DotZyx(Dir, Heading));

    // Both compares fail on unordered.
    if (!(HeadingAlignment > 0.0f))
    {
        return false;
    }
    return Dot < CosThreshold;
}

// FUN_062e4ea0.
double GetSignedRadialClearanceToPoint(FAiTarget* Elem, Vector const& Point)
{
    Elem->RefreshPos();
    // Sum order (dx*dx + dz*dz) + dy*dy; only the distance is rounded.
    Vector const& ElemPos = Elem->CachedPos;
    double const Dx = (double)ElemPos.x - Point.x;
    double const Dy = (double)ElemPos.y - Point.y;
    double const Dz = (double)ElemPos.z - Point.z;
    float const Dist = (float)std::sqrt((Dx * Dx + Dz * Dz) + Dy * Dy);

    // Retail re-checks the guard here.
    Elem->RefreshPos();
    return (double)Dist - Elem->CachedRadius;
}

// FUN_062e4f60. See common_aisteer.h's declaration for the operand order; names
// follow the call-site order, not retail's ESI/EDI assignment.
bool IsTargetCoAlignedAndAhead(FAiTarget* Target, FAiTarget* Reference, float CosThreshold)
{
    // Dot of the two forward axes, rounded once. Retail negates both first
    // (VecNegate3); (-a)*(-b) == a*b exactly, so the port drops the negations.
    Vector const RefForward = MatrixColumn2(Reference->Iface->get_transform().Rotation);
    Vector const TargetForward = MatrixColumn2(Target->Iface->get_transform().Rotation);
    float const HeadingDot = (float)DotZyx(RefForward, TargetForward);

    // Reference first, then Target.
    if (Reference->bPosValid == 0)
    {
        Reference->bPosValid = 1;
        Reference->Iface->get_physical_radius(Reference->CachedRadius, Reference->CachedPos);
    }
    if (Target->bPosValid == 0)
    {
        Target->bPosValid = 1;
        Target->Iface->get_physical_radius(Target->CachedRadius, Target->CachedPos);
    }

    Vector Direction;
    Direction.x = Target->CachedPos.x - Reference->CachedPos.x;
    Direction.y = Target->CachedPos.y - Reference->CachedPos.y;
    Direction.z = Target->CachedPos.z - Reference->CachedPos.z;

    // A real sqrt here, not the fast-length idiom; compared unrounded against 0.01f
    // (.rdata:0639e6b8), not-greater zeroes. Only the normalised components round.
    double const Length = std::sqrt(((double)Direction.z * Direction.z +
                                     (double)Direction.y * Direction.y) +
                                    (double)Direction.x * Direction.x);
    if (Length > 0.01f)
    {
        double const InvLength = 1.0 / Length;
        Direction.x = (float)(Direction.x * InvLength);
        Direction.y = (float)(Direction.y * InvLength);
        Direction.z = (float)(Direction.z * InvLength);
    }
    else
    {
        Direction.x = 0.0f;
        Direction.y = 0.0f;
        Direction.z = 0.0f;
    }

    // Retail re-fetches the reference's forward axis instead of reusing it; the
    // cosine stays unrounded through the clamp and compare.
    Vector const RefForwardAgain = MatrixColumn2(Reference->Iface->get_transform().Rotation);
    Vector const RefHeading{ -RefForwardAgain.x, -RefForwardAgain.y, -RefForwardAgain.z };
    double const Cosine = ClampCos11(DotZyx(Direction, RefHeading));

    // Strictly greater than 0.0f (.rdata:06399408).
    if (!(HeadingDot > 0.0f))
    {
        return false;
    }
    return Cosine < CosThreshold;
}

// FUN_062e6a80. Catalogued with the IUStateGraph cluster (see common_aisteer.h), but
// it is a lazily cached accessor on the same Elem record, so it lives here.
void GetCachedRange(FAiTarget* Elem, float* OutLow, float* OutHigh)
{
    if (Elem->bAttitudeValid == 0)
    {
        IObjInspect* const RefIface = Elem->AttitudeRefIface;
        IObjInspect* const OtherIface = Elem->Iface;

        Elem->CachedAttitude = 0.0f;
        Elem->CachedReverseAttitude = 0.0f;

        if (RefIface != nullptr && OtherIface != nullptr)
        {
            // Slot 27; 0 on success.
            int const Result = RefIface->get_attitude_towards_symmetrical(
                Elem->CachedAttitude, OtherIface, Elem->CachedReverseAttitude);
            if (Result != 0)
            {
                // Retail re-zeroes both on failure (redundant, kept).
                Elem->CachedAttitude = 0.0f;
                Elem->CachedReverseAttitude = 0.0f;
            }
        }

        Elem->bAttitudeValid = 1; // regardless of success/failure
    }

    *OutLow = Elem->CachedAttitude;
    *OutHigh = Elem->CachedReverseAttitude;
}

// FUN_062e6150. Minus column 2 (the forward axis) of the iface's transform; zero on
// null.
void GetNegatedIfaceDirection(Vector* OutDir, IObjInspect const* Iface)
{
    if (Iface != nullptr)
    {
        Matrix const& Basis = Iface->get_transform().Rotation;
        OutDir->x = -Basis.E[0][2];
        OutDir->y = -Basis.E[1][2];
        OutDir->z = -Basis.E[2][2];
        return;
    }
    *OutDir = Vector{ 0.0f, 0.0f, 0.0f };
}

// FUN_062e61a0. The speed of the iface, returned unrounded.
double GetIfaceVectorMagnitude(IObjInspect const* Iface)
{
    if (Iface == nullptr)
        return 0.0f;

    Vector const V = Iface->get_velocity();
    double const X = V.x;
    double const Y = V.y;
    double const Z = V.z;
    return std::sqrt((X * X + Y * Y) + Z * Z);
}

// FUN_062e4910 (Ghidra misses parameters; see common_aisteer.h). Retail unrolls eight
// blocks, one per corner of [Min, Max] in 3-bit order (X high, Z low), each transformed
// by the math engine's TransformXform (+0x28). Each block re-acquires the engine
// (DAT_063fca48), hence the call inside the loop.
void TransformBoundingBoxCorners(Transform const& Xform, Vector const& Min,
                                            Vector const& Max, Vector* OutCorners)
{
    for (int Index = 0; Index < 8; ++Index)
    {
        Vector const Corner{ ((Index & 4) != 0) ? Max.x : Min.x,
                             ((Index & 2) != 0) ? Max.y : Min.y,
                             ((Index & 1) != 0) ? Max.z : Min.z };

        Vector Transformed;
        Acquire3DMathEngine()->TransformXform(Transformed, Xform, Corner);
        OutCorners[Index] = Transformed;
    }
}

// FUN_062e55b0 (Ghidra's corruptions are false positives; see common_aisteer.h).
// A dodge direction around Target, biased by the obstacle's shape and returned in
// world space.
Vector* ComputeShapeBiasedAvoidDirection(Vector* Out, FAiTarget* Elem, IObjInspect* Target)
{
    // Zeroed before the null test.
    Vector Direction{ 0.0f, 0.0f, 0.0f };

    if (Target == nullptr)
    {
        *Out = Direction;
        return Out;
    }

    // The radius is written and never read; its slot is not pre-zeroed.
    float Radius;
    Vector TargetCentre;
    Target->get_physical_radius(Radius, TargetCentre);

    // The target centre in Elem's local space (InverseTransform, +0x3c).
    Vector LocalPos;
    Acquire3DMathEngine()->InverseTransform(LocalPos, Elem->Iface->get_transform(), TargetCentre);

    // Fast length of the unrounded squared length against the double 1e-5
    // (DAT_0639f418); only "less" answers (1, 0, 0).
    float const WorkX = LocalPos.x;
    float WorkY = LocalPos.y;
    float WorkZ = LocalPos.z;
    double const LocalLenSq = (static_cast<double>(LocalPos.x) * LocalPos.x +
                               static_cast<double>(LocalPos.y) * LocalPos.y) +
                              static_cast<double>(LocalPos.z) * LocalPos.z;
    if (InlineFastLength(LocalLenSq) < 1e-5)
    {
        Out->x = 1.0f;
        Out->y = 0.0f;
        Out->z = 0.0f;
        return Out;
    }

    // In double: flat = H / V > 2.0 (DAT_0639c420), tall = V / H > 2.0f (DAT_063a0498).
    Vector ExtentsMin;
    Vector ExtentsMax;
    Target->get_archetype_extents(ExtentsMin, ExtentsMax);
    double const HorizontalHalf = ((static_cast<double>(ExtentsMax.z) - ExtentsMin.z) +
                                   (static_cast<double>(ExtentsMax.x) - ExtentsMin.x)) * 0.5;
    double const VerticalExtent = static_cast<double>(ExtentsMax.y) - ExtentsMin.y;
    bool const bFlatObstacle = (HorizontalHalf / VerticalExtent) > 2.0;
    bool const bTallObstacle = (VerticalExtent / HorizontalHalf) > 2.0;

    // Flat: damp x by 0.3f (DAT_063a0788) and renormalise; Axial stays unrounded, y and
    // z use float(Inv).
    double Axial;
    if (bFlatObstacle)
    {
        double const Scaled = static_cast<double>(LocalPos.x) * 0.3f;
        double const Inv = 1.0 / std::sqrt((static_cast<double>(WorkZ) * WorkZ + static_cast<double>(WorkY) * WorkY) +
                                           Scaled * Scaled);
        float const InvF = static_cast<float>(Inv);
        Axial = Scaled * Inv;
        WorkY = static_cast<float>(static_cast<double>(InvF) * WorkY);
        WorkZ = static_cast<float>(static_cast<double>(WorkZ) * InvF);
    }
    else
    {
        Axial = WorkX;
    }

    // Tall: damp y by 0.3f and renormalise; z is not written.
    if (bTallObstacle)
    {
        double const Scaled = static_cast<double>(WorkY) * 0.3f;
        double const Inv = 1.0 / std::sqrt((static_cast<double>(WorkZ) * WorkZ + Scaled * Scaled) + Axial * Axial);
        Axial = Axial * Inv;
        WorkY = static_cast<float>(Inv * Scaled);
    }

    // The local dodge direction: unit (-Axial, -W.y, 0); x uses the unrounded Inv2, y
    // and z float(Inv2) (z = Inv2F * 0.0f, DAT_06399408).
    double const NegAxial = -Axial;
    double const NegY = -static_cast<double>(WorkY);
    double const Inv2 = 1.0 / std::sqrt(NegY * NegY + NegAxial * NegAxial);
    float const Inv2F = static_cast<float>(Inv2);
    Direction.x = static_cast<float>(Inv2 * NegAxial);
    Direction.y = static_cast<float>(NegY * Inv2F);
    Direction.z = static_cast<float>(static_cast<double>(Inv2F) * 0.0f);

    // Back to world space (Rotate, +0x30).
    Vector World;
    Acquire3DMathEngine()->Rotate(World, Elem->Iface->get_transform(), Direction);
    *Out = World;
    return Out;
}

// FUN_062e5c00 (Ghidra's corruptions are false positives; see common_aisteer.h). The
// result is returned unrounded (the MoveIdea caller compares it before storing).
double EstimateTurnCircleApproachCost(IObjInspect* Iface, Vector const& Point,
                                      float AngularSpeed, float LinearSpeed)
{
    // 1.0f is the early-exit result. The throttle is written and never read.
    float const DefaultResult = 1.0f;
    float Throttle = 1.0f;
    Iface->get_throttle(Throttle);

    float Radius = 0.0f;
    Vector Center;
    Iface->get_physical_radius(Radius, Center);

    // The transform with its translation replaced by the centre.
    Transform Frame = Iface->get_transform();
    Frame.Translation = Center;
    Vector ToPoint;
    ToPoint.x = Point.x - Frame.Translation.x;
    ToPoint.y = Point.y - Frame.Translation.y;
    ToPoint.z = Point.z - Frame.Translation.z;

    // A rounded copy of LenSq is kept; the fast length uses the unrounded sum.
    double const LenSq = (static_cast<double>(ToPoint.z) * ToPoint.z + static_cast<double>(ToPoint.y) * ToPoint.y) +
                         static_cast<double>(ToPoint.x) * ToPoint.x;
    float const LenSqF = static_cast<float>(LenSq);
    double const Dist = InlineFastLength(LenSq);

    // MaxRange = max(Radius * 5.0f, 100.0) unrounded; Dist must lie strictly inside
    // (0.01f, MaxRange), else 1.0f.
    double MaxRange = static_cast<double>(Radius) * 5.0f;
    if (!(MaxRange > 100.0))
        MaxRange = 100.0;
    if (!(Dist > static_cast<double>(0.01f)))
        return DefaultResult;
    if (!(Dist < MaxRange))
        return DefaultResult;

    // Normalise by 1 / sqrt(rounded LenSq), kept unrounded.
    double const Inv = 1.0 / std::sqrt(static_cast<double>(LenSqF));
    ToPoint.x = static_cast<float>(ToPoint.x * Inv);
    ToPoint.y = static_cast<float>(ToPoint.y * Inv);
    ToPoint.z = static_cast<float>(ToPoint.z * Inv);

    // A zero speed argument is refilled from the iface (slots +0xf0 / +0xd8).
    if (AngularSpeed == 0.0f)
        AngularSpeed = Iface->get_angular_speed_XY(0.0f, 1.0f);
    if (LinearSpeed == 0.0f)
        LinearSpeed = Iface->get_speed(1.0f, ThrustEquipEngine);

    // The unrounded quotient must exceed 0.01f.
    double const TurnRadiusD = static_cast<double>(LinearSpeed) / AngularSpeed;
    float const TurnRadius = static_cast<float>(TurnRadiusD);
    if (!(TurnRadiusD > static_cast<double>(0.01f)))
        return DefaultResult;

    // The two turn-circle centres, +/-TurnRadius along the local x axis.
    Vector const OffsetRight{ TurnRadius, 0.0f, 0.0f };
    Vector RightCentre;
    Acquire3DMathEngine()->TransformXform(RightCentre, Frame, OffsetRight);
    Vector const OffsetLeft{ -TurnRadius, 0.0f, 0.0f };
    Vector LeftCentre;
    Acquire3DMathEngine()->TransformXform(LeftCentre, Frame, OffsetLeft);

    // The right distance is rounded to float; the left one stays unrounded.
    Vector ToRight;
    ToRight.x = Point.x - RightCentre.x;
    ToRight.y = Point.y - RightCentre.y;
    ToRight.z = Point.z - RightCentre.z;
    float const DistRight = static_cast<float>(InlineFastLength(
        (static_cast<double>(ToRight.z) * ToRight.z + static_cast<double>(ToRight.y) * ToRight.y) +
        static_cast<double>(ToRight.x) * ToRight.x));

    Vector ToLeft;
    ToLeft.x = Point.x - LeftCentre.x;
    ToLeft.y = Point.y - LeftCentre.y;
    ToLeft.z = Point.z - LeftCentre.z;
    double const DistLeft = InlineFastLength(
        (static_cast<double>(ToLeft.z) * ToLeft.z + static_cast<double>(ToLeft.y) * ToLeft.y) +
        static_cast<double>(ToLeft.x) * ToLeft.x);

    // Unordered keeps the left distance. Inside a turn circle (strictly less than the
    // rounded TurnRadius) the approach costs extra; otherwise 1.0f.
    double const MinCentreDist = (static_cast<double>(DistRight) < DistLeft) ? DistRight : DistLeft;
    if (!(MinCentreDist < static_cast<double>(TurnRadius)))
        return DefaultResult;

    float const Scaled = static_cast<float>(MinCentreDist * AngularSpeed);

    // Facing is clamped at 0 only when strictly negative.
    Vector Forward;
    Forward.x = -Frame.Rotation.E[0][2];
    Forward.y = -Frame.Rotation.E[1][2];
    Forward.z = -Frame.Rotation.E[2][2];
    double const FacingD = (static_cast<double>(ToPoint.z) * Forward.z + static_cast<double>(ToPoint.y) * Forward.y) +
                           static_cast<double>(Forward.x) * ToPoint.x;
    float Facing = static_cast<float>(FacingD);
    if (FacingD < 0.0)
        Facing = 0.0f;

    // _CIpow(Facing, 2.0) (DAT_0639c420), unrounded.
    return std::pow(static_cast<double>(Facing), 2.0) * Scaled / LinearSpeed;
}

// FUN_062e6200. The null guard is retail's. Retail reuses its argument slot as the
// out buffer (not pre-zeroed); failure returns 0 either way, so the zero-init is
// equivalent.
unsigned GetObjectTypeOrZero(IObjInspect const* Iface)
{
    if (Iface == nullptr)
        return 0;

    unsigned Type = 0;
    if (Iface->get_type(Type) != 0) // vtable +0x88, nonzero == failure
        return 0;
    return Type;
}

// FUN_062e7400, __thiscall, RET 4. ECX is SystemManager+0x34 (the timer table), whose
// first dword is its IObjDB*.
IObjRW* SystemManagerTimerTable::FindObjectDockingAtBase(IObjInspect const* BaseIface) const
{
    if (BaseIface == nullptr)
        return nullptr;

    // OBJ_MOON | OBJ_PLANET.
    if ((GetObjectTypeOrZero(BaseIface) & 0x3u) == 0)
        return nullptr;

    unsigned BaseId = 0;
    BaseIface->get_base(BaseId); // vtable +0x8c; retail ignores the result

    // The 3-dword {cursor, cursor, TypeMask} walk state IObjDB's slot 0 owns
    // and updates in place. Identical layout to ScanList::add_in_range's own
    // FIterState (common_cescanner.cpp) and CAsteroidSystem::update's.
    struct FObjDbIterState
    {
        unsigned Cursor0;
        unsigned Cursor1;
        unsigned TypeMask;
    } IterState = { 0, 0, 0x20u }; // OBJ_DOCKING_RING

    IObjDB* const ObjDb = ObjectDb;

    IObjRW* Candidate = ObjDb->GetNext(&IterState);
    while (Candidate != nullptr)
    {
        unsigned DockTarget = 0; // retail re-zeroes this slot before every call
        Candidate->get_dock_target(DockTarget);
        if (DockTarget == BaseId)
            return Candidate;

        Candidate = ObjDb->GetNext(&IterState);
    }
    return nullptr;
}

// FUN_062e60e0. See common_aisteer.h.
void GetIfacePositionOrZero(Vector* OutPos, IObjInspect const* Iface)
{
    if (Iface != nullptr)
    {
        // Writes the object's physical centre (get_physical_radius), not its position;
        // the radius is discarded. A non-zero status calls get_id() and returns zero.
        float UnusedRadius = 0.0f;
        int const Status = Iface->get_physical_radius(UnusedRadius, *OutPos);
        if (Status == 0)
            return;
        (void)Iface->get_id();
    }
    *OutPos = Vector{ 0.0f, 0.0f, 0.0f };
}
