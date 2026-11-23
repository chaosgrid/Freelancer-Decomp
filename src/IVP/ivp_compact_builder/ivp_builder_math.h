// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>
#include <cstring>

// Small numeric helpers shared by the polygon / tetra / pointsoup builder
// chain. Retail evaluates these on the x87 stack: intermediates are doubles
// here and only the float stores round.

// Raw .rdata float constants are written by their bit patterns.
inline float IVP_BuilderBitsToFloat(uint32_t Bits)
{
    float Value;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

inline double IVP_BuilderBitsToDouble(uint64_t Bits)
{
    double Value;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

// Retail FUN_0628bed0 (thiscall: this = Out; RET 8), Vector cross product
// Out = A x B. Each component is rounded once; y is formed as
// B.x*A.z - A.x*B.z. All inputs are read before Out is written.
inline void IVP_BuilderCrossProduct(float* Out, const float* A, const float* B)
{
    const double AxBy = (double)(A[0]) * B[1];
    const double AyBx = (double)(A[1]) * B[0];
    const double Y = (double)(B[0]) * A[2] - (double)(A[0]) * B[2];
    const double X = (double)(A[1]) * B[2] - (double)(A[2]) * B[1];
    Out[0] = (float)(X);
    Out[1] = (float)(Y);
    Out[2] = (float)(AxBy - AyBx);
}
