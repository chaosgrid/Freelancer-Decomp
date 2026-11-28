// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common.dll-internal oriented-box helpers shared by the PathfindManager user-zone probe
// (common_pathfind.cpp, where both are defined) and the zone shapes (common_zone.cpp).
// Neither is exported by retail.

#include "common_ini.h"  // Vector
#include "common_csys.h" // Matrix
#include <cstddef>

// FUN_062dbb90: Out = the 3DMathEngine's TransposeTransform (slot +0x38) of Src by M, bounced
// through a scratch Vector.
void MathTransposeTransformVia3DMathEngine(Vector* Out, Matrix const* M, Vector const* Src);

// The 0x3c-byte oriented box FUN_062dba00 works on: PathfindManager's UserZone
// {BoxRotation, BoxCenter, BoxHalfExtent} and common_zone.cpp's FZoneFrame have this layout.
// Only HalfExtent is read.
struct FOrientedBox
{
    Transform Frame;      // +0x00 orientation, +0x24 centre
    float HalfExtent[3];  // +0x30
};
static_assert(offsetof(FOrientedBox, HalfExtent) == 0x30, "FOrientedBox::HalfExtent offset");
static_assert(sizeof(FOrientedBox) == 0x3c, "FOrientedBox size");

// FUN_062dba00 (__thiscall on Box in retail): Dst = Src snapped onto the box surface; returns
// whether Src was outside.
bool ClampPointToBoxSurface(FOrientedBox const* Box, Vector const* Src, Vector* Dst);
