// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// LaneAction (trade-lane transit controller). Declared in common_cequip.h. Retail re-derives the watched traveller (`head ? head - 8 : 0`)
// at every use; TradelaneObj() is that expression, and nothing between two uses changes it.
#include "common_cequip.h"
#include "x86math.h"

#include "common_cobject.h"
#include "common_cobjdb.h"   // IObjDB::FindById (the ring hop)
#include "common_csys.h"     // Acquire3DMathEngine, MatrixSetIdentity
#include "common_iface.h"    // IObjRW / IObjInspect / IObjAffect
#include "common_ini.h"
#include "common_misc.h"     // SinglePlayer
#include "common_physys.h"
#include "common_universe.h"
#include "ivp_physics/ivp_controller.h"  // IVP_Event_Sim (do_simulation_controller)
#include "x87_trig.h"        // retail's inline FSIN / FCOS (BuildRingTransform)

#include <math.h>
#include <string.h>

namespace
{
    // FUN_0628b070: Out = MatrixToQuaternion(In) through the 3D math engine (slot 0x50, no
    // null test); returns &Out.
    Quaternion* MatrixToQuaternionViaMathEngine(Quaternion& Out, Matrix const& In)
    {
        I3DMathEngine* Math = Acquire3DMathEngine();
        Math->MatrixToQuaternion((FQuaternion&)Out, (FMatrix const&)In); // +0x50
        return &Out;
    }
}

IObjRW* LaneAction::TradelaneObj() const
{
    return const_cast<IObjRW*>(static_cast<IObjRW const*>(TradelaneObjWatcher.target()));
}

LaneAction::LaneAction(IObjInspect const* InEntranceRing, IObjInspect const* InExitRing, IObjRW* InTradelaneObj,
                       bool bFastExit, float InSpeed)
{
    // FUN_062c1220. Deviation: retail leaves +0xcc..+0xe7 (filled by InitializeApproach) and
    // +0xf9..+0xfb unwritten; the port zeroes them.
    CapturedShipPos = Vector{ 0.0f, 0.0f, 0.0f };
    memset(&CapturedShipOrient, 0, sizeof(CapturedShipOrient));
    Opaque_F8[1] = 0;
    Opaque_F8[2] = 0;
    Opaque_F8[3] = 0;

    // The ring slots are left for BuildRingTransform.
    MatrixSetIdentity(LegStartXform.Orient);
    LegStartXform.Pos = Vector{ 0.0f, 0.0f, 0.0f };
    MatrixSetIdentity(LegEndXform.Orient);
    LegEndXform.Pos = Vector{ 0.0f, 0.0f, 0.0f };
    MatrixSetIdentity(LanePose.Rotation);
    LanePose.Translation = Vector{ 0.0f, 0.0f, 0.0f };

    TradelaneObjWatcher.set(InTradelaneObj);

    EntranceRing = InEntranceRing;   // +0x20
    ExitRing = InExitRing;           // +0x24

    // Set only when the ring after the entrance ring is the exit ring. No null tests.
    bHpLeftLane = 1;
    unsigned NextRingId = 0;
    EntranceRing->traverse_rings(NextRingId, true);
    if (NextRingId != ExitRing->get_id())
        bHpLeftLane = 0;
    Field95 = 0;
    Opaque_F8[0] = 0;

    BuildRingTransform(EntranceRing, LegStartXform);
    BuildRingTransform(ExitRing, LegEndXform);

    bRingApproachValid = bFastExit ? 1 : 0;   // +0x97
    Speed = InSpeed;                          // +0x98
    LaneState = LANE_STOPPED;
    bExitFlag = 0;
    TransitElapsed = 0.0f;
    TransitRamp = 0.0f;
    TransitRampTarget = 0.0f;
    ExitRampStart = 0.0f;

    InitializeApproach();
}

LaneAction::~LaneAction()
{
    // FUN_062c1420: member and base destructors only.
}

bool LaneAction::update(float Dt)
{
    // FUN_062c13f0. LaneState is +0x90 (+0x80 from the IAction base).
    (void)Dt;
    return LaneState != LANE_STOPPED;
}

void LaneAction::request_exit()
{
    // FUN_062c2300
    bExitFlag = 1;
    LaneState = LANE_LEAVING;
}

void LaneAction::stop(char bSkipStopTradelane)
{
    // FUN_062c2350
    if (LaneState != LANE_STOPPED)
    {
        LaneState = LANE_STOPPED;
        PhySys::Controller::UnRegister(this);

        if (TradelaneObjWatcher.target() != nullptr)
        {
            IObjRW* const Rw = TradelaneObj();
            if (Rw != nullptr)
            {
                // The CEqObj narrowing is used without a further test.
                CObject* const Obj = (CObject*)Rw->cobject();
                CEqObj* const Eq = (Obj != nullptr && (Obj->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
                    ? (CEqObj*)Obj : nullptr;
                Eq->set_control_exclusion(0);
                Eq->enable_collisions_r(true);

                if (bSkipStopTradelane == 0)
                    Rw->stop_tradelane();          // IObjAffect slot 26

                (void)Rw->cobject();               // result dropped
                Rw->set_throttle(1.0f);            // IObjAffect slot 0
            }
        }
        LaneState = LANE_STOPPED;
    }
}

void LaneAction::BuildRingTransform(IObjInspect const* Ring, LaneRingTransform& OutXform)
{
    // FUN_062c1880. Retail calls the traveller's get_orientation and drops the result.
    (void)TradelaneObj()->get_orientation();

    OutXform.Ring = Ring;

    // get_hardpoint is slot 48.
    Vector HpPos;
    Matrix HpRightOrient;
    Ring->get_hardpoint("HpRightLane", &HpPos, &HpRightOrient);
    OutXform.Pos = HpPos;

    if (bHpLeftLane != 0)   // +0x96
    {
        OutXform.Orient = HpRightOrient;
        return;
    }

    // HpLeftLane supplies only the position; the right-lane orientation is turned by pi.
    Matrix HpLeftOrient;
    Ring->get_hardpoint("HpLeftLane", &HpPos, &HpLeftOrient);
    OutXform.Pos = HpPos;

    // DAT_0639e2d0: float pi widened, not M_PI; cos and sin each rounded to float.
    double const Angle = 3.1415927410125732;
    float const CosA = X87CosF(Angle);
    float const SinA = X87SinF(Angle);
    Matrix const RotY(CosA, 0.0f, SinA,
                      0.0f, 1.0f, 0.0f,
                      -SinA, 0.0f, CosA);

    // MulMatrix (slot 0x20), no null test.
    Matrix Turned;
    Acquire3DMathEngine()->MulMatrix((FMatrix&)Turned, (FMatrix const&)RotY, (FMatrix const&)HpRightOrient);
    OutXform.Orient = Turned;
}

void LaneAction::InitializeApproach()
{
    // FUN_062c1490
    if (bRingApproachValid != 0)
    {
        // LANE_CRUISE, or LANE_LAST_LEG when the exit ring has no ring after it.
        LaneState = LANE_CRUISE;
        unsigned NextRingId = 0;
        LegEndXform.Ring->traverse_rings(NextRingId, bHpLeftLane != 0);
        if (NextRingId == 0)
        {
            LaneState = LANE_LAST_LEG;
        }

        // DAT_0639f410 (80.0f) / DAT_0639f3cc (2500.0f).
        TransitRamp = 1.0f;
        TransitElapsed = 3.0f;
        TransitRampTarget = 80.0f / 2500.0f;
    }
    else
    {
        // LANE_SPIN_UP: the traveller's speed / 2500 through the inlined 0x5f3759df fast length.
        LaneState = LANE_SPIN_UP;

        Vector const Vel = TradelaneObj()->get_velocity();   // no null test

        // S stays unrounded; only its float image seeds the guess.
        double const SpeedSq = ((double)Vel.x * Vel.x + (double)Vel.y * Vel.y) + (double)Vel.z * Vel.z;
        float const SpeedSqImage = (float)SpeedSq;
        int Bits;
        memcpy(&Bits, &SpeedSqImage, sizeof(Bits));
        Bits = 0x5f3759df - (Bits >> 1);
        float Guess;
        memcpy(&Guess, &Bits, sizeof(Guess));
        // 1.5f DAT_0639e6d0, 0.5f DAT_0639c13c, 2500.0f DAT_0639f3cc; one rounding into each of
        // +0xf0 and +0xec.
        double const Refine = 1.5 - (((double)Guess * Guess) * SpeedSq) * 0.5;
        double const Ramp = ((SpeedSq / 2500.0) * Refine) * Guess;
        TransitRampTarget = (float)Ramp;
        TransitRamp = (float)Ramp;
    }

    IObjRW* const Traveller = TradelaneObj();

    // No null tests on the cobject() result or the CEqObj narrowing.
    CObject* const Dock = (CObject*)Traveller->cobject();                 // slot 84 (+0x150)
    Dock->enable_collisions_r(false);
    CEqObj* const DockEq = (Dock != nullptr && (Dock->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
        ? (CEqObj*)Dock : nullptr;
    DockEq->set_control_exclusion(3);
    PhySys::Controller::Register(this, Dock);

    Quaternion QuatOut{};
    MatrixToQuaternionViaMathEngine(QuatOut, Traveller->get_transform().Rotation);
    CapturedShipOrient = QuatOut;   // +0xd8

    CapturedShipPos = Traveller->get_position();
}

void LaneAction::ProjectOntoSegment(LaneRingTransform const& A, LaneRingTransform const& B, float* OutProgress, Transform* OutResult)
{
    // FUN_062c1660. No null test on the traveller. Delta is rounded per component; its length
    // (sum z, y, x) is tested unrounded and divided by as a float.
    Vector const ObjPos = TradelaneObj()->get_position();

    Vector const Delta{ B.Pos.x - A.Pos.x, B.Pos.y - A.Pos.y, B.Pos.z - A.Pos.z };
    double const SegLenUnrounded =
        sqrt(((double)Delta.z * Delta.z + (double)Delta.y * Delta.y) + (double)Delta.x * Delta.x);
    float const SegLen = (float)SegLenUnrounded;

    // DAT_0639f418; unordered takes the degenerate branch.
    if (!(SegLenUnrounded > 1e-05))
    {
        *OutProgress = 0.0f;
        OutResult->Translation = A.Pos;
        OutResult->Rotation = A.Orient;
        return;
    }

    // Retail recomputes the length; the reciprocal stays unrounded.
    double const InvLen =
        1.0 / sqrt(((double)Delta.z * Delta.z + (double)Delta.y * Delta.y) + (double)Delta.x * Delta.x);
    Vector const Dir{ (float)(Delta.x * InvLen), (float)(Delta.y * InvLen), (float)(Delta.z * InvLen) };

    Vector const Rel{ ObjPos.x - A.Pos.x, ObjPos.y - A.Pos.y, ObjPos.z - A.Pos.z };

    // Both sums z, y, x, unrounded; progress rounded once.
    double const Dot = ((double)Dir.z * Rel.z + (double)Dir.y * Rel.y) + (double)Rel.x * Dir.x;
    double const DirLenSq = ((double)Dir.z * Dir.z + (double)Dir.y * Dir.y) + (double)Dir.x * Dir.x;
    double const Along = Dot / DirLenSq;
    *OutProgress = (float)(Along / SegLen);

    Vector const Scaled{ (float)(Dir.x * Along), (float)(Dir.y * Along), (float)(Dir.z * Along) };
    OutResult->Translation = Vector{ Scaled.x + A.Pos.x, Scaled.y + A.Pos.y, Scaled.z + A.Pos.z };
    OutResult->Rotation = A.Orient;
}

void LaneAction::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    (void)CoreList;

    // FUN_062c19d0
    if (TradelaneObjWatcher.target() == nullptr || TradelaneObj() == nullptr || LaneState == LANE_DONE)
    {
        stop(0);
        return;
    }

    // Slot 4 shares slot 3's body (common_iface.h), so this is the full transform; read by
    // the formation-leader check below.
    Transform const Snapshot = TradelaneObj()->get_transform();

    float const Dt = EventSim->delta_time;
    TransitElapsed = TransitElapsed + Dt;

    // Retail also builds an identity transform on the stack (FUN_062748a0) that nothing reads.

    float Progress = 0.0f;

    if (LaneState == LANE_LEAVING)
    {
        // Exiting: the lane pose follows the traveller's own transform.
        LanePose = TradelaneObj()->get_transform();

        if (ExitRampStart == 0.0f)
        {
            ExitRampStart = TransitRamp;
        }

        if (bExitFlag != 0)
        {
            // A yaw of Dt * -0.56 (double DAT_0639f460); cos rounded to float, sin not.
            double const Angle = (double)Dt * -0.56;
            float const CosA = X87CosF(Angle);
            double const SinA = X87Sin(Angle);
            Matrix Turn(1.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f,
                        0.0f, 0.0f, 1.0f);
            for (int Row = 0; Row < 3; ++Row)
            {
                float const C0 = Turn.E[Row][0];
                float const C2 = Turn.E[Row][2];
                Turn.E[Row][0] = (float)((double)CosA * C0 - SinA * C2);
                Turn.E[Row][2] = (float)((double)C0 * SinA + (double)CosA * C2);
            }

            // MulMatrix (slot 0x20), no null test.
            Matrix Turned;
            Acquire3DMathEngine()->MulMatrix((FMatrix&)Turned, (FMatrix const&)Turn,
                                             (FMatrix const&)LanePose.Rotation);
            LanePose.Rotation = Turned;
        }

        // 0.4 is DAT_0639f458. The unrounded value is compared; greater or unordered goes on.
        double const NewRamp =
            (double)TransitRamp - ((double)ExitRampStart - (double)TransitRampTarget * 0.4) * Dt;
        TransitRamp = (float)NewRamp;

        if (NewRamp <= (double)TransitRampTarget)
        {
            TransitRamp = TransitRampTarget;
            LaneState = LANE_DONE;
            stop(0);
            return;
        }
    }
    else
    {
        ProjectOntoSegment(LegStartXform, LegEndXform, &Progress, &LanePose);
    }

    // A damaged leg-end ring (health != 1.0f, NaN included) forces an exit once the traveller is
    // half way.
    if (LegEndXform.Ring != nullptr)
    {
        float Health = 1.0f;
        LegEndXform.Ring->get_status(Health);

        if (Health != 1.0f && (double)Progress >= 0.5)
        {
            if (TradelaneObj()->is_player())
            {
                // IObjAffect slot 27, unnamed (see common_iface.h).
                TradelaneObj()->slot_6c(1, 0);
            }

            bExitFlag = 1;
            LaneState = LANE_LEAVING;
            return;
        }
    }

    if (LaneState == LANE_SPIN_UP || LaneState == LANE_CRUISE || LaneState == LANE_LAST_LEG)
    {
        // Ring hop; unordered skips.
        if (Progress >= 1.0f)
        {
            // No null test on the CSimple narrowing (ObjectDb +0xac).
            CObject const* const Resolved = TradelaneObj()->cobject();
            CSimple const* const Simple =
                (Resolved != nullptr && (Resolved->ObjectClass & CObject::ClassSimple) == CObject::ClassSimple)
                    ? (CSimple const*)Resolved
                    : nullptr;
            IObjDB* const ObjDb = Simple->ObjectDb;

            if (ObjDb != nullptr)
            {
                // The end frame becomes the start frame whatever the answer.
                unsigned NextRingId = 0;
                LegEndXform.Ring->traverse_rings(NextRingId, bHpLeftLane != 0);
                LegStartXform = LegEndXform;

                if (NextRingId != 0)
                {
                    BuildRingTransform(ObjDb->FindById(&NextRingId), LegEndXform);   // IObjDB slot 1

                    Progress = (float)fmod((double)Progress, 1.0);   // _CIfmod

                    if (LaneState != LANE_LAST_LEG)
                    {
                        unsigned AfterNextId = NextRingId;
                        LegEndXform.Ring->traverse_rings(AfterNextId, bHpLeftLane != 0);
                        if (AfterNextId == 0)
                            LaneState = LANE_LAST_LEG;
                    }
                }
                else
                {
                    LegEndXform.Ring = nullptr;
                    LaneState = LANE_LEAVING;
                }
            }
        }

        // A formation leader other than the traveller gives the formation offset; otherwise a
        // non-player with a follow leader gives the follow offset.
        IObjRW* FormationLeader = nullptr;
        TradelaneObj()->get_formation_leader(FormationLeader);

        bool bHaveOffset = false;
        Vector Offset{ 0.0f, 0.0f, 0.0f };
        if (FormationLeader != nullptr && FormationLeader != TradelaneObj())
        {
            TradelaneObj()->get_formation_offset(Offset);
            bHaveOffset = true;
        }
        else if (!TradelaneObj()->is_player())
        {
            IObjRW* FollowLeader = nullptr;
            TradelaneObj()->get_follow_leader(FollowLeader);
            if (FollowLeader != nullptr)
            {
                TradelaneObj()->get_follow_offset(Offset);
                bHaveOffset = true;
            }
        }

        if (bHaveOffset)
        {
            // Math engine slot 0x30, no null test.
            Offset.z = 0.0f;
            Vector Rotated{ 0.0f, 0.0f, 0.0f };
            Acquire3DMathEngine()->Rotate((FVector&)Rotated, (FTransform const&)LanePose, (FVector const&)Offset);
            LanePose.Translation.x = Rotated.x + LanePose.Translation.x;
            LanePose.Translation.y = Rotated.y + LanePose.Translation.y;
            LanePose.Translation.z = Rotated.z + LanePose.Translation.z;
        }

        // Multiplayer only: a formation leader (not the traveller) that has left the lane and
        // is no more than 500 behind along the traveller's backward axis starts the exit.
        if (!SinglePlayer() && FormationLeader != nullptr && FormationLeader != TradelaneObj())
        {
            bool bLeaderInLane = true;
            FormationLeader->is_using_tradelane(&bLeaderInLane);

            if (!bLeaderInLane)
            {
                Vector Backward{ -Snapshot.Rotation.E[0][2], -Snapshot.Rotation.E[1][2], -Snapshot.Rotation.E[2][2] };
                Vector const& LeaderPos = FormationLeader->get_position();

                // FUN_06347e60 length, unrounded; each component rounded.
                double const InvLen = 1.0 / sqrt(((double)Backward.x * Backward.x + (double)Backward.y * Backward.y)
                                                 + (double)Backward.z * Backward.z);
                Backward.x = (float)(Backward.x * InvLen);
                Backward.y = (float)(Backward.y * InvLen);
                Backward.z = (float)(Backward.z * InvLen);

                // Retail's sum orders, unrounded.
                double const LeaderAlong = ((double)Backward.y * LeaderPos.y + (double)Backward.z * LeaderPos.z)
                                           + (double)Backward.x * LeaderPos.x;
                double const SelfAlong = ((double)Snapshot.Translation.x * Backward.x
                                          + (double)Backward.z * Snapshot.Translation.z)
                                         + (double)Backward.y * Snapshot.Translation.y;

                // DAT_0639f450; unordered does not exit.
                if (SelfAlong + -LeaderAlong > -500.0)
                {
                    LaneState = LANE_LEAVING;
                    return;
                }
            }
        }
    }

    // Speed schedule.
    if (LaneState == LANE_SPIN_UP)
    {
        // 0.125 is DAT_0639f440; the unrounded sum is compared (unordered stays in LANE_SPIN_UP).
        double const Ramped = (double)Dt * 0.125 + TransitRamp;
        TransitRamp = (float)Ramped;
        if (Ramped >= 1.0)
        {
            LaneState = LANE_CRUISE;
            TransitRamp = 1.0f;
        }
    }
    else if (LaneState == LANE_CRUISE)
    {
        TransitRamp = 1.0f;
    }
    else if (LaneState == LANE_LAST_LEG)
    {
        // 400 is the double DAT_0639f448, 2500.0f DAT_0639f3cc.
        TransitRamp = (float)(1.0 - (1.0 - 400.0 / 2500.0) * Progress);
    }

    // Advance along the lane pose's backward axis. x87: x and y are rounded before they are
    // added, z is added unrounded.
    float const LaneSpeed = 2500.0f * TransitRamp;
    double const Advance = (double)LaneSpeed * Dt;
    float const Dx = (float)(-LanePose.Rotation.E[0][2] * Advance);
    float const Dy = (float)(-LanePose.Rotation.E[1][2] * Advance);
    double const Dz = -LanePose.Rotation.E[2][2] * Advance;

    LanePose.Translation.x = Dx + LanePose.Translation.x;
    LanePose.Translation.y = Dy + LanePose.Translation.y;
    LanePose.Translation.z = (float)(Dz + LanePose.Translation.z);
    CapturedShipPos.x = Dx + CapturedShipPos.x;
    CapturedShipPos.y = Dy + CapturedShipPos.y;
    CapturedShipPos.z = (float)(Dz + CapturedShipPos.z);

    // For the first 3 s (double DAT_0639f438) of LANE_SPIN_UP, blend from the captured pose.
    if (LaneState == LANE_SPIN_UP && TransitElapsed < 3.0)
    {
        I3DMathEngine* const Math = Acquire3DMathEngine();

        Quaternion LaneQuat;
        Math->MatrixToQuaternion((FQuaternion&)LaneQuat, (FMatrix const&)LanePose.Rotation);   // slot 0x50

        // 1/3 is the double DAT_0639e0e8.
        float const Blend = (float)((double)TransitElapsed * (1.0 / 3.0));

        Quaternion Blended;
        Math->QuatSlerp((FQuaternion&)Blended, (FQuaternion const&)CapturedShipOrient,
                        (FQuaternion const&)LaneQuat, Blend);   // slot 0x58

        Matrix BlendedRotation;
        Math->QuaternionToMatrix((FMatrix&)BlendedRotation, (FQuaternion const&)Blended);   // slot 0x54
        LanePose.Rotation = BlendedRotation;

        // x87: x keeps both products unrounded, y rounds the captured product, z rounds both.
        double const Keep = 1.0 - Blend;
        double const LaneX = (double)Blend * LanePose.Translation.x;
        double const LaneY = (double)Blend * LanePose.Translation.y;
        float const LaneZ = (float)((double)Blend * LanePose.Translation.z);
        float const KeepY = (float)(Keep * CapturedShipPos.y);
        float const KeepZ = (float)(Keep * CapturedShipPos.z);
        LanePose.Translation.x = (float)(Keep * CapturedShipPos.x + LaneX);
        LanePose.Translation.y = (float)(KeepY + LaneY);
        LanePose.Translation.z = KeepZ + LaneZ;
    }

    CObject* const Committed = (CObject*)TradelaneObj()->cobject();
    if (Committed != nullptr && (Committed->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
    {
        ((CShip*)Committed)->set_tradelane_speed(LaneSpeed);
    }

    // cobject() again, no null test; beam_object is CObject slot 0x7c.
    ((CObject*)TradelaneObj()->cobject())->beam_object(LanePose.Translation, LanePose.Rotation, true);
}
