// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "MovieWindow.h"
#include "MovieCca.h"
#include "MovieD4.h"
#include "MovieField.h"
#include "MovieFieldHigh.h"
#include "MovieGraph.h"
#include "MovieWide.h"

#include <intrin.h>

// Karatsuba helpers for the windowed multiply (0x06aca920 / 0x06acb0a0). A
// block's operand of 2*Half limbs is Low (limbs 0..Half) and High (Half..2*Half).

// Dest = Left + Right + CarryIn over Count limbs, ascending; returns the carry out.
static unsigned int AddLimbRun(unsigned int* Dest, const unsigned int* Left, const unsigned int* Right,
                               unsigned int Count, unsigned int CarryIn)
{
    unsigned int Carry = CarryIn;
    for (unsigned int Index = 0u; Index < Count; ++Index)
    {
        const unsigned long long Sum = (unsigned long long)Left[Index] + Right[Index] + Carry;
        Dest[Index] = (unsigned int)Sum;
        Carry = (unsigned int)(Sum >> 32);
    }
    return Carry;
}

// Dest = Left - Right over Count limbs, ascending; returns the borrow out.
static unsigned int SubtractLimbRun(unsigned int* Dest, const unsigned int* Left, const unsigned int* Right,
                                    unsigned int Count)
{
    unsigned int Borrow = 0u;
    for (unsigned int Index = 0u; Index < Count; ++Index)
    {
        const unsigned long long Need = (unsigned long long)Right[Index] + Borrow;
        const unsigned int Minuend = Left[Index];
        Dest[Index] = (unsigned int)((unsigned long long)Minuend - Need);
        Borrow = ((unsigned long long)Minuend < Need) ? 1u : 0u;
    }
    return Borrow;
}

// Dest = Source + Carry over Count limbs; returns the carry out.
static unsigned int CopyLimbRunWithCarry(unsigned int* Dest, const unsigned int* Source, unsigned int Count,
                                         unsigned int Carry)
{
    for (unsigned int Index = 0u; Index < Count; ++Index)
    {
        const unsigned int Value = Source[Index] + Carry;
        Carry = (Carry != 0u && Value == 0u) ? 1u : 0u;
        Dest[Index] = Value;
    }
    return Carry;
}

// Ripples a carry (or a borrow) into Limbs through at most Reach limbs; what is
// left after the last one is dropped.
static void RippleCarry(unsigned int* Limbs, unsigned int Carry, unsigned int Reach)
{
    for (unsigned int Index = 0u; Carry != 0u && Index < Reach; ++Index)
    {
        Limbs[Index] = Limbs[Index] + 1u;
        Carry = (Limbs[Index] == 0u) ? 1u : 0u;
    }
}

static void RippleBorrow(unsigned int* Limbs, unsigned int Borrow, unsigned int Reach)
{
    for (unsigned int Index = 0u; Borrow != 0u && Index < Reach; ++Index)
    {
        Borrow = (Limbs[Index] == 0u) ? 1u : 0u;
        Limbs[Index] = Limbs[Index] - 1u;
    }
}

// DAT_06add4cc. Index is Dispatch[2]. Empty slots are the retail null dwords.
// Slots 1..4, 6, 8 and 12 are Multiply*LimbPairs.
typedef void (__stdcall *FWidthProduct)(unsigned int* Left, unsigned int* Right, unsigned int* Product, unsigned int Scale);

static FWidthProduct const GWidthProducts[13] =
{
    0,
    MultiplyOneLimbPairs,
    MultiplyTwoLimbPairs,
    MultiplyThreeLimbPairs,
    MultiplyFourLimbPairs,
    0,
    MultiplySixLimbPairs,
    0,
    MultiplyEightLimbPairs,
    0,
    0,
    0,
    MultiplyTwelveLimbPairs
};

// DAT_06ada800. 3^0 through 3^15. The dword before it (0x06ada7fc) is not read
// by the loops below: they start at index Halvings-1 and walk down to index 0.
static unsigned int const GPowersOfThree[16] =
{
    0x00000001u, 0x00000003u, 0x00000009u, 0x0000001bu,
    0x00000051u, 0x000000f3u, 0x000002d9u, 0x0000088bu,
    0x000019a1u, 0x00004ce3u, 0x0000e6a9u, 0x0002b3fbu,
    0x00081bf1u, 0x001857d3u, 0x0048fb79u, 0x00daf26bu
};

static void CopyOrZeroBytes(unsigned int* Dest, unsigned int* Source, unsigned int ByteCount, int FillZero)
{
    unsigned int Words = ByteCount >> 2;
    unsigned int Tail = ByteCount & 3u;
    unsigned int Index;
    unsigned char* DestBytes = (unsigned char*)(Dest + Words);
    unsigned int TailIndex;
    if (FillZero == 0)
    {
        unsigned char* SourceBytes = (unsigned char*)(Source + Words);
        for (Index = 0u; Index < Words; Index = Index + 1u)
        {
            Dest[Index] = Source[Index];
        }
        for (TailIndex = 0u; TailIndex < Tail; TailIndex = TailIndex + 1u)
        {
            DestBytes[TailIndex] = SourceBytes[TailIndex];
        }
    }
    else
    {
        for (Index = 0u; Index < Words; Index = Index + 1u)
        {
            Dest[Index] = 0u;
        }
        for (TailIndex = 0u; TailIndex < Tail; TailIndex = TailIndex + 1u)
        {
            DestBytes[TailIndex] = 0;
        }
    }
}

// 0x06ac9a30. Exponent is one limb. Below 2, Dest is replaced by the field one
// (Context+0x451) or by Base, and the return is 0. Otherwise the bits below the
// top pair walk a 2-bit window. The return is squares plus multiplies, including
// when Dest is null and the limb calls are skipped.
int __stdcall Movie_ExponentiateLimb(unsigned int* Base, unsigned int Exponent, unsigned int* Dest, unsigned int* Context)
{
    unsigned int MultiplyCount = 0u;
    unsigned int SquareCount = 0u;
    unsigned int ByteCount;
    unsigned int* Source;
    if (Exponent <= 1u)
    {
        if (Dest == 0)
        {
            return 0;
        }
        Source = Context + 0x451;
        if (Exponent != 0u)
        {
            Source = Base;
        }
        ByteCount = Context[0x40] << 2;
        CopyOrZeroBytes(Dest, Source, ByteCount, 0);
        return 0;
    }

    unsigned long BitIndex = 0;
    _BitScanReverse(&BitIndex, Exponent);
    unsigned int Shift = BitIndex + 1u - 2u;
    unsigned int Top = Exponent >> Shift;
    unsigned int TopIsThree = (Top == 3u) ? 1u : 0u;
    unsigned int* Work = (unsigned int*)Movie_AllocateBlock(0x2e94u);
    if (Work == 0)
    {
        if (Dest != 0)
        {
            ByteCount = Context[0x40] << 2;
            CopyOrZeroBytes(Dest, 0, ByteCount, 1);
        }
        return 0;
    }

    unsigned char* Raw = (unsigned char*)Work;
    unsigned int* Low = Work;
    unsigned int* LowTail = (unsigned int*)(Raw + 0x514);
    unsigned int* Second = (unsigned int*)(Raw + 0x53c);
    unsigned int* SecondTail = (unsigned int*)(Raw + 0xa50);
    unsigned int* Temp = (unsigned int*)(Raw + 0xa78);
    unsigned int* Scratch = (unsigned int*)(Raw + 0xb78);
    unsigned int* Dispatch = (unsigned int*)Context[0x42];
    unsigned int LimbCount = Context[0x40];
    if (Dest != 0)
    {
        Movie_EvaluateWindow(Base, LimbCount, Low, (unsigned char*)LowTail, Dispatch);
        Movie_MultiplyTermsModulo(Low, LowTail, Low, LowTail, Dest, Context, Scratch);
    }
    SquareCount = 1u;
    if (TopIsThree != 0u)
    {
        if (Dest != 0)
        {
            Movie_MultiplyFactorModulo(Dest, Low, LowTail, Dest, Context, Scratch);
        }
        MultiplyCount = 1u;
    }
    if (Shift != 0u)
    {
        unsigned int Window = 0u;
        if (Dest != 0)
        {
            Movie_EvaluateWindow(Dest, LimbCount, Second, (unsigned char*)SecondTail, Dispatch);
        }
        while (Shift != 0u)
        {
            Shift = Shift - 1u;
            unsigned int Bit = (Exponent >> Shift) & 1u;
            Window = Bit + Window * 2u;
            // Above 3 the switch is skipped and the shift is tested again.
            if (Window > 3u)
            {
            }
            else if (Window <= 1u)
            {
                if (Dest != 0)
                {
                    Movie_MultiplyModulo(Dest, Dest, Dest, Context, Scratch);
                }
                SquareCount = SquareCount + 1u;
            }
            else if (Window == 2u)
            {
                MultiplyCount = MultiplyCount + 1u;
                SquareCount = SquareCount + 1u;
                Window = 0u;
                if (Dest != 0)
                {
                    Movie_MultiplyFactorModulo(Dest, Low, LowTail, Dest, Context, Scratch);
                    Movie_MultiplyModulo(Dest, Dest, Dest, Context, Scratch);
                }
            }
            else
            {
                if (TopIsThree == 0u)
                {
                    if (Dest != 0)
                    {
                        Movie_MultiplyTermsModulo(Second, SecondTail, Low, LowTail, Temp, Context, Scratch);
                        Movie_EvaluateWindow(Temp, LimbCount, Second, (unsigned char*)SecondTail, Dispatch);
                    }
                    TopIsThree = 1u;
                    MultiplyCount = MultiplyCount + 1u;
                }
                if (Dest != 0)
                {
                    Movie_MultiplyModulo(Dest, Dest, Dest, Context, Scratch);
                    Movie_MultiplyFactorModulo(Dest, Second, SecondTail, Dest, Context, Scratch);
                }
                MultiplyCount = MultiplyCount + 1u;
                SquareCount = SquareCount + 1u;
                Window = 0u;
            }
        }
        if (Window != 0u)
        {
            if (Dest != 0)
            {
                Movie_MultiplyFactorModulo(Dest, Low, LowTail, Dest, Context, Scratch);
            }
            MultiplyCount = MultiplyCount + 1u;
        }
    }
    Movie_FreeBlock(Work);
    return (int)(MultiplyCount + SquareCount);
}

// 0x06aca450.
void __stdcall Movie_FillByteBuffer(unsigned char* Buffer, unsigned int ByteCount)
{
    GenerateRngBytes(0, 0, Buffer, ByteCount);
}

// 0x06aca890. Dispatch[0] is the width, [1] the halving count, [2] the width-product
// index. The first call uses 3^Halvings. Each later step folds Product with one
// power, walking Dispatch[2] upward and the power table downward.
void __stdcall Movie_MultiplyWindows(unsigned int* Left, unsigned int* LeftTail, unsigned int* Right, unsigned int* RightTail, unsigned int* Product, unsigned int* Dispatch)
{
    unsigned int Halvings = Dispatch[1];
    unsigned int Width = Dispatch[0];
    unsigned int Selector = Dispatch[2];
    GWidthProducts[Selector](Left, Right, Product, GPowersOfThree[Halvings]);
    if (Halvings == 0u)
    {
        return;
    }
    unsigned int Shift = Halvings;
    unsigned int* Factor = Dispatch + 2;
    unsigned int const* Power = GPowersOfThree + (Halvings - 1u);
    while (Shift != 0u)
    {
        unsigned int Scale = *Power;
        unsigned int Half = Scale >> 1;
        Movie_FoldPartialProducts(
            Product,
            Width >> (Shift & 31u),
            Scale,
            (*Factor) << 1,
            (unsigned char*)LeftTail + Half,
            (unsigned char*)RightTail + Half);
        Power = Power - 1;
        Factor = Factor + 1;
        Shift = Shift - 1u;
    }
}

// 0x06acae10. Halvings == 0 calls the width product with scale 1 and returns.
// Otherwise the slot is Dispatch[Halvings+1]. Mark bytes choose add versus subtract
// at the end: equal selects 0x06ac8910, different selects 0x06ac8500.
void __stdcall Movie_MultiplyWindowCross(unsigned int* Left, unsigned char* LeftMark, unsigned int* Right, unsigned char* RightMark, unsigned int* Middle, unsigned int* Product, unsigned int* Dispatch)
{
    unsigned int Selector = Dispatch[2];
    unsigned int Halvings = Dispatch[1];
    FWidthProduct ProductFn = GWidthProducts[Selector];
    if (Halvings == 0u)
    {
        ProductFn(Left, Right, Product, 1u);
        return;
    }

    unsigned int Slot = Dispatch[Halvings + 1u];
    unsigned int Width = Dispatch[0];
    unsigned int Mask = (LeftMark[0] == RightMark[0]) ? 0u : 0xffffffffu;
    ProductFn(Left + Slot, Right + Slot, Product + (Slot * 2u), GPowersOfThree[Halvings - 1u] << 1);
    if (Halvings != 1u)
    {
        unsigned int Shift = Halvings;
        unsigned int LeftTrips = Halvings - 1u;
        unsigned int const* Power = GPowersOfThree + (Halvings - 2u);
        unsigned int* Factor = Dispatch + 2;
        while (LeftTrips != 0u)
        {
            unsigned int Current = Power[0];
            unsigned int Next = Power[1];
            unsigned int Sum = (Next >> 1) + Current;
            Movie_FoldPartialProducts(
                Product + (Slot * 2u),
                Width >> (Shift & 31u),
                Current * 2u,
                (*Factor) << 1,
                LeftMark + Sum,
                RightMark + Sum);
            Power = Power - 1;
            Factor = Factor + 1;
            Shift = Shift - 1u;
            LeftTrips = LeftTrips - 1u;
        }
    }

    unsigned int Half = Width >> 1;
    unsigned int CarryOut = 0u;
    unsigned int Adjust = 2u - Mask;
    unsigned int Index;
    if (Half != 0u)
    {
        for (Index = 0u; Index < Half; Index = Index + 1u)
        {
            unsigned int FromMiddle = Middle[Index];
            unsigned int FromSlot16 = Product[(Slot * 4u) + Index];
            unsigned int FromSlot8 = Product[(Slot * 2u) + Index];
            unsigned int FromHalf = Product[Half + Index];
            // Four ADD / ADC 0 pairs: the low dword is the sum, the high one
            // counts the carries and is the next limb's Adjust.
            const unsigned long long Total = (unsigned long long)(FromSlot16 ^ Mask) + FromHalf +
                                             (unsigned int)~FromSlot8 + (unsigned int)~FromMiddle + Adjust;
            unsigned int Mixed = (unsigned int)Total;
            Adjust = (unsigned int)(Total >> 32);
            Mixed = Mixed + CarryOut;
            unsigned int Sum = FromSlot8 + Mixed;
            Product[Index] = FromMiddle;
            Product[Half + Index] = FromMiddle;
            Product[(Half * 2u) + Index] = Sum;
            if (Mixed != 0u)
            {
                CarryOut = (Sum < Mixed) ? 1u : 0u;
            }
        }
    }

    unsigned int* AtHalf = Product + Half;
    unsigned int* AtTwice = Product + (Half * 2u);
    unsigned int* AtTriple = Product + (Half * 3u);
    unsigned int* AtPair = Product + (Half + Slot * 2u);
    if (Half != 0u)
    {
        if (AtPair == AtTriple)
        {
            unsigned int Limb = AtTriple[0];
            unsigned int Limit = 0xffffffffu - CarryOut;
            if (Limb > Limit)
            {
                AddLimbs(AtPair, (int)Half, &CarryOut, 1, (int)AtTriple);
            }
            else
            {
                AtTriple[0] = Limb + CarryOut;
            }
        }
        else
        {
            AddLimbs(AtPair, (int)Half, &CarryOut, 1, (int)AtTriple);
        }
    }
    AddLimbs(AtHalf, (int)(Half * 3u), AtTwice, (int)(Half * 2u), (int)AtHalf);
    if (Mask == 0u)
    {
        Movie_SubtractLimbsWide(AtHalf, (int)(Half * 3u), Product + (Slot * 4u), (int)(Half * 2u), (int)AtHalf);
    }
    else
    {
        AddLimbs(AtHalf, (int)(Half * 3u), Product + (Slot * 4u), (int)(Half * 2u), (int)AtHalf);
    }
}

// 0x06acb290. Count above Dispatch[0] sets status 0x37 and returns.
// Otherwise the source is copied, the gap to the width is zeroed, and each
// halving calls 0x06acb0a0. DestTail is a byte address: the sign base adds Power>>1.
void __stdcall Movie_EvaluateWindow(unsigned int* Source, unsigned int Count, unsigned int* Dest, unsigned char* DestTail, unsigned int* Dispatch)
{
    unsigned int Depth = Dispatch[1];
    unsigned int Width = Dispatch[0];
    unsigned int WordIndex = Dispatch[Depth + 2u];
    if (Count > Width)
    {
        GMovieGraphCode = 0x37;
        return;
    }

    unsigned int CopyBytes = Count << 2;
    CopyOrZeroBytes(Dest, Source, CopyBytes, 0);
    unsigned int Gap = Width - Count;
    unsigned int GapBytes = Gap << 2;
    CopyOrZeroBytes(Dest + Count, 0, GapBytes, 1);
    if (Depth == 0u)
    {
        return;
    }

    unsigned int Level = 0u;
    unsigned int* Factor = Dispatch + Depth + 1u;
    while (Level != Depth)
    {
        unsigned int Power = GPowersOfThree[Level];
        unsigned int Scale = *Factor;
        Movie_SubtractHalves(
            Dest + WordIndex,
            Width >> ((Level + 1u) & 31u),
            Power,
            Scale,
            DestTail + (Power >> 1));
        Level = Level + 1u;
        Factor = Factor - 1;
    }
}

// 0x06aca920. Karatsuba recombination, one block per group, blocks ascending
// 3*SlotWords limbs apart. A block holds three products SlotWords limbs apart:
// P0 = Low*Low', P1 = High*High', P2 = |Low-High|*|Low'-High'|, each 2*Half
// limbs. It is rewritten in place to the 4*Half-limb product
//   P0 + (P0 + P1 -/+ P2) * B^Half + P1 * B^(2*Half)
// with P2 subtracted when the group's two sign bytes are equal, added otherwise.
// The carries out of the top quarter are not all kept: the carry out of
// P0.high + P1 is dropped, and the two carries rippled into the top quarter
// stop after Half/2 limbs in the paired loop, after Half limbs in the unrolled
// Half 3 / 4 bodies retail uses when SlotWords is exactly 2*Half. The paired
// loop does nothing for an odd Half.
void __stdcall Movie_FoldPartialProducts(unsigned int* Products, unsigned int Half, unsigned int Groups,
                                         unsigned int SlotWords, unsigned char* SignLeft,
                                         unsigned char* SignRight)
{
    const bool bUnrolled = (SlotWords - Half == Half) && (Half == 4u || Half == 3u);
    if (!bUnrolled && (Half & 1u) != 0u)
        return;
    const unsigned int Reach = bUnrolled ? Half : (Half >> 1);

    for (unsigned int Group = 0u; Group < Groups; ++Group)
    {
        unsigned int* Block = Products + 3u * SlotWords * Group;
        unsigned int* Second = Block + Half;
        unsigned int* Third = Block + 2u * Half;
        unsigned int* Top = Block + 3u * Half;
        const unsigned int* Square1 = Block + SlotWords;
        const unsigned int* Cross = Block + 2u * SlotWords;

        // Y = P0.high + P1 into the upper half.
        const unsigned int CarryY = AddLimbRun(Third, Second, Square1, Half, 0u);
        CopyLimbRunWithCarry(Top, Square1 + Half, Half, CarryY);
        // Second quarter: P0.low + Y.low; third: Y.low + Y.high.
        const unsigned int CarryMid = AddLimbRun(Second, Block, Third, Half, 0u);
        const unsigned int CarryHigh = AddLimbRun(Third, Third, Top, Half, CarryMid);
        RippleCarry(Top, CarryHigh, Reach);

        if (SignLeft[Group] == SignRight[Group])
            RippleBorrow(Top, SubtractLimbRun(Second, Second, Cross, 2u * Half), Reach);
        else
            RippleCarry(Top, AddLimbRun(Second, Second, Cross, 2u * Half, 0u), Reach);
    }
}

// 0x06acb0a0. Karatsuba split, one block per trip, blocks walked downward from
// Top, 3*Stride limbs each. A block's 2*Half-limb operand becomes three slots
// Stride limbs apart: Low stays, High moves up to the second slot (top limb
// first), and |Low - High| goes to the third. Signs[trip], counted from the
// lowest block, is 0xff when Low < High, else 0.
void __stdcall Movie_SubtractHalves(unsigned int* Top, unsigned int Half, unsigned int Blocks,
                                    unsigned int Stride, unsigned char* Signs)
{
    unsigned int* Block = Top;
    for (unsigned int Remaining = Blocks; Remaining != 0u; --Remaining)
    {
        Block = Block - 3u * Stride;
        const unsigned int* Low = Block;
        const unsigned int* High = Block + Half;

        bool bLowBelowHigh = false;
        for (unsigned int Index = Half; Index != 0u; --Index)
        {
            if (Low[Index - 1u] != High[Index - 1u])
            {
                bLowBelowHigh = Low[Index - 1u] < High[Index - 1u];
                break;
            }
        }
        Signs[Remaining - 1u] = bLowBelowHigh ? 0xffu : 0x00u;
        if (bLowBelowHigh)
            SubtractLimbRun(Block + 2u * Stride, High, Low, Half);
        else
            SubtractLimbRun(Block + 2u * Stride, Low, High, Half);

        if (Stride != Half)
        {
            for (unsigned int Index = Half; Index != 0u; --Index)
                Block[Stride + Index - 1u] = High[Index - 1u];
        }
    }
}
