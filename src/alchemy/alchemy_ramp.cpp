// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "alchemy_ramp.h"

#include "alchemy_common.h"
#include "alchemy_appearances.h"
#include "alchemy_math.h"

#include <intrin.h>
#include <xmmintrin.h>

#include <cmath>
#include <cstring>
#include <new>

// alchemy.dll -- the keyed ramps. The class layouts and the shared key container are
// described in alchemy_ramp.h, the interface slots in alchemy_types.h ("Keyed ramps").
namespace AlchemyImpl
{
    namespace
    {
    }

    // ---- Shared helpers ---------------------------------------------------------------

    // MSVCRT ftol: truncation toward zero of the x87 value, low 32 bits of the 64-bit
    // result (a NaN or an out-of-range value gives 0x80000000'00000000, low half 0).
    // The value arrives unrounded from the x87 stack; the process runs at 53-bit
    // precision control (exe DAT_00610848 = 0x35), so the port passes a double.
    static long RampFtol(double Value)
    {
        return static_cast<long>(static_cast<long long>(Value));
    }

    // SetMode's CPUID test (06242930 / 06241c0a): EDX bit 25, SSE. Executed on every
    // SetMode call, as retail does.
    static bool RampCpuHasSse()
    {
        int Registers[4] = {0, 0, 0, 0};
        __cpuid(Registers, 1);
        return (Registers[3] & 0x2000000) != 0;
    }

    // DAT_0624bac4 = 1.0f, DAT_0624bb14 = 3.0f, DAT_0624babc = 255.0f,
    // DAT_0624bc98 = 0x3b808081 (1/255 rounded up), DAT_0624bc38 = 0.0f.
    static const float kRampOne = 1.0f;
    static const float kRampThree = 3.0f;
    static const float kRamp255 = 255.0f;
    static const float kRampInv255 = 0.003921568859368563f;

    // ---- FxRampSingle interpolators (cdecl, (A, B, T) -> ST0) ----------------------------
    // x87 bodies 06242970..06242a70; the SSE table swaps only mode 4 for 06242ac0. Every
    // x87 body keeps its intermediates on the FPU stack and returns ST0 unrounded, so the
    // port computes in double and returns double.

    // [0] 06242970: step.
    static double RampSingleStep(float A, float /*B*/, float /*T*/)
    {
        return A;
    }

    // [1] 06242980.
    static double RampSingleLinear(float A, float B, float T)
    {
        return (static_cast<double>(B) - A) * T + A;
    }

    // [2] 062429a0.
    static double RampSingleEaseIn(float A, float B, float T)
    {
        return (static_cast<double>(B) - A) * (static_cast<double>(T) * T) + A;
    }

    // [3] 062429c0.
    static double RampSingleEaseOut(float A, float B, float T)
    {
        const double OneMinusT = static_cast<double>(kRampOne) - T;
        const double W = static_cast<double>(kRampOne) - OneMinusT * OneMinusT;
        return (static_cast<double>(B) - A) * W + A;
    }

    // [4] 062429f0.
    static double RampSingleSmooth(float A, float B, float T)
    {
        const double W = (static_cast<double>(kRampThree) - (static_cast<double>(T) + T)) * T * T;
        return (static_cast<double>(B) - A) * W + A;
    }

    // [5] 06242a20: equal and unordered take the ease-out body.
    static double RampSingleEaseByRise(float A, float B, float T)
    {
        if (A < B)
            return RampSingleEaseIn(A, B, T);
        return RampSingleEaseOut(A, B, T);
    }

    // [6] 06242a70: the same with the operands swapped.
    static double RampSingleEaseByFall(float A, float B, float T)
    {
        if (B < A)
            return RampSingleEaseIn(A, B, T);
        return RampSingleEaseOut(A, B, T);
    }

    // SSE [4] 06242ac0 (DAT_0624fa84 = -2.0f, DAT_0624fa88 = 3.0f): all single precision,
    // so this one returns a float-rounded value.
    static double RampSingleSmoothSse(float A, float B, float T)
    {
        const float W = (T * -2.0f + 3.0f) * T * T;
        const float Result = (B - A) * W + A;
        return Result;
    }

    // SetMode 06242880 fills two seven-entry tables once (guard bits 1 and 2 of
    // DAT_06257c1c): x87 at DAT_06257c20, SSE at DAT_06257c3c, and stores
    // table[Mode] into +0x24 with no bound on Mode. The tables are adjacent in retail
    // .data, so they are one array here: x87 entries 0..6, SSE entries 7..13. A Mode above
    // 6 therefore reads on into the next table exactly as retail does (x87 mode 7 lands on
    // the SSE step entry); retail has no check and neither does the port.
    typedef double (*FRampSingleInterpolator)(float A, float B, float T);
    static const FRampSingleInterpolator G_RampSingleInterpolators[14] = {
        RampSingleStep,     RampSingleLinear, RampSingleEaseIn,     RampSingleEaseOut,
        RampSingleSmooth,   RampSingleEaseByRise, RampSingleEaseByFall,
        RampSingleStep,     RampSingleLinear, RampSingleEaseIn,     RampSingleEaseOut,
        RampSingleSmoothSse, RampSingleEaseByRise, RampSingleEaseByFall,
    };

    // ---- FxRampColor interpolators (cdecl, (A, B, T) -> 0x00RRGGBB) ----------------------
    // x87 bodies 06241cc0..06241fc0; SSE table 06242070..06242360 repeats them byte for
    // byte except mode 4 (06242230, MMX).

    // The shared channel blend of every x87 body: W = ftol(w * 255), then per channel
    // ((b - a) * W >> 8) + a with an arithmetic shift, masked back into R (<<16), G (<<8)
    // and B; the top byte comes out 0. The weight never leaves the x87 stack, hence double.
    static unsigned long RampColorBlend(unsigned long A, unsigned long B, double Weight)
    {
        const long W = RampFtol(Weight * kRamp255);
        const long RedA = static_cast<long>((A >> 16) & 0xFF);
        const long GreenA = static_cast<long>((A >> 8) & 0xFF);
        const long BlueA = static_cast<long>(A & 0xFF);
        const long RedB = static_cast<long>((B >> 16) & 0xFF);
        const long GreenB = static_cast<long>((B >> 8) & 0xFF);
        const long BlueB = static_cast<long>(B & 0xFF);
        const long Red = (((RedB - RedA) * W) >> 8) + RedA;
        const long Green = (((GreenB - GreenA) * W) >> 8) + GreenA;
        const long Blue = (((BlueB - BlueA) * W) >> 8) + BlueA;
        return ((static_cast<unsigned long>(Red) << 16) & 0xFF0000) |
               ((static_cast<unsigned long>(Green) << 8) & 0xFF00) |
               (static_cast<unsigned long>(Blue) & 0xFF);
    }

    // [0] 06241cc0 / 06242070: step, the whole dword including its top byte.
    static unsigned long RampColorStep(unsigned long A, unsigned long /*B*/, float /*T*/)
    {
        return A;
    }

    // [1] 06241cd0 / 06242080: W = T.
    static unsigned long RampColorLinear(unsigned long A, unsigned long B, float T)
    {
        return RampColorBlend(A, B, T);
    }

    // [2] 06241d50 / 06242100: W = T * T.
    static unsigned long RampColorEaseIn(unsigned long A, unsigned long B, float T)
    {
        return RampColorBlend(A, B, static_cast<double>(T) * T);
    }

    // [3] 06241de0 / 06242190: W = 1 - (1 - T)^2.
    static unsigned long RampColorEaseOut(unsigned long A, unsigned long B, float T)
    {
        const double OneMinusT = static_cast<double>(kRampOne) - T;
        return RampColorBlend(A, B, static_cast<double>(kRampOne) - OneMinusT * OneMinusT);
    }

    // [4] 06241e80: W = (3 - (T + T)) * T * T.
    static unsigned long RampColorSmooth(unsigned long A, unsigned long B, float T)
    {
        return RampColorBlend(
            A, B, (static_cast<double>(kRampThree) - (static_cast<double>(T) + T)) * T * T);
    }

    // [5] 06241f10 / 062422b0: unsigned compare of the packed dwords.
    static unsigned long RampColorEaseByRise(unsigned long A, unsigned long B, float T)
    {
        if (A < B)
            return RampColorEaseIn(A, B, T);
        return RampColorEaseOut(A, B, T);
    }

    // [6] 06241fc0 / 06242360.
    static unsigned long RampColorEaseByFall(unsigned long A, unsigned long B, float T)
    {
        if (B < A)
            return RampColorEaseIn(A, B, T);
        return RampColorEaseOut(A, B, T);
    }

    // SSE [4] 06242230: W = cvtss2si((T * -2 + 3) * T * T * 255) (DAT_0624fa68/6c/70,
    // rounded to nearest by MXCSR, not truncated). MMX: mm3 = {W, W, W, 0} words,
    // mm4 = {0xFF, 0xFF, 0xFF, 0} - mm3 (DAT_062575e0), the colours unpacked to words;
    // per word (b * W) and (a * (255 - W)) keep their low 16 bits (PMULLW), shift right 8
    // (PSRLW), add with signed saturation (PADDSW), pack with unsigned saturation
    // (PACKUSWB). The alpha word has both weights 0, so the top byte comes out 0.
    static unsigned long RampColorSmoothMmx(unsigned long A, unsigned long B, float T)
    {
        const float Weight = (T * -2.0f + 3.0f) * T * T * kRamp255;
        const int W = _mm_cvtss_si32(_mm_set_ss(Weight));
        const unsigned short WeightB = static_cast<unsigned short>(W);
        const unsigned short WeightA = static_cast<unsigned short>(0xFF - WeightB);
        unsigned long Result = 0;
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            const unsigned int ByteA = (A >> (Channel * 8)) & 0xFF;
            const unsigned int ByteB = (B >> (Channel * 8)) & 0xFF;
            const unsigned int PartB = static_cast<unsigned short>(ByteB * WeightB) >> 8;
            const unsigned int PartA = static_cast<unsigned short>(ByteA * WeightA) >> 8;
            int Sum = static_cast<int>(static_cast<short>(PartB)) +
                      static_cast<int>(static_cast<short>(PartA));
            if (Sum > 0x7FFF)
                Sum = 0x7FFF;
            if (Sum < 0)
                Sum = 0;
            if (Sum > 0xFF)
                Sum = 0xFF;
            Result |= static_cast<unsigned long>(Sum) << (Channel * 8);
        }
        return Result;
    }

    // SetMode 06241b50: the same two adjacent seven-entry tables (x87 at DAT_06257be4,
    // SSE at DAT_06257c00, guard DAT_06257be0), stored into +0x24 unchecked.
    typedef unsigned long (*FRampColorInterpolator)(unsigned long A, unsigned long B, float T);
    static const FRampColorInterpolator G_RampColorInterpolators[14] = {
        RampColorStep,   RampColorLinear, RampColorEaseIn,     RampColorEaseOut,
        RampColorSmooth, RampColorEaseByRise, RampColorEaseByFall,
        RampColorStep,   RampColorLinear, RampColorEaseIn,     RampColorEaseOut,
        RampColorSmoothMmx, RampColorEaseByRise, RampColorEaseByFall,
    };

    // The float-to-packed conversion AddKey (062098e0) and SetKeyRgb (06209a70) inline:
    // G, R, B each ftol(c * 255), combined as ((R << 8 | G) << 8) | B with no masking,
    // so an out-of-range channel bleeds into its neighbours exactly as in retail.
    static unsigned long RampPackRgb(const float* Rgb)
    {
        const unsigned long Green =
            static_cast<unsigned long>(RampFtol(static_cast<double>(Rgb[1]) * kRamp255));
        const unsigned long Red =
            static_cast<unsigned long>(RampFtol(static_cast<double>(Rgb[0]) * kRamp255));
        unsigned long Packed = Green | (Red << 8);
        Packed <<= 8;
        const unsigned long Blue =
            static_cast<unsigned long>(RampFtol(static_cast<double>(Rgb[2]) * kRamp255));
        return Packed | Blue;
    }

    // GetKeyRgb (062099f0) / EvalRgb (06242410): each byte as a float times
    // DAT_0624bc98.
    static void RampUnpackRgb(unsigned long Packed, float* Out)
    {
        Out[0] = static_cast<float>((Packed >> 16) & 0xFF) * kRampInv255;
        Out[1] = static_cast<float>((Packed >> 8) & 0xFF) * kRampInv255;
        Out[2] = static_cast<float>(Packed & 0xFF) * kRampInv255;
    }

    // ---- FxRampSingle (vtable 0x0624bbf0) -------------------------------------------------

    // The inline constructor of FUN_06208bc0 / 06242c90: Mode 4, empty keys, RefCount 1.
    // Retail leaves InvSpan, the cached segment and the interpolator uninitialised until
    // SetMode / AddKey write them; every retail construction runs both before any Eval.
    FRampSingle::FRampSingle()
        : Mode_(4), InvSpan_(0.0f), Cache_(0), Interpolator_(nullptr), RefCount_(1)
    {
    }

    // 0620a950: interface table 0x0624bcf0 {"1.11_IRampSingle", 0},
    // {"1.11_IDAComponent", 0}.
    GENRESULT DACOM_API FRampSingle::QueryInterface(const char* Iid, void** Out)
    {
        if (Iids::EqualNoNullTest(Iid, Iids::RampSingle) || Iids::EqualNoNullTest(Iid, Iids::DAComponent))
        {
            AddRef();
            *Out = static_cast<IRampSingle*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    // 0620aa00 (count at +0x28).
    unsigned long DACOM_API FRampSingle::AddRef()
    {
        ++RefCount_;
        return RefCount_;
    }

    // 0620aa10: guarded decrement; at zero RefCount = 1, then the key container's
    // destructor (FUN_0620cce0) and the free.
    unsigned long DACOM_API FRampSingle::Release()
    {
        if (RefCount_ != 0)
            --RefCount_;
        if (RefCount_ == 0)
        {
            RefCount_ = 1;
            delete this;
            return 0;
        }
        return RefCount_;
    }

    // 06208c50: resize the keys to 0 (FUN_0620a260(0)), free the buffer when Capacity is
    // non-zero. Mode, InvSpan, the cache and the interpolator are untouched.
    GENRESULT DACOM_API FRampSingle::Reset()
    {
        Keys_.Free();
        return GR_OK;
    }

    // 06242810.
    GENRESULT DACOM_API FRampSingle::CopyFrom(IRampSingle* Other)
    {
        Reset();
        SetMode(Other->GetMode());
        const int Count = Other->GetKeyCount();
        for (int Index = 0; Index < Count; ++Index)
        {
            float Time = 0.0f;
            float Value = 0.0f;
            Other->GetKeyTime(Index, &Time);
            Other->GetKeyValue(Index, &Value);
            AddKey(Time, &Value);
        }
        return GR_OK;
    }

    void FRampSingle::ResetEvalCache()
    {
        // Only an exactly-zero span skips the divide (unordered divides).
        Cache_ = 0;
        InvSpan_ = 0.0f;
        if (Keys_.Count() > 1)
        {
            // The span stays on the x87 stack; only the quotient is stored as a float.
            const double Span = static_cast<double>(Keys_.At(1).Time) - Keys_.At(0).Time;
            if (!(Span == 0.0))
                InvSpan_ = static_cast<float>(static_cast<double>(kRampOne) / Span);
        }
    }

    // 06208c90: inserts after keys of equal time (see UpperBound); returns the index.
    int DACOM_API FRampSingle::AddKey(float Time, const float* Value)
    {
        const int Index = Keys_.UpperBound(Time);
        Keys_.InsertAt(Index, Time, *Value);
        ResetEvalCache();
        return Index;
    }

    // 06208d80. No bounds check.
    GENRESULT DACOM_API FRampSingle::RemoveKey(int Index)
    {
        Keys_.EraseAt(Index);
        ResetEvalCache();
        return GR_OK;
    }

    // 06242b10. No guard for an empty ramp: retail reads key 0 unconditionally. Every
    // compare is ordered: an unordered time falls through to the cached segment.
    double DACOM_API FRampSingle::Eval(float Time)
    {
        if (Time <= Keys_.At(0).Time)
            return Keys_.At(0).Value;
        const int Last = Keys_.LastIndex();
        if (Time >= Keys_.At(Last).Time)
            return Keys_.At(Last).Value;

        int Index = Cache_;
        bool bRecompute = false;
        if (Time < Keys_.At(Index).Time)
        {
            --Index;
            Cache_ = Index;
            while (Time < Keys_.At(Index).Time)
            {
                --Index;
                Cache_ = Index;
            }
            bRecompute = true;
        }
        else if (Keys_.At(Index + 1).Time < Time)
        {
            ++Index;
            Cache_ = Index;
            while (Keys_.At(Index + 1).Time < Time)
            {
                ++Index;
                Cache_ = Index;
            }
            bRecompute = true;
        }
        if (bRecompute)
        {
            // The span stays on the x87 stack; only the quotient is stored as a float.
            const double Span =
                static_cast<double>(Keys_.At(Index + 1).Time) - Keys_.At(Index).Time;
            if (Span == 0.0)
                InvSpan_ = 0.0f;
            else
                InvSpan_ = static_cast<float>(static_cast<double>(kRampOne) / Span);
        }
        const float U =
            static_cast<float>((static_cast<double>(Time) - Keys_.At(Index).Time) * InvSpan_);
        // The interpolator's ST0 is returned as it is.
        return Interpolator_(Keys_.At(Index).Value, Keys_.At(Index + 1).Value, U);
    }

    // 06208e00.
    GENRESULT DACOM_API FRampSingle::GetKeyTime(int Index, float* Out)
    {
        *Out = Keys_.At(Index).Time;
        return GR_OK;
    }

    // 06208e20.
    GENRESULT DACOM_API FRampSingle::GetKeyValue(int Index, float* Out)
    {
        *Out = Keys_.At(Index).Value;
        return GR_OK;
    }

    // 06208e40.
    GENRESULT DACOM_API FRampSingle::SetKeyTime(int Index, float Time, int* OutNewIndex)
    {
        const int NewIndex = Keys_.Reposition(Index, Time);
        ResetEvalCache();
        *OutNewIndex = NewIndex;
        return GR_OK;
    }

    // 062090f0. No eval-cache reset.
    GENRESULT DACOM_API FRampSingle::SetKeyValue(int Index, const float* Value)
    {
        Keys_.At(Index).Value = *Value;
        return GR_OK;
    }

    // 06209110: [+4].
    unsigned long DACOM_API FRampSingle::GetMode()
    {
        return Mode_;
    }

    // 06242880 (see G_RampSingleInterpolators).
    GENRESULT DACOM_API FRampSingle::SetMode(unsigned long Mode)
    {
        Mode_ = Mode;
        const unsigned long TableBase = RampCpuHasSse() ? 7 : 0;
        Interpolator_ = G_RampSingleInterpolators[TableBase + Mode];
        return GR_OK;
    }

    // 06209120: LastIndex + 1.
    int DACOM_API FRampSingle::GetKeyCount()
    {
        return Keys_.Count();
    }

    // 06209130: Out is not written.
    GENRESULT DACOM_API FRampSingle::GetKeyFactoryName(const char** /*Out*/)
    {
        return kRampNoKeyFactory;
    }

    // 06209210, called only by the factory.
    GENRESULT DACOM_API FRampSingle::Initialize(AGGDESC* /*Desc*/)
    {
        Reset();
        SetMode(4);
        const float Zero = 0.0f;
        AddKey(0.0f, &Zero);
        return GR_OK;
    }

    // ---- FxRampColor (vtable 0x0624bc40) --------------------------------------------------

    // FUN_06209240 / 06242630 inline constructor: Mode 4, empty keys, RefCount 1.
    FRampColor::FRampColor()
        : Mode_(4), InvSpan_(0.0f), Cache_(0), Interpolator_(nullptr), RefCount_(1)
    {
    }

    // 0620aa50: interface table 0x0624bd08 {"1.11_IRampColor", 0},
    // {"1.11_IDAComponent", 0}.
    GENRESULT DACOM_API FRampColor::QueryInterface(const char* Iid, void** Out)
    {
        if (Iids::EqualNoNullTest(Iid, Iids::RampColor) || Iids::EqualNoNullTest(Iid, Iids::DAComponent))
        {
            AddRef();
            *Out = static_cast<IRampColor*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    // 0620ab00.
    unsigned long DACOM_API FRampColor::AddRef()
    {
        ++RefCount_;
        return RefCount_;
    }

    // 0620ab10: the FxRampSingle Release shape.
    unsigned long DACOM_API FRampColor::Release()
    {
        if (RefCount_ != 0)
            --RefCount_;
        if (RefCount_ == 0)
        {
            RefCount_ = 1;
            delete this;
            return 0;
        }
        return RefCount_;
    }

    // 062092d0: FUN_0620a380(0), free when Capacity is non-zero.
    GENRESULT DACOM_API FRampColor::Reset()
    {
        Keys_.Free();
        return GR_OK;
    }

    // 06241ae0: the packed value round-trips through RGB floats, as in retail.
    GENRESULT DACOM_API FRampColor::CopyFrom(IRampColor* Other)
    {
        Reset();
        SetMode(Other->GetMode());
        const int Count = Other->GetKeyCount();
        for (int Index = 0; Index < Count; ++Index)
        {
            float Time = 0.0f;
            float Rgb[3] = {0.0f, 0.0f, 0.0f};
            Other->GetKeyTime(Index, &Time);
            Other->GetKeyRgb(Index, Rgb);
            AddKey(Time, Rgb);
        }
        return GR_OK;
    }

    void FRampColor::ResetEvalCache()
    {
        Cache_ = 0;
        InvSpan_ = 0.0f;
        if (Keys_.Count() > 1)
        {
            // The span stays on the x87 stack; only the quotient is stored as a float.
            const double Span = static_cast<double>(Keys_.At(1).Time) - Keys_.At(0).Time;
            if (!(Span == 0.0))
                InvSpan_ = static_cast<float>(static_cast<double>(kRampOne) / Span);
        }
    }

    // 062098e0: pack the RGB floats (RampPackRgb), then the AddKeyPacked body.
    int DACOM_API FRampColor::AddKey(float Time, const float* Rgb)
    {
        const unsigned long Packed = RampPackRgb(Rgb);
        const int Index = Keys_.UpperBound(Time);
        Keys_.InsertAt(Index, Time, Packed);
        ResetEvalCache();
        return Index;
    }

    // 06209400: the FxRampSingle RemoveKey body.
    GENRESULT DACOM_API FRampColor::RemoveKey(int Index)
    {
        Keys_.EraseAt(Index);
        ResetEvalCache();
        return GR_OK;
    }

    // 06242410: EvalPacked through the vtable.
    float* DACOM_API FRampColor::EvalRgb(float* Out, float Time)
    {
        const unsigned long Packed = EvalPacked(Time);
        RampUnpackRgb(Packed, Out);
        return Out;
    }

    // 062424b0: FxRampSingle::Eval's search and cache (06242b10) over packed values.
    unsigned long DACOM_API FRampColor::EvalPacked(float Time)
    {
        if (Time <= Keys_.At(0).Time)
            return Keys_.At(0).Value;
        const int Last = Keys_.LastIndex();
        if (Time >= Keys_.At(Last).Time)
            return Keys_.At(Last).Value;

        int Index = Cache_;
        bool bRecompute = false;
        if (Time < Keys_.At(Index).Time)
        {
            --Index;
            Cache_ = Index;
            while (Time < Keys_.At(Index).Time)
            {
                --Index;
                Cache_ = Index;
            }
            bRecompute = true;
        }
        else if (Keys_.At(Index + 1).Time < Time)
        {
            ++Index;
            Cache_ = Index;
            while (Keys_.At(Index + 1).Time < Time)
            {
                ++Index;
                Cache_ = Index;
            }
            bRecompute = true;
        }
        if (bRecompute)
        {
            // The span stays on the x87 stack; only the quotient is stored as a float.
            const double Span =
                static_cast<double>(Keys_.At(Index + 1).Time) - Keys_.At(Index).Time;
            if (Span == 0.0)
                InvSpan_ = 0.0f;
            else
                InvSpan_ = static_cast<float>(static_cast<double>(kRampOne) / Span);
        }
        const float U =
            static_cast<float>((static_cast<double>(Time) - Keys_.At(Index).Time) * InvSpan_);
        return Interpolator_(Keys_.At(Index).Value, Keys_.At(Index + 1).Value, U);
    }

    // 06209480.
    GENRESULT DACOM_API FRampColor::GetKeyTime(int Index, float* Out)
    {
        *Out = Keys_.At(Index).Time;
        return GR_OK;
    }

    // 062099f0: the stored packed value unpacked into three floats.
    GENRESULT DACOM_API FRampColor::GetKeyRgb(int Index, float* Out)
    {
        RampUnpackRgb(Keys_.At(Index).Value, Out);
        return GR_OK;
    }

    // 062094c0: the FxRampSingle SetKeyTime body (06208e40).
    GENRESULT DACOM_API FRampColor::SetKeyTime(int Index, float Time, int* OutNewIndex)
    {
        const int NewIndex = Keys_.Reposition(Index, Time);
        ResetEvalCache();
        *OutNewIndex = NewIndex;
        return GR_OK;
    }

    // 06209a70.
    GENRESULT DACOM_API FRampColor::SetKeyRgb(int Index, const float* Rgb)
    {
        Keys_.At(Index).Value = RampPackRgb(Rgb);
        return GR_OK;
    }

    // 06209790.
    unsigned long DACOM_API FRampColor::GetMode()
    {
        return Mode_;
    }

    // 06241b50 (see G_RampColorInterpolators).
    GENRESULT DACOM_API FRampColor::SetMode(unsigned long Mode)
    {
        Mode_ = Mode;
        const unsigned long TableBase = RampCpuHasSse() ? 7 : 0;
        Interpolator_ = G_RampColorInterpolators[TableBase + Mode];
        return GR_OK;
    }

    // 062097a0.
    int DACOM_API FRampColor::GetKeyCount()
    {
        return Keys_.Count();
    }

    // 062097b0: Out is not written.
    GENRESULT DACOM_API FRampColor::GetKeyFactoryName(const char** /*Out*/)
    {
        return kRampNoKeyFactory;
    }

    // 06209310: the AddKey body over a caller-packed value.
    int DACOM_API FRampColor::AddKeyPacked(float Time, const unsigned long* Packed)
    {
        const int Index = Keys_.UpperBound(Time);
        Keys_.InsertAt(Index, Time, *Packed);
        ResetEvalCache();
        return Index;
    }

    // 062094a0.
    GENRESULT DACOM_API FRampColor::GetKeyPacked(int Index, unsigned long* Out)
    {
        *Out = Keys_.At(Index).Value;
        return GR_OK;
    }

    // 06209770.
    GENRESULT DACOM_API FRampColor::SetKeyPacked(int Index, const unsigned long* Packed)
    {
        Keys_.At(Index).Value = *Packed;
        return GR_OK;
    }

    // 06209890, called only by the factory 06242630.
    GENRESULT DACOM_API FRampColor::Initialize(AGGDESC* /*Desc*/)
    {
        Reset();
        SetMode(4);
        const float Grey[3] = {0.8f, 0.8f, 0.8f};
        AddKey(0.0f, Grey);
        return GR_OK;
    }

    // ---- Nested scalar ramps (FxRampSingleRamp 0x0624bb18, FxAnimatedSingleRamp
    // 0x0624bac8) -------------------------------------------------------------------------
    // Addresses are given as RampSingleRamp / AnimatedSingleRamp.

    // FUN_06207490 / FUN_06210860 (and the inline copies in the factories 062414e0 /
    // 06241900 and in FUN_06210900): Mode 4, empty keys, RefCount 1.
    template <typename TTraits>
    TNestedSingleRamp<TTraits>::TNestedSingleRamp() : Mode_(4), RefCount_(1)
    {
    }

    // Release's teardown (FUN_0620cf20 / FUN_0620cde0): every stored child released,
    // then the buffer freed by the key container's destructor.
    template <typename TTraits>
    TNestedSingleRamp<TTraits>::~TNestedSingleRamp()
    {
        ReleaseChildren();
    }

    template <typename TTraits>
    void TNestedSingleRamp<TTraits>::ReleaseChildren()
    {
        // FUN_062086e0 / FUN_06208910 per key: release a non-null child and null the slot.
        const int Last = Keys_.LastIndex();
        for (int Index = 0; Index <= Last; ++Index)
        {
            FChild* Child = Keys_.At(Index).Value;
            if (Child != nullptr)
            {
                Child->Release();
                Keys_.At(Index).Value = nullptr;
            }
        }
    }

    // 0620ad40 / 0620ac40: "1.11_IRampSingleRamp" / "1.11_IAnimatedSingleRamp", then
    // "1.11_IDAComponent".
    template <typename TTraits>
    GENRESULT DACOM_API TNestedSingleRamp<TTraits>::QueryInterface(const char* Iid, void** Out)
    {
        if (Iids::EqualNoNullTest(Iid, TTraits::Iid()) || Iids::EqualNoNullTest(Iid, Iids::DAComponent))
        {
            AddRef();
            *Out = static_cast<typename TTraits::FInterface*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    // 0620adf0 / 0620acf0 (count at +0x1C).
    template <typename TTraits>
    unsigned long DACOM_API TNestedSingleRamp<TTraits>::AddRef()
    {
        ++RefCount_;
        return RefCount_;
    }

    // 0620ae00 / 0620ad00: the FxRampSingle Release shape.
    template <typename TTraits>
    unsigned long DACOM_API TNestedSingleRamp<TTraits>::Release()
    {
        if (RefCount_ != 0)
            --RefCount_;
        if (RefCount_ == 0)
        {
            RefCount_ = 1;
            delete this;
            return 0;
        }
        return RefCount_;
    }

    // 062074f0 / 06206d40: releases every child (FUN_0620a4c0(0)) and frees the buffer.
    // Mode is untouched.
    template <typename TTraits>
    GENRESULT DACOM_API TNestedSingleRamp<TTraits>::Reset()
    {
        ReleaseChildren();
        Keys_.Free();
        return GR_OK;
    }

    // 06207530 / 06206d80: Mode is written straight into +4 (not SetMode); each key is
    // deep-copied through a fresh child from CreateKeyValue. Retail's two smart-pointer
    // locals (EH state 06247520 / 062474c0) are plain pointers here, released in the same
    // order. CreateKeyValue's result is not checked, as in retail.
    template <typename TTraits>
    GENRESULT DACOM_API TNestedSingleRamp<TTraits>::CopyFrom(INestedSingleRamp* Other)
    {
        Reset();
        Mode_ = Other->GetMode();
        const int Count = Other->GetKeyCount();
        for (int Index = 0; Index < Count; ++Index)
        {
            float Time = 0.0f;
            IDAComponent* Source = nullptr;
            IDAComponent* Copy = nullptr;
            Other->GetKeyTime(Index, &Time);
            Other->GetKeyValue(Index, &Source);
            CreateKeyValue(&Copy);
            TTraits::CopyChild(static_cast<FChild*>(Copy), static_cast<FChild*>(Source));
            AddKey(Time, Copy);
            if (Copy != nullptr)
                Copy->Release();
            if (Source != nullptr)
                Source->Release();
        }
        return GR_OK;
    }

    // 06207650 / 06206ea0: inserts after keys of equal time; the stored copy AddRefs a
    // non-null child. No eval cache in the nested ramps.
    template <typename TTraits>
    int DACOM_API TNestedSingleRamp<TTraits>::AddKey(float Time, IDAComponent* Child)
    {
        FChild* Typed = static_cast<FChild*>(Child);
        const int Index = Keys_.UpperBound(Time);
        Keys_.InsertAt(Index, Time, Typed);
        if (Typed != nullptr)
            Typed->AddRef();
        return Index;
    }

    // 062076f0 / 06206f40.
    template <typename TTraits>
    GENRESULT DACOM_API TNestedSingleRamp<TTraits>::RemoveKey(int Index)
    {
        FChild* Child = Keys_.At(Index).Value;
        if (Child != nullptr)
        {
            Child->Release();
            Keys_.At(Index).Value = nullptr;
        }
        Keys_.EraseAt(Index);
        return GR_OK;
    }

    // 06207740 / 06206f90 (child evaluation: IRampSingle::Eval at child +0x1C /
    // IFxAnimatedSingle::Eval at child +0x28). No eval cache and no empty-ramp guard; all
    // compares are ordered. Next is evaluated before Floor. The segment divide has no
    // zero guard. Modes other than 2..6 (and 1) blend linearly.
    template <typename TTraits>
    double DACOM_API TNestedSingleRamp<TTraits>::Eval(float OuterTime, float InnerTime)
    {
        if (OuterTime <= Keys_.At(0).Time)
            return TTraits::EvalChild(Keys_.At(0).Value, InnerTime);
        const int Last = Keys_.LastIndex();
        if (Last + 1 == 1)
            return TTraits::EvalChild(Keys_.At(0).Value, InnerTime);
        if (OuterTime >= Keys_.At(Last).Time)
            return TTraits::EvalChild(Keys_.At(Last).Value, InnerTime);

        int Index = 0;
        while (Index < Last && !(OuterTime < Keys_.At(Index + 1).Time))
            ++Index;

        if (Mode_ == RampModeStep)
            return TTraits::EvalChild(Keys_.At(Index).Value, InnerTime);

        // Next is stored to the Inner argument slot as a float; Floor, U and W stay on the
        // x87 stack, hence double here.
        const float NextValue =
            static_cast<float>(TTraits::EvalChild(Keys_.At(Index + 1).Value, InnerTime));
        const double FloorValue = TTraits::EvalChild(Keys_.At(Index).Value, InnerTime);
        const double FloorTime = Keys_.At(Index).Time;
        const double U = (static_cast<double>(OuterTime) - FloorTime) /
                         (static_cast<double>(Keys_.At(Index + 1).Time) - FloorTime);
        const double One = kRampOne;

        double W = U;
        switch (Mode_)
        {
        case RampModeEaseIn:
            W = U * U;
            break;
        case RampModeEaseOut:
            W = One - (One - U) * (One - U);
            break;
        case RampModeEaseInOut:
            W = U * ((static_cast<double>(kRampThree) - (U + U)) * U);
            break;
        case RampModeEaseInWhenFalling:
            if (NextValue < FloorValue)
                W = U * U;
            else
                W = One - (One - U) * (One - U);
            break;
        case RampModeEaseInWhenRising:
            if (FloorValue < NextValue)
                W = U * U;
            else
                W = One - (One - U) * (One - U);
            break;
        default:
            break;
        }
        return FloorValue * (One - W) + static_cast<double>(NextValue) * W;
    }

    // 06207880 / 062070d0.
    template <typename TTraits>
    GENRESULT DACOM_API TNestedSingleRamp<TTraits>::GetKeyTime(int Index, float* Out)
    {
        *Out = Keys_.At(Index).Time;
        return GR_OK;
    }

    // 062078a0 / 062070f0: AddRef with no null check.
    template <typename TTraits>
    GENRESULT DACOM_API TNestedSingleRamp<TTraits>::GetKeyValue(int Index, IDAComponent** Out)
    {
        FChild* Child = Keys_.At(Index).Value;
        *Out = Child;
        Child->AddRef();
        return GR_OK;
    }

    // 062078c0 / 06207110. Retail holds the moved child in a refcounted temporary across
    // the erase and the insert; the count ends where it started, so the port moves the
    // pointer.
    template <typename TTraits>
    GENRESULT DACOM_API TNestedSingleRamp<TTraits>::SetKeyTime(int Index, float Time,
                                                               int* OutNewIndex)
    {
        *OutNewIndex = Keys_.Reposition(Index, Time);
        return GR_OK;
    }

    // 06207b70 / 062073c0.
    template <typename TTraits>
    GENRESULT DACOM_API TNestedSingleRamp<TTraits>::SetKeyValue(int Index, IDAComponent* Child)
    {
        FChild* Typed = static_cast<FChild*>(Child);
        FChild*& Slot = Keys_.At(Index).Value;
        if (Slot != Typed)
        {
            if (Slot != nullptr)
            {
                Slot->Release();
                Slot = nullptr;
            }
            Slot = Typed;
            if (Typed != nullptr)
                Typed->AddRef();
        }
        return GR_OK;
    }

    // 06207bb0 / 06207400.
    template <typename TTraits>
    unsigned long DACOM_API TNestedSingleRamp<TTraits>::GetMode()
    {
        return Mode_;
    }

    // 06207bc0 / 06207410: a plain store, no interpolator table.
    template <typename TTraits>
    GENRESULT DACOM_API TNestedSingleRamp<TTraits>::SetMode(unsigned long Mode)
    {
        Mode_ = Mode;
        return GR_OK;
    }

    // 06207bd0 / 06207420.
    template <typename TTraits>
    int DACOM_API TNestedSingleRamp<TTraits>::GetKeyCount()
    {
        return Keys_.Count();
    }

    // 06209b40 / 06209b60.
    template <typename TTraits>
    GENRESULT DACOM_API TNestedSingleRamp<TTraits>::GetKeyFactoryName(const char** Out)
    {
        *Out = TTraits::KeyFactoryName();
        return GR_OK;
    }

    // 062074e0 / 06206d30 (factories 062414e0 / 06241900).
    template <typename TTraits>
    GENRESULT DACOM_API TNestedSingleRamp<TTraits>::Initialize(AGGDESC* /*Desc*/)
    {
        Reset();
        return GR_OK;
    }

    // 06207be0 / 06207430: GetKeyFactoryName(&Name), then CreateRampKeyValue.
    template <typename TTraits>
    GENRESULT DACOM_API TNestedSingleRamp<TTraits>::CreateKeyValue(IDAComponent** Out)
    {
        const char* Name = nullptr;
        GetKeyFactoryName(&Name);
        return CreateRampKeyValue(Name, Out);
    }

    template class TNestedSingleRamp<FRampSingleRampTraits>;
    template class TNestedSingleRamp<FAnimatedSingleRampTraits>;

    // ---- FxRampColorRamp (vtable 0x0624bb68) ---------------------------------------------

    // FUN_062108b0 / FUN_06207c40 / factory 062416f0: Mode 4, empty keys, RefCount 1.
    FRampColorRamp::FRampColorRamp() : Mode_(4), RefCount_(1)
    {
    }

    // FUN_0620d0a0-style teardown: children released, buffer freed.
    FRampColorRamp::~FRampColorRamp()
    {
        ReleaseChildren();
    }

    void FRampColorRamp::ReleaseChildren()
    {
        // FUN_06208800 per key.
        const int Last = Keys_.LastIndex();
        for (int Index = 0; Index <= Last; ++Index)
        {
            IRampColor* Child = Keys_.At(Index).Value;
            if (Child != nullptr)
            {
                Child->Release();
                Keys_.At(Index).Value = nullptr;
            }
        }
    }

    // 0620ae40: "1.11_IRampColorRamp", then "1.11_IDAComponent".
    GENRESULT DACOM_API FRampColorRamp::QueryInterface(const char* Iid, void** Out)
    {
        if (Iids::EqualNoNullTest(Iid, Iids::RampColorRamp) || Iids::EqualNoNullTest(Iid, Iids::DAComponent))
        {
            AddRef();
            *Out = static_cast<IRampColorRamp*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    // 0620aef0.
    unsigned long DACOM_API FRampColorRamp::AddRef()
    {
        ++RefCount_;
        return RefCount_;
    }

    // 0620af00.
    unsigned long DACOM_API FRampColorRamp::Release()
    {
        if (RefCount_ != 0)
            --RefCount_;
        if (RefCount_ == 0)
        {
            RefCount_ = 1;
            delete this;
            return 0;
        }
        return RefCount_;
    }

    // 06207ca0.
    GENRESULT DACOM_API FRampColorRamp::Reset()
    {
        ReleaseChildren();
        Keys_.Free();
        return GR_OK;
    }

    // 06207ce0: the 06207530 body over colour children (GetKeyCount +0x3C,
    // GetKeyValue +0x28, CreateKeyValue +0x48, child CopyFrom +0x10, AddKey +0x14).
    GENRESULT DACOM_API FRampColorRamp::CopyFrom(IRampColorRamp* Other)
    {
        Reset();
        Mode_ = Other->GetMode();
        const int Count = Other->GetKeyCount();
        for (int Index = 0; Index < Count; ++Index)
        {
            float Time = 0.0f;
            IRampColor* Source = nullptr;
            IDAComponent* Copy = nullptr;
            Other->GetKeyTime(Index, &Time);
            Other->GetKeyValue(Index, &Source);
            CreateKeyValue(&Copy);
            IRampColor* TypedCopy = static_cast<IRampColor*>(Copy);
            TypedCopy->CopyFrom(Source);
            AddKey(Time, TypedCopy);
            if (Copy != nullptr)
                Copy->Release();
            if (Source != nullptr)
                Source->Release();
        }
        return GR_OK;
    }

    // 06207e00: the 06207650 body over colour children.
    int DACOM_API FRampColorRamp::AddKey(float Time, IRampColor* Child)
    {
        const int Index = Keys_.UpperBound(Time);
        Keys_.InsertAt(Index, Time, Child);
        if (Child != nullptr)
            Child->AddRef();
        return Index;
    }

    // 06207ea0.
    GENRESULT DACOM_API FRampColorRamp::RemoveKey(int Index)
    {
        IRampColor* Child = Keys_.At(Index).Value;
        if (Child != nullptr)
        {
            Child->Release();
            Keys_.At(Index).Value = nullptr;
        }
        Keys_.EraseAt(Index);
        return GR_OK;
    }

    // 06207ef0: the 06207740 search over colour children (EvalRgb, child +0x1C). Modes 5
    // and 6 compare channel sums ((r + g) + b).
    float* DACOM_API FRampColorRamp::EvalRgb(float* Out, float OuterTime, float InnerTime)
    {
        if (OuterTime <= Keys_.At(0).Time)
        {
            Keys_.At(0).Value->EvalRgb(Out, InnerTime);
            return Out;
        }
        const int Last = Keys_.LastIndex();
        if (Last + 1 == 1)
        {
            Keys_.At(0).Value->EvalRgb(Out, InnerTime);
            return Out;
        }
        if (OuterTime >= Keys_.At(Last).Time)
        {
            Keys_.At(Last).Value->EvalRgb(Out, InnerTime);
            return Out;
        }

        int Index = 0;
        while (Index < Last && !(OuterTime < Keys_.At(Index + 1).Time))
            ++Index;

        if (Mode_ == RampModeStep)
        {
            Keys_.At(Index).Value->EvalRgb(Out, InnerTime);
            return Out;
        }

        float Next[3];
        float Floor[3];
        Keys_.At(Index + 1).Value->EvalRgb(Next, InnerTime);
        Keys_.At(Index).Value->EvalRgb(Floor, InnerTime);
        // U, W and 1 - W stay on the x87 stack.
        const double FloorTime = Keys_.At(Index).Time;
        const double U = (static_cast<double>(OuterTime) - FloorTime) /
                         (static_cast<double>(Keys_.At(Index + 1).Time) - FloorTime);
        const double One = kRampOne;

        // Mode 1 and every Mode above 6 blend with W = U.
        double W = U;
        switch (Mode_)
        {
        case RampModeEaseIn:
            W = U * U;
            break;
        case RampModeEaseOut:
            W = One - (One - U) * (One - U);
            break;
        case RampModeEaseInOut:
            W = U * ((static_cast<double>(kRampThree) - (U + U)) * U);
            break;
        case RampModeEaseInWhenFalling:
        {
            // Ease-in only when SumFloor > SumNext (unordered takes ease-out).
            const double SumNext = (static_cast<double>(Next[0]) + Next[1]) + Next[2];
            const double SumFloor = (static_cast<double>(Floor[0]) + Floor[1]) + Floor[2];
            if (SumFloor > SumNext)
                W = U * U;
            else
                W = One - (One - U) * (One - U);
            break;
        }
        case RampModeEaseInWhenRising:
        {
            // The sums pushed the other way: ease-in only when SumNext > SumFloor.
            const double SumFloor = (static_cast<double>(Floor[0]) + Floor[1]) + Floor[2];
            const double SumNext = (static_cast<double>(Next[0]) + Next[1]) + Next[2];
            if (SumNext > SumFloor)
                W = U * U;
            else
                W = One - (One - U) * (One - U);
            break;
        }
        default:
            break;
        }

        // The three Floor * (1 - W) and the first two Next * W products are stored as
        // floats; Next[2] * W stays on the stack until its add.
        const double OneMinusW = One - W;
        const float FloorPart0 = static_cast<float>(Floor[0] * OneMinusW);
        const float FloorPart1 = static_cast<float>(Floor[1] * OneMinusW);
        const float FloorPart2 = static_cast<float>(Floor[2] * OneMinusW);
        const float NextPart0 = static_cast<float>(Next[0] * W);
        const float NextPart1 = static_cast<float>(Next[1] * W);
        const double NextPart2 = Next[2] * W;
        Out[0] = static_cast<float>(static_cast<double>(NextPart0) + FloorPart0);
        Out[1] = static_cast<float>(static_cast<double>(NextPart1) + FloorPart1);
        Out[2] = static_cast<float>(NextPart2 + FloorPart2);
        return Out;
    }

    // 06241320: the same search with child->EvalPacked(Inner) (child +0x20). Retail quirk
    // kept: the weight is ftol(InnerTime * 255), not the segment's U, there is no mode
    // switch, and the blend runs from next towards floor.
    unsigned long DACOM_API FRampColorRamp::EvalPacked(float OuterTime, float InnerTime)
    {
        if (OuterTime <= Keys_.At(0).Time)
            return Keys_.At(0).Value->EvalPacked(InnerTime);
        const int Last = Keys_.LastIndex();
        if (Last + 1 == 1)
            return Keys_.At(0).Value->EvalPacked(InnerTime);
        if (OuterTime >= Keys_.At(Last).Time)
            return Keys_.At(Last).Value->EvalPacked(InnerTime);

        int Index = 0;
        while (Index < Last && !(OuterTime < Keys_.At(Index + 1).Time))
            ++Index;

        if (Mode_ == RampModeStep)
            return Keys_.At(Index).Value->EvalPacked(InnerTime);

        const unsigned long Next = Keys_.At(Index + 1).Value->EvalPacked(InnerTime);
        const unsigned long Floor = Keys_.At(Index).Value->EvalPacked(InnerTime);
        const long W = RampFtol(static_cast<double>(InnerTime) * kRamp255);
        const long RedNext = static_cast<long>((Next >> 16) & 0xFF);
        const long GreenNext = static_cast<long>((Next >> 8) & 0xFF);
        const long BlueNext = static_cast<long>(Next & 0xFF);
        const long RedFloor = static_cast<long>((Floor >> 16) & 0xFF);
        const long GreenFloor = static_cast<long>((Floor >> 8) & 0xFF);
        const long BlueFloor = static_cast<long>(Floor & 0xFF);
        const long Red = (((RedFloor - RedNext) * W) >> 8) + RedNext;
        const long Green = (((GreenFloor - GreenNext) * W) >> 8) + GreenNext;
        const long Blue = (((BlueFloor - BlueNext) * W) >> 8) + BlueNext;
        return ((static_cast<unsigned long>(Red) << 16) & 0xFF0000) |
               ((static_cast<unsigned long>(Green) << 8) & 0xFF00) |
               (static_cast<unsigned long>(Blue) & 0xFF);
    }

    // 062080f0.
    GENRESULT DACOM_API FRampColorRamp::GetKeyTime(int Index, float* Out)
    {
        *Out = Keys_.At(Index).Time;
        return GR_OK;
    }

    // 06208110: AddRef unchecked.
    GENRESULT DACOM_API FRampColorRamp::GetKeyValue(int Index, IRampColor** Out)
    {
        IRampColor* Child = Keys_.At(Index).Value;
        *Out = Child;
        Child->AddRef();
        return GR_OK;
    }

    // 06208130.
    GENRESULT DACOM_API FRampColorRamp::SetKeyTime(int Index, float Time, int* OutNewIndex)
    {
        *OutNewIndex = Keys_.Reposition(Index, Time);
        return GR_OK;
    }

    // 062083e0: the 06207b70 body.
    GENRESULT DACOM_API FRampColorRamp::SetKeyValue(int Index, IRampColor* Child)
    {
        IRampColor*& Slot = Keys_.At(Index).Value;
        if (Slot != Child)
        {
            if (Slot != nullptr)
            {
                Slot->Release();
                Slot = nullptr;
            }
            Slot = Child;
            if (Child != nullptr)
                Child->AddRef();
        }
        return GR_OK;
    }

    // 06208420.
    unsigned long DACOM_API FRampColorRamp::GetMode()
    {
        return Mode_;
    }

    // 06208430.
    GENRESULT DACOM_API FRampColorRamp::SetMode(unsigned long Mode)
    {
        Mode_ = Mode;
        return GR_OK;
    }

    // 06208440.
    int DACOM_API FRampColorRamp::GetKeyCount()
    {
        return Keys_.Count();
    }

    // 06209b50.
    GENRESULT DACOM_API FRampColorRamp::GetKeyFactoryName(const char** Out)
    {
        *Out = "FxRampColor";
        return GR_OK;
    }

    // 06207c90.
    GENRESULT DACOM_API FRampColorRamp::Initialize(AGGDESC* /*Desc*/)
    {
        Reset();
        return GR_OK;
    }

    // 06208450.
    GENRESULT DACOM_API FRampColorRamp::CreateKeyValue(IDAComponent** Out)
    {
        const char* Name = nullptr;
        GetKeyFactoryName(&Name);
        return CreateRampKeyValue(Name, Out);
    }

    // ---- Free helpers ---------------------------------------------------------------------

    GENRESULT CreateRampKeyValue(const char* Name, IDAComponent** Out)
    {
        AGGDESC Desc;
        Desc.size = 8;
        Desc.interface_name = Name;
        ICOManager* Manager = DACOM_Acquire();
        return Manager->CreateInstance(&Desc, reinterpret_cast<void**>(Out));
    }

    // FUN_06208bc0: operator_new(0x2C), not null-checked.
    void MakeRampSingle(float Value, IRampSingle** Out)
    {
        FRampSingle* Ramp = new FRampSingle();
        Ramp->SetMode(4);
        Ramp->AddKey(0.0f, &Value);
        *Out = Ramp;
    }

    // FUN_06209240: the same over FxRampColor.
    void MakeRampColor(const float* Rgb, IRampColor** Out)
    {
        FRampColor* Ramp = new FRampColor();
        Ramp->SetMode(4);
        Ramp->AddKey(0.0f, Rgb);
        *Out = Ramp;
    }

    // FUN_06210900: a fresh FxAnimatedSingleRamp (0x20, vtable 0x0624bac8) holding one
    // fresh FxAnimatedSingle (0x10) at time 0.
    void MakeAnimatedSingleRamp(float Weight, INestedSingleRamp** Out)
    {
        FAnimatedSingleRamp* const Ramp = new FAnimatedSingleRamp();
        struct FChildReference
        {
            FFxAnimatedSingle* Child;
            ~FChildReference() noexcept(false)
            {
                if (Child != nullptr)
                    Child->Release();
            }
        };
        FChildReference ChildReference = {nullptr};
        FFxAnimatedSingle* const Child = new FFxAnimatedSingle();
        Child->SetWeight(Weight);
        ChildReference.Child = Child;
        Ramp->AddKey(0.0f, Child);
        *Out = Ramp;
    }

    // Inlined in the descriptor builders (e.g. at 062324a3) and their twins.
    void MakeRampSingleRamp(float Value, INestedSingleRamp** Out)
    {
        IRampSingle* Child = nullptr;
        MakeRampSingle(Value, &Child);
        FRampSingleRamp* Ramp = new FRampSingleRamp();
        Ramp->AddKey(0.0f, Child);
        *Out = Ramp;
        Child->Release();
    }

    // The colour twin (e.g. at 06232408).
    void MakeRampColorRamp(const float* Rgb, IRampColorRamp** Out)
    {
        IRampColor* Child = nullptr;
        MakeRampColor(Rgb, &Child);
        FRampColorRamp* Ramp = new FRampColorRamp();
        Ramp->AddKey(0.0f, Child);
        *Out = Ramp;
        Child->Release();
    }

    // ---- FFxAnimatedSingle (vtable 0x0624bca0) ---------------------------------------------

    FFxAnimatedSingle::FFxAnimatedSingle() : Weight_(1.0f), Track_(nullptr), RefCount_(1)
    {
    }

    FFxAnimatedSingle::~FFxAnimatedSingle()
    {
        if (Track_ != nullptr)
        {
            delete Track_;
            Track_ = nullptr;
        }
    }

    // 0620ab50: {"1.11_IAnimatedSingle", "1.11_IDAComponent"}.
    GENRESULT DACOM_API FFxAnimatedSingle::QueryInterface(const char* Iid, void** Out)
    {
        if (Iids::EqualNoNullTest(Iid, Iids::AnimatedSingle) || Iids::EqualNoNullTest(Iid, Iids::DAComponent))
        {
            AddRef();
            *Out = static_cast<IFxAnimatedSingle*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    // 0620ac00 (count at +0xC).
    unsigned long DACOM_API FFxAnimatedSingle::AddRef()
    {
        ++RefCount_;
        return RefCount_;
    }

    // 0620ac10: the FxRampSingle Release shape (scalar deleting destructor 06209b10).
    unsigned long DACOM_API FFxAnimatedSingle::Release()
    {
        if (RefCount_ != 0)
            --RefCount_;
        if (RefCount_ == 0)
        {
            RefCount_ = 1;
            delete this;
            return 0;
        }
        return RefCount_;
    }

    // 06242ec0: with a track, erase [begin, end) -- end = begin, the buffer is kept.
    // LoopType and Weight are untouched.
    GENRESULT DACOM_API FFxAnimatedSingle::Reset()
    {
        if (Track_ != nullptr)
            Track_->Keys.clear();
        return GR_OK;
    }

    // 06242f30: the time scratch slot is not re-zeroed per key; SetLoopType's failure
    // is ignored.
    GENRESULT DACOM_API FFxAnimatedSingle::CopyFrom(IFxAnimatedSingle* Other)
    {
        Reset();
        const int Count = Other->GetKeyCount();
        float Time = 0.0f;
        int Index = 0;
        while (Index < Count)
        {
            float Value[3] = {0.0f, 0.0f, 0.0f};
            Other->GetKeyTime(Index, &Time);
            Other->GetKeyValue(Index, Value);
            AddKey(Time, Value);
            ++Index;
        }
        unsigned long LoopType = 0;
        Other->GetLoopType(&LoopType);
        SetLoopType(LoopType);
        float Weight = 0.0f;
        Other->GetWeight(&Weight);
        SetWeight(Weight);
        return GR_OK;
    }

    // 06243000: 0 without a track or with a null begin, else (end - begin) / 16.
    int DACOM_API FFxAnimatedSingle::GetKeyCount()
    {
        if (Track_ == nullptr)
            return 0;
        return static_cast<int>(Track_->Keys.size());
    }

    // 06242fd0 (Weight at +4).
    GENRESULT DACOM_API FFxAnimatedSingle::SetWeight(float Weight)
    {
        Weight_ = Weight;
        return GR_OK;
    }

    // 06242fe0.
    GENRESULT DACOM_API FFxAnimatedSingle::GetWeight(float* Out)
    {
        *Out = Weight_;
        return GR_OK;
    }

    // FUN_06243280: operator_new(0x14), LoopType 0, empty vector, stored at [this+8].
    void FFxAnimatedSingle::EnsureTrack()
    {
        if (Track_ == nullptr)
        {
            Track_ = new FTrack();
            Track_->LoopType = 0;
        }
    }

    // 06243030.
    int DACOM_API FFxAnimatedSingle::AddKey(float Time, const float* Value)
    {
        EnsureTrack();
        FKey Key;
        Key.Value = Value[0];
        Key.Time = Time;
        Key.InSlope = Value[1];
        Key.OutSlope = Value[2];
        return InsertTrackKey(Track_, Key);
    }

    // FUN_06246780: the insertion index is the first key with NewTime <= key time
    // (unordered keeps scanning), so a new key lands before keys of the same time. The
    // rest is the VC6 vector insert-one (grow to size + max(size, 1)).
    int FFxAnimatedSingle::InsertTrackKey(FTrack* Track, const FKey& Key)
    {
        const int Count = static_cast<int>(Track->Keys.size());
        int Index = 0;
        while (Index < Count && !(Key.Time <= Track->Keys[Index].Time))
            ++Index;
        Track->Keys.insert(Track->Keys.begin() + Index, Key);
        return Index;
    }

    // 06243080: no track check and no bounds check.
    GENRESULT DACOM_API FFxAnimatedSingle::RemoveKey(int Index)
    {
        Track_->Keys.erase(Track_->Keys.begin() + Index);
        return GR_OK;
    }

    // 062430e0.
    double DACOM_API FFxAnimatedSingle::Eval(float Time)
    {
        if (Track_ == nullptr)
            return Weight_;
        return EvalTrack(Track_, Time);
    }

    // 06243100: no bounds check.
    GENRESULT DACOM_API FFxAnimatedSingle::GetKeyTime(int Index, float* Out)
    {
        if (Track_ == nullptr)
        {
            *Out = 0.0f;
            return GR_OK;
        }
        *Out = Track_->Keys.data()[Index].Time;
        return GR_OK;
    }

    // 06243130.
    GENRESULT DACOM_API FFxAnimatedSingle::GetKeyValue(int Index, float* Out)
    {
        float Triple[3] = {0.0f, 0.0f, 0.0f};
        if (Track_ != nullptr)
        {
            const FKey& Key = Track_->Keys.data()[Index];
            Triple[0] = Key.Value;
            Triple[1] = Key.InSlope;
            Triple[2] = Key.OutSlope;
        }
        Out[0] = Triple[0];
        Out[1] = Triple[1];
        Out[2] = Triple[2];
        return GR_OK;
    }

    // 062431a0.
    GENRESULT DACOM_API FFxAnimatedSingle::SetKeyTime(int Index, float Time, int* OutNewIndex)
    {
        if (Track_ == nullptr)
            return GR_GENERIC;
        *OutNewIndex = SetTrackKeyTime(Track_, Index, Time);
        return GR_OK;
    }

    // 062431d0.
    GENRESULT DACOM_API FFxAnimatedSingle::SetKeyValue(int Index, const float* Value)
    {
        if (Track_ == nullptr)
            return GR_GENERIC;
        FKey& Key = Track_->Keys.data()[Index];
        Key.InSlope = Value[1];
        Key.OutSlope = Value[2];
        Key.Value = Value[0];
        return GR_OK;
    }

    // 06243210.
    GENRESULT DACOM_API FFxAnimatedSingle::SetLoopType(unsigned long LoopType)
    {
        if (Track_ == nullptr)
            return GR_GENERIC;
        Track_->LoopType = LoopType;
        return GR_OK;
    }

    // 06243230.
    GENRESULT DACOM_API FFxAnimatedSingle::GetLoopType(unsigned long* Out)
    {
        if (Track_ == nullptr)
        {
            *Out = 0;
            return GR_OK;
        }
        *Out = Track_->LoopType;
        return GR_OK;
    }

    // 06243260.
    GENRESULT DACOM_API FFxAnimatedSingle::ComputeSlopes(unsigned long Mode)
    {
        if (Track_ == nullptr)
            return GR_GENERIC;
        ComputeTrackSlopes(Track_, Mode);
        return GR_OK;
    }

    // 06242f20. The factory 06243390 runs it right after construction.
    GENRESULT DACOM_API FFxAnimatedSingle::Initialize(AGGDESC* /*Desc*/)
    {
        Reset();
        return GR_OK;
    }

    // FUN_06246660: the scan continues while NewTime < key or unordered. A stop right
    // after Index updates the time in place; otherwise the key is erased and re-inserted
    // as in FUN_06246780 (insert FUN_06246d50). No bounds check.
    int FFxAnimatedSingle::SetTrackKeyTime(FTrack* Track, int Index, float NewTime)
    {
        const int Count = static_cast<int>(Track->Keys.size());
        int Scan = 0;
        while (Scan < Count)
        {
            if (NewTime >= Track->Keys[Scan].Time && Scan != Index)
                break;
            ++Scan;
        }
        if (Scan == Index + 1)
        {
            Track->Keys.data()[Index].Time = NewTime;
            return Index;
        }

        const FKey Old = Track->Keys.data()[Index];
        FKey Moved;
        Moved.Value = Old.Value;
        Moved.Time = NewTime;
        Moved.InSlope = Old.InSlope;
        Moved.OutSlope = Old.OutSlope;
        Track->Keys.erase(Track->Keys.begin() + Index);

        const int Remaining = static_cast<int>(Track->Keys.size());
        int Insert = 0;
        while (Insert < Remaining && !(NewTime <= Track->Keys[Insert].Time))
            ++Insert;
        Track->Keys.insert(Track->Keys.begin() + Insert, Moved);
        return Insert;
    }

    // FUN_06246550(Mode): LoopType = 0; then for every adjacent pair (Prev, Cur):
    //   Dt and Dv are stored as float; only Dt < 1e-4 (ordered, DAT_0624f1a4) is clamped
    //   to 1e-4f. H = Dv / (Dt * 0.5) (DAT_0624bab8) stays on the x87 stack.
    //   0: Prev.OutSlope = 0xFFFFFFFF (step)      1: both = Dv / Dt
    //   2: Prev.Out = 0, Cur.In = H               3: Prev.Out = H, Cur.In = 0
    //   4: both 0
    //   5: Prev.Value <= Cur.Value (or unordered) -> as 3, greater -> as 2.
    //   6: Prev.Value >= Cur.Value (or unordered) -> as 3, less -> as 2.
    //   Other modes leave the pair alone.
    void FFxAnimatedSingle::ComputeTrackSlopes(FTrack* Track, unsigned long Mode)
    {
        Track->LoopType = 0;
        const int Count = static_cast<int>(Track->Keys.size());
        int CurIndex = 1;
        while (CurIndex < Count)
        {
            FKey& Prev = Track->Keys[CurIndex - 1];
            FKey& Cur = Track->Keys[CurIndex];
            float Dt = Cur.Time - Prev.Time;
            const float Dv = Cur.Value - Prev.Value;
            if (Dt < 0.0001f)
                Dt = 0.0001f;
            const double Half = static_cast<double>(Dv) / (static_cast<double>(Dt) * 0.5f);
            switch (Mode)
            {
            case RampModeStep:
            {
                const unsigned long StepBits = 0xFFFFFFFFu;
                std::memcpy(&Prev.OutSlope, &StepBits, sizeof(StepBits));
                break;
            }
            case RampModeLinear:
            {
                const float Slope = static_cast<float>(static_cast<double>(Dv) / Dt);
                Cur.InSlope = Slope;
                Prev.OutSlope = Slope;
                break;
            }
            case RampModeEaseIn:
                Prev.OutSlope = 0.0f;
                Cur.InSlope = static_cast<float>(Half);
                break;
            case RampModeEaseOut:
                Prev.OutSlope = static_cast<float>(Half);
                Cur.InSlope = 0.0f;
                break;
            case RampModeEaseInOut:
                Prev.OutSlope = 0.0f;
                Cur.InSlope = 0.0f;
                break;
            case RampModeEaseInWhenFalling:
                if (Prev.Value > Cur.Value)
                {
                    Prev.OutSlope = 0.0f;
                    Cur.InSlope = static_cast<float>(Half);
                }
                else
                {
                    Prev.OutSlope = static_cast<float>(Half);
                    Cur.InSlope = 0.0f;
                }
                break;
            case RampModeEaseInWhenRising:
                if (Prev.Value < Cur.Value)
                {
                    Prev.OutSlope = 0.0f;
                    Cur.InSlope = static_cast<float>(Half);
                }
                else
                {
                    Prev.OutSlope = static_cast<float>(Half);
                    Cur.InSlope = 0.0f;
                }
                break;
            default:
                break;
            }
            ++CurIndex;
        }
    }

    // FUN_06246950 (cdecl, result in ST0): Hermite segment. Span stays on the x87 stack;
    // U is stored as float. A.OutSlope bits 0xFFFFFFFF mean step.
    double FFxAnimatedSingle::EvalSegment(const FKey* A, const FKey* B, float Time)
    {
        if (A->Time == Time)
            return A->Value;
        if (B->Time == Time)
            return B->Value;
        const double Span = static_cast<double>(B->Time) - A->Time;
        const float U = static_cast<float>((static_cast<double>(Time) - A->Time) / Span);
        unsigned long OutBits = 0;
        std::memcpy(&OutBits, &A->OutSlope, sizeof(OutBits));
        if (OutBits == 0xFFFFFFFFu)
            return A->Value;
        const double U2 = static_cast<double>(U) * U;
        const double U3 = U * U2;
        const double TwoU3 = U3 + U3;
        const double ThreeU2 = U2 * 3.0f;
        const double Tangents = (((U3 - (U2 + U2)) + U) * A->OutSlope * Span) +
                                ((U3 - U2) * B->InSlope * Span);
        const double WithA = Tangents + ((TwoU3 - ThreeU2) + 1.0f) * A->Value;
        return WithA + (ThreeU2 - TwoU3) * B->Value;
    }

    // FUN_062469f0(Time) on the track (low nibble of LoopType: before the first key,
    // high nibble: after the last; 0 holds the end value, 1 / 0x10 cycle, 2 / 0x20 cycle
    // with a value offset, 3 / 0x30 ping-pong, 4 / 0x40 extrapolate along the end slope).
    //   One key: only LoopType bit 0x4 (before) or 0x40 (after) goes on to the general
    //   path. The general path is also taken with no key at all and then reads begin[0]
    //   and begin[-1], as retail does. Rel = T - T0 is kept on the stack for fmod
    //   (_CIfmod) but rounded to float for Q. The lookup guesses
    //   ftol((T - T0) / Span * Last) and steps from there.
    double FFxAnimatedSingle::EvalTrack(FTrack* Track, float Time)
    {
        const int Count = static_cast<int>(Track->Keys.size());
        const FKey* const Begin = Track->Keys.data();
        const unsigned long LoopType = Track->LoopType;
        if (Count == 1)
        {
            if (Time == Begin[0].Time)
                return Begin[0].Value;
            if (Time < Begin[0].Time && (LoopType & 0x4u) == 0)
                return Begin[0].Value;
            if (Time > Begin[0].Time && (LoopType & 0x40u) == 0)
                return Begin[0].Value;
        }

        const int Last = Count - 1;
        const FKey* const LastKey = Begin + Last;
        const float T0 = Begin[0].Time;
        const float V0 = Begin[0].Value;
        const float Tl = LastKey->Time;
        const float Vl = LastKey->Value;
        const float Span = Tl - T0;
        float Offset = 0.0f;

        if (Time < T0)
        {
            const unsigned long Mode = LoopType & 0xFu;
            if (Mode == 0)
                return V0;
            const double Rel = static_cast<double>(Time) - T0;
            const float RelF = static_cast<float>(Rel);
            const float Fm = static_cast<float>(std::fmod(Rel, static_cast<double>(Span)));
            double Q = static_cast<double>(RelF) / Span;
            if (Q < 0.0f)
                Q = Q - 1.0f;
            const float N = static_cast<float>(RampFtol(Q));
            switch (Mode)
            {
            case 1:
                Time = static_cast<float>(static_cast<double>(Fm) + Tl);
                break;
            case 2:
                Time = static_cast<float>(static_cast<double>(Fm) + Tl);
                Offset = static_cast<float>((static_cast<double>(Vl) - V0) * N);
                break;
            case 3:
                if ((RampFtol(N) & 1) == 0)
                    Time = static_cast<float>(static_cast<double>(Fm) + Tl);
                else
                    Time = static_cast<float>(static_cast<double>(T0) - Fm);
                break;
            case 4:
                return V0 - (static_cast<double>(T0) - Time) * Begin[0].InSlope;
            default:
                break;
            }
        }
        else if (Time > Tl)
        {
            const unsigned long Mode = LoopType & 0xF0u;
            if (Mode == 0)
                return Vl;
            const double Rel = static_cast<double>(Time) - T0;
            const float RelF = static_cast<float>(Rel);
            const float Fm = static_cast<float>(std::fmod(Rel, static_cast<double>(Span)));
            double Q = static_cast<double>(RelF) / Span;
            if (Q < 0.0f)
                Q = Q - 1.0f;
            const float N = static_cast<float>(RampFtol(Q));
            switch (Mode)
            {
            case 0x10:
                Time = static_cast<float>(static_cast<double>(Fm) + T0);
                break;
            case 0x20:
                Time = static_cast<float>(static_cast<double>(Fm) + T0);
                Offset = static_cast<float>((static_cast<double>(Vl) - V0) * N);
                break;
            case 0x30:
                if ((RampFtol(N) & 1) == 0)
                    Time = static_cast<float>(static_cast<double>(Fm) + T0);
                else
                    Time = static_cast<float>(static_cast<double>(Tl) - Fm);
                break;
            case 0x40:
                return Vl - (static_cast<double>(Tl) - Time) * LastKey->OutSlope;
            default:
                break;
            }
        }

        const long Guess = RampFtol((static_cast<double>(Time) - T0) / Span * Last);
        const FKey* Probe = Begin + Guess;
        const FKey* SegmentA = nullptr;
        const FKey* SegmentB = nullptr;
        if (Probe->Time > Time)
        {
            while (Probe > Begin && !(Probe->Time < Time))
                --Probe;
            SegmentA = Probe;
            SegmentB = Probe + 1;
        }
        else
        {
            while (Probe <= LastKey && !(Probe->Time > Time))
                ++Probe;
            SegmentA = Probe - 1;
            SegmentB = Probe;
        }
        return EvalSegment(SegmentA, SegmentB, Time) + Offset;
    }

    // ---- FFxAnimatedTransform (vtable 0x0624bbb8) ------------------------------------------

    // DAT_062575e8.
    const float FFxAnimatedTransform::kChannelDefaults[FFxAnimatedTransform::kChannelCount] = {
        0.0f, 0.0f, 0.0f, // Translate X/Y/Z
        0.0f, 0.0f, 0.0f, // Rotate X/Y/Z (degrees)
        1.0f, 1.0f, 1.0f, // Scale X/Y/Z
    };

    // FUN_062434c0 (via FUN_06208620, which then stores vtable 0x0624bbb8): each channel
    // gets an FxAnimatedSingleRamp holding one default key, then Reset (FUN_062436a0,
    // inlined) rebuilds them all again, as retail does.
    FFxAnimatedTransform::FFxAnimatedTransform() : RefCount_(1), PackedModes_(0x435), ChannelFlags_(0)
    {
        unsigned long Index = 0;
        for (Index = 0; Index < kChannelCount; ++Index)
            Children_[Index] = nullptr;
        for (Index = 0; Index < kChannelCount; ++Index)
        {
            if (Children_[Index] != nullptr)
            {
                Children_[Index]->Release();
                Children_[Index] = nullptr;
            }
            const float Weight = kChannelDefaults[Index];
            FAnimatedSingleRamp* Ramp = new FAnimatedSingleRamp();
            FFxAnimatedSingle* Key = new FFxAnimatedSingle();
            Key->SetWeight(Weight);
            Ramp->AddKey(0.0f, Key);
            Children_[Index] = Ramp;
            Key->Release();
        }
        Reset();
    }

    // 0620b910: the QI table walk over {"1.11_IAnimatedTransform", "1.11_IDAComponent"}.
    GENRESULT DACOM_API FFxAnimatedTransform::QueryInterface(const char* Iid, void** Out)
    {
        if (Iids::EqualNoNullTest(Iid, Iids::AnimatedTransform) || Iids::EqualNoNullTest(Iid, Iids::DAComponent))
        {
            AddRef();
            *Out = static_cast<IAnimatedTransform*>(this);
            return GR_OK;
        }
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    unsigned long DACOM_API FFxAnimatedTransform::Release()
    {
        if (RefCount_ != 0)
            --RefCount_;
        if (RefCount_ == 0)
        {
            RefCount_ = 1;
            this->~FFxAnimatedTransform();
            ::operator delete(this);
            return 0;
        }
        return RefCount_;
    }

    // +0xC FUN_062436a0.
    GENRESULT DACOM_API FFxAnimatedTransform::Reset()
    {
        unsigned long Index = 0;
        for (Index = 0; Index < kChannelCount; ++Index)
        {
            Children_[Index]->Reset();
            FFxAnimatedSingle* Key = new FFxAnimatedSingle();
            Key->SetWeight(kChannelDefaults[Index]);
            Children_[Index]->AddKey(0.0f, Key);
            Key->Release();
        }
        ChannelFlags_ = 0x1FF;
        return GR_OK;
    }

    // +0x10 LAB_06243770. No null test on Other.
    GENRESULT DACOM_API FFxAnimatedTransform::CopyFrom(IAnimatedTransform* Other)
    {
        unsigned long Index = 0;
        for (Index = 0; Index < kChannelCount; ++Index)
        {
            IDAComponent* OtherChild = nullptr;
            Other->GetChild(Index, &OtherChild);
            Children_[Index]->CopyFrom(static_cast<INestedSingleRamp*>(OtherChild));
            if (OtherChild != nullptr)
                OtherChild->Release();
        }
        unsigned long A = 0;
        unsigned long B = 0;
        unsigned long C = 0;
        Other->GetPackedModes(&A, &B, &C);
        SetPackedModes(A, B, C);
        Other->GetChannelFlags(&ChannelFlags_);
        return GR_OK;
    }

    // +0x14 LAB_06243830. No bounds or null tests.
    GENRESULT DACOM_API FFxAnimatedTransform::GetChild(unsigned long Index, IDAComponent** Out)
    {
        *Out = Children_[Index];
        (*Out)->AddRef();
        return GR_OK;
    }

    // +0x18 FUN_06243850: a channel's bit is set when every key still equals its default
    // (DAT_062575e8[i]; unordered counts as different). Retail quirk kept: the loop
    // re-reads key 0 on every iteration instead of key j.
    GENRESULT DACOM_API FFxAnimatedTransform::UpdateChannelFlags()
    {
        ChannelFlags_ = 0;
        unsigned long Bit = 1;
        unsigned long Index = 0;
        for (Index = 0; Index < kChannelCount; ++Index)
        {
            FAnimatedSingleRamp* Child = Children_[Index];
            const int KeyCount = Child->GetKeyCount();
            bool bMatchesDefault = true;
            int KeyIndex = 0;
            while (KeyIndex < KeyCount)
            {
                IDAComponent* EntryObject = nullptr;
                Child->GetKeyValue(0, &EntryObject);
                IFxAnimatedSingle* Entry = static_cast<IFxAnimatedSingle*>(EntryObject);
                const int EntryKeyCount = Entry->GetKeyCount();
                float Value = 0.0f;
                if (EntryKeyCount == 0)
                {
                    Entry->GetWeight(&Value);
                }
                else if (EntryKeyCount == 1)
                {
                    float Triple[3] = {0.0f, 0.0f, 0.0f};
                    Entry->GetKeyValue(0, Triple);
                    Value = Triple[0];
                }
                else
                {
                    if (Entry != nullptr)
                        Entry->Release();
                    bMatchesDefault = false;
                    break;
                }
                if (!(Value == kChannelDefaults[Index]))
                {
                    if (Entry != nullptr)
                        Entry->Release();
                    bMatchesDefault = false;
                    break;
                }
                if (Entry != nullptr)
                    Entry->Release();
                ++KeyIndex;
            }
            if (bMatchesDefault)
                ChannelFlags_ |= Bit;
            Bit = Bit + Bit;
        }
        return GR_OK;
    }

    // FUN_06243c50 (thiscall). DAT_0624d4c4 = 0x3c8efa35 (pi/180f). cos c uses the
    // unrounded angle, sin c the float-rounded one; cos a / cos b are stored as float,
    // sin a / sin b stay on the x87 stack. Each element is stored as float through the
    // native x87 kernels.
#pragma float_control(except, on, push)
    void FFxAnimatedTransform::BuildEulerRotation3x3(float* Out9, const float& AngleA,
                                                     const float& AngleB, const float& AngleC)
    {
        const float kDegToRad = 0.017453292f;

        const double RadC = static_cast<double>(AngleC) * kDegToRad;
        const unsigned long ZeroBits = 0;
        const unsigned long OneBits = 0x3F800000;
        std::memcpy(Out9 + 1, &ZeroBits, sizeof(ZeroBits));
        std::memcpy(Out9 + 4, &OneBits, sizeof(OneBits));
        std::memcpy(Out9 + 5, &ZeroBits, sizeof(ZeroBits));
        std::memcpy(Out9 + 7, &ZeroBits, sizeof(ZeroBits));
        float RadCf;
        StoreAlchemyFpuFloat(RadCf, RadC);
        const double CosC = ComputeAlchemyFpuCosineAndClear(RadC, Out9[3]);
        const double RadCfExt = RadCf;
        const double SinC = ComputeAlchemyFpuSine(RadCfExt);
        StoreAlchemyFpuFloat(Out9[0], CosC);
        StoreAlchemyFpuFloat(Out9[2], SinC);
        StoreAlchemyFpuFloat(Out9[6], -SinC);
        StoreAlchemyFpuFloat(Out9[8], CosC);

        const double RadA = static_cast<double>(AngleA) * kDegToRad;
        float CosA;
        StoreAlchemyFpuFloat(CosA, ComputeAlchemyFpuCosine(RadA));
        const double SinA = ComputeAlchemyFpuSine(RadA);
        int Column = 0;
        for (Column = 0; Column < 3; ++Column)
        {
            const double Row1 = Out9[3 + Column];
            StoreAlchemyFpuFloat(Out9[3 + Column],
                                 static_cast<double>(CosA) * Row1 - SinA * Out9[6 + Column]);
            StoreAlchemyFpuFloat(Out9[6 + Column],
                                 static_cast<double>(CosA) * Out9[6 + Column] + Row1 * SinA);
        }

        const double RadB = static_cast<double>(AngleB) * kDegToRad;
        float CosB;
        StoreAlchemyFpuFloat(CosB, ComputeAlchemyFpuCosine(RadB));
        const double SinB = ComputeAlchemyFpuSine(RadB);
        for (Column = 0; Column < 3; ++Column)
        {
            const double Row0 = Out9[Column];
            StoreAlchemyFpuFloat(Out9[Column],
                                 static_cast<double>(CosB) * Row0 - SinB * Out9[3 + Column]);
            StoreAlchemyFpuFloat(Out9[3 + Column],
                                 static_cast<double>(CosB) * Out9[3 + Column] + Row0 * SinB);
        }
    }

    // +0x2C LAB_06243a60. The ramp's outer time is TimeA. Rotation children are
    // evaluated in the order 4, 5, 3, then FUN_06243c50(Out; child3, child5, child4).
    void DACOM_API FFxAnimatedTransform::Evaluate(unsigned long TimeA, unsigned long TimeB,
                                                  Transform* Out)
    {
        const unsigned long IdentityBits[12] = {
            0x3F800000, 0, 0, 0, 0x3F800000, 0, 0, 0, 0x3F800000, 0, 0, 0};
        std::memcpy(Out, IdentityBits, sizeof(IdentityBits));

        if (ChannelFlags_ == 0x1FFu)
            return;

        // The two times travel as float bit patterns (the interface declares them as
        // unsigned long); retail pushes the raw dwords into the child calls.
        float TimeAf;
        float TimeBf;
        std::memcpy(&TimeAf, &TimeA, sizeof(float));
        std::memcpy(&TimeBf, &TimeB, sizeof(float));

        if ((ChannelFlags_ & 0x38u) != 0x38u)
        {
            FAnimatedSingleRamp* const Child4 = Children_[4];
            FAnimatedSingleRamp* const Child5 = Children_[5];
            FAnimatedSingleRamp* const Child3 = Children_[3];
            float Rotate4;
            float Rotate5;
            float Rotate3;
            StoreAlchemyFpuFloat(Rotate4, ForwardAppearanceSingleEval(Child4, TimeAf, TimeBf));
            StoreAlchemyFpuFloat(Rotate5, ForwardAppearanceSingleEval(Child5, TimeAf, TimeBf));
            StoreAlchemyFpuFloat(Rotate3, ForwardAppearanceSingleEval(Child3, TimeAf, TimeBf));
            BuildEulerRotation3x3(&Out->Rotation.E[0][0], Rotate3, Rotate5, Rotate4);
        }

        // Scale on a local identity's diagonal; the 3DMathEngine (DAT_06257c58, created
        // on first use) composes Scale * Out.
        if ((ChannelFlags_ & 0x1C0u) != 0x1C0u)
        {
            Transform ScaleXform;
            std::memcpy(&ScaleXform, IdentityBits, sizeof(IdentityBits));
            StoreAlchemyFpuFloat(ScaleXform.Rotation.E[0][0],
                                 ForwardAppearanceSingleEval(Children_[6], TimeAf, TimeBf));
            StoreAlchemyFpuFloat(ScaleXform.Rotation.E[1][1],
                                 ForwardAppearanceSingleEval(Children_[7], TimeAf, TimeBf));
            StoreAlchemyFpuFloat(ScaleXform.Rotation.E[2][2],
                                 ForwardAppearanceSingleEval(Children_[8], TimeAf, TimeBf));

            I3DMathEngine* Math = Shared3DMath();
            Transform Composed;
            Math->MulTransform( // +0x1C
                Composed,
                ScaleXform,
                *Out);
            std::memcpy(Out, &Composed, sizeof(Composed));
        }

        // Translation children are evaluated in the order 2, 1, 0.
        if ((ChannelFlags_ & 0x7u) != 0x7u)
        {
            FAnimatedSingleRamp* const Child2 = Children_[2];
            FAnimatedSingleRamp* const Child1 = Children_[1];
            FAnimatedSingleRamp* const Child0 = Children_[0];
            float Translate2;
            float Translate1;
            StoreAlchemyFpuFloat(Translate2, ForwardAppearanceSingleEval(Child2, TimeAf, TimeBf));
            StoreAlchemyFpuFloat(Translate1, ForwardAppearanceSingleEval(Child1, TimeAf, TimeBf));
            StoreAlchemyFpuFloat(Out->Translation.x, ForwardAppearanceSingleEval(Child0, TimeAf, TimeBf));
            std::memcpy(&Out->Translation.y, &Translate1, sizeof(Translate1));
            std::memcpy(&Out->Translation.z, &Translate2, sizeof(Translate2));
        }
    }
#pragma float_control(pop)

} // namespace AlchemyImpl
