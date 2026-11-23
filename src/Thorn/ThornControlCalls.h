// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "thorn_controls.h"
#include "thorn_iface.h"

// Retail 06f493a1: publish the captured four-word packet through slot 3.
void ThornSetReverbProperties(IReverbControl* Control,
                              const FThornReverb4* Properties);

// Retail 06f49b45: raw stored-height publication through slot 5.
void ThornSetFloorHeight(ICompoundControl* Control, const float* Height);
