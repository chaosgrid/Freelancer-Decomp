// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "x86math.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <new>

int CallDacomStricmp(const char* Left, const char* Right);

namespace
{
    const char G_ComponentName[] = "3DMathEngine";
    const char* G_ImplementationId = "x86";
    const float G_MinDet = 1e-8f;
    const unsigned int G_RegisterPriority = 0x40000000u;
    // LAB_06f71170 compares exactly 13 bytes (12 chars + NUL), not CRT strcmp.
    const unsigned int G_ComponentNameBytes = 13;
    static_assert(sizeof(G_ComponentName) == G_ComponentNameBytes, "CMPSB ECX=0xD");

    // DAT_06f74030..06f7442f: 0x400 bytes (06f74430 is a separate guard byte). Readers index
    // with (bits >> 14) & 0x3FF.
    unsigned char G_InvSqrtTable[0x400];

    // The table is BSS in retail; FUN_06f71010 (from DllMain) fills all 0x400 entries via
    // FSQRT + FDIVR qword 1.0, then (bits + 0x2000) >> 15, on x87 as here.
    void BuildInvSqrtTable()
    {
        const double One = 1.0;
        for (unsigned int Index = 0; Index < 0x400; ++Index)
        {
            unsigned int Bits = (Index | 0xFC00u) << 14;
            unsigned int RecipBits = 0;
#if defined(_M_IX86)
            __asm
            {
                fld dword ptr [Bits]
                fsqrt
                fdivr qword ptr [One]
                fstp dword ptr [RecipBits]
            }
#else
            union
            {
                unsigned int AsBits;
                float Value;
            } Pun;
            Pun.AsBits = Bits;
            Pun.Value = static_cast<float>(1.0 / std::sqrt(static_cast<double>(Pun.Value)));
            RecipBits = Pun.AsBits;
#endif
            G_InvSqrtTable[Index] = static_cast<unsigned char>((RecipBits + 0x2000u) >> 15);
        }
        // Index 0x200 is the mantissa-1.0 slot (X = 1.0, 4.0, ...): its computed byte wraps
        // to 0x00, and retail patches it to 0xFF.
        G_InvSqrtTable[0x200] = 0xFF;
    }

    float TableInvSqrt(float X)
    {
        union
        {
            unsigned int Bits;
            float Value;
        } Pun;
        Pun.Value = X;
        unsigned int Exponent = (Pun.Bits >> 23) & 0xFFu;
        const unsigned int TableIndex = (Pun.Bits >> 14) & 0x3FFu;
        unsigned int Guess = (0x5F000000u - (Exponent << 22)) & 0xFF800000u;
        Guess |= static_cast<unsigned int>(G_InvSqrtTable[TableIndex]) << 15;
        Pun.Bits = Guess;
        float Y = Pun.Value;
        Y = (3.0f - Y * Y * X) * Y * 0.5f;
        Y = (3.0f - Y * Y * X) * Y * 0.5f;
        return Y;
    }

    // LAB_06f71830: Rotate/TransformMatrix literal shared x87 body.
    // Stores precede the next component source reads, including aliased calls.
    void MulVecByMatrix(Vector& Dst, const Matrix& Mat, const Vector& Source)
    {
        float* ResultVector = &Dst.x;
        const float* MatrixValues = &Mat.E[0][0];
        const float* SourceVector = &Source.x;
        __asm
        {
            mov ecx, MatrixValues
            mov eax, SourceVector
            mov edx, ResultVector
            fld dword ptr [ecx + 8]
            fmul dword ptr [eax + 8]
            fld dword ptr [ecx + 4]
            fmul dword ptr [eax + 4]
            faddp st(1), st(0)
            fld dword ptr [ecx]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fstp dword ptr [edx]
            fld dword ptr [ecx + 014h]
            fmul dword ptr [eax + 8]
            fld dword ptr [ecx + 0ch]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fld dword ptr [ecx + 010h]
            fmul dword ptr [eax + 4]
            faddp st(1), st(0)
            fstp dword ptr [edx + 4]
            fld dword ptr [ecx + 020h]
            fmul dword ptr [eax + 8]
            fld dword ptr [ecx + 018h]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fld dword ptr [ecx + 01ch]
            fmul dword ptr [eax + 4]
            xor eax, eax
            faddp st(1), st(0)
            fstp dword ptr [edx + 8]
        }
    }

    // LAB_06f718f0: shared InverseRotate/TransposeTransform body. Each
    // output float is stored before the next component reads the source.
    void MulVecByTranspose(Vector& Dst, const Matrix& Mat, const Vector& Source)
    {
        float* ResultVector = &Dst.x;
        const float* MatrixValues = &Mat.E[0][0];
        const float* SourceVector = &Source.x;
        __asm
        {
            mov ecx, MatrixValues
            mov eax, SourceVector
            mov edx, ResultVector
            fld dword ptr [ecx + 018h]
            fmul dword ptr [eax + 8]
            fld dword ptr [ecx + 0ch]
            fmul dword ptr [eax + 4]
            faddp st(1), st(0)
            fld dword ptr [ecx]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fstp dword ptr [edx]
            fld dword ptr [ecx + 01ch]
            fmul dword ptr [eax + 8]
            fld dword ptr [ecx + 4]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fld dword ptr [ecx + 010h]
            fmul dword ptr [eax + 4]
            faddp st(1), st(0)
            fstp dword ptr [edx + 4]
            fld dword ptr [ecx + 020h]
            fmul dword ptr [eax + 8]
            fld dword ptr [ecx + 8]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fld dword ptr [ecx + 014h]
            fmul dword ptr [eax + 4]
            xor eax, eax
            faddp st(1), st(0)
            fstp dword ptr [edx + 8]
        }
    }

    void TransposeMatrix(Matrix& Dst, const Matrix& Source)
    {
        Dst.E[0][0] = Source.E[0][0];
        Dst.E[0][1] = Source.E[1][0];
        Dst.E[0][2] = Source.E[2][0];
        Dst.E[1][0] = Source.E[0][1];
        Dst.E[1][1] = Source.E[1][1];
        Dst.E[1][2] = Source.E[2][1];
        Dst.E[2][0] = Source.E[0][2];
        Dst.E[2][1] = Source.E[1][2];
        Dst.E[2][2] = Source.E[2][2];
    }

    void WriteAdjugate(Matrix& Dst, const Matrix& Source, float Scale)
    {
        Dst.E[0][0] = (Source.E[1][1] * Source.E[2][2] - Source.E[1][2] * Source.E[2][1]) * Scale;
        Dst.E[1][0] = -(Source.E[1][0] * Source.E[2][2] - Source.E[1][2] * Source.E[2][0]) * Scale;
        Dst.E[2][0] = (Source.E[1][0] * Source.E[2][1] - Source.E[1][1] * Source.E[2][0]) * Scale;
        Dst.E[0][1] = -(Source.E[0][1] * Source.E[2][2] - Source.E[0][2] * Source.E[2][1]) * Scale;
        Dst.E[1][1] = (Source.E[0][0] * Source.E[2][2] - Source.E[0][2] * Source.E[2][0]) * Scale;
        Dst.E[2][1] = -(Source.E[0][0] * Source.E[2][1] - Source.E[0][1] * Source.E[2][0]) * Scale;
        Dst.E[0][2] = (Source.E[0][1] * Source.E[1][2] - Source.E[0][2] * Source.E[1][1]) * Scale;
        Dst.E[1][2] = -(Source.E[0][0] * Source.E[1][2] - Source.E[0][2] * Source.E[1][0]) * Scale;
        Dst.E[2][2] = (Source.E[0][0] * Source.E[1][1] - Source.E[0][1] * Source.E[1][0]) * Scale;
    }

    // Retail FSQRT + FDIVR dword 1.0 (DAT_06f730f8). quat_slerp (LAB_06f724d0)
    // uses this path, not table InvSqrt (FUN_06f72830).
    float RecipSqrtFsqrt(float LenSq)
    {
        const float One = 1.0f;
#if defined(_M_IX86)
        float Result = 0.0f;
        __asm
        {
            fld dword ptr [LenSq]
            fsqrt
            fdivr dword ptr [One]
            fstp dword ptr [Result]
        }
        return Result;
#else
        return One / std::sqrt(LenSq);
#endif
    }

    // FastDot (FUN_06f71af0 + LAB_06f71d60…LAB_06f71f90): classify each row coeff
    // as ==0 / ==1 / other. Retail layout: kernel @ +0xC, saved-ops int @ +0x10.
    // Callers: FUN_06f719b0 (slot 17) and LAB_06f71fb0 (slot 16) only.
    // Kernels are thiscall, one argument, result in ST0; slots 16/17 are DACOM stdcall.
    struct FFastDot
    {
        float A;
        float B;
        float C;
        float (FFastDot::*Kernel)(const Vector* Vec);
        int SavedOps;

        float FastDotZZZ(const Vector* Vec);
        float FastDotZZO(const Vector* Vec);
        float FastDotZZG(const Vector* Vec);
        float FastDotZOZ(const Vector* Vec);
        float FastDotZOO(const Vector* Vec);
        float FastDotZOG(const Vector* Vec);
        float FastDotZGZ(const Vector* Vec);
        float FastDotZGO(const Vector* Vec);
        float FastDotZGG(const Vector* Vec);
        float FastDotOZZ(const Vector* Vec);
        float FastDotOZO(const Vector* Vec);
        float FastDotOZG(const Vector* Vec);
        float FastDotOOZ(const Vector* Vec);
        float FastDotOOO(const Vector* Vec);
        float FastDotOOG(const Vector* Vec);
        float FastDotOGZ(const Vector* Vec);
        float FastDotOGO(const Vector* Vec);
        float FastDotOGG(const Vector* Vec);
        float FastDotGZZ(const Vector* Vec);
        float FastDotGZO(const Vector* Vec);
        float FastDotGZG(const Vector* Vec);
        float FastDotGOZ(const Vector* Vec);
        float FastDotGOO(const Vector* Vec);
        float FastDotGOG(const Vector* Vec);
        float FastDotGGZ(const Vector* Vec);
        float FastDotGGO(const Vector* Vec);
        float FastDotGGG(const Vector* Vec);
    };

    typedef float (FFastDot::*FFastDotKernel)(const Vector* Vec);

    int ClassifyLane(float X)
    {
        if (X == 0.0f)
            return 0;
        if (X == 1.0f)
            return 1;
        return 2;
    }

#if defined(_M_IX86)
    // DAT_06f730fc — FLD of +0.0; FLDZ is the same value (ZZZ only).
    __declspec(naked) float FFastDot::FastDotZZZ(const Vector* /*Vec*/)
    {
        __asm
        {
            fldz
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotZZO(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 8]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotZZG(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 8]
            fmul dword ptr [ecx + 8]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotZOZ(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 4]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotZOO(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 8]
            fadd dword ptr [eax + 4]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotZOG(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 8]
            fmul dword ptr [ecx + 8]
            fadd dword ptr [eax + 4]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotZGZ(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 4]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotZGO(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 4]
            fadd dword ptr [eax + 8]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotZGG(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 8]
            fmul dword ptr [ecx + 8]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 4]
            faddp st(1), st
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotOZZ(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotOZO(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 8]
            fadd dword ptr [eax]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotOZG(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 8]
            fmul dword ptr [ecx + 8]
            fadd dword ptr [eax]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotOOZ(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 4]
            fadd dword ptr [eax]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotOOO(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 8]
            fadd dword ptr [eax + 4]
            fadd dword ptr [eax]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotOOG(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 8]
            fmul dword ptr [ecx + 8]
            fadd dword ptr [eax + 4]
            fadd dword ptr [eax]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotOGZ(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 4]
            fadd dword ptr [eax]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotOGO(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 4]
            fadd dword ptr [eax + 8]
            fadd dword ptr [eax]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotOGG(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 8]
            fmul dword ptr [ecx + 8]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 4]
            faddp st(1), st
            fadd dword ptr [eax]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotGZZ(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax]
            fmul dword ptr [ecx]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotGZO(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax]
            fmul dword ptr [ecx]
            fadd dword ptr [eax + 8]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotGZG(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 8]
            fmul dword ptr [ecx + 8]
            fld dword ptr [eax]
            fmul dword ptr [ecx]
            faddp st(1), st
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotGOZ(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax]
            fmul dword ptr [ecx]
            fadd dword ptr [eax + 4]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotGOO(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax]
            fmul dword ptr [ecx]
            fadd dword ptr [eax + 8]
            fadd dword ptr [eax + 4]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotGOG(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 8]
            fmul dword ptr [ecx + 8]
            fld dword ptr [eax]
            fmul dword ptr [ecx]
            faddp st(1), st
            fadd dword ptr [eax + 4]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotGGZ(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 4]
            fld dword ptr [eax]
            fmul dword ptr [ecx]
            faddp st(1), st
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotGGO(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 4]
            fld dword ptr [eax]
            fmul dword ptr [ecx]
            faddp st(1), st
            fadd dword ptr [eax + 8]
            ret 4
        }
    }
    __declspec(naked) float FFastDot::FastDotGGG(const Vector* /*Vec*/)
    {
        __asm
        {
            mov eax, dword ptr [esp + 4]
            fld dword ptr [eax + 8]
            fmul dword ptr [ecx + 8]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 4]
            faddp st(1), st
            fld dword ptr [eax]
            fmul dword ptr [ecx]
            faddp st(1), st
            ret 4
        }
    }
#else
    float FFastDot::FastDotZZZ(const Vector*) { return 0.0f; }
    float FFastDot::FastDotZZO(const Vector* Vec) { return Vec->Z; }
    float FFastDot::FastDotZZG(const Vector* Vec) { return Vec->Z * C; }
    float FFastDot::FastDotZOZ(const Vector* Vec) { return Vec->Y; }
    float FFastDot::FastDotZOO(const Vector* Vec) { return Vec->Z + Vec->Y; }
    float FFastDot::FastDotZOG(const Vector* Vec) { return Vec->Z * C + Vec->Y; }
    float FFastDot::FastDotZGZ(const Vector* Vec) { return Vec->Y * B; }
    float FFastDot::FastDotZGO(const Vector* Vec) { return Vec->Y * B + Vec->Z; }
    float FFastDot::FastDotZGG(const Vector* Vec) { return Vec->Z * C + Vec->Y * B; }
    float FFastDot::FastDotOZZ(const Vector* Vec) { return Vec->X; }
    float FFastDot::FastDotOZO(const Vector* Vec) { return Vec->Z + Vec->X; }
    float FFastDot::FastDotOZG(const Vector* Vec) { return Vec->Z * C + Vec->X; }
    float FFastDot::FastDotOOZ(const Vector* Vec) { return Vec->Y + Vec->X; }
    float FFastDot::FastDotOOO(const Vector* Vec) { return Vec->Z + Vec->Y + Vec->X; }
    float FFastDot::FastDotOOG(const Vector* Vec) { return Vec->Z * C + Vec->Y + Vec->X; }
    float FFastDot::FastDotOGZ(const Vector* Vec) { return Vec->Y * B + Vec->X; }
    float FFastDot::FastDotOGO(const Vector* Vec) { return Vec->Y * B + Vec->Z + Vec->X; }
    float FFastDot::FastDotOGG(const Vector* Vec) { return Vec->Z * C + Vec->Y * B + Vec->X; }
    float FFastDot::FastDotGZZ(const Vector* Vec) { return Vec->X * A; }
    float FFastDot::FastDotGZO(const Vector* Vec) { return Vec->X * A + Vec->Z; }
    float FFastDot::FastDotGZG(const Vector* Vec) { return Vec->Z * C + Vec->X * A; }
    float FFastDot::FastDotGOZ(const Vector* Vec) { return Vec->X * A + Vec->Y; }
    float FFastDot::FastDotGOO(const Vector* Vec) { return Vec->X * A + Vec->Z + Vec->Y; }
    float FFastDot::FastDotGOG(const Vector* Vec) { return Vec->Z * C + Vec->X * A + Vec->Y; }
    float FFastDot::FastDotGGZ(const Vector* Vec) { return Vec->Y * B + Vec->X * A; }
    float FFastDot::FastDotGGO(const Vector* Vec) { return Vec->Y * B + Vec->X * A + Vec->Z; }
    float FFastDot::FastDotGGG(const Vector* Vec) { return Vec->Z * C + Vec->Y * B + Vec->X * A; }
#endif

    const FFastDotKernel G_FastDotKernels[27] = {
        &FFastDot::FastDotZZZ, &FFastDot::FastDotZZO, &FFastDot::FastDotZZG, &FFastDot::FastDotZOZ,
        &FFastDot::FastDotZOO, &FFastDot::FastDotZOG, &FFastDot::FastDotZGZ, &FFastDot::FastDotZGO,
        &FFastDot::FastDotZGG, &FFastDot::FastDotOZZ, &FFastDot::FastDotOZO, &FFastDot::FastDotOZG,
        &FFastDot::FastDotOOZ, &FFastDot::FastDotOOO, &FFastDot::FastDotOOG, &FFastDot::FastDotOGZ,
        &FFastDot::FastDotOGO, &FFastDot::FastDotOGG, &FFastDot::FastDotGZZ, &FFastDot::FastDotGZO,
        &FFastDot::FastDotGZG, &FFastDot::FastDotGOZ, &FFastDot::FastDotGOO, &FFastDot::FastDotGOG,
        &FFastDot::FastDotGGZ, &FFastDot::FastDotGGO, &FFastDot::FastDotGGG};

    const int G_FastDotSavedOps[27] = {5, 5, 4, 5, 4, 3, 4, 3, 2, 5, 4, 3, 4, 3, 2, 3, 2, 1, 4, 3, 2, 3, 2, 1, 2, 1, 0};

    static_assert(offsetof(FFastDot, A) == 0, "FastDot A");
    static_assert(offsetof(FFastDot, B) == 4, "FastDot B");
    static_assert(offsetof(FFastDot, C) == 8, "FastDot C");
    static_assert(offsetof(FFastDot, Kernel) == 0xC, "retail kernel @ +0xC");
    static_assert(offsetof(FFastDot, SavedOps) == 0x10, "retail saved-ops @ +0x10");
    static_assert(sizeof(FFastDotKernel) == 4, "MSVC simple-class member pointer is a code address");

    // Plain member dispatch; the ST0 result is stored straight to Dst.
    void StoreKernelPlain(FFastDot& Dot, const Vector* Src, float* Dst)
    {
        *Dst = (Dot.*Dot.Kernel)(Src);
    }

    void StoreKernelAdd(FFastDot& Dot, const Vector* Src, float* Dst, const float* Addend)
    {
        *Dst = (Dot.*Dot.Kernel)(Src) + *Addend;
    }

    void BindFastDot(FFastDot& Dot, float A, float B, float C)
    {
        Dot.A = A;
        Dot.B = B;
        Dot.C = C;
        const int KernelId = ClassifyLane(A) * 9 + ClassifyLane(B) * 3 + ClassifyLane(C);
        Dot.SavedOps = G_FastDotSavedOps[KernelId];
        Dot.Kernel = G_FastDotKernels[KernelId];
    }

    void TransformListFast(Vector* Dst, const Matrix& Mat, const Vector* Source, int Count,
                           const Vector* Translation)
    {
        FFastDot Row0;
        FFastDot Row1;
        FFastDot Row2;
        BindFastDot(Row0, Mat.E[0][0], Mat.E[0][1], Mat.E[0][2]);
        BindFastDot(Row1, Mat.E[1][0], Mat.E[1][1], Mat.E[1][2]);
        BindFastDot(Row2, Mat.E[2][0], Mat.E[2][1], Mat.E[2][2]);

        const Vector* End = Source + Count;
        const Vector* Src = Source;
        Vector* Out = Dst;

        if (Row0.SavedOps + Row1.SavedOps + Row2.SavedOps < 4)
        {
            while (Src < End)
            {
                const float X = Src->x;
                const float Y = Src->y;
                const float Z = Src->z;
                if (Translation != nullptr)
                {
                    // The FIRST row adds X/Z/Y; rows 1/2 add Z/Y/X.
                    // Each destination is written before reading the next row.
                    Out->x = ((X * Mat.E[0][0] + Z * Mat.E[0][2]) +
                              Y * Mat.E[0][1]) + Translation->x;
                    Out->y = ((Z * Mat.E[1][2] + Y * Mat.E[1][1]) +
                              X * Mat.E[1][0]) + Translation->y;
                    Out->z = ((Z * Mat.E[2][2] + Y * Mat.E[2][1]) +
                              X * Mat.E[2][0]) + Translation->z;
                }
                else
                {
                    // Z/Y/X addition order, one final float store.
                    Out->x = (Z * Mat.E[0][2] + Y * Mat.E[0][1]) + X * Mat.E[0][0];
                    Out->y = (Z * Mat.E[1][2] + Y * Mat.E[1][1]) + X * Mat.E[1][0];
                    Out->z = (Z * Mat.E[2][2] + Y * Mat.E[2][1]) + X * Mat.E[2][0];
                }
                ++Src;
                ++Out;
            }
            return;
        }

        if (Translation != nullptr)
        {
            while (Src < End)
            {
                StoreKernelAdd(Row0, Src, &Out->x, &Translation->x);
                StoreKernelAdd(Row1, Src, &Out->y, &Translation->y);
                StoreKernelAdd(Row2, Src, &Out->z, &Translation->z);
                ++Src;
                ++Out;
            }
            return;
        }

        while (Src < End)
        {
            StoreKernelPlain(Row0, Src, &Out->x);
            StoreKernelPlain(Row1, Src, &Out->y);
            StoreKernelPlain(Row2, Src, &Out->z);
            ++Src;
            ++Out;
        }
    }
}

struct FMathEngine final : I3DMathEngine, IComponentFactory
{
    GENRESULT DACOM_API QueryInterface(const char* /*Iid*/, void** Out) override
    {
        if (Out != nullptr)
            *Out = nullptr;
        return GR_GENERIC;
    }

    unsigned long DACOM_API AddRef() override { return 1; }
    unsigned long DACOM_API Release() override { return 1; }

    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Instance) override
    {
        if (Instance == nullptr)
            return GR_INVALID_PARMS;
        *Instance = nullptr;

        FMathDesc* Info = reinterpret_cast<FMathDesc*>(Desc);
        if (Info == nullptr || Info->InterfaceName == nullptr)
            return GR_INVALID_PARMS;

        if (Info->Size != sizeof(FMathDesc) ||
            std::memcmp(Info->InterfaceName, G_ComponentName, G_ComponentNameBytes) != 0)
            return GR_INTERFACE_UNSUPPORTED;

        if (Info->Implementation != nullptr && CallDacomStricmp(Info->Implementation, G_ImplementationId) != 0)
            return GR_INTERFACE_UNSUPPORTED;

        AddRef();
        *Instance = static_cast<I3DMathEngine*>(this);
        return GR_OK;
    }

    GENRESULT DACOM_API Inverse(Matrix& Dst, const Matrix& Source) override
    {
        const float Determinant = Det(Source);
        if (std::fabs(Determinant) > G_MinDet)
        {
            WriteAdjugate(Dst, Source, 1.0f / Determinant);
            return GR_OK;
        }
        WriteAdjugate(Dst, Source, 1.0f);
        return GR_INVALID_PARMS;
    }

    GENRESULT DACOM_API Scale(Matrix& Dst, const Matrix& Source, float Scale) override
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Col = 0; Col < 3; ++Col)
                Dst.E[Row][Col] = Source.E[Row][Col] * Scale;
        }
        return GR_OK;
    }

    float DACOM_API Det(const Matrix& Source) override
    {
        return Source.E[0][0] * Source.E[1][1] * Source.E[2][2] +
               Source.E[0][1] * Source.E[1][2] * Source.E[2][0] +
               Source.E[0][2] * Source.E[1][0] * Source.E[2][1] -
               Source.E[0][0] * Source.E[1][2] * Source.E[2][1] -
               Source.E[0][1] * Source.E[1][0] * Source.E[2][2] -
               Source.E[0][2] * Source.E[1][1] * Source.E[2][0];
    }

    GENRESULT DACOM_API MulQuat(Quaternion& Dst, const Quaternion& A, const Quaternion& B) override
    {
        Dst.w = A.w * B.w - (A.x * B.x + A.y * B.y + A.z * B.z);
        Dst.x = A.w * B.x + B.w * A.x + (A.y * B.z - A.z * B.y);
        Dst.y = A.w * B.y + B.w * A.y + (A.z * B.x - A.x * B.z);
        Dst.z = A.w * B.z + B.w * A.z + (A.x * B.y - A.y * B.x);
        return GR_OK;
    }

    GENRESULT DACOM_API MulTransform(Transform& Dst, const Transform& A, const Transform& B) override
    {
        // LAB_06f716f0: literal row/translation interleave and x87 sum order.
        // Input words are read after each output store, including aliased calls.
        float* ResultTransform = &Dst.Rotation.E[0][0];
        const float* LeftTransform = &A.Rotation.E[0][0];
        const float* RightTransform = &B.Rotation.E[0][0];
        __asm
        {
            mov eax, LeftTransform
            mov ecx, RightTransform
            mov edx, ResultTransform
            fld dword ptr [eax]
            fmul dword ptr [ecx]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 0ch]
            faddp st(1), st(0)
            fld dword ptr [ecx + 018h]
            fmul dword ptr [eax + 8]
            faddp st(1), st(0)
            fstp dword ptr [edx]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 010h]
            fld dword ptr [ecx + 01ch]
            fmul dword ptr [eax + 8]
            faddp st(1), st(0)
            fld dword ptr [ecx + 4]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fstp dword ptr [edx + 4]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 014h]
            fld dword ptr [ecx + 020h]
            fmul dword ptr [eax + 8]
            faddp st(1), st(0)
            fld dword ptr [ecx + 8]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fstp dword ptr [edx + 8]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 028h]
            fld dword ptr [ecx + 02ch]
            fmul dword ptr [eax + 8]
            faddp st(1), st(0)
            fld dword ptr [ecx + 024h]
            fmul dword ptr [eax]
            faddp st(1), st(0)
            fadd dword ptr [eax + 024h]
            fstp dword ptr [edx + 024h]
            fld dword ptr [eax + 010h]
            fmul dword ptr [ecx + 0ch]
            fld dword ptr [eax + 0ch]
            fmul dword ptr [ecx]
            faddp st(1), st(0)
            fld dword ptr [eax + 014h]
            fmul dword ptr [ecx + 018h]
            faddp st(1), st(0)
            fstp dword ptr [edx + 0ch]
            fld dword ptr [eax + 014h]
            fmul dword ptr [ecx + 01ch]
            fld dword ptr [eax + 010h]
            fmul dword ptr [ecx + 010h]
            faddp st(1), st(0)
            fld dword ptr [eax + 0ch]
            fmul dword ptr [ecx + 4]
            faddp st(1), st(0)
            fstp dword ptr [edx + 010h]
            fld dword ptr [eax + 014h]
            fmul dword ptr [ecx + 020h]
            fld dword ptr [eax + 010h]
            fmul dword ptr [ecx + 014h]
            faddp st(1), st(0)
            fld dword ptr [eax + 0ch]
            fmul dword ptr [ecx + 8]
            faddp st(1), st(0)
            fstp dword ptr [edx + 014h]
            fld dword ptr [ecx + 02ch]
            fmul dword ptr [eax + 014h]
            fld dword ptr [ecx + 028h]
            fmul dword ptr [eax + 010h]
            faddp st(1), st(0)
            fld dword ptr [ecx + 024h]
            fmul dword ptr [eax + 0ch]
            faddp st(1), st(0)
            fadd dword ptr [eax + 028h]
            fstp dword ptr [edx + 028h]
            fld dword ptr [eax + 01ch]
            fmul dword ptr [ecx + 0ch]
            fld dword ptr [eax + 020h]
            fmul dword ptr [ecx + 018h]
            faddp st(1), st(0)
            fld dword ptr [ecx]
            fmul dword ptr [eax + 018h]
            faddp st(1), st(0)
            fstp dword ptr [edx + 018h]
            fld dword ptr [eax + 020h]
            fmul dword ptr [ecx + 01ch]
            fld dword ptr [eax + 01ch]
            fmul dword ptr [ecx + 010h]
            faddp st(1), st(0)
            fld dword ptr [ecx + 4]
            fmul dword ptr [eax + 018h]
            faddp st(1), st(0)
            fstp dword ptr [edx + 01ch]
            fld dword ptr [eax + 020h]
            fmul dword ptr [ecx + 020h]
            fld dword ptr [eax + 01ch]
            fmul dword ptr [ecx + 014h]
            faddp st(1), st(0)
            fld dword ptr [ecx + 8]
            fmul dword ptr [eax + 018h]
            faddp st(1), st(0)
            fstp dword ptr [edx + 020h]
            fld dword ptr [eax + 020h]
            fmul dword ptr [ecx + 02ch]
            fld dword ptr [eax + 01ch]
            fmul dword ptr [ecx + 028h]
            faddp st(1), st(0)
            fld dword ptr [ecx + 024h]
            fmul dword ptr [eax + 018h]
            faddp st(1), st(0)
            fadd dword ptr [eax + 02ch]
            xor eax, eax
            fstp dword ptr [edx + 02ch]
        }
        return GR_OK;
    }

    GENRESULT DACOM_API MulMatrix(Matrix& Dst, const Matrix& A, const Matrix& B) override
    {
        // LAB_06f71670: literal x87 loop, preserving product/add/store order
        // and immediate alias visibility between individual result writes.
        float* ResultMatrix = &Dst.E[0][0];
        const float* LeftMatrix = &A.E[0][0];
        const float* RightMatrix = &B.E[0][0];
        __asm
        {
            mov edx, ResultMatrix
            mov ecx, RightMatrix
            mov edi, LeftMatrix
            mov esi, edx
            sub esi, edi
            lea eax, [edi + 4]
            mov edi, 3
        MatrixRowLoop:
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 18h]
            fld dword ptr [ecx + 0ch]
            fmul dword ptr [eax]
            add eax, 0ch
            add edx, 0ch
            dec edi
            faddp st(1), st(0)
            fld dword ptr [eax - 10h]
            fmul dword ptr [ecx]
            faddp st(1), st(0)
            fstp dword ptr [edx - 0ch]
            fld dword ptr [ecx + 4]
            fmul dword ptr [eax - 10h]
            fld dword ptr [eax - 8]
            fmul dword ptr [ecx + 1ch]
            faddp st(1), st(0)
            fld dword ptr [ecx + 10h]
            fmul dword ptr [eax - 0ch]
            faddp st(1), st(0)
            fstp dword ptr [esi + eax - 0ch]
            fld dword ptr [ecx + 8]
            fmul dword ptr [eax - 10h]
            fld dword ptr [eax - 8]
            fmul dword ptr [ecx + 20h]
            faddp st(1), st(0)
            fld dword ptr [ecx + 14h]
            fmul dword ptr [eax - 0ch]
            faddp st(1), st(0)
            fstp dword ptr [edx - 4]
            jnz MatrixRowLoop
        }
        return GR_OK;
    }

    GENRESULT DACOM_API TransformQuat(Vector& Dst, const Quaternion& Quat, const Vector& Source) override
    {
        const float DotVv = Quat.x * Quat.x + Quat.y * Quat.y + Quat.z * Quat.z;
        const float Scale = Quat.w * Quat.w - DotVv;
        const float DotQv = Quat.x * Source.x + Quat.y * Source.y + Quat.z * Source.z;
        const float CrossX = Quat.y * Source.z - Quat.z * Source.y;
        const float CrossY = Quat.z * Source.x - Quat.x * Source.z;
        const float CrossZ = Quat.x * Source.y - Quat.y * Source.x;
        Dst.x = Source.x * Scale + Quat.x * (2.0f * DotQv) + CrossX * (Quat.w * 2.0f);
        Dst.y = Source.y * Scale + Quat.y * (2.0f * DotQv) + CrossY * (Quat.w * 2.0f);
        Dst.z = Source.z * Scale + Quat.z * (2.0f * DotQv) + CrossZ * (Quat.w * 2.0f);
        return GR_OK;
    }

    GENRESULT DACOM_API TransformXform(Vector& Dst, const Transform& Xform, const Vector& Source) override
    {
        // LAB_06f71890: translation is added while each dot remains extended,
        // then that component is stored before the next source read.
        float* ResultVector = &Dst.x;
        const float* TransformValues = &Xform.Rotation.E[0][0];
        const float* SourceVector = &Source.x;
        __asm
        {
            mov eax, TransformValues
            mov ecx, SourceVector
            mov edx, ResultVector
            fld dword ptr [eax + 8]
            fmul dword ptr [ecx + 8]
            fld dword ptr [eax + 4]
            fmul dword ptr [ecx + 4]
            faddp st(1), st(0)
            fld dword ptr [eax]
            fmul dword ptr [ecx]
            faddp st(1), st(0)
            fadd dword ptr [eax + 24h]
            fstp dword ptr [edx]
            fld dword ptr [eax + 14h]
            fmul dword ptr [ecx + 8]
            fld dword ptr [eax + 0ch]
            fmul dword ptr [ecx]
            faddp st(1), st(0)
            fld dword ptr [eax + 10h]
            fmul dword ptr [ecx + 4]
            faddp st(1), st(0)
            fadd dword ptr [eax + 28h]
            fstp dword ptr [edx + 4]
            fld dword ptr [eax + 20h]
            fmul dword ptr [ecx + 8]
            fld dword ptr [eax + 18h]
            fmul dword ptr [ecx]
            faddp st(1), st(0)
            fld dword ptr [eax + 1ch]
            fmul dword ptr [ecx + 4]
            faddp st(1), st(0)
            fadd dword ptr [eax + 2ch]
            fstp dword ptr [edx + 8]
        }
        return GR_OK;
    }

    GENRESULT DACOM_API TransformMatrix(Vector& Dst, const Matrix& Mat, const Vector& Source) override
    {
        MulVecByMatrix(Dst, Mat, Source);
        return GR_OK;
    }

    GENRESULT DACOM_API Rotate(Vector& Dst, const Transform& Xform, const Vector& Source) override
    {
        MulVecByMatrix(Dst, Xform.Rotation, Source);
        return GR_OK;
    }

    GENRESULT DACOM_API InverseRotate(Vector& Dst, const Transform& Xform, const Vector& Source) override
    {
        MulVecByTranspose(Dst, Xform.Rotation, Source);
        return GR_OK;
    }

    GENRESULT DACOM_API TransposeTransform(Vector& Dst, const Matrix& Mat, const Vector& Source) override
    {
        MulVecByTranspose(Dst, Mat, Source);
        return GR_OK;
    }

    GENRESULT DACOM_API InverseTransform(Vector& Dst, const Transform& Xform, const Vector& Source) override
    {
        const Vector Delta = {Source.x - Xform.Translation.x, Source.y - Xform.Translation.y,
                               Source.z - Xform.Translation.z};
        MulVecByTranspose(Dst, Xform.Rotation, Delta);
        return GR_OK;
    }

    GENRESULT DACOM_API TransformListXform(Vector* Dst, const Transform& Xform, const Vector* Source,
                                           int Count) override
    {
        if (Xform.Translation.x == 0.0f && Xform.Translation.y == 0.0f && Xform.Translation.z == 0.0f)
            return TransformListMatrix(Dst, Xform.Rotation, Source, Count);

        TransformListFast(Dst, Xform.Rotation, Source, Count, &Xform.Translation);
        return GR_OK;
    }

    GENRESULT DACOM_API TransformListMatrix(Vector* Dst, const Matrix& Mat, const Vector* Source,
                                            int Count) override
    {
        TransformListFast(Dst, Mat, Source, Count, nullptr);
        return GR_OK;
    }

    GENRESULT DACOM_API TransposeTransformList(Vector* Dst, const Matrix& Mat, const Vector* Source,
                                               int Count) override
    {
        Matrix Transposed;
        TransposeMatrix(Transposed, Mat);
        return TransformListMatrix(Dst, Transposed, Source, Count);
    }

    GENRESULT DACOM_API InverseTransformList(Vector* Dst, const Transform& Xform, const Vector* Source,
                                             int Count) override
    {
        Vector NegTranslation = {-Xform.Translation.x, -Xform.Translation.y, -Xform.Translation.z};
        Vector InvTranslation;
        MulVecByTranspose(InvTranslation, Xform.Rotation, NegTranslation);

        Transform Inverse;
        std::memset(&Inverse, 0, sizeof(Inverse));
        TransposeMatrix(Inverse.Rotation, Xform.Rotation);
        Inverse.Translation = InvTranslation;
        return TransformListXform(Dst, Inverse, Source, Count);
    }

    GENRESULT DACOM_API MatrixToQuaternion(Quaternion& Dst, const Matrix& Source) override
    {
        const Matrix* SourceMatrix = &Source;
        Quaternion* DestinationQuat = &Dst;
        const float Zero = 0.0f;
        const float One = 1.0f;
        const float Half = 0.5f;
        static const int NextAxis[3] = {1, 2, 0};
        // LAB_06f72290 keeps the trace, root and reciprocal on x87;
        // only the four destination writes round to float. AND EAX,4100/JNZ
        // takes the not-greater (including unordered) branch in all three tests.
        // Keep the branch-specific store/read order for aliased source/output.
        __asm
        {
            mov ecx, SourceMatrix
            mov edi, DestinationQuat
            fld dword ptr [ecx + 10h]
            fadd dword ptr [ecx]
            fadd dword ptr [ecx + 20h]
            fcom Zero
            fnstsw ax
            and eax, 4100h
            jnz MatrixQuatNotPositive
            fadd One
            fsqrt
            fld Half
            fmul st(0), st(1)
            fstp dword ptr [edi]
            fdivr Half
            fld dword ptr [ecx + 1ch]
            fsub dword ptr [ecx + 14h]
            fmul st(0), st(1)
            fstp dword ptr [edi + 4]
            fld dword ptr [ecx + 8]
            fsub dword ptr [ecx + 18h]
            fmul st(0), st(1)
            fstp dword ptr [edi + 8]
            fld dword ptr [ecx + 0ch]
            fsub dword ptr [ecx + 4]
            fmul st(0), st(1)
            fstp dword ptr [edi + 0ch]
            fstp st(0)
            jmp MatrixQuatDone
        MatrixQuatNotPositive:
            fstp st(0)
            fld dword ptr [ecx + 10h]
            fcomp dword ptr [ecx]
            xor edx, edx
            fnstsw ax
            and eax, 4100h
            jnz MatrixQuatSecondDiagonal
            mov edx, 1
        MatrixQuatSecondDiagonal:
            fld dword ptr [ecx + 20h]
            mov eax, edx
            shl eax, 4
            fcomp dword ptr [ecx + eax]
            fnstsw ax
            and eax, 4100h
            jnz MatrixQuatIndices
            mov edx, 2
        MatrixQuatIndices:
            mov eax, dword ptr NextAxis[edx * 4]
            mov esi, dword ptr NextAxis[eax * 4]
            mov ebx, esi
            shl ebx, 4
            fld dword ptr [ecx + ebx]
            mov ebx, eax
            shl ebx, 4
            fadd dword ptr [ecx + ebx]
            mov ebx, edx
            shl ebx, 4
            fsubr dword ptr [ecx + ebx]
            fadd One
            fsqrt
            fld Half
            fmul st(0), st(1)
            fstp dword ptr [edi + edx * 4 + 4]
            fdivr Half
            lea ebx, [esi + esi * 2]
            add ebx, eax
            fld dword ptr [ecx + ebx * 4]
            lea ebx, [eax + eax * 2]
            add ebx, esi
            fsub dword ptr [ecx + ebx * 4]
            fmul st(0), st(1)
            fstp dword ptr [edi]
            lea ebx, [edx + edx * 2]
            add ebx, eax
            fld dword ptr [ecx + ebx * 4]
            lea ebx, [eax + eax * 2]
            add ebx, edx
            fadd dword ptr [ecx + ebx * 4]
            fmul st(0), st(1)
            fstp dword ptr [edi + eax * 4 + 4]
            lea ebx, [edx + edx * 2]
            add ebx, esi
            fld dword ptr [ecx + ebx * 4]
            lea ebx, [esi + esi * 2]
            add ebx, edx
            fadd dword ptr [ecx + ebx * 4]
            fmul st(0), st(1)
            fstp dword ptr [edi + esi * 4 + 4]
            fstp st(0)
        MatrixQuatDone:
        }
        return GR_OK;
    }

    GENRESULT DACOM_API QuaternionToMatrix(Matrix& Dst, const Quaternion& Quat) override
    {
        // LAB_06f723c0: retain S/Xs and selected products on x87; the
        // asymmetric float scratch stores are part of the arithmetic contract.
        float Scratch[5];
        float Zs;
        const float One = 1.0f;
        const float Two = 2.0f;
        __asm
        {
            mov eax, Quat
            lea edx, Scratch
            fld dword ptr [eax + 0ch]
            fld dword ptr [eax + 8]
            fld dword ptr [eax + 4]
            fld dword ptr [eax]
            fld st(0)
            fmul st(0), st(1)
            fld st(2)
            fmul st(0), st(3)
            faddp st(1), st(0)
            fld st(3)
            fmul st(0), st(4)
            faddp st(1), st(0)
            fld st(4)
            fmul st(0), st(5)
            faddp st(1), st(0)
            fdivr Two
            fstp st(4)
            fstp st(0)
            fstp st(0)
            fstp st(0)
            fld st(0)
            fmul dword ptr [eax + 4]
            fld st(1)
            fmul dword ptr [eax + 8]
            fstp dword ptr [edx]
            fxch st(1)
            fmul dword ptr [eax + 0ch]
            fstp Zs
            fld st(0)
            fmul dword ptr [eax]
            fstp dword ptr [edx + 0ch]
            fld dword ptr [edx]
            fmul dword ptr [eax]
            fstp dword ptr [edx + 8]
            fld Zs
            fmul dword ptr [eax]
            fstp dword ptr [edx + 4]
            fmul dword ptr [eax + 4]
            fld dword ptr [edx]
            fmul dword ptr [eax + 4]
            fld Zs
            fmul dword ptr [eax + 4]
            fld dword ptr [edx]
            fmul dword ptr [eax + 8]
            fstp dword ptr [edx + 10h]
            fld Zs
            fmul dword ptr [eax + 8]
            fstp dword ptr [edx]
            fld Zs
            fmul dword ptr [eax + 0ch]
            fld dword ptr [edx + 10h]
            // Every quaternion input read precedes the first output write.
            mov eax, Dst
            fadd st(0), st(1)
            fsubr One
            fstp dword ptr [eax]
            fld st(2)
            fsub dword ptr [edx + 4]
            fstp dword ptr [eax + 4]
            fld st(1)
            fadd dword ptr [edx + 8]
            fstp dword ptr [eax + 8]
            fxch st(2)
            fadd dword ptr [edx + 4]
            fstp dword ptr [eax + 0ch]
            fxch st(1)
            fadd st(0), st(2)
            fsubr One
            fstp dword ptr [eax + 10h]
            fld dword ptr [edx]
            fsub dword ptr [edx + 0ch]
            fstp dword ptr [eax + 14h]
            fsub dword ptr [edx + 8]
            fstp dword ptr [eax + 18h]
            fld dword ptr [edx]
            fadd dword ptr [edx + 0ch]
            fstp dword ptr [eax + 1ch]
            fld dword ptr [edx + 10h]
            fadd st(0), st(1)
            fsubr One
            fstp dword ptr [eax + 20h]
            fstp st(0)
        }
        return GR_OK;
    }

    GENRESULT DACOM_API QuatSlerp(Quaternion& Dst, const Quaternion& A, const Quaternion& B, float T) override
    {
        return QuatSlerpValue(Dst, A, B, &T);
    }

    GENRESULT DACOM_API QuatSlerpValue(Quaternion& Dst, const Quaternion& A, const Quaternion& B, const float* T) override
    {
        // LAB_06f724d0: normalized linear interpolation, literal x87 terms,
        // component store order and NaN hemisphere selection. Inputs may alias Dst.
        static const float Zero = 0.0f;
        static const float One = 1.0f;
        __asm
        {
            mov esi, T
            mov ecx, A
            mov edx, B
            fld dword ptr [ecx + 8]
            fmul dword ptr [edx + 8]
            fld dword ptr [ecx + 4]
            fmul dword ptr [edx + 4]
            faddp st(1), st(0)
            fld dword ptr [edx + 0ch]
            fmul dword ptr [ecx + 0ch]
            faddp st(1), st(0)
            fld dword ptr [ecx]
            fmul dword ptr [edx]
            faddp st(1), st(0)
            fcomp Zero
            fnstsw ax
            test ah, 05h
            mov eax, Dst
            // Less=01h (odd parity) falls through. Nonnegative=00h and
            // unordered=05h (even parity) take the forward branch.
            jp QuatForwardHemisphere
            fld dword ptr [ecx]
            fadd dword ptr [edx]
            fmul dword ptr [esi]
            fsubr dword ptr [ecx]
            fstp dword ptr [eax]
            fld dword ptr [ecx + 4]
            fadd dword ptr [edx + 4]
            fmul dword ptr [esi]
            fsubr dword ptr [ecx + 4]
            fstp dword ptr [eax + 4]
            fld dword ptr [ecx + 8]
            fadd dword ptr [edx + 8]
            fmul dword ptr [esi]
            fsubr dword ptr [ecx + 8]
            fstp dword ptr [eax + 8]
            fld dword ptr [edx + 0ch]
            fadd dword ptr [ecx + 0ch]
            fmul dword ptr [esi]
            fsubr dword ptr [ecx + 0ch]
            jmp QuatNormalize
        QuatForwardHemisphere:
            fld dword ptr [edx]
            fsub dword ptr [ecx]
            fmul dword ptr [esi]
            fadd dword ptr [ecx]
            fstp dword ptr [eax]
            fld dword ptr [edx + 4]
            fsub dword ptr [ecx + 4]
            fmul dword ptr [esi]
            fadd dword ptr [ecx + 4]
            fstp dword ptr [eax + 4]
            fld dword ptr [edx + 8]
            fsub dword ptr [ecx + 8]
            fmul dword ptr [esi]
            fadd dword ptr [ecx + 8]
            fstp dword ptr [eax + 8]
            fld dword ptr [edx + 0ch]
            fsub dword ptr [ecx + 0ch]
            fmul dword ptr [esi]
            fadd dword ptr [ecx + 0ch]
        QuatNormalize:
            fstp dword ptr [eax + 0ch]
            fld dword ptr [eax + 0ch]
            fld dword ptr [eax + 8]
            fld dword ptr [eax + 4]
            fld dword ptr [eax]
            fld st(0)
            fmul st(0), st(1)
            fld st(2)
            fmul st(0), st(3)
            faddp st(1), st(0)
            fld st(3)
            fmul st(0), st(4)
            faddp st(1), st(0)
            fld st(4)
            fmul st(0), st(5)
            faddp st(1), st(0)
            fsqrt
            fstp st(4)
            fstp st(0)
            fstp st(0)
            fstp st(0)
            fdivr One
            fld st(0)
            fmul dword ptr [eax]
            fstp dword ptr [eax]
            fld st(0)
            fmul dword ptr [eax + 4]
            fstp dword ptr [eax + 4]
            fld st(0)
            fmul dword ptr [eax + 8]
            fstp dword ptr [eax + 8]
            fmul dword ptr [eax + 0ch]
            fstp dword ptr [eax + 0ch]
        }
        return GR_OK;
    }

    GENRESULT DACOM_API GeneralInverse(Transform& Dst, float& W, const Transform& Source) override
    {
        const float Determinant = Source.Rotation.E[0][0] * (Source.Rotation.E[1][1] * Source.Rotation.E[2][2] -
                                                             Source.Rotation.E[2][1] * Source.Rotation.E[1][2]) -
                                  Source.Rotation.E[0][1] * (Source.Rotation.E[1][0] * Source.Rotation.E[2][2] -
                                                             Source.Rotation.E[2][0] * Source.Rotation.E[1][2]) +
                                  Source.Rotation.E[0][2] * (Source.Rotation.E[1][0] * Source.Rotation.E[2][1] -
                                                             Source.Rotation.E[2][0] * Source.Rotation.E[1][1]);

        GENRESULT Result = GR_OK;
        float InvDet = 1.0f;
        if (std::fabs(Determinant) > G_MinDet)
            InvDet = 1.0f / Determinant;
        else
            Result = GR_INVALID_PARMS;

        WriteAdjugate(Dst.Rotation, Source.Rotation, InvDet);

        const Matrix& M = Source.Rotation;
        const Vector& T = Source.Translation;
        Dst.Translation.x = -(M.E[0][1] * M.E[1][2] * T.z + M.E[1][1] * M.E[2][2] * T.x + M.E[2][1] * M.E[0][2] * T.y -
                              M.E[0][1] * M.E[2][2] * T.y - M.E[1][1] * M.E[0][2] * T.z - M.E[2][1] * M.E[1][2] * T.x) *
                            InvDet;
        Dst.Translation.y = (M.E[0][0] * M.E[1][2] * T.z + M.E[1][0] * M.E[2][2] * T.x + M.E[2][0] * M.E[0][2] * T.y -
                             M.E[0][0] * M.E[2][2] * T.y - M.E[1][0] * M.E[0][2] * T.z - M.E[2][0] * M.E[1][2] * T.x) *
                            InvDet;
        Dst.Translation.z = -(M.E[0][0] * M.E[1][1] * T.z + M.E[1][0] * M.E[2][1] * T.x + M.E[2][0] * M.E[0][1] * T.y -
                              M.E[0][0] * M.E[2][1] * T.y - M.E[1][0] * M.E[0][1] * T.z - M.E[2][0] * M.E[1][1] * T.x) *
                            InvDet;

        W = (M.E[0][0] * M.E[1][1] * M.E[2][2] + M.E[1][0] * M.E[2][1] * M.E[0][2] + M.E[2][0] * M.E[0][1] * M.E[1][2] -
             M.E[2][0] * M.E[1][1] * M.E[0][2] - M.E[0][0] * M.E[2][1] * M.E[1][2] - M.E[1][0] * M.E[0][1] * M.E[2][2]) *
            InvDet;
        return Result;
    }

    GENRESULT DACOM_API InvSqrt(float& Dst, float X) override
    {
        Dst = TableInvSqrt(X);
        return GR_OK;
    }

    GENRESULT DACOM_API Sqrt(float& Dst, float X) override
    {
        Dst = TableInvSqrt(X) * X;
        return GR_OK;
    }
};

static_assert(sizeof(FMathEngine) == 8, "retail DllMain allocates operator_new(8) — two vfptrs");

FMathEngine* G_Engine = nullptr;

BOOL WINAPI DllMain(HINSTANCE Module, DWORD Reason, LPVOID /*Reserved*/)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Module);
        // FUN_06f71090: operator new(8); on null the engine stays null, attach still
        // succeeds and the InvSqrt table is not built.
        void* Memory = ::operator new(8, std::nothrow);
        if (Memory == nullptr)
        {
            G_Engine = nullptr;
            return TRUE;
        }
        G_Engine = new (Memory) FMathEngine();
        BuildInvSqrtTable();
        // A null DACOM_Acquire skips registration silently (no log in retail).
        ICOManager* Dacom = DACOM_Acquire();
        if (Dacom != nullptr)
        {
            Dacom->RegisterComponent(static_cast<IComponentFactory*>(G_Engine), G_ComponentName,
                                     reinterpret_cast<void*>(G_RegisterPriority));
        }
    }
    return TRUE;
}
