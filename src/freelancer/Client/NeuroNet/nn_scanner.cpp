// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_Scanner -- see nn_scanner.h for the class, its vftables and layout.

#include "NeuroNet/nn_scanner.h"

#include "Core/globals.h"         // IsClientServerTickPaused (FUN_0042d730)
#include "Space/HUD/hud_target.h"      // HudIsMousePickBlocked (FUN_0045a740)
#include "Interface/menu_ui_init.h"    // PlayOrRewindUiSoundSlot (FUN_005645c0)
#include "NeuroNet/nn_modal.h"        // NnMainButtonOnModalOpen, NnMainButtonHasActivePanel, G_bNnScreenUp
#include "Space/player_behavior.h" // GetPlayerObjRW (FUN_0054baf0)
#include "Space/space_letterbox.h" // IsLetterboxVisible (FUN_0041a3e0)
#include "Interface/win3d.h"           // Win3DFindByName (FUN_0059da10)
#include "Interface/win_factory.h"

// Retail WINFACTORY<NN_Scanner> DAT_00672294 "NN_Scanner" (static init LAB_004b6b30 -> ctor
// FUN_004b7200; atexit LAB_004b6b50 -> FUN_00579380; Create FUN_004b7270).
WINFACTORY<NnScanner> G_NnScannerFactory("NN_Scanner");

namespace
{
    // Port-only helper for the gate FUN_004b6b60 (0x004b6b69) and Update (0x004b6e36) inline.
    bool NnScannerMayStand()
    {
        if (GetPlayerObjRW() == nullptr)
            return false;
        if (Win3DFindByName("NeuroNetMissionMessage") != nullptr)
            return false;
        if (Win3DFindByName("NN_Trade") != nullptr)
            return false;
        if (NnMainButtonHasActivePanel())
            return false;
        if (G_bNnScreenUp)
            return false;
        if (IsClientServerTickPaused())
            return false;
        if (IsLetterboxVisible())
            return false;
        return true;
    }
}

NnScanner::NnScanner()
    : NnInventory()
{
    // Inlined into FUN_004b7270 (0x004b72af).
    ScannedCargo.Items.clear();
}

NnScanner::~NnScanner()
{
    // FUN_004b6c80; the member and base destructors do all of it.
}

bool NnScanner::Create(Win3D* NewParent, void* Data)
{
    // FUN_004b6d40 (RET 8).
    bool const bOk = NnInventory::Create(NewParent, Data);
    if (bOk)
    {
        BindControls();

        if (PlayerTitle != nullptr)
            PlayerTitle->SetProperty(0x1e, 0xbcb, 0);

        // Hide the controls a scan does not use; the full cargo bar pointer is dropped.
        if (JettisonButton != nullptr)
            JettisonButton->SetProperty(1, 1, 0);
        if (PlayerCash != nullptr)
            PlayerCash->SetProperty(1, 1, 0);
        if (Ship.Toggle != nullptr)
            Ship.Toggle->SetProperty(1, 1, 0);
        if (CargoLabel != nullptr)
            CargoLabel->SetProperty(1, 1, 0);
        if (FullCargoBar != nullptr)
        {
            FullCargoBar->SetProperty(1, 1, 0);
            FullCargoBar = nullptr;
        }

        SetMode(4, true); // FUN_0047f240, the commodity filter
    }

    // FUN_005760d0 on the +0x974 face, whatever Create returned.
    RegisterEvent(0xf);
    return bOk;
}

void NnScanner::Update()
{
    // LAB_004b6e30. ProcessMessage(0xef02) deletes this.
    if (!NnScannerMayStand())
    {
        ProcessMessage(0xef02, 0, 0);
        return;
    }

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

    // One FUN_0047c740 row per scanned item; the first that answers true is the selection.
    int Counter = 0;
    EquipDesc const* Selected = nullptr;
    for (std::list<EquipDesc>::const_iterator It = ScannedCargo.Items.begin();
         It != ScannedCargo.Items.end(); ++It)
    {
        EquipDesc const& Desc = *It;
        bool const bIsSelected = FillItemRow(&Counter, Desc.ArchId, GetItemFilterMode(&Desc),
                                             Desc.Count, 0, 0, 0, 0, 0, 1, 0, 0, 0);
        if (Selected == nullptr && bIsSelected)
            Selected = &Desc;
    }

    // FUN_0047ba90: the scanned ship, else the selected item's archetype, or 0.
    unsigned InfoArchId = 0;
    if (bShowShipInfoCard != 0)
        InfoArchId = Ship.ShipArchId;
    else if (Selected != nullptr)
        InfoArchId = Selected->ArchId;
    ShowInfoCard(InfoArchId, 0);

    UpdateRowVisibility(Counter); // FUN_0047f3e0

    // Not ported: retail then calls 0x0053e4a0 on +0x330, a bare `RET 4` (ICF-named
    // IDPMsgHandler::OnAddHost).

    if (Counter != LastRowCount)
    {
        SelectedIndex = -1;
        if (InventoryScroll != nullptr)
            InventoryScroll->SetProperty(0x37, 0, static_cast<uintptr_t>(Counter - 1));

        // Pull the first row back so the page ends at the list's end (unsigned compares).
        if (FirstRow != 0
            && static_cast<unsigned>(FirstRow + kNnInventoryRows) > static_cast<unsigned>(Counter))
        {
            if (static_cast<unsigned>(Counter) > static_cast<unsigned>(kNnInventoryRows))
                FirstRow = Counter - kNnInventoryRows;
            else
                FirstRow = 0;
            if (InventoryScroll != nullptr)
                InventoryScroll->SetProperty(0x38, static_cast<uintptr_t>(FirstRow * 10), 0);
        }

        LastRowCount = Counter;
        Update(); // slot 11 again, recursively
    }

    // The damage bars hidden (no null test).
    for (int Row = 0; Row < kNnInventoryRows; ++Row)
        ItemDamage[Row]->Flags = static_cast<unsigned char>(ItemDamage[Row]->Flags & 0xfc);

    if (SelectedIndex == -1)
        bShowShipInfoCard = 1;
}

bool NnScanner::ProcessMessage(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_004b7180 (RET 0xc).
    if (Msg == 0xef02)
    {
        PlayOrRewindUiSoundSlot(0xe);
        Close();
        return true;
    }
    return NnInventory::ProcessMessage(Msg, A, B);
}

intptr_t NnScanner::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004b7050 (RET 0xc).
    if (Msg == 0x26)
    {
        if (A == reinterpret_cast<uintptr_t>(CloseButton))
        {
            ProcessMessage(0xef02, 0, 0);
            return 1;
        }
    }
    else if (Msg == 0x49)
    {
        // The clear runs first, so A == &ScannedCargo leaves the list empty, as in retail.
        Ship.ShipArchId = static_cast<unsigned>(B);
        ScannedCargo.Items.clear();
        EquipDescList const* const Source = reinterpret_cast<EquipDescList const*>(A);
        if (Source != &ScannedCargo)
            ScannedCargo.Items = Source->Items;
        Update();
    }
    else if (Msg == 0x4a)
    {
        Flags = static_cast<unsigned char>(Flags | 3);
    }

    return NnInventory::SetProperty(Msg, A, B);
}

bool NnScanner::IsScanner()
{
    // FUN_004b6c50.
    return true;
}

bool NnScanner::OnEvent(unsigned MsgId)
{
    // FUN_004b71c0 (RET 4).
    if (HudIsMousePickBlocked())
        return false;
    if (MsgId != 0xf)
        return false;
    PlayOrRewindUiSoundSlot(0xe);
    Close(); // FUN_0047ad20
    return true;
}

void OpenNnScanner(EquipDescList const* Cargo, unsigned ShipArchId)
{
    // FUN_004b6b60 (cdecl).
    NnMainButtonOnModalOpen(); // FUN_0045a460, before any test

    if (!NnScannerMayStand())
        return;

    Win3D* const Created = WinFactoryCreate("NN_Scanner", "NN_Scanner", nullptr, nullptr);
    if (Created == nullptr)
        return;
    WinControl* const Window = Created->AsWinControl();
    if (Window == nullptr)
        return;

    // {0, 0.076, -1.288}.
    float const Position[3] = {0.0f, NnFloatBits(0x3d9ba5e3), NnFloatBits(0xbfa4dd2f)};
    Window->SetPosition(Position);

    Window->SetProperty(0x49, reinterpret_cast<uintptr_t>(Cargo), ShipArchId);
    Window->SetProperty(0x4a, 0, 0);

    PlayOrRewindUiSoundSlot(0xd);
}
