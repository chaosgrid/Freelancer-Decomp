// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ivp_random.h"

namespace
{
    // DAT_063ee4dc: the generator's only state; never re-seeded.
    unsigned g_IVP_FastRandomSeed = 1;
}

float IVP_FastRandomFloat()
{
    // seed *= 75 as retail's three LEA multiplies (3, 5, 5).
    g_IVP_FastRandomSeed = g_IVP_FastRandomSeed * 3u;
    g_IVP_FastRandomSeed = g_IVP_FastRandomSeed * 5u;
    g_IVP_FastRandomSeed = g_IVP_FastRandomSeed * 5u;

    const unsigned Low16 = g_IVP_FastRandomSeed & 0xffffu;
    return (float)Low16 * (1.0f / 65536.0f);
}
