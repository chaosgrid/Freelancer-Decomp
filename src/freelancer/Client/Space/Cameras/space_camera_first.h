// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail first-person (cockpit) camera: the object at in-flight-controller + 0x00c, vtable
// .rdata 0x005db944, ctor 0x00517180, size 0x1a4 (the gap to the third-person camera at + 0x1b0).
//
// This is the only one of the four cockpit cameras that owns a mesh, and the only one whose
// slot 0x50 is not the shared 0x0051c8b0 (it uses 0x00401e90, a stub). Its own slots are
// 0x54 (0x00517330, the name), 0x58 (0x005178e0, the attach), 0x64 (0x0051b0a0),
// 0x6c (0x00517ba0) and 0x78 (0x00518420); the rest it shares with its siblings.
//
// Its ini loader FUN_005173c0 is the one the in-flight init calls FIRST; all four loaders are
// handed the same cockpit path.

#include "Space/Cameras/space_camera_game.h"

#include "Common/Graphics/client_mesh_load.h"   // FMeshHolder (FUN_00420d40)

#include <stddef.h>

// The three (position, orientation) pairs the ctor plants, at their retail offsets. The loader
// overwrites the middle one from the cockpit mesh's "cam" part; the attach (LAB_005178e0) writes
// the first and the eye position, the per-frame update (LAB_00517ba0) the eye position.
struct FFirstPersonCamera : FHudCamera
{
    void ScaleEnvironmentEffects() override;
    char const* GetName() override;
    void Attach(IObjRW* Object) override;
    void Deactivate() override;
    void Update(float DeltaSeconds) override;
    void DrawScene(unsigned LayerFlags, unsigned Param2) override;
    bool OnEvent(unsigned MsgId) override;


    Vector CockpitHpPosition;        // +0x100  zeroed; the attach writes the "HpCockpit" hardpoint
    Matrix CockpitHpOrientation;     // +0x10c  identity; likewise
    Vector HeadPosition;             // +0x130  zeroed; loader fills it from IEngine slot 0x90
    Matrix HeadOrientation;          // +0x13c  identity; loader fills it from IEngine slot 0x98
    Vector EyePosition;              // +0x160  zeroed; the eye point the attach and update place
    float EyeOrientation[9];         // +0x16c  identity, never written again; the update
                                     //         multiplies it onto the head orientation

    // FUN_00420d40 builds the cockpit mesh holder in place here; FUN_00420dd0
    // loads it from the mesh and material-library names the loader collected, and its +0x04
    // (+0x194 here) is the compound instance CompoundInstanceAnalyze walks to find the
    // cockpit's camera hardpoint.
    FMeshHolder Mesh;                // +0x190

    unsigned char bJustAttached;     // +0x1a0  ctor and attach set 1, the update clears it
    unsigned char Pad1A1[3];

    // FUN_00517180. Chains to the HUDCamera ctor, builds the mesh helper, then plants three
    // identity transforms and clears the two HUDCamera flag bytes at +0xfc and +0xfd.
    void Construct();

    // FUN_00518410: FUN_00420f70 on +0x190 -- release the cockpit mesh holder (destroy the engine
    // instance, drop both material-cache slots). Called by the space teardown FUN_0054aec0.
    void ReleaseMesh();

    // FUN_005173c0, taking the cockpit ini path. Reads [Cockpit] (mesh and
    // material_library into 0x104-byte stack strings, head_turn into two globals), warns about the
    // obsolete [Camera] header, loads the cockpit mesh, takes the head transform from its "cam"
    // part and finishes with the same 4:3 sanity check the third-person loader runs. Returns
    // nothing: a missing file and an odd aspect are FDUMP warnings only.
    void Load(const char* CockpitIniPath);
};

// The third-person camera sits at controller + 0x1b0 and this one at + 0x00c.
static_assert(sizeof(FFirstPersonCamera) == 0x1a4, "FFirstPersonCamera must be 0x1a4");
static_assert(offsetof(FFirstPersonCamera, CockpitHpPosition) == 0x100, "FP +0x100");
static_assert(offsetof(FFirstPersonCamera, CockpitHpOrientation) == 0x10c, "FP +0x10c");
static_assert(offsetof(FFirstPersonCamera, HeadPosition) == 0x130, "FP +0x130");
static_assert(offsetof(FFirstPersonCamera, HeadOrientation) == 0x13c, "FP +0x13c");
static_assert(offsetof(FFirstPersonCamera, EyePosition) == 0x160, "FP +0x160");
static_assert(offsetof(FFirstPersonCamera, EyeOrientation) == 0x16c, "FP +0x16c");
static_assert(offsetof(FFirstPersonCamera, Mesh) == 0x190, "FP +0x190");
static_assert(offsetof(FFirstPersonCamera, bJustAttached) == 0x1a0, "FP +0x1a0");

// [Cockpit] head_turn, which retail keeps in two globals rather than on the camera --
// DAT_006135a0 and DAT_006135a4 (.data 30.0f / 25.0f; written by the loader, read by nothing).
extern float G_CockpitHeadTurnX;    // DAT_006135a0
extern float G_CockpitHeadTurnY;    // DAT_006135a4

// DAT_00675220: Cockpit.cpp's lazily-filled I3DMathEngine cache (FUN_005b73e0 on first use),
// read by the first-person attach/update and by the rear-view update LAB_0051b0d0.
struct I3DMathEngine;
extern I3DMathEngine* G_FirstPersonMathEngine;

// Slot 0x54 (0x00517330): the class name, a plain .rdata literal.
const char* FirstPersonCameraGetName();
