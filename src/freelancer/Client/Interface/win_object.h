// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail WinObject (RTTI .?AUWinObject@@ : Win3D) -- a Win3D carrying one engine mesh
// instance. Base of WinButton -> HUD_Button (0x005d6fac) and UIWinObject (0x005e5d34).
//
// Scalar deleting dtor FUN_004c9210 (slot 0); vftable 0x005d709c (28 slots).
//
// Layout:
//   +0x07c FClientMeshHolder (0x28); the holder's vftable 0x005d2efc is not modelled
//   +0x0a4 flag byte: bit0 draw the tinted pass, bit1 skip the plain pass (and the lighting of
//          the tinted one)
//   +0x0a5 tint RGBA
//   size 0xac

#include "Common/Graphics/client_mesh_load.h"
#include "Interface/win3d.h"

#include <stddef.h>

class WinObject : public Win3D
{
public:
    // FUN_004c9060
    WinObject();
    // FUN_004c9230
    ~WinObject() override;

    // slot 5 FUN_004c91a0
    void SetPosition(float const* NewPosition) override;
    // slot 10 FUN_004c9150
    void Destroy() override;
    // slot 11 FUN_004c91d0
    void Update() override;
    // slot 13 FUN_0059f120
    void Draw() override;

    // FUN_0059f1c0 -- load MeshPath into the holder, then Create(Parent, 0) through slot 9.
    // HUD_Button's "object" key calls it with (0, path, 0).
    bool LoadMeshAndCreate(Win3D* Parent, char const* MeshPath, char const* MaterialPath);

    FClientMeshHolder MeshHolder;   // +0x07c
    unsigned char DrawFlags;        // +0x0a4
    unsigned char TintRgba[4];      // +0x0a5
    unsigned char PadA9[3];         // +0x0a9

private:
    WinObject(WinObject const&) = delete;
    WinObject& operator=(WinObject const&) = delete;
};

static_assert(sizeof(WinObject) == 0xac, "retail WinObject is 0xac bytes");
static_assert(offsetof(WinObject, MeshHolder) == 0x7c, "WinObject +0x7c mesh holder");
static_assert(offsetof(WinObject, DrawFlags) == 0xa4, "WinObject +0xa4 draw flags");
static_assert(offsetof(WinObject, TintRgba) == 0xa5, "WinObject +0xa5 tint");
