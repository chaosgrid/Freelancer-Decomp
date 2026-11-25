// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The "static" in-flight camera -- the one a cutscene parks somewhere and leaves. Its vtable is
// .rdata 0x005e0594 and it sits at controller + 0x9a4.
//
// No fields of its own: the controller ctor calls the FGameCamera ctor (FUN_0051b2f0) and then
// overwrites the vptr (0x005493c6), and the docking camera at controller + 0xa9c is exactly 0xf8
// past it. Three overridden slots:
//
//   0x64  LAB_0051e920  activate:   base activate, bars in, tell the ship the view is cinematic
//   0x68  LAB_0051e980  deactivate: the mirror image, bars out
//   0x74  LAB_0051e9d0  event:      event 15 hands the view back to the player
//
// Everything else, including the per-frame update, is FGameCamera's.

#include "Space/Cameras/space_camera_game.h"

struct FStaticCamera : FGameCamera
{
    bool Activate(int const* ViewRect4) override;
    void Deactivate() override;
    int HandleCommand74(void* Command) override;

};

static_assert(sizeof(FStaticCamera) == 0x0f8, "FStaticCamera adds nothing to FGameCamera");

// The controller ctor's step for this slot: FUN_0051b2f0 then the vptr, nothing more.
void StaticCameraConstruct(FStaticCamera* Cam);

// Slot 0x64 (LAB_0051e920).
bool StaticCameraActivate(FStaticCamera* Cam, const int* ViewRect4);

// Slot 0x68 (LAB_0051e980).
void StaticCameraDeactivate(FStaticCamera* Cam);

// Slot 0x74 (LAB_0051e9d0). Retail's argument is the command record SpaceSendCommand
// (FUN_0054bcb0) routes, whose first dword is its USER_* id; the only id this camera reacts to
// is USER_CANCEL (15), and it reacts by asking for the "player" mode. Always returns 0.
int StaticCameraOnEvent(FStaticCamera* Cam, const int* Event);

inline FSpaceCamera* SpaceCameraOf(FStaticCamera* Cam) { return Cam; }
