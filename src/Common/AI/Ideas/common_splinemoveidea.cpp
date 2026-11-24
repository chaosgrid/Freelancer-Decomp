// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll SplineMoveIdea (ctor 0x062d9bf0, Update LAB_062d9d00). See
// src/include/common_idea.h.

#include "common_idea.h"
#include "AI/Ideas/common_idea_internal.h"
#include "common_aisteer.h"   // BearingAngleToPoint, GetIfacePositionOrZero
#include "AI/common_aisteer_internal.h"
#include "common_behavior.h"
#include "common_cequip.h"    // ThrustEquipType (IObjInspect::get_speed's 2nd argument)
#include "common_csys.h"      // Matrix / Transform
#include "common_gotobehavior.h" // FormationFollowerEnvelope (SplineMoveIdea::Update)
#include "common_iface.h"     // IObjInspect / IObjRW / IBehaviorManager / ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager (IBehaviorManager::Pathfind)
#include "common_systemmanager.h" // SystemManager / ThrustManager (BaseIdea::Handle)
#include "x86math.h"          // I3DMathEngine::TransformMatrix

#include <cmath>
#include <cstring>

using namespace CmnIdea;
using namespace CmnAiSteer;

// ----------------------------------------------------------------------------
// SplineMoveIdea
// ----------------------------------------------------------------------------

// FUN_062d9bf0. See common_idea.h for the RTTI walk and field citations. Retail
// leaves Spline/CoastDistance/SplineLength/PersistedT untouched; zeroed here as a
// port-only safety init.
SplineMoveIdea::SplineMoveIdea()
    : BaseIdea()
    , Move()
    , Face()
    , Spline(nullptr)
    , WideMode(0)
    , Pad151{}
    , BankBlendCap(1.0f)
    , StopStyle(0)
    , MaxSpeed15c(0.0f)
    , CoastDistance(0.0f)
    , SplineLength(0.0f)
    , StepNumerator(10.0f)
    , ExtrapolateBase(50.0f)
    , PersistedT(0.0f)
{
}

// Own vtable slot 0 override -- FUN_062d9c80, RET 0xc. All three context pointers
// go to both embedded members (Ghidra shows two arguments; the listing pushes three).
bool SplineMoveIdea::SetContext(void* InObject, void* InHandle, void* InContext)
{
    if (InObject == nullptr || InHandle == nullptr)
    {
        return false;
    }

    Attached = 1;
    Object = InObject;
    Handle = (SystemManager*)InHandle;
    Context = InContext;

    // Real virtual dispatch through each member's own slot 0.
    Move.SetContext(InObject, InHandle, InContext);
    Face.SetContext(InObject, InHandle, InContext);

    Spline = nullptr;

    // Context is dereferenced unguarded, as retail does.
    *reinterpret_cast<int*>(SlotArrayOf(Context) + 0x64) = 0;

    PersistedT = 0.0f;
    return true;
}

// Own vtable slot 4 override -- LAB_062da310.
void SplineMoveIdea::Commit()
{
    *reinterpret_cast<int*>(SlotArrayOf(Context) + 0x64) = 0;

    Move.Commit();
    Face.Commit();

    Vector const ZeroVector{ 0.0f, 0.0f, 0.0f };
    Handle->Thrust.SetNudgeDirection(ZeroVector); // never tested

    PersistedT = 0.0f;
}

// Folded at .text:062da4d0 with IBehaviorManager::set_camera_mode --
// `MOV [ECX+0x154],EAX; RET 4`. See the declaration for the receiver proof.
void SplineMoveIdea::SetBankBlendCap(float Value)
{
    BankBlendCap = Value;
}

// FUN_062da4e0. No Mode ladder: the thrust type is always ThrustEquipEngine.
void SplineMoveIdea::SetThrottleScale(float Value)
{
    BankBlendCap = Behavior_GetProjectedThrottle(ObjInspectOf(Object), Value, ThrustEquipEngine);
}

// FUN_062da400 -- `MOV [ECX+0x158],EAX; RET 4`. See the declaration for the
// receiver proof.
void SplineMoveIdea::SetStopStyle(int Value)
{
    StopStyle = Value;
}

// FUN_062da410, RET 4. Retail's branch tree is MoveIdea::SetMode (FUN_062d5980)
// inlined on Move with NewMode == (bWide ? 2 : 0); every transition matches.
void SplineMoveIdea::SetWideMode(unsigned char Value)
{
    WideMode = Value; // the caller's byte stored raw
    Move.SetMode(Value != 0 ? 2 : 0);
}

// FUN_062da380, RET 4.
void SplineMoveIdea::SetSpline(ICRSplineSegment* NewSpline)
{
    Move.ArrivalRange = ExtrapolateBase;

    Spline = NewSpline;
    Move.StopStyle = 2;
    Move.bKillEngineOffBearing = 0;

    // FUN_062e5af0: get_speed at full engine throttle.
    MaxSpeed15c = Behavior_GetSpeed(ObjInspectOf(Object), 1.0f, ThrustEquipEngine);

    // FUN_062e5ab0 consumes the rounded result above.
    CoastDistance = Behavior_GetCoastDistance(Object, MaxSpeed15c);

    // No null test on the spline, as retail.
    SplineLength = Spline->get_spline_length();
}

// Own vtable slot 1 -- LAB_062d9d00, RET 4. Returns 3 when not attached / no spline
// / no object, 0 while steering, 1 once the segment end is reached and the idea
// has committed itself.
int SplineMoveIdea::Update(float Dt)
{
    if (Attached == 0)
        return 3;
    if (Spline == nullptr)
        return 3;
    if (Object == nullptr)
        return 3;

    // FUN_062ebf50 on Handle+0x1364 into an envelope retail zeroes first. Its
    // Throttle is read twice: here and as the tail's first min-clamp.
    FormationFollowerEnvelope Envelope;
    Envelope.Speed = 0.0f;
    Envelope.Throttle = 0.0f;
    Envelope.AngularSpeed = 0.0f;
    Envelope.AngularThrottle = 0.0f;
    if (Handle->FormationLeaders.ComputeFormationFollowerEnvelope(&Envelope))
        Move.Throttle = Envelope.Throttle;

    FAiTarget* const Elem = &Handle->SelfTarget;

    // FAiTarget::RefreshPos, inlined by retail.
    Elem->RefreshPos();
    // The centre is copied once; both get_t_from_point and the look-ahead use the copy.
    Vector const ElemPos = Elem->CachedPos;

    float const T1 = Spline->get_t_from_point(ElemPos, PersistedT, StepNumerator / SplineLength);
    PersistedT = T1;
    // Retail samples at T1 and never reads the result.
    (void)Spline->calculate_spline_vector(T1);

    // Unrounded |v| against 1.0f; not-less (and NaN) normalises the velocity.
    Vector const RawForward = ObjInspectOf(Object)->get_velocity();
    double const RawForwardLen = std::sqrt(((double)RawForward.y * RawForward.y + (double)RawForward.z * RawForward.z) +
                                           (double)RawForward.x * RawForward.x);
    Vector DirA;
    if (!(RawForwardLen < 1.0))
    {
        VecNormalize3(&RawForward, &DirA);
    }
    else
    {
        // Nearly stopped: use the heading, -(column 2).
        Vector const Column2 = MatrixColumn2(Elem->Iface->get_transform().Rotation);
        DirA.x = -Column2.x;
        DirA.y = -Column2.y;
        DirA.z = -Column2.z;
    }

    // LookaheadPoint = ElemPos + DirA * CoastDistance (the element position, not the
    // spline sample); the scaled vector rounds before the add.
    Vector const ScaledDirA{ DirA.x * CoastDistance, DirA.y * CoastDistance, DirA.z * CoastDistance };
    Vector const LookaheadPoint{ ElemPos.x + ScaledDirA.x, ElemPos.y + ScaledDirA.y, ElemPos.z + ScaledDirA.z };
    float const T2 = Spline->get_t_from_point(LookaheadPoint, T1, StepNumerator / SplineLength);

    bool const bWide = WideMode != 0;
    double BankBlend = 1.0f;

    // Both arms refresh the cached radius again and sample the far point at
    // T3 = ((Radius + bonus) / SplineLength) + T2, rounded once.
    Elem->RefreshPos();
    float const LookaheadRadiusBonus = bWide ? 200.0f : 50.0f; // DAT_0639b22c / DAT_0639e6d4
    float const T3 = (float)(((double)Elem->CachedRadius + LookaheadRadiusBonus) / SplineLength + T2);
    Vector const FarPoint = Spline->calculate_spline_vector(T3);

    if (!bWide)
    {
        Vector const Delta = Sub(FarPoint, LookaheadPoint);
        // Unrounded |Delta| against 0.99989998f (DAT_0639f538); not-less (and NaN)
        // takes the dot arm.
        double const DeltaLen = std::sqrt(((double)Delta.y * Delta.y + (double)Delta.z * Delta.z) +
                                          (double)Delta.x * Delta.x);
        double BankFactor = 1.0f;
        if (!(DeltaLen < 0.99989998f))
        {
            // Unrounded dot, floored at 0.1f (NaN kept).
            Vector NormDelta;
            VecNormalize3(&Delta, &NormDelta);
            double const Dot = ClampedDot3(NormDelta, DirA);
            BankFactor = (Dot < 0.1f) ? (double)0.1f : Dot;
        }

        // FUN_062e5bc0 then FUN_062e5b70(Object, that, Engine).
        IObjInspect const* const Inspect = ObjInspectOf(Object);
        float const SlotDCResult = Behavior_GetInitialSpeedToCoastDistance(Inspect, (float)(BankFactor * CoastDistance));
        BankBlend = Behavior_GetProjectedThrottle(Inspect, SlotDCResult, ThrustEquipEngine);
    }
    else
    {
        // MoveIdea::SetMode(2) inlined.
        if (Move.Mode != 2 && Move.Mode == 1)
        {
            Vector ZeroDir{ 0.0f, 0.0f, 0.0f };
            Move.Handle->Thrust.SetNudgeDirection(ZeroDir);
        }
        Move.Mode = 2;
    }

    // Two min-clamps on the unrounded value: the first keeps it on less or
    // unordered, the second on less, equal or unordered.
    if (!(BankBlend < Envelope.Throttle))
        BankBlend = Envelope.Throttle;
    if (BankBlend > BankBlendCap)
        BankBlend = BankBlendCap;

    // Move's bearing default back to pi (0x40490fdb).
    Move.Throttle = (float)BankBlend;
    Move.MaxThrustBearing = BitsToFloat(0x40490fdbu);
    Move.TargetKind48 = 2;

    // Retail dereferences the Context->Pathfind chain unconditionally.
    Move.Position = Spline->P2;
    ((IBehaviorManager*)Move.Context)->Pathfind->CachedTargetPosition = Move.Position;

    ((IBehaviorManager*)Move.Context)->Pathfind->NudgeObstacleTypeMask = 0x30038010;

    int const MoveResult = Move.Update(Dt);

    // Against the double 1.0 (DAT_06399410): not-less (and NaN) faces along the
    // chord P3 - P2 (tag 3), less faces FarPoint (tag 2).
    if (!((double)T2 < 1.0))
    {
        Vector const Chord{ Spline->P3.x - Spline->P2.x, Spline->P3.y - Spline->P2.y, Spline->P3.z - Spline->P2.z };
        Face.Direction28 = Chord;
        Face.TargetKind = 3;
    }
    else
    {
        Face.Position1c = FarPoint;
        Face.TargetKind = 2;
    }
    (void)Face.Update(Dt);

    // Probe only when Move returned 1, or else when T1 > the double 1.0.
    if (MoveResult != 1 && !((double)T1 > 1.0))
        return 0;

    // ProbePos = P2 + normalize(P3 - P2) * s, s = -ExtrapolateBase - 1.0f unrounded;
    // the x and y products round before their adds, z's does not.
    Vector const Chord{ Spline->P3.x - Spline->P2.x, Spline->P3.y - Spline->P2.y, Spline->P3.z - Spline->P2.z };
    Vector ChordDir;
    VecNormalize3(&Chord, &ChordDir);
    double const ExtrapolateFactor = -(double)ExtrapolateBase - 1.0f;
    float const ScaledX = (float)(ChordDir.x * ExtrapolateFactor);
    float const ScaledY = (float)(ChordDir.y * ExtrapolateFactor);
    Vector const ProbePos{
        ScaledX + Spline->P2.x,
        ScaledY + Spline->P2.y,
        (float)(ExtrapolateFactor * ChordDir.z + Spline->P2.z)
    };
    // FUN_062e53e0.
    if (!IsPointOutsideHeadingCone(ProbePos, ObjInspectOf(Object), 0.0f))
        return 0;

    // Commit() is a virtual call through this object's slot 4.
    Move.StopStyle = StopStyle;
    Commit();
    return 1;
}
