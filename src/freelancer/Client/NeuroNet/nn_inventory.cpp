// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_Inventory -- see nn_inventory.h for the class, its vftable and layout.

#include "NeuroNet/nn_inventory.h"

#include "Interface/cargo_bar_control.h"      // ToolTipHoverInit / SetCaption
#include "NeuroNet/Economy/client_base_market.h"     // GetClientLocationBaseMarket (FUN_0043a510), ClientMarketGetPrice
#include "NeuroNet/Economy/commodity_by_base.h"      // AppendCommodityTradeReport (FUN_004704d0)
#include "Interface/Pages/character_select_equip.h" // GetPlayerShipArchetypeIdRetail (FUN_004c3e10),
                                    // GetPlayerEquipDescListRetail (FUN_004c4060),
                                    // GetPlayerBaseIdRetail (DAT_00673358)
#include "NeuroNet/Economy/dealer_equip_stats.h"     // BuildDealerEquipmentStatText (FUN_00484090)
#include "Core/game_options.h"           // GetGameOption (FUN_005792b0)
#include "GameFlow/gf_object_ship.h"         // GfObjectShip (FUN_00446be0), SetEquipment / RebuildModel
#include "Core/globals.h"                // GetClientFrameTimeCapped (FUN_0042d680)
#include "Space/HUD/hud_command_dispatch.h"   // HudPostCommand (FUN_005763b0)
#include "Space/HUD/hud_weapon_groups.h"      // G_WeaponGroups (DAT_00674c0c)
#include "Common/Fonts/ids_rdl.h"                // LoadIdsIntoRdl (FUN_0057da40), AppendTextMaybeXmlRdl
                                    // (FUN_0057e2c0), RdlAppendNode
#include "Common/Fonts/ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Network/login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "Interface/menu_ui_init.h"           // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "GameFlow/menu_universe_base.h"     // GetClientLocationCurrentBase (DAT_0066873c)
#include "Common/misc_util.h"              // FormatGroupedNumber (FUN_004779a0)
#include "GameFlow/nav_bar.h"                // NavBarRefreshRepairTab (FUN_004427e0), NavBarShowDealerTab (FUN_00442850)
#include "NeuroNet/nn_dealer.h"
#include "NeuroNet/nn_main_button.h"         // NN_MainButton::FreeScaledBackground (FUN_0045dc60)
#include "NeuroNet/nn_messaging.h"           // NnVoiceEnqueue (FUN_00489e80)
#include "NeuroNet/nn_modal.h"              // NnMainButtonSetPanelOrigin (FUN_0045a780),
                                    // NnMainButtonOnModalOpen (FUN_0045a460)
#include "NeuroNet/nn_text_scratch.h"        // DAT_0066dc60 / DAT_0066fc60 / DAT_006119f8
#include "NeuroNet/nn_ui_pulse.h"            // G_HudChromeColorRgba (DAT_00679bb4), DAT_00679bac
#include "Space/player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "Space/player_spawn.h"           // GetPlayerConnectionId (DAT_00673344)
#include "Interface/tooltip_hover.h"          // FToolTipHoverInfo, NnToolTipFeedHover (FUN_004be370)
#include "Interface/rich_text_win.h"
#include "Interface/ship_view_control.h"
#include "Space/star_sphere.h"            // Win3DBindModelview (FUN_00422690)
#include "Interface/ui_scroll.h"
#include "Interface/win3d.h"
#include "Interface/win3d_immediate.h"        // Win3DBegin / Win3DVertex3f / Win3DEnd / ... (OpenGL.cpp)
#include "Interface/win_factory.h"
#include "Interface/wire_controls.h"          // WireControlsJoinColumn (FUN_005a4b50)
#include "NeuroNet/Chat/chat_init.h"        // EChatTextStyle

#include "DALib.h"
#include "render/IRenderPipeline.h"
#include "render/rendcomp.h"

#include "common_archetype.h"
#include "common_goods.h"
#include "common_id.h"              // CreateID
#include "common_iface.h"           // IObjRW / IObjAffect
#include "common_server.h"          // IServer
#include "common_text.h"            // RenderDisplayList, StyleNode
#include "common_ini.h"             // StringAlloc
#include "common_misc.h"            // SUBOBJ_ID_NONE

#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <vector>

// Retail WINFACTORY<NN_Inventory> "NN_Inventory" (static init FUN_00485670, vftable 0x005d1d20,
// Create FUN_004856e0).
WINFACTORY<NnInventory> G_NnInventoryFactory("NN_Inventory");

// DAT_00671ce4 -- the filter mode the player's own inventory last showed. SetMode (FUN_0047f240)
// writes it when the window is neither a dealer nor docked next to one; BindControls
// (0x0047b9f6) reopens the list in it.
int G_NnInventoryLastMode = 0;

// ---------------------------------------------------------------------------------------------
// FInventoryShipModel (+0x330)
// ---------------------------------------------------------------------------------------------

FInventoryShipModel::FInventoryShipModel()
    : ShipView(nullptr)
    , NameText(nullptr)
    , DamageBar(nullptr)
    , Frame(nullptr)
    , Toggle(nullptr)
    , ShipArchId(0)
    , Loadout()
    , bDpConnect(0)
{
    // FUN_004c6140. Retail leaves +0x10 and +0x14 unwritten; the port zeroes them with the rest.
    InfoText = nullptr;

    // FUN_004c6250 inlined; the list is still empty and the view null.
    Clear();
}

FInventoryShipModel::~FInventoryShipModel()
{
    // FUN_004c6200; the EquipDescList member's own destructor frees the list.
}

void FInventoryShipModel::Clear()
{
    // FUN_004c6250.
    Loadout.Items.clear();
    bDpConnect = 0;
    if (ShipView != nullptr)
    {
        ShipView->SelectGood(0, false);
        ShipView->SelectInstance(-1, false, 0);
    }
}

void FInventoryShipModel::LoadShip(EquipDescList const* Source, unsigned char bNewDpConnect)
{
    // FUN_004c62b0 (RET 8).
    if (ShipView != nullptr)
        ShipView->Reset(); // FUN_00583b90
    if (ShipArchId == 0)
        return;
    Archetype::Ship* const ShipArch = Archetype::GetShip(ShipArchId);
    if (ShipArch == nullptr)
        return;

    // FUN_004c6250's body, inlined in retail.
    Clear();

    bDpConnect = bNewDpConnect;
    if (ShipView != nullptr)
    {
        ShipView->LoadShip(&ShipArch->Nickname, true, bNewDpConnect != 0); // FUN_00585a90
        ShipView->LoadEquipment(Source);                          // FUN_005850f0
    }

    SortFrom(Source);

    for (std::list<EquipDesc>::iterator It = Loadout.Items.begin(); It != Loadout.Items.end(); ++It)
    {
        EquipDesc& Item = *It;
        if (GoodList::find_by_archetype(Item.ArchId) != nullptr)
            Item.set_owner(static_cast<int>(reinterpret_cast<intptr_t>(this)));
    }

    if (NameText != nullptr)
        NameText->SetProperty(kTextPropSetIds, static_cast<uintptr_t>(ShipArch->IdsName), 0);

    // The x87 product of two floats is exact in double, as it is in extended precision.
    if (DamageBar != nullptr)
    {
        int const Health = static_cast<int>(static_cast<double>(GetPlayerHealthScaleRetail()) *
                                            100.0); // FUN_004c4f70, DAT_005ca230
        DamageBar->SetProperty(0x40, 100, static_cast<uintptr_t>(Health));
        DamageBar->SetProperty(kWinPropToolTip, 0x408, 0);
    }
}

void FInventoryShipModel::GetCargoCapacityAndUsed(float* OutCapacity, float* OutUsed) const
{
    // FUN_004c63e0 (RET 8). The ship lookup is not null-tested.
    *OutCapacity = 0.0f;
    *OutUsed = 0.0f;
    if (ShipArchId == 0)
        return;
    *OutCapacity = Archetype::GetShip(ShipArchId)->HoldSize;
    *OutUsed = Loadout.get_cargo_space_occupied();
}

bool FInventoryShipModel::RemoveItem(EquipDesc const* Desc, int Count)
{
    // FUN_004c6430 (RET 8). Count 0 takes Desc's own count (read before the search).
    unsigned Take = static_cast<unsigned>(Count);
    if (Take == 0)
        Take = static_cast<unsigned>(Desc->Count);

    std::list<EquipDesc>::iterator Found = Loadout.Items.begin();
    for (; Found != Loadout.Items.end(); ++Found)
    {
        if (&*Found == Desc)
            break;
    }
    if (Found == Loadout.Items.end())
        return false;

    // Unsigned compare.
    EquipDesc& Item = *Found;
    unsigned const Have = static_cast<unsigned>(Item.Count);
    if (Take >= Have)
        Loadout.Items.erase(Found);
    else
        Item.set_count(static_cast<int>(Have - Take));

    if (ShipView != nullptr)
        ShipView->LoadEquipment(&Loadout);
    return true;
}

bool FInventoryShipModel::AddItem(EquipDesc* Desc, int Count, bool bPlaySound)
{
    // FUN_004c64b0 (RET 0xc). Desc's count is swapped for Count while the copy is added.
    if (Desc == nullptr)
        return false;
    int Add = Count;
    if (Add == 0)
        Add = Desc->Count;
    int const Saved = Desc->Count;
    Desc->set_count(Add);
    Desc->set_equipped(false);
    Loadout.add_equipment_item(*Desc, arch_is_combinable(Desc->ArchId));
    if (bPlaySound)
        PlayOrRewindUiSoundSlot(0x10); // FUN_005645c0
    Desc->set_count(Saved);
    return true;
}

bool FInventoryShipModel::MountItem(EquipDesc* Desc, CacheString const* Hardpoint, int Count,
                                    bool bPlaySound)
{
    // FUN_004c6530 (RET 0x10).
    if (Desc == nullptr || ShipView == nullptr)
        return false;

    // Hold check: the volume is taken for at least one unit; both sides are truncated and
    // compared unsigned.
    bool bNoRoom = false;
    int Mount = Count;
    if (Mount == 0)
        Mount = Desc->Count;
    float Volume = 0.0f;
    if (Desc->ArchId != 0)
    {
        Archetype::Equipment* const Arch = Archetype::GetEquipment(Desc->ArchId);
        if (Arch != nullptr)
            Volume = Arch->Volume;
    }
    int const Units = Mount < 1 ? 1 : Mount;
    unsigned const Need =
        static_cast<unsigned>(static_cast<int>(static_cast<double>(Units) * Volume));
    float Capacity = 0.0f;
    double Used = 0.0;
    if (ShipArchId != 0)
    {
        Capacity = Archetype::GetShip(ShipArchId)->HoldSize; // not null-tested
        Used = Loadout.get_cargo_space_occupied();
    }
    unsigned const Free = static_cast<unsigned>(static_cast<int>(Capacity - Used));
    if (Need > Free)
        bNoRoom = true;

    int const Saved = Desc->Count;
    Desc->set_count(Mount);

    if (Desc->is_internal() && Hardpoint->Value == EquipDesc::CARGO_BAY_HP_NAME.Value)
    {
        // Cargo into the cargo bay.
        if (bNoRoom)
        {
            Desc->set_count(Saved);
            if (bPlaySound)
            {
                // "insufficient_cargo_space" voice line (DAT_0067339c).
                NnVoiceEnqueue(G_NnInsufficientCargoSpaceId, 0);
            }
            return false;
        }
        Desc->set_equipped(false);
        Loadout.add_equipment_item(*Desc, arch_is_combinable(Desc->ArchId));
        if (bPlaySound)
            PlayOrRewindUiSoundSlot(0x10);
    }
    else
    {
        HardpointSummary Summary;
        Summary.analyze_instance(ShipView->Instance);

        if (Hardpoint->Value == EquipDesc::CARGO_BAY_HP_NAME.Value)
        {
            // Equipment into the cargo bay.
            if (bNoRoom)
            {
                Desc->set_count(Saved);
                if (bPlaySound)
                {
                    NnVoiceEnqueue(G_NnInsufficientCargoSpaceId, 0);
                }
                return false;
            }
            Desc->make_internal();
            Desc->set_equipped(false);
        }
        else
        {
            // Onto a hardpoint; a missing hardpoint also plays the cargo-space line.
            if (Summary.find_name(Hardpoint->Value) == -1)
            {
                Desc->set_count(Saved);
                if (bPlaySound)
                {
                    NnVoiceEnqueue(G_NnInsufficientCargoSpaceId, 0);
                }
                return false;
            }
            Desc->set_hardpoint(*Hardpoint);
            if (Hardpoint->Value != EquipDesc::CARGO_BAY_HP_NAME.Value)
                Desc->set_equipped(true);
        }

        Loadout.add_equipment_item(*Desc, arch_is_combinable(Desc->ArchId));
        if (bPlaySound)
        {
            if (Desc->is_internal())
                PlayOrRewindUiSoundSlot(0x10);
            else
                PlayMountSound(Desc->ArchId); // the same switch, inlined in retail
        }
    }

    ShipView->LoadEquipment(&Loadout);
    Desc->set_count(Saved);
    return true;
}

void FInventoryShipModel::PlayMountSound(unsigned ArchId)
{
    // FUN_004c6860; retail switches through the byte table 0x004c68e8.
    Archetype::Equipment* const Arch = Archetype::GetEquipment(ArchId);
    if (Arch == nullptr)
        return;
    switch (Arch->get_class_type())
    {
    case Archetype::AClassEquipment:       // 6
    case Archetype::AClassCommodity:       // 0xa
    case Archetype::AClassScanner:         // 0x14
    case Archetype::AClassLight:           // 0x15
    case Archetype::AClassTractor:         // 0x16
    case Archetype::AClassRepairDroid:     // 0x19
    case Archetype::AClassCloakingDevice:  // 0x1c
        PlayOrRewindUiSoundSlot(0x16);
        break;
    case Archetype::AClassPower:           // 0xb
    case Archetype::AClassShield:          // 0xd
    case Archetype::AClassShieldGenerator: // 0xe
        PlayOrRewindUiSoundSlot(0x15);
        break;
    case Archetype::AClassEngine:          // 0xc
    case Archetype::AClassThruster:        // 0xf
        PlayOrRewindUiSoundSlot(0x13);
        break;
    case Archetype::AClassGun:                   // 0x11
    case Archetype::AClassCounterMeasureDropper: // 0x13
    case Archetype::AClassCounterMeasure:        // 0x21
        PlayOrRewindUiSoundSlot(0x12);
        break;
    case Archetype::AClassMineDropper:     // 0x12
    case Archetype::AClassProjectile:      // 0x1e
    case Archetype::AClassMunition:        // 0x1f
    case Archetype::AClassMine:            // 0x20
        PlayOrRewindUiSoundSlot(0x14);
        break;
    default:
        break;
    }
}

void FInventoryShipModel::SetItemHardpoint(EquipDesc* Desc, char const* Hardpoint)
{
    // FUN_004c6910 (RET 8).
    if (Desc == nullptr || ShipView == nullptr)
        return;
    if (_stricmp(Desc->Hardpoint, Hardpoint) == 0) // DACOM stricmp
        return;
    CacheString NewHardpoint;
    NewHardpoint.Value = StringAlloc(Hardpoint, false);
    Desc->set_hardpoint(NewHardpoint);
    // Retail compares the caller's pointer, not the allocated one.
    if (Hardpoint != EquipDesc::CARGO_BAY_HP_NAME.Value)
        Desc->set_equipped(true);
    ShipView->LoadEquipment(&Loadout);
    if (Desc->is_internal())
    {
        PlayOrRewindUiSoundSlot(0x10);
        return;
    }
    PlayMountSound(Desc->ArchId); // the same switch, inlined in retail
}

bool FInventoryShipModel::ShowItem(EquipDesc const* Item, bool bFlag, int Mode)
{
    // FUN_004c6a50 (RET 0xc).
    if (ShipView == nullptr)
        return false;
    if (Item == nullptr)
    {
        ShipView->SelectGood(0, false);
        ShipView->SelectInstance(-1, false, 0);
        return true;
    }

    // Retail has two identical branches here.
    if ((Item->is_internal() && Mode == kNnFilterWeapons) || (Item->is_internal() && Mode == kNnFilterEquipment))
    {
        Archetype::Equipment const* Weapon = nullptr;
        Archetype::Equipment const* Shield = nullptr;
        int const HpClass = ShipViewEquipDescHpClass(Item, &Weapon, &Shield); // FUN_00583690
        ShipView->SelectHpClass(HpClass, Weapon, Shield, bFlag);             // FUN_00585cd0
        ShipView->SelectInstance(-1, false, 0);
        return true;
    }

    if (Item->is_internal())
    {
        ShipView->SelectGood(0, bFlag);
        ShipView->SelectInstance(-1, false, 0);
        return true;
    }

    ShipView->SelectEquipDesc(Item, bFlag);
    ShipView->SelectInstance(ShipView->GetInstanceOnHardpoint(Item->Hardpoint), true, 0);
    return true;
}

namespace
{
    // FUN_004c6b80's 0x90-byte sort record.
    struct FInventorySortRecord
    {
        EquipDesc const* Desc;             // +0x00
        GoodInfo const* Good;              // +0x04
        Archetype::Equipment* Arch;        // +0x08
        unsigned char bUsable;             // +0x0c
        unsigned char bHasName;            // +0x0d
        wchar_t Name[0x40];                // +0x0e
    };
    static_assert(sizeof(FInventorySortRecord) == 0x90, "FUN_004c6b80 record stride 0x90");
}

void FInventoryShipModel::SortFrom(EquipDescList const* Source)
{
    // FUN_004c6b80 (RET 4).
    Loadout.Items.clear();

    int const Count = static_cast<int>(Source->Items.size());
    std::vector<FInventorySortRecord> Records(static_cast<size_t>(Count < 0 ? 0 : Count));
    std::list<EquipDesc>::const_iterator SourceIt = Source->Items.begin();
    for (int Index = 0; Index < Count; ++Index, ++SourceIt)
    {
        EquipDesc const& Item = *SourceIt;
        FInventorySortRecord& Record = Records[static_cast<size_t>(Index)];
        GoodInfo const* const Good = GoodList::find_by_archetype(Item.ArchId);
        Record.Desc = &Item;
        Record.Good = Good;
        Record.Arch = Archetype::GetEquipment(Item.ArchId);
        Record.bUsable = 1;
        Record.bHasName = 0;
        int const Filter = NnItemFilterMode(Item.ArchId); // FUN_0047e460

        // Usable: weapons / equipment need a free-able hardpoint, ammo a launcher in Source.
        if (Good != nullptr && ShipView != nullptr)
        {
            if (Filter == kNnFilterWeapons || Filter == kNnFilterEquipment)
            {
                if (ShipView->FindHardpointForGood(Good->Id, true) == nullptr) // FUN_00585f20
                    Record.bUsable = 0;
            }
            else if (Filter == kNnFilterAmmo)
            {
                Record.bUsable = NnListHasLauncherFor(Source, Item.ArchId) ? 1 : 0;
            }
        }

        // Commodities sort by their name.
        if (ShipView != nullptr && Filter == kNnFilterCommodity && Record.Arch != nullptr && !Item.bMission)
        {
            Record.bHasName = 1;
            GetIdsStringWWithRawFallback(static_cast<unsigned>(Record.Arch->IdsName), Record.Name,
                                         0x40); // FUN_004347e0(DAT_0067eca8, ...)
        }
    }

    // Bubble sort, swapping the whole record.
    bool bSorted = true;
    do
    {
        bSorted = true;
        if (Count - 1 <= 0)
            break;
        for (int Index = 0; Index < Count - 1; ++Index)
        {
            FInventorySortRecord& A = Records[static_cast<size_t>(Index)];
            FInventorySortRecord& B = Records[static_cast<size_t>(Index) + 1];
            bool bSwap = false;
            if (A.bHasName != 0)
            {
                if (B.bHasName == 0)
                    bSwap = true;
                else if (_wcsicmp(A.Name, B.Name) > 0)
                    bSwap = true;
            }
            else if (B.bHasName == 0)
            {
                // Unusable ones last, then mission ones, then by class, then by archetype id
                // (unsigned).
                if (A.bUsable == 0 && B.bUsable != 0)
                    bSwap = true;
                else if (A.bUsable == B.bUsable)
                {
                    unsigned char const MissionA = A.Desc->bMission ? 1 : 0;
                    unsigned char const MissionB = B.Desc->bMission ? 1 : 0;
                    if (MissionA == 0 && MissionB != 0)
                        bSwap = true;
                    else if (MissionA == MissionB)
                    {
                        int ClassA = 0;
                        int ClassB = 0;
                        if (A.Arch != nullptr)
                            ClassA = static_cast<int>(A.Arch->get_class_type());
                        if (B.Arch != nullptr)
                            ClassB = static_cast<int>(B.Arch->get_class_type());
                        if (ClassA > ClassB)
                            bSwap = true;
                        else if (ClassA == ClassB && A.Desc->ArchId > B.Desc->ArchId)
                            bSwap = true;
                    }
                }
            }
            if (bSwap)
            {
                FInventorySortRecord const Swapped = A;
                A = B;
                B = Swapped;
                bSorted = false;
            }
        }
    } while (!bSorted);

    for (int Index = 0; Index < Count; ++Index)
        Loadout.Items.push_back(*Records[static_cast<size_t>(Index)].Desc);
}

void FInventoryShipModel::RefreshView()
{
    // FUN_004c6e70
    if (ShipView != nullptr)
        ShipView->LoadEquipment(&Loadout);
}

// ---------------------------------------------------------------------------------------------
// Inventory helpers
// ---------------------------------------------------------------------------------------------

// Static initializer LAB_004c6120.
unsigned G_NnInsufficientCargoSpaceId = CreateID("insufficient_cargo_space");

// Static initializers LAB_004da100 / LAB_004da120.
unsigned G_NanobotArchId = CreateID("ge_s_repair_01");
unsigned G_ShieldBatteryArchId = CreateID("ge_s_battery_01");

// Static initializers LAB_004778c0 / 004778e0 / 00477900 / 00477920 / 00477940 / 00477960 /
// 00477980: CreateID of each voice line name.
unsigned G_NnVoiceInsufficientCargoSpaceId = CreateID("insufficient_cargo_space");
unsigned G_NnVoiceNotEnoughMoneyId = CreateID("not_enough_money");
unsigned G_NnVoiceMountedId = CreateID("mounted");
unsigned G_NnVoiceLoadedIntoCargoHoldId = CreateID("loaded_into_cargo_hold");
unsigned G_NnVoiceNoPlaceToMountId = CreateID("no_place_to_mount");
unsigned G_NnVoiceCargoJettisonedId = CreateID("cargo_jettisoned");
unsigned G_NnVoiceShipPurchaseCompleteId = CreateID("ship_purchase_complete");

// ---------------------------------------------------------------------------------------------
// The player's loadout requests (0x004c4xxx; FUN_004c43d0 / FUN_004c4f90 are in
// character_select_equip.cpp with the list and its counter)
// ---------------------------------------------------------------------------------------------

bool IsPlayerDockedRetail()
{
    // FUN_004c4810.
    return (GetPlayerBaseIdRetail() & 0x3fffffffu) != 0;
}

void ReqPlayerEquipmentRetail(EquipDescList const* List)
{
    // FUN_004c4040 (cdecl): G_Server slot 28 (+0x70); G_Server is not null-tested.
    G_Server->ReqEquipment(*List, GetPlayerConnectionId());
}

bool ReqModifyPlayerEquipRetail(unsigned short Id, CacheString const* Hardpoint, int Count,
                                float Health, unsigned char bEquipped)
{
    // FUN_004c4540 (cdecl): G_Server slot 32 (+0x80), then the local copy.
    G_Server->ReqModifyItem(Id, Hardpoint->Value, Count, Health, bEquipped,
                            GetPlayerConnectionId());
    return ModifyPlayerEquipRetail(Id, Hardpoint, Count, Health, bEquipped);
}

void ReqPlayerShipArchRetail(unsigned ArchId)
{
    // FUN_004c3df0 (cdecl): G_Server slot 25 (+0x64); G_Server is not null-tested.
    G_Server->ReqShipArch(ArchId, GetPlayerConnectionId());
}

void ReqPlayerHullStatusRetail(float Health)
{
    // FUN_004c4f50 (cdecl): G_Server slot 26 (+0x68); the float goes through as a dword.
    G_Server->ReqHullStatus(Health, GetPlayerConnectionId());
}

void ReqPlayerCollisionGroupsRetail(std::list<CollisionGroupDesc> const* Groups)
{
    // FUN_004c3f10 (cdecl): G_Server slot 27 (+0x6c) with the list by reference.
    G_Server->ReqCollisionGroups(*Groups, GetPlayerConnectionId());
}

void ReqPlayerCashRetail(int Cash)
{
    // FUN_004c4cc0 (cdecl): G_Server slot 33 (+0x84).
    G_Server->ReqSetCash(Cash, GetPlayerConnectionId());
}

void ReqPlayerSpendCashRetail(int Amount)
{
    // FUN_004c4d50 (cdecl): G_Server slot 34 (+0x88) with the amount negated.
    G_Server->ReqChangeCash(-Amount, GetPlayerConnectionId());
}

void ReqPlayerGainCashRetail(int Amount)
{
    // FUN_004c4d30 (cdecl): G_Server slot 34 (+0x88) with the amount as given.
    G_Server->ReqChangeCash(Amount, GetPlayerConnectionId());
}

void ReqAddPlayerItemOnHardpointRetail(unsigned ArchId, CacheString const* Hardpoint, float Health)
{
    // FUN_004c41d0 (cdecl): G_Server slot 30 (+0x78) -- (ArchId, *Hardpoint, 1, Health, 1,
    // DAT_00673344).
    G_Server->ReqAddItem(ArchId, Hardpoint->Value, 1, Health, true, GetPlayerConnectionId());
}

void ReqAddPlayerMountedCargoRetail(unsigned ArchId, float Health)
{
    // FUN_004c4200 (cdecl): slot 30 -- (ArchId, CARGO_BAY_HP_NAME, 1, Health, 1, DAT_00673344).
    G_Server->ReqAddItem(ArchId, EquipDesc::CARGO_BAY_HP_NAME.Value, 1, Health, true,
                         GetPlayerConnectionId());
}

void ReqAddPlayerCargoRetail(unsigned ArchId, int Count, float Health)
{
    // FUN_004c4230 (cdecl): slot 30 -- (ArchId, CARGO_BAY_HP_NAME, Count, Health, 0,
    // DAT_00673344).
    G_Server->ReqAddItem(ArchId, EquipDesc::CARGO_BAY_HP_NAME.Value, Count, Health, false,
                         GetPlayerConnectionId());
}

bool ReqRemovePlayerEquipRetail(unsigned short Id, int Count)
{
    // FUN_004c4260 (cdecl): G_Server slot 31 (+0x7c).
    G_Server->ReqRemoveItem(Id, Count, GetPlayerConnectionId());
    // FUN_004c4320's body, inlined in retail.
    return RemovePlayerEquipRetail(0, Id, Count);
}

int NnLoadoutResaleValue(EquipDescList const* List, FInventoryShipModel const* Owner,
                         EquipDescList* Saved)
{
    // FUN_004c6f70 (cdecl).
    unsigned const BaseId = GetClientLocationCurrentBase();
    if (BaseId == 0)
        return 0;

    // The null test never fails (the market is runtime + 0x30).
    FClientMarket const* const Market = GetClientLocationBaseMarket(BaseId);
    if (Market == nullptr)
        return 0;

    int Total = 0;
    unsigned const OwnerTag = static_cast<unsigned>(reinterpret_cast<uintptr_t>(Owner));
    for (std::list<EquipDesc>::const_iterator It = List->Items.begin(); It != List->Items.end();
         ++It)
    {
        EquipDesc const& Item = *It;
        GoodInfo const* const Good = GoodList::find_by_archetype(Item.ArchId); // node +0xc
        if (Good == nullptr)
            continue;
        if (Owner != nullptr && Item.Owner != OwnerTag)
            continue;

        bool bHeld = true;
        if (Saved != nullptr && Saved->find_equipment_item(Item.Id) == nullptr)
            bHeld = false;

        if (NnItemFilterMode(Item.ArchId) != kNnFilterCommodity && !Good->bCombinable && bHeld) // +0x70
        {
            // Used goods (DAT_005d6d3c / DAT_005d1914).
            int const Unit = static_cast<int>(
                static_cast<double>(ClientMarketGetPrice(Market, Good->Id)) * 0.30000001192092896 +
                0.4999989867210388);
            Total += static_cast<int>(static_cast<double>(Item.Count) *
                                      static_cast<double>(Item.Health) * static_cast<double>(Unit));
        }
        else
        {
            Total += static_cast<int>(ClientMarketGetPrice(Market, Good->Id)) * Item.Count;
        }
    }
    return Total;
}

int NnShipResaleValue(unsigned ShipArchId, bool bApplyCondition)
{
    // FUN_004c6e90 (cdecl).
    if (ShipArchId == 0)
        return 0;
    unsigned const BaseId = GetClientLocationCurrentBase(); // DAT_0066873c
    if (BaseId == 0)
        return 0;
    FClientMarket const* const Market = GetClientLocationBaseMarket(BaseId);
    if (Market == nullptr)
        return 0;

    GoodInfo const* const Good = GoodList_get()->find_by_ship_arch(ShipArchId);
    if (Good == nullptr)
        return 0;
    int const Price = static_cast<int>(ClientMarketGetPrice(Market, Good->Id)); // +0x48
    if (!bApplyCondition)
        return Price;

    // Hull scale (FUN_004c4f70) and the mean collision-group health (FUN_004c3f00), 1.0
    // without any.
    double const HullScale = static_cast<double>(GetPlayerHealthScaleRetail());
    std::list<CollisionGroupDesc> const& Groups = GetPlayerCollisionGroupListRetail();
    double Mean = 0.0;
    int Count = 0;
    for (std::list<CollisionGroupDesc>::const_iterator It = Groups.begin(); It != Groups.end();
         ++It)
    {
        Mean += static_cast<double>(It->Health);
        ++Count;
    }
    if (Count > 0)
        Mean /= static_cast<double>(Count);
    else
        Mean = 1.0;

    // Two truncations; 0.8 is DAT_005d6d38.
    int const Conditioned = static_cast<int>(static_cast<double>(Price) * Mean * HullScale);
    return static_cast<int>(static_cast<double>(Conditioned) * 0.800000011920929);
}

int NnItemFilterMode(unsigned ArchId)
{
    // FUN_0047e460 (cdecl).
    Archetype::Equipment* const Arch = Archetype::GetEquipment(ArchId);
    if (Arch == nullptr || Arch->IdsName == 0)
        return kNnFilterNone;

    // Docked: a market price of exactly 0.0 returns 5 (an unordered compare goes on).
    unsigned const BaseId = GetClientLocationCurrentBase();
    if (BaseId != 0)
    {
        FClientMarket* const Market = GetClientLocationBaseMarket(BaseId);
        if (Market != nullptr &&
            static_cast<double>(ClientMarketGetPrice(Market, Arch2Good(ArchId))) == 0.0)
            return kNnFilterNone;
    }

    if (GoodList::find_by_id(Arch2Good(ArchId)) == nullptr)
        return kNnFilterNone;

    // Retail switches through the byte table 0x0047e540.
    int const HpType = static_cast<int>(Arch->get_hp_type());
    if (HpType == HpTypeInternal)
        return kNnFilterCargoBay;
    if (HpType == HpTypeMunition)
        return kNnFilterAmmo;
    if (HpType >= HpTypeShieldGenerator && HpType <= HpTypeThruster)
        return kNnFilterEquipment;
    if (HpType == HpTypeCargoPod)
        return kNnFilterCommodity;
    if (HpType >= HpTypeFighterShieldSpecial1 && HpType <= HpTypeFreighterShieldSpecial10)
        return kNnFilterEquipment;
    if (HpType == HpTypeGun || (HpType >= HpTypeTorpedo && HpType <= HpTypeTurret) ||
        (HpType >= HpTypeGunSpecial1 && HpType <= HpTypeTorpedoSpecial1))
        return kNnFilterWeapons;
    // Below HpTypeInternal or above 0x42: the default case, 0x0047e516.
    return kNnFilterCommodity;
}

int NnInventory::GetItemFilterMode(EquipDesc const* Desc)
{
    // FUN_0047e440 (RET 4; ECX is not read).
    if (Desc == nullptr)
        return kNnFilterNone;
    return NnItemFilterMode(Desc->ArchId); // FUN_0047e460
}

bool NnListHasLauncherFor(EquipDescList const* List, unsigned AmmoArchId)
{
    // FUN_00482d80 (cdecl).
    for (std::list<EquipDesc>::const_iterator It = List->Items.begin(); It != List->Items.end(); ++It)
    {
        Archetype::Equipment* const Arch = Archetype::GetEquipment(It->ArchId);
        if (Arch == nullptr)
            continue;
        Archetype::AClassType const Class = Arch->get_class_type();
        if (Class != Archetype::AClassGun && Class != Archetype::AClassMineDropper &&
            Class != Archetype::AClassCounterMeasureDropper)
            continue;
        Archetype::Launcher const* const Launcher = static_cast<Archetype::Launcher const*>(Arch);
        if (Launcher->ProjectileArchetype.IsEmpty())
            continue;
        Archetype::Equipment* const Ammo = Archetype::GetEquipmentByName(Launcher->ProjectileArchetype);
        if (Ammo != nullptr && Ammo->Nickname.Id == AmmoArchId)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// NnInventory
// ---------------------------------------------------------------------------------------------

NnInventory::NnInventory()
    : Mode(0)
{
    // FUN_00477cf0. Retail leaves +0x32c (Mode) to SetMode; the port starts it at 0.
    PendingMountHardpoint.clear();
    ResetFields();
}

void NnInventory::ResetFields()
{
    // FUN_00477ee0, in the order the stores are made.
    FirstRow = 0;              // +0x3c4
    LastRowCount = -1;             // +0x3c8
    SelectedIndex = -1;        // +0x3a4
    HoverIndex = -1;           // +0x3cc
    SelectedItem = nullptr;    // +0x4f4
    InventoryScroll = nullptr; // +0x3d0
    PlayerCash = nullptr;      // +0x3b4
    FilterWeapons = nullptr;   // +0x3d4
    FilterCargoBay = nullptr;  // +0x3d8
    FilterEquipment = nullptr; // +0x3dc
    FilterCommodity = nullptr; // +0x3e0
    FilterAmmo = nullptr;      // +0x3e4
    JettisonButton = nullptr;     // +0x4b0
    InfoCardBackground = nullptr; // +0x4b4
    InfoCardName = nullptr;       // +0x4b8
    InfoCardInfo = nullptr;       // +0x4bc
    InfoCardClose = nullptr;      // +0x4c0
    InfoCardView = nullptr;       // +0x4c4
    InfoCardScroll = nullptr;     // +0x4c8
    EmptyCargoBar = nullptr;           // +0x4cc
    SelectedCargoBar = nullptr;           // +0x4d4
    FullCargoBar = nullptr;           // +0x4d0
    CloseButton = nullptr;        // +0x4d8

    for (int Row = 0; Row < kNnInventoryRows; ++Row)
    {
        ItemName[Row] = nullptr;         // +0x3e8
        ItemView[Row] = nullptr;         // +0x3fc
        ItemPrice[Row] = nullptr;        // +0x410
        ItemQuantity[Row] = nullptr;     // +0x424
        ItemButton[Row] = nullptr;       // +0x438
        ItemMesh[Row] = nullptr;         // +0x44c
        ItemGoodDeal[Row] = nullptr;     // +0x460
        CargoMissionText[Row] = nullptr; // +0x49c
        ItemDamage[Row] = nullptr;       // +0x474
        EquipToggle[Row] = nullptr;      // +0x488
    }

    LastEquipRevision = -1;
    LastCollisionRevision = -1;
    Field4e1 = 0;
    Field4e2 = 0;
    Field4e3 = 0;
    Field4e4 = 0;
    bNoTransaction = 0;
    bShowShipInfoCard = 0;
    bSelectedSellable = 0;
    bNeedBind = 0;
    bDealerMode = 0;
    Field4f0 = 0;
    Dealer = nullptr;
    Field904 = 0;
    ResultFn = nullptr;
    TransferItemButton = nullptr;
    TransferItemView = nullptr;
    TransferItemMesh = nullptr;
    TransferItemName = nullptr;
    TransferItemDamage = nullptr;
    TransferItemPrice = nullptr;
    TransferItemGoodDeal = nullptr;
    TransferItemUnitPrice = 0;
    TransferItemMaxCount = 0;
    HoveredCargoVolume = 0;
    TransferItemArchId = 0;
    CargoLabel = nullptr;
    JettisonCooldown = 0.0f;
    Field918 = 0;
    InfoCardFacts = nullptr;
    InfoCardStats = nullptr;
    InfoCardValues = nullptr;
    for (int Index = 0; Index < 8; ++Index)
        QuantityControl[Index] = nullptr; // +0x91c..+0x938
    JettisonQuantity = 1;
    JettisonMaxQuantity = 1;
    InfoCardTitle = nullptr;
    FocusEquipId = SUBOBJ_ID_NONE;
    bCheckHardpointAfterBuy = 0;
    bTradePending = 0;
    bCheckStockAfterBuy = 0;
    bDeselectBoughtGoodIfEmpty = 0;
    ShownInfoArchId = 0;
    ShownInfoIds = 0;
    TradeRouteButton = nullptr;
    TradeRouteText = nullptr;
}

NnInventory::~NnInventory()
{
    // FUN_00477da0. An attached dealer window is torn down and the NavBar's dealer tab put back.
    if (Dealer != nullptr)
    {
        Win3DReparent(Dealer, nullptr);
        NnDealer* const Attached = Dealer;
        if (Attached != nullptr)
        {
            Win3D* Child = Attached->ChildHead;
            while (Child != nullptr)
            {
                Win3D* const Next = Child->NextSibling;
                Win3DDeleteTree(Child);
                Child = Next;
            }
            Attached->Destroy();
            delete Attached;
        }
        Dealer = nullptr;
        NavBarShowDealerTab(); // FUN_00442850
    }

    Vector const PanelOrigin = {0.0f, NnFloatBits(0x3d9ba5e3), NnFloatBits(0xbfa4dd2f)};
    NnMainButtonSetPanelOrigin(0x66, &PanelOrigin);
}

void NnInventory::SetPosition(Vector const* NewPosition)
{
    // FUN_0047fbb0.
    WinControl::SetPosition(NewPosition);
    if (Dealer != nullptr)
        Dealer->SetPosition(NewPosition);
}

bool NnInventory::Create(Win3D* NewParent, void* Data)
{
    // FUN_0047b3e0. Data is not read.
    (void)Data;
    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>(Flags & 0xf8);
    bNeedBind = 1;
    return true;
}

void NnInventory::Update()
{
    // FUN_0047b310. The next sibling is read before the call.
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        if ((Child->Flags & kWin3DActive) != 0)
            Child->Update();
        else
            Child->UpdateInactive();
        Child = Next;
    }

    if (bNeedBind != 0)
    {
        BindControls();
        // DAT_0066d3f0; no null test on the singleton.
        G_NnMainButton->FreeScaledBackground();
    }

    RefreshItemList(&Ship.Loadout, 0, 1);

    if (GetPlayerShipArchetypeIdRetail() == 0 && InventoryScroll != nullptr)
        InventoryScroll->SetProperty(kScrollPropSetRange, 0, 0);

    if (JettisonCooldown > 0.0f)
    {
        double const Remaining = static_cast<double>(JettisonCooldown) - GetClientFrameTimeCapped();
        JettisonCooldown = static_cast<float>(Remaining);
        if (Remaining < 0.0)
            JettisonCooldown = 0.0f;
    }
}

void NnInventory::Close()
{
    // FUN_0047ad20.
    if (Dealer != nullptr && (Dealer->Flags & kWin3DVisible) != 0)
    {
        Dealer->Flags = static_cast<unsigned char>(Dealer->Flags & 0xfc);
        if (Dealer->TradeMode != kTradeModeNone)
            Dealer->TradeMode = kTradeModeNone;
    }

    Ship.LoadShip(&GetPlayerEquipDescListRetail(), 1);

    Field4e3 = 0;
    if (ResultFn != nullptr)
    {
        ResultFn(bNoTransaction, ResultArg);
        ResultFn = nullptr;
    }

    Win3DReparent(this, nullptr);
    Win3DDeleteTree(this); // FUN_0043d2a0, inlined in retail
}

bool NnInventory::ProcessMessage(unsigned Msg, unsigned A, unsigned B)
{
    // FUN_0047ade0 (RET 0xc).
    if (Msg == kWinMsgHide)
    {
        // Retail inlines FUN_0047ad20's body.
        Close();
        return true;
    }

    if (Msg == kWinMsgShow)
    {
        WinControl::ProcessMessage(kWinMsgShow, A, B);
        if (Dealer != nullptr)
            Dealer->Flags = static_cast<unsigned char>(Dealer->Flags | 3);
        Update();
        if (Dealer != nullptr)
        {
            Dealer->Update();
            if (Dealer->TradeMode == kTradeModeShip)
            {
                Dealer->RestorePlayerShipView();
                Update();
                Dealer->Update();
            }
        }
        return true;
    }

    return WinControl::ProcessMessage(Msg, A, B);
}

void NnInventory::ShowQuantityControls(bool bShow)
{
    // FUN_004795f0 (RET 4). No test on the eight pointers themselves -- only +0x91c is checked.
    if (QuantityControl[0] == nullptr)
        return;
    if (bShow)
    {
        for (int Index = 0; Index < 8; ++Index)
            QuantityControl[Index]->Flags = static_cast<unsigned char>(QuantityControl[Index]->Flags | 3);
        if (InfoCardInfo == nullptr || InfoCardTitle == nullptr)
            return;
        InfoCardInfo->Flags = static_cast<unsigned char>(InfoCardInfo->Flags & 0xfc);
        InfoCardTitle->Flags = static_cast<unsigned char>(InfoCardTitle->Flags & 0xfc);
        return;
    }
    for (int Index = 0; Index < 8; ++Index)
        QuantityControl[Index]->Flags = static_cast<unsigned char>(QuantityControl[Index]->Flags & 0xfc);
    if (InfoCardInfo == nullptr || InfoCardTitle == nullptr)
        return;
    InfoCardInfo->Flags = static_cast<unsigned char>(InfoCardInfo->Flags | 3);
    InfoCardTitle->Flags = static_cast<unsigned char>(InfoCardTitle->Flags | 3);
}

bool NnInventory::IsScanner()
{
    // FUN_0044fd00.
    return false;
}

void NnInventory::ClearSelection()
{
    // Port-only helper: the sequence retail inlines after most list changes (e.g. 0x0047f2a3,
    // 0x0047d432, 0x0047a33f).
    SelectedIndex = -1;
    SelectedItem = nullptr;
    Ship.ShowItem(nullptr, 0, 5);
    ShowQuantityControls(false);
    if (Dealer != nullptr)
    {
        Dealer->ShownInfoArchId = 0;
        Dealer->ShownInfoIds = 0;
    }
}

void NnInventory::UpdateFilterButtons()
{
    // FUN_0047e590.
    if (FilterCargoBay != nullptr)
        FilterCargoBay->SetProperty(0xc, Mode == kNnFilterCargoBay ? 1 : 0, 0);
    if (FilterWeapons != nullptr)
        FilterWeapons->SetProperty(0xc, Mode == kNnFilterWeapons ? 1 : 0, 0);
    if (FilterEquipment != nullptr)
        FilterEquipment->SetProperty(0xc, Mode == kNnFilterEquipment ? 1 : 0, 0);
    if (FilterCommodity != nullptr)
        FilterCommodity->SetProperty(0xc, Mode == kNnFilterCommodity ? 1 : 0, 0);
    if (FilterAmmo != nullptr)
        FilterAmmo->SetProperty(0xc, Mode == kNnFilterAmmo ? 1 : 0, 0);
}

void NnInventory::SetMode(int NewMode, bool bPropagate)
{
    // FUN_0047f240 (RET 8).
    Mode = NewMode;
    if (Dealer == nullptr && bDealerMode == 0)
        G_NnInventoryLastMode = NewMode;

    FirstRow = 0;
    SelectedIndex = -1;
    UpdateFilterButtons();
    ShowQuantityControls(false);
    if (InventoryScroll != nullptr)
        InventoryScroll->SetProperty(kScrollPropSetValue, static_cast<uintptr_t>(FirstRow), 0);

    ClearSelection();

    if (InventoryScroll != nullptr)
        InventoryScroll->SetProperty(kScrollPropSetValue, 0, 0);

    if (Dealer != nullptr && bPropagate && Dealer->TradeMode != kTradeModeCommodity)
    {
        Dealer->SetMode(Mode, true);
        Dealer->ClearSelection();
    }

    // Retail switches on the argument, not +0x32c.
    if (Ship.ShipView == nullptr)
        return;
    switch (NewMode)
    {
    case 0:
        Ship.ShipView->SetProperty(0x4c, 0, 0);
        break;
    case 2:
        Ship.ShipView->SetProperty(0x4c, 1, 0);
        break;
    case 1:
    case 3:
    case 4:
    case 5:
        Ship.ShipView->SetProperty(0x4c, 2, 0);
        break;
    default:
        break;
    }
}

namespace
{
    // FUN_0047af30's 8-byte record: the item's EquipDesc id and its sort price.
    struct FTransferRecord
    {
        unsigned short Id; // +0
        float Price;       // +4
    };
}

void NnInventory::TransferEquipmentFromDealer()
{
    // FUN_0047af30.
    if (Dealer == nullptr)
        return;
    Archetype::Ship* const ShipArch = Archetype::GetShip(Ship.ShipArchId);
    if (ShipArch == nullptr)
        return;

    Dealer->Ship.LoadShip(&GetPlayerEquipDescListRetail(), 0);

    EquipDescList& Source = Dealer->Ship.Loadout;
    int const Count = static_cast<int>(Source.Items.size());
    std::vector<FTransferRecord> Records(static_cast<size_t>(Count < 0 ? 0 : Count));
    FClientMarket* Market = nullptr;
    unsigned const BaseId = GetClientLocationCurrentBase();
    if (BaseId != 0)
        Market = GetClientLocationBaseMarket(BaseId);

    // Engines, scanners, tractors and power plants are skipped (GetEquipment is not
    // null-tested). A price of exactly 0.0 becomes 1e8; any other, unordered included, is kept.
    std::list<EquipDesc>::const_iterator SourceIt = Source.Items.begin();
    for (int Index = 0; Index < Count; ++Index, ++SourceIt)
    {
        FTransferRecord& Record = Records[static_cast<size_t>(Index)];
        Record.Id = SourceIt->Id;
        if (Market == nullptr)
        {
            Record.Price = 100000000.0f; // DAT_005d15b8
            continue;
        }
        unsigned const GoodId = Arch2Good(SourceIt->ArchId);
        int const Class =
            static_cast<int>(Archetype::GetEquipment(SourceIt->ArchId)->get_class_type()); // vt+0x10
        if (GoodId == 0 || Class == Archetype::AClassEngine || Class == Archetype::AClassScanner ||
            Class == Archetype::AClassTractor || Class == Archetype::AClassPower)
        {
            Record.Id = 0;
            Record.Price = 0.0f;
            continue;
        }
        float const Price = ClientMarketGetPrice(Market, GoodId);
        Record.Price = static_cast<double>(Price) == 0.0 ? 100000000.0f : Price;
    }

    // Bubble sort, most expensive first; >= and unordered do not swap.
    int const Last = Count - 1;
    bool bSorted = true;
    do
    {
        bSorted = true;
        if (Last <= 0)
            break;
        for (int Index = 0; Index < Last; ++Index)
        {
            FTransferRecord& A = Records[static_cast<size_t>(Index)];
            FTransferRecord& B = Records[static_cast<size_t>(Index) + 1];
            if (A.Price < B.Price)
            {
                FTransferRecord const Swapped = A;
                A = B;
                B = Swapped;
                bSorted = false;
            }
        }
    } while (!bSorted);

    for (int Index = 0; Index < Count; ++Index)
    {
        unsigned short const Id = Records[static_cast<size_t>(Index)].Id;
        if (Id == 0)
            continue;
        EquipDesc* const Desc = Source.find_equipment_item(Id);
        if (Desc == nullptr)
            continue;

        CacheString Hardpoint;
        Hardpoint.Value = StringAlloc(Ship.ShipView->FindHardpointForEquipDesc(Desc, false), false);
        if (Hardpoint.Value[0] == '\0')
            Hardpoint.Value = EquipDesc::CARGO_BAY_HP_NAME.Value;

        // Nanobots and shield batteries are capped at the hull's limit (a negative limit caps
        // nothing).
        int Mount = Desc->Count;
        int Limit = 0;
        bool bLimited = false;
        if (Desc->ArchId == G_NanobotArchId)
        {
            Limit = ShipArch->NanobotLimit; // +0x140
            bLimited = true;
        }
        else if (Desc->ArchId == G_ShieldBatteryArchId)
        {
            Limit = ShipArch->ShieldBatteryLimit; // +0x144
            bLimited = true;
        }
        if (bLimited && Mount > Limit && Limit >= 0)
            Mount = Limit;

        if (Ship.MountItem(Desc, &Hardpoint, Mount, false))
        {
            Dealer->Ship.RemoveItem(Desc, Mount);
            continue;
        }

        // A stack that did not fit moves as many units as the hold takes.
        if (Desc->Count <= 1)
            continue;
        float Capacity = 0.0f;
        float Used = 0.0f;
        Ship.GetCargoCapacityAndUsed(&Capacity, &Used);
        float const Free = Capacity - Used;
        if (Desc->ArchId == 0)
            continue;
        Archetype::Equipment* const Arch = Archetype::GetEquipment(Desc->ArchId);
        if (Arch == nullptr)
            continue;
        if (!(Arch->Volume > 0.0f))
            continue;
        int Fit = static_cast<int>(static_cast<double>(Free) / Arch->Volume);
        if (Fit > Mount)
            Fit = Mount;
        if (Fit <= 0)
            continue;
        if (Ship.MountItem(Desc, &Hardpoint, Fit, false))
            Dealer->Ship.RemoveItem(Desc, Fit);
    }

    NnDealer* const Partner = Dealer;
    Partner->SelectedItem = nullptr;
    ClearSelection();
    Partner->ClearSelection();
}

void NnInventory::TakeDealerSelection()
{
    // FUN_0047e920. Caller: NN_Dealer's SetProperty 0x00482755.
    if (Dealer == nullptr)
        return;
    EquipDesc* const Item = Dealer->GetSelectedItem(); // FUN_0047e790 on the dealer
    if (Item == nullptr)
        return;
    int const Count = Dealer->Quantity;
    if (Count == 0)
        return;

    // The local CacheString is not released afterwards (retail has no clear on it either).
    Item->make_internal();
    Item->set_equipped(false);
    char const* Hardpoint = Ship.ShipView->FindHardpointForEquipDesc(Item, false);
    if (Hardpoint == nullptr)
        Hardpoint = EquipDesc::CARGO_BAY_HP_NAME.Value;
    CacheString HardpointName;
    HardpointName.Value = StringAlloc(Hardpoint, false);

    if (Ship.MountItem(Item, &HardpointName, Count, true))
        Dealer->Ship.RemoveItem(Item, Count);

    SelectedItem = nullptr;         // +0x4f4
    Dealer->SelectedItem = nullptr; // dealer +0x4f4
}

void NnInventory::BuyFromDealer(int Count)
{
    // FUN_0047eab0 (RET 4). Caller: NN_Dealer's SetProperty 0x00482833.
    if (Count == 0)
        return;
    if (Dealer == nullptr)
        return;
    unsigned const GoodId = Dealer->GetSelectedMarketGood(); // FUN_00483580
    GoodInfo const* const Good = GoodList::find_by_id(GoodId);
    if (Good == nullptr)
        return;

    // The market is not null-tested.
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    FClientMarketGood* const Entry = ClientMarketFindEntry(Market, GoodId);
    if (Entry != nullptr && Entry->Stock > 0)
    {
        // Unsigned min.
        if (static_cast<unsigned>(Entry->Stock) < static_cast<unsigned>(Count))
            Count = Entry->Stock;
        Entry->Stock -= Count;
        ClientMarketRequestBuy(Market, GetClientLocationCurrentBase(), GoodId, Count);

        if (Entry->Stock == 0 && InfoCardFacts != nullptr) // +0x398
            ClearInfoCard(); // FUN_0047e9e0

        bool bInHold = true;
        if (Dealer->TradeMode == kTradeModeEquipment)
        {
            if (PendingMountHardpoint.Value[0] != '\0')
            {
                // Onto the pending hardpoint, one, at full health. A gun whose projectile does not
                // require ammo joins weapon group 0 (FUN_004f6200).
                ReqAddPlayerItemOnHardpointRetail(Good->ArchId, &PendingMountHardpoint, 1.0f); // FUN_004c41d0
                char const* const Hardpoint = PendingMountHardpoint.Value;
                bInHold = false;
                Archetype::Equipment const* const Equip = Archetype::GetEquipment(Good->ArchId);
                if (Equip != nullptr && Equip->get_class_type() == Archetype::AClassGun)
                {
                    ID_String const& ProjectileName =
                        static_cast<Archetype::Launcher const*>(Equip)->ProjectileArchetype;
                    if (!ProjectileName.IsEmpty())
                    {
                        Archetype::Equipment const* const Projectile =
                            Archetype::GetEquipmentByName(ProjectileName);
                        if (Projectile != nullptr &&
                            !static_cast<Archetype::Projectile const*>(Projectile)->bRequiresAmmo)
                            G_WeaponGroups.AddHardpoint(0, Hardpoint); // FUN_004f6200
                    }
                }
                NnVoiceEnqueue(G_NnVoiceMountedId, 0); // FUN_00489e80(DAT_00671c70, 0)
                FInventoryShipModel::PlayMountSound(Good->ArchId);
            }
            else if (NnItemFilterMode(Good->ArchId) == kNnFilterCargoBay && Count == 1 &&
                     Good->ArchId != G_NanobotArchId && Good->ArchId != G_ShieldBatteryArchId &&
                     !HasEquippedOfClass(Good->ArchId))
            {
                // A single cargo-bay item with nothing of its class equipped goes in mounted.
                ReqAddPlayerMountedCargoRetail(Good->ArchId, 1.0f); // FUN_004c4200
                NnVoiceEnqueue(G_NnVoiceMountedId, 0);
            }
            else
            {
                ReqAddPlayerCargoRetail(Good->ArchId, Count, 1.0f); // FUN_004c4230
                NnVoiceEnqueue(G_NnVoiceLoadedIntoCargoHoldId, 0); // DAT_00671c6c
            }
        }
        else
        {
            // The other dealers: into the hold, no voice.
            ReqAddPlayerCargoRetail(Good->ArchId, Count, 1.0f);
        }

        ReqPlayerSpendCashRetail(GetMarketPackagePrice(Entry->Info.GoodId) * Count);
        bNoTransaction = 0;
        bTradePending = 1;

        // A good with an ammo package (GoodInfo +0xb8 / +0xbc).
        if (Good->FreeAmmoArchId != 0 && Good->FreeAmmoCount > 0)
        {
            // The gun's volume counts only when it went into the hold.
            float GunVolume = 0.0f;
            Archetype::Equipment const* const Gun = Archetype::GetEquipment(Good->ArchId);
            if (Gun != nullptr)
                GunVolume = Gun->Volume;
            if (!bInHold)
                GunVolume = 0.0f;

            // Without an ammo archetype the package count stands and the market is not asked.
            float AmmoVolume = 0.0f;
            int AmmoCount = Good->FreeAmmoCount;
            Archetype::Equipment const* const Ammo = Archetype::GetEquipment(Good->FreeAmmoArchId);
            if (Ammo != nullptr)
            {
                AmmoVolume = Ammo->Volume;
                unsigned const AmmoGood = Arch2Good(Good->FreeAmmoArchId);
                FClientMarketGood* const AmmoEntry = ClientMarketFindEntry(Market, AmmoGood);
                if (AmmoEntry != nullptr && AmmoEntry->Stock > 0)
                {
                    if (AmmoEntry->Stock < AmmoCount)
                        AmmoCount = AmmoEntry->Stock;
                    AmmoEntry->Stock -= AmmoCount;
                    ClientMarketRequestBuy(Market, GetClientLocationCurrentBase(), AmmoGood,
                                           AmmoCount);
                }
                else
                    AmmoCount = 0;
            }

            // Count is widened as unsigned; the result is not clamped at 0. An ammo volume of
            // exactly 0.0 leaves the ammo bought.
            float const HoldSize = GetPlayerShipHoldSizeRetail();                               // FUN_004c4610
            float const Used = GetPlayerEquipDescListRetail().get_cargo_space_occupied();       // FUN_004c4640
            double const Free = static_cast<double>(HoldSize) - static_cast<double>(Used) -
                                static_cast<double>(static_cast<unsigned>(Count)) *
                                    static_cast<double>(GunVolume);
            int Take = AmmoCount;
            if (static_cast<double>(AmmoVolume) != 0.0)
            {
                Take = static_cast<int>(Free / static_cast<double>(AmmoVolume)); // ftol
                if (Take > AmmoCount)
                    Take = AmmoCount;
            }

            int Room = MAX_PLAYER_AMMO;
            EquipDesc const* const Carried =
                Ship.Loadout.find_matching_cargo(Good->FreeAmmoArchId, 0, 1.0f);
            if (Carried != nullptr)
                Room -= Carried->Count;
            if (Take > Room)
                Take = Room;
            if (Take > 0)
                ReqAddPlayerCargoRetail(Good->FreeAmmoArchId, Take, 1.0f);
        }
    }

    // Remember the good for the next list refresh (not for ships).
    if (Dealer->TradeMode == kTradeModeShip)
        return;
    PendingBuyGoodId = GoodId; // +0x3bc
    if (Mode == kNnFilterWeapons || Mode == kNnFilterEquipment)
        bCheckHardpointAfterBuy = 1;  // +0x3b9
    else
        bCheckStockAfterBuy = 1;  // +0x3c0
}

void NnInventory::SellSelected(int Count)
{
    // FUN_0047eea0 (RET 4). Caller: NN_Dealer's SetProperty 0x00482859.
    if (Count == 0)
        return;
    EquipDesc* const Item = GetSelectedItem(); // FUN_0047e790
    if (Item == nullptr)
        return;
    if (Mode == kNnFilterWeapons) // +0x32c, weapons
        G_WeaponGroups.RemoveHardpoint(Item->Hardpoint); // FUN_004f6380

    // The market is not null-tested.
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    unsigned const GoodId = Arch2Good(Item->ArchId);
    FClientMarketGood* const Entry = ClientMarketFindEntry(Market, GoodId);
    if (Entry != nullptr)
    {
        // Unsigned min.
        int const ItemCount = Item->Count;
        if (static_cast<unsigned>(ItemCount) < static_cast<unsigned>(Count))
            Count = ItemCount;
        Entry->Stock += Count;

        // A used item (not a commodity, not combinable, in the saved loadout +0x3a8) sells at
        // the used price (DAT_005d0e2c / DAT_005d1914), all on the x87 stack.
        int Cash = 0;
        if (NnItemFilterMode(Item->ArchId) != kNnFilterCommodity && !arch_is_combinable(Item->ArchId) &&
            SavedLoadout.find_equipment_item(Item->Id) != nullptr)
        {
            double const Unit =
                static_cast<double>(ClientMarketGetPrice(Market, Entry->Info.GoodId)) *
                    0.30000001192092896 +
                0.4999989867210388;
            Cash = static_cast<int>(static_cast<double>(Item->Health) *
                                    static_cast<double>(Count) * Unit);
        }
        else
        {
            Cash = static_cast<int>(
                static_cast<double>(ClientMarketGetPrice(Market, Entry->Info.GoodId)) *
                static_cast<double>(Count));
        }
        ReqPlayerGainCashRetail(Cash); // FUN_004c4d30

        unsigned short const ItemId = Item->Id;
        bTradePending = 1;
        ClientMarketRequestSell(Market, GetClientLocationCurrentBase(), GoodId, Count);
        ReqRemovePlayerEquipRetail(ItemId, Count);
        Ship.LoadShip(&GetPlayerEquipDescListRetail(), 1);
        bNoTransaction = 0;

        if (Count == ItemCount && InfoCardFacts != nullptr) // +0x398
            ClearInfoCard(); // FUN_0047e9e0
    }

    NavBarRefreshRepairTab(false);
}

int NnInventory::GetMarketPackagePrice(unsigned GoodId)
{
    // FUN_0047f040 (RET 4). The good's price plus its ammo package (GoodInfo +0xb8 / +0xbc),
    // capped by market stock, free hold space and MAX_PLAYER_AMMO.
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    if (Market == nullptr)
        return 0;

    int Price = static_cast<int>(ClientMarketGetPrice(Market, GoodId)); // ftol
    GoodInfo const* const Good = GoodList::find_by_id(GoodId);
    if (Good == nullptr || Good->FreeAmmoArchId == 0 || Good->FreeAmmoCount <= 0)
        return Price;

    unsigned const AmmoGood = Arch2Good(Good->FreeAmmoArchId);
    int const AmmoPrice = static_cast<int>(ClientMarketGetPrice(Market, AmmoGood)); // ftol
    int Count = Good->FreeAmmoCount;
    FClientMarketGood const* const Entry = ClientMarketFindEntry(Market, AmmoGood); // FUN_00437d90
    if (Entry == nullptr || Entry->Stock <= 0)
        Count = 0;
    else if (Entry->Stock < Count)
        Count = Entry->Stock;

    float GunVolume = 0.0f;
    unsigned const GunArch = Good2Arch(GoodId);
    if (GunArch != 0)
    {
        Archetype::Equipment const* const Gun = Archetype::GetEquipment(GunArch);
        if (Gun != nullptr)
            GunVolume = Gun->Volume;
    }
    float AmmoVolume = 0.0f;
    unsigned const AmmoArch = Good2Arch(AmmoGood);
    if (AmmoArch != 0)
    {
        Archetype::Equipment const* const Ammo = Archetype::GetEquipment(AmmoArch);
        if (Ammo != nullptr)
            AmmoVolume = Ammo->Volume;
    }

    // The ship whose hold and loadout count: the partner's on a dealer (+0x35c), else this one's.
    NnInventory* const Owner = bDealerMode != 0 ? static_cast<NnDealer*>(this)->Inventory : this;

    // <= and unordered skip the hold cap.
    if (static_cast<double>(GunVolume) > 0.0 && static_cast<double>(AmmoVolume) > 0.0)
    {
        float Capacity = 0.0f;
        float Used = 0.0f;
        Owner->Ship.GetCargoCapacityAndUsed(&Capacity, &Used); // FUN_004c63e0
        int Fit = static_cast<int>((static_cast<double>(Capacity) - static_cast<double>(Used) -
                                    static_cast<double>(GunVolume)) /
                                   static_cast<double>(AmmoVolume)); // ftol
        if (Fit < 0)
            Fit = 0;
        if (Count > Fit)
            Count = Fit;
    }

    int Room = MAX_PLAYER_AMMO;
    EquipDesc const* const Carried =
        Owner->Ship.Loadout.find_matching_cargo(Good->FreeAmmoArchId, 0, 1.0f);
    if (Carried != nullptr)
        Room -= Carried->Count;
    if (Count > Room)
        Count = Room;
    Price += Count * AmmoPrice;
    return Price;
}

void NnInventory::CreateControls()
{
    // FUN_00478100. Positions are float[3] in window space built with retail's arithmetic
    // (FUN_00422b80 / FUN_00423b30); where x87 keeps an intermediate at extended precision
    // across two operations, the port carries it in double.
    unsigned char bShowFilters = 1;
    unsigned char bShowCommodity = 1;
    float Base[3] = {0.0f, 0.0f, 0.0f};         // the item list's origin
    float FilterBase[3] = {0.0f, 0.0f, 0.0f};   // the filter row's origin
    unsigned char bOwnWindow = 0;      // the player's own inventory
    float PriceColumn = 0.025f;        // 0x3ccccccd

    if (Dealer != nullptr)
    {
        // The half next to a dealer.
        SetMesh("Interface\\NeuroNet\\Inventory\\INV_biginforcard.cmp", 0); // slot 38
        HalfExtents.x = 0.66f;  // +0xb4
        HalfExtents.y = 0.397f;
        HalfExtents.z = 0.0f;
        HitOffset.x = 0.64f;    // +0xc0
        HitOffset.y = -0.398f;
        HitOffset.z = 0.0f;
        if (Dealer->TradeMode == kTradeModeCommodity) // +0x978
        {
            bShowFilters = 0;
            bShowCommodity = 0;
            PriceColumn = 0.0f;
        }
        else if (Dealer->TradeMode == kTradeModeEquipment)
        {
            bShowCommodity = 0;
        }
    }
    else if (bDealerMode == 0)
    {
        // The player's own inventory window.
        SetMesh("Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp", 0); // slot 38
        Vector const ClosePosition = {0.401f, 0.237f, 0.0f};
        Vector const CloseHalfExtents = {0.02f, 0.02f, 0.0f};
        CloseButton = CreateButtonControl("Inventory_Close", // FUN_005a0c90
                                          "Interface\\NeuroNet\\Inventory\\x.3db", nullptr,
                                          "ui_select_item", nullptr, &ClosePosition, nullptr,
                                          &CloseHalfExtents, 0, 1, 0, 0, 0x409, 0, 0);
        WinControlHalfExtentsFromMesh(&HalfExtents, &MeshHolder); // FUN_005a1340
        Base[0] = -0.43f;
        Base[1] = 0.258f;
        Base[2] = 0.0f;
        FilterBase[0] = -0.43f;
        FilterBase[1] = 0.244f;
        FilterBase[2] = 0.0f;
        bOwnWindow = 1;
    }

    if (GetPlayerShipArchetypeIdRetail() == 0 && Dealer == nullptr) // FUN_004c3e10
    {
        bShowFilters = 0;
        bShowCommodity = 0;
    }

    // FUN_005a0e30; the result is not kept here.
    if (bOwnWindow != 0)
    {
        Vector const TitlePosition = {-0.42f, 0.261f, 0.0f};
        CreateTextControl("PlayerTitle", "PageHeader", &TitlePosition, nullptr, 0.839f, 0.03f,
                          1.65f, 0x213f, 1, 0, 1);
    }
    else
    {
        float const Delta[3] = {0.219f, -0.039f, 0.0f};
        Vector const TitlePosition = {Base[0] + Delta[0], Base[1] + Delta[1],
                                      Base[2] + Delta[2]}; // FUN_00423b30
        CreateTextControl("PlayerTitle", "PageHeader", &TitlePosition, nullptr, 0.394f, 0.03f,
                          1.65f, 0x213f, 1, 0, 0);
    }

    {
        float const Delta[3] = {0.219f, -0.072f, 0.0f};
        Vector const CashPosition = {Base[0] + Delta[0], Base[1] + Delta[1],
                                     Base[2] + Delta[2]}; // FUN_00423b30
        CreateTextControl("PlayerCash", "SubHeader", &CashPosition, nullptr, 0.394f, 0.03f, 1.4f,
                          0, 1, 0, 0);
    }

    float const PriceX = PriceColumn + 0.293f; // DAT_005d1554

    // The five item rows. Names count from 1, the row offset from 0.
    char Name[0x100];
    for (int Row = 0; Row < kNnInventoryRows; ++Row)
    {
        int const Number = Row + 1;

        // The product is exact in double; x87 subtracts the double 0.131 (DAT_005d1538) from it
        // before the float stores.
        sprintf(Name, "ItemButton%d", Number);
        double const RowOffset = static_cast<double>(Row) * static_cast<double>(-0.068f); // DAT_005d1540
        float const RowY = static_cast<float>(RowOffset);
        float const ButtonY = static_cast<float>(RowOffset - 0.131);
        {
            Vector const ButtonPosition = {Base[0] + 0.067f, Base[1] + ButtonY,
                                           Base[2] + 0.0f}; // FUN_00423b30
            Vector const ButtonHalfExtents = {0.035f, 0.0335f, 0.0f};
            CreateWireButtonControl(Name, "ui_item_select", &ButtonPosition, &ButtonHalfExtents,
                                    0, 0, 0, 0, 0); // FUN_005a0d00
        }

        sprintf(Name, "ItemView0%d", Number);
        {
            Vector const ViewPosition = {Base[0] + 0.248f, Base[1] + ButtonY,
                                         Base[2] + 0.0f}; // FUN_00423b30
            Vector const ViewHalfExtents = {0.138f, 0.034f, 0.0f};
            CreateWireButtonControl(Name, "ui_item_select", &ViewPosition, &ViewHalfExtents, 0, 0,
                                    0, 0, 0); // FUN_005a0d00
        }

        // A static mesh placeholder.
        sprintf(Name, "ItemMesh%d", Number);
        double const MeshOffset = static_cast<double>(RowY) - static_cast<double>(0.131f); // DAT_005d150c
        float const MeshRowY = static_cast<float>(MeshOffset);
        {
            Vector const MeshPosition = {Base[0] + 0.066999994f,                
                                         static_cast<float>(static_cast<double>(Base[1]) +
                                                              MeshOffset),
                                         Base[2]};
            Vector const MeshHalfExtents = {0.036f, 0.041f, 0.0f};
            Vector const MeshVector338 = {0.0f, 0.0f, 0.0f};
            CreateStaticControl(Name, "equipment\\models\\weapons\\li_rad_launcher.cmp",
                                &MeshPosition, nullptr, &MeshHalfExtents, 0, 0, 0, 1, 1, 1, 1, 0x50,
                                0, 0, &MeshVector338); // FUN_005a0db0
        }

        sprintf(Name, "ItemName0%d", Number);
        {
            Vector const NamePosition = {
                Base[0] + 0.256f,                
                static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.118f) +
                                   static_cast<double>(Base[1])),                
                Base[2]};
            CreateTextControl(Name, "ItemName", &NamePosition, nullptr, 0.27f, 0.04f, 1.5f, 0, 0,
                              0, 0); // FUN_005a0e30
        }

        // The row y computed here is reused for the "good deal" marker.
        sprintf(Name, "ItemQuantity0%d", Number);
        double const QuantityOffset = static_cast<double>(RowY) - static_cast<double>(0.148f); // DAT_005d14b0
        float const QuantityRowY = static_cast<float>(QuantityOffset);
        {
            Vector const QuantityPosition = {
                Base[0] + 0.171f,                
                static_cast<float>(QuantityOffset + static_cast<double>(Base[1])), Base[2]};
            CreateTextControl(Name, "ItemName", &QuantityPosition, nullptr, 0.1f, 0.04f, 1.5f, 0,
                              0, 0xbcf, 0); // FUN_005a0e30
        }

        // Mount / unmount toggle (FUN_005a0fb0).
        sprintf(Name, "EquipToggle%d", Number);
        {
            Vector const TogglePosition = {
                Base[0] + 0.047000002f,                
                static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.11f) +
                                   static_cast<double>(Base[1])),                
                Base[2] + 0.001f};                               // DAT_005d1494
            Vector const ToggleHalfExtents = {0.012f, 0.012f, 0.0f};
            CreateToggleButtonControl(Name, "Interface\\neuronet\\inventory\\unmount.3db",
                                      "Interface\\neuronet\\inventory\\mount.3db", nullptr,
                                      nullptr, &TogglePosition, nullptr, &ToggleHalfExtents, 0, 0,
                                      0, 0, 0x426, 0x425);
        }

        // A horizontal cargo bar (FUN_005a1080) with the empty mesh path DAT_005c865c and the
        // colour DAT_00679bac for both states.
        sprintf(Name, "ItemDamage%d", Number);
        {
            Vector const DamagePosition = {
                Base[0] + 0.066999994f,                
                static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.158f) +
                                   static_cast<double>(Base[1])),                
                Base[2] + 0.001f};                               // DAT_005d1494
            Vector const DamageHalfExtents = {0.03f, 0.003f, 0.0f};
            CreateCargoBarControl(Name, "", &DamagePosition, nullptr, &DamageHalfExtents, 1, 0x43c,
                                  G_NnPaletteGreen, G_NnPaletteGreen);
        }

        sprintf(Name, "ItemPrice0%d", Number);
        {
            Vector const PricePosition = {
                PriceX + Base[0],
                static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.152f) +
                                   static_cast<double>(Base[1])),                
                Base[2]};
            CreateTextControl(Name, "ItemName", &PricePosition, nullptr, 0.12f, 0.04f, 1.5f, 0xbd4,
                              2, 0xbd4, 0); // FUN_005a0e30
        }

        if (Dealer != nullptr && Dealer->TradeMode == kTradeModeCommodity)
        {
            sprintf(Name, "ItemGoodDeal0%d", Number);
            Vector const DealPosition = {Base[0] + 0.365f, QuantityRowY + Base[1],                
                                         Base[2] + 0.001f};                        // DAT_005d1494
            Vector const DealHalfExtents = {0.012f, 0.012f, 0.0f};
            Vector const DealVector338 = {0.0f, 0.0f, 0.0f};
            CreateStaticControl(Name, "Interface\\NeuroNet\\Inventory\\INV_green_square.3db",
                                &DealPosition, nullptr, &DealHalfExtents, 0, 0, 0, 1, 0, 0, 1, 0, 0,
                                0, &DealVector338); // FUN_005a0db0
        }

        sprintf(Name, "CargoMissionText%d", Number);
        {
            Vector const MissionPosition = {Base[0] + 0.183f,                
                                            MeshRowY + Base[1], Base[2]};
            CargoMissionText[Row] = CreateTextControl(Name, "ItemName", &MissionPosition, nullptr,
                                                      0.17f, 0.04f, 1.5f, 0, 2, 0, 1);
        }
    }

    // FUN_005a10e0.
    if (bOwnWindow != 0)
    {
        Vector const InfoOffset = {0.027f, 0.151f, 0.0f};
        CreateRichTextWin("InfoCard_Info", 0.36f, 0.31f, &InfoOffset, true);
    }

    // FUN_005a17e0.
    {
        Vector const ScrollPosition = {Base[0] + 0.3989f, Base[1] - 0.099f,                       
                                       Base[2]};
        CreateUIScroll("Inventory_Scroll", &ScrollPosition, 0.019f, 0.335f, 0, 5, 5, 0);
    }

    // FUN_005a0c90.
    Vector const FilterHalfExtents = {0.017f, 0.017f, 0.0f};
    if (bShowFilters != 0)
    {
        float const FilterY = FilterBase[1] - 0.463f; // DAT_005d13b8
        Vector const WeaponsPosition = {FilterBase[0] + 0.226f, FilterY, FilterBase[2]};
        CreateButtonControl("Filter_Weapons", "Interface\\NeuroNet\\Inventory\\weaponsicon.3db",
                            nullptr, "ui_filter_select", nullptr, &WeaponsPosition, nullptr,
                            &FilterHalfExtents, 0, 1, 0, 0, 0x2135, 0, 0);
        Vector const AmmoPosition = {FilterBase[0] + 0.26f, FilterY, FilterBase[2]};
        CreateButtonControl("Filter_Ammo", "Interface\\NeuroNet\\Inventory\\ammo.3db", nullptr,
                            "ui_filter_select", nullptr, &AmmoPosition, nullptr, &FilterHalfExtents,
                            0, 1, 0, 0, 0x214a, 0, 0);
        Vector const EquipmentPosition = {FilterBase[0] + 0.294f, FilterY, FilterBase[2]};
        CreateButtonControl("Filter_Equipment",
                            "Interface\\NeuroNet\\Inventory\\equipmenticon.3db", nullptr,
                            "ui_filter_select", nullptr, &EquipmentPosition, nullptr,
                            &FilterHalfExtents, 0, 1, 0, 0, 0x2142, 0, 0);
        Vector const CargoBayPosition = {FilterBase[0] + 0.33f, FilterY, FilterBase[2]};
        CreateButtonControl("Filter_CargoBay", "Interface\\NeuroNet\\Inventory\\cargobay.3db",
                            nullptr, "ui_filter_select", nullptr, &CargoBayPosition, nullptr,
                            &FilterHalfExtents, 0, 1, 0, 0, 0x2141, 0, 0);
    }

    if (bShowCommodity != 0)
    {
        Vector const CommodityPosition = {FilterBase[0] + 0.364f, FilterBase[1] - 0.463f,
                                          FilterBase[2]};
        CreateButtonControl("Filter_Commodity", "Interface\\NeuroNet\\Inventory\\commodities.3db",
                            nullptr, "ui_filter_select", nullptr, &CommodityPosition, nullptr,
                            &FilterHalfExtents, 0, 1, 0, 0, 0x2138, 0, 0);
    }

    if (bOwnWindow != 0)
    {
        Vector const RoutePosition = {Base[0] + 0.641f, Base[1] - 0.455f, Base[2]};
        Vector const RouteHalfExtents = {0.146f, 0.026f, 0.0f};
        TradeRouteButton = CreateWireButtonControl("TradeRouteButton", "ui_item_select",
                                                   &RoutePosition, &RouteHalfExtents, 0, 0, 0x3d4,
                                                   0, 0); // +0x970
        Vector const RouteTextPosition = {Base[0] + 0.54f, Base[1] - 0.433f, Base[2]};
        TradeRouteText = CreateTextControl("TradeRouteText", "ButtonText", &RouteTextPosition,
                                           nullptr, 0.201f, 0.06f, 1.6f, 0x3dc, 1, 0,
                                           1); // +0x96c
    }

    if (GetPlayerShipArchetypeIdRetail() != 0) // FUN_004c3e10
    {
        Vector const BarPosition = {FilterBase[0] + 0.0984f, FilterBase[1] - 0.453f,
                                    FilterBase[2]};
        Vector const FullHalfExtents = {0.06f, 0.014f, 0.0f};
        Vector const ZeroVector = {0.0f, 0.0f, 0.0f};
        FullCargoBar = CreateStaticControl("Full_Cargobar",
                                       "Interface\\NeuroNet\\Inventory\\cargofull.3db",
                                       &BarPosition, nullptr, &FullHalfExtents, 0, 0, 0, 1, 0, 0, 1,
                                       0, 0, 0, &ZeroVector); // +0x4d0
        FullCargoBar->ToolTipIds = 0x43b;                        // +0xcc, no null test
        EmptyCargoBar = CreateStaticControl("Empty_Cargobar",
                                       "Interface\\NeuroNet\\Inventory\\cargoempty.3db",
                                       &BarPosition, nullptr, &ZeroVector, 0, 0, 0, 1, 0, 0, 1, 0, 0,
                                       0, &ZeroVector); // +0x4cc
        EmptyCargoBar->SetProperty(kWinPropHide, 1, 0);
        SelectedCargoBar = CreateStaticControl("SelectedCargobar",
                                       "Interface\\NeuroNet\\Inventory\\cargoselected.3db",
                                       &BarPosition, nullptr, &ZeroVector, 0, 0, 0, 1, 0, 0, 1, 0, 0,
                                       0, &ZeroVector); // +0x4d4
        SelectedCargoBar->SetProperty(kWinPropHide, 1, 0);
        Vector const LabelPosition = {FilterBase[0] + 0.166f, FilterBase[1] - 0.489f,
                                      FilterBase[2]};
        CargoLabel = CreateTextControl("CargoLabel", "Normal", &LabelPosition, nullptr, 0.26f,
                                       0.06f, 1.5f, 0x43b, 0, 0, 0); // +0x4dc
    }

    // FUN_005a1020, created hidden.
    {
        Vector const ViewPosition = {Base[0] + 0.25f, Base[1] - 0.245f, Base[2]}; // DAT_005c94f8
        Vector const RotSpeed = {0.0f, 0.0f, 0.0f};
        Vector const Ypr = {0.0f, 0.0f, 0.0f};
        Vector const ViewSize = {0.13f, -0.147f, 0.0f};
        unsigned const WireColor = 0xffffffffu;
        WinControl* const View = CreateShipViewControl(
            "ShipView_Control", &RotSpeed, &ViewPosition, &Ypr, &ViewSize, nullptr, false, false,
            reinterpret_cast<unsigned const*>(G_HudChromeColorRgba), &WireColor);
        View->Flags = static_cast<unsigned char>(View->Flags & 0xfc);
    }

    if (GetPlayerShipArchetypeIdRetail() != 0) // FUN_004c3e10
    {
        if (bOwnWindow != 0)
        {
            Vector const TitlePosition = {0.027f, 0.202f, 0.0f};
            InfoCardTitle = CreateTextControl("Infocard_Title", "SubHeader", &TitlePosition, nullptr,
                                         0.364f, 0.016f, 1.4f, 0x387, 1, 0, 1); // +0x944
        }
        else if (Dealer != nullptr && Dealer->TradeMode != kTradeModeCommodity)
        {
            Vector const InfoPosition = {0.478f, -0.424f, 0.388f};
            Vector const InfoHalfExtents = {0.012f, 0.012f, 0.0f};
            CreateButtonControl("ShipView_Info", "Interface\\NeuroNet\\Inventory\\question.3db",
                                nullptr, "ui_open_infocard", nullptr, &InfoPosition, nullptr,
                                &InfoHalfExtents, 0, 1, 0, 0, 0xbc4, 0, 0);
        }
    }

    // No jettison controls while docked.
    if (GetClientLocationCurrentBase() != 0)
        return;

    // The result is not kept here.
    {
        Vector const JettisonPosition = {Base[0] + 0.42f, Base[1] - 0.076f, Base[2]};
        Vector const JettisonHalfExtents = {0.014f, 0.014f, 0.0f};
        CreateButtonControl("JettisonButton", "Interface\\NeuroNet\\Inventory\\INV_dump.3db",
                            nullptr, "ui_item_select", nullptr, &JettisonPosition, nullptr,
                            &JettisonHalfExtents, 0, 1, 0, 0, 0xbc6, 0, 0);
    }

    // The jettison quantity panel, +0x91c..+0x938.
    {
        Vector const TitlePosition = {Base[0] + 0.62f, Base[1] - 0.121f, Base[2]};
        QuantityControl[0] = CreateTextControl("Inv_Jettison_Title", "SubHeader", &TitlePosition,
                                               nullptr, 0.26f, 0.016f, 1.4f, 0x638, 1, 0,
                                               0); // +0x91c
    }
    {
        Vector const SliderPosition = {Base[0] + 0.494f, Base[1] - 0.182f, Base[2]};
        UiScroll* const Slider = CreateUIScroll("JettisonQuantitySlider", &SliderPosition, 0.253f,
                                                0.019f, 0, 5, 1, 1);
        QuantityControl[1] = Slider; // +0x920
        Slider->SetStepMode(true);   // +0x3b4 = 1, no null test
        Slider->SetPageField(1.0f);  // +0x3cc = 1.0f
    }
    {
        Vector const TextPosition = {Base[0] + 0.706f, Base[1] - 0.258f, Base[2]};
        QuantityControl[2] = CreateTextControl("JettisonQuantityText", "Normal", &TextPosition,
                                               nullptr, 0.384f, 0.06f, 1.6f, 0x47a, 0, 0,
                                               0); // +0x924
    }
    {
        Vector const EditPosition = {Base[0] + 0.864f, Base[1] - 0.258f, Base[2]};
        QuantityControl[3] = CreateEditControl("JettisonQuantityEdit", "Normal", &EditPosition,
                                               nullptr, 0.384f, 0.06f, 1.6f, 0, 0,
                                               0); // FUN_005a0e90, +0x928
        QuantityControl[3]->SetProperty(0x43, 1, 8);
        QuantityControl[3]->SetProperty(0x44, 1, 0);
    }
    Vector const JettisonButtonHalfExtents = {0.104f, 0.026f, 0.0f};
    {
        Vector const CancelPosition = {Base[0] + 0.621f, Base[1] - 0.319f, Base[2]};
        QuantityControl[4] = CreateWireButtonControl("JettisonCancelButton", "ui_item_select",
                                                     &CancelPosition, &JettisonButtonHalfExtents, 0,
                                                     0, 0x639, 0, 0); // +0x92c
        Vector const CancelTextPosition = {Base[0] + 0.52f, Base[1] - 0.297f, Base[2]};
        QuantityControl[5] = CreateTextControl("JettisonCancelText", "ButtonText",
                                               &CancelTextPosition, nullptr, 0.201f, 0.06f, 1.6f,
                                               0x2148, 1, 0, 1); // +0x930
    }
    {
        Vector const AcceptPosition = {Base[0] + 0.621f, Base[1] - 0.391f, Base[2]};
        QuantityControl[6] = CreateWireButtonControl("JettisonAcceptButton", "ui_item_select",
                                                     &AcceptPosition, &JettisonButtonHalfExtents, 0,
                                                     0, 0x63a, 0, 0); // +0x934
        Vector const AcceptTextPosition = {Base[0] + 0.52f, Base[1] - 0.369f, Base[2]};
        QuantityControl[7] = CreateTextControl("JettisonAcceptText", "ButtonText",
                                               &AcceptTextPosition, nullptr, 0.201f, 0.06f, 1.6f,
                                               0x2146, 1, 0, 1); // +0x938
    }
    JettisonQuantity = 1;
    JettisonMaxQuantity = 100;
    ShowQuantityControls(false); // FUN_004795f0(0)
}

// The quad block NN_Inventory's Draw inlines twice (0x00479a64 and 0x00479bd5): a shaded quad
// in Bar's frame (Win3D +0x3c orientation, +0x60 + +0xa8 position) from x = X to x = -HalfX.
static void NnInventoryDrawBarQuad(WinControl const* Bar, float X, float HalfX, float HalfY)
{
    float Frame[12];
    float const* const BarOrientation = &Bar->Orientation.E[0][0];
    for (int Index = 0; Index < 9; ++Index)
        Frame[Index] = BarOrientation[Index];
    Frame[9] = Bar->Offset.x + Bar->Position.x;
    Frame[10] = Bar->Offset.y + Bar->Position.y;
    Frame[11] = Bar->Offset.z + Bar->Position.z;
    Win3DBindModelview(nullptr, reinterpret_cast<Transform const*>(Frame)); // FUN_00422690(0, &Transform)
    Win3DDisable(kWin3DTexture2D);          // FUN_00421ed0(0xde1)
    Win3DEnable(kWin3DBlend);               // FUN_00421dd0(0xbe2)
    Win3DSetBlendFunc(2, 4);                // FUN_004220c0
    Win3DColor4ub(0, 0, 0, 0x0a);           // FUN_00421b70
    Win3DBegin(kWin3DQuads);                // FUN_00421670(8)
    Win3DVertex3f(X, HalfY, 0.001f);        // FUN_00421cc0
    Win3DVertex3f(X, -HalfY, 0.001f);
    Win3DVertex3f(-HalfX, -HalfY, 0.001f);
    Win3DVertex3f(-HalfX, HalfY, 0.001f);
    Win3DEnd();                             // FUN_00421690
}

void NnInventory::Draw()
{
    // FUN_00479700 (slot 13).
    if (GetGameOption(kGameOptHud) == 0) // FUN_005792b0(10)
        return;
    if (bNeedBind != 0)
        return;

    int HoverRow = -1;
    if (HoverIndex >= 0)
    {
        HoverRow = HoverIndex - FirstRow;
        if (HoverRow < 0 || HoverRow >= kNnInventoryRows)
            HoverRow = -1;
    }
    int SelectedRow = -1;
    if (SelectedIndex >= 0)
    {
        SelectedRow = SelectedIndex - FirstRow;
        if (SelectedRow < 0 || SelectedRow >= kNnInventoryRows)
            SelectedRow = -1;
    }

    // Draw layer (+0x70): 1 for the hovered row's controls, 2 for the selected row's.
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
    {
        Child->DrawLayer = 0;
        if (HoverRow >= 0)
        {
            WinControl* const AsControl = Child->AsWinControl(); // slot 2
            if (AsControl == ItemButton[HoverRow] || AsControl == ItemView[HoverRow]
                || AsControl == ItemDamage[HoverRow] || AsControl == EquipToggle[HoverRow]
                || AsControl == ItemMesh[HoverRow])
            {
                Child->DrawLayer = 1;
            }
        }
        if (SelectedRow >= 0)
        {
            WinControl* const AsControl = Child->AsWinControl(); // slot 2
            if (AsControl == ItemButton[SelectedRow] || AsControl == ItemView[SelectedRow]
                || AsControl == ItemDamage[SelectedRow] || AsControl == EquipToggle[SelectedRow]
                || AsControl == ItemMesh[SelectedRow])
            {
                Child->DrawLayer = 2;
            }
        }
    }

    WireControlsJoinColumn(ItemButton, kNnInventoryRows); // FUN_005a4b50
    WireControlsJoinColumn(ItemView, kNnInventoryRows);
    WinControl::Draw(); // FUN_005a0760

    if (EmptyCargoBar == nullptr || FullCargoBar == nullptr || SelectedCargoBar == nullptr)
        return;

    // The volume the dealer's offer (dealer +0x390 times +0x988) frees (+0x910) or, with
    // nothing selected, would take.
    int OfferVolume = 0;
    if (HoveredCargoVolume == 0 && Dealer != nullptr && Dealer->TransferItemArchId != 0)
    {
        Archetype::Equipment* const Offer = Archetype::GetEquipment(Dealer->TransferItemArchId);
        if (Offer != nullptr)
        {
            if (SelectedIndex != -1)
            {
                // ftol of the extended product; the port forms it in double.
                HoveredCargoVolume = static_cast<int>(static_cast<double>(Dealer->Quantity) *
                                            static_cast<double>(Offer->Volume));
                if (Mode == kNnFilterWeapons || Mode == kNnFilterEquipment)
                {
                    EquipDesc* const Item = GetSelectedItem(); // FUN_0047e790
                    if (bDealerMode != 0 || (Item != nullptr && Item->bEquipped))
                        HoveredCargoVolume = 0;
                }
            }
            else
            {
                HoveredCargoVolume = 0;
                if (PendingMountHardpoint.Value[0] == '\0')
                {
                    OfferVolume = static_cast<int>(static_cast<double>(Dealer->Quantity) *
                                                   static_cast<double>(Offer->Volume));
                }
                if (Mode == kNnFilterWeapons || Mode == kNnFilterEquipment)
                    OfferVolume = 0;
            }
        }
    }

    // The free fraction stays on the x87 stack for the next block, hence double.
    float Capacity = 0.0f;
    float Used = 0.0f;
    Ship.GetCargoCapacityAndUsed(&Capacity, &Used);
    float Fill = 0.0f;
    float Offered = 0.0f;
    double Free = 0.0;
    if (Capacity > 0.0f) // skipped at <= 0 or unordered
    {
        float const Inverse = 1.0f / Capacity;
        Free = (static_cast<double>(Capacity) - static_cast<double>(Used)) *
               static_cast<double>(Inverse);
        Fill = static_cast<float>(static_cast<double>(HoveredCargoVolume) * static_cast<double>(Inverse));
        Offered = static_cast<float>(static_cast<double>(OfferVolume) *
                                     static_cast<double>(Inverse));
    }

    WinControl* Bar = FullCargoBar;
    float const HalfX = Bar->HalfExtents.x;
    float const HalfY = Bar->HalfExtents.y;
    float const Width = HalfX + HalfX;
    float Right = static_cast<float>(static_cast<double>(HalfX) -
                                     static_cast<double>(Width) * Free);
    float Split = 0.0f;

    bool bDrawFirst = true;
    if (HoveredCargoVolume > 0)
    {
        if (Fill < 0.025f) // DAT_005d15b4; unordered keeps Fill
            Fill = 0.025f;
        Split = static_cast<float>(static_cast<double>(Right) -
                                   static_cast<double>(Width) * static_cast<double>(Fill));
    }
    else if (OfferVolume <= 0)
    {
        bDrawFirst = false;
    }
    else
    {
        Split = Right;
        Right = static_cast<float>(static_cast<double>(Width) * static_cast<double>(Offered) +
                                   static_cast<double>(Right));
    }

    IRenderPipeline* const Pipe = DALib::Pipeline;
    if (bDrawFirst)
    {
        Pipe->SetRenderState(D3DRS_ZENABLE, 1);
        Pipe->SetRenderState(D3DRS_ZFUNC, 8);
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
        NnInventoryDrawBarQuad(Bar, Split, HalfX, HalfY);
        Pipe->SetRenderState(D3DRS_ZFUNC, 4);
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 0);
        Bar = SelectedCargoBar;
        DALib::Renderer->RenderInstance(Win3DGetCamera(), Bar->MeshHolder.EngineInstance, 1.0f, 0, nullptr,
                               nullptr); // IRenderer2 +0x1c, &DAT_0067dbf8
    }

    // The second quad uses the selected bar's frame when the first was drawn, else the full one's.
    Pipe->SetRenderState(D3DRS_ZENABLE, 1);
    Pipe->SetRenderState(D3DRS_ZFUNC, 8);
    Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
    NnInventoryDrawBarQuad(Bar, Right, HalfX, HalfY);
    Pipe->SetRenderState(D3DRS_ZFUNC, 4);
    DALib::Renderer->RenderInstance(Win3DGetCamera(), EmptyCargoBar->MeshHolder.EngineInstance, 1.0f, 0,
                           nullptr, nullptr);
}

// The byte several NN_Inventory methods compute before FUN_004c6a50 (e.g. 0x0047e112,
// 0x0047f945): true unless the window sits next to a dealer in trade mode 1.
static bool NnInventoryShowFlag(NnDealer const* Dealer)
{
    return Dealer == nullptr || Dealer->TradeMode == kTradeModeShip || Dealer->TradeMode == kTradeModeEquipment;
}

intptr_t NnInventory::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_00479d10 (RET 0xc). A switch over 0x13..0x4f (byte table 0x0047acdc); WinControl's
    // slot 42 is never called. Every case that does not return by itself falls into the
    // dealer block after the switch (caseD_14, 0x0047abce), which returns 0.
    switch (Msg)
    {
    case kWinNotifyToolTip:
    {
        // A child asks for its tooltip.
        WinControl* const Control = reinterpret_cast<WinControl*>(A);
        if (Control == FullCargoBar)
        {
            float Capacity = 0.0f;
            float Used = 0.0f;
            Ship.GetCargoCapacityAndUsed(&Capacity, &Used); // FUN_004c63e0
            GetIdsStringWWithRawFallback(0x49b, ToolTipFormat, 0xff);
            int const Free = static_cast<int>(static_cast<double>(Capacity) - Used); // ftol
            int const Total = static_cast<int>(Capacity);                             // ftol
            swprintf(ToolTipText, 0xff, ToolTipFormat, Free, Total);

            FToolTipHoverInfo Info;
            ToolTipHoverInit(&Info);
            ToolTipHoverSetCaption(&Info, ToolTipText);
            Info.IdsAlt = 0x43b;
            Info.Ids = 0x43b;
            // IEngine slot 40 (+0xa0): 3x4 transform, floats 9..11 the position.
            const Transform* const Transform =
                DALib::Engine->GetInstanceTransform(FullCargoBar->MeshHolder.EngineInstance);
            Info.WorldPos[0] = Transform->Translation.x;
            Info.WorldPos[1] = Transform->Translation.y;
            Info.WorldPos[2] = Transform->Translation.z;
            Info.HalfExtents[0] = FullCargoBar->HalfExtents.x;
            Info.HalfExtents[1] = FullCargoBar->HalfExtents.y;
            Info.HalfExtents[2] = FullCargoBar->HalfExtents.z;
            Info.bLive = 1;
            NnToolTipFeedHover(&Info); // FUN_004be370
            return 1; // ~FToolTipHoverInfo
        }

        // An item button; anything else returns 0 without the dealer block.
        int Row = 0;
        for (; Row < kNnInventoryRows; ++Row)
        {
            if (Control == ItemButton[Row])
                break;
        }
        if (Row == kNnInventoryRows)
            return 0;
        WinControl* const Toggle = EquipToggle[Row];
        if (Toggle == nullptr || (Toggle->Flags & 2) == 0)
            return 0;
        if (Control->ToolTipIds == 0)
            return 0;

        // Retail does not initialise State before the toggle's slot 42 0x2f writes it.
        int State = 0;
        Toggle->SetProperty(0x2f, reinterpret_cast<uintptr_t>(&State), 0);
        GetIdsStringWWithRawFallback(static_cast<unsigned>(Control->ToolTipIds), G_NnTextScratchB,
                                     G_NnTextScratchCapacity);
        GetIdsStringWWithRawFallback(State != 0 ? 0x52a : 0x52b, G_NnTextScratchA,
                                     G_NnTextScratchCapacity);
        wcscat(G_NnTextScratchB, G_NnTextScratchA);

        // Keyed by the button, at its wire centre (+0x350).
        FToolTipHoverInfo Info;
        ToolTipHoverInit(&Info);
        ToolTipHoverSetCaption(&Info, G_NnTextScratchB);
        Info.IdsAlt = static_cast<int>(reinterpret_cast<intptr_t>(Control));
        Info.Ids = 0;
        WireStaticControl const* const Wire = static_cast<WireStaticControl const*>(ItemButton[Row]);
        Info.WorldPos[0] = Wire->WireCenter[0];
        Info.WorldPos[1] = Wire->WireCenter[1];
        Info.WorldPos[2] = Wire->WireCenter[2];
        Info.HalfExtents[0] = ItemButton[Row]->HalfExtents.x;
        Info.HalfExtents[1] = ItemButton[Row]->HalfExtents.y;
        Info.HalfExtents[2] = ItemButton[Row]->HalfExtents.z;
        Info.bLive = 1;
        NnToolTipFeedHover(&Info); // FUN_004be370
        return 1; // ~FToolTipHoverInfo
    }

    case kWinNotifyHighlight:
    case kWinNotifyUnhighlight:
    {
        // The mouse enters (0x2c) or leaves (0x2d) a control of a row; the row's button and view
        // (other than the sender) get the same message.
        WinControl* const Control = reinterpret_cast<WinControl*>(A);
        int Row = 0;
        for (; Row < kNnInventoryRows; ++Row)
        {
            if (Control == ItemButton[Row] || Control == ItemView[Row] ||
                Control == ItemGoodDeal[Row] || Control == ItemDamage[Row] ||
                Control == EquipToggle[Row])
                break;
        }
        if (Row == kNnInventoryRows)
            break;
        if (Msg == kWinNotifyUnhighlight)
        {
            if (HoverIndex == FirstRow + Row)
                HoverIndex = -1;
        }
        else
        {
            HoverIndex = FirstRow + Row;
        }
        if (Control != ItemButton[Row])
            ItemButton[Row]->SetProperty(Msg, 0, 0);
        if (Control != ItemView[Row])
            ItemView[Row]->SetProperty(Msg, 0, 0);
        break;
    }

    case 0x15:
        // The ship view picked an instance.
        SelectHardpoint(Ship.ShipView->GetHardpointOfInstance(static_cast<long>(A))); // FUN_00585e20
        if (Dealer != nullptr)
            Dealer->ClearSelection();
        break;

    case 0x16:
    {
        // An item dropped on hardpoint A (docked only).
        if (!IsPlayerDockedRetail()) // FUN_004c4810
            break;
        CacheString Hardpoint;
        Hardpoint.Value = StringAlloc(reinterpret_cast<char const*>(A), false);
        if (Dealer != nullptr && Dealer->GetSelectedItem() != nullptr)
        {
            // The dealer's selected item, mounted straight from the dealer.
            EquipDesc* const Item = Dealer->GetSelectedItem();
            if (Item == nullptr)
                break;
            Ship.ShipView->CollectHpTypes(Hardpoint.Value); // FUN_00586560
            int const Filter = NnItemFilterMode(Item->ArchId);
            int Group = 0;
            if (Filter == kNnFilterWeapons)
                Group = 0;
            else if (Filter == kNnFilterEquipment)
                Group = 1;
            else
                break;
            if (!Ship.ShipView->HasHpTypeGroup(Group)) // FUN_00586800
                break;
            if (Ship.MountItem(Item, &Hardpoint, 0, true)) // FUN_004c6530
                Dealer->Ship.RemoveItem(Item, 0);          // FUN_004c6430
            ClearSelection();
            Dealer->ClearSelection();
            break;
        }

        // This window's selected item moves to the hardpoint.
        if (GetSelectedItem() == nullptr)
            break;
        EquipDesc* const Item = GetSelectedItem();
        if (Item == nullptr)
            break;
        Ship.ShipView->CollectHpTypes(Hardpoint.Value); // FUN_00586560
        int const Filter = NnItemFilterMode(Item->ArchId);
        int Group = 0;
        if (Filter == kNnFilterWeapons)
            Group = 0;
        else if (Filter == kNnFilterEquipment)
            Group = 1;
        else
            break;
        if (!Ship.ShipView->HasHpTypeGroup(Group)) // FUN_00586800
            break;
        Ship.SetItemHardpoint(Item, Hardpoint.Value); // FUN_004c6910
        if (!(Dealer != nullptr && Dealer->TradeMode == kTradeModeShip) && IsPlayerDockedRetail())
        {
            ReqPlayerEquipmentRetail(&Ship.Loadout); // FUN_004c4040
            FObjectShip* const GfShip = GfObjectShip();
            if (GfShip != nullptr)
            {
                GfShip->SetEquipment(&Ship.Loadout);
                GfShip->RebuildModel();
            }
        }
        ClearSelection();
        break;
    }

    case 0x13:
        if (Dealer != nullptr)
            Dealer->ShowShipPackage(static_cast<unsigned>(A)); // FUN_00483d40
        break;

    case 0x47:
        if (Dealer != nullptr)
        {
            Dealer->TradeMode = static_cast<int>(A);
            Dealer->Field980 = 1;
        }
        break;

    case 0x48:
    {
        // Open the window: A / B are the result callback's argument and function.
        int const TradeMode = (Dealer != nullptr) ? Dealer->TradeMode : kTradeModeNone;
        if (Dealer != nullptr)
        {
            Field4e4 = 1;
            Dealer->TradeMode = TradeMode;
            Dealer->ProcessMessage(kWinMsgShow, 0, 0);
        }
        bNoTransaction = 1;
        ResultArg = reinterpret_cast<void*>(A);
        ResultFn = reinterpret_cast<FNnInventoryResultFn>(B);
        if (TradeMode == kTradeModeCommodity)
            SetMode(kNnFilterCommodity, true);
        else if (TradeMode == kTradeModeShip || TradeMode == kTradeModeEquipment)
            SetMode(kNnFilterWeapons, true);

        SavedLoadout.Items.clear();
        EquipDescList& PlayerList = GetPlayerEquipDescListRetail(); // FUN_004c4060
        if (&SavedLoadout != &PlayerList)
            SavedLoadout = PlayerList;

        Ship.ShipArchId = GetPlayerShipArchetypeIdRetail(); // FUN_004c3e10
        Ship.LoadShip(&GetPlayerEquipDescListRetail(), 1);   // FUN_004c62b0
        // Retail then calls FUN_0053e4a0(+0x4e2) on +0x330, an empty `RET 4`.

        // The dealer's +0x364..+0x37c.
        if (Dealer != nullptr)
        {
            TransferItemButton = Dealer->TransferItemButton;
            TransferItemView = Dealer->TransferItemView;
            TransferItemMesh = Dealer->TransferItemMesh;
            TransferItemName = Dealer->TransferItemName;
            TransferItemDamage = Dealer->TransferItemDamage;
            TransferItemPrice = Dealer->TransferItemPrice;
            TransferItemGoodDeal = Dealer->TransferItemGoodDeal;
        }
        RefreshItemList(&Ship.Loadout, 0, 1); // FUN_0047d2f0
        if (GetParent() != nullptr)
            HudPostCommand(USER_INVENTORY, 0); // FUN_005763b0
        break;
    }

    case 0x45:
    {
        // The quantity edit's text changed.
        wchar_t const* const Text = reinterpret_cast<wchar_t const*>(A);
        if (Text == nullptr)
        {
            QuantityControl[1]->SetProperty(kScrollPropSetValue, 1, 0);
            break;
        }
        int Value = 0;
        swscanf(Text, L"%d", &Value);
        if (Value < 1)
            JettisonQuantity = 1;
        else if (Value > JettisonMaxQuantity)
            JettisonQuantity = JettisonMaxQuantity;
        else
            JettisonQuantity = Value;
        swprintf(ToolTipText, 0xff, L"%d", JettisonQuantity); // PTR_DAT_005d0ec4
        if (_wcsicmp(Text, ToolTipText) != 0)
            QuantityControl[3]->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
        QuantityControl[1]->SetProperty(kScrollPropSetValue, static_cast<uintptr_t>(JettisonQuantity), 0);
        break;
    }

    case kWinNotifyScrollValue:
        // A slider moved: the list scroll or the quantity slider.
        if (reinterpret_cast<WinControl*>(A) == InventoryScroll)
        {
            FirstRow = static_cast<int>(B);
            break;
        }
        if (reinterpret_cast<WinControl*>(A) == QuantityControl[1])
        {
            JettisonQuantity = static_cast<int>(B);
            if (JettisonQuantity < 0)
                JettisonQuantity = 0;
            swprintf(ToolTipText, 0xff, L"%d", JettisonQuantity);
            QuantityControl[3]->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
        }
        break;

    case 0x1a:
    case kWinNotifyClick:
    {
        // A click.
        WinControl* const Control = reinterpret_cast<WinControl*>(A);
        if (Control == CloseButton)
        {
            NnMainButtonOnModalOpen(); // FUN_0045a460
            return 1;
        }
        if (Control == TradeRouteButton)
        {
            if (Mode == kNnFilterCommodity && SelectedIndex != -1)
            {
                EquipDesc* const Item = GetSelectedItem();
                if (Item != nullptr)
                {
                    unsigned const ArchId = Item->ArchId;
                    NnMainButtonOnModalOpen(); // FUN_0045a460
                    WinFactoryCreate("NN_CommodityByBase", "NN_CommodityByBase", nullptr,
                                     reinterpret_cast<void*>(static_cast<uintptr_t>(ArchId)));
                }
            }
            return 1;
        }
        if (Control == QuantityControl[4])
        {
            ShowQuantityControls(false);
            return 1;
        }
        if (Control == QuantityControl[6])
        {
            // Jettison the chosen quantity. The item is not null-tested.
            ShowQuantityControls(false);
            if (SelectedIndex != -1 && Field4e3 == 0)
            {
                EquipDesc* const Item = GetSelectedItem();
                if (!Item->bEquipped)
                {
                    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
                    if (Player != nullptr)
                    {
                        static_cast<IObjAffect*>(Player)->jettison_cargo(Item->Id, JettisonQuantity,
                                                                  static_cast<int>(GetPlayerConnectionId()));
                        JettisonCooldown = 1.0f;
                        NnVoiceEnqueue(G_NnVoiceCargoJettisonedId, 0);
                    }
                }
            }
            return 1;
        }

        for (int Row = 0; Row < kNnInventoryRows; ++Row)
        {
            if (Control == ItemButton[Row] || Control == ItemView[Row] ||
                Control == ItemGoodDeal[Row])
            {
                SelectRow(Row, 1);
                bShowShipInfoCard = 0;
                if (Dealer != nullptr)
                    Dealer->ClearSelection();
                break;
            }
            if (Control == EquipToggle[Row])
            {
                SelectRow(Row, 1);
                int State = 0; // retail: uninitialised until slot 42 0x2f writes it
                EquipToggle[Row]->SetProperty(0x2f, reinterpret_cast<uintptr_t>(&State), 0);
                EquipDesc* const Item = GetSelectedItem();
                if (Item != nullptr)
                    ToggleEquip(Item, State); // FUN_0047f720
                break;
            }
        }

        if (Control == FilterCargoBay)
            SetMode(kNnFilterCargoBay, true);
        else if (Control == FilterWeapons)
            SetMode(kNnFilterWeapons, true);
        else if (Control == FilterEquipment)
            SetMode(kNnFilterEquipment, true);
        else if (Control == FilterCommodity)
            SetMode(kNnFilterCommodity, true);
        else if (Control == FilterAmmo)
            SetMode(kNnFilterAmmo, true);
        else if (Control == JettisonButton)
            JettisonSelected(); // FUN_0047e650
        else if (Control == Ship.InfoText)
        {
            SelectRow(-1, 1);
            bShowShipInfoCard = 1;
        }
        break;
    }

    default:
        break;
    }

    // The shared tail (0x0047abce): a ship dealer (trade mode 2) with a ship view shows this
    // window's selected item on its own ship too.
    if (Dealer != nullptr && Dealer->TradeMode == kTradeModeShip && Dealer->Ship.ShipView != nullptr &&
        GetSelectedItem() != nullptr)
    {
        EquipDesc* const Item = GetSelectedItem();
        Dealer->ClearSelection();
        Dealer->Ship.ShipView->SelectEquipDesc(Item, true); // FUN_00585d20
        Dealer->Ship.ShipView->SelectInstance(Ship.ShipView->SelInstance, false,
                                              static_cast<int>(Item->ArchId)); // FUN_00585bd0
    }
    return 0;
}

void NnInventory::SelectRow(int Row, int Unused)
{
    // FUN_0047e0e0 (RET 8).
    (void)Unused;
    ShowQuantityControls(false);
    if (Row == -1)
    {
        FirstRow = 0;
        if (InventoryScroll != nullptr)
            InventoryScroll->SetProperty(kScrollPropSetValue, 0, 0);
    }
    bool const bFlag = NnInventoryShowFlag(Dealer);
    SelectedItem = nullptr;
    SelectedIndex = FirstRow + Row;

    if (Dealer != nullptr)
    {
        int Count = 1;
        EquipDesc* const Selected = GetSelectedItem();
        if (Selected != nullptr)
            Count = Selected->Count;
        NnDealer* const Partner = Dealer;
        if (Count > 1)
            Partner->QuantitySlider->SetProperty(kScrollPropSetRange, 1, static_cast<uintptr_t>(Count));
        Partner->Quantity = Count;
        Partner->QuantitySlider->SetProperty(kScrollPropSetValue, static_cast<uintptr_t>(Count), 0);
        swprintf(Partner->ToolTipText, 0xff, L"%d", Partner->Quantity); // PTR_DAT_005d0ec4
        Partner->QuantityEdit->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(Partner->ToolTipText), 0);
    }

    EquipDesc* const Item = GetSelectedItem();
    Ship.ShowItem(Item, bFlag, Item != nullptr ? NnItemFilterMode(Item->ArchId) : kNnFilterNone);

    // Retail clears +0x4e8 / +0x4ec a second time after ClearSelection.
    if (Dealer != nullptr)
    {
        Dealer->ClearSelection();
        Dealer->ShownInfoArchId = 0;
        Dealer->ShownInfoIds = 0;
    }
}

void NnInventory::SelectHardpoint(char const* Name)
{
    // FUN_0047e270 (RET 4).
    if (Name == nullptr)
        return;
    if (Dealer != nullptr && Dealer->TradeMode == kTradeModeCommodity)
        return;

    SelectedIndex = 0;
    SelectedItem = nullptr;
    ShowQuantityControls(false);
    EquipDesc* const First = GetSelectedItem();
    if (First != nullptr && _stricmp(First->Hardpoint, Name) == 0) // DACOM stricmp
    {
        Ship.ShowItem(First, NnInventoryShowFlag(Dealer), NnItemFilterMode(First->ArchId));
        return;
    }

    int const SavedFirstRow = FirstRow;
    int const SavedIndex = SelectedIndex;
    EquipDesc* const SavedItem = First;
    int const SavedMode = Mode;
    Ship.ShipView->CollectHpTypes(Name); // FUN_00586560
    if (Ship.ShipView->HasHpTypeGroup(0)) // FUN_00586800
        SetMode(kNnFilterWeapons, true);
    else if (Ship.ShipView->HasHpTypeGroup(1))
        SetMode(kNnFilterEquipment, true);
    else if (Ship.ShipView->HasHpType(1)) // FUN_00586510
        SetMode(kNnFilterCargoBay, true);

    // Walk the list page by page; stop after a page whose last row is empty.
    SelectRow(-1, 1);
    FirstRow = 0;
    EquipDesc* Found = nullptr;
    do
    {
        for (int Row = 0; Row < kNnInventoryRows; ++Row)
        {
            SelectRow(Row, 1);
            Found = GetSelectedItem();
            if (Found != nullptr && _stricmp(Found->Hardpoint, Name) == 0)
            {
                if (InventoryScroll != nullptr)
                    InventoryScroll->SetProperty(kScrollPropSetValue, static_cast<uintptr_t>(FirstRow), 0);
                return;
            }
        }
        ++FirstRow;
    } while (Found != nullptr);

    FirstRow = SavedFirstRow;
    SelectedIndex = SavedIndex;
    SelectedItem = SavedItem;
    Mode = SavedMode;
}

void NnInventory::JettisonSelected()
{
    // FUN_0047e650. The selected item is not null-tested.
    if (SelectedIndex == -1)
        return;
    if (Field4e3 != 0)
        return;
    EquipDesc* Item = GetSelectedItem();
    if (Item->bEquipped)
        return;
    JettisonQuantity = 1;
    JettisonMaxQuantity = Item->Count;
    if (JettisonMaxQuantity != 1)
    {
        ShowQuantityControls(true);
        QuantityControl[1]->SetProperty(kScrollPropSetRange, 1, static_cast<uintptr_t>(JettisonMaxQuantity));
        QuantityControl[1]->SetProperty(kScrollPropSetValue, static_cast<uintptr_t>(JettisonQuantity), 0);
        swprintf(ToolTipText, 0xff, L"%d", JettisonQuantity); // PTR_DAT_005d0ec4
        QuantityControl[3]->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
        return;
    }

    // A single item goes at once (the conditions are tested again).
    ShowQuantityControls(false);
    if (SelectedIndex == -1)
        return;
    if (Field4e3 != 0)
        return;
    Item = GetSelectedItem();
    if (Item->bEquipped)
        return;
    IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW()); // FUN_0054baf0
    if (Player == nullptr)
        return;
    static_cast<IObjAffect*>(Player)->jettison_cargo(Item->Id, JettisonQuantity,
                                              static_cast<int>(GetPlayerConnectionId()));
    JettisonCooldown = 1.0f;
    NnVoiceEnqueue(G_NnVoiceCargoJettisonedId, 0);
}

void NnInventory::ToggleEquip(EquipDesc* Item, int State)
{
    // FUN_0047f720 (RET 8).
    int Filter = kNnFilterWeapons; // only read on the hardpoint path
    bool bHardpointItem = false;
    if (Item != nullptr)
    {
        Filter = NnItemFilterMode(Item->ArchId);
        bHardpointItem = (Filter == kNnFilterWeapons || Filter == kNnFilterEquipment);
    }

    if (!bHardpointItem)
    {
        // An item without a hardpoint (Item is not null-tested here). In a dealer's trade mode 3
        // every change goes through the server (FUN_004c4540).
        bool const bThroughServer = Dealer != nullptr && Dealer->TradeMode == kTradeModeEquipment;
        if (State != 0)
        {
            if (bThroughServer)
            {
                ReqModifyPlayerEquipRetail(Item->Id, &Item->get_hardpoint(), Item->Count,
                                           Item->Health, false);
                return;
            }
            Item->set_equipped(false);
            return;
        }

        // Equip Item alone: every other item of its class comes off.
        Archetype::Equipment* const Arch = Archetype::GetEquipment(Item->ArchId);
        if (Arch != nullptr)
        {
            Archetype::AClassType const Class = Arch->get_class_type();
            for (std::list<EquipDesc>::iterator It = Ship.Loadout.Items.begin();
                 It != Ship.Loadout.Items.end(); ++It)
            {
                EquipDesc* const Other = &*It;
                if (Other == Item)
                    continue;
                Archetype::Equipment* const OtherArch = Archetype::GetEquipment(Other->ArchId);
                if (OtherArch == nullptr)
                    continue;
                if (OtherArch->get_class_type() != Class)
                    continue;
                if (Dealer != nullptr && Dealer->TradeMode == kTradeModeEquipment)
                    ReqModifyPlayerEquipRetail(Other->Id, &Other->get_hardpoint(), Other->Count,
                                               Other->Health, false);
                else
                    Other->set_equipped(false);
            }
        }
        if (Dealer != nullptr && Dealer->TradeMode == kTradeModeEquipment)
        {
            ReqModifyPlayerEquipRetail(Item->Id, &Item->get_hardpoint(), Item->Count, Item->Health,
                                       true);
            return;
        }
        Item->set_equipped(true);
        return;
    }

    // Weapons and equipment.
    FocusEquipId = Item->Id;
    if (State != 0)
    {
        // Into the cargo bay, when the hold has room for it.
        float Volume = 0.0f;
        if (Item->ArchId != 0)
        {
            Archetype::Equipment* const Arch = Archetype::GetEquipment(Item->ArchId);
            if (Arch != nullptr)
                Volume = Arch->Volume; // +0x5c
        }
        int Count = Item->Count;
        if (Count < 1)
            Count = 1;
        float Capacity = 0.0f;
        float Used = 0.0f;
        Ship.GetCargoCapacityAndUsed(&Capacity, &Used); // FUN_004c63e0
        // 64-bit ftol on both sides, compared unsigned.
        unsigned const Need =
            static_cast<unsigned>(static_cast<long long>(static_cast<double>(Count) * Volume));
        unsigned const Free = static_cast<unsigned>(
            static_cast<long long>(static_cast<double>(Capacity) - Used));
        if (Need > Free)
        {
            NnVoiceEnqueue(G_NnVoiceInsufficientCargoSpaceId, 0);
            return;
        }

        PlayOrRewindUiSoundSlot(0x11); // FUN_005645c0
        if (Filter == kNnFilterWeapons)
            G_WeaponGroups.RemoveHardpoint(Item->Hardpoint); // FUN_004f6380
        Item->make_internal();
        Item->set_equipped(false);
        Ship.ShowItem(Item, NnInventoryShowFlag(Dealer), NnItemFilterMode(Item->ArchId));

        // No dealer: a dealer window itself refreshes in trade mode 2, the player's own window
        // syncs the server.
        if (Dealer == nullptr)
        {
            if (bDealerMode != 0)
            {
                if (static_cast<NnDealer*>(this)->TradeMode != kTradeModeShip)
                    return;
                Ship.RefreshView(); // FUN_004c6e70
                return;
            }
        }
        else if (Dealer->TradeMode != kTradeModeEquipment)
        {
            Ship.RefreshView(); // FUN_004c6e70
            return;
        }
    }
    else
    {
        // Onto the first free hardpoint that takes it.
        char const* const Hardpoint = Ship.ShipView->FindHardpointForEquipDesc(Item, false); // FUN_00585ef0
        if (Hardpoint == nullptr)
        {
            NnVoiceEnqueue(G_NnVoiceNoPlaceToMountId, 0);
            return;
        }
        CacheString NewHardpoint;
        NewHardpoint.Value = StringAlloc(Hardpoint, false);
        Item->set_hardpoint(NewHardpoint);
        Item->set_equipped(true);
        NnVoiceEnqueue(G_NnVoiceMountedId, 0);

        // Retail quirk: FUN_004c6860 is handed the GOOD id, not the archetype id.
        FInventoryShipModel::PlayMountSound(Arch2Good(Item->ArchId));

        // A gun whose projectile needs no ammo joins weapon group 0.
        Archetype::Equipment* const Arch = Archetype::GetEquipment(Item->ArchId);
        if (Arch != nullptr && Arch->get_class_type() == Archetype::AClassGun)
        {
            ID_String const& ProjectileName = static_cast<Archetype::Launcher*>(Arch)->ProjectileArchetype;
            if (!ProjectileName.IsEmpty())
            {
                Archetype::Equipment* const AmmoArch = Archetype::GetEquipmentByName(ProjectileName);
                if (AmmoArch != nullptr &&
                    !static_cast<Archetype::Projectile*>(AmmoArch)->bRequiresAmmo) // +0x78
                    G_WeaponGroups.AddHardpoint(0, Hardpoint); // FUN_004f6200
            }
        }

        Ship.ShowItem(Item, NnInventoryShowFlag(Dealer), NnItemFilterMode(Item->ArchId));
        if (Dealer != nullptr && Dealer->TradeMode != kTradeModeEquipment)
        {
            Ship.RefreshView(); // FUN_004c6e70
            return;
        }
    }

    if (IsPlayerDockedRetail()) // FUN_004c4810
    {
        ReqPlayerEquipmentRetail(&Ship.Loadout); // FUN_004c4040
        FObjectShip* const GfShip = GfObjectShip();
        if (GfShip != nullptr)
        {
            GfShip->SetEquipment(&Ship.Loadout);
            GfShip->RebuildModel();
        }
    }
}

// PTR_s_Interface_NeuroNet_Inventory_INV_green_circle_00611a4c: the good-deal icons FUN_0047c740
// indexes with the market verdict (0..2 selling to the dealer, 3..5 buying from it).
static char const* const G_NnGoodDealIcons[6] = {
    "Interface\\NeuroNet\\Inventory\\INV_green_circle.3db",   // 005d0c7c
    "Interface\\NeuroNet\\Inventory\\INV_red_circle.3db",     // 005d0c4c
    "Interface\\NeuroNet\\Inventory\\INV_yellow_circle.3db",  // 005d0c18
    "Interface\\NeuroNet\\Inventory\\INV_green_square.3db",   // 005d0be4
    "Interface\\NeuroNet\\Inventory\\INV_red_square.3db",     // 005d0bb4
    "Interface\\NeuroNet\\Inventory\\INV_yellow_square.3db",  // 005d0b80
};

// Inlined in FUN_0047c740 (0x0047c939, byte table 0x0047d2bc): the class number (1..10) a
// hardpoint type carries in the item name -- hp_gun_special_1..10 (0xf..0x18),
// hp_turret_special_1..10 (0x19..0x22) and the three shield-special runs (0x25..0x42); 0 for
// every other type.
static int NnHpTypeClassNumber(int HpType)
{
    unsigned const Index = static_cast<unsigned>(HpType - HpTypeGunSpecial1);
    if (Index > 0x33) // through HpTypeFreighterShieldSpecial10
        return 0;
    if (HpType == HpTypeTorpedoSpecial2 || HpType == HpTypeTorpedoSpecial1)
        return 0;
    if (HpType <= HpTypeTurretSpecial10)
        return (HpType - HpTypeGunSpecial1) % 10 + 1;
    return (HpType - HpTypeFighterShieldSpecial1) % 10 + 1;
}

bool NnInventory::FillItemRow(int* Counter, unsigned ArchId, int FilterMode, int Count, int Price,
                              int Health, unsigned char bShowToggle, unsigned char bEquipped,
                              unsigned char bMission, unsigned char bUsable, int EmptyIds,
                              int HpClass, unsigned char bAlwaysShowCount)
{
    // FUN_0047c740 (RET 0x34).
    (void)HpClass; // the twelfth argument is never read
    if (ArchId == 0 && EmptyIds == 0)
        return false;

    // The dealer's transfer panel mirrors the selected entry when all of it exists.
    unsigned char bTransfer = 0;
    if ((bDealerMode != 0 || Dealer != nullptr) && TransferItemMesh != nullptr &&
        TransferItemName != nullptr && TransferItemDamage != nullptr &&
        TransferItemPrice != nullptr && TransferItemGoodDeal != nullptr)
        bTransfer = 1;

    if (FilterMode != Mode && FilterMode != -1)
        return false;

    // The selected entry records itself on the dealer (or on this window).
    unsigned char const bSelected = (*Counter == SelectedIndex) ? 1 : 0;
    if (bSelected == 0)
        bTransfer = 0;
    if (ArchId == 0)
    {
        bTransfer = 0;
        if (bSelected != 0)
        {
            if (Dealer == nullptr)
                TransferItemArchId = 0;
            else
                Dealer->TransferItemArchId = 0;
        }
    }
    else if (bTransfer != 0)
    {
        if (Dealer == nullptr)
        {
            TransferItemMaxCount = Count;
            TransferItemArchId = ArchId;
            TransferItemUnitPrice = Price;
        }
        else
        {
            Dealer->TransferItemArchId = ArchId;
            Dealer->TransferItemMaxCount = Count;
            Dealer->TransferItemUnitPrice = Price;
        }
    }

    GoodInfo const* Good = nullptr;
    if (ArchId != 0)
        Good = GoodList::find_by_archetype(ArchId);
    if (Good == nullptr && EmptyIds == 0)
        return false;

    // Entries off the page are only counted.
    unsigned const Row = static_cast<unsigned>(*Counter - FirstRow);
    if (Row < kNnInventoryRows)
    {
        // In modes 0 / 2 a special hardpoint type adds its class number to the name
        // ("<name> <IDS 0x3df><n><IDS 0x3e0>").
        WinControl* const NameText = ItemName[Row];
        if (NameText != nullptr)
        {
            if (ArchId == 0)
            {
                if (EmptyIds != 0)
                    NameText->SetProperty(kTextPropSetIds, 0x38a, 0);
            }
            else
            {
                Archetype::Equipment* const Arch = Archetype::GetEquipment(ArchId);
                if (Arch == nullptr)
                {
                    NameText->SetProperty(kTextPropSetIds, 0, 0);
                }
                else
                {
                    int ClassNumber = 0;
                    if (Mode == kNnFilterWeapons || Mode == kNnFilterEquipment)
                    {
                        int HpType = static_cast<int>(Arch->get_hp_type()); // vt+0x18
                        if (Arch->get_class_type() == Archetype::AClassGun)
                        {
                            Archetype::Gun const* const Gun = static_cast<Archetype::Gun const*>(Arch);
                            int const TypeCount = Gun->get_number_of_hp_types();
                            for (int Index = 0; Index < TypeCount; ++Index)
                            {
                                if (static_cast<int>(Gun->get_hp_type_by_index(Index)) > HpType)
                                    HpType = static_cast<int>(Gun->get_hp_type_by_index(Index));
                            }
                        }
                        ClassNumber = NnHpTypeClassNumber(HpType);
                    }
                    if (ClassNumber != 0)
                    {
                        GetIdsStringWWithRawFallback(static_cast<unsigned>(Arch->IdsName), G_NnTextScratchB,
                                                     G_NnTextScratchCapacity);
                        GetIdsStringWWithRawFallback(0x3df, G_NnTextScratchA, G_NnTextScratchCapacity);
                        wcscat(G_NnTextScratchB, G_NnTextScratchA);
                        swprintf(G_NnTextScratchA, G_NnTextScratchCapacity, L"%d", ClassNumber); // PTR_DAT_005d0ec4
                        wcscat(G_NnTextScratchB, G_NnTextScratchA);
                        GetIdsStringWWithRawFallback(0x3e0, G_NnTextScratchA, G_NnTextScratchCapacity);
                        wcscat(G_NnTextScratchB, G_NnTextScratchA);
                        NameText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
                        if (bTransfer != 0)
                            TransferItemName->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
                    }
                    else
                    {
                        NameText->SetProperty(kTextPropSetIds, static_cast<uintptr_t>(Arch->IdsName), 0);
                        if (bTransfer != 0)
                            TransferItemName->SetProperty(kTextPropSetIds, static_cast<uintptr_t>(Arch->IdsName), 0);
                    }
                }
            }
            NameText->SetProperty(0xc, bSelected, 0);
        }

        // A dealer outside trade mode 2, or a ship dealer's non-combinable good, shows no count
        // unless bAlwaysShowCount.
        WinControl* const QuantityText = ItemQuantity[Row];
        if (QuantityText != nullptr)
        {
            int Shown = Count;
            bool bForceZero = false;
            if (bDealerMode != 0)
            {
                if (static_cast<NnDealer*>(this)->TradeMode != kTradeModeShip)
                    bForceZero = true;
                else if (Good != nullptr && !Good->bCombinable) // +0x70
                    bForceZero = true;
            }
            bool bShowCount = false;
            if (bForceZero)
            {
                Shown = 0;
                bShowCount = bAlwaysShowCount != 0;
            }
            else
            {
                bShowCount = Shown > 0 || bAlwaysShowCount != 0;
            }
            if (bShowCount)
            {
                swprintf(ToolTipText, 0xff, L"%d", Shown); // PTR_DAT_005d0ec4
                QuantityText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
                QuantityText->SetProperty(0xc, bSelected, 0);
                QuantityText->SetProperty(kWinPropHide, 0, 0);
            }
            else
            {
                QuantityText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(L""), 0); // DAT_005c8ce0
            }
            if (Mode == kNnFilterCargoBay)
            {
                unsigned char bOn = 1;
                if (ArchId == G_NanobotArchId || ArchId == G_ShieldBatteryArchId || bDealerMode != 0)
                    bOn = 0;
                QuantityText->SetProperty(kWinPropHide, bOn, 0);
            }
            if (EmptyIds != 0)
            {
                QuantityText->SetProperty(kTextPropSetIds, static_cast<uintptr_t>(EmptyIds), 0);
                QuantityText->SetProperty(kWinPropHide, 0, 0);
                QuantityText->SetProperty(0xc, bSelected, 0);
            }
            else if (bDealerMode == 0 && (Mode == kNnFilterWeapons || Mode == kNnFilterEquipment))
            {
                QuantityText->SetProperty(kWinPropHide, 1, 0);
            }
        }

        WinControl* const PriceText = ItemPrice[Row];
        if (PriceText != nullptr)
        {
            if (Price > 0 && bMission == 0)
            {
                FormatGroupedNumber(ToolTipText, Price, -1); // FUN_004779a0
                PriceText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
                PriceText->SetProperty(0xc, bSelected, 0);
                if (bTransfer != 0)
                {
                    TransferItemPrice->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
                    TransferItemGoodDeal->Flags = static_cast<unsigned char>(TransferItemGoodDeal->Flags | 3);
                    bool bShowDeal = true;
                    if (bDealerMode != 0)
                        bShowDeal = static_cast<NnDealer*>(this)->TradeMode == kTradeModeCommodity;
                    else if (Dealer != nullptr)
                        bShowDeal = Dealer->TradeMode == kTradeModeCommodity;
                    if (bShowDeal)
                        TransferItemGoodDeal->Flags = static_cast<unsigned char>(TransferItemGoodDeal->Flags | 3);
                    else
                        TransferItemGoodDeal->Flags = static_cast<unsigned char>(TransferItemGoodDeal->Flags & 0xfc);
                }
                if (ItemGoodDeal[Row] != nullptr)
                    ItemGoodDeal[Row]->SetProperty(kWinPropDisable, 0, 0);
            }
            else
            {
                PriceText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(L""), 0); // DAT_005c8ce0
                if (ItemGoodDeal[Row] != nullptr)
                    ItemGoodDeal[Row]->SetProperty(kWinPropDisable, 1, 0);
                if (bTransfer != 0)
                {
                    TransferItemPrice->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(L""), 0);
                    TransferItemGoodDeal->SetProperty(kWinPropDisable, 1, 0);
                }
            }
        }

        if (CargoMissionText[Row] != nullptr)
        {
            if (bMission != 0)
                CargoMissionText[Row]->SetProperty(kTextPropSetIds, 0x576, 0);
            else
                CargoMissionText[Row]->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(L""), 0);
        }

        if (ItemDamage[Row] != nullptr)
        {
            ItemDamage[Row]->SetProperty(0x40, 100, static_cast<uintptr_t>(Health));
            unsigned Colour = 0xff2626ffu; // bytes ff 26 26 ff
            if (Health >= 70)
                Colour = 0xff30c24bu;      // bytes 4b c2 30 ff
            else if (Health >= 30)
                Colour = 0xff26eaf5u;      // bytes f5 ea 26 ff
            ItemDamage[Row]->SetProperty(0x3f, Colour, 0);
            if (bTransfer != 0)
            {
                TransferItemDamage->SetProperty(0x40, 100, static_cast<uintptr_t>(Health));
                TransferItemDamage->SetProperty(0x3f, Colour, 0);
            }
        }

        // In modes 0 / 2 an unequipped good needs a hardpoint of the ship that takes it.
        if (EquipToggle[Row] != nullptr)
        {
            unsigned char bToggle = bShowToggle;
            if (ArchId == G_NanobotArchId || ArchId == G_ShieldBatteryArchId)
            {
                bToggle = 0;
            }
            else if (bEquipped == 0 && (Mode == kNnFilterWeapons || Mode == kNnFilterEquipment))
            {
                if (Good == nullptr)
                    bToggle = 0;
                else if (Ship.ShipView->FindHardpointForGood(Good->Id, false) == nullptr) // FUN_00585f20
                    bToggle = 0;
            }
            EquipToggle[Row]->SetProperty(kWinPropHide, bToggle == 0 ? 1 : 0, 0);
            if (bToggle != 0)
                EquipToggle[Row]->SetProperty(0x30, bEquipped != 0 ? 0 : 1, 0);
        }

        // The good-deal marker; the market is not null-tested.
        if (ItemGoodDeal[Row] != nullptr && Good != nullptr)
        {
            int IconIndex = 0;
            unsigned Ids = 0;
            unsigned const BaseId = GetClientLocationCurrentBase();
            if (BaseId != 0)
            {
                FClientMarket* const Market = GetClientLocationBaseMarket(BaseId);
                if (Dealer != nullptr)
                {
                    // Against good_sell_price (+0x64) and bad_sell_price (+0x68); unordered falls
                    // to icon 1.
                    double const MarketPrice =
                        static_cast<double>(ClientMarketGetPrice(Market, Good->Id));
                    if (MarketPrice >= static_cast<double>(Good->GoodSellPrice)) // unordered: false
                    {
                        IconIndex = 0;
                        Ids = 0x41a;
                    }
                    else if (MarketPrice > static_cast<double>(Good->BadSellPrice))
                    {
                        IconIndex = 2;
                        Ids = 0x41c;
                    }
                    else
                    {
                        IconIndex = 1;
                        Ids = 0x41b;
                    }
                }
                else
                {
                    // Against good_buy_price (+0x5c) and bad_buy_price (+0x60); unordered falls
                    // to icon 4.
                    double const MarketPrice =
                        static_cast<double>(ClientMarketGetPrice(Market, Arch2Good(ArchId)));
                    if (MarketPrice <= static_cast<double>(Good->GoodBuyPrice))
                    {
                        IconIndex = 3;
                        Ids = 0x417;
                    }
                    else if (MarketPrice < static_cast<double>(Good->BadBuyPrice))
                    {
                        IconIndex = 5;
                        Ids = 0x419;
                    }
                    else
                    {
                        IconIndex = 4;
                        Ids = 0x418;
                    }
                }
            }
            ItemGoodDeal[Row]->SetProperty(kWinPropMesh, reinterpret_cast<uintptr_t>(G_NnGoodDealIcons[IconIndex]), 0);
            ItemGoodDeal[Row]->SetProperty(kWinPropToolTip, Ids, 0);
            if (bTransfer != 0)
            {
                TransferItemGoodDeal->SetProperty(kWinPropToolTip, Ids, 0);
                TransferItemGoodDeal->SetProperty(kWinPropMesh, reinterpret_cast<uintptr_t>(G_NnGoodDealIcons[IconIndex]), 0);
            }
        }

        if (ItemView[Row] != nullptr)
            ItemView[Row]->SetProperty(0xc, bSelected, 0);

        WinControl* const Mesh = ItemMesh[Row];
        if (Mesh != nullptr && ItemButton[Row] != nullptr)
        {
            int NameIds = 0;
            if (Good != nullptr)
            {
                Mesh->UserData = static_cast<int>(Row);
                char const* const Icon = Good->ItemIcon.Value; // +0x78
                if (Icon != nullptr)
                {
                    Mesh->SetProperty(kWinPropMesh, reinterpret_cast<uintptr_t>(Icon), 0);
                    if (bTransfer != 0)
                        TransferItemMesh->SetProperty(kWinPropMesh, reinterpret_cast<uintptr_t>(Icon), 0);
                }
                NameIds = Good->IdsName; // +0x88
            }
            else
            {
                // Retail's two EmptyIds branches issue the same call.
                Mesh->SetProperty(0x17, static_cast<uintptr_t>(-1), 0);
            }
            Mesh->SetProperty(0x19, 0x10000, 0x10000);
            if (InfoCardView != nullptr)
                InfoCardView->SetProperty(0x19, 0, 0);
            Mesh->SetProperty(0xc, bSelected, 0);
            ItemButton[Row]->SetProperty(0xc, bSelected, 0);
            ItemButton[Row]->SetProperty(kWinPropToolTip, static_cast<uintptr_t>(NameIds), 0);
        }

        // Grey an unusable, unselected row.
        if (ItemButton[Row] != nullptr && ItemView[Row] != nullptr && ItemName[Row] != nullptr &&
            ItemPrice[Row] != nullptr && ItemQuantity[Row] != nullptr)
        {
            unsigned char bGrey = (bUsable == 0 && bSelected == 0) ? 1 : 0;
            unsigned char Dark[4] = {0x80, 0x80, 0x80, 0xff};
            ItemButton[Row]->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(Dark), bGrey);
            ItemView[Row]->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(Dark), bGrey);
            unsigned char Light[4] = {0xc0, 0xc0, 0xc0, 0xff};
            ItemPrice[Row]->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(Light), bGrey);
            if (ArchId == 0)
                bGrey = 1;
            ItemName[Row]->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(Light), bGrey);
            if (EmptyIds != 0)
                bGrey = 1;
            ItemQuantity[Row]->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(Light), bGrey);
        }
    }

    ++*Counter;
    return bSelected != 0;
}

void NnInventory::BindControls()
{
    // FUN_0047b410 -- every lookup below is slot 37 (+0x94, FindChild) on this window.
    bNeedBind = 0;
    CreateControls(); // +0xb4

    InventoryScroll = FindChild("Inventory_Scroll");
    PlayerCash = FindChild("PlayerCash");
    ItemName[0] = FindChild("ItemName01");
    ItemName[1] = FindChild("ItemName02");
    ItemName[2] = FindChild("ItemName03");
    ItemName[3] = FindChild("ItemName04");
    ItemName[4] = FindChild("ItemName05");
    ItemView[0] = FindChild("ItemView01");
    ItemView[1] = FindChild("ItemView02");
    ItemView[2] = FindChild("ItemView03");
    ItemView[3] = FindChild("ItemView04");
    ItemView[4] = FindChild("ItemView05");
    ItemPrice[0] = FindChild("ItemPrice01");
    ItemPrice[1] = FindChild("ItemPrice02");
    ItemPrice[2] = FindChild("ItemPrice03");
    ItemPrice[3] = FindChild("ItemPrice04");
    ItemPrice[4] = FindChild("ItemPrice05");
    ItemQuantity[0] = FindChild("ItemQuantity01");
    ItemQuantity[1] = FindChild("ItemQuantity02");
    ItemQuantity[2] = FindChild("ItemQuantity03");
    ItemQuantity[3] = FindChild("ItemQuantity04");
    ItemQuantity[4] = FindChild("ItemQuantity05");
    ItemButton[0] = FindChild("ItemButton1");
    ItemButton[1] = FindChild("ItemButton2");
    ItemButton[2] = FindChild("ItemButton3");
    ItemButton[3] = FindChild("ItemButton4");
    ItemButton[4] = FindChild("ItemButton5");
    ItemMesh[0] = FindChild("ItemMesh1");
    ItemMesh[1] = FindChild("ItemMesh2");
    ItemMesh[2] = FindChild("ItemMesh3");
    ItemMesh[3] = FindChild("ItemMesh4");
    ItemMesh[4] = FindChild("ItemMesh5");
    ItemGoodDeal[0] = FindChild("ItemGoodDeal01");
    ItemGoodDeal[1] = FindChild("ItemGoodDeal02");
    ItemGoodDeal[2] = FindChild("ItemGoodDeal03");
    ItemGoodDeal[3] = FindChild("ItemGoodDeal04");
    ItemGoodDeal[4] = FindChild("ItemGoodDeal05");
    ItemDamage[0] = FindChild("ItemDamage1");
    ItemDamage[1] = FindChild("ItemDamage2");
    ItemDamage[2] = FindChild("ItemDamage3");
    ItemDamage[3] = FindChild("ItemDamage4");
    ItemDamage[4] = FindChild("ItemDamage5");
    EquipToggle[0] = FindChild("EquipToggle1");
    EquipToggle[1] = FindChild("EquipToggle2");
    EquipToggle[2] = FindChild("EquipToggle3");
    EquipToggle[3] = FindChild("EquipToggle4");
    EquipToggle[4] = FindChild("EquipToggle5");

    // HitPriority is set through slot 45 in retail.
    for (int Row = 0; Row < kNnInventoryRows; ++Row)
    {
        if (ItemGoodDeal[Row] != nullptr)
            ItemGoodDeal[Row]->HitPriority = 100;
        if (CargoMissionText[Row] != nullptr)
            CargoMissionText[Row]->HitPriority = 100;
        if (ItemDamage[Row] != nullptr)
            ItemDamage[Row]->SetProperty(0x19, 0x10000, 0);
        if (EquipToggle[Row] != nullptr)
        {
            EquipToggle[Row]->HitPriority = 100;
            EquipToggle[Row]->SetProperty(0x34, 1, 0);
        }
        if (ItemButton[Row] != nullptr)
            ItemButton[Row]->SetProperty(0x35, 1, 0);
    }

    InfoCardBackground = FindChild("InfoCard_Background");
    InfoCardName = FindChild("InfoCard_Name");
    InfoCardInfo = static_cast<RichTextWin*>(FindChild("InfoCard_Info"));
    InfoCardClose = FindChild("InfoCard_Close");
    InfoCardView = FindChild("InfoCard_View");
    InfoCardScroll = FindChild("InfoCard_Scroll");
    if (InfoCardClose != nullptr)
        InfoCardClose->HitPriority = 100;
    if (InfoCardView != nullptr)
    {
        // +0x518; the view is a ShipViewControl.
        ShipViewControl* const View = static_cast<ShipViewControl*>(InfoCardView);
        View->ViewFlags = static_cast<unsigned char>(View->ViewFlags & 0x7f);
    }

    FilterWeapons = FindChild("Filter_Weapons");
    FilterCargoBay = FindChild("Filter_CargoBay");
    FilterEquipment = FindChild("Filter_Equipment");
    FilterCommodity = FindChild("Filter_Commodity");
    FilterAmmo = FindChild("Filter_Ammo");

    Ship.InfoText = FindChild("ShipView_Info");
    Ship.Toggle = FindChild("ShipView_Toggle");
    Ship.DamageBar = FindChild("ShipView_DamageBar");
    Ship.NameText = FindChild("ShipView_Name");
    Ship.Frame = FindChild("Shipview_Frame");
    Ship.ShipView = static_cast<ShipViewControl*>(FindChild("ShipView_Control"));

    SetMode(G_NnInventoryLastMode, true);

    JettisonButton = FindChild("JettisonButton");
    Ship.ShipArchId = GetPlayerShipArchetypeIdRetail(); // FUN_004c3e10
    Ship.LoadShip(&GetPlayerEquipDescListRetail(), 1);  // FUN_004c4060
    if (bDealerMode == 0)
        Ship.ShipView->BuildHardpointList(); // FUN_00586b70
    RefreshItemList(&Ship.Loadout, 0, 1);

    PlayerTitle = FindChild("PlayerTitle");
    if (bDealerMode == 0 && Dealer == nullptr)
        bShowShipInfoCard = 1;
}

void NnInventory::ClearInfoCard()
{
    // FUN_0047e9e0. This window's own card is cleared only on a dealer.
    if (InfoCardInfo != nullptr && bDealerMode != 0)
    {
        InfoCardInfo->SetScrollOrigin(0, 0); // FUN_005798b0
        InfoCardInfo->ClearContent();        // FUN_0057b370
    }

    if (Dealer != nullptr && Dealer->InfoCardInfo != nullptr)
    {
        Dealer->InfoCardInfo->SetScrollOrigin(0, 0);
        Dealer->InfoCardInfo->ClearContent();
    }

    // Only the first of the three dealer panes is null-tested.
    if (InfoCardFacts != nullptr)
    {
        InfoCardFacts->SetScrollOrigin(0, 0);
        InfoCardFacts->ClearContent();
        InfoCardStats->SetScrollOrigin(0, 0);
        InfoCardStats->ClearContent();
        InfoCardValues->SetScrollOrigin(0, 0);
        InfoCardValues->ClearContent();
    }

    // Retail stores the partner dealer's +0x4e8 twice, reloading +0x360 in between.
    ShownInfoArchId = 0;
    if (Dealer != nullptr)
    {
        Dealer->ShownInfoArchId = 0;
        Dealer->ShownInfoArchId = 0;
    }
}

void NnInventory::ShowInfoCard(unsigned ArchId, unsigned Ids)
{
    // FUN_0047ba90 (RET 8).
    if (QuantityControl[0] != nullptr && (QuantityControl[0]->Flags & 2) != 0)
        return;

    // Borrow the partner dealer's card when this window has none.
    if (InfoCardInfo == nullptr && Dealer != nullptr)
        InfoCardInfo = Dealer->InfoCardInfo;

    if (ArchId == 0)
    {
        ShownInfoArchId = 0;
        Field4e1 = 0;
        if (Ids == 0)
        {
            ShownInfoIds = 0;
            if (Dealer != nullptr)
            {
                Dealer->ShownInfoArchId = 0;
                Dealer->ShownInfoIds = 0;
            }
            if (bDealerMode != 0)
            {
                NnInventory* const Partner = static_cast<NnDealer*>(this)->Inventory;
                Partner->ShownInfoArchId = 0;
                Partner->ShownInfoIds = 0;
            }
            return;
        }

        if (Dealer != nullptr)
        {
            Dealer->ShownInfoArchId = 0;
            Dealer->ShownInfoIds = 0;
        }
        if (bDealerMode != 0)
        {
            NnInventory* const Partner = static_cast<NnDealer*>(this)->Inventory;
            Partner->ShownInfoArchId = 0;
            Partner->ShownInfoIds = 0;
        }

        if (ShownInfoIds == Ids)
            return;
        ShownInfoIds = Ids;

        // FUN_0057da40's result is not read.
        RenderDisplayList Text;
        {
            StyleNode Style(kTextStyleBody);
            RdlAppendNode(Text, Style.Clone());
        }
        LoadIdsIntoRdl(ShownInfoIds, Text);

        // The dealer panes give way to the single card; a window without them relabels its
        // title (+0x944).
        if (InfoCardFacts != nullptr)
        {
            InfoCardFacts->Flags = static_cast<unsigned char>(InfoCardFacts->Flags & 0xfc);
            InfoCardStats->Flags = static_cast<unsigned char>(InfoCardStats->Flags & 0xfc);
            InfoCardValues->Flags = static_cast<unsigned char>(InfoCardValues->Flags & 0xfc);
            InfoCardInfo->Flags = static_cast<unsigned char>(InfoCardInfo->Flags | 3);
        }
        else if (InfoCardTitle != nullptr)
        {
            InfoCardTitle->SetProperty(kTextPropSetIds, 0x388, 0);
        }

        // The card is not null-tested here.
        InfoCardInfo->SetScrollOrigin(0, 0); // FUN_005798b0
        InfoCardInfo->ClearContent();        // FUN_0057b370
        InfoCardInfo->AppendContent(Text);   // FUN_0057b3e0
        return;                              // FUN_0046ba90: the list goes with Text
    }

    if (ShownInfoArchId == ArchId)
        return;
    ShownInfoArchId = ArchId;
    ShownInfoIds = 0;
    if (Dealer != nullptr)
    {
        Dealer->ShownInfoArchId = 0;
        Dealer->ShownInfoIds = 0;
    }
    if (bDealerMode != 0)
    {
        NnInventory* const Partner = static_cast<NnDealer*>(this)->Inventory;
        Partner->ShownInfoArchId = 0;
        Partner->ShownInfoIds = 0;
    }

    int IdsName = 0;
    int IdsFirst = 0;
    int IdsSecond = 0;
    bool bTradeText = false; // retail reuses the second argument's byte

    if (bShowShipInfoCard != 0)
    {
        Archetype::Ship const* const ShipArch = Archetype::GetShip(ArchId);
        if (ShipArch != nullptr)
        {
            IdsFirst = ShipArch->IdsInfo1;
            IdsName = ShipArch->IdsName;
            IdsSecond = ShipArch->IdsInfo;

            if ((bDealerMode != 0 || Dealer != nullptr) && InfoCardFacts != nullptr)
            {
                // Panes 1..3: IdsInfo1..3 (+0xec / +0xf0 / +0xf4).
                RenderDisplayList Facts;
                {
                    StyleNode Style(kTextStyleBody);
                    RdlAppendNode(Facts, Style.Clone());
                }
                LoadIdsIntoRdl(static_cast<unsigned>(ShipArch->IdsInfo1), Facts);

                // The single card is not null-tested.
                InfoCardFacts->Flags = static_cast<unsigned char>(InfoCardFacts->Flags | 3);
                InfoCardStats->Flags = static_cast<unsigned char>(InfoCardStats->Flags | 3);
                InfoCardValues->Flags = static_cast<unsigned char>(InfoCardValues->Flags | 3);
                InfoCardInfo->Flags = static_cast<unsigned char>(InfoCardInfo->Flags & 0xfc);

                InfoCardFacts->SetScrollOrigin(0, 0);
                InfoCardFacts->ClearContent();
                InfoCardFacts->AppendContent(Facts);

                RenderDisplayList Stats;
                {
                    StyleNode Style(kTextStyleBody);
                    RdlAppendNode(Stats, Style.Clone());
                }
                LoadIdsIntoRdl(static_cast<unsigned>(ShipArch->IdsInfo2), Stats);
                InfoCardStats->SetScrollOrigin(0, 0);
                InfoCardStats->ClearContent();
                InfoCardStats->AppendContent(Stats);

                RenderDisplayList Values;
                {
                    StyleNode Style(kTextStyleBody);
                    RdlAppendNode(Values, Style.Clone());
                }
                LoadIdsIntoRdl(static_cast<unsigned>(ShipArch->IdsInfo3), Values);
                InfoCardValues->SetScrollOrigin(0, 0);
                InfoCardValues->ClearContent();
                InfoCardValues->AppendContent(Values);

                // The name label is not set on this path.
                return;
            }
        }
    }
    else
    {
        // Item mode: the good's texts, else the archetype's.
        Archetype::Equipment const* const Equip = Archetype::GetEquipment(ArchId);
        GoodInfo const* const Good = GoodList::find_by_archetype(ArchId);
        if (Good != nullptr)
        {
            // FUN_005835f0 == 1 (no hp type) asks for the trade text; retail's two out-values
            // land in stack slots overwritten next.
            Archetype::Equipment const* OutWeapon = nullptr;
            Archetype::Equipment const* OutShield = nullptr;
            bTradeText = ShipViewEquipmentHpClass(ArchId, &OutWeapon, &OutShield) == 1;
            IdsName = Good->IdsName;
            IdsFirst = Good->IdsInfo;
        }
        else if (Equip != nullptr)
        {
            IdsName = Equip->IdsName;
            IdsFirst = Equip->IdsInfo;
        }
    }

    if (InfoCardName != nullptr)
        InfoCardName->SetProperty(kTextPropSetIds, static_cast<uintptr_t>(IdsName), 0);

    if (InfoCardInfo == nullptr)
        return;

    RenderDisplayList Card;
    {
        StyleNode Style(kTextStyleBody);
        RdlAppendNode(Card, Style.Clone());
    }
    if (IdsFirst != 0)
        LoadIdsIntoRdl(static_cast<unsigned>(IdsFirst), Card);
    if (IdsSecond != 0)
        LoadIdsIntoRdl(static_cast<unsigned>(IdsSecond), Card);

    // Items with a stat table get the three panes (switch table 0x0047c72c).
    if (bShowShipInfoCard == 0 && InfoCardFacts != nullptr)
    {
        Archetype::Equipment* const Equip = Archetype::GetEquipment(ArchId);
        if (Equip != nullptr)
        {
            int const Class = static_cast<int>(Equip->get_class_type()); // vt+0x10
            bool bTwoColumn = false;
            switch (Class)
            {
            case Archetype::AClassShieldGenerator:
            case Archetype::AClassThruster:
            case Archetype::AClassGun:
            case Archetype::AClassMineDropper:
            case Archetype::AClassCounterMeasureDropper:
            case Archetype::AClassRepairKit:
            case Archetype::AClassShieldBattery:
            case Archetype::AClassMunition:
            case Archetype::AClassMine:
            case Archetype::AClassCounterMeasure:
                bTwoColumn = true;
                break;
            default:
                break;
            }

            if (bTwoColumn)
            {
                // The info text goes to pane 1.
                InfoCardFacts->Flags = static_cast<unsigned char>(InfoCardFacts->Flags | 3);
                InfoCardStats->Flags = static_cast<unsigned char>(InfoCardStats->Flags | 3);
                InfoCardValues->Flags = static_cast<unsigned char>(InfoCardValues->Flags | 3);
                InfoCardInfo->Flags = static_cast<unsigned char>(InfoCardInfo->Flags & 0xfc);
                InfoCardFacts->SetScrollOrigin(0, 0);
                InfoCardFacts->ClearContent();
                InfoCardFacts->AppendContent(Card);

                // Pane 2: the stat labels (L"\n" is DAT_005d0254).
                RenderDisplayList Labels;
                {
                    StyleNode Style(kTextStyleCardTitle);
                    RdlAppendNode(Labels, Style.Clone());
                }
                LoadIdsIntoRdl(0x6e1, Labels);
                AppendTextMaybeXmlRdl(L"\n", static_cast<unsigned>(wcslen(L"\n")), Labels, false);
                {
                    StyleNode Style(kTextStyleBody);
                    RdlAppendNode(Labels, Style.Clone());
                }
                BuildDealerEquipmentStatText(Labels, ArchId, true, false); // FUN_00484090(,,1,0)
                InfoCardStats->SetScrollOrigin(0, 0);
                InfoCardStats->ClearContent();
                InfoCardStats->AppendContent(Labels);

                // Pane 3: the stat values.
                RenderDisplayList Values;
                {
                    StyleNode Style(kTextStyleBody);
                    RdlAppendNode(Values, Style.Clone());
                }
                AppendTextMaybeXmlRdl(L"\n", static_cast<unsigned>(wcslen(L"\n")), Values, false);
                BuildDealerEquipmentStatText(Values, ArchId, false, true); // FUN_00484090(,,0,1)
                InfoCardValues->SetScrollOrigin(0, 0);
                InfoCardValues->ClearContent();
                InfoCardValues->AppendContent(Values);

                return;
            }
        }
    }

    // The single card; a window without panes relabels its title (+0x944, not null-tested).
    if (InfoCardFacts != nullptr)
    {
        InfoCardFacts->Flags = static_cast<unsigned char>(InfoCardFacts->Flags & 0xfc);
        InfoCardStats->Flags = static_cast<unsigned char>(InfoCardStats->Flags & 0xfc);
        InfoCardValues->Flags = static_cast<unsigned char>(InfoCardValues->Flags & 0xfc);
        InfoCardInfo->Flags = static_cast<unsigned char>(InfoCardInfo->Flags | 3);
    }
    else if (bShowShipInfoCard != 0)
    {
        InfoCardTitle->SetProperty(kTextPropSetIds, 0x387, 0);
    }
    else
    {
        InfoCardTitle->SetProperty(kTextPropSetIds, 0x388, 0);
    }

    if (bTradeText && (bDealerMode != 0 || Dealer != nullptr))
    {
        // The commodity's price at every known base.
        AppendCommodityTradeReport(&Card, ArchId);
    }
    else if (bShowShipInfoCard == 0)
    {
        BuildDealerEquipmentStatText(Card, ArchId, true, true); // FUN_00484090(,,1,1)
    }

    InfoCardInfo->SetScrollOrigin(0, 0);
    InfoCardInfo->ClearContent();
    InfoCardInfo->AppendContent(Card);
}

void NnInventory::UpdateRowVisibility(int Count)
{
    // FUN_0047f3e0 (RET 4).
    bool bHideAll = false;
    if (bDealerMode != 0 && static_cast<NnDealer*>(this)->bPreviewingShip != 0) // +0x98c
        bHideAll = true;

    for (int Row = 0; Row < kNnInventoryRows; ++Row)
    {
        // Unsigned compare.
        bool const bHide =
            static_cast<unsigned>(FirstRow + Row) >= static_cast<unsigned>(Count) || bHideAll;

        // The price columns also hide while the partner dealer is hidden.
        bool bHidePrice = bHide;
        if (Dealer != nullptr && (Dealer->Flags & 2) == 0)
            bHidePrice = true;

        // ...and when no price applies: an own inventory next to no dealer, a commodity trader
        // (1) outside the commodity filter, an equipment dealer (3) on the commodity filter.
        bool bNoPrice = true;
        if (Dealer != nullptr)
        {
            int const TradeMode = Dealer->TradeMode;
            if (TradeMode == kTradeModeEquipment)
            {
                if (Mode != kNnFilterCommodity)
                    bNoPrice = false;
            }
            else if (TradeMode == kTradeModeCommodity)
            {
                if (Mode == kNnFilterCommodity)
                    bNoPrice = false;
            }
            if (TradeMode == kTradeModeShip)
                bNoPrice = false;
        }
        if (bDealerMode != 0)
            bNoPrice = false;

        uintptr_t const HidePriceValue = (bHidePrice || bNoPrice) ? 1 : 0;

        if (ItemName[Row] != nullptr)
            ItemName[Row]->SetProperty(kWinPropHide, bHide ? 1 : 0, 0);
        if (ItemPrice[Row] != nullptr)
            ItemPrice[Row]->SetProperty(kWinPropHide, HidePriceValue, 0);
        if (ItemQuantity[Row] != nullptr && bHide)
            ItemQuantity[Row]->SetProperty(kWinPropHide, 1, 0);
        if (ItemGoodDeal[Row] != nullptr)
            ItemGoodDeal[Row]->SetProperty(kWinPropHide, HidePriceValue, 0);
        if (ItemDamage[Row] != nullptr)
            ItemDamage[Row]->SetProperty(kWinPropHide, bHide ? 1 : 0, 0);
        if (CargoMissionText[Row] != nullptr)
            CargoMissionText[Row]->SetProperty(kWinPropHide, bHide ? 1 : 0, 0);

        if (EquipToggle[Row] != nullptr)
        {
            if (bHide || Mode == kNnFilterCommodity || Mode == kNnFilterAmmo)
                EquipToggle[Row]->SetProperty(kWinPropHide, 1, 0);
            if (bDealerMode != 0)
            {
                if (static_cast<NnDealer*>(this)->TradeMode != kTradeModeShip)
                    EquipToggle[Row]->SetProperty(kWinPropDisable, 1, 0);
            }
            else if (!IsPlayerDockedRetail()) // FUN_004c4810
            {
                EquipToggle[Row]->SetProperty(kWinPropDisable, 1, 0);
            }
        }

        if (ItemView[Row] != nullptr && ItemButton[Row] != nullptr && ItemMesh[Row] != nullptr)
        {
            uintptr_t const HideValue = bHide ? 1 : 0;
            ItemButton[Row]->SetProperty(kWinPropHide, 0, 0);
            ItemMesh[Row]->SetProperty(kWinPropHide, HideValue, 0);
            ItemView[Row]->SetProperty(kWinPropHide, 0, 0);
            ItemView[Row]->SetProperty(kWinPropDisable, HideValue, 0);
            ItemButton[Row]->SetProperty(kWinPropDisable, HideValue, 0);
            if (bHide)
            {
                ItemView[Row]->SetProperty(kWinPropToolTip, 0, 0);
                ItemButton[Row]->SetProperty(kWinPropToolTip, 0, 0);
            }
        }
    }

    if (TradeRouteButton == nullptr || TradeRouteText == nullptr)
        return;

    bool bMission = false;
    EquipDesc const* const Selected = GetSelectedItem(); // FUN_0047e790
    if (Selected != nullptr && Selected->bMission)
        bMission = true;

    // IsScanner is slot 46 (true for NN_Scanner).
    if (Mode == kNnFilterCommodity && SelectedIndex != -1 && !bMission && !IsScanner() &&
        (QuantityControl[0] == nullptr || (QuantityControl[0]->Flags & 2) == 0))
    {
        TradeRouteButton->Flags = static_cast<unsigned char>(TradeRouteButton->Flags | 3);
        TradeRouteText->Flags = static_cast<unsigned char>(TradeRouteText->Flags | 3);
        return;
    }

    TradeRouteButton->Flags = static_cast<unsigned char>(TradeRouteButton->Flags & 0xfc);
    TradeRouteText->Flags = static_cast<unsigned char>(TradeRouteText->Flags & 0xfc);
}

// DAT_00611a24 -- the info-card text of an empty hardpoint row, by FShipViewHpEntry::Class
// (read only at 0x0047e03e).
static unsigned const G_NnHpClassInfoIds[10] = {
    0x38b, 0x38c, 0x394, 0x38f, 0x38d, 0x38e, 0x390, 0x391, 0x392, 0x393,
};

void NnInventory::RefreshItemList(EquipDescList const* List, int Arg2, unsigned char bResync)
{
    // FUN_0047d2f0 (RET 0xc). Arg2 is only handed on to the recursive call.
    int const DealerTradeMode = (Dealer != nullptr) ? Dealer->TradeMode : kTradeModeNone;

    // Resync with the player's loadout.
    if (bResync != 0 && GetPlayerShipArchetypeIdRetail() != 0) // FUN_004c3e10
    {
        if (LastEquipRevision != static_cast<int>(GetPlayerEquipRevisionRetail())) // FUN_004c4f90
        {
            bTradePending = 0;
            LastEquipRevision = static_cast<int>(GetPlayerEquipRevisionRetail());
            if (Ship.ShipView != nullptr)
                Ship.ShipView->LoadEquipment(&GetPlayerEquipDescListRetail()); // FUN_005850f0
            Ship.SortFrom(&GetPlayerEquipDescListRetail()); // FUN_004c6b80
            SelectedItem = nullptr;                          // +0x4f4

            // Next to an equipment dealer, the partner's +0x394 names the hardpoint the dealer's
            // selected good would go on (weapons / equipment filters).
            NnDealer* const Partner = Dealer;
            if (Partner != nullptr && Partner->TradeMode == kTradeModeEquipment && Partner->Inventory != nullptr)
            {
                Partner->Inventory->PendingMountHardpoint.clear();
                unsigned const GoodId = Partner->GetSelectedMarketGood(); // FUN_00483580
                if (GoodId != 0 && (Partner->Mode == kNnFilterWeapons || Partner->Mode == kNnFilterEquipment))
                {
                    char const* const Hardpoint =
                        Partner->Inventory->Ship.ShipView->FindHardpointForGood(GoodId, false); // FUN_00585f20
                    if (Hardpoint != nullptr)
                        Partner->Inventory->PendingMountHardpoint.Value = StringAlloc(Hardpoint, false);
                }
            }

            // A pending good (+0x3bc) that no longer fits drops the partner dealer's selection.
            // The ship view is not null-tested here.
            if (bCheckHardpointAfterBuy != 0)
            {
                unsigned const PendingGood = PendingBuyGoodId;
                bCheckHardpointAfterBuy = 0;
                if (Ship.ShipView->FindHardpointForGood(PendingGood, false) == nullptr &&
                    Dealer != nullptr)
                    Dealer->ClearSelection();
            }

            if (bCheckStockAfterBuy != 0)
            {
                bCheckStockAfterBuy = 0;
                bDeselectBoughtGoodIfEmpty = 1;
            }
        }

        // The collision groups changed: reload the hull.
        if (LastCollisionRevision != static_cast<int>(GetPlayerCollisionRevisionRetail())) // FUN_004c4fa0
        {
            LastCollisionRevision = static_cast<int>(GetPlayerCollisionRevisionRetail());
            Archetype::Ship* const ShipArch = Archetype::GetShip(Ship.ShipArchId); // +0x348
            if (ShipArch != nullptr && Ship.ShipView != nullptr)
            {
                Ship.ShipView->LoadEquipment(nullptr);                 // FUN_005850f0(0)
                Ship.ShipView->SetInstance(-1, true);                  // FUN_00585380(-1, 1)
                Ship.ShipView->LoadShip(&ShipArch->Nickname, true, true); // FUN_00585a90(+8, 1, 1)
            }

            if (Ship.ShipView != nullptr)
                Ship.ShipView->LoadEquipment(&GetPlayerEquipDescListRetail());
            Ship.SortFrom(&GetPlayerEquipDescListRetail());

            if (DealerTradeMode == kTradeModeEquipment)
            {
                FObjectShip* const GfShip = GfObjectShip();
                if (GfShip != nullptr)
                {
                    GfShip->SetEquipment(&Ship.Loadout);
                    GfShip->RebuildModel();
                }
            }
        }
    }

    if (PlayerCash != nullptr)
    {
        FormatGroupedNumber(ToolTipText, static_cast<int>(GetPlayerCreditsRetail()), 0x476); // FUN_004779a0(,FUN_004c4ce0(),)
        PlayerCash->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
    }

    // Every use of the market below tests it for null.
    FClientMarket* Market = nullptr;
    {
        unsigned const BaseId = GetClientLocationCurrentBase();
        if (BaseId != 0)
            Market = GetClientLocationBaseMarket(BaseId);
    }
    int Counter = 0;
    EquipDesc const* FirstSelected = nullptr;
    GoodInfo const* FirstGood = nullptr;
    EquipDesc const* Hovered = nullptr;

    // A ship dealer lists every filter at once.
    bool bAllModes = false;
    if (bDealerMode != 0 && static_cast<NnDealer*>(this)->TradeMode == kTradeModeShip)
        bAllModes = true;
    int FocusCounter = -1;
    int SelectedHpClass = -1;

    // An own inventory lists the ship's hardpoints first (empty ones too).
    if (bDealerMode == 0)
    {
        int const HardpointCount = Ship.ShipView->GetHardpointListCount(); // FUN_00587050
        for (int Index = 0; Index < HardpointCount; ++Index)
        {
            FShipViewHpEntry const* const Entry = Ship.ShipView->GetHardpointListEntry(Index); // FUN_00587070
            if (Entry == nullptr)
                continue;

            unsigned ArchId = 0;
            int Price = 0;
            int Health = 0;
            unsigned char bShowToggle = 0;
            unsigned char bEquipped = 0;
            unsigned char bMission = 0;
            int const RowMode = (Entry->Group == 1) ? 2 : 0;

            CacheString Hardpoint;
            Hardpoint.Value = StringAlloc(Entry->Name.c_str(), false);
            EquipDesc const* const Item = List->find_equipment_item(Hardpoint);
            GoodInfo const* Good = nullptr;
            if (Item != nullptr)
            {
                bMission = Item->bMission ? 1 : 0;
                ArchId = Item->ArchId;
                bShowToggle = 1;
                bEquipped = 1;
                Health = static_cast<int>(static_cast<double>(Item->Health) * 100.0); // DAT_005ca230
                Good = GoodList::find_by_archetype(ArchId);

                if ((RowMode == Mode || bAllModes) && Counter == HoverIndex && Hovered == nullptr)
                    Hovered = Item;

                // Transaction type (FUN_00438100) 0 or 1 prices the item; at type 0 a used,
                // non-combinable item takes the used rate (DAT_005d0e2c / DAT_005d1910).
                Price = 0;
                if (Good != nullptr && Market != nullptr)
                {
                    int const Type = ClientMarketGetTransactionType(Market, Good->Id);
                    if (Type == 0 || Type == 1)
                    {
                        Price = static_cast<int>(
                            static_cast<double>(ClientMarketGetPrice(Market, Good->Id)) *
                            static_cast<double>(Item->Health));
                        if (Type == 0 && !Good->bCombinable &&
                            SavedLoadout.find_equipment_item(Item->Id) != nullptr)
                            Price = static_cast<int>(static_cast<double>(Price) *
                                                         0.30000001192092896 +
                                                     0.49998998641967773);
                    }
                }

                if (FocusEquipId != SUBOBJ_ID_NONE && Item->Id == FocusEquipId)
                    FocusCounter = Counter;
            }

            bool const bSelected = FillItemRow(&Counter, ArchId, RowMode, 1, Price, Health,
                                               bShowToggle, bEquipped, bMission, 1, Entry->Ids,
                                               Entry->Class, 0);

            // An empty hardpoint keeps its class for the info card.
            if (FirstSelected == nullptr && bSelected)
            {
                FirstSelected = Item;
                FirstGood = Good;
                if (ArchId == 0)
                    SelectedHpClass = Entry->Class;
            }
        }
    }

    for (std::list<EquipDesc>::const_iterator It = List->Items.begin(); It != List->Items.end(); ++It)
    {
        EquipDesc const* const Item = &*It;
        GoodInfo const* const Good = GoodList::find_by_archetype(Item->ArchId);
        int Filter = NnItemFilterMode(Item->ArchId); // FUN_0047e460

        // Commodities are not scaled by health. The used rate applies to a non-combinable item
        // in this window's saved loadout +0x3a8 (type 0 only), or -- any type -- on a ship
        // dealer to one in its partner's +0x3a8. Retail: the rate (and DAT_005d1910's rounding)
        // applies to the health-scaled price, where NnLoadoutResaleValue / SellSelected scale
        // the used unit price by health afterwards, so list and paid prices can differ by one.
        int Price = 0;
        if (Good != nullptr && Market != nullptr)
        {
            int const Type = ClientMarketGetTransactionType(Market, Good->Id);
            if (Type == 0 || Type == 1)
            {
                if (Filter == kNnFilterCommodity)
                    Price = static_cast<int>(ClientMarketGetPrice(Market, Good->Id));
                else
                    Price = static_cast<int>(
                        static_cast<double>(ClientMarketGetPrice(Market, Good->Id)) *
                        static_cast<double>(Item->Health));

                bool bUsed = false;
                if (Type == 0 && Filter != kNnFilterCommodity && !Good->bCombinable &&
                    SavedLoadout.find_equipment_item(Item->Id) != nullptr)
                    bUsed = true;
                else if (bDealerMode != 0 && static_cast<NnDealer*>(this)->TradeMode == kTradeModeShip &&
                         Filter != kNnFilterCommodity && !Good->bCombinable)
                {
                    NnInventory* const Partner = static_cast<NnDealer*>(this)->Inventory;
                    if (Partner != nullptr &&
                        Partner->SavedLoadout.find_equipment_item(Item->Id) != nullptr)
                        bUsed = true;
                }
                if (bUsed)
                    Price = static_cast<int>(static_cast<double>(Price) * 0.30000001192092896 +
                                             0.49998998641967773);
            }
        }

        int const Health = static_cast<int>(static_cast<double>(Item->Health) * 100.0);
        unsigned char bShowToggle = 0;
        unsigned char bEquipped = 0;
        if (Good != nullptr && Filter != kNnFilterAmmo && Filter != kNnFilterCommodity)
        {
            bEquipped = Item->bEquipped ? 1 : 0;
            bShowToggle = 1;
        }

        if (bAllModes)
            Filter = -1;

        // Mounted weapons and equipment are already on the hardpoint rows.
        bool bSelected = false;
        if (!((Filter == kNnFilterWeapons || Filter == kNnFilterEquipment) && bEquipped != 0))
        {
            if (FocusEquipId != SUBOBJ_ID_NONE && Item->Id == FocusEquipId)
                FocusCounter = Counter;

            // Usable: a free hardpoint for an unmounted weapon / equipment of an own inventory,
            // a launcher for ammo.
            bool bUnusable = false;
            if (bEquipped == 0 && bDealerMode == 0 && Good != nullptr && (Mode == kNnFilterWeapons || Mode == kNnFilterEquipment))
            {
                if (Ship.ShipView->FindHardpointForGood(Good->Id, true) == nullptr) // FUN_00585f20
                    bUnusable = true;
            }
            else if (Mode == kNnFilterAmmo)
            {
                bUnusable = !NnListHasLauncherFor(List, Item->ArchId); // FUN_00482d80
            }

            if ((Filter == Mode || bAllModes) && Counter == HoverIndex && Hovered == nullptr)
                Hovered = Item;

            bSelected = FillItemRow(&Counter, Item->ArchId, Filter, Item->Count, Price, Health,
                                    bShowToggle, bEquipped, Item->bMission ? 1 : 0,
                                    bUnusable ? 0 : 1, 0, 0, 0);
        }

        if (FirstSelected == nullptr && bSelected)
        {
            FirstSelected = Item;
            FirstGood = Good;
        }
    }

    // Empty rows for ammo the list lacks for its launchers, once per ammo type.
    std::vector<unsigned> ListedAmmo;
    for (std::list<EquipDesc>::const_iterator It = List->Items.begin(); It != List->Items.end(); ++It)
    {
        EquipDesc const* const Item = &*It;

        unsigned AmmoId = 0;
        Archetype::Equipment* const Arch = Archetype::GetEquipment(Item->ArchId);
        if (Arch != nullptr)
        {
            int const Class = static_cast<int>(Arch->get_class_type()); // vt+0x10
            if (Class == Archetype::AClassGun || Class == Archetype::AClassMineDropper ||
                Class == Archetype::AClassCounterMeasureDropper)
            {
                ID_String const& Projectile = static_cast<Archetype::Launcher*>(Arch)->ProjectileArchetype; // +0x98
                if (!Projectile.IsEmpty())
                    AmmoId = Projectile.Id;
            }
        }
        if (AmmoId == 0)
            continue;

        if (List->find_matching_cargo(AmmoId, 0, 1.0f) != nullptr)
            continue;

        bool bListed = false;
        for (size_t Seen = 0; Seen < ListedAmmo.size(); ++Seen)
        {
            if (ListedAmmo[Seen] == AmmoId)
            {
                bListed = true;
                break;
            }
        }
        if (bListed)
            continue;

        // The hovered row is the launcher's.
        if ((Mode == kNnFilterAmmo || bAllModes) && Counter == HoverIndex && Hovered == nullptr)
            Hovered = Item;

        bool const bSelected = FillItemRow(&Counter, AmmoId, 1, 0, 0, 100, 0, 0, 0, 0, 0, 0, 1);

        ListedAmmo.push_back(AmmoId);

        // Selecting it selects a stand-in item of that ammo (+0x94c).
        if (FirstSelected == nullptr && bSelected)
        {
            EmptyAmmoStandIn.set_arch_id(AmmoId);
            FirstSelected = &EmptyAmmoStandIn;
            FirstGood = GoodList::find_by_archetype(AmmoId);
        }
    }

    // FirstGood is not null-tested.
    bSelectedSellable = 0;
    if (FirstSelected != nullptr && !FirstSelected->bEquipped && Market != nullptr)
    {
        int const Type = ClientMarketGetTransactionType(Market, FirstGood->Id);
        if (Type == 0 || Type == 1)
            bSelectedSellable = 1;
    }

    HoveredCargoVolume = 0;
    if (Hovered != nullptr)
    {
        HoveredCargoVolume = static_cast<int>(Hovered->get_cargo_space_occupied());
        if ((Mode == kNnFilterWeapons || Mode == kNnFilterEquipment) && (bDealerMode != 0 || Hovered->bEquipped))
            HoveredCargoVolume = 0;
    }

    UpdateRowVisibility(Counter); // FUN_0047f3e0

    // Retail then calls LAB_0053e4a0 (an empty `RET 4`) on &+0x330.

    if (JettisonButton != nullptr)
    {
        unsigned char bMission = 0;
        unsigned char bLootable = 0;
        unsigned char bEquipped = 0;
        if (FirstSelected != nullptr)
        {
            bMission = FirstSelected->bMission ? 1 : 0;
            bEquipped = FirstSelected->bEquipped ? 1 : 0;
            Archetype::Equipment* const Arch = Archetype::GetEquipment(FirstSelected->ArchId);
            if (Arch != nullptr)
                bLootable = Arch->bLootable ? 1 : 0; // +0x6c
        }
        // Disabled while the jettison cooldown (+0x914) runs.
        bool bDisabled = true;
        if (SelectedIndex != -1 && bEquipped == 0 && !(JettisonCooldown > 0.0f) && bMission == 0 &&
            bLootable != 0 && FirstSelected != &EmptyAmmoStandIn)
            bDisabled = false;
        JettisonButton->SetProperty(kWinPropDisable, bDisabled ? 1 : 0, 0);
    }

    if (Ship.DamageBar != nullptr)
    {
        int const ShipHealth = static_cast<int>(static_cast<double>(GetPlayerHealthScaleRetail()) *
                                                100.0); // FUN_004c4f70, DAT_005ca230
        Ship.DamageBar->SetProperty(0x40, 100, static_cast<uintptr_t>(ShipHealth));
    }

    // The row count changed: select the focused item, rescale the scroll bar, keep the focus
    // on the page, then build the list again.
    if (Counter != LastRowCount)
    {
        SelectedIndex = -1;
        if (FocusCounter != -1)
            SelectedIndex = FocusCounter;
        if (InventoryScroll != nullptr)
            InventoryScroll->SetProperty(kScrollPropSetRange, 0, static_cast<uintptr_t>(Counter - 1));

        // Unsigned compares.
        if (FocusCounter != -1)
        {
            if (static_cast<unsigned>(FocusCounter) > static_cast<unsigned>(FirstRow + 4))
                FirstRow = FocusCounter - 4;
            if (static_cast<unsigned>(FirstRow) > static_cast<unsigned>(FocusCounter))
                FirstRow = FocusCounter;
            if (InventoryScroll != nullptr)
                InventoryScroll->SetProperty(kScrollPropSetValue, static_cast<uintptr_t>(FirstRow), 0);
        }

        // No gap below the last row.
        if (FirstRow != 0 &&
            static_cast<unsigned>(FirstRow + 5) > static_cast<unsigned>(Counter))
        {
            if (static_cast<unsigned>(Counter) > 5)
                FirstRow = Counter - 5;
            else
                FirstRow = 0;
            if (InventoryScroll != nullptr)
                InventoryScroll->SetProperty(kScrollPropSetValue, static_cast<uintptr_t>(FirstRow), 0);
        }

        LastRowCount = Counter;
        RefreshItemList(List, Arg2, bResync);
    }

    // +0x4e0 (the card shows the ship) holds on an own inventory with no dealer and no
    // selection; any selection here or on the partner dealer clears it on all three windows.
    if (SelectedIndex != -1 || (Dealer != nullptr && Dealer->SelectedIndex != -1))
    {
        bShowShipInfoCard = 0;
        if (Dealer != nullptr)
            Dealer->bShowShipInfoCard = 0;
        if (bDealerMode != 0)
            static_cast<NnDealer*>(this)->Inventory->bShowShipInfoCard = 0;
    }
    if (bDealerMode == 0 && Dealer == nullptr && SelectedIndex == -1)
        bShowShipInfoCard = 1;

    if (bShowShipInfoCard != 0)
    {
        ShowInfoCard(Ship.ShipArchId, 0);
    }
    else if (!bAllModes && SelectedIndex != -1)
    {
        if (SelectedHpClass != -1)
            ShowInfoCard(0, G_NnHpClassInfoIds[SelectedHpClass]); // DAT_00611a24
        else if (FirstSelected != nullptr)
            ShowInfoCard(FirstSelected->ArchId, 0);
        else
            ShowInfoCard(0, 0);
    }
    else if (SelectedIndex == -1 && Dealer != nullptr && Dealer->SelectedIndex == -1)
    {
        if (Dealer->TradeMode == kTradeModeShip)
            ShowInfoCard(0, 0x487);
        else
            ClearInfoCard(); // FUN_0047e9e0
    }

    FocusEquipId = SUBOBJ_ID_NONE;
}

bool NnInventory::HasEquippedOfClass(unsigned ArchId) const
{
    // FUN_0047fb40 (RET 4).
    Archetype::Equipment* const Arch = Archetype::GetEquipment(ArchId);
    if (Arch == nullptr)
        return false;
    Archetype::AClassType const Class = Arch->get_class_type();
    for (std::list<EquipDesc>::const_iterator It = Ship.Loadout.Items.begin();
         It != Ship.Loadout.Items.end(); ++It)
    {
        EquipDesc const& Item = *It;
        Archetype::Equipment* const ItemArch = Archetype::GetEquipment(Item.ArchId);
        if (ItemArch == nullptr)
            continue;
        if (ItemArch->get_class_type() == Class && Item.bEquipped)
            return true;
    }
    return false;
}

EquipDesc* NnInventory::GetSelectedItem()
{
    // FUN_0047e790.
    if (SelectedIndex == -1)
    {
        SelectedItem = nullptr;
        return nullptr;
    }
    if (SelectedItem != nullptr)
        return SelectedItem;

    // A dealer (+0x35c) in trade mode 2 (+0x978) lists every mode.
    unsigned char bAllModes = 0;
    if (bDealerMode != 0 && static_cast<NnDealer*>(this)->TradeMode == kTradeModeShip)
        bAllModes = 1;

    int Counter = 0;
    if (bDealerMode == 0)
    {
        // The ship view's hardpoint rows first: group 1 (+0x18) belongs to mode 2, every other
        // group to mode 0.
        int const Count = Ship.ShipView->GetHardpointListCount(); // FUN_00587050
        for (int Index = 0; Index < Count; ++Index)
        {
            FShipViewHpEntry const* const Entry = Ship.ShipView->GetHardpointListEntry(Index); // FUN_00587070
            if (Entry == nullptr)
                continue;
            int const RowMode = (Entry->Group == 1) ? 2 : 0;
            if (bAllModes == 0 && RowMode != Mode)
                continue;
            CacheString Hardpoint;
            Hardpoint.Value = StringAlloc(Entry->Name.c_str(), false);
            EquipDesc* const Found = Ship.Loadout.find_equipment_item(Hardpoint);
            if (bAllModes != 0 || RowMode == Mode)
            {
                if (Counter == SelectedIndex)
                {
                    SelectedItem = Found;
                    return Found;
                }
            }
            ++Counter;
        }
    }

    // Then the loadout's items of the mode; a mounted item counts only outside modes 0 and 2,
    // whose mounted rows came from the hardpoint list above.
    for (std::list<EquipDesc>::iterator It = Ship.Loadout.Items.begin();
         It != Ship.Loadout.Items.end(); ++It)
    {
        EquipDesc* const Item = &*It;
        int const Filter = NnItemFilterMode(Item->ArchId); // FUN_0047e460
        if (Filter == kNnFilterNone)
            continue;
        if (bAllModes == 0 && Filter != Mode)
            continue;
        if (Item->bEquipped && (Mode == kNnFilterWeapons || Mode == kNnFilterEquipment))
            continue;
        if (Counter == SelectedIndex)
        {
            SelectedItem = Item;
            return Item;
        }
        ++Counter;
    }
    return SelectedItem;
}

void NnInventoryOpenDealer(int TradeMode, FNnInventoryResultFn ResultFn, void* ResultArg,
                           unsigned ShipPackage)
{
    // FUN_00477ab0 (cdecl).
    if (Win3DFindByName("NN_Dealer") != nullptr)
        return;

    // Made anew when there is none or it carries flag 4.
    Win3D* Window = Win3DFindByName("NN_Inventory");
    bool bCreated = false;
    if (Window == nullptr || (Window->Flags & 4) != 0)
    {
        Window = WinFactoryCreate("NN_Inventory", "NN_Inventory", NnMainButtonGetPanelButton(0x66),
                                  nullptr);
        if (Window == nullptr)
            return;
        Window->Flags = static_cast<unsigned char>(Window->Flags & 0xfc);
        bCreated = true;
    }

    // {-0.64, 0.33, -1.288}.
    Vector const PanelOrigin = {NnFloatBits(0xbf23d70a), NnFloatBits(0x3ea8f5c3),
                                NnFloatBits(0xbfa4dd2f)};
    NnMainButtonSetPanelOrigin(0x66, &PanelOrigin);
    // FUN_0045dcb0 on DAT_0066d3f0 (not null-tested).
    G_NnMainButton->SetInfoCardLayout();

    NnInventory* const Inventory = static_cast<NnInventory*>(Window->AsWinControl());

    Win3D* const DealerWindow =
        WinFactoryCreate("NN_Dealer", "NN_Dealer", Inventory->Parent, nullptr);
    if (DealerWindow != nullptr)
    {
        DealerWindow->Flags = static_cast<unsigned char>(DealerWindow->Flags & 0xfc);
        Inventory->Dealer = static_cast<NnDealer*>(DealerWindow->AsWinControl());
        Inventory->SetProperty(0x47, static_cast<uintptr_t>(TradeMode), 0);
        // The dealer is not null-tested here.
        Inventory->Dealer->BindControls();
        if (bCreated)
            Inventory->BindControls();
        if (Inventory->Dealer != nullptr)
        {
            Inventory->Dealer->Inventory = Inventory; // +0x974
            Inventory->Dealer->Flags = static_cast<unsigned char>(Inventory->Dealer->Flags & 0xfc);
        }
    }

    Inventory->SetProperty(0x48, reinterpret_cast<uintptr_t>(ResultArg),
                           reinterpret_cast<uintptr_t>(ResultFn));
    Inventory->SetProperty(0x13, static_cast<uintptr_t>(ShipPackage), 0);
}

void NnInventoryOpenShipDealer(int Index, FNnInventoryResultFn ResultFn, void* ResultArg)
{
    // FUN_00477bf0 (cdecl). Opens the ship dealer on the Index-th ship (category 3) of the
    // current base's market, else the first.
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    std::list<FClientMarketGood const*> Goods;
    ClientMarketCollectCategory(Market, &Goods, 3); // FUN_00437f70

    if (Goods.empty())
        return;
    FClientMarketGood const* Good = Goods.front();
    int Position = 0;
    for (std::list<FClientMarketGood const*>::const_iterator It = Goods.begin();
         It != Goods.end(); ++It)
    {
        if (Position == Index)
        {
            Good = *It;
            break;
        }
        ++Position;
    }

    NnInventoryOpenDealer(kTradeModeShip, ResultFn, ResultArg, Good->Info.GoodId);
}
