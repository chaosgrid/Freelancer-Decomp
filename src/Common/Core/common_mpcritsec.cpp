// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll MPCritSec: a lazily initialised Win32 critical section.

#include "common_net.h"
#include "common_misc.h" // GbSinglePlayer

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>

// ===========================================================================
// MPCritSec
// ===========================================================================

MPCritSec::MPCritSec()
    : bInitialized(false)
{
    // FUN_06304750. Deviation: retail stores only the flag and leaves the section bytes
    // indeterminate until lock(); the port zeroes them.
    memset(&CriticalSection, 0, sizeof(CriticalSection));
}

MPCritSec::~MPCritSec()
{
    // FUN_06304760
    if (bInitialized)
    {
        bInitialized = false;
        DeleteCriticalSection(&CriticalSection);
    }
}

MPCritSec& MPCritSec::operator=(MPCritSec const& Other)
{
    // FUN_06261d70: a raw copy of all 7 dwords, no self-assignment test (hence memmove). See
    // the header comment for the aliasing caveat this reproduces.
    memmove((void*)this, &Other, sizeof(MPCritSec));
    return *this;
}

void MPCritSec::lock()
{
    // FUN_06304780: a no-op in single-player.
    if (GbSinglePlayer)
        return;
    if (!bInitialized)
    {
        InitializeCriticalSection(&CriticalSection);
        bInitialized = true;
    }
    EnterCriticalSection(&CriticalSection);
}

void MPCritSec::unlock()
{
    // FUN_063047b0: no bInitialized check.
    if (GbSinglePlayer)
        return;
    LeaveCriticalSection(&CriticalSection);
}
