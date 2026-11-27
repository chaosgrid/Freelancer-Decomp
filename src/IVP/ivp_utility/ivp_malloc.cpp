// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP aligned allocator pair.

#include "ivp_malloc.h"

#include <cstdint>
#include <cstdlib>

// FUN_06343fc0. Rounding with `& -Align` is only correct for a power-of-two
// Align, true at every call site. The 0x65981234 marker at the raw start is
// never read back. Retail does not test the malloc result.
void* __cdecl ivp_malloc_aligned(unsigned Size, unsigned Align)
{
    unsigned char* Raw = static_cast<unsigned char*>(malloc(Size + Align + 8));                           // FUN_06343f70
    unsigned char* Aligned = reinterpret_cast<unsigned char*>(((uintptr_t)Raw + Align + 7) & (0 - (uintptr_t)Align));
    *reinterpret_cast<unsigned*>(Raw) = 0x65981234;
    (reinterpret_cast<void**>(Aligned))[-1] = Raw;
    return Aligned;
}

// FUN_06343ff0. No null test; every caller tests first (e.g.
// PhyArch::Part::~Part 0627f9fc, Part::clear 0627fa41).
void __cdecl ivp_free_aligned(void* AlignedPtr)
{
    void* RawPtr = (static_cast<void**>(AlignedPtr))[-1];
    free(RawPtr);                            // FUN_06343fb0
}
