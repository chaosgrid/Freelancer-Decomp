// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The static in-flight camera: built inline by the controller (0x005493c1), slots LAB_0051e920
// (0x64), LAB_0051e980 (0x68) and LAB_0051e9d0 (0x74).

#include "Space/Cameras/space_camera_static.h"

#include "Space/Cameras/space_camera.h"
#include "Space/space_letterbox.h"

namespace
{
    // The same third of a second the script camera uses.
    const float kLetterboxSpeed = 3.0f;

    // Vtable .rdata 0x005e0594 (.?AVStaticCamera@@): GameCamera's 0x005dbf5c except slots 0x64,
    // 0x68 and 0x74 (GameCamera 0x0051b450, 0x0051b480, 0x0053e180).
}

void StaticCameraConstruct(FStaticCamera* Cam)
{
    // The controller calls the FGameCamera ctor directly and swaps the vptr; no ctor of its own,
    // so the class has no fields. No null test (the caller passes SpaceApp + 0x9a4).
    GameCameraConstruct((FGameCamera*)Cam);
}

bool StaticCameraActivate(FStaticCamera* Cam, const int* ViewRect4)
{
    const bool bBaseResult = GameCameraActivate((FGameCamera*)Cam, ViewRect4);

    SetLetterboxTarget(1.0f, kLetterboxSpeed);

    NotifyCinematicView(true);

    return bBaseResult;
}

void StaticCameraDeactivate(FStaticCamera* Cam)
{
    (void)Cam;

    // The base deactivate is the empty IDPMsgHandler::OnDisconnect.

    // The reverse of activate's order: notify BEFORE the bars here, AFTER them there.
    NotifyCinematicView(false);

    SetLetterboxTarget(0.0f, kLetterboxSpeed);
}

int StaticCameraOnEvent(FStaticCamera* Cam, const int* Event)
{
    (void)Cam;

    // No null test on Event.
    if (*Event == kStaticCameraReleaseEvent)
        RequestSpaceCameraMode("player");

    // Always zero, whether or not the event matched.
    return 0;
}

bool FStaticCamera::Activate(int const* ViewRect4)
{
    return StaticCameraActivate(this, ViewRect4);
}

void FStaticCamera::Deactivate()
{
    StaticCameraDeactivate(this);
}

int FStaticCamera::HandleCommand74(void* Command)
{
    return StaticCameraOnEvent(this, (int const*)Command);
}
