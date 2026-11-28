// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/room_control.h"

#include "alchemy.h"
#include "GameFlow/anim_script_set.h"
#include "Interface/Pages/character_select_equip.h" // GetPlayerShipArchetypeIdRetail / GetPlayerEquipDescListRetail
#include "NeuroNet/Economy/client_base_market.h"     // FClientMarket, GetClientLocationBaseMarket (FUN_0043a510)
#include "Common/Graphics/fade_to_black.h"          // SetFadeImmediate (FUN_00412070), SetFadeTarget (FUN_00412050)
#include "Space/Effects/flash_light.h"
#include "GameFlow/gameflow.h"               // HoldGameflowTime / ClearHoveredHotspot / UnlockHotspotInput
#include "Common/input_manager.h"          // MouseSetCursor (FUN_0041dde0), SetClientMouseCursorDrawFlag
#include "Space/space_letterbox.h"        // SetLetterboxTarget (FUN_0041a0f0)
#include "GameFlow/object_prop.h"
#include "Core/game_options.h"
#include "Space/player_spawn.h"       // GetPlayerConnectionId (DAT_00673344)
#include "Network/server_connect.h"     // GetLocalServerInterface (DAT_0067ecd0)
#include "GameFlow/Thorn/gf_script.h"
#include "GameFlow/gf_character_object.h"
#include "GameFlow/gf_object_ship.h"
#include "GameFlow/Thorn/gf_script_object.h"
#include "GameFlow/menu_universe_base.h"
#include "GameFlow/mirror_property_bucket.h"
#include "NeuroNet/nn_main_button.h"     // G_bNnKeepPanelOpen (DAT_0066d359)
#include "GameFlow/room_camera.h"
#include "GameFlow/room_control_hotspot.h" // RoomButton family (Client\Gf\RoomControlHotspot.cpp)
#include "Common/client_cliobj.h"
#include "Common/engine_light.h"
#include "Core/perf_options.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win3d_render.h"

#include "DALib.h"
#include "common_archetype.h"   // Archetype::GetShip
#include "common_csys.h"
#include "x86math.h"            // I3DMathEngine (the room camera move's quaternions)
#include "common_goods.h"
#include "common_id.h"          // MakeLocationID (FUN_0043a4f0's callee)
#include "common_iface.h"
#include "common_misc.h"
#include "common_newchar.h"
#include "common_server.h"      // IServer
#include "dacom.h"
#include "engine/engbase.h"
#include "render/IFLAppearance.h"
#include "render/IRenderPipeline.h"
#include "render/rendcomp.h"
#include "render/shading.h"

#include <list>
#include <math.h>
#include <new>
#include <set>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

namespace
{
    // .rdata 0x005cbf24 -- the source path RoomControl.cpp hands FDUMP and FUN_00422610.
    char const kRoomControlCpp[] = "E:\\FL\\Scratch\\Source\\Client\\gf\\RoomControl.cpp";

    // Inlined in FUN_004496b0: FUN_00447bb0 packs RoomData's camera frame (+0x1d8 orientation,
    // +0x1cc position) into one transform for FUN_00556cd0, then FUN_00556ce0 / 556e00 / 556f10
    // take +0x1fc / +0x200 / +0x204 as they are.
    void InstallRoomDataCamera(FRoomCamera* Camera, RoomCameraInfo const& Info)
    {
        float Transform[12];
        memcpy(Transform, &Info.Frame.Orient, sizeof(float) * 9);
        memcpy(Transform + 9, &Info.Frame.Pos, sizeof(float) * 3);
        RoomCameraSetTransform(Camera, Transform);
        RoomCameraSetHalfFovX(Camera, Info.Fov);
        RoomCameraSetZNear(Camera, Info.NearPlane);
        RoomCameraSetZFar(Camera, Info.FarPlane);
    }

}

static_assert(offsetof(FRoomObject, bNoUpdate) == 0x04, "RoomObject +0x04");
static_assert(offsetof(FRoomObject, Owner) == 0x08, "RoomObject owner +0x08");
static_assert(offsetof(FRoomObject, AmbientSource) == 0x0C, "RoomObject +0x0c");
static_assert(offsetof(FRoomObject, bVisible) == 0x10, "RoomObject +0x10");
static_assert(offsetof(FRoomObject, Layer) == 0x16, "RoomObject layer +0x16");
static_assert(offsetof(FRoomObject, RenderMode) == 0x18, "RoomObject +0x18");
static_assert(offsetof(FRoomObject, FloorHeight) == 0x1C, "RoomObject +0x1c");
static_assert(offsetof(FRoomObject, MaterialEntries) == 0x20, "RoomObject vector +0x20");

// The offsets below are retail's. They hold when std::vector is 0x10 bytes (the iterator-debug
// builds, like retail's VC6 vector with its allocator byte); a release build's 0x0c-byte vector
// shifts everything past +0x2c by four, which nothing in the port reads by raw offset.
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
static_assert(sizeof(FRoomObject) == 0x30, "RoomObject sizeof 0x30");
static_assert(offsetof(FRoomRuntime, BaseId) == 0x34, "FRoomRuntime +0x34");
static_assert(offsetof(FRoomRuntime, MainInstance) == 0x44, "FRoomRuntime +0x44");
static_assert(offsetof(FRoomRuntime, MainScript) == 0x48, "FRoomRuntime +0x48");
static_assert(offsetof(FRoomRuntime, Scripts) == 0x4C, "FRoomRuntime +0x4C");
static_assert(offsetof(FRoomRuntime, Handles64) == 0x64, "FRoomRuntime +0x64");
static_assert(offsetof(FRoomRuntime, Room) == 0x40, "FRoomRuntime +0x40");
static_assert(offsetof(FRoomRuntime, RoomLights) == 0xE4, "FRoomRuntime +0xE4");
static_assert(offsetof(FRoomRuntime, RoomLightCount) == 0x2E4, "FRoomRuntime +0x2E4");
static_assert(offsetof(FRoomRuntime, FrameLights) == 0x2E8, "FRoomRuntime +0x2E8");
static_assert(offsetof(FRoomRuntime, FrameLightCount) == 0x4E8, "FRoomRuntime +0x4E8");
static_assert(offsetof(FRoomRuntime, FrameAmbient) == 0x4EC, "FRoomRuntime +0x4EC");
static_assert(offsetof(FRoomRuntime, SingleLightIndex) == 0x4F8, "FRoomRuntime +0x4F8");
static_assert(offsetof(FRoomRuntime, bSingleLight) == 0x4FC, "FRoomRuntime +0x4FC");
static_assert(offsetof(FRoomRuntime, Camera) == 0x500, "FRoomRuntime Camera +0x500");
static_assert(offsetof(FRoomRuntime, Placements) == 0x504, "FRoomRuntime +0x504");
static_assert(offsetof(FRoomRuntime, SceneFlags) == 0x514, "FRoomRuntime +0x514");
static_assert(offsetof(FRoomRuntime, Characters) == 0x518, "FRoomRuntime +0x518");
static_assert(offsetof(FRoomRuntime, Props) == 0x618, "FRoomRuntime +0x618");
static_assert(offsetof(FRoomRuntime, PropCount) == 0x818, "FRoomRuntime +0x818");
static_assert(offsetof(FRoomRuntime, Flashlights) == 0x81C, "FRoomRuntime +0x81C");
static_assert(offsetof(FRoomRuntime, FlashlightCount) == 0x101C, "FRoomRuntime +0x101C");
static_assert(offsetof(FRoomRuntime, ShipPads) == 0x1020, "FRoomRuntime +0x1020");
static_assert(offsetof(FRoomRuntime, SceneAmbient) == 0x1038, "FRoomRuntime +0x1038");
static_assert(offsetof(FRoomRuntime, SceneTraffic) == 0x108C, "FRoomRuntime +0x108C");
static_assert(offsetof(FRoomRuntime, SceneOnce) == 0x10E0, "FRoomRuntime +0x10E0");
static_assert(offsetof(FRoomRuntime, SceneOnceCount) == 0x1130, "FRoomRuntime +0x1130");
static_assert(offsetof(FRoomRuntime, ControllerCount) == 0x1334, "FRoomRuntime +0x1334");
static_assert(offsetof(FRoomRuntime, bCameraMoving) == 0x1338, "FRoomRuntime +0x1338");
static_assert(offsetof(FRoomRuntime, bCameraMoveSkippable) == 0x1340, "FRoomRuntime +0x1340");
static_assert(offsetof(FRoomRuntime, CameraToQuat) == 0x1348, "FRoomRuntime +0x1348");
static_assert(offsetof(FRoomRuntime, CameraFromQuat) == 0x1370, "FRoomRuntime +0x1370");
static_assert(offsetof(FRoomRuntime, CameraDone) == 0x1398, "FRoomRuntime +0x1398");
static_assert(offsetof(FRoomRuntime, TrackCameraIndex) == 0x13a4, "FRoomRuntime +0x13a4");
static_assert(sizeof(FRoomRuntime) == 0x13AC, "FRoomRuntime sizeof 0x13AC");
#endif

// DAT_00679a10 (list head DAT_00679a14, size DAT_00679a18) -- every RoomObject, in
// construction order.
namespace
{
    std::list<FRoomObject*> G_RoomObjects;
}

std::list<FRoomObject*>& GetRoomObjectList()
{
    // FUN_005593c0.
    return G_RoomObjects;
}

FRoomObject::FRoomObject(FRoomRuntime* InOwner)
    : bNoUpdate(0),
      Owner(InOwner),
      AmbientSource(nullptr),
      bVisible(1),
      bLit(1),
      bFog(1),
      bEnvMap(0),
      bRunningLights(0),
      LightGroup(0),
      Layer(0),
      RenderMode(kRoomRenderMesh),
      FloorHeight(0.0f),
      MaterialEntries()
{
    // FUN_005593d0 (the vector's allocator byte at +0x20 takes the argument's low byte, which
    // only VC6's allocator stored); then appended to DAT_00679a10.
    G_RoomObjects.push_back(this);
}

FRoomObject::~FRoomObject()
{
    // FUN_005594b0. The size is re-read each pass.
    size_t Index = 0;
    for (Index = 0; Index < MaterialEntries.size(); ++Index)
        Archetype::ReleaseMaterialEntrySlot(&MaterialEntries[Index]);
    MaterialEntries.clear();

    std::list<FRoomObject*>::iterator It = G_RoomObjects.begin();
    for (; It != G_RoomObjects.end(); ++It)
    {
        if (*It == this)
        {
            G_RoomObjects.erase(It);
            break;
        }
    }
}

bool FRoomObject::Slot1()
{
    // FUN_0044fd00.
    return false;
}

bool FRoomObject::IsVisible()
{
    // LAB_00444050.
    return bVisible != 0;
}

void FRoomObject::SetFloorHeight(float Value)
{
    // LAB_00443850.
    FloorHeight = Value;
}

float FRoomObject::GetFloorHeight()
{
    // LAB_00443860.
    return FloorHeight;
}

InstanceIndex FRoomObject::GetEngineInstance()
{
    // FUN_00450870.
    return kRoomObjectNoInstance;
}

void FRoomObject::DrawOpaque(ICamera* /*Camera*/)
{
    // 0x0053e4a0 (ICF-folded empty body, IDPMsgHandler::OnAddHost).
}

void FRoomObject::DrawTranslucent(ICamera* /*Camera*/)
{
    // 0x0053e4a0.
}

void FRoomObject::DrawEffects(ICamera* /*Camera*/)
{
    // 0x0053e4a0.
}

void FRoomObject::OnHotspotEnter(unsigned long /*Cookie*/, int const* /*Point*/)
{
    // 0x0053aaa0 (ICF-folded empty body, IDPMsgHandler::OnReceive).
}

void FRoomObject::OnHotspotLeave(unsigned long /*Cookie*/, int const* /*Point*/)
{
    // 0x0053aaa0.
}

void FRoomObject::OnHotspotClick(unsigned long /*Cookie*/, int const* /*Point*/)
{
    // 0x0053aaa0.
}

// ---------------------------------------------------------------------------
// The character placements (+0x504) and the characters (+0x518).
// ---------------------------------------------------------------------------

// FUN_004356d0 (thiscall on the vector, RET 4) -- the room's placements whose posture is not 3
// (special), counted, the vector grown to hold them, then each appended {placement, null}.
static void BuildCharacterPlacements(std::vector<FRoomCharPlacement>& Placements,
                                     std::vector<CharPlaceInfo*> const& Source)
{
    int Wanted = 0;
    int Index = 0;
    for (Index = 0; Index < static_cast<int>(Source.size()); ++Index)
    {
        if (Source[Index]->Posture != kSetpointPostureSpecial)
            ++Wanted;
    }
    // Retail reallocates to exactly that many.
    if (Placements.capacity() < static_cast<size_t>(Wanted))
        Placements.reserve(static_cast<size_t>(Wanted));
    // The grow paths FUN_004363b0 / FUN_00436bb0 are vector::insert's.
    for (Index = 0; Index < static_cast<int>(Source.size()); ++Index)
    {
        CharPlaceInfo* const Info = Source[Index];
        if (Info->Posture == kSetpointPostureSpecial)
            continue;
        FRoomCharPlacement Placement;
        Placement.Info = Info;
        Placement.Character = nullptr;
        Placements.push_back(Placement);
    }
}

// FUN_00435b40 (cdecl) -- a costume worth drawing: any of the four part ids, or accessories.
static bool CostumeHasParts(Costume const& Cost)
{
    bool bHas = false;
    if (Cost.PartIds[0] != 0)
        bHas = true;
    if (Cost.PartIds[1] != 0)
        bHas = true;
    if (Cost.PartIds[2] != 0)
        bHas = true;
    if (Cost.PartIds[3] != 0)
        bHas = true;
    if (static_cast<int>(Cost.AccessoryCount) > 0)
        bHas = true;
    return bHas;
}

// FUN_00435960 (thiscall on the vector, RET 8) -- a character for Props on a free placement:
// the one named by the record (+0x18), else for a pc / rpc record (a foreign session makes it
// rpc, 2) the first of that char type (+0x34) and posture (+0x38 == record +0x20); a free
// placement found -> FUN_00435b80 with it (bScriptSpatial 0), stored in the element. A record
// of another kind gets a character with no placement (FUN_00435a5c: index -1, identity frame,
// bScriptSpatial 1). No placement found -> null.
static FCharacterObject* CreatePlacedCharacter(std::vector<FRoomCharPlacement>& Placements,
                                               CharacterProperties const& Props,
                                               FRoomRuntime* Room)
{
    int CharType = static_cast<int>(Props.CharType);
    if (Props.SessionId != 0 && Props.SessionId != GetPlayerConnectionId())
        CharType = kSetpointCharRpc;

    int Found = -1;
    int Index = 0;
    if (Props.NameAnsi != nullptr && Props.NameAnsi[0] != '\0')
    {
        for (Index = 0; Index < static_cast<int>(Placements.size()); ++Index)
        {
            if (Placements[Index].Character != nullptr)
                continue;
            if (Placements[Index].Info->is_named(Props.NameAnsi))
            {
                Found = Index;
                break;
            }
        }
    }
    else if (CharType == kSetpointCharPc || CharType == kSetpointCharRpc)
    {
        for (Index = 0; Index < static_cast<int>(Placements.size()); ++Index)
        {
            if (Placements[Index].Character != nullptr)
                continue;
            CharPlaceInfo const* const Info = Placements[Index].Info;
            if (CharType == Info->CharType && static_cast<int>(Props.Posture) == Info->Posture)
            {
                Found = Index;
                break;
            }
        }
    }
    else
    {
        // FUN_00435a5c (a tail jump): no placement, the identity frame.
        Csys Frame;
        Frame.Pos.x = 0.0f;
        Frame.Pos.y = 0.0f;
        Frame.Pos.z = 0.0f;
        MatrixSetIdentity(Frame.Orient);
        return CreateCharacterObject(Props, nullptr, -1, Room, 1, Frame);
    }

    // The -1 test is retail's own.
    if (Found == -1)
        return nullptr;
    CharPlaceInfo* const Info = Placements[Found].Info;
    Csys const Frame = Info->Frame;
    FCharacterObject* const Character = CreateCharacterObject(Props, Info, Found, Room, 0, Frame);
    Placements[Found].Character = Character;
    return Character;
}

// FUN_00449e10 (thiscall, RET 4) -- character Slot deleted: its placement freed
// (FUN_00435b20 on +0x504), the object through its deleting destructor, the slot cleared.

// FUN_0044ceb0 (thiscall) -- the +0x1134 buttons, +0x1334 and +0x13a8 zeroed. Caller: the ctor
// FUN_00449260.
void FRoomRuntime::ClearButtons()
{
    int Index = 0;
    for (Index = 0; Index < 0x80; ++Index)
        Controllers[Index] = nullptr;
    ControllerCount = 0;
    Field_0x13a8 = 0;
}

// FUN_0044d1c0 (thiscall) -- every non-null +0x1134 button deleted (slot 0 with 1) and its slot
// cleared; the count (+0x1334, re-read each pass) is left as it is. Caller: the dtor
// FUN_00449420.
void FRoomRuntime::DeleteButtons()
{
    int Index = 0;
    for (Index = 0; Index < ControllerCount; ++Index)
    {
        if (Controllers[Index] != nullptr)
        {
            delete Controllers[Index];
            Controllers[Index] = nullptr;
        }
    }
}

FRoomRuntime::FRoomRuntime()
    : FRoomObject(nullptr)
{
    // FUN_00449260. The RoomObject base with a null owner; FUN_00555a40 / FUN_0059ec40 (the
    // Placements member) / FUN_004488d0.
    Scripts.Init();
    ShipPads.Init();

    // +0x44 / +0x48 are not written here; the zero-filled block CreateRoomRuntime hands in leaves
    // them 0 until FUN_004496b0 sets -1.
    BaseId = 0;
    RoomId = 0;
    LocationBase = nullptr;
    Room = nullptr;

    unsigned Index = 0;
    for (Index = 0; Index < 0x20; ++Index)
        Handles64[Index] = 0xffffffffu;

    RoomLightCount = 0;
    for (Index = 0; Index < 0x80; ++Index)
        RoomLights[Index] = nullptr;
    FrameLightCount = 0;
    for (Index = 0; Index < 0x80; ++Index)
        FrameLights[Index] = nullptr;

    FrameAmbient[0] = 0.0f;
    FrameAmbient[1] = 0.0f;
    FrameAmbient[2] = 0.0f;
    GfFogReset(); // FUN_004546d0

    SingleLightIndex = 0;
    bSingleLight = 0;
    Camera = nullptr;
    SceneFlags[kRoomStartScriptChecked] = 0;
    SceneFlags[kRoomEntryScriptStarted] = 0;
    SceneFlags[kRoomStartScriptRequested] = 0;
    SceneFlags[kRoomStartScriptRan] = 0;
    for (Index = 0; Index < 0x40; ++Index)
        Characters[Index] = nullptr;
    PropCount = 0;
    for (Index = 0; Index < 0x80; ++Index)
        Props[Index] = nullptr;
    for (Index = 0; Index < 0x200; ++Index)
        Flashlights[Index] = nullptr;
    FlashlightCount = 0;

    ClearButtons(); // FUN_0044ceb0

    // One index of each array per pass, as retail.
    for (Index = 0; Index < 0x10; ++Index)
    {
        SceneAmbient[Index] = nullptr;
        SceneTraffic[Index] = nullptr;
        SceneOnce[Index] = nullptr;
        SceneAmbientFlag[Index] = 1;
        SceneTrafficFlag[Index] = 1;
        SceneOnceFlag[Index] = 1;
    }
    SceneAmbientCount = 0;
    SceneTrafficCount = 0;
    SceneOnceCount = 0;
    bCameraMoving = 0;
    TrackCameraIndex = 0;
    bCameraMoveSkippable = 0;
}

FRoomRuntime::~FRoomRuntime()
{
    // FUN_00449420. Every script sequence of any room is deleted (FUN_00456930). The scene
    // counts are re-read each pass and never written.
    G_GfScriptManager.DestroyAll();
    int SceneIndex = 0;
    for (SceneIndex = 0; SceneIndex < SceneAmbientCount; ++SceneIndex)
        malloc_strfree(&SceneAmbient[SceneIndex]);
    for (SceneIndex = 0; SceneIndex < SceneTrafficCount; ++SceneIndex)
        malloc_strfree(&SceneTraffic[SceneIndex]);
    for (SceneIndex = 0; SceneIndex < SceneOnceCount; ++SceneIndex)
        malloc_strfree(&SceneOnce[SceneIndex]);

    DeleteButtons(); // FUN_0044d1c0

    // Only with RoomData+0x1a4 (the FlashLightSet records) not empty.
    if (!Room->FlashLightSets.empty())
    {
        int FlashIndex = 0;
        for (FlashIndex = 0; FlashIndex < FlashlightCount; ++FlashIndex)
        {
            if (Flashlights[FlashIndex] != nullptr)
                delete Flashlights[FlashIndex];
            Flashlights[FlashIndex] = nullptr;
        }
        FlashlightCount = 0;
    }

    // Deleting the room's main prop first sets +0x58 (the script table's +0x0c) and +0x48 to -1, because the
    // prop's own destructor closes that script.
    int PropIndex = 0;
    for (PropIndex = 0; PropIndex < PropCount; ++PropIndex)
    {
        if (Props[PropIndex]->bRoomMain == 1)
        {
            Scripts.Script = 0xffffffffu;
            MainScript = 0xffffffffu;
        }
        if (Props[PropIndex] != nullptr)
            delete Props[PropIndex];
        Props[PropIndex] = nullptr;
    }
    PropCount = 0;

    int CharacterSlot = 0;
    for (CharacterSlot = 0; CharacterSlot < 0x40; ++CharacterSlot)
        DeleteCharacter(CharacterSlot);

    RoomCameraDestroy(Camera);
    Camera = nullptr;
    int LightIndex = 0;
    for (LightIndex = 0; LightIndex < RoomLightCount; ++LightIndex)
        GfLightWrapperDestroy(&RoomLights[LightIndex]);
    RoomLightCount = 0;
    FrameLightCount = 0;

    int AnimIndex = 0;
    for (AnimIndex = 0; AnimIndex < Room->AnimationCount; ++AnimIndex)
        AnimScriptSetStopPlayback(&Handles64[AnimIndex]); // FUN_00555ca0

    Scripts.Destroy();
    MainInstance = kRoomObjectNoInstance;
    MainScript = 0xffffffffu;
    Room->unfixup();

    SetFadeImmediate(1.0f); // FUN_00412070

    // Then, as retail: the ShipPads member's destructor FUN_004488f0 first, the Placements
    // member's FUN_004de730, the +0x4c member's FUN_00555a90 (a second pass over the emptied
    // table), ~EventHandler FUN_00576010, and ~FRoomObject FUN_005594b0.
}

// FUN_0044a030 (thiscall, no arguments) -- one Gf::ObjectProp per RoomData+0x158 record whose
// priority the RtcSlider enables, its RoomObject flags copied from the record; the record
// marked as the room's main prop hands its engine instance and main script to +0x44 / +0x48.
void FRoomRuntime::LoadProps()
{
    PropCount = 0;
    unsigned Index = 0;
    for (Index = 0; Index < Room->Props.size(); ++Index)
    {
        RoomPropInfo const* Record = Room->Props[Index];
        if (!RtcSlider::is_enabled(static_cast<unsigned long>(Record->PriorityId)))
            continue;

        FPropData Data;
        Data.Template = Record->Template;
        Data.Name = Record->Name;
        Data.Type = Record->Category;
        Data.Pos = Record->Frame.Pos;
        Data.Orient = Record->Frame.Orient;
        Data.Loadout = Record->Loadout;

        FObjectProp* Prop = new FObjectProp(this, Data); // operator new(0x80)
        Props[PropCount] = Prop;
        Prop->bVisible = Record->bVisible ? 1 : 0;
        Prop->bLit = Record->bLit ? 1 : 0;
        Prop->bFog = Record->bFog ? 1 : 0;
        Prop->bRoomMain = Record->bRoomMain ? 1 : 0;
        Prop->LightGroup = Record->LightGroup;
        Prop->Layer = Record->Layer;
        Prop->RenderMode = static_cast<int>(Record->RenderType);
        Prop->bRunningLights = Record->bRunningLights ? 1 : 0;
        if (Record->bRoomMain)
        {
            MainInstance = Prop->GetEngineInstance();
            MainScript = Prop->GetMainScript();
        }
        ++PropCount;
    }
}

FObjectProp* FRoomRuntime::FindProp(char const* Name)
{
    // FUN_0044a2f0: the first prop whose +0x38 name (null -> "") equals Name byte for byte.
    int Index = 0;
    for (Index = 0; Index < PropCount; ++Index)
    {
        if (strcmp(Props[Index]->GetName(), Name) == 0)
            return Props[Index];
    }
    return nullptr;
}

void FRoomRuntime::AddProp(FObjectProp* Prop)
{
    // FUN_0044a380: no bound check.
    Props[PropCount] = Prop;
    ++PropCount;
}

unsigned GetRoomRuntimeBaseId(FRoomRuntime* Runtime)
{
    // +0x34, read directly by FUN_00446280 through the prop's owner.
    return Runtime->BaseId;
}

RoomData* GetRoomRuntimeRoomData(FRoomRuntime* Runtime)
{
    // +0x40, read directly by the Gf script spawn (Script+0x30 -> +0x40).
    return Runtime->Room;
}

unsigned char FRoomRuntime::IsCameraMoving()
{
    // FUN_0044c4a0.
    return bCameraMoving;
}

void FRoomRuntime::ClearCameraMoving()
{
    // FUN_0044c490. G_bNnKeepPanelOpen is DAT_0066d359.
    bCameraMoving = 0;
    G_bNnKeepPanelOpen = 0;
}

void RoomControlSetCameraMoveSkippable(FRoomRuntime* Runtime, unsigned char Value)
{
    // Inlined in the NavBar.
    Runtime->bCameraMoveSkippable = Value;
}

void FRoomRuntime::NotifyControllers(int /*A*/, int B, int C)
{
    // FUN_0044d240.
    if (C == -1)
        return;
    if (B != 3)
        return;
    // The count is re-read each pass and the entries are not null-tested.
    int Index = 0;
    for (Index = 0; Index < ControllerCount; ++Index)
    {
        RoomButton* const Button = Controllers[Index];
        if (static_cast<int>(Button->Location) == C || Button->Behavior == C)
            Button->Activate();
    }
}

namespace
{
    // DAT_00668ac0 -- the room camera move's lazily acquired 3DMathEngine (FUN_005b73e0 on
    // first use, at every use site).
    I3DMathEngine* G_RoomCameraMath = nullptr;

    I3DMathEngine* RoomCameraMath()
    {
        if (G_RoomCameraMath == nullptr)
            G_RoomCameraMath = Acquire3DMathEngine();
        return G_RoomCameraMath;
    }

    // The RoomData +0x138 camera named CameraName (DACOM stricmp(CameraName, info +0x00), the
    // count (+0x13c - +0x138) / 4, 0 for a null vector), or null -- the loop FUN_0044c290 and
    // FUN_0044ba60 both inline.
    RoomCameraInfo const* FindRoomDataCamera(FRoomRuntime* Runtime, char const* CameraName)
    {
        std::vector<RoomCameraInfo*> const& Cameras = Runtime->Room->Cameras;
        int const Count = static_cast<int>(Cameras.size());
        int Index = 0;
        for (Index = 0; Index < Count; ++Index)
        {
            RoomCameraInfo const* const Info = Cameras[Index];
            if (_stricmp(CameraName, Info->Name) == 0)
                return Info;
        }
        return nullptr;
    }

    // The part FUN_0044c290 and FUN_0044ba60 share: the destination from the RoomData camera (orientation as a quaternion through
    // slot 0x50, position +0x04, half fov / near / far +0x34 / +0x38 / +0x3c) and the start
    // from the +0x500 camera (slot 4 orientation -> slot 0x50, slot 3 position, slots 6 / 8 / 9).
    // The start's far plane is returned; FUN_0044c290 stores it at +0x1394 at once, FUN_0044ba60
    // after the callback's three arguments are read.
    float SetCameraMoveEnds(FRoomRuntime* Runtime, RoomCameraInfo const* Info)
    {
        RoomCameraMath()->MatrixToQuaternion(Runtime->CameraToQuat,
                                             Info->Frame.Orient);
        memcpy(Runtime->CameraToPos, &Info->Frame.Pos, sizeof(Runtime->CameraToPos));
        Runtime->CameraToHalfFovX = Info->Fov;
        Runtime->CameraToZNear = Info->NearPlane;
        Runtime->CameraToZFar = Info->FarPlane;

        FRoomCamera* const Camera = Runtime->Camera;
        const float* const Orient = reinterpret_cast<const float*>(Camera->GetOrientation());
        RoomCameraMath()->MatrixToQuaternion(Runtime->CameraFromQuat,
                                             *reinterpret_cast<Matrix const*>(Orient));
        memcpy(Runtime->CameraFromPos, Camera->GetPosition(), sizeof(Runtime->CameraFromPos));
        Runtime->CameraFromHalfFovX = Camera->GetHalfFovXDeg();
        Runtime->CameraFromZNear = Camera->GetZNear();
        return Camera->GetZFar();
    }

    // FUN_00556cd0 on the +0x500 camera with {Orient, Pos} (orientation first, as retail packs
    // the 0x30-byte frame on its stack), then FUN_00556ce0 / 556e00 / 556f10.
    void ApplyRoomCameraFrame(FRoomRuntime* Runtime, Matrix const& Orient, float const* Pos,
                              float HalfFovX, float ZNear, float ZFar)
    {
        float Transform[12];
        memcpy(Transform, &Orient, sizeof(float) * 9);
        memcpy(Transform + 9, Pos, sizeof(float) * 3);
        RoomCameraSetTransform(Runtime->Camera, Transform);
        RoomCameraSetHalfFovX(Runtime->Camera, HalfFovX);
        RoomCameraSetZNear(Runtime->Camera, ZNear);
        RoomCameraSetZFar(Runtime->Camera, ZFar);
    }

    // |A - B| < Eps, unrounded; not-less or NaN fails.
    bool CameraMoveNear(float A, float B, double Eps)
    {
        return fabs(static_cast<double>(A) - static_cast<double>(B)) < Eps;
    }

    // DAT_005cbfc0 / DAT_005cbfb8 / DAT_005cbfb0 (doubles holding 1e-7f / 1e-4f / 1e-5f).
    double const kCameraPosEps = static_cast<double>(1e-7f);
    double const kCameraFovEps = static_cast<double>(1e-4f);
    double const kCameraQuatEps = static_cast<double>(1e-5f);

    // FUN_0044bd50 (thiscall, RET 4) -- true when a RoomData camera of that name exists and the
    // +0x500 camera already stands on it: the same ends and tolerances FUN_0044ba60 tests, on
    // stack copies (nothing in the runtime is written). No camera of that name -> false.
    bool IsRoomCameraAt(FRoomRuntime* Runtime, char const* CameraName)
    {
        RoomCameraInfo const* const Info = FindRoomDataCamera(Runtime, CameraName);
        if (Info == nullptr)
            return false;
        Quaternion ToQuat;
        RoomCameraMath()->MatrixToQuaternion(ToQuat,
                                             Info->Frame.Orient);
        FRoomCamera* const Camera = Runtime->Camera;
        Quaternion FromQuat;
        RoomCameraMath()->MatrixToQuaternion(FromQuat,
                                             *Camera->GetOrientation());
        float FromPos[3];
        memcpy(FromPos, Camera->GetPosition(), sizeof(FromPos));
        float const FromHalfFovX = Camera->GetHalfFovXDeg();
        float const FromZNear = Camera->GetZNear();
        float const FromZFar = Camera->GetZFar();
        return CameraMoveNear(Info->Frame.Pos.x, FromPos[0], kCameraPosEps) &&
               CameraMoveNear(Info->Frame.Pos.y, FromPos[1], kCameraPosEps) &&
               CameraMoveNear(Info->Frame.Pos.z, FromPos[2], kCameraPosEps) &&
               CameraMoveNear(Info->Fov, FromHalfFovX, kCameraFovEps) &&
               CameraMoveNear(Info->NearPlane, FromZNear, kCameraPosEps) &&
               CameraMoveNear(Info->FarPlane, FromZFar, kCameraPosEps) &&
               CameraMoveNear(ToQuat.w, FromQuat.w, kCameraQuatEps) &&
               CameraMoveNear(ToQuat.x, FromQuat.x, kCameraQuatEps) &&
               CameraMoveNear(ToQuat.y, FromQuat.y, kCameraQuatEps) &&
               CameraMoveNear(ToQuat.z, FromQuat.z, kCameraQuatEps);
    }
}

bool FRoomRuntime::SnapCamera(char const* CameraName)
{
    // FUN_0044c290. Any move stops before the lookup.
    bCameraMoving = 0;
    G_bNnKeepPanelOpen = 0;
    RoomCameraInfo const* const Info = FindRoomDataCamera(this, CameraName);
    if (Info == nullptr)
        return false;

    CameraFromZFar = SetCameraMoveEnds(this, Info);

    // The destination goes back through the quaternion (slot 0x54), as retail.
    Matrix Orient;
    RoomCameraMath()->QuaternionToMatrix(Orient, CameraToQuat);
    ApplyRoomCameraFrame(this, Orient, CameraToPos, CameraToHalfFovX,
                         CameraToZNear, CameraToZFar);
    return true;
}

bool FRoomRuntime::TrackCamera(char const* CameraName, FRoomCameraDoneCallback Done, void* A,
                               void* B)
{
    // FUN_0044ba60.
    if (bCameraMoving != 0)
        return false;
    RoomCameraInfo const* const Info = FindRoomDataCamera(this, CameraName);
    if (Info == nullptr)
        return false;

    bCameraMoving = 1;
    G_bNnKeepPanelOpen = 1;
    CameraMoveElapsed = 0.0f;
    CameraMoveDuration = 2.0f;

    float const FromZFar = SetCameraMoveEnds(this, Info);
    CameraFromZFar = FromZFar;
    CameraDone = Done;
    CameraDoneA = A;
    CameraDoneB = B;

    // When the camera is already there (a NaN keeps the move) the move is dropped again. The far
    // test uses the slot 9 value itself, before it went through +0x1394 (the same float).
    double const kPosEps = kCameraPosEps;
    double const kFovEps = kCameraFovEps;
    double const kQuatEps = kCameraQuatEps;
    if (CameraMoveNear(CameraToPos[0], CameraFromPos[0], kPosEps) &&
        CameraMoveNear(CameraToPos[1], CameraFromPos[1], kPosEps) &&
        CameraMoveNear(CameraToPos[2], CameraFromPos[2], kPosEps) &&
        CameraMoveNear(CameraToHalfFovX, CameraFromHalfFovX, kFovEps) &&
        CameraMoveNear(CameraToZNear, CameraFromZNear, kPosEps) &&
        CameraMoveNear(CameraToZFar, FromZFar, kPosEps) &&
        CameraMoveNear(CameraFromQuat.x, CameraToQuat.x, kQuatEps) &&
        CameraMoveNear(CameraFromQuat.y, CameraToQuat.y, kQuatEps) &&
        CameraMoveNear(CameraFromQuat.z, CameraToQuat.z, kQuatEps) &&
        CameraMoveNear(CameraFromQuat.w, CameraToQuat.w, kQuatEps))
    {
        bCameraMoving = 0;
        G_bNnKeepPanelOpen = 0;
    }

    return bCameraMoving != 0;
}

void FRoomRuntime::UpdateCameraMove(float Delta)
{
    // FUN_00449990.
    if (bCameraMoving == 0)
        return;

    // Stored as a float; the unrounded sum is compared (a NaN is not done).
    double const Elapsed = static_cast<double>(Delta) +
                           static_cast<double>(CameraMoveElapsed);
    CameraMoveElapsed = static_cast<float>(Elapsed);
    bool const bDone = Elapsed > static_cast<double>(CameraMoveDuration);

    // The skip latch is FUN_00454910.
    if ((GfScriptSequenceSkipLatch() && bCameraMoveSkippable != 0) || bDone)
    {
        bCameraMoving = 0;
        G_bNnKeepPanelOpen = 0;
        Matrix Orient;
        RoomCameraMath()->QuaternionToMatrix(Orient, CameraToQuat);
        ApplyRoomCameraFrame(this, Orient, CameraToPos, CameraToHalfFovX,
                             CameraToZNear, CameraToZFar);
        if (CameraDone != nullptr)
            CameraDone(CameraDoneA, CameraDoneB, 0);
        return;
    }

    // DAT_005c8a5c pi, DAT_005c75dc 1.0, DAT_005c75e0 0.5, all floats; T is stored as a float.
    double const Phase = static_cast<double>(CameraMoveElapsed) /
                         static_cast<double>(CameraMoveDuration) *
                         static_cast<double>(3.14159274101257324f);
    float const T = static_cast<float>(1.0 - (cos(Phase) + 1.0) * 0.5);

    // Pos = (To - From) * T + From. x's product and z's difference are rounded to floats on the
    // way, y stays on the x87 stack throughout.
    double const DeltaX = static_cast<double>(CameraToPos[0]) -
                          static_cast<double>(CameraFromPos[0]);
    double const DeltaY = static_cast<double>(CameraToPos[1]) -
                          static_cast<double>(CameraFromPos[1]);
    float const DeltaZ = static_cast<float>(static_cast<double>(CameraToPos[2]) -
                                            static_cast<double>(CameraFromPos[2]));
    float const ScaledX = static_cast<float>(DeltaX * static_cast<double>(T));
    float Pos[3];
    Pos[0] = static_cast<float>(static_cast<double>(ScaledX) +
                                static_cast<double>(CameraFromPos[0]));
    Pos[1] = static_cast<float>(DeltaY * static_cast<double>(T) +
                                static_cast<double>(CameraFromPos[1]));
    Pos[2] = static_cast<float>(static_cast<double>(DeltaZ) * static_cast<double>(T) +
                                static_cast<double>(CameraFromPos[2]));

    // Slerp (slot 0x58), back to a matrix (slot 0x54).
    Quaternion Quat;
    RoomCameraMath()->QuatSlerp(Quat, CameraFromQuat, CameraToQuat, T);
    Matrix Orient;
    RoomCameraMath()->QuaternionToMatrix(Orient, Quat);
    float Transform[12];
    memcpy(Transform, &Orient, sizeof(float) * 9);
    memcpy(Transform + 9, Pos, sizeof(float) * 3);
    RoomCameraSetTransform(Camera, Transform);

    // Each (To - From) * T + From rounded once.
    RoomCameraSetHalfFovX(Camera,
                          static_cast<float>((static_cast<double>(CameraToHalfFovX) -
                                              static_cast<double>(CameraFromHalfFovX)) *
                                                 static_cast<double>(T) +
                                             static_cast<double>(CameraFromHalfFovX)));
    RoomCameraSetZNear(Camera,
                       static_cast<float>((static_cast<double>(CameraToZNear) -
                                           static_cast<double>(CameraFromZNear)) *
                                              static_cast<double>(T) +
                                          static_cast<double>(CameraFromZNear)));
    RoomCameraSetZFar(Camera,
                      static_cast<float>((static_cast<double>(CameraToZFar) -
                                          static_cast<double>(CameraFromZFar)) *
                                             static_cast<double>(T) +
                                         static_cast<double>(CameraFromZFar)));
}

// LAB_0044ad80 (Event::Handler slot 0 of the +0x30 face; retail's offsets are 0x30 lower because
// ECX is the face). Id 0xf with a camera move running finishes the move at once, as
// FUN_00449990's end does.
bool FRoomRuntime::OnEvent(unsigned MsgId)
{
    if (MsgId != USER_CANCEL || bCameraMoving == 0)
        return false;
    bCameraMoving = 0;
    G_bNnKeepPanelOpen = 0;
    Matrix Orient;
    RoomCameraMath()->QuaternionToMatrix(Orient, CameraToQuat);
    ApplyRoomCameraFrame(this, Orient, CameraToPos, CameraToHalfFovX, CameraToZNear,
                         CameraToZFar);
    if (CameraDone != nullptr)
        CameraDone(CameraDoneA, CameraDoneB, 0);
    return true;
}

bool FRoomRuntime::TrackScriptCamera(int Arg0, FRoomCameraDoneCallback Done, void* A, void* B,
                                     unsigned char bArg4)
{
    // FUN_0044bfd0 -- a step along the room's numbered "Camera_Track_%d" cameras, +0x13a4 the
    // current number (0 = none yet). Only Arg0's low byte is tested: set steps
    // forward, clear backward. bArg4: when the camera already stands on the chosen one
    // (FUN_0044bd50), the step is repeated from there with bArg4 clear, and that answer is
    // returned. Otherwise the answer is FUN_0044ba60's move to it.
    bool const bForward = static_cast<unsigned char>(Arg0) != 0;
    char const* const kFirstTrack = "Camera_Track_1"; // s_Camera_Track_1_005cb893+1
    char Name[0x100];

    if (TrackCameraIndex == 0)
    {
        // The first step goes to Camera_Track_1.
        TrackCameraIndex = 1;
        if (bArg4 != 0 && IsRoomCameraAt(this, kFirstTrack))
            return TrackScriptCamera(Arg0, Done, A, B, 0);
        return TrackCamera(kFirstTrack, Done, A, B);
    }

    if (bForward)
    {
        TrackCameraIndex += 1;
        sprintf(Name, "Camera_Track_%d", TrackCameraIndex);
        if (bArg4 != 0 && IsRoomCameraAt(this, Name))
            return TrackScriptCamera(Arg0, Done, A, B, 0);
        if (TrackCamera(Name, Done, A, B))
            return true;
        // No such camera, or already there -> back to 1.
        TrackCameraIndex = 1;
        if (bArg4 != 0 && IsRoomCameraAt(this, kFirstTrack))
            return TrackScriptCamera(Arg0, Done, A, B, 0);
        return TrackCamera(kFirstTrack, Done, A, B);
    }

    TrackCameraIndex -= 1;
    if (TrackCameraIndex == 0)
    {
        // Below 1 wraps to the highest number whose camera exists
        // (counting up from 1 until a name has no RoomData camera; 0 when there is none).
        for (;;)
        {
            TrackCameraIndex += 1;
            sprintf(Name, "Camera_Track_%d", TrackCameraIndex);
            if (FindRoomDataCamera(this, Name) == nullptr)
                break;
        }
        TrackCameraIndex -= 1;
    }

    // The repeat passes Arg0 = 0 (backward).
    sprintf(Name, "Camera_Track_%d", TrackCameraIndex);
    if (bArg4 != 0 && IsRoomCameraAt(this, Name))
        return TrackScriptCamera(0, Done, A, B, 0);
    return TrackCamera(Name, Done, A, B);
}

unsigned char RoomControlStartScriptRan(FRoomRuntime* Runtime)
{
    // Read inline by FUN_005588a0.
    return Runtime->SceneFlags[kRoomStartScriptRan];
}

void FRoomRuntime::RequestCharacterScripts()
{
    // FUN_0044ad70. Read by the room update LAB_0044ab70, which then runs the room characters'
    // start script once.
    SceneFlags[kRoomStartScriptRequested] = 1;
}

unsigned GetRoomRuntimeRoomId(FRoomRuntime* Runtime)
{
    // +0x38.
    return Runtime->RoomId;
}

FCharacterObject* FRoomRuntime::FindCharacterByKey(unsigned Key)
{
    // FUN_00449c90: the first slot whose character's record key (+0x154) is Key.
    int Slot = 0;
    for (Slot = 0; Slot < 0x40; ++Slot)
    {
        FCharacterObject* const Character = Characters[Slot];
        if (Character != nullptr && Character->Props.Key == Key)
            return Character;
    }
    return nullptr;
}

FCharacterObject* FRoomRuntime::PlayerCharacter()
{
    // FUN_00449cd0: the first character on a placement (+0x1b8) whose char type (+0x34) is 1.
    int Slot = 0;
    for (Slot = 0; Slot < 0x40; ++Slot)
    {
        FCharacterObject* const Character = Characters[Slot];
        if (Character == nullptr)
            continue;
        CharPlaceInfo const* const Placement = Character->Placement;
        if (Placement != nullptr && Placement->CharType == kSetpointCharPc)
            return Character;
    }
    return nullptr;
}

FCharacterObject* FRoomRuntime::FindCharacterByRole(int Role)
{
    // FUN_00449d10: the first character whose role (+0x300) is Role.
    int Slot = 0;
    for (Slot = 0; Slot < 0x40; ++Slot)
    {
        FCharacterObject* const Character = Characters[Slot];
        if (Character != nullptr && Character->Role == Role)
            return Character;
    }
    return nullptr;
}

FCharacterObject* FRoomRuntime::FindCharacterByName(char const* Name)
{
    // FUN_00449d50: the first character on a placement whose setpoint segment
    // (SetpointProperties::extract_from_name, +0x08) is Name (DACOM stricmp).
    int Slot = 0;
    for (Slot = 0; Slot < 0x40; ++Slot)
    {
        FCharacterObject* const Character = Characters[Slot];
        if (Character == nullptr)
            continue;
        CharPlaceInfo const* const Placement = Character->Placement;
        if (Placement == nullptr)
            continue;
        SetpointProperties Setpoint;
        Setpoint.extract_from_name(Placement->Name);
        if (_stricmp(Setpoint.Segment, Name) == 0)
            return Character;
    }
    return nullptr;
}

void FRoomRuntime::DeleteCharacter(int Slot)
{
    // FUN_00449e10.
    FCharacterObject* const Character = Characters[Slot];
    if (Character == nullptr)
        return;
    // Its placement freed again (FUN_00435b20).
    if (Character->PlacementIndex != -1)
        Placements[Character->PlacementIndex].Character = nullptr;
    delete Characters[Slot];
    Characters[Slot] = nullptr;
}

void FRoomRuntime::UpdateCharacter(CharacterProperties const& Props)
{
    // FUN_00449e60. Only this room's records with a costume (FUN_00435b40).
    if (Props.RoomId != RoomId)
        return;
    if (!CostumeHasParts(Props.Cost))
        return;

    int Slot = 0;
    for (Slot = 0; Slot < 0x40; ++Slot)
    {
        FCharacterObject* const Character = Characters[Slot];
        if (Character != nullptr && Character->Props.Key == Props.Key)
            break;
    }
    if (Slot < 0x40)
    {
        // Updated in place: the hotspot re-gated (FUN_00445a60) and the record's idle script
        // made the own one (FUN_00445580 with 0).
        FCharacterObject* const Character = Characters[Slot];
        Character->Props.destroy();
        Character->Props = Props;
        Character->UpdateHotspotEnabled();
        Characters[Slot]->SetOwnScript(Props.Script, 0);
        return;
    }

    // The first empty slot takes a new character (FUN_00435960). Retail: with all 0x40 slots
    // taken it stores through slot -1 (`mov [ebp+esi*4+518h],eax`, esi = -1), the four
    // SceneFlags bytes just in front of the array; the port writes the same bytes explicitly.
    int Free = -1;
    for (Slot = 0; Slot < 0x40; ++Slot)
    {
        if (Characters[Slot] == nullptr)
        {
            Free = Slot;
            break;
        }
    }
    FCharacterObject* const Character = CreatePlacedCharacter(Placements, Props, this);
    if (Free == -1)
    {
        memcpy(SceneFlags, &Character, sizeof(SceneFlags));
        return;
    }
    Characters[Free] = Character;
}

namespace
{
    // FUN_0044a540's three sequence loops: a sequence per path (findable only for the ambient
    // ones), the script added with an identity frame and no engine (loaded from the path). The
    // count is re-read from the room after every sequence, hence the reference.
    void PlayRoomSceneList(FRoomRuntime* Runtime, char* const* Paths,
                           unsigned char const* Flags, int const& Count, unsigned char bFindable,
                           int Kind, unsigned char bLoop)
    {
        int Index = 0;
        for (Index = 0; Index < Count; ++Index)
        {
            FGfScriptSequence* const Sequence =
                G_GfScriptManager.CreateSequence(Runtime, bFindable);
            Csys Frame;
            Frame.set_identity();
            Sequence->AddScript(Paths[Index], Kind, &Frame, nullptr);
            Sequence->BindMode = 0;
            Sequence->bLoop = bLoop;
            Sequence->bLetterbox = 0;
            Sequence->bApplySpatial = 1;
            Sequence->bRestoreCamera = 0;
            Sequence->Priority = 0;
            Sequence->bSoundsUseFade = Flags[Index];
            Sequence->Play();
        }
    }
}

// FUN_00449f40 -- the room character whose script behaviour is a start script. Each index gets
// a fresh walk to it. No behaviour with the character's id -> +0x09 is read through null, as
// retail (no guard). No break: the last such character wins. Callers: FUN_0044a540,
// FUN_0044aca0, the room update LAB_0044ab70.
bool FRoomRuntime::FindStartScript(int* OutCharacterKey, int* OutBehaviorKey)
{
    *OutCharacterKey = 0;
    *OutBehaviorKey = -1;
    bool bFound = false;
    TMirrorPropertyBucket<CharacterProperties>* const Characters =
        GetClientLocationRoomCharacters(RoomId);
    TMirrorPropertyBucket<ScriptBehavior>* const Behaviors =
        GetClientLocationRoomScriptBehaviors(RoomId);

    int CharacterCount = 0;
    MetaNode<CharacterProperties>* CharacterCursor = nullptr;
    while (Characters->List.traverse(CharacterCursor, true))
        ++CharacterCount;

    int CharacterIndex = 0;
    for (CharacterIndex = 0; CharacterIndex < CharacterCount; ++CharacterIndex)
    {
        CharacterProperties const* Character = nullptr;
        int Step = 0;
        CharacterCursor = nullptr;
        while (Characters->List.traverse(CharacterCursor, true))
        {
            if (Step == CharacterIndex)
            {
                Character = CharacterCursor->Data;
                break;
            }
            ++Step;
        }
        unsigned const BehaviorId = Character->BehaviorId;
        if (BehaviorId == 0xffffffffu)
            continue;

        ScriptBehavior const* Behavior = nullptr;
        MetaNode<ScriptBehavior>* BehaviorCursor = nullptr;
        while (Behaviors->List.traverse(BehaviorCursor, true))
        {
            if (static_cast<unsigned>(BehaviorCursor->Data->BehaviorId) == BehaviorId)
            {
                Behavior = BehaviorCursor->Data;
                break;
            }
        }
        if (Behavior->bAutoplay == 1)
        {
            *OutCharacterKey = static_cast<int>(Character->Key);
            *OutBehaviorKey = Behavior->BehaviorId;
            bFound = true;
        }
    }
    return bFound;
}

// The "found" tail both FUN_0044aca0 and LAB_0044ab70 inline. No character with the key ->
// +0x1b8 is read through null, as retail (no guard). FUN_00435dd0, then IServer slot 41.
static void RoomControlRunStartScript(FRoomRuntime* Runtime, int CharacterKey, int BehaviorKey)
{
    FCharacterObject* Character = nullptr;
    int Slot = 0;
    for (Slot = 0; Slot < 0x40; ++Slot)
    {
        FCharacterObject* const Candidate = Runtime->Characters[Slot];
        if (Candidate != nullptr && Candidate->Props.Key == static_cast<unsigned>(CharacterKey))
        {
            Character = Candidate;
            break;
        }
    }
    StartCharacterBehaviorScript(Character, Runtime, Character->Placement,
                                 static_cast<unsigned>(BehaviorKey), nullptr, nullptr);
    GetLocalServerInterface()
        ->GFObjSelect(static_cast<unsigned>(CharacterKey), GetPlayerConnectionId());
    Runtime->SceneFlags[kRoomStartScriptRan] = 1;
}

// FUN_0044a540 -- start the room's scene scripts: the RoomData "scene =" records and the room
// state's ambient scripts (FUN_0043aff0) are collected (no bound check) and played; a room
// character with a start script (FUN_00449f40) skips the ship, goods-cart and character scripts.
void FRoomRuntime::StartScenes()
{
    RoomData* const Room = this->Room;
    int RecordIndex = 0;
    for (RecordIndex = 0; RecordIndex < static_cast<int>(Room->AmbientScripts.size());
         ++RecordIndex)
    {
        AmbientScriptInfo const& Record = *Room->AmbientScripts[RecordIndex];
        if (!RtcSlider::is_enabled(static_cast<unsigned long>(Record.Priority)))
            continue;
        if (Record.Type == kAmbientScriptAmbient)
        {
            malloc_strcpy(&SceneAmbient[SceneAmbientCount], Record.Path);
            SceneAmbientFlag[SceneAmbientCount] = 1;
            ++SceneAmbientCount;
        }
        else if (Record.Type == kAmbientScriptTraffic)
        {
            malloc_strcpy(&SceneTraffic[SceneTrafficCount], Record.Path);
            SceneTrafficFlag[SceneTrafficCount] = 1;
            ++SceneTrafficCount;
        }
        else if (Record.Type == kAmbientScriptOnce)
        {
            malloc_strcpy(&SceneOnce[SceneOnceCount], Record.Path);
            SceneOnceFlag[SceneOnceCount] = 1;
            ++SceneOnceCount;
        }
    }

    TMirrorPropertyBucket<AmbientScriptProperties>* const Scripts =
        GetClientLocationRoomAmbientScripts(RoomId);
    int NodeCount = 0;
    MetaNode<AmbientScriptProperties>* Cursor = nullptr;
    while (Scripts->List.traverse(Cursor, true))
        ++NodeCount;
    int NodeIndex = 0;
    for (NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
    {
        // A walk that ran out would read through null; NodeIndex < NodeCount keeps it reachable.
        AmbientScriptProperties const* Script = nullptr;
        int Step = 0;
        Cursor = nullptr;
        while (Scripts->List.traverse(Cursor, true))
        {
            if (Step == NodeIndex)
            {
                Script = Cursor->Data;
                break;
            }
            ++Step;
        }
        if (Script->ScriptId == kAmbientScriptAmbient)
        {
            malloc_strcpy(&SceneAmbient[SceneAmbientCount], Script->Name);
            SceneAmbientFlag[SceneAmbientCount] =
                static_cast<unsigned char>(Script->bFlag);
            ++SceneAmbientCount;
        }
        else if (Script->ScriptId == kAmbientScriptTraffic)
        {
            malloc_strcpy(&SceneTraffic[SceneTrafficCount], Script->Name);
            SceneTrafficFlag[SceneTrafficCount] =
                static_cast<unsigned char>(Script->bFlag);
            ++SceneTrafficCount;
        }
        else if (Script->ScriptId == kAmbientScriptOnce)
        {
            malloc_strcpy(&SceneOnce[SceneOnceCount], Script->Name);
            SceneOnceFlag[SceneOnceCount] =
                static_cast<unsigned char>(Script->bFlag);
            ++SceneOnceCount;
        }
    }

    PlayRoomSceneList(this, SceneAmbient, SceneAmbientFlag,
                      SceneAmbientCount, 1, 0, 1);
    PlayRoomSceneList(this, SceneTraffic, SceneTrafficFlag,
                      SceneTrafficCount, 0, 5, 0);
    PlayRoomSceneList(this, SceneOnce, SceneOnceFlag,
                      SceneOnceCount, 0, 0, 0);

    int StartCharacterKey = 0;
    int StartBehaviorKey = -1;
    if (FindStartScript(&StartCharacterKey, &StartBehaviorKey))
    {
        RoomControlSetLandingPending(0);
        return;
    }

    if (ShipPads.PlayLanding()) // FUN_00448a20
        SceneFlags[kRoomEntryScriptStarted] = 1;

    if (GfObjectShip() != nullptr && Room->GoodscartScript != nullptr &&
        RtcSlider::is_enabled(static_cast<unsigned long>(Room->GoodscartPriorityId)))
    {
        FGfScriptSequence* const Sequence = G_GfScriptManager.CreateSequence(this, 0);
        Csys Frame;
        Frame.set_identity();
        Sequence->AddScript(Room->GoodscartScript, 0, &Frame, nullptr);
        Sequence->bPropsToRoom = 1;
        Sequence->BindMode = 0;
        Sequence->bLetterbox = 1;
        Sequence->bApplySpatial = 1;
        Sequence->bRestoreCamera = 1;
        Sequence->Priority = 0;
        Sequence->bTransition = 1;
        Sequence->Play();
        SceneFlags[kRoomEntryScriptStarted] = 1;
    }

    // +0x515 is set after each character, even when no sequence could be made.
    int Slot = 0;
    for (Slot = 0; Slot < 0x40; ++Slot)
    {
        FCharacterObject* const Character = Characters[Slot];
        if (Character == nullptr)
            continue;
        if (Character->PlacementIndex == -1)
            continue;
        char const* const StartScript =
            Placements[Character->PlacementIndex].Info->StartScript;
        if (StartScript == nullptr || StartScript[0] == '\0')
            continue;
        FGfScriptSequence* const Sequence = G_GfScriptManager.CreateSequence(this, 0);
        if (Sequence != nullptr)
        {
            Csys Frame;
            Frame.Pos.x = 0.0f;
            Frame.Pos.y = 0.0f;
            Frame.Pos.z = 0.0f;
            MatrixSetIdentity(Frame.Orient);
            Sequence->AddScript(StartScript, 0, &Frame, nullptr);
            Sequence->Mirror.Add(Characters[Slot], nullptr);
            Sequence->BindMode = 1;
            Sequence->bLetterbox = 1;
            Sequence->bApplySpatial = 1;
            Sequence->bRestorePlayer = 0;
            Sequence->bRestoreCamera = 1;
            Sequence->Priority = 0;
            Sequence->bDuckAmbient = 0;
            Sequence->bDuckMusic = 0;
            Sequence->bTransition = 1;
            Sequence->Play();
        }
        SceneFlags[kRoomEntryScriptStarted] = 1;
    }

    RoomControlSetLandingPending(0);
}

void FRoomRuntime::PlayScenes()
{
    // FUN_0044aca0.
    StartScenes();
    if (SceneFlags[kRoomEntryScriptStarted] != 0)
        return;
    if (SceneFlags[kRoomStartScriptChecked] != 0)
        return;
    int CharacterKey = 0;
    int BehaviorKey = -1;
    if (FindStartScript(&CharacterKey, &BehaviorKey))
        RoomControlRunStartScript(this, CharacterKey, BehaviorKey);
    SceneFlags[kRoomStartScriptChecked] = 1;
}

// FUN_0044d200 -- the count is re-read each pass and the entries are not null-tested.
void FRoomRuntime::UpdateButtons(float Delta)
{
    int Index = 0;
    for (Index = 0; Index < ControllerCount; ++Index)
        Controllers[Index]->Update(Delta);
}

void FRoomRuntime::Update(double DeltaSeconds)
{
    // LAB_0044ab70 (RoomControl slot 5). Called every frame by the gameflow RoomObject walk
    // FUN_00558ac0. The double is narrowed to float once and every callee receives that float.
    float const Delta = static_cast<float>(DeltaSeconds);

    UpdateCameraMove(Delta);

    // A restarted character start script (+0x516, FUN_0044ad70).
    if (SceneFlags[kRoomStartScriptRequested] != 0)
    {
        if (SceneFlags[kRoomStartScriptChecked] == 0)
        {
            int CharacterKey = 0;
            int BehaviorKey = -1;
            if (FindStartScript(&CharacterKey, &BehaviorKey))
                RoomControlRunStartScript(this, CharacterKey, BehaviorKey);
            SceneFlags[kRoomStartScriptChecked] = 1;
        }
        SceneFlags[kRoomStartScriptRequested] = 0;
    }

    // FUN_00557860 on each light; the count is re-read every iteration.
    if (GetGameOption(kGameOptEffectsGroup) != 0 && GetGameOption(kGameOptFlashlights) > 0)
    {
        int Index = 0;
        for (Index = 0; Index < FlashlightCount; ++Index)
            Flashlights[Index]->Update(Delta);
    }

    UpdateButtons(Delta);
}

namespace
{
    // DAT_00668ab0 -- FUN_004489f0's byte: the player's ship plays its pad's landing script in
    // the next room that starts its scenes (FUN_00448a20).
    unsigned char G_RoomControlLandingPending = 0;
}

void RoomControlSetLandingPending(unsigned char Value)
{
    // FUN_004489f0.
    G_RoomControlLandingPending = Value;
}

// ---------------------------------------------------------------------------
// The ship-pad table (Runtime+0x1020).
// ---------------------------------------------------------------------------

void FRoomShipPad::Clear()
{
    // FUN_004486d0. +0x08 first, then +0x0c.
    if (Ship != nullptr)
    {
        delete Ship;
        Ship = nullptr;
    }
    if (ForSale != nullptr)
    {
        delete ForSale;
        ForSale = nullptr;
    }
}

void FRoomShipPad::CreatePlayerShip()
{
    // FUN_004487f0. The description's ctor is FUN_00446bf0, its dtor FUN_00446c50.
    FObjectShipDesc Desc;
    Desc.Pos = Place->Frame.Pos;
    Desc.Orient = Place->Frame.Orient;
    Desc.bShadow = 1;
    Desc.FloorHeight = Place->Frame.Pos.y; // +0x3c, the pad's y
    malloc_strcpy(&Desc.LaunchScript, Place->LaunchingScript);
    Desc.Ship = Archetype::GetShip(GetPlayerShipArchetypeIdRetail()); // FUN_004c3e10
    Desc.Equipment = &GetPlayerEquipDescListRetail();                  // FUN_004c4060
    Ship = new FObjectShip(Room, Desc); // operator new(0xb0), FUN_00446ef0
}

void FRoomShipPad::CreateForSaleShip(unsigned GoodId)
{
    // FUN_00448700. The description's ctor is FUN_00443770.
    FDealerShipDesc Desc;
    Desc.Pos = Place->Frame.Pos;
    Desc.Orient = Place->Frame.Orient;
    Desc.bShadow = 1;
    Desc.Height = Place->Frame.Pos.y;
    // None of the three lookups is null-tested, as retail.
    GoodInfo const* const Package = GoodList::find_by_id(GoodId);
    GoodInfo const* const Hull = GoodList::find_by_id(Package->HullGoodId);
    Desc.Ship = Archetype::GetShip(Hull->ShipArchId);
    // DEVIATION: retail hands the GoodInfo's own +0x94 addon list; the port's GoodInfo keeps
    // the addons in a vector (common_goods.h), so they are copied into an EquipDescList first,
    // as NN_Dealer does.
    EquipDescList Addons;
    Addons.Items.assign(Package->AddonLow.begin(), Package->AddonLow.end());
    Desc.Loadout = &Addons;
    ForSale = new FDealerShipObject(Room, Desc); // operator new(0x64), FUN_004437d0
}

FRoomShipPadTable::FRoomShipPadTable()
    : Count(0), PlayerPad(-1)
{
    // The runtime's ctor calls FUN_004488d0 (Init) on this member; the vector itself is the
    // std::vector default state (FUN_004488d0 zeroes its three pointers).
}

void FRoomShipPadTable::Init()
{
    // FUN_004488d0. It also zeroes the vector's pointers, already so from its default
    // construction.
    Count = 0;
    PlayerPad = -1;
}

FRoomShipPadTable::~FRoomShipPadTable()
{
    // FUN_004488f0: erase(begin, end), each pad through its element dtor FUN_004486d0 (inlined).
    // The port's pads have no destructor of their own (the only resize, FUN_00448a60, runs on
    // an empty table), so the element dtor runs here; std::vector frees the storage.
    size_t Index = 0;
    for (Index = 0; Index < Pads.size(); ++Index)
        Pads[Index].Clear();
}

void FRoomShipPadTable::Build(FRoomRuntime* Runtime, FClientMarket* Market)
{
    // FUN_00448a60.
    std::vector<ShipPlaceInfo*> const& Placements = Runtime->Room->ShipPlacements;
    Count = static_cast<int>(Placements.size());

    // FUN_00448ef0, then the temporary's element dtor.
    FRoomShipPad Blank = {};
    Pads.resize(static_cast<size_t>(Count), Blank);
    Blank.Clear();

    int Index = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        Pads[Index].Room = Runtime;
        Pads[Index].Place = Placements[Index];
    }

    int Found = -1;
    for (Index = 0; Index < Count; ++Index)
    {
        FRoomShipPad const& Pad = Pads[Index];
        if (Pad.Ship == nullptr && Pad.ForSale == nullptr && Pad.Place->bIsPlayerShip)
        {
            Found = Index;
            break;
        }
    }
    PlayerPad = Found;

    if (PlayerPad != -1 && GetPlayerShipArchetypeIdRetail() != 0 &&
        RtcSlider::is_enabled("player_ship"))
    {
        Pads[PlayerPad].CreatePlayerShip();
    }

    // The market's ship packages (category 3, FUN_00437f70). FUN_0043a510's result is passed
    // on without a null test.
    std::list<FClientMarketGood const*> Packages;
    ClientMarketCollectCategory(Market, &Packages, 3);

    // No free for-sale pad -> the package is skipped and the walk goes on.
    std::list<FClientMarketGood const*>::const_iterator It;
    for (It = Packages.begin(); It != Packages.end(); ++It)
    {
        int FreePad = -1;
        for (Index = 0; Index < Count; ++Index)
        {
            FRoomShipPad const& Pad = Pads[Index];
            if (Pad.Ship == nullptr && Pad.ForSale == nullptr && Pad.Place->bIsForSale)
            {
                FreePad = Index;
                break;
            }
        }
        if (FreePad != -1)
            Pads[FreePad].CreateForSaleShip((*It)->Info.GoodId);
    }
}

bool FRoomShipPadTable::HasLandingScript() const
{
    // FUN_00448a00.
    if (PlayerPad == -1)
        return false;
    return Pads[PlayerPad].Place->LandingScript != nullptr;
}

bool FRoomShipPadTable::PlayLanding()
{
    // FUN_00448a20.
    if (G_RoomControlLandingPending == 0)
        return false;
    if (PlayerPad == -1)
        return false;
    FRoomShipPad const& Pad = Pads[PlayerPad];
    char const* const Script = Pad.Place->LandingScript;
    if (Script == nullptr)
        return false;
    if (Pad.Ship == nullptr)
        return false;
    Pad.Ship->PlayLandingScript(Script);
    return true;
}

bool FRoomRuntime::HasLandingScript()
{
    // FUN_0044ab60, a thunk to FUN_00448a00.
    return ShipPads.HasLandingScript();
}

void ClearRoomObjectList()
{
    // FUN_00559380: every node unlinked and freed, the size decremented per node.
    G_RoomObjects.clear();
}

// FUN_004498b0 (thiscall, no arguments) -- start every RoomData animation found in the room's
// main script: play it on the main instance into +0x64 and evaluate it at -2 (looped with
// flag 2 when RoomData+0x94 marks it); FDUMP an error for a name the script does not have.
void FRoomRuntime::StartAnimations()
{
    RoomData* Room = this->Room;
    int Index = 0;
    for (Index = 0; Index < Room->AnimationCount; ++Index)
    {
        char const* Name = Room->Animations[Index];
        int const Script = Scripts.Find(Name); // FUN_00555b20
        if (Script == -1)
        {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: animation name %s is not present in room mesh",
                  kRoomControlCpp, 0xed, Name);
            continue;
        }
        unsigned long const Playback = Scripts.Play(MainInstance, Script);
        Handles64[Index] = Playback;
        unsigned long const Flags = Room->AnimationLoop[Index] ? 2u : 0u;
        DALib::Anim->EvalPlayback(Playback, Flags, -2.0f, 1.0f, AsOpaqueDword(0),
                                  AsOpaqueDword(1.0f), AsOpaqueDword(1.0f), nullptr);
    }
}

namespace
{
    // .rdata 0x005cc01c -- the source path RoomControlHotspot.cpp hands FDUMP.
    char const kRoomControlHotspotCpp[] =
        "E:\\FL\\Scratch\\Source\\Client\\Gf\\RoomControlHotspot.cpp";
}

// FUN_0044ced0 (thiscall) -- one room button per RoomData hotspot (+0x40's vector at +0x194,
// its size re-read every pass) by the hotspot's behavior, appended at +0x1134 with its location
// (FUN_0043a4f0 = MakeLocationID(+0x34, room_switch), 0 without a room_switch) and behavior
// (FUN_0044d310). No bound on the 0x80 slots, as in retail. Caller: FUN_004496b0.
void FRoomRuntime::BuildButtons()
{
    Field_0x13a8 = 0;
    ControllerCount = 0;

    RoomData* const Room = this->Room;
    size_t Index = 0;
    for (Index = 0; Index < Room->Hotspots.size(); ++Index)
    {
        RoomButtonInfo const* const Info = Room->Hotspots[Index];
        int const Behavior = Info->Behavior;
        // The next slot is cleared before the type test.
        Controllers[ControllerCount] = nullptr;

        RoomButton* Button = nullptr;
        switch (Behavior)
        {
        case kHotspotExitDoor:
            // operator_new(0x18), vftable 0x005cc068.
            Button = new ExitDoorButton();
            break;
        case kHotspotNewsVendor:
            // operator_new(0xc), FUN_0044d410.
            Button = new NewsVendorButton();
            break;
        case kHotspotMissionVendor:
            // operator_new(0xc), FUN_0044d540.
            Button = new MissionVendorButton();
            break;
        case kHotspotLaunch:
            // operator_new(0x18), vftable 0x005cc058.
            Button = new LaunchButton();
            break;
        case kHotspotStartDealer:
        case kHotspotFrontDesk:
            // operator_new(0xc), FUN_0044d2c0.
            Button = new RoomButton();
            break;
        case kHotspotStartShipDealer:
        case kHotspotStartEquipDealer:
        case kHotspotMoveRight:
        case kHotspotMoveLeft:
        case kHotspotVirtualRoom:
        case kHotspotRepair:
            // No button, the count unchanged.
            continue;
        default:
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Unknown button behavior type [%d]",
                  kRoomControlHotspotCpp, 0x5b, Behavior);
            continue;
        }
        Controllers[ControllerCount] = Button;

        unsigned Location = 0;
        if (Info->RoomSwitch != nullptr)
            Location = MakeLocationID(BaseId, Info->RoomSwitch);
        Controllers[ControllerCount]->Set(Location, Behavior);
        ++ControllerCount;
    }
}

// FUN_0044a3a0 (thiscall, no arguments) -- one FlashLight per resolved frame of every RoomData
// FlashLightSet record (RoomData+0x1a4), appended to +0x81c / +0x101c with no bound check. The
// lights of one set share a period and are staggered by "gap" along it.
void FRoomRuntime::LoadFlashlights()
{
    // The size is re-read every iteration.
    int SetIndex = 0;
    for (SetIndex = 0; SetIndex < static_cast<int>(Room->FlashLightSets.size());
         ++SetIndex)
    {
        FlashLightSetInfo* Set = Room->FlashLightSets[SetIndex];
        // The resolved frame count (+0xe28).
        int const Count = static_cast<int>(Set->Cursor);
        if (Count <= 0)
            continue;

        // Period = (Count - 1) * gap (+0x10) + endpause (+0x18), rounded to float.
        float const Period = static_cast<float>(
            static_cast<double>(Count - 1) * static_cast<double>(Set->Gap) +
            static_cast<double>(Set->EndPause));
        // DAT_005c8ba8 (0.0); a NaN period keeps Step 0.
        float Step = 0.0f;
        if (Period > 0.0)
            Step = static_cast<float>(static_cast<double>(Set->Gap) /
                                      static_cast<double>(Period));

        Vector const Black = {0.0f, 0.0f, 0.0f};
        // The count is re-read after every light.
        int LightIndex = 0;
        for (LightIndex = 0; LightIndex < static_cast<int>(Set->Cursor); ++LightIndex)
        {
            // operator new(0x7c), FUN_00557630; DAT_005cbf58 is the 0.25.
            float const Phase =
                static_cast<float>(-(static_cast<double>(LightIndex) * static_cast<double>(Step)));
            float const BulbSize = static_cast<float>(static_cast<double>(Set->Scale) * 0.25);
            FFlashLight* Light = new FFlashLight(reinterpret_cast<Vector const*>(&Set->ColorR),
                                                 &Black, Period, Phase, Set->Blink, BulbSize,
                                                 Set->Scale);
            // FUN_00557810 on the set's resolved frame (+0xe2c + LightIndex * 0x30).
            Flashlights[FlashlightCount] = Light;
            Flashlights[FlashlightCount]->SetFrame(Set->FinalFrames[LightIndex]);
            ++FlashlightCount;
        }
    }
}

FRoomRuntime* CreateRoomRuntime(RoomData* Room)
{
    // operator new(0x13ac) then FUN_00449260. Retail leaves the bytes FUN_00449260 does not
    // write indeterminate; the port hands the ctor a zero-filled block instead.
    void* Memory = operator new(sizeof(FRoomRuntime));
    memset(Memory, 0, sizeof(FRoomRuntime));
    FRoomRuntime* Runtime = new (Memory) FRoomRuntime();

    // FUN_004496b0 (BaseId, RoomId). Retail finds the RoomData with FUN_0043a3d0(RoomId); the
    // caller already holds it.
    Runtime->RoomId = Room->RoomId;
    Runtime->BaseId = Room->BaseId;
    Runtime->LocationBase = LookupClientLocationBase(Runtime->BaseId);
    Runtime->Room = Room;
    Runtime->MainInstance = kRoomObjectNoInstance;
    Runtime->MainScript = 0xffffffffu;
    Runtime->LoadProps();
    Room->fixup(reinterpret_cast<long>(Runtime->MainInstance));
    if (Runtime->MainScript != 0xffffffffu)
        Runtime->Scripts.Load(Runtime->MainScript); // FUN_00555bf0
    Runtime->StartAnimations();

    // One FUN_00556fd0 wrapper per RoomData+0x144 light, no bound check.
    int LightIndex = 0;
    for (LightIndex = 0; LightIndex < static_cast<int>(Room->Lights.size()); ++LightIndex)
    {
        RoomLightInfo* Light = Room->Lights[LightIndex];
        Runtime->RoomLights[LightIndex] = GfLightWrapperCreate(
            &Light->Frame, reinterpret_cast<FD3dLight8 const*>(&Light->Mode),
            &Light->Direction, Light->Type);
    }
    Runtime->RoomLightCount = static_cast<int>(Room->Lights.size());

    FRoomCamera* Cam = RoomCameraCreate(); // FUN_00556be0
    Runtime->Camera = Cam;
    InstallRoomDataCamera(Cam, Room->Camera);

    BuildCharacterPlacements(Runtime->Placements, Room->CharacterPlacements);

    Runtime->LoadFlashlights();

    // The base's market (FUN_0043a510) is not null-tested.
    Runtime->ShipPads.Build(Runtime, GetClientLocationBaseMarket(Runtime->BaseId));

    Runtime->BuildButtons();

    // The fade opened at the speed of its current duration (DAT_0061650c); DAT_005c89b8 is 1.0.
    SetFadeTarget(0.0f, static_cast<float>(1.0 / static_cast<double>(GetFadeDuration())));
    SetLetterboxTarget(0.0f, 3.0f);     // FUN_0041a0f0
    SetClientMouseCursorDrawFlag(1);    // FUN_0041dda0
    Runtime->RegisterEvent(USER_CANCEL);        // FUN_005760d0 on the +0x30 face
    MouseSetCursor("arrow", false);     // FUN_0041dde0
    // FUN_00558bf0, FUN_00558dd0, FUN_005592a0.
    HoldGameflowTime();
    ClearHoveredHotspot();
    UnlockHotspotInput();
    return Runtime;
}

void DestroyRoomRuntime(FRoomRuntime* Runtime)
{
    // FUN_00449400 (the deleting destructor; no null test). The three callers in
    // menu_universe_base.cpp (UnloadLocationBase, the shutdown walk, UnloadClientLocationRoomState)
    // test the pointer before the delete.
    delete Runtime;
}

ICamera* GetRoomRuntimeCamera(FRoomRuntime* Runtime)
{
    return Runtime != nullptr ? Runtime->Camera : nullptr;
}

FRoomCamera* GetRoomRuntimeRoomCamera(FRoomRuntime* Runtime)
{
    return Runtime != nullptr ? Runtime->Camera : nullptr;
}

unsigned GetRoomRuntimeByteSize()
{
    return static_cast<unsigned>(sizeof(FRoomRuntime));
}

// FUN_0044b450 (the camera argument is never read). Rebuilds the per-frame
// light list at +0x2e8 when the LIGHTING option is on.
void FRoomRuntime::CollectLights(ICamera* /*Camera*/)
{
    if (GetGameOption(kGameOptLighting) == 0)
        return;

    if (bSingleLight != 0)
    {
        FrameAmbient[0] = 0.0f;
        FrameAmbient[1] = 0.0f;
        FrameAmbient[2] = 0.0f;
        FrameLights[0] = RoomLights[SingleLightIndex];
        FrameLightCount = 1;
        return;
    }

    FrameAmbient[0] = Room->AmbientColor.x;
    FrameAmbient[1] = Room->AmbientColor.y;
    FrameAmbient[2] = Room->AmbientColor.z;
    FrameLightCount = 0;

    int Index = 0;
    for (Index = 0; Index < RoomLightCount; ++Index)
    {
        FGfLightWrapper* Wrapper = RoomLights[Index];
        if (Wrapper == nullptr || Wrapper->Light == nullptr)
            continue;
        if (FrameLightCount >= 0x80)
            continue;
        FrameLights[FrameLightCount] = Wrapper;
        ++FrameLightCount;
    }

    // Then every enabled script light (DAT_0066d130), same filter.
    const int Count = GfLightRegistryCount();
    for (Index = 0; Index < Count; ++Index)
    {
        FGfLightWrapper* Wrapper = GfLightRegistryGet(Index);
        if (Wrapper == nullptr || Wrapper->Light == nullptr)
            continue;
        if (FrameLightCount >= 0x80)
            continue;
        FrameLights[FrameLightCount] = Wrapper;
        ++FrameLightCount;
    }
}

int const* FRoomObject::GetAmbientOverride() const
{
    // FUN_00559590.
    if (AmbientSource == nullptr)
        return nullptr;
    return reinterpret_cast<int const*>(static_cast<unsigned char const*>(AmbientSource) + 0x6c);
}

// ---------------------------------------------------------------------------
// The material-batcher sink (Client\cmn\MaterialLib.cpp unit).
// ---------------------------------------------------------------------------

namespace
{
    // DAT_006166d4 -- raised by FUN_0041a670, dropped by FUN_0041a6b0; nothing reads it.
    unsigned char G_RoomBatchActive = 0;

    // DAT_006166bc -- the one IMaterialBatcherEvents the exe implements: vptr 0x005c9230 and a
    // reference count DAT_006166c0, both stored by the static initializer LAB_0041a610. While a
    // room's slot-7 pass runs it is the batcher's observer, and every queued record it flushes
    // relights its object (FUN_0044ae80) from the room and object stored with the record.
    struct FRoomBatcherSink final : public IMaterialBatcherEvents
    {
        unsigned long RefCount; // +0x04 DAT_006166c0

        FRoomBatcherSink()
            : RefCount(1)
        {
        }

        GENRESULT DACOM_API QueryInterface(const char* Iid, void** Out) override
        {
            // LAB_0041a750: the interface table 0x005c9268 holds this one entry.
            if (strcmp(Iid, Iids::MaterialBatcherEvents) == 0)
            {
                AddRef();
                *Out = static_cast<IMaterialBatcherEvents*>(this);
                return GR_OK;
            }
            *Out = nullptr;
            return GR_INTERFACE_UNSUPPORTED;
        }

        unsigned long DACOM_API AddRef() override
        {
            // LAB_0051e8e0.
            ++RefCount;
            return RefCount;
        }

        unsigned long DACOM_API Release() override
        {
            // LAB_0041a800 (ICF-shared with FScriptCamSink::Release). Retail runs this body on
            // the static DAT_006166bc, whose count starts at 1 and only sees the batcher's
            // balanced AddRef / Release pairs, so the free is never reached; it is kept as retail.
            if (RefCount > 0)
                --RefCount;
            if (RefCount != 0)
                return RefCount;
            RefCount = 1;
            free(this);
            return 0;
        }

        void DACOM_API BeginFlush(unsigned char /*Clear*/) override
        {
            // LAB_00451370.
        }

        void DACOM_API EndFlush(unsigned char /*Clear*/) override
        {
            // LAB_00451370.
        }

        void DACOM_API BeginRecord(void* /*Material*/, void* Object, void* Room) override;

        void DACOM_API EndRecord(void* /*Material*/, void* /*Object*/, void* /*Room*/) override
        {
            // LAB_0041a630.
        }
    };

    FRoomBatcherSink G_RoomBatcherSink;

    // FUN_0041a670 -- observer on, the room pass flag up.
    void RoomBatcherBegin()
    {
        IMaterialBatcher2* Batcher = DALib::MaterialBatcher;
        if (Batcher == nullptr)
            return;
        G_RoomBatchActive = 1;
        Batcher->SetBinOrder(1, 1);
        Batcher->SetEvents(static_cast<IMaterialBatcherEvents*>(&G_RoomBatcherSink));
    }

    // FUN_0041a6b0 -- flag down, flush every bin (so the sink sees each record), observer off.
    void RoomBatcherEnd()
    {
        IMaterialBatcher2* Batcher = DALib::MaterialBatcher;
        if (Batcher == nullptr)
            return;
        G_RoomBatchActive = 0;
        Batcher->FlushAll(1);
        Batcher->SetEvents(nullptr);
        Batcher->SetBinOrder(1, -1);
    }

    // FUN_0041a6f0(room, object) -- tag the records queued from here on.
    void RoomBatcherTagObject(FRoomRuntime* Room, FRoomObject* Object)
    {
        IMaterialBatcher2* Batcher = DALib::MaterialBatcher;
        if (Batcher == nullptr)
            return;
        Batcher->SetRecordTag(Object);
        Batcher->SetRecordTagContext(Room);
    }

    // FUN_0041a720 -- untag.
    void RoomBatcherUntagObject()
    {
        IMaterialBatcher2* Batcher = DALib::MaterialBatcher;
        if (Batcher == nullptr)
            return;
        Batcher->SetRecordTag(nullptr);
        Batcher->SetRecordTagContext(nullptr);
    }

    // FUN_0044ae80 -- fog enable, scene ambient and the lights for one object.
    void ApplyObjectLighting(FRoomRuntime* Room, FRoomObject* Object)
    {
        // Room fog active is FUN_00454700.
        int const bFog = (GfFogIsActive() && Object->bFog != 0) ? 1 : 0;
        DALib::Pipeline->SetRenderState(D3DRS_FOGENABLE, bFog);

        // The override's 1/255 is DAT_005c89c8.
        if (!Object->IsVisible())
        {
            GfSetSceneAmbient(0.0f, 0.0f, 0.0f);
        }
        else
        {
            int const* Override = Object->GetAmbientOverride();
            if (Override == nullptr)
            {
                GfSetSceneAmbient(Room->FrameAmbient[0], Room->FrameAmbient[1],
                                  Room->FrameAmbient[2]);
            }
            else
            {
                const double kInv255 = 1.0 / 255.0;
                GfSetSceneAmbient(static_cast<float>(static_cast<double>(Override[0]) * kInv255),
                                  static_cast<float>(static_cast<double>(Override[1]) * kInv255),
                                  static_cast<float>(static_cast<double>(Override[2]) * kInv255));
            }
        }

        // Lit objects take the best four frame lights of their group (FUN_005572b0), unlit ones
        // clear the light list (thunk FUN_004101e0).
        if (Object->bLit != 0)
        {
            const InstanceIndex Instance = Object->GetEngineInstance();
            GfLightsApplyNearest(Room->FrameLights, Room->FrameLightCount, Instance,
                                 Object->LightGroup, Room->Camera);
            return;
        }
        EngineLightClearRegisteredList();
    }

    void DACOM_API FRoomBatcherSink::BeginRecord(void* /*Material*/, void* Object, void* Room)
    {
        // LAB_0041a640. The record's two tags: the object and its room.
        DALib::Pipeline->SetRenderState(D3DRS_FOGENABLE, 0);
        if (Object != nullptr)
            ApplyObjectLighting(static_cast<FRoomRuntime*>(Room),
                                static_cast<FRoomObject*>(Object));
    }

    // FUN_00558000 is DrawFlashlightFlares (flash_light.cpp).

    // The FUN_0044b570 layer walk: every object of Room in DAT_00679a10 order whose layer byte
    // is Layer.
    template <typename FVisit>
    void ForEachRoomObjectInLayer(FRoomRuntime* Room, unsigned char Layer, FVisit Visit)
    {
        std::list<FRoomObject*>::iterator It = G_RoomObjects.begin();
        for (; It != G_RoomObjects.end(); ++It)
        {
            FRoomObject* Object = *It;
            if (Object->Owner != Room)
                continue;
            if (Object->Layer != Layer)
                continue;
            Visit(Object);
        }
    }
}

void FRoomRuntime::DrawObject(FRoomObject* Object)
{
    // FUN_0044af60.
    const InstanceIndex Instance = Object->GetEngineInstance();
    if (Instance == kRoomObjectNoInstance)
        return;

    ApplyObjectLighting(this, Object);

    // The glare light is RoomData +0x10c; appearance slot 6, otherwise slot 7(0).
    RoomData* Data = this->Room;
    if (RtcSlider::is_enabled("shadow_glare") && Data->bGlareLight)
    {
        // Engine slot 36 (+0x90): the instance position.
        const Vector* InstancePos = DALib::Engine->GetInstancePosition(Instance);
        float Pos[3] = {InstancePos->x, InstancePos->y, InstancePos->z};
        float Direction[3];
        if (Data->bGlareLightWorldSpaceDir)
        {
            // +0x10d: the world-space direction (+0x11c) as it is.
            Direction[0] = Data->GlareLightDirection.x;
            Direction[1] = Data->GlareLightDirection.y;
            Direction[2] = Data->GlareLightDirection.z;
        }
        else
        {
            // FUN_00423b60(out, instance position, light position +0x110), FUN_0041a390.
            Direction[0] = Pos[0] - Data->GlareLightPosition.x;
            Direction[1] = Pos[1] - Data->GlareLightPosition.y;
            Direction[2] = Pos[2] - Data->GlareLightPosition.z;
            RoomVec3Normalize(Direction);
        }
        float Color[3] = {Data->GlareLightColor.x, Data->GlareLightColor.y,
                          Data->GlareLightColor.z};
        IFLAppearance* Appearance = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance,
                                          reinterpret_cast<void**>(&Appearance)) >= 0)
            Appearance->SetDirectionalLight(Direction, Color);
        // FUN_00526a20: release and clear.
        if (Appearance != nullptr)
            Appearance->Release();
    }
    else
    {
        IFLAppearance* Appearance = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance,
                                          reinterpret_cast<void**>(&Appearance)) >= 0)
            Appearance->SetDirectionalLightEnabled(0);
        if (Appearance != nullptr)
            Appearance->Release();
    }

    int const Mode = Object->RenderMode;
    if (Mode == kRoomRenderMesh)
    {
        // Perf slot 0 is FUN_005b3510(0), compared with DAT_005c7474 (0.0f); appearance slot 8,
        // then slot 3.
        void* EnvValue = nullptr;
        if (Object->bEnvMap != 0 && GetPerfOptionSlot(kPerfSlotUseEnvironmentMaps) > 0.0f)
        {
            IFLAppearance* Appearance = nullptr;
            bool bHaveEnv = false;
            if (DALib::Engine->QueryInterface(Iids::FLAppearance,
                                              reinterpret_cast<void**>(&Appearance)) >= 0)
            {
                bHaveEnv = Appearance->GetOverrideMaterial(4, &EnvValue) >= 0;
                Appearance->Release();
                Appearance = nullptr;
                if (bHaveEnv)
                {
                    const InstanceIndex EnvInstance = Object->GetEngineInstance();
                    if (DALib::Engine->QueryInterface(Iids::FLAppearance,
                                                      reinterpret_cast<void**>(&Appearance)) >= 0)
                    {
                        Appearance->SetInstanceValue(static_cast<int>(EnvInstance),
                                          const_cast<char*>("envmapbasic"));
                    }
                    if (Appearance != nullptr)
                        Appearance->Release();
                }
            }
        }
        // Renderer slot 7.
        DALib::Renderer->RenderInstance(Camera, Instance, 1.0f, kDrawPickSwitch, nullptr, EnvValue);
    }
    else if (Mode == kRoomRenderStarsphere)
    {
        // Appearance slot 10; the QI result is tested for exactly 0 here.
        IFLAppearance* Appearance = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance,
                                          reinterpret_cast<void**>(&Appearance)) == 0)
            Appearance->DrawStarsphere(Camera, Instance);
        if (Appearance != nullptr)
            Appearance->Release();
    }
    else if (Mode == kRoomRenderBackdrop)
    {
        // Appearance slot 9.
        IFLAppearance* Appearance = nullptr;
        if (DALib::Engine->QueryInterface(Iids::FLAppearance,
                                          reinterpret_cast<void**>(&Appearance)) == 0)
            Appearance->DrawBackdrop(Camera, Instance);
        if (Appearance != nullptr)
            Appearance->Release();
    }
    else
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Invalid render type %d\n", kRoomControlCpp,
              0x6d1, Mode);
    }

    // Appearance slot 7(0) again.
    IFLAppearance* Appearance = nullptr;
    if (DALib::Engine->QueryInterface(Iids::FLAppearance,
                                      reinterpret_cast<void**>(&Appearance)) >= 0)
        Appearance->SetDirectionalLightEnabled(0);
    if (Appearance != nullptr)
        Appearance->Release();
}

void FRoomRuntime::DrawShadow(unsigned long A, unsigned long B, unsigned char bFlag)
{
    // FUN_0044b370. The shadow light is RoomData +0xe4.
    if (GetGameOption(kGameOptShadows) == 0)
        return;
    if (!RtcSlider::is_enabled("shadow_glare"))
        return;
    if (!Room->bShadowLight)
        return;
    IFLAppearance* Appearance = nullptr;
    if (DALib::Engine->QueryInterface(Iids::FLAppearance,
                                      reinterpret_cast<void**>(&Appearance)) >= 0)
    {
        // Appearance slot 12 with bFlag, else slot 11.
        unsigned long Pair[2] = {A, B};
        if (bFlag != 0)
            Appearance->DrawFixedShadows(Camera, Pair, reinterpret_cast<void*>(1),
                               reinterpret_cast<void*>(static_cast<intptr_t>(-1)));
        else
            Appearance->DrawFadingShadows(Camera, Pair, reinterpret_cast<void*>(1),
                               reinterpret_cast<void*>(static_cast<intptr_t>(-1)));
    }
    if (Appearance != nullptr)
        Appearance->Release();
}

void FRoomRuntime::LightObject(FRoomObject* Object)
{
    // FUN_0044ae80 (the body is ApplyObjectLighting above).
    ApplyObjectLighting(this, Object);
}

void FRoomRuntime::Draw()
{
    // FUN_0044b570.
    RoomBatcherBegin();

    // The camera is applied through FUN_00422950.
    Win3DSetViewport(Camera, kRoomControlCpp, 0x748);
    Win3DApplyCamera(Camera);
    Win3DSetDepthFunc(2);
    Win3DEnable(kWin3DDepthTest);
    Win3DSetDepthWrite(1);
    Win3DEnable(kWin3DCullFace);
    Win3DEnable(kWin3DBlend);
    Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);

    CollectLights(Camera);
    GfFogApply(); // FUN_00454760

    // The layer set's compare FUN_0044c520 is signed, so negative background layers precede
    // layer 0 in all three draw passes.
    std::set<signed char> Layers;
    std::list<FRoomObject*>::iterator It = G_RoomObjects.begin();
    for (; It != G_RoomObjects.end(); ++It)
    {
        if ((*It)->Owner == this)
            Layers.insert(static_cast<signed char>((*It)->Layer));
    }

    // Slot 7, each object tagged into the batcher records.
    std::set<signed char>::iterator LayerIt = Layers.begin();
    for (; LayerIt != Layers.end(); ++LayerIt)
    {
        ForEachRoomObjectInLayer(this, static_cast<unsigned char>(*LayerIt), [this](FRoomObject* Object) {
            RoomBatcherTagObject(this, Object);
            Object->DrawOpaque(Camera);
            RoomBatcherUntagObject();
        });
    }
    RoomBatcherEnd();

    // Slot 8.
    for (LayerIt = Layers.begin(); LayerIt != Layers.end(); ++LayerIt)
    {
        ForEachRoomObjectInLayer(this, static_cast<unsigned char>(*LayerIt), [this](FRoomObject* Object) {
            Object->DrawTranslucent(Camera);
        });
    }

    // FUN_00558000.
    if (GetGameOption(kGameOptEffectsGroup) != 0 && GetGameOption(kGameOptFlashlights) > 0)
        DrawFlashlightFlares(Flashlights, FlashlightCount, Camera);

    // The same states again; FUN_00556f20 ambient, thunk FUN_004101e0.
    Win3DSetDepthFunc(2);
    Win3DEnable(kWin3DDepthTest);
    Win3DSetDepthWrite(1);
    Win3DEnable(kWin3DCullFace);
    Win3DEnable(kWin3DBlend);
    Win3DSetBlendFunc(kWin3DBlendSrcAlpha, kWin3DBlendInvSrcAlpha);
    GfSetSceneAmbient(1.0f, 1.0f, 1.0f);
    EngineLightClearRegisteredList();

    IRenderPipeline* Pipe = DALib::Pipeline;
    int SavedDither = 0;
    Pipe->GetRenderState(D3DRS_DITHERENABLE, &SavedDither);
    Pipe->SetRenderState(D3DRS_DITHERENABLE, 0);

    // Alchemy slots 29 / 30; object slot 9 per layer.
    if (DALib::Alchemy->BeginDraw() >= 0)
    {
        for (LayerIt = Layers.begin(); LayerIt != Layers.end(); ++LayerIt)
        {
            ForEachRoomObjectInLayer(this, static_cast<unsigned char>(*LayerIt), [this](FRoomObject* Object) {
                Object->DrawEffects(Camera);
            });
        }
        G_GfScriptManager.DrawEffects(this, Camera); // FUN_00456740
        DALib::Alchemy->EndDraw();
    }

    Pipe->SetRenderState(D3DRS_DITHERENABLE, SavedDither);
}
