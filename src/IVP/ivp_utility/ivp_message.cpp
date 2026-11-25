// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// The IVP library's error-message sink, FUN_06356960. C linkage because the
// vendored qhull sources call it (declared in src/qhull/vendor/mem.h); C++
// callers include ivp_message.h.

#include "ivp_message.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <windows.h>

// ivp_message: vsprintf into a 10000-byte stack buffer (stack probe
// FUN_06391dc0), then OutputDebugStringA.
//
// Retail quirks kept: the "ERROR: " prefix is sprintf'd into the same buffer
// address vsprintf then overwrites, so it never reaches the output; and only
// the first 1000 bytes of the buffer are zeroed.
extern "C" void ivp_message(const char* Format, ...)
{
    char Buffer[10000]; // 0x2710
    memset(Buffer, 0, 1000); // retail only zero-fills the first 0xfa dwords
    sprintf(Buffer, "ERROR: "); // dead write, as in retail
    va_list Args;
    va_start(Args, Format);
    vsprintf(Buffer, Format, Args);
    va_end(Args);
    OutputDebugStringA(Buffer);
}
