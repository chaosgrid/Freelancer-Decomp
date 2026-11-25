// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Modular reduction and sliding-window exponentiation from movie.dll. Bodies are in
// MovieExp.cpp. The retail address is on each definition.

// 0x06ac45a0. RET 0x18. Modular reduction of a long integer.
long __stdcall ReduceLimbsModulo(unsigned int* Source, unsigned int Count, unsigned int Mode, unsigned int* Dest, unsigned int* Modulus, void* Scratch);

// 0x06ac5610. RET 0x1C. Signed sliding-window exponentiation.
long __stdcall ExponentiateSlidingWindow(void* Base, int Window, int Span, void* Exponent, int ExponentCount, void* Dest, void* FieldHolder);
