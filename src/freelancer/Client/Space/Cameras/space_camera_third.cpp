// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail third-person camera (in-flight controller + 0x1b0, vtable .rdata 0x005dbb14): ctor
// 0x00518600, name (slot 0x54), cockpit-ini loader FUN_005187d0 (almost entirely validation --
// [ThirdPerson] is OBSOLETE and no key is read from it), Event::Handler face FUN_0051a190,
// activate 0x64 (FUN_00518ad0), attach 0x58 (0x00518af0), update 0x6c (0x00518e40), and the
// chase solver FUN_00518f20.

#include "Space/Cameras/space_camera_third.h"
#include "Space/space_scene_obj_draw.h"

#include "Space/client_gameobj.h"          // FClientShipGameobj::JumpState
#include "Space/HUD/hud_command_dispatch.h"    // HudRegisterCommandHandler (FUN_005760d0)
#include "Space/HUD/hud_target.h"              // HudIsMousePickBlocked (FUN_0045a740)
#include "Space/Cameras/ship_camera_params.h"      // FShipCameraParams (ship companion +0x68)
#include "Space/Cameras/space_camera_first.h"      // G_FirstPersonMathEngine (DAT_00675220)
#include "Interface/win3d.h"                   // Win3DTransformByMatrix (FUN_00423b90)

#include "common_archetype.h"        // Archetype::Root::Companion (+0x04)
#include "common_cobject.h"          // CObject::get_archetype
#include "common_csys.h"             // Acquire3DMathEngine (FUN_005b73e0)
#include "common_goods.h"            // FindHardpoint_PS, HardpointInfo
#include "common_iface.h"            // GetBehaviorManager, IBehaviorManager
#include "common_ini.h"
#include "dacom.h"
#include "x86math.h"

#include <math.h>
#include <string.h>

namespace
{
    // .rdata 0x005c77f4, the float the loader multiplies both angles by.
    const float kDegreesToRadians = 0.01745329238474369f;
    // .rdata 0x005dba18 and 0x005d5b58, both doubles.
    const double kExpectedAspect = 1.333;
    const double kAspectTolerance = 0.05;

    const char kCockpitCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Cockpit.cpp";

    // A BaseWatcher holds the Watchable eight bytes into the client object.
    const int kWatchableOffset = 8;

    // FUN_00518f20's constants.
    const float  kMinDeltaTime = 9.999999747378752e-06f;   // .rdata 0x005d7e84
    const float  kSlackAccelScale = 0.019999999552965164f; // .rdata 0x005d1fe4
    const double kSlackThreshold = 0.01;                   // .rdata 0x005d5138 (double)
    const float  kSlackCapScale = 0.03999999910593033f;    // .rdata 0x005d845c
    const float  kLevelTurnMinDot = -0.9900000095367432f;  // .rdata 0x005dbcb0 (float)
    const double kLevelTurnMaxDot = 0.9998999834060669;    // .rdata 0x005dbca8 (double)
    const double kLevelUpAgreeDot = 0.995;                 // .rdata 0x005dbca0 (double)
    const double kLevelAxisMaxDot = 0.9999;                // .rdata 0x005dafa8 (double)
    const double kMinTurnAngle = 0.0001;                   // .rdata 0x005dbc98 (double)
    const float  kDownTurnYawScale = 0.800000011920929f;   // .data  0x006135c4 (read only)

    // Event::Handler face table .rdata 0x005dbb0c: one slot, FUN_0051a190.

    // DAT_00675220 (shared with Cockpit.cpp), acquired on first use -- retail repeats the test
    // before every engine call in FUN_00518f20.
    I3DMathEngine* CockpitMathEngine()
    {
        if (G_FirstPersonMathEngine == nullptr)
            G_FirstPersonMathEngine = Acquire3DMathEngine();
        return G_FirstPersonMathEngine;
    }

    Matrix const& AsMatrix(const float* M9)
    {
        return *reinterpret_cast<Matrix const*>(M9);
    }

    Matrix& AsMatrix(float* M9)
    {
        return *reinterpret_cast<Matrix*>(M9);
    }

    Vector const& AsVector(const Vector* V3)
    {
        return *reinterpret_cast<Vector const*>(V3);
    }

    // FUN_00423b30: Out = A + B, z summed first, each sum rounded to float.
    void Vec3AddStored(float* Out, const float* A, const float* B)
    {
        const float Z = static_cast<float>(static_cast<double>(A[2]) + B[2]);
        const float Y = static_cast<float>(static_cast<double>(A[1]) + B[1]);
        const float X = static_cast<float>(static_cast<double>(A[0]) + B[0]);
        Out[0] = X;
        Out[1] = Y;
        Out[2] = Z;
    }

    // FUN_0051a0c0: Out = A x B, terms in double, rounded to float.
    void Vec3CrossStored(Vector* Out, const Vector* A, const Vector* B)
    {
        const double Z = static_cast<double>(A->x) * B->y - static_cast<double>(A->y) * B->x;
        const double Y = static_cast<double>(A->z) * B->x - static_cast<double>(B->z) * A->x;
        const double X = static_cast<double>(A->y) * B->z - static_cast<double>(A->z) * B->y;
        Out->x = static_cast<float>(X);
        Out->y = static_cast<float>(Y);
        Out->z = static_cast<float>(Z);
    }

    // FUN_00410190: sqrt((x*x + y*y) + z*z), unrounded.
    double Vec3Length(const Vector* V)
    {
        double SumSq = static_cast<double>(V->x) * V->x + static_cast<double>(V->y) * V->y;
        SumSq = SumSq + static_cast<double>(V->z) * V->z;
        return sqrt(SumSq);
    }

    // FUN_0041a390: scale by 1.0f / length (unrounded), each product rounded.
    void Vec3NormaliseInPlace(Vector* V)
    {
        const double Inv = 1.0f / Vec3Length(V);
        V->x = static_cast<float>(Inv * V->x);
        V->y = static_cast<float>(Inv * V->y);
        V->z = static_cast<float>(Inv * V->z);
    }

    // FUN_004cbf40: (a.z*b.z + a.y*b.y) + a.x*b.x, unrounded.
    double Vec3DotZyx(const Vector* A, const Vector* B)
    {
        double Sum = static_cast<double>(A->z) * B->z + static_cast<double>(A->y) * B->y;
        return Sum + static_cast<double>(A->x) * B->x;
    }

    // Column C of a row-major 3x3 (elements C, C + 3, C + 6).
    void GetColumn(Vector* Out, const float* M9, int Column)
    {
        Out->x = M9[Column];
        Out->y = M9[Column + 3];
        Out->z = M9[Column + 6];
    }

    // FUN_0051a100 / FUN_0051a120 / FUN_00411f70: write column 0 / 1 / 2.
    void SetColumn(float* M9, int Column, const Vector* V)
    {
        M9[Column] = V->x;
        M9[Column + 3] = V->y;
        M9[Column + 6] = V->z;
    }

    // The two rotation loops FUN_00518f20 runs over a 3x3 in place: cos rounded to float, sin
    // kept double. Pitch works columns 1 and 2, yaw columns 0 and 2.
    void RotatePitchRows(float* M9, double Angle)
    {
        const float CosA = static_cast<float>(cos(Angle));
        const double SinA = sin(Angle);
        for (int Row = 0; Row < 3; ++Row)
        {
            const double B = M9[Row * 3 + 1];
            const double C = M9[Row * 3 + 2];
            M9[Row * 3 + 1] = static_cast<float>(SinA * C + B * CosA);
            M9[Row * 3 + 2] = static_cast<float>(static_cast<double>(CosA) * C - B * SinA);
        }
    }

    void RotateYawRows(float* M9, double Angle)
    {
        const float CosA = static_cast<float>(cos(Angle));
        const double SinA = sin(Angle);
        for (int Row = 0; Row < 3; ++Row)
        {
            const double A = M9[Row * 3 + 0];
            const double C = M9[Row * 3 + 2];
            M9[Row * 3 + 0] = static_cast<float>(static_cast<double>(CosA) * A - SinA * C);
            M9[Row * 3 + 2] = static_cast<float>(static_cast<double>(CosA) * C + A * SinA);
        }
    }

    void MakeIdentityView(float* View12)
    {
        for (int Index = 0; Index < 12; ++Index)
            View12[Index] = 0.0f;
        View12[0] = 1.0f;
        View12[4] = 1.0f;
        View12[8] = 1.0f;
    }

    // Retail re-reads +0xc0 each time it needs the object.
    IObjRW* WatchedObject(FThirdPersonCamera* Cam)
    {
        return static_cast<IObjRW*>(GameCameraGetTarget((FGameCamera*)Cam));
    }

    CEqObj* WatchedShip(FThirdPersonCamera* Cam)
    {
        return GameCameraWatchedShip(Cam);
    }
}

float* CockpitMulMatrix(float* Out9, const float* A9, const float* B9)
{
    // FUN_0051a140.
    float Product[9];
    CockpitMathEngine()->MulMatrix(AsMatrix(Product), AsMatrix(A9), AsMatrix(B9));
    memcpy(Out9, Product, sizeof(Product));
    return Out9;
}

bool ThirdPersonCameraOnEvent(FThirdPersonCamera* Cam, unsigned MsgId)
{
    // FUN_0051a190 (slot 0 of the Event::Handler face).
    if (HudIsMousePickBlocked())
        return false;
    if (HudCameraOnEvent((FHudCamera*)Cam, MsgId))
        return true;

    // Only 0x3f is this class's own: toggles auto-level.
    if (MsgId != USER_TOGGLE_LEVEL_CAMERA)
        return false;

    Cam->bCameraLevelEnabled = (Cam->bCameraLevelEnabled == 0) ? 1 : 0;

    const unsigned char* const Held =
        reinterpret_cast<const unsigned char*>(Cam->Watched.target());
    IObjRW* const Target =
        (Held != nullptr)
            ? reinterpret_cast<IObjRW*>(const_cast<unsigned char*>(Held - kWatchableOffset))
            : nullptr;
    IBehaviorManager* const Manager = GetBehaviorManager(Target);
    if (Manager != nullptr)
        Manager->update_level_camera(Cam->bCameraLevelEnabled != 0);
    return true;
}

const char* ThirdPersonCameraGetName()
{
    // 0x005187c0.
    return "ThirdPerson";
}

void ThirdPersonCameraConstruct(FThirdPersonCamera* Cam)
{
    // FUN_00518600: HUDCamera's ctor, then the two vptrs, then the fields in retail's order.
    HudCameraConstruct((FHudCamera*)Cam);

    Cam->HardpointPos[0] = 0.0f;
    Cam->HardpointPos[1] = 0.0f;
    Cam->HardpointPos[2] = 0.0f;

    Cam->HardpointOrient[7] = 0.0f;   // +0x128
    Cam->HardpointOrient[6] = 0.0f;   // +0x124
    Cam->HardpointOrient[5] = 0.0f;   // +0x120
    Cam->HardpointOrient[3] = 0.0f;   // +0x118
    Cam->HardpointOrient[2] = 0.0f;   // +0x114
    Cam->HardpointOrient[1] = 0.0f;   // +0x110
    Cam->HardpointOrient[8] = 1.0f;   // +0x12c
    Cam->HardpointOrient[4] = 1.0f;   // +0x11c
    Cam->HardpointOrient[0] = 1.0f;   // +0x10c

    // The look offset {0, 7, 60}.
    Cam->LookOffsetX = 0.0f;
    Cam->LookOffsetY = 7.0f;
    Cam->LookOffsetZ = 60.0f;

    // The second offset vector.
    Cam->ExtraOffsetX = 0.0f;
    Cam->ExtraOffsetY = 0.0f;
    Cam->ExtraOffsetZ = 0.0f;

    // HUDCamera's bytes +0xfc and +0xfd.
    Cam->bFlagFC = 0;
    Cam->bFlagFD = 0;

    // The speed tracker, auto-level on.
    Cam->SpeedSlack = 0.0f;
    Cam->SmoothedSpeed = 0.0f;
    Cam->PrevSpeed = 0.0f;
    Cam->bCameraLevelEnabled = 1;
    Cam->LookAheadMultiplier = 0.05f;      // 0x3d4ccccd
    Cam->AccelerationLimit = 40.0f;        // 0x42200000

    // The orientation history, identity.
    Cam->ResetOrient[8] = 1.0f;   // +0x178
    Cam->ResetOrient[4] = 1.0f;   // +0x168
    Cam->ResetOrient[0] = 1.0f;   // +0x158
    Cam->ResetOrient[7] = 0.0f;   // +0x174
    Cam->ResetOrient[6] = 0.0f;   // +0x170
    Cam->ResetOrient[5] = 0.0f;   // +0x16c
    Cam->ResetOrient[3] = 0.0f;   // +0x164
    Cam->ResetOrient[2] = 0.0f;   // +0x160
    Cam->ResetOrient[1] = 0.0f;   // +0x15c

    // The turn angles (radians) and the look-ahead slerp.
    Cam->VerticalTurnUpAngle = 0.1f;          // 0x3dcccccd
    Cam->TurnLookAheadSlerpAmount = 0.5f;     // 0x3f000000
    Cam->HorizontalTurnAngle = 0.2f;          // 0x3e4ccccd
    Cam->VerticalTurnDownAngle = 0.2f;        // 0x3e4ccccd

    Cam->UpAxis.y = 1.0f;
    Cam->UpAxis.x = 0.0f;
    Cam->UpAxis.z = 0.0f;
    Cam->Unused210 = 0.0f;

    Cam->TurnRateScale = 2.0f;                // 0x40000000
    Cam->LevelAxis.x = 0.0f;
    Cam->LevelAxis.y = 0.0f;
    Cam->LevelAxis.z = -1.0f;                // 0xbf800000
    Cam->PrevAngularRate = 0.0f;
    Cam->bInitialised = 0;

    Cam->AngularSlerpMultiplier = 2.0f;       // 0x40000000
    Cam->AngularAcceleration = 0.049087385f;  // 0x3d490fdb (pi / 64)

    // Not written by the retail ctor (the controller is zeroed static storage):
    // TranslationCache (+0x130), TargetOrientB (+0x17c), TargetOrientA (+0x1b0) and the pad bytes.
}

bool ThirdPersonCameraActivate(FThirdPersonCamera* Cam, const int* ViewRect4)
{
    // FUN_00518ad0: event 0x3f registered on the face at +0xf8, then HUDCamera's activate.
    FHudCmdHandler* const Face = Cam->AsHudCmdHandler();
    HudRegisterCommandHandler(Face, 0x3f);
    return HudCameraActivate((FHudCamera*)Cam, ViewRect4);
}

void ThirdPersonCameraAttach(FThirdPersonCamera* Cam, void* Object)
{
    // LAB_00518af0 (slot 0x58).
    HudCameraAttach((FHudCamera*)Cam, Object);

    // The second offset vector, on every attach.
    Cam->ExtraOffsetX = 0.0f;
    Cam->ExtraOffsetY = 0.0f;
    Cam->ExtraOffsetZ = 0.0f;

    const unsigned char* const Held =
        reinterpret_cast<const unsigned char*>(Cam->Watched.target());
    if (Held != nullptr && Held - kWatchableOffset != nullptr)
    {
        // With a client ship, the camera block (+0x68) of its archetype's companion (Root +
        // 0x04), read at fixed offsets (FShipCameraParams).
        CEqObj* const Ship = WatchedShip(Cam);
        if (Ship != nullptr)
        {
            const unsigned char* const Companion = reinterpret_cast<const unsigned char*>(
                Ship->get_archetype()->Companion);
            if (Companion != nullptr)
            {
                const FShipCameraParams* const Params =
                    reinterpret_cast<const FShipCameraParams*>(Companion + 0x68);
                Cam->AngularSlerpMultiplier = Params->AngularSlerpMultiplier;      // +0x74
                Cam->AngularAcceleration = Params->AngularAcceleration;            // +0x70
                Cam->HorizontalTurnAngle = Params->HorizontalTurnAngle;            // +0x78
                Cam->VerticalTurnUpAngle = Params->VerticalTurnUpAngle;            // +0x7c
                Cam->VerticalTurnDownAngle = Params->VerticalTurnDownAngle;        // +0x80
                Cam->TurnLookAheadSlerpAmount = Params->TurnLookAheadSlerpAmount;  // +0x84
                Cam->LookOffsetX = 0.0f;
                Cam->LookOffsetY = Params->Offset[0];                                  // +0x68
                Cam->LookOffsetZ = Params->Offset[1];                                  // +0x6c
            }
        }

        // Two copies of the object's orientation; the up axis is the first one's column 1.
        IObjRW* const Target = WatchedObject(Cam);
        memcpy(Cam->TargetOrientA, &Target->get_transform().Rotation.E[0][0], 9 * sizeof(float));
        memcpy(Cam->TargetOrientB, &WatchedObject(Cam)->get_transform().Rotation.E[0][0],
               9 * sizeof(float));
        Cam->UpAxis.x = Cam->TargetOrientA[1];
        Cam->UpAxis.y = Cam->TargetOrientA[4];
        Cam->UpAxis.z = Cam->TargetOrientA[7];

        // Slot 0x10's transform (twelve dwords) to the world transform, view dirty first.
        FSpaceCamera* const Space = SpaceCameraOf((FHudCamera*)Cam);
        const float* const Xform =
            reinterpret_cast<const float*>(&WatchedObject(Cam)->get_orientation());
        Space->bViewDirty = 1;
        memcpy(&Space->World, Xform, 9 * sizeof(float));
        Space->WorldPos.x = Xform[9];
        Space->WorldPos.y = Xform[10];
        Space->WorldPos.z = Xform[11];

        // The "HpCockpit" hardpoint in parent space; the fallback is (0, 0, -4) and identity.
        long HardpointHandle = 0;
        HardpointInfo Info;
        if (FindHardpoint_PS(WatchedObject(Cam)->get_index(), "HpCockpit", HardpointHandle, Info))
        {
            Cam->HardpointPos[0] = Info.Position.x;
            Cam->HardpointPos[1] = Info.Position.y;
            Cam->HardpointPos[2] = Info.Position.z;
            memcpy(Cam->HardpointOrient, &Info.Orient, 9 * sizeof(float));
        }
        else
        {
            Cam->HardpointPos[0] = 0.0f;
            Cam->HardpointPos[1] = 0.0f;
            Cam->HardpointPos[2] = -4.0f;   // 0xc0800000
            Cam->HardpointOrient[8] = 1.0f;
            Cam->HardpointOrient[4] = 1.0f;
            Cam->HardpointOrient[0] = 1.0f;
            Cam->HardpointOrient[7] = 0.0f;
            Cam->HardpointOrient[6] = 0.0f;
            Cam->HardpointOrient[5] = 0.0f;
            Cam->HardpointOrient[3] = 0.0f;
            Cam->HardpointOrient[2] = 0.0f;
            Cam->HardpointOrient[1] = 0.0f;
        }
    }

    // Retail tests the watcher again.
    const unsigned char* const HeldAgain =
        reinterpret_cast<const unsigned char*>(Cam->Watched.target());
    if (HeldAgain == nullptr || HeldAgain - kWatchableOffset == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kCockpitCpp, 0x3c9,
              "no object linked to third person camera");
        Cam->bInitialised = 1;
        return;
    }

    // A reset solve (dt 0) from an identity view, whatever it answers; its translation is also
    // cached in +0x130.
    float View[12];
    MakeIdentityView(View);
    ThirdPersonCameraSolve(Cam, View, 0.0f, true);
    FSpaceCamera* const Space = SpaceCameraOf((FHudCamera*)Cam);
    memcpy(&Space->World, View, 9 * sizeof(float));
    Space->WorldPos.x = View[9];
    Space->WorldPos.y = View[10];
    Space->WorldPos.z = View[11];
    Cam->TranslationCache[0] = View[9];
    Cam->TranslationCache[1] = View[10];
    Cam->TranslationCache[2] = View[11];
    Space->bViewDirty = 1;
    Cam->bInitialised = 1;
}

void ThirdPersonCameraUpdate(FThirdPersonCamera* Cam, float DeltaSeconds)
{
    // LAB_00518e40 (slot 0x6c). The identity view is built on every call.
    float View[12];
    MakeIdentityView(View);

    // Skipped for the frame right after an attach.
    if (Cam->bInitialised == 0)
    {
        if (ThirdPersonCameraSolve(Cam, View, DeltaSeconds, false))
        {
            FSpaceCamera* const Space = SpaceCameraOf((FHudCamera*)Cam);
            memcpy(&Space->World, View, 9 * sizeof(float));
            Space->WorldPos.x = View[9];
            Space->WorldPos.y = View[10];
            Space->WorldPos.z = View[11];
            Space->bViewDirty = 1;
        }
        else
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kCockpitCpp, 0x3e9,
                  "third person camera unable to calculate viewer transform, missin linked Ship*");
        }
    }

    // HUDCamera's update, then the attach latch cleared.
    HudCameraUpdate((FHudCamera*)Cam, DeltaSeconds);
    Cam->bInitialised = 0;
}

bool ThirdPersonCameraSolve(FThirdPersonCamera* Cam, float* Out12, float DeltaSeconds, bool bReset)
{
    // FUN_00518f20.
    if (WatchedShip(Cam) == nullptr)
        return false;

    // The rotation pointer is kept and read again further down; M is a working copy.
    const float* const ShipRot = &WatchedObject(Cam)->get_transform().Rotation.E[0][0];
    float M[9];
    memcpy(M, ShipRot, sizeof(M));

    const Vector ShipPosRef = WatchedObject(Cam)->get_position();
    float Pos[3] = {ShipPosRef.x, ShipPosRef.y, ShipPosRef.z};

    // The speed, rounded to float.
    const Vector Velocity = WatchedObject(Cam)->get_velocity();
    Vector Heading = {Velocity.x, Velocity.y, Velocity.z};
    const double SpeedExact = Vec3Length(&Heading);
    const float Speed = static_cast<float>(SpeedExact);

    // Over 1.0f (not for a NaN): the velocity again, normalised, and the heading replaced by
    // minus the rotation's column 2. Neither the direction nor Heading is read afterwards (in
    // retail the direction's frame slot is overwritten on every later path before a read).
    if (SpeedExact > 1.0f)
    {
        const Vector Again = WatchedObject(Cam)->get_velocity();
        Vector VelocityDir;
        VelocityDir.x = Again.x;
        VelocityDir.y = Again.y;
        VelocityDir.z = Again.z;
        const double Inv = 1.0f / Vec3Length(&VelocityDir);
        VelocityDir.x = static_cast<float>(VelocityDir.x * Inv);
        VelocityDir.y = static_cast<float>(VelocityDir.y * Inv);
        VelocityDir.z = static_cast<float>(VelocityDir.z * Inv);
        const float* const Rot = &WatchedObject(Cam)->get_transform().Rotation.E[0][0];
        Heading.x = -Rot[2];
        Heading.y = -Rot[5];
        Heading.z = -Rot[8];
    }

    // Null allowed.
    IBehaviorManager* const Manager = GetBehaviorManager(WatchedObject(Cam));

    // The reset: tracker seeded with this speed, level axis (0,0,-1), the orientation history
    // from the object's rotation, up = its column 1, and the manager put in level mode.
    if (bReset)
    {
        Cam->SmoothedSpeed = Speed;
        Cam->PrevSpeed = Speed;
        Cam->SpeedSlack = 0.0f;
        Cam->LevelAxis.x = 0.0f;
        Cam->LevelAxis.y = 0.0f;
        Cam->LevelAxis.z = -1.0f;
        memcpy(Cam->ResetOrient, &WatchedObject(Cam)->get_transform().Rotation.E[0][0],
               9 * sizeof(float));
        Vector ResetUp;
        GetColumn(&ResetUp, Cam->ResetOrient, 1);
        Cam->UpAxis.x = ResetUp.x;
        Cam->UpAxis.y = ResetUp.y;
        Cam->UpAxis.z = ResetUp.z;
        if (Manager != nullptr)
        {
            Manager->set_camera_mode(IBehaviorManager::CameraModeLevel);
            Vector Up;
            Up.x = Cam->ResetOrient[1];
            Up.y = Cam->ResetOrient[4];
            Up.z = Cam->ResetOrient[7];
            Manager->submit_camera_up(Up);
        }
    }

    // The speed tracker. Speed * look-ahead stays unrounded until the (updated) slack is added
    // at the end.
    const double LookAheadBase = static_cast<double>(Speed) * Cam->LookAheadMultiplier;
    // dt not above 1e-5 (or NaN) gives 0.0.
    double Accel = 0.0;
    if (DeltaSeconds > kMinDeltaTime)
        Accel = (static_cast<double>(Speed) - Cam->PrevSpeed) / DeltaSeconds;
    // The slack, zeroed when the unrounded sum is below zero.
    const double SlackSum =
        (static_cast<double>(Speed) - Cam->SmoothedSpeed) * DeltaSeconds + Cam->SpeedSlack;
    Cam->SpeedSlack = static_cast<float>(SlackSum);
    if (SlackSum < 0.0f)
        Cam->SpeedSlack = 0.0f;
    // The catch-up limit, rounded to float.
    const double Slack = Cam->SpeedSlack;
    const float Limit = static_cast<float>(
        (Slack * Slack * kSlackAccelScale + 1.0f) * Cam->AccelerationLimit);
    // Ramp when the slack is over 0.01 or the acceleration strictly above the limit; otherwise
    // snap.
    bool bRamp = true;
    if (!(static_cast<double>(Cam->SpeedSlack) > kSlackThreshold))
        bRamp = Accel > Limit;
    if (bRamp)
    {
        // limit * dt onto the smoothed speed, capped by k*k*speed + speed with k = slack * 0.04f
        // -- the cap rounded, the ramp compared unrounded.
        const double Ramped = static_cast<double>(Limit) * DeltaSeconds + Cam->SmoothedSpeed;
        Cam->SmoothedSpeed = static_cast<float>(Ramped);
        const double K = static_cast<double>(Cam->SpeedSlack) * kSlackCapScale;
        const float Cap = static_cast<float>(K * K * Speed + Speed);
        if (Ramped > Cap)
            Cam->SmoothedSpeed = Cap;
    }
    else
    {
        Cam->SmoothedSpeed = Speed;
    }
    const float LookAhead = static_cast<float>(LookAheadBase + Cam->SpeedSlack);

    // The camera offset = hardpoint + ((look offset + extra offset) + (0, 0, look-ahead)), each
    // sum rounded to float.
    Vector CameraOffset = {Cam->HardpointPos[0], Cam->HardpointPos[1], Cam->HardpointPos[2]};
    float LookAheadOffset[3];
    LookAheadOffset[0] = 0.0f;
    LookAheadOffset[1] = 0.0f;
    LookAheadOffset[2] = LookAhead;
    float LookOffsetSum[3];
    Vec3AddStored(LookOffsetSum, &Cam->LookOffsetX, &Cam->ExtraOffsetX);
    float OffsetPlusLookAhead[3];
    Vec3AddStored(OffsetPlusLookAhead, LookOffsetSum, LookAheadOffset);
    CameraOffset.x = static_cast<float>(static_cast<double>(OffsetPlusLookAhead[0]) + CameraOffset.x);
    CameraOffset.y = static_cast<float>(static_cast<double>(CameraOffset.y) + OffsetPlusLookAhead[1]);
    CameraOffset.z = static_cast<float>(static_cast<double>(CameraOffset.z) + OffsetPlusLookAhead[2]);

    // Every slerp below writes QSlerp and converts back from the copy QSlerpCopy, as retail does.
    Quaternion QSlerp;
    Quaternion QSlerpCopy;

    // Auto-level only with +0x13c set, a manager, and the manager's camera-level status.
    if (Cam->bCameraLevelEnabled != 0 && Manager != nullptr && Manager->get_camera_level_status())
    {
        if (Manager->get_user_turning_input_state())
        {
            // The player is steering: tell the manager the reset orientation's up (column 1).
            Vector Up;
            Up.x = Cam->ResetOrient[1];
            Up.y = Cam->ResetOrient[4];
            Up.z = Cam->ResetOrient[7];
            Manager->submit_camera_up(Up);

            // q(M levelled to +0x204).
            float Levelled[9];
            CameraOrientFromForwardAndUp(reinterpret_cast<Matrix*>(Levelled), reinterpret_cast<Matrix const*>(M), &Cam->UpAxis);
            Quaternion QFromUpAxis;
            CockpitMathEngine()->MatrixToQuaternion(QFromUpAxis, AsMatrix(Levelled));

            // q(M levelled to the reset up).
            Vector HistoryUp;
            GetColumn(&HistoryUp, Cam->ResetOrient, 1);
            CameraOrientFromForwardAndUp(reinterpret_cast<Matrix*>(Levelled), reinterpret_cast<Matrix const*>(M), &HistoryUp);
            Quaternion QFromHistory;
            CockpitMathEngine()->MatrixToQuaternion(QFromHistory, AsMatrix(Levelled));

            const float T = static_cast<float>(static_cast<double>(DeltaSeconds) * Cam->TurnRateScale);
            CockpitMathEngine()->QuatSlerp(QSlerp, QFromUpAxis, QFromHistory, T);

            // Back to a matrix; up = its column 1; M = it.
            QSlerpCopy = QSlerp;
            float Slerped[9];
            CockpitMathEngine()->QuaternionToMatrix(AsMatrix(Slerped), QSlerpCopy);
            Vector SlerpedUp;
            GetColumn(&SlerpedUp, Slerped, 1);
            memcpy(M, Slerped, sizeof(M));
            Cam->UpAxis.x = SlerpedUp.x;
            Cam->UpAxis.y = SlerpedUp.y;
            Cam->UpAxis.z = SlerpedUp.z;
        }
        else
        {
            const Vector ShipUp = Manager->get_ship_up_direction();
            Vector LevelUp;
            LevelUp.x = ShipUp.x;
            LevelUp.y = ShipUp.y;
            LevelUp.z = ShipUp.z;

            // In jump states 2 or 3 (+0x158; the object is tested but not its class), an up
            // other than exactly (0, 1, 0) -- NaN included -- is replaced by it and pushed to the
            // manager.
            const unsigned char* const Held =
                reinterpret_cast<const unsigned char*>(Cam->Watched.target());
            if (Held != nullptr && Held - kWatchableOffset != nullptr)
            {
                FClientShipGameobj* const ShipGo = static_cast<FClientShipGameobj*>(
                    dynamic_cast<FClientGameobj*>(reinterpret_cast<IObjInspect*>(
                        const_cast<unsigned char*>(Held - kWatchableOffset))));
                const int State = ShipGo->JumpState;
                if ((State == kJumpStateJumpOutTunnel || State == kJumpStateJumpInTunnel) &&
                    (LevelUp.x != 0.0f || LevelUp.y != 1.0f || LevelUp.z != 0.0f))
                {
                    LevelUp.x = 0.0f;
                    LevelUp.y = 1.0f;
                    LevelUp.z = 0.0f;
                    Vector Up;
                    Up.x = 0.0f;
                    Up.y = 1.0f;
                    Up.z = 0.0f;
                    Manager->set_ship_up_direction(Up);
                }
            }

            // M levelled to that up; how far it is from +0x204, and how far the levelled up is
            // from the ship's own (both rounded).
            float Levelled[9];
            CameraOrientFromForwardAndUp(reinterpret_cast<Matrix*>(Levelled), reinterpret_cast<Matrix const*>(M), &LevelUp);
            float Dot = static_cast<float>(
                (static_cast<double>(LevelUp.z) * Cam->UpAxis.z +
                 static_cast<double>(LevelUp.y) * Cam->UpAxis.y) +
                static_cast<double>(LevelUp.x) * Cam->UpAxis.x);
            const float UpAgree = static_cast<float>(
                (static_cast<double>(Levelled[7]) * ShipRot[7] +
                 static_cast<double>(Levelled[4]) * ShipRot[4]) +
                static_cast<double>(Levelled[1]) * ShipRot[1]);

            if (Dot > kLevelTurnMinDot)
            {
                // Unless the two ups already agree (|dot| not under 0.9999f), turn from M
                // levelled to +0x204 towards Levelled at (max(dot, 0) + 1) * +0x200 * dt * 0.5f.
                if (fabs(static_cast<double>(Dot)) < kLevelTurnMaxDot)
                {
                    float FromUpAxis[9];
                    CameraOrientFromForwardAndUp(reinterpret_cast<Matrix*>(FromUpAxis), reinterpret_cast<Matrix const*>(M), &Cam->UpAxis);
                    Quaternion QTowards;
                    CockpitMathEngine()->MatrixToQuaternion(QTowards, AsMatrix(Levelled));
                    Quaternion QFrom;
                    CockpitMathEngine()->MatrixToQuaternion(QFrom, AsMatrix(FromUpAxis));
                    if (Dot < 0.0f)
                        Dot = 0.0f;
                    const float T = static_cast<float>(
                        (static_cast<double>(Dot) + 1.0f) * Cam->TurnRateScale * DeltaSeconds * 0.5f);
                    CockpitMathEngine()->QuatSlerp(QSlerp, QFrom, QTowards, T);
                    QSlerpCopy = QSlerp;
                    float Turned[9];
                    CockpitMathEngine()->QuaternionToMatrix(AsMatrix(Turned), QSlerpCopy);
                    Vector TurnedUp;
                    GetColumn(&TurnedUp, Turned, 1);
                    memcpy(Levelled, Turned, sizeof(Levelled));
                    LevelUp.x = TurnedUp.x;
                    LevelUp.y = TurnedUp.y;
                    LevelUp.z = TurnedUp.z;
                }

                Cam->UpAxis.x = LevelUp.x;
                Cam->UpAxis.y = LevelUp.y;
                Cam->UpAxis.z = LevelUp.z;
                Vector Up;
                Up.x = Levelled[1];
                Up.y = Levelled[4];
                Up.z = Levelled[7];
                memcpy(M, Levelled, sizeof(M));
                Manager->submit_camera_up(Up);
            }
            else if (static_cast<double>(UpAgree) > kLevelUpAgreeDot)
            {
                // Almost opposite, but the ship's own up agrees: adopt it without turning.
                Cam->UpAxis.x = LevelUp.x;
                Cam->UpAxis.y = LevelUp.y;
                Cam->UpAxis.z = LevelUp.z;
            }
        }
    }

    if (bReset)
    {
        memcpy(Cam->ResetOrient, M, sizeof(M));
        Vector Offset;
        CockpitMathEngine()->TransformMatrix(Offset, AsMatrix(M), AsVector(&CameraOffset));
        Pos[0] = static_cast<float>(static_cast<double>(Offset.x) + Pos[0]);
        Pos[1] = static_cast<float>(static_cast<double>(Offset.y) + Pos[1]);
        Pos[2] = static_cast<float>(static_cast<double>(Offset.z) + Pos[2]);
    }
    else
    {
        // q(M) before the turn.
        Quaternion QBeforeTurn;
        CockpitMathEngine()->MatrixToQuaternion(QBeforeTurn, AsMatrix(M));

        // The object's axis throttle (answer ignored), into M's frame. Retail's out buffer is
        // the stack slot that still holds OffsetPlusLookAhead, so a getter that writes nothing
        // leaves that there.
        Vector Throttle;
        Throttle.x = OffsetPlusLookAhead[0];
        Throttle.y = OffsetPlusLookAhead[1];
        Throttle.z = OffsetPlusLookAhead[2];
        WatchedObject(Cam)->get_axis_throttle(Throttle);
        Vector Local;
        CockpitMathEngine()->TransposeTransform(Local, AsMatrix(M),
                                                Throttle);
        float LocalThrottle[3];
        LocalThrottle[0] = Local.x;
        LocalThrottle[1] = Local.y;
        LocalThrottle[2] = Local.z;

        // ThrottleTurn = identity, turned by the throttle.
        float ThrottleTurn[9];
        for (int Index = 0; Index < 9; ++Index)
            ThrottleTurn[Index] = 0.0f;
        ThrottleTurn[0] = 1.0f;
        ThrottleTurn[4] = 1.0f;
        ThrottleTurn[8] = 1.0f;
        if (LocalThrottle[0] > 0.0f)
        {
            RotatePitchRows(ThrottleTurn,
                            -(static_cast<double>(LocalThrottle[0]) * Cam->VerticalTurnUpAngle));
            RotateYawRows(ThrottleTurn,
                          -(static_cast<double>(LocalThrottle[1]) * Cam->HorizontalTurnAngle));
        }
        else
        {
            // Not above zero, or NaN: the down angle, and yaw scaled by |x| * 0.8f + 1.0.
            RotatePitchRows(ThrottleTurn,
                            -(static_cast<double>(LocalThrottle[0]) * Cam->VerticalTurnDownAngle));
            const double Scale = fabs(static_cast<double>(LocalThrottle[0])) * kDownTurnYawScale + 1.0;
            RotateYawRows(ThrottleTurn,
                          Scale * -(static_cast<double>(LocalThrottle[1]) * Cam->HorizontalTurnAngle));
        }

        float Product[9];
        CockpitMulMatrix(Product, M, ThrottleTurn);
        memcpy(M, Product, sizeof(M));

        // The level axis through the ship's live rotation, against M's column 1.
        Vector LevelWorld;
        CockpitMathEngine()->TransformMatrix(LevelWorld, AsMatrix(ShipRot), AsVector(&Cam->LevelAxis));
        Vector CamUp;
        GetColumn(&CamUp, M, 1);
        Vector LevelDir;
        LevelDir.x = LevelWorld.x;
        LevelDir.y = LevelWorld.y;
        LevelDir.z = LevelWorld.z;
        const double AxisDot = Vec3DotZyx(&CamUp, &LevelDir);

        // Unless the two are (anti)parallel, the target frame is {normalise(LevelDir x CamUp),
        // normalise(that x LevelDir), -LevelDir} by columns; otherwise M itself.
        float TargetFrame[9];
        if (fabs(AxisDot) < kLevelAxisMaxDot)
        {
            Vector Right;
            Vec3CrossStored(&Right, &LevelDir, &CamUp);
            Vec3NormaliseInPlace(&Right);
            Vector Up;
            Vec3CrossStored(&Up, &Right, &LevelDir);
            Vec3NormaliseInPlace(&Up);
            SetColumn(TargetFrame, 0, &Right);
            SetColumn(TargetFrame, 1, &Up);
            Vector Back = {-LevelDir.x, -LevelDir.y, -LevelDir.z};   // FUN_00411350
            SetColumn(TargetFrame, 2, &Back);
        }
        else
        {
            memcpy(TargetFrame, M, sizeof(TargetFrame));
        }

        Quaternion QTarget;
        CockpitMathEngine()->MatrixToQuaternion(QTarget, AsMatrix(TargetFrame));
        Quaternion QTurned;
        CockpitMathEngine()->MatrixToQuaternion(QTurned, AsMatrix(M));

        // The angle between their column 2s: the cosine clamped to [0, 1] (the low test on the
        // unrounded value, the high on the rounded one).
        Vector CamBack;
        GetColumn(&CamBack, M, 2);
        Vector TargetBack;
        GetColumn(&TargetBack, TargetFrame, 2);
        const double CosSum =
            (static_cast<double>(CamBack.z) * TargetBack.z +
             static_cast<double>(CamBack.y) * TargetBack.y) +
            static_cast<double>(TargetBack.x) * CamBack.x;
        float Cos = static_cast<float>(CosSum);
        if (CosSum < 0.0)
            Cos = 0.0f;
        else if (static_cast<double>(Cos) > 1.0)
            Cos = 1.0f;
        const float Angle = static_cast<float>(acos(static_cast<double>(Cos)));

        // The turn rate W = angle / dt, its growth capped by +0x1d4; the slerp fraction
        // dt / angle * W (1.0f for an angle not above 1e-4), capped by +0x1d8 -- which then also
        // recomputes W from the rounded 1/dt. W is kept in +0x1ac.
        const double InvDt = 1.0f / static_cast<double>(DeltaSeconds);
        const float InvDtStored = static_cast<float>(InvDt);
        double Rate = InvDt * Angle;
        if (Rate - Cam->PrevAngularRate > Cam->AngularAcceleration)
            Rate = static_cast<double>(Cam->AngularAcceleration) + Cam->PrevAngularRate;
        float Fraction = 1.0f;
        if (static_cast<double>(Angle) > kMinTurnAngle)
            Fraction = static_cast<float>(static_cast<double>(DeltaSeconds) / Angle * Rate);
        if (Fraction > Cam->AngularSlerpMultiplier)
        {
            Fraction = Cam->AngularSlerpMultiplier;
            Rate = static_cast<double>(InvDtStored) * Fraction * Angle;
        }
        Cam->PrevAngularRate = static_cast<float>(Rate);

        CockpitMathEngine()->QuatSlerp(QSlerp, QTarget, QTurned, Fraction);
        QSlerpCopy = QSlerp;
        float Slerped[9];
        CockpitMathEngine()->QuaternionToMatrix(AsMatrix(Slerped), QSlerpCopy);
        memcpy(M, Slerped, sizeof(M));

        // The offset through M onto the position -- via the Win3D engine cache DAT_006679d8,
        // not the cockpit one.
        Vector Offset;
        Win3DTransformByMatrix(Offset, AsMatrix(M), AsVector(&CameraOffset));
        Pos[0] = static_cast<float>(static_cast<double>(Offset.x) + Pos[0]);
        Pos[1] = static_cast<float>(static_cast<double>(Pos[1]) + Offset.y);
        Pos[2] = static_cast<float>(static_cast<double>(Pos[2]) + Offset.z);

        // The new level axis = -(ship rotation^T * M's column 2).
        Vector NewBack;
        GetColumn(&NewBack, M, 2);
        Vector BackLocal;
        CockpitMathEngine()->TransposeTransform(BackLocal, AsMatrix(ShipRot), AsVector(&NewBack));
        Cam->LevelAxis.x = -BackLocal.x;
        Cam->LevelAxis.y = -BackLocal.y;
        Cam->LevelAxis.z = -BackLocal.z;

        // The history = M, then M = slerp(q(M), q(before), +0x1e8).
        memcpy(Cam->ResetOrient, M, sizeof(M));
        Quaternion QAfterTurn;
        CockpitMathEngine()->MatrixToQuaternion(QAfterTurn, AsMatrix(M));
        const float LookAheadSlerp = Cam->TurnLookAheadSlerpAmount;
        CockpitMathEngine()->QuatSlerp(QSlerp, QAfterTurn, QBeforeTurn, LookAheadSlerp);
        QSlerpCopy = QSlerp;
        CockpitMathEngine()->QuaternionToMatrix(AsMatrix(Slerped), QSlerpCopy);
        memcpy(M, Slerped, sizeof(M));
    }

    Cam->TranslationCache[0] = Pos[0];
    Cam->TranslationCache[1] = Pos[1];
    Cam->TranslationCache[2] = Pos[2];
    Cam->PrevSpeed = Speed;
    HudCameraApplyShake((FHudCamera*)Cam, reinterpret_cast<Matrix*>(M));
    memcpy(Out12, M, 9 * sizeof(float));
    Out12[9] = Cam->TranslationCache[0];
    Out12[10] = Cam->TranslationCache[1];
    Out12[11] = Cam->TranslationCache[2];
    return true;
}

bool ThirdPersonCameraLoad(FThirdPersonCamera* Cam, const char* CockpitIniPath)
{
    // FUN_005187d0, in retail's order.
    bool bOk = true;

    INI_Reader Reader;
    if (!Reader.open(CockpitIniPath, false))
    {
        // Client\space\Cockpit.cpp:0x347
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: third person cockpit file not found '%s'\n",
              kCockpitCpp, 0x347, CockpitIniPath);
        bOk = false;
    }

    // Retail walks every header only to warn about [ThirdPerson]; no key is read from it. The
    // camera's real parameters come from the ship archetype (slot 0x58).
    while (Reader.read_header())
    {
        if (Reader.is_header("ThirdPerson"))
        {
            // Client\space\Cockpit.cpp:0x34e
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: [ThirdPerson] is obsolete in file %s\n",
                  kCockpitCpp, 0x34e, CockpitIniPath);
        }
    }

    // The 4:3 aspect sanity check, read through the vtable (slots 0x18 and 0x1c).
    ICamera* const Space = AsCamera(SpaceCameraOf((FHudCamera*)Cam));

    const double TanX = tan(static_cast<double>(Space->GetHalfFovXDeg() * kDegreesToRadians));
    const double TanY = tan(static_cast<double>(Space->GetFovYDeg() * kDegreesToRadians));
    const double Aspect = TanX / TanY;

    if (fabs(Aspect - kExpectedAspect) >= kAspectTolerance)
    {
        // Client\space\Cockpit.cpp:0x35e
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: cockpit aspect (%f) is not PC-compatible?\n",
              kCockpitCpp, 0x35e, Aspect);
        bOk = false;
    }

    return bOk;
}

char const* FThirdPersonCamera::GetName()
{
    return ThirdPersonCameraGetName();
}

void FThirdPersonCamera::Attach(void* Object)
{
    ThirdPersonCameraAttach(this, Object);
}

bool FThirdPersonCamera::Activate(int const* ViewRect4)
{
    return ThirdPersonCameraActivate(this, ViewRect4);
}

void FThirdPersonCamera::Update(float DeltaSeconds)
{
    ThirdPersonCameraUpdate(this, DeltaSeconds);
}

bool FThirdPersonCamera::OnEvent(unsigned MsgId)
{
    return ThirdPersonCameraOnEvent(this, MsgId);
}

void FThirdPersonCamera::Deactivate()
{
    HudCameraDerivedDeactivate(this);
}

void FThirdPersonCamera::DrawScene(unsigned LayerFlags, unsigned Param2)
{
    SpaceCameraRenderSlot78(this, LayerFlags, Param2);
}
