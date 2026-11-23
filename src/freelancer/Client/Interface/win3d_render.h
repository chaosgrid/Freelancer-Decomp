// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Win3D / OpenGL layer helpers: the glClear translation and the camera apply.
// glEnable / glDisable (FUN_00421dd0 / FUN_00421ed0) are Win3DEnable / Win3DDisable in
// win3d_immediate.h.

// FUN_004215e0 -- the glClear translation. Retail only looks at bits 0x4000 (colour) and 0x0100
// (depth); every other bit is ignored. They map to D3DCLEAR_TARGET (1) and D3DCLEAR_ZBUFFER (2).
enum EWin3DClearBits
{
    kWin3DClearColour = 0x4000,
    kWin3DClearDepth  = 0x0100
};

void ClearWin3DBuffers(unsigned int Bits);

// FUN_00564f90 (cdecl, R,G,B,A bytes) -- sets the clear colour (each byte times 1/255), then
// clears both the colour and depth buffers.
void ClearWin3DBuffersToColor(unsigned char const* Rgba);

// FUN_00422950 (cdecl) -- the camera's perspective, then its view copied into DAT_006648dc;
// pipeline state 7 is READ: non-zero sends the copy through SetModelview, zero sets an identity
// view and the camera's view as the world.
struct ICamera;
void Win3DApplyCamera(ICamera* Camera);
