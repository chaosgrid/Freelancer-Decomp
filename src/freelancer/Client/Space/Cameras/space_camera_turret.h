// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail turret camera: the object at in-flight-controller + 0x3c8, vtable .rdata 0x005dbcc4,
// ctor 0x0051a210, size 0x138 (the gap to the rear-view camera at + 0x500).
//
// The ini loader reads four keys and nothing else. Its own vtable slots are
// 0x54 (the name), 0x58 (0x0051a6a0, the attach), 0x64 (0x0051a640) and 0x6c (0x0051a730); the
// rest it shares with the other cockpit cameras. Its secondary input vptr is 0x005dbcb8, whose
// single method is 0x0051ac20 -- its own, unlike the rear-view camera which reuses HUDCamera's.

#include "Space/Cameras/space_camera_game.h"

#include <stddef.h>

struct FTurretCamera : FHudCamera
{
    char const* GetName() override;
    void Attach(void* Object) override;
    bool Activate(int const* ViewRect4) override;
    void Deactivate() override;
    void Update(float DeltaSeconds) override;
    void DrawScene(unsigned LayerFlags, unsigned Param2) override;
    bool OnEvent(unsigned MsgId) override;


    Vector Tether;               // +0x100  [TurretCamera] tether, default (0, 5, 25)
    float Yaw;              // +0x10c  zeroed by the ctor; the current yaw FUN_0051a7d0 turns by
    float TargetYaw;              // +0x110  zeroed; the yaw it eases towards (wrapped by 2*pi)
    float Pitch;              // +0x114  zeroed; the current pitch
    float TargetPitch;              // +0x118  zeroed; the pitch it eases towards (+/- pi/3)
    float YawKeyRate;              // +0x11c  zeroed; the yaw key rate (event 0x32 / 0x33)
    float PitchKeyRate;              // +0x120  zeroed; the pitch key rate (event 0x34 / 0x35)
    float KeyRotateRate;              // +0x124  0.75f, no ini key reaches it
    float YawRotateSpeed;        // +0x128  [TurretCamera] yaw_rotate_speed,   default 2.0
    float PitchRotateSpeed;      // +0x12c  [TurretCamera] pitch_rotate_speed, default 1.5
    float AccelSpeed;            // +0x130  [TurretCamera] accel_speed,        default 5.0
    float MouseAimTime;          // +0x134  FUN_0051a7d0's mouse-aim clock (frame time summed while
                                 //         mouse flight steers; zeroed when it does not). The
                                 //         retail ctor never touches it.

    // FUN_0051a210. Chains to the HUDCamera ctor, then plants the defaults (+0x124 gets 0.75f, which
    // no ini key can change).
    void Construct();

    // FUN_0051a7d0: the turret view -- the watched ship's orientation turned by
    // the eased yaw (+0x10c) and pitch (+0x114), shaken, and the tether (+0x100) through it onto the
    // ship's position, into Out12 = {3x3, position}. The targets (+0x110/+0x118) move by the key
    // rates (+0x11c/+0x120) or, after 0.25s of left-button / locked mouse flight, by the cursor's
    // offset from the surface centre. Answers true (Out12 untouched) when the watcher does not hold
    // a client ship; false otherwise. The last argument is passed by both callers and never read.
    bool ComputeView(Transform* OutView, float DeltaSeconds, bool bAttach);

    // FUN_0051a2b0, taking the cockpit ini path. Reads [TurretCamera] and warns about
    // the obsolete [Turret] section. Returns false when the file could not be opened.
    bool Load(const char* CockpitIniPath);
};

// The rear-view camera sits at controller + 0x500 and this one at + 0x3c8.
static_assert(sizeof(FTurretCamera) == 0x138, "FTurretCamera must be 0x138");
static_assert(offsetof(FTurretCamera, Tether) == 0x100, "TC +0x100");
static_assert(offsetof(FTurretCamera, KeyRotateRate) == 0x124, "TC +0x124");
static_assert(offsetof(FTurretCamera, YawRotateSpeed) == 0x128, "TC +0x128");
static_assert(offsetof(FTurretCamera, PitchRotateSpeed) == 0x12c, "TC +0x12c");
static_assert(offsetof(FTurretCamera, AccelSpeed) == 0x130, "TC +0x130");

// Slot 0x54 (0x0051a2a0): the class name, a plain .rdata literal.
const char* TurretCameraGetName();
