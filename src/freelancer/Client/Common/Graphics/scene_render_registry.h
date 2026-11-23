// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Registration into the Win3D registry is Win3D::Create (FUN_004b5060, win3d.h: FUN_0059dd00
// parent, FUN_0059dbf0 register, flags = (flags & 0xfb) | 3) and its overrides such as
// FUN_0047b3e0 (the same two calls, flags &= 0xf8, byte +0x4e7 = 1). Neither retail sequence
// unregisters first.
