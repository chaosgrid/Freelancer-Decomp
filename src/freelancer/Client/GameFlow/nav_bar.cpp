// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "GameFlow/nav_bar.h"

#include "Interface/Pages/character_select_equip.h" // GetPlayerShipArchetypeIdRetail (FUN_004c3e10), visit flags
#include "NeuroNet/Economy/client_base_market.h"     // GetClientCurrentLocationBaseData (FUN_0043b630)
#include "Common/Graphics/fade_to_black.h"          // SetFadeTarget (FUN_00412050), GetFadeDuration (DAT_0061650c)
#include "GameFlow/gameflow.h"               // RequestGameflowMissionBriefing (FUN_00558a30)
#include "GameFlow/gf_character_object.h"    // FCharacterObject, PlacePlayerAtSetpoint (FUN_00435cf0)
#include "GameFlow/gf_object_ship.h"         // GfObjectShip (FUN_00446be0), PlayLaunchScript (FUN_00447cd0)
#include "GameFlow/Thorn/gf_script_object.h"       // G_GfScriptManager (DAT_0066d2d0), FGfScriptSequence
#include "Core/globals.h"                // GetClientFrameTimeCapped (FUN_0042d680)
#include "Space/HUD/hud_command_dispatch.h"   // HudPostCommand (FUN_005763b0)
#include "Space/HUD/hud_maneuvers.h"          // FHudNamedValue, HudLookupNamedValue (FUN_004d9ca0)
#include "Space/HUD/hud_target.h"             // HudIsMousePickBlocked (FUN_0045a740)
#include "Common/input_manager.h"          // Get/SetClientMouseCursorDrawFlag (FUN_0041dd90 / FUN_0041dda0)
#include "Interface/menu_ui_init.h"           // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "Interface/menu_ui_widgets.h"
#include "NeuroNet/nn_rollover.h"            // NnRolloverShowIdsGlobal (FUN_004b62f0)
#include "GameFlow/menu_universe_base.h"     // GetClientCurrentLocationRuntime (FUN_0043a460 / FUN_00438c00)
#include "GameFlow/mirror_property_bucket.h"
#include "NeuroNet/nn_button_control.h"      // NnButtonControl (+0x330 bits)
#include "NeuroNet/nn_inventory.h"           // NnInventoryOpenDealer (FUN_00477ab0) / OpenShipDealer (FUN_00477bf0)
#include "NeuroNet/nn_messaging.h"           // NnVoiceEnqueue (FUN_00489e80)
#include "NeuroNet/nn_modal.h"               // NnMainButtonOnModalOpen (FUN_0045a460), G_bNnScreenUp, NnFloatBits
#include "NeuroNet/nn_repair.h"              // NnRepair::PlayerNeedsRepair (FUN_004b4510)
#include "NeuroNet/nn_tooltip.h"             // NnToolTipHideGlobal (FUN_004be390)
#include "GameFlow/room_control.h"
#include "GameFlow/room_control_hotspot.h"  // EHotspotBehavior
#include "Interface/win3d.h"                  // Win3DFindByName (FUN_0059da10), Win3DDeleteTree (FUN_0043d2a0)
#include "Interface/win_factory.h"            // WINFACTORY, WinFactoryCreate (FUN_0059db20)

#include "DALib.h"
#include "common_archetype.h"       // Archetype::GetEquipment
#include "common_csys.h"            // Csys, Acquire3DMathEngine (FUN_005b73e0)
#include "common_goods.h"           // RoomData, RoomButtonInfo, BaseData, EquipDesc
#include "common_id.h"              // CreateID, MakeLocationID
#include "common_ini.h"             // INI_Reader
#include "common_misc.h"            // GetFileSysPath
#include "dacom.h"                  // FDUMP
#include "engine/engbase.h"
#include "x86math.h"

#include <stdio.h>
#include <string.h>

// WINFACTORY<NavBar> DAT_006687b4 (LAB_0043d6b0: FUN_004429a0(&DAT_006687b4, "NavBar"), atexit
// LAB_0043d6d0 -> FUN_00579380).
WINFACTORY<NavBar> G_NavBarFactory("NavBar");

namespace
{
    char const kNavBarCpp[] = "E:\\FL\\Scratch\\Source\\Client\\GF\\NavBar.cpp"; // 0x005cb860

    // DAT_00668828 -- FUN_00441ea0's lazily acquired 3DMathEngine (FUN_005b73e0).
    I3DMathEngine* G_NavBarMath = nullptr;

    // DAT_005cb4d0 (0x3e99999a) -- every slide and scale; DAT_005cb4d4 -- the layout delay.
    float const kAnimSeconds = NnFloatBits(0x3e99999a);
    float const kLayoutDelay = NnFloatBits(0x3e800000);

    // LAB_0043d5a0 / LAB_0043d5b0 / LAB_0043d5c0 -- three float statics of this translation unit
    // (DAT_00668824 = DAT_00668820 = [0x005cb4c4] -0.6, DAT_0066881c = [0x005cb4c8] 0.6). Each
    // has one XREF, its initializer's write; nothing reads them (the same trio as NN_Repair's and
    // NN_MissionVendor's).
    float G_NavBarStatic668824 = NnFloatBits(0xbf19999a);
    float G_NavBarStatic668820 = NnFloatBits(0xbf19999a);
    float G_NavBarStatic66881c = NnFloatBits(0x3f19999a);

    // The launch checks' voice messages, CreateID'd at load (LAB_0043d5d0 / LAB_0043d5f0 /
    // LAB_0043d610).
    unsigned const G_NavBarVoiceNoEngine = CreateID("must_have_mounted_engine_to_launch"); // DAT_00668818
    unsigned const G_NavBarVoiceNoPower =
        CreateID("must_have_mounted_power_plant_to_launch");                                // DAT_00668814
    unsigned const G_NavBarVoiceNoScanner = CreateID("must_have_mounted_scanner_to_launch"); // DAT_00668810

    // Where an opened service tab slides to (LAB_0043d630 / LAB_0043d650 / LAB_0043d670 /
    // LAB_0043d690).
    Vector const G_NavBarRepairTarget = {0.0f, NnFloatBits(0xbeb33333), 0.0f};  // DAT_00668804 (0xc)
    Vector const G_NavBarShipTarget = {0.0f, NnFloatBits(0xbeb33333), 0.0f};    // DAT_006687f8 (7)
    Vector const G_NavBarNewsTarget = {0.0f, NnFloatBits(0xbecccccd), 0.0f};    // DAT_006687ec (2)
    Vector const G_NavBarMissionTarget = {0.0f, NnFloatBits(0xbecccccd), 0.0f}; // DAT_006687e0 (3)

    // DAT_00610bf0 -- the maneuver tab mesh for 1..3 service tabs.
    char const* const kTabMeshes[3] = {"Interface\\Baseside\\CITY_maneuvertab1.cmp",
                                       "Interface\\Baseside\\CITY_maneuvertab2.cmp",
                                       "Interface\\Baseside\\CITY_maneuvertab3.cmp"};

    // DAT_00610c10 -- the tabs' x offsets for 1..3 tabs: DAT_0066882c (.bss, never written),
    // DAT_00610bfc, DAT_00610c04.
    float G_NavBarTabOffset1[1] = {0.0f};
    float const kTabOffset2[2] = {NnFloatBits(0xbc9374bc), NnFloatBits(0x3c9374bc)};
    float const kTabOffset3[3] = {NnFloatBits(0xbd178d50), 0.0f, NnFloatBits(0x3d178d50)};
    float const* const kTabOffsets[3] = {G_NavBarTabOffset1, kTabOffset2, kTabOffset3};

    // DAT_00610c1c -- the room buttons' x offsets for 2, 3, 4 and 5 buttons, one
    // contiguous run as in retail (row 2 at [0], row 3 at [2], row 4 at [5], row 5 at [9]).
    // FUN_0043e4c0 caps its count at 5 but walks every room button, so a sixth..ninth button
    // reads on past row 5 into the pointer table DAT_00610c50 (+4..+0x10): those four dwords
    // are kept here with retail's bits (a ninth button reads index 17, 0x00610c60).
    float const kRowOffsetData[18] = {
        NnFloatBits(0x3d051eb8), NnFloatBits(0xbd051eb8),                          // 2
        NnFloatBits(0x3d851eb8), 0.0f, NnFloatBits(0xbd851eb8),                    // 3
        NnFloatBits(0x3dc7ae14), NnFloatBits(0x3d051eb8), NnFloatBits(0xbd051eb8),
        NnFloatBits(0xbdc7ae14),                                                   // 4
        NnFloatBits(0x3e051eb8), NnFloatBits(0x3d851eb8), 0.0f, NnFloatBits(0xbd851eb8),
        NnFloatBits(0xbe051eb8),                                                   // 5
        NnFloatBits(0x00668830), NnFloatBits(0x00610c1c), NnFloatBits(0x00610c24),
        NnFloatBits(0x00610c30)};                                                  // past row 5
    // DAT_00668830 -- row 1 (.bss, never written).
    float G_NavBarRowOffset1[1] = {0.0f};
    // DAT_00610c50 -- [count] for 1..5; [0] is the dword 0xbe051eb8 (row 5's last float, never
    // dereferenced: a zero count walks no button).
    float const* const kRowOffsets[6] = {nullptr, G_NavBarRowOffset1, &kRowOffsetData[0],
                                         &kRowOffsetData[2], &kRowOffsetData[5],
                                         &kRowOffsetData[9]};

    // DAT_00610c68 -- the maneuver frame mesh for 1..5 room buttons.
    char const* const kBoxMeshes[5] = {"Interface\\HUD\\HUD_maneuverbox2.cmp",
                                       "Interface\\HUD\\HUD_maneuverbox3.cmp",
                                       "Interface\\HUD\\HUD_maneuverbox4.cmp",
                                       "Interface\\HUD\\HUD_maneuverbox5.cmp",
                                       "Interface\\HUD\\HUD_maneuverbox6.cmp"};

    // DAT_00610c80 -- 0x13 {IDS name, tooltip ids} pairs (FUN_004d9ca0 is a stricmp walk).
    FHudNamedValue const kHotspotIds[0x13] = {
        {"IDS_HOTSPOT_EXIT", 0x1965},
        {"IDS_HOTSPOT_BAR", 0x196a},
        {"IDS_HOTSPOT_COMMODITYTRADER", 0x1969},
        {"IDS_HOTSPOT_EQUIPMENTDEALER", 0x1968},
        {"IDS_HOTSPOT_SHIPDEALER", 0x196b},
        {"IDS_HOTSPOT_PLANETSCAPE", 0x1976},
        {"IDS_HOTSPOT_DECK", 0x1977},
        {"IDS_HOTSPOT_LAUNCH", 0x196c},
        {"IDS_NN_REPAIR_YOUR_SHIP", 0x475},
        {"IDS_HOTSPOT_NEWSVENDOR", 0x1966},
        {"IDS_HOTSPOT_MISSIONVENDOR", 0x1967},
        {"IDS_COMMODITYTRADER_NAME", 0x48b},
        {"IDS_DEALER_FRONT_DESK", 0x196e},
        {"IDS_EQUIPMENTDEALER_NAME", 0x48c},
        {"IDS_SHIPDEALER_NAME", 0x48d},
        {"IDS_EQUIPMENT_ROOM_RIGHT", 0x40e},
        {"IDS_HOTSPOT_COMMODITYTRADER_ROOM", 0x527},
        {"IDS_HOTSPOT_EQUIPMENTDEALER_ROOM", 0x528},
        {"IDS_HOTSPOT_SHIPDEALER_ROOM", 0x529}};

    // FUN_0043a460 / FUN_00438c00 (identical bodies) -- the current room's runtime.
    FRoomRuntime* CurrentRoomRuntime()
    {
        return static_cast<FRoomRuntime*>(GetClientCurrentLocationRuntime());
    }

    // FUN_0043b630 then its first dword -- the id of the base the player is at.
    unsigned CurrentBaseId()
    {
        return GetClientCurrentLocationBaseData()->get_base_id();
    }

    // The inline FUN_0059da10(Name) -> slot 2 -> SetProperty(0x4e, 0, 0) of FUN_0043e600 and
    // OnButtonClicked.
    void NotifyServiceWindow(char const* Name)
    {
        Win3D* const Found = Win3DFindByName(Name);
        if (Found == nullptr)
            return;
        WinControl* const Control = Found->AsWinControl();
        if (Control == nullptr)
            return;
        Control->SetProperty(0x4e, 0, 0);
    }

    // A Win3D shown and parented: [+0x6c] & 2 and [+0x04].
    bool IsShownWindow(Win3D const* Window)
    {
        return Window != nullptr && (Window->Flags & 2) != 0 && Window->GetParent() != nullptr;
    }

    // FUN_0059da10("NavBar") -> slot 2 (the wrappers' shared head).
    NavBar* FindNavBar()
    {
        Win3D* const Found = Win3DFindByName("NavBar");
        if (Found == nullptr)
            return nullptr;
        WinControl* const Control = Found->AsWinControl();
        if (Control == nullptr)
            return nullptr;
        return static_cast<NavBar*>(Control);
    }

    // FUN_004422b0 (cdecl) -- the player's ship may launch: a copy of the equipment list
    // (FUN_004c4060) walked for an equipped (+0x0c) engine (class 0xc), power plant (0xb) and
    // scanner (0x14); the first one missing is voiced (FUN_00489e80) and its rollover shown
    // for 8 seconds (FUN_004b62f0), false. Caller: the launch tab.
    bool CheckLaunchEquipment()
    {
        bool bEngine = false;
        bool bPower = false;
        bool bScanner = false;
        std::list<EquipDesc> const Items = GetPlayerEquipDescListRetail().Items;
        std::list<EquipDesc>::const_iterator It;
        for (It = Items.begin(); It != Items.end(); ++It)
        {
            if (!It->is_equipped())
                continue;
            Archetype::Equipment* const Arch = Archetype::GetEquipment(It->get_arch_id());
            if (Arch == nullptr)
                continue;
            Archetype::AClassType const Class = Arch->get_class_type();
            if (Class == Archetype::AClassPower)
                bPower = true;
            else if (Class == Archetype::AClassEngine)
                bEngine = true;
            else if (Class == Archetype::AClassScanner)
                bScanner = true;
        }
        if (!bEngine)
        {
            NnVoiceEnqueue(G_NavBarVoiceNoEngine, 0);
            NnRolloverShowIdsGlobal(0x49d, NnFloatBits(0x41000000));
            return false;
        }
        if (!bPower)
        {
            NnVoiceEnqueue(G_NavBarVoiceNoPower, 0);
            NnRolloverShowIdsGlobal(0x49e, NnFloatBits(0x41000000));
            return false;
        }
        if (!bScanner)
        {
            NnVoiceEnqueue(G_NavBarVoiceNoScanner, 0);
            NnRolloverShowIdsGlobal(0x49f, NnFloatBits(0x41000000));
            return false;
        }
        return true;
    }

    // The service types FUN_004424f0 / OnButtonClicked close (their jump tables send 2, 3, 7
    // and 0xc to the close, everything else past it).
    bool IsServiceType(unsigned Type)
    {
        return Type == kHotspotNewsVendor || Type == kHotspotMissionVendor ||
               Type == kHotspotStartShipDealer || Type == kHotspotRepair;
    }
}

FNavBarButton::FNavBarButton()
    : bDisabled(0)
    , bRoomSwitch(1)
    , Name()
    , IdsName(nullptr)
    , Control(nullptr)
    , Hotspot(nullptr)
    , bOpen(0)
    , Type(0)
{
    // FUN_0043fd00: +0x24 Position is not written.
}

NavBar::NavBar()
    : WinControl()
    , ButtonMover()
    , ButtonScaler()
    , VirtualRoom()
    , Buttons()
    , IniValues()
    , RoomControlNames()
    , ButtonControlNames()
    , Current(nullptr)
    , bFadingOut(0)
    , FadeTimer(0.0f)
    , bFadingIn(0)
    , bVirtualRoomPending(0)
    , VirtualRoomLocation(0)
    , bScriptRunning(0)
    , LayoutDelay(0.0f)
    , bRepairRedirect(0)
    , bReturnToDesk(0)
    , ManeuverFrame(nullptr)
    , ManeuverTab(nullptr)
    , PendingCharacter(nullptr)
    , bCharacterPending(0)
    , ScaledBackground(nullptr)
    , ClosingButton(nullptr)
    , QueuedClose(nullptr)
    , QueuedOpen(nullptr)
{
    // FUN_0043d6e0. +0x39c DefaultMesh is left unwritten (Create sets it).
}

NavBar::~NavBar()
{
    // FUN_0043da70. The background subtree first.
    if (ScaledBackground != nullptr)
    {
        Win3DDeleteTree(ScaledBackground);
        ScaledBackground = nullptr;
    }
    // Every IniValues buffer freed, then the map emptied. The lists,
    // the string and the mover / scaler vectors follow in reverse declaration order, then
    // WinControl's dtor (FUN_0059fa50).
    std::map<std::string, char*>::iterator It;
    for (It = IniValues.begin(); It != IniValues.end(); ++It)
        delete[] It->second;
    IniValues.clear();
}

bool NavBar::Create(Win3D* NewParent, void* Data)
{
    // LAB_0043dc40. "ManeuverFrame" at (0, 0.176, -0.5).
    Vector const HalfExtents = {0.0f, 0.0f, 0.0f};
    Vector const Zero338 = {0.0f, 0.0f, 0.0f};
    Vector const FramePosition = {0.0f, NnFloatBits(0x3e343958), NnFloatBits(0xbf000000)};
    ManeuverFrame = CreateStaticControl("ManeuverFrame", "Interface\\HUD\\HUD_maneuverbox6.cmp",
                                        &FramePosition, nullptr, &HalfExtents, 0, 0, 0, 1, 0, 0, 1,
                                        0, 0, 0, &Zero338);
    // "ManeuverTab" at (0, 0.283, -1.0), hidden (property 1).
    Vector const TabPosition = {0.0f, NnFloatBits(0x3e90624e), NnFloatBits(0xbf800000)};
    ManeuverTab = CreateStaticControl("ManeuverTab", "Interface\\Baseside\\CITY_maneuvertab3.cmp",
                                      &TabPosition, nullptr, &HalfExtents, 0, 0, 0, 1, 0, 0, 1, 0,
                                      0, 0, &Zero338);
    ManeuverTab->SetProperty(kWinPropHide, 1, 0);

    // Win3D::Create (FUN_004b5060); on failure both children deleted
    // (their pointers are not cleared) and false.
    if (!Win3D::Create(NewParent, Data))
    {
        if (ManeuverFrame != nullptr)
            Win3DDeleteTree(ManeuverFrame);
        if (ManeuverTab != nullptr)
            Win3DDeleteTree(ManeuverTab);
        return false;
    }

    // The layout ini through slot 28.
    char Path[0x100];
    GetFileSysPath(Path, "interface\\baseside\\navbar.ini", nullptr);
    LoadIni(Path);

    // The same file's [NavBar] values into IniValues (FUN_0043f060).
    INI_Reader Ini;
    if (Ini.open(Path, false) && Ini.find_header("NavBar") && Ini.read_value())
    {
        do
        {
            char const* const Name = Ini.get_name_ptr();
            SetIniValue(Name, Ini.get_value_string());
        } while (Ini.read_value());
    }

    // The mesh an unconfigured button shows.
    DefaultMesh = "interface\\baseside\\unknown.3db";
    return true;
}

void NavBar::OnLocationEntered()
{
    // LAB_0043dfe0.
    bReturnToDesk = 0;
    if (CurrentRoomRuntime() == nullptr)
        return;
    if (GetClientCurrentLocationBaseData() == nullptr)
        return;
    Rebuild();
    // FUN_0043a460 again, its result unused.
    CurrentRoomRuntime();
    // Current = the first button whose hotspot reads state 1.
    FNavBarButton* Found = nullptr;
    std::list<FNavBarButton>::iterator It;
    for (It = Buttons.begin(); It != Buttons.end(); ++It)
    {
        if (It->Hotspot != nullptr && It->Hotspot->StateRead == 1)
        {
            Found = &*It;
            break;
        }
    }
    Current = Found;
    Refresh();
}

void NavBar::Slot46(unsigned /*A*/)
{
    // FUN_0053e4a0 (empty).
}

void NavBar::Refresh()
{
    // FUN_0043e050.
    std::list<FNavBarButton>::iterator It;
    if (VirtualRoom.size() == 0)
    {
        // A room button is highlighted (0xc) when it is the current
        // location (DAT_00668740).
        unsigned const Location = GetClientLocationCurrentRoom();
        for (It = Buttons.begin(); It != Buttons.end(); ++It)
        {
            FNavBarButton& Button = *It;
            if (Button.bRoomSwitch != 0)
            {
                Button.Control->SetProperty(0xc, Button.Type == Location ? 1 : 0, 0);
            }
            else if (Button.Type == kHotspotFrontDesk)
            {
                Button.bDisabled = 1;
                Button.Control->SetProperty(kWinPropDisable, 1, 0);
            }
            else if (Button.Type == kHotspotRepair)
            {
                bool const bNeedsRepair = NnRepair::PlayerNeedsRepair();
                Button.Control->SetProperty(kWinPropDisable, bNeedsRepair ? 0 : 1, 0);
                Button.bDisabled = bNeedsRepair ? 0 : 1;
            }
            else
            {
                Button.Control->SetProperty(kWinPropDisable, 0, 0);
            }
        }
    }
    else
    {
        // Inside a virtual room the room button whose hotspot's
        // room_switch strcmp-equals it is highlighted (the hotspot is not null-tested).
        for (It = Buttons.begin(); It != Buttons.end(); ++It)
        {
            FNavBarButton& Button = *It;
            if (Button.bRoomSwitch != 0)
            {
                char const* const Switch = Button.Hotspot->RoomSwitch;
                bool const bHere = Switch != nullptr && strcmp(VirtualRoom.c_str(), Switch) == 0;
                Button.Control->SetProperty(0xc, bHere ? 1 : 0, 0);
            }
            else if (Button.Type == kHotspotFrontDesk)
            {
                Button.bDisabled = 1;
                Button.Control->SetProperty(kWinPropDisable, 1, 0);
            }
            else if (Button.Type == kHotspotRepair)
            {
                bool const bNeedsRepair = NnRepair::PlayerNeedsRepair();
                Button.Control->SetProperty(kWinPropDisable, bNeedsRepair ? 0 : 1, 0);
                Button.bDisabled = bNeedsRepair ? 0 : 1;
            }
            else
            {
                Button.Control->SetProperty(kWinPropDisable, 0, 0);
            }
        }
    }
    LayoutRoomButtons();
    LayoutTabs(true);
}

void NavBar::LayoutTabs(bool bAnimate)
{
    // FUN_0043e210. The selected room button (+0x330 bit
    // 0x40). The first room button selected -> step -0.01 and stop; the selected one being the
    // last room button -> +0.01; otherwise 0 (reset at every room button). No selection ->
    // return.
    FNavBarButton* Selected = nullptr;
    FNavBarButton* Last = nullptr;
    float Step = 0.0f;
    bool bFirst = true;
    bool bFirstSelected = false;
    std::list<FNavBarButton>::iterator It;
    for (It = Buttons.begin(); It != Buttons.end(); ++It)
    {
        FNavBarButton& Button = *It;
        if (Button.bRoomSwitch == 0)
            continue;
        Step = 0.0f;
        if ((static_cast<NnButtonControl*>(Button.Control)->GetButtonBits() & 0x40) != 0)
        {
            Selected = &Button;
            if (bFirst)
            {
                Step = NnFloatBits(0xbc23d70a);
                bFirstSelected = true;
                break;
            }
        }
        Last = &Button;
        bFirst = false;
    }
    if (!bFirstSelected && Last == Selected)
        Step = NnFloatBits(0x3c23d70a);
    if (Selected == nullptr)
        return;

    // Its offset (property 7).
    Vector Base = {0.0f, 0.0f, 0.0f};
    Selected->Control->SetProperty(kWinPropGetOffset, reinterpret_cast<uintptr_t>(&Base), 0);

    // The enabled service tabs; 1..3 of them, else return.
    int Count = 0;
    for (It = Buttons.begin(); It != Buttons.end(); ++It)
    {
        if (It->bRoomSwitch == 0 && It->bDisabled == 0)
            ++Count;
    }
    if (Count <= 0 || Count >= 4)
        return;
    int const TabIndex = Count - 1;
    if (TabIndex > 0)
        Base.x = Base.x + Step;

    // The tab mesh and its offset (x, 0.295, -1.0).
    ManeuverTab->SetProperty(kWinPropMesh, reinterpret_cast<uintptr_t>(kTabMeshes[TabIndex]), 0);
    Vector const TabPosition = {Base.x, NnFloatBits(0x3e970a3d), NnFloatBits(0xbf800000)};
    ManeuverTab->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&TabPosition), 0);

    float const* Offset = kTabOffsets[TabIndex];
    ButtonMover::Start(kAnimSeconds);
    LayoutDelay = kLayoutDelay;

    // Every enabled tab laid out 0.065 below the selected button at
    // its x offset (slid there when bAnimate, placed otherwise); the desk (type 6) and dealer
    // (5 / 7 / 8) tabs remembered, enabled or not.
    FNavBarButton* DeskTab = nullptr;
    FNavBarButton* DealerTab = nullptr;
    for (It = Buttons.begin(); It != Buttons.end(); ++It)
    {
        FNavBarButton& Button = *It;
        if (Button.bRoomSwitch != 0)
            continue;
        if (Button.bDisabled == 0)
        {
            Vector End;
            if (bAnimate)
            {
                Button.Control->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Base), 0);
                End.x = *Offset + Base.x;
                End.y = Base.y - NnFloatBits(0x3d851eb8);
                End.z = Base.z;
                ++Offset;
                ButtonMover::AddButton(Button.Control, &Base, &End);
            }
            else
            {
                End.x = *Offset + Base.x;
                End.y = Base.y - NnFloatBits(0x3d851eb8);
                End.z = Base.z;
                ++Offset;
                Button.Control->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&End), 0);
            }
            Button.Position.x = End.x;
            Button.Position.y = End.y;
            Button.Position.z = End.z;
        }
        if (Button.Type == kHotspotFrontDesk)
            DeskTab = &Button;
        if (Button.Type == kHotspotStartDealer || Button.Type == kHotspotStartShipDealer ||
            Button.Type == kHotspotStartEquipDealer)
            DealerTab = &Button;
    }

    // While sliding, the desk tab starts where the dealer tab sits.
    if (bAnimate && DeskTab != nullptr && DealerTab != nullptr)
        DeskTab->Control->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&DealerTab->Position), 0);
}

void NavBar::LayoutRoomButtons()
{
    // FUN_0043e4c0. The room buttons, counted up to 5.
    int Count = 0;
    std::list<FNavBarButton>::iterator It;
    for (It = Buttons.begin(); It != Buttons.end(); ++It)
    {
        if (It->bRoomSwitch != 0)
            ++Count;
    }
    if (Count > 5)
        Count = 5;

    // Each at (offset, 0.007, 0), walking every room button.
    float const* Offset = kRowOffsets[Count];
    for (It = Buttons.begin(); It != Buttons.end(); ++It)
    {
        if (It->bRoomSwitch == 0)
            continue;
        Vector const Position = {*Offset, NnFloatBits(0x3be56042), 0.0f};
        It->Control->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Position), 0);
        ++Offset;
    }

    // The frame mesh DAT_00610c68[count - 1]. With no room button
    // retail reads index -1, the dword 0x00610c40 -- row 5's offsets taken as a mesh name
    // (a retail defect, reproduced).
    char const* const Mesh =
        Count > 0 ? kBoxMeshes[Count - 1] : reinterpret_cast<char const*>(&kRowOffsetData[9]);
    ManeuverFrame->SetProperty(kWinPropMesh, reinterpret_cast<uintptr_t>(Mesh), 0);
}

void NavBar::RefreshRepairTab(bool bForce)
{
    // FUN_0043e570: the first repair tab (type 0xc) disabled unless the ship is
    // damaged and bForce is clear; a changed state re-lays the tabs without sliding.
    std::list<FNavBarButton>::iterator It;
    for (It = Buttons.begin(); It != Buttons.end(); ++It)
    {
        FNavBarButton& Button = *It;
        if (Button.bRoomSwitch != 0 || Button.Type != kHotspotRepair)
            continue;
        bool const bNeedsRepair = NnRepair::PlayerNeedsRepair();
        unsigned char const bDisable = (bNeedsRepair && !bForce) ? 0 : 1;
        Button.Control->SetProperty(kWinPropDisable, bDisable, 0);
        unsigned char const bOld = Button.bDisabled;
        Button.bDisabled = bDisable;
        if (bOld != bDisable)
            LayoutTabs(false);
        return;
    }
}

void NavBar::NotifyServiceWindows()
{
    // FUN_0043e600.
    NotifyServiceWindow("NN_Repair");
    NotifyServiceWindow("NN_ShipTrader");
    NotifyServiceWindow("NN_News");
    NotifyServiceWindow("NN_MissionVendor");
}

void NavBar::Draw()
{
    // FUN_0043e6c0: WinControl::Draw (FUN_005a0760) only while the cursor shows, no fade runs
    // and the HUD does not block picking.
    if (GetClientMouseCursorDrawFlag() == 0)
        return;
    if (bFadingOut != 0 || bFadingIn != 0)
        return;
    if (HudIsMousePickBlocked())
        return;
    WinControl::Draw();
}

void NavBar::Update()
{
    // LAB_0043e700. The children first (FUN_005a17b0 inlined).
    Win3D::Update();

    // Everything hidden unless the cursor shows, no fade runs and no
    // NeuroNet screen is up; the tab mesh also needs an enabled tab; disabled buttons re-greyed.
    bool const bShow = GetClientMouseCursorDrawFlag() != 0 && bFadingOut == 0 &&
                       bFadingIn == 0 && !G_bNnScreenUp;
    uintptr_t const Hide = bShow ? 0 : 1;
    ManeuverFrame->SetProperty(kWinPropHide, Hide, 0);
    int EnabledTabs = 0;
    std::list<FNavBarButton>::iterator It;
    for (It = Buttons.begin(); It != Buttons.end(); ++It)
    {
        if (It->bRoomSwitch == 0 && It->bDisabled == 0)
            ++EnabledTabs;
    }
    ManeuverTab->SetProperty(kWinPropHide, (bShow && EnabledTabs != 0) ? 0 : 1, 0);
    for (It = Buttons.begin(); It != Buttons.end(); ++It)
    {
        It->Control->SetProperty(kWinPropHide, Hide, 0);
        if (It->bDisabled != 0)
            It->Control->SetProperty(kWinPropDisable, 1, 0);
    }

    // With the bar up, the return to the desk tab and the animations.
    if (GetClientMouseCursorDrawFlag() != 0 && bFadingOut == 0 && bFadingIn == 0 &&
        !G_bNnScreenUp)
    {
        if (bReturnToDesk != 0)
        {
            // An open repair tab, or no animation running.
            bool bRepairOpen = false;
            for (It = Buttons.begin(); It != Buttons.end(); ++It)
            {
                if (It->bRoomSwitch == 0 && It->bOpen != 0 && It->Type == kHotspotRepair)
                    bRepairOpen = true;
            }
            if (bRepairOpen || !IsAnimating())
            {
                // No dealer window -> the desk tab (type 6) activated.
                if (Win3DFindByName("NN_Dealer") == nullptr &&
                    Win3DFindByName("NN_ShipTrader") == nullptr)
                {
                    bReturnToDesk = 0;
                    for (It = Buttons.begin(); It != Buttons.end(); ++It)
                    {
                        if (It->bRoomSwitch == 0 && It->Type == kHotspotFrontDesk)
                        {
                            ActivateButton(Current, &*It);
                            break;
                        }
                    }
                }
            }
        }
        // The layout delay counts down, then the animations run.
        if (LayoutDelay > 0.0f)
            LayoutDelay = static_cast<float>(static_cast<double>(LayoutDelay) -
                                             GetClientFrameTimeCapped());
        else
        {
            ButtonMover::Update();
            ButtonScaler::Update();
        }
    }

    // A running fade; once FadeTimer (kept at extended precision for
    // the compare) reaches the fade duration, the fade-out moves the camera to the desk and
    // the fade-in shows the cursor and enters Current's room.
    if (bFadingOut != 0 || bFadingIn != 0)
    {
        double const Elapsed = GetClientFrameTimeCapped() + static_cast<double>(FadeTimer);
        FadeTimer = static_cast<float>(Elapsed);
        if (Elapsed >= static_cast<double>(GetFadeDuration()))
        {
            if (bFadingOut != 0)
            {
                bFadingOut = 0;
                CameraToDesk();
            }
            else if (bFadingIn != 0)
            {
                bFadingIn = 0;
                SetClientMouseCursorDrawFlag(1);
                SelectRoomButton(Current);
            }
        }
    }

    // A virtual room entered by a room button, once the location
    // changed -- its desk camera and the player at its desk (three independent tests).
    if (bVirtualRoomPending != 0 && VirtualRoomLocation != GetClientLocationCurrentRoom())
    {
        bVirtualRoomPending = 0;
        if (VirtualRoom.size() != 0)
        {
            FRoomRuntime* const Room = CurrentRoomRuntime();
            if (_stricmp(VirtualRoom.c_str(), "Trader") == 0) // DACOM stricmp
            {
                RoomControlSnapCamera(Room, "Camera_Trader_Desk");
                PlacePlayerAtSetpoint("Player_Trader");
            }
            if (_stricmp(VirtualRoom.c_str(), "Equipment") == 0)
            {
                RoomControlSnapCamera(Room, "Camera_Equip_Desk");
                PlacePlayerAtSetpoint("Player_Equip");
            }
            if (_stricmp(VirtualRoom.c_str(), "ShipDealer") == 0)
            {
                RoomControlSnapCamera(Room, "Camera_Ship_Desk");
                PlacePlayerAtSetpoint("Player_Ship");
            }
        }
    }

    // A repair redirect waiting for the equipment room opens the
    // repair tab once the bar is idle there.
    if (bRepairRedirect != 0 && bFadingOut == 0 && bFadingIn == 0 && LayoutDelay <= 0.0f &&
        !IsAnimating() && GetClientMouseCursorDrawFlag() != 0 && bFadingOut == 0 &&
        bFadingIn == 0 && !G_bNnScreenUp)
    {
        bool bInEquipment = false;
        if (VirtualRoom.size() != 0)
            bInEquipment = _stricmp(VirtualRoom.c_str(), "Equipment") == 0;
        else
        {
            unsigned const Location = GetClientLocationCurrentRoom();
            bInEquipment = Location == MakeLocationID(CurrentBaseId(), "Equipment");
        }
        if (bInEquipment)
        {
            bRepairRedirect = 0;
            for (It = Buttons.begin(); It != Buttons.end(); ++It)
            {
                if (It->bRoomSwitch == 0 && It->Type == kHotspotRepair)
                {
                    OpenService(&*It);
                    break;
                }
            }
        }
    }

    // A clicked character's walk finished -- the slide completed and
    // the character clicked (slot 12 with a zeroed point, FUN_00415850). Without a character
    // the pending flag stays set.
    if (bCharacterPending != 0 && bFadingOut == 0 && bFadingIn == 0 &&
        GetClientMouseCursorDrawFlag() != 0 && bFadingOut == 0 && bFadingIn == 0 &&
        !G_bNnScreenUp)
    {
        ButtonMover::Finish();
        if (PendingCharacter != nullptr)
        {
            int const Point[2] = {0, 0};
            PendingCharacter->OnHotspotClick(0, Point);
            PendingCharacter = nullptr;
            bCharacterPending = 0;
        }
    }

    // A queued close runs once idle (FUN_00440260's body inlined:
    // the background at full size, shrunk to the type's scale); a close that finds the bar
    // animating again after the notify is dropped.
    if (!IsAnimating() && QueuedClose != nullptr)
    {
        NotifyServiceWindows();
        FNavBarButton* const Button = QueuedClose;
        if (IsAnimating())
        {
            QueuedClose = nullptr;
            return;
        }
        ClosingButton = Button;
        PlayOrRewindUiSoundSlot(0x1b);
        MakeServiceBackground(Button->Type, 1.0f);
        float const Scale = ServiceScale(Button->Type);
        ButtonScaler::Start(kAnimSeconds);
        ButtonScaler::AddButton(ScaledBackground, 1.0f, Scale);
        QueuedClose = nullptr;
        return;
    }
    // Else a queued open.
    if (!IsAnimating() && QueuedOpen != nullptr)
    {
        OpenService(QueuedOpen);
        QueuedOpen = nullptr;
    }
}

intptr_t NavBar::SetProperty(int Msg, uintptr_t A, uintptr_t /*B*/)
{
    // LAB_0043ee80. Retail answers every other Msg with 0 and does not forward to
    // WinControl::SetProperty.
    if (Msg != kWinNotifyClick)
        return 0;
    if (G_bNnScreenUp)
        return 0;
    // Swallowed while animating or while the room camera moves.
    if (IsAnimating())
        return 1;
    FRoomRuntime* const Room = CurrentRoomRuntime();
    if (Room != nullptr && RoomControlIsCameraMoving(Room) != 0)
        return 1;

    // The button whose control was clicked; none -> 0.
    FNavBarButton* Button = nullptr;
    std::list<FNavBarButton>::iterator It;
    for (It = Buttons.begin(); It != Buttons.end(); ++It)
    {
        if (reinterpret_cast<uintptr_t>(It->Control) == A)
        {
            Button = &*It;
            break;
        }
    }
    if (Button == nullptr)
        return 0;

    // A dealer tab clicks its dealer character when the room has one.
    if (Button->bRoomSwitch == 0)
    {
        int Role = kCharacterRoleNone;
        if (Button->Type == kHotspotStartDealer)
            Role = kCharacterRoleTrader;
        else if (Button->Type == kHotspotStartShipDealer)
            Role = kCharacterRoleShipDealer;
        else if (Button->Type == kHotspotStartEquipDealer)
            Role = kCharacterRoleEquipmentDealer;
        if (Role != kCharacterRoleNone)
        {
            FCharacterObject* const Character =
                RoomControlFindCharacterByRole(CurrentRoomRuntime(), Role);
            if (Character != nullptr)
            {
                int const Point[2] = {0, 0}; // FUN_00415850
                Character->OnHotspotClick(0, Point);
                return 1;
            }
        }
    }

    // A stateful hotspot runs its transition, anything else its action.
    if (Button->Hotspot != nullptr && Button->Hotspot->StateRead != 0)
    {
        if (Current == nullptr)
            Current = Button;
        ActivateButton(Current, Button);
        return 1;
    }
    OnButtonClicked(Button);
    return 1;
}

void NavBar::SaveIni(FILE* File)
{
    // LAB_0043efd0.
    fprintf(File, "[NavBar]\n");
    std::map<std::string, char*>::iterator It;
    for (It = IniValues.begin(); It != IniValues.end(); ++It)
        fprintf(File, "%s = %s\n", It->first.c_str(), It->second);
    fprintf(File, "\n");
}

bool NavBar::SetIniValue(char const* Name, char const* Value)
{
    // FUN_0043f060: a missing key gets a new 0x80-byte buffer (FUN_00442b10 insert),
    // then the key is looked up again (FUN_00594de0) and Value copied in unbounded.
    std::string const Key(Name);
    if (IniValues.find(Key) == IniValues.end())
        IniValues.insert(std::map<std::string, char*>::value_type(Key, new char[0x80]));
    strcpy(IniValues.find(Key)->second, Value);
    return true;
}

void NavBar::Rebuild()
{
    // FUN_0043f2e0. The list emptied and the nine RoomControl /
    // ButtonControl child names queued.
    Buttons.clear();
    char ControlName[0x100];
    int Index = 0;
    for (Index = 1; Index < 10; ++Index)
    {
        sprintf(ControlName, "RoomControl%d", Index);
        RoomControlNames.push_back(std::string(ControlName));
        sprintf(ControlName, "ButtonControl%d", Index);
        ButtonControlNames.push_back(std::string(ControlName));
    }

    // One button per hotspot of the current room (runtime +0x40).
    RoomData* const Data = GetRoomRuntimeRoomData(CurrentRoomRuntime());
    size_t HotspotIndex = 0;
    for (HotspotIndex = 0; HotspotIndex < Data->Hotspots.size(); ++HotspotIndex)
    {
        RoomButtonInfo const* const Hotspot = Data->Hotspots[HotspotIndex];

        // A hotspot of another virtual room is skipped, except the
        // Deck / Planetscape ones outside any virtual room.
        if (Hotspot->VirtualRoom != nullptr && VirtualRoom.compare(Hotspot->VirtualRoom) != 0)
        {
            if (_stricmp(Hotspot->VirtualRoom, "Deck") != 0 &&
                _stricmp(Hotspot->VirtualRoom, "Planetscape") != 0)
                continue;
            if (VirtualRoom.size() != 0)
                continue;
        }

        // A news hotspot (behaviour 2) only with news at this base.
        if (Hotspot->Behavior == kHotspotNewsVendor)
        {
            TMirrorPropertyBucket<NewsBroadcastProperties>* const News =
                GetClientLocationBaseNews(GetRoomRuntimeBaseId(CurrentRoomRuntime()));
            int NewsCount = 0;
            MetaNode<NewsBroadcastProperties>* Node = nullptr;
            while (News->List.traverse(Node, true))
                ++NewsCount;
            if (NewsCount == 0)
                continue;
        }

        // A room button for a set_virtual_room or room_switch hotspot
        // (the latter selected when it names this room), else a tab of the hotspot's behaviour.
        FNavBarButton* Button = nullptr;
        if (Hotspot->SetVirtualRoom != nullptr && Hotspot->RoomSwitch != nullptr)
        {
            unsigned const Location = MakeLocationID(CurrentBaseId(), Hotspot->RoomSwitch);
            Button = AddButton(Location, 0, 1, Hotspot->SetVirtualRoom, Hotspot->Name);
        }
        else if (Hotspot->RoomSwitch != nullptr)
        {
            int const bThisRoom = strcmp(Hotspot->RoomSwitch, Data->Nickname) == 0 ? 1 : 0;
            unsigned const Location = MakeLocationID(CurrentBaseId(), Hotspot->RoomSwitch);
            Button = AddButton(Location, bThisRoom, 1, Hotspot->RoomSwitch, Hotspot->Name);
        }
        else
        {
            Button = AddButton(static_cast<unsigned>(Hotspot->Behavior), 0, 0, Hotspot->Name,
                               Hotspot->Name);
        }
        if (Button == nullptr)
            continue;

        Button->Hotspot = Hotspot;
        if (Hotspot->RoomSwitch != nullptr && VirtualRoom.compare(Hotspot->RoomSwitch) == 0)
            Current = Button;
    }

    // The launch tab in the Cityscape / Deck / Planetscape rooms outside
    // any other virtual room, with a ship to launch.
    char const* const Nickname = Data->Nickname;
    if (strcmp(Nickname, "Cityscape") == 0 || strcmp(Nickname, "Deck") == 0 ||
        strcmp(Nickname, "Planetscape") == 0)
    {
        if (VirtualRoom.size() == 0 || strcmp(VirtualRoom.c_str(), "Deck") == 0 ||
            strcmp(VirtualRoom.c_str(), "Planetscape") == 0)
        {
            if (GetPlayerShipArchetypeIdRetail() != 0)
                AddButton(4, 0, 0, "IDS_HOTSPOT_LAUNCH", "IDS_HOTSPOT_LAUNCH");
        }
    }

    // Every unused child reset to the default mesh, no tooltip,
    // hidden and disabled; both name queues emptied (ButtonControl first).
    std::list<std::string>::iterator Name;
    for (Name = ButtonControlNames.begin(); Name != ButtonControlNames.end(); ++Name)
    {
        WinControl* const Control = FindChild(Name->c_str());
        if (Control == nullptr)
            continue;
        Control->SetProperty(kWinPropMesh, reinterpret_cast<uintptr_t>(DefaultMesh), 0);
        Control->SetProperty(kWinPropToolTip, 0, 0);
        Control->SetProperty(kWinPropHide, 1, 0);
        Control->SetProperty(kWinPropDisable, 1, 0);
    }
    ButtonControlNames.clear();
    for (Name = RoomControlNames.begin(); Name != RoomControlNames.end(); ++Name)
    {
        WinControl* const Control = FindChild(Name->c_str());
        if (Control == nullptr)
            continue;
        Control->SetProperty(kWinPropMesh, reinterpret_cast<uintptr_t>(DefaultMesh), 0);
        Control->SetProperty(kWinPropToolTip, 0, 0);
        Control->SetProperty(kWinPropHide, 1, 0);
        Control->SetProperty(kWinPropDisable, 1, 0);
    }
    RoomControlNames.clear();
}

FNavBarButton* NavBar::AddButton(unsigned Type, int /*Unused*/, unsigned char bRoomSwitch,
                                 char const* Name, char const* IdsName)
{
    // FUN_0043f8e0. These names are always room buttons.
    if (strcmp(Name, "Cityscape") == 0 || strcmp(Name, "Deck") == 0 ||
        strcmp(Name, "Planetscape") == 0 || strcmp(Name, "IDS_HOTSPOT_DECK") == 0 ||
        strcmp(Name, "IDS_HOTSPOT_PLANETSCAPE") == 0)
        bRoomSwitch = 1;

    // Room buttons take the next "ButtonControlN", tabs the next
    // "RoomControlN" (the queue's front, popped). Retail assumes the queue is not empty.
    std::string ControlName;
    if (bRoomSwitch != 0)
    {
        ControlName = ButtonControlNames.front();
        ButtonControlNames.pop_front();
    }
    else
    {
        ControlName = RoomControlNames.front();
        RoomControlNames.pop_front();
    }

    // A type of -1 becomes the location Name of this base.
    if (Type == 0xffffffffu)
        Type = MakeLocationID(CurrentBaseId(), Name);

    // The child; none -> null.
    WinControl* const Control = FindChild(ControlName.c_str());
    if (Control == nullptr)
        return nullptr;

    // Pushed to the front (FUN_0043fd00, FUN_00442ab0 at begin()).
    Buttons.push_front(FNavBarButton());
    FNavBarButton* const Button = &Buttons.front();
    Button->IdsName = IdsName;
    Button->Name = Name;
    Button->bRoomSwitch = bRoomSwitch;
    Button->Type = Type;
    Button->Control = Control;

    // Its tooltip ids (DAT_00610c80), FDUMP when unknown.
    int Ids = 0;
    if (!HudLookupNamedValue(kHotspotIds, Button->IdsName, &Ids, 0x13))
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: unknown HotspotName = '%s'\n", kNavBarCpp,
              0x530, Button->IdsName);
    }

    // Shown, the tooltip (a ship dealer's tab of type 8 says
    // "equipment dealer"), the ids kept in +0x2ec, tabs flagged 0x36.
    Button->Control->SetProperty(kWinPropHide, 0, 0);
    Button->Control->SetProperty(kWinPropToolTip, static_cast<uintptr_t>(Ids), 0);
    if (Ids == 0x196b && Type == kHotspotStartEquipDealer)
        Button->Control->SetProperty(kWinPropToolTip, 0x1968, 0);
    Button->Control->UserData = Ids;
    if (bRoomSwitch == 0)
        Button->Control->SetProperty(0x36, 1, 0);

    // Its mesh from IniValues; without an entry the default mesh, and
    // the path the control reports (property 0xb) recorded under the hotspot name.
    std::map<std::string, char*>::iterator Found = IniValues.find(Button->Name);
    if (Found != IniValues.end())
    {
        Button->Control->SetProperty(kWinPropMesh, reinterpret_cast<uintptr_t>(Found->second), 0);
    }
    else
    {
        Button->Control->SetProperty(kWinPropMesh, reinterpret_cast<uintptr_t>(DefaultMesh), 0);
        char MeshPath[0x100];
        Button->Control->SetProperty(kWinPropGetMeshPath, reinterpret_cast<uintptr_t>(MeshPath), 0x100);
        SetIniValue(Button->Name.c_str(), MeshPath);
    }
    return Button;
}

void NavBar::SelectRoomButton(FNavBarButton* Button)
{
    // FUN_0043fd30. The room already entered -> nothing (the
    // hotspot is not null-tested here).
    char const* const Switch = Button->Hotspot->RoomSwitch;
    if (Switch != nullptr && strcmp(VirtualRoom.c_str(), Switch) == 0)
        return;

    // The first call latches bit 0x10000 of the control's +0x2ec and
    // fades out (FUN_00412050(1.0, 1 / duration)); the room camera released, the cursor hidden
    // and the service windows told.
    WinControl* const Control = Button->Control;
    if ((Control->UserData & 0x10000) == 0)
    {
        Control->UserData |= 0x10000;
        Current = Button;
        SetFadeTarget(1.0f, static_cast<float>(1.0 / static_cast<double>(GetFadeDuration())));
        bFadingOut = 1;
        FadeTimer = 0.0f;
        RoomControlClearCameraMoving(CurrentRoomRuntime());
        SetClientMouseCursorDrawFlag(0);
        NotifyServiceWindows();
        return;
    }

    // The second (after the fade) clears the latch word, enters the
    // virtual room (or none) and rebuilds.
    Control->UserData &= 0xffff;
    RoomButtonInfo const* const Hotspot = Button->Hotspot;
    if (Hotspot != nullptr && Hotspot->RoomSwitch != nullptr)
        VirtualRoom.assign(Hotspot->RoomSwitch);
    else
        VirtualRoom.erase();
    Current = nullptr;
    Rebuild();
    Refresh();
}

void NavBar::CameraToDesk()
{
    // FUN_0043ffa0. Current is not null-tested.
    FNavBarButton* const Button = Current;
    FRoomRuntime* const Room = CurrentRoomRuntime();
    RoomButtonInfo const* const Hotspot = Button->Hotspot;
    bool bDefaultCamera = true;
    if (Hotspot != nullptr && Hotspot->RoomSwitch != nullptr)
    {
        char const* const Switch = Hotspot->RoomSwitch;
        if (_stricmp(Switch, "Trader") == 0)
        {
            RoomControlSnapCamera(Room, "Camera_Trader_Desk");
            PlacePlayerAtSetpoint("Player_Trader");
            bDefaultCamera = false;
        }
        else if (_stricmp(Switch, "Equipment") == 0)
        {
            RoomControlSnapCamera(Room, "Camera_Equip_Desk");
            PlacePlayerAtSetpoint("Player_Equip");
            bDefaultCamera = false;
        }
        else if (_stricmp(Switch, "ShipDealer") == 0)
        {
            RoomControlSnapCamera(Room, "Camera_Ship_Desk");
            PlacePlayerAtSetpoint("Player_Ship");
            bDefaultCamera = false;
        }
        else if (_stricmp(Switch, "Planetscape") != 0 && _stricmp(Switch, "Deck") != 0)
        {
            // Another room keeps its camera.
            bDefaultCamera = false;
        }
    }
    if (bDefaultCamera)
        RoomControlSnapCamera(Room, "Camera_0");

    // Fade back in.
    SetFadeTarget(0.0f, static_cast<float>(1.0 / static_cast<double>(GetFadeDuration())));
    bFadingIn = 1;
    FadeTimer = 0.0f;
}

void NavBar::OpenService(FNavBarButton* Button)
{
    // FUN_004400d0. While animating it is queued.
    if (IsAnimating())
    {
        QueuedOpen = Button;
        return;
    }
    NnMainButtonOnModalOpen();
    PlayOrRewindUiSoundSlot(0x1b);

    // A service tab slides from where it is to its type's target
    // (the repair tab also asks for the desk afterwards).
    Vector const* Target = nullptr;
    switch (Button->Type)
    {
    case kHotspotRepair:
        Target = &G_NavBarRepairTarget;
        break;
    case kHotspotStartShipDealer:
        Target = &G_NavBarShipTarget;
        break;
    case kHotspotMissionVendor:
        Target = &G_NavBarMissionTarget;
        break;
    case kHotspotNewsVendor:
        Target = &G_NavBarNewsTarget;
        break;
    default:
        return;
    }
    Button->bOpen = 1;
    Button->Control->SetProperty(kWinPropGetOffset, reinterpret_cast<uintptr_t>(&Button->Position), 0);
    if (Button->Type == kHotspotRepair)
        bReturnToDesk = 1;
    ButtonMover::Start(kAnimSeconds);
    ButtonMover::AddButton(Button->Control, &Button->Position, Target);
}

void NavBar::CloseService(FNavBarButton* Button)
{
    // FUN_00440260. While animating it is queued.
    if (IsAnimating())
    {
        QueuedClose = Button;
        return;
    }
    // The background at full size shrinks to the type's scale.
    ClosingButton = Button;
    PlayOrRewindUiSoundSlot(0x1b);
    MakeServiceBackground(Button->Type, 1.0f);
    float const Scale = ServiceScale(Button->Type);
    ButtonScaler::Start(kAnimSeconds);
    ButtonScaler::AddButton(ScaledBackground, 1.0f, Scale);
}

float NavBar::ServiceScale(unsigned Type)
{
    // FUN_00440410: 2 / 3 / 0xc -> 0.08 (DAT_005cb890), 7 -> 0.05 (DAT_005cb88c),
    // anything else 1.0 (DAT_005c75dc).
    switch (Type)
    {
    case kHotspotNewsVendor:
    case kHotspotMissionVendor:
    case kHotspotRepair:
        return NnFloatBits(0x3da3d70a);
    case kHotspotStartShipDealer:
        return NnFloatBits(0x3d4ccccd);
    default:
        return 1.0f;
    }
}

void NavBar::MakeServiceBackground(unsigned Type, float Scale)
{
    // FUN_00440460: the type's background mesh and position; other types none.
    switch (Type)
    {
    case kHotspotRepair:
    {
        Vector const Position = {0.0f, 0.0f, NnFloatBits(0xbf8ccccd)};
        MakeScaledBackground(Scale, "Interface\\NeuroNet\\Inventory\\repair_bakgrnd.cmp",
                             &Position);
        return;
    }
    case kHotspotStartShipDealer:
    {
        Vector const Position = {0.0f, NnFloatBits(0xbdb020c5), NnFloatBits(0xbfa4dd2f)};
        MakeScaledBackground(Scale, "Interface\\NeuroNet\\Inventory\\INV_biginforcard_center.cmp",
                             &Position);
        return;
    }
    case kHotspotNewsVendor:
    case kHotspotMissionVendor:
    {
        Vector const Position = {0.0f, NnFloatBits(0xbdcccccd), NnFloatBits(0xbfbfbe77)};
        MakeScaledBackground(Scale, "Interface\\NeuroNet\\NewsVendor\\NEWS_background.cmp",
                             &Position);
        return;
    }
    default:
        return;
    }
}

void NavBar::OnScaleFinished()
{
    // LAB_00440560 (this = the +0x34c face).
    if (ClosingButton != nullptr)
    {
        // The shrunk background deleted.
        if (ScaledBackground != nullptr)
        {
            Win3DDeleteTree(ScaledBackground);
            ScaledBackground = nullptr;
        }
        // The closing tab slides home from its target and is enabled
        // again; other types do nothing.
        switch (ClosingButton->Type)
        {
        case kHotspotRepair:
            ButtonMover::Start(kAnimSeconds);
            ButtonMover::AddButton(ClosingButton->Control, &G_NavBarRepairTarget,
                                   &ClosingButton->Position);
            ClosingButton->bDisabled = 0;
            ClosingButton->Control->SetProperty(kWinPropDisable, 0, 0);
            ClosingButton->bOpen = 0;
            return;
        case kHotspotStartShipDealer:
        case kHotspotNewsVendor:
        case kHotspotMissionVendor:
        {
            Vector const* const Target = ClosingButton->Type == kHotspotStartShipDealer
                                            ? &G_NavBarShipTarget
                                            : (ClosingButton->Type == kHotspotNewsVendor ? &G_NavBarNewsTarget
                                                                        : &G_NavBarMissionTarget);
            ButtonMover::Start(kAnimSeconds);
            ClosingButton->bOpen = 0;
            ButtonMover::AddButton(ClosingButton->Control, Target, &ClosingButton->Position);
            ClosingButton->bDisabled = 0;
            ClosingButton->Control->SetProperty(kWinPropDisable, 0, 0);
            return;
        }
        default:
            return;
        }
    }

    // The grown background deleted.
    if (ScaledBackground != nullptr)
    {
        Win3DDeleteTree(ScaledBackground);
        ScaledBackground = nullptr;
    }

    // The open tab's window comes up.
    std::list<FNavBarButton>::iterator It;
    for (It = Buttons.begin(); It != Buttons.end(); ++It)
    {
        FNavBarButton& Button = *It;
        if (Button.bRoomSwitch != 0 || Button.bOpen == 0)
            continue;
        switch (Button.Type)
        {
        case kHotspotNewsVendor:
        case kHotspotMissionVendor:
        {
            // The room's controllers for the news / mission vendor.
            NnMainButtonOnModalOpen();
            FRoomRuntime* const Room = CurrentRoomRuntime();
            if (Room != nullptr)
                RoomControlNotifyControllers(Room, -1, 3, static_cast<int>(Button.Type));
            return;
        }
        case kHotspotRepair:
        {
            // NN_Repair, placed (property 0x42) at (0, 0, -1.1).
            Win3D* const Repair = WinFactoryCreate("NN_Repair", "NN_Repair", nullptr, nullptr);
            if (Repair == nullptr)
                return;
            float const Position[3] = {0.0f, 0.0f, NnFloatBits(0xbf8ccccd)};
            static_cast<WinControl*>(Repair)->SetProperty(
                0x42, reinterpret_cast<uintptr_t>(Position), 0);
            return;
        }
        case kHotspotStartShipDealer:
        {
            // The base's solar marked visited for the ship dealer
            // (flag 0x10, the server told), NN_ShipTrader, the desk tab back.
            unsigned const Solar = GetClientLocationBaseSolarId();
            if (Solar != 0)
            {
                unsigned char Flags = 0;
                GetPlayerVisitFlagsRetail(Solar, &Flags, 0, 0);
                Flags |= 0x10;
                SetPlayerVisitFlagsRetail(Solar, Flags, true);
            }
            WinFactoryCreate("NN_ShipTrader", "NN_ShipTrader", nullptr, nullptr);
            ToggleDealerTabs(true);
            return;
        }
        default:
            return;
        }
    }
}

void NavBar::OnMoveFinished()
{
    // LAB_004408b0 (this = the +0x32c face). A closed tab is home --
    // the dealer tab swapped back for the ship dealer, the repair tab refreshed.
    if (ClosingButton != nullptr)
    {
        if (ClosingButton->Type == kHotspotStartShipDealer)
            ToggleDealerTabs(false);
        ClosingButton = nullptr;
        RefreshRepairTab(false);
        return;
    }

    if (ScaledBackground != nullptr)
    {
        Win3DDeleteTree(ScaledBackground);
        ScaledBackground = nullptr;
    }

    // An opened tab arrived -- its background grows from the type's
    // scale to full size and the tab is disabled.
    std::list<FNavBarButton>::iterator It;
    for (It = Buttons.begin(); It != Buttons.end(); ++It)
    {
        FNavBarButton& Button = *It;
        if (Button.bRoomSwitch != 0 || Button.bOpen == 0)
            continue;
        float const Scale = ServiceScale(Button.Type);
        MakeServiceBackground(Button.Type, Scale);
        ButtonScaler::Start(kAnimSeconds);
        ButtonScaler::AddButton(ScaledBackground, Scale, 1.0f);
        Button.bDisabled = 1;
        Button.Control->SetProperty(kWinPropDisable, 1, 0);
        return;
    }
}

void NavBar::OnButtonClicked(FNavBarButton* Button)
{
    // FUN_00440ac0.
    bRepairRedirect = 0;
    FRoomRuntime* const Room = CurrentRoomRuntime(); // FUN_00438c00
    RoomButtonInfo const* const Hotspot = Button->Hotspot;

    // Behaviour 0xb hotspots switch rooms.
    if (Hotspot != nullptr && Hotspot->Behavior == kHotspotVirtualRoom)
    {
        SelectRoomButton(Button);
        return;
    }

    // The current location's own button leaves a virtual room.
    unsigned const Type = Button->Type;
    if (Type != 0 && Type == GetClientLocationCurrentRoom())
    {
        if (VirtualRoom.size() == 0)
            return;
        VirtualRoom.erase();
        SelectRoomButton(Button);
        return;
    }

    // Another room button -- its virtual room (entered once the
    // location changes), the room's controllers told, the service windows told.
    if (Button->bRoomSwitch != 0 && Type != 0 && Hotspot != nullptr)
    {
        VirtualRoom.erase();
        if (Hotspot->SetVirtualRoom != nullptr)
        {
            VirtualRoom.assign(Hotspot->SetVirtualRoom);
            bVirtualRoomPending = 1;
            VirtualRoomLocation = GetClientLocationCurrentRoom();
        }
        RoomControlNotifyControllers(Room, -1, 3, static_cast<int>(Button->Type));
        NotifyServiceWindows();
        return;
    }

    // Outside 2..0xc retail takes the 0xb case.
    switch (Type)
    {
    case kHotspotNewsVendor:
    case kHotspotMissionVendor:
    {
        // News / missions: open -- the other open service closed
        // first; open already -- its window told and closed.
        if (Button->bOpen == 0)
        {
            NotifyServiceWindows();
            std::list<FNavBarButton>::iterator It;
            for (It = Buttons.begin(); It != Buttons.end(); ++It)
            {
                if (It->bRoomSwitch == 0 && It->bOpen != 0)
                {
                    if (IsServiceType(It->Type))
                        CloseService(&*It);
                    break;
                }
            }
            OpenService(Button);
            return;
        }
        NotifyServiceWindow(Type == kHotspotNewsVendor ? "NN_News" : "NN_MissionVendor");
        CloseService(Button);
        return;
    }
    case kHotspotLaunch:
    {
        // Launch -- the equipment checked, the ship's launch script
        // (or straight to the mission briefing), then the bar and its controls destroyed.
        if (!CheckLaunchEquipment())
            return;
        if (GetPlayerShipArchetypeIdRetail() == 0)
            return;
        FObjectShip* const Ship = GfObjectShip();
        if (Ship != nullptr)
            Ship->PlayLaunchScript();
        else
            RequestGameflowMissionBriefing();
        Win3D* const Found = Win3DFindByName("NavBar");
        if (Found == nullptr)
            return;
        NavBar* const Bar = static_cast<NavBar*>(Found);
        if (Bar->ScaledBackground != nullptr)
        {
            Win3DDeleteTree(Bar->ScaledBackground);
            Bar->ScaledBackground = nullptr;
        }
        NavBarDeleteRoomControls();
        Bar->Destroy();
        delete Bar; // usually this -- nothing after it touches a member
        return;
    }
    case kHotspotStartDealer:
    case kHotspotStartEquipDealer:
    {
        // Commodity dealer (visit flag 4) / equipment dealer (8):
        // the base's solar marked, the dealer screen opened.
        unsigned const Solar = GetClientLocationBaseSolarId();
        if (Solar != 0)
        {
            unsigned char Flags = 0;
            GetPlayerVisitFlagsRetail(Solar, &Flags, 0, 0);
            Flags |= Type == kHotspotStartDealer ? 4 : 8;
            SetPlayerVisitFlagsRetail(Solar, Flags, true);
        }
        // FUN_00477ab0(1 or 3, 0, 0, 0).
        NnInventoryOpenDealer(Type == kHotspotStartDealer ? kTradeModeCommodity : kTradeModeEquipment,
                              nullptr, nullptr, 0);
        return;
    }
    case kHotspotFrontDesk:
    case kHotspotMoveRight:
    case kHotspotMoveLeft:
        return;
    case kHotspotStartShipDealer:
    {
        // The ship dealer tab.
        if (Button->bOpen == 0)
        {
            OpenService(Button);
            return;
        }
        NotifyServiceWindow("NN_ShipTrader");
        CloseService(Button);
        return;
    }
    case kHotspotRepair:
    {
        // The repair tab. Open -- NN_Repair told and closed.
        if (Button->bOpen != 0)
        {
            NotifyServiceWindow("NN_Repair");
            CloseService(Button);
            return;
        }
        // Repairs happen in the equipment room.
        NnMainButtonOnModalOpen();
        unsigned const EquipmentLocation = MakeLocationID(CurrentBaseId(), "Equipment");
        std::list<FNavBarButton>::iterator It;
        if (VirtualRoom.size() == 0 && EquipmentLocation != 0)
        {
            // There already -> open; else its room button clicked
            // and the redirect armed.
            if (GetClientLocationCurrentRoom() == EquipmentLocation)
            {
                OpenService(Button);
                return;
            }
            for (It = Buttons.begin(); It != Buttons.end(); ++It)
            {
                if (It->bRoomSwitch != 0 && It->Type == EquipmentLocation)
                {
                    OnButtonClicked(&*It);
                    bRepairRedirect = 1;
                    break;
                }
            }
        }
        else
        {
            // In the "Equipment" virtual room -> open; else the room
            // button switching to it selected and the redirect armed.
            if (_stricmp(VirtualRoom.c_str(), "Equipment") == 0)
            {
                OpenService(Button);
                return;
            }
            for (It = Buttons.begin(); It != Buttons.end(); ++It)
            {
                if (It->bRoomSwitch == 0)
                    continue;
                char const* const Switch = It->Hotspot->RoomSwitch;
                if (Switch != nullptr && strcmp(Switch, "Equipment") == 0)
                {
                    SelectRoomButton(&*It);
                    bRepairRedirect = 1;
                    break;
                }
            }
        }
        if (bRepairRedirect != 0)
            return;
        // Else the room button that sets the "Equipment" virtual room,
        // after the layout delay.
        for (It = Buttons.begin(); It != Buttons.end(); ++It)
        {
            if (It->bRoomSwitch == 0)
                continue;
            char const* const SetRoom = It->Hotspot->SetVirtualRoom;
            if (SetRoom != nullptr && strcmp(SetRoom, "Equipment") == 0)
            {
                LayoutDelay = kLayoutDelay;
                OnButtonClicked(&*It);
                bRepairRedirect = 1;
                return;
            }
        }
        return;
    }
    default:
    {
        // 0xb and every type outside 2..0xc: a shown NN_Inventory is
        // closed (command 0x6b), then the room's controllers told.
        if (IsShownWindow(Win3DFindByName("NN_Inventory")))
            HudPostCommand(USER_INVENTORY_CLOSE, 0);
        RoomControlNotifyControllers(Room, -1, 3, static_cast<int>(Button->Type));
        return;
    }
    }
}

void NavBar::ActivateButton(FNavBarButton* From, FNavBarButton* To)
{
    // FUN_00441700.
    NnToolTipHideGlobal();
    Current = To;
    if (To->Hotspot == nullptr)
    {
        OnButtonClicked(Current);
        return;
    }

    // From's state_transition naming To's state ("%d,") runs the
    // script named after that prefix (the prefix is assumed to lead the entry).
    char StateKey[8];
    sprintf(StateKey, "%d,", static_cast<int>(To->Hotspot->StateRead));
    int Index = 0;
    for (Index = 0; Index < 4; ++Index)
    {
        char const* const Transition = From->Hotspot->StateTransition[Index];
        if (Transition != nullptr && strstr(Transition, StateKey) != nullptr)
        {
            StartScript(Transition + strlen(StateKey), 0, 0);
            return;
        }
    }

    // Else To's behaviour moves the camera (jump table 0x00441a1c on
    // behaviour - 5); a track that does not start falls back to the button's action.
    FRoomRuntime* const Room = CurrentRoomRuntime();
    int const Behavior = To->Hotspot->Behavior;
    switch (Behavior)
    {
    case kHotspotStartEquipDealer:
    {
        // The bar camera.
        NnMainButtonOnModalOpen();
        NotifyServiceWindows();
        RoomControlSetCameraMoveSkippable(Room, 1);
        if (RoomControlTrackCamera(Room, "Camera_Track_1", &NavBar::ScriptDone, this, nullptr))
        {
            SetClientMouseCursorDrawFlag(0);
            ToggleDealerTabs(true);
            bReturnToDesk = 1;
            return;
        }
        ToggleDealerTabs(true);
        bReturnToDesk = 1;
        OnButtonClicked(Current);
        return;
    }
    case kHotspotStartDealer:
    case kHotspotStartShipDealer:
    {
        // The wide camera (the trader's inside a virtual room, for behaviour 5).
        if (Behavior != kHotspotStartShipDealer)
            NnMainButtonOnModalOpen();
        char const* Camera = "Camera_Wide";
        if (VirtualRoom.size() != 0 && To->Hotspot->Behavior == kHotspotStartDealer)
            Camera = "Camera_Trader_Wide";
        RoomControlSetCameraMoveSkippable(Room, 1);
        if (!RoomControlTrackCamera(Room, Camera, &NavBar::ScriptDone, this, nullptr))
        {
            OnButtonClicked(Current);
            return;
        }
        SetClientMouseCursorDrawFlag(0);
        bReturnToDesk = 1;
        if (To->Hotspot->Behavior != kHotspotStartShipDealer)
            ToggleDealerTabs(true);
        return;
    }
    case kHotspotFrontDesk:
    {
        // The desk camera of the virtual room; a shown dealer with a shown
        // inventory closes it (command 0x6b); NN_ShipTrader told; the room camera released.
        char const* Camera = "Camera_0";
        if (VirtualRoom.size() != 0)
        {
            if (_stricmp(VirtualRoom.c_str(), "Trader") == 0)
                Camera = "Camera_Trader_Desk";
            else if (_stricmp(VirtualRoom.c_str(), "Equipment") == 0)
                Camera = "Camera_Equip_Desk";
            else if (_stricmp(VirtualRoom.c_str(), "ShipDealer") == 0)
                Camera = "Camera_Ship_Desk";
        }
        if (IsShownWindow(Win3DFindByName("NN_Dealer")) &&
            IsShownWindow(Win3DFindByName("NN_Inventory")))
            HudPostCommand(USER_INVENTORY_CLOSE, 0);
        NotifyServiceWindow("NN_ShipTrader");
        RoomControlSetCameraMoveSkippable(Room, 1);
        RoomControlClearCameraMoving(Room);
        if (!RoomControlTrackCamera(Room, Camera, &NavBar::ScriptDone, this, nullptr))
        {
            OnButtonClicked(Current);
            return;
        }
        SetClientMouseCursorDrawFlag(0);
        ToggleDealerTabs(false);
        bReturnToDesk = 0;
        return;
    }
    case kHotspotMoveRight:
    case kHotspotMoveLeft:
    {
        // The scripted camera track.
        RoomControlSetCameraMoveSkippable(Room, 0);
        if (RoomControlTrackScriptCamera(Room, 0, &NavBar::ScriptDone, this, nullptr, 1))
            return;
        OnButtonClicked(Current);
        return;
    }
    default:
        OnButtonClicked(Current);
        return;
    }
}

void NavBar::ToggleDealerTabs(bool bShowDesk)
{
    // FUN_00441a40. The last desk tab (type 6) and the last
    // dealer tab (5 / 7 / 8); both needed.
    FNavBarButton* DeskTab = nullptr;
    FNavBarButton* DealerTab = nullptr;
    std::list<FNavBarButton>::iterator It;
    for (It = Buttons.begin(); It != Buttons.end(); ++It)
    {
        if (It->bRoomSwitch != 0)
            continue;
        if (It->Type == kHotspotFrontDesk)
            DeskTab = &*It;
        else if (It->Type == kHotspotStartDealer || It->Type == kHotspotStartShipDealer ||
                 It->Type == kHotspotStartEquipDealer)
            DealerTab = &*It;
    }
    if (DeskTab == nullptr || DealerTab == nullptr)
        return;

    Vector Offset = {0.0f, 0.0f, 0.0f};
    if (bShowDesk)
    {
        // An open dealer tab put back; the desk tab enabled and moved
        // to the dealer tab's place; the dealer tab disabled.
        if (DealerTab->bOpen != 0)
        {
            DealerTab->bOpen = 0;
            DealerTab->Control->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&DealerTab->Position), 0);
        }
        DeskTab->bDisabled = 0;
        DeskTab->Control->SetProperty(kWinPropDisable, 0, 0);
        DealerTab->Control->SetProperty(kWinPropGetOffset, reinterpret_cast<uintptr_t>(&Offset), 0);
        DeskTab->Control->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Offset), 0);
        DealerTab->bDisabled = 1;
        DealerTab->Control->SetProperty(kWinPropDisable, 1, 0);
        return;
    }
    // The mirror image.
    DealerTab->bDisabled = 0;
    DealerTab->Control->SetProperty(kWinPropDisable, 0, 0);
    DeskTab->Control->SetProperty(kWinPropGetOffset, reinterpret_cast<uintptr_t>(&Offset), 0);
    DealerTab->Control->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&Offset), 0);
    DeskTab->bDisabled = 1;
    DeskTab->Control->SetProperty(kWinPropDisable, 1, 0);
}

void NavBar::StartScript(char const* Name, unsigned char bFade, unsigned char bLetterbox)
{
    // FUN_00441b90: one script at a time.
    if (bScriptRunning != 0)
        return;
    bScriptRunning = 1;
    FGfScriptSequence* const Sequence = G_GfScriptManager.CreateSequence(CurrentRoomRuntime(), 0);
    Csys Frame;
    Frame.set_identity();
    Sequence->AddScript(Name, 0, &Frame, nullptr);
    Sequence->BindMode = 0;
    Sequence->bLetterbox = bLetterbox != 0 ? 1 : 0;
    Sequence->bFade = bFade;
    Sequence->bApplySpatial = 1;
    Sequence->bRestoreCamera = 0;
    Sequence->Priority = 1;
    Sequence->Callback = &NavBar::ScriptDone;
    Sequence->CallbackA = this;
    Sequence->CallbackB = nullptr;
    Sequence->Play();
    SetClientMouseCursorDrawFlag(0);
}

void __cdecl NavBar::ScriptDone(void* Bar, void* /*Unused*/, unsigned char /*bArg*/)
{
    // FUN_00441c40 (cdecl; only the first argument is read).
    NavBar* const Self = static_cast<NavBar*>(Bar);
    Self->bScriptRunning = 0;
    SetClientMouseCursorDrawFlag(1);
    if (Self->Current != nullptr)
        Self->OnButtonClicked(Self->Current);
}

bool NavBar::CharacterClicked(FCharacterObject* Character, int Role)
{
    // FUN_00441c70. Not while a click is pending; a shown
    // NN_Dealer takes it.
    if (bCharacterPending != 0)
        return false;
    if (IsShownWindow(Win3DFindByName("NN_Dealer")))
        return true;

    // The dealer's room -- not when already in it; its room button.
    char const* RoomName = nullptr;
    if (Role == kCharacterRoleEquipmentDealer)
        RoomName = "Equipment";
    else if (Role == kCharacterRoleTrader)
        RoomName = "Trader";
    else if (Role == kCharacterRoleShipDealer)
        RoomName = "ShipDealer";
    else
        return false;
    if (_stricmp(VirtualRoom.c_str(), RoomName) == 0)
        return false;
    FNavBarButton* Found = nullptr;
    std::list<FNavBarButton>::iterator It;
    for (It = Buttons.begin(); It != Buttons.end(); ++It)
    {
        if (It->bRoomSwitch == 0 || It->Hotspot == nullptr)
            continue;
        char const* const Switch = It->Hotspot->RoomSwitch;
        if (Switch != nullptr && strcmp(Switch, RoomName) == 0)
        {
            Found = &*It;
            break;
        }
    }
    if (Found == nullptr)
        return false;

    // The click held until the walk ends (Update), the room entered.
    bCharacterPending = 1;
    PendingCharacter = Character;
    if (Found->Hotspot != nullptr && Found->Hotspot->StateRead != 0)
    {
        if (Current == nullptr)
            Current = Found;
        ActivateButton(Current, Found);
        return true;
    }
    OnButtonClicked(Found);
    return true;
}

void NavBar::MakeScaledBackground(float Scale, char const* Mesh, Vector const* Position)
{
    // FUN_00441ea0. The old background deleted first.
    if (ScaledBackground != nullptr)
    {
        Win3DDeleteTree(ScaledBackground);
        ScaledBackground = nullptr;
    }

    // "ScaledBackground" (arguments 0, {0,0,0}, 0,0,0,1,0,0,1,0,0,0,
    // {0,0,0}).
    Vector const HalfExtents = {0.0f, 0.0f, 0.0f};
    Vector const Zero338 = {0.0f, 0.0f, 0.0f};
    ScaledBackground = CreateStaticControl("ScaledBackground", Mesh, Position, nullptr,
                                           &HalfExtents, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, &Zero338);

    // The instance's rotation reset to identity scaled by Scale (its
    // translation kept), SetInstanceMatrix, UpdateInstance. No null or -1 test.
    const InstanceIndex Instance = ScaledBackground->MeshHolder.EngineInstance;
    Transform Xform;
    Xform.Rotation.E[0][0] = 1.0f;
    Xform.Rotation.E[0][1] = 0.0f;
    Xform.Rotation.E[0][2] = 0.0f;
    Xform.Rotation.E[1][0] = 0.0f;
    Xform.Rotation.E[1][1] = 1.0f;
    Xform.Rotation.E[1][2] = 0.0f;
    Xform.Rotation.E[2][0] = 0.0f;
    Xform.Rotation.E[2][1] = 0.0f;
    Xform.Rotation.E[2][2] = 1.0f;
    const Transform* const CurrentTransform = DALib::Engine->GetInstanceTransform(Instance);
    Xform.Translation.x = CurrentTransform->Translation.x;
    Xform.Translation.y = CurrentTransform->Translation.y;
    Xform.Translation.z = CurrentTransform->Translation.z;
    if (G_NavBarMath == nullptr)
        G_NavBarMath = Acquire3DMathEngine();
    G_NavBarMath->Scale(Xform.Rotation, Xform.Rotation, Scale);
    DALib::Engine->SetInstanceMatrix(Instance, &Xform);
    DALib::Engine->UpdateInstance(Instance, 0, nullptr);
}

void NavBarEnsure()
{
    // FUN_00442060: found or created (FUN_0059db20("NavBar", "NavBar", 0, 0)), then slot 2 and
    // slot 45 (+0xb4, tail jump).
    Win3D* Found = Win3DFindByName("NavBar");
    if (Found == nullptr)
    {
        Found = WinFactoryCreate("NavBar", "NavBar", nullptr, nullptr);
        if (Found == nullptr)
            return;
    }
    WinControl* const Control = Found->AsWinControl();
    if (Control == nullptr)
        return;
    static_cast<NavBar*>(Control)->OnLocationEntered();
}

void NavBarDeleteRoomControls()
{
    // FUN_004420a0: for 1..9 each "RoomControl%d", then "ButtonControl%d", window answering
    // slot 2 goes through its scalar deleting dtor (no slot 10 Destroy).
    char Name[0x100];
    int Index = 0;
    for (Index = 1; Index < 10; ++Index)
    {
        sprintf(Name, "RoomControl%d", Index);
        Win3D* Found = Win3DFindByName(Name);
        if (Found != nullptr)
        {
            WinControl* const Control = Found->AsWinControl();
            if (Control != nullptr)
                delete Control;
        }
        sprintf(Name, "ButtonControl%d", Index);
        Found = Win3DFindByName(Name);
        if (Found != nullptr)
        {
            WinControl* const Control = Found->AsWinControl();
            if (Control != nullptr)
                delete Control;
        }
    }
}

void NavBarDestroy()
{
    // FUN_00442130: the "NavBar" window (no slot 2 test) -- its background subtree, the room
    // controls (FUN_004420a0), slot 10 and the scalar deleting dtor.
    Win3D* const Found = Win3DFindByName("NavBar");
    if (Found == nullptr)
        return;
    NavBar* const Bar = static_cast<NavBar*>(Found);
    if (Bar->ScaledBackground != nullptr)
    {
        Win3DDeleteTree(Bar->ScaledBackground);
        Bar->ScaledBackground = nullptr;
    }
    NavBarDeleteRoomControls();
    Bar->Destroy();
    delete Bar;
}

void NavBarOpenDealer(int Index)
{
    // FUN_004421a0.
    NavBar* const Bar = FindNavBar();
    if (Bar == nullptr)
        return;
    // FUN_0059da10("NN_Dealer"), the result unused.
    Win3DFindByName("NN_Dealer");
    Bar->ButtonMover::Finish();
    Bar->ToggleDealerTabs(true);
    Bar->bReturnToDesk = 1;
    NnInventoryOpenShipDealer(Index, nullptr, nullptr); // FUN_00477bf0
}

void NavBarOpenButton(unsigned Type)
{
    // FUN_00442200: not during a repair redirect; a running slide finished first; the first
    // button (of either kind) of Type activated.
    NavBar* const Bar = FindNavBar();
    if (Bar == nullptr)
        return;
    if (Bar->bRepairRedirect != 0)
        return;
    if (Bar->ButtonMover::IsActive())
        Bar->ButtonMover::Finish();
    std::list<FNavBarButton>::iterator It;
    for (It = Bar->Buttons.begin(); It != Bar->Buttons.end(); ++It)
    {
        if (It->Type != Type)
            continue;
        FNavBarButton* const Button = &*It;
        if (Button->Hotspot != nullptr && Button->Hotspot->StateRead != 0)
        {
            if (Bar->Current == nullptr)
                Bar->Current = Button;
            Bar->ActivateButton(Bar->Current, Button);
            return;
        }
        Bar->OnButtonClicked(Button);
        return;
    }
}

void NavBarCloseService(bool bNotify)
{
    // FUN_004424f0: the first open tab closed when it is a service (2 / 3 / 7 / 0xc; the
    // FUN_00440260 body inlined).
    NavBar* const Bar = FindNavBar();
    if (Bar == nullptr)
        return;
    if (bNotify)
        NavBar::NotifyServiceWindows();
    std::list<FNavBarButton>::iterator It;
    for (It = Bar->Buttons.begin(); It != Bar->Buttons.end(); ++It)
    {
        if (It->bRoomSwitch != 0 || It->bOpen == 0)
            continue;
        if (IsServiceType(It->Type))
            Bar->CloseService(&*It);
        return;
    }
}

bool NavBarHasServiceWindow()
{
    // FUN_00442720.
    if (FindNavBar() == nullptr)
        return false;
    if (Win3DFindByName("NN_Repair") != nullptr)
        return true;
    if (Win3DFindByName("NN_ShipTrader") != nullptr)
        return true;
    if (Win3DFindByName("NN_News") != nullptr)
        return true;
    return Win3DFindByName("NN_MissionVendor") != nullptr;
}

bool NavBarIsAnimating()
{
    // FUN_00442790: the "NavBar" window's +0x32c / +0x34c active flags, no slot 2 test.
    Win3D* const Found = Win3DFindByName("NavBar");
    if (Found == nullptr)
        return false;
    return static_cast<NavBar*>(Found)->IsAnimating();
}

void NavBarRefreshRepairTab(bool bForce)
{
    // FUN_004427e0.
    NavBar* const Bar = FindNavBar();
    if (Bar == nullptr)
        return;
    Bar->RefreshRepairTab(bForce);
}

bool NavBarCharacterClicked(FCharacterObject* Character, int Role)
{
    // FUN_00442810.
    NavBar* const Bar = FindNavBar();
    if (Bar == nullptr)
        return false;
    return Bar->CharacterClicked(Character, Role);
}

void NavBarShowDealerTab()
{
    // FUN_00442850.
    NavBar* const Bar = FindNavBar();
    if (Bar == nullptr)
        return;
    Bar->ToggleDealerTabs(false);
}
