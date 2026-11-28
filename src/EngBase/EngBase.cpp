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

// Thin wrappers over the 3DMathEngine calls the channel and joint code makes.
static GENRESULT MathMulMatrix(I3DMathEngine* Math, Matrix* Dst, const Matrix* A, const Matrix* B)
{
    return Math->MulMatrix(*Dst, *A, *B);
}

static GENRESULT MathTransformMatrix(I3DMathEngine* Math, Vector* Dst, const Matrix* A, const Vector* B)
{
    return Math->TransformMatrix(*Dst, *A, *B);
}

static GENRESULT MathMatrixToQuat(I3DMathEngine* Math, Quaternion* Dst, const Matrix* A)
{
    return Math->MatrixToQuaternion(*Dst, *A);
}

GENRESULT MathQuatToMatrix(I3DMathEngine* Math, Matrix* Dst, const Quaternion* A)
{
    return Math->QuaternionToMatrix(*Dst, *A);
}

static GENRESULT MathQuatSlerp(I3DMathEngine* Math, Quaternion* Dst, const Quaternion* A, const Quaternion* B, float C)
{
    return Math->QuatSlerp(*Dst, *A, *B, C);
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

bool IsTypedChannelType(unsigned long Type)
{
    return Type == G_TypedTypeScalar || Type == G_TypedTypeVector || Type == G_TypedTypeQuat ||
           Type == G_TypedTypePosQuat || Type == G_TypedTypeBase;
}

static unsigned long TypedKindFromType(unsigned long Type)
{
    if (Type == G_TypedTypeScalar || Type == G_TypedTypeVector)
        return G_MixKindScalar;
    if (Type == 3)
        return 2;
    if (Type == G_TypedTypeQuat)
        return G_MixKindQuat;
    if (Type == 5)
        return G_MixKindVec3;
    if (Type == G_TypedTypePosQuat || Type == G_TypedTypeBase)
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
    This->PlayFlags &= ~(G_ChannelPlayFlag | G_ChannelBlendFlag);
    return This->PlayFlags;
}

static void TypedCopyVec3(float* Dest, const float* Source)
{
    Dest[0] = Source[0];
    Dest[1] = Source[1];
    Dest[2] = Source[2];
}

static void TypedStoreIdentityMatrix(Matrix* Dest)
{
    Dest->E[0][0] = 1.0f;
    Dest->E[0][1] = 0.0f;
    Dest->E[0][2] = 0.0f;
    Dest->E[1][0] = 0.0f;
    Dest->E[1][1] = 1.0f;
    Dest->E[1][2] = 0.0f;
    Dest->E[2][0] = 0.0f;
    Dest->E[2][1] = 0.0f;
    Dest->E[2][2] = 1.0f;
}

unsigned long FPosQuatTypedChannel::BindHeader(const FPlayHeader* Header)
{
    TypedBind(this, Header);
    if (Kind != G_BindKindObject)
    {
        TypedStoreIdentityMatrix(&Orientation);
        Delta.x = 0.0f;
        Delta.y = 0.0f;
        Delta.z = 0.0f;
        return 0;
    }
    const Transform* Block = Header->Transform;
    FEngineInstance* Instance = reinterpret_cast<FEngineInstance*>((size_t)(this->Instance));
    if (Header->StartTime == 0.0f || (Header->LoopFlags & 0x40) != 0)
    {
        if (Block != nullptr)
        {
            std::memcpy(&Orientation, &Block->Rotation, sizeof(Orientation));
            TypedCopyVec3(&Delta.x, &Block->Translation.x);
            unsigned long Bits = 0;
            std::memcpy(&Bits, &Block->Translation.x, sizeof(Bits));
            return Bits;
        }
        const Matrix* Orient = G_AnimEngine->GetInstanceOrientation((InstanceIndex)Instance);
        std::memcpy(&Orientation, Orient, sizeof(Orientation));
        const Vector* Position = G_AnimEngine->GetInstancePosition((InstanceIndex)Instance);
        TypedCopyVec3(&Delta.x, &Position->x);
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
    const FPosQuatSample* const Sample = reinterpret_cast<const FPosQuatSample*>(Pose);
    const double Factor = (double)(TranslationScale) * (double)(Scale);
    Vector Pos;
    Pos.x = (float)((double)(Sample->Pos.x) * Factor);
    Pos.y = (float)((double)(Sample->Pos.y) * Factor);
    Pos.z = (float)((double)(Sample->Pos.z) * Factor);
    I3DMathEngine* Math = Acquire3DMathEngine();
    Vector Xformed;
    if (Block != nullptr)
    {
        std::memcpy(&Orientation, &Block->Rotation, sizeof(Orientation));
        MathTransformMatrix(Math, &Xformed, &Orientation, &Pos);
        Delta.x = Block->Translation.x - Xformed.x;
        Delta.y = Block->Translation.y - Xformed.y;
        Delta.z = Block->Translation.z - Xformed.z;
    }
    else
    {
        Matrix QuatMatrix;
        MathQuatToMatrix(Math, &QuatMatrix, &Sample->Quat);
        Matrix Transposed;
        Transposed.E[0][0] = QuatMatrix.E[0][0];
        Transposed.E[0][1] = QuatMatrix.E[1][0];
        Transposed.E[0][2] = QuatMatrix.E[2][0];
        Transposed.E[1][0] = QuatMatrix.E[0][1];
        Transposed.E[1][1] = QuatMatrix.E[1][1];
        Transposed.E[1][2] = QuatMatrix.E[2][1];
        Transposed.E[2][0] = QuatMatrix.E[0][2];
        Transposed.E[2][1] = QuatMatrix.E[1][2];
        Transposed.E[2][2] = QuatMatrix.E[2][2];
        const Matrix* Orient = G_AnimEngine->GetInstanceOrientation((InstanceIndex)Instance);
        Matrix Composed;
        MathMulMatrix(Math, &Composed, Orient, &Transposed);
        std::memcpy(&Orientation, &Composed, sizeof(Orientation));
        MathTransformMatrix(Math, &Xformed, &Orientation, &Pos);
        const Vector* Position = G_AnimEngine->GetInstancePosition((InstanceIndex)Instance);
        Delta.x = Position->x - Xformed.x;
        Delta.y = Position->y - Xformed.y;
        Delta.z = Position->z - Xformed.z;
    }
    unsigned long Bits = 0;
    std::memcpy(&Bits, &Delta.y, sizeof(Bits));
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
    if (This->Kind == G_BindKindJoint && G_AnimEngine != nullptr)
    {
        FEngineJointBlock* Relative = G_AnimEngine->GetInstanceRelative(
            This->Instance);
        unsigned long MixKind = 0;
        if (Relative != nullptr)
        {
            const unsigned long Case = Relative->Type - 1;
            if (Case == 0 || Case == 1)
                MixKind = G_MixKindScalar;
            else if (Case == 2)
                MixKind = 2;
            else if (Case == 4)
                MixKind = G_MixKindVec3;
            else if (Case == 3)
                MixKind = G_MixKindQuat;
            else if (Case == 5)
                MixKind = G_MixKindPosQuat;
        }
        if (MixKind * 4 == Bytes)
            return 1;
        return 0;
    }
    if (This->Kind == G_BindKindRoot)
        return Flags == G_ChannelFlagScalarOnly ? 1 : 0;
    return Bytes == 0x1C ? 1 : 0;
}

unsigned long FScalarTypedChannel::BindMap(unsigned long* Map, void* Bind)
{
    const unsigned long bBound = TypedBindMap(this, Map, Bind);
    if (bBound != 0)
    {
        FEngineJointBlock* Relative = G_AnimEngine->GetInstanceRelative(Map[3]);
        if (Relative != nullptr)
            JointType = Relative->Type;
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

    if (JointType == kJointTypeRev)
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
    Quaternion RightCopy;
    RightCopy.w = Right[0];
    RightCopy.x = Right[1];
    RightCopy.y = Right[2];
    RightCopy.z = Right[3];
    Quaternion LeftCopy;
    LeftCopy.w = Left[0];
    LeftCopy.x = Left[1];
    LeftCopy.y = Left[2];
    LeftCopy.z = Left[3];
    I3DMathEngine* Math = Acquire3DMathEngine();
    Quaternion Result;
    MathQuatSlerp(Math, &Result, &LeftCopy, &RightCopy, (float)Alpha);
    Out[0] = Result.w;
    Out[1] = Result.x;
    Out[2] = Result.y;
    Out[3] = Result.z;
    unsigned long ScaleBits = 0;
    std::memcpy(&ScaleBits, &Scale, sizeof(ScaleBits));
    if (ScaleBits == 0x3F800000)
        return (unsigned long)((size_t)Out);
    Result.w = 1.0f;
    Result.x = 0.0f;
    Result.y = 0.0f;
    Result.z = 0.0f;
    MathQuatSlerp(Math, &LeftCopy, &Result, reinterpret_cast<const Quaternion*>(Out), Scale);
    Out[0] = LeftCopy.w;
    Out[1] = LeftCopy.x;
    Out[2] = LeftCopy.y;
    Out[3] = LeftCopy.z;
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

unsigned long FTypedChannel::TouchMapped(Vector* A, Vector* B)
{
    (void)A;
    (void)B;
    return 0;
}

// LAB_06612170: slot 0x38 of every table except type-6's. Type-6's LAB_0661bd30 shows the
// second argument is a float, so the slot uses that one signature.
unsigned long FTypedChannel::Step(Vector* Out, float Dt)
{
    (void)Dt;
    if (Out == nullptr)
        return 0;
    Out->x = 0.0f;
    Out->y = 0.0f;
    Out->z = 0.0f;
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
    PlayFlags |= G_ChannelPlayFlag;
    if (Archetype == nullptr)
        return;
    unsigned long Flags = 0;
    Archetype->GetFlags(&Flags);
    if (Flags == G_ChannelFlagScalarOnly)
        return;
    float Time = this->Time;
    if ((PlayFlags & G_ChannelBlendFlag) != 0)
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
        if ((Mode & G_CollectIncludeStart) != 0)
        {
            Archetype->CollectAt(T0, Buf, Cap, &Found);
            *Count += Found;
        }
        if (T0 != T1)
        {
            Found = 0;
            Archetype->CollectRange(T0, T1, Buf + *Count * 2, Cap - *Count, &Found);
            *Count += Found;
            if ((Mode & G_CollectIncludeEnd) != 0)
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
    if ((Mode & G_CollectIncludeStart) != 0)
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
        if ((Mode & G_CollectIncludeEnd) != 0)
        {
            Archetype->CollectAt(EndTime, Buf + *Count * 2, Cap - *Count, &Found);
            *Count += Found;
        }
    }

    const unsigned long Loop = LoopFlags;
    const bool bForward = !(Rate < 0.0f);
    if ((Loop & G_LoopFlagLoop) != 0)
    {
        Found = 0;
        if (bForward)
            Collect(0.0f, (float)((double)T1 - Duration), G_CollectIncludeStart | G_CollectIncludeEnd,
                    Buf + *Count * 2, Cap - *Count, &Found);
        else
            Collect(Duration, (float)((double)Duration + T1), G_CollectIncludeStart | G_CollectIncludeEnd,
                    Buf + *Count * 2, Cap - *Count, &Found);
        *Count += Found;
        return 0;
    }
    if ((Loop & G_LoopFlagPingPong) != 0)
    {
        const float Turn = bForward ? Duration : 0.0f;
        Rate = -Rate;
        Found = 0;
        Collect(Turn, (float)((double)Turn - T1), G_CollectIncludeEnd,
                Buf + *Count * 2, Cap - *Count, &Found);
        *Count += Found;
        return 0;
    }
    Time = bForward ? Duration : 0.0f;
    PlayFlags |= G_ChannelBlendFlag;
    return 1;
}

// FUN_0661b420: advance the channel by Dt * Rate and pose it. Answers 1
// when a non-looping channel has run off its end this tick and nothing has posed it since
// (PlayFlags bit 0 clear); an owner-driven channel answers whatever its owner does.
unsigned long FTypedChannel::Tick(float Dt)
{
    unsigned long Result = 0;
    PlayFlags |= G_ChannelHoldFlag;
    if ((PlayFlags & (G_ChannelPlayFlag | G_ChannelBlendFlag)) != 0)
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
    if (Flags == G_ChannelFlagScalarOnly)
    {
        float Keys[0x40][2];
        unsigned long KeyCount = 0;
        Result = Collect(OldTime, Time, G_CollectIncludeStart | G_CollectIncludeEnd, &Keys[0][0], 0x40, &KeyCount);
        if (KeyCount == 0)
            return Result;
        IChannelSink* Sink = reinterpret_cast<IChannelSink*>(this->Sink);
        if (Sink == nullptr)
            return Result;
        FChannelKeyBatch Batch;
        Batch.Count = KeyCount;
        // Retail copies (count & 0x1FFFFFFF) * 2 dwords.
        std::memcpy(Batch.Keys, Keys, (KeyCount & 0x1FFFFFFFu) * 2 * sizeof(float));
        Sink->OnKeys((unsigned long)((size_t)this),
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
        if ((Loop & G_LoopFlagLoop) != 0)
        {
            // The applied pose (orientation, then position) and the matrix QuaternionToMatrix
            // writes, which is copied over the orientation.
            Transform Frame;
            Matrix Rebuilt;
            if (Kind == G_BindKindObject)
            {
                const unsigned char bLast = Rate < 0.0f ? 0 : 1;
                const unsigned char* FrameData = nullptr;
                Archetype->GetFrame(bLast, &FrameData);
                Apply(
                    (const float*)FrameData,
                    (const float*)FrameData,
                    (double)Dt);
                // Retail reads +0x78 / +0x84 straight off the object, whatever its class.
                FPosQuatTypedChannel* Pose = static_cast<FPosQuatTypedChannel*>(this);
                Frame.Translation.x = Pose->AppliedPos.x;
                Frame.Translation.y = Pose->AppliedPos.y;
                Frame.Translation.z = Pose->AppliedPos.z;
                I3DMathEngine* Math = Acquire3DMathEngine();
                Math->QuaternionToMatrix(Rebuilt, Pose->AppliedQuat);
                std::memcpy(&Frame.Rotation, &Rebuilt, sizeof(Matrix));
                IChannelSink* Sink = reinterpret_cast<IChannelSink*>(this->Sink);
                if (Sink != nullptr)
                {
                    Sink->OnLoop((unsigned long)((size_t)this),
                                     &Frame,
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
            Header.Transform = Kind == G_BindKindObject ? &Frame : nullptr;
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
        else if ((Loop & G_LoopFlagPingPong) != 0)
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
            PlayFlags |= G_ChannelBlendFlag;
            if ((PlayFlags & G_ChannelPlayFlag) == 0)
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
unsigned long FPosQuatTypedChannel::SetOverlayPos(const Vector* Target)
{
    if (Kind != G_BindKindObject)
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
    Vector Pos;
    Pos.x = P0Scaled * TranslationScale;
    Pos.y = P1Scaled * TranslationScale;
    Pos.z = (float)(P2Scaled * (double)(TranslationScale));
    Vector Xformed;
    MathTransformMatrix(Acquire3DMathEngine(), &Xformed, &Orientation, &Pos);
    Delta.x = Target->x - Xformed.x;
    Delta.y = Target->y - Xformed.y;
    Delta.z = Target->z - Xformed.z;
    AppliedPos.x = Target->x;
    AppliedPos.y = Target->y;
    AppliedPos.z = Target->z;
    unsigned long Bits = 0;
    std::memcpy(&Bits, &Target->z, sizeof(Bits));
    return Bits;
}

// LAB_0661bc80: lerp position, slerp quaternion (3DMath +0x58). Not IK.
unsigned long FPosQuatTypedChannel::InterpolatePose(float* Out, const float* Left, const float* Right, double Alpha)
{
    FPosQuatSample* const OutPose = reinterpret_cast<FPosQuatSample*>(Out);
    const FPosQuatSample* const LeftPose = reinterpret_cast<const FPosQuatSample*>(Left);
    const FPosQuatSample* const RightPose = reinterpret_cast<const FPosQuatSample*>(Right);
    OutPose->Pos.x = (float)(((double)(RightPose->Pos.x) - LeftPose->Pos.x) * Alpha + LeftPose->Pos.x);
    OutPose->Pos.y = (float)(((double)(RightPose->Pos.y) - LeftPose->Pos.y) * Alpha + LeftPose->Pos.y);
    OutPose->Pos.z = (float)(((double)(RightPose->Pos.z) - LeftPose->Pos.z) * Alpha + LeftPose->Pos.z);
    const float Blend = (float)Alpha;
    I3DMathEngine* Math = Acquire3DMathEngine();
    Quaternion Quat;
    MathQuatSlerp(Math, &Quat, &LeftPose->Quat, &RightPose->Quat, Blend);
    OutPose->Quat.w = Quat.w;
    OutPose->Quat.x = Quat.x;
    OutPose->Quat.y = Quat.y;
    OutPose->Quat.z = Quat.z;
    unsigned long Bits = 0;
    std::memcpy(&Bits, &Quat.x, sizeof(Bits));
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
    FPosQuatSample* const OutPose = reinterpret_cast<FPosQuatSample*>(Out);
    I3DMathEngine* Math = Acquire3DMathEngine();
    const Quaternion Identity = { 1.0f, 0.0f, 0.0f, 0.0f };
    Quaternion Slerped;
    MathQuatSlerp(Math, &Slerped, &Identity, &OutPose->Quat, Scale);
    OutPose->Quat.w = Slerped.w;
    OutPose->Quat.x = Slerped.x;
    OutPose->Quat.y = Slerped.y;
    OutPose->Quat.z = Slerped.z;
    unsigned long LastBits = 0;
    std::memcpy(&LastBits, &Slerped.z, sizeof(LastBits));
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
    const FPosQuatSample* const Sample = reinterpret_cast<const FPosQuatSample*>(Local);
    const unsigned long Kind = this->Kind;
    if (Kind == G_BindKindObject)
    {
        const double Factor = (double)(TranslationScale) * (double)(Scale);
        Vector Pos;
        Pos.x = (float)((double)(Sample->Pos.x) * Factor);
        Pos.y = (float)((double)(Sample->Pos.y) * Factor);
        Pos.z = (float)((double)(Sample->Pos.z) * Factor);
        I3DMathEngine* Math = Acquire3DMathEngine();
        Vector Xformed;
        MathTransformMatrix(Math, &Xformed, &Orientation, &Pos);
        AppliedPos.x = Xformed.x + Delta.x;
        AppliedPos.y = Xformed.y + Delta.y;
        AppliedPos.z = Xformed.z + Delta.z;
        Quaternion MatrixQuat;
        MathMatrixToQuat(Math, &MatrixQuat, &Orientation);
        Quaternion Composed;
        Math->MulQuat(Composed,
                              MatrixQuat,
                              Sample->Quat);   // +0x18
        AppliedQuat.w = Composed.w;
        AppliedQuat.x = Composed.x;
        AppliedQuat.y = Composed.y;
        AppliedQuat.z = Composed.z;
        unsigned long Bits = 0;
        std::memcpy(&Bits, &Composed.z, sizeof(Bits));
        return Bits;
    }
    if (Kind == G_BindKindJoint)
    {
        TypedCopyVec3(&AppliedPos.x, &Sample->Pos.x);
        AppliedQuat.w = Sample->Quat.w;
        AppliedQuat.x = Sample->Quat.x;
        AppliedQuat.y = Sample->Quat.y;
        AppliedQuat.z = Sample->Quat.z;
        unsigned long Bits = 0;
        std::memcpy(&Bits, &Sample->Quat.z, sizeof(Bits));
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
    Out[0] = Sampled.w;
    Out[1] = Sampled.x;
    Out[2] = Sampled.y;
    Out[3] = Sampled.z;
    return (unsigned long)((size_t)Out);
}

// LAB_066121d0, type-2 slot 6: 12 bytes from +0x3C. No null checks.
unsigned long FVectorTypedChannel::CopySampled(float* Out)
{
    TypedCopyVec3(Out, &Sampled.x);
    return (unsigned long)((size_t)Out);
}

// LAB_066121f0: 12 bytes from +0x78 and 16 from +0x84; returns Out+0xC. No null checks.
unsigned long FPosQuatTypedChannel::CopySampled(float* Out)
{
    TypedCopyVec3(Out, &AppliedPos.x);
    Out[3] = AppliedQuat.w;
    Out[4] = AppliedQuat.x;
    Out[5] = AppliedQuat.y;
    Out[6] = AppliedQuat.z;
    return (unsigned long)((size_t)(Out + 3));
}

// LAB_0661c6d0: Delta is at +0x60.
unsigned long FPosQuatTypedChannel::AddOverlayPos(const Vector* Add)
{
    Delta.x += Add->x;
    Delta.y += Add->y;
    Delta.z += Add->z;
    if ((PlayFlags & G_ChannelPlayFlag) != 0)
        ApplyFromArchetype();
    return (unsigned long)((size_t)Add);
}

// LAB_06612240: +0x78. No null check.
float* FPosQuatTypedChannel::GetSampled()
{
    return &AppliedPos.x;
}

// LAB_0661c5c0, the type-6 table's slot 0x30.
unsigned long FPosQuatTypedChannel::TouchMapped(Vector* OutPos, Vector* OutSampledPos)
{
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
    OutSampledPos->x = Pose[0];
    OutSampledPos->y = Pose[1];
    OutSampledPos->z = Pose[2];
    Vector Xformed;
    MathTransformMatrix(Acquire3DMathEngine(), &Xformed, &Orientation, OutSampledPos);
    OutPos->x = Xformed.x + Delta.x;
    OutPos->y = Xformed.y + Delta.y;
    OutPos->z = Xformed.z + Delta.z;
    return (unsigned long)((size_t)OutPos);
}

// One pass of LAB_0661c320 (retail inlines it twice).
static void TypedType6SampleXform(FPosQuatTypedChannel* This, float Time, const float** KeyA,
                                  const float** KeyB, float* Alpha, Vector* Xformed)
{
    This->Archetype->Sample(Time, (const unsigned char**)KeyA,
                            (const unsigned char**)KeyB, Alpha);
    const float* A = *KeyA;
    const float* B = *KeyB;
    Vector V;
    V.x = (B[0] - A[0]) * *Alpha;
    V.y = (B[1] - A[1]) * *Alpha;
    V.z = (B[2] - A[2]) * *Alpha;
    V.x = V.x + A[0];
    V.y = V.y + A[1];
    V.z = V.z + A[2];
    const double Factor = (double)(This->Scale) * (double)(This->TranslationScale);
    V.x = (float)((double)(V.x) * Factor);
    V.y = (float)((double)(V.y) * Factor);
    V.z = (float)((double)(V.z) * Factor);
    MathTransformMatrix(Acquire3DMathEngine(), Xformed, &This->Orientation, &V);
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
    Vector Xformed;
    TypedType6SampleXform(this, Time, &KeyA, &KeyB, &Alpha, &Xformed);
    Vector Now;
    Now.x = Xformed.x + Delta.x;
    Now.y = Xformed.y + Delta.y;
    Now.z = Xformed.z + Delta.z;
    TypedType6SampleXform(this, Shifted, &KeyA, &KeyB, &Alpha, &Xformed);
    const double S0 = (double)(Xformed.x) + (double)(Delta.x);
    const double S1 = (double)(Xformed.y) + (double)(Delta.y);
    const float S2 = (float)((double)(Xformed.z) + (double)(Delta.z));
    const float Dx = (float)(S0 - (double)(Now.x));
    const double Dy = S1 - (double)(Now.y);
    const double Dz = (double)S2 - (double)(Now.z);
    const double Inv = 1.0 / (double)Dt;
    StepDelta.x = (float)((double)Dx * Inv);
    StepDelta.y = (float)(Dy * Inv);
    StepDelta.z = (float)(Dz * Inv);
    unsigned long Bits = 0;
    std::memcpy(&Bits, &StepDelta.y, sizeof(Bits));
    return Bits;
}

// Round each position scaling step to float.
static void TypedType6ScaledFrame(FPosQuatTypedChannel* This, unsigned char bLast, Vector* Out)
{
    const float* Frame = nullptr;
    This->Archetype->GetFrame(bLast, (const unsigned char**)(&Frame));
    Vector Scaled;
    Scaled.x = This->Scale * Frame[0];
    Scaled.y = This->Scale * Frame[1];
    Scaled.z = This->Scale * Frame[2];
    Out->x = Scaled.x * This->TranslationScale;
    Out->y = Scaled.y * This->TranslationScale;
    Out->z = Scaled.z * This->TranslationScale;
}

// LAB_0661bd30, the type-6 table's slot 0x38.
unsigned long FPosQuatTypedChannel::Step(Vector* Out, float Dt)
{
    const float RatedDt = Dt * Rate;
    float Duration = 0.0f;
    Archetype->GetDuration(&Duration);
    Vector Step;
    Step.x = 0.0f;
    Step.y = 0.0f;
    Step.z = 0.0f;
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
    Vector Lerp;
    Lerp.x = KeyB[0] - KeyA[0];
    Lerp.y = KeyB[1] - KeyA[1];
    Lerp.z = KeyB[2] - KeyA[2];
    Lerp.x = Lerp.x * Alpha;
    Lerp.y = Lerp.y * Alpha;
    Lerp.z = Lerp.z * Alpha;
    Lerp.x = KeyA[0] + Lerp.x;
    Lerp.y = KeyA[1] + Lerp.y;
    Lerp.z = KeyA[2] + Lerp.z;
    const double Factor = (double)(Scale) * (double)(TranslationScale);
    Vector Now;
    Now.x = (float)((double)(Lerp.x) * Factor);
    Now.y = (float)((double)(Lerp.y) * Factor);
    Now.z = (float)((double)(Lerp.z) * Factor);

    if (bCrossed != 0 && (LoopFlags & G_LoopFlagLoop) != 0)
    {
        Vector EndA;
        TypedType6ScaledFrame(this, bReverse != 0 ? 0 : 1, &EndA);
        Step.x = EndA.x - Now.x;
        Step.y = EndA.y - Now.y;
        Step.z = EndA.z - Now.z;
        Vector EndB;
        TypedType6ScaledFrame(this, bReverse, &EndB);
        const float Leftover = bReverse != 0 ? Duration - SampleTime : SampleTime - Duration;
        Archetype->Sample(Leftover, (const unsigned char**)(&KeyA),
                                (const unsigned char**)(&KeyB), &Alpha);
        Lerp.x = KeyB[0] - KeyA[0];
        Lerp.y = KeyB[1] - KeyA[1];
        Lerp.z = KeyB[2] - KeyA[2];
        Lerp.x = Lerp.x * Alpha;
        Lerp.y = Lerp.y * Alpha;
        Lerp.z = Lerp.z * Alpha;
        Lerp.x = Lerp.x + KeyA[0];
        Lerp.y = Lerp.y + KeyA[1];
        Lerp.z = Lerp.z + KeyA[2];
        Vector Wrapped;
        Wrapped.x = (float)((double)(Lerp.x) * Factor - (double)(EndB.x));
        Wrapped.y = (float)((double)(Lerp.y) * Factor - (double)(EndB.y));
        Wrapped.z = (float)((double)(Lerp.z) * Factor - (double)(EndB.z));
        Step.x = Wrapped.x + Step.x;
        Step.y = Step.y + Wrapped.y;
        Step.z = Step.z + Wrapped.z;
    }
    else if (Archetype->Sample(SampleTime, (const unsigned char**)(&KeyA),
                                     (const unsigned char**)(&KeyB), &Alpha) == 0)
    {
        Vector Diff;
        Diff.x = KeyB[0] - KeyA[0];
        Diff.y = KeyB[1] - KeyA[1];
        Diff.z = KeyB[2] - KeyA[2];
        const float Shifted0 = (float)((double)(Diff.x) * (double)Alpha +
                                                  (double)(KeyA[0]));
        const float Shifted1 = (float)((double)(Diff.y) * (double)Alpha +
                                                  (double)(KeyA[1]));
        const float Diff2Alpha = Diff.z * Alpha;
        const float Shifted2 = Diff2Alpha + KeyA[2];
        const float Scaled1 = (float)((double)Shifted1 * Factor);
        const float Scaled2 = (float)((double)Shifted2 * Factor);
        Step.x = (float)((double)Shifted0 * Factor - (double)(Now.x));
        Step.y = Scaled1 - Now.y;
        Step.z = Scaled2 - Now.z;
    }
    // A failed Sample leaves Step zero.

    Vector Xformed;
    MathTransformMatrix(Acquire3DMathEngine(), &Xformed, &Orientation, &Step);
    Out->x = Xformed.x;
    Out->y = Xformed.y;
    Out->z = Xformed.z;
    return (unsigned long)((size_t)Out);
}

// LAB_0661cc40, the type-6 table's slot 9 (+0x24, .rdata:06629114).
unsigned long FPosQuatTypedChannel::SetOverlayExtra(const Matrix* Orient)
{
    if (Kind != G_BindKindObject)
        return 0;
    const unsigned char* KeyA = nullptr;
    const unsigned char* KeyB = nullptr;
    float Alpha = 0.0f;
    Archetype->Sample(Time, &KeyA, &KeyB, &Alpha);
    float Pose[7];
    Interpolate(Pose, (const float*)KeyA,
                                           (const float*)KeyB,
                                           (double)Alpha);
    Vector Pos;
    Pos.x = Pose[0];
    Pos.y = Pose[1];
    Pos.z = Pose[2];
    Quaternion Quat;
    Quat.w = Pose[3];
    Quat.x = Pose[4];
    Quat.y = Pose[5];
    Quat.z = Pose[6];
    FEngineInstance* Instance = reinterpret_cast<FEngineInstance*>((size_t)(this->Instance));
    const Matrix* InstanceOrient = G_AnimEngine->GetInstanceOrientation((InstanceIndex)Instance);
    Matrix Transposed;                                         // FUN_0661caa0
    Transposed.E[0][0] = InstanceOrient->E[0][0];
    Transposed.E[0][1] = InstanceOrient->E[1][0];
    Transposed.E[0][2] = InstanceOrient->E[2][0];
    Transposed.E[1][0] = InstanceOrient->E[0][1];
    Transposed.E[1][1] = InstanceOrient->E[1][1];
    Transposed.E[1][2] = InstanceOrient->E[2][1];
    Transposed.E[2][0] = InstanceOrient->E[0][2];
    Transposed.E[2][1] = InstanceOrient->E[1][2];
    Transposed.E[2][2] = InstanceOrient->E[2][2];
    Matrix R1;
    MathMulMatrix(Acquire3DMathEngine(), &R1, Orient, &Transposed);
    Matrix R2;
    MathMulMatrix(Acquire3DMathEngine(), &R2, &R1, &Orientation);
    Vector Xformed;
    MathTransformMatrix(Acquire3DMathEngine(), &Xformed, &Orientation, &Pos);
    Vector World;
    World.x = Xformed.x + Delta.x;
    World.y = Xformed.y + Delta.y;
    World.z = Xformed.z + Delta.z;
    MathTransformMatrix(Acquire3DMathEngine(), &Xformed, &R2, &Pos);
    Delta.x = World.x - Xformed.x;
    Delta.y = World.y - Xformed.y;
    Delta.z = World.z - Xformed.z;
    std::memcpy(&Orientation, &R2, sizeof(Orientation));
    MathTransformMatrix(Acquire3DMathEngine(), &Xformed, &Orientation, &Pos);
    AppliedPos.x = Xformed.x + Delta.x;
    AppliedPos.y = Xformed.y + Delta.y;
    AppliedPos.z = Xformed.z + Delta.z;
    Matrix QuatMatrix;
    MathQuatToMatrix(Acquire3DMathEngine(), &QuatMatrix, &Quat);
    Matrix R3;
    MathMulMatrix(Acquire3DMathEngine(), &R3, &R2, &QuatMatrix);
    MathMatrixToQuat(Acquire3DMathEngine(), &AppliedQuat, &R3);
    unsigned long Bits = 0;
    std::memcpy(&Bits, &AppliedQuat.z, sizeof(Bits));
    return Bits;
}

// The typed-channel vtable. The 15 slots follow retail's __thiscall table order
// (PTR_LAB_066294ac, PTR_LAB_0662912c/168/a4 and PTR_LAB_066290f0).
unsigned long FTypedChannel::Interpolate(float* Out, const float* Left, const float* Right, double Alpha) { return NoOpTypedChannelInterpolate(this, Out, Left, Right, Alpha); }
unsigned long FTypedChannel::BindHeader(const FPlayHeader* Header) { return TypedBind(this, Header); }
unsigned long FTypedChannel::InterpolatePose(float* Out, const float* Left, const float* Right, double Alpha) { return NoOpTypedChannelInterpolate(this, Out, Left, Right, Alpha); }
unsigned long FTypedChannel::BindMap(unsigned long* Map, void* Bind) { return TypedBindMap(this, Map, Bind); }
unsigned long FTypedChannel::SetOverlayPos(const Vector* Pos) { (void)Pos; return 0; }
unsigned long FTypedChannel::SetOverlayExtra(const Matrix* Extra) { (void)Extra; return 0; }
unsigned long FTypedChannel::AddOverlayPos(const Vector* Delta) { (void)Delta; return 0; }
float* FTypedChannel::GetSampled() { return nullptr; }
unsigned long FTypedChannel::UpdateDeltaRate(float Dt) { (void)Dt; return 0; }
// Type 1 (066291a4): slots 0/1/4/8..10/12..14 inherited (the shared bodies), 2 LAB_0661b880,
// 3 LAB_0661b8a0, 5 LAB_0661b870, 6 LAB_06612190, 7 FUN_0661b830, 11 LAB_066121a0.
unsigned long FScalarTypedChannel::Apply(const float* Left, const float* Right, double Alpha) { return ApplyInterpolatedChannelSample(this, Sampled, Left, Right, Alpha); }
float* FScalarTypedChannel::GetSampled() { return Sampled; } // LAB_066121a0 (+0x3C)

// Type 2 (06629168): 2 LAB_0661b880, 3 LAB_0661cfa0, 5 LAB_0661d040, 6 LAB_066121d0,
// 7 FUN_0661adc0 (the base's), 11 LAB_066121a0; the rest as the base.
unsigned long FVectorTypedChannel::Apply(const float* Left, const float* Right, double Alpha) { return ApplyInterpolatedChannelSample(this, &Sampled.x, Left, Right, Alpha); }
float* FVectorTypedChannel::GetSampled() { return &Sampled.x; } // LAB_066121a0

// Type 4 (0662912c): 2 LAB_0661b880, 3 LAB_0661b950, 5 LAB_0661b940, 6 LAB_066121b0,
// 7 FUN_0661adc0 (the base's), 11 LAB_066121a0; the rest as the base.
unsigned long FQuatTypedChannel::Apply(const float* Left, const float* Right, double Alpha) { return ApplyInterpolatedChannelSample(this, &Sampled.w, Left, Right, Alpha); }
float* FQuatTypedChannel::GetSampled() { return &Sampled.w; } // LAB_066121a0

// ---- Typed-channel construction (retail operator new + ctor chain) ----------------------------
static void TypedResetPlayState(FTypedChannel* This)
{
    This->Time = 0.0f;
    This->LoopFlags = 0;
    This->Rate = 1.0f;
    This->Scale = 1.0f;
    This->PlayFlags &= ~(G_ChannelPlayFlag | G_ChannelBlendFlag | G_ChannelHoldFlag);
    This->TranslationScale = 1.0f;
}

// FUN_0661ad50: +4 Owner, +8 Kind 2, +0x14 -1, +0xC / +0x10 = 0, +0x24 Archetype = 0, the
// type-8 table (PTR_LAB_066294ac), then FUN_0661acd0. Every other member keeps what the
// allocation held.
FTypedChannel::FTypedChannel(IChannelOwner* NewOwner)
{
    Owner = NewOwner;
    Kind = G_BindKindRoot;
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
    Kind = G_BindKindRoot;
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
    if (Type == G_TypedTypeScalar)
        return new FScalarTypedChannel(Owner);
    if (Type == G_TypedTypeVector)
        return new FVectorTypedChannel(Owner);
    if (Type == G_TypedTypeQuat)
        return new FQuatTypedChannel(Owner);
    if (Type == G_TypedTypePosQuat)
        return new FPosQuatTypedChannel(Owner);
    if (Type == G_TypedTypeBase)
        return new FTypedChannel(Owner);
    return nullptr;
}

FTypedChannel* NewTypedChannelWithArchetype(unsigned long Type, FChannelArchetypeObject* Archetype)
{
    if (Type == G_TypedTypeScalar)
        return new FScalarTypedChannel(Archetype);
    if (Type == G_TypedTypeVector)
        return new FVectorTypedChannel(Archetype);
    if (Type == G_TypedTypeQuat)
        return new FQuatTypedChannel(Archetype);
    if (Type == G_TypedTypePosQuat)
        return new FPosQuatTypedChannel(Archetype);
    if (Type == G_TypedTypeBase)
        return new FTypedChannel(Archetype);
    return nullptr;
}

// The IFileSystem calls every EngBase site makes inline through the vtable, without testing
// the pointer or the arguments.
int FileSystemOpenNode(IFileSystem* FileSystem, const char* Name)
{
    return FileSystem->OpenNode(Name); // +0x60
}

int FileSystemFindFirst(IFileSystem* FileSystem, const char* Pattern, WIN32_FIND_DATAA* FindData)
{
    return FileSystem->FindFirst(Pattern, // +0x48
                                            FindData);
}

int FileSystemFindNext(IFileSystem* FileSystem, int FindHandle, WIN32_FIND_DATAA* FindData)
{
    return FileSystem->FindNext((unsigned long)FindHandle, // +0x4C
                                            FindData);
}

void FileSystemFindClose(IFileSystem* FileSystem, int FindHandle)
{
    FileSystem->FindClose((unsigned long)FindHandle); // +0x50
}

void MathMulMatrix3(I3DMathEngine* Math, Matrix* Out, const Matrix* Left, const Matrix* Right)
{
    MathMulMatrix(Math, Out, Left, Right);
}

void MathTransformVec(I3DMathEngine* Math, Vector* Out, const Matrix* Rotation, const Vector* In)
{
    MathTransformMatrix(Math, Out, Rotation, In);
}

static I3DMathEngine* G_MathEngine = nullptr;

// FUN_06628190. The hr is not tested; the out local is uninitialised in retail, 0 here.
I3DMathEngine* Create3DMathEngine()
{
    FAggDescC Desc = {};
    Desc.Size = G_AggDescSizeMath;
    Desc.Name = G_Name3DMathEngine;
    void* Created = nullptr;
    DACOM_Acquire()->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &Created);
    return static_cast<I3DMathEngine*>(Created);
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
        Dacom->RegisterComponent(Factory, Name,
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
        static_cast<FAnimationFactory*>(std::calloc(1, sizeof(FAnimationFactory)));
    if (Animation != nullptr)
    {
        new (Animation) FAnimationFactory();
        RegisterFactory(Animation, G_NameAnimation, G_PriorityAnimation);
    }
    FChannelArchetypeFactory* ChannelArch = static_cast<FChannelArchetypeFactory*>(
        std::calloc(1, sizeof(FChannelArchetypeFactory)));
    if (ChannelArch != nullptr)
    {
        new (ChannelArch) FChannelArchetypeFactory();
        RegisterFactory(ChannelArch, G_NameChannelArchetype, G_PriorityChannelArchetype);
    }
    FHardpointFactory* Hardpoint =
        static_cast<FHardpointFactory*>(std::calloc(1, sizeof(FHardpointFactory)));
    if (Hardpoint != nullptr)
    {
        new (Hardpoint) FHardpointFactory();
        RegisterFactory(Hardpoint, G_NameHardpoint, G_PriorityHardpoint);
    }
    FEngineFactory* Engine = static_cast<FEngineFactory*>(std::calloc(1, sizeof(FEngineFactory)));
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
