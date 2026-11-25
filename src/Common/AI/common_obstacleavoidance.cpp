// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll AI obstacle avoidance: ObstacleAvoidance (PathfindManager+0x268), the
// obstacle-candidate / heading-blend helpers and FindClosestObstacleToAvoid (FUN_062e7a30).
// See src/include/common_aisteer.h for the subsystem writeup.

#include "common_aisteer.h"
#include "AI/common_aisteer_internal.h"
#include "x86math.h"
#include "common_csys.h"          // Acquire3DMathEngine, Matrix
#include "common_physys.h"        // PhySys::FindRayCollisions, RayHit, Quaternion
#include "common_misc.h"          // FastInvSqrt, SUBOBJ_ID_NONE
#include "common_cequip.h"
#include "common_cobject.h"       // CObject::ClassShip, CShip
#include "common_behavior.h"      // Behavior_* helpers
#include "common_systemmanager.h" // SystemManager, SystemManagerTimerTable
#include "common_pathfind.h"      // PathfindManager (embeds ObstacleAvoidance at +0x268)
#include "common_cobjdb.h"        // IObjDB
#include "common_spatial.h"       // ISpatialPartition, FSpatialQueryResult (ClassifyShipObstaclePhase)
#include "x87_trig.h"             // retail's inline FSIN / FCOS

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

using namespace CmnAiSteer;

namespace
{
    // DAT_063fc9a0 / DAT_063fca14. A 25-dword all-zero .data table that FUN_062de490 shuffles
    // once and nothing ever reads; the only observable effect is the 50 rand() calls of the
    // first tick.
    unsigned GAvoidIndexTable[25] = {};
    unsigned GAvoidIndexTableShuffled = 0;

    // DAT_063ec6a8, a writable .data float (2.0f), read once by FUN_062de490.
    float GAvoidTurnGain = 2.0f;
}

// ============================================================================
// ObstacleAvoidance -- construction and owner bind (PathfindManager+0x268)
// ============================================================================

ObstacleAvoidance::ObstacleAvoidance()
{
    // Inlined into FUN_062db3b0; the 25 slots (0x1c bytes, from this+0x14) are built by the
    // vector constructor iterator, whose element ctor (0x062db770) only zeroes Timer.
    for (int Index = 0; Index < 25; ++Index)
    {
        Slots[Index].Timer = 0.0f;
    }
    CursorIndex = 0;
    ResetContact();
}

void ObstacleAvoidance::ResetContact()
{
    // FUN_062db780, ECX = this+0x2d4.
    Active = 0;
    HasContact = 0;
    ContactObjectId = 0;
    ContactCloseRate = 0.0f;
    ContactTimer = 0.0f;
}

void ObstacleAvoidance::AttachOwner(IBehaviorManager* NewOwner)
{
    // FUN_062de220. Breadth takes SpanY on equal or unordered spans.
    Owner = NewOwner;
    ElapsedTime = 0.0f;

    Vector Min;
    Vector Max;
    PathfindOwnerObject(NewOwner)->get_archetype_extents(Min, Max);

    NoseDistance = -Min.z;

    float const SpanX = std::fabs(Max.x - Min.x);
    float const SpanY = std::fabs(Max.y - Min.y);
    Breadth = (SpanX > SpanY) ? SpanX : SpanY;
}

float ObstacleAvoidance::GetCloseRateScale() const
{
    // FUN_062df2d0
    if (Active == 0)
    {
        return 1.0f;
    }
    // Retail tests both `W == 0` and `W - 8 == 0`.
    if (Owner->WatchablePtr == nullptr || ObjRwFromWatchable(Owner->WatchablePtr) == nullptr)
    {
        return 1.0f;
    }
    if (ContactCloseRate > 5.0) // double DAT_0639fa58
    {
        return 0.8f;
    }
    if (ContactCloseRate < 1.0)
    {
        return 0.4f;
    }
    // _CIpow with the double 2.0.
    double const Excess = ((double)(ContactCloseRate) - 1.0) * 0.25;
    return (float)(std::pow(Excess, 2.0) * 0.4f + 0.4f);
}

bool ObstacleAvoidance::GetAvoidDirection(Vector* OutDirection, unsigned* OutObjectId) const
{
    // FUN_062df360
    if (Active == 0)
    {
        return false;
    }
    *OutDirection = AvoidDirection;
    *OutObjectId = ContactObjectId;
    return true;
}

bool ObstacleAvoidance::GetAvoidPoint(Vector* OutPoint, unsigned* OutObjectId) const
{
    // FUN_062df3a0
    if (Active == 0)
    {
        return false;
    }
    *OutPoint = AvoidPoint;
    *OutObjectId = ContactObjectId;
    return true;
}

void ObstacleAvoidance::Update(float Dt)
{
    // FUN_062de490. Values retail keeps on the x87 stack are held in double; a float local is
    // a retail `FSTP dword`.

    // The watched object (0 when nothing is watched); retail dereferences it without a test.
    IObjRW* const Ship = PathfindOwnerObject(Owner);

    // Age the slot timers; the unrounded difference is compared with 0 for the clamp.
    for (int Index = 0; Index < 25; ++Index)
    {
        Slot& Entry = Slots[Index];
        if (Entry.Timer > 0.0f)
        {
            double const Left = (double)(Entry.Timer) - Dt;
            Entry.Timer = (float)(Left);
            if (Left < 0.0)
            {
                Entry.Timer = 0.0f;
            }
        }
    }

    ElapsedTime = Dt + ElapsedTime;
    {
        double const Left = (double)(ContactTimer) - Dt;
        ContactTimer = (float)(Left);
        if (Left < 0.0)                              // double 0.0 (DAT_0639c430)
        {
            ContactTimer = 0.0f;
        }
    }

    if (PhysSystemId == 0)
    {
        ContactObjectId = 0;
        ContactCloseRate = 0.0f;
        ContactTimer = 0.0f;
        Active = 0;
        HasContact = 0;
        return;
    }

    // The fast speed is kept as a float for later; the unrounded length is compared with 1.0f
    // (unordered continues).
    Vector const Velocity = Ship->get_velocity();
    double const SpeedExt = InlineFastLength((double)(Velocity.x) * Velocity.x
                                             + (double)(Velocity.y) * Velocity.y
                                             + (double)(Velocity.z) * Velocity.z);
    float const Speed = (float)(SpeedExt);
    if (SpeedExt < 1.0)
    {
        ContactObjectId = 0;
        ContactCloseRate = 0.0f;
        ContactTimer = 0.0f;
        Active = 0;
        HasContact = 0;
        return;
    }

    // 2*pi is double DAT_0639c9e0.
    float const SpiralAngle = (float)(
        std::fmod((double)(6.89f) * ElapsedTime, 6.2831854820251465));

    // Retail calls slot 4 (get_orientation), which shares its body with get_transform
    // (0x062bf1f0), and reads the whole Transform off the result.
    Transform const& ShipXform = Ship->get_transform();

    // Forward = -column 2 of the rotation (retail builds it twice, with equal values).
    Vector const Forward{ -ShipXform.Rotation.E[0][2], -ShipXform.Rotation.E[1][2],
                          -ShipXform.Rotation.E[2][2] };

    // FUN_06288830 then FUN_062887d0, each component a float.
    Vector const NoseOffset{ Forward.x * NoseDistance, Forward.y * NoseDistance, Forward.z * NoseDistance };
    Vector const Nose{ ShipXform.Translation.x + NoseOffset.x, ShipXform.Translation.y + NoseOffset.y,
                       ShipXform.Translation.z + NoseOffset.z };

    Matrix const Rotation = ShipXform.Rotation;

    // The unrounded product is compared with 0; only "equal" bails.
    double const LookAheadExt = 50.0f * (double)(NoseDistance);
    float LookAhead = (float)(LookAheadExt);
    if (LookAheadExt == 0.0)
    {
        Active = 0;
        HasContact = 0;
        ContactObjectId = 0;
        ContactCloseRate = 0.0f;
        ContactTimer = 0.0f;
        return;
    }

    if (LookAhead > 750.0f)
    {
        LookAhead = 750.0f;
    }

    // Probe spiral in ship space; the radius stays unrounded.
    double const SpiralRadius = 900.0f / (double)(LookAhead) * Breadth;
    Vector const ProbeLocal{ (float)(X87Cos((double)(SpiralAngle)) * SpiralRadius),
                             (float)(X87Sin((double)(SpiralAngle)) * SpiralRadius),
                             -900.0f };

    // DAT_063fca10 is the function's cached math engine.
    I3DMathEngine* const Math = Acquire3DMathEngine();
    Vector ProbeOffset;
    Math->TransformMatrix(ProbeOffset, Rotation, ProbeLocal);
    Vector const ProbeEnd{ Nose.x + ProbeOffset.x, Nose.y + ProbeOffset.y, Nose.z + ProbeOffset.z };

    PhySys::RayHit Hits[8];
    int const HitCount = PhySys::FindRayCollisions(PhysSystemId, Nose, ProbeEnd, Hits, 8);

    // Hits of the ship's own type are ignored below. Retail leaves OwnType uninitialised;
    // get_type stores 0 into it first on every path, so the zero here changes nothing.
    unsigned OwnType = 0;
    Ship->get_type(OwnType);

    // One-time XOR shuffle of the all-zero, never-read table.
    if (GAvoidIndexTableShuffled == 0)
    {
        for (int Round = 0; Round < 25; ++Round)
        {
            int const A = (std::rand() * 25) >> 15;
            int const B = (std::rand() * 25) >> 15;
            GAvoidIndexTable[A] ^= GAvoidIndexTable[B];
            GAvoidIndexTable[B] ^= GAvoidIndexTable[A];
            GAvoidIndexTable[A] ^= GAvoidIndexTable[B];
        }
        GAvoidIndexTableShuffled = 1;
    }

    // The steering target cached at PathfindManager+0x200.
    Vector const Target = Owner->Pathfind->CachedTargetPosition;
    bool const bHaveTarget = ((double)(Target.z) * Target.z + (double)(Target.y) * Target.y
                              + (double)(Target.x) * Target.x) > 1.0;

    bool bBlocked = false;
    HasContact = 0;
    for (int HitIndex = 0; HitIndex < HitCount; ++HitIndex)
    {
        PhySys::RayHit const& Hit = Hits[HitIndex];
        CObject* const HitObject = Hit.Object;
        if (HitObject == nullptr)
        {
            continue;
        }
        if (((unsigned)HitObject->ObjectClass & 3) != 3)
        {
            continue;
        }
        CSimple const* const HitSimple = (CSimple const*)HitObject;
        unsigned const HitType = HitSimple->TypeBits;
        if ((HitType & 0x20c150) == 0 || HitType == OwnType)
        {
            continue;
        }

        Vector const ToHit{ Hit.Point.x - Nose.x, Hit.Point.y - Nose.y, Hit.Point.z - Nose.z };
        float const HitDistance = (float)(InlineFastLength(
            (double)(ToHit.z) * ToHit.z + (double)(ToHit.y) * ToHit.y
            + (double)(ToHit.x) * ToHit.x));

        // A target nearer than the hit drops the whole avoidance state.
        if (bHaveTarget)
        {
            Vector const ToTarget{ Target.x - Nose.x, Target.y - Nose.y, Target.z - Nose.z };
            double const TargetDistance = InlineFastLength(
                (double)(ToTarget.z) * ToTarget.z + (double)(ToTarget.y) * ToTarget.y
                + (double)(ToTarget.x) * ToTarget.x);
            if (TargetDistance < HitDistance)
            {
                Active = 0;
                HasContact = 0;
                ContactObjectId = 0;
                ContactCloseRate = 0.0f;
                ContactTimer = 0.0f;
                return;
            }
        }

        // Every surviving hit marks the contact; one nearer than LookAhead also records its
        // point and normal.
        HasContact = 1;
        ContactObjectId = HitSimple->Id;
        if (HitDistance < LookAhead)
        {
            ContactPoint = Hit.Point;
            ContactNormal = Hit.Normal;
            bBlocked = true;
        }
    }

    if (!bBlocked)
    {
        // Entered only when ContactTimer < 0, which the clamp above never leaves, so retail
        // never takes this branch either; it is kept as written.
        if (!(ContactTimer < 0.0f) || !bHaveTarget)
        {
            Active = 0;
            return;
        }

        PhySys::RayHit TargetHit[1];
        if (PhySys::FindRayCollisions(PhysSystemId, Nose, Target, TargetHit, 1) <= 0)
        {
            Active = 0;
            return;
        }

        Vector ToTarget{ Target.x - Nose.x, Target.y - Nose.y, Target.z - Nose.z };
        double const InvLength = 1.0f / std::sqrt((double)(ToTarget.z) * ToTarget.z
                                                  + (double)(ToTarget.y) * ToTarget.y
                                                  + (double)(ToTarget.x) * ToTarget.x);
        ToTarget.x = (float)(ToTarget.x * InvLength);
        ToTarget.y = (float)(ToTarget.y * InvLength);
        ToTarget.z = (float)(ToTarget.z * InvLength);

        // Axis = Forward x ToTarget; Dot stays unrounded.
        Vector const Axis{
            (float)((double)(ToTarget.z) * Forward.y - (double)(Forward.z) * ToTarget.y),
            (float)((double)(Forward.z) * ToTarget.x - (double)(ToTarget.z) * Forward.x),
            (float)((double)(ToTarget.y) * Forward.x - (double)(Forward.y) * ToTarget.x) };
        double const Dot = (double)(ToTarget.z) * Forward.z + (double)(Forward.y) * ToTarget.y
                           + (double)(Forward.x) * ToTarget.x;

        // 0.98 is double DAT_063a0610; otherwise (unordered too) Forward is turned about Axis.
        Vector Heading;
        if (std::fabs(Dot) > 0.98)
        {
            Heading = ToTarget;
        }
        else
        {
            double const HalfAngle = (1.0 - Dot) * GAvoidTurnGain * 0.5f;
            Quaternion Turn;
            Turn.w = X87CosF(HalfAngle);
            float const AxisScale = (float)(X87Sin(HalfAngle) / std::sqrt(
                (double)(Axis.z) * Axis.z + (double)(Axis.y) * Axis.y
                + (double)(Axis.x) * Axis.x));
            Turn.x = Axis.x * AxisScale;                 // FUN_06288830
            Turn.y = Axis.y * AxisScale;
            Turn.z = Axis.z * AxisScale;
            Matrix TurnMatrix;
            Math->QuaternionToMatrix(TurnMatrix, Turn);  // +0x54
            Math->TransformMatrix(Heading, TurnMatrix,
                                  Forward); // +0x2c
        }

        // 1000.0f is DAT_063a0608. Active is left as it is.
        AvoidDirection = Heading;
        Vector const Reach{ Heading.x * 1000.0f, Heading.y * 1000.0f, Heading.z * 1000.0f };
        AvoidPoint = Vector{ Reach.x + Nose.x, Reach.y + Nose.y, Reach.z + Nose.z };
        return;
    }

    // Blocked: three random rays into a cone of radius LookAhead * 0.5 (double DAT_0639df88)
    // at depth -LookAhead.
    Active = 1;
    float const ConeRadius = (float)(LookAhead * 0.5);
    float const ConeDepth = -LookAhead;
    for (int Attempt = 3; Attempt != 0; --Attempt)
    {
        // The radius is rounded to float, the angle stays on the x87 stack.
        float const Radius = (float)((double)(std::rand()) * 3.0517578125e-05f * ConeRadius);
        double const Angle = (double)(std::rand()) * 3.0517578125e-05f * 6.2831854820251465;
        Vector const ConeLocal{ (float)(X87Cos(Angle) * Radius),
                                (float)(X87Sin(Angle) * Radius), ConeDepth };

        Vector ConeOffset;
        Math->TransformMatrix(ConeOffset, Rotation, ConeLocal);
        Vector const ConeEnd{ ConeOffset.x + Nose.x, ConeOffset.y + Nose.y, ConeOffset.z + Nose.z };
        Vector ConeDirection{ ConeEnd.x - Nose.x, ConeEnd.y - Nose.y, ConeEnd.z - Nose.z };

        // Normalised and never read again (kept as retail).
        double const InvConeLength = 1.0f / std::sqrt((double)(ConeDirection.y) * ConeDirection.y
                                                      + (double)(ConeDirection.z) * ConeDirection.z
                                                      + (double)(ConeDirection.x) * ConeDirection.x);
        ConeDirection.x = (float)(ConeDirection.x * InvConeLength);
        ConeDirection.y = (float)(ConeDirection.y * InvConeLength);
        ConeDirection.z = (float)(ConeDirection.z * InvConeLength);

        PhySys::RayHit ConeHit[1];
        if (PhySys::FindRayCollisions(PhysSystemId, Nose, ConeEnd, ConeHit, 1) <= 0)
        {
            continue;
        }

        // Pick a slot, walking (CursorIndex + K) % 25. An expired one is taken at once and
        // CursorIndex becomes K -- the step count, not the slot index. Otherwise the lowest
        // timer below 1e8f (DAT_063a0604) is tracked by K, and retail then takes Slots[K], not
        // the slot it compared. Retail quirks kept, including index -1 when no timer is below
        // 1e8f (unreachable with the 1.0f timers written here).
        ObstacleAvoidance::Slot* Chosen = nullptr;
        {
            float Lowest = 100000000.0f;
            int LowestStep = -1;
            int Cursor = CursorIndex;
            int Step = 0;
            for (; Step < 25; ++Step, ++Cursor)
            {
                ObstacleAvoidance::Slot& Candidate = Slots[Cursor % 25];
                if (Candidate.Timer <= 0.0f)
                {
                    CursorIndex = Step;
                    Chosen = &Candidate;
                    break;
                }
                if (Lowest > Candidate.Timer)
                {
                    Lowest = Candidate.Timer;
                    LowestStep = Step;
                }
            }
            if (Step == 25)
            {
                // Retail indexes by the loop step, not by the cursor-relative slot it compared.
                Chosen = Slots + LowestStep;
            }
        }

        // Retail's null test; never null here.
        if (Chosen != nullptr)
        {
            Chosen->Point = ConeHit[0].Point;
            Chosen->Direction = ConeHit[0].Normal;
            Chosen->Timer = 1.0f;
        }
    }

    Vector Escape;
    if (!ObstacleAvoidance_AverageActiveDirection(Slots, ShipXform, Escape))
    {
        Active = 0;
        HasContact = 0;
        ContactObjectId = 0;
        ContactCloseRate = 0.0f;
        ContactTimer = 0.0f;
        return;
    }

    AvoidDirection = Escape;
    ContactTimer = 1.0f;                             // DAT_063a05fc

    // Uses the float-rounded Speed.
    Vector const ToContact{ ContactPoint.x - Nose.x, ContactPoint.y - Nose.y, ContactPoint.z - Nose.z };
    double const ContactLengthSq = (double)(ToContact.z) * ToContact.z
                                   + (double)(ToContact.y) * ToContact.y
                                   + (double)(ToContact.x) * ToContact.x;
    if (Speed > 1.0f)
    {
        ContactCloseRate = (float)(InlineFastLength(ContactLengthSq) / Speed);
    }
    else
    {
        ContactCloseRate = 0.0f;
    }

    // The unrounded |Dot| is tested against 0.999 (double DAT_0639fd40); the float image feeds
    // the turn angle.
    double const EscapeDotExt = (double)(Forward.z) * AvoidDirection.z
                                + (double)(Forward.y) * AvoidDirection.y
                                + (double)(Forward.x) * AvoidDirection.x;
    float const EscapeDot = (float)(EscapeDotExt);
    Vector Heading;
    if (std::fabs(EscapeDotExt) <= 0.999)
    {
        Vector const Axis{
            (float)((double)(Forward.y) * AvoidDirection.z - (double)(Forward.z) * AvoidDirection.y),
            (float)((double)(Forward.z) * AvoidDirection.x - (double)(Forward.x) * AvoidDirection.z),
            (float)((double)(Forward.x) * AvoidDirection.y - (double)(Forward.y) * AvoidDirection.x) };

        // 0.2f is DAT_063a0600.
        double TurnAngle = (1.0 - EscapeDot) * 0.2f;
        if (EscapeDot > 0.0)
        {
            TurnAngle = 0.0f;
        }
        double const HalfAngle = TurnAngle * 0.5f;

        Quaternion Turn;
        Turn.w = X87CosF(HalfAngle);
        float const AxisScale = (float)(X87Sin(HalfAngle) / std::sqrt(
            (double)(Axis.z) * Axis.z + (double)(Axis.y) * Axis.y
            + (double)(Axis.x) * Axis.x));
        Turn.x = Axis.x * AxisScale;                     // FUN_06288830
        Turn.y = Axis.y * AxisScale;
        Turn.z = Axis.z * AxisScale;
        Matrix TurnMatrix;
        Math->QuaternionToMatrix(TurnMatrix, Turn);
        Math->TransformMatrix(Heading, TurnMatrix, Forward);
    }
    else
    {
        Heading = Forward;
    }

    // 10.0f is DAT_0639e884. Retail rounds x * LookAhead * 10 once, z * LookAhead before the
    // * 10, and keeps y unrounded until the sum.
    AvoidDirection = Heading;
    float const ReachX = (float)((double)(Heading.x) * LookAhead * 10.0f);
    double const ReachY = (double)(Heading.y) * LookAhead * 10.0f;
    float const ReachZScaled = (float)((double)(Heading.z) * LookAhead);
    AvoidPoint = Vector{ ReachX + Nose.x,
                         (float)(ReachY + Nose.y),
                         (float)((double)(ReachZScaled) * 10.0f + Nose.z) };
}

bool ObstacleAvoidance_AverageActiveDirection(ObstacleAvoidance::Slot const* Slots,
                                              Transform const& Ship,
                                              Vector& OutDirection)
{
    // FUN_062de2a0
    Vector Sum{ 0.0f, 0.0f, 0.0f };
    int Count = 0;
    for (int Index = 0; Index < 25; ++Index)
    {
        ObstacleAvoidance::Slot const& Entry = Slots[Index];

        if (!(Entry.Timer > 0.0f))
        {
            continue;
        }

        Vector Away{ Ship.Translation.x - Entry.Point.x, Ship.Translation.y - Entry.Point.y,
                     Ship.Translation.z - Entry.Point.z };

        double const InvLength = 1.0f / std::sqrt((double)(Away.z) * Away.z
                                                  + (double)(Away.y) * Away.y
                                                  + (double)(Away.x) * Away.x);
        Away.x = (float)(Away.x * InvLength);
        Away.y = (float)(Away.y * InvLength);
        Away.z = (float)(Away.z * InvLength);

        Vector const Term{ (Away.x + Away.x) + Entry.Direction.x, (Away.y + Away.y) + Entry.Direction.y,
                           (Away.z + Away.z) + Entry.Direction.z };
        ++Count;
        Sum.x = Term.x + Sum.x;
        Sum.y = Term.y + Sum.y;
        Sum.z = Term.z + Sum.z;
    }

    // No active slot: false, OutDirection untouched.
    if (Count <= 0)
    {
        return false;
    }

    double const InvCount = 1.0f / (double)(Count);
    OutDirection.x = (float)(Sum.x * InvCount);
    OutDirection.y = (float)(Sum.y * InvCount);
    OutDirection.z = (float)(Sum.z * InvCount);

    // Against double 1e-05 (DAT_0639f418); a short or unordered result is returned
    // un-normalised.
    double const LengthSq = (double)(OutDirection.x) * OutDirection.x
                            + (double)(OutDirection.y) * OutDirection.y
                            + (double)(OutDirection.z) * OutDirection.z;
    if (!(LengthSq > 1e-05))
    {
        return true;
    }

    double const InvLength = 1.0f / std::sqrt(LengthSq);
    OutDirection.x = (float)(InvLength * OutDirection.x);
    OutDirection.y = (float)(InvLength * OutDirection.y);
    OutDirection.z = (float)(InvLength * OutDirection.z);
    return true;
}

// ----------------------------------------------------------------------------
// Ship heading-blend classifier pair -- see common_aisteer.h for the field layout.
// ----------------------------------------------------------------------------

namespace
{
    // The ship's current Transform via WatchablePtr - 8, shared by ClassifyShipObstaclePhase
    // and UpdateShipHeadingBlend. Retail tests only the watched pointer for the -8 adjust
    // and then calls through the result unconditionally; there is no null return.
    Transform const* ResolveShipTransform(IBehaviorManager* Owner)
    {
        IObjRW* const ProviderBase = ObjRwFromWatchable(Owner->WatchablePtr);
        // Retail calls slot 4 (get_orientation), folded with get_transform.
        return &ProviderBase->get_transform();
    }
}

// FUN_062dc440, cdecl (Out, From, To, Angle). See common_aisteer.h for the algorithm and the
// Ghidra call-argument-corruption note. 0.999 is double DAT_0639fd40; Axis x and y stay
// unrounded, z is rounded to float. The math engine is used with no null test.
Vector RotateTowardBySlerp(Vector const& From, Vector const& To, float Angle)
{
    double const Dot = ((double)(From.x) * To.x + (double)(To.z) * From.z) +
                       (double)(From.y) * To.y;
    if (Dot > 0.999)
        return To;

    double const AxisX = (double)(From.y) * To.z - (double)(To.y) * From.z;
    double const AxisY = (double)(To.x) * From.z - (double)(To.z) * From.x;
    float const AxisZ = (float)((double)(To.y) * From.x - (double)(From.y) * To.x);
    double const HalfAngle = (double)(Angle) * 0.5f;
    double const AxisLength = std::sqrt(((double)(AxisZ) * AxisZ + AxisX * AxisX) + AxisY * AxisY);
    float const SinOverLength = (float)(X87Sin(HalfAngle) / AxisLength);

    Quaternion Quat;
    Quat.w = X87CosF(HalfAngle);
    Quat.x = (float)(SinOverLength * AxisX);
    Quat.y = (float)(SinOverLength * AxisY);
    Quat.z = (float)((double)(AxisZ) * SinOverLength);

    Matrix RotMatrix;
    Acquire3DMathEngine()->QuaternionToMatrix(RotMatrix, Quat); // +0x54
    Vector Result;
    Acquire3DMathEngine()->TransformMatrix(Result,
                                           RotMatrix,
                                           From); // +0x2C
    return Result;
}

// FUN_062dbfb0. See common_aisteer.h for the algorithm and field shapes.
namespace
{
    // FUN_06347e60 in double; defined with FindClosestObstacleToAvoid's helpers below.
    double VectorLengthX87(Vector const& V);

    // FUN_062dc3e0, cdecl, result left unrounded in ST0. Not the arithmetic of FUN_062df7f0
    // (1.5f*y - (0.5f*X*y)*(y*y)).
    double FastInvSqrtNewton(float X)
    {
        int Bits;
        std::memcpy(&Bits, &X, sizeof(Bits));
        Bits = 0x5f3759df - (Bits >> 1);
        float Y;
        std::memcpy(&Y, &Bits, sizeof(Y));
        return (1.5f - (((double)Y * Y) * X) * 0.5f) * Y;
    }

    // The |V|^2 = (z*z + y*y) + x*x order NudgeObstaclePathPoint uses for every vector
    // it measures inline, left unrounded.
    double LenSqZyx(Vector const& V)
    {
        return ((double)V.z * V.z + (double)V.y * V.y) + (double)V.x * V.x;
    }
}

int NudgeObstaclePathPoint(ObstacleAvoidance::Slot const& Candidate, Vector const& Point,
                                       Transform const& Obstacle, Vector* OutPoint, bool ExtraCheck)
{
    Vector const Midpoint = Add(Candidate.Point, Scale(Candidate.Direction, 0.5f));
    Vector const& ObstaclePos = Obstacle.Translation; // Obstacle + 0x24

    // Scaled by the unrounded reciprocal of the FUN_06347e60 length.
    Vector DirToObstacle = Sub(ObstaclePos, Midpoint);
    {
        double const Inv = 1.0 / VectorLengthX87(DirToObstacle);
        DirToObstacle.x = (float)(DirToObstacle.x * Inv);
        DirToObstacle.y = (float)(DirToObstacle.y * Inv);
        DirToObstacle.z = (float)(DirToObstacle.z * Inv);
    }

    // FUN_062e1330 with the candidate's capsule record.
    Vector HitPoint0;
    if (!RaySegmentCapsuleFirstHit(Midpoint, DirToObstacle, Candidate.Timer, Candidate.Point, Candidate.Direction, &HitPoint0))
        return 3;

    // The unrounded length is compared with 1.0f; FUN_062dc410 normalises by the float image.
    Vector ObstacleDir = Sub(ObstaclePos, Midpoint);
    double const DistToObstacle = std::sqrt(LenSqZyx(ObstacleDir));
    if (DistToObstacle < 1.0)
        return 1;
    VecNormalizeInPlaceByLength3(&ObstacleDir, (float)DistToObstacle);

    Vector PointDir = Sub(Point, Midpoint);
    double const DistPointToMid = std::sqrt(LenSqZyx(PointDir));
    if (DistPointToMid < 1.0)
        return 1;
    VecNormalizeInPlaceByLength3(&PointDir, (float)DistPointToMid);

    // This length is never rounded to float.
    Vector ObstacleToPointDir = Sub(Point, ObstaclePos);
    double const DistPointToObstacle = std::sqrt(LenSqZyx(ObstacleToPointDir));
    if (DistPointToObstacle < 1.0)
        return 1;
    {
        double const Inv = 1.0 / DistPointToObstacle;
        ObstacleToPointDir.x = (float)(ObstacleToPointDir.x * Inv);
        ObstacleToPointDir.y = (float)(ObstacleToPointDir.y * Inv);
        ObstacleToPointDir.z = (float)(ObstacleToPointDir.z * Inv);
    }

    double const AlignDot = ((double)PointDir.z * ObstacleDir.z + (double)PointDir.y * ObstacleDir.y) +
                            (double)ObstacleDir.x * PointDir.x;
    if (AlignDot > 0.9f)
    {
        if (!ExtraCheck)
            return 1;
        // -column 2 of the obstacle's rotation (FUN_0628b030); NaN returns 1.
        Vector const NegForward{ -Obstacle.Rotation.E[0][2], -Obstacle.Rotation.E[1][2], -Obstacle.Rotation.E[2][2] };
        double const FacingDot = ((double)NegForward.z * ObstacleToPointDir.z + (double)NegForward.y * ObstacleToPointDir.y) +
                                 (double)NegForward.x * ObstacleToPointDir.x;
        if (!(FacingDot < 0.85f))
            return 1;
    }

    // B's fast length runs on the unrounded B; A's on its float image, times 1.2f.
    float const LenSqA = (float)LenSqZyx(Sub(HitPoint0, Midpoint));
    double const LenSqB = LenSqZyx(Sub(ObstaclePos, Midpoint));
    double const ApproxDistA = InlineFastLength((double)LenSqA) * 1.2f;
    double const ApproxDistB = InlineFastLength(LenSqB);
    if (ApproxDistA < ApproxDistB)
        return 2;

    // C is rounded to float; not-greater (and NaN) hands Point straight back.
    Vector const PointToObstacle = Sub(Point, ObstaclePos);
    float const LenSqC = (float)LenSqZyx(PointToObstacle);
    if (!(FastInvSqrtNewton(LenSqC) * LenSqC > 100.0f))
    {
        *OutPoint = Point;
        return 0;
    }

    // Swing ObstacleDir 0.3 rad toward PointDir and cast again, straight into OutPoint.
    Vector const RotatedDir = RotateTowardBySlerp(ObstacleDir, PointDir, 0.3f);
    if (!RaySegmentCapsuleFirstHit(Midpoint, RotatedDir, Candidate.Timer, Candidate.Point, Candidate.Direction, OutPoint))
        return 3;
    return 0;
}

// FUN_062e74e0, thiscall, RET 8. Ghidra's decompile of it is wrong (`unaff_ESI` is the
// get_equipment_status out-buffer, `unaff_retaddr` the second stack argument). Callers pass
// per-call filter masks (0xfd58 here, 0x7f0000 and 0x10000000 at 062ce790/062ce948).
// Callers: ClassifyShipObstaclePhase (below) and LootBehavior.
bool SystemManagerTimerTable::GetOwnScanList(ScanList const*& OutList, unsigned FilterMask)
{
    // No test on Owner itself; the watched own ship (raw and raw - 8) is the only gate.
    Watchable const* const ListHandle = Owner->Watcher.target();
    if (ListHandle == nullptr)
        return false;
    IObjRW* const ProviderBase = ObjRwFromWatchable(const_cast<Watchable*>(ListHandle));
    if (ProviderBase == nullptr)
        return false;

    // Retail masks bits of the uninitialised out-buffer before the call overwrites it;
    // harmless, not reproduced.
    EquipStatus Scratch{};
    ProviderBase->get_equipment_status(Scratch, ScannerEquipId);

    if ((Scratch.Flags & 2) != 0)
        return false;

    // Retail re-derives the ProviderBase chain here with no null test (a null owner faults on
    // the vtable read in retail, as here).
    IObjRW* const ProviderBase2 = ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()));

    int const Result = ProviderBase2->get_scanlist(OutList, FilterMask, bKeepCache != 0);
    return Result == 0;
}

// FUN_062dbbe0. A PathfindManager thiscall in retail whose `this` is never read, RET 8
// (Candidate, Slot).
bool ResolveObstacleCandidateSlot(IObjRW* Candidate, ObstacleAvoidance::Slot& OutSlot)
{
    IObjRW* const Obj = Candidate;

    // Retail calls slot +0x10, folded with get_transform.
    Transform const& CandXform = Obj->get_transform();
    Vector Min;
    Vector Max;
    if (Obj->get_archetype_extents(Min, Max) != 0)
    {
        // Sphere: Slot = {radius, centre, (0, 1, 0)}; a greater atmosphere range replaces
        // the radius.
        float Radius = 0.0f;
        Vector Centre;
        Obj->get_physical_radius(Radius, Centre);
        OutSlot.Direction.x = 0.0f;
        OutSlot.Direction.y = 1.0f;
        OutSlot.Direction.z = 0.0f;
        OutSlot.Point = Centre;
        OutSlot.Timer = Radius;
        float Range = 0.0f;
        Obj->get_atmosphere_range(Range);
        if (Range > Radius)
            OutSlot.Timer = Range;
        return true;
    }

    Vector Sum;
    Sum.x = Min.x + Max.x;
    Sum.y = Min.y + Max.y;
    Sum.z = Min.z + Max.z;
    Vector LocalCentre;
    LocalCentre.x = Sum.x * 0.5f;
    LocalCentre.y = Sum.y * 0.5f;
    LocalCentre.z = Sum.z * 0.5f;

    // The result is never read again (kept as retail).
    Vector WorldCentre;
    Acquire3DMathEngine()->TransformXform(WorldCentre, CandXform, LocalCentre);

    // Ey and Ez are rounded to float, Ex stays unrounded.
    double const ExtentX = (double)(Max.x) - Min.x;
    float const ExtentY = (float)((double)(Max.y) - Min.y);
    float const ExtentZ = (float)((double)(Max.z) - Min.z);

    // The capsule runs along the strictly longest extent (X, then Y, else Z), starting at
    // Min on that axis and the centre on the other two; its radius is half the larger of
    // the other two extents (a tie takes the later-tested one).
    double Across;
    if (ExtentX > ExtentY && ExtentX > ExtentZ)
    {
        OutSlot.Point.x = Min.x;
        OutSlot.Point.y = LocalCentre.y;
        OutSlot.Point.z = LocalCentre.z;
        OutSlot.Direction.x = Max.x - OutSlot.Point.x;
        OutSlot.Direction.y = LocalCentre.y - OutSlot.Point.y;
        OutSlot.Direction.z = LocalCentre.z - OutSlot.Point.z;
        Across = (ExtentY > ExtentZ) ? ExtentY : ExtentZ;
    }
    else if (ExtentY > ExtentX && ExtentY > ExtentZ)
    {
        OutSlot.Point.x = LocalCentre.x;
        OutSlot.Point.y = Min.y;
        OutSlot.Point.z = LocalCentre.z;
        OutSlot.Direction.x = LocalCentre.x - OutSlot.Point.x;
        OutSlot.Direction.y = Max.y - OutSlot.Point.y;
        OutSlot.Direction.z = LocalCentre.z - OutSlot.Point.z;
        Across = (ExtentX > ExtentZ) ? ExtentX : (double)(ExtentZ);
    }
    else
    {
        OutSlot.Point.x = LocalCentre.x;
        OutSlot.Point.y = LocalCentre.y;
        OutSlot.Point.z = Min.z;
        OutSlot.Direction.x = LocalCentre.x - OutSlot.Point.x;
        OutSlot.Direction.y = LocalCentre.y - OutSlot.Point.y;
        OutSlot.Direction.z = Max.z - OutSlot.Point.z;
        Across = (ExtentX > ExtentY) ? ExtentX : (double)(ExtentY);
    }

    OutSlot.Timer = (float)(Across * 0.5); // double DAT_0639df88

    Vector WorldPoint;
    Acquire3DMathEngine()->TransformXform(WorldPoint, CandXform, OutSlot.Point);
    OutSlot.Point = WorldPoint;
    Vector WorldDir;
    Acquire3DMathEngine()->Rotate(WorldDir, CandXform, OutSlot.Direction);
    OutSlot.Direction = WorldDir;
    return true;
}

// FUN_062dc840 (Owner at +0x1C0). Returns 4 for "no reaction", 0 after committing a reaction,
// or NudgeObstaclePathPoint's own non-zero result.
int PathfindManager::ClassifyObstaclePhase()
{
    PathfindManager* const This = this;

    This->Reaction.State = 3;
    This->Reaction.LastObstacleTag = 0;
    This->Reaction.Heading.z = 0.0f;
    This->Reaction.Heading.y = 0.0f;
    This->Reaction.Heading.x = 0.0f;
    This->Reaction.bInReaction = false;
    This->Reaction.bCloseEncounter = false;
    This->CachedObstacleId = 0;
    This->bHasCachedObstacle = false;

    IBehaviorManager* const Owner = This->Owner;
    Vector WorkingTarget = Owner->Pathfind->CachedTargetPosition;
    This->TargetPosition = WorkingTarget;

    // Behaviour 10 never reacts.
    int const BehaviorIndex = Owner->CurrentBehaviorIndex;
    if (BehaviorIndex == 10)
        return 4;

    int const LookaheadBehaviors[6] = { 1, 3, 5, 0xb, 0xd, 0x12 };
    bool bFlagA = false;
    for (int Index = 0; Index < 6; ++Index)
    {
        if (LookaheadBehaviors[Index] == BehaviorIndex)
        {
            bFlagA = true;
            break;
        }
    }

    Transform const* const ShipXform = ResolveShipTransform(Owner);

    // No target (unordered keeps it): look 2000 ahead of the ship, or give up without FlagA.
    double const TargetLenSq = ((double)(WorkingTarget.x) * WorkingTarget.x +
                                (double)(WorkingTarget.y) * WorkingTarget.y) +
                               (double)(WorkingTarget.z) * WorkingTarget.z;
    if (TargetLenSq < 1.0)
    {
        if (!bFlagA)
        {
            This->bBlendEnabled = false;
            return 4;
        }
        This->bBlendEnabled = true;
        Vector const LocalAhead{ 0.0f, 0.0f, -2000.0f };
        Vector Ahead;
        Acquire3DMathEngine()->TransformXform(Ahead, *ShipXform, LocalAhead);
        WorkingTarget = Ahead;
    }
    else
    {
        This->bBlendEnabled = true;
    }

    // 0.1 is double DAT_063a0460.
    float Throttle = 1.0f;
    ObjRwFromWatchable(Owner->WatchablePtr)->get_throttle(Throttle);
    if ((double)(Throttle) < 0.1)
        return 4;

    // The return value is ignored; only a null list returns 4.
    ScanList const* CandidateList = nullptr;
    Owner->Sys->Timers.GetOwnScanList(CandidateList, 0xfd58u);
    if (CandidateList == nullptr)
        return 4;

    FSpatialQueryResult Nearby;
    Nearby.Count = 0;
    Nearby.Capacity = 0x800;
    ISpatialPartition* const Bucket = (ISpatialPartition*)Owner->ObjectDb->GetBucket(7);
    if (Bucket != nullptr)
    {
        Geometry::Sphere Query;
        Query.Center = ShipXform->Translation;
        Query.Radius = 20000.0f;
        Bucket->QuerySphere(&Query, &Nearby);
    }

    // Pass 0 walks the scan list (mask 0xfd58), pass 1 the sphere query (mask 7). A candidate
    // id missing from RecentObstacleIds takes the next deflection slot, refilled by
    // FUN_062dbbe0.
    float BestDistance = 3.40282347e+38f; // 0x7f7fffff
    IObjRW* BestCandidate = nullptr;
    ObstacleAvoidance::Slot BestSlot{};   // read only after a candidate set it
    for (int Pass = 0; Pass < 2; ++Pass)
    {
        unsigned const Count = (Pass == 0) ? (unsigned)CandidateList->Count : Nearby.Count;
        for (unsigned Index = 0; Index < Count; ++Index)
        {
            IObjRW* Candidate;
            unsigned TypeMask;
            if (Pass == 0)
            {
                Candidate = ObjRwFromWatchable(const_cast<Watchable*>(CandidateList->Entries[Index].target()));
                TypeMask = 0xfd58u;
            }
            else
            {
                Candidate = (IObjRW*)Nearby.Items[Index];
                TypeMask = 7u;
            }

            unsigned Type = 0;
            Candidate->get_type(Type);
            if ((Type & TypeMask) == 0)
                continue;
            bool const bCandidateClose = (Type & 7u) != 0;

            unsigned const CandidateId = Candidate->get_id();
            ObstacleAvoidance::Slot* SlotPtr = nullptr;
            for (int Cached = 0; Cached < 8; ++Cached)
            {
                if (This->RecentObstacleIds[Cached] == CandidateId)
                {
                    SlotPtr = &This->DeflectionSlots[Cached];
                    break;
                }
            }
            if (SlotPtr == nullptr)
            {
                int const Next = ((int)This->NextDeflectionSlot + 1) % 8;   // signed remainder
                This->NextDeflectionSlot = (unsigned)Next;
                SlotPtr = &This->DeflectionSlots[Next];
                ResolveObstacleCandidateSlot(Candidate, *SlotPtr);
            }

            // NaN passes both tests.
            if (SlotPtr->Timer < 100.0f || SlotPtr->Timer > 20000.0f)
                continue;

            double const Scaled = (double)(SlotPtr->Timer) * 0.15f;
            float const Floor = (bCandidateClose && bFlagA) ? 750.0f : 250.0f;
            float const Margin = ((double)(Floor) > Scaled) ? Floor : (float)(Scaled);

            // The running sum is not rounded in between.
            SlotPtr->Timer = (float)(((double)(Margin) + SlotPtr->Timer) +
                                                (double)(This->CoastDistanceFrom300) * 0.5);

            // FUN_062e1410; the nearest slot midpoint to WorkingTarget wins.
            if (PointInCapsule(ShipXform->Translation, SlotPtr->Timer, SlotPtr->Point, SlotPtr->Direction))
            {
                Vector Mid;
                Mid.x = SlotPtr->Direction.x * 0.5f + SlotPtr->Point.x;
                Mid.y = SlotPtr->Direction.y * 0.5f + SlotPtr->Point.y;
                Mid.z = SlotPtr->Direction.z * 0.5f + SlotPtr->Point.z;
                double const DX = (double)(Mid.x) - WorkingTarget.x;
                double const DY = (double)(Mid.y) - WorkingTarget.y;
                double const DZ = (double)(Mid.z) - WorkingTarget.z;
                double const Distance = InlineFastLength((DX * DX + DZ * DZ) + DY * DY);
                if (Distance < (double)(BestDistance))
                {
                    BestDistance = (float)(Distance);
                    BestSlot = *SlotPtr;
                    BestCandidate = Candidate;
                }
            }

            SlotPtr->Timer = (float)((double)(SlotPtr->Timer) - Margin);
        }
    }

    if (BestCandidate == nullptr)
        return 4;

    // Behaviour 2: a winner sitting on the target (within 1.0) does not count; unordered
    // continues.
    if (BehaviorIndex == 2)
    {
        Vector const& WinnerPos = BestCandidate->get_position();
        double const DX = (double)(WinnerPos.x) - WorkingTarget.x;
        double const DY = (double)(WinnerPos.y) - WorkingTarget.y;
        double const DZ = (double)(WinnerPos.z) - WorkingTarget.z;
        double const Distance = InlineFastLength((DX * DX + DZ * DZ) + DY * DY);
        if (Distance <= 1.0)
            return 4;
    }

    This->CachedObstacleId = BestCandidate->get_id();
    This->bHasCachedObstacle = true;
    unsigned WinnerType = 0;
    BestCandidate->get_type(WinnerType);
    bool const bHasCloseFlags = (WinnerType & 7u) != 0;

    if (bHasCloseFlags)
    {
        // The squared distance is rounded to float before the fast length.
        Vector const& WinnerPos = BestCandidate->get_position();
        double const DX = (double)(WinnerPos.x) - ShipXform->Translation.x;
        double const DY = (double)(WinnerPos.y) - ShipXform->Translation.y;
        double const DZ = (double)(WinnerPos.z) - ShipXform->Translation.z;
        float const LenSqF = (float)((DZ * DZ + DY * DY) + DX * DX);
        float Range = 0.0f;
        BestCandidate->get_atmosphere_range(Range);
        if (InlineFastLength(LenSqF) <= (double)(Range))
            This->Reaction.bCloseEncounter = true;

        // With FlagA, a ship target farther than 500 replaces WorkingTarget.
        if (bFlagA)
        {
            IObjRW* Target = nullptr;
            ObjRwFromWatchable(Owner->WatchablePtr)->get_target(Target);
            if (Target != nullptr)
            {
                Vector const& TargetPos = Target->get_position();
                double const TX = (double)(TargetPos.x) - ShipXform->Translation.x;
                double const TY = (double)(TargetPos.y) - ShipXform->Translation.y;
                double const TZ = (double)(TargetPos.z) - ShipXform->Translation.z;
                if (InlineFastLength((TZ * TZ + TY * TY) + TX * TX) > 500.0)
                {
                    WorkingTarget = TargetPos;
                }
                else
                {
                    // Within 500 (or unordered): head away from the slot midpoint. z is
                    // rounded and squared as z * float(z).
                    This->Reaction.ActiveSlot = BestSlot;
                    This->Reaction.State = 1;
                    This->Reaction.LastObstacleTag = BestCandidate->get_id();
                    This->Reaction.bInReaction = true;
                    Vector Mid;
                    Mid.x = BestSlot.Direction.x * 0.5f + BestSlot.Point.x;
                    Mid.y = BestSlot.Direction.y * 0.5f + BestSlot.Point.y;
                    Mid.z = BestSlot.Direction.z * 0.5f + BestSlot.Point.z;
                    double const MX = (double)(Mid.x) - ShipXform->Translation.x;
                    double const MY = (double)(Mid.y) - ShipXform->Translation.y;
                    double const MZ = (double)(Mid.z) - ShipXform->Translation.z;
                    float const MZf = (float)(MZ);
                    float const Inv = (float)(1.0 / std::sqrt((MZ * MZf + MY * MY) + MX * MX));
                    This->Reaction.Heading.x = -(float)(MX * Inv);
                    This->Reaction.Heading.y = -(float)(MY * Inv);
                    This->Reaction.Heading.z = -(float)((double)(MZf) * Inv);
                    return 0;
                }
            }
        }
    }

    // The shared commit (LAB_062dd273). The normalisation has no zero test; bInReaction is
    // set when the heading is more than ~60 degrees off the ship's forward.
    auto CommitReaction = [&](Vector const& OutPoint) -> int
    {
        This->Reaction.ActiveSlot = BestSlot;
        This->Reaction.State = 0;
        This->Reaction.LastObstacleTag = BestCandidate->get_id();
        This->Reaction.bInReaction = false;
        Vector& Heading = This->Reaction.Heading;
        Heading.x = OutPoint.x - ShipXform->Translation.x;
        Heading.y = OutPoint.y - ShipXform->Translation.y;
        Heading.z = OutPoint.z - ShipXform->Translation.z;
        double const Inv = 1.0 / std::sqrt(((double)(Heading.x) * Heading.x +
                                            (double)(Heading.y) * Heading.y) +
                                           (double)(Heading.z) * Heading.z);
        Heading.x = (float)(Inv * Heading.x);
        Heading.y = (float)(Inv * Heading.y);
        Heading.z = (float)(Inv * Heading.z);
        Matrix const& Rotation = ShipXform->Rotation;
        double const Facing = (-(double)(Rotation.E[2][2]) * Heading.z +
                               -(double)(Rotation.E[1][2]) * Heading.y) +
                              -(double)(Rotation.E[0][2]) * Heading.x;
        if (Facing < 0.5)
            This->Reaction.bInReaction = true;
        return 0;
    };

    Vector OutPoint;
    if (PointInCapsule(WorkingTarget, BestSlot.Timer, BestSlot.Point, BestSlot.Direction))
    {
        int const Nudge = NudgeObstaclePathPoint(BestSlot, WorkingTarget, *ShipXform, &OutPoint,
                                                 This->Reaction.bCloseEncounter);
        return Nudge == 0 ? CommitReaction(OutPoint) : Nudge;
    }

    if (bHasCloseFlags)
    {
        // Ray from the ship toward WorkingTarget against the larger of radius and atmosphere
        // range plus 200 (FUN_062df830).
        float Radius = 0.0f;
        Vector Center;
        BestCandidate->get_physical_radius(Radius, Center);
        float Range = 0.0f;
        BestCandidate->get_atmosphere_range(Range);
        if (Range > Radius)
            Radius = Range;
        float const SphereRadius = (float)((double)(Radius) + 200.0f);
        double const RX = (double)(WorkingTarget.x) - ShipXform->Translation.x;
        double const RY = (double)(WorkingTarget.y) - ShipXform->Translation.y;
        double const RZ = (double)(WorkingTarget.z) - ShipXform->Translation.z;
        float const RZf = (float)(RZ);
        float const Inv = (float)(1.0 / std::sqrt((RZ * RZf + RY * RY) + RX * RX));
        Vector RayDir;
        RayDir.x = (float)(RX * Inv);
        RayDir.y = (float)(RY * Inv);
        RayDir.z = (float)((double)(RZf) * Inv);
        if (!RaySphereIntersect(ShipXform->Translation, RayDir, Center, SphereRadius, nullptr))
            return 4;
    }
    else
    {
        CObject const* const Cobj = BestCandidate->cobject();
        CObject* const Exclude = (Cobj != nullptr && ((unsigned)Cobj->ObjectClass & 3u) == 3u)
            ? const_cast<CObject*>(Cobj)
            : nullptr;
        PhySys::RayHit Hit[1];
        if (PhySys::FindRayIntersect(Exclude, ShipXform->Translation, WorkingTarget, Hit, 1) == 0)
            return 4;
    }

    int const Nudge = NudgeObstaclePathPoint(BestSlot, WorkingTarget, *ShipXform, &OutPoint, false);
    return Nudge == 0 ? CommitReaction(OutPoint) : Nudge;
}

// FUN_062dc590
void PathfindManager::UpdateHeadingBlend(float Dt)
{
    // The ship transform is fetched before any branch.
    int const Phase = ClassifyObstaclePhase();
    Transform const* const ShipXform = ResolveShipTransform(Owner);

    Vector& Heading = Reaction.Heading;
    float& Timer = BlendTimer;

    if (Phase == 0)
    {
        BlendStart = Heading;
        Timer = 4.0f;
        return;
    }

    if (Phase != 1)
    {
        // The unrounded difference is compared; NaN continues.
        double const NewTimer = (double)(Timer) - Dt;
        float const LocalTimer = (float)(NewTimer);
        Timer = LocalTimer;
        if (!(NewTimer < 0.0) && bBlendEnabled)
        {
            // x / y stay unrounded, z is rounded and squared as z * float(z).
            Vector const& TargetPos = TargetPosition;
            double const DX = (double)(TargetPos.x) - ShipXform->Translation.x;
            double const DY = (double)(TargetPos.y) - ShipXform->Translation.y;
            double const DZ = (double)(TargetPos.z) - ShipXform->Translation.z;
            float const DZf = (float)(DZ);
            float const Inv = (float)(1.0 / std::sqrt((DZ * DZf + DX * DX) + DY * DY));
            Vector Dir;
            Dir.x = (float)(DX * Inv);
            Dir.y = (float)(DY * Inv);
            Dir.z = (float)((double)(DZf) * Inv);

            // The unrounded Dot is compared; the float image feeds acos.
            double const Dot = ((double)(Dir.x) * BlendStart.x + (double)(Dir.z) * BlendStart.z) +
                               (double)(Dir.y) * BlendStart.y;
            float const DotF = (float)(Dot);
            Vector NewHeading;
            if (Dot > 0.999)
            {
                NewHeading = Dir;
            }
            else
            {
                Vector Axis;
                Axis.x = (float)((double)(Dir.z) * BlendStart.y - (double)(Dir.y) * BlendStart.z);
                Axis.y = (float)((double)(Dir.x) * BlendStart.z - (double)(Dir.z) * BlendStart.x);
                Axis.z = (float)((double)(Dir.y) * BlendStart.x - (double)(Dir.x) * BlendStart.y);

                double const HalfAngle = std::acos((double)(DotF)) *
                                         (((double)(4.0f) - LocalTimer) * 0.25f) * 0.5f;
                Quaternion Quat;
                Quat.w = X87CosF(HalfAngle);

                // FUN_06288830 scales Axis by K (one float product per component).
                double const AxisLength = std::sqrt(((double)(Axis.z) * Axis.z +
                                                     (double)(Axis.x) * Axis.x) +
                                                    (double)(Axis.y) * Axis.y);
                float const K = (float)(X87Sin(HalfAngle) / AxisLength);
                Quat.x = Axis.x * K;
                Quat.y = Axis.y * K;
                Quat.z = Axis.z * K;

                // The engine (DAT_063fc994) is created lazily with no null test.
                Matrix RotMatrix;
                Acquire3DMathEngine()->QuaternionToMatrix(RotMatrix, Quat);
                Acquire3DMathEngine()->TransformMatrix(NewHeading, RotMatrix, BlendStart);
            }

            Reaction.LastObstacleTag = 0;
            Reaction.bInReaction = false;
            Reaction.State = 2;
            Heading = NewHeading;
            return;
        }
    }

    // Full reset: phase 1, timer expired, or blending disabled.
    Timer = 0.0f;
    Reaction.LastObstacleTag = 0;
    Reaction.State = 3;
    Heading = Vector{ 0.0f, 0.0f, 0.0f };
    Reaction.bInReaction = false;
    Reaction.bCloseEncounter = false;
}

// ============================================================================
// FAiTarget's two out-of-line methods, and the "pick the obstacle worth
// dodging" scan that uses them.
// ============================================================================


namespace
{
    // .data:DAT_063ec6b0 == 1.0f. Never written; read only by FindClosestObstacleToAvoid,
    // twice, to scale a radius before a collision test -- a tuning knob shipped at default.
    float const g_AiObstacleRadiusScale = 1.0f;

    // get_scanlist's `Type` and the per-candidate type re-test; retail spells it twice.
    unsigned const kAvoidScanTypeMask = 0x207f8010u;

    // SystemManager+0x14FC -- the owner's own FAiTarget. Duplicates common_iustategraph.cpp's
    // OwnerElementOf(), which has internal linkage there.
    FAiTarget* AiTargetOwnerElementOf(SystemManager* Sys)
    {
        return &Sys->SelfTarget;
    }

    // FindClosestObstacleToAvoid re-derives this nine times (once per candidate in the loop)
    // rather than caching it; reproduced, since the watcher target can go null in between.
    IObjRW* AiOwnerObjectOf(SystemManager* Sys)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()));
    }

    // Lazily cached type flags, inlined three times in FindClosestObstacleToAvoid. Unlike
    // FAiTarget::RefreshPos, the guard is raised after the vcall (as in the copies in
    // common_largeshipbehavior.cpp and common_iustategraph.cpp). No null test on Iface.
    void RefreshAiTargetFlags(FAiTarget& Self)
    {
        if (Self.bFlagsValid != 0)
            return;
        Self.Iface->get_type(Self.CachedFlags);
        Self.bFlagsValid = 1;
    }

    // FUN_06347e60 as retail leaves it in ST0: sqrt((x*x + y*y) + z*z), never rounded.
    double VectorLengthX87(Vector const& V)
    {
        return std::sqrt(((double)(V.x) * V.x + (double)(V.y) * V.y) +
                         (double)(V.z) * V.z);
    }
}

// FUN_062e7a30. See common_aisteer.h for the receiver identification, the inverted reject
// tests and the swapped geometry arms.
IObjInspect* FindClosestObstacleToAvoid(SystemManagerTimerTable* Self,
                                                   unsigned ExcludeId,
                                                   unsigned TypeMask)
{
    SystemManager* const Sys = Self->Owner;
    FAiTarget* const OwnerElem = AiTargetOwnerElementOf(Sys);

    // ---- too slow to need to dodge anything ----
    Vector const OwnerVelocity = OwnerElem->Iface->get_velocity();
    // The rounded Speed is used later, but the test compares the unrounded length
    // (unordered continues).
    double const SpeedExact = VectorLengthX87(OwnerVelocity);
    float const Speed = (float)(SpeedExact);
    if (SpeedExact < 5.0) // .rdata:063a078c
        return nullptr;

    // ---- the owner's frame and swept radius ----
    // Retail calls slot 4 (get_orientation), an /OPT:ICF fold of get_transform (see
    // common_iface.h), and reads the Transform off it.
    Transform const& OwnerXform = OwnerElem->Iface->get_transform();
    OwnerElem->RefreshPos(); // FUN_062c47c0
    float const OwnerRadiusScaled = OwnerElem->CachedRadius * g_AiObstacleRadiusScale;

    // FUN_062e6150 returns the negated column 2 of the rotation -- this module's "forward",
    // so the dot test below reads as "is the candidate ahead".
    Vector Heading{ 0.0f, 0.0f, 0.0f };
    GetNegatedIfaceDirection(&Heading, AiOwnerObjectOf(Sys));

    // ---- engine-readiness gate ----
    // 0.5 is the double at .rdata:0639df88, so the product is evaluated in double; a NaN
    // readiness goes on.
    double const Readiness =
        (double)(Behavior_GetSpeed(AiOwnerObjectOf(Sys), 1.0f,
                                                         ThrustEquipTradelane)) * 0.5;
    if (Readiness <= 1.0) // .rdata:0639a1d0
        return nullptr;

    // ---- the look-ahead horizon ----
    // FUN_062e59b0. `stopping distance / speed + 6 seconds` is a time; Reach is how far the
    // ship travels in it. LookAheadTime is the rounded image, but ReachSq is built from the
    // unrounded time and rounded once.
    double const LookAheadTimeExact =
        (double)(Behavior_QueryChainedRadiusEffect(AiOwnerObjectOf(Sys), 0.0f,
                                                              ThrustEquipTradelane)) / Speed
        + 6.0; // .rdata:063a07e0
    float const LookAheadTime = (float)(LookAheadTimeExact);
    double const Reach = LookAheadTimeExact * Speed;
    float const ReachSq = (float)(Reach * Reach);

    // ---- the owner's own archetype box, inflated ----
    // Interleaved {MinX, MaxX, MinY, MaxY, MinZ, MaxZ}, the Aabb layout
    // TransformedBoxesIntersect takes; 1.5f is .rdata:0639e6d0.
    // Deviation: retail leaves the six slots uninitialised when get_archetype_extents fails
    // (-5 without SurfaceExtentsPtr) and still uses them; the port zero-fills them. The
    // watcher target is not null-tested, as in retail.
    float OwnerBox[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    {
        Vector OwnerMin{ 0.0f, 0.0f, 0.0f };
        Vector OwnerMax{ 0.0f, 0.0f, 0.0f };
        if (AiOwnerObjectOf(Sys)->get_archetype_extents(OwnerMin, OwnerMax) == 0)
        {
            OwnerBox[0] = OwnerMin.x * 1.5f;
            OwnerBox[1] = OwnerMax.x * 1.5f;
            OwnerBox[2] = OwnerMin.y * 1.5f;
            OwnerBox[3] = OwnerMax.y * 1.5f;
            OwnerBox[4] = OwnerMin.z * 1.5f;
            OwnerBox[5] = OwnerMax.z * 1.5f;
        }
    }

    // ---- who the owner is flying formation with ----
    // No null test on the watcher target, as in retail.
    IObjRW* OwnerFormationLeader = nullptr;
    AiOwnerObjectOf(Sys)->get_formation_leader(OwnerFormationLeader);

    // ---- the scanner, and its scan list ----
    // The one watcher read retail guards.
    IObjRW* const Scanner = AiOwnerObjectOf(Sys);
    if (Scanner == nullptr)
        return nullptr;

    // FUN_062e8270 (EquipStatus's inlined ctor) only masks bitfields of an uninitialised
    // local; only bit 0x02 of Flags is read and that mask clears it, so zero-init is
    // equivalent (as in BuildAiTargetList).
    EquipStatus Status = {};

    ScanList const* List = nullptr;
    Scanner->get_equipment_status(Status, Self->ScannerEquipId);
    if ((Status.Flags & EquipStatusFlagDestroyed) == 0)
    {
        // A destroyed scanner leaves List null: "no obstacle". No null test on the watcher
        // target, as in retail.
        AiOwnerObjectOf(Sys)->get_scanlist(List, kAvoidScanTypeMask, Self->bKeepCache != 0);
    }
    if (List == nullptr)
        return nullptr;

    // ---- one pass over the scanner's contacts ----
    // Count is compared unsigned and re-read every iteration, as in retail.
    IObjInspect* Result = nullptr;
    float BestDistance = 1.0e12f;                   // 0x5368d4a5

    // Retail keeps one 0x88-byte scratch record on its frame and re-Init()s it per
    // candidate; zero-filled once here, as BuildAiTargetList does.
    FAiTarget Candidate;
    std::memset(&Candidate, 0, sizeof(Candidate));

    for (unsigned Index = 0; Index < (unsigned)List->Count; ++Index)
    {
        // Retail also re-tests the adjusted pointer (dead: only a literal 0x8 entry hits it).
        IObjRW* const CandidateObj =
            ObjRwFromWatchable(const_cast<Watchable*>(List->Entries[Index].target()));
        if (CandidateObj == nullptr)
            continue;

        // The owner is the attitude reference (+0x80), the contact goes in +0x84.
        Candidate.Init(AiOwnerObjectOf(Sys), CandidateObj);

        // The caller's own type filter.
        RefreshAiTargetFlags(Candidate);
        if ((Candidate.CachedFlags & TypeMask) == 0)
            continue;

        Candidate.RefreshPos();

        // Is it ahead? Kept when in the forward half-space (reject on a strict dot < 0). The
        // sum stays unrounded in retail's operand order.
        Vector const ToCandidate = Sub(Candidate.CachedPos, OwnerXform.Translation);
        double const AheadDot =
            ((double)(Heading.x) * ToCandidate.x + (double)(Heading.z) * ToCandidate.z) +
            (double)(Heading.y) * ToCandidate.y;
        if (AheadDot < 0.0) // .rdata:06399408
            continue;

        // Inside the look-ahead? Reject on a strict LenSq > ReachSq (unrounded vs float).
        double const ToCandidateLenSq =
            ((double)(ToCandidate.x) * ToCandidate.x + (double)(ToCandidate.z) * ToCandidate.z) +
            (double)(ToCandidate.y) * ToCandidate.y;
        if (ToCandidateLenSq > ReachSq)
            continue;

        if (Candidate.Iface->get_id() == ExcludeId)
            continue;

        // Never dodge yourself. Retail asks the owner first, then the candidate; order kept
        // because both are virtual calls.
        {
            unsigned const OwnerId = OwnerElem->Iface->get_id();
            unsigned const CandidateId = Candidate.Iface->get_id();
            if (CandidateId == OwnerId)
                continue;
        }

        // The scan-list mask re-applied; the scanner can return contacts outside it.
        RefreshAiTargetFlags(Candidate);
        if ((Candidate.CachedFlags & kAvoidScanTypeMask) == 0)
            continue;

        // Too big to dodge (planets, stations): reject on a strict radius > 1000.
        Candidate.RefreshPos();
        if (Candidate.CachedRadius > 1000.0f) // .rdata:063a0608
            continue;

        // ---- pick the geometry arm and the horizon ----
        RefreshAiTargetFlags(Candidate);
        float EffectiveTime = LookAheadTime;
        // Bit 29 of the type flags.
        bool bUseBoxTest = ((Candidate.CachedFlags >> 29) & 1u) != 0;

        IObjRW* CandidateLeader = nullptr;
        Candidate.Iface->get_formation_leader(CandidateLeader);
        if (CandidateLeader != nullptr && OwnerFormationLeader != nullptr)
        {
            // Retail asks the candidate's leader first.
            unsigned const CandidateLeaderId = CandidateLeader->get_id();
            unsigned const OwnerLeaderId = OwnerFormationLeader->get_id();
            if (CandidateLeaderId == OwnerLeaderId)
            {
                // A wingman in the same formation gets the tight box test over
                // a much shorter horizon -- you are supposed to fly close to it.
                bUseBoxTest = true;
                EffectiveTime = 0.3f; // 0x3e99999a
            }
        }

        // ---- both bodies' travel over the horizon ----
        Candidate.RefreshPos();
        Vector const CandidatePos = Candidate.CachedPos;
        Vector const CandidateVelocity = Candidate.Iface->get_velocity();
        float const OwnerTravel = EffectiveTime * Speed;
        // The unrounded length times EffT, rounded once.
        float const CandidateTravel =
            (float)(VectorLengthX87(CandidateVelocity) * EffectiveTime);
        // Retail stores both travels negated; kept positive here with the sign moved to the
        // use sites.

        bool bWouldCollide = false;
        if (bUseBoxTest)
        {
            // ---- oriented-box sweep ----
            Vector CandidateMin{ 0.0f, 0.0f, 0.0f };
            Vector CandidateMax{ 0.0f, 0.0f, 0.0f };
            // Unlike the owner's extents, a failing query here rejects the candidate.
            if (Candidate.Iface->get_archetype_extents(CandidateMin, CandidateMax) != 0)
                continue;

            // The box swept along its own local -Z. Retail does not apply the 1.5f inflation to
            // the candidate, only to the owner.
            float const CandidateBox[6] = {
                CandidateMin.x, CandidateMax.x,
                CandidateMin.y, CandidateMax.y,
                CandidateMin.z - CandidateTravel, CandidateMax.z
            };

            // The owner's swept Z minimum changes per candidate; retail saves and restores it.
            float const SavedOwnerMinZ = OwnerBox[4];
            OwnerBox[4] = SavedOwnerMinZ - OwnerTravel;

            // Slot 4 again (folded with get_transform).
            Transform const& CandidateXform = Candidate.Iface->get_transform();
            // Retail tests the result "== 1"; same thing for a bool.
            bWouldCollide = TransformedBoxesIntersect(OwnerXform, OwnerBox,
                                                      CandidateXform, CandidateBox);

            OwnerBox[4] = SavedOwnerMinZ;
        }
        else
        {
            // ---- swept-sphere time of impact ----
            // FUN_062e6070; its only call site in the module.
            float const CandidateRadiusScaled =
                GetAiTargetEffectiveRadius(&Candidate) * g_AiObstacleRadiusScale;

            // The candidate's forward (negated column 2, FUN_062c5a70), as for the owner.
            Transform const& CandidateXform = Candidate.Iface->get_transform();
            Vector const CandidateForward{ -CandidateXform.Rotation.E[0][2],
                                           -CandidateXform.Rotation.E[1][2],
                                           -CandidateXform.Rotation.E[2][2] };

            // Where each body ends up after EffectiveTime on its current heading. CandidateEnd
            // rounds twice (FUN_0628b030 builds the scaled forward first); OwnerEnd keeps each
            // product unrounded and rounds the sum once.
            Vector const CandidateEnd = Add(CandidatePos, Scale(CandidateForward, CandidateTravel));
            Vector const OwnerEnd{
                (float)((double)(Heading.x) * OwnerTravel + OwnerXform.Translation.x),
                (float)((double)(Heading.y) * OwnerTravel + OwnerXform.Translation.y),
                (float)((double)(Heading.z) * OwnerTravel + OwnerXform.Translation.z) };

            // ImpactTime is never read; retail wants only the boolean.
            float ImpactTime = 0.0f;
            bWouldCollide = SweptSphereCollisionTime(OwnerRadiusScaled,
                                                     OwnerXform.Translation, OwnerEnd,
                                                     CandidateRadiusScaled,
                                                     CandidatePos, CandidateEnd,
                                                     &ImpactTime);
        }

        if (!bWouldCollide)
            continue;

        // ---- nearest wins ----
        // Retail's inlined fast length (CmnAiSteer::InlineFastLength). The new best must be
        // strictly nearer; ties and NaN keep the earlier candidate.
        double const Dx = (double)(CandidatePos.x) - OwnerXform.Translation.x;
        double const Dy = (double)(CandidatePos.y) - OwnerXform.Translation.y;
        double const Dz = (double)(CandidatePos.z) - OwnerXform.Translation.z;
        double const SeparationSq = (Dz * Dz + Dy * Dy) + Dx * Dx;
        double const Distance = InlineFastLength(SeparationSq);
        if (Distance < BestDistance)
        {
            BestDistance = (float)(Distance);
            Result = Candidate.Iface;
        }
    }

    return Result;
}
