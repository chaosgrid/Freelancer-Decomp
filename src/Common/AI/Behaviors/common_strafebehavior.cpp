// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_strafebehavior.h"

#include "common_csys.h"           // Transform, Matrix, Acquire3DMathEngine
#include "common_iface.h"          // IObjInspect, IObjRW, ObjRwFromWatchable, ObjInspectOf
#include "common_systemmanager.h"  // SystemManager -- what SysMgr actually points at
#include "common_misc.h"           // random_sphere_constrained_2
#include "common_behavior.h"       // Behavior_GetIfaceMaxScalarAttribute and friends
#include "common_aisteer.h"        // GetIfacePositionOrZero, GetObjectTypeOrZero, FAiTarget,
                                   // BearingAngleToIface
#include "common_cobjdb.h"         // IObjDB::GetZoneDb
#include "common_universe.h"       // Universe::ISystem::GetId
#include "common_physys.h"         // PhySys::FindRayCollisions
#include "x86math.h"               // I3DMathEngine::TransformXform
#include "AI/common_aisteer_internal.h" // CmnAiSteer::InlineFastLength

#include <cmath>

namespace
{
    // SelectStrafePoint's inline normalize (062c991d, 062c9c16): (z*z + y*y) + x*x, the
    // reciprocal kept unrounded, each component rounded once. No zero-length guard, as retail.
    void NormalizeInlineX87(Vector& V)
    {
        double const Inv = 1.0 / std::sqrt(((double)V.z * V.z + (double)V.y * V.y) + (double)V.x * V.x);
        V.x = (float)(V.x * Inv);
        V.y = (float)(V.y * Inv);
        V.z = (float)(V.z * Inv);
    }

    // The other normalize shape (062c9a0f, 062c9a61): FUN_06347e60's unrounded
    // sqrt((x*x + y*y) + z*z), then the same per-component rounding.
    void NormalizeByVectorLengthX87(Vector& V)
    {
        double const Inv = 1.0 / std::sqrt(((double)V.x * V.x + (double)V.y * V.y) + (double)V.z * V.z);
        V.x = (float)(V.x * Inv);
        V.y = (float)(V.y * Inv);
        V.z = (float)(V.z * Inv);
    }

    // The SystemManager's own watched object (SystemManager::Watcher, +0x08), minus 8, or a
    // null receiver -- retail dispatches through it either way.
    IObjInspect const* WatchedSelf(SystemManager* Sys)
    {
        return ObjInspectOf(ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target())));
    }

    // The SystemManager+0x14fc self record (an FAiTarget, see common_aitarget.cpp).
    FAiTarget* SelfRecord(SystemManager* Sys)
    {
        return &Sys->SelfTarget;
    }
}

// ----------------------------------------------------------------------------
// FUN_062c9540 (vtable 0639f9dc). MoveIdea +0x70, FaceIdea +0xe8, AfterburnerIdea +0x1a8
// (EH states 0/1/2 follow the member order). The port zeroes the fields retail leaves to
// SetContext/OnEngage (+0x2c..+0x6c); every one is written there before a reader runs.
// ----------------------------------------------------------------------------
StrafeBehavior::StrafeBehavior(int InTypeId)
    : Behavior(InTypeId)
    , TriggerRange2c(0.0f)
    , Opaque30{}
    , StrafeDistance34(0.0f)
    , StrafeDir38{}
    , ForwardExtent44(0.0f)
    , StrafeMode48(kStrafeModeStart)
    , StrafePoint4c{}
    , ScaleNear58(0.0f)
    , ScaleFar5c(0.0f)
    , Field60(2)
    , Field64(0)
    , Pad65{}
    , Field68(0)
    , Field6c(0)
    , Move()
    , Face()
    , Afterburner()
{
}

// FUN_062c95e0: the FaceIdea (+0xfc) and MoveIdea (+0x84) watcher releases are their member
// destructors; ~Behavior (FUN_062d2080) chains after.
StrafeBehavior::~StrafeBehavior()
{
}

// ----------------------------------------------------------------------------
// Slot 1, LAB_062c9660. The ideas get the raw Owner/Sys arguments, FaceIdea first.
// ----------------------------------------------------------------------------
bool StrafeBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    bool const bOk = BindContext(Owner, Manager, Sys);
    if (!bOk)
        return false;
    Face.SetContext(Owner, Sys, Manager);
    Move.SetContext(Owner, Sys, Manager);
    Afterburner.SetContext(Owner, Sys, Manager);
    Field60 = 2;
    StrafeMode48 = kStrafeModeStart;
    Field64 = 0;
    Field6c = 0;
    Field68 = 999;
    return bOk;
}

// ----------------------------------------------------------------------------
// Slot 10, LAB_062c96e0.
// ----------------------------------------------------------------------------
bool StrafeBehavior::OnApplyDirective(void* DirectiveOp)
{
    if (DirectiveOp != nullptr)
    {
        unsigned const* const OpTarget = &static_cast<pub::AI::DirectiveStrafeOp const*>(DirectiveOp)->TargetId;
        if (SysMgr->Timers.FindVerifiedObject(OpTarget) != nullptr)
        {
            Flags28 = static_cast<unsigned char>(Flags28 | kBehaviorFlagDirectiveAccepted);
            return true;
        }
    }
    Flags28 = static_cast<unsigned char>(Flags28 & ~kBehaviorFlagDirectiveAccepted);
    return false;
}

// ----------------------------------------------------------------------------
// Slot 5, LAB_062c9720.
// ----------------------------------------------------------------------------
int StrafeBehavior::OnEvaluateReadiness(float Unused)
{
    (void)Unused;
    SystemManager* const Sys = SysMgr;
    int Result = 2;
    if (!Sys->FormationLeaders.FollowerDirectiveGateA())   // FUN_062ecb20
    {
        IObjRW* const Target = Sys->Targeting.GetTarget();         // FUN_062ed5a0
        if (Target != nullptr &&
            (GetObjectTypeOrZero(Target) & (OBJ_FIGHTER | OBJ_FREIGHTER | OBJ_GUNBOAT)) == 0)
        {
            // FUN_062ca1b0 (FAiTarget::GetFlags, latch +0x7d).
            if ((SelfRecord(Sys)->GetFlags() & (OBJ_FIGHTER | OBJ_FREIGHTER | OBJ_GUNBOAT)) != 0)
            {
                // DAT_0639fa48 = 400.0f; unordered takes it too.
                Vector const Velocity = Target->get_velocity();
                double const SpeedSq =
                    (static_cast<double>(Velocity.x) * Velocity.x + static_cast<double>(Velocity.y) * Velocity.y) +
                    static_cast<double>(Velocity.z) * Velocity.z;
                if (!(SpeedSq > 400.0))
                {
                    Result = 1;
                    Field60 = 1;
                }
            }
        }
    }
    if (Flag29 != 0)
    {
        Readiness = 3;
        return 3;
    }
    Readiness = Result;
    return Result;
}

// ----------------------------------------------------------------------------
// Slot 3, LAB_062ca1e0.
// ----------------------------------------------------------------------------
void StrafeBehavior::OnEngage()
{
    Behavior::OnEngage();
    SetAvoidanceAllowed(1, kAvoidanceObstacleNudge | kAvoidancePoint);                                            // FUN_062d21d0

    IObjInspect const* const Owner = ObjInspectOf(OwnerContext);
    SystemManager* const Sys = SysMgr;

    // Step starts at 0.1f and grows by 0.1f (DAT_0639d20c) after each call; the 10.0 bound
    // is tested before the result against pi/2.
    float Step = 0.1f;
    float Angular = 0.0f;
    for (;;)
    {
        Angular = Behavior_GetAngularDistanceTravelled(Owner, 1.0f, 0.0f, 0.0f, Step);
        double const Next = static_cast<double>(Step) + static_cast<double>(0.1f);
        Step = static_cast<float>(Next);
        if (Next > 10.0)
            break;
        if (!(static_cast<double>(Angular) < 1.5707963705062866 ||
              static_cast<double>(Angular) == 1.5707963705062866))
            break;
    }

    // FUN_062e5af0 (engine speed), times (Step + 1.0).
    float const Speed = Behavior_GetSpeed(Owner, 1.0f, ThrustEquipEngine);
    TriggerRange2c = static_cast<float>(static_cast<double>(Speed) *
                                        (static_cast<double>(Step) + 1.0));

    Vector ExtentMin{ 0.0f, 0.0f, 0.0f };
    Vector ExtentMax{ 0.0f, 0.0f, 0.0f };
    (void)WatchedSelf(Sys)->get_archetype_extents(ExtentMin, ExtentMax);
    ForwardExtent44 = ExtentMin.z;

    Transform const& Xform = Sys->SelfTarget.Iface->get_transform();
    Vector const Column2 = MatrixColumn2(Xform.Rotation);
    Field64 = 0;
    StrafeDir38 = Vector{ -Column2.x, -Column2.y, -Column2.z };

    // Personality.StrafeParams (Sys+0x16c8/+0x16cc/+0x16d0).
    pub::AI::Personality::StrafeUseStruct const& Params = Sys->PersonalityData.StrafeParams;
    StrafeDistance34 = Params.StrafeRunAwayDistance;
    ScaleNear58 = Params.StrafeAttackThrottle;
    ScaleFar5c = Params.StrafeTurnThrottle;
    StrafeMode48 = kStrafeModeStart;
}

// ----------------------------------------------------------------------------
// Slot 4, FUN_062ca310.
// ----------------------------------------------------------------------------
void StrafeBehavior::OnDisengage()
{
    Move.PublishTargetId(0);                                  // FUN_062d4e50
    Move.Commit();
    Face.Commit();
    Afterburner.Commit();
    Behavior::OnDisengage();
}

// ----------------------------------------------------------------------------
// Slot 7, LAB_062c9d20.
// ----------------------------------------------------------------------------
int StrafeBehavior::OnUpdate()
{
    SystemManager* const Sys = SysMgr;

    IObjRW* const Target = Sys->Targeting.GetTarget();
    if (Target == nullptr)
    {
        UpdateResult = kBehaviorUpdateAborted;
        return kBehaviorUpdateAborted;
    }

    Move.PublishTargetId(Target->get_id());

    // Needs type bits 0x70000 clear on the target and set on the self record, and target
    // speed squared <= 400 (unordered continues); otherwise 3.
    if ((GetObjectTypeOrZero(Target) & (OBJ_FIGHTER | OBJ_FREIGHTER | OBJ_GUNBOAT)) != 0 ||
        (SelfRecord(Sys)->GetFlags() & (OBJ_FIGHTER | OBJ_FREIGHTER | OBJ_GUNBOAT)) == 0)
    {
        UpdateResult = kBehaviorUpdateAborted;
        return kBehaviorUpdateAborted;
    }
    {
        Vector const Velocity = Target->get_velocity();
        double const SpeedSq =
            (static_cast<double>(Velocity.x) * Velocity.x + static_cast<double>(Velocity.y) * Velocity.y) +
            static_cast<double>(Velocity.z) * Velocity.z;
        if (SpeedSq > 400.0)
        {
            UpdateResult = kBehaviorUpdateAborted;
            return kBehaviorUpdateAborted;
        }
    }

    FAiTarget* const Self = SelfRecord(Sys);
    int Result = 1;
    Self->RefreshPos();                                             // FUN_062c47c0
    Vector const& SelfPos = Self->CachedPos;

    if (StrafeMode48 == kStrafeModeAttack)
    {
        // MoveIdea scale by the unrounded bearing to the target, split at pi/8 (>= and
        // unordered take the far value).
        double const Bearing = BearingAngleToIface(OwnerContext, Target);
        if (Bearing < 0.39269909262657166)
            Move.SetScale40(ScaleNear58);
        else
            Move.SetScale40(ScaleFar5c);

        // Ahead = Transform * (0, 0, ForwardExtent44 * 1.2). Retail fetches the transform
        // through slot 4 (get_orientation), ICF-folded with slot 3 get_transform, and
        // TransformXform reads its translation at +0x24; the port asks slot 3 explicitly.
        Vector const Local{ 0.0f, 0.0f,
                            static_cast<float>(static_cast<double>(ForwardExtent44) * 1.2) };
        Transform const& Xform = Sys->SelfTarget.Iface->get_transform();
        Vector Ahead{ 0.0f, 0.0f, 0.0f };
        Acquire3DMathEngine()->TransformXform(Ahead, Xform, Local);

        // DAT_0639f7a4 = 10000.0f.
        Matrix const& Orientation = Sys->SelfTarget.Iface->get_orientation();
        Vector const Column2 = MatrixColumn2(Orientation);
        Vector const Backward{ -Column2.x, -Column2.y, -Column2.z };
        Vector const Reach{ Backward.x * 10000.0f, Backward.y * 10000.0f, Backward.z * 10000.0f };
        Vector const RayEnd{ Ahead.x + Reach.x, Ahead.y + Reach.y, Ahead.z + Reach.z };

        // The PhySys system id via the manager's object database; one hit at most.
        IBehaviorManager* const Manager = Sys->Owner;
        Universe::ISystem const* const System = Manager->ObjectDb->GetZoneDb();
        PhySys::RayHit Hit;
        int const HitCount = PhySys::FindRayCollisions(System->GetId(), Ahead, RayEnd, &Hit, 1);

        Vector Gap{ 0.0f, 0.0f, 0.0f };
        if (HitCount > 0)
        {
            Gap = Vector{ Ahead.x - Hit.Point.x, Ahead.y - Hit.Point.y, Ahead.z - Hit.Point.z };
        }
        else
        {
            Vector const& TargetPos = Target->get_position();
            Gap = Vector{ SelfPos.x - TargetPos.x, SelfPos.y - TargetPos.y, SelfPos.z - TargetPos.z };
        }

        // The inlined 0x5f3759df fast length on the unrounded squared length; >= skips.
        double const GapSq = (static_cast<double>(Gap.z) * Gap.z + static_cast<double>(Gap.y) * Gap.y)
                           + static_cast<double>(Gap.x) * Gap.x;
        double const GapLength = CmnAiSteer::InlineFastLength(GapSq);
        if (GapLength < TriggerRange2c)
            SelectStrafePoint(kStrafeModeRunAway, Target);
        Result = 0;
    }
    else if (StrafeMode48 == kStrafeModeRunAway)
    {
        // Toward = (TargetPos - SelfPos) / fast length; only the length is stored to float.
        Vector const& TargetPos = Target->get_position();
        Vector Toward{ TargetPos.x - SelfPos.x, TargetPos.y - SelfPos.y, TargetPos.z - SelfPos.z };
        double const TowardSq = (static_cast<double>(Toward.z) * Toward.z
                                 + static_cast<double>(Toward.y) * Toward.y)
                              + static_cast<double>(Toward.x) * Toward.x;
        float const Length = static_cast<float>(CmnAiSteer::InlineFastLength(TowardSq));
        double const Inv = 1.0 / static_cast<double>(Length);
        Toward.x = static_cast<float>(Toward.x * Inv);
        Toward.y = static_cast<float>(Toward.y * Inv);
        Toward.z = static_cast<float>(Toward.z * Inv);

        // Only a strictly negative facing dot continues (DAT_0639c430 = 0.0 double).
        Matrix const& Orientation = Sys->SelfTarget.Iface->get_orientation();
        Vector const Column2 = MatrixColumn2(Orientation);
        Vector const Backward{ -Column2.x, -Column2.y, -Column2.z };
        double const Facing = (static_cast<double>(Backward.z) * Toward.z
                               + static_cast<double>(Backward.y) * Toward.y)
                            + static_cast<double>(Backward.x) * Toward.x;
        if (Facing < 0.0)
        {
            // FUN_062e6010(Target) + StrafeDistance34, strictly less than Length.
            double const Reach = static_cast<double>(Behavior_GetIfaceMaxScalarAttribute(Target)) +
                                 static_cast<double>(StrafeDistance34);
            if (Reach < static_cast<double>(Length))
                SelectStrafePoint(kStrafeModeAttack, Target);
        }
    }
    else if (StrafeMode48 == kStrafeModeStart)
    {
        SelectStrafePoint(kStrafeModeAttack, ObjInspectOf(Target));
    }

    (void)Move.Update(DeltaTime);
    (void)Face.Update(DeltaTime);
    UpdateResult = Result;
    return Result;
}

// ----------------------------------------------------------------------------
// FUN_062c9810. Retail null-tests neither SysMgr, the watched object nor the sub-object
// centre receiver.
// ----------------------------------------------------------------------------
void StrafeBehavior::SelectStrafePoint(int Mode, IObjInspect const* Target)
{
    SystemManager* const Sys = SysMgr;

    if (Mode == kStrafeModeRunAway)
    {
        // This ship (SystemManager::Watcher's object).
        Vector const SelfPos = WatchedSelf(Sys)->get_position();

        Vector const TargetPos = Target->get_position();

        // FUN_062e6010: max(physical radius, atmosphere range).
        float const TargetExtent = Behavior_GetIfaceMaxScalarAttribute(Target);

        Vector const Delta{ SelfPos.x - TargetPos.x, SelfPos.y - TargetPos.y, SelfPos.z - TargetPos.z };

        // Summed in double and rounded once; the float copy seeds the 0x5f3759df guess.
        float const DeltaLenSq =
            (float)(((double)Delta.z * Delta.z + (double)Delta.y * Delta.y) + (double)Delta.x * Delta.x);

        Vector ToTarget{ TargetPos.x - SelfPos.x, TargetPos.y - SelfPos.y, TargetPos.z - SelfPos.z };
        NormalizeInlineX87(ToTarget);

        // Fast length, compared unrounded.
        double const Distance = CmnAiSteer::InlineFastLength(DeltaLenSq);

        // >= and unordered take the else arm.
        if (Distance < TargetExtent)
        {
            StrafeDir38 = random_sphere_constrained_2(ToTarget, 0.0f, 0.15707964f);   // 0x3e20d97c
        }
        else
        {
            // cross(ToTarget, (0,1,0)) with the multiplications by 1 folded and the ones by
            // 0.0f (DAT_06399408) kept.
            StrafeDir38 = Vector{ ToTarget.y * 0.0f - ToTarget.z,
                                  ToTarget.z * 0.0f - ToTarget.x * 0.0f,
                                  ToTarget.x - ToTarget.y * 0.0f };
            NormalizeByVectorLengthX87(StrafeDir38);
            StrafeDir38 = random_sphere_constrained_2(StrafeDir38, 0.0f, 0.34906587f);  // 0x3eb2b8c3
        }

        // Flatten to the horizontal plane and renormalise.
        StrafeDir38.y = 0.0f;
        NormalizeByVectorLengthX87(StrafeDir38);

        StrafePoint4c = Vector{ SelfPos.x + StrafeDir38.x * StrafeDistance34,
                                SelfPos.y + StrafeDir38.y * StrafeDistance34,
                                SelfPos.z + StrafeDir38.z * StrafeDistance34 };

        Move.SetPosition(StrafePoint4c);   // FUN_062d5840
        Face.SetPosition(StrafePoint4c);   // FUN_062d7560

        if (Sys->SelfTarget.bPosValid == 0)
        {
            Sys->SelfTarget.bPosValid = 1;
            Sys->SelfTarget.Iface
                ->get_physical_radius(Sys->SelfTarget.CachedRadius, Sys->SelfTarget.CachedPos);
        }

        // Radius times the double 5.0 (DAT_0639fa58).
        Move.SetRange3c(static_cast<float>(static_cast<double>(Sys->SelfTarget.CachedRadius) * 5.0));

        StrafeMode48 = Mode;
        return;
    }

    if (Mode == kStrafeModeAttack)
    {
        Vector TargetPos{ 0.0f, 0.0f, 0.0f };
        GetIfacePositionOrZero(&TargetPos, const_cast<IObjInspect*>(Target));
        StrafePoint4c = TargetPos;

        // This ship's own interface (Sys+0x1580).
        IObjInspect const* const SelfExternal = Sys->SelfTarget.Iface;
        unsigned short SubtargetId = 0;
        SelfExternal->get_subtarget(SubtargetId);

        if (SubtargetId != SUBOBJ_ID_NONE)
        {
            // Retail asks a second time and uses the second answer.
            unsigned short SubtargetIdAgain = 0;
            SelfExternal->get_subtarget(SubtargetIdAgain);

            // Non-zero skips.
            Vector SubCenter{ 0.0f, 0.0f, 0.0f };
            int const SubCenterFailed =
                WatchedSelf(Sys)->get_sub_obj_center_of_mass(SubtargetIdAgain, SubCenter);

            if (SubCenterFailed == 0)
            {
                // From the subtarget centre to the target's centre of mass.
                Vector const TargetCoM = Target->get_center_of_mass();
                Vector Toward{ TargetCoM.x - SubCenter.x, TargetCoM.y - SubCenter.y, TargetCoM.z - SubCenter.z };
                NormalizeInlineX87(Toward);

                Transform const& SelfXform = SelfExternal->get_transform();
                Vector const Column2 = MatrixColumn2(SelfXform.Rotation);
                Vector Backward{ 0.0f, 0.0f, 0.0f };
                VecNegate3(&Column2, &Backward);

                // Summed in double against 0.2 (DAT_0639fa50); only strictly greater aims at
                // the subtarget.
                double const Facing =
                    ((double)Backward.z * Toward.z + (double)Backward.y * Toward.y) + (double)Backward.x * Toward.x;
                if (Facing > 0.2)
                    StrafePoint4c = SubCenter;
            }
        }

        Move.SetField44(kMoveStopFlyThrough);                // FUN_062d88b0
        Move.SetRange3c(1.0f);             // ICF fold 062a66b0
        Move.SetPosition(StrafePoint4c);
        Face.SetPosition(StrafePoint4c);
    }

    StrafeMode48 = Mode;
}
