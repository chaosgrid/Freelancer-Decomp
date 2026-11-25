// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/gf_character_object.h"

#include "Interface/cargo_bar_control.h" // ToolTipHoverInit / ToolTipHoverSetCaption
#include "Common/client_cliobj.h"     // PushMaterialPathPair, ResolveMaterialEntry, ReleaseMaterialEntrySlot
#include "Core/game_options.h"      // GetGameOption (FUN_005792b0)
#include "GameFlow/Thorn/gf_script_object.h"  // G_GfScriptManager (DAT_0066d2d0), FGfScriptSequence
#include "Core/globals.h"           // GetClientFrameTimeCapped (FUN_0042d680), G_RenderSurfaceWidth
#include "Interface/Pages/character_select_equip.h" // GetPlayerVisitFlagsRetail / SetPlayerVisitFlagsRetail
#include "Common/input_manager.h"     // MouseSetCursor (FUN_0041dde0)
#include "GameFlow/menu_universe_base.h" // GetClientLocationRoomScriptBehaviors (FUN_0043ad50)
#include "GameFlow/mirror_property_bucket.h"
#include "Common/misc_util.h"         // MiscUtilLoadEngineInstance, MiscUtilLoadIdsCopy, MiscUtilSphereInView
#include "GameFlow/nav_bar.h"           // NavBarCharacterClicked (FUN_00442810), NavBarOpenButton (FUN_00442200)
#include "GameFlow/room_control_hotspot.h" // EHotspotBehavior
#include "NeuroNet/nn_tooltip.h"        // NnToolTipHideGlobal (FUN_004be390)
#include "Space/player_spawn.h"      // GetPlayerConnectionId (DAT_00673344)
#include "Network/server_connect.h"    // GetLocalServerInterface (DAT_0067ecd0)
#include "Space/Cameras/space_camera.h"      // FSpaceCamera (DAT_0067dbf8)
#include "Interface/tooltip_hover.h"     // FToolTipHoverInfo / NnToolTipFeedHover (FUN_004be370)
#include "Core/widescreen.h"      // port-only Hor+ (FREELANCER_WIDESCREEN_SUPPORT)
#include "Interface/win3d.h"

#include "DALib.h"
#include "common_cobject.h"    // CompoundInstanceFindFirstName
#include "common_iface.h"      // ScriptBehavior
#include "common_ini.h"        // StringAlloc, RefreshLoadingProgress
#include "common_misc.h"       // malloc_strcpy / malloc_strfree / malloc_wstrcpy / get_projected_bounding_sphere
#include "common_newchar.h"    // RtcSlider
#include "common_reputation.h" // CostumeDescriptions, DetailSwitchTable
#include "common_server.h"     // IServer
#include "common_universe.h"   // Universe::get_base (FUN_00445720)
#include "dacom.h"             // FDUMP, DACOM_CRC
#include "engine/engbase.h"
#include "x86math.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

namespace
{
    char const kObjectDeformableCpp[] = "E:\\FL\\Scratch\\Source\\Client\\gf\\ObjectDeformable.cpp";

    // DAT_00668848 (0x100 bytes, bss) -- FUN_00445720's result buffer; every exit
    // returns its address. That function is its only reader and writer.
    char G_BgcsContextBuffer[256];

    // DAT_00668948 (0x124 bytes) -- the body every "pc" character shares. The static
    // initializer LAB_00443e40 zeroes the instance and both four-entry arrays and registers an
    // empty atexit; zero initialization here is that.
    FCharacterLodSet G_SharedCharacterLod;


    // FUN_00443b60 (thiscall on Set) -- build Set's deformable from the four bodyparts
    // (0 body, 1 head, 2 left hand, 3 right hand) and read back its LOD switch table.
    void BuildCharacterDeformable(FCharacterLodSet* Set, bodypart const* const* BodyParts)
    {
        // "low_character_lod" sets Flags bit 1 (Deformable2 skips LOD 0)
        // and raises the LOD limit of the non-first parts from 2 to 3.
        unsigned long PartFlags = 0;
        unsigned long ChildLodLimit = 2;
        if (RtcSlider::is_enabled("low_character_lod"))
        {
            PartFlags = 2;
            ChildLodLimit = 3;
        }

        FDeformPartDesc Entries[4];
        int Index = 0;
        for (Index = 0; Index < 4; ++Index)
        {
            Entries[Index].LodCount = 0;
            Entries[Index].Lods = nullptr;
            Entries[Index].Field08 = 0;
            Entries[Index].Path = nullptr;
            Entries[Index].Flags = 0;
            Entries[Index].LodLimit = 0xFFFFFFFFu;
        }

        // One entry per bodypart with a mesh; with FileSystem and Name zeroed Deformable2 opens
        // the part by Path.
        FDeformLodDesc LodSlots[4][16];
        char Paths[4][0x400];
        int Count = 0;
        for (Index = 0; Index < 4; ++Index)
        {
            bodypart const* const Part = BodyParts[Index];
            if (Part == nullptr || Part->Mesh.empty())
                continue;
            strcpy(Paths[Count], GetFullFilename(Part->Mesh.c_str()));
            int Lod = 0;
            for (Lod = 0; Lod < 16; ++Lod)
            {
                LodSlots[Count][Lod].FileSystem = nullptr;
                LodSlots[Count][Lod].Name = nullptr;
            }
            Entries[Count].LodCount = 16;
            Entries[Count].Lods = LodSlots[Count];
            Entries[Count].Field08 = 0;
            Entries[Count].Path = Paths[Count];
            Entries[Count].Flags = PartFlags;
            Entries[Count].LodLimit = (Index == 0) ? 0xFFFFFFFFu : ChildLodLimit;
            ++Count;
        }

        struct FDeformPartTable
        {
            int Count;
            FDeformPartDesc* Entries;
        };
        FDeformPartTable Table;
        Table.Count = Count;
        Table.Entries = Entries;
        RefreshLoadingProgress();

        // A material-library reference per bodypart mesh (FUN_0041aa50) while the body loads.
        Archetype::FCliMaterialEntry* Materials[4];
        for (Index = 0; Index < 4; ++Index)
        {
            bodypart const* const Part = BodyParts[Index];
            if (Part == nullptr || Part->Mesh.empty())
                continue;
            Materials[Index] = Archetype::ResolveMaterialEntry(Part->Mesh.c_str(), true,
                                                               0xffffffffu);
        }
        RefreshLoadingProgress();

        DALib::Deform->Create(&Set->Instance, reinterpret_cast<int*>(&Table), nullptr, nullptr,
                              nullptr);
        RefreshLoadingProgress();

        for (Index = 0; Index < 4; ++Index)
        {
            bodypart const* const Part = BodyParts[Index];
            if (Part == nullptr || Part->Mesh.empty())
                continue;
            Archetype::ReleaseMaterialEntrySlot(&Materials[Index]);
        }
        RefreshLoadingProgress();

        // Per bodypart, the LOD count the part came back with and each
        // LOD's switch distance (DetailSwitchTable::get_switch_distance(fraction * 100.0)); the
        // last one is the table's distance at 0. A part without a mesh gets 0 and no table.
        int EntryIndex = 0;
        for (Index = 0; Index < 4; ++Index)
        {
            bodypart const* const Part = BodyParts[Index];
            if (Part == nullptr || Part->Mesh.empty())
            {
                Set->Counts[Index] = 0;
                Set->Tables[Index] = nullptr;
                continue;
            }
            int const LodCount = Entries[EntryIndex].LodCount;
            Set->Counts[Index] = LodCount;
            int Lod = 0;
            for (Lod = 0; Lod < LodCount; ++Lod)
            {
                DetailSwitchTable* const Switch = Part->Switch;
                float const Fraction = static_cast<float>(
                    static_cast<double>(Entries[EntryIndex].Lods[Lod].Fraction) * 100.0);
                Set->Switch[Index][Lod] = Switch->get_switch_distance(Fraction);
                Set->Tables[Index] = Switch;
            }
            if (Set->Counts[Index] > 0)
                Set->Switch[Index][Set->Counts[Index] - 1] = Part->Switch->get_switch_distance(0.0f);
            ++EntryIndex;
        }
    }

    // The dealer screen a role opens (inlined in FUN_00445c10 and LAB_00445d30): 2 -> 5,
    // 3 -> 7, 4 -> 8 (NavBarOpenButton, FUN_00442200); the bartender (1) and no role open
    // nothing.
    void OpenRoleDealer(int Role)
    {
        if (Role == kCharacterRoleBartender)
            return;
        if (Role == kCharacterRoleTrader)
            NavBarOpenButton(kHotspotStartDealer);
        else if (Role == kCharacterRoleShipDealer)
            NavBarOpenButton(kHotspotStartShipDealer);
        else if (Role == kCharacterRoleEquipmentDealer)
            NavBarOpenButton(kHotspotStartEquipDealer);
    }

    // LAB_00445d30 (cdecl) -- the stop callback of a click's behaviour script: A is the
    // character (the sequence's +0x1c).
    void __cdecl OnBehaviorScriptStopped(void* A, void* /*B*/, unsigned char /*bArg*/)
    {
        OpenRoleDealer(static_cast<FCharacterObject*>(A)->Role);
    }

    // FUN_00437020 (thiscall on the bucket) -- the first node whose behaviour has Key,
    // or null.
    MetaNode<ScriptBehavior>* FindBehaviorNode(TMirrorPropertyBucket<ScriptBehavior>* Bucket,
                                                unsigned Key)
    {
        MetaNode<ScriptBehavior>* Cursor = nullptr;
        while (Bucket->List.traverse(Cursor, true))
        {
            if (static_cast<unsigned>(Cursor->Data->BehaviorId) == Key)
                return Cursor;
        }
        return nullptr;
    }
}

// ============================================================================================
// FCharacterDesc
// ============================================================================================

FCharacterDesc::FCharacterDesc()
    : Placement(nullptr)
    , PlacementIndex(-1)
    , bScriptSpatial(0)
{
    // FUN_00443a90.
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;
    MatrixSetIdentity(Orient);
}

FCharacterDesc::~FCharacterDesc()
{
    // FUN_00443b10.
    Props.destroy();
}

// ============================================================================================
// FCharacterHotspot
// ============================================================================================

FCharacterHotspot::FCharacterHotspot()
    : Character(nullptr)
    , bOffscreen(0)
{
}

FCharacterHotspot::~FCharacterHotspot()
{
    // FUN_00444960 (then ~FHotspot, FUN_00555ea0).
}

void FCharacterHotspot::UpdateScreenRect(ICamera* Camera)
{
    // FUN_00555f60.
    const Vector* const Where = DALib::Engine->GetInstancePosition(Character->GetEngineInstance());
    Vector Center;
    Center.x = Where->x;
    Center.y = Where->y;
    Center.z = Where->z;

    float ScreenX = 0.0f;
    float ScreenY = 0.0f;
    float Radius = 0.0f;
    float Depth = 0.0f;
    if (get_projected_bounding_sphere(Camera, Center, 1.0f, ScreenX, ScreenY, Radius, Depth) == 0)
    {
        bOffscreen = 1;
        ScreenRect.Left = 0;
        ScreenRect.Top = 0;
        ScreenRect.Width = 0;
        ScreenRect.Height = 0;
        return;
    }

    // The float constants 0.6f (0x005e1220) and 1.2f (0x005c9800) are widened to double.
    bOffscreen = 0;
    int const Left = static_cast<int>(floor(static_cast<double>(ScreenX) -
                                            static_cast<double>(Radius) *
                                                static_cast<double>(0.6f) +
                                            0.5));
    int const Top = static_cast<int>(
        floor(static_cast<double>(ScreenY) - static_cast<double>(Radius) + 0.5));
    int const Width = static_cast<int>(
        floor(static_cast<double>(Radius) * static_cast<double>(1.2f) + 0.5));
    int const Height = static_cast<int>(
        floor(static_cast<double>(Radius) + static_cast<double>(Radius) + 0.5));
    ScreenRect.Left = Left;
    ScreenRect.Top = Top;
    ScreenRect.Width = Width;
    ScreenRect.Height = Height;
}

// ============================================================================================
// FCharacterObject
// ============================================================================================

FCharacterObject::FPartCacheEntry::FPartCacheEntry()
    : Name(nullptr)
    , Part(-1)
    , bUsed(0)
{
    // LAB_00444060.
}

FCharacterObject::FPartCacheEntry::~FPartCacheEntry()
{
    // FUN_00444080: +0x04 = -1, the name freed (not cleared), +0x08 = 0.
    Part = -1;
    if (Name != nullptr)
        free(Name);
    bUsed = 0;
}

FCharacterObject::FCharacterObject(FRoomRuntime* InOwner, FCharacterDesc const& Desc)
    : FRoomObject(InOwner)
{
    // FUN_00443ed0.
    Lod.Instance = nullptr;
    int Index = 0;
    for (Index = 0; Index < 4; ++Index)
    {
        Lod.Counts[Index] = 0;
        Lod.Tables[Index] = nullptr;
    }
    Hotspot = nullptr;
    Role = kCharacterRoleNone;
    Placement = nullptr;
    for (Index = 0; Index < 4; ++Index)
        LodIds[Index] = -1;
    for (Index = 0; Index < kMaxAccessories; ++Index)
    {
        AccessoryInstances[Index] = -1;
        AccessoryHardpoints[Index].clear();
    }
    AccessoryCount = 0;
    bHovered = 0;
    DisplayName = nullptr;
    Hardpoints.clear();
    OwnScript = nullptr;
    OwnScriptName = nullptr;
    bOwnScriptPlaying = 0;
    ScriptState = kCharacterScriptOwn;
    ActiveMotions = 0;
    ActiveAims = 0;
    bPoseDirty = 0;
    bHasLastPose = 0;
    Load(Desc);
}

FCharacterObject::~FCharacterObject()
{
    // FUN_004440c0.
    if (ScriptState == kCharacterScriptTakenOver)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kObjectDeformableCpp, 0x175,
              "Character is being destroyed while still scripted, GAME IS ABOUT TO CRASH!!!\n");
    }
    // The own script is forgotten, not destroyed.
    if (OwnScript != nullptr)
    {
        OwnScript = nullptr;
        bOwnScriptPlaying = 0;
        malloc_strfree(&OwnScriptName);
    }
    int Index = 0;
    for (Index = 0; Index < AccessoryCount; ++Index)
    {
        if (AccessoryInstances[Index] != -1)
        {
            DALib::Engine->DestroyInstance(AccessoryInstances[Index]);
            AccessoryInstances[Index] = -1;
        }
        AccessoryHardpoints[Index].destroy();
    }
    AccessoryCount = 0;
    malloc_wstrfree(&DisplayName);
    Hardpoints.destroy();
    Props.destroy();
    // An own body is destroyed; the shared one is left alone.
    if (bSharedDeform == 0)
    {
        if (Lod.Instance != nullptr)
            DALib::Deform->Destroy(Lod.Instance);
        Lod.Instance = nullptr;
        for (Index = 0; Index < 4; ++Index)
        {
            Lod.Counts[Index] = 0;
            Lod.Tables[Index] = nullptr;
        }
    }
    if (Hotspot != nullptr)
    {
        delete Hotspot;
        Hotspot = nullptr;
    }
    // The members' destructors and ~FRoomObject (FUN_005594b0) follow.
}

void FCharacterObject::Load(FCharacterDesc const& Desc)
{
    // FUN_00444290. A "pc" record (+0x1c == 1) shares the one body.
    RefreshLoadingProgress();
    Props = Desc.Props;
    Placement = Desc.Placement;
    PlacementIndex = Desc.PlacementIndex;
    bSharedDeform = (Props.CharType == 1) ? 1 : 0;
    Role = kCharacterRoleNone;

    // The role from the placement's setpoint segment.
    if (Placement != nullptr)
    {
        SetpointProperties Setpoint;
        Setpoint.extract_from_name(Placement->Name);
        if (_stricmp(Setpoint.Segment, "bartender") == 0) // DACOM stricmp
            Role = kCharacterRoleBartender;
        else if (_stricmp(Setpoint.Segment, "trader") == 0)
            Role = kCharacterRoleTrader;
        else if (_stricmp(Setpoint.Segment, "shipdealer") == 0)
            Role = kCharacterRoleShipDealer;
        else if (_stricmp(Setpoint.Segment, "equipment") == 0)
            Role = kCharacterRoleEquipmentDealer;
    }

    // The head is costume part 0, the body part 1.
    CostumeDescriptions const* const Descs = GetCostumeDescriptions();
    bodypart const* BodyParts[4];
    BodyParts[1] = Descs->find_bodypart(1, Props.Cost.PartIds[0]);
    BodyParts[0] = Descs->find_bodypart(0, Props.Cost.PartIds[1]);
    BodyParts[2] = Descs->find_bodypart(2, Props.Cost.PartIds[2]);
    BodyParts[3] = Descs->find_bodypart(3, Props.Cost.PartIds[3]);
    accessory const* Accessories[kMaxAccessories];
    int Index = 0;
    for (Index = 0; Index < static_cast<int>(Props.Cost.AccessoryCount); ++Index)
        Accessories[Index] = Descs->find_accessory(Props.Cost.AccessoryIds[Index]);

    // The body -- built for this character, or the shared one (built the first time) copied in.
    if (bSharedDeform == 0)
    {
        BuildCharacterDeformable(&Lod, BodyParts);
    }
    else
    {
        if (G_SharedCharacterLod.Instance == nullptr)
            BuildCharacterDeformable(&G_SharedCharacterLod, BodyParts);
        Lod.Instance = G_SharedCharacterLod.Instance;
        for (Index = 0; Index < 4; ++Index)
        {
            Lod.Counts[Index] = G_SharedCharacterLod.Counts[Index];
            Lod.Tables[Index] = G_SharedCharacterLod.Tables[Index];
            int Lod16 = 0;
            for (Lod16 = 0; Lod16 < 16; ++Lod16)
                Lod.Switch[Index][Lod16] = G_SharedCharacterLod.Switch[Index][Lod16];
        }
    }

    // With a root bone, the pose and the height (RoomObject slot 3 with the position's y).
    int Bone = -1;
    DALib::Deform->GetFirstBone(Lod.Instance, &Bone);
    if (Bone != -1)
    {
        DALib::Deform->SetPosePos(Lod.Instance, const_cast<float*>(&Desc.Pos.x));
        SetFloorHeight(Desc.Pos.y);
        DALib::Deform->SetPoseOri(Lod.Instance, const_cast<Matrix*>(&Desc.Orient));
    }
    Hardpoints.analyze_instance(Bone);
    RefreshLoadingProgress();

    // Each accessory with a mesh: loaded (FUN_0041c540, the material entries into RoomObject
    // +0x20) and its AccessoryHardpoint connected to the body's CharacterHardpoint (IHardpoint
    // slot 6) when both exist.
    for (Index = 0; Index < static_cast<int>(Props.Cost.AccessoryCount); ++Index)
    {
        accessory const* const Found = Accessories[Index];
        if (Found == nullptr || Found->Mesh.empty())
            continue;

        Archetype::FCliVc6Vector<Archetype::FCliMaterialPathPair> Paths;
        Paths.Cookie = 0;
        Paths.Begin = nullptr;
        Paths.End = nullptr;
        Paths.Capacity = nullptr;
        std::list<std::string>::const_iterator It = Found->Materials.begin();
        for (; It != Found->Materials.end(); ++It)
        {
            CacheString Name;
            Name.clear();
            Name.Value = StringAlloc(It->c_str(), true);
            Archetype::PushMaterialPathPair(&Paths, Name.Value, 0xffffffffu);
        }

        const InstanceIndex Instance = MiscUtilLoadEngineInstance(Found->Mesh.c_str(), DALib::Data,
                                                          nullptr, nullptr, &Paths,
                                                          &MaterialEntries, nullptr);
        AccessoryInstances[Index] = reinterpret_cast<long>(Instance);
        AccessoryHardpoints[Index].analyze_instance(AccessoryInstances[Index]);
        if (AccessoryInstances[Index] != -1)
        {
            char const* const CharacterHp = Found->CharacterHardpoint.c_str();
            char const* const AccessoryHp = Found->AccessoryHardpoint.c_str();
            int const CharacterIndex = Hardpoints.find_name(CharacterHp);
            int const AccessoryIndex = AccessoryHardpoints[Index].find_name(AccessoryHp);
            if (CharacterIndex != -1 && AccessoryIndex != -1)
            {
                FHardpointKey AccessoryKey;
                AccessoryKey.Name = AccessoryHp;
                AccessoryKey.Crc = DACOM_CRC::GetCRC32(AccessoryHp);
                FHardpointKey CharacterKey;
                CharacterKey.Name = CharacterHp;
                CharacterKey.Crc = DACOM_CRC::GetCRC32(CharacterHp);
                const InstanceIndex AccessoryPart = AccessoryHardpoints[Index].get_instance(AccessoryIndex);
                const InstanceIndex CharacterPart =
                    Hardpoints.get_instance(CharacterIndex);
                DALib::Hardpoint->ConnectHardpoints(CharacterPart, &CharacterKey, AccessoryPart,
                                                    &AccessoryKey);
            }
        }
        delete[] Paths.Begin;
    }

    AccessoryCount = static_cast<int>(Props.Cost.AccessoryCount);
    RefreshLoadingProgress();

    // The hotspot is enabled only with a behaviour or a role (FUN_00445a60, inlined).
    Hotspot = new FCharacterHotspot();
    Hotspot->Character = this;
    Hotspot->bEnabled = 1;
    Hotspot->Owner = this;
    Hotspot->Cookie = 0;
    UpdateHotspotEnabled();

    // The record's ids (FUN_0041c770), else its wide name.
    DisplayName = nullptr;
    if (Props.NameIds != 0)
        MiscUtilLoadIdsCopy(&DisplayName, Props.NameIds);
    else
        malloc_wstrcpy(&DisplayName, Props.NameWide);

    // No self-update until the room starts it; the idle script (+0x1ac) becomes the own script
    // unless it already is.
    bNoUpdate = 1;
    unsigned char const bSpatial = Desc.bScriptSpatial;
    char const* const IdleScript = Props.Script;
    bool bSame = false;
    if (IdleScript != nullptr && OwnScriptName != nullptr &&
        _stricmp(IdleScript, OwnScriptName) == 0) // DACOM stricmp
    {
        bSame = true;
    }
    if (!bSame)
    {
        if (OwnScript != nullptr)
        {
            G_GfScriptManager.DestroySequence(OwnScript);
            OwnScript = nullptr;
            bOwnScriptPlaying = 0;
            malloc_strfree(&OwnScriptName);
        }
        malloc_strcpy(&OwnScriptName, IdleScript);
        OwnScript = CreateOwnScript(IdleScript, bSpatial);
        if (ScriptState == kCharacterScriptOwn && OwnScript != nullptr)
        {
            OwnScript->Play();
            bOwnScriptPlaying = 1;
        }
    }

    // One full step, then a random fraction (rand() / 32768, DAT_005caeec) when it is above 0.
    if (OwnScript != nullptr)
        OwnScript->AdvanceCurrent(1.0f);
    float const Random = static_cast<float>(rand()) * 3.0517578125e-05f;
    if (OwnScript != nullptr && Random > 0.0f)
        OwnScript->AdvanceCurrent(Random);

    RenderMode = 0;
    LightGroup = 0;
    Layer = 0;
}

InstanceIndex FCharacterObject::GetEngineInstance()
{
    // LAB_00444970: IDeformable slot 18 into an int that starts at -1.
    int Bone = -1;
    DALib::Deform->GetFirstBone(Lod.Instance, &Bone);
    return static_cast<intptr_t>(Bone);
}

void FCharacterObject::SetFloorHeight(float Value)
{
    // LAB_004449a0: IDeformable slot 24 with the value, +0x1c = value, +0x378 = 1.
    DALib::Deform->SetHeight(Lod.Instance, Value);
    FloorHeight = Value;
    bPoseDirty = 1;
}

void FCharacterObject::Update(double DeltaSeconds)
{
    // LAB_004449d0.
    if (GetGameOption(kGameOptObjectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptCharacters) == 0)
        return;

    // A script let go of the character -- its own script again, and
    // no self-update.
    if (ScriptState == kCharacterScriptReleased)
    {
        ScriptState = kCharacterScriptOwn;
        RestartOwnScript();
        bNoUpdate = 1;
    }

    // A pose other than last frame's (compared bitwise, dword by dword) marks the pose dirty.
    float Pos[3];
    memcpy(Pos, DALib::Engine->GetInstancePosition(GetEngineInstance()), sizeof(Pos));
    float Orient[9];
    memcpy(Orient, DALib::Engine->GetInstanceOrientation(GetEngineInstance()), sizeof(Orient));
    if (bHasLastPose == 0 || memcmp(LastPos, Pos, sizeof(Pos)) != 0 ||
        memcmp(LastOrient, Orient, sizeof(Orient)) != 0)
    {
        bPoseDirty = 1;
    }
    memcpy(LastPos, Pos, sizeof(Pos));
    memcpy(LastOrient, Orient, sizeof(Orient));
    bHasLastPose = 1;

    // IAnimation2 slot 27 and IEngine slot 27.
    if (ActiveMotions != 0 || ActiveAims != 0 || bPoseDirty != 0)
    {
        float const Time = static_cast<float>(DeltaSeconds);
        DALib::Anim->MixerWalk(static_cast<unsigned long>(
                                   static_cast<uintptr_t>(GetEngineInstance())),
                               Time);
        void* TimeArg = nullptr;
        memcpy(&TimeArg, &Time, sizeof(Time));
        DALib::Engine->UpdateInstance(GetEngineInstance(), 0, TimeArg);
    }

    // Over a 3D window: the arrow cursor, and no name.
    if (Win3DGetHover() != nullptr)
    {
        MouseSetCursor("arrow", false);
        bHovered = 0;
    }

    // Hovered with a name -- the name as a tooltip keyed on this
    // object, pointing along the view ray through the hotspot's top-left corner 30 pixels up.
    if (bHovered == 0)
        return;
    if (DisplayName == nullptr)
        return;

    FToolTipHoverInfo Info;
    ToolTipHoverInit(&Info);
    ToolTipHoverSetCaption(&Info, DisplayName);
    Info.Ids = static_cast<int>(reinterpret_cast<intptr_t>(this));

    // The corner on the Win3D camera's near plane (DAT_0067dc60 /
    // 0067dc64 centre, 0067dc70 / 0067dc74 scale, 0067dc9c znear), turned by its orientation
    // DAT_0067dbfc (FUN_00423b90).
    FSpaceCamera* const Cam = Win3DGetCamera();
    Vector Local;
    Win3DScreenToNearPlane(Cam, Local, static_cast<float>(Hotspot->ScreenRect.Left),
                           static_cast<float>(Hotspot->ScreenRect.Top + -0x1e));
    Matrix CamOrientation;
    memcpy(&CamOrientation, &Cam->World, sizeof(CamOrientation));
    Vector World;
    Win3DTransformByMatrix(World, CamOrientation, Local);

    // Normalised with the sum order (z^2 + x^2) + y^2; 1.0 is DAT_005c75dc.
    double const X = World.x;
    double const Y = World.y;
    double const Z = World.z;
    double const Inv = 1.0 / sqrt(Z * Z + X * X + Y * Y);
    Info.WorldPos[0] = static_cast<float>(X * Inv);
    Info.WorldPos[1] = static_cast<float>(Y * Inv);
    Info.WorldPos[2] = static_cast<float>(Z * Inv);
    Info.HalfExtents[0] = 0.0f;
    Info.HalfExtents[1] = 0.0f;
    Info.HalfExtents[2] = 0.0f;
    Info.bLive = 1;
    NnToolTipFeedHover(&Info);
}

void FCharacterObject::PickLods(ICamera* Camera)
{
    // FUN_00444f90. The camera distance is 1.0 without an instance.
    float Distance = 1.0f;
    const InstanceIndex Instance = GetEngineInstance();
    if (Instance != InvalidInstanceIndex)
    {
        const Vector* const Where = DALib::Engine->GetInstancePosition(Instance);
        float const X = Where->x;
        float const Y = Where->y;
        float const Z = Where->z;
        const Vector* const Eye = Camera->GetPosition();
        double const Dx = static_cast<double>(X) - Eye->x;
        double const Dy = static_cast<double>(Y) - Eye->y;
        double const Dz = static_cast<double>(Z) - Eye->z;
        Distance = static_cast<float>(sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
    }
    float const HalfFov = Camera->GetHalfFovXDeg();
#ifdef FREELANCER_WIDESCREEN_SUPPORT
    // Port-only Hor+: slot 6 is the angle on a 4:3 surface, so the LOD pairs it with the width
    // of that surface and picks what a 4:3 screen of this height would.
    float const SurfaceWidth = static_cast<float>(WidescreenEquivalentSurfaceWidth());
#else
    float const SurfaceWidth = static_cast<float>(static_cast<int>(G_RenderSurfaceWidth));
#endif

    // Per part, the first LOD whose switch distance is not below the adjusted distance (a NaN
    // counts as a hit); none -> -1. With "low_character_lod" and the adjusted distance beyond the
    // 100% switch distance, one LOD coarser, or past the last: the first part keeps its last LOD,
    // the others none. The picks are packed.
    int Written = 0;
    int Part = 0;
    for (Part = 0; Part < 4; ++Part)
    {
        int const Count = Lod.Counts[Part];
        if (Count <= 0)
            continue;
        float const Adjusted =
            Lod.Tables[Part]->adjust_distance(Distance, HalfFov, SurfaceWidth);
        int Pick = -1;
        int Index = 0;
        for (Index = 0; Index < Count; ++Index)
        {
            if (!(Lod.Switch[Part][Index] < Adjusted))
                break;
        }
        if (Index < Count)
        {
            Pick = Index;
            if (RtcSlider::is_enabled("low_character_lod"))
            {
                float const Full = Lod.Tables[Part]->get_switch_distance(100.0f);
                if (Full < Adjusted)
                {
                    if (Index + 1 < Count)
                        Pick = Index + 1;
                    else if (Part == 0)
                        Pick = Lod.Counts[0] - 1;
                    else
                        Pick = -1;
                }
            }
        }
        LodIds[Written] = Pick;
        ++Written;
    }
    for (; Written < 4; ++Written)
        LodIds[Written] = -1;
}

void FCharacterObject::DrawOpaque(ICamera* Camera)
{
    // LAB_00445150.
    if (GetGameOption(kGameOptObjectsGroup) == 0)
        return;
    if (GetGameOption(kGameOptCharacters) == 0)
        return;
    if (GetGameOption(kGameOptLikePetal) != 0)
        return;

    // The hotspot follows the room camera; the body's bounding sphere must touch the room
    // camera's view (FUN_0041c8d0).
    ICamera* const RoomCamera = GetRoomRuntimeCamera(Owner);
    Hotspot->UpdateScreenRect(RoomCamera);
    float Radius = 0.0f;
    float Center[3];
    DALib::Deform->GetBoundSphere(Lod.Instance, &Radius, Center);
    if (!MiscUtilSphereInView(RoomCamera, Center, Radius))
        return;
    if (Camera == nullptr)
        return;
    const InstanceIndex Instance = GetEngineInstance();
    if (Instance == InvalidInstanceIndex)
        return;

    PickLods(Camera);
    float const FrameTime = static_cast<float>(GetClientFrameTimeCapped());
    DALib::Deform->SetLod(Lod.Instance, FrameTime, LodIds);

    // FUN_0044b370.
    float const Height = GetFloorHeight();
    unsigned long HeightBits = 0;
    memcpy(&HeightBits, &Height, sizeof(HeightBits));
    RoomControlDrawShadow(Owner, static_cast<unsigned long>(Instance),
                          HeightBits, 1);
    RoomControlLightObject(Owner, this); // FUN_0044ae80
    // The out int is an uninitialised stack slot in retail.
    int DrawnLod = 0;
    DALib::Deform->Draw(Lod.Instance, Camera, LodIds, nullptr, &DrawnLod, 0);
}

void FCharacterObject::RestartOwnScript()
{
    // FUN_00445280.
    if (OwnScript != nullptr)
    {
        if (bOwnScriptPlaying != 0)
        {
            OwnScript->RestartCurrent();
            return;
        }
        OwnScript->Play();
        bOwnScriptPlaying = 1;
        return;
    }
    // The idle script, unless it already is the own one.
    char const* const IdleScript = Props.Script;
    if (IdleScript != nullptr && OwnScriptName != nullptr &&
        _stricmp(IdleScript, OwnScriptName) == 0) // DACOM stricmp
    {
        return;
    }
    if (OwnScript != nullptr)
    {
        G_GfScriptManager.DestroySequence(OwnScript);
        OwnScript = nullptr;
        bOwnScriptPlaying = 0;
        malloc_strfree(&OwnScriptName);
    }
    malloc_strcpy(&OwnScriptName, IdleScript);
    OwnScript = CreateOwnScript(IdleScript, 0);
    if (ScriptState != kCharacterScriptOwn)
        return;
    if (OwnScript == nullptr)
        return;
    OwnScript->Play();
    bOwnScriptPlaying = 1;
}

FGfScriptSequence* FCharacterObject::CreateOwnScript(char const* ScriptName, unsigned char bSpatial)
{
    // FUN_00445340.
    if (ScriptName == nullptr || ScriptName[0] == '\0')
        return nullptr;

    FGfScriptSequence* const Sequence = G_GfScriptManager.CreateSequence(Owner, 0);
    if (Sequence == nullptr)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kObjectDeformableCpp, 0x453,
              "Cannot allocate thorn script\n");
        return Sequence;
    }

    // The play frame starts at the identity.
    IScriptEngine* const Engine = ThornScriptLoad(ScriptName);
    Csys const ScriptFrame = ThornScriptGetCsys(Engine, nullptr);
    Csys Frame;
    Frame.Pos.x = 0.0f;
    Frame.Pos.y = 0.0f;
    Frame.Pos.z = 0.0f;
    MatrixSetIdentity(Frame.Orient);

    // On a placement, the placement's frame taken back through the
    // script's own (FUN_00435c90: Placement * ScriptFrame^-1), and its name.
    char const* PlacementName = nullptr;
    if (Placement != nullptr)
    {
        Csys const PlacementFrame = Placement->Frame;
        PlacementName = Placement->Name;
        Frame = PlacementFrame * ScriptFrame.get_inverse();
    }

    // This character in the mirror; the sequence binds the mirror's first.
    Sequence->AddScript(ScriptName, 0, &Frame, Engine);
    Sequence->Mirror.Add(this, nullptr);
    Sequence->BindMode = 1;
    Sequence->bOwnScript = 1;
    Sequence->bLoop = 1;
    Sequence->bLetterbox = 0;
    Sequence->bApplySpatial = bSpatial;
    Sequence->bRestoreCamera = 0;
    Sequence->bHoldTime = 0;
    Sequence->Priority = 0;
    Sequence->Callback = nullptr;
    Sequence->CallbackA = nullptr;
    Sequence->CallbackB = nullptr;
    if (PlacementName != nullptr)
        Sequence->SetName(PlacementName);
    return Sequence;
}

void FCharacterObject::SetOwnScript(char const* ScriptName, unsigned char bSpatial)
{
    // FUN_00445580.
    if (ScriptName != nullptr && OwnScriptName != nullptr &&
        _stricmp(ScriptName, OwnScriptName) == 0) // DACOM stricmp
    {
        return;
    }
    if (OwnScript != nullptr)
    {
        G_GfScriptManager.DestroySequence(OwnScript);
        OwnScript = nullptr;
        bOwnScriptPlaying = 0;
        malloc_strfree(&OwnScriptName);
    }
    malloc_strcpy(&OwnScriptName, ScriptName);
    OwnScript = CreateOwnScript(ScriptName, bSpatial);
    if (ScriptState != kCharacterScriptOwn)
        return;
    if (OwnScript == nullptr)
        return;
    OwnScript->Play();
    bOwnScriptPlaying = 1;
}

void FCharacterObject::UpdateHotspotEnabled()
{
    // FUN_00445a60: no behaviour (+0x1b0 == -1) and no role -> disabled.
    if (Props.BehaviorId == 0xffffffffu && Role == kCharacterRoleNone)
    {
        Hotspot->bEnabled = 0;
        return;
    }
    Hotspot->bEnabled = 1;
}

void FCharacterObject::OnHotspotEnter(unsigned long /*Cookie*/, int const* /*Point*/)
{
    // FUN_00445a90. Not while a window holds the mouse.
    if (Win3DAnyCaptured())
        return;
    bHovered = 1;
    char const* Cursor = "talk_blowoff";
    if (Role == kCharacterRoleTrader)
        Cursor = "talk_commodity_dealer";
    else if (Role == kCharacterRoleShipDealer)
        Cursor = "talk_ship_dealer";
    else if (Role == kCharacterRoleEquipmentDealer)
        Cursor = "talk_equip_dealer";

    // With a behaviour the room knows, the cursor by its kind; an unknown kind keeps the role's
    // cursor.
    unsigned const BehaviorId = Props.BehaviorId;
    if (BehaviorId != 0xffffffffu)
    {
        MetaNode<ScriptBehavior>* const Node =
            FindBehaviorNode(GetClientLocationRoomScriptBehaviors(GetRoomRuntimeRoomId(Owner)),
                             BehaviorId);
        if (Node != nullptr && Node->Data != nullptr)
        {
            ScriptBehavior const* const Behavior = Node->Data;
            if (Behavior->bGenericScript == 0)
            {
                Cursor = "talk_story";
            }
            else
            {
                unsigned const Kind = Behavior->DialogType;
                if (Kind == 0)
                    Cursor = "talk_blowoff";
                else if (Kind == 1)
                    Cursor = "talk_bribe";
                else if (Kind == 2)
                    Cursor = "talk_rumor";
                else if (Kind == 3)
                    Cursor = "talk_mission";
                else if (Kind == 4)
                    Cursor = "talk_info";
                else if (Kind == 5)
                    Cursor = "talk_story";
            }
        }
    }
    MouseSetCursor(Cursor, false);
}

void FCharacterObject::OnHotspotLeave(unsigned long /*Cookie*/, int const* /*Point*/)
{
    // FUN_00445be0. NnToolTipHideGlobal is FUN_004be390.
    if (Win3DAnyCaptured())
        return;
    MouseSetCursor("arrow", false);
    bHovered = 0;
    NnToolTipHideGlobal();
}

void FCharacterObject::OnHotspotClick(unsigned long Cookie, int const* Point)
{
    // FUN_00445c10. Not while a window holds the mouse.
    if (Win3DAnyCaptured())
        return;
    OnHotspotLeave(Cookie, Point); // slot 11

    // The character's faction (+0x164) marked met in the visit map (flags | 0x41, the server
    // told) unless bit 0 is already set.
    unsigned const Faction = Props.FactionId;
    if (Faction != 0xffffffffu)
    {
        unsigned char Flags = 0;
        GetPlayerVisitFlagsRetail(Faction, &Flags, 0, 0);
        if ((Flags & 1) == 0)
            SetPlayerVisitFlagsRetail(Faction, static_cast<unsigned char>(Flags | 0x41), true);
    }

    // The NavBar may take the click; a character under a script's control does nothing.
    if (NavBarCharacterClicked(this, Role))
        return;
    if (ScriptState != kCharacterScriptOwn)
        return;

    // With a behaviour, its script (FUN_00435dd0, the dealer opened when it stops) and the
    // server told (IServer slot 41).
    if (Props.BehaviorId != 0xffffffffu)
    {
        StartCharacterBehaviorScript(this, Owner, Placement, Props.BehaviorId,
                                     &OnBehaviorScriptStopped, this);
        static_cast<IServer*>(GetLocalServerInterface())->GFObjSelect(Props.Key,
                                                                      GetPlayerConnectionId());
        return;
    }
    OpenRoleDealer(Role);
}

void FCharacterObject::TakeOverForScript(unsigned char bKeepOwnScript)
{
    // FUN_004454e0.
    if (bKeepOwnScript == 0)
    {
        if (OwnScript != nullptr)
        {
            G_GfScriptManager.DestroySequence(OwnScript);
            OwnScript = nullptr;
            bOwnScriptPlaying = 0;
            malloc_strfree(&OwnScriptName);
        }
        ScriptState = kCharacterScriptTakenOver;
    }
    bNoUpdate = 1;
}

void FCharacterObject::ResetScriptState(unsigned char bRelease)
{
    // FUN_00445540. +0x04 is written before the branch.
    bNoUpdate = 0;
    if (bRelease == 0)
    {
        ScriptState = kCharacterScriptReleased;
        return;
    }
    OwnScript = nullptr;
    bOwnScriptPlaying = 0;
    malloc_strfree(&OwnScriptName);
}

long FCharacterObject::FindPart(char const* PartName)
{
    // FUN_00445620. With no unused entry the hit goes to entry 0. The name is replaced with CRT
    // free / _strdup, not the malloc_str* pair.
    int Unused = -1;
    int Index = 0;
    for (; Index < kPartCacheEntries; ++Index)
    {
        FPartCacheEntry const& Entry = Parts[Index];
        if (Entry.bUsed != 0 && Entry.Name != nullptr && _stricmp(Entry.Name, PartName) == 0 &&
            Entry.Part != -1)
            return Entry.Part;
        if (Unused < 0 && Entry.bUsed == 0)
            Unused = Index;
    }
    if (Unused < 0)
        Unused = 0;

    long const Part = CompoundInstanceFindFirstName(
        GetEngineInstance(), PartName);
    if (Part != -1)
    {
        FPartCacheEntry& Entry = Parts[Unused];
        Entry.Part = Part;
        if (Entry.Name != nullptr)
        {
            std::free(Entry.Name);
            Entry.Name = nullptr;
        }
        Entry.Name = _strdup(PartName);
        Entry.bUsed = 1;
    }
    return Part;
}

void FCharacterObject::AddMotionHandle(int Handle)
{
    // FUN_004456e0.
    if (Handle != -1)
        ++ActiveMotions;
}

void FCharacterObject::RemoveMotionHandle(int Handle)
{
    // FUN_004456f0.
    if (Handle != -1)
        --ActiveMotions;
}

void FCharacterObject::AddAimHandle(void* Handle)
{
    // FUN_00445700.
    if (reinterpret_cast<intptr_t>(Handle) != -1)
        ++ActiveAims;
}

void FCharacterObject::RemoveAimHandle(void* Handle)
{
    // FUN_00445710.
    if (reinterpret_cast<intptr_t>(Handle) != -1)
        --ActiveAims;
}

char const* FCharacterObject::ResolveBgcsContextToken(char const* TemplateName) const
{
    // FUN_00445720. Every exit returns DAT_00668848. Retail's Universe::IBase calls go through
    // its vtable (+0x04 nickname, +0x14 BGCS_base_run_by); the port's IBase publishes the same
    // two strings as fields.

    // DAT_005cbb64 / DAT_005cbb5c.
    int const Gender = GetCostumeDescriptions()->get_costume_gender(Props.Cost);
    if (Gender == 1)
        strcpy(G_BgcsContextBuffer, "gcs_ml_");
    else if (Gender == 2)
        strcpy(G_BgcsContextBuffer, "gcs_fm_");
    else
    {
        G_BgcsContextBuffer[0] = '\0';
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kObjectDeformableCpp, 0x51f,
              "Invalid gender for character, cannot find GCS voice profile message name\n");
    }

    unsigned const BaseId = GetRoomRuntimeBaseId(Owner); // [[this+8]+0x34]

    if (_stricmp(TemplateName, "$BGCS_Base_Name") == 0)
    {
        // A trailing "_base" is dropped when the whole buffer is longer.
        strcat(G_BgcsContextBuffer, "W02aF_");
        strcat(G_BgcsContextBuffer, Universe::get_base(BaseId)->Nickname);
        int const Length = static_cast<int>(strlen(G_BgcsContextBuffer));
        int const SuffixLength = static_cast<int>(strlen("_base"));
        if (Length <= SuffixLength)
            return G_BgcsContextBuffer;
        char* const Suffix = G_BgcsContextBuffer + (Length - SuffixLength);
        if (_stricmp(Suffix, "_base") != 0)
            return G_BgcsContextBuffer;
        *Suffix = '\0';
        return G_BgcsContextBuffer;
    }

    if (_stricmp(TemplateName, "$BGCS_Base_Run_By") == 0)
    {
        // An empty BGCS_base_run_by is reported with a second get_base for the nickname.
        char const* const RunBy = Universe::get_base(BaseId)->GetBgcsBaseRunBy();
        strcat(G_BgcsContextBuffer, RunBy);
        if (RunBy[0] != '\0')
            return G_BgcsContextBuffer;
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: No BGCS_base_run_by specified for base %s, cannot resolve "
              "voice profile message %s\n",
              kObjectDeformableCpp, 0x54a, Universe::get_base(BaseId)->Nickname, TemplateName);
        return G_BgcsContextBuffer;
    }

    // DAT_005cba3c / DAT_005cba44.
    char const* Prefix = nullptr;
    if (_stricmp(TemplateName, "$BGCS_My_Faction_ML") == 0 ||
        _stricmp(TemplateName, "$BGCS_My_Faction_FM") == 0)
    {
        Prefix = "D06aG_";
    }
    else if (_stricmp(TemplateName, "$BGCS_Faction_Rebuff_ML") == 0 ||
             _stricmp(TemplateName, "$BGCS_Faction_Rebuff_FM") == 0)
    {
        Prefix = "A03aG_";
    }
    else
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Unknown GCS generic voice profile message %s\n",
              kObjectDeformableCpp, 0x571, TemplateName);
        return G_BgcsContextBuffer;
    }

    // The faction nickname of +0x164.
    strcat(G_BgcsContextBuffer, Prefix);
    TString<16> Nickname;
    Nickname.Length = 0;
    Nickname.Data[0] = '\0';
    Reputation::get_nickname(Nickname, Props.FactionId);
    strcat(G_BgcsContextBuffer, Nickname.Data);
    return G_BgcsContextBuffer;
}

// ============================================================================================
// Free functions
// ============================================================================================

void ReleaseSharedCharacterLod()
{
    // FUN_00443e90.
    if (G_SharedCharacterLod.Instance != nullptr)
        DALib::Deform->Destroy(G_SharedCharacterLod.Instance);
    G_SharedCharacterLod.Instance = nullptr;
    int Index = 0;
    for (Index = 0; Index < 4; ++Index)
    {
        G_SharedCharacterLod.Counts[Index] = 0;
        G_SharedCharacterLod.Tables[Index] = nullptr;
    }
}

FCharacterObject* CreateCharacterObject(CharacterProperties const& Props, CharPlaceInfo* Placement,
                                        int PlacementIndex, FRoomRuntime* Owner,
                                        unsigned char bScriptSpatial, Csys const& Frame)
{
    // FUN_00435b80.
    FCharacterDesc Desc;
    Desc.Props = Props;
    Desc.Placement = Placement;
    Desc.PlacementIndex = PlacementIndex;
    Desc.bScriptSpatial = bScriptSpatial;
    Desc.Pos = Frame.Pos;
    Desc.Orient = Frame.Orient;
    // operator new(0x3b0); a failed allocation gives null.
    FCharacterObject* const Character = new (std::nothrow) FCharacterObject(Owner, Desc);
    return Character;
}

void StartCharacterBehaviorScript(FCharacterObject* Character, FRoomRuntime* Room,
                                  CharPlaceInfo const* Placement, unsigned BehaviorKey,
                                  void(__cdecl* Callback)(void*, void*, unsigned char),
                                  void* CallbackA)
{
    // FUN_00435dd0. The lookup is FUN_0043ad50 + FUN_00437020.
    unsigned const RoomId = (Room != nullptr) ? GetRoomRuntimeRoomId(Room) : 0;
    char const* const PlacementName = (Placement != nullptr) ? Placement->Name : nullptr;
    MetaNode<ScriptBehavior>* const Node =
        FindBehaviorNode(GetClientLocationRoomScriptBehaviors(RoomId), BehaviorKey);
    if (Node == nullptr)
        return;
    ScriptBehavior const* const Behavior = Node->Data;
    if (Behavior == nullptr)
        return;

    FGfScriptSequence* const Sequence = G_GfScriptManager.CreateSequence(Room, 0);
    if (Sequence == nullptr)
        return;

    // Each of the behaviour's scripts with a file, at the identity.
    Csys Frame;
    Frame.Pos.x = 0.0f;
    Frame.Pos.y = 0.0f;
    Frame.Pos.z = 0.0f;
    MatrixSetIdentity(Frame.Orient);
    int Index = 0;
    for (Index = 0; Index < Behavior->StringArray2Count; ++Index)
    {
        char const* const File = Behavior->StringArray2[Index];
        if (File == nullptr || File[0] == '\0')
            continue;
        Sequence->AddScript(File, static_cast<int>(Behavior->StringArray2Dword[Index]), &Frame,
                            nullptr);
    }

    Sequence->SetDialog(static_cast<int>(Behavior->DialogType), static_cast<int>(Behavior->OfferId),
                        static_cast<int>(Behavior->OfferKind), Behavior->NameFmtStr,
                        Behavior->DescFmtStr);

    // +0x14 set -- this character alone, bound as the mirror's first;
    // clear -- the listed characters by key under their names, bound by name.
    int BindMode = 0;
    if (Behavior->bBindSingleCharacter != 0)
    {
        Sequence->Mirror.Add(Character, nullptr);
        BindMode = 1;
    }
    else
    {
        BindMode = 2;
        for (Index = 0; Index < Behavior->IdArrayCount; ++Index)
        {
            char const* const Name = Behavior->StringArray[Index];
            FCharacterObject* const Listed =
                RoomControlFindCharacterByKey(Room, Behavior->IdArray[Index]);
            Sequence->Mirror.Add(Listed, Name);
        }
    }

    // A dealer role's script (trader, ship dealer, equipment dealer) counts as a transition.
    Sequence->BindMode = BindMode;
    Sequence->bLetterbox = 1;
    Sequence->bApplySpatial = Behavior->bApplySpatial;
    Sequence->SetName(PlacementName);
    Sequence->Priority = 0;
    Sequence->Callback = Callback;
    Sequence->CallbackB = nullptr;
    Sequence->bDuckAmbient = 1;
    Sequence->bDuckMusic = 1;
    Sequence->CallbackA = CallbackA;
    Sequence->Location = Behavior->RelocateLocationId;
    Sequence->bSkippable = Behavior->bGenericScript;
    Sequence->RtcId = Behavior->RtcId;
    int const Role = Character->Role;
    Sequence->bTransition = (Role == kCharacterRoleTrader || Role == kCharacterRoleShipDealer ||
                             Role == kCharacterRoleEquipmentDealer)
                                ? 1
                                : 0;
    Sequence->Play();
}

void PlacePlayerAtSetpoint(char const* SetpointName)
{
    // FUN_00435cf0. The room runtime (FUN_0043a460) and the RoomData (FUN_0043a3d0) are not
    // null-tested.
    FRoomRuntime* const Runtime = static_cast<FRoomRuntime*>(GetClientCurrentLocationRuntime());
    RoomData* const Room = LookupClientLocationRoom(GetClientLocationCurrentRoom());

    // The setpoint vector (+0x164) size is re-read every pass.
    SetpointInfo const* Found = nullptr;
    size_t Index = 0;
    for (Index = 0; Index < Room->Setpoints.size(); ++Index)
    {
        SetpointInfo const* const Setpoint = Room->Setpoints[Index];
        if (_stricmp(Setpoint->Name, SetpointName) == 0) // DACOM stricmp
        {
            Found = Setpoint;
            break;
        }
    }
    if (Found == nullptr)
        return;

    Vector Pos = Found->Frame.Pos;
    Matrix Orient = Found->Frame.Orient;

    // FUN_00449cd0.
    FCharacterObject* const Player = RoomControlPlayerCharacter(Runtime);
    if (Player == nullptr)
        return;
    void* const Instance = Player->Lod.Instance;
    DALib::Deform->SetPosePos(Instance, &Pos.x);
    DALib::Deform->SetPoseOri(Instance, &Orient);
    Player->SetFloorHeight(Pos.y);
}
