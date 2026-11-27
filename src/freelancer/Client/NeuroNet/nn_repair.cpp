// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_repair.h"

#include "Interface/Pages/character_select_equip.h" // GetPlayerCreditsRetail, GetPlayerHealthScaleRetail, the
                                    // revisions, the collision-group and equipment lists,
                                    // GetPlayerShipArchetypeIdRetail
#include "NeuroNet/Economy/client_base_market.h"     // ClientMarketGetPrice (FUN_00438060),
                                    // GetClientLocationBaseMarket (FUN_0043a510),
                                    // GetClientCurrentLocationBaseData (FUN_0043b630)
#include "Common/client_cliobj.h"          // Archetype::CliObjMaterialPaths (companion +0x20)
#include "Space/HUD/hud_command_dispatch.h"   // EventHandler::PushCancelHandler (FUN_00576360)
#include "Space/HUD/hud_target.h"             // HudIsMousePickBlocked (FUN_0045a740),
                                    // HudCollisionGroupTypeIds (FUN_0045a7c0)
#include "Interface/menu_ui_init.h"           // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "GameFlow/menu_universe_base.h"     // GetClientLocationCurrentBase (DAT_0066873c)
#include "Common/misc_util.h"              // FormatGroupedNumber (FUN_004779a0)
#include "GameFlow/nav_bar.h"                // NavBarCloseService (FUN_004424f0)
#include "NeuroNet/nn_inventory.h"           // ReqPlayerSpendCashRetail, ReqPlayerHullStatusRetail,
                                    // ReqPlayerCollisionGroupsRetail, ReqPlayerEquipmentRetail
#include "NeuroNet/nn_messaging.h"           // NnVoiceEnqueue (FUN_00489e80)
#include "NeuroNet/nn_ui_pulse.h"            // G_NnPaletteGreen
#include "Interface/ship_view_control.h"      // ShipViewEquipmentHpClass (FUN_005835f0)
#include "Interface/ui_scroll.h"
#include "Interface/win3d.h"                  // Win3DReparent, Win3DFindByName, Win3DDeleteTree
#include "Interface/win_factory.h"
#include "Interface/wire_controls.h"          // WireControlsJoinColumn (FUN_005a4b50)

#include "common_archetype.h"
#include "common_goods.h"           // EquipDesc, EquipDescList, GoodInfo, BaseData, Arch2Good
#include "common_id.h"              // CreateID

#include <list>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

// Retail DAT_00672210 -- WINFACTORY<NN_Repair> static (LAB_004b2ba0 -> FUN_004b4830; Create
// FUN_004b48a0; atexit LAB_004b2bc0 -> FUN_00579380).
WINFACTORY<NnRepair> G_NnRepairFactory("NN_Repair");

namespace
{
    char const kBackgroundMesh[] = "Interface\\NeuroNet\\Inventory\\repair_bakgrnd.cmp";
    char const kCloseMesh[] = "Interface\\NeuroNet\\Inventory\\X.3db";
    char const kCheckMesh[] = "Interface\\NeuroNet\\Inventory\\check.3db";
    char const kBlackBoxMesh[] = "Interface\\NeuroNet\\Inventory\\repairblackbox.3db";
    char const kCheckToggleMesh[] = "Interface\\NeuroNet\\Inventory\\checktoggle.3db";
    char const kSelectSound[] = "ui_select_item";
    // PTR_DAT_005d0ec4 -- L"%d".
    wchar_t const kCountFormat[] = L"%d";

    // LAB_004b2bd0 -- DAT_0067220c = CreateID("repair_complete"); read once, by the OK button
    // (0x004b37b5).
    unsigned const G_NnVoiceRepairCompleteId = CreateID("repair_complete");

    // DAT_00672244 / DAT_00672240 / DAT_0067223c (-0.6f / -0.6f / 0.6f, static inits
    // LAB_004b2b70 / LAB_004b2b80 / LAB_004b2b90) are this TU's copies of a header float
    // constant that nothing reads; the port has no counterpart.

    uintptr_t PointerArg(void const* Pointer)
    {
        return reinterpret_cast<uintptr_t>(Pointer);
    }

    // The collision-group tests compare the health's bits against 0x3f800000 (0x004b3ab8 /
    // 0x004b4564), not the value.
    bool IsOneBits(float Value)
    {
        uint32_t Bits = 0;
        memcpy(&Bits, &Value, sizeof(Bits));
        return Bits == 0x3f800000u;
    }

    // The first material path of an equipment archetype's companion, 0 when it has none
    // (inlined at 0x004b3c77 / 0x004b3c9f).
    char const* FirstMaterialPath(Archetype::Equipment const* Equipment)
    {
        Archetype::ICliObj* const Companion = Equipment->Companion;
        if (Companion == nullptr)
            return nullptr;
        Archetype::FCliVc6Vector<Archetype::FCliMaterialPathPair> const* const Paths =
            Archetype::CliObjMaterialPaths(Companion);
        if (Paths->Begin == nullptr || Paths->End - Paths->Begin == 0)
            return nullptr;
        return Paths->Begin->Path;
    }
}

NnRepair::NnRepair()
    : ScrollOffset(0)
    , OkButton(nullptr)
    , CancelButton(nullptr)
    , Hovered(-1)
    , EquipRevision(0xffffffffu)
    , CollisionRevision(0xffffffffu)
    , HealthScale(-1.0f)
{
    // Inlined into FUN_004b48a0 (0x004b48e3). The control arrays are not written.
}

NnRepair::~NnRepair()
{
    // FUN_004b2c10.
}

int NnRepair::SelectedTotal() const
{
    int Total = 0;
    for (size_t Index = 0; Index < Items.size(); ++Index)
    {
        if (Items[Index].bSelected != 0)
            Total += Items[Index].Cost;
    }
    return Total;
}

void NnRepair::ShowTotalCost()
{
    wchar_t Buffer[0x100];
    swprintf(Buffer, 0x100, kCountFormat, SelectedTotal());
    TotalCost->SetProperty(kTextPropSetText, PointerArg(Buffer), 0);
}

void NnRepair::ShowPlayerCash()
{
    wchar_t Buffer[0x100];
    FormatGroupedNumber(Buffer, static_cast<int>(GetPlayerCreditsRetail()), 0x476);
    PlayerCash->SetProperty(kTextPropSetText, PointerArg(Buffer), 0);
}

bool NnRepair::Create(Win3D* NewParent, void* Data)
{
    // FUN_004b2c90 (RET 8).
    SetMesh(kBackgroundMesh, 0);
    WinControlHalfExtentsFromMesh(&HalfExtents, &MeshHolder); // FUN_005a1340

    {
        Vector const Position = {0.0f, -0.204f, 0.0f};
        Vector const Half = {0.133f, 0.026f, 0.0f};
        OkButton = CreateWireButtonControl("RepairOkButton", "ui_execute_transaction", &Position,
                                           &Half, 0, 0, 0x438, 0, 0);
        OkButton->SetProperty(kWinPropHide, 1, 0);
    }
    {
        Vector const Position = {0.0f, -0.216f, 0.0f};
        RepairText = CreateTextControl("RepairText", "ButtonText", &Position, nullptr, 0.265f, 0.06f,
                                       1.5f, 0x427, 1, 0, 0);
        RepairText->SetProperty(kWinPropHide, 1, 0);
    }
    {
        Vector const Position = {0.22f, 0.231f, 0.0f};
        Vector const Half = {0.02f, 0.02f, 0.0f};
        CancelButton = CreateButtonControl("RepairCancelButton", kCloseMesh, nullptr, kSelectSound,
                                           nullptr, &Position, nullptr, &Half, 0, 0, 0, 0, 0x2148, 0,
                                           0);
    }

    // Nine rows, 0.0282 apart, all in float (the item-name row's qword 0x005d5370 is 0.141f
    // widened, so it is the same value).
    char Name[0x100];
    for (int Index = 0; Index < 9; ++Index)
    {
        float const Step = static_cast<float>(Index) * 0.0282f;
        float const CheckY = 0.14f - Step;
        float const RowY = 0.141f - Step;
        float const DamageY = 0.13f - Step;
        Vector const Small = {0.014f, 0.014f, 0.0f};

        sprintf(Name, "RepairCheckBox%d", Index);
        {
            Vector const Position = {-0.215f, CheckY, 0.0f};
            CheckBoxes[Index] = CreateToggleButtonControl(Name, kCheckMesh, kBlackBoxMesh,
                                                          kSelectSound, nullptr, &Position, nullptr,
                                                          &Small, 0, 0, 1, 0, 0x43a, 0);
        }
        sprintf(Name, "RepairCheckBoxFrame%d", Index);
        {
            Vector const Position = {-0.215f, CheckY, 0.0f};
            CheckBoxFrames[Index] = CreateWireButtonControl(Name, kSelectSound, &Position, &Small, 0, 0,
                                                            0, 0, 0);
        }
        sprintf(Name, "RepairIconFrames%d", Index);
        {
            Vector const Position = {-0.176f, RowY, 0.0f};
            IconFrames[Index] = CreateWireButtonControl(Name, kSelectSound, &Position, &Small, 0, 0, 0,
                                                        0, 0);
        }
        sprintf(Name, "RepairSmallIcons%d", Index);
        {
            Vector const Position = {-0.176f, RowY, -1.1f};
            Vector const Zero = {0.0f, 0.0f, 0.0f};
            SmallIcons[Index] = CreateStaticControl(Name, kBlackBoxMesh, &Position, nullptr, &Small, 0,
                                                    0, 0, 1, 0, 1, 1, 0x6e, 0, 0, &Zero);
            // Not pickable.
            SmallIcons[Index]->Flags = static_cast<unsigned char>(SmallIcons[Index]->Flags & ~kWin3DPickable);
        }
        sprintf(Name, "RepairTextArea%d", Index);
        {
            Vector const Position = {0.02f, RowY, 0.0f};
            Vector const Half = {0.171f, 0.014f, 0.0f};
            TextAreas[Index] = CreateWireButtonControl(Name, kSelectSound, &Position, &Half, 0, 0, 0, 0,
                                                       0);
        }
        sprintf(Name, "RepairItemName%d", Index);
        {
            Vector const Position = {-0.01f, RowY, 0.0f};
            ItemNames[Index] = CreateTextControl(Name, "ItemName", &Position, nullptr, 0.27f, 0.03f,
                                                 1.2f, 0x3f6, 0, 0, 0);
        }
        sprintf(Name, "RepairItemPrice%d", Index);
        {
            Vector const Position = {0.136f, RowY, 0.0f};
            ItemPrices[Index] = CreateTextControl(Name, "ItemName", &Position, nullptr, 0.105f, 0.03f,
                                                  1.2f, 0x40e, 2, 0, 0);
        }
        sprintf(Name, "RepairItemDamage%d", Index);
        {
            Vector const Position = {-0.176f, DamageY, 0.001f};
            Vector const Half = {0.014f, 0.002f, 0.0f};
            ItemDamages[Index] = CreateCargoBarControl(Name, "", &Position, nullptr, &Half, 1, 0x43c,
                                                       G_NnPaletteGreen,
                                                       G_NnPaletteGreen);
            ItemDamages[Index]->SetProperty(kWinPropRenderFlags, kRenderArgNoWireRender, 0);
        }
    }

    {
        Vector const Position = {0.19f, 0.14f, -1.0f};
        Scroll = CreateUIScroll("RepairScroll", &Position, 0.018f, 0.23f, 0, 5, 9, 0);
    }
    {
        Vector const Position = {0.01f, 0.233f, 0.0f};
        CreateTextControl("RepairList", "PageHeader", &Position, nullptr, 0.483f, 0.03f, 1.7f, 0x4a8,
                          1, 0, 0);
    }
    {
        Vector const Position = {-0.215f, -0.13f, 0.0f};
        Vector const Half = {0.02f, 0.02f, 0.0f};
        ToggleChecks = CreateButtonControl("RepairToggleChecks", kCheckToggleMesh, nullptr,
                                           kSelectSound, nullptr, &Position, nullptr, &Half, 0, 0, 0,
                                           0, 0x439, 0, 0);
    }
    {
        Vector const Position = {0.005f, 0.192f, 0.0f};
        PlayerCash = CreateTextControl("RepairPlayerCash", "Normal", &Position, nullptr, 0.488f, 0.03f,
                                       1.4f, 0, 1, 0, 0);
    }
    {
        Vector const Position = {-0.067f, -0.13f, 0.0f};
        CreateTextControl("RepairTotalCostText", "Normal", &Position, nullptr, 0.15f, 0.03f, 1.2f,
                          0x4a9, 0, 0, 0);
    }
    {
        Vector const Position = {0.12f, -0.13f, 0.0f};
        TotalCost = CreateTextControl("RepairTotalCost", "Normal", &Position, nullptr, 0.15f, 0.03f,
                                      1.2f, 0, 2, 0, 0);
    }
    {
        Vector const Position = {0.005f, -0.195f, 0.0f};
        NotEnoughCash = CreateTextControl("RepairNotEnoughCash", "Normal", &Position, nullptr, 0.488f,
                                          0.03f, 1.2f, 0x4aa, 1, 0, 0);
        NotEnoughCash->SetProperty(kWinPropHide, 1, 0);
    }

    ShowPlayerCash();
    PushCancelHandler(0); // FUN_00576360
    return Win3D::Create(NewParent, Data);      // FUN_004b5060
}

intptr_t NnRepair::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004b3530 (RET 0xc): byte table 0x004b397c over Msg - 0x26. Every path returns 0.
    switch (Msg)
    {
    case kWinNotifyHighlight:
    case kWinNotifyUnhighlight:
    {
        int Row = 0;
        for (; Row < 9; ++Row)
        {
            if (A == PointerArg(CheckBoxFrames[Row]) || A == PointerArg(IconFrames[Row]) ||
                A == PointerArg(TextAreas[Row]) || A == PointerArg(CheckBoxes[Row]))
                break;
        }
        if (Row == 9)
            return 0;
        if (Msg == kWinNotifyHighlight)
            Hovered = Row;
        else if (Hovered == Row)
            Hovered = -1;
        // The row's other controls get the same message.
        if (A != PointerArg(CheckBoxFrames[Row]))
            CheckBoxFrames[Row]->SetProperty(Msg, 0, 0);
        if (A != PointerArg(CheckBoxes[Row]))
            CheckBoxes[Row]->SetProperty(Msg, 0, 0);
        if (A != PointerArg(IconFrames[Row]))
            IconFrames[Row]->SetProperty(Msg, 0, 0);
        if (A != PointerArg(TextAreas[Row]))
            TextAreas[Row]->SetProperty(Msg, 0, 0);
        return 0;
    }
    case kWinPropCloseFromOutside:
    {
        // Closed from outside: no NavBar.
        Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));
        Win3DReparent(this, nullptr); // FUN_0059dd00
        Win3DDeleteTree(this);
        return 0;
    }
    case kWinNotifyToggled:
    {
        // A check box changed; B == 0 checks its item. Every row is tested (no break); an item
        // index past the list is ignored (unsigned compare).
        for (int Row = 0; Row < 9; ++Row)
        {
            if (A != PointerArg(CheckBoxes[Row]))
                continue;
            unsigned const Index = static_cast<unsigned>(ScrollOffset + Row);
            if (Index < Items.size())
                Items[Index].bSelected = (B == 0) ? 1 : 0;
        }
        ShowTotalCost();
        return 0;
    }
    case kWinNotifyScrollValue:
    {
        if (A != PointerArg(Scroll))
            return 0;
        ScrollOffset = static_cast<int>(B);
        RefreshRows();
        return 0;
    }
    case kWinPropPlaceAtOffset:
    {
        float const* const Target = reinterpret_cast<float const*>(A);
        SetOffset(Target[0], Target[1], Target[2]);
        SetPosition(&Position);
        PlayOrRewindUiSoundSlot(kUiSoundWindowOpen); // FUN_005645c0
        return 0;
    }
    case kWinNotifyClick:
    {
        // An icon frame or a text area toggles its row's check box: property 0x30 flips on its
        // argument, so writing the state read back is the toggle.
        for (int Row = 0; Row < 9; ++Row)
        {
            if (A != PointerArg(IconFrames[Row]) && A != PointerArg(TextAreas[Row]))
                continue;
            int State = 0;
            CheckBoxes[Row]->SetProperty(kTogglePropGetState, PointerArg(&State), 0);
            CheckBoxes[Row]->SetProperty(kTogglePropSetState, static_cast<uintptr_t>(State), 0);
            SetProperty(kWinNotifyToggled, PointerArg(CheckBoxes[Row]), (State != 0) ? 0u : 1u);
            return 0;
        }
        if (A == PointerArg(OkButton))
        {
            DoRepair(); // FUN_004b40d0
            NnVoiceEnqueue(G_NnVoiceRepairCompleteId, 0); // FUN_00489e80
            return 0;
        }
        if (A == PointerArg(CancelButton))
        {
            Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));
            Win3DReparent(this, nullptr); // FUN_0059dd00
            NavBarCloseService(false);    // FUN_004424f0(0)
            Win3DDeleteTree(this);
            return 0;
        }
        if (A == PointerArg(ToggleChecks))
        {
            for (size_t Index = 0; Index < Items.size(); ++Index)
                Items[Index].bSelected = (Items[Index].bSelected == 0) ? 1 : 0;
            RefreshRows();
            ShowTotalCost();
        }
        return 0;
    }
    default:
        return 0;
    }
}

void NnRepair::PopulateList()
{
    // FUN_004b39b0.
    Items.clear();

    // The base's repair scale (BaseData +0x0c). The FPU keeps the product at extended
    // precision; the port's double product truncates the same way for every realistic
    // hit-point and scale value.
    float const RepairScale = GetClientCurrentLocationBaseData()->ShipRepairCost;
    float const Health = GetPlayerHealthScaleRetail();
    Archetype::Ship const* Ship = nullptr;
    int HullCost = 0;
    unsigned const ShipArchId = GetPlayerShipArchetypeIdRetail();
    if (ShipArchId != 0)
    {
        Ship = Archetype::GetShip(ShipArchId);
        if (Ship != nullptr)
            HullCost = static_cast<int>((1.0 - static_cast<double>(Health)) *
                                        static_cast<double>(Ship->HitPts) *
                                        static_cast<double>(RepairScale));
    }

    // A damaged hull is listed with at least 1 (a negative cost is dropped); an undamaged one
    // only for a positive cost.
    bool bAddHull = false;
    if (Health != 1.0f)
    {
        if (HullCost == 0)
        {
            HullCost = 1;
            bAddHull = true;
        }
        else
        {
            bAddHull = HullCost > 0;
        }
    }
    else
    {
        bAddHull = HullCost > 0;
    }
    if (bAddHull)
    {
        FRepairItem Item;
        memset(&Item, 0, sizeof(Item));
        Item.Kind = kRepairItemHull;
        Item.Id = 0;
        Item.NameIds = 0x428;
        Item.Cost = HullCost;
        Item.HealthPercent = static_cast<int>(static_cast<double>(Health) * 100.0);
        Item.IconMesh = nullptr;
        Item.IconMaterial = nullptr;
        Item.bSelected = 1;
        Items.push_back(Item); // FUN_004b49a0(end, &Item)
    }

    // Every collision group whose health is not exactly 1.0, cost at least 1. Retail: the
    // get_group_by_id result is not null-tested (0x004b39b0).
    std::list<CollisionGroupDesc> const& Groups = GetPlayerCollisionGroupListRetail();
    for (std::list<CollisionGroupDesc>::const_iterator It = Groups.begin(); It != Groups.end(); ++It)
    {
        CollisionGroupDesc const& Desc = *It;
        if (IsOneBits(Desc.Health))
            continue;
        int NameIds = 0x429;
        int Cost = 0;
        if (Ship != nullptr)
        {
            Archetype::CollisionGroup const* const Group = Ship->get_group_by_id(Desc.Id);
            NameIds = static_cast<int>(HudCollisionGroupTypeIds(Group->Type)); // FUN_0045a7c0
            Cost = static_cast<int>((1.0 - static_cast<double>(Desc.Health)) *
                                    static_cast<double>(Group->HitPts) *
                                    static_cast<double>(RepairScale));
        }
        if (Cost == 0)
            Cost = 1;

        FRepairItem Item;
        memset(&Item, 0, sizeof(Item));
        Item.Kind = kRepairItemCollisionGroup;
        Item.Id = Desc.Id;
        Item.NameIds = NameIds;
        Item.Cost = Cost;
        Item.HealthPercent = static_cast<int>(static_cast<double>(Desc.Health) * 100.0);
        Item.IconMesh = nullptr;
        Item.IconMaterial = nullptr;
        Item.bSelected = 1;
        Items.push_back(Item); // FUN_004b49a0(end, &Item)
    }

    // A copy of the player's equipment, priced at the market of the base the player is in.
    EquipDescList const Equipment(GetPlayerEquipDescListRetail());
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    for (std::list<EquipDesc>::const_iterator It = Equipment.Items.begin(); It != Equipment.Items.end();
         ++It)
    {
        EquipDesc const& Desc = *It;

        // Items of hp class 1 are never repaired.
        Archetype::Equipment const* Weapon = nullptr;
        Archetype::Equipment const* Shield = nullptr;
        if (ShipViewEquipmentHpClass(Desc.ArchId, &Weapon, &Shield) == 1) // FUN_005835f0
            continue;

        // 30% (DAT_005d51b4) of the market price, scaled by the damage and the count. Both
        // prices go back to the FPU as unsigned qwords; the cost is stored as float.
        unsigned const GoodId = Arch2Good(Desc.ArchId);
        int const Price1 = static_cast<int>(ClientMarketGetPrice(Market, GoodId)); // FUN_00438060
        int const Price2 = static_cast<int>(
            static_cast<double>(static_cast<uint32_t>(Price1)) * static_cast<double>(0.3f));
        float const CostF = static_cast<float>(
            static_cast<double>(static_cast<uint32_t>(Price2)) *
            (1.0 - static_cast<double>(Desc.Health)) * static_cast<double>(Desc.Count));

        // An unknown archetype, one without a name or an undamaged item is skipped.
        Archetype::Equipment const* const Arch = Archetype::GetEquipment(Desc.ArchId);
        if (Arch == nullptr)
            continue;
        int const IdsName = Arch->IdsName;
        if (IdsName == 0)
            continue;
        if (Desc.Health == 1.0f)
            continue;

        // The icon: the good's item icon; else its shop model with the companion's first
        // material; without a good the archetype's model with that material.
        char const* Mesh = nullptr;
        char const* Material = nullptr;
        GoodInfo const* const Good = GoodList::find_by_id(GoodId);
        if (Good != nullptr)
        {
            Mesh = Good->ItemIcon.Value; // +0x78
            if (Mesh == nullptr)
            {
                Mesh = Good->ShopArchetype.Value; // +0x74
                Material = FirstMaterialPath(Arch);
            }
        }
        else
        {
            Mesh = Arch->DaArchetype.Value; // +0x0c
            Material = FirstMaterialPath(Arch);
        }

        int Cost = static_cast<int>(CostF);
        if (Cost == 0)
            Cost = 1;
        FRepairItem Item;
        memset(&Item, 0, sizeof(Item));
        Item.Kind = kRepairItemEquipment;
        Item.Id = Desc.Id;
        Item.NameIds = IdsName;
        Item.Cost = Cost;
        Item.HealthPercent = static_cast<int>(static_cast<double>(Desc.Health) * 100.0);
        Item.IconMesh = Mesh;
        Item.IconMaterial = Material;
        Item.bSelected = 1;
        Items.insert(Items.end(), 1, Item); // FUN_004b4ba0(end, 1, &Item)
    }

    // -1 for an empty list, as in retail.
    Scroll->SetProperty(kScrollPropSetRange, 0, static_cast<uintptr_t>(static_cast<int>(Items.size()) - 1));
}

void NnRepair::RefreshRows()
{
    // FUN_004b3dd0.
    wchar_t Buffer[0x100];
    int Row = 0;
    for (int Index = ScrollOffset; static_cast<unsigned>(Index) < Items.size(); ++Index)
    {
        FRepairItem const& Item = Items[static_cast<unsigned>(Index)];

        ItemNames[Row]->SetProperty(kWinPropDisable, 0, 0);
        ItemPrices[Row]->SetProperty(kWinPropDisable, 0, 0);
        CheckBoxes[Row]->SetProperty(kWinPropDisable, 0, 0);
        CheckBoxFrames[Row]->SetProperty(kWinPropDisable, 0, 0);
        IconFrames[Row]->SetProperty(kWinPropDisable, 0, 0);
        SmallIcons[Row]->SetProperty(kWinPropHide, 0, 0);
        TextAreas[Row]->SetProperty(kWinPropDisable, 0, 0);
        ItemDamages[Row]->SetProperty(kWinPropHide, 0, 0);

        ItemNames[Row]->SetProperty(kTextPropSetIds, static_cast<uintptr_t>(Item.NameIds), 0);
        swprintf(Buffer, 0x100, kCountFormat, Item.Cost);
        ItemPrices[Row]->SetProperty(kTextPropSetText, PointerArg(Buffer), 0);
        CheckBoxes[Row]->SetProperty(kTogglePropSetState, Item.bSelected, 0);

        if (Item.IconMesh == nullptr)
            SmallIcons[Row]->SetProperty(kWinPropMesh, PointerArg(kBlackBoxMesh), 0);
        else
            SmallIcons[Row]->SetProperty(kWinPropMesh, PointerArg(Item.IconMesh), PointerArg(Item.IconMaterial));

        int const Percent = Item.HealthPercent;
        ItemDamages[Row]->SetProperty(kBarPropFill, 100, static_cast<uintptr_t>(Percent));
        unsigned Colour = 0;
        if (Percent >= 0x46)
            Colour = 0xff30c24bu; // bytes 4b c2 30 ff
        else if (Percent >= 0x1e)
            Colour = 0xff26eaf5u; // bytes f5 ea 26 ff
        else
            Colour = 0xff2626ffu; // bytes ff 26 26 ff
        ItemDamages[Row]->SetProperty(kBarPropColour, Colour, 0);

        ++Row;
        if (Row >= 9)
            return;
    }

    // The rows past the list.
    for (; Row < 9; ++Row)
    {
        ItemNames[Row]->SetProperty(kTextPropSetIds, 0, 0);
        ItemNames[Row]->SetProperty(kWinPropDisable, 1, 0);
        ItemPrices[Row]->SetProperty(kTextPropSetIds, 0, 0);
        ItemPrices[Row]->SetProperty(kWinPropDisable, 1, 0);
        CheckBoxes[Row]->SetProperty(kWinPropDisable, 1, 0);
        CheckBoxFrames[Row]->SetProperty(kWinPropDisable, 1, 0);
        IconFrames[Row]->SetProperty(kWinPropDisable, 1, 0);
        TextAreas[Row]->SetProperty(kWinPropDisable, 1, 0);
        ItemDamages[Row]->SetProperty(kWinPropHide, 1, 0);
        CheckBoxes[Row]->SetProperty(kTogglePropSetState, 0, 0);
        SmallIcons[Row]->SetProperty(kWinPropHide, 1, 0);
    }
}

void NnRepair::DoRepair()
{
    // FUN_004b40d0. Signed compare.
    int const Total = SelectedTotal();
    if (static_cast<int>(GetPlayerCreditsRetail()) < Total)
        return;

    ReqPlayerSpendCashRetail(Total); // FUN_004c4d50
    std::list<CollisionGroupDesc> Groups(GetPlayerCollisionGroupListRetail());
    EquipDescList Equipment(GetPlayerEquipDescListRetail());

    // The hull goes through the server at once; every group / item of the item's id is set to
    // 1.0 in the copies (no break).
    bool bGroupsChanged = false;
    bool bEquipmentChanged = false;
    for (size_t Index = 0; Index < Items.size(); ++Index)
    {
        FRepairItem const& Item = Items[Index];
        if (Item.bSelected == 0)
            continue;
        switch (Item.Kind)
        {
        case kRepairItemHull:
            ReqPlayerHullStatusRetail(1.0f); // FUN_004c4f50
            break;
        case kRepairItemCollisionGroup:
            for (std::list<CollisionGroupDesc>::iterator It = Groups.begin(); It != Groups.end(); ++It)
            {
                if (It->Id == Item.Id)
                {
                    It->Health = 1.0f;
                    bGroupsChanged = true;
                }
            }
            break;
        case kRepairItemEquipment:
            for (std::list<EquipDesc>::iterator It = Equipment.Items.begin();
                 It != Equipment.Items.end(); ++It)
            {
                if (It->Id == Item.Id)
                {
                    It->set_status(1.0f);
                    bEquipmentChanged = true;
                }
            }
            break;
        default:
            break;
        }
    }

    if (bGroupsChanged)
        ReqPlayerCollisionGroupsRetail(&Groups); // FUN_004c3f10
    if (bEquipmentChanged)
        ReqPlayerEquipmentRetail(&Equipment);    // FUN_004c4040
    ShowPlayerCash();
}

void NnRepair::Draw()
{
    // FUN_004b43b0.
    if (HudIsMousePickBlocked()) // FUN_0045a740
        return;

    // With something checked, "not enough cash" shows exactly when the total is over the
    // credits (signed), and the OK button and caption when it is not.
    int const Total = SelectedTotal();
    if (Total == 0)
    {
        NotEnoughCash->SetProperty(kWinPropHide, 1, 0);
        OkButton->SetProperty(kWinPropHide, 1, 0);
        RepairText->SetProperty(kWinPropHide, 1, 0);
    }
    else
    {
        bool const bOver = Total > static_cast<int>(GetPlayerCreditsRetail());
        NotEnoughCash->SetProperty(kWinPropHide, bOver ? 0u : 1u, 0);
        OkButton->SetProperty(kWinPropHide, bOver ? 1u : 0u, 0);
        RepairText->SetProperty(kWinPropHide, bOver ? 1u : 0u, 0);
    }

    // The hovered row's frames draw at 0x65.
    for (Win3D* Child = ChildHead; Child != nullptr; Child = Child->NextSibling)
    {
        Child->DrawLayer = 0x64;
        if (Hovered < 0)
            continue;
        WinControl* const Control = Child->AsWinControl(); // slot 2
        if (Control == CheckBoxFrames[Hovered] || Control == IconFrames[Hovered] ||
            Control == TextAreas[Hovered])
            Child->DrawLayer = 0x65;
    }

    WireControlsJoinColumn(IconFrames, 9);     // FUN_005a4b50
    WireControlsJoinColumn(TextAreas, 9);
    WireControlsJoinColumn(CheckBoxFrames, 9);
    WinControl::Draw(); // FUN_005a0760 (tail jump)
}

bool NnRepair::PlayerNeedsRepair()
{
    // FUN_004b4510.
    if (GetPlayerHealthScaleRetail() != 1.0f)
        return true;

    std::list<CollisionGroupDesc> const& Groups = GetPlayerCollisionGroupListRetail();
    for (std::list<CollisionGroupDesc>::const_iterator It = Groups.begin(); It != Groups.end(); ++It)
    {
        if (!IsOneBits(It->Health))
            return true;
    }

    // In a copy of the equipment, a damaged item of any hp class but 1.
    EquipDescList const Equipment(GetPlayerEquipDescListRetail());
    for (std::list<EquipDesc>::const_iterator It = Equipment.Items.begin(); It != Equipment.Items.end();
         ++It)
    {
        if (It->Health == 1.0f)
            continue;
        Archetype::Equipment const* Weapon = nullptr;
        Archetype::Equipment const* Shield = nullptr;
        if (ShipViewEquipmentHpClass(It->ArchId, &Weapon, &Shield) != 1) // FUN_005835f0
            return true;
    }
    return false;
}

void NnRepair::Update()
{
    // LAB_004b4670. Out of the base the window takes itself apart (no NavBar; retail tests
    // `this` first).
    if (GetClientLocationCurrentBase() == 0)
    {
        Win3DDeleteTree(this);
        return;
    }

    // Rebuild only when the equipment / collision revisions or the hull health changed.
    Win3D::Update(); // FUN_005a17b0
    if (EquipRevision == GetPlayerEquipRevisionRetail() &&
        CollisionRevision == GetPlayerCollisionRevisionRetail() &&
        GetPlayerHealthScaleRetail() == HealthScale)
        return;

    EquipRevision = GetPlayerEquipRevisionRetail();
    CollisionRevision = GetPlayerCollisionRevisionRetail();
    HealthScale = GetPlayerHealthScaleRetail();
    PopulateList();
    RefreshRows();
    ShowTotalCost();
    ShowPlayerCash();
}

bool NnRepair::OnEvent(unsigned MsgId)
{
    // LAB_004b47c0 (RET 4), on the +0x32c face: USER_STACKABLE_CANCEL (0x10) closes the window like the
    // cancel button. (Retail tests the adjusted `this` for null.)
    if (MsgId != USER_STACKABLE_CANCEL)
        return false;
    Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));
    Win3DReparent(this, nullptr); // FUN_0059dd00
    NavBarCloseService(false);    // FUN_004424f0(0)
    Win3DDeleteTree(this);
    return true;
}
