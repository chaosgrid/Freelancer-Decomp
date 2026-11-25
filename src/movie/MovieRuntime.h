// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Entry points of movie.dll's statically linked CRT that the rest of the module calls.
// Bodies are in MovieRuntime.cpp.

// 0x06acf3db. movie.dll operator new: __nh_malloc(size, 1). Null on failure, does not throw.
void* __cdecl MovieOperatorNew(unsigned int Size);

// FUN_06acf3d0. movie.dll operator delete: forwards to free (FUN_06ad07b3).
void __cdecl MovieOperatorDelete(void* Block);

// FUN_06ad970e. Throws the "string too long" C++ exception. No return on the retail path.
void __cdecl MovieThrowStringTooLong();

// FUN_06acf876. movie.dll atexit registration (not the CRT atexit import).
int __cdecl MovieRegisterAtexit(void (__cdecl* Callback)());

// LAB_06acf7ef. __amsg_exit(0x19). Slots +0x04..+0x28 of PTR_LAB_06ada20c.
void __cdecl MovieAbortRuntime();
