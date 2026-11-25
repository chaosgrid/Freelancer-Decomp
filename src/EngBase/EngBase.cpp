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

#pragma comment(lib, "dacom.lib")

// Typed math calls preserve the caller's float buffers and their spill boundaries.
static GENRESULT MathMulMatrix(I3DMathEngine* Math, float* Dst, const float* A, const float* B)
{
    return Math->MulMatrix(*(FMatrix*)Dst, *(const FMatrix*)A, *(const FMatrix*)B);
}

static GENRESULT MathTransformMatrix(I3DMathEngine* Math, float* Dst, const float* A, const float* B)
{
    return Math->TransformMatrix(*(FVector*)Dst, *(const FMatrix*)A, *(const FVector*)B);
}

static GENRESULT MathMatrixToQuat(I3DMathEngine* Math, float* Dst, const float* A)
{
    return Math->MatrixToQuaternion(*(FQuaternion*)Dst, *(const FMatrix*)A);
}

GENRESULT MathQuatToMatrix(I3DMathEngine* Math, float* Dst, const float* A)
{
    return Math->QuaternionToMatrix(*(FMatrix*)Dst, *(const FQuaternion*)A);
}

static GENRESULT MathQuatSlerp(I3DMathEngine* Math, float* Dst, const float* A, const float* B, float C)
{
    return Math->QuatSlerp(*(FQuaternion*)Dst, *(const FQuaternion*)A, *(const FQuaternion*)B, C);
}
static const char* const G_FdumpFmtNotice = "%s(%d) : NOTICE:General:%s";
static const char* const G_FdumpFmtError = "%s(%d) : ERROR:General:%s";
static const char* const G_FdumpFmtWarning = "%s(%d) : WARNING:General:%s";
// Pushed with 0x100000 (e.g. 06615567 / 0661556c).
static const char* const G_FdumpFmtFatal = "%s(%d) : FATAL:General:%s";
static_assert(sizeof(FAggDesc18) == 0x18, "AGGDESC 0x18");
static_assert(sizeof(FAggDescC) == 0xC, "AGGDESC 0xC");

// Preserve imported FDUMP formatting and call behavior.
void EngBaseFdump(int Flags, const char* File, int Line, const char* Kind, const char* Message)
{
    const char* Format = G_FdumpFmtNotice;
    if (Flags == kFdumpError)
        Format = G_FdumpFmtError;
    else if (Flags == kFdumpWarning)
        Format = G_FdumpFmtWarning;
    else if (Flags == kFdumpFatal)
        Format = G_FdumpFmtFatal;
    (void)Kind;
    FDUMP(Flags, Format, File, Line, Message);
}

// FUN_06611d80 (cdecl, variadic): _vsnprintf(Buffer, 0x2000, Format, args) and return Buffer.
char* FormatFdumpMessage(char* Buffer, const char* Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    _vsnprintf(Buffer, 0x2000, Format, Args);
    va_end(Args);
    return Buffer;
}

// FUN_0661d700: always 0; factories destroy the object if nonzero.
int DACOM_API EngBaseAggregationHook(void* Desc)
{
    (void)Desc;
    return 0;
}

GENRESULT DACOM_API FDacomFactory::QueryInterface(const char* Iid, void** Out)
{
    if (Out != nullptr)
        *Out = nullptr;
    if (Iid == nullptr || Out == nullptr)
        return GR_INTERFACE_UNSUPPORTED;
    if (!Iids::Equal(Iid, Iids::ComponentFactory))
        return GR_INTERFACE_UNSUPPORTED;
    *Out = this;
    AddRef();
    return GR_OK;
}

unsigned long DACOM_API FDacomFactory::AddRef()
{
    RefCount += 1;
    return RefCount;
}

unsigned long DACOM_API FDacomFactory::Release()
{
    if (RefCount != 0)
        RefCount -= 1;
    const unsigned long Count = RefCount;
    if (Count == 0)
    {
        RefCount = 1;
        std::free(this);
        return 0;
    }
    return Count;
}

// The hardpoint IEngineComponent table's empty slots (06612160 RET 8 for DestroyInstance and
// Notify, 06626ce0 RET 0Ch for UpdateInstance) are a bare RET n and leave EAX unset; these
// return GR_GENERIC in its place.
GENRESULT DACOM_API StubRet8(void* This, void* A)
{
    (void)This;
    (void)A;
    return GR_GENERIC;
}

GENRESULT DACOM_API StubRetC(void* This, void* A, void* B)
{
    (void)This;
    (void)A;
    (void)B;
    return GR_GENERIC;
}

// EngBase plumbs the FileSystem component around as void*; this is the one place that
// states what it really is, so every call below goes through ReadFile's published ABI.
IFileSystem* AsFileSystem(void* FileSystem)
{
    return (IFileSystem*)FileSystem;
}

bool IsTypedChannelType(unsigned long Type)
{
    return Type == 1 || Type == 2 || Type == 4 || Type == 6 || Type == 8;
}

static unsigned long TypedKindFromType(unsigned long Type)
{
    if (Type == 1 || Type == 2)
        return G_MixKindScalar;
    if (Type == 3)
        return 2;
    if (Type == 4)
        return G_MixKindQuat;
    if (Type == 5)
        return G_MixKindVec3;
    if (Type == 6 || Type == 8)
        return G_MixKindPosQuat;
    return G_MixKindScalar;
}


static unsigned long DACOM_API TypedBind(FTypedChannel* This, const FPlayHeader* Header)
{
    This->LoopFlags = Header->LoopFlags;
    This->Rate = Header->Rate;
    This->Scale = Header->Scale;
    This->TranslationScale = Header->TranslationScale;
    This->SetTime(Header->StartTime);
    This->PlayFlags &= ~3UL;
    return This->PlayFlags;
}

static void TypedCopyVec3(float* Dest, const float* Source)
{
    Dest[0] = Source[0];
    Dest[1] = Source[1];
    Dest[2] = Source[2];
}

static void TypedStoreIdentityMatrix(float* Matrix)
{
    Matrix[0] = 1.0f;
    Matrix[1] = 0.0f;
    Matrix[2] = 0.0f;
    Matrix[3] = 0.0f;
    Matrix[4] = 1.0f;
    Matrix[5] = 0.0f;
    Matrix[6] = 0.0f;
    Matrix[7] = 0.0f;
    Matrix[8] = 1.0f;
}

unsigned long FPosQuatTypedChannel::BindHeader(const FPlayHeader* Header)
{
    TypedBind(this, Header);
    if (Kind != 1)
    {
        TypedStoreIdentityMatrix(Matrix);
        Delta[0] = 0.0f;
        Delta[1] = 0.0f;
        Delta[2] = 0.0f;
        return 0;
    }
    const float* Block = Header->Transform;
    FEngineInstance* Instance = (FEngineInstance*)((size_t)(this->Instance));
    if (Header->StartTime == 0.0f || (Header->LoopFlags & 0x40) != 0)
    {
        if (Block != nullptr)
        {
            std::memcpy(Matrix, Block, sizeof(Matrix));
            TypedCopyVec3(Delta, Block + 9);
            unsigned long Bits = 0;
            std::memcpy(&Bits, &Block[9], sizeof(Bits));
            return Bits;
        }
        const float* Orient = G_AnimEngine->GetInstanceOrientation(Instance);
        std::memcpy(Matrix, Orient, sizeof(Matrix));
        float* Position = G_AnimEngine->GetInstancePosition(Instance);
        TypedCopyVec3(Delta, Position);
        return (unsigned long)((size_t)Position);
    }
    const unsigned char* KeyA = nullptr;
    const unsigned char* KeyB = nullptr;
    float Alpha = 0.0f;
    Archetype->Sample(Time, &KeyA, &KeyB, &Alpha);
    float Pose[7];
    Interpolate(Pose, (const float*)KeyA,
                                           (const float*)KeyB,
                                           (double)Alpha);
    const double Factor = (double)(TranslationScale) * (double)(Scale);
    float Pos[3];
    Pos[0] = (float)((double)(Pose[0]) * Factor);
    Pos[1] = (float)((double)(Pose[1]) * Factor);
    Pos[2] = (float)((double)(Pose[2]) * Factor);
    I3DMathEngine* Math = Acquire3DMathEngine();
    float Xformed[3];
    if (Block != nullptr)
    {
        std::memcpy(Matrix, Block, sizeof(Matrix));
        MathTransformMatrix(Math, Xformed, Matrix, Pos);
        Delta[0] = Block[9] - Xformed[0];
        Delta[1] = Block[10] - Xformed[1];
        Delta[2] = Block[11] - Xformed[2];
    }
    else
    {
        float QuatMatrix[9];
        MathQuatToMatrix(Math, QuatMatrix, Pose + 3);
        float Transposed[9];
        Transposed[0] = QuatMatrix[0];
        Transposed[1] = QuatMatrix[3];
        Transposed[2] = QuatMatrix[6];
        Transposed[3] = QuatMatrix[1];
        Transposed[4] = QuatMatrix[4];
        Transposed[5] = QuatMatrix[7];
        Transposed[6] = QuatMatrix[2];
        Transposed[7] = QuatMatrix[5];
        Transposed[8] = QuatMatrix[8];
        const float* Orient = G_AnimEngine->GetInstanceOrientation(Instance);
        float Composed[9];
        MathMulMatrix(Math, Composed, Orient, Transposed);
        std::memcpy(Matrix, Composed, sizeof(Matrix));
        MathTransformMatrix(Math, Xformed, Matrix, Pos);
        const float* Position = G_AnimEngine->GetInstancePosition(Instance);
        Delta[0] = Position[0] - Xformed[0];
        Delta[1] = Position[1] - Xformed[1];
        Delta[2] = Position[2] - Xformed[2];
    }
    unsigned long Bits = 0;
    std::memcpy(&Bits, &Delta[1], sizeof(Bits));
    return Bits;
}

static unsigned long DACOM_API TypedGetFlags(FTypedChannel* This)
{
    if (This == nullptr || This->Archetype == nullptr)
        return 0;
    return This->Archetype->Flags;
}

static unsigned long ChannelPayloadBytesFromFlags(unsigned long Flags)
{
    unsigned long Bytes = 0;
    if ((Flags & G_ChannelFlagScalar) != 0)
        Bytes = 4;
    if ((Flags & G_ChannelFlagPosition) != 0)
        Bytes += 0xC;
    if ((Flags & G_ChannelFlagQuat) != 0)
        Bytes += 0x10;
    if ((Flags & G_ChannelFlagScalarOnly) != 0)
        Bytes = 4;
    if ((Flags & G_ChannelFlagCompQuat3) != 0)
        Bytes += 6;
    if ((Flags & G_ChannelFlagCompAxis) != 0)
        Bytes += 6;
    if ((Flags & G_ChannelFlagCompQuat4) != 0)
        Bytes += 8;
    return Bytes;
}

static unsigned long DACOM_API TypedBindMap(FTypedChannel* This, unsigned long* Map, void* Bind)
{
    This->Kind = Map[0];
    This->Sink = Map[1];
    This->SinkContext = Map[2];
    This->Instance = Map[3];
    This->Bind = Bind;
    if (This->Owner != nullptr)
        return 1;
    unsigned long Flags = 0;
    This->Archetype->GetFlags(&Flags);
    const unsigned long Bytes = ChannelPayloadBytesFromFlags(Flags);
    if (This->Kind == 0 && G_AnimEngine != nullptr)
    {
        unsigned long* Relative = G_AnimEngine->GetInstanceRelative(
            (void*)(This->Instance));
        unsigned long MixKind = 0;
        if (Relative != nullptr)
        {
            const unsigned long Case = Relative[0] - 1;
            if (Case == 0 || Case == 1)
                MixKind = 1;
            else if (Case == 2)
                MixKind = 2;
            else if (Case == 4)
                MixKind = 3;
            else if (Case == 3)
                MixKind = 4;
            else if (Case == 5)
                MixKind = 7;
        }
        if (MixKind * 4 == Bytes)
            return 1;
        return 0;
    }
    if (This->Kind == 2)
        return Flags == G_ChannelFlagScalarOnly ? 1 : 0;
    return Bytes == 0x1C ? 1 : 0;
}

unsigned long FScalarTypedChannel::BindMap(unsigned long* Map, void* Bind)
{
    const unsigned long bBound = TypedBindMap(this, Map, Bind);
    if (bBound != 0)
    {
        unsigned long* Relative = (unsigned long*)(
            G_AnimEngine->GetInstanceRelative((void*)(Map[3])));
        if (Relative != nullptr)
            JointType = Relative[0];
    }
    return bBound;
}

static unsigned long DACOM_API TypedNop(FTypedChannel* This)
{
    (void)This;
    return 0;
}

static unsigned long DACOM_API TypedReturnZero(FTypedChannel* This)
{
    (void)This;
    return 0;
}

unsigned long FTypedChannel::GetType()
{
    return 8;
}

unsigned long FScalarTypedChannel::GetType()
{
    FTypedChannel* This = this;
    (void)This;
    return 1;
}

unsigned long FVectorTypedChannel::GetType()
{
    FTypedChannel* This = this;
    (void)This;
    return 2;
}

unsigned long FQuatTypedChannel::GetType()
{
    FTypedChannel* This = this;
    (void)This;
    return 4;
}

unsigned long FScalarTypedChannel::InterpolatePose(float* Out, const float* A, const float* B, double Alpha)
{

    const double Left = A[0];
    const double Right = B[0];
    double Sum = 0.0;

    if (JointType == 2)
    {
        const double Delta = Right - Left;
        const double NegPi = (double)(-3.14159274f);
        const double Pi = (double)(3.14159274f);
        const double TwoPi = (double)(6.28318548f);
        double Target = Right;
        if (Delta < NegPi)
            Target = Right + TwoPi;
        else if (Delta > Pi)
            Target = Right - TwoPi;
        Sum = (Target - Left) * Alpha + Left;
    }
    else
        Sum = (Right - Left) * Alpha + Left;
    Out[0] = (float)Sum;
    Out[0] = Scale * Out[0];

    return (unsigned long)((size_t)Out);
}

unsigned long FQuatTypedChannel::InterpolatePose(float* Out, const float* Left, const float* Right, double Alpha)
{
    float RightCopy[4];
    RightCopy[0] = Right[0];
    RightCopy[1] = Right[1];
    RightCopy[2] = Right[2];
    RightCopy[3] = Right[3];
    float LeftCopy[4];
    LeftCopy[0] = Left[0];
    LeftCopy[1] = Left[1];
    LeftCopy[2] = Left[2];
    LeftCopy[3] = Left[3];
    I3DMathEngine* Math = Acquire3DMathEngine();
    float Result[4];
    MathQuatSlerp(Math, Result, LeftCopy, RightCopy, (float)Alpha);
    Out[0] = Result[0];
    Out[1] = Result[1];
    Out[2] = Result[2];
    Out[3] = Result[3];
    unsigned long ScaleBits = 0;
    std::memcpy(&ScaleBits, &Scale, sizeof(ScaleBits));
    if (ScaleBits == 0x3F800000)
        return (unsigned long)((size_t)Out);
    Result[0] = 1.0f;
    Result[1] = 0.0f;
    Result[2] = 0.0f;
    Result[3] = 0.0f;
    MathQuatSlerp(Math, LeftCopy, Result, Out, Scale);
    Out[0] = LeftCopy[0];
    Out[1] = LeftCopy[1];
    Out[2] = LeftCopy[2];
    Out[3] = LeftCopy[3];
    return (unsigned long)((size_t)Out);
}

static void TypedStoreMatrix3x3(float* Dest, const float* M00)
{
    if (Dest == nullptr || M00 == nullptr)
        return;
    unsigned long Index = 0;
    while (Index < 9)
    {
        Dest[Index] = M00[Index];
        Index += 1;
    }
}

unsigned long FVectorTypedChannel::InterpolatePose(float* Out, const float* A, const float* B, double Alpha)
{
    const double D0 = (double)(B[0]) - (double)(A[0]);
    const double D1 = (double)(B[1]) - (double)(A[1]);
    const float D2 = (float)((double)(B[2]) - (double)(A[2]));
    const float Blend = (float)Alpha;
    const float T0 = (float)(D0 * (double)Blend);
    const double T1 = D1 * (double)Blend;
    const double T2 = (double)D2 * (double)Blend;
    const float Out0 = (float)((double)T0 + (double)(A[0]));
    const float Out1 = (float)(T1 + (double)(A[1]));
    const float Out2 = (float)(T2 + (double)(A[2]));
    Out[0] = Out0;
    Out[1] = Out1;
    Out[2] = Out2;
    const double Factor = (double)(TranslationScale) * (double)(Scale);
    Out[0] = (float)(Factor * (double)(Out[0]));
    Out[1] = (float)(Factor * (double)(Out[1]));
    Out[2] = (float)(Factor * (double)(Out[2]));
    return (unsigned long)((size_t)Out);
}

// The base table's empty slots (066294ac): LAB_0661b820 / 0661b810 / 06612160 / 0661ad30 are a
// bare RET n and leave EAX unset (void in the original); the port returns 0 in its place.
unsigned long FTypedChannel::Apply(const float* Left, const float* Right, double Alpha)
{
    (void)Left;
    (void)Right;
    (void)Alpha;
    return 0;
}

static unsigned long DACOM_API NoOpTypedChannelInterpolate(FTypedChannel* This, float* Out, const float* Left,
                                             const float* Right, double Alpha)
{
    (void)This;
    (void)Out;
    (void)Left;
    (void)Right;
    (void)Alpha;
    return 0;
}

unsigned long FTypedChannel::CopySampled(float* Out)
{
    void* A = Out;
    (void)A;
    return 0;
}

unsigned long FTypedChannel::TouchMapped(void* A, void* B)
{
    (void)A;
    (void)B;
    return 0;
}

// LAB_06612170: slot 0x38 of every table except type-6's. Type-6's LAB_0661bd30 shows the
// second argument is a float, so the slot uses that one signature.
unsigned long FTypedChannel::Step(float* Out, float Dt)
{
    (void)Dt;
    if (Out == nullptr)
        return 0;
    Out[0] = 0.0f;
    Out[1] = 0.0f;
    Out[2] = 0.0f;
    return 0;
}

// FUN_0661af70: header[0] is the start time.
unsigned long DACOM_API FTypedChannel::TimePlayable(const float* Time)
{
    unsigned long Bits = 0;
    std::memcpy(&Bits, Time, sizeof(Bits));
    if (Bits == 0xBF800000)
        return 1;
    if (*Time == 0.0f)
        return 1;
    if (Bits == 0xC0000000)
        return 1;
    float Duration = 0.0f;
    Archetype->GetDuration(&Duration);
    if (!(*Time <= Duration))
        return 0;
    if (!(*Time >= 0.0f))
        return 0;
    return 1;
}

// FUN_0661ad90.
unsigned long DACOM_API FTypedChannel::TryBindIfTimeValid(const FPlayHeader* Header)
{
    const unsigned long Ok = TimePlayable(&Header->StartTime);
    if (Ok != 0)
        BindHeader(Header);
    return Ok;
}

// FUN_0661b070: pose the channel at its current time -- or, once it has stopped (PlayFlags
// bit 1), at the end it ran into -- without advancing it.
void FTypedChannel::ApplyFromArchetype()
{
    PlayFlags |= 1;
    if (Archetype == nullptr)
        return;
    unsigned long Flags = 0;
    Archetype->GetFlags(&Flags);
    if (Flags == 8)
        return;
    float Time = this->Time;
    if ((PlayFlags & 2) != 0)
    {
        // Greater, equal and unordered Rate all take Duration; only Rate < 0 gives 0.
        if (Rate < 0.0f)
            Time = 0.0f;
        else
        {
            float Duration = 0.0f;
            Archetype->GetDuration(&Duration);
            Time = Duration;
        }
    }
    const unsigned char* Left = nullptr;
    const unsigned char* Right = nullptr;
    float Alpha = 0.0f;
    Archetype->Sample(Time, &Left, &Right, &Alpha);
    // Retail pushes Alpha as a double here.
    Apply((const float*)Left,
                                     (const float*)Right, Alpha);
}

// FUN_0661b120: gather the archetype keys between T0 and T1 into Buf, recursing across the
// end of a looping or ping-pong channel.
unsigned long FTypedChannel::Collect(float T0, float T1, unsigned long Mode, float* Buf,
                                     unsigned long Cap, unsigned long* Count)
{
    float Duration = 0.0f;
    Archetype->GetDuration(&Duration);
    unsigned long Found = 0;

    // An unordered Rate counts as forward (>= 0).
    unsigned char bWrap = 0;
    if (!(Rate < 0.0f))
    {
        if (!(T1 < Duration || T1 != T1))
            bWrap = 1;
    }
    else if (T1 <= 0.0f)
        bWrap = 1;

    if (bWrap == 0)
    {
        Found = 0;
        if ((Mode & 1) != 0)
        {
            Archetype->CollectAt(T0, Buf, Cap, &Found);
            *Count += Found;
        }
        if (T0 != T1)
        {
            Found = 0;
            Archetype->CollectRange(T0, T1, Buf + *Count * 2, Cap - *Count, &Found);
            *Count += Found;
            if ((Mode & 2) != 0)
            {
                Found = 0;
                Archetype->CollectAt(T1, Buf + *Count * 2, Cap - *Count, &Found);
                *Count += Found;
            }
        }
        Time = T1;
        return 0;
    }

    // The span up to the end the channel is running towards.
    float Span = 0.0f;
    if (Rate < 0.0f)
        Span = -T0;
    else
        Span = (float)((double)Duration - T0);
    Found = 0;
    if ((Mode & 1) != 0)
    {
        Archetype->CollectAt(T0, Buf, Cap, &Found);
        *Count += Found;
    }
    // Retail does not clear Found before either of the two calls below.
    if (Span != 0.0f)
    {
        const float EndTime = (float)((double)Span + T0);
        Archetype->CollectRange(T0, EndTime, Buf + *Count * 2, Cap - *Count, &Found);
        *Count += Found;
        if ((Mode & 2) != 0)
        {
            Archetype->CollectAt(EndTime, Buf + *Count * 2, Cap - *Count, &Found);
            *Count += Found;
        }
    }

    const unsigned long Loop = LoopFlags;
    const bool bForward = !(Rate < 0.0f);
    if ((Loop & 2) != 0)
    {
        Found = 0;
        if (bForward)
            Collect(0.0f, (float)((double)T1 - Duration), 3,
                    Buf + *Count * 2, Cap - *Count, &Found);
        else
            Collect(Duration, (float)((double)Duration + T1), 3,
                    Buf + *Count * 2, Cap - *Count, &Found);
        *Count += Found;
        return 0;
    }
    if ((Loop & 4) != 0)
    {
        const float Turn = bForward ? Duration : 0.0f;
        Rate = -Rate;
        Found = 0;
        Collect(Turn, (float)((double)Turn - T1), 2,
                Buf + *Count * 2, Cap - *Count, &Found);
        *Count += Found;
        return 0;
    }
    Time = bForward ? Duration : 0.0f;
    PlayFlags |= 2;
    return 1;
}

// FUN_0661b420: advance the channel by Dt * Rate and pose it. Answers 1
// when a non-looping channel has run off its end this tick and nothing has posed it since
// (PlayFlags bit 0 clear); an owner-driven channel answers whatever its owner does.
unsigned long FTypedChannel::Tick(float Dt)
{
    unsigned long Result = 0;
    PlayFlags |= 4;
    if ((PlayFlags & 3) != 0)
        return 0;

    const float OldTime = Time;
    const float NewTime = (float)((double)Dt * Rate + Time);
    Time = NewTime;

    if (this->Owner != nullptr)
    {
        float* Sampled = GetSampled();
        IChannelOwner* Owner = this->Owner;
        return (unsigned long)(Owner->Apply(
            Sampled, (unsigned long)((size_t)this),
            TypedBindDesc(this), NewTime));
    }

    unsigned long Flags = 0;
    Archetype->GetFlags(&Flags);
    if (Flags == 8)
    {
        float Keys[0x40][2];
        unsigned long KeyCount = 0;
        Result = Collect(OldTime, Time, 3, &Keys[0][0], 0x40, &KeyCount);
        if (KeyCount == 0)
            return Result;
        void* Sink = (void*)((size_t)(this->Sink));
        if (Sink == nullptr)
            return Result;
        FChannelKeyBatch Batch;
        Batch.Count = KeyCount;
        // Retail copies (count & 0x1FFFFFFF) * 2 dwords.
        std::memcpy(Batch.Keys, Keys, (KeyCount & 0x1FFFFFFFu) * 2 * sizeof(float));
        IChannelSink* KeySink = (IChannelSink*)Sink;
        KeySink->OnKeys((unsigned long)((size_t)this),
                        (void*)((size_t)(SinkContext)),
                        &Batch);
        return Result;
    }

    float Duration = 0.0f;
    Archetype->GetDuration(&Duration);
    // Rate >= 0 and unordered take Duration - Time.
    const double Remaining = Rate < 0.0f
                                 ? (double)(Time)
                                 : (double)Duration - Time;
    // "Greater" and unordered go straight to sampling.
    if (Remaining <= 0.0)
    {
        unsigned long Wraps = 0;
        if (Duration != 0.0f)
        {
            // _ftol, then the absolute value of the low dword.
            const long Low = (long)((long long)(Remaining / Duration));
            Wraps = (unsigned long)((Low ^ (Low >> 31)) - (Low >> 31));
        }
        const float Frac = (float)(std::fabs(Remaining) -
                                              (double)Wraps * Duration);
        const unsigned long Loop = LoopFlags;
        FPlayHeader Header;
        if ((Loop & 2) != 0)
        {
            // A 3x3 orientation (0..8), the applied position (9..11) and the matrix
            // QuaternionToMatrix writes (12..20), which is copied over 0..8.
            float Frame[21];
            if (Kind == 1)
            {
                const unsigned char bLast = Rate < 0.0f ? 0 : 1;
                const unsigned char* FrameData = nullptr;
                Archetype->GetFrame(bLast, &FrameData);
                Apply(
                    (const float*)FrameData,
                    (const float*)FrameData,
                    (double)Dt);
                // Retail reads +0x78 / +0x84 straight off the object, whatever its class.
                FPosQuatTypedChannel* Pose = (FPosQuatTypedChannel*)this;
                Frame[9] = Pose->AppliedPos[0];
                Frame[10] = Pose->AppliedPos[1];
                Frame[11] = Pose->AppliedPos[2];
                I3DMathEngine* Math = Acquire3DMathEngine();
                Math->QuaternionToMatrix(*(FMatrix*)(&Frame[12]),
                                         *(const FQuaternion*)(Pose->AppliedQuat));
                std::memcpy(Frame, &Frame[12], 9 * sizeof(float));
                void* Sink = (void*)((size_t)(this->Sink));
                if (Sink != nullptr)
                {
                    IChannelSink* LoopSink = (IChannelSink*)Sink;
                    LoopSink->OnLoop((unsigned long)((size_t)this),
                                     Frame,
                                     (void*)((size_t)(SinkContext)));
                }
            }
            // Rebind at the start the channel wraps back to.
            float StartTime = 0.0f;
            if (Rate < 0.0f)
                StartTime = Duration;
            std::memcpy(&Header.StartTime, &StartTime, sizeof(float));
            Header.LoopFlags = Loop;
            std::memcpy(&Header.Rate, &Rate, sizeof(float));
            std::memset(&Header.BlendDuration, 0, sizeof(float));
            std::memcpy(&Header.Scale, &Scale, sizeof(float));
            std::memcpy(&Header.TranslationScale, &TranslationScale, sizeof(float));
            Header.Transform = Kind == 1 ? Frame : nullptr;
            BindHeader(&Header);
            // Rate >= 0 or unordered takes Frac.
            if (Duration != 0.0f)
            {
                if (Rate < 0.0f)
                    Time = (float)((double)Duration - Frac);
                else
                    Time = Frac;
            }
        }
        else if ((Loop & 4) != 0)
        {
            // Ping-pong: an odd number of wraps keeps the direction, an even one reverses it.
            float NewRate = Rate;
            if ((Wraps & 1) == 0)
                NewRate = -NewRate;
            float StartTime = Frac;
            if (NewRate < 0.0f)
                StartTime = (float)((double)Duration - Frac);
            std::memcpy(&Header.StartTime, &StartTime, sizeof(float));
            Header.LoopFlags = Loop;
            std::memcpy(&Header.Rate, &NewRate, sizeof(float));
            std::memset(&Header.BlendDuration, 0, sizeof(float));
            std::memcpy(&Header.Scale, &Scale, sizeof(float));
            std::memcpy(&Header.TranslationScale, &TranslationScale, sizeof(float));
            Header.Transform = nullptr;
            BindHeader(&Header);
        }
        else
        {
            Time = Rate < 0.0f ? 0.0f : Duration;
            PlayFlags |= 2;
            if ((PlayFlags & 1) == 0)
                Result = 1;
        }
    }

    const unsigned char* Left = nullptr;
    const unsigned char* Right = nullptr;
    float Alpha = 0.0f;
    Archetype->Sample(Time, &Left, &Right, &Alpha);
    Apply((const float*)Left,
                                     (const float*)Right,
                                     (double)Alpha);
    return Result;
}

// LAB_0661caf0, the type-6 table's slot 8 (+0x20, .rdata:06629110).
unsigned long FPosQuatTypedChannel::SetOverlayPos(const float* Target)
{
    if (Kind != 1)
        return 0;
    const float* KeyA = nullptr;
    const float* KeyB = nullptr;
    float Alpha = 0.0f;
    Archetype->Sample(Time, (const unsigned char**)(&KeyA),
                            (const unsigned char**)(&KeyB), &Alpha);
    const double D0 = (double)(KeyB[0]) - (double)(KeyA[0]);
    const double D1 = (double)(KeyB[1]) - (double)(KeyA[1]);
    const float D2 = KeyB[2] - KeyA[2];
    const float V0 = (float)(D0 * (double)Alpha);
    const float P0 = V0 + KeyA[0];
    const float P1 = (float)(D1 * (double)Alpha + (double)(KeyA[1]));
    const double P2 = (double)D2 * (double)Alpha + (double)(KeyA[2]);
    const float P0Scaled = P0 * Scale;
    const float P1Scaled = P1 * Scale;
    const double P2Scaled = P2 * (double)(Scale);
    float Pos[3];
    Pos[0] = P0Scaled * TranslationScale;
    Pos[1] = P1Scaled * TranslationScale;
    Pos[2] = (float)(P2Scaled * (double)(TranslationScale));
    float Xformed[3];
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, Matrix, Pos);
    Delta[0] = Target[0] - Xformed[0];
    Delta[1] = Target[1] - Xformed[1];
    Delta[2] = Target[2] - Xformed[2];
    AppliedPos[0] = Target[0];
    AppliedPos[1] = Target[1];
    AppliedPos[2] = Target[2];
    unsigned long Bits = 0;
    std::memcpy(&Bits, &Target[2], sizeof(Bits));
    return Bits;
}

// LAB_0661bc80: lerp position, slerp quaternion (3DMath +0x58). Not IK.
unsigned long FPosQuatTypedChannel::InterpolatePose(float* Out, const float* Left, const float* Right, double Alpha)
{
    Out[0] = (float)(((double)(Right[0]) - Left[0]) * Alpha + Left[0]);
    Out[1] = (float)(((double)(Right[1]) - Left[1]) * Alpha + Left[1]);
    Out[2] = (float)(((double)(Right[2]) - Left[2]) * Alpha + Left[2]);
    const float Blend = (float)Alpha;
    I3DMathEngine* Math = Acquire3DMathEngine();
    float Quat[4];
    MathQuatSlerp(Math, Quat, Left + 3, Right + 3, Blend);
    Out[3] = Quat[0];
    Out[4] = Quat[1];
    Out[5] = Quat[2];
    Out[6] = Quat[3];
    unsigned long Bits = 0;
    std::memcpy(&Bits, &Quat[1], sizeof(Bits));
    return Bits;
}

// LAB_0661ba60, the type-6 table's slot 0 (.rdata:PTR_LAB_066290f0[0]).
unsigned long FPosQuatTypedChannel::Interpolate(float* Out, const float* Left, const float* Right, double Alpha)
{
    InterpolatePose(Out, Left, Right, Alpha);
    unsigned long ScaleBits = 0;
    std::memcpy(&ScaleBits, &Scale, sizeof(ScaleBits));
    if (ScaleBits == 0x3F800000)
        return ScaleBits;
    I3DMathEngine* Math = Acquire3DMathEngine();
    float const Identity[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    float Slerped[4];
    MathQuatSlerp(Math, Slerped, Identity, Out + 3, Scale);
    Out[3] = Slerped[0];
    Out[4] = Slerped[1];
    Out[5] = Slerped[2];
    Out[6] = Slerped[3];
    unsigned long LastBits = 0;
    std::memcpy(&LastBits, &Slerped[3], sizeof(LastBits));
    return LastBits;
}

// LAB_0661b880, slot 2 of the type 1 / 2 / 4 tables (.rdata:066291ac, 06629170, 06629134).
static unsigned long DACOM_API ApplyInterpolatedChannelSample(FTypedChannel* This, float* Sampled,
                                               const float* Left, const float* Right, double Alpha)
{
    return This->InterpolatePose(Sampled, Left, Right, Alpha);
}

// LAB_0661bb10, the type-6 table's slot 2 (.rdata:066290f8).
unsigned long FPosQuatTypedChannel::Apply(const float* Left, const float* Right, double Alpha)
{
    float Local[7];
    Interpolate(Local, Left, Right, Alpha);
    const unsigned long Kind = this->Kind;
    if (Kind == 1)
    {
        const double Factor = (double)(TranslationScale) * (double)(Scale);
        float Pos[3];
        Pos[0] = (float)((double)(Local[0]) * Factor);
        Pos[1] = (float)((double)(Local[1]) * Factor);
        Pos[2] = (float)((double)(Local[2]) * Factor);
        I3DMathEngine* Math = Acquire3DMathEngine();
        float Xformed[3];
        MathTransformMatrix(Math, Xformed, Matrix, Pos);
        AppliedPos[0] = Xformed[0] + Delta[0];
        AppliedPos[1] = Xformed[1] + Delta[1];
        AppliedPos[2] = Xformed[2] + Delta[2];
        float MatrixQuat[4];
        MathMatrixToQuat(Math, MatrixQuat, Matrix);
        float Composed[4];
        Math->MulQuat(*(FQuaternion*)Composed,
                              *(const FQuaternion*)MatrixQuat,
                              *(const FQuaternion*)(Local + 3));   // +0x18
        AppliedQuat[0] = Composed[0];
        AppliedQuat[1] = Composed[1];
        AppliedQuat[2] = Composed[2];
        AppliedQuat[3] = Composed[3];
        unsigned long Bits = 0;
        std::memcpy(&Bits, &Composed[3], sizeof(Bits));
        return Bits;
    }
    if (Kind == 0)
    {
        TypedCopyVec3(AppliedPos, Local);
        AppliedQuat[0] = Local[3];
        AppliedQuat[1] = Local[4];
        AppliedQuat[2] = Local[5];
        AppliedQuat[3] = Local[6];
        unsigned long Bits = 0;
        std::memcpy(&Bits, &Local[6], sizeof(Bits));
        return Bits;
    }
    return Kind;
}

// LAB_0661afe0.
unsigned long FTypedChannel::SetTime(float Time)
{
    if (Time == -1.0f)
    {
        float Duration = Time;
        Archetype->GetDuration(&Duration);
        this->Time = Duration;
        return 1;
    }
    if (Time == 0.0f)
    {
        this->Time = 0.0f;
        return 1;
    }
    if (Time == -2.0f)
        return 1;
    if (Time < 0.0f)
        return 0;
    this->Time = Time;
    return 1;
}

// LAB_0661cf90.
unsigned long FPosQuatTypedChannel::GetType()
{
    FTypedChannel* This = this;
    (void)This;
    return 6;
}

// LAB_06612190: copies the dword at +0x3C and returns it. No null checks.
unsigned long FScalarTypedChannel::CopySampled(float* Out)
{
    Out[0] = Sampled[0];
    unsigned long Bits = 0;
    std::memcpy(&Bits, &Sampled[0], sizeof(Bits));
    return Bits;
}

// LAB_066121b0, type-4 slot 6: 16 bytes from +0x3C. No null checks.
unsigned long FQuatTypedChannel::CopySampled(float* Out)
{
    Out[0] = Sampled[0];
    Out[1] = Sampled[1];
    Out[2] = Sampled[2];
    Out[3] = Sampled[3];
    return (unsigned long)((size_t)Out);
}

// LAB_066121d0, type-2 slot 6: 12 bytes from +0x3C. No null checks.
unsigned long FVectorTypedChannel::CopySampled(float* Out)
{
    TypedCopyVec3(Out, Sampled);
    return (unsigned long)((size_t)Out);
}

// LAB_066121f0: 12 bytes from +0x78 and 16 from +0x84; returns Out+0xC. No null checks.
unsigned long FPosQuatTypedChannel::CopySampled(float* Out)
{
    TypedCopyVec3(Out, AppliedPos);
    Out[3] = AppliedQuat[0];
    Out[4] = AppliedQuat[1];
    Out[5] = AppliedQuat[2];
    Out[6] = AppliedQuat[3];
    return (unsigned long)((size_t)(Out + 3));
}

// LAB_0661c6d0: Delta is at +0x60.
unsigned long FPosQuatTypedChannel::AddOverlayPos(const float* Add)
{
    Delta[0] += Add[0];
    Delta[1] += Add[1];
    Delta[2] += Add[2];
    if ((PlayFlags & G_ChannelPlayFlag) != 0)
        ApplyFromArchetype();
    return (unsigned long)((size_t)Add);
}

// LAB_06612240: +0x78. No null check.
float* FPosQuatTypedChannel::GetSampled()
{
    return AppliedPos;
}

// LAB_0661c5c0, the type-6 table's slot 0x30.
unsigned long FPosQuatTypedChannel::TouchMapped(void* OutPos, void* OutSampledPos)
{
    float* Out = (float*)OutPos;
    float* OutSampled = (float*)OutSampledPos;
    const unsigned char* KeyA = nullptr;
    const unsigned char* KeyB = nullptr;
    float Alpha = 0.0f;
    if ((PlayFlags & G_ChannelBlendFlag) != 0)
    {
        const unsigned char bLast = (double)(Rate) < 0.0 ? 0 : 1;
        Archetype->GetFrame(bLast, &KeyA);
        Alpha = 0.0f;
        KeyB = KeyA;
    }
    else
        Archetype->Sample(Time, &KeyA, &KeyB, &Alpha);
    float Pose[7];
    Interpolate(Pose, (const float*)KeyA,
                                           (const float*)KeyB,
                                           (double)Alpha);
    OutSampled[0] = Pose[0];
    OutSampled[1] = Pose[1];
    OutSampled[2] = Pose[2];
    float Xformed[3];
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, Matrix, OutSampled);
    Out[0] = Xformed[0] + Delta[0];
    Out[1] = Xformed[1] + Delta[1];
    Out[2] = Xformed[2] + Delta[2];
    return (unsigned long)((size_t)Out);
}

// One pass of LAB_0661c320 (retail inlines it twice).
static void TypedType6SampleXform(FPosQuatTypedChannel* This, float Time, const float** KeyA,
                                  const float** KeyB, float* Alpha, float* Xformed)
{
    This->Archetype->Sample(Time, (const unsigned char**)KeyA,
                            (const unsigned char**)KeyB, Alpha);
    const float* A = *KeyA;
    const float* B = *KeyB;
    float V[3];
    V[0] = (B[0] - A[0]) * *Alpha;
    V[1] = (B[1] - A[1]) * *Alpha;
    V[2] = (B[2] - A[2]) * *Alpha;
    V[0] = V[0] + A[0];
    V[1] = V[1] + A[1];
    V[2] = V[2] + A[2];
    const double Factor = (double)(This->Scale) * (double)(This->TranslationScale);
    V[0] = (float)((double)(V[0]) * Factor);
    V[1] = (float)((double)(V[1]) * Factor);
    V[2] = (float)((double)(V[2]) * Factor);
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, This->Matrix, V);
}

// LAB_0661c320, the type-6 table's slot 0x34.
unsigned long FPosQuatTypedChannel::UpdateDeltaRate(float Dt)
{
    const float Shifted = (double)(Rate) < 0.0
                              ? (float)((double)(Time) - (double)Dt)
                              : (float)((double)(Time) + (double)Dt);
    const float* KeyA = nullptr;
    const float* KeyB = nullptr;
    float Alpha = 0.0f;
    float Xformed[3];
    TypedType6SampleXform(this, Time, &KeyA, &KeyB, &Alpha, Xformed);
    float Now[3];
    Now[0] = Xformed[0] + Delta[0];
    Now[1] = Xformed[1] + Delta[1];
    Now[2] = Xformed[2] + Delta[2];
    TypedType6SampleXform(this, Shifted, &KeyA, &KeyB, &Alpha, Xformed);
    const double S0 = (double)(Xformed[0]) + (double)(Delta[0]);
    const double S1 = (double)(Xformed[1]) + (double)(Delta[1]);
    const float S2 = (float)((double)(Xformed[2]) + (double)(Delta[2]));
    const float Dx = (float)(S0 - (double)(Now[0]));
    const double Dy = S1 - (double)(Now[1]);
    const double Dz = (double)S2 - (double)(Now[2]);
    const double Inv = 1.0 / (double)Dt;
    StepDelta[0] = (float)((double)Dx * Inv);
    StepDelta[1] = (float)(Dy * Inv);
    StepDelta[2] = (float)(Dz * Inv);
    unsigned long Bits = 0;
    std::memcpy(&Bits, &StepDelta[1], sizeof(Bits));
    return Bits;
}

// Round each position scaling step to float.
static void TypedType6ScaledFrame(FPosQuatTypedChannel* This, unsigned char bLast, float* Out)
{
    const float* Frame = nullptr;
    This->Archetype->GetFrame(bLast, (const unsigned char**)(&Frame));
    float Scaled[3];
    Scaled[0] = This->Scale * Frame[0];
    Scaled[1] = This->Scale * Frame[1];
    Scaled[2] = This->Scale * Frame[2];
    Out[0] = Scaled[0] * This->TranslationScale;
    Out[1] = Scaled[1] * This->TranslationScale;
    Out[2] = Scaled[2] * This->TranslationScale;
}

// LAB_0661bd30, the type-6 table's slot 0x38.
unsigned long FPosQuatTypedChannel::Step(float* Out, float Dt)
{
    const float RatedDt = Dt * Rate;
    float Duration = 0.0f;
    Archetype->GetDuration(&Duration);
    float Step[3];
    Step[0] = 0.0f;
    Step[1] = 0.0f;
    Step[2] = 0.0f;
    const unsigned char bReverse = (double)(Rate) < 0.0 ? 1 : 0;
    const double SampleTimeWide = bReverse != 0
                                      ? (double)(Time) - (double)RatedDt
                                      : (double)(Time) + (double)RatedDt;
    const float SampleTime = (float)SampleTimeWide;
    unsigned char bCrossed = 0;
    if (bReverse != 0)
        bCrossed = SampleTimeWide < 0.0 ? 1 : 0;
    else
        bCrossed = SampleTimeWide > (double)Duration ? 1 : 0;

    const float* KeyA = nullptr;
    const float* KeyB = nullptr;
    float Alpha = 0.0f;
    Archetype->Sample(Time, (const unsigned char**)(&KeyA),
                            (const unsigned char**)(&KeyB), &Alpha);
    float Lerp[3];
    Lerp[0] = KeyB[0] - KeyA[0];
    Lerp[1] = KeyB[1] - KeyA[1];
    Lerp[2] = KeyB[2] - KeyA[2];
    Lerp[0] = Lerp[0] * Alpha;
    Lerp[1] = Lerp[1] * Alpha;
    Lerp[2] = Lerp[2] * Alpha;
    Lerp[0] = KeyA[0] + Lerp[0];
    Lerp[1] = KeyA[1] + Lerp[1];
    Lerp[2] = KeyA[2] + Lerp[2];
    const double Factor = (double)(Scale) * (double)(TranslationScale);
    float Now[3];
    Now[0] = (float)((double)(Lerp[0]) * Factor);
    Now[1] = (float)((double)(Lerp[1]) * Factor);
    Now[2] = (float)((double)(Lerp[2]) * Factor);

    if (bCrossed != 0 && (LoopFlags & 2) != 0)
    {
        float EndA[3];
        TypedType6ScaledFrame(this, bReverse != 0 ? 0 : 1, EndA);
        Step[0] = EndA[0] - Now[0];
        Step[1] = EndA[1] - Now[1];
        Step[2] = EndA[2] - Now[2];
        float EndB[3];
        TypedType6ScaledFrame(this, bReverse, EndB);
        const float Leftover = bReverse != 0 ? Duration - SampleTime : SampleTime - Duration;
        Archetype->Sample(Leftover, (const unsigned char**)(&KeyA),
                                (const unsigned char**)(&KeyB), &Alpha);
        Lerp[0] = KeyB[0] - KeyA[0];
        Lerp[1] = KeyB[1] - KeyA[1];
        Lerp[2] = KeyB[2] - KeyA[2];
        Lerp[0] = Lerp[0] * Alpha;
        Lerp[1] = Lerp[1] * Alpha;
        Lerp[2] = Lerp[2] * Alpha;
        Lerp[0] = Lerp[0] + KeyA[0];
        Lerp[1] = Lerp[1] + KeyA[1];
        Lerp[2] = Lerp[2] + KeyA[2];
        float Wrapped[3];
        Wrapped[0] = (float)((double)(Lerp[0]) * Factor - (double)(EndB[0]));
        Wrapped[1] = (float)((double)(Lerp[1]) * Factor - (double)(EndB[1]));
        Wrapped[2] = (float)((double)(Lerp[2]) * Factor - (double)(EndB[2]));
        Step[0] = Wrapped[0] + Step[0];
        Step[1] = Step[1] + Wrapped[1];
        Step[2] = Step[2] + Wrapped[2];
    }
    else if (Archetype->Sample(SampleTime, (const unsigned char**)(&KeyA),
                                     (const unsigned char**)(&KeyB), &Alpha) == 0)
    {
        float Diff[3];
        Diff[0] = KeyB[0] - KeyA[0];
        Diff[1] = KeyB[1] - KeyA[1];
        Diff[2] = KeyB[2] - KeyA[2];
        const float Shifted0 = (float)((double)(Diff[0]) * (double)Alpha +
                                                  (double)(KeyA[0]));
        const float Shifted1 = (float)((double)(Diff[1]) * (double)Alpha +
                                                  (double)(KeyA[1]));
        const float Diff2Alpha = Diff[2] * Alpha;
        const float Shifted2 = Diff2Alpha + KeyA[2];
        const float Scaled1 = (float)((double)Shifted1 * Factor);
        const float Scaled2 = (float)((double)Shifted2 * Factor);
        Step[0] = (float)((double)Shifted0 * Factor - (double)(Now[0]));
        Step[1] = Scaled1 - Now[1];
        Step[2] = Scaled2 - Now[2];
    }
    // A failed Sample leaves Step zero.

    float Xformed[3];
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, Matrix, Step);
    Out[0] = Xformed[0];
    Out[1] = Xformed[1];
    Out[2] = Xformed[2];
    return (unsigned long)((size_t)Out);
}

// LAB_0661cc40, the type-6 table's slot 9 (+0x24, .rdata:06629114).
unsigned long FPosQuatTypedChannel::SetOverlayExtra(const float* Orient)
{
    if (Kind != 1)
        return 0;
    const unsigned char* KeyA = nullptr;
    const unsigned char* KeyB = nullptr;
    float Alpha = 0.0f;
    Archetype->Sample(Time, &KeyA, &KeyB, &Alpha);
    float Pose[7];
    Interpolate(Pose, (const float*)KeyA,
                                           (const float*)KeyB,
                                           (double)Alpha);
    float Pos[3];
    Pos[0] = Pose[0];
    Pos[1] = Pose[1];
    Pos[2] = Pose[2];
    float Quat[4];
    Quat[0] = Pose[3];
    Quat[1] = Pose[4];
    Quat[2] = Pose[5];
    Quat[3] = Pose[6];
    FEngineInstance* Instance = (FEngineInstance*)((size_t)(this->Instance));
    const float* InstanceOrient = G_AnimEngine->GetInstanceOrientation(Instance);
    float Transposed[9];                                         // FUN_0661caa0
    Transposed[0] = InstanceOrient[0];
    Transposed[1] = InstanceOrient[3];
    Transposed[2] = InstanceOrient[6];
    Transposed[3] = InstanceOrient[1];
    Transposed[4] = InstanceOrient[4];
    Transposed[5] = InstanceOrient[7];
    Transposed[6] = InstanceOrient[2];
    Transposed[7] = InstanceOrient[5];
    Transposed[8] = InstanceOrient[8];
    float R1[9];
    MathMulMatrix(Acquire3DMathEngine(), R1, Orient, Transposed);
    float R2[9];
    MathMulMatrix(Acquire3DMathEngine(), R2, R1, Matrix);
    float Xformed[3];
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, Matrix, Pos);
    float World[3];
    World[0] = Xformed[0] + Delta[0];
    World[1] = Xformed[1] + Delta[1];
    World[2] = Xformed[2] + Delta[2];
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, R2, Pos);
    Delta[0] = World[0] - Xformed[0];
    Delta[1] = World[1] - Xformed[1];
    Delta[2] = World[2] - Xformed[2];
    std::memcpy(Matrix, R2, sizeof(Matrix));
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, Matrix, Pos);
    AppliedPos[0] = Xformed[0] + Delta[0];
    AppliedPos[1] = Xformed[1] + Delta[1];
    AppliedPos[2] = Xformed[2] + Delta[2];
    float QuatMatrix[9];
    MathQuatToMatrix(Acquire3DMathEngine(), QuatMatrix, Quat);
    float R3[9];
    MathMulMatrix(Acquire3DMathEngine(), R3, R2, QuatMatrix);
    MathMatrixToQuat(Acquire3DMathEngine(), AppliedQuat, R3);
    unsigned long Bits = 0;
    std::memcpy(&Bits, &AppliedQuat[3], sizeof(Bits));
    return Bits;
}

// The typed-channel vtable. The 15 slots follow retail's __thiscall table order
// (PTR_LAB_066294ac, PTR_LAB_0662912c/168/a4 and PTR_LAB_066290f0).
unsigned long FTypedChannel::Interpolate(float* Out, const float* Left, const float* Right, double Alpha) { return NoOpTypedChannelInterpolate(this, Out, Left, Right, Alpha); }
unsigned long FTypedChannel::BindHeader(const FPlayHeader* Header) { return TypedBind(this, Header); }
unsigned long FTypedChannel::InterpolatePose(float* Out, const float* Left, const float* Right, double Alpha) { return NoOpTypedChannelInterpolate(this, Out, Left, Right, Alpha); }
unsigned long FTypedChannel::BindMap(unsigned long* Map, void* Bind) { return TypedBindMap(this, Map, Bind); }
unsigned long FTypedChannel::SetOverlayPos(const float* Pos) { (void)Pos; return 0; }
unsigned long FTypedChannel::SetOverlayExtra(const float* Extra) { (void)Extra; return 0; }
unsigned long FTypedChannel::AddOverlayPos(const float* Delta) { (void)Delta; return 0; }
float* FTypedChannel::GetSampled() { return nullptr; }
unsigned long FTypedChannel::UpdateDeltaRate(float Dt) { (void)Dt; return 0; }
// Type 1 (066291a4): slots 0/1/4/8..10/12..14 inherited (the shared bodies), 2 LAB_0661b880,
// 3 LAB_0661b8a0, 5 LAB_0661b870, 6 LAB_06612190, 7 FUN_0661b830, 11 LAB_066121a0.
unsigned long FScalarTypedChannel::Apply(const float* Left, const float* Right, double Alpha) { return ApplyInterpolatedChannelSample(this, Sampled, Left, Right, Alpha); }
float* FScalarTypedChannel::GetSampled() { return Sampled; } // LAB_066121a0 (+0x3C)

// Type 2 (06629168): 2 LAB_0661b880, 3 LAB_0661cfa0, 5 LAB_0661d040, 6 LAB_066121d0,
// 7 FUN_0661adc0 (the base's), 11 LAB_066121a0; the rest as the base.
unsigned long FVectorTypedChannel::Apply(const float* Left, const float* Right, double Alpha) { return ApplyInterpolatedChannelSample(this, Sampled, Left, Right, Alpha); }
float* FVectorTypedChannel::GetSampled() { return Sampled; } // LAB_066121a0

// Type 4 (0662912c): 2 LAB_0661b880, 3 LAB_0661b950, 5 LAB_0661b940, 6 LAB_066121b0,
// 7 FUN_0661adc0 (the base's), 11 LAB_066121a0; the rest as the base.
unsigned long FQuatTypedChannel::Apply(const float* Left, const float* Right, double Alpha) { return ApplyInterpolatedChannelSample(this, Sampled, Left, Right, Alpha); }
float* FQuatTypedChannel::GetSampled() { return Sampled; } // LAB_066121a0

// ---- Typed-channel construction (retail operator new + ctor chain) ----------------------------
static void TypedResetPlayState(FTypedChannel* This)
{
    This->Time = 0.0f;
    This->LoopFlags = 0;
    This->Rate = 1.0f;
    This->Scale = 1.0f;
    This->PlayFlags &= 0xFFFFFFF8u;
    This->TranslationScale = 1.0f;
}

// FUN_0661ad50: +4 Owner, +8 Kind 2, +0x14 -1, +0xC / +0x10 = 0, +0x24 Archetype = 0, the
// type-8 table (PTR_LAB_066294ac), then FUN_0661acd0. Every other member keeps what the
// allocation held.
FTypedChannel::FTypedChannel(IChannelOwner* NewOwner)
{
    Owner = NewOwner;
    Kind = 2;
    Instance = 0xFFFFFFFF;
    Sink = 0;
    SinkContext = 0;
    Archetype = nullptr;
    TypedResetPlayState(this);
}

// FUN_0661acf0: as FUN_0661ad50 with Owner = 0 and +0x24 = Archetype, AddRef'd when non-null.
FTypedChannel::FTypedChannel(FChannelArchetypeObject* NewArchetype)
{
    Owner = nullptr;
    Kind = 2;
    Instance = 0xFFFFFFFF;
    Sink = 0;
    SinkContext = 0;
    Archetype = NewArchetype;
    if (NewArchetype != nullptr)
        NewArchetype->AddRef();
    TypedResetPlayState(this);
}

// The class tails: type 1 stores -1 at +0x40 (CreateTyped 06615998, CreateFromObject 06615b55,
// FUN_06611f90 06612080) before its table goes in; types 2 / 4 / 6 only install their tables.
FScalarTypedChannel::FScalarTypedChannel(IChannelOwner* NewOwner) : FTypedChannel(NewOwner) { JointType = 0xFFFFFFFF; }
FScalarTypedChannel::FScalarTypedChannel(FChannelArchetypeObject* NewArchetype) : FTypedChannel(NewArchetype) { JointType = 0xFFFFFFFF; }
FVectorTypedChannel::FVectorTypedChannel(IChannelOwner* NewOwner) : FTypedChannel(NewOwner) {}
FVectorTypedChannel::FVectorTypedChannel(FChannelArchetypeObject* NewArchetype) : FTypedChannel(NewArchetype) {}
FQuatTypedChannel::FQuatTypedChannel(IChannelOwner* NewOwner) : FTypedChannel(NewOwner) {}
FQuatTypedChannel::FQuatTypedChannel(FChannelArchetypeObject* NewArchetype) : FTypedChannel(NewArchetype) {}
FPosQuatTypedChannel::FPosQuatTypedChannel(IChannelOwner* NewOwner) : FTypedChannel(NewOwner) {}
FPosQuatTypedChannel::FPosQuatTypedChannel(FChannelArchetypeObject* NewArchetype) : FTypedChannel(NewArchetype) {}

// The switch every creation site runs on the type (1 / 2 / 4 / 6 / 8).
FTypedChannel* NewTypedChannelWithOwner(unsigned long Type, IChannelOwner* Owner)
{
    if (Type == 1)
        return new FScalarTypedChannel(Owner);
    if (Type == 2)
        return new FVectorTypedChannel(Owner);
    if (Type == 4)
        return new FQuatTypedChannel(Owner);
    if (Type == 6)
        return new FPosQuatTypedChannel(Owner);
    if (Type == 8)
        return new FTypedChannel(Owner);
    return nullptr;
}

FTypedChannel* NewTypedChannelWithArchetype(unsigned long Type, FChannelArchetypeObject* Archetype)
{
    if (Type == 1)
        return new FScalarTypedChannel(Archetype);
    if (Type == 2)
        return new FVectorTypedChannel(Archetype);
    if (Type == 4)
        return new FQuatTypedChannel(Archetype);
    if (Type == 6)
        return new FPosQuatTypedChannel(Archetype);
    if (Type == 8)
        return new FTypedChannel(Archetype);
    return nullptr;
}

// The IFileSystem calls every EngBase site makes inline through the vtable, without testing
// the pointer or the arguments.
int FileSystemOpenNode(void* FileSystem, const char* Name)
{
    return AsFileSystem(FileSystem)->OpenNode(Name); // +0x60
}

int FileSystemFindFirst(void* FileSystem, const char* Pattern, WIN32_FIND_DATAA* FindData)
{
    return AsFileSystem(FileSystem)->FindFirst(Pattern, // +0x48
                                            FindData);
}

int FileSystemFindNext(void* FileSystem, int FindHandle, WIN32_FIND_DATAA* FindData)
{
    return AsFileSystem(FileSystem)->FindNext((unsigned long)FindHandle, // +0x4C
                                            FindData);
}

void FileSystemFindClose(void* FileSystem, int FindHandle)
{
    AsFileSystem(FileSystem)->FindClose((unsigned long)FindHandle); // +0x50
}

void MathMulMatrix3(I3DMathEngine* Math, float* Out9, const float* Left, const float* Right)
{
    MathMulMatrix(Math, Out9, Left, Right);
}

void MathTransformVec(I3DMathEngine* Math, float* Out3, const float* Matrix, const float* In3)
{
    MathTransformMatrix(Math, Out3, Matrix, In3);
}

static I3DMathEngine* G_MathEngine = nullptr;

// FUN_06628190. The hr is not tested; the out local is uninitialised in retail, 0 here.
I3DMathEngine* Create3DMathEngine()
{
    FAggDescC Desc = {};
    Desc.Size = G_AggDescSizeMath;
    Desc.Name = G_Name3DMathEngine;
    void* Created = nullptr;
    DACOM_Acquire()->CreateInstance((AGGDESC*)(&Desc), &Created);
    return (I3DMathEngine*)Created;
}

// Shared with EngBaseMixer.cpp; see EngBase.h for the per-site caches retail keeps instead.
I3DMathEngine* Acquire3DMathEngine()
{
    if (G_MathEngine == nullptr)
        G_MathEngine = Create3DMathEngine();
    return G_MathEngine;
}
struct FEngineFactory : FDacomFactory
{
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Out) override;
};
struct FAnimationFactory : FDacomFactory
{
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Out) override;
};
struct FHardpointFactory : FDacomFactory
{
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Out) override;
};
struct FChannelArchetypeFactory : FDacomFactory
{
    GENRESULT DACOM_API CreateInstance(AGGDESC* Desc, void** Out) override;
};

GENRESULT DACOM_API FEngineFactory::CreateInstance(AGGDESC* Desc, void** Out)
{
    return EngineFactoryCreateInstance(this, Desc, Out);
}
GENRESULT DACOM_API FAnimationFactory::CreateInstance(AGGDESC* Desc, void** Out)
{
    return AnimationFactoryCreateInstance(this, Desc, Out);
}
GENRESULT DACOM_API FHardpointFactory::CreateInstance(AGGDESC* Desc, void** Out)
{
    return HardpointFactoryCreateInstance(this, Desc, Out);
}
GENRESULT DACOM_API FChannelArchetypeFactory::CreateInstance(AGGDESC* Desc, void** Out)
{
    return ChannelArchetypeFactoryCreateInstance(this, Desc, Out);
}

static void RegisterFactory(FDacomFactory* Factory, const char* Name, unsigned long Priority)
{
    if (Factory == nullptr)
        return;
    Factory->RefCount = 1;
    Factory->Name = Name;
    ICOManager* Dacom = DACOM_Acquire();
    if (Dacom != nullptr)
    {
        Dacom->RegisterComponent((IDAComponent*)Factory, Name,
                                 (void*)Priority);
        Factory->Release();
    }
    else
    {
        Factory->Release();
    }
}

void RegisterEngBaseComponents()
{
    FAnimationFactory* Animation =
        (FAnimationFactory*)(std::calloc(1, sizeof(FAnimationFactory)));
    if (Animation != nullptr)
    {
        new (Animation) FAnimationFactory();
        RegisterFactory(Animation, G_NameAnimation, G_PriorityAnimation);
    }
    FChannelArchetypeFactory* ChannelArch = (FChannelArchetypeFactory*)(
        std::calloc(1, sizeof(FChannelArchetypeFactory)));
    if (ChannelArch != nullptr)
    {
        new (ChannelArch) FChannelArchetypeFactory();
        RegisterFactory(ChannelArch, G_NameChannelArchetype, G_PriorityChannelArchetype);
    }
    FHardpointFactory* Hardpoint =
        (FHardpointFactory*)(std::calloc(1, sizeof(FHardpointFactory)));
    if (Hardpoint != nullptr)
    {
        new (Hardpoint) FHardpointFactory();
        RegisterFactory(Hardpoint, G_NameHardpoint, G_PriorityHardpoint);
    }
    FEngineFactory* Engine = (FEngineFactory*)(std::calloc(1, sizeof(FEngineFactory)));
    if (Engine != nullptr)
    {
        new (Engine) FEngineFactory();
        RegisterFactory(Engine, G_NameEngine, G_PriorityEngine);
    }
}

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, LPVOID Reserved)
{
    (void)Reserved;
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Module);
        RegisterEngBaseComponents();
    }
    return TRUE;
}
