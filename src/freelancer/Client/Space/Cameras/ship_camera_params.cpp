// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Ship ICliObj camera block: the ini value handler at 0x0040763f.
//
// camera_angular_acceleration lands at +0x70 while camera_angular_slerp_multiplier -- which
// retail tests FIRST -- lands at +0x74, and the three turn angles are clamped before the
// degrees-to-radians conversion, not after.

#include "Space/Cameras/ship_camera_params.h"

namespace
{
    // .rdata 0x005c7474, 0x005c77f0, 0x005c77f4.
    const float kMinTurnAngleDegrees = 0.0f;
    const float kMaxTurnAngleDegrees = 90.0f;
    const float kDegreesToRadians = 0.01745329238474369f;
}

float ShipCameraClampTurnAngle(float Degrees)
{
    // Only a strictly negative value clamps to 0 and only a greater one to 90; retail keeps a
    // NaN through both tests, so a NaN turn angle in the ini stays NaN all the way into the
    // camera.
    if (Degrees < kMinTurnAngleDegrees)
        return kMinTurnAngleDegrees * kDegreesToRadians;
    if (Degrees > kMaxTurnAngleDegrees)
        return kMaxTurnAngleDegrees * kDegreesToRadians;
    return Degrees * kDegreesToRadians;
}
