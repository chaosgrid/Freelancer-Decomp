// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// IVP_U_BigVector growth helpers.

#include "ivp_u_bigvector.h"

#include "../ivp_physics/ivp_surface_manager.h"

#include <cstdlib>

static_assert(sizeof(IVP_SurfaceManager_OutList) == 0xc, "the inline buffer starts right after the 12-byte header");

// FUN_0635d3e0 (callers 0634aadb, 0634ab2e, 0634e357, 06350597, 0635abd2,
// 06362445, 0636247c). The new buffer is OldCapacity*8+8 bytes; the old one is
// freed (and the field nulled) unless it is null or the inline buffer right
// after the header.
void __fastcall IVP_U_BigVector_Grow(IVP_SurfaceManager_OutList* Vector)
{
    void* newBuffer = malloc(static_cast<size_t>(Vector->Capacity) * 8 + 8); // FUN_06343f70
    Vector->Capacity = Vector->Capacity * 2 + 1;
    int count = Vector->Count;
    void** newElems = (void**)newBuffer;
    void** oldElems = Vector->Elems;
    for (int i = 0; i < count; ++i)
    {
        newElems[i] = oldElems[i];
    }
    void** oldBuffer = Vector->Elems;
    if (oldBuffer != (void**)(Vector + 1) && oldBuffer != nullptr)
    {
        free(oldBuffer); // FUN_06343fb0
        Vector->Elems = nullptr;
    }
    Vector->Elems = newElems;
}

// FUN_06350590 (caller 0634e223): grow only when Count >= Capacity (signed).
void __fastcall IVP_U_BigVector_EnsureRoom(IVP_SurfaceManager_OutList* Vector)
{
    if (Vector->Count >= Vector->Capacity)
    {
        IVP_U_BigVector_Grow(Vector);
    }
}
