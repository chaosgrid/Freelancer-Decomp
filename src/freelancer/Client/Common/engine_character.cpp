// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Common/engine_character.h"

#include "GameFlow/character_info.h" // GetCharacterInfoDb (FUN_004356c0), AcquireScriptByName (FUN_00434e90)
#include "common_goods.h"
#include "common_ini.h"
#include "Common/misc_util.h"

#include "DALib.h"
#include "dacom.h"
#include "engine/engbase.h"
#include "readfile.h" // IFileSystem : IComponentFactory (DALib::Data into FDeformLodDesc)

#include <stdint.h>
#include <string.h>

static_assert(sizeof(FEngineCharacterPartPath) == 0x94, "EngineCharacter part path is 0x94");
static_assert(sizeof(FDeformPartDesc) == 0x18, "EngineCharacter part entry is 0x18");
static_assert(sizeof(FDeformLodDesc) == 0xc, "EngineCharacter part source is 0xc");
static_assert(offsetof(FEngineCharacterBodyDesc, Entry) == 0x68, "BodyDesc entries at +0x68");
static_assert(offsetof(FEngineCharacterBodyDesc, Part) == 0x128, "BodyDesc paths at +0x128");
static_assert(sizeof(FEngineCharacterBodyDesc) == 0x5c8, "BodyDesc is 0x5c8");
static_assert(sizeof(FEngineCharacterBody) == 0x298, "EngineCharacter body is 0x298");
static_assert(offsetof(FEngineCharacterBody, AnimNames) == 0x44, "body anim names at +0x44");
static_assert(offsetof(FEngineCharacterBody, AnimIds) == 0x254, "body anim ids at +0x254");
static_assert(offsetof(FEngineCharacterBody, RootTransform) == 0x268, "body transform at +0x268");
static_assert(offsetof(FEngineCharacterAccessoryLink, AccessoryHardpoint) == 0x84, "link +0x84");
static_assert(offsetof(FEngineCharacterAccessoryLink, CharacterHardpoint) == 0xc8, "link +0xc8");
static_assert(offsetof(FEngineCharacterAccessoryLink, MaterialPaths) == 0x10c, "link +0x10c");

namespace
{
    char const kEngineCharacterH[] = "cmn\\EngineCharacter.h";

    // The MSVCP60 empty string every "c_str or empty" read falls back to.
    char const kEmpty[] = "";

    // std::string::c_str() of a VC6 string reads its +4 pointer and falls back to the shared
    // empty string when it is null.
    char const* CStrOrEmpty(std::string const& Text)
    {
        return Text.c_str();
    }

    void EmptyName(TString<0x80>& Name)
    {
        Name.Length = 0;
        Name.Data[0] = '\0';
    }

    // IDeformable::Play takes every trailing argument as a dword; a float goes through as its
    // bit pattern.
    void* DwordArg(float Value)
    {
        uintptr_t Bits = 0;
        memcpy(&Bits, &Value, sizeof(Value));
        return reinterpret_cast<void*>(Bits);
    }

    void* DwordArg(unsigned Value)
    {
        return reinterpret_cast<void*>(static_cast<uintptr_t>(Value));
    }
}

// ============================================================================================
// FEngineCharacterBodyDesc
// ============================================================================================

FEngineCharacterBodyDesc::FEngineCharacterBodyDesc(Costume const& Source)
{
    // FUN_004c9f00.
    for (int Index = 0; Index < 8; ++Index)
    {
        Entry[Index].LodCount = 0;
        Entry[Index].Lods = nullptr;
        Entry[Index].Field08 = 0;
        Entry[Index].Path = nullptr;
        Entry[Index].Flags = 0;
        Entry[Index].LodLimit = 0xFFFFFFFFu;
    }
    for (int Index = 0; Index < 8; ++Index)
    {
        Part[Index].Path.Length = 0;
        Part[Index].Path.Data[0] = '\0';
    }
    Count = 0;
    Entries = &Entry[0];

    // PartIds[1] as type 0, in front of whatever is there.
    unsigned long const Id1 = Source.PartIds[1];
    CostumeDescriptions const* Descs = GetCostumeDescriptions();
    if (Id1 != 0)
    {
        char const* Path = nullptr;
        bodypart const* const Found = Descs->find_bodypart(0, Id1);
        if (Found != nullptr)
            Path = CStrOrEmpty(Found->Mesh);
        int const Old = Count;
        if (Old < 8)
        {
            Count = Old + 1;
            int Slot = Old;
            if (Old != 0)
            {
                ShiftPartsUp();
                Slot = 0;
            }
            SetPart(Slot, 0, Path);
        }
    }

    // PartIds[0] as type 1, appended.
    unsigned long const Id0 = Source.PartIds[0];
    Descs = GetCostumeDescriptions();
    if (Id0 != 0)
    {
        char const* Path = nullptr;
        bodypart const* const Found = Descs->find_bodypart(1, Id0);
        if (Found != nullptr)
            Path = CStrOrEmpty(Found->Mesh);
        int const Old = Count;
        if (Old < 8)
        {
            Count = Old + 1;
            SetPart(Old, 1, Path);
        }
    }

    // PartIds[2] as type 2 through AddPart.
    unsigned long const Id2 = Source.PartIds[2];
    Descs = GetCostumeDescriptions();
    if (Id2 != 0)
    {
        char const* Path = nullptr;
        bodypart const* const Found = Descs->find_bodypart(2, Id2);
        if (Found != nullptr)
            Path = CStrOrEmpty(Found->Mesh);
        AddPart(2, Path);
    }

    // PartIds[3] as type 3, appended.
    unsigned long const Id3 = Source.PartIds[3];
    Descs = GetCostumeDescriptions();
    if (Id3 != 0)
    {
        char const* Path = nullptr;
        bodypart const* const Found = Descs->find_bodypart(3, Id3);
        if (Found != nullptr)
            Path = CStrOrEmpty(Found->Mesh);
        int const Old = Count;
        if (Old < 8)
        {
            Count = Old + 1;
            SetPart(Old, 3, Path);
        }
    }
}

void FEngineCharacterBodyDesc::AddPart(int TypeId, char const* Path)
{
    // FUN_004ca070.
    int Slot = Count;
    if (Slot >= 8)
        return;
    Count = Slot + 1;
    if (TypeId == 0 && Slot != 0)
    {
        // FUN_004ca2f0's body, inlined in retail.
        ShiftPartsUp();
        Slot = 0;
    }
    // FUN_004ca220's body, inlined in retail.
    SetPart(Slot, TypeId, Path);
}

void FEngineCharacterBodyDesc::SetPart(int Index, int TypeId, char const* Path)
{
    // FUN_004ca220.
    FEngineCharacterPartPath& Record = Part[Index];
    Record.Path = Path; // null -> "", else <= 0x7f chars
    Record.Source.FileSystem = DALib::Data;
    Record.Source.Name = Record.Path.Data;
    FDeformPartDesc& Slot = Entry[Index];
    Slot.Lods = &Record.Source;
    Slot.Field08 = 0;
    Slot.Path = nullptr;
    Slot.LodCount = 1;
    Slot.Flags = 1;
    Record.TypeId = TypeId;
}

void FEngineCharacterBodyDesc::ShiftPartsUp()
{
    // FUN_004ca2f0: from slot 7 down to slot 1, each takes slot - 1.
    for (int Index = 7; Index >= 1; --Index)
    {
        FEngineCharacterPartPath& To = Part[Index];
        FEngineCharacterPartPath const& From = Part[Index - 1];
        To.Path = From.Path.Data;                    // strlen, <= 0x7f
        To.TypeId = From.TypeId;                     // +0x90
        To.Source = From.Source;                     // +0x84..+0x8c
        Entry[Index] = Entry[Index - 1];             // six dwords
        Entry[Index].Lods = &To.Source;
        To.Source.Name = To.Path.Data;
    }
}

void* FEngineCharacterBodyDesc::CreateDeformable()
{
    // FUN_004ca3c0.
    void* Instance = nullptr;
    if (Count == 0)
        return nullptr;

    Archetype::FCliMaterialEntry* Materials[8];
    for (int Index = 0; Index < Count; ++Index)
    {
        char const* const PathText = Entries[Index].Lods->Name;
        Materials[Index] = Archetype::ResolveMaterialEntry(PathText, true, 0xffffffffu);
    }

    if (DALib::Deform->CreateAlias(&Instance, reinterpret_cast<int*>(this), nullptr, nullptr,
                                   nullptr) != 0)
    {
        FDUMP(kFdumpWarning,
              "%s(%d) : *** WARNING: BodyDesc - failed to create deformable with %d parts\n",
              kEngineCharacterH, 0xd5, Count);
        Instance = nullptr;
    }

    for (int Index = 0; Index < Count; ++Index)
        Archetype::ReleaseMaterialEntrySlot(&Materials[Index]); // FUN_0041b030
    return Instance;
}

// ============================================================================================
// FEngineCharacterAccessoryLink
// ============================================================================================

FEngineCharacterAccessoryLink::FEngineCharacterAccessoryLink()
{
    // FUN_004c9de0.
    Mesh.Length = 0;
    Mesh.Data[0] = '\0';
    AccessoryHardpoint.Length = 0;
    AccessoryHardpoint.Data[0] = '\0';
    CharacterHardpoint.Length = 0;
    CharacterHardpoint.Data[0] = '\0';
    MaterialPaths.Cookie = 0;
    MaterialPaths.Begin = nullptr;
    MaterialPaths.End = nullptr;
    MaterialPaths.Capacity = nullptr;
}

FEngineCharacterAccessoryLink::~FEngineCharacterAccessoryLink()
{
    // FUN_004de730 / FUN_004ca610: free the buffer, zero the three pointers.
    delete[] MaterialPaths.Begin;
    MaterialPaths.Begin = nullptr;
    MaterialPaths.End = nullptr;
    MaterialPaths.Capacity = nullptr;
}

void* FEngineCharacterAccessoryLink::Connect(long Bone)
{
    // FUN_004c9e20.
    void* const Instance = MiscUtilLoadEngineInstance(Mesh.Data, DALib::Data, nullptr, nullptr,
                                                      &MaterialPaths, nullptr, nullptr);
    long Hardpoint = -1;
    bool bFailed = true;
    if (FindHardpoint(Bone, CharacterHardpoint.Data, Hardpoint))
    {
        FHardpointKey AccessoryKey;
        AccessoryKey.Name = AccessoryHardpoint.Data;
        AccessoryKey.Crc = DACOM_CRC::GetCRC32(AccessoryHardpoint.Data);
        FHardpointKey CharacterKey;
        CharacterKey.Name = CharacterHardpoint.Data;
        CharacterKey.Crc = DACOM_CRC::GetCRC32(CharacterHardpoint.Data);
        bFailed = DALib::Hardpoint->ConnectHardpoints(reinterpret_cast<void*>(Hardpoint),
                                                     &CharacterKey, Instance,
                                                     &AccessoryKey) != 0;
    }
    if (bFailed)
    {
        FDUMP(kFdumpWarning,
              "%s(%d) : *** WARNING: Accessory - connection failed! (%s)  acc_hp='%s'  "
              "obj_hp='%s'\n",
              kEngineCharacterH, 0x4e, Mesh.Data, AccessoryHardpoint.Data,
              CharacterHardpoint.Data);
    }
    return Instance;
}

// ============================================================================================
// FEngineCharacterBody
// ============================================================================================

FEngineCharacterBody::FEngineCharacterBody()
{
    // The holders are built by their member initialisers (FUN_00420d40: instance -1, both
    // material slots null). The names are emptied twice, as in retail.
    for (int Index = 0; Index < 4; ++Index)
        EmptyName(AnimNames[Index]);
    // FUN_004215b0 on +0x268, then +0x28c..+0x294 zeroed.
    memset(&RootTransform, 0, sizeof(RootTransform));
    RootTransform.Rotation.D[0][0] = 1.0f;
    RootTransform.Rotation.D[1][1] = 1.0f;
    RootTransform.Rotation.D[2][2] = 1.0f;
    Deformable = nullptr;
    for (int Index = 0; Index < 4; ++Index)
    {
        EmptyName(AnimNames[Index]);
        AnimIds[Index] = -1;
    }
    AnimRate = 1.0f;
}

FEngineCharacterBody::~FEngineCharacterBody()
{
    // FUN_004ca530.
    Reset();
}

void FEngineCharacterBody::StopAnimations()
{
    // FUN_004c9a30.
    if (Deformable == nullptr)
        return;
    DALib::Deform->DestroyAll(Deformable);
    for (int Index = 0; Index < 4; ++Index)
    {
        if (AnimIds[Index] != -1)
        {
            EmptyName(AnimNames[Index]);
            AnimIds[Index] = -1;
        }
    }
}

void FEngineCharacterBody::Reset()
{
    // FUN_004ca4b0.
    StopAnimations();
    for (int Index = 0; Index < 4; ++Index)
        ClientMeshHolderRelease(&Accessories[Index]); // FUN_00420f70
    if (Deformable != nullptr)
    {
        DALib::Deform->Destroy(Deformable);
        Deformable = nullptr;
    }
}

long FEngineCharacterBody::GetRootBone() const
{
    // FUN_004ca480.
    int Bone = -1;
    if (Deformable != nullptr)
        DALib::Deform->GetFirstBone(Deformable, &Bone);
    return Bone;
}

FTransform* FEngineCharacterBody::GetRootTransform(FTransform* Out) const
{
    // FUN_004cba10. The bone lookup is inlined (the same body as FUN_004ca480).
    int Bone = -1;
    if (Deformable != nullptr)
        DALib::Deform->GetFirstBone(Deformable, &Bone);
    float const* const Source = DALib::Engine->GetInstanceTransform(reinterpret_cast<void*>(Bone));
    memcpy(Out, Source, sizeof(FTransform));
    return Out;
}

void FEngineCharacterBody::Advance(float Dt)
{
    // FUN_004cba60.
    if (Deformable == nullptr)
        return;
    float const Scaled = Dt * AnimRate;

    int Bone = -1;
    DALib::Deform->GetFirstBone(Deformable, &Bone);
    DALib::Anim->MixerWalk(static_cast<unsigned long>(Bone), Scaled);

    // The bone read again (the same test as FUN_004ca480), then UpdateInstance with the scaled
    // time as its third argument.
    int Root = -1;
    if (Deformable != nullptr)
        DALib::Deform->GetFirstBone(Deformable, &Root);
    void* TimeArg = nullptr;
    memcpy(&TimeArg, &Scaled, sizeof(Scaled));
    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Root), 0, TimeArg);

    // No playback left -> every slot emptied (no id test here, unlike StopAnimations).
    int Running = 0;
    DALib::Deform->GetListCount(Deformable, &Running);
    if (Running == 0)
    {
        for (int Index = 0; Index < 4; ++Index)
        {
            EmptyName(AnimNames[Index]);
            AnimIds[Index] = -1;
        }
    }
}

bool FEngineCharacterBody::Build(Costume const& Key)
{
    // FUN_0059b220 (thiscall on UIWinDeform +0x8c, RET 4) -- UIWin.cpp's own copy of the body
    // build FindOrCreate (FUN_004c9a80) inlines.
    Reset();

    // The part description (FUN_0059b6b0 per bodypart, in FUN_004c9f00's order) and the
    // deformable (FUN_004ca3c0's body).
    {
        FEngineCharacterBodyDesc Desc(Key);
        Deformable = Desc.CreateDeformable();
    }

    // The accessories, as FindOrCreate's loop (the same off-by-one bound; the holder pointer
    // advanced for every index); FUN_004c9e20's body is inlined here.
    CostumeDescriptions const* const Descs = GetCostumeDescriptions();
    FMeshHolder* Holder = &Accessories[0];
    for (int Index = 0; Index < static_cast<int>(Key.AccessoryCount); ++Index, ++Holder)
    {
        if (static_cast<unsigned>(Index) > 4u)
            break;
        accessory const* const Found = Descs->find_accessory(Key.AccessoryIds[Index]);
        if (Found == nullptr)
            continue;

        FEngineCharacterAccessoryLink Link; // FUN_005a6900 + the three TString clears
        Link.Mesh = CStrOrEmpty(Found->Mesh);                              // FUN_004cec20
        Link.CharacterHardpoint = CStrOrEmpty(Found->CharacterHardpoint);  // FUN_004cebc0
        Link.AccessoryHardpoint = CStrOrEmpty(Found->AccessoryHardpoint);  // FUN_004cebc0
        Holder->EngineInstance = Link.Connect(GetRootBone());

        // With a material library, the first one into the holder (FUN_00420f30).
        if (!Found->Materials.empty())
        {
            char const* const First = CStrOrEmpty(Found->Materials.front());
            CacheString Material;
            Material.clear();
            Material.Value = StringAlloc(First, true);
            ClientMeshHolderSetMaterials(Holder, Material.Value, nullptr);
        }
    } // Link destroyed (FUN_004de730)

    return Deformable != nullptr;
}

bool FEngineCharacterBody::IsPlaybackIdle() const
{
    // FUN_00560a80. Retail does not test Deformable for null.
    int Running = 0;
    DALib::Deform->GetListCount(Deformable, &Running);
    return Running == 0;
}

bool FEngineCharacterBody::PlayAnimation(char const* Name, unsigned Arg2, float Arg3, float Arg4,
                                         int Arg5)
{
    // FUN_00560ab0. FUN_004cba10's body first.
    GetRootTransform(&RootTransform);

    if (Name == nullptr || Name[0] == '\0')
        return false;

    // The first slot whose id is -1. No free slot leaves -1 and retail writes the name one slot
    // below the array; the callers queue at most the pilot's body.anim list into a body
    // StopAnimations just emptied.
    int Slot = -1;
    for (int Index = 0; Index < 4; ++Index)
    {
        if (AnimIds[Index] == -1)
        {
            Slot = Index;
            break;
        }
    }

    AnimNames[Slot] = Name;

    void* const Script = GetCharacterInfoDb()->AcquireScriptByName(AnimNames[Slot].Data);

    GENRESULT const Result = DALib::Deform->Play(Deformable, &AnimIds[Slot], Script,
                                                 const_cast<char*>(Name), DwordArg(Arg2),
                                                 DwordArg(Arg3), DwordArg(Arg4), DwordArg(1.0f),
                                                 DwordArg(static_cast<unsigned>(Arg5)),
                                                 DwordArg(-1.0f), nullptr);
    if (Result != 0)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: failed to play character animation '%s'\n",
              kEngineCharacterH, 0x1f7, Name);
    }
    return Result == 0;
}

// ============================================================================================
// FEngineCharacterCache
// ============================================================================================

FEngineCharacterCache G_EngineCharacterCache;

FEngineCharacterCache::FEngineCharacterCache()
{
    // FUN_004c98c0: the sentinel node, size 0.
}

FEngineCharacterCache::~FEngineCharacterCache()
{
    // FUN_004c9900.
    std::list<FEngineCharacterCacheNode>::iterator It = Nodes.begin();
    while (It != Nodes.end())
    {
        FEngineCharacterBody* const Body = It->Body;
        if (Body != nullptr)
        {
            // FUN_004ca530 + free, inlined in retail.
            delete Body;
        }
        It->Body = nullptr;
        It = Nodes.erase(It);
    }
}

FEngineCharacterBody* FEngineCharacterCache::FindOrCreate(Costume const& Key)
{
    // FUN_004c9a80.
    for (std::list<FEngineCharacterCacheNode>::iterator It = Nodes.begin(); It != Nodes.end();
         ++It)
    {
        if (Costume_is_equal(It->CostumeKey, Key))
        {
            ++It->RefCount;
            return It->Body;
        }
    }

    // Only the first Count accessory ids are copied.
    FEngineCharacterCacheNode Node;
    Node.CostumeKey.PartIds[0] = Key.PartIds[0];
    Node.CostumeKey.PartIds[1] = Key.PartIds[1];
    Node.CostumeKey.PartIds[2] = Key.PartIds[2];
    Node.CostumeKey.PartIds[3] = Key.PartIds[3];
    Node.CostumeKey.AccessoryCount = Key.AccessoryCount;
    for (unsigned Index = 0; static_cast<int>(Index) < static_cast<int>(Key.AccessoryCount);
         ++Index)
        Node.CostumeKey.AccessoryIds[Index] = Key.AccessoryIds[Index];

    FEngineCharacterBody* Body = new FEngineCharacterBody();

    Body->Reset();
    {
        FEngineCharacterBodyDesc Desc(Key);
        Body->Deformable = Desc.CreateDeformable();
    }

    // Retail bug, kept: the bound is `i > 4`, so a costume with five or more accessories
    // reaches index 4 -- one holder past the four the body has, over AnimNames[0]. Shipped
    // costume tables never have that many.
    CostumeDescriptions const* const Descs = GetCostumeDescriptions();
    FMeshHolder* Holder = &Body->Accessories[0];
    for (int Index = 0; Index < static_cast<int>(Key.AccessoryCount); ++Index, ++Holder)
    {
        if (static_cast<unsigned>(Index) > 4u)
            break;
        accessory const* const Found = Descs->find_accessory(Key.AccessoryIds[Index]);
        if (Found == nullptr)
            continue;

        FEngineCharacterAccessoryLink Link;
        Link.Mesh = CStrOrEmpty(Found->Mesh);
        Link.CharacterHardpoint = CStrOrEmpty(Found->CharacterHardpoint);
        Link.AccessoryHardpoint = CStrOrEmpty(Found->AccessoryHardpoint);
        Holder->EngineInstance = Link.Connect(Body->GetRootBone());

        // With a material library, the first one (the local is never cleared again) into the
        // holder's first material slot.
        if (!Found->Materials.empty())
        {
            char const* const First = CStrOrEmpty(Found->Materials.front());
            CacheString Material;
            Material.clear();
            Material.Value = StringAlloc(First, true);
            ClientMeshHolderSetMaterials(Holder, Material.Value, nullptr);
        }
    }

    if (Body->Deformable == nullptr)
    {
        delete Body;
        Body = nullptr;
    }

    // Appended with count 1, the null body included.
    Node.Body = Body;
    Node.RefCount = 1;
    Nodes.push_back(Node);
    return Body;
}

void FEngineCharacterCache::DeleteAll()
{
    // Inlined in HUD_Receiver's destructor.
    std::list<FEngineCharacterCacheNode>::iterator It = Nodes.begin();
    while (It != Nodes.end())
    {
        if (It->Body != nullptr)
            delete It->Body; // FUN_004ceb30(1): FUN_004ca530 + free
        It->Body = nullptr;
        It = Nodes.erase(It);
    }
}
