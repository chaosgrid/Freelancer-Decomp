// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail rear-view camera: the object at in-flight-controller + 0x500, vtable .rdata 0x005dbe24,
// ctor 0x0051ad30, size 0x10c (the gap to the death camera at + 0x60c).
//
// The smallest of the four cockpit cameras: one field, an ini loader that reads one key, and
// its own update in slot 0x6c. Every slot except 0x54 and 0x6c is shared with its siblings:
//
//   +0x50  0x0051c8b0   shared    +0x64  0x0051b0a0   shared with FirstPerson
//   +0x54  0x0051ad70   its own, the name literal
//   +0x58  0x0051cbb0   shared with HUDCamera (install the watch)
//   +0x5c  0x0051cb90   shared    +0x68  0x0051b0b0   shared
//   +0x60  0x005172f0   shared (has-no-target)
//   +0x6c  0x0051b0d0   its own    +0x70  0x0051ad20   shared (tail-jumps to 0x0051da40)
//   +0x74  0x0051da70   shared     +0x78  0x0051b250   shared
//
// Its secondary (input) vptr is 0x005dbe18, whose single method is 0x0051d7f0 -- the same
// implementation HUDCamera uses, so RearView does not override the key handler.

#include "Space/Cameras/space_camera_game.h"

#include <stddef.h>

struct FRearViewCamera : FHudCamera
{
    char const* GetName() override;
    void Deactivate() override;
    void Update(float DeltaSeconds) override;
    void DrawScene(unsigned LayerFlags, unsigned Param2) override;

    float      ViewPosition[3];  // +0x100  [RearViewCamera] view_position, default (0, 5, 20)
};

// The death camera sits at controller + 0x60c and this one at + 0x500.
static_assert(sizeof(FRearViewCamera) == 0x10c, "FRearViewCamera must be 0x10c");
static_assert(offsetof(FRearViewCamera, ViewPosition) == 0x100, "RV +0x100");

// FUN_0051ad30. Chains to the HUDCamera ctor and then plants the default view position (0, 5, 20).
void RearViewCameraConstruct(FRearViewCamera* Cam);

// Slot 0x54 (0x0051ad70): the class name, a plain .rdata literal.
const char* RearViewCameraGetName();

// Slot 0x6c (LAB_0051b0d0): with a watched client ship, the ship's orientation turned by the
// yaw at 0x005dbf48 ((double)(float)pi), shaken (FUN_0051dbd0), and the eye at ship position +
// that orientation x +0x100 -- the camera's world transform; then HUDCamera's update
// (FUN_0051d070).
void RearViewCameraUpdate(FRearViewCamera* Cam, float DeltaSeconds);

// FUN_0051ad80, taking the cockpit ini path. Reads the single key
// [RearViewCamera] view_position and warns about the obsolete [RearView] section. Returns false
// when the file could not be opened, which is the one case retail also reports.
bool RearViewCameraLoad(FRearViewCamera* Cam, const char* CockpitIniPath);
