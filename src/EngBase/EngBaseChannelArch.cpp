// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "x86math.h"
#include "EngBase.h"
#include "readfile.h"

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>

static_assert(offsetof(FChannelArchetypeObject, RefCount) == 0x1C, "ChannelArchetype.RefCount");
static_assert(offsetof(FChannelArchetypeObject, FrameCount) == 0x04, "ChannelArchetype.FrameCount");
static_assert(offsetof(FChannelArchetypeObject, Duration) == 0x18, "ChannelArchetype.Duration");

static_assert(sizeof(FChannelArchetypeObject) == G_ChannelArchetypeObjectSize,
              "ChannelArchetype object size");

// DAT_0662b10c: live ChannelArchetype count (INC in FUN_0661d730, DEC in FUN_0661d750).
static unsigned long G_ChannelArchetypeLive = 0;

// FUN_0661d730: zero +0x10 / +0x14 / +0x18, vtable PTR_LAB_0662952c. No null check.
void FChannelArchetypeObject::InnerCtor()
{
    Frames = nullptr;
    Stride = 0;
    Duration = 0.0f;
    G_ChannelArchetypeLive += 1;
}

// Shared compressed-frame scratch; freed with the last channel archetype.
static unsigned char* G_ChannelFramePool = nullptr;
static unsigned long G_ChannelFramePoolSize = 0;

// FUN_0661d750. The last live archetype also frees the shared frame pool.
void FChannelArchetypeObject::Teardown()
{
    if (Frames != nullptr)
        std::free(Frames);
    G_ChannelArchetypeLive -= 1;
    if (G_ChannelArchetypeLive == 0 && G_ChannelFramePool != nullptr)
    {
        std::free(G_ChannelFramePool);
        G_ChannelFramePool = nullptr;
        G_ChannelFramePoolSize = 0;
    }
}

// FUN_0661d7a0: Frames (+0x10), Stride (+0x14); time-stamped frames skip their leading time.
const unsigned char* FChannelArchetypeObject::FramePayload(unsigned long Index) const
{
    const unsigned long Skip = (double)(SampleRate) < 0.0 ? 4 : 0;
    return Frames + Stride * Index + Skip;
}

// LAB_0661d7e0, IChannelArchetype slot 5: Flags is +0xC. No checks.
GENRESULT DACOM_API FChannelArchetypeObject::GetFlags(unsigned long* Out)
{
    *Out = Flags;
    return GR_OK;
}

// LAB_0661d800, IChannelArchetype slot 4: Duration is +0x18. No checks.
GENRESULT DACOM_API FChannelArchetypeObject::GetDuration(float* Out)
{
    *Out = Duration;
    return GR_OK;
}

// The frame-time idiom every ChannelArchetype sampler inlines (e.g. FUN_0661d820). A negative
// SampleRate means each frame carries its own time stamp.
double FChannelArchetypeObject::FrameTime(unsigned long Index) const
{
    if (!((double)(SampleRate) >= 0.0))
    {
        float Time = 0.0f;
        std::memcpy(&Time, Frames + Stride * Index, sizeof(Time));
        return (double)Time;
    }
    return (double)((unsigned long long)Index) *
           (double)(SampleRate);
}

// FUN_0661d820. No checks.
unsigned long FChannelArchetypeObject::FrameIndexAt(float Time) const
{
    if ((double)(SampleRate) > 0.0)
    {
        const unsigned long Last = FrameCount - 1;
        const long long Truncated = (long long)((double)Time /
                                                           (double)(SampleRate));
        const unsigned long Index = (unsigned long)Truncated + 1;
        return Last < Index ? Last : Index;
    }
    unsigned long Lo = 0;
    unsigned long Hi = FrameCount - 1;
    unsigned long Mid = Hi >> 1;
    if (Hi == 0)
        return Mid;
    do
    {
        if (!(FrameTime(Mid) <= (double)Time))
            Hi = Mid;
        else
            Lo = Mid + 1;
        Mid = (Hi + Lo) >> 1;
    } while (Lo < Hi);
    return Mid;
}

static unsigned long ChannelStrideFromHeader(unsigned long Flags, float SampleRate)
{
    unsigned long Stride = 0;
    if ((Flags & G_ChannelFlagScalar) != 0)
        Stride = 4;
    if ((Flags & G_ChannelFlagPosition) != 0)
        Stride += 0xC;
    if ((Flags & G_ChannelFlagQuat) != 0)
        Stride += 0x10;
    if ((Flags & G_ChannelFlagScalarOnly) != 0)
        Stride = 4;
    if ((Flags & G_ChannelFlagZeroPos) != 0)
        Stride += 0;
    if ((Flags & G_ChannelFlagIdentQuat) != 0)
        Stride += 0;
    if ((Flags & G_ChannelFlagCompQuat3) != 0)
        Stride += 6;
    if ((Flags & G_ChannelFlagCompAxis) != 0)
        Stride += 6;
    if ((Flags & G_ChannelFlagCompQuat4) != 0)
        Stride += 8;
    if (SampleRate < 0.0f)
        Stride += 4;
    return Stride;
}

// LAB_0661d8d0, IChannelArchetype slot 3 (Load). This is not tested.
GENRESULT DACOM_API FChannelArchetypeObject::Load(IFileSystem* FileSystem)
{
    if (FileSystem == nullptr)
        return GR_INVALID_PARMS;
    IFileSystem* Fs = AsFileSystem(FileSystem);

    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, G_NameHeader, G_NameDos);
    const int HeaderHandle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    if (HeaderHandle == -1)
        return (GENRESULT)0xFFFFFFFA;
    unsigned long Header[3];
    unsigned long BytesRead = 0;
    const int HeaderOk = Fs->Read((unsigned long)HeaderHandle, Header, 0xC,
                                  &BytesRead, 0); // +0x14
    Fs->CloseFile((unsigned long)HeaderHandle); // +0x10
    if (HeaderOk == 0 || BytesRead != 0xC)
        return (GENRESULT)0xFFFFFFFA;

    FrameCount = Header[0];
    std::memcpy(&SampleRate, &Header[1], sizeof(SampleRate));
    Flags = Header[2];
    Stride = ChannelStrideFromHeader(Flags, SampleRate);

    Desc.DataPath = G_NameFrames;
    const int FramesHandle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    if (FramesHandle == -1)
        return (GENRESULT)0xFFFFFFFA;
    const unsigned long FileSize = Fs->GetSize((unsigned long)FramesHandle, 0); // +0x28
    if (Flags != G_ChannelFlagScalarOnly && FileSize != FrameCount * Stride)
    {
        Fs->CloseFile((unsigned long)FramesHandle); // +0x10
        return GR_GENERIC;
    }
    if ((Flags & (G_ChannelFlagZeroPos | G_ChannelFlagIdentQuat | G_ChannelFlagCompQuat3 | G_ChannelFlagCompAxis)) != 0 || (Flags & G_ChannelFlagCompQuat4) != 0)
    {
        if (FileSize > G_ChannelFramePoolSize)
        {
            G_ChannelFramePool = (unsigned char*)(std::realloc(G_ChannelFramePool, FileSize));
            G_ChannelFramePoolSize = FileSize;
        }
        Frames = G_ChannelFramePool;
    }
    else
    {
        Frames = (unsigned char*)(std::malloc(FileSize));
    }
    if (Frames == nullptr)
        return GR_OUT_OF_MEMORY;
    const int FramesOk = Fs->Read((unsigned long)FramesHandle, Frames, FileSize,
                                  &BytesRead, 0); // +0x14
    Fs->CloseFile((unsigned long)FramesHandle); // +0x10
    if (FramesOk == 0 || BytesRead != FileSize)
        return (GENRESULT)0xFFFFFFFA;

    DecompressFrames();
    const unsigned long Last = FrameCount - 1;
    if ((double)(SampleRate) < 0.0)
    {
        float Time = 0.0f;
        std::memcpy(&Time, Frames + Stride * Last, sizeof(Time));
        Duration = Time;
    }
    else
    {
        Duration = (float)((double)((unsigned long long)Last) *
                                            (double)(SampleRate));
    }
    return GR_OK;
}

// One output dword of FUN_0661db70.
static void ChannelPutDword(unsigned char*& Dst, const void* Value)
{
    std::memcpy(Dst, Value, 4);
    Dst += 4;
}

// .data:0662a3c0 (from the PE bytes): the __FILE__ every FDUMP of FUN_0661db70
// (FChannelArchetypeObject::DecompressFrames) pushes.
static const char* const G_FdumpFileChannelArch =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Channel\\channel_arch.cpp";

static void ChannelPutFloat(unsigned char*& Dst, float Value)
{
    ChannelPutDword(Dst, &Value);
}

// A packed quaternion component times DAT_0662955c (1/32767). A 16-bit integer times a float is
// exact in a double, so this is the value the FPU holds.
static double ChannelUnpackShort(const unsigned char* Src)
{
    short Packed = 0;
    std::memcpy(&Packed, Src, sizeof(Packed));
    return (double)Packed * (double)G_ShortToUnit;
}

// FUN_0661db70 (called only from ChannelArchetype::Load).
void FChannelArchetypeObject::DecompressFrames()
{
    const unsigned long Flags = this->Flags;
    if ((Flags & (G_ChannelFlagZeroPos | G_ChannelFlagIdentQuat | G_ChannelFlagCompQuat3 | G_ChannelFlagCompAxis)) == 0 && (Flags & G_ChannelFlagCompQuat4) == 0)
        return;
    if ((Flags & (G_ChannelFlagScalar | G_ChannelFlagScalarOnly)) != 0)
    {
        EngBaseFdump(kFdumpFatal, G_FdumpFileChannelArch, 0x13C, "FATAL",
                     "Cannot uncompress data other than vectors and quaternions");
        return;
    }
    const bool bStamp = (double)(SampleRate) < 0.0;
    const bool bPos = (Flags & G_ChannelFlagPosition) != 0;
    const bool bZeroPos = (Flags & G_ChannelFlagZeroPos) != 0;
    const bool bHasPos = bPos || bZeroPos;
    if (bPos && bZeroPos)
    {
        EngBaseFdump(kFdumpFatal, G_FdumpFileChannelArch, 0x15F, "FATAL",
                     "Inconsistent specification for vector data");
        return;
    }
    const bool bQuat = (Flags & G_ChannelFlagQuat) != 0;
    const bool bIdent = (Flags & G_ChannelFlagIdentQuat) != 0;
    const bool bComp3 = (Flags & G_ChannelFlagCompQuat3) != 0;
    const bool bAxis = (Flags & G_ChannelFlagCompAxis) != 0;
    const bool bComp4 = (Flags & G_ChannelFlagCompQuat4) != 0;
    const bool bHasQuat = bQuat || bIdent || bComp3 || bAxis || bComp4;
    const bool bAnyPacked = bComp3 || bAxis || bComp4;
    if ((bQuat && bIdent) || ((bQuat || bIdent) && bAnyPacked) || (bComp3 && bAxis) ||
        (bComp3 && bComp4) || (bAxis && bComp4))
    {
        EngBaseFdump(kFdumpFatal, G_FdumpFileChannelArch, 0x199, "FATAL",
                     "Inconsistent specification for quaternion data");
        return;
    }

    unsigned long Words = bStamp ? 1 : 0;
    if (bHasPos)
        Words += 3;
    if (bHasQuat)
        Words += 4;
    unsigned char* const Out =
        (unsigned char*)(std::malloc(FrameCount * Words * 4));
    this->Flags = this->Flags & ~G_ChannelCompressMask;
    if (bHasPos)
        this->Flags = this->Flags | G_ChannelFlagPosition;
    if (bHasQuat)
        this->Flags = this->Flags | G_ChannelFlagQuat;
    Stride = ChannelStrideFromHeader(this->Flags, SampleRate);

    const unsigned char* Src = Frames;
    unsigned char* Dst = Out;
    for (unsigned long Frame = 0; Frame < FrameCount; ++Frame)
    {
        if (bStamp)
        {
            ChannelPutDword(Dst, Src);
            Src += 4;
        }
        if (bHasPos)
        {
            if (bPos)
            {
                ChannelPutDword(Dst, Src);
                ChannelPutDword(Dst, Src + 4);
                ChannelPutDword(Dst, Src + 8);
                Src += 0xC;
            }
            else if (bZeroPos)
            {
                ChannelPutFloat(Dst, 0.0f);
                ChannelPutFloat(Dst, 0.0f);
                ChannelPutFloat(Dst, 0.0f);
            }
            else
            {
                EngBaseFdump(kFdumpFatal, G_FdumpFileChannelArch, 0x1F0, "FATAL",
                             "Cannot generate vector data");
            }
        }
        if (!bHasQuat)
            continue;
        if (bQuat)
        {
            ChannelPutDword(Dst, Src);
            ChannelPutDword(Dst, Src + 4);
            ChannelPutDword(Dst, Src + 8);
            ChannelPutDword(Dst, Src + 0xC);
            Src += 0x10;
        }
        else if (bIdent)
        {
            ChannelPutFloat(Dst, 1.0f);
            ChannelPutFloat(Dst, 0.0f);
            ChannelPutFloat(Dst, 0.0f);
            ChannelPutFloat(Dst, 0.0f);
        }
        else if (bComp3)
        {
            // X and Y stay on the FPU, Z is rounded to float.
            const double X = ChannelUnpackShort(Src);
            const double Y = ChannelUnpackShort(Src + 2);
            const float Z = (float)(ChannelUnpackShort(Src + 4));
            Src += 6;
            const double Sum = (double)Z * (double)Z + Y * Y + X * X;
            const float SumStored = (float)Sum;
            double W = 0.0;
            if (Sum < 1.0)
                W = std::sqrt(1.0 - (double)SumStored);
            ChannelPutFloat(Dst, (float)W);
            ChannelPutFloat(Dst, (float)X);
            ChannelPutFloat(Dst, (float)Y);
            ChannelPutFloat(Dst, Z);
        }
        else if (bAxis)
        {
            // Z is stored as float and the FPU value multiplied by it.
            const double X = ChannelUnpackShort(Src);
            const double Y = ChannelUnpackShort(Src + 2);
            const double ZFull = ChannelUnpackShort(Src + 4);
            const float Z = (float)ZFull;
            Src += 6;
            const double Sum = ZFull * (double)Z + Y * Y + X * X;
            if (Sum > 0.0)
            {
                const double Length = std::sqrt(Sum);
                const double Sine = std::sin((double)(3.1415927f) * Length * 0.5);
                const float SineStored = (float)Sine;
                const float Scale = (float)(Sine / Length);
                const double W = std::sqrt(1.0 - (double)SineStored *
                                                     (double)SineStored);
                ChannelPutFloat(Dst, (float)W);
                ChannelPutFloat(Dst, (float)(X * (double)Scale));
                ChannelPutFloat(Dst, (float)(Y * (double)Scale));
                ChannelPutFloat(Dst, (float)((double)Scale *
                                                        (double)Z));
            }
            else
            {
                ChannelPutFloat(Dst, 1.0f);
                ChannelPutFloat(Dst, (float)X);
                ChannelPutFloat(Dst, (float)Y);
                ChannelPutFloat(Dst, Z);
            }
        }
        else if (bComp4)
        {
            // X and Y stay on the FPU, Z is rounded to float, W is stored as float and the FPU
            // value multiplied by it. Only the first output uses the unrounded Mid.
            const double X = ChannelUnpackShort(Src);
            const double Y = ChannelUnpackShort(Src + 2);
            const float Z = (float)(ChannelUnpackShort(Src + 4));
            const double WFull = ChannelUnpackShort(Src + 6);
            const float W = (float)WFull;
            Src += 8;
            const double Sum = WFull * (double)W +
                               (double)Z * (double)Z + Y * Y + X * X;
            const double Mid = (3.0 - Sum) * 0.5;
            const float MidStored = (float)Mid;
            ChannelPutFloat(Dst, (float)(Mid * X));
            ChannelPutFloat(Dst, (float)((double)MidStored * Y));
            ChannelPutFloat(Dst, (float)((double)MidStored *
                                                    (double)Z));
            ChannelPutFloat(Dst, (float)((double)MidStored *
                                                    (double)W));
        }
        else
        {
            EngBaseFdump(kFdumpFatal, G_FdumpFileChannelArch, 0x28E, "FATAL",
                         "Cannot generate quaternion data");
        }
    }
    Frames = Out;
}

// LAB_0661e1a0, IChannelArchetype slot 6.
GENRESULT DACOM_API FChannelArchetypeObject::Sample(float Time, const unsigned char** OutA,
                                                     const unsigned char** OutB, float* Alpha)
{
    float Duration = 0.0f;
    GetDuration(&Duration);
    if (!((double)Time >= 0.0))
        return GR_INVALID_PARMS;
    if (!(Time <= Duration))
        return GR_INVALID_PARMS;
    const unsigned long Hi = FrameIndexAt(Time);
    const double TimeHi = FrameTime(Hi);
    if (!(TimeHi > (double)Time))
    {
        const unsigned char* Payload = FramePayload(Hi);
        *OutB = Payload;
        *OutA = Payload;
        *Alpha = 0.0f;
        return GR_OK;
    }
    const unsigned long Lo = Hi - 1;
    const double TimeLo = FrameTime(Lo);
    *Alpha = (float)(((double)Time - TimeLo) / (TimeHi - TimeLo));
    *OutA = FramePayload(Lo);
    *OutB = FramePayload(Hi);
    return GR_OK;
}

// LAB_0661e2e0, IChannelArchetype slot 8: the first or the last frame.
GENRESULT DACOM_API FChannelArchetypeObject::GetFrame(unsigned char bLast, const unsigned char** Out)
{
    long Index = 0;
    if (bLast != 0)
    {
        Index = (long)(FrameCount - 1);
        if (Index < 0)
            return GR_GENERIC;
    }
    *Out = FramePayload((unsigned long)Index);
    return GR_OK;
}

// LAB_0661e310, IChannelArchetype slot 10.
GENRESULT DACOM_API FChannelArchetypeObject::CollectAt(float Time, float* OutKeys,
                                                        unsigned long Capacity, unsigned long* Count)
{
    *Count = 0;
    if (Flags != G_ChannelFlagScalarOnly)
        return GR_GENERIC;
    GENRESULT Result = GR_OK;
    for (unsigned long Index = 0; Index < FrameCount; ++Index)
    {
        if (FrameTime(Index) == (double)Time)
        {
            if (*Count < Capacity)
            {
                OutKeys[*Count * 2] = Time;
                const unsigned long Key = RelocatedKey(Index);
                std::memcpy(OutKeys + *Count * 2 + 1, &Key, sizeof(Key));
            }
            else
                Result = GR_GENERIC;
            *Count += 1;
        }
    }
    return Result;
}

// FUN_0661e3d0: the payload dword is an offset past the end of the frame block (FrameCount
// at +4). No checks.
unsigned long FChannelArchetypeObject::RelocatedKey(unsigned long Index) const
{
    const unsigned char* Payload = FramePayload(Index);
    unsigned long Reloc = 0;
    std::memcpy(&Reloc, Payload, sizeof(Reloc));
    return Stride * FrameCount + (unsigned long)(Frames) +
           Reloc;
}

// .rdata:06629560 / 06629564 (PE bytes 95 BF D6 B3 / 95 BF D6 33): -1e-7f and +1e-7f.
static const float G_ChannelTimeEpsNeg = -1.0000000116860974e-07f;
static const float G_ChannelTimeEpsPos = 1.0000000116860974e-07f;

// LAB_0661e400, IChannelArchetype slot 9.
GENRESULT DACOM_API FChannelArchetypeObject::CollectRange(float Time0, float Time1, float* OutKeys,
                                                          unsigned long Capacity, unsigned long* Count)
{
    *Count = 0;
    if (Flags != G_ChannelFlagScalarOnly)
        return GR_GENERIC;
    float Duration = 0.0f;
    GetDuration(&Duration);
    const float DurEps = Duration + G_ChannelTimeEpsPos;
    if (!(Time0 >= G_ChannelTimeEpsNeg) || !(Time0 <= DurEps))
        return GR_INVALID_PARMS;
    if (!(Time1 >= G_ChannelTimeEpsNeg) || !(Time1 <= DurEps))
        return GR_INVALID_PARMS;
    if (Time0 == Time1)
        return GR_INVALID_PARMS;

    long Start = 0;
    std::memcpy(&Start, &Time0, sizeof(Start));
    long Stop = 0;
    std::memcpy(&Stop, &DurEps, sizeof(Stop));
    long Step = 1;
    if (Time0 < Time1)
    {
        for (long Index = 0; Index < (long)(FrameCount); ++Index)
        {
            const double FrameTime = this->FrameTime((unsigned long)Index);
            if (FrameTime <= (double)Time0)
                Start = Index;
            if (!(FrameTime < (double)Time1))
                break;
            Stop = Index;
        }
        Start += 1;
        Stop += 1;
    }
    else
    {
        Step = -1;
        for (long Index = (long)(FrameCount) - 1; Index >= 0; --Index)
        {
            const double FrameTime = this->FrameTime((unsigned long)Index);
            if (FrameTime >= (double)Time0)
                Start = Index;
            if (!(FrameTime > (double)Time1))
                break;
            Stop = Index;
        }
        Start -= 1;
        Stop -= 1;
    }

    while (Start != Stop)
    {
        if (*Count < Capacity)
        {
            OutKeys[*Count * 2] =
                (float)(this->FrameTime((unsigned long)Start));
            const unsigned long Key = RelocatedKey((unsigned long)Start);
            std::memcpy(OutKeys + *Count * 2 + 1, &Key, sizeof(Key));
        }
        Start += Step;
        *Count += 1;
    }
    return GR_OK;
}

// LAB_0661e670, IChannelArchetype slot 7.
GENRESULT DACOM_API FChannelArchetypeObject::SamplePair(float Time, const unsigned char** OutLo,
                                                         const unsigned char** OutHi, float* TimeLo,
                                                         float* TimeHi, float* Alpha)
{
    float Duration = 0.0f;
    GetDuration(&Duration);
    if (!((double)Time >= 0.0))
        return GR_INVALID_PARMS;
    if (!(Time <= Duration))
        return GR_INVALID_PARMS;
    const unsigned long Hi = FrameIndexAt(Time);
    const double FrameTime = this->FrameTime(Hi);
    if (!(FrameTime > (double)Time))
    {
        const unsigned char* Payload = FramePayload(Hi);
        *OutHi = Payload;
        *OutLo = Payload;
        const double Again = this->FrameTime(Hi);
        *TimeHi = (float)Again;
        *TimeLo = (float)Again;
        *Alpha = 0.0f;
        return GR_OK;
    }
    const unsigned long Lo = Hi - 1;
    *TimeLo = (float)(this->FrameTime(Lo));
    *TimeHi = (float)FrameTime;
    *Alpha = (float)(((double)Time - (double)(*TimeLo)) /
                                (FrameTime - (double)(*TimeLo)));
    *OutLo = FramePayload(Lo);
    *OutHi = FramePayload(Hi);
    return GR_OK;
}

// LAB_0661e800. No null checks of This / Desc / Out.
GENRESULT DACOM_API ChannelArchetypeFactoryCreateInstance(FDacomFactory* This, AGGDESC* Desc,
                                                         void** Out)
{
    if (Desc->size != G_AggDescSizeChannelArchetype ||
        std::strcmp(Desc->interface_name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    FChannelArchetypeObject* Object =
        (FChannelArchetypeObject*)(std::calloc(1, sizeof(FChannelArchetypeObject)));
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    new (Object) FChannelArchetypeObject();
    Object->InnerCtor();
    Object->RefCount = 1;
    const int HookResult = EngBaseAggregationHook(Desc);
    if (HookResult != 0)
    {
        Object->Teardown();
        std::free(Object);
        *Out = nullptr;
        return (GENRESULT)HookResult;
    }
    *Out = Object;
    return (GENRESULT)HookResult;
}

// LAB_0661e8e0: the {iid, offset} table at 066295a4 has the one entry.
GENRESULT DACOM_API FChannelArchetypeObject::QueryInterface(const char* Iid, void** Out)
{
    if (std::strcmp(Iids::ChannelArchetype, Iid) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }
    AddRef();
    *Out = this;
    return GR_OK;
}

// LAB_0661e990: count at +0x1C. No null check.
unsigned long DACOM_API ChannelArchetypeAddRef(FChannelArchetypeObject* This)
{
    This->RefCount += 1;
    return This->RefCount;
}

// FUN_0661e9a0: at zero the count is reset to 1 before teardown. No null check.
unsigned long DACOM_API FChannelArchetypeObject::Release()
{
    if (RefCount != 0)
        RefCount -= 1;
    if (RefCount == 0)
    {
        RefCount = 1;
        Teardown();
        std::free(this);
        return 0;
    }
    return RefCount;
}

unsigned long DACOM_API FChannelArchetypeObject::AddRef()
{
    return ChannelArchetypeAddRef(this);
}
