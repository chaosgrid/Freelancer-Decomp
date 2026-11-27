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

static long ParseHardpointJointType(const char* Name);
static GENRESULT ReadHardpointOrientation(IFileSystem* Fs, const char* Name, float* Out);

static GENRESULT DACOM_API HardpointQueryInterface(FHardpointObject* This, const char* Iid,
                                                  void** Out);

static_assert(sizeof(FHardpointRecord) == 0x50, "hardpoint list value 0x50 (node 0x58)");

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

// .data:0662aac0 (from the PE bytes): the __FILE__ of every FDUMP site in HardPoint.cpp
// (06625d4d..06626dd8).
static const char* const G_FdumpFileHardPoint =
    "C:\\work\\builds\\dalibs\\dalibs-build\\build\\Src\\EngBase\\Hardpoint\\HardPoint.cpp";

// FUN_06625d00 (dtor, thiscall). Reports each archetype still in the map, in ascending key order.
void FHardpointObject::Destroy()
{
    for (FHardpointMap::iterator It = Archetypes.begin(); It != Archetypes.end();
         ++It)
    {
        char Message[0x2000];
        EngBaseFdump(kFdumpNotice, G_FdumpFileHardPoint, 0xC8, "NOTICE",
                     FormatFdumpMessage(Message,
                                        "Hardpoint: dtor: Archetype %d has dangling hardpoints\n",
                                        It->first));
    }
    Engine = nullptr;
    this->~FHardpointObject();
}

static FHardpointObject* HardpointFromNested(void* Nested, unsigned long Offset)
{
    return reinterpret_cast<FHardpointObject*>((unsigned char*)Nested - Offset);
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

// The fixed-size field read LAB_06625e60 inlines four times. Dest is written only on a full read.
static void ReadHardpointField(IFileSystem* Fs, const char* Name, void* Dest, unsigned long Size)
{
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Name, nullptr);
    const int FileHandle = Fs->OpenByDesc(reinterpret_cast<FFileSystemDesc*>(&Desc)); // +0x7C
    if (FileHandle == -1)
        return;
    unsigned char Temp[0xC];
    unsigned long BytesRead = 0;
    if (Fs->Read((unsigned long)FileHandle, Temp, Size, &BytesRead, 0) != 0 && // +0x14
        BytesRead == Size)
        std::memcpy(Dest, Temp, Size);
    Fs->CloseFile((unsigned long)FileHandle); // +0x10
}

// The map key: retail stores the archetype handle as a signed int (FUN_06612e70).
static long HardpointKeyOf(FArchetypeHandle* Handle)
{
    return (long)((intptr_t)Handle);
}

// LAB_06625e60 (IEC+0x10, this = obj+4). No argument checks.
static GENRESULT DACOM_API HardpointCreateArchetype(void* Nested, FArchetypeHandle* Handle,
                                                   IFileSystem* FileSystem)
{
    FHardpointObject* Object = HardpointFromNested(Nested, 4);
    FHardpointMap::iterator Entry = Object->Archetypes.end();

    if (FileSystem->OpenNode(G_NameHardpointsFolder) == 0) // +0x60
        return 0;

    WIN32_FIND_DATAA Outer;
    const int OuterFind = FileSystem->FindFirst(G_NameStar, // +0x48
                                     &Outer);
    if (OuterFind == -1)
        return 0;

    do
    {
        if (FileSystem->OpenNode(Outer.cFileName) == 0)
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
        const int InnerFind = FileSystem->FindFirst("*.*", // +0x48
                                         &Inner);
        if (InnerFind != -1)
        {
            do
            {
                if (FileSystem->OpenNode(Inner.cFileName) == 0)
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
                        ReadHardpointField(FileSystem, G_NameAxis, Data + 0x34, 0xC);
                        ReadHardpointField(FileSystem, G_NameMin, Data + 0x40, 4);
                        ReadHardpointField(FileSystem, G_NameMax, Data + 0x44, 4);
                    }
                    ReadHardpointField(FileSystem, G_NamePosition, Data + 4, 0xC);
                    ReadHardpointOrientation(FileSystem, G_NameOrientation,
                                             (float*)(Data + 0x10));
                }
                FileSystem->OpenNode(G_NameParentDot); // +0x60
            } while (FileSystem->FindNext((unsigned long)InnerFind, // +0x4C
                                &Inner) != 0);
            FileSystem->FindClose((unsigned long)InnerFind); // +0x50
        }
        FileSystem->OpenNode(G_NameParentDot); // +0x60
    } while (FileSystem->FindNext((unsigned long)OuterFind, // +0x4C
                        &Outer) != 0);
    FileSystem->FindClose((unsigned long)OuterFind); // +0x50
    FileSystem->OpenNode(G_NameParentDot);                     // +0x60
    return Entry != Object->Archetypes.end() ? 1 : 0;
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

static GENRESULT DACOM_API HardpointEngineComponentQueryInterface(void* Nested, const char* Iid,
                                                                 void** Out)
{
    return HardpointQueryInterface(HardpointFromNested(Nested, 4), Iid, Out);
}

static unsigned long DACOM_API HardpointAddRef(FHardpointObject* This)
{
    return This->Controller->AddRef();
}

static unsigned long DACOM_API HardpointEngineComponentAddRef(void* Nested)
{
    return HardpointAddRef(HardpointFromNested(Nested, 4));
}

static unsigned long DACOM_API HardpointRelease(FHardpointObject* This)
{
    return This->Controller->Release();
}

static unsigned long DACOM_API HardpointEngineComponentRelease(void* Nested)
{
    return HardpointRelease(HardpointFromNested(Nested, 4));
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
unsigned long DACOM_API FHardpointObject::GetHardpoint(ArchetypeIndex InHandle, FHardpointKey const* Key,
                                                       void* Out)
{
    FArchetypeHandle* Handle = reinterpret_cast<FArchetypeHandle*>(InHandle);
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
unsigned long DACOM_API FHardpointObject::AddHardpoint(ArchetypeIndex InHandle, FHardpointKey const* Key,
                                                       const void* Data)
{
    FArchetypeHandle* Handle = reinterpret_cast<FArchetypeHandle*>(InHandle);
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
                                                          ArchetypeIndex InHandle, void* Context)
{
    FArchetypeHandle* Handle = reinterpret_cast<FArchetypeHandle*>(InHandle);
    FHardpointMap::iterator Entry = HardpointLookup(this, Handle);
    if (Entry == Archetypes.end())
        return GR_OK;
    FHardpointList& List = Entry->second;
    for (FHardpointList::iterator It = List.begin(); It != List.end(); ++It)
        Callback(It->Name, Context);
    return GR_OK;
}

// LAB_06626690 (IHardpoint+0x18): connect InstA's hardpoint NameA to InstB's NameB with a joint.
// Status 1/2/3 says which step failed; 0 is success.
unsigned long DACOM_API FHardpointObject::ConnectHardpoints(InstanceIndex InInstA, FHardpointKey const* NameA,
                                                            InstanceIndex InInstB, FHardpointKey const* NameB)
{
    FEngineInstance* InstA = reinterpret_cast<FEngineInstance*>(InInstA);
    FEngineInstance* InstB = reinterpret_cast<FEngineInstance*>(InInstB);
    IEngine* Engine = this->Engine;
    ArchetypeIndex HandleB = (ArchetypeIndex)G_InvalidArchetype;
    unsigned long Status = 1;
    unsigned char DataA[0x48];
    unsigned char DataB[0x48];

    ArchetypeIndex HandleA = Engine->GetInstanceArchetype((InstanceIndex)InstA);
    if (HandleA != (ArchetypeIndex)G_InvalidArchetype && GetHardpoint(HandleA, NameA, DataA) != 0)
    {
        Status = 2;
        HandleB = Engine->GetInstanceArchetype((InstanceIndex)InstB);
        if (HandleB != (ArchetypeIndex)G_InvalidArchetype && GetHardpoint(HandleB, NameB, DataB) != 0)
        {
            unsigned long Payload[G_EngineJointPayloadDwords];
            std::memset(Payload, 0, sizeof(Payload));
            Status = 3;
            const unsigned long TypeB = *(unsigned long*)DataB;
            Payload[0] = TypeB;
            float* PayloadF = (float*)Payload;
            const Vector* PosA = reinterpret_cast<const Vector*>(DataA + 4);
            const Vector* PosB = reinterpret_cast<const Vector*>(DataB + 4);
            const Matrix* OrientA = reinterpret_cast<const Matrix*>(DataA + 0x10);
            const Matrix* OrientB = reinterpret_cast<const Matrix*>(DataB + 0x10);
            const Vector* AxisB = reinterpret_cast<const Vector*>(DataB + 0x34);

            if (TypeB == kJointTypeFix)
            {
                const Matrix Transposed(OrientB->E[0][0], OrientB->E[1][0], OrientB->E[2][0],
                                        OrientB->E[0][1], OrientB->E[1][1], OrientB->E[2][1],
                                        OrientB->E[0][2], OrientB->E[1][2], OrientB->E[2][2]);
                Matrix OutMat;
                Vector OutVec;
                MathMulMatrix3(Acquire3DMathEngine(), &OutMat, OrientA, &Transposed);
                MathTransformVec(Acquire3DMathEngine(), &OutVec, &OutMat, PosB);
                PayloadF[4] = PosA->x - OutVec.x;
                PayloadF[5] = PosA->y - OutVec.y;
                PayloadF[6] = PosA->z - OutVec.z;
                std::memcpy(PayloadF + 7, &OutMat, sizeof(OutMat));
            }
            else if (TypeB == kJointTypePris || TypeB == kJointTypeRev)
            {
                std::memcpy(PayloadF + 1, PosA, 3 * sizeof(float));
                std::memcpy(PayloadF + 4, PosB, 3 * sizeof(float));
                Matrix OutMat;
                Vector AxisXformed;
                MathMulMatrix3(Acquire3DMathEngine(), &OutMat, OrientA, OrientB);
                MathTransformVec(Acquire3DMathEngine(), &AxisXformed, &OutMat, AxisB);
                std::memcpy(PayloadF + 7, &OutMat, sizeof(OutMat));
                std::memcpy(PayloadF + 16, &AxisXformed, sizeof(AxisXformed));
                std::memcpy(PayloadF + 19, DataB + 0x40, 2 * sizeof(float));
            }

            if (Engine->CreateJoint((InstanceIndex)InstA, (InstanceIndex)InstB, Payload) != 0)
                Status = 0;
        }
    }

    Engine->ReleaseHandle(HandleB);
    Engine->ReleaseHandle(HandleA);
    return Status;
}

// FUN_066269b0 (IHardpoint+0x1C). No argument checks.
char const* DACOM_API FHardpointObject::FindHardpoint(ArchetypeIndex InHandle, unsigned long const* Crc)
{
    FArchetypeHandle* Handle = reinterpret_cast<FArchetypeHandle*>(InHandle);
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

// FUN_06627b30: the same open as ReadHardpointField, a 0x24-byte read.
static GENRESULT ReadHardpointOrientation(IFileSystem* Fs, const char* Name, float* Out)
{
    FEngBaseFileSystemDesc Desc;
    FillFileSystemDesc(&Desc, Name, nullptr);
    const int FileHandle = Fs->OpenByDesc(reinterpret_cast<FFileSystemDesc*>(&Desc)); // +0x7C
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

// FUN_06627c40. No checks of This / Desc / Out.
GENRESULT DACOM_API HardpointFactoryCreateInstance(FDacomFactory* This, AGGDESC* Desc, void** Out)
{
    if (Desc->size != G_AggDescSizeHardpoint || std::strcmp(Desc->interface_name, This->Name) != 0)
    {
        *Out = nullptr;
        return GR_INTERFACE_UNSUPPORTED;
    }

    FHardpointObject* Object =
        static_cast<FHardpointObject*>(std::calloc(1, sizeof(FHardpointObject)));
    if (Object == nullptr)
    {
        *Out = nullptr;
        return GR_OUT_OF_MEMORY;
    }

    Object->Construct();
    // Inner unknown {table 06629838 (installed by FHardpointObject::Construct's placement new), 1, object}.
    Object->Inner.RefCount = 1;
    Object->Inner.Object = Object;

    FAggDesc18* Agg = reinterpret_cast<FAggDesc18*>(Desc);
    IDAComponent* const InnerUnknown = &Object->Inner;
    Object->Controller = Agg->Inner;
    if (Agg->Inner == nullptr)
        Object->Controller = InnerUnknown;
    else
        *Agg->Outer = InnerUnknown;

    const int HookResult = EngBaseAggregationHook(Desc);
    if (HookResult != 0)
    {
        if (Agg->Outer != nullptr)
            *Agg->Outer = nullptr;
        Object->Destroy();
        std::free(Object);
        *Out = nullptr;
        return (GENRESULT)HookResult;
    }

    *Out = Object;
    return (GENRESULT)HookResult;
}

// Inner QI table @ 06629844 (PE bytes): {IHardpoint, 0}, {IEngineComponent, 4},
// {IAggregateComponent, 4}, then a null iid. IAggregateComponent answers with the IEC facet.
static const FInnerIidEntry G_HardpointInnerIids[] = {
    {Iids::Hardpoint, 0},
    {Iids::EngineComponent, 0x04},
    {Iids::AggregateComponent, 0x04},
    {nullptr, 0},
};

// LAB_06627e30 (inner QI, RET 0xC) over 06629844.
GENRESULT DACOM_API FHardpointInner::QueryInterface(const char* Iid, void** Out)
{
    return InnerQueryTable(Object, Iid, Out, G_HardpointInnerIids);
}

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
        Owner->Destroy();
        std::free(Owner);
    }
    return 0;
}

// FUN_06627f40 / 06627f50 / 06627f60: forward to the controlling unknown at +0x34. No checks.
static GENRESULT DACOM_API HardpointQueryInterface(FHardpointObject* This, const char* Iid,
                                                  void** Out)
{
    return This->Controller->QueryInterface(Iid, Out);
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

static void* NestedThis(void* Object, unsigned long Offset)
{
    return (unsigned char*)Object + Offset;
}

int DACOM_API FHardpointObject::Initialize()
{
    return (int)(HardpointEngineComponentInitialize(NestedThis(this, 4)));
}

int DACOM_API FHardpointObject::CreateArchetype(ArchetypeIndex Archetype, IFileSystem* FileSystem)
{
    return (int)(
        HardpointCreateArchetype(NestedThis(this, 4), reinterpret_cast<FArchetypeHandle*>(Archetype), FileSystem));
}

int DACOM_API FHardpointObject::DuplicateArchetype(ArchetypeIndex NewArchetype, ArchetypeIndex OldArchetype)
{
    return (int)(
        HardpointDuplicateArchetype(NestedThis(this, 4), reinterpret_cast<FArchetypeHandle*>(NewArchetype),
                                        reinterpret_cast<FArchetypeHandle*>(OldArchetype)));
}

int DACOM_API FHardpointObject::DestroyArchetype(ArchetypeIndex Archetype)
{
    return (int)(HardpointDestroyArchetype(NestedThis(this, 4),
                                                       reinterpret_cast<FArchetypeHandle*>(Archetype)));
}

int DACOM_API FHardpointObject::QueryArchetype(ArchetypeIndex Archetype, const char* Iid, void** Out)
{
    return (int)(RejectHardpointQuery(NestedThis(this, 4), (void*)Archetype, Iid, Out));
}

int DACOM_API FHardpointObject::CreateInstance(InstanceIndex Instance, ArchetypeIndex Archetype)
{
    return (int)(NoOpHardpointInstance(NestedThis(this, 4), (void*)Instance, (void*)Archetype));
}

int DACOM_API FHardpointObject::DestroyInstance(InstanceIndex Instance)
{
    return (int)(StubRet8(NestedThis(this, 4), (void*)Instance));
}

int DACOM_API FHardpointObject::UpdateInstance(InstanceIndex Instance, void* Arg)
{
    return (int)(StubRetC(NestedThis(this, 4), (void*)Instance, Arg));
}

int DACOM_API FHardpointObject::QueryInstance(InstanceIndex Instance, const char* Iid, void** Out)
{
    return (int)(RejectHardpointQuery(NestedThis(this, 4), (void*)Instance, Iid, Out));
}

int DACOM_API FHardpointObject::Notify(void* Arg)
{
    return (int)(StubRet8(NestedThis(this, 4), Arg));
}
