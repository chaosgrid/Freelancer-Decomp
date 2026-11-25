// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Limb and field helpers from movie.dll whose bodies are in MovieFieldHigh.cpp.
// The retail address is on each definition. Calling conventions match the listing RET n.

// 0x06ac8d40. RET 0x8. Strips leading zero limbs, then BSR. Returns the bit length.
int __stdcall LimbBitLength(int Limbs, int Count);

// 0x06ac8bd0. RET 0x10. In-place shift by a signed count in -32..32. Returns the spilled bits.
unsigned int __stdcall ShiftLimbs(unsigned int* Source, unsigned int BitCount, unsigned int* Dest, int Count);

// 0x06ac8b30. RET 0x10. Shift by an arbitrary bit count: bit shift, then a whole-limb slide.
void __stdcall ShiftLimbsByBits(unsigned int* Source, unsigned int BitCount, unsigned int* Dest, unsigned int Count);

// 0x06ac8a90. RET 0x10. Magnitude compare. 1 if Left is greater, -1 if Right is greater, 0 if equal.
long __stdcall CompareLimbs(int Left, unsigned int LeftCount, int Right, int RightCount);

// 0x06ac89d0. RET 0x10. Subtracts Count limbs. Returns the borrow in EAX.
long __stdcall SubtractLimbs(unsigned int* Minuend, unsigned int* Subtrahend, int Difference, int Count);

// 0x06ac8500. RET 0x14. Adds RightCount limbs, then propagates the carry through Left. Returns the carry.
long __stdcall AddLimbs(unsigned int* Left, int LeftCount, unsigned int* Right, int RightCount, int Sum);

// 0x06ac54d0. RET 0xC. Normalizes the leading limbs and writes two dwords: reciprocal, then shift.
void __stdcall StoreLeadingReciprocal(int Limbs, unsigned int Count, unsigned int* Reciprocal);

// 0x06aca7d0. RET 0x4. Fills the twelve window records: doubled widths, then products by 3^n.
void __stdcall FillWindowTable(unsigned int Width);

// 0x06aca860. RET 0x4. The last window slot whose width is >= LimbCount.
unsigned int* __stdcall FindWindowSlot(unsigned int LimbCount);

// 0x06ac9d20. RET 0x18. Shift-and-XOR the longer side into the shorter until one bit length is zero.
long __stdcall ReduceLimbsShiftXor(unsigned int* Left, unsigned int LeftBits, unsigned int* Right, unsigned int RightBits, unsigned int* Coefficient, unsigned int* Remainder);

// 0x06ac9f60. RET 0x14. Bit-serial shift-XOR multiply of Left by Right, reduced by Modulus.
long __stdcall MultiplyLimbsShiftXor(int Left, int Right, unsigned int* Product, int Modulus, unsigned int ModulusBits);

// 0x06ac6bb0. RET 0x10. Seeds the work buffer from the field, or hands the same buffers to 0x06ac7570.
// The fourth push is the scratch base.
long __stdcall SeedFieldWork(unsigned int* Value, unsigned int* Work, int* FieldHolder, unsigned int* Upper);

// 0x06ac8010. RET 0x8. Multiplies the value through the two field factors at +4 and +0x104.
long __stdcall MultiplyByFieldFactors(unsigned int* Value, int* FieldHolder);

// 0x06ac8190. RET 0x8. Kind 1 duplicates the constant at Field+0x10. Otherwise calls 0x06ac3110.
long __stdcall StoreFieldUnit(unsigned int* Dest, int* FieldHolder);

// 0x06ac7210. RET 0x10. Field multiply of Left by Right through Scratch. Kind selects the factor chain.
long __stdcall MultiplyFieldLimbs(unsigned int* Left, unsigned int* Right, int* FieldHolder, unsigned int* Scratch);

// 0x06ac5d70. RET 0x14. For each row, squares the work buffer, then keeps the row or stores the field unit.
long __stdcall CollectSquaredRows(unsigned int* Value, int SquareSteps, int RowCount, void* Table, void* FieldHolder);

// 0x06ac8360. RET 0xC. Rewrites Dest until it compares below Source. Top limb comes from 0x06ac8300.
long __stdcall ReduceLimbsBelow(int Source, unsigned char* Dest, unsigned int Count);

// 0x06aca0f0. RET 0x10. Shifts out factors of two and subtracts until one window is zero.
long __stdcall BinaryGcdLimbs(unsigned int* Left, unsigned int* Context, unsigned int* Coefficient, unsigned int* Result);

// 0x06aca470. RET 0xC. Squares Output and returns 1 when that square equals Input.
unsigned int __stdcall SquareLimbByKind(unsigned int* Input, unsigned int* Output, unsigned int* Context);
