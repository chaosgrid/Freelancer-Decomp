// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The docking camera: ctor FUN_0051ea20, slots FUN_0051eaa0 (0x58), LAB_0051ea50 (0x64),
// FUN_0051ea80 (0x68), LAB_0051eae0 (0x6c), and the non-virtual arm FUN_0051ebd0.

#include "Space/Cameras/space_camera_docking.h"

#include "Common/Graphics/fade_to_black.h"
#include "Space/player_behavior.h"
#include "Space/player_spawn.h"
#include "Space/Cameras/space_camera.h"
#include "Space/space_letterbox.h"

#include "common_csys.h"
#include "common_iface.h"   // IObjInspect -- the ship's thiscall getters

#include <string.h>

namespace
{
    // The same third of a second every cinematic camera uses.
    const float kLetterboxSpeed = 3.0f;

    // The elapsed counter is seeded with a tenth of a second, not with zero.
    const float kDockingElapsedSeed = 0.1f;

    // The watcher holds the Watchable, eight bytes into the IObjRW.
    const int kWatchableOffset = 8;

    // FUN_0054baf0: the player's ship as an IObjRW, or null -- the Watched pointer of the
    // BaseWatcher at .data 0x00679744, minus eight.
    void* GetPlayerShipObjRW()
    {
        void* const Watched = GetPlayerShipWatcher();
        if (Watched == nullptr)
            return nullptr;
        return static_cast<unsigned char*>(Watched) - kWatchableOffset;
    }

    // Vtable PTR_LAB_005dc2d4: the GameCamera table with slot 0x60 = FUN_0044fd00 (the shared
    // "never finished" `XOR AL,AL / RET`). Slot 0x54 stays GameCamera's (LAB_0051b3b0).
}

void DockingCameraConstruct(FDockingCamera* Cam)
{
    // No null test on `this`.
    GameCameraConstruct((FGameCamera*)Cam);

    Cam->bFadePending = 0;          // a BYTE
    Cam->TotalSeconds = 0.0f;       // written as a zero dword
    Cam->ElapsedSeconds = 0.0f;

    // Retail then sets the vptr to PTR_LAB_005dc2d4.
}

void DockingCameraAttach(FDockingCamera* Cam, void* Object)
{
    // Before the watcher is touched, the player ship stops steering: behaviour direction reset
    // and strafe burst cancelled, both through IBehaviorManager on the player IObjRW.
    ResetPlayerBehaviorDirection();
    SetPlayerStrafeBurst(0);   // StrafeNone

    Watchable const* const Target =
        (Object != nullptr)
            ? reinterpret_cast<Watchable const*>(static_cast<unsigned char*>(Object) +
                                                 kWatchableOffset)
            : nullptr;

    if (Target != Cam->Watched.target())
        Cam->Watched.set(Target);
}

bool DockingCameraActivate(FDockingCamera* Cam, const int* ViewRect4)
{
    const bool bBaseResult = GameCameraActivate((FGameCamera*)Cam, ViewRect4);

    SetLetterboxTarget(1.0f, kLetterboxSpeed);

    // Clears the FLAG only: the two floats keep whatever the last sequence left, and the arm
    // rewrites both.
    Cam->bFadePending = 0;

    // Unlike the script and static cameras, this one does NOT notify the ship (the calls are
    // absent from 0x0051ea50).

    return bBaseResult;
}

void DockingCameraDeactivate(FDockingCamera* Cam)
{
    // The base deactivate is the empty IDPMsgHandler::OnDisconnect.
    SetLetterboxTarget(0.0f, kLetterboxSpeed);

    Cam->bFadePending = 0;
}

void DockingCameraUpdate(FDockingCamera* Cam, float DeltaSeconds)
{
    // The aim half runs only while there is a ship; the countdown half runs either way.
    void* const Ship = GetPlayerShipObjRW();
    if (Ship != nullptr)
    {
        const Vector& ShipPosition = static_cast<IObjInspect*>(Ship)->get_position();

        const float Ship3[3] = {ShipPosition.x, ShipPosition.y, ShipPosition.z};

        // The camera's position comes through its slot 0x0c, not off the field.
        FSpaceCamera* const Base = (FSpaceCamera*)Cam;
        const Vector* const CameraPosition = AsCamera(Base)->GetPosition();

        Vector Direction;
        Direction.x = Ship3[0] - CameraPosition->x;
        Direction.y = Ship3[1] - CameraPosition->y;
        Direction.z = Ship3[2] - CameraPosition->z;

        const Matrix Look = LookMatrixYup(Direction);

        // Re-read after the call; it is the camera's OWN position -- the docking camera only
        // turns, never moves.
        const Vector* const HoldPosition = AsCamera(Base)->GetPosition();

        memcpy(&Base->World, &Look.E[0][0], 9 * sizeof(float));
        Base->WorldPos.x = HoldPosition->x;
        Base->WorldPos.y = HoldPosition->y;
        Base->WorldPos.z = HoldPosition->z;
        Base->bViewDirty = 1;
    }

    // The docking fade, once armed.
    if (!Cam->bFadePending)
        return;

    // The subtraction below uses the unrounded sum (FST leaves it on the FPU).
    const double ElapsedSum =
        static_cast<double>(DeltaSeconds) + static_cast<double>(Cam->ElapsedSeconds);
    Cam->ElapsedSeconds = static_cast<float>(ElapsedSum);

    // Once the REMAINING time drops below the fade's own duration the fade starts, so it ends
    // with the docking sequence. Ordered "less" only.
    const double Remaining = static_cast<double>(Cam->TotalSeconds) - ElapsedSum;
    if (!(Remaining < static_cast<double>(GetFadeDuration())))
        return;

    // Disarmed first, so the fade is started exactly once.
    Cam->bFadePending = 0;
    FadeOut();
}

void DockingCameraBeginSequence(FDockingCamera* Cam, float Seconds)
{
    // FUN_0051ebd0.
    Cam->bFadePending = 1;
    Cam->TotalSeconds = Seconds;
    Cam->ElapsedSeconds = kDockingElapsedSeed;
}

void FDockingCamera::Attach(void* Object)
{
    DockingCameraAttach(this, Object);
}

bool FDockingCamera::IsFinished()
{
    return false;   // FUN_0044fd00
}

bool FDockingCamera::Activate(int const* ViewRect4)
{
    return DockingCameraActivate(this, ViewRect4);
}

void FDockingCamera::Deactivate()
{
    DockingCameraDeactivate(this);
}

void FDockingCamera::Update(float DeltaSeconds)
{
    DockingCameraUpdate(this, DeltaSeconds);
}
