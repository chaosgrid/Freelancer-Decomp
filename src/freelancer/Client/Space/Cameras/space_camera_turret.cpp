// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail turret camera (in-flight controller + 0x3c8, vtable .rdata 0x005dbcc4): ctor
// 0x0051a210, name 0x0051a2a0, ini loader 0x0051a2b0, Event::Handler face 0x0051ac20, slots
// 0x58 (0x0051a6a0), 0x64 (0x0051a640), 0x6c (0x0051a730) and their view solver FUN_0051a7d0.

#include "Space/Cameras/space_camera_turret.h"
#include "Space/space_scene_obj_draw.h"

#include "Space/Cameras/space_camera_first.h"   // G_FirstPersonMathEngine (DAT_00675220)

#include "common_cobject.h"
#include "common_csys.h"          // Acquire3DMathEngine (FUN_005b73e0)
#include "common_iface.h"         // IObjRW
#include "common_ini.h"
#include "dacom.h"
#include "Core/globals.h"              // GetClientFrameTimeCapped (FUN_0042d680), G_RenderSurface*
#include "Space/HUD/hud_command_dispatch.h" // HudCommandRecord (FUN_00576240)
#include "Space/HUD/hud_target.h"           // HudIsMousePickBlocked (FUN_0045a740)
#include "Common/input_manager.h"        // GetClientMouseButtons, GetClientSoftwareCursor
#include "Space/player_behavior.h"      // IsMouseFlightLocked (FUN_004c7c80)
#include "x86math.h"

#include <cstddef>
#include <math.h>
#include <string.h>

namespace
{
    const char kCockpitCpp[] = "E:\\FL\\Scratch\\Source\\Client\\space\\Cockpit.cpp";

    // Event::Handler face table .rdata 0x005dbcb8: one slot, FUN_0051ac20.

    // FUN_0051a7d0's constants.
    const float kTurretYawWrap = 6.2831854820251465f;        // .rdata 0x005da774 (2*pi)
    const float kTurretPitchLimit = 1.0471975803375244f;     // .rdata 0x005dbe10 (pi/3)
    const float kTurretPitchLimitNeg = -1.0471975803375244f; // .rdata 0x005dbe0c
    const float kTurretMouseAimDelay = 0.25f;                // .rdata 0x005c94f8

    // The identity 3x4 both callers of FUN_0051a7d0 build on the stack first.
    void MakeIdentityView(Transform* View)
    {
        float* const View12 = &View->Rotation.E[0][0];
        for (int Index = 0; Index < 12; ++Index)
            View12[Index] = 0.0f;
        View->Rotation.E[0][0] = 1.0f;
        View->Rotation.E[1][1] = 1.0f;
        View->Rotation.E[2][2] = 1.0f;
    }

    // {orientation, position} -- twelve dwords -- into the camera's world transform (+0x04).
    void StoreWorldView(FTurretCamera* Cam, const Transform* View)
    {
        FSpaceCamera* const Space = SpaceCameraOf(Cam);
        memcpy(&Space->World, &View->Rotation, 9 * sizeof(float));
        Space->WorldPos.x = View->Translation.x;
        Space->WorldPos.y = View->Translation.y;
        Space->WorldPos.z = View->Translation.z;
    }
}

void FTurretCamera::Construct()
{
    // FUN_0051a210, in retail's own order: Z of the tether goes in first, then X and Y.
    FHudCamera::Construct();

    Tether.z = 25.0f;      // 0x41c80000
    Tether.x = 0.0f;
    Tether.y = 5.0f;       // 0x40a00000

    Yaw = 0.0f;
    TargetYaw = 0.0f;
    Pitch = 0.0f;
    TargetPitch = 0.0f;
    YawKeyRate = 0.0f;
    PitchKeyRate = 0.0f;

    KeyRotateRate = 0.75f;       // 0x3f400000 -- nothing in the ini can move this
    YawRotateSpeed = 2.0f;  // 0x40000000
    PitchRotateSpeed = 1.5f;// 0x3fc00000
    AccelSpeed = 5.0f;      // retail reuses the register still holding Tether[1]'s 5.0f

    // +0x134 (MouseAimTime) is not written by the retail ctor: the controller lives in zeroed
    // static storage (G_SpaceApp), as the port's camera does.
}

bool FTurretCamera::Activate(int const* ViewRect4)
{
    // FUN_0051a640: the five turret ids registered on the face at +0xf8, then HUDCamera's
    // activate.
    static const unsigned kTurretEvents[5] = {USER_LOOK_ROTATE_CAMERA_LEFT, USER_LOOK_ROTATE_CAMERA_RIGHT,
                                              USER_LOOK_ROTATE_CAMERA_UP, USER_LOOK_ROTATE_CAMERA_DOWN,
                                              USER_LOOK_ROTATE_CAMERA_RESET};
    FHudCmdHandler* const Face = AsHudCmdHandler();
    for (unsigned Index = 0; Index < 5; ++Index)
        Face->RegisterEvent(kTurretEvents[Index]);
    return FHudCamera::Activate(ViewRect4);
}

bool FTurretCamera::ComputeView(Transform* OutView, float DeltaSeconds, bool)
{
    // FUN_0051a7d0. The third argument (1 from the attach, 0 from the update) is never read.
    FHudCamera* const Hud = this;

    // Not a watched ship (CObject mask 0x503) -> true.
    if (GameCameraWatchedShip(Hud) == nullptr)
        return true;

    IObjRW* const Object = GameCameraGetTarget(Hud);
    Matrix const* const ShipRot = &Object->get_transform().Rotation;

    // This frame's yaw (a float) and pitch (kept double).
    float Yaw = 0.0f;
    double Pitch = 0.0;
    bool bSteer = true;
    if ((GetClientMouseButtons() & 1u) != 1u && !IsMouseFlightLocked())
    {
        // Keys: the clock resets; each non-zero key rate (only an ordered zero skips) times its
        // speed and dt.
        MouseAimTime = 0.0f;
        if (YawKeyRate != 0.0f)
            Yaw = static_cast<float>(static_cast<double>(YawRotateSpeed) * YawKeyRate *
                                     DeltaSeconds);
        if (PitchKeyRate != 0.0f)
            Pitch = static_cast<double>(PitchRotateSpeed) * PitchKeyRate * DeltaSeconds;
    }
    else
    {
        // Left button held or mouse flight locked: the clock runs (compared unrounded); under
        // 0.25s (or unordered) nothing steers yet.
        const double Clock = GetClientFrameTimeCapped() + MouseAimTime;
        MouseAimTime = static_cast<float>(Clock);
        if (!(Clock >= kTurretMouseAimDelay))
        {
            bSteer = false;
        }
        else
        {
            // The cursor's offset from the surface centre, normalised, times speed and dt.
            const double HalfW = static_cast<double>(static_cast<int>(G_RenderSurfaceWidth)) * 0.5f;
            const double HalfH = static_cast<double>(static_cast<int>(G_RenderSurfaceHeight)) * 0.5f;
            int CursorX = 0;
            int CursorY = 0;
            int CursorZ = 0;
            GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);
            Yaw = static_cast<float>((static_cast<double>(CursorX) - HalfW) / HalfW *
                                     YawRotateSpeed * DeltaSeconds);
            Pitch = (static_cast<double>(CursorY) - HalfH) / HalfH * PitchRotateSpeed *
                    DeltaSeconds;
        }
    }

    // Unless both are an ordered zero, move the targets.
    if (bSteer && (Yaw != 0.0f || Pitch != 0.0))
    {
        // Yaw target (+0x110), wrapped into [0, 2*pi] by moving the current yaw (+0x10c) along.
        const double YawTarget = static_cast<double>(Yaw) + TargetYaw;
        TargetYaw = static_cast<float>(YawTarget);
        if (YawTarget > kTurretYawWrap)
        {
            TargetYaw = static_cast<float>(YawTarget - kTurretYawWrap);
            this->Yaw = static_cast<float>(static_cast<double>(this->Yaw) - kTurretYawWrap);
        }
        else if (YawTarget < 0.0)
        {
            TargetYaw = static_cast<float>(YawTarget + kTurretYawWrap);
            this->Yaw = static_cast<float>(static_cast<double>(this->Yaw) + kTurretYawWrap);
        }

        // Pitch target (+0x118), clamped to +/- pi/3.
        const double PitchTarget = Pitch + TargetPitch;
        TargetPitch = static_cast<float>(PitchTarget);
        if (PitchTarget > kTurretPitchLimit)
            TargetPitch = kTurretPitchLimit;
        else if (PitchTarget < kTurretPitchLimitNeg)
            TargetPitch = kTurretPitchLimitNeg;
    }

    // Ease the current angles towards the targets by dt * accel_speed; over 1.0f they snap.
    {
        const double Ease = static_cast<double>(DeltaSeconds) * AccelSpeed;
        if (Ease > 1.0f)
        {
            this->Yaw = TargetYaw;
            this->Pitch = TargetPitch;
        }
        else
        {
            this->Yaw = static_cast<float>(
                (static_cast<double>(TargetYaw) - this->Yaw) * Ease + this->Yaw);
            this->Pitch = static_cast<float>(
                (static_cast<double>(TargetPitch) - this->Pitch) * Ease + this->Pitch);
        }
    }

    // The ship's rotation turned by -yaw (columns 0 and 2; cos rounded to float, sin kept double).
    Matrix M;
    memcpy(&M, ShipRot, sizeof(M));
    {
        const double Angle = -static_cast<double>(this->Yaw);
        const float CosA = static_cast<float>(cos(Angle));
        const double SinA = sin(Angle);
        for (int Row = 0; Row < 3; ++Row)
        {
            const double A = M.E[Row][0];
            const double C = M.E[Row][2];
            M.E[Row][0] = static_cast<float>(static_cast<double>(CosA) * A - SinA * C);
            M.E[Row][2] = static_cast<float>(A * SinA + static_cast<double>(CosA) * C);
        }
    }
    // Then by -pitch (columns 1 and 2).
    {
        const double Angle = -static_cast<double>(this->Pitch);
        const float CosB = static_cast<float>(cos(Angle));
        const double SinB = sin(Angle);
        for (int Row = 0; Row < 3; ++Row)
        {
            const double B = M.E[Row][1];
            const double C = M.E[Row][2];
            M.E[Row][1] = static_cast<float>(static_cast<double>(CosB) * B + SinB * C);
            M.E[Row][2] = static_cast<float>(static_cast<double>(CosB) * C - B * SinB);
        }
    }

    // FUN_00516fe0 with the ship's up column into a stack block that is overwritten before
    // anything reads it -- dead in retail too.
    {
        const Vector Up = {ShipRot->E[0][1], ShipRot->E[1][1], ShipRot->E[2][1]};
        Matrix Discarded;
        CameraOrientFromForwardAndUp(&Discarded, &M, &Up);
    }

    Hud->ApplyShake(&M);

    // The tether (+0x100) through the orientation, using DAT_00675220.
    if (G_FirstPersonMathEngine == nullptr)
        G_FirstPersonMathEngine = Acquire3DMathEngine();
    Vector Offset;
    G_FirstPersonMathEngine->TransformMatrix(Offset, M,
                                             Tether);

    // Retail re-reads the watcher.
    IObjRW* const Again = GameCameraGetTarget(Hud);
    Vector const& ShipPos = Again->get_position();

    memcpy(&OutView->Rotation, &M, 9 * sizeof(float));
    OutView->Translation.x = Offset.x + ShipPos.x;
    OutView->Translation.y = Offset.y + ShipPos.y;
    OutView->Translation.z = Offset.z + ShipPos.z;
    return false;
}

void FTurretCamera::Attach(IObjRW* Object)
{
    // FUN_0051a6a0 (slot 0x58). The view solve's answer is ignored.
    Yaw = 0.0f;
    Pitch = 0.0f;
    TargetYaw = 0.0f;
    TargetPitch = 0.0f;
    FHudCamera::Attach(Object);

    Transform View;
    MakeIdentityView(&View);
    ComputeView(&View, 0.0f, true);
    SpaceCameraOf(this)->bViewDirty = 1;
    StoreWorldView(this, &View);
}

void FTurretCamera::Update(float DeltaSeconds)
{
    // FUN_0051a730 (slot 0x6c).
    Transform View;
    MakeIdentityView(&View);
    ComputeView(&View, DeltaSeconds, false);
    StoreWorldView(this, &View);
    SpaceCameraOf(this)->bViewDirty = 1;
    FHudCamera::Update(DeltaSeconds);
}

bool FTurretCamera::OnEvent(unsigned MsgId)
{
    // FUN_0051ac20 (slot 0 of the Event::Handler face).
    if (HudIsMousePickBlocked())
        return false;
    if (FHudCamera::OnEvent(MsgId))
        return true;

    // The record's key-down byte decides between +/-KeyRotateRate and 0.
    const bool bDown = HudCommandRecord()->bKeyDown != 0;
    switch (MsgId)
    {
    case USER_LOOK_ROTATE_CAMERA_LEFT:
        YawKeyRate = bDown ? -KeyRotateRate : 0.0f;
        return true;
    case USER_LOOK_ROTATE_CAMERA_RIGHT:
        YawKeyRate = bDown ? KeyRotateRate : 0.0f;
        return true;
    case USER_LOOK_ROTATE_CAMERA_UP:
        PitchKeyRate = bDown ? -KeyRotateRate : 0.0f;
        return true;
    case USER_LOOK_ROTATE_CAMERA_DOWN:
        PitchKeyRate = bDown ? KeyRotateRate : 0.0f;
        return true;
    case USER_LOOK_ROTATE_CAMERA_RESET:
        // Recentre.
        Yaw = 0.0f;
        Pitch = 0.0f;
        TargetYaw = 0.0f;
        TargetPitch = 0.0f;
        return true;
    default:
        return false;
    }
}

const char* TurretCameraGetName()
{
    return "Turret";             // 0x0051a2a0 -> .rdata 0x005dbd44
}

bool FTurretCamera::Load(const char* CockpitIniPath)
{
    // FUN_0051a2b0. Same shape as the rear-view loader, four keys instead of one.
    bool bOk = true;

    INI_Reader Reader;
    if (!Reader.open(CockpitIniPath, false))
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: turret camera file not found '%s'\n",
              kCockpitCpp, 0x5a0, CockpitIniPath);
        bOk = false;
    }

    while (Reader.read_header())
    {
        // [Turret] is recognised only to warn, like [ThirdPerson] and [RearView].
        if (Reader.is_header("Turret"))
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: [Turret] is obsolete in file %s\n",
                  kCockpitCpp, 0x5a7, CockpitIniPath);
            continue;
        }

        if (!Reader.is_header("TurretCamera"))
            continue;

        while (Reader.read_value())
        {
            if (Reader.is_value("tether"))
            {
                // Retail pulls the components 1, 0, 2 (FPU stack juggling) and stores 0, 1, 2.
                const float X = Reader.get_value_float(0);
                const float Y = Reader.get_value_float(1);
                const float Z = Reader.get_value_float(2);
                Tether.x = X;
                Tether.y = Y;
                Tether.z = Z;
            }
            else if (Reader.is_value("yaw_rotate_speed"))
            {
                YawRotateSpeed = Reader.get_value_float(0);
            }
            else if (Reader.is_value("pitch_rotate_speed"))
            {
                PitchRotateSpeed = Reader.get_value_float(0);
            }
            else if (Reader.is_value("accel_speed"))
            {
                AccelSpeed = Reader.get_value_float(0);
            }
        }
    }

    return bOk;
}

char const* FTurretCamera::GetName()
{
    return TurretCameraGetName();
}

void FTurretCamera::Deactivate()
{
    DerivedDeactivate();
}

void FTurretCamera::DrawScene(unsigned LayerFlags, unsigned Param2)
{
    SpaceCameraRenderSlot78(this, LayerFlags, Param2);
}
