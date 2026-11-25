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

static GENRESULT MathQuatToMatrix(I3DMathEngine* Math, float* Dst, const float* A)
{
    return Math->QuaternionToMatrix(*(FMatrix*)Dst, *(const FQuaternion*)A);
}

static GENRESULT MathQuatSlerp(I3DMathEngine* Math, float* Dst, const float* A, const float* B, float C)
{
    return Math->QuatSlerp(*(FQuaternion*)Dst, *(const FQuaternion*)A, *(const FQuaternion*)B, C);
}

static const int kFdumpNotice = 0x100003;
static const int kFdumpError = 0x100001;
static const int kFdumpWarning = 0x100002;
static const int kFdumpFatal = 0x100000;
static const char* const G_FdumpFmtNotice = "%s(%d) : NOTICE:General:%s";
static const char* const G_FdumpFmtError = "%s(%d) : ERROR:General:%s";
static const char* const G_FdumpFmtWarning = "%s(%d) : WARNING:General:%s";
// Pushed with 0x100000 (e.g. 06615567 / 0661556c).
static const char* const G_FdumpFmtFatal = "%s(%d) : FATAL:General:%s";
// .data:0662a208 (from the PE bytes): the __FILE__ that the
// "Unknown data type!" sites of IChannel::CreateTyped (06615a5f, line 0x44D) and
// CreateFromObject (06615c16, line 0x479) push.
static const char* const G_FdumpFileAnimComponent =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Anim\\AnimComponent.cpp";
// .data:0662a5c8 / .data:0662aac0 (from the PE bytes): the __FILE__ of every FDUMP site in
// engine.cpp (0661f96a..06622a5d) and HardPoint.cpp (06625d4d..06626dd8).
static const char* const G_FdumpFileEngine =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Engine\\engine.cpp";
static const char* const G_FdumpFileHardPoint =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Hardpoint\\HardPoint.cpp";
// .data:0662a0f0 (from the PE bytes): the __FILE__ the FDUMPs of FUN_06611b00 (0x176 / 0x189 /
// 0x193), FUN_06611e50 (0x1D8) and FUN_066124e0 (0x2F5 / 0x2FA) push.
static const char* const G_FdumpFileAnim =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Anim\\Anim.cpp";
// .data:0662a3c0 (from the PE bytes): the __FILE__ every FDUMP of FUN_0661db70
// (FChannelArchetypeObject::DecompressFrames) pushes.
static const char* const G_FdumpFileChannelArch =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Channel\\channel_arch.cpp";
static const char* const G_AnonymousFormat = "anonymous-%d";
static unsigned long G_AnonymousIndex = 0;
// .data:0662a51c / .data:0662a510 (PE bytes): the status word of the load_engine_components
// aggregation NOTICE (0661fb18 / 0661fb6c).
static const char* const G_StatusOk = "[OK]";
static const char* const G_StatusFailed = "[FAILED]";

// Local copy of DALib FFileSystemAggDesc (0x34). Do not include DALib.h.
struct FEngBaseFileSystemDesc
{
    unsigned long Size;
    const char* ComponentName;
    const char* BackendName;
    const char* DataPath;
    unsigned long Access;
    unsigned long ShareMode;
    unsigned long Security;
    unsigned long Creation;
    unsigned long Flags;
    unsigned long TemplateFile;
    unsigned long Parent;
    unsigned long Extra;
    unsigned long FindHandle;
};

// FEngineObject holds a real std::map (the instance-root map), so only the facet offset is
// asserted; the retail byte layout is documented on the struct.
static_assert(offsetof(FEngineObject, SearchPath) == 0x04, "Engine.SearchPath");
static_assert(sizeof(FConstraintRecord) == 0x64, "constraint record 0x64");
static_assert(offsetof(FConstraintRecord, ChildPart) == 0x00, "constraint.ChildPart");
static_assert(offsetof(FConstraintRecord, ParentPart) == 0x04, "constraint.ParentPart");
static_assert(offsetof(FConstraintRecord, Payload) == 0x08, "constraint.Payload");
static_assert(sizeof(FArchetypeChild) == 0x48, "archetype child size");
static_assert(offsetof(FArchetypeChild, Crc) == 0x40, "child.Crc");
static_assert(offsetof(FArchetypeChild, Handle) == 0x44, "child.Handle");
static_assert(sizeof(FHardpointRecord) == 0x50, "hardpoint list value 0x50 (node 0x58)");
static_assert(offsetof(FEngineChildNode, Name) == 0x08, "child node Name");
static_assert(offsetof(FEngineChildNode, Object) == 0x38, "child node Object");
static_assert(sizeof(FEngineChildNode) == 0x3C, "child node size");
static_assert(offsetof(FAnimationObject, NextPlaybackId) == 0x10, "Animation.NextPlaybackId");
static_assert(offsetof(FAnimationObject, Playbacks) == 0x14, "Animation.Playbacks");
static_assert(offsetof(FAnimationObject, Channels) == 0x28, "Animation.Channels");
static_assert(offsetof(FAnimationObject, Controller) == 0x50, "Animation.Controller");
static_assert(offsetof(FAnimationObject, Inner) == 0x54, "Animation.Inner");
static_assert(sizeof(FAnimationObject) == G_AnimationObjectSize, "Animation object size");
static_assert(offsetof(FChannelArchetypeObject, RefCount) == 0x1C, "ChannelArchetype.RefCount");
static_assert(offsetof(FChannelArchetypeObject, FrameCount) == 0x04, "ChannelArchetype.FrameCount");
static_assert(offsetof(FChannelArchetypeObject, Duration) == 0x18, "ChannelArchetype.Duration");
static_assert(sizeof(FChannelArchetypeObject) == G_ChannelArchetypeObjectSize,
              "ChannelArchetype object size");
static_assert(sizeof(FAggDesc18) == 0x18, "AGGDESC 0x18");
static_assert(sizeof(FEngBaseFileSystemDesc) == 0x34, "FileSystem AGGDESC 0x34");
static_assert(sizeof(FEngineInstance) == G_EngineInstanceSize, "instance size");
static_assert(offsetof(FEngineInstance, Archetype) == 0x08, "instance.Archetype");
static_assert(offsetof(FEngineInstance, ParentArchetype) == 0x0C, "instance.ParentArchetype");
static_assert(offsetof(FEngineInstance, Parent) == 0x10, "instance.Parent");
static_assert(offsetof(FEngineInstance, NextSibling) == 0x14, "instance.NextSibling");
static_assert(offsetof(FEngineInstance, FirstChild) == 0x1C, "instance.FirstChild");
static_assert(offsetof(FEngineInstance, LastChild) == 0x20, "instance.LastChild");
static_assert(offsetof(FEngineInstance, UserData) == 0x28, "instance.UserData");
static_assert(offsetof(FEngineInstance, Matrix) == 0x2C, "instance.Matrix");
static_assert(offsetof(FEngineInstance, Relative) == 0x5C, "instance.Relative");
static_assert(offsetof(FEngineInstance, JointState) == 0xB8, "instance.JointState");
static_assert(offsetof(FEngineInstance, bWorldDirty) == 0xD4, "instance.bWorldDirty");
static_assert(offsetof(FEngineInstance, PartChild) == 0xE8, "instance.PartChild");
static_assert(sizeof(FInstanceTransform) == G_TransformObjectSize, "transform size");
static_assert(offsetof(FInstanceTransform, Matrix) == 0x04, "transform.Matrix");
static_assert(offsetof(FInstanceTransform, Extra) == 0x34, "transform.Extra");
static_assert(offsetof(FInstanceTransform, User) == 0x44, "transform.User");
static_assert(sizeof(FAggDescC) == 0xC, "AGGDESC 0xC");

struct FInnerIidEntry
{
    const char* Iid;
    long Offset;
};

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
static char* FormatFdumpMessage(char* Buffer, const char* Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    _vsnprintf(Buffer, 0x2000, Format, Args);
    va_end(Args);
    return Buffer;
}

bool EngBaseIidEqual(const char* Left, const char* Right)
{
    if (Left == nullptr || Right == nullptr)
        return false;
    return std::strcmp(Left, Right) == 0;
}

// Node names occupy 0x30 bytes; preserve the existing bounded-copy policy.
static void BuildNodeName(char* Buffer, const char* Source)
{
    if (Source == nullptr)
    {
        Buffer[0] = '\0';
        return;
    }
    const size_t Length = std::strlen(Source) + 1;
    std::memcpy(Buffer, Source, Length < 0x30 ? Length : 0x30);
}

// FUN_0661d700: always 0; factories destroy the object if nonzero.
static int DACOM_API EngBaseAggregationHook(void* Desc)
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
    if (!EngBaseIidEqual(Iid, Iids::ComponentFactory))
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
static GENRESULT DACOM_API StubRet8(void* This, void* A)
{
    (void)This;
    (void)A;
    return GR_GENERIC;
}

static GENRESULT DACOM_API StubRetC(void* This, void* A, void* B)
{
    (void)This;
    (void)A;
    (void)B;
    return GR_GENERIC;
}

// LAB_06626cd0 (IEC+0x20): returns 0.
static GENRESULT DACOM_API NoOpHardpointInstance(void* Nested, void* A, void* B)
{
    (void)Nested;
    (void)A;
    (void)B;
    return GR_OK;
}

// LAB_06626cf0 (IEC+0x1C and +0x2C): returns -1.
static GENRESULT DACOM_API RejectHardpointQuery(void* Nested, void* A, const void* ArgIid, void* C)
{
    (void)Nested;
    (void)A;
    (void)ArgIid;
    (void)C;
    return (GENRESULT)(-1);
}

// LAB_06627ef0, ICF-shared by six inner/factory vtables. No null check of this.
unsigned long DACOM_API FAnimationInner::AddRef()
{
    RefCount += 1;
    return RefCount;
}

unsigned long DACOM_API FHardpointInner::AddRef()
{
    RefCount += 1;
    return RefCount;
}

// _strdup + _strlwr, the pair every hardpoint-record builder calls through the MSVCRT imports.
static char* DuplicateLoweredName(char const* Name)
{
    char* Copy = _strdup(Name);
    _strlwr(Copy);
    return Copy;
}

// FUN_06626310 (create_archetype, 0662604b) / FUN_066265e0 behind DAT_0662ace4 (add_hardpoint).
FHardpointRecord::FHardpointRecord(unsigned long InCrc, char const* InName)
    : Crc(InCrc)
    , Name(DuplicateLoweredName(InName != nullptr ? InName : ""))
{
    std::memset(Data, 0, sizeof(Data));
}

// FUN_06627f70 / the element loops of FUN_06627d60 and FUN_06627fe0: Crc and blob copied, the
// name duplicated + lowercased only when the source has one.
FHardpointRecord::FHardpointRecord(FHardpointRecord const& Other)
    : Crc(Other.Crc)
    , Name(nullptr)
{
    std::memcpy(Data, Other.Data, sizeof(Data));
    if (Other.Name != nullptr)
        Name = DuplicateLoweredName(Other.Name);
}

// FUN_066280b0's per-node step: free the name when there is one.
FHardpointRecord::~FHardpointRecord()
{
    if (Name != nullptr)
        std::free(Name);
}

// FUN_06625c70 (ctor): the map is set up empty, +0x20 / +0x24 / +0x08 / +0x0C zeroed and the
// cache byte cleared. Called on calloc'd memory through placement new.
void FHardpointObject::Construct()
{
    new (this) FHardpointObject();
    LookupCount = 0;
    CacheHits = 0;
    Field_0x24 = 0;
    bCacheValid = 0;
}

// FUN_06625d00 (dtor, thiscall). Reports each archetype still in the map, in ascending key order.
static void HardpointDestruct(FHardpointObject* Object)
{
    for (FHardpointMap::iterator It = Object->Archetypes.begin(); It != Object->Archetypes.end();
         ++It)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpNotice, G_FdumpFileHardPoint, 0xC8, "NOTICE",
                     FormatFdumpMessage(Message,
                                        "Hardpoint: dtor: Archetype %d has dangling hardpoints\n",
                                        It->first));
    }
    Object->Engine = nullptr;
    Object->~FHardpointObject();
}

static void DestroyAnimationLists(FAnimationObject* Object);

// LAB_06627f00. Unsigned decrement guarded at zero; at zero the count is reset to 1 and the
// object (dtor FUN_06625d00) is freed.
unsigned long DACOM_API FHardpointInner::Release()
{
    if (RefCount > 0)
        RefCount -= 1;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    FHardpointObject* Owner = Object;
    if (Owner != nullptr)
    {
        HardpointDestruct(Owner);
        std::free(Owner);
    }
    return 0;
}

// LAB_0661a940. Same shape as FHardpointInner::Release; object pointer at +8, dtor FUN_06616d90.
unsigned long DACOM_API FAnimationInner::Release()
{
    if (RefCount > 0)
        RefCount -= 1;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    FAnimationObject* Owner = Object;
    if (Owner != nullptr)
    {
        DestroyAnimationLists(Owner);
        std::free(Owner);
    }
    return 0;
}

// The {iid, offset} walk both inner unknowns inline (LAB_0661a880, LAB_06627e30).
static GENRESULT InnerQueryTable(void* Object, const char* Iid, void** Out,
                                 const FInnerIidEntry* Table)
{
    for (int Index = 0; Table[Index].Iid != nullptr; ++Index)
    {
        if (std::strcmp(Table[Index].Iid, Iid) != 0)
            continue;
        unsigned char* Base = (unsigned char*)Object;
        const long Offset = Table[Index].Offset;
        IDAComponent* Interface = nullptr;
        if (Offset < 0)
            std::memcpy(&Interface, Base + ((unsigned long)Offset & 0x7FFFFFFFu),
                        sizeof(Interface));
        else
            Interface = (IDAComponent*)(Base + Offset);
        Interface->AddRef();
        *Out = Interface;
        return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

// Inner QI table @ 06629478 (PE bytes): {IAnimation2, 0}, {IAnimation, 0}, {IChannel, 8},
// {IRootData, 0xC}, {IAggregateComponent, 4}, then a null iid.
static const FInnerIidEntry G_AnimationInnerIids[] = {
    {Iids::Animation2, 0},
    {Iids::Animation, 0},
    {Iids::Channel, 0x08},
    {Iids::RootData, 0x0C},
    {Iids::AggregateComponent, 0x04},
    {nullptr, 0},
};

// Inner QI table @ 06629844 (PE bytes): {IHardpoint, 0}, {IEngineComponent, 4},
// {IAggregateComponent, 4}, then a null iid. IAggregateComponent answers with the IEC facet.
static const FInnerIidEntry G_HardpointInnerIids[] = {
    {Iids::Hardpoint, 0},
    {Iids::EngineComponent, 0x04},
    {Iids::AggregateComponent, 0x04},
    {nullptr, 0},
};

// LAB_0661a880 (inner QI, RET 0xC) over 06629478; the facet offsets are added to the object
// pointer at inner +8.
GENRESULT DACOM_API FAnimationInner::QueryInterface(const char* Iid, void** Out)
{
    return InnerQueryTable(Object, Iid, Out, G_AnimationInnerIids);
}

// LAB_06627e30 (inner QI, RET 0xC) over 06629844.
GENRESULT DACOM_API FHardpointInner::QueryInterface(const char* Iid, void** Out)
{
    return InnerQueryTable(Object, Iid, Out, G_HardpointInnerIids);
}

// EngBase plumbs the FileSystem component around as void*; this is the one place that
// states what it really is, so every call below goes through ReadFile's published ABI.
static IFileSystem* AsFileSystem(void* FileSystem)
{
    return (IFileSystem*)FileSystem;
}

static int FileSystemOpenNode(void* FileSystem, const char* Name);
static int FileSystemFindFirst(void* FileSystem, const char* Pattern, WIN32_FIND_DATAA* FindData);
static int FileSystemFindNext(void* FileSystem, int FindHandle, WIN32_FIND_DATAA* FindData);
static void FileSystemFindClose(void* FileSystem, int FindHandle);

// LAB_0661a980 / LAB_0661a990 / LAB_0661a9a0 (IAnimation2 slots 0-2, also reached from the
// three facet thunks): forward to the controller at +0x50. No checks.
static GENRESULT DACOM_API AnimationQueryInterface(FAnimationObject* This, const char* Iid,
                                                  void** Out)
{
    return This->Controller->QueryInterface(Iid, Out);
}

unsigned long DACOM_API FAnimationObject::AddRef()
{
    return Controller->AddRef();
}

unsigned long DACOM_API FAnimationObject::Release()
{
    return Controller->Release();
}

static FAnimationObject* AnimationFromNested(void* Nested, unsigned long Offset)
{
    return (FAnimationObject*)((unsigned char*)Nested - Offset);
}

// Shared engine cache; Engine2 has its own QI output slot.
FEngineObject* G_AnimEngine = nullptr;
static FEngineObject* G_AnimEngine2 = nullptr;

// LAB_06616d40
GENRESULT DACOM_API FAnimationObject::Initialize()
{
    void* Nested = (IAggregateComponent*)this;
    FAnimationObject* Object = AnimationFromNested(Nested, 4);
    GENRESULT Result =
        AnimationQueryInterface(Object, Iids::Engine, (void**)(&G_AnimEngine));
    if (Result != 0)
        return Result;
    G_AnimEngine->Release();
    Result =
        AnimationQueryInterface(Object, Iids::Engine2, (void**)(&G_AnimEngine2));
    if (Result != 0)
        return Result;
    G_AnimEngine2->Release();
    return GR_OK;
}

static FAnimScript* const G_InvalidAnimScript =
    (FAnimScript*)((ptrdiff_t)(-1));

struct FAnimHandleNode
{
    FAnimHandleNode* Next;
    unsigned long Key;
};

static FAnimHandleNode* G_AnimHandleHead = nullptr;

static void FillFileSystemDesc(FEngBaseFileSystemDesc* Desc, const char* Path, const char* Backend);
static unsigned long DACOM_API ChannelArchetypeAddRef(FChannelArchetypeObject* This);
static FAnimNameEntry* FindAnimName(FAnimScript* Script, unsigned long Crc);
static unsigned char FindPlaybackRecord(FAnimationObject* Animation, unsigned long Key,
                                        FAnimPlayback** Out);
static void BindPlaybackTracks(FAnimPlayback* Playback, FEngineInstance* Root, void* Sink,
                               void* SinkContext);

static void UnregisterAnimHandle(unsigned long Key);

static bool IsValidAnimScript(FAnimScript* Script)
{
    return Script != G_InvalidAnimScript && Script != nullptr;
}

unsigned long DACOM_API FAnimScript::RejectOperation(void* ArgA, void* ArgB)
{
    (void)ArgA;
    (void)ArgB;
    return 0xFFFFFFFF;
}

unsigned long DACOM_API FAnimScript::AddRef()
{
    RefCount += 1;
    return RefCount;
}

unsigned long DACOM_API FAnimScript::Release()
{
    RefCount -= 1;
    const unsigned long Left = RefCount;
    if (Left != 0)
        return Left;
    delete this;
    return 0;
}

FAnimTrack::FAnimTrack(const FAnimTrack& Other)
    : Channel(Other.Channel)
    , ParentCrc(Other.ParentCrc)
    , ChildCrc(Other.ChildCrc)
    , ParentName(nullptr)
    , ChildName(nullptr)
{
    if (Channel != nullptr)
        Channel->AddRef();
    if (Other.ParentName != nullptr)
        ParentName = _strdup(Other.ParentName);
    if (Other.ChildName != nullptr)
        ChildName = _strdup(Other.ChildName);
}

FAnimTrack::~FAnimTrack()
{
    if (ParentName != nullptr)
        std::free(ParentName);
    if (ChildName != nullptr)
        std::free(ChildName);
    if (Channel != nullptr)
    {
        Channel->Release();
        Channel = nullptr;
    }
}

FAnimNameEntry::~FAnimNameEntry()
{
    std::free(Name);
    std::vector<FAnimTrack>().swap(Tracks);
    if (LongestChannel != nullptr)
    {
        LongestChannel->Release();
        LongestChannel = nullptr;
    }
}

static FAnimScript* AllocAnimScript()
{
    return new (std::nothrow) FAnimScript();
}

static FAnimNameEntry* InsertAnimName(FAnimScript* Script, unsigned long Crc)
{
    return &Script->Names[Crc];
}

static FTypedChannel* FindTypedChannel(FAnimationObject* Animation, unsigned long Key)
{
    if (Key == 0xFFFFFFFF)
        return nullptr;
    FTypedChannel* Wanted = (FTypedChannel*)((size_t)Key);
    FTypedChannelSet::iterator It = Animation->Channels->find(Wanted);
    if (It == Animation->Channels->end())
        return nullptr;
    return *It;
}

static void DestroyAnimationLists(FAnimationObject* Object)
{

    while (G_AnimHandleHead != nullptr)
    {
        FAnimHandleNode* Next = G_AnimHandleHead->Next;
        std::free(G_AnimHandleHead);
        G_AnimHandleHead = Next;
    }

    Object->MixerDestroyAll();
    delete Object->Channels;
    Object->Channels = nullptr;

    delete Object->Playbacks;
    Object->Playbacks = nullptr;
}

static unsigned long G_JointMapLength = 0;
static unsigned long G_ObjectMapLength = 0;
static unsigned long G_EventMapLength = 0;

static void StoreAnimMapNameLengths()
{
    G_JointMapLength = (unsigned long)(std::strlen(G_NameJointMap));
    G_ObjectMapLength = (unsigned long)(std::strlen(G_NameObjectMap));
    G_EventMapLength = (unsigned long)(std::strlen(G_NameEventMap));
}

static unsigned long LoadAnimScriptNames(FAnimScript* Script, void* FileSystem)
{
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameScript) == 0)
        return 0;
    StoreAnimMapNameLengths();
    WIN32_FIND_DATAA FindData;
    const int FindHandle = Fs->FindFirst(G_NameStar,
                                      &FindData);
    if (FindHandle != -1)
    {
        do
        {
            if (FindData.cFileName[0] == '.')
                continue;
            FEngBaseFileSystemDesc Desc;
            FillFileSystemDesc(&Desc, FindData.cFileName, nullptr);
            Desc.FindHandle = (unsigned long)FindHandle;
            void* NestedFs = nullptr;
            Fs->CreateInstance((AGGDESC*)(&Desc), &NestedFs);
            if (((FindData.dwFileAttributes >> 4) & 1) != 0)
            {
                FAnimNameEntry* Entry =
                    InsertAnimName(Script, DACOM_CRC::GetCRC32(FindData.cFileName));
                Entry->LoadScriptMaps(FindData.cFileName, NestedFs);
            }
            if (NestedFs != nullptr)
                ((IDAComponent*)NestedFs)->Release();
        } while (Fs->FindNext((unsigned long)FindHandle,
                            &FindData) != 0);
        Fs->GetFileSystemError();
        Fs->FindClose((unsigned long)FindHandle);
    }
    Fs->OpenNode(G_NameParentDot);
    return 0;
}

static unsigned long LoadAnimScriptsByNameList(FAnimScript* Script, void* FileSystem,
                                              const char** Names, int Count)
{
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameScript) == 0)
        return 0;
    StoreAnimMapNameLengths();
    for (int Index = 0; Index < Count; ++Index)
    {
        const char* Name = Names[Index];
        if (Name == nullptr)
        {
            EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x176, "WARNING",
                         "Null name in animation sub set. Aborting subset load.\n");
            break;
        }
        char Message[0x2000];
        if ((Fs->GetPathAttributes(Name) & 0x10) == 0)
        {
            FormatFdumpMessage(Message,"Unknown or old style animation \"%s\". Skipping.\n", Name);
            EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x193, "WARNING", Message);
            continue;
        }
        if (FindAnimName(Script, DACOM_CRC::GetCRC32(Name)) != nullptr)
            continue;
        FEngBaseFileSystemDesc Desc;
        FillFileSystemDesc(&Desc, Name, nullptr);
        void* NestedFs = nullptr;
        if (Fs->CreateInstance((AGGDESC*)(&Desc), &NestedFs) == 0)
        {
            FAnimNameEntry* Entry = InsertAnimName(Script, DACOM_CRC::GetCRC32(Name));
            Entry->LoadScriptMaps(Name, NestedFs);
        }
        else
        {
            FormatFdumpMessage(Message,"Failed to open animation \"%s\". Skipping.\n", Name);
            EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x189, "WARNING", Message);
        }
        if (NestedFs != nullptr)
            ((IDAComponent*)NestedFs)->Release();
    }
    Fs->OpenNode(G_NameParentDot);
    return 0;
}

void* DACOM_API FAnimationObject::OpenScript(void* FileSystem)
{
    FAnimScript* Script = AllocAnimScript();
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameAnimation) == 0)
    {
        Script->Release();
        return G_InvalidAnimScript;
    }
    LoadAnimScriptNames(Script, FileSystem);
    Fs->OpenNode(G_NameParentDot);
    return Script;
}

GENRESULT DACOM_API FAnimationObject::CloseScript(void* InScript)
{
    FAnimScript* Script = (FAnimScript*)InScript;

    if (!IsValidAnimScript(Script))
        return (GENRESULT)((size_t)Script);
    return (GENRESULT)(Script->Release());
}

void* DACOM_API FAnimationObject::CreateEmptyScript()
{

    return AllocAnimScript();
}

GENRESULT DACOM_API FAnimationObject::EnumScriptNames(void* Callback, void* InScript, void* Context)
{
    FAnimScript* Script = (FAnimScript*)InScript;

    if (Callback == nullptr || !IsValidAnimScript(Script))
        return GR_OK;
    using FnCb = void(__cdecl*)(const char*, void*);
    FnCb Enum = (FnCb)Callback;
    FAnimNameMap::iterator It = Script->Names.begin();
    while (It != Script->Names.end())
    {
        Enum(It->second.Name, Context);
        ++It;
    }
    return GR_OK;
}

GENRESULT DACOM_API FAnimationObject::FindScriptName(void* Callback, void* InScript, const char* Name,
                                                     void* Context)
{
    FAnimScript* Script = (FAnimScript*)InScript;
    const GENRESULT Miss = (GENRESULT)0xFFFFFFFE;
    if (Callback == nullptr || !IsValidAnimScript(Script))
        return Miss;
    FAnimNameEntry* Entry = FindAnimName(Script, DACOM_CRC::GetCRC32(Name));
    if (Entry == nullptr)
        return Miss;
    using FnCb = void(__cdecl*)(void*, void*);
    FnCb Enum = (FnCb)Callback;
    std::vector<FAnimTrack>::iterator Track = Entry->Tracks.begin();
    while (Track != Entry->Tracks.end())
    {
        if (Track->Channel != nullptr)
        {
            ChannelArchetypeAddRef(Track->Channel);
            Enum((IChannelArchetype*)(Track->Channel), Context);
        }
        ++Track;
    }
    return 0;
}

static unsigned char FindPlaybackRecord(FAnimationObject* Animation, unsigned long Key,
                                        FAnimPlayback** Out)
{
    FAnimPlaybackMap::iterator It = Animation->Playbacks->find((long)Key);
    if (It == Animation->Playbacks->end())
        return 0;
    *Out = &It->second;
    return 1;
}

GENRESULT DACOM_API FAnimationObject::DestroyScript(void* InScript)
{
    unsigned long PlaybackId = (unsigned long)((size_t)InScript);
    FAnimPlaybackMap::iterator It = Playbacks->find((long)PlaybackId);
    if (It == Playbacks->end())
        return GR_OK;
    FAnimPlayback* Playback = &It->second;
    FAnimScript* Script = Playback->Script;
    std::vector<FTypedChannel*>::iterator Cursor = Playback->Typed.begin();
    while (Cursor != Playback->Typed.end())
    {
        MixerUnbindTyped(this, *Cursor);
        Channels->erase(*Cursor);
        ++Cursor;
    }
    Playback->FreeTyped();
    Playbacks->erase(It);
    Script->Release();
    return GR_OK;
}

// Slots 5, 10 and 17 take a playback id, not a script (FUN_06615160 / LAB_06614a50 /
// LAB_066148d0 all open with FUN_06614580 or FUN_06612e70 on the +0x14 map); the shared
// header still carries the dword as void*.
float DACOM_API FAnimationObject::GetScriptDuration(void* Script)
{
    unsigned long PlaybackId = (unsigned long)((size_t)Script);
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0.0f;
    float Duration = -1.0f;
    Playback->Name->LongestChannel->GetDuration(&Duration);
    return Duration;
}

float DACOM_API FAnimationObject::GetNamedDuration(void* InScript, const char* Name)
{
    FAnimScript* Script = (FAnimScript*)InScript;
    if (!IsValidAnimScript(Script))
        return -1.0f;
    FAnimNameEntry* Entry = FindAnimName(Script, DACOM_CRC::GetCRC32(Name));
    if (Entry == nullptr)
        return -1.0f;
    float Duration = -1.0f;
    Entry->LongestChannel->GetDuration(&Duration);
    return Duration;
}

float DACOM_API FAnimationObject::GetStoredDuration(unsigned long PlaybackId)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return -1.0f;
    return Playback->StoredDuration();
}

unsigned long DACOM_API FAnimationObject::HasScriptName(void* InScript, const char* Name)
{
    FAnimScript* Script = (FAnimScript*)InScript;
    if (!IsValidAnimScript(Script) || Name == nullptr)
        return 0xFFFFFFFF;

    const unsigned long Crc = DACOM_CRC::GetCRC32(Name);
    if (Script->Names.find(Crc) != Script->Names.end())
        return 0;
    return 0xFFFFFFFF;
}

unsigned long DACOM_API FAnimationObject::GetScriptNameCount(void* InScript, unsigned long* Out)
{
    FAnimScript* Script = (FAnimScript*)InScript;

    if (!IsValidAnimScript(Script))
        return 0xFFFFFFFF;
    *Out = (unsigned long)(Script->Names.size());
    return 0;
}

static FAnimNameEntry* FindAnimName(FAnimScript* Script, unsigned long Crc)
{
    FAnimNameMap::iterator It = Script->Names.find(Crc);
    if (It == Script->Names.end())
        return nullptr;
    return &It->second;
}

static bool RemoveAnimNameByCrc(FAnimScript* Script, unsigned long Crc)
{
    return Script->Names.erase(Crc) != 0;
}

static bool FindAnimHandle(unsigned long Key)
{
    FAnimHandleNode* Node = G_AnimHandleHead;
    while (Node != nullptr)
    {
        if (Node->Key == Key)
            return true;
        Node = Node->Next;
    }
    return false;
}

static unsigned long RegisterAnimHandle(unsigned long Key)
{
    if (Key == 0xFFFFFFFF)
        return 0xFFFFFFFF;
    if (FindAnimHandle(Key))
        return 0;
    FAnimHandleNode* Node = (FAnimHandleNode*)(std::calloc(1, sizeof(FAnimHandleNode)));
    if (Node == nullptr)
        return 0xFFFFFFFF;
    Node->Key = Key;
    Node->Next = G_AnimHandleHead;
    G_AnimHandleHead = Node;
    return 0;
}

static void UnregisterAnimHandle(unsigned long Key)
{
    FAnimHandleNode* Prev = nullptr;
    FAnimHandleNode* Node = G_AnimHandleHead;
    while (Node != nullptr)
    {
        if (Node->Key == Key)
        {
            if (Prev != nullptr)
                Prev->Next = Node->Next;
            else
                G_AnimHandleHead = Node->Next;
            std::free(Node);
            return;
        }
        Prev = Node;
        Node = Node->Next;
    }
}

unsigned long DACOM_API FAnimationObject::FilterObjects(void* Filter)
{
    unsigned long PlaybackId = (unsigned long)((size_t)Filter);
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0;
    IChannel* ChannelFacet = (IChannel*)this;
    return Playback->FilterTyped(ChannelFacet);
}

unsigned long DACOM_API FAnimationObject::HasObject(unsigned long PlaybackId, void* Object)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0;
    return Playback->HasTyped(Object);
}

unsigned long DACOM_API FAnimationObject::GetFirstInstanceField(unsigned long PlaybackId,
                                                               unsigned long* Out)
{
    FAnimPlaybackMap::iterator It = Playbacks->find((long)PlaybackId);
    if (It == Playbacks->end())
        return 0;
    FAnimNameMap& Names = It->second.Script->Names;
    FAnimNameMap::iterator Cursor = Names.begin();
    while (Cursor != Names.end())
    {
        if (&Cursor->second == It->second.Name)
        {
            *Out = (unsigned long)((size_t)(Cursor->second.Name));
            return 1;
        }
        ++Cursor;
    }
    return 0;
}

unsigned long DACOM_API FAnimationObject::GetDurationIfNamed(unsigned long PlaybackId, float* Out)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0;
    if (Playback->Name == nullptr)
        return 0;
    if (Playback->Name->bHasRootHeight == 0)
        return 0;
    *Out = Playback->Name->RootHeight;
    return 1;
}

unsigned long DACOM_API FAnimationObject::GetNamedDurationOut(void* InScript, const char* Name, float* Out)
{
    FAnimScript* Script = (FAnimScript*)InScript;
    if (!IsValidAnimScript(Script))
        return 0;
    FAnimNameEntry* Entry = FindAnimName(Script, DACOM_CRC::GetCRC32(Name));
    if (Entry == nullptr)
        return 0;
    if (Entry->bHasRootHeight == 0)
        return 0;
    *Out = Entry->RootHeight;
    return 1;
}

unsigned long DACOM_API FAnimationObject::FindObject(unsigned long PlaybackId, void* Query)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0xFFFFFFFF;
    return (unsigned long)(
        Playback->FindTypedByHeader((const FAnimBindOut*)Query));
}

unsigned long DACOM_API FAnimationObject::AddScriptNames(void* InScript, void* FileSystem,
                                                       const char** Names, int Count)
{
    FAnimScript* Script = (FAnimScript*)InScript;
    if (!IsValidAnimScript(Script))
        return 0xFFFFFFFF;
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameAnimation) == 0)
        return 0xFFFFFFFF;
    const unsigned long Result = LoadAnimScriptsByNameList(Script, FileSystem, Names, Count);
    Fs->OpenNode(G_NameParentDot);
    return Result;
}

unsigned long DACOM_API FAnimationObject::RemoveScriptNames(void* InScript, const char** Names, int Count)
{
    FAnimScript* Script = (FAnimScript*)InScript;
    if (!IsValidAnimScript(Script))
        return 0xFFFFFFFF;
    for (int Index = 0; Index < Count; ++Index)
    {
        const char* Name = Names[Index];
        if (Name == nullptr)
            continue;
        if (!RemoveAnimNameByCrc(Script, DACOM_CRC::GetCRC32(Name)))
        {
            char Message[0x2000];
            FormatFdumpMessage(Message,"Failed to remove animation \"%s\".\n", Name);
            EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x1D8, "WARNING", Message);
        }
    }
    return 0;
}

unsigned long DACOM_API FAnimationObject::EnumFsScripts(void* Callback, void* FileSystem, void* Context)
{

    if (FileSystemOpenNode(FileSystem, G_NameAnimation) == 0)
        return 0xFFFFFFFF;

    if (FileSystemOpenNode(FileSystem, G_NameScript) == 0)
    {
        FileSystemOpenNode(FileSystem, G_NameParentDot);
        return 0xFFFFFFFF;
    }

    WIN32_FIND_DATAA FindData = {};
    const int FindHandle = FileSystemFindFirst(FileSystem, G_NameStar, &FindData);
    if (FindHandle != -1)
    {
        using FnCb = void(__cdecl*)(const char*, void*);
        FnCb Enum = (FnCb)Callback;
        do
        {

            if (FindData.cFileName[0] != '.' &&
                (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                Enum(FindData.cFileName, Context);
            }
        } while (FileSystemFindNext(FileSystem, FindHandle, &FindData) != 0);

        AsFileSystem(FileSystem)->GetFileSystemError();
        FileSystemFindClose(FileSystem, FindHandle);
    }
    FileSystemOpenNode(FileSystem, G_NameParentDot);
    FileSystemOpenNode(FileSystem, G_NameParentDot);
    return 0;
}

unsigned long DACOM_API FAnimationObject::EnumObjects(void* Callback, unsigned long PlaybackId,
                                                       void* Context)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0xFFFFFFFF;
    using FnEnum = unsigned char(__cdecl*)(void* Object, void* User);
    FnEnum Enum = (FnEnum)Callback;
    std::vector<FTypedChannel*>::iterator Cursor = Playback->Typed.begin();
    while (Cursor != Playback->Typed.end())
    {
        if (Enum(*Cursor, Context) == 0)
            break;
        ++Cursor;
    }
    return 0;
}

unsigned long DACOM_API FAnimationObject::RegisterHandle(unsigned long Key)
{
    return RegisterAnimHandle(Key);
}

unsigned long DACOM_API FAnimationObject::HasHandle(unsigned long Key, unsigned char* Out)
{
    if (Out != nullptr)
        *Out = FindAnimHandle(Key) ? 1 : 0;
    return 0;
}

unsigned long DACOM_API FAnimationObject::HasAnyHandle(unsigned long Key)
{
    if (!FindAnimHandle(Key))
        return 0xFFFFFFFF;
    UnregisterAnimHandle(Key);
    return 0;
}

static bool IsTypedChannelType(unsigned long Type)
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
static FTypedChannel* NewTypedChannelWithOwner(unsigned long Type, IChannelOwner* Owner)
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

static FTypedChannel* NewTypedChannelWithArchetype(unsigned long Type, FChannelArchetypeObject* Archetype)
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

// FUN_06612a00.
void FAnimPlayback::FreeTyped()
{
    std::vector<FTypedChannel*>::iterator Cursor = this->Typed.begin();
    while (Cursor != this->Typed.end())
    {
        FTypedChannel* Typed = *Cursor;
        if (Typed != nullptr)
        {
            if (Typed->Archetype != nullptr)
            {
                Typed->Archetype->Release();
                Typed->Archetype = nullptr;
            }
            ::operator delete(Typed); // Nonvirtual base: archetype released above, trivial tails.
        }
        ++Cursor;
    }
    this->Typed.clear();
    LongestTyped = nullptr;
}

// FUN_06611f90. Record +0 is the name entry.
void FAnimPlayback::CloneTypedChannels(FAnimNameEntry* Entry)
{
    Name = Entry;
    this->Typed.resize(Entry->Tracks.size(), nullptr);
    std::vector<FTypedChannel*>::iterator Slot = this->Typed.begin();
    std::vector<FAnimTrack>::iterator Cursor = Entry->Tracks.begin();
    while (Cursor != Entry->Tracks.end())
    {
        unsigned long Type = 0;
        Cursor->Channel->GetFlags(&Type);
        FTypedChannel* Typed = NewTypedChannelWithArchetype(Type, Cursor->Channel);
        Typed->PlayState &= 0xFE;
        *Slot = Typed;
        ++Slot;
        ++Cursor;
    }
}

// FUN_06612250.
unsigned long FAnimPlayback::FilterTyped(IChannel* Filter)
{
    std::vector<FTypedChannel*>::iterator Cursor = this->Typed.begin();
    while (Cursor != this->Typed.end())
    {
        FTypedChannel* Typed = *Cursor;
        const unsigned long Type = Typed->GetType();
        if (Type != 8)
        {
            if (Filter->InMixer((unsigned long)((size_t)Typed)) != 0)
                return 0;
        }
        ++Cursor;
    }
    return 1;
}

// FUN_066122a0. Sole XREF LAB_066150a0.
unsigned long FAnimPlayback::HasTyped(void* Object)
{
    std::vector<FTypedChannel*>::iterator Cursor = Typed.begin();
    while (Cursor != Typed.end())
    {
        if (*Cursor == Object)
            return 1;
        ++Cursor;
    }
    return 0;
}

// FUN_066122d0.
unsigned long FAnimPlayback::TouchMappedTyped(void* OutPos, void* OutSampledPos)
{
    std::vector<FTypedChannel*>::iterator Cursor = this->Typed.begin();
    while (Cursor != this->Typed.end())
    {
        FTypedChannel* Typed = *Cursor;
        const unsigned long Type = Typed->GetType();
        if ((Type & 2) != 0)
        {
            Typed->TouchMapped(OutPos, OutSampledPos);
            return 1;
        }
        ++Cursor;
    }
    return 0;
}

// The shared tail of IChannel::CreateTyped and CreateFromObject.
static unsigned long ChannelBindAndInsert(FAnimationObject* Animation, FTypedChannel* Typed,
                                          void* BindA, void* BindB, unsigned char bFreeOnRefusal)
{
    const unsigned long bBound =
        Typed->BindMap((unsigned long*)BindA, BindB);
    if ((bBound & 0xFF) == 0)
    {
        if (bFreeOnRefusal != 0)
        {
            if (Typed->Archetype != nullptr)
            {
                Typed->Archetype->Release();
                Typed->Archetype = nullptr;
            }
            ::operator delete(Typed); // FUN_066281d0; nonvirtual base, trivial tails.
        }
        return 0xFFFFFFFF;
    }
    Animation->Channels->insert(Typed);
    return (unsigned long)((size_t)Typed);
}

// LAB_06615950 (IChannel+0x0C).
unsigned long DACOM_API FAnimationChannelFacet::CreateTyped(void* Owner, unsigned long Type,
                                                            void* BindA, void* BindB)
{
    void* ChannelThis = this;
    if (Owner == nullptr)
        return 0xFFFFFFFF;
    if (!IsTypedChannelType(Type))
    {
        EngBaseFdump(kFdumpFatal, G_FdumpFileAnimComponent, 0x44D, "FATAL", "Unknown data type!\n");
        // Retail's invalid-type continuation reinterprets the owner as a channel.
        const unsigned long bBound =
            ((FTypedChannel*)Owner)->BindMap((unsigned long*)BindA, BindB);
        if ((bBound & 0xFF) == 0)
        {
            IDAComponent** Held =
                (IDAComponent**)((unsigned char*)Owner + 0x24);
            if (*Held != nullptr)
            {
                (*Held)->Release();
                *Held = nullptr;
            }
            ::operator delete(Owner);                            // FUN_066281d0
            return 0xFFFFFFFF;
        }
        AnimationFromNested(ChannelThis, 8)->Channels->insert((FTypedChannel*)Owner);
        return (unsigned long)((size_t)Owner);
    }
    FTypedChannel* Typed = NewTypedChannelWithOwner(Type, (IChannelOwner*)Owner);
    return ChannelBindAndInsert(AnimationFromNested(ChannelThis, 8), Typed, BindA, BindB, 1);
}

// LAB_06615b00 (IChannel+0x10): the type is the archetype's flags.
unsigned long DACOM_API FAnimationChannelFacet::CreateFromObject(void* Object, void* BindA,
                                                                 void* BindB)
{
    void* ChannelThis = this;
    if (Object == nullptr)
        return 0xFFFFFFFF;
    FChannelArchetypeObject* Archetype =
        (FChannelArchetypeObject*)((IChannelArchetype*)Object);
    unsigned long Type = 0;
    Archetype->GetFlags(&Type);
    if (!IsTypedChannelType(Type))
    {
        EngBaseFdump(kFdumpFatal, G_FdumpFileAnimComponent, 0x479, "FATAL", "Unknown data type!\n");
        return 0xFFFFFFFF;
    }
    FTypedChannel* Typed = NewTypedChannelWithArchetype(Type, Archetype);
    return ChannelBindAndInsert(AnimationFromNested(ChannelThis, 8), Typed, BindA, BindB, 0);
}

// FUN_06615e10 (IChannel+0x14). Retail frees the channel before erasing its pointer from the
// set (FUN_06617c90).
unsigned long DACOM_API FAnimationChannelFacet::Destroy(unsigned long Key)
{
    void* ChannelThis = this;
    FAnimationObject* Animation = AnimationFromNested(ChannelThis, 8);
    FTypedChannel* Typed = FindTypedChannel(Animation, Key);
    if (Typed == nullptr)
        return 0xFFFFFFFF;
    MixerUnbindTyped(Animation, Typed);                          // FUN_06615ec0
    if (Typed->Archetype != nullptr)
    {
        Typed->Archetype->Release();
        Typed->Archetype = nullptr;
    }
    ::operator delete(Typed); // FUN_066281d0; nonvirtual base, trivial tails.
    Animation->Channels->erase(Typed);
    return 0;
}

// LAB_06615d80 (IChannel+0x18): the archetype is at +0x24. A miss leaves Out untouched.
unsigned long DACOM_API FAnimationChannelFacet::GetObject(unsigned long Key, void** Out)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return 0xFFFFFFFF;
    *Out = (IChannelArchetype*)(Typed->Archetype);
    if (Typed->Archetype != nullptr)
        Typed->Archetype->AddRef();
    return 0;
}

// FUN_06616640 (IChannel+0x2C): the dword goes to typed +0x2C (Scale).
GENRESULT DACOM_API FAnimationChannelFacet::SetUser(unsigned long Key, unsigned long User)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return GR_OK;
    std::memcpy(&Typed->Scale, &User, sizeof(Typed->Scale));
    return GR_OK;
}

// FUN_066166a0 (IChannel+0x30): typed +0x2C (Scale).
float DACOM_API FAnimationChannelFacet::GetUser(unsigned long Key)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return 0.0f;
    return Typed->Scale;
}

// FUN_06616ac0 (IChannel+0x48): Rate is +0x28; unordered counts as forward. A miss leaves
// Out untouched.
GENRESULT DACOM_API FAnimationChannelFacet::GetMapped(unsigned long Key, unsigned char* Out)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return (GENRESULT)0xFFFFFFFE;
    *Out = Typed->Rate < 0.0f ? 0 : 1;
    return GR_OK;
}

// LAB_06616550 (IChannel+0x24): typed +0x1C, the channel's current time despite the slot's name.
float DACOM_API FAnimationChannelFacet::GetDuration(unsigned long Key)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return -1.0f;
    return Typed->Time;
}

// LAB_066169b0 (IChannel+0x40): Out always receives 16 bytes, the defaults on a miss.
unsigned long* DACOM_API FAnimationChannelFacet::GetHeader(unsigned long* Out, unsigned long Key)
{
    void* ChannelThis = this;
    unsigned long Default[4];
    Default[0] = 2;
    Default[1] = 0;
    Default[2] = 0;
    Default[3] = 0xFFFFFFFF;
    const unsigned long* Source = Default;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed != nullptr)
        Source = (const unsigned long*)(TypedBindDesc(Typed));
    std::memcpy(Out, Source, sizeof(Default));
    return Out;
}

// FUN_066164d0 (IChannel+0x20): a playing channel (PlayState bit 0) is posed where it stands
// and stops playing.
unsigned long DACOM_API FAnimationChannelFacet::StopGraph(unsigned long Key)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr || (Typed->PlayState & 1) == 0)
        return 0;
    Typed->ApplyFromArchetype();
    Typed->PlayState &= 0xFE;
    return 1;
}

// FUN_066165d0 (IChannel+0x28): typed slot 0x10 (SetTime).
unsigned long DACOM_API FAnimationChannelFacet::Blend(unsigned long Key, float Duration)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return 0;
    return Typed->SetTime(Duration);
}

// FUN_06616720 (IChannel+0x34): typed slot 0x20 and nothing else -- unlike IAnimation2's playback setter (FUN_06612a70), no mixer overlay is touched here.
GENRESULT DACOM_API FAnimationChannelFacet::SetOverlayPos(unsigned long Key, float* Pos)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return GR_OK;
    Typed->SetOverlayPos(Pos);
    return GR_OK;
}

// FUN_06616790 (IChannel+0x38): typed slot 0x24 only.
GENRESULT DACOM_API FAnimationChannelFacet::SetOverlayExtra(unsigned long Key, float* Extra)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return GR_OK;
    Typed->SetOverlayExtra(Extra);
    return GR_OK;
}

// FUN_066168f0 (IChannel+0x3C): 1 when the channel is a leaf of its mixer list.
unsigned long DACOM_API FAnimationChannelFacet::InMixer(unsigned long Key)
{
    void* ChannelThis = this;
    FAnimationObject* Animation = AnimationFromNested(ChannelThis, 8);
    FTypedChannel* Typed = FindTypedChannel(Animation, Key);
    if (Typed == nullptr)
        return 0;
    return MixerHasLeaf(Animation, Typed);
}

// FUN_06616a50 (IChannel+0x44): typed slot 0x28 only.
GENRESULT DACOM_API FAnimationChannelFacet::AddOverlayPos(unsigned long Key, float* Delta)
{
    void* ChannelThis = this;
    FTypedChannel* Typed = FindTypedChannel(AnimationFromNested(ChannelThis, 8), Key);
    if (Typed == nullptr)
        return GR_OK;
    Typed->AddOverlayPos(Delta);
    return GR_OK;
}

// FUN_06627f40 / 06627f50 / 06627f60: forward to the controlling unknown at +0x34. No checks.
static GENRESULT DACOM_API HardpointQueryInterface(FHardpointObject* This, const char* Iid,
                                                  void** Out)
{
    return This->Controller->QueryInterface(Iid, Out);
}

static unsigned long DACOM_API HardpointAddRef(FHardpointObject* This)
{
    return This->Controller->AddRef();
}

static unsigned long DACOM_API HardpointRelease(FHardpointObject* This)
{
    return This->Controller->Release();
}

static FHardpointObject* HardpointFromNested(void* Nested, unsigned long Offset)
{
    return (FHardpointObject*)((unsigned char*)Nested - Offset);
}

static GENRESULT DACOM_API HardpointEngineComponentQueryInterface(void* Nested, const char* Iid,
                                                                 void** Out)
{
    return HardpointQueryInterface(HardpointFromNested(Nested, 4), Iid, Out);
}

static unsigned long DACOM_API HardpointEngineComponentAddRef(void* Nested)
{
    return HardpointAddRef(HardpointFromNested(Nested, 4));
}

static unsigned long DACOM_API HardpointEngineComponentRelease(void* Nested)
{
    return HardpointRelease(HardpointFromNested(Nested, 4));
}

// LAB_0661e990: count at +0x1C. No null check.
static unsigned long DACOM_API ChannelArchetypeAddRef(FChannelArchetypeObject* This)
{
    This->RefCount += 1;
    return This->RefCount;
}

// DAT_0662b10c: live ChannelArchetype count (INC in FUN_0661d730, DEC in FUN_0661d750).
static unsigned long G_ChannelArchetypeLive = 0;
// Shared compressed-frame scratch; freed with the last channel archetype.
static unsigned char* G_ChannelFramePool = nullptr;
static unsigned long G_ChannelFramePoolSize = 0;

// FUN_0661d730: zero +0x10 / +0x14 / +0x18, vtable PTR_LAB_0662952c. No null check.
void FChannelArchetypeObject::InnerCtor()
{
    Frames = nullptr;
    Stride = 0;
    Duration = 0.0f;
    G_ChannelArchetypeLive += 1;
}

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

// ---- Fixed-size free lists (see FEngBaseFreeList in EngBase.h) ----
// Element sizes 0x38 / 0xEC / 0x48 (static_asserts above), limit 0x40 and the empty list are
// the values LAB_0661f6a0 / LAB_0661f6e0 / LAB_0661f720 store.
static FEngBaseFreeList G_ArchetypeHandlePool = {sizeof(FArchetypeHandle), nullptr, 0x40, 0}; // DAT_0662b168
static FEngBaseFreeList G_InstancePool = {sizeof(FEngineInstance), nullptr, 0x40, 0};         // DAT_0662b158
static FEngBaseFreeList G_TransformPool = {sizeof(FInstanceTransform), nullptr, 0x40, 0};     // DAT_0662b114

// FUN_06625bd0: an element's first dword is the next link. The memory is not cleared.
void* FEngBaseFreeList::Alloc()
{
    void* Element = FreeHead;
    if (Element != nullptr)
    {
        FreeHead = *(void**)Element;
        FreeCount -= 1;
        return Element;
    }
    return std::malloc(ElementSize);
}

// FUN_06625c00: a negative limit means unbounded.
void FEngBaseFreeList::Free(void* Element)
{
    if (FreeLimit > FreeCount || FreeLimit < 0)
    {
        FreeCount += 1;
        *(void**)Element = FreeHead;
        FreeHead = Element;
        return;
    }
    std::free(Element);
}

// FUN_06625c40. Live elements are not tracked and are not touched.
void FEngBaseFreeList::Drain()
{
    void* Element = FreeHead;
    while (Element != nullptr)
    {
        void* Next = *(void**)Element;
        std::free(Element);
        Element = Next;
    }
    FreeCount = 0;
    FreeHead = nullptr;
}

// The list sentinel the ctor FUN_0661f550 builds for +0x48 and +0x54 (operator new(0x3c)).
// No null test.
static FEngineChildNode* CreateListSentinel()
{
    FEngineChildNode* Sentinel =
        new FEngineChildNode;
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    return Sentinel;
}

// FUN_066254d0 with the flag clear: the node value's dtor zeroes name[0] and the +0x38 pointer.
static void DestroyChildNodeValue(FEngineChildNode* Node)
{
    Node->Name[0] = '\0';
    Node->Object = nullptr;
}

// FUN_06623620 (list erase(first, last)).
static void EraseChildNodes(FEngineChildNode* First, FEngineChildNode* Last, unsigned long* Count)
{
    FEngineChildNode* Node = First;
    while (Node != Last)
    {
        FEngineChildNode* Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        DestroyChildNodeValue(Node);
        delete Node;
        *Count -= 1;
        Node = Next;
    }
}

// FUN_066235d0 (list insert(where, value)): operator new(0x3c), 0x34-byte value copy.
void FEngineChildNode::Insert(unsigned long* Count, const char* Name, IDAComponent* Object)
{
    FEngineChildNode* Node = new FEngineChildNode;
    Node->Next = this;
    Node->Prev = Prev != nullptr ? Prev : Node;
    Prev = Node;
    Node->Prev->Next = Node;
    std::memcpy(Node->Name, Name, sizeof(Node->Name));
    Node->Object = Object;
    *Count += 1;
}

// FUN_0661f550, in retail order.
void FEngineObject::Construct()
{
    new (this) FEngineObject();
    SearchPath = nullptr;
    InstanceLookupCount = 0;
    InstanceCacheHits = 0;
    bInstanceCacheValid = 0;
    ComponentList = CreateListSentinel();
    ComponentCount = 0;
    EngineComponentList = CreateListSentinel();
    EngineComponentCount = 0;
    bInitialized = 0;
}

// FUN_0661f750, in retail order (including the repeated erase of the engine-component list).
void FEngineObject::Destruct()
{
    G_ArchetypeHandlePool.Drain();
    G_InstancePool.Drain();
    G_TransformPool.Drain();
    if (SearchPath != nullptr)
    {
        SearchPath->Release();
        SearchPath = nullptr;
    }
    bInstanceCacheValid = 0;
    Instances.clear();
    Archetypes.clear();

    FEngineChildNode* Sentinel = ComponentList;
    FEngineChildNode* Node = Sentinel->Next;
    while (Node != Sentinel)
    {
        Node->Object->Release();
        FEngineChildNode* Next = Node->Next;
        Node->Prev->Next = Next;
        Next->Prev = Node->Prev;
        DestroyChildNodeValue(Node);
        delete Node;
        ComponentCount -= 1;
        Node = Next;
    }

    EraseChildNodes(EngineComponentList->Next, EngineComponentList,
                    &EngineComponentCount);
    bInitialized = 0;
    EraseChildNodes(EngineComponentList->Next, EngineComponentList,
                    &EngineComponentCount);
    delete EngineComponentList;
    EngineComponentList = nullptr;
    EngineComponentCount = 0;
    EraseChildNodes(ComponentList->Next, ComponentList, &ComponentCount);
    delete ComponentList;
    ComponentList = nullptr;
    ComponentCount = 0;
    this->~FEngineObject();
}

// LAB_06625460: count at +0x60.
unsigned long DACOM_API FEngineObject::AddRef()
{
    RefCount += 1;
    return RefCount;
}

// FUN_06625470: at zero the count is pinned to 1 before the dtor runs.
unsigned long DACOM_API FEngineObject::Release()
{
    if (RefCount > 0)
        RefCount -= 1;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    Destruct();
    std::free(this);
    return 0;
}

// LAB_06621ca0: the {iid, offset} table at 06629608, then each aggregated component.
GENRESULT DACOM_API FEngineObject::QueryInterface(const char* Iid, void** Out)
{
    if (std::strcmp(Iids::Engine, Iid) == 0 || std::strcmp(Iids::Engine2, Iid) == 0)
    {
        AddRef();
        *Out = this;
        return GR_OK;
    }

    *Out = nullptr;
    FEngineChildNode* Sentinel = ComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        if (Node->Object->QueryInterface(Iid, Out) >= 0)
            return GR_OK;
    }
    *Out = nullptr;
    return GR_INTERFACE_UNSUPPORTED;
}

// The smart-pointer assignment into +4 that FUN_0661fee0, LAB_0661fdf0 and LAB_0661ffc0 inline.
static void ReplaceSearchPath(FEngineObject* This, IDAComponent* NewPath)
{
    if (This->SearchPath == NewPath)
        return;
    if (This->SearchPath != nullptr)
    {
        This->SearchPath->Release();
        This->SearchPath = nullptr;
    }
    This->SearchPath = NewPath;
    if (NewPath != nullptr)
        NewPath->AddRef();
}

// Retail strtok on DAT_0662a524 = " \t". First token = name; token starting with '=' then next = value.
static void ParseEngineProfileLine(char* Line, char** NameOut, const char** ValueOut)
{
    *NameOut = nullptr;
    *ValueOut = nullptr;
    char* Name = nullptr;
    const char* Value = nullptr;
    unsigned char bSawEquals = 0;
    char* Token = std::strtok(Line, " \t");
    while (Token != nullptr)
    {
        if (Token[0] == ';')
            break;
        if (Name == nullptr)
            Name = Token;
        else if (bSawEquals != 0)
            Value = Token;
        else if (Token[0] == '=')
            bSawEquals = 1;
        Token = std::strtok(nullptr, " \t");
    }
    if (Name != nullptr)
    {
        char* Space = std::strchr(Name, ' ');
        if (Space != nullptr)
            *Space = '\0';
        char* Tab = std::strchr(Name, '\t');
        if (Tab != nullptr)
            *Tab = '\0';
    }
    *NameOut = Name;
    *ValueOut = Value;
}

// LAB_0661f930 / IEngine +0x0C (RET 8). The engine's own refcount is never touched here.
GENRESULT DACOM_API FEngineObject::LoadEngineComponents(IDAComponent* SystemContainer)
{
    if (bInitialized != 0)
        return GR_OK;

    if (SystemContainer == nullptr)
    {
        EngBaseFdump(kFdumpNotice, G_FdumpFileEngine, 0x1A3, "NOTICE",
                     "Engine: load_engine_components: system parameter is NULL, continuing with "
                     "complete faith in the client...");
    }

    ICOManager* Dacom = DACOM_Acquire();
    IProfileParser* Parser = nullptr;
    if (Dacom->QueryInterface(Iids::ProfileParser, (void**)(&Parser)) < 0)
    {
        EngBaseFdump(kFdumpError, G_FdumpFileEngine, 0x1AF, "ERROR",
                     "Engine: load_engine_components: unable to acquire IID_IProfileParser");
        if (Parser != nullptr)
            Parser->Release();
        return GR_GENERIC;
    }

    void* Section = Parser->OpenSection(G_NameEngine, 3);
    if (Section == nullptr)
    {
        bInitialized = 1;
        if (Parser != nullptr)
            Parser->Release();
        return GR_OK;
    }

    char Line[0x100];
    char NodeName[0x30];
    int LineIndex = 0;
    while (Parser->ReadLine(Section, LineIndex++, Line, sizeof(Line)) != 0)
    {
        char* Name = nullptr;
        const char* Value = nullptr;
        ParseEngineProfileLine(Line, &Name, &Value);
        if (Name == nullptr)
            continue;

        void* OuterInner = nullptr;
        void* Created = nullptr;
        FAggDesc18 Desc;
        Desc.Size = G_AggDescSizeAnimation;
        Desc.Name = Name;
        Desc.Inner = this;
        Desc.Outer = &OuterInner;
        Desc.Value = Value;
        Desc.SystemContainer = SystemContainer;

        const char* Status = G_StatusFailed;
        if (Dacom->CreateInstance((AGGDESC*)(&Desc), &Created) >= 0)
        {
            Status = G_StatusOk;
            BuildNodeName(NodeName, Name);
            ComponentList->Insert(&ComponentCount, NodeName,
                                        (IDAComponent*)OuterInner);
        }

        char Notice[0x1FF0];
        std::sprintf(Notice, "ENGINE::load_engine_components: aggregation of '%s' [%s] returned %s\n",
                     Name, Value != nullptr ? Value : "", Status);
        EngBaseFdump(kFdumpNotice, G_FdumpFileEngine, 0x1FC, "NOTICE", Notice);
    }

    // Pass 2. Aggregate is a smart pointer: released before every QI and once after the walk.
    FEngineChildNode* Sentinel = ComponentList;
    IAggregateComponent* Aggregate = nullptr;
    FEngineChildNode* Node = Sentinel->Next;
    while (Node != Sentinel)
    {
        if (Aggregate != nullptr)
        {
            Aggregate->Release();
            Aggregate = nullptr;
        }
        if (Node->Object->QueryInterface(Iids::AggregateComponent,
                                         (void**)(&Aggregate)) >= 0 &&
            Aggregate->Initialize() >= 0)
        {
            Node = Node->Next;
            continue;
        }

        char Warning[0x1FF0];
        std::sprintf(Warning,
                     "ENGINE::load_engine_components: initialization of '%s' failed, removing "
                     "from aggregate\n",
                     Node->Name);
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x224, "WARNING", Warning);
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        DestroyChildNodeValue(Node);
        delete Node;
        ComponentCount -= 1;
        Node = ComponentList->Next;
    }
    if (Aggregate != nullptr)
    {
        Aggregate->Release();
        Aggregate = nullptr;
    }

    // Pass 3.
    Sentinel = ComponentList;
    for (Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        IDAComponent* EngineComponent = nullptr;
        if (Node->Object->QueryInterface(Iids::EngineComponent,
                                         (void**)(&EngineComponent)) < 0)
            continue;
        EngineComponent->Release();
        BuildNodeName(NodeName, Node->Name);
        EngineComponentList->Insert(&EngineComponentCount, NodeName,
                                          EngineComponent);
    }

    bInitialized = 1;
    if (Parser != nullptr)
        Parser->Release();
    return GR_OK;
}

// LAB_0661fdf0 (IEngine +0x18).
GENRESULT DACOM_API FEngineObject::SetSearchPath(const char* Path)
{
    IDAComponent* Created = nullptr;
    if (Path == nullptr)
    {
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x270, "WARNING",
                     "Engine: set_search_path: path is NULL");
    }
    else
    {
        FAggDesc8 Desc;
        Desc.Size = G_AggDescSizeSearchPath;
        Desc.Name = G_NameSearchPath;
        if (DACOM_Acquire()->CreateInstance((AGGDESC*)(&Desc),
                                            (void**)(&Created)) >= 0)
        {
            ((ISearchPath*)((void*)Created))->SetPath(Path);
            ReplaceSearchPath(this, Created);
            if (Created != nullptr)
                return (GENRESULT)(Created->Release());
            return GR_OK;
        }
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x275, "WARNING",
                     "Engine: set_search_path: unable to create search path");
    }
    if (Created != nullptr)
        return (GENRESULT)(Created->Release());
    return GR_OK;
}

// FUN_0661fee0 (IEngine +0x20). Retail returns whatever EAX the last AddRef / Release left (or
// the old pointer); the port returns 0.
GENRESULT DACOM_API FEngineObject::SetFilesystem(IDAComponent* NewPath)
{
    ReplaceSearchPath(this, NewPath);
    return GR_OK;
}

// FUN_0661ff20 (IEngine +0x1C).
GENRESULT DACOM_API FEngineObject::QuerySearchPath(char* Dest, unsigned long MaxChars)
{
    ISearchPath* SearchPathIface = nullptr;
    if (SearchPath != nullptr &&
        SearchPath->QueryInterface(Iids::SearchPath,
                                         (void**)(&SearchPathIface)) >= 0)
    {
        const GENRESULT Result = SearchPathIface->GetPath(Dest, MaxChars);
        if (SearchPathIface != nullptr)
            SearchPathIface->Release();
        return Result;
    }
    *Dest = 0;
    if (SearchPathIface != nullptr)
        SearchPathIface->Release();
    return GR_OK;
}

static FArchetypeHandle* const G_InvalidArchetype =
    (FArchetypeHandle*)G_InvalidEngineHandle;

// The 0x34 FileSystem descriptor every caller builds inline (LAB_0661ffc0, FUN_066201b0,
// FUN_06611840, ...).
static void FillFileSystemDesc(FEngBaseFileSystemDesc* Desc, const char* Path, const char* Backend)
{
    std::memset(Desc, 0, sizeof(*Desc));
    Desc->Size = 0x34;
    Desc->ComponentName = G_NameFileSystem;
    Desc->BackendName = Backend;
    Desc->DataPath = Path;
    Desc->Access = 0x80000000;
    Desc->ShareMode = 1;
    Desc->Creation = 3;
    Desc->Flags = 0x8000080;
    Desc->FindHandle = 0xFFFFFFFF;
}

// Retail tests only these two forms.
static bool IsAbsoluteFilePath(const char* Path)
{
    if (Path[1] == ':')
        return true;
    if (Path[0] == '\\' && Path[1] == '\\')
        return true;
    return false;
}

// FUN_0661f8b0: find(Crc) in the map at +0x30 (lower_bound FUN_0661a260).
FArchetypeHandle* FEngineObject::LookupArchetype(FArchetypeName* Name)
{
    if (Name->Crc == 0)
        return G_InvalidArchetype;
    FArchetypeMap::iterator It = Archetypes.find(Name->Crc);
    if (It == Archetypes.end())
        return G_InvalidArchetype;
    return It->second;
}

// FUN_0661ea00, on a block from FUN_0661e9e0 (free list DAT_0662b168).
FArchetypeHandle::FArchetypeHandle(const FArchetypeName* Source)
    : Name(Source->Text != nullptr ? Source->Text : "")
    , Crc(Source->Crc)
    , RefCount(0)
{
}

// FUN_0661eab0: ref at +0x14; every child of the vector at +0x1C (stride 0x48) goes through
// IEngine slot 14 with its +0x44 handle. No tests.
unsigned long FArchetypeHandle::AddRef(FEngineObject* Engine)
{
    RefCount += 1;
    for (std::vector<FArchetypeChild>::iterator Child = Children.begin();
         Child != Children.end(); ++Child)
        Engine->AddRefHandle(Child->Handle);
    return RefCount;
}

// FUN_0661eaf0: no floor on the decrement; children go through IEngine slot 15.
unsigned long FArchetypeHandle::Release(FEngineObject* Engine)
{
    RefCount -= 1;
    for (std::vector<FArchetypeChild>::iterator Child = Children.begin();
         Child != Children.end(); ++Child)
        Engine->ReleaseHandle(Child->Handle);
    return RefCount;
}

// LAB_066213b0 (IEngine +0x38). Retail leaves EAX unwritten for an invalid handle; the port
// returns 0.
static unsigned long DACOM_API EngineAddRefHandle(FEngineObject* This, FArchetypeHandle* Handle)
{
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return 0;
    return Handle->AddRef(This);
}

// LAB_066213d0 (IEngine +0x3C).
static unsigned long DACOM_API EngineReleaseHandle(FEngineObject* This, FArchetypeHandle* Handle)
{
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return 0;
    const unsigned long Count = Handle->Release(This);
    if (Count != 0)
        return Count;

    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->DestroyArchetype(Handle);

    if (This->Archetypes.erase(Handle->Crc) == 0)
    {
        char Message[0x2000];
        std::sprintf(Message, "Failed to find %s in archetype name map\n", Handle->Name.c_str());
        EngBaseFdump(kFdumpError, G_FdumpFileEngine, 0x47E, "ERROR", Message);
    }

    // Retail inlines ~FArchetypeHandle; FUN_0661e9f0 returns the block.
    Handle->~FArchetypeHandle();
    G_ArchetypeHandlePool.Free(Handle);
    return 0;
}

static void CallEngineSlot14(FEngineObject* This, FArchetypeHandle* Handle)
{
    This->AddRefHandle(Handle);
}

// The Engine +0x58 walk of LAB_06621150: CreateArchetype on every engine component. No checks.
static void FanoutIecCreateArchetype(FEngineObject* This, FArchetypeHandle* Handle, void* FileSystem)
{
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->CreateArchetype(Handle, FileSystem);
}

static void FanoutIecDuplicateArchetype(FEngineObject* This, FArchetypeHandle* NewHandle,
                                       FArchetypeHandle* OldHandle)
{
    // IEC +0x14 on every node of +0x54, no tests.
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->DuplicateArchetype(NewHandle, OldHandle);
}

// LAB_0661fda0 (IEngine +0x10): only the callback's low byte is tested.
static unsigned long DACOM_API EngineEnumerateEngineComponents(FEngineObject* This,
                                                              int(__cdecl* Callback)(const char*,
                                                                                    void*))
{
    if (Callback != nullptr)
    {
        FEngineChildNode* Sentinel = This->EngineComponentList;
        for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        {
            if ((unsigned char)(Callback(Node->Name, Node->Object)) == 0)
                break;
        }
    }
    return This->EngineComponentCount;
}

static void EngineUpdateInstanceTree(FEngineObject* This, FEngineInstance* Instance,
                                    unsigned long Flags, void* Arg);

// FUN_06621de0 (IEngine+0x14). No checks.
GENRESULT DACOM_API FEngineObject::NotifyEngineComponents(void* Arg)
{
    FEngineChildNode* Sentinel = EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->Notify(Arg);
    for (FEngineInstanceMap::iterator It = Instances.begin(); It != Instances.end();
         ++It)
        EngineUpdateInstanceTree(this, It->second, 0, nullptr);
    return GR_OK;
}

// LAB_0661ff90 (IEngine+0x24). No checks.
GENRESULT DACOM_API FEngineObject::GetSearchPath(void** Out)
{
    *Out = SearchPath;
    if (*Out == nullptr)
        *Out = DACOM_Acquire();
    return (GENRESULT)(((IDAComponent*)(*Out))->AddRef());
}

// LAB_0661ffc0 (IEngine +0x28).
GENRESULT DACOM_API FEngineObject::CreateFileSystem(const char* Path, void** Out, void* Factory)
{
    if (Path == nullptr || Out == nullptr)
        return GR_INVALID_PARMS;

    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Path, nullptr);

    // Every candidate is called through vtable +0xC (CreateInstance): a component factory,
    // the COManager, or whatever +4 holds.
    IComponentFactory* Creator = (IComponentFactory*)Factory;
    if (Creator == nullptr)
    {
        if (IsAbsoluteFilePath(Path))
        {
            Creator = (IComponentFactory*)((void*)(DACOM_Acquire()));
        }
        else
        {
            if (SearchPath == nullptr)
                ReplaceSearchPath(this, DACOM_Acquire());
            Creator = (IComponentFactory*)((void*)(SearchPath));
        }
    }
    return Creator->CreateInstance((AGGDESC*)(&Desc), Out);
}

// LAB_066200c0 (IEngine +0x2C).
void* DACOM_API FEngineObject::CreateCompoundArchetype(FEngineName* InName)
{
    FArchetypeName* Name = InName;
    DACOM_CRC::GetCRC32(nullptr);
    FArchetypeName Local;
    char Anonymous[0x100];
    if (Name->Crc != 0)
    {
        Local.Text = Name->Text;
        Local.Crc = Name->Crc;
    }
    else
    {
        Local.Text = Anonymous;
        do
        {
            wsprintfA(Anonymous, G_AnonymousFormat, G_AnonymousIndex++);
            Local.Crc = DACOM_CRC::GetCRC32(Anonymous);
        } while (LookupArchetype(&Local) != G_InvalidArchetype);
        Local.Crc = DACOM_CRC::GetCRC32(Anonymous);
    }

    void* Block = G_ArchetypeHandlePool.Alloc();
    FArchetypeHandle* Handle = nullptr;
    if (Block != nullptr)
        Handle = new (Block) FArchetypeHandle(&Local);
    Archetypes.insert(FArchetypeMap::value_type(Local.Crc, Handle));
    AddRefHandle(Handle);
    return Handle;
}

// The child vector's resize(size + 1, element) in FUN_06620500.
static FArchetypeChild* AppendArchetypeChild(FArchetypeHandle* Handle)
{
    const std::size_t Count = Handle->Children.size();
    Handle->Children.resize(Count + 1, FArchetypeChild());
    return Handle->Children.data() + Count;
}

// The resize(n) back to the size before a failed FUN_066201b0 (erase, FUN_066230f0).
static void ShrinkArchetypeChildren(FArchetypeHandle* Handle, std::size_t Count)
{
    Handle->Children.erase(Handle->Children.begin() + Count, Handle->Children.end());
}

// The IFileSystem calls every EngBase site makes inline through the vtable, without testing
// the pointer or the arguments.
static int FileSystemOpenNode(void* FileSystem, const char* Name)
{
    return AsFileSystem(FileSystem)->OpenNode(Name); // +0x60
}

static int FileSystemFindFirst(void* FileSystem, const char* Pattern, WIN32_FIND_DATAA* FindData)
{
    return AsFileSystem(FileSystem)->FindFirst(Pattern, // +0x48
                                            FindData);
}

static int FileSystemFindNext(void* FileSystem, int FindHandle, WIN32_FIND_DATAA* FindData)
{
    return AsFileSystem(FileSystem)->FindNext((unsigned long)FindHandle, // +0x4C
                                            FindData);
}

static void FileSystemFindClose(void* FileSystem, int FindHandle)
{
    AsFileSystem(FileSystem)->FindClose((unsigned long)FindHandle); // +0x50
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

// FUN_0661d7a0: Frames (+0x10), Stride (+0x14); time-stamped frames skip their leading time.
const unsigned char* FChannelArchetypeObject::FramePayload(unsigned long Index) const
{
    const unsigned long Skip = (double)(SampleRate) < 0.0 ? 4 : 0;
    return Frames + Stride * Index + Skip;
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

// One output dword of FUN_0661db70.
static void ChannelPutDword(unsigned char*& Dst, const void* Value)
{
    std::memcpy(Dst, Value, 4);
    Dst += 4;
}

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
    if ((Flags & 0xF0) == 0 && (Flags & 0x100) == 0)
        return;
    if ((Flags & 9) != 0)
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
    this->Flags = this->Flags & 0xFFFFFE0F;
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

// LAB_0661d8d0, IChannelArchetype slot 3 (Load). This is not tested.
GENRESULT DACOM_API FChannelArchetypeObject::Load(void* FileSystem)
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
    if ((Flags & 0xF0) != 0 || (Flags & 0x100) != 0)
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

// LAB_0661d800, IChannelArchetype slot 4: Duration is +0x18. No checks.
GENRESULT DACOM_API FChannelArchetypeObject::GetDuration(float* Out)
{
    *Out = Duration;
    return GR_OK;
}

// LAB_0661d7e0, IChannelArchetype slot 5: Flags is +0xC. No checks.
GENRESULT DACOM_API FChannelArchetypeObject::GetFlags(unsigned long* Out)
{
    *Out = Flags;
    return GR_OK;
}

// LAB_0661e1a0, IChannelArchetype slot 6.
GENRESULT DACOM_API FChannelArchetypeObject::Sample(float Time, const unsigned char** OutA,
                                                     const unsigned char** OutB, float* Alpha)
{
    float Duration = 0.0f;
    GetDuration(&Duration);
    if (!((double)Time >= 0.0))
        return (GENRESULT)0xFFFFFFFE;
    if (!(Time <= Duration))
        return (GENRESULT)0xFFFFFFFE;
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

// LAB_0661e670, IChannelArchetype slot 7.
GENRESULT DACOM_API FChannelArchetypeObject::SamplePair(float Time, const unsigned char** OutLo,
                                                         const unsigned char** OutHi, float* TimeLo,
                                                         float* TimeHi, float* Alpha)
{
    float Duration = 0.0f;
    GetDuration(&Duration);
    if (!((double)Time >= 0.0))
        return (GENRESULT)0xFFFFFFFE;
    if (!(Time <= Duration))
        return (GENRESULT)0xFFFFFFFE;
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

// LAB_0661e2e0, IChannelArchetype slot 8: the first or the last frame.
GENRESULT DACOM_API FChannelArchetypeObject::GetFrame(unsigned char bLast, const unsigned char** Out)
{
    long Index = 0;
    if (bLast != 0)
    {
        Index = (long)(FrameCount - 1);
        if (Index < 0)
            return (GENRESULT)0xFFFFFFFF;
    }
    *Out = FramePayload((unsigned long)Index);
    return GR_OK;
}

// .rdata:06629560 / 06629564 (PE bytes 95 BF D6 B3 / 95 BF D6 33): -1e-7f and +1e-7f.
static const float G_ChannelTimeEpsNeg = -1.0000000116860974e-07f;
static const float G_ChannelTimeEpsPos = 1.0000000116860974e-07f;

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

// LAB_0661e400, IChannelArchetype slot 9.
GENRESULT DACOM_API FChannelArchetypeObject::CollectRange(float Time0, float Time1, float* OutKeys,
                                                          unsigned long Capacity, unsigned long* Count)
{
    *Count = 0;
    if (Flags != G_ChannelFlagScalarOnly)
        return (GENRESULT)0xFFFFFFFF;
    float Duration = 0.0f;
    GetDuration(&Duration);
    const float DurEps = Duration + G_ChannelTimeEpsPos;
    if (!(Time0 >= G_ChannelTimeEpsNeg) || !(Time0 <= DurEps))
        return (GENRESULT)0xFFFFFFFE;
    if (!(Time1 >= G_ChannelTimeEpsNeg) || !(Time1 <= DurEps))
        return (GENRESULT)0xFFFFFFFE;
    if (Time0 == Time1)
        return (GENRESULT)0xFFFFFFFE;

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

// LAB_0661e310, IChannelArchetype slot 10.
GENRESULT DACOM_API FChannelArchetypeObject::CollectAt(float Time, float* OutKeys,
                                                        unsigned long Capacity, unsigned long* Count)
{
    *Count = 0;
    if (Flags != G_ChannelFlagScalarOnly)
        return (GENRESULT)0xFFFFFFFF;
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
                Result = (GENRESULT)0xFFFFFFFF;
            *Count += 1;
        }
    }
    return Result;
}

// FUN_06611190: From itself, then its descendants through IEngine GetRelatedInstance.
static FEngineInstance* AnimEngineFindInstanceByName(FEngineInstance* From, const char* Name)
{
    FEngineInstance* const Invalid =
        (FEngineInstance*)((ptrdiff_t)(-1));
    FArchetypeName Out;
    const char* Text = G_AnimEngine->GetInstanceName(&Out, From)->Text;
    if (Text == nullptr)
        Text = "";
    if (std::strcmp(Text, Name) == 0)
        return From;
    FEngineInstance* Cursor =
        (FEngineInstance*)(G_AnimEngine->GetRelatedInstance(From, 0, Invalid));
    while (Cursor != Invalid)
    {
        Text = G_AnimEngine->GetInstanceName(&Out, Cursor)->Text;
        if (Text == nullptr)
            Text = "";
        if (std::strcmp(Text, Name) == 0)
            return Cursor;
        Cursor = (FEngineInstance*)(G_AnimEngine->GetRelatedInstance(From, 0, Cursor));
    }
    return Invalid;
}

// FUN_06612320.
static unsigned char BindTrackTarget(FAnimBindOut* Out, FEngineInstance* Root, FAnimTrack* Track,
                                    void* Sink, void* SinkContext, unsigned char* Already)
{
    FEngineInstance* const Invalid =
        (FEngineInstance*)((ptrdiff_t)(-1));
    *Already = 0;
    if (Root == Invalid && Sink == nullptr)
    {
        Out->Kind = 0;
        Out->Sink = nullptr;
        Out->SinkContext = nullptr;
        Out->Instance = nullptr;
        return 0;
    }
    Out->SinkContext = SinkContext;
    if (Track->ChildCrc != 0)
    {
        const char* ChildName = Track->ChildName != nullptr ? Track->ChildName : "";
        FEngineInstance* Inst = AnimEngineFindInstanceByName(Root, ChildName);
        if (Inst == Invalid)
            return 0;
        const bool bMapped = FindAnimHandle((unsigned long)((size_t)Inst));
        *Already = bMapped ? 1 : 0;
        if (bMapped)
            return 0;
        FEngineInstance* Parent = (FEngineInstance*)(G_AnimEngine->GetInstanceParent(Inst));
        if (Parent == Invalid)
            return 0;
        FArchetypeName NameOut;
        if (Track->ParentCrc != G_AnimEngine->GetInstanceName(&NameOut, Parent)->Crc)
            return 0;
        Out->Kind = 0;
        Out->Instance = Inst;
        Out->Sink = Sink;
        return 1;
    }
    if (Track->ParentCrc != 0)
    {
        const char* ParentName = Track->ParentName != nullptr ? Track->ParentName : "";
        FEngineInstance* Inst = AnimEngineFindInstanceByName(Root, ParentName);
        if (Inst == Invalid)
            Inst = Root;
        const bool bMapped = FindAnimHandle((unsigned long)((size_t)Inst));
        *Already = bMapped ? 1 : 0;
        if (bMapped)
            return 0;
        Out->Instance = Inst;
        Out->Kind = 1;
        Out->Sink = Sink;
        return 1;
    }
    Out->Instance = Root;
    Out->Kind = 2;
    Out->Sink = Sink;
    return Sink != nullptr ? 1 : 0;
}

// FUN_066124e0. Record +4 is Root.
static void BindPlaybackTracks(FAnimPlayback* Playback, FEngineInstance* Root, void* Sink,
                               void* SinkContext)
{
    Playback->Root = Root;
    Playback->LongestTyped = nullptr;
    float BestDuration = -1.0f;
    if (Playback->Typed.empty())
        return;
    std::vector<FTypedChannel*>::iterator Cursor = Playback->Typed.begin();
    // The track cursor walks the name value's vector (+0x14 first) alongside, advancing once
    // per iteration whether or not the channel was kept.
    FAnimTrack* Track = Playback->Name->Tracks.data();
    while (Cursor != Playback->Typed.end())
    {
        FAnimBindOut Bind;
        Bind.Kind = 2;
        Bind.Sink = nullptr;
        Bind.SinkContext = nullptr;
        Bind.Instance = (FEngineInstance*)((ptrdiff_t)-1);
        unsigned char bAlready = 0;
        unsigned char bBound = BindTrackTarget(&Bind, Root, Track, Sink, SinkContext, &bAlready);
        if (bBound != 0)
        {
            FTypedChannel* Typed = *Cursor;
            bBound = (unsigned char)(
                Typed->BindMap(
                    (unsigned long*)(&Bind), Root));
        }
        if (bBound != 0)
        {
            float Duration = 0.0f;
            Track->Channel->GetDuration(&Duration);
            if (BestDuration < Duration)
            {
                Playback->LongestTyped = *Cursor;
                BestDuration = Duration;
            }
            ++Cursor;
        }
        else
        {
            if (bAlready == 0)
            {
                if (Track->ChildCrc != 0)
                {
                    const char* Parent = Track->ParentName != nullptr ? Track->ParentName : "";
                    const char* Child = Track->ChildName != nullptr ? Track->ChildName : "";
                    char Message[0x2000];
                    _snprintf(Message, sizeof(Message),
                              "%s failed to map JOINT (parent: %s, child: %s) channel",
                              Playback->Name->Name, Parent, Child);
                    Message[sizeof(Message) - 1] = '\0';
                    EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x2F5, "WARNING", Message);
                }
                else if (Track->ParentCrc != 0)
                {
                    const char* Parent = Track->ParentName != nullptr ? Track->ParentName : "";
                    char Message[0x2000];
                    _snprintf(Message, sizeof(Message), "%s failed to map OBJECT (%s) channel",
                              Playback->Name->Name, Parent);
                    Message[sizeof(Message) - 1] = '\0';
                    EngBaseFdump(kFdumpWarning, G_FdumpFileAnim, 0x2FA, "WARNING", Message);
                }
            }
            FTypedChannel* Typed = *Cursor;
            if (Typed != nullptr)
            {
                if (Typed->Archetype != nullptr)
                {
                    Typed->Archetype->Release();
                    Typed->Archetype = nullptr;
                }
                ::operator delete(Typed); // Nonvirtual base: archetype released above, trivial tails.
            }
            Cursor = Playback->Typed.erase(Cursor);
        }
        ++Track;
    }
}

// FUN_066128f0: clear the play bit first, then pose (unlike ChannelStopGraph).
void FAnimPlayback::StopTyped()
{
    unsigned long Index = 0;
    while (Index < this->Typed.size())
    {
        FTypedChannel* Typed = this->Typed[Index];
        Typed->PlayState &= 0xFE;
        Typed->ApplyFromArchetype();
        Index += 1;
    }
}

// FUN_06612920.
void FAnimPlayback::ApplyTyped()
{
    unsigned long Index = 0;
    while (Index < Typed.size())
    {
        Typed[Index]->ApplyFromArchetype();
        Index += 1;
    }
}

// Rebind every channel with one play header (inlined in FUN_066126e0 and FUN_066127e0).
static unsigned long PlaybackBindAll(FAnimPlayback* Playback, const FPlayHeader* Header,
                                     unsigned long Result)
{
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        if ((Playback->Typed[Index]->TryBindIfTimeValid(Header) & 0xFF) == 0)
            Result = 0xFFFFFFFF;                                 // no break
        Index += 1;
    }
    return Result;
}

// Reverse playback requires compatible track durations.
static unsigned long PlaybackDurationsDiffer(FAnimPlayback* Playback)
{
    unsigned long Result = 0;
    float First = -1.0f;
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        FTypedChannel* Typed = Playback->Typed[Index];
        if (First < 0.0f)
            Typed->Archetype->GetDuration(&First);
        else
        {
            float Duration = 0.0f;
            Typed->Archetype->GetDuration(&Duration);
            if (!(First == Duration))
                Result = 1;
        }
        Index += 1;
    }
    return Result;
}

// FUN_066126e0: build the play header and bind every channel with it. The three float fields
// after Rate arrive as raw dwords and are stored bit for bit.
unsigned long FAnimPlayback::EvalTyped(unsigned long Flags, float Start, float Speed,
                                       unsigned long BlendDurationBits, unsigned long ScaleBits,
                                       unsigned long TranslationScaleBits, const float* Transform)
{
    // Flags bit 0 means reverse.
    const long Sign = (Flags & 1) == 0 ? 1 : -1;
    const float Rate = (float)(std::fabs((double)Speed) *
                                          (double)Sign);
    FPlayHeader Header;
    std::memcpy(&Header.StartTime, &Start, sizeof(float));
    Header.LoopFlags = Flags & 0xFE;
    std::memcpy(&Header.Rate, &Rate, sizeof(float));
    std::memcpy(&Header.BlendDuration, &BlendDurationBits, sizeof(float));
    std::memcpy(&Header.Scale, &ScaleBits, sizeof(float));
    std::memcpy(&Header.TranslationScale, &TranslationScaleBits, sizeof(float));
    Header.Transform = Transform;
    unsigned long Result = 0;
    if ((Flags & 1) != 0 && Typed.size() != 0)
        Result = PlaybackDurationsDiffer(this);
    return PlaybackBindAll(this, &Header, Result);
}

// FUN_066127e0: rebind every channel from the longest one's own state.
unsigned long FAnimPlayback::EvalStoredTyped()
{
    FTypedChannel* Longest = LongestTyped;
    const float Start = -2.0f;
    FPlayHeader Header;
    std::memcpy(&Header.StartTime, &Start, sizeof(float));
    Header.LoopFlags = Longest->LoopFlags & 0xFE;
    std::memcpy(&Header.Rate, &Longest->Rate, sizeof(float));
    std::memset(&Header.BlendDuration, 0, sizeof(float));
    std::memcpy(&Header.Scale, &Longest->Scale, sizeof(float));
    std::memcpy(&Header.TranslationScale, &Longest->TranslationScale, sizeof(float));
    Header.Transform = nullptr;
    return PlaybackBindAll(this, &Header, 0);
}

// FUN_06612940: match on the bind descriptor (typed +8). Kinds 0/1 compare the instance (+0xC),
// kind 2 the sink (+4); other kinds never match.
FTypedChannel* FAnimPlayback::FindTypedByHeader(const FAnimBindOut* Header)
{
    unsigned long Index = 0;
    while (Index < this->Typed.size())
    {
        FTypedChannel* Typed = this->Typed[Index];
        const FAnimBindOut* Desc = TypedBindDesc(Typed);
        if (Desc->Kind == Header->Kind)
        {
            if (Desc->Kind == 0 || Desc->Kind == 1)
            {
                if (Desc->Instance == Header->Instance)
                    return Typed;
            }
            else if (Desc->Kind == 2)
            {
                if (Desc->Sink == Header->Sink)
                    return Typed;
            }
        }
        Index += 1;
    }
    return (FTypedChannel*)((ptrdiff_t)(-1));
}

// FUN_066129c0.
void FAnimPlayback::SetTypedTime(float Time)
{
    unsigned long Index = 0;
    while (Index < this->Typed.size())
    {
        FTypedChannel* Typed = this->Typed[Index];
        Typed->SetTime(Time);
        Index += 1;
    }
}

// FUN_066129f0: the longest channel's current time (+0x1C), despite the caller's name.
float FAnimPlayback::StoredDuration()
{
    return LongestTyped->Time;
}

// FUN_06611290 (thiscall on the track, which it does not use). No checks.
static unsigned long ReadUtfNameCrc(void* FileSystem, const char* Name, char** Out)
{
    IFileSystem* Fs = AsFileSystem(FileSystem);
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Name, nullptr);
    const int Handle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    if (Handle != -1)
    {
        const unsigned long Size = Fs->GetSize((unsigned long)Handle, 0); // +0x28
        *Out = (char*)(std::malloc(Size));
        unsigned long BytesRead = 0;
        Fs->Read((unsigned long)Handle, *Out, Size, &BytesRead, 0); // +0x14
        Fs->CloseFile((unsigned long)Handle); // +0x10
    }
    return DACOM_CRC::GetCRC32(*Out);
}

// FUN_06611350. No checks.
void FAnimTrack::LoadChannelNode(void* FileSystem)
{
    IFileSystem* Fs = AsFileSystem(FileSystem);
    if (Fs->OpenNode(G_NameChannel) == 0) // +0x60
        return;
    if (Channel != nullptr)
    {
        Channel->Release();
        Channel = nullptr;
    }
    FAggDesc8 Desc;
    Desc.Size = G_AggDescSizeChannelArchetype;
    Desc.Name = G_NameChannelArchetype;
    ICOManager* Dacom = DACOM_Acquire();
    if (Dacom->CreateInstance((AGGDESC*)(&Desc),
                              (void**)(&Channel)) == 0) // +0xC
        Channel->Load(FileSystem); // +0xC
    Fs->OpenNode(G_NameParentDot); // +0x60
}

// FUN_06611440. Retail returns 1, which no caller reads.
void FAnimNameEntry::LoadScriptMaps(const char* Name, void* FileSystem)
{
    this->Name = _strdup(Name);
    IFileSystem* Fs = AsFileSystem(FileSystem);
    WIN32_FIND_DATAA FindData;
    const int FindHandle = Fs->FindFirst(G_NameStar, // +0x48
                                      &FindData);
    if (FindHandle == -1)
        return;
    float BestDuration = -1.0f;
    do
    {
        if ((FindData.dwFileAttributes & 0x10) == 0)
        {
            if (std::strcmp(FindData.cFileName, G_NameRootHeight) != 0)
                continue;
            FEngBaseFileSystemDesc Desc;
            FillFileSystemDesc(&Desc, FindData.cFileName, nullptr);
            Desc.FindHandle = (unsigned long)FindHandle;
            const int Handle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
            if (Handle == -1)
                continue;
            unsigned long BytesRead = 0;
            Fs->Read((unsigned long)Handle, &RootHeight, 4, &BytesRead, 0); // +0x14
            Fs->CloseFile((unsigned long)Handle); // +0x10
            bHasRootHeight = 1;
            continue;
        }
        if (FindData.cFileName[0] == '.')
            continue;
        FEngBaseFileSystemDesc Desc;
        FillFileSystemDesc(&Desc, FindData.cFileName, nullptr);
        Desc.FindHandle = (unsigned long)FindHandle;
        void* MapFs = nullptr;
        Fs->CreateInstance((AGGDESC*)(&Desc), &MapFs); // +0xC
        if (std::strncmp(FindData.cFileName, G_NameEventMap, G_EventMapLength) != 0)
        {
            Tracks.resize(Tracks.size() + 1, FAnimTrack());
            FAnimTrack* Track = &Tracks.back();
            if (std::strncmp(FindData.cFileName, G_NameJointMap, G_JointMapLength) == 0)
            {
                Track->ParentCrc = ReadUtfNameCrc(MapFs, G_NameParentName, &Track->ParentName);
                Track->ChildCrc = ReadUtfNameCrc(MapFs, G_NameChildName, &Track->ChildName);
                Track->LoadChannelNode(MapFs);
            }
            else if (std::strncmp(FindData.cFileName, G_NameObjectMap, G_ObjectMapLength) == 0)
            {
                Track->ParentCrc = ReadUtfNameCrc(MapFs, G_NameParentName, &Track->ParentName);
                Track->LoadChannelNode(MapFs);
            }
            // Retail: unreachable under the outer "Event map" test, so event maps are never loaded.
            else if (std::strncmp(FindData.cFileName, G_NameEventMap, G_EventMapLength) == 0)
            {
                Track->LoadChannelNode(MapFs);
            }
            if (Track->Channel != nullptr)
            {
                float Duration;
                Track->Channel->GetDuration(&Duration); // +0x10
                if (BestDuration < Duration)
                {
                    if (LongestChannel != nullptr)
                    {
                        LongestChannel->Release();
                        LongestChannel = nullptr;
                    }
                    LongestChannel = Track->Channel;
                    LongestChannel->AddRef();
                    BestDuration = Duration;
                }
            }
        }
        if (MapFs != nullptr)
            ((IDAComponent*)MapFs)->Release();
    } while (Fs->FindNext((unsigned long)FindHandle, // +0x4C
                        &FindData) != 0);
    Fs->GetFileSystemError(); // +0x78
    Fs->FindClose((unsigned long)FindHandle); // +0x50
}

// LAB_06614920 (IAnimation2 slot 9). Sink / SinkContext become every bound channel's
// IChannelSink and its context (typed +0x0C / +0x10); a root-bound (event) track needs a sink.
unsigned long DACOM_API FAnimationObject::PlayScript(void* InScript, void* InRoot, const char* Name,
                                                      void* Sink, void* SinkContext)
{
    FAnimScript* Script = (FAnimScript*)InScript;
    FEngineInstance* Root = (FEngineInstance*)InRoot;
    if (!IsValidAnimScript(Script))
        return 0xFFFFFFFF;
    FAnimNameEntry* Entry = FindAnimName(Script, DACOM_CRC::GetCRC32(Name));
    if (Entry == nullptr)
        return 0xFFFFFFFF;
    FAnimPlayback Record;
    Record.Name = nullptr;                                         // FUN_06611f60
    Record.Root = (FEngineInstance*)((ptrdiff_t)(-1));
    Record.LongestTyped = nullptr;
    Record.Script = nullptr;
    Record.CloneTypedChannels(Entry);
    Record.Script = Script;
    BindPlaybackTracks(&Record, Root, Sink, SinkContext);
    std::vector<FTypedChannel*>::iterator Cursor = Record.Typed.begin();
    while (Cursor != Record.Typed.end())
    {
        Channels->insert(*Cursor);
        ++Cursor;
    }
    Script->AddRef();
    const unsigned long Id = NextPlaybackId;
    NextPlaybackId = Id + 1;
    Playbacks->insert(FAnimPlaybackMap::value_type((long)Id, Record));
    return Id;
}

// LAB_06614bb0 (IAnimation2+0x2C): FUN_066126e0, then every channel joins its mixer with the
// blend duration. The last four arguments are play header [3]..[6], carried as dwords.
unsigned long DACOM_API FAnimationObject::EvalPlayback(unsigned long PlaybackId, unsigned long Flags,
                                                     float Time, float Speed, void* BlendDuration,
                                                     void* Scale, void* TranslationScale,
                                                     void* Transform)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0xFFFFFFFF;
    const unsigned long Result = Playback->EvalTyped(Flags, Time, Speed, (unsigned long)((size_t)BlendDuration),
        (unsigned long)((size_t)Scale),
        (unsigned long)((size_t)TranslationScale),
        (const float*)Transform);
    if (Result == 0xFFFFFFFF)
        return Result;
    const float BlendTime = FloatFromOpaqueDword(BlendDuration);
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        MixerBlendAttach(this, Playback->Typed[Index], BlendTime, Flags);
        Index += 1;
    }
    return Result;
}

// LAB_06614c40 (IAnimation2+0x30): FUN_066127e0.
unsigned long DACOM_API FAnimationObject::EvalStored(unsigned long PlaybackId)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0xFFFFFFFF;
    return Playback->EvalStoredTyped();
}

// LAB_06614c70 (IAnimation2+0x38): FUN_066128f0, then every channel leaves its mixer
// (FUN_06615ec0).
unsigned long DACOM_API FAnimationObject::StopPlayback(unsigned long PlaybackId)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0;
    Playback->StopTyped();
    unsigned long Index = 0;
    while (Index < Playback->Typed.size())
    {
        MixerUnbindTyped(this, Playback->Typed[Index]);
        Index += 1;
    }
    return 0;
}

// LAB_06614cc0 (IAnimation2+0x34): FUN_06612920.
unsigned long DACOM_API FAnimationObject::ApplyChannels(unsigned long PlaybackId)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) != 0)
        Playback->ApplyTyped();
    return 0;
}

// LAB_06614860 (IAnimation2+0x3C): FUN_066129c0. The interface carries the time as void*.
unsigned long DACOM_API FAnimationObject::StopChannels(unsigned long PlaybackId, void* Time)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) != 0)
        Playback->SetTypedTime(FloatFromOpaqueDword(Time));
    return 0;
}

// LAB_06614de0 (IAnimation2+0x48): the playback's root entry in the mixer map
// (FUN_06614f70 on playback +4); when there is one, FUN_06612a70 writes the position into the
// channels and the object overlays.
unsigned long DACOM_API FAnimation2Facet::SetOverlayPos(unsigned long PlaybackId, float* Pos)
{
    FAnimationObject* This = (FAnimationObject*)this;
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    FMixerDescMap* Inner =
        This->MixerFindKey((long)((ptrdiff_t)(Playback->Root)));
    if (Inner != nullptr)
        Playback->WriteOverlayPos(Pos, Inner);
    return 0;
}

// LAB_06614fd0 (IAnimation2+0x4C): as above with FUN_06612b80 (orientation matrix).
unsigned long DACOM_API FAnimation2Facet::SetOverlayExtra(unsigned long PlaybackId, float* Extra)
{
    FAnimationObject* This = (FAnimationObject*)this;
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    FMixerDescMap* Inner =
        This->MixerFindKey((long)((ptrdiff_t)(Playback->Root)));
    if (Inner != nullptr)
        Playback->WriteOverlayOrient(Extra, Inner);
    return 0;
}

// LAB_066150e0 (IAnimation2+0x54): lookup FUN_06614580, then FUN_066122d0.
unsigned long DACOM_API FAnimationObject::TouchMapped(unsigned long PlaybackId, float* Pos, float* Extra)
{
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(this, PlaybackId, &Playback) == 0)
        return 0;
    return Playback->TouchMappedTyped(Pos, Extra);
}

// LAB_06615120 (IAnimation2+0x58): as SetOverlayPos with FUN_06612c60 (add a delta).
unsigned long DACOM_API FAnimation2Facet::AddOverlayPos(unsigned long PlaybackId, float* Delta)
{
    FAnimationObject* This = (FAnimationObject*)this;
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    FMixerDescMap* Inner =
        This->MixerFindKey((long)((ptrdiff_t)(Playback->Root)));
    if (Inner != nullptr)
        Playback->AddOverlayPos(Delta, Inner);
    return 0;
}

// LAB_06616800 (IAnimation2+0x6C): walk every mixer list under one bind key
// -- a playback's root, or the cookie an IChannel caller bound with. Not a playback id.
unsigned long DACOM_API FAnimationObject::MixerWalk(unsigned long Key, float Time)
{
    MixerWalkKey(this, (long)Key, Time);
    return 0;
}

// LAB_066157f0 (IAnimation2+0x70): every mixer list, then every playing channel no
// mixer leaf ticked.
static unsigned long DACOM_API AnimationMixerWalkAll(FAnimationObject* This, float Time)
{
    MixerWalkAll(This, Time);
    return 0;
}

// LAB_06616430 (IChannel+0x1C): rebind the channel with the caller's play header; the play bit
// follows the bind result, and a playing channel joins its mixer.
unsigned long DACOM_API FAnimationChannelFacet::Play(unsigned long Key, float* Time)
{
    void* ChannelThis = this;
    if (Key == 0xFFFFFFFF || Time == nullptr)
        return 0;
    FAnimationObject* Animation = AnimationFromNested(ChannelThis, 8);
    FTypedChannel* Typed = FindTypedChannel(Animation, Key);
    if (Typed == nullptr)
        return 0;
    const FPlayHeader* Header = (const FPlayHeader*)Time;
    const unsigned long Bound = Typed->TryBindIfTimeValid(Header);
    Typed->PlayState = (unsigned char)(
        ((Typed->PlayState ^ (unsigned char)Bound) & 1) ^ Typed->PlayState);
    const unsigned char bPlaying = Typed->PlayState & 1;
    if (bPlaying != 0)
        MixerBlendAttach(Animation, Typed, Header->BlendDuration, Header->LoopFlags);
    return bPlaying;
}

// LAB_06614b30 (IRootData+0x0C).
unsigned char DACOM_API FAnimationObject::Eval(unsigned long PlaybackId, void* OutSampledPos,
                                               unsigned long Flags, float Time, float Speed,
                                               void* BlendDuration, void* Scale,
                                               float TranslationScale)
{
    void* RootThis = (IRootData*)this;
    // The IRootData facet sits at Animation+0xC.
    FAnimationObject* This = AnimationFromNested(RootThis, 0xC);
    FAnimPlayback* Playback = nullptr;
    if (FindPlaybackRecord(This, PlaybackId, &Playback) == 0)
        return 0;
    unsigned long TranslationScaleBits = 0;
    std::memcpy(&TranslationScaleBits, &TranslationScale, sizeof(TranslationScaleBits));
    const unsigned long Eval = Playback->EvalTyped(Flags, Time, Speed, (unsigned long)((size_t)BlendDuration),
        (unsigned long)((size_t)Scale), TranslationScaleBits, nullptr);
    if (Eval == 0xFFFFFFFF)
        return 0;
    // The world position lands in a 12-byte local of this frame and is dropped; the sampled
    // (local) position goes to the caller.
    unsigned char WorldPos[0xC];
    std::memset(WorldPos, 0, sizeof(WorldPos));
    unsigned char bTouch = 0;
    if (Playback->TouchMappedTyped(WorldPos, OutSampledPos) != 0)
        bTouch = 1;
    Playback->StopTyped();
    return bTouch;
}

static FArchetypeHandle* DACOM_API EngineCreateArchetypeHandle(FEngineObject* This,
                                                              FArchetypeName* Name,
                                                              void* FileSystem);

// The "Object name" / "File name" reads of FUN_066201b0, capped at 0x3F bytes.
static bool ReadPartNameField(IFileSystem* Fs, const char* Field, char* Buffer)
{
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Field, nullptr);
    const int Handle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    if (Handle == -1)
        return false;
    unsigned long Size = Fs->GetSize((unsigned long)Handle, 0); // +0x28
    if (!(Size < 0x3F))
        Size = 0x3F;
    bool bRead = false;
    unsigned long BytesRead = 0;
    if (Fs->Read((unsigned long)Handle, Buffer, Size, &BytesRead, 0) != 0 && // +0x14
        BytesRead == Size)
    {
        bRead = true;
        Buffer[BytesRead] = '\0';
    }
    Fs->CloseFile((unsigned long)Handle); // +0x10
    return bRead;
}

// FUN_066201b0. Retries CreateFileSystem without the parent FS when the first attempt fails.
static GENRESULT LinkCmpndPartHandle(FEngineObject* Engine, const char* Name, void* CmpndFs,
                                    unsigned long FindHandle, void* ParentFs, FArchetypeChild* Child)
{
    FEngBaseFileSystemDesc PartDesc;
    FillFileSystemDesc(&PartDesc, Name, nullptr);
    PartDesc.FindHandle = FindHandle;
    void* FileFs = nullptr;
    void* PartFs = nullptr;
    GENRESULT ObjectResult = (GENRESULT)0xFFFFFFFF;
    GENRESULT FileResult = (GENRESULT)0xFFFFFFFF;
    char FileName[0x40];
    if (AsFileSystem(CmpndFs)->CreateInstance((AGGDESC*)(&PartDesc), &PartFs) == 0)
    {
        IFileSystem* Fs = AsFileSystem(PartFs);
        ObjectResult = ReadPartNameField(Fs, G_NameObjectName, Child->Name) ? GR_OK : GR_E_FAIL;
        FileResult = ReadPartNameField(Fs, G_NameFileName, FileName) ? GR_OK : GR_E_FAIL;
    }
    if (PartFs != nullptr)
    {
        ((IDAComponent*)PartFs)->Release();
        PartFs = nullptr;
    }
    if (ObjectResult < 0 || FileResult < 0)
        return GR_E_FAIL;

    if (Engine->CreateFileSystem(FileName, &FileFs, ParentFs) != 0) // +0x28
    {
        if (FileFs != nullptr)
        {
            ((IDAComponent*)FileFs)->Release();
            FileFs = nullptr;
        }
        if (Engine->CreateFileSystem(FileName, &FileFs, nullptr) != 0) // +0x28
        {
            if (FileFs != nullptr)
                ((IDAComponent*)FileFs)->Release();
            return GR_E_FAIL;
        }
    }

    FArchetypeName Nested;
    Nested.Text = FileName;
    Nested.Crc = DACOM_CRC::GetCRC32(FileName);
    void* PartHandle = Engine->CreateArchetypeHandle(&Nested, FileFs); // +0x30
    if (PartHandle == G_InvalidArchetype)
    {
        if (FileFs != nullptr)
            ((IDAComponent*)FileFs)->Release();
        return GR_E_FAIL;
    }
    Child->Handle = (FArchetypeHandle*)PartHandle;
    Child->Crc = DACOM_CRC::GetCRC32(Child->Name);
    if (FileFs != nullptr)
        ((IDAComponent*)FileFs)->Release();
    return GR_OK;
}

static const unsigned long G_JointTypeFix = 0;
static const unsigned long G_JointTypePris = 1;
static const unsigned long G_JointTypeRev = 2;
static const unsigned long G_JointTypeCyl = 3;
static const unsigned long G_JointTypeSphere = 4;
static const unsigned long G_JointTypeTrans = 5;
static const unsigned long G_JointTypeLoose = 6;
static const unsigned long G_JointUtfNameBytes = 0x40;
static const unsigned long G_JointUtfHeaderBytes = 0x80;
static const unsigned long G_JointUtfMaxBytes = 0xD8;

static unsigned long JointTypeFromName(const char* Name)
{
    if (Name == nullptr)
        return 0xFFFFFFFF;
    if (DACOM_CRC::CompareStringsI(Name, G_NameFix) == 0)
        return G_JointTypeFix;
    if (DACOM_CRC::CompareStringsI(Name, G_NamePris) == 0)
        return G_JointTypePris;
    if (DACOM_CRC::CompareStringsI(Name, G_NameRev) == 0)
        return G_JointTypeRev;
    if (DACOM_CRC::CompareStringsI(Name, G_NameCyl) == 0)
        return G_JointTypeCyl;
    if (DACOM_CRC::CompareStringsI(Name, G_NameSphere) == 0)
        return G_JointTypeSphere;
    if (DACOM_CRC::CompareStringsI(Name, G_NameTrans) == 0)
        return G_JointTypeTrans;
    if (DACOM_CRC::CompareStringsI(Name, G_NameLoose) == 0)
        return G_JointTypeLoose;
    return 0xFFFFFFFF;
}

static void CopyPayloadDwords(unsigned long* Dest, const unsigned long* Source,
                              unsigned long Count)
{
    unsigned long Index = 0;
    while (Index < Count)
    {
        Dest[Index] = Source[Index];
        Index += 1;
    }
}

// Decode the type-specific joint payload from the UTF record.
static void RemapJointPayload(unsigned long Type, const unsigned char* UtfExtra,
                              unsigned long* Payload)
{
    const unsigned long* Source = (const unsigned long*)UtfExtra;
    switch (Type)
    {
    case G_JointTypeFix:
    case G_JointTypeTrans:
    case G_JointTypeLoose:
        CopyPayloadDwords(Payload + 4, Source, 12);
        break;
    case G_JointTypePris:
    case G_JointTypeRev:
        CopyPayloadDwords(Payload + 1, Source, 20);
        break;
    case G_JointTypeCyl:
        CopyPayloadDwords(Payload + 1, Source, 22);
        break;
    case G_JointTypeSphere:
        CopyPayloadDwords(Payload + 1, Source, 15);
        Payload[16] = Source[19];
        Payload[17] = Source[20];
        CopyPayloadDwords(Payload + 19, Source + 15, 4);
        break;
    default:
        break;
    }
}

// PTR_LAB_06621118: joint type to UTF record size (not the FUN_06625560 apply).
static unsigned long JointUtfRecordSize(unsigned long Type)
{
    if (Type == G_JointTypeFix || Type == G_JointTypeTrans || Type == G_JointTypeLoose)
        return 0xB0;
    if (Type == G_JointTypePris || Type == G_JointTypeRev)
        return 0xD0;
    if (Type == G_JointTypeCyl)
        return 0xD8;
    if (Type == G_JointTypeSphere)
        return 0xD4;
    return 0;
}

// FUN_0661eb30: the first child whose +0x40 equals the CRC. No tests.
FArchetypeChild* FArchetypeHandle::FindChildByCrc(unsigned long Crc)
{
    for (std::vector<FArchetypeChild>::iterator Child = Children.begin();
         Child != Children.end(); ++Child)
    {
        if (Child->Crc == Crc)
            return &*Child;
    }
    return nullptr;
}

// The record loop of FUN_06620500 for one open Cons file.
static void ApplyConsFileRecords(FArchetypeHandle* Handle, IFileSystem* Fs, int File,
                                 unsigned long Type, unsigned long RecordSize, unsigned long FileSize)
{
    unsigned long Left = FileSize / RecordSize;
    while (Left != 0)
    {
        unsigned char Buffer[G_JointUtfMaxBytes];
        unsigned long BytesRead = 0;
        if (Fs->Read((unsigned long)File, Buffer, RecordSize, &BytesRead, 0) != 0 && // +0x14
            BytesRead == RecordSize)
        {
            const char* FirstName = (const char*)Buffer;
            const char* SecondName = (const char*)(Buffer + G_JointUtfNameBytes);
            FArchetypeChild* FirstPart =
                Handle->FindChildByCrc(DACOM_CRC::GetCRC32(FirstName));
            FArchetypeChild* SecondPart =
                Handle->FindChildByCrc(DACOM_CRC::GetCRC32(SecondName));
            if (FirstPart == nullptr || SecondPart == nullptr)
            {
                char Message[0x2000];
                _snprintf(Message, sizeof(Message), "Joint %s<->%s not found!", FirstName,
                          SecondName);
                EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x389, "WARNING", Message);
            }
            else
            {
                // A zeroed temporary appended with insert(end, 1, record) (FUN_066237a0).
                FConstraintRecord Record = {};
                Record.ChildPart = SecondPart;
                Record.ParentPart = FirstPart;
                Record.Payload[0] = Type;
                RemapJointPayload(Type, Buffer + G_JointUtfHeaderBytes, Record.Payload);
                Handle->Constraints.push_back(Record);
            }
        }
        Left -= 1;
    }
}

// The Cons pass of FUN_06620500 ("Cons" is DAT_0662a788).
static void ParseConsConstraints(FArchetypeHandle* Handle, IFileSystem* Fs)
{
    if (Fs->OpenNode(G_NameCons) == 0) // +0x60
        return;
    WIN32_FIND_DATAA FindData;
    const int FindHandle = Fs->FindFirst(G_NameStar, // +0x48
                                      &FindData);
    if (FindHandle != -1)
    {
        do
        {
            if (FindData.cFileName[0] == '.')
                continue;
            FEngBaseFileSystemDesc Desc;
            FillFileSystemDesc(&Desc, FindData.cFileName, nullptr);
            Desc.FindHandle = (unsigned long)FindHandle;
            const int File = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
            if (File == -1)
                continue;
            const unsigned long FileSize = Fs->GetSize((unsigned long)File, 0); // +0x28
            const unsigned long Type = JointTypeFromName(FindData.cFileName);
            const unsigned long RecordSize = JointUtfRecordSize(Type);
            if (Type == 0xFFFFFFFF)
                EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x376, "WARNING",
                             "Engine: unknown joint type, probably random garbage");
            // Retail: RecordSize is then 0 and the record count divides by it (06620a79 DIV EBP).
            ApplyConsFileRecords(Handle, Fs, File, Type, RecordSize, FileSize);
            Fs->CloseFile((unsigned long)File); // +0x10
        } while (Fs->FindNext((unsigned long)FindHandle, // +0x4C
                            &FindData) != 0);
        Fs->FindClose((unsigned long)FindHandle); // +0x50
    }
    Fs->OpenNode(G_NameParentDot); // +0x60
}

// .data:0662a798: the text both FUN_06620500 part failures push.
static const char* const G_CmpndPartFailText =
    "Engine: create_compound_archetype: unable to create part archetype, ignoring...";

// FUN_06620500.
static GENRESULT ParseCmpndChildren(FEngineObject* Engine, FArchetypeHandle* Handle, void* CmpndFs,
                                   void* ParentFs)
{
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return GR_E_FAIL;
    IFileSystem* Fs = AsFileSystem(CmpndFs);

    std::size_t Count = Handle->Children.size();
    FArchetypeChild* Child = AppendArchetypeChild(Handle);
    if (LinkCmpndPartHandle(Engine, G_NameRoot, CmpndFs, 0xFFFFFFFF, ParentFs, Child) < 0)
    {
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x331, "WARNING", G_CmpndPartFailText);
        ShrinkArchetypeChildren(Handle, Count);
    }

    WIN32_FIND_DATAA FindData;
    const int FindHandle = Fs->FindFirst(G_NamePartStar, // +0x48
                                      &FindData);
    if (FindHandle != -1)
    {
        do
        {
            Count = Handle->Children.size();
            Child = AppendArchetypeChild(Handle);
            if (LinkCmpndPartHandle(Engine, FindData.cFileName, CmpndFs,
                                    (unsigned long)FindHandle, ParentFs, Child) < 0)
            {
                ShrinkArchetypeChildren(Handle, Count);
                EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x345, "WARNING",
                             G_CmpndPartFailText);
            }
        } while (Fs->FindNext((unsigned long)FindHandle, // +0x4C
                            &FindData) != 0);
        Fs->FindClose((unsigned long)FindHandle); // +0x50
    }

    ParseConsConstraints(Handle, Fs);
    return GR_OK;
}

// LAB_06621150 (IEngine +0x30).
static FArchetypeHandle* DACOM_API EngineCreateArchetypeHandle(FEngineObject* This,
                                                              FArchetypeName* Name,
                                                              void* FileSystem)
{
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, G_NameCmpnd, nullptr);
    void* CmpndFs = nullptr;
    if (Name->Crc == 0)
        return G_InvalidArchetype;

    FArchetypeHandle* Existing = This->LookupArchetype(Name);
    if (Existing != G_InvalidArchetype)
    {
        This->AddRefHandle(Existing); // +0x38
        return Existing;
    }
    if (FileSystem == nullptr)
        return G_InvalidArchetype;

    FArchetypeHandle* Handle = (FArchetypeHandle*)(This->CreateCompoundArchetype(Name)); // +0x2C
    if (Handle == G_InvalidArchetype)
        return G_InvalidArchetype;

    if (AsFileSystem(FileSystem)->CreateInstance((AGGDESC*)(&Desc), &CmpndFs) < 0 ||
        ParseCmpndChildren(This, Handle, CmpndFs, FileSystem) < 0)
        FanoutIecCreateArchetype(This, Handle, FileSystem);

    if (CmpndFs != nullptr)
        ((IDAComponent*)CmpndFs)->Release();
    return Handle;
}

// LAB_06621850 (IEngine+0x40). No null test on Out.
static FArchetypeName* DACOM_API EngineGetArchetypeName(FEngineObject* This, FArchetypeName* Out,
                                                       FArchetypeHandle* Handle)
{
    (void)This;
    if (Handle == G_InvalidArchetype || Handle == nullptr)
    {
        Out->Text = nullptr;
        Out->Crc = DACOM_CRC::GetCRC32(nullptr);
        return Out;
    }
    // Retail returns the string's _Ptr (+4), or MSVCP60's _Nullstr when it has none.
    Out->Text = Handle->Name.c_str();
    Out->Crc = Handle->Crc;                                  // +0x10
    return Out;
}

// LAB_06621530 (IEngine+0x44): lookup + slot 14.
void* DACOM_API FEngineObject::FindArchetype(FEngineName* InName)
{
    FArchetypeName* Name = InName;
    FArchetypeHandle* Handle = LookupArchetype(Name);
    if (Handle != G_InvalidArchetype)
        CallEngineSlot14(this, Handle);
    return Handle;
}

// LAB_06622400 (IEngine+0x4C).
unsigned long DACOM_API FEngineObject::IsCompoundArchetype(void* InHandle)
{
    FArchetypeHandle* Handle = (FArchetypeHandle*)InHandle;
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return 0;
    return Handle->Children.size() != 0 ? 1 : 0;
}

// Packed 3x3 orientation followed by translation (12 floats).
static float G_IdentityMatrix[12] = {
    1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};

static void InitIdentity3x4(float* Matrix)
{
    std::memcpy(Matrix, G_IdentityMatrix, sizeof(G_IdentityMatrix));
}

static void InitInstanceMatrix(FEngineInstance* Instance)
{
    InitIdentity3x4(Instance->Matrix);
}

static FEngineInstance* const G_InvalidInstance =
    (FEngineInstance*)G_InvalidEngineHandle;

static bool IsValidInstance(FEngineInstance* Instance)
{
    return Instance != G_InvalidInstance && Instance != nullptr;
}

// Engine-owned transform.

// LAB_0661f340 (slot 0). +0x44 (user) is NOT touched.
GENRESULT DACOM_API FInstanceTransform::Init(void* Instance)
{
    (void)Instance;
    InitIdentity3x4(Matrix);
    Extra = 0.0f;
    ExtraVec[0] = 0.0f;
    ExtraVec[1] = 0.0f;
    ExtraVec[2] = 0.0f;
    return GR_OK;
}

// LAB_06612160 (slots 1 and 2): no body.
GENRESULT DACOM_API FInstanceTransform::AddRef(void* Instance)
{
    (void)Instance;
    return GR_OK;
}

GENRESULT DACOM_API FInstanceTransform::Release(void* Instance)
{
    (void)Instance;
    return GR_OK;
}

// LAB_0661f380 (slot 3): +0x28.
GENRESULT DACOM_API FInstanceTransform::SetPosition(void* Instance, const float* Position)
{
    (void)Instance;
    Matrix[9] = Position[0];
    Matrix[10] = Position[1];
    Matrix[11] = Position[2];
    return GR_OK;
}

// LAB_0661f3a0 (slot 4): +0x28.
float* DACOM_API FInstanceTransform::GetPosition(void* Instance)
{
    (void)Instance;
    return Matrix + 9;
}

// LAB_0661f3b0 (slot 5): nine dwords to +4.
GENRESULT DACOM_API FInstanceTransform::SetOrientation(void* Instance, const float* Rows)
{
    (void)Instance;
    std::memcpy(Matrix, Rows, 9 * sizeof(float));
    return GR_OK;
}

// LAB_0661f3d0 (slots 6 and 8): +4.
float* DACOM_API FInstanceTransform::GetOrientation(void* Instance)
{
    (void)Instance;
    return Matrix;
}

float* DACOM_API FInstanceTransform::GetMatrix(void* Instance)
{
    (void)Instance;
    return Matrix;
}

// LAB_0661f3e0 (slot 7): twelve dwords to +4.
GENRESULT DACOM_API FInstanceTransform::SetMatrix(void* Instance, const float* NewMatrix)
{
    (void)Instance;
    std::memcpy(Matrix, NewMatrix, 12 * sizeof(float));
    return GR_OK;
}

// LAB_0661f400 (slot 9): +0x34, +0x38..+0x40.
GENRESULT DACOM_API FInstanceTransform::GetExtra(void* Instance, float* OutExtra, float* OutVec)
{
    (void)Instance;
    *OutExtra = Extra;
    OutVec[0] = ExtraVec[0];
    OutVec[1] = ExtraVec[1];
    OutVec[2] = ExtraVec[2];
    return GR_OK;
}

// LAB_0661f430 (slot 10).
GENRESULT DACOM_API FInstanceTransform::SetExtra(void* Instance, float NewExtra, const float* Vec)
{
    (void)Instance;
    Extra = NewExtra;
    ExtraVec[0] = Vec[0];
    ExtraVec[1] = Vec[1];
    ExtraVec[2] = Vec[2];
    return GR_OK;
}

// LAB_0661f460 (slot 11).
GENRESULT DACOM_API FInstanceTransform::SetUser(void* Instance, unsigned long NewUser)
{
    (void)Instance;
    User = NewUser;
    return GR_OK;
}

// LAB_0661f470 (slot 12).
unsigned long DACOM_API FInstanceTransform::GetUser(void* Instance)
{
    (void)Instance;
    return User;
}

// LAB_0661f320 (slot 13): returns 0.
unsigned long DACOM_API FInstanceTransform::MarkWorldDirty(void* Instance)
{
    (void)Instance;
    return 0;
}

// FUN_0661f2f0 (free list DAT_0662b114) + FUN_0661f310 (ctor: +0x44 = 0, vtable 066295d0).
// The rest of the block is left as the allocator hands it out (slot 0 Init fills it).
static FInstanceTransform* AllocInstanceTransform()
{
    void* Block = G_TransformPool.Alloc();
    if (Block == nullptr)
        return nullptr;
    return new (Block) FInstanceTransform;
}

// FUN_0661f330 (dtor: vtable back to 066295d0) + FUN_0661f300 (block back to DAT_0662b114).
void FInstanceTransform::Free()
{
    this->~FInstanceTransform();
    G_TransformPool.Free(this);
}

static void CopyTransformState(IInstanceTransform* Dest, IInstanceTransform* Source,
                               FEngineInstance* Instance)
{
    // Always through the interface on both sides. No tests. The two locals of the extra pair
    // are uninitialised in retail; the port zeroes them.
    Dest->SetMatrix(Instance, Source->GetMatrix(Instance));
    float Extra = 0.0f;
    float ExtraVec[3] = {0.0f, 0.0f, 0.0f};
    Source->GetExtra(Instance, &Extra, ExtraVec);
    Dest->SetExtra(Instance, Extra, ExtraVec);
    const unsigned long User = Source->GetUser(Instance);
    Dest->SetUser(Instance, User);
}

// FUN_0661eb80.
static FEngineInstance* EngineInstanceInit(FEngineInstance* This, FArchetypeHandle* Handle)
{
    if (This == nullptr)
        return nullptr;
    std::memset(This, 0, sizeof(FEngineInstance));
    This->Archetype = Handle;
    This->Extent = -1.0f;
    This->bWorldDirty = 1;
    This->JointState[3] = 1.0f;
    This->Relative[0] = 0xFFFFFFFF;
    InitInstanceMatrix(This);
    return This;
}

// FUN_0661eb60 (pool DAT_0662b158, 0xEC) + FUN_0661eb80 (ctor). A null allocation yields 0,
// which retail then dereferences -- no failure branch.
static FEngineInstance* AllocEngineInstance(FArchetypeHandle* Handle)
{
    FEngineInstance* Instance = (FEngineInstance*)(G_InstancePool.Alloc());
    if (Instance != nullptr)
        EngineInstanceInit(Instance, Handle);
    return Instance;
}

// The instance-root map key: the instance pointer as a signed int (FUN_06612e70 compares signed).
static long EngineInstanceKey(FEngineInstance* Instance)
{
    return (long)((intptr_t)Instance);
}

// FUN_06623ac0 with the pair {Instance, Instance} and the cache update every caller does
// after it.
static void InsertRootInstance(FEngineObject* Engine, FEngineInstance* Instance)
{
    FEngineInstanceMap::iterator It =
        Engine->Instances.insert(FEngineInstanceMap::value_type(EngineInstanceKey(Instance), Instance))
            .first;
    if (It != Engine->Instances.end())
    {
        Engine->CachedInstance = It;
        Engine->bInstanceCacheValid = 1;
    }
}

// The erase every caller inlines (FUN_06625220 / FUN_06624ca0 / FUN_06625500 / FUN_06623200).
static void EraseRootInstance(FEngineObject* Engine, FEngineInstance* Instance)
{
    const long Key = EngineInstanceKey(Instance);
    if (Engine->bInstanceCacheValid != 0 && Engine->CachedInstance->first == Key)
        Engine->bInstanceCacheValid = 0;
    Engine->Instances.erase(Key);
}


// FUN_06625560: this = inst+0x5C, dest = inst+0x2C. Jump table PTR_LAB_0662aa84.
// Relative: [0] type, [1..3] rest pos, [4..6] T @ +0x10, [7..15] 3x3 @ +0x1C, [16..18] axis @ +0x40.
// DAT_06629510 = 0.5f (half-angle). Only 3DMath +0x20/+0x2C/+0x54 are used; there is no IK.
static const float G_JointHalfAngle = 0.5f;

static void CopyRelative3x3(float* Dest, const float* Rel)
{
    std::memcpy(Dest, Rel + 7, 9 * sizeof(float));
}

static void CopyRelativeTranslation(float* Dest, const float* Rel)
{
    Dest[9] = Rel[4];
    Dest[10] = Rel[5];
    Dest[11] = Rel[6];
}

static void AddVec3(float* Dest, const float* A, const float* B)
{
    Dest[0] = A[0] + B[0];
    Dest[1] = A[1] + B[1];
    Dest[2] = A[2] + B[2];
}

static void SubVec3(float* Dest, const float* A, const float* B)
{
    Dest[0] = A[0] - B[0];
    Dest[1] = A[1] - B[1];
    Dest[2] = A[2] - B[2];
}

// Quaternion order is W, X, Y, Z; preserve the float spill sites.
static void AxisAngleToQuat(float* OutQuat, const float* Axis, float Angle)
{
    const double Half = (double)Angle * (double)G_JointHalfAngle;
    const double Sin = std::sin(Half);
    const double X = Axis[0];
    const double Y = Axis[1];
    const double Z = Axis[2];
    const double Len = std::sqrt(X * X + Y * Y + Z * Z);
    const float K = (float)(Sin / Len);
    OutQuat[0] = (float)(std::cos(Half));
    OutQuat[1] = (float)((double)K * X);
    OutQuat[2] = (float)((double)K * Y);
    OutQuat[3] = (float)((double)K * Z);
}

// Preserve separate float rounding for Y/Z translation intermediates.
static void SlideTranslation(float* OutT, const float* Rel, float Slide, const float* V)
{
    const double S = Slide;
    const double Ax = (double)(Rel[16]) * S;
    const float Ty = (float)(S * (double)(Rel[17]));
    const float Tz = (float)(S * (double)(Rel[18]));
    const float Pz = (float)((double)Tz + (double)(Rel[3]));
    OutT[0] = (float)((Ax + (double)(Rel[1])) - (double)(V[0]));
    OutT[1] = (float)(((double)Ty + (double)(Rel[2])) -
                                 (double)(V[1]));
    OutT[2] = (float)((double)Pz - (double)(V[2]));
}

static void MathMulMatrix3(I3DMathEngine* Math, float* Out9, const float* Left, const float* Right)
{
    MathMulMatrix(Math, Out9, Left, Right);
}

static void MathTransformVec(I3DMathEngine* Math, float* Out3, const float* Matrix, const float* In3)
{
    MathTransformMatrix(Math, Out3, Matrix, In3);
}

// FUN_0661ed40: the transform at +4, slot 9. No checks.
void FEngineInstance::TransformExtra(float* OutExtent, float* OutVec)
{
    Transform->GetExtra(this, OutExtent, OutVec);
}

// DAT_0662b1b0: the 3DMathEngine every joint helper re-checks before each call
// (FUN_06628190 creates it).
static I3DMathEngine* G_JointMath = nullptr;

static I3DMathEngine* JointMath()
{
    if (G_JointMath == nullptr)
        G_JointMath = Create3DMathEngine();
    return G_JointMath;
}

// Rev / Sphere tail: T = Rel[1..3] - Dest 3x3 * Rel[4..6], float subtractions.
static void ApplyRestTranslation(float* Dest, const float* Rel)
{
    float Rotated[3];
    MathTransformVec(JointMath(), Rotated, Dest, Rel + 4);
    SubVec3(Dest + 9, Rel + 1, Rotated);
}

static void ApplyFixJoint(float* Dest, const float* Rel)
{
    CopyRelative3x3(Dest, Rel);
    CopyRelativeTranslation(Dest, Rel);
}

static void ApplyTransJoint(float* Dest, const float* Rel, const float* JointState)
{
    CopyRelative3x3(Dest, Rel);
    AddVec3(Dest + 9, JointState, Rel + 4);
}

// LAB_06625730 (Pris).
static void ApplyPrisJoint(float* Dest, const float* Rel, const float* JointState)
{
    CopyRelative3x3(Dest, Rel);
    float Rotated[3];
    MathTransformVec(JointMath(), Rotated, Dest, Rel + 4);
    SlideTranslation(Dest + 9, Rel, JointState[0], Rotated);
}

// LAB_066255e0 (Rev): the joint rotation multiplies on the LEFT of the rest 3x3.
static void ApplyRevJoint(float* Dest, const float* Rel, const float* JointState)
{
    float Quat[4];
    float Rot[9];
    AxisAngleToQuat(Quat, Rel + 16, JointState[0]);
    MathQuatToMatrix(JointMath(), Rot, Quat);
    float Product[9];
    MathMulMatrix3(JointMath(), Product, Rot, Rel + 7);
    std::memcpy(Dest, Product, sizeof(Product));
    ApplyRestTranslation(Dest, Rel);
}

// LAB_066257f0 (Cyl): rotation from State[1] multiplies on the RIGHT; slide from State[0].
static void ApplyCylJoint(float* Dest, const float* Rel, const float* JointState)
{
    float Quat[4];
    float Rot[9];
    AxisAngleToQuat(Quat, Rel + 16, JointState[1]);
    MathQuatToMatrix(JointMath(), Rot, Quat);
    float Product[9];
    MathMulMatrix3(JointMath(), Product, Rel + 7, Rot);
    std::memcpy(Dest, Product, sizeof(Product));
    float Rotated[3];
    MathTransformVec(JointMath(), Rotated, Dest, Rel + 4);
    SlideTranslation(Dest + 9, Rel, JointState[0], Rotated);
}

// LAB_06625980 (Sphere): quaternion State[3..6].
static void ApplySphereJoint(float* Dest, const float* Rel, const float* JointState)
{
    float Quat[4];
    std::memcpy(Quat, JointState + 3, sizeof(Quat));
    float Rot[9];
    MathQuatToMatrix(JointMath(), Rot, Quat);
    float Product[9];
    MathMulMatrix3(JointMath(), Product, Rot, Rel + 7);
    std::memcpy(Dest, Product, sizeof(Product));
    ApplyRestTranslation(Dest, Rel);
}

// LAB_06625af0 (Loose): quaternion State[3..6], translation State[0..2] + Rel[4..6].
static void ApplyLooseJoint(float* Dest, const float* Rel, const float* JointState)
{
    float Quat[4];
    std::memcpy(Quat, JointState + 3, sizeof(Quat));
    float Rot[9];
    MathQuatToMatrix(JointMath(), Rot, Quat);
    float Product[9];
    MathMulMatrix3(JointMath(), Product, Rot, Rel + 7);
    std::memcpy(Dest, Product, sizeof(Product));
    AddVec3(Dest + 9, JointState, Rel + 4);
}

// FUN_06625560 (thiscall on inst+0x5C); only caller FUN_0661f250.
static unsigned long ApplyJointToLocal(FEngineInstance* Instance, unsigned char bForce)
{
    if (Instance->bWorldDirty == 0 && bForce == 0)
        return 0;
    Instance->bWorldDirty = 0;
    const unsigned long Type = Instance->Relative[0];
    if (Type == 0xFFFFFFFF)
        return 0;

    float* Dest = Instance->Matrix;
    const float* Rel = (const float*)(Instance->Relative);
    const float* State = Instance->JointState;
    if (Type == G_JointTypeFix)
        ApplyFixJoint(Dest, Rel);
    else if (Type == G_JointTypePris)
        ApplyPrisJoint(Dest, Rel, State);
    else if (Type == G_JointTypeRev)
        ApplyRevJoint(Dest, Rel, State);
    else if (Type == G_JointTypeCyl)
        ApplyCylJoint(Dest, Rel, State);
    else if (Type == G_JointTypeSphere)
        ApplySphereJoint(Dest, Rel, State);
    else if (Type == G_JointTypeTrans)
        ApplyTransJoint(Dest, Rel, State);
    else if (Type == G_JointTypeLoose)
        ApplyLooseJoint(Dest, Rel, State);
    else
        return 0;
    return 1;
}

// FUN_0661ec90: copy 0x17 dwords from constraint+8 to inst+0x5C, then link as the last child.
unsigned char FEngineInstance::ConnectChild(FEngineInstance* Child, const unsigned long* Payload)
{
    Child->bWorldDirty = 1;
    std::memcpy(Child->Relative, Payload, sizeof(Child->Relative));
    FEngineInstance* Last = LastChild;
    LastChild = Child;
    if (Last == nullptr)
        FirstChild = Child;
    else
        Last->NextSibling = Child;
    Child->NextSibling = nullptr;
    Child->PrevSibling = Last;
    ChildCount += 1;
    Child->Parent = this;
    InvalidateExtentChain(1);
    return 1;
}

// FUN_0661ed60: walk up the parents (+0x10), writing -1 to the extent (+0xD8) until one already
// holds -1 (a dword compare). No test of this.
unsigned char FEngineInstance::InvalidateExtentChain(unsigned char bFromThis)
{
    FEngineInstance* Cursor = this;
    if (bFromThis == 0)
        Cursor = Cursor->Parent;
    while (Cursor != nullptr)
    {
        unsigned long Bits = 0;
        std::memcpy(&Bits, &Cursor->Extent, sizeof(Bits));
        if (Bits == 0xBF800000)
            break;
        Cursor->Extent = -1.0f;
        Cursor = Cursor->Parent;
    }
    return 1;
}

// FUN_0661ece0 (only caller IEngine slot 33).
void FEngineInstance::SetExtentAndInvalidate(float Extent, const float* Vec, unsigned char bXform)
{
    if (bXform != 0)
    {
        Transform->SetExtra(this, Extent, Vec);
        InvalidateExtentChain(bXform);
        return;
    }
    this->Extent = Extent;
    ExtentVec[0] = Vec[0];
    ExtentVec[1] = Vec[1];
    ExtentVec[2] = Vec[2];
    InvalidateExtentChain(0);
}

// FUN_0661ec30 (thiscall, this = PARENT).
unsigned char FEngineInstance::UnlinkChild(FEngineInstance* Node)
{
    if (Node->PrevSibling == nullptr)
        FirstChild = Node->NextSibling;
    else
        Node->PrevSibling->NextSibling = Node->NextSibling;
    if (Node->NextSibling == nullptr)
        LastChild = Node->PrevSibling;
    else
        Node->NextSibling->PrevSibling = Node->PrevSibling;
    ChildCount -= 1;
    Node->Parent = nullptr;
    Node->NextSibling = nullptr;
    Node->PrevSibling = nullptr;
    return 1;
}


// FUN_06621630's leaf path. No failure branch and no instance-map insert.
static FEngineInstance* EngineCreateInstanceSimple(FEngineObject* This, FArchetypeHandle* Handle,
                                                  IInstanceTransform* Transform)
{
    This->AddRefHandle(Handle);
    FEngineInstance* Instance = AllocEngineInstance(Handle);
    This->SetInstanceTransform(Instance, Transform);
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->CreateInstance(Instance, Handle);
    Instance->Transform->AddRef(Instance);
    return Instance;
}

static FEngineInstance* DACOM_API EngineCreateInstance(FEngineObject* This,
                                                      FArchetypeHandle* Handle, void* Transform);

// FUN_06622860, called only from FUN_06621630 for compound archetypes.
static FEngineInstance* EngineCreateInstanceTree(FEngineObject* This, FArchetypeHandle* Handle,
                                                IInstanceTransform* Transform)
{
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return G_InvalidInstance;

    std::map<unsigned long, FEngineInstance*> Parts; // FUN_06623730

    // The caller's transform goes to every child until the first one is created
    // successfully; every later part then gets its own engine-owned transform.
    IInstanceTransform* ChildTransform = Transform;
    FArchetypeChild* Child = Handle->Children.data();
    while (Child != Handle->Children.data() + Handle->Children.size())
    {
        // No filtering: slot 23 itself answers -1 for a -1/null handle.
        FEngineInstance* ChildInst =
            (FEngineInstance*)(This->CreateInstance(Child->Handle, ChildTransform));
        if (ChildInst != G_InvalidInstance)
        {
            ChildInst->ParentArchetype = Handle; // +0xC
            ChildInst->PartChild = Child;        // +0xE8
            // Overwrites an existing entry with the same CRC, as operator[] does.
            Parts[Child->Crc] = ChildInst;
            This->AddRefHandle(Handle);
            ChildTransform = nullptr;
        }
        Child += 1;
    }

    FEngineInstance* Root = nullptr;
    FConstraintRecord* Constraint = Handle->Constraints.data();
    while (Constraint != Handle->Constraints.data() + Handle->Constraints.size())
    {
        // A record with a null child or parent part is skipped silently.
        if (Constraint->ChildPart != nullptr && Constraint->ParentPart != nullptr)
        {
            std::map<unsigned long, FEngineInstance*>::iterator ChildIt =
                Parts.find(Constraint->ChildPart->Crc);
            if (ChildIt == Parts.end())
            {
                FEngineName ArchName;
                const char* ArchText = This->GetArchetypeName(&ArchName, Handle)->Text;
                if (ArchText == nullptr)
                    ArchText = "";               // DAT_0662ace4
                char Message[0x2000];
                _snprintf(Message, sizeof(Message),
                          "Unable to find child part '%s' in archetype '%s'",
                          Constraint->ChildPart->Name, ArchText);
                EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x8D7, "WARNING", Message);
            }
            else
            {
                FEngineInstance* ChildInst = ChildIt->second;
                std::map<unsigned long, FEngineInstance*>::iterator ParentIt =
                    Parts.find(Constraint->ParentPart->Crc);
                if (ParentIt == Parts.end())
                {
                    FEngineName ArchName;
                    const char* ArchText = This->GetArchetypeName(&ArchName, Handle)->Text;
                    if (ArchText == nullptr)
                        ArchText = "";           // DAT_0662ace4
                    char Message[0x2000];
                    _snprintf(Message, sizeof(Message),
                              "Unable to find parent part '%s' in archetype '%s'",
                              Constraint->ParentPart->Name, ArchText);
                    EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x8E0, "WARNING", Message);
                }
                else
                {
                    // The parent is kept as Root even when the connect fails.
                    Root = ParentIt->second;
                    if (Root->ConnectChild(ChildInst, Constraint->Payload) == 0)
                        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x8EB, "WARNING",
                                     "Unable to connect child to parent");
                }
            }
        }
        Constraint += 1;
    }

    if (Root != nullptr)
    {
        // Climb to the top of the last connected parent's tree.
        while (Root->Parent != nullptr)
            Root = Root->Parent;
    }
    else
    {
        // No constraint connected anything: the part with the lowest CRC.
        if (Parts.empty())
            return G_InvalidInstance;
        Root = Parts.begin()->second;
    }

    // Instance-root map at +0x10, cache at +0x28 / byte +0x2C.
    InsertRootInstance(This, Root);
    return Root;
}

// FUN_06621630 (IEngine+0x5C): compound archetypes go through FUN_06622860; otherwise
// allocate, slot 28 and IEC+0x20.
static FEngineInstance* DACOM_API EngineCreateInstance(FEngineObject* This,
                                                      FArchetypeHandle* Handle, void* Transform)
{
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return G_InvalidInstance;
    IInstanceTransform* Xf = (IInstanceTransform*)Transform;
    if (Handle->Children.size() != 0)
    {
        FEngineInstance* Root = EngineCreateInstanceTree(This, Handle, Xf);
        if (Root == G_InvalidInstance)
            return Root;
        Root->Transform->AddRef(Root);
        return Root;
    }
    return EngineCreateInstanceSimple(This, Handle, Xf);
}

// FUN_06621710, in retail order.
void FEngineObject::DestroyInstanceBody(FEngineInstance* Instance)
{
    while (Instance->FirstChild != nullptr)
        DestroyInstanceBody(Instance->FirstChild);
    Instance->Transform->Release(Instance);
    if (Instance->Parent != nullptr)
        Instance->Parent->UnlinkChild(Instance);
    FEngineChildNode* Sentinel = EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->DestroyInstance(Instance);
    if ((Instance->Flags & G_InstanceFlagOwnedTransform) != 0 && Instance->Transform != nullptr)
        ((FInstanceTransform*)(Instance->Transform))->Free(); // flag 2: ours
    Instance->Transform = nullptr;
    EraseRootInstance(this, Instance);
    ReleaseHandle(Instance->Archetype);
    if (Instance->ParentArchetype != nullptr)
        ReleaseHandle(Instance->ParentArchetype);
    G_InstancePool.Free(Instance);
}

// LAB_06621830 (IEngine+0x60): an invalid instance is returned as the result, as retail leaves
// the argument in EAX on that path.
GENRESULT DACOM_API FEngineObject::DestroyInstance(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return (GENRESULT)((intptr_t)Instance);
    DestroyInstanceBody(Instance);
    return GR_OK;
}

// LAB_066215d0 (IEngine+0x58): slot 12, slot 23, slot 15.
void* DACOM_API FEngineObject::CreateInstanceFromName(FEngineName* InName, void* FileSystem,
                                                      void* Transform)
{
    FArchetypeName* Name = InName;
    FArchetypeHandle* Handle = EngineCreateArchetypeHandle(this, Name, FileSystem);
    if (Handle == G_InvalidArchetype)
        return G_InvalidInstance;
    FEngineInstance* Instance = EngineCreateInstance(this, Handle, Transform);
    EngineReleaseHandle(this, Handle);
    return Instance;
}

// LAB_06622360 (IEngine +0x50): the argument is an archetype handle; only -1 is tested.
static GENRESULT DACOM_API EngineEnumerateArchetypeConstraints(FEngineObject* This,
                                                              FArchetypeHandle* Handle,
                                                              int(__cdecl* Callback)(void*, void*,
                                                                                      void*, void*),
                                                              void* Context)
{
    (void)This;
    if (Handle == G_InvalidArchetype)
        return (GENRESULT)(-1);
    FConstraintRecord* Record = Handle->Constraints.data();
    FConstraintRecord* const End = Record + Handle->Constraints.size();
    while (Record != End)
    {
        if ((unsigned char)(Callback(Record->ParentPart->Handle,
                                                Record->ChildPart->Handle, &Record->Payload[0],
                                                Context)) == 0)
            break;
        Record += 1;
    }
    return GR_OK;
}

// FUN_066223b0 (IEngine +0x54): only -1 is tested.
static GENRESULT DACOM_API EngineEnumerateArchetypeChildren(FEngineObject* This,
                                                           FArchetypeHandle* Handle,
                                                           int(__cdecl* Callback)(void*, void*, int,
                                                                                   void*),
                                                           void* Context)
{
    (void)This;
    if (Handle == G_InvalidArchetype)
        return (GENRESULT)(-1);
    FArchetypeChild* Child = Handle->Children.data();
    FArchetypeChild* const End = Child + Handle->Children.size();
    while (Child != End)
    {
        if ((unsigned char)(Callback(Handle, Child->Handle, 0, Context)) == 0)
            break;
        Child += 1;
    }
    return GR_OK;
}

// FUN_066218a0 (IEngine+0x64): inst+8, slot 14.
void* DACOM_API FEngineObject::GetInstanceArchetype(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return G_InvalidArchetype;
    EngineAddRefHandle(this, Instance->Archetype);
    return Instance->Archetype;
}

// LAB_06622530 (IEngine+0x68): Text is the part child (+0xE8, whose name leads the record),
// Crc its +0x40. A root instance (+0xC == 0) gets the empty name.
static FArchetypeName* DACOM_API EngineGetInstanceName(FEngineObject* This, FArchetypeName* Out,
                                                      FEngineInstance* Instance)
{
    (void)This;
    if (Out == nullptr)
        return nullptr;
    if (!IsValidInstance(Instance) || Instance->ParentArchetype == nullptr)
    {
        Out->Text = nullptr;
        Out->Crc = DACOM_CRC::GetCRC32(nullptr);
        return Out;
    }
    Out->Text = (char*)(Instance->PartChild);
    if (Instance->PartChild != nullptr)
        Out->Crc = Instance->PartChild->Crc;
    else
        Out->Crc = DACOM_CRC::GetCRC32(nullptr);
    return Out;
}

// FUN_066219d0 (IEngine+0x78): IEC+0x2C on every engine component. Out is not tested; only
// instance -1 is rejected.
GENRESULT DACOM_API FEngineObject::QueryInstance(void* InInstance, const char* Iid, void** Out)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    *Out = nullptr;
    if (Instance == G_InvalidInstance || Iid == nullptr)
        return GR_INVALID_PARMS;
    FEngineChildNode* Sentinel = EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        const GENRESULT Result = AsEngineComponent(Node->Object)->QueryInstance(Instance, Iid, Out);
        if (Result >= 0)
            return GR_OK;
    }
    return (GENRESULT)(-1);
}

// LAB_066218d0 (IEngine +0x70), in retail order.
GENRESULT DACOM_API FEngineObject::SetInstanceTransform(void* InInstance, void* Transform)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    IInstanceTransform* NewTransform = (IInstanceTransform*)Transform;
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return GR_OK;
    const unsigned long OldFlags = Instance->Flags;
    IInstanceTransform* Old = Instance->Transform;
    IInstanceTransform* Attached = NewTransform;
    if (Attached == nullptr)
    {
        Instance->Flags = OldFlags | G_InstanceFlagOwnedTransform;
        Attached = AllocInstanceTransform();
    }
    else
    {
        Instance->Flags = OldFlags & ~G_InstanceFlagOwnedTransform;
    }

    if (Attached == Old)
        return GR_OK;

    Attached->Init(Instance);
    if (Old != nullptr)
    {
        CopyTransformState(Attached, Old, Instance);
        Old->Release(Instance);
    }
    if ((OldFlags & G_InstanceFlagOwnedTransform) != 0 && Old != nullptr)
        ((FInstanceTransform*)Old)->Free(); // flag 2: ours
    Instance->Transform = Attached;
    return GR_OK;
}

// LAB_066219b0 (IEngine +0x74): only -1 is tested. Retail returns Out in EAX; the port
// returns 0.
GENRESULT DACOM_API FEngineObject::GetInstanceTransformObject(void* InInstance, void** InOut)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    IInstanceTransform** Out = (IInstanceTransform**)InOut;
    *Out = nullptr;
    if (Instance != G_InvalidInstance)
        *Out = Instance->Transform;
    return GR_OK;
}

// IEC +0x28 UpdateInstance on every node of +0x54, no tests.
static void FanoutIecUpdateInstance(FEngineObject* This, FEngineInstance* Instance, void* Arg)
{
    FEngineChildNode* Sentinel = This->EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        AsEngineComponent(Node->Object)->UpdateInstance(Instance, Arg);
}


static void EngineUpdateInstanceTree(FEngineObject* This, FEngineInstance* Instance,
                                    unsigned long Flags, void* Arg)
{
    if ((Flags & 2) == 2)
        FanoutIecUpdateInstance(This, Instance, Arg);
    if ((Flags & 1) == 1)
        return;
    FEngineInstance* Child = Instance->FirstChild;
    while (Child != nullptr)
    {
        // FUN_06621ea0 always composes each child; flag 8 only gates the ancestor pass in
        // FUN_06621e60.
        Child->ComposeWorld();
        EngineUpdateInstanceTree(This, Child, Flags, Arg);
        Child = Child->NextSibling;
    }
}

// The transform's vtable +0x34 with the instance; its AL decides the FUN_0661f210 recursion.
static unsigned char MarkInstanceWorldDirty(FEngineInstance* Instance)
{
    return (unsigned char)Instance->Transform->MarkWorldDirty(Instance);
}

// FUN_0661f210. No tests.
static void EngineWalkDirtyChildren(FEngineInstance* Instance)
{
    for (FEngineInstance* Child = Instance->FirstChild; Child != nullptr; Child = Child->NextSibling)
    {
        if (MarkInstanceWorldDirty(Child) != 0)
            EngineWalkDirtyChildren(Child);
    }
}

// DAT_0662b178: FUN_0661f250's own lazily created 3DMathEngine.
static I3DMathEngine* G_ComposeMath = nullptr;

// FUN_0661f250, in retail order.
unsigned char FEngineInstance::ComposeWorld()
{
    if (Parent == nullptr)
        return 0;
    const unsigned long bApplied = ApplyJointToLocal(this, 0);
    const float* ParentWorld = Parent->Transform->GetMatrix(Parent);
    if (G_ComposeMath == nullptr)
        G_ComposeMath = Create3DMathEngine();
    float Composed[12];
    G_ComposeMath->MulTransform(*(FTransform*)Composed, // +0x1C
                                        *(const FTransform*)ParentWorld,
                                        *(const FTransform*)(Matrix));
    float Copy[12];
    std::memcpy(Copy, Composed, sizeof(Copy));
    Transform->SetMatrix(this, Copy);
    if ((bApplied & 0xFF) != 0)
    {
        InvalidateExtentChain(1);
        if (MarkInstanceWorldDirty(this) != 0)
            EngineWalkDirtyChildren(this);
    }
    return 0;
}

// FUN_06621f20.
static void EngineComposeAncestors(FEngineInstance* Instance)
{
    if (Instance == nullptr || Instance->Parent == nullptr)
        return;
    EngineComposeAncestors(Instance->Parent);
    Instance->ComposeWorld();
}

// FUN_06621e60 (IEngine+0x6C): compose ancestors, IEC+0x28, walk children.
GENRESULT DACOM_API FEngineObject::UpdateInstance(void* InInstance, unsigned long Flags, void* Arg)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return GR_OK;
    if ((Flags & 8) == 0)
        EngineComposeAncestors(Instance);
    EngineUpdateInstanceTree(this, Instance, Flags, Arg);
    return GR_OK;
}

// LAB_06621a40 (IEngine +0x7C): inst+0x28. Retail returns the instance argument; the port
// returns 0.
GENRESULT DACOM_API FEngineObject::SetInstanceUserData(void* InInstance, void* Value)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (Instance != G_InvalidInstance && Instance != nullptr)
        Instance->UserData = Value;
    return GR_OK;
}

// LAB_06621a60 (IEngine +0x80): inst+0x28.
void* DACOM_API FEngineObject::GetInstanceUserData(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return nullptr;
    return Instance->UserData;
}

// LAB_06621a80 (IEngine +0x84): FUN_0661ece0. Retail leaves the callee's EAX; the port
// returns 0.
GENRESULT DACOM_API FEngineObject::SetInstanceExtent(void* InInstance, unsigned long Flags, float Extent,
                                                      const float* Vec)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (Instance == G_InvalidInstance || Instance == nullptr)
        return GR_OK;
    Instance->SetExtentAndInvalidate(Extent, Vec, (unsigned char)(Flags & 1));
    return GR_OK;
}

// Distance on the FPU as FUN_0661ed90 does it: the three differences stay unrounded and the
// squares are summed z, y, x.
static double ExtentDistance(const float* A, const float* B)
{
    const double Dx = (double)(A[0]) - (double)(B[0]);
    const double Dy = (double)(A[1]) - (double)(B[1]);
    const double Dz = (double)(A[2]) - (double)(B[2]);
    return std::sqrt(Dz * Dz + Dy * Dy + Dx * Dx);
}

// FUN_0661ed90: the bounding sphere of the instance and its children, cached in +0xD8.
void FEngineInstance::ComputeInstanceExtent(unsigned char bRecursive, float* OutExtent,
                                            float* OutVec)
{
    if (bRecursive == 0)
    {
        TransformExtra(OutExtent, OutVec);
        return;
    }
    if (Extent > 0.0f)
    {
        *OutExtent = Extent;
        OutVec[0] = ExtentVec[0];
        OutVec[1] = ExtentVec[1];
        OutVec[2] = ExtentVec[2];
        return;
    }

    float* WorldXf = Transform->GetMatrix(this);
    float OwnR = 0.0f;
    float OwnLocal[3];
    TransformExtra(&OwnR, OwnLocal);
    float OwnWorld[3];
    Acquire3DMathEngine()->TransformXform(*(FVector*)OwnWorld,
                                                  *(const FTransform*)WorldXf,
                                                  *(const FVector*)OwnLocal);

    float MinV[3];
    float MaxV[3];
    for (unsigned long Axis = 0; Axis < 3; ++Axis)
    {
        MinV[Axis] = OwnWorld[Axis] - OwnR;
        MaxV[Axis] = OwnWorld[Axis] + OwnR;
    }

    FEngineInstance* Child = FirstChild;
    while (Child != nullptr)
    {
        float* ChildXf = Child->Transform->GetMatrix(Child);
        float ChildR = 0.0f;
        float ChildLocal[3];
        Child->ComputeInstanceExtent(1, &ChildR, ChildLocal);
        float ChildWorld[3];
        Acquire3DMathEngine()->TransformXform(*(FVector*)ChildWorld,
                                                      *(const FTransform*)ChildXf,
                                                      *(const FVector*)ChildLocal);
        for (unsigned long Axis = 0; Axis < 3; ++Axis)
        {
            const float Lo = ChildWorld[Axis] - ChildR;
            if (Lo < MinV[Axis])
                MinV[Axis] = Lo;
            const float Hi = ChildWorld[Axis] + ChildR;
            if (MaxV[Axis] < Hi)
                MaxV[Axis] = Hi;
        }
        Child = Child->NextSibling;
    }

    float CenterW[3];
    CenterW[0] = (MaxV[0] + MinV[0]) * 0.5f;
    CenterW[1] = (MaxV[1] + MinV[1]) * 0.5f;
    CenterW[2] = (MaxV[2] + MinV[2]) * 0.5f;
    const float SpanZ = MaxV[2] - MinV[2];
    const float SpanY = MaxV[1] - MinV[1];
    const float SpanX = MaxV[0] - MinV[0];
    float Widest = SpanX < SpanY ? SpanY : SpanX;
    if (Widest < SpanZ)
        Widest = SpanZ;
    float Radius = Widest * 0.5f;
    const double OwnSphere = ExtentDistance(CenterW, OwnWorld) + (double)OwnR;
    if (OwnSphere > (double)Radius)
        Radius = (float)OwnSphere;

    Child = FirstChild;
    while (Child != nullptr)
    {
        float* ChildXf = Child->Transform->GetMatrix(Child);
        float ChildR = 0.0f;
        float ChildLocal[3];
        Child->ComputeInstanceExtent(1, &ChildR, ChildLocal);
        float ChildWorld[3];
        Acquire3DMathEngine()->TransformXform(*(FVector*)ChildWorld,
                                                      *(const FTransform*)ChildXf,
                                                      *(const FVector*)ChildLocal);
        const float ChildSphere =
            (float)(ExtentDistance(ChildWorld, CenterW) + (double)ChildR);
        if (ChildSphere > Radius)
            Radius = ChildSphere;
        Child = Child->NextSibling;
    }

    float LocalCenter[3];
    Acquire3DMathEngine()->InverseTransform(*(FVector*)LocalCenter,
                                                    *(const FTransform*)WorldXf,
                                                    *(const FVector*)CenterW);
    Extent = Radius;
    ExtentVec[0] = LocalCenter[0];
    ExtentVec[1] = LocalCenter[1];
    ExtentVec[2] = LocalCenter[2];
    *OutExtent = Radius;
    OutVec[0] = LocalCenter[0];
    OutVec[1] = LocalCenter[1];
    OutVec[2] = LocalCenter[2];
}

// LAB_06621ab0, IEngine slot 34 (+0x88).
GENRESULT DACOM_API FEngineObject::GetInstanceExtent(void* InInstance, unsigned long Flags,
                                                      float* OutExtent, float* OutVec)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
    {
        *OutExtent = -1.0f;
        OutVec[2] = 0.0f;
        OutVec[1] = 0.0f;
        OutVec[0] = 0.0f;
        return GR_OK;
    }
    if ((Flags & 1) != 0)
    {
        Instance->ComputeInstanceExtent(0, OutExtent, OutVec);
        return GR_OK;
    }
    Instance->ComputeInstanceExtent(1, OutExtent, OutVec);
    return GR_OK;
}

GENRESULT DACOM_API FEngineObject::SetInstancePosition(void* InInstance, void* Position)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return GR_OK;
    return Instance->Transform->SetPosition(Instance, (const float*)Position);
}

// LAB_06621b40 (IEngine +0x90) -> transform +0x10; fallback [DAT_0662b124].
float* DACOM_API FEngineObject::GetInstancePosition(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return G_IdentityMatrix + 9;
    return Instance->Transform->GetPosition(Instance);
}

// LAB_06621b70 (IEngine +0x94) -> transform +0x14.
GENRESULT DACOM_API FEngineObject::SetInstanceOrientation(void* InInstance, void* Rows)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return GR_OK;
    return Instance->Transform->SetOrientation(Instance, (const float*)Rows);
}

// LAB_06621ba0 (IEngine +0x98) -> transform +0x18; fallback [DAT_0662b110].
float* DACOM_API FEngineObject::GetInstanceOrientation(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return G_IdentityMatrix;
    return Instance->Transform->GetOrientation(Instance);
}

// LAB_06621bd0 (IEngine +0x9C) -> transform +0x1C.
GENRESULT DACOM_API FEngineObject::SetInstanceMatrix(void* InInstance, void* Matrix)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return GR_OK;
    return Instance->Transform->SetMatrix(Instance, (const float*)Matrix);
}

// LAB_06621c00 (IEngine +0xA0) -> transform +0x20; fallback DAT_0662b128 itself.
float* DACOM_API FEngineObject::GetInstanceTransform(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return G_IdentityMatrix;
    return Instance->Transform->GetMatrix(Instance);
}

// LAB_06621c30 (IEngine +0xA8 and +0xB0): return [DAT_0662b124] -- the zero
// translation of the identity, not the matrix.
static float* DACOM_API EngineGetIdentityMatrix(FEngineObject* This, void* Unused)
{
    (void)This;
    (void)Unused;
    return G_IdentityMatrix + 9;
}

// LAB_06621c40 (IEngine +0xB4) -> transform +0x2C.
GENRESULT DACOM_API FEngineObject::SetTransformUser(void* InInstance, unsigned long InUser)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    void* User = (void*)InUser;
    if (!IsValidInstance(Instance))
        return GR_OK;
    return Instance->Transform->SetUser(Instance, (unsigned long)User);
}

// LAB_06621c70 (IEngine +0xB8) -> transform +0x30.
unsigned long DACOM_API FEngineObject::GetTransformUser(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return 0;
    return Instance->Transform->GetUser(Instance);
}

// LAB_06622580 (IEngine+0xC4): inst+0x5C, or 0 for a root.
unsigned long* DACOM_API FEngineObject::GetInstanceRelative(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance) || Instance->Parent == nullptr)
        return nullptr;
    return Instance->Relative;
}

static void CopyJointFloats(float* Dest, const float* Source, unsigned long Count)
{
    if (Dest == nullptr || Source == nullptr)
        return;
    unsigned long Index = 0;
    while (Index < Count)
    {
        Dest[Index] = Source[Index];
        Index += 1;
    }
}

// LAB_066226e0 (IEngine+0xC8): set stored joint state. No solver.
unsigned long DACOM_API FEngineObject::SetJointParams(void* InInstance, unsigned long Index,
                                                      const float* Values)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance) || Instance->Parent == nullptr || Values == nullptr)
        return 0;
    if (Index != 0)
    {
        // All 7 floats (the Loose case); only the Index == 0 path sets +0xD4.
        CopyJointFloats(Instance->JointState, Values, 7);
        return 1;
    }

    Instance->bWorldDirty = 1;
    const unsigned long Type = Instance->Relative[0];
    if (Type == G_JointTypePris || Type == G_JointTypeRev)
    {
        const float Input = Values[0];
        const float MinLimit = *(const float*)(&Instance->Relative[0x13]);
        const float MaxLimit = *(const float*)(&Instance->Relative[0x14]);
        if (MinLimit > Input)
            Instance->JointState[0] = MinLimit;
        else if (MaxLimit < Input)
            Instance->JointState[0] = MaxLimit;
        else
            Instance->JointState[0] = Input;
        return 1;
    }
    if (Type == G_JointTypeCyl)
    {
        CopyJointFloats(Instance->JointState, Values, 2);
        return 1;
    }
    if (Type == G_JointTypeSphere)
    {
        CopyJointFloats(Instance->JointState + 3, Values, 4);
        return 1;
    }
    if (Type == G_JointTypeTrans)
    {
        CopyJointFloats(Instance->JointState, Values, 3);
        return 1;
    }
    if (Type == G_JointTypeLoose)
    {
        CopyJointFloats(Instance->JointState, Values, 7);
        return 1;
    }
    return 1;
}

// LAB_066225b0 (IEngine+0xCC): get stored joint state. No solver.
unsigned long DACOM_API FEngineObject::GetJointParams(void* InInstance, unsigned long Index, float* Out)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance) || Instance->Parent == nullptr || Out == nullptr)
        return 0;
    if (Index != 0)
    {
        CopyJointFloats(Out, Instance->JointState, 7);
        return 1;
    }

    const unsigned long Type = Instance->Relative[0];
    if (Type == G_JointTypePris || Type == G_JointTypeRev)
    {
        CopyJointFloats(Out, Instance->JointState, 1);
        return 1;
    }
    if (Type == G_JointTypeCyl)
    {
        CopyJointFloats(Out, Instance->JointState, 2);
        return 1;
    }
    if (Type == G_JointTypeSphere)
    {
        CopyJointFloats(Out, Instance->JointState + 3, 4);
        return 1;
    }
    if (Type == G_JointTypeTrans)
    {
        CopyJointFloats(Out, Instance->JointState, 3);
        return 1;
    }
    if (Type == G_JointTypeLoose)
    {
        CopyJointFloats(Out, Instance->JointState, 7);
        return 1;
    }
    return 1;
}

// LAB_066224b0 (IEngine+0xD4): inst+0x10.
void* DACOM_API FEngineObject::GetInstanceParent(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance) || Instance->Parent == nullptr)
        return G_InvalidInstance;
    return Instance->Parent;
}

// LAB_066224d0 (IEngine+0xD8): first child / next sibling / parent-walk.
void* DACOM_API FEngineObject::GetRelatedInstance(void* InFrom, unsigned long Flags, void* InCursor)
{
    FEngineInstance* From = (FEngineInstance*)InFrom;
    FEngineInstance* Cursor = (FEngineInstance*)InCursor;
    FEngineInstance* Root = IsValidInstance(From) ? From : nullptr;
    FEngineInstance* Node = IsValidInstance(Cursor) ? Cursor : nullptr;
    if (Root == nullptr)
        return G_InvalidInstance;
    if (Node == nullptr)
        return Root->FirstChild != nullptr ? Root->FirstChild : G_InvalidInstance;
    if ((Flags & 1) != 0)
        return Node->NextSibling != nullptr ? Node->NextSibling : G_InvalidInstance;
    if (Node->FirstChild != nullptr)
        return Node->FirstChild;
    while (Node != nullptr)
    {
        if (Node->NextSibling != nullptr)
            return Node->NextSibling;
        Node = Node->Parent;
        if (Node == Root)
            break;
    }
    return G_InvalidInstance;
}

// LAB_06622480 (IEngine+0xDC): walk +0x10 to root.
void* DACOM_API FEngineObject::GetInstanceRoot(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (!IsValidInstance(Instance))
        return G_InvalidInstance;
    while (Instance->Parent != nullptr)
        Instance = Instance->Parent;
    return Instance;
}

// LAB_06622450 (IEngine+0xE0): find the archetype child record by nested handle.
void* DACOM_API FEngineObject::FindArchetypeChild(void* InHandle, void* InChildHandle)
{
    FArchetypeHandle* Handle = (FArchetypeHandle*)InHandle;
    FArchetypeHandle* ChildHandle = (FArchetypeHandle*)InChildHandle;
    if (Handle == G_InvalidArchetype || Handle == nullptr)
        return nullptr;
    FArchetypeChild* Child = Handle->Children.data();          // end read once
    FArchetypeChild* const End = Child + Handle->Children.size();
    while (Child != End)
    {
        if (Child->Handle == ChildHandle)
            return Child;
        Child += 1;
    }
    return nullptr;
}

// LAB_06612df0 (IEngine+0xE4): always -1.
unsigned long DACOM_API FEngineObject::RejectOperation(void* A, void* B)
{
    (void)A;
    (void)B;
    return 0xFFFFFFFF;
}

// LAB_066222a0 (IEngine+0xD0): iterate the instance-root map. No checks.
void* DACOM_API FEngineObject::FindMappedInstance(void* InInstance)
{
    FEngineInstance* Instance = (FEngineInstance*)InInstance;
    if (Instance == G_InvalidInstance)
    {
        if (Instances.empty())
            return G_InvalidInstance;
        return (FEngineInstance*)(
            (intptr_t)(Instances.begin()->first));
    }
    const long Key = EngineInstanceKey(Instance);
    InstanceLookupCount += 1;
    FEngineInstanceMap::iterator It;
    if (bInstanceCacheValid != 0 && CachedInstance->first == Key)
    {
        InstanceCacheHits += 1;
        It = CachedInstance;
    }
    else
    {
        It = Instances.find(Key);
        if (It != Instances.end())
        {
            CachedInstance = It;
            bInstanceCacheValid = 1;
        }
    }
    if (It == Instances.end())
        return G_InvalidInstance;
    ++It;
    if (It == Instances.end())
        return G_InvalidInstance;
    return (FEngineInstance*)((intptr_t)(It->first));
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

// LAB_066212c0 (IEngine+0x34).
void* DACOM_API FEngineObject::DuplicateArchetype(void* InArchIn, FEngineName* InName)
{
    FArchetypeHandle* ArchIn = (FArchetypeHandle*)InArchIn;
    FArchetypeName* Name = InName;
    if (ArchIn == G_InvalidArchetype)
        return G_InvalidArchetype;
    if (ArchIn != nullptr && ArchIn->Children.size() != 0)
    {
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x42A, "WARNING",
                     "Engine: duplicate_archetype: arch_index is a compound archetype (not valid).");
        return G_InvalidArchetype;
    }
    if (LookupArchetype(Name) != G_InvalidArchetype)
    {
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x42F, "WARNING",
                     "Engine: duplicate_archetype: archetype name already exists in duplicate_archetype().");
        return G_InvalidArchetype;
    }

    FArchetypeHandle* NewHandle =
        (FArchetypeHandle*)(CreateCompoundArchetype(Name)); // vtable +0x2C
    if (NewHandle == G_InvalidArchetype)
        return G_InvalidArchetype;
    FanoutIecDuplicateArchetype(this, NewHandle, ArchIn);
    return NewHandle;
}

// FUN_06621560 (IEngine+0x48): IEC+0x1C on every engine component; the first hr >= 0 wins.
// Out is not tested.
GENRESULT DACOM_API FEngineObject::QueryEngineComponent(int Id, void* Name, void** Out)
{
    *Out = nullptr;
    if (Id == -1 || Name == nullptr)
        return GR_INVALID_PARMS;

    FEngineChildNode* Sentinel = EngineComponentList;
    for (FEngineChildNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        const GENRESULT Result = AsEngineComponent(Node->Object)->QueryArchetype(
            (void*)((ptrdiff_t)Id), (const char*)Name, Out);
        if (Result >= 0)
            return GR_OK;
    }
    return (GENRESULT)(-1);
}

// The map key: retail stores the archetype handle as a signed int (FUN_06612e70).
static long HardpointKeyOf(FArchetypeHandle* Handle)
{
    return (long)((intptr_t)Handle);
}

// The cached lookup every IHardpoint / IEngineComponent method of this object inlines.
static FHardpointMap::iterator HardpointLookup(FHardpointObject* This, FArchetypeHandle* Handle)
{
    const long Key = HardpointKeyOf(Handle);
    This->LookupCount += 1;
    if (This->bCacheValid != 0 && This->CachedNode->first == Key)
    {
        This->CacheHits += 1;
        return This->CachedNode;
    }
    FHardpointMap::iterator Found = This->Archetypes.find(Key);
    if (Found != This->Archetypes.end())
    {
        This->CachedNode = Found;
        This->bCacheValid = 1;
    }
    return Found;
}

// FUN_06626350 (IHardpoint+0x0C). No argument checks.
unsigned long DACOM_API FHardpointObject::GetHardpoint(void* InHandle, FHardpointKey const* Key,
                                                       void* Out)
{
    FArchetypeHandle* Handle = (FArchetypeHandle*)InHandle;
    FHardpointMap::iterator Entry = HardpointLookup(this, Handle);
    if (Entry == Archetypes.end())
        return 0;
    FHardpointList& List = Entry->second;
    for (FHardpointList::iterator It = List.begin(); It != List.end(); ++It)
    {
        if (It->Crc == Key->Crc)
        {
            std::memcpy(Out, It->Data, sizeof(It->Data));
            return 1;
        }
    }
    return 0;
}

// LAB_066263e0 (IHardpoint+0x10). No argument checks.
unsigned long DACOM_API FHardpointObject::AddHardpoint(void* InHandle, FHardpointKey const* Key,
                                                       const void* Data)
{
    FArchetypeHandle* Handle = (FArchetypeHandle*)InHandle;
    FHardpointMap::iterator Entry = HardpointLookup(this, Handle);
    if (Entry == Archetypes.end())
    {
        Entry = Archetypes
                    .insert(FHardpointMap::value_type(HardpointKeyOf(Handle), FHardpointList()))
                    .first;
        if (Entry != Archetypes.end())
        {
            CachedNode = Entry;
            bCacheValid = 1;
        }
        if (Entry == Archetypes.end())
            return 0;
    }
    FHardpointList& List = Entry->second;
    for (FHardpointList::iterator It = List.begin(); It != List.end(); ++It)
    {
        if (It->Crc == Key->Crc)
        {
            std::memcpy(It->Data, Data, sizeof(It->Data));
            return 1;
        }
    }
    FHardpointRecord Temp(Key->Crc, Key->Name);
    std::memcpy(Temp.Data, Data, sizeof(Temp.Data));
    List.push_back(Temp);
    return 1;
}

// FUN_06626610 (IHardpoint+0x14). No argument checks.
GENRESULT DACOM_API FHardpointObject::EnumerateHardpoints(FHardpointEnumProc Callback,
                                                          void* InHandle, void* Context)
{
    FArchetypeHandle* Handle = (FArchetypeHandle*)InHandle;
    FHardpointMap::iterator Entry = HardpointLookup(this, Handle);
    if (Entry == Archetypes.end())
        return GR_OK;
    FHardpointList& List = Entry->second;
    for (FHardpointList::iterator It = List.begin(); It != List.end(); ++It)
        Callback(It->Name, Context);
    return GR_OK;
}

// FUN_066269b0 (IHardpoint+0x1C). No argument checks.
char const* DACOM_API FHardpointObject::FindHardpoint(void* InHandle, unsigned long const* Crc)
{
    FArchetypeHandle* Handle = (FArchetypeHandle*)InHandle;
    FHardpointMap::iterator Entry = HardpointLookup(this, Handle);
    if (Entry == Archetypes.end())
        return nullptr;
    FHardpointList& List = Entry->second;
    for (FHardpointList::iterator It = List.begin(); It != List.end(); ++It)
    {
        if (It->Crc == *Crc)
            return It->Name;
    }
    return nullptr;
}

// LAB_06626690 (IHardpoint+0x18): connect InstA's hardpoint NameA to InstB's NameB with a joint.
// Status 1/2/3 says which step failed; 0 is success.
unsigned long DACOM_API FHardpointObject::ConnectHardpoints(void* InInstA, FHardpointKey const* NameA,
                                                            void* InInstB, FHardpointKey const* NameB)
{
    FEngineInstance* InstA = (FEngineInstance*)InInstA;
    FEngineInstance* InstB = (FEngineInstance*)InInstB;
    IEngine* Engine = this->Engine;
    void* HandleB = G_InvalidArchetype;
    unsigned long Status = 1;
    unsigned char DataA[0x48];
    unsigned char DataB[0x48];

    void* HandleA = Engine->GetInstanceArchetype(InstA);
    if (HandleA != G_InvalidArchetype && GetHardpoint(HandleA, NameA, DataA) != 0)
    {
        Status = 2;
        HandleB = Engine->GetInstanceArchetype(InstB);
        if (HandleB != G_InvalidArchetype && GetHardpoint(HandleB, NameB, DataB) != 0)
        {
            unsigned long Payload[G_EngineJointPayloadDwords];
            std::memset(Payload, 0, sizeof(Payload));
            Status = 3;
            const unsigned long TypeB = *(unsigned long*)DataB;
            Payload[0] = TypeB;
            float* PayloadF = (float*)Payload;
            const float* PosA = (const float*)(DataA + 4);
            const float* PosB = (const float*)(DataB + 4);
            const float* OrientA = (const float*)(DataA + 0x10);
            const float* OrientB = (const float*)(DataB + 0x10);
            const float* AxisB = (const float*)(DataB + 0x34);

            if (TypeB == 0)
            {
                const float Transposed[9] = {OrientB[0], OrientB[3], OrientB[6],
                                             OrientB[1], OrientB[4], OrientB[7],
                                             OrientB[2], OrientB[5], OrientB[8]};
                float OutMat[9];
                float OutVec[3];
                MathMulMatrix3(Acquire3DMathEngine(), OutMat, OrientA, Transposed);
                MathTransformVec(Acquire3DMathEngine(), OutVec, OutMat, PosB);
                PayloadF[4] = PosA[0] - OutVec[0];
                PayloadF[5] = PosA[1] - OutVec[1];
                PayloadF[6] = PosA[2] - OutVec[2];
                std::memcpy(PayloadF + 7, OutMat, sizeof(OutMat));
            }
            else if (TypeB == 1 || TypeB == 2)
            {
                std::memcpy(PayloadF + 1, PosA, 3 * sizeof(float));
                std::memcpy(PayloadF + 4, PosB, 3 * sizeof(float));
                float OutMat[9];
                float AxisXformed[3];
                MathMulMatrix3(Acquire3DMathEngine(), OutMat, OrientA, OrientB);
                MathTransformVec(Acquire3DMathEngine(), AxisXformed, OutMat, AxisB);
                std::memcpy(PayloadF + 7, OutMat, sizeof(OutMat));
                std::memcpy(PayloadF + 16, AxisXformed, sizeof(AxisXformed));
                std::memcpy(PayloadF + 19, DataB + 0x40, 2 * sizeof(float));
            }

            if (Engine->CreateJoint(InstA, InstB, Payload) != 0)
                Status = 0;
        }
    }

    Engine->ReleaseHandle(HandleB);
    Engine->ReleaseHandle(HandleA);
    return Status;
}

// LAB_06621f90 (IEngine+0xBC).
unsigned long DACOM_API FEngineObject::CreateJoint(void* InParent, void* InChild,
                                                     const unsigned long* Payload)
{
    FEngineInstance* Parent = (FEngineInstance*)InParent;
    FEngineInstance* Child = (FEngineInstance*)InChild;
    if (Parent == G_InvalidInstance || Parent == nullptr)
        return 0;
    if (Child == G_InvalidInstance || Child == nullptr)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x741, "WARNING",
                     FormatFdumpMessage(Message,
                                        "Engine: create_joint: Failing to connect %d <- %d, "
                                        "invalid instances.",
                                        Parent, Child));
        return 0;
    }
    if (Child->Parent != nullptr)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x746, "WARNING",
                     FormatFdumpMessage(Message,
                                        "Engine: create_joint: Failing to connect %d <- %d, "
                                        "child already has a parent.\n",
                                        Parent, Child));
        return 0;
    }
    if ((long)(Child->ChildCount) > 0)
        EraseRootInstance(this, Child);
    Parent->ConnectChild(Child, Payload);
    if (Parent->ChildCount == 1 && Parent->Parent == nullptr)
        InsertRootInstance(this, Parent);
    UpdateInstance(Child, 0, nullptr);
    return 1;
}

// LAB_06622140 (IEngine+0xC0). The instance acted on is Child; Parent only feeds the messages.
unsigned long DACOM_API FEngineObject::DestroyJoint(void* InParent, void* InChild)
{
    FEngineInstance* Parent = (FEngineInstance*)InParent;
    FEngineInstance* Child = (FEngineInstance*)InChild;
    if (Child == G_InvalidInstance || Child == nullptr)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x763, "WARNING",
                     FormatFdumpMessage(Message,
                                        "Engine: destroy_joint: Failing to disconnect %d <- %d, "
                                        "child not found.",
                                        Parent, Child));
        return 0;
    }
    if (Child->Parent == nullptr)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpWarning, G_FdumpFileEngine, 0x769, "WARNING",
                     FormatFdumpMessage(Message,
                                        "Engine: destroy_joint: Failing to disconnect %d <- %d, "
                                        "not connected.",
                                        Parent, Child));
        return 0;
    }
    FEngineInstance* OldParent = Child->Parent;
    OldParent->UnlinkChild(Child);
    if (OldParent->ChildCount == 0 && OldParent->Parent == nullptr)
        EraseRootInstance(this, OldParent);
    if ((long)(Child->ChildCount) > 0)
        InsertRootInstance(this, Child);
    return 1;
}

// LAB_06625e00 (IEC+0xC). QI through the IEC facet to the controlling unknown.
static GENRESULT DACOM_API HardpointEngineComponentInitialize(void* Nested)
{
    FHardpointObject* Object = HardpointFromNested(Nested, 4);
    if (HardpointQueryInterface(Object, Iids::Engine, (void**)(&Object->Engine)) < 0)
    {
        EngBaseFdump(kFdumpError, G_FdumpFileHardPoint, 0xDC, "ERROR",
                     "HardPoint: Initialize: unable to acquire IID_IEngine, cannot continue");
        return GR_GENERIC;
    }
    Object->Engine->Release();
    return GR_OK;
}

// FUN_06626d00 (thiscall, ECX unused). Case-sensitive compares, in this order. No null check.
static long ParseHardpointJointType(const char* Name)
{
    if (std::strcmp(Name, G_NameFixed) == 0)
        return 0;
    if (std::strcmp(Name, G_NameRevolute) == 0)
        return 2;
    if (std::strcmp(Name, G_NamePrismatic) == 0)
        return 1;
    EngBaseFdump(kFdumpWarning, G_FdumpFileHardPoint, 0x25F, "WARNING",
                 "HardPoint: unknown joint type, returning JT_FIXED");
    return 0;
}

// The fixed-size field read LAB_06625e60 inlines four times. Dest is written only on a full read.
static void ReadHardpointField(IFileSystem* Fs, const char* Name, void* Dest, unsigned long Size)
{
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Name, nullptr);
    const int FileHandle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    if (FileHandle == -1)
        return;
    unsigned char Temp[0xC];
    unsigned long BytesRead = 0;
    if (Fs->Read((unsigned long)FileHandle, Temp, Size, &BytesRead, 0) != 0 && // +0x14
        BytesRead == Size)
        std::memcpy(Dest, Temp, Size);
    Fs->CloseFile((unsigned long)FileHandle); // +0x10
}

// FUN_06627b30: the same open as ReadHardpointField, a 0x24-byte read.
static GENRESULT ReadHardpointOrientation(IFileSystem* Fs, const char* Name, float* Out)
{
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Name, nullptr);
    const int FileHandle = Fs->OpenByDesc((FFileSystemDesc*)(&Desc)); // +0x7C
    bool bRead = false;
    if (FileHandle != -1)
    {
        float Temp[9];
        unsigned long BytesRead = 0;
        if (Fs->Read((unsigned long)FileHandle, Temp, sizeof(Temp), &BytesRead, 0) !=
                0 &&
            BytesRead == sizeof(Temp))
        {
            bRead = true;
            std::memcpy(Out, Temp, sizeof(Temp));
        }
        Fs->CloseFile((unsigned long)FileHandle); // +0x10
    }
    return bRead ? GR_OK : GR_E_FAIL;
}

// LAB_06625e60 (IEC+0x10, this = obj+4). No argument checks.
static GENRESULT DACOM_API HardpointCreateArchetype(void* Nested, FArchetypeHandle* Handle,
                                                   void* FileSystem)
{
    FHardpointObject* Object = HardpointFromNested(Nested, 4);
    IFileSystem* Fs = AsFileSystem(FileSystem);
    FHardpointMap::iterator Entry = Object->Archetypes.end();

    if (Fs->OpenNode(G_NameHardpointsFolder) == 0) // +0x60
        return 0;

    WIN32_FIND_DATAA Outer;
    const int OuterFind = Fs->FindFirst(G_NameStar, // +0x48
                                     &Outer);
    if (OuterFind == -1)
        return 0;

    do
    {
        if (Fs->OpenNode(Outer.cFileName) == 0)
        {
            char Message[0x2000];
            EngBaseFdump(kFdumpWarning, G_FdumpFileHardPoint, 0xFF, "WARNING",
                         FormatFdumpMessage(Message,
                                            "Hardpoint: create_archetype: '%s' has old hardpoint "
                                            "layout. If you see this message, mail pbleisch\n",
                                            Outer.cFileName));
            continue;
        }

        WIN32_FIND_DATAA Inner;
        const int InnerFind = Fs->FindFirst("*.*", // +0x48
                                         &Inner);
        if (InnerFind != -1)
        {
            do
            {
                if (Fs->OpenNode(Inner.cFileName) == 0)
                    continue;
                if (Entry == Object->Archetypes.end())
                {
                    Entry = Object->Archetypes
                                .insert(FHardpointMap::value_type(HardpointKeyOf(Handle),
                                                                  FHardpointList()))
                                .first;
                    if (Entry != Object->Archetypes.end())
                    {
                        Object->CachedNode = Entry;
                        Object->bCacheValid = 1;
                    }
                }
                {
                    FHardpointRecord Temp(DACOM_CRC::GetCRC32(Inner.cFileName), Inner.cFileName);
                    Entry->second.push_back(Temp);
                }
                unsigned char* Data = Entry->second.back().Data;
                const long Type = ParseHardpointJointType(Outer.cFileName);
                std::memcpy(Data, &Type, sizeof(Type));
                // Signed compares against 0 and 2.
                if (Type < 0 || Type > 2)
                {
                    EngBaseFdump(kFdumpWarning, G_FdumpFileHardPoint, 0x120, "WARNING",
                                 "Hardpoint: unknown hardpoint type, probably random garbage");
                }
                else
                {
                    if (Type != 0)
                    {
                        ReadHardpointField(Fs, G_NameAxis, Data + 0x34, 0xC);
                        ReadHardpointField(Fs, G_NameMin, Data + 0x40, 4);
                        ReadHardpointField(Fs, G_NameMax, Data + 0x44, 4);
                    }
                    ReadHardpointField(Fs, G_NamePosition, Data + 4, 0xC);
                    ReadHardpointOrientation(Fs, G_NameOrientation,
                                             (float*)(Data + 0x10));
                }
                Fs->OpenNode(G_NameParentDot); // +0x60
            } while (Fs->FindNext((unsigned long)InnerFind, // +0x4C
                                &Inner) != 0);
            Fs->FindClose((unsigned long)InnerFind); // +0x50
        }
        Fs->OpenNode(G_NameParentDot); // +0x60
    } while (Fs->FindNext((unsigned long)OuterFind, // +0x4C
                        &Outer) != 0);
    Fs->FindClose((unsigned long)OuterFind); // +0x50
    Fs->OpenNode(G_NameParentDot);                     // +0x60
    return Entry != Object->Archetypes.end() ? 1 : 0;
}

// LAB_06626b90 (IEC+0x14, this = obj+4).
static GENRESULT DACOM_API HardpointDuplicateArchetype(void* Nested, FArchetypeHandle* NewHandle,
                                                      FArchetypeHandle* OldHandle)
{
    FHardpointObject* Object = HardpointFromNested(Nested, 4);
    FHardpointMap::iterator Source = HardpointLookup(Object, OldHandle);
    if (Source == Object->Archetypes.end())
        return GR_OK;
    FHardpointList Copy;
    for (FHardpointList::iterator It = Source->second.begin(); It != Source->second.end(); ++It)
        Copy.push_back(*It);
    FHardpointMap::iterator Inserted =
        Object->Archetypes.insert(FHardpointMap::value_type(HardpointKeyOf(NewHandle), Copy)).first;
    if (Inserted != Object->Archetypes.end())
    {
        Object->CachedNode = Inserted;
        Object->bCacheValid = 1;
    }
    return GR_OK;
}

// LAB_06626a30 (IEC+0x18, this = obj+4). No argument checks.
static GENRESULT DACOM_API HardpointDestroyArchetype(void* Nested, FArchetypeHandle* Handle)
{
    FHardpointObject* Object = HardpointFromNested(Nested, 4);
    if (HardpointLookup(Object, Handle) == Object->Archetypes.end())
        return GR_OK;
    const long Key = HardpointKeyOf(Handle);
    if (Object->bCacheValid != 0 && Object->CachedNode->first == Key)
        Object->bCacheValid = 0;
    Object->Archetypes.erase(Key);
    return GR_OK;
}

// LAB_06625380.
GENRESULT DACOM_API EngineFactoryCreateInstance(FDacomFactory* This, AGGDESC* Desc, void** Out)
{
    if (Desc->size != G_AggDescSizeEngine || std::strcmp(Desc->interface_name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    FEngineObject* Object = (FEngineObject*)(std::calloc(1, sizeof(FEngineObject)));
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }
    Object->Construct();
    Object->RefCount = 1;
    const GENRESULT Hook = EngBaseAggregationHook(Desc);
    if (Hook != 0)
    {
        Object->Destruct();
        std::free(Object);
        *Out = nullptr;
        return Hook;
    }
    *Out = Object;
    return GR_OK;
}

// FUN_0661a750. No checks of This / Desc / Out.
GENRESULT DACOM_API AnimationFactoryCreateInstance(FDacomFactory* This, AGGDESC* Desc, void** Out)
{
    if (Desc->size != G_AggDescSizeAnimation || std::strcmp(Desc->interface_name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    FAnimationObject* Object = (FAnimationObject*)(std::calloc(1, sizeof(FAnimationObject)));
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }

    new (Object) FAnimationObject(); // the four facet tables and the inner unknown's table
    Object->NextPlaybackId = G_AnimFirstPlaybackId;
    // Retail constructs the three trees in place (FUN_06616c10); see FAnimationObject.
    Object->Playbacks = new FAnimPlaybackMap;
    Object->Channels = new FTypedChannelSet;
    Object->Mixers = new FMixerKeyMap;
    // Inner unknown {table 0662946c (installed by the placement new above), 1, object}.
    Object->Inner.RefCount = 1;
    Object->Inner.Object = Object;

    FAggDesc18* Agg = (FAggDesc18*)Desc;
    IDAComponent* const InnerUnknown = &Object->Inner;
    Object->Controller = (IDAComponent*)(Agg->Inner);
    if (Agg->Inner != nullptr)
        *Agg->Outer = InnerUnknown;
    else
        Object->Controller = InnerUnknown;

    const int HookResult = EngBaseAggregationHook(Desc);
    if (HookResult != 0)
    {
        if (Agg->Outer != nullptr)
            *Agg->Outer = nullptr;
        DestroyAnimationLists(Object);
        std::free(Object);
        *Out = nullptr;
        return (GENRESULT)HookResult;
    }

    *Out = Object;
    return GR_OK;
}

// FUN_06627c40. No checks of This / Desc / Out.
GENRESULT DACOM_API HardpointFactoryCreateInstance(FDacomFactory* This, AGGDESC* Desc, void** Out)
{
    if (Desc->size != G_AggDescSizeHardpoint || std::strcmp(Desc->interface_name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    FHardpointObject* Object =
        (FHardpointObject*)(std::calloc(1, sizeof(FHardpointObject)));
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }

    Object->Construct();
    // Inner unknown {table 06629838 (installed by FHardpointObject::Construct's placement new), 1, object}.
    Object->Inner.RefCount = 1;
    Object->Inner.Object = Object;

    FAggDesc18* Agg = (FAggDesc18*)Desc;
    IDAComponent* const InnerUnknown = &Object->Inner;
    Object->Controller = (IDAComponent*)(Agg->Inner);
    if (Agg->Inner == nullptr)
        Object->Controller = InnerUnknown;
    else
        *Agg->Outer = InnerUnknown;

    const int HookResult = EngBaseAggregationHook(Desc);
    if (HookResult != 0)
    {
        if (Agg->Outer != nullptr)
            *Agg->Outer = nullptr;
        HardpointDestruct(Object);
        std::free(Object);
        *Out = nullptr;
        return (GENRESULT)HookResult;
    }

    *Out = Object;
    return (GENRESULT)HookResult;
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

static void* NestedThis(void* Object, unsigned long Offset)
{
    return (unsigned char*)Object + Offset;
}

unsigned long DACOM_API FEngineObject::EnumerateEngineComponents(
    int(__cdecl* Callback)(const char*, void*))
{
    return EngineEnumerateEngineComponents(this, Callback);
}
void* DACOM_API FEngineObject::CreateArchetypeHandle(FEngineName* Name, void* FileSystem)
{
    return EngineCreateArchetypeHandle(this, Name, FileSystem);
}
unsigned long DACOM_API FEngineObject::AddRefHandle(void* Handle)
{
    return EngineAddRefHandle(this, (FArchetypeHandle*)Handle);
}
unsigned long DACOM_API FEngineObject::ReleaseHandle(void* Handle)
{
    return EngineReleaseHandle(this, (FArchetypeHandle*)Handle);
}
FEngineName* DACOM_API FEngineObject::GetArchetypeName(FEngineName* Out, void* Handle)
{
    return EngineGetArchetypeName(this, Out, (FArchetypeHandle*)Handle);
}
GENRESULT DACOM_API FEngineObject::EnumerateArchetypeConstraints(
    void* Handle, int(__cdecl* Callback)(void*, void*, void*, void*), void* Context)
{
    return EngineEnumerateArchetypeConstraints(this, (FArchetypeHandle*)Handle,
                                               Callback, Context);
}
GENRESULT DACOM_API FEngineObject::EnumerateArchetypeChildren(
    void* Handle, int(__cdecl* Callback)(void*, void*, int, void*), void* Context)
{
    return EngineEnumerateArchetypeChildren(this, (FArchetypeHandle*)Handle, Callback,
                                               Context);
}
void* DACOM_API FEngineObject::CreateInstance(void* Handle, void* Transform)
{
    return EngineCreateInstance(this, (FArchetypeHandle*)Handle, Transform);
}
FEngineName* DACOM_API FEngineObject::GetInstanceName(FEngineName* Out, void* Instance)
{
    return EngineGetInstanceName(this, Out, (FEngineInstance*)Instance);
}
GENRESULT DACOM_API FEngineObject::NoOp41(void* A, void* B)
{
    return StubRetC(this, A, B);
}
float* DACOM_API FEngineObject::GetIdentityMatrix(void* Unused)
{
    return EngineGetIdentityMatrix(this, Unused);
}
GENRESULT DACOM_API FEngineObject::NoOp43(void* A, void* B)
{
    return StubRetC(this, A, B);
}
float* DACOM_API FEngineObject::GetIdentityMatrix2(void* Unused)
{
    return EngineGetIdentityMatrix(this, Unused);
}
GENRESULT DACOM_API FAnimationObject::QueryInterface(const char* Iid, void** Out)
{
    return AnimationQueryInterface(this, Iid, Out);
}
unsigned long DACOM_API FAnimationObject::MixerWalkAll(float Time)
{
    return AnimationMixerWalkAll(this, Time);
}
GENRESULT DACOM_API FHardpointObject::QueryInterface(const char* Iid, void** Out)
{
    return HardpointQueryInterface(this, Iid, Out);
}
unsigned long DACOM_API FHardpointObject::AddRef()
{
    return HardpointAddRef(this);
}
unsigned long DACOM_API FHardpointObject::Release()
{
    return HardpointRelease(this);
}
int DACOM_API FHardpointObject::Initialize()
{
    return (int)(HardpointEngineComponentInitialize(NestedThis(this, 4)));
}
int DACOM_API FHardpointObject::CreateArchetype(void* Archetype, void* FileSystem)
{
    return (int)(
        HardpointCreateArchetype(NestedThis(this, 4), (FArchetypeHandle*)Archetype, FileSystem));
}
int DACOM_API FHardpointObject::DuplicateArchetype(void* NewArchetype, void* OldArchetype)
{
    return (int)(
        HardpointDuplicateArchetype(NestedThis(this, 4), (FArchetypeHandle*)NewArchetype,
                                        (FArchetypeHandle*)OldArchetype));
}
int DACOM_API FHardpointObject::DestroyArchetype(void* Archetype)
{
    return (int)(HardpointDestroyArchetype(NestedThis(this, 4),
                                                       (FArchetypeHandle*)Archetype));
}
int DACOM_API FHardpointObject::QueryArchetype(void* Archetype, const char* Iid, void** Out)
{
    return (int)(RejectHardpointQuery(NestedThis(this, 4), Archetype, Iid, Out));
}
int DACOM_API FHardpointObject::CreateInstance(void* Instance, void* Archetype)
{
    return (int)(NoOpHardpointInstance(NestedThis(this, 4), Instance, Archetype));
}
int DACOM_API FHardpointObject::DestroyInstance(void* Instance)
{
    return (int)(StubRet8(NestedThis(this, 4), Instance));
}
int DACOM_API FHardpointObject::UpdateInstance(void* Instance, void* Arg)
{
    return (int)(StubRetC(NestedThis(this, 4), Instance, Arg));
}
int DACOM_API FHardpointObject::QueryInstance(void* Instance, const char* Iid, void** Out)
{
    return (int)(RejectHardpointQuery(NestedThis(this, 4), Instance, Iid, Out));
}
int DACOM_API FHardpointObject::Notify(void* Arg)
{
    return (int)(StubRet8(NestedThis(this, 4), Arg));
}

unsigned long DACOM_API FChannelArchetypeObject::AddRef()
{
    return ChannelArchetypeAddRef(this);
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
