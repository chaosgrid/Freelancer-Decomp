// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// FUN_06356960, the IVP library's error-message sink (ivp_message.cpp). C linkage because the
// vendored qhull sources call it too (they declare it in src/qhull/vendor/mem.h).
extern "C" void ivp_message(const char* Format, ...);
