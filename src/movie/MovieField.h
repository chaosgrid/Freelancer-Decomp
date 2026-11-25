// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <stddef.h>

// Field arithmetic from movie.dll. Each body is in MovieField.cpp; the retail
// address is on the definition. Calling conventions match the listing RET n.

// The 0x231C-byte scratch of the modular multiply (0x06ac4160, 4250, 42e0) and of
// ReduceLimbsModulo (0x06ac45a0); either allocates one when its Scratch argument is null.
// The multiply writes the double-width product at Product and passes the same block on,
// so the reduction's copy of its Source into Product is in place. Members run up to the next one.
struct FMovieModScratch
{
    unsigned int LeftWindow[0x144];       // +0x0000 0x06ac4160: Movie_EvaluateWindow of Left
    unsigned int LeftMark[0xA];           // +0x0510 its mark bytes
    unsigned int RightWindow[0x145];      // +0x0538 the second operand's window (Movie_AlignDword may start it one dword later)
    unsigned int RightMark[0xA];          // +0x0A4C its mark bytes
    unsigned int Product[0x289];          // +0x0A74 product of the multiply; ReduceLimbsModulo's Input
    unsigned int TempA[0x80];             // +0x1498 ReduceLimbsModulo
    unsigned int TempB[0x80];             // +0x1698 ReduceLimbsModulo
    unsigned int Shifted[0x80];           // +0x1898 ReduceLimbsModulo: the input shifted by the record's shift
    unsigned int ReduceProduct[0xD8];     // +0x1A98 ReduceLimbsModulo: low window product
    unsigned int ReduceProductHigh[0xD8]; // +0x1DF8 ReduceLimbsModulo: high window product (ReduceProduct + 0x360)
    unsigned int PairBase[0x6D];          // +0x2158 ReduceLimbsModulo: window of the partial result
    unsigned int PairTail[4];             // +0x230C its mark bytes
};

static_assert(offsetof(FMovieModScratch, LeftMark) == 0x510, "LEA [scratch+0x510] in 0x06ac4160");
static_assert(offsetof(FMovieModScratch, RightWindow) == 0x538, "LEA [scratch+0x538] in 0x06ac4160 and 0x06ac4250");
static_assert(offsetof(FMovieModScratch, RightMark) == 0xA4C, "LEA [scratch+0xa4c] in 0x06ac4160 and 0x06ac4250");
static_assert(offsetof(FMovieModScratch, Product) == 0xA74, "LEA [scratch+0xa74] in 0x06ac4160, 4250, 42e0 and 45a0");
static_assert(offsetof(FMovieModScratch, TempA) == 0x1498, "LEA [scratch+0x1498] in 0x06ac45a0");
static_assert(offsetof(FMovieModScratch, TempB) == 0x1698, "LEA [scratch+0x1698] in 0x06ac45a0");
static_assert(offsetof(FMovieModScratch, Shifted) == 0x1898, "LEA [scratch+0x1898] in 0x06ac45a0");
static_assert(offsetof(FMovieModScratch, ReduceProduct) == 0x1A98, "LEA [scratch+0x1a98] in 0x06ac45a0");
static_assert(offsetof(FMovieModScratch, ReduceProductHigh) == 0x1DF8, "0x1a98 + 0x360 in 0x06ac45a0");
static_assert(offsetof(FMovieModScratch, PairBase) == 0x2158, "LEA [scratch+0x2158] in 0x06ac45a0");
static_assert(offsetof(FMovieModScratch, PairTail) == 0x230C, "LEA [scratch+0x230c] in 0x06ac45a0");
static_assert(sizeof(FMovieModScratch) == 0x231C, "PUSH 0x231c before Movie_AllocateBlock in 0x06ac4160 and 0x06ac45a0");

// 0x06ac4080. RET 0xC.
unsigned int __stdcall Movie_CopyOrReduceField(unsigned int* Source, unsigned int* Dest, unsigned int* Modulus);

// 0x06ac40e0. RET 0x10.
void __stdcall Movie_AddModulo(unsigned int* Left, unsigned int* Right, unsigned int* Sum, unsigned int* Modulus);

// 0x06ac4160. RET 0x14. Scratch may be null; this function then allocates it.
void __stdcall Movie_MultiplyModulo(unsigned int* Left, unsigned int* Right, unsigned int* Dest, unsigned int* Modulus, unsigned int* Scratch);

// 0x06ac4250. RET 0x18. Scratch is the caller's buffer.
void __stdcall Movie_MultiplyFactorModulo(unsigned int* Source, unsigned int* Factor, unsigned int* FactorTail, unsigned int* Dest, unsigned int* Modulus, unsigned int* Scratch);

// 0x06ac42e0. RET 0x1C. The four terms are forwarded to the product kernel.
void __stdcall Movie_MultiplyTermsModulo(unsigned int* TermA, unsigned int* TermB, unsigned int* TermC, unsigned int* TermD, unsigned int* Dest, unsigned int* Modulus, unsigned int* Scratch);

// 0x06ac4340. RET 0xC.
void __stdcall Movie_NegateModulo(unsigned int* Source, unsigned int* Dest, unsigned int* Modulus);

// 0x06ac4390. RET 0x10. BitCount is a signed shift.
void __stdcall Movie_ShiftModulo(unsigned int* Source, int BitCount, unsigned int* Dest, unsigned int* Modulus);

// 0x06ac4550. RET 0x10.
void __stdcall Movie_SubtractModulo(unsigned int* Left, unsigned int* Right, unsigned int* Dest, unsigned int* Modulus);

// 0x06ac4db0. RET 0x10. Bound[0] is the split limit; Bound is also passed through.
void __stdcall Movie_SplitLimbs(unsigned int* Source, unsigned int Count, unsigned int* Dest, unsigned int* Bound);

// 0x06ac4e20. RET 0x10. 0 if the workspace allocation fails, 1 otherwise.
long __stdcall Movie_ReduceModulo(unsigned int* Source, unsigned int Count, unsigned int* Dest, unsigned int* Modulus);

// 0x06ac4f40. cdecl.
void __cdecl Movie_InitForProcessor();

// 0x06ac4f80. RET 0x4.
void* __stdcall Movie_AllocateBlock(unsigned int Size);

// 0x06ac4fb0. RET 0x4.
void __stdcall Movie_FreeBlock(void* Block);

// 0x06ac4fd0. RET 0x10. High half of the reciprocal product.
unsigned int __stdcall Movie_EstimateQuotient(unsigned int Carry, unsigned int Top, unsigned int Second, unsigned int* Reciprocal);

// 0x06ac5050. RET 0x14. Dividend is (High << 32) | Low.
void __stdcall Movie_DivideDoubleWord(unsigned int Low, unsigned int High, unsigned int Divisor, int* Quotient, int* Remainder);

// 0x06ac5090. RET 0x1C. Quotient may be 0. Returns the remainder's significant length, or 0 on error.
int __stdcall Movie_DivideLimbs(unsigned int* Dividend, unsigned int DividendCount, unsigned int* Divisor, int DivisorCount, unsigned int* Reciprocal, unsigned int Quotient, unsigned int* Remainder);
