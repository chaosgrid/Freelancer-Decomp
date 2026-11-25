// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_Dealer -- see nn_dealer.h for the class, its vftable and layout.

#include "NeuroNet/nn_dealer.h"

#include "NeuroNet/Economy/client_base_market.h"     // GetClientLocationBaseMarket (FUN_0043a510), the market
                                    // queries FUN_00438060 / 00438160 / 004381c0 / 00438220
#include "Interface/Pages/character_select_equip.h" // GetPlayerStarSystemIdRetail (FUN_004c4830),
                                    // GetPlayerBaseIdRetail (DAT_00673358 via FUN_004c4900),
                                    // GetPlayerRepVibeRetail (FUN_004c4670)
#include "Space/HUD/hud_command_dispatch.h"   // HudPostCommand (FUN_005763b0)
#include "Space/HUD/hud_weapon_groups.h"      // G_WeaponGroups (DAT_00674c0c)
#include "Common/Fonts/ids_strings.h"            // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Network/login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "Interface/menu_ui_widgets.h"        // HudMessageQueueRequestRankRefresh (FUN_004c55f0)
#include "GameFlow/menu_universe_base.h"     // GetClientLocationCurrentBase (DAT_0066873c)
#include "Common/misc_util.h"              // FormatGroupedNumber (FUN_004779a0)
#include "GameFlow/nav_bar.h"                // NavBarOpenButton (FUN_00442200), NavBarRefreshRepairTab (FUN_004427e0)
#include "NeuroNet/nn_messaging.h"           // NnVoiceEnqueue (FUN_00489e80)
#include "NeuroNet/nn_modal.h"               // NnFloatBits
#include "NeuroNet/nn_text_scratch.h"        // DAT_0066dc60 / DAT_0066fc60 / DAT_006119f8
#include "NeuroNet/nn_text_control.h"        // NnTextControl::GetFontString (Font::String +0x334)
#include "NeuroNet/nn_ui_pulse.h"            // G_NnPaletteGreen (DAT_00679bac)
#include "Space/player_spawn.h"           // GetPlayerConnectionId (DAT_00673344)
#include "Interface/rich_text_win.h"
#include "Interface/ship_view_control.h"
#include "Interface/ui_scroll.h"
#include "Interface/win3d.h"                  // Win3DFindByName (FUN_0059da10)
#include "Interface/win_factory.h"
#include "Interface/wire_controls.h"          // WireStaticControl::SoundSlot (+0x380)

#include "common_archetype.h"       // Archetype::Gun / Munition (the equipment sort)
#include "common_goods.h"           // Good2Arch / Arch2Good / GoodList
#include "common_ini.h"             // StringAlloc
#include "common_reputation.h"
#include "common_server.h"
#include "common_misc.h"            // SinglePlayer
#include "common_tstring.h"
#include "common_universe.h"

#include <list>
#include <stdio.h>
#include <vector>
#include <wchar.h>
#include <windows.h> // CharUpperW (FUN_005b74d9)

// Retail WINFACTORY<NN_Dealer> "NN_Dealer" (static init FUN_004857f0, vftable 0x005d1d28,
// Create FUN_00485860).
WINFACTORY<NnDealer> G_NnDealerFactory("NN_Dealer");

NnDealer::NnDealer()
    : NnInventory()
{
    // FUN_0047fbe0 (NN_Inventory's ctor inlined); FUN_00477ee0 runs a second time.
    ResetFields();

    // The control pointers +0x994..+0x9f0, +0x97c and +0x9fc are not
    // written here in retail (operator_new memory); the port starts them at zero.
    ShipPackageGoodId = 0;
    WelcomeText = nullptr;
    WelcomeLine = nullptr;
    WelcomeHelp = nullptr;
    BuyText = nullptr;
    BuyArrow = nullptr;
    SellText = nullptr;
    SellArrow = nullptr;
    BuySellTradeButton = nullptr;
    QuantityPrompt = nullptr;
    QuantitySlider = nullptr;
    QuantityText = nullptr;
    QuantityPrice = nullptr;
    QuantityTotal = nullptr;
    QuantityTotalText = nullptr;
    QuantityEdit = nullptr;
    TransferItemQuantity = nullptr;
    ErrorHelp = nullptr;
    PlusMinus = nullptr;
    Field9dc = 0;
    ContinueButton = nullptr;
    ContinueText = nullptr;
    ShipBuyProblem = nullptr;
    BackToShipDealer = nullptr;
    ShipDealerShipPrice = nullptr;
    PlayerShipTradeInValue = 0;

    TradeMode = kTradeModeCommodity;      // +0x978
    bDealerMode = 1;    // +0x35c
    Inventory = nullptr; // +0x974
    Field980 = 1;
    Field984 = 0;
    Quantity = 1;
    bTransferPanelHidden = 1;
    bPreviewingShip = 0;
    ShipHullPrice = 0;
    ShownInfoArchId = 0;

    BaseFeeling = GetBaseFactionFeeling(); // FUN_00483df0

    // Single player asks for the rank level (FUN_004c55f0), multiplayer
    // for the player stats (IServer slot 73, +0x124) with a zeroed 4-byte block.
    if (SinglePlayer())
    {
        HudMessageQueueRequestRankRefresh();
        return;
    }
    unsigned char Data[4] = {0, 0, 0, 0};
    G_Server->RequestPlayerStats(GetPlayerConnectionId(), Data, 4);
}

// The TextControl idiom retail inlines after several creations: the label's Font::String (+0x334) goes
// multi-line and drops its layout, unless it already is. The factory's result is not tested.
static void NnDealerMakeMultiLine(WinControl* Control)
{
    FFontString* const Text = static_cast<NnTextControl*>(Control)->GetFontString();
    if (Text->bMultiLine != 1)
    {
        Text->bMultiLine = 1; // +0x30
        Text->LayoutGen = 0;  // +0x28
    }
}

void NnDealer::CreateControls()
{
    // FUN_00480d10 -- every position is absolute (the dealer half of the screen); where x87 keeps
    // an intermediate at extended precision across two operations, the port carries it in
    // double. The results of the row controls are not kept: NN_Inventory's BindControls finds
    // them by name.
    float PriceColumn = 0.025f; // [ESP+0x2c] (0x3ccccccd)
    if (TradeMode == kTradeModeCommodity)         // +0x978
        PriceColumn = 0.0f;

    // "PlayerTitle"; the result is not kept.
    {
        float const TitlePosition[3] = {NnFloatBits(0x3f870a3e), -0.039f, 0.0f}; // 1.055
        CreateTextControl("PlayerTitle", "PageHeader", TitlePosition, nullptr, 0.394f, 0.03f,
                          1.65f, 0x2143, 1, 0, 0); // FUN_005a0e30
    }

    // The price column's x.
    float const PriceX = PriceColumn + NnFloatBits(0x3f908313); // DAT_005d1cec, 1.129 (not 1.129f)

    // The five item rows. Names count from 1, the row offset from 0.
    char Name[0x100];
    for (int Row = 0; Row < 5; ++Row)
    {
        int const Number = Row + 1;

        // "ItemButton%d". The product is exact in double; x87 subtracts the
        // double 0.131 (DAT_005d1538) from it before the float stores.
        sprintf(Name, "ItemButton%d", Number);
        double const RowOffset = static_cast<double>(Row) * static_cast<double>(-0.068f); // DAT_005d1540
        float const RowY = static_cast<float>(RowOffset);
        float const ButtonY = static_cast<float>(RowOffset - 0.131); // [ESP+0x3c] / [ESP+0x70]
        {
            float const ButtonPosition[3] = {0.903f, ButtonY, 0.0f};
            float const ButtonHalfExtents[3] = {0.035f, 0.0335f, 0.0f};
            CreateWireButtonControl(Name, "ui_item_select", ButtonPosition, ButtonHalfExtents, 0,
                                    0, 0, 0, 0); // FUN_005a0d00
        }

        sprintf(Name, "ItemView0%d", Number);
        {
            float const ViewPosition[3] = {1.084f, ButtonY, 0.0f};
            float const ViewHalfExtents[3] = {0.138f, 0.034f, 0.0f};
            CreateWireButtonControl(Name, "ui_item_select", ViewPosition, ViewHalfExtents, 0, 0,
                                    0, 0, 0); // FUN_005a0d00
        }

        // "ItemMesh%d" -- a static mesh placeholder.
        sprintf(Name, "ItemMesh%d", Number);
        {
            float const MeshPosition[3] = {
                0.903f,
                static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.131f)), // DAT_005d150c
                0.0f};
            float const MeshHalfExtents[3] = {0.036f, 0.041f, 0.0f};
            float const MeshVector338[3] = {0.0f, 0.0f, 0.0f};
            CreateStaticControl(Name, "equipment\\models\\weapons\\li_rad_launcher.cmp",
                                MeshPosition, nullptr, MeshHalfExtents, 0, 0, 0, 1, 1, 1, 1, 0x50,
                                0, 0, MeshVector338); // FUN_005a0db0
        }

        sprintf(Name, "ItemName0%d", Number);
        {
            float const NamePosition[3] = {
                1.092f,
                static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.118f)), // DAT_005d14c8
                0.0f};
            CreateTextControl(Name, "ItemName", NamePosition, nullptr, 0.27f, 0.04f, 1.5f, 0, 0, 0,
                              0); // FUN_005a0e30
        }

        // "ItemQuantity0%d". Its y is kept for the "good deal" marker
        // ([ESP+0x3c]).
        sprintf(Name, "ItemQuantity0%d", Number);
        float const QuantityY =
            static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.148f)); // DAT_005d14b0
        {
            float const QuantityPosition[3] = {1.01f, QuantityY, 0.0f};
            CreateTextControl(Name, "ItemName", QuantityPosition, nullptr, 0.1f, 0.04f, 1.5f, 0, 0,
                              0xbcf, 0); // FUN_005a0e30
        }

        sprintf(Name, "ItemPrice0%d", Number);
        {
            float const PricePosition[3] = {
                PriceX,
                static_cast<float>(static_cast<double>(RowY) - static_cast<double>(0.152f)), // DAT_005d1414
                0.0f};
            CreateTextControl(Name, "ItemName", PricePosition, nullptr, 0.12f, 0.04f, 1.5f, 0xbd4, 2,
                              0xbd4, 0); // FUN_005a0e30
        }

        // "ItemGoodDeal0%d" -- the commodity dealer (trade mode 1) only.
        if (TradeMode == kTradeModeCommodity)
        {
            sprintf(Name, "ItemGoodDeal0%d", Number);
            float const DealPosition[3] = {1.201f, QuantityY, 0.0001f}; // 0x38d1b717
            float const DealHalfExtents[3] = {0.012f, 0.012f, 0.0f};
            float const DealVector338[3] = {0.0f, 0.0f, 0.0f};
            CreateStaticControl(Name, "Interface\\NeuroNet\\Inventory\\INV_green_square.3db",
                                DealPosition, nullptr, DealHalfExtents, 0, 0, 0, 1, 0, 0, 1, 0, 0,
                                0, DealVector338); // FUN_005a0db0
        }
    }

    // The info card (FUN_005a10e0). "InfoCard_Info" is not kept; the three
    // panes start hidden (flags bits 0..1 cleared). FUN_005a10e0's result is not tested.
    {
        float const InfoOffset[3] = {0.09f, -0.537f, 0.0f};
        CreateRichTextWin("InfoCard_Info", 1.09f, 0.255f, InfoOffset, true);
        InfoCardFacts = CreateRichTextWin("InfoCard_Facts", 0.5f, 0.265f, InfoOffset, true); // +0x398
        InfoCardFacts->Flags = static_cast<unsigned char>(InfoCardFacts->Flags & 0xfc);
        float const StatsOffset[3] = {0.69f, -0.537f, 0.0f};
        InfoCardStats = CreateRichTextWin("InfoCard_Stats", 0.29f, 0.265f, StatsOffset, true); // +0x39c
        InfoCardStats->Flags = static_cast<unsigned char>(InfoCardStats->Flags & 0xfc);
        float const ValuesOffset[3] = {0.99f, -0.537f, 0.0f};
        InfoCardValues = CreateRichTextWin("InfoCard_Values", 0.17f, 0.265f, ValuesOffset, true); // +0x3a0
        InfoCardValues->Flags = static_cast<unsigned char>(InfoCardValues->Flags & 0xfc);
    }

    // "Inventory_Scroll" (FUN_005a17e0); the result is not kept.
    {
        float const ScrollPosition[3] = {1.2349f, -0.099f, 0.0f};
        CreateUIScroll("Inventory_Scroll", ScrollPosition, 0.019f, 0.335f, 0, 5, 5, 0);
    }

    // "Inventory_Close" (FUN_005a0c90) -> +0x4d8.
    {
        float const ClosePosition[3] = {1.251f, -0.04f, 0.0f};
        float const CloseHalfExtents[3] = {0.02f, 0.02f, 0.0f};
        CloseButton = CreateButtonControl("Inventory_Close", "Interface\\NeuroNet\\Inventory\\x.3db",
                                          nullptr, "ui_select_item", nullptr, ClosePosition,
                                          nullptr, CloseHalfExtents, 0, 1, 0, 0, 0x409, 0, 0);
    }

    // "WelcomeText" -> +0x994, multi-line.
    {
        float const WelcomePosition[3] = {0.645f, -0.05f, 0.0f};
        WelcomeText = CreateTextControl("WelcomeText", "SubHeader", WelcomePosition, nullptr,
                                        0.37f, 0.06f, 1.8f, 0, 1, 0, 0);
        NnDealerMakeMultiLine(WelcomeText);
    }

    // The ship dealer's welcome block sits higher.
    float HelpY = -0.2f;   // [ESP+0x24] (0xbe4ccccd)
    float LineY = -0.143f; // DAT_005d1cac
    if (TradeMode == kTradeModeShip)
    {
        HelpY = -0.131f;   // 0xbe0624dd
        LineY = -0.072f;   // DAT_005d1ca8
    }

    // "WelcomeLine" -> +0x998.
    {
        float const LinePosition[3] = {0.643f, LineY, 0.0f};
        float const LineHalfExtents[3] = {0.0f, 0.0f, 0.0f};
        float const LineVector338[3] = {0.0f, 0.0f, 0.0f};
        WelcomeLine = CreateStaticControl("WelcomeLine",
                                          "Interface\\NeuroNet\\Inventory\\partitionline.3db",
                                          LinePosition, nullptr, LineHalfExtents, 0, 0, 0, 1, 0, 0,
                                          1, 0, 0, 0, LineVector338); // FUN_005a0db0
    }

    // The dealer's title (64 wchars) and the help ids.
    // FUN_004347e0 failures leave an empty title.
    wchar_t Title[0x40]; // [ESP+0xc0]
    Title[0] = 0;
    unsigned HelpIds = 0;
    if (TradeMode == kTradeModeCommodity)
    {
        if (GetIdsStringWWithRawFallback(0x48b, Title, 0x40) == 0)
            Title[0] = 0;
        HelpIds = 0x478;
    }
    else if (TradeMode == kTradeModeShip)
    {
        if (GetIdsStringWWithRawFallback(0x486, Title, 0x40) == 0)
            Title[0] = 0;
        HelpIds = GetPlayerShipArchetypeIdRetail() != 0 ? 0x489 : 0x499; // FUN_004c3e10
    }
    else if (TradeMode == kTradeModeEquipment)
    {
        if (GetIdsStringWWithRawFallback(0x48c, Title, 0x40) == 0)
            Title[0] = 0;
        HelpIds = 0x47d;
    }

    // "WelcomeHelp" -> +0x99c, multi-line.
    {
        float const HelpPosition[3] = {0.657f, HelpY, 0.0f};
        WelcomeHelp = CreateTextControl("WelcomeHelp", "Normal", HelpPosition, nullptr, 0.35f,
                                        0.06f, 1.4f, HelpIds, 0, 0, 0);
        NnDealerMakeMultiLine(WelcomeHelp);
    }

    // FUN_0043bbe0 on DAT_00668708 -- the strid_name of the base the player is docked at; zero
    // skips the block. With one, WelcomeText's text (property
    // 0x1d, vt+0xa8) is the title alone for the ship dealer (TradeMode 2, EBP); otherwise the
    // base name (64 wchars, empty when FUN_004347e0 fails) and the title go through ids 0x477
    // (63 wchars, the result not tested) with swprintf into [ESP+0x344] (256 wchars), upper-cased
    // by FUN_005b74d9 (the delay-loaded CharUpperW).
    {
        int const BaseIds = GetClientCurrentBaseStridName();
        if (BaseIds != 0)
        {
            wchar_t const* WelcomeString = Title;
            wchar_t BaseName[0x40];      // [ESP+0x244]
            wchar_t Format[0x3f];        // [ESP+0x2c4]
            wchar_t Welcome[0x100];      // [ESP+0x344]
            if (TradeMode != kTradeModeShip)
            {
                if (GetIdsStringWWithRawFallback(static_cast<unsigned>(BaseIds), BaseName, 0x40) ==
                    0)
                    BaseName[0] = 0;
                GetIdsStringWWithRawFallback(0x477, Format, 0x3f);
                _swprintf(Welcome, Format, BaseName, Title);
                CharUpperW(Welcome);
                WelcomeString = Welcome;
            }
            WelcomeText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(WelcomeString), 0);
        }
    }

    float const ZeroVector[3] = {0.0f, 0.0f, 0.0f};
    {
        float const BuyTextPosition[3] = {0.683f, -0.081f, 0.0f};
        BuyText = CreateTextControl("BuyText", "ButtonText", BuyTextPosition, nullptr, 0.164f,
                                    0.06f, 2.0f, 0xbc8, 1, 0, 0);
    }

    {
        float const ArrowPosition[3] = {0.563f, -0.074f, 0.0f};
        BuyArrow = CreateStaticControl("BuyArrow", "Interface\\NeuroNet\\Inventory\\INV_buyarrow.3db",
                                       ArrowPosition, nullptr, ZeroVector, 0, 0, 0, 1, 0, 0, 1, 0,
                                       0, 0, ZeroVector); // FUN_005a0db0
    }

    {
        float const SellTextPosition[3] = {0.601f, -0.081f, 0.0f};
        SellText = CreateTextControl("SellText", "ButtonText", SellTextPosition, nullptr, 0.164f,
                                     0.06f, 2.0f, 0xbc9, 1, 0, 0);
    }

    {
        float const ArrowPosition[3] = {0.726f, -0.073f, 0.0f};
        SellArrow = CreateStaticControl("SellArrow",
                                        "Interface\\NeuroNet\\Inventory\\INV_sellarrow.3db",
                                        ArrowPosition, nullptr, ZeroVector, 0, 0, 0, 1, 0, 0, 1, 0,
                                        0, 0, ZeroVector); // FUN_005a0db0
    }

    {
        float const ButtonPosition[3] = {0.645f, -0.073f, 0.0f};
        float const ButtonHalfExtents[3] = {0.134f, 0.026f, 0.0f};
        BuySellTradeButton = CreateWireButtonControl("BuySellTradeButton", "ui_item_select",
                                                     ButtonPosition, ButtonHalfExtents, 0, 0,
                                                     0xbc8, 0, 0); // FUN_005a0d00
    }

    // The partner inventory's transfer panel, kept on this window
    // (+0x364..+0x37c); NN_Inventory's SetProperty 0x48 copies them to the partner.
    {
        float const ButtonPosition[3] = {0.502f, -0.171f, 0.0f};
        float const ButtonHalfExtents[3] = {0.035f, 0.0335f, 0.0f};
        TransferItemButton = CreateWireStaticControl("TransferItemButton", ButtonPosition,
                                                     ButtonHalfExtents, 0, 0, 0, 0); // FUN_005a0d60
    }
    {
        float const ViewPosition[3] = {0.683f, -0.171f, 0.0f};
        float const ViewHalfExtents[3] = {0.138f, 0.034f, 0.0f};
        TransferItemView = CreateWireStaticControl("TransferItemView", ViewPosition,
                                                   ViewHalfExtents, 0, 0, 0, 0); // FUN_005a0d60
    }
    {
        float const MeshPosition[3] = {0.502f, -0.171f, 0.0f};
        float const MeshHalfExtents[3] = {0.036f, 0.041f, 0.0f};
        TransferItemMesh = CreateStaticControl("TransferItemMesh",
                                               "equipment\\models\\weapons\\li_rad_launcher.cmp",
                                               MeshPosition, nullptr, MeshHalfExtents, 0, 0, 0, 1,
                                               1, 1, 1, 0x50, 0, 0, ZeroVector); // FUN_005a0db0
    }
    {
        float const NamePosition[3] = {0.691f, NnFloatBits(0xbe21cac0), 0.0f}; // -0.158
        TransferItemName = CreateTextControl("TransferItemName", "ItemName", NamePosition, nullptr,
                                             0.27f, 0.04f, 1.5f, 0, 0, 0, 0);
    }
    {
        float const QuantityPosition[3] = {NnFloatBits(0x3f1be76c), -0.188f, 0.0f}; // 0.609
        TransferItemQuantity = CreateTextControl("TransferItemQuantity", "ItemName",
                                                 QuantityPosition, nullptr, 0.1f, 0.04f, 1.5f, 0,
                                                 0, 0xbcf, 0); // +0x9d0
    }
    {
        // A horizontal cargo bar with the empty mesh path DAT_005c865c and the
        // colour DAT_00679bac for both states, then property 0x19 (0x10000, 0).
        float const DamagePosition[3] = {0.502f, NnFloatBits(0xbe4ac084), 0.001f}; // -0.198
        float const DamageHalfExtents[3] = {0.03f, 0.003f, 0.0f};
        TransferItemDamage = CreateCargoBarControl("TransferItemDamage", "", DamagePosition,
                                                   nullptr, DamageHalfExtents, 1, 0x43c,
                                                   G_NnPaletteGreen,
                                                   G_NnPaletteGreen); // FUN_005a1080
        TransferItemDamage->SetProperty(0x19, 0x10000, 0);
    }
    {
        // x = the price column + 0.728 (the float at 0x005d1b40).
        float const PricePosition[3] = {PriceColumn + NnFloatBits(0x3f3a5e36), -0.192f, 0.0f};
        TransferItemPrice = CreateTextControl("TransferItemPrice", "ItemName", PricePosition,
                                              nullptr, 0.12f, 0.04f, 1.5f, 0xbd4, 2, 0xbd4, 0);
    }
    {
        float const DealPosition[3] = {0.8f, -0.188f, 0.001f};
        float const DealHalfExtents[3] = {0.012f, 0.012f, 0.0f};
        TransferItemGoodDeal = CreateStaticControl(
            "TransferItemGoodDeal", "Interface\\NeuroNet\\Inventory\\INV_green_square.3db",
            DealPosition, nullptr, DealHalfExtents, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
            ZeroVector); // FUN_005a0db0
        TransferItemGoodDeal->HitPriority = 100; // +0x74, no null test
    }

    // The quantity panel's rows; the ship dealer's sit higher.
    float PromptY = NnFloatBits(0xbe851eb8); // DAT_005d1b10, -0.26
    float TotalY = -0.461f;                  // [ESP+0x28]
    float TextY = -0.359f;                   // [ESP+0x24]
    float SliderY = -0.28f;                  // [ESP+0x3c]
    float PriceY = -0.41f;                   // [ESP+0x2c]
    if (TradeMode == kTradeModeShip)
    {
        TotalY = -0.424f;
        PromptY = NnFloatBits(0xbe7ced91); // DAT_005d1b0c, -0.247
        TextY = -0.337f;
        SliderY = -0.263f;
        PriceY = -0.382f;
    }

    {
        float const PromptPosition[3] = {0.648f, PromptY, 0.0f};
        QuantityPrompt = CreateTextControl("QuantityPrompt", "Normal", PromptPosition, nullptr,
                                           0.384f, 0.06f, 1.8f, 0x479, 1, 0, 0);
    }

    // "QuantitySlider" -> +0x9b8 (no null test before the two stores).
    {
        float const SliderPosition[3] = {0.494f, SliderY, 0.0f};
        QuantitySlider = CreateUIScroll("QuantitySlider", SliderPosition, 0.314f, 0.019f, 0, 5, 1,
                                        1);
        QuantitySlider->SetStepMode(true);  // +0x3b4 = 1
        QuantitySlider->SetPageField(1.0f); // +0x3cc = 1.0f
    }

    // "QuantityText" +0x9bc, "QuantityPrice" +0x9c0, "QuantityTotal" +0x9c4.
    {
        float const TextPosition[3] = {0.706f, TextY, 0.0f};
        QuantityText = CreateTextControl("QuantityText", "Normal", TextPosition, nullptr, 0.384f,
                                         0.06f, 1.6f, 0x47a, 0, 0, 0);
        float const PricePosition[3] = {0.706f, PriceY, 0.0f};
        QuantityPrice = CreateTextControl("QuantityPrice", "Normal", PricePosition, nullptr,
                                          0.384f, 0.06f, 1.6f, 0x47b, 0, 0, 0);
        float const TotalPosition[3] = {0.706f, TotalY, 0.0f};
        QuantityTotal = CreateTextControl("QuantityTotal", "Normal", TotalPosition, nullptr,
                                          0.384f, 0.06f, 1.6f, 0x47c, 0, 0, 0);
    }

    // "TransferPriceText" (+0x380) and "QuantityTotalText" (+0x9c8).
    {
        float const PricePosition[3] = {0.864f, PriceY, 0.0f};
        TransferPriceText = CreateTextControl("TransferPriceText", "Normal", PricePosition,
                                              nullptr, 0.384f, 0.06f, 1.6f, 0, 0, 0, 0);
        float const TotalPosition[3] = {0.864f, TotalY, 0.0f};
        QuantityTotalText = CreateTextControl("QuantityTotalText", "Normal", TotalPosition,
                                              nullptr, 0.384f, 0.06f, 1.6f, 0, 0, 0, 0);
    }

    // "ErrorHelp" -> +0x9d4, multi-line, then property 0x33 with the colour
    // {0xff, 0xff, 0xff, 0xff} ([ESP+0x40]) and 1.
    unsigned char White[4] = {0xff, 0xff, 0xff, 0xff}; // [ESP+0x40]
    {
        float const ErrorPosition[3] = {0.645f, -0.072f, 0.0f};
        ErrorHelp = CreateTextControl("ErrorHelp", "Normal", ErrorPosition, nullptr, 0.352f,
                                      0.06f, 1.4f, 0, 0, 0, 0);
        NnDealerMakeMultiLine(ErrorHelp);
        ErrorHelp->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(White), 1);
    }

    {
        float const PlusMinusPosition[3] = {0.677f, TotalY, 0.0f};
        PlusMinus = CreateTextControl("PlusMinus", "Normal", PlusMinusPosition, nullptr, 0.05f,
                                      0.06f, 1.8f, 0x481, 0, 0, 0);
    }

    // "QuantityEdit" (FUN_005a0e90) -> +0x9cc, then properties 0x43 (1, 8)
    // and 0x44 (1, 0).
    {
        float const EditPosition[3] = {0.864f, TextY, 0.0f};
        QuantityEdit = CreateEditControl("QuantityEdit", "Normal", EditPosition, nullptr, 0.384f,
                                         0.06f, 1.6f, 0, 0, 0);
        QuantityEdit->SetProperty(0x43, 1, 8);
        QuantityEdit->SetProperty(0x44, 1, 0);
    }

    ContinueButton = nullptr;   // +0x9e0
    ContinueText = nullptr;     // +0x9e4
    ShipBuyProblem = nullptr;   // +0x9e8
    BackToShipDealer = nullptr; // +0x9ec
    if (TradeMode != kTradeModeShip)
        return;

    // The ship dealer's continue button.
    bPreviewingShip = 1;
    {
        float const ButtonPosition[3] = {0.645f, -0.473f, 0.0f};
        float const ButtonHalfExtents[3] = {0.134f, 0.026f, 0.0f};
        ContinueButton = CreateWireButtonControl("ContinueButton", "ui_item_select",
                                                 ButtonPosition, ButtonHalfExtents, 0, 0, 0x32c, 0,
                                                 0); // FUN_005a0d00
        float const TextPosition[3] = {0.643f, -0.4816f, 0.0f};
        ContinueText = CreateTextControl("ContinueText", "ButtonText", TextPosition, nullptr,
                                         0.265f, 0.06f, 1.8f, 0x48e, 1, 0, 0);
    }

    // The rest only when the player owns a ship.
    if (GetPlayerShipArchetypeIdRetail() == 0) // FUN_004c3e10
        return;

    // "BackToShipDealer" (FUN_005a0c90) -> +0x9ec.
    {
        float const BackPosition[3] = {1.21f, -0.04f, 0.0f};
        float const BackHalfExtents[3] = {0.02f, 0.02f, 0.0f};
        BackToShipDealer = CreateButtonControl(
            "BackToShipDealer", "Interface\\neuronet\\inventory\\INV_goback.3db", nullptr,
            "ui_select_item", nullptr, BackPosition, nullptr, BackHalfExtents, 0, 1, 0, 0, 0x39f, 0,
            0);
    }

    {
        float const PricePosition[3] = {1.051f, -0.072f, 0.0f};
        ShipDealerShipPrice = CreateTextControl("ShipDealerShipPrice", "SubHeader", PricePosition,
                                                nullptr, 0.403f, 0.03f, 1.4f, 0x3c9, 1, 0, 0);
    }

    // "ShipBuyProblem" -> +0x9e8, multi-line, property 0x33 with the same
    // white colour.
    {
        float const ProblemPosition[3] = {0.474f, -0.431f, 0.0f};
        ShipBuyProblem = CreateTextControl("ShipBuyProblem", "SubHeader", ProblemPosition, nullptr,
                                           0.36f, 0.06f, 1.6f, 0, 0, 0, 1);
        NnDealerMakeMultiLine(ShipBuyProblem);
        ShipBuyProblem->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(White), 1);
    }
}

void NnDealer::Update()
{
    // FUN_0047fef0 (slot 11). The partner's visibility bit (flags bit 1) and,
    // while it has none, this window's three info card panes.
    if (Inventory != nullptr)
    {
        Flags = static_cast<unsigned char>((Flags & ~2u) | (Inventory->Flags & 2u));
        if (Inventory->InfoCardFacts == nullptr)
        {
            Inventory->InfoCardFacts = InfoCardFacts;   // +0x398
            Inventory->InfoCardStats = InfoCardStats;   // +0x39c
            Inventory->InfoCardValues = InfoCardValues; // +0x3a0
        }
    }

    // Every child -- bit0 ? slot 11 : slot 12, the next sibling read before
    // the call.
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

    // The rest only while active.
    if ((Flags & kWin3DActive) == 0)
        return;

    if (TradeMode == kTradeModeCommodity || TradeMode == kTradeModeEquipment)
    {
        RefreshMarketList(); // FUN_00482a90
    }
    else if (TradeMode == kTradeModeShip)
    {
        RefreshItemList(&Ship.Loadout, 0, 0); // FUN_0047d2f0
        EquipDesc* const Item = GetSelectedItem();
        if (Item != nullptr)
            ShowInfoCard(Item->ArchId, 0); // FUN_0047ba90
        Field4e2 = 0;
    }

    ErrorHelp->SetProperty(kWinPropHide, 1, 0);
    Field4e2 = 0;

    // A selection here and on the partner; a
    // window in dealer mode (+0x35c) tests its row, otherwise its item. The partner is not
    // null-tested. The ship purchase step (+0x98c) counts as no selection.
    bool const bOwnSelection =
        bDealerMode != 0 ? SelectedIndex != -1 : GetSelectedItem() != nullptr;
    bool bPartnerSelection = Inventory->bDealerMode != 0 ? Inventory->SelectedIndex != -1
                                                         : Inventory->GetSelectedItem() != nullptr;
    bool bAnySelection = bOwnSelection || bPartnerSelection; // BL
    if (bPreviewingShip != 0)
    {
        bAnySelection = false;
        bPartnerSelection = false;
    }

    // Property 1 (hide) on the welcome block with a selection, on the buy
    // side without one here, on the sell side without one on the partner, on the transfer panel
    // without any.
    uintptr_t const HideWelcome = bAnySelection ? 1 : 0;
    uintptr_t const HideBuy = bOwnSelection ? 0 : 1;
    uintptr_t const HideSell = bPartnerSelection ? 0 : 1;
    uintptr_t const HideTransfer = bAnySelection ? 0 : 1;
    WelcomeText->SetProperty(kWinPropHide, HideWelcome, 0);
    WelcomeLine->SetProperty(kWinPropHide, HideWelcome, 0);
    WelcomeHelp->SetProperty(kWinPropHide, HideWelcome, 0);
    BuyText->SetProperty(kWinPropHide, HideBuy, 0);
    BuyArrow->SetProperty(kWinPropHide, HideBuy, 0);
    SellText->SetProperty(kWinPropHide, HideSell, 0);
    SellArrow->SetProperty(kWinPropHide, HideSell, 0);
    BuySellTradeButton->SetProperty(kWinPropHide, HideTransfer, 0);
    TransferItemButton->SetProperty(kWinPropHide, HideTransfer, 0);   // +0x364
    TransferItemView->SetProperty(kWinPropHide, HideTransfer, 0);     // +0x368
    TransferItemMesh->SetProperty(kWinPropHide, HideTransfer, 0);     // +0x36c
    TransferItemName->SetProperty(kWinPropHide, HideTransfer, 0);     // +0x370
    TransferItemQuantity->SetProperty(kWinPropHide, HideTransfer, 0); // +0x9d0
    TransferItemDamage->SetProperty(kWinPropHide, HideSell, 0);       // +0x374
    TransferItemPrice->SetProperty(kWinPropHide, HideTransfer, 0);    // +0x378

    // The good-deal marker only for the commodity dealer, whose trade button
    // plays UI sound 0x25 on a buy (WireStaticControl +0x380).
    if (TradeMode != kTradeModeCommodity)
    {
        TransferItemGoodDeal->Flags = static_cast<unsigned char>(TransferItemGoodDeal->Flags & 0xfc);
    }
    else
    {
        TransferItemGoodDeal->SetProperty(kWinPropHide, HideTransfer, 0);
        static_cast<WireStaticControl*>(BuySellTradeButton)->SoundSlot = bOwnSelection ? 0x25 : 0;
    }

    QuantityPrompt->SetProperty(kWinPropHide, HideTransfer, 0);
    QuantitySlider->SetProperty(kWinPropHide, HideTransfer, 0);
    QuantityText->SetProperty(kWinPropHide, HideTransfer, 0);
    QuantityPrice->SetProperty(kWinPropHide, HideTransfer, 0);
    QuantityTotal->SetProperty(kWinPropHide, HideTransfer, 0);
    TransferPriceText->SetProperty(kWinPropHide, HideTransfer, 0); // +0x380
    QuantityTotalText->SetProperty(kWinPropHide, HideTransfer, 0);
    QuantityEdit->SetProperty(kWinPropHide, HideTransfer, 0);
    PlusMinus->SetProperty(kWinPropHide, HideTransfer, 0);

    // The button's caption (property 3) and the plus / minus sign.
    if (TradeMode == kTradeModeShip)
        BuySellTradeButton->SetProperty(kWinPropToolTip, 0x32d, 0);
    else
        BuySellTradeButton->SetProperty(kWinPropToolTip, bOwnSelection ? 0x32e : 0x32f, 0);
    PlusMinus->SetProperty(kTextPropSetIds, bOwnSelection ? 0x482 : 0x481, 0);

    // The transfer panel appeared or went -- the quantity starts at the
    // partner's selected stack (1 without one). The partner is not null-tested.
    if (bTransferPanelHidden != static_cast<unsigned char>(HideTransfer))
    {
        bTransferPanelHidden = static_cast<unsigned char>(HideTransfer);
        Quantity = 1;
        EquipDesc* const PartnerItem = Inventory->GetSelectedItem();
        if (PartnerItem != nullptr)
            Quantity = PartnerItem->Count;
        QuantitySlider->SetProperty(kScrollPropSetValue, static_cast<uintptr_t>(Quantity), 0);
        swprintf(ToolTipText, 0xff, L"%d", Quantity); // PTR_DAT_005d0ec4
        QuantityEdit->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
    }

    int MaxCount = 0; // [ESP+0x14]
    if (bAnySelection)
    {
        // The unit price, and a quantity of at least 1.
        FormatGroupedNumber(ToolTipText, TransferItemUnitPrice, -1); // FUN_004779a0
        TransferPriceText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
        if (Quantity == 0)
        {
            Quantity = 1;
            swprintf(ToolTipText, 0xff, L"%d", 1);
            QuantityEdit->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
        }

        if (SelectedIndex != -1)
        {
            // How many can be bought / taken.
            MaxCount = TransferItemMaxCount;
            float RankRequired = 0.0f; // [ESP+0x1c]
            // [ESP+0x18]: GetMaxBuyCount writes it; on the ship dealer's path retail leaves it
            // uninitialised unless a cap below sets it -- the port starts it at 0 (no message).
            int Reason = 0;
            if (TradeMode != kTradeModeShip)
            {
                unsigned const GoodId = GetSelectedMarketGood(); // FUN_00483580
                MaxCount = GetMaxBuyCount(GoodId, &Reason);      // FUN_004837a0
                if (Reason == 0 || Reason == 2)
                {
                    // Docked (DAT_0066873c, unsigned) at a base with a market
                    // (FUN_0043a510 on DAT_00668708, null-tested).
                    unsigned const BaseId = GetClientLocationCurrentBase();
                    FClientMarket* const Market =
                        BaseId != 0 ? GetClientLocationBaseMarket(BaseId) : nullptr;
                    if (Market != nullptr)
                    {
                        // The good's required rank, the
                        // player's rank, the good's required feeling ([ESP+0x24]).
                        RankRequired = ClientMarketGetMinLevel(Market, GoodId); // FUN_00438160
                        int const Rank = GetPlayerRankLevelRetail();            // FUN_004c55d0
                        float const FeelingRequired = ClientMarketGetMinRep(Market, GoodId); // FUN_004381c0

                        // In single player, a rank in 0..0x3e6 below the requirement (x87:
                        // only "less" is taken) --
                        // reason 5; otherwise the base's feeling (+0x990) below the good's --
                        // reason 6. Either leaves nothing to buy.
                        if (SinglePlayer() && Rank >= 0 && Rank < 0x3e7 &&
                            static_cast<double>(Rank) < static_cast<double>(RankRequired))
                        {
                            Reason = 5;
                            MaxCount = 0;
                        }
                        else if (static_cast<double>(BaseFeeling) <
                                 static_cast<double>(FeelingRequired))
                        {
                            Reason = 6;
                            MaxCount = 0;
                        }

                        // The partner's weapons / equipment filters (+0x32c
                        // 0 or 2; the partner is not null-tested): no hardpoint for the good at
                        // all (FUN_00585f20(GoodId, 1)) -- reason 7; with something to buy, no
                        // free one (FUN_00585f20(GoodId, 0)) -- reason 8.
                        int const Filter = Inventory->Mode;
                        if (Filter == kNnFilterWeapons || Filter == kNnFilterEquipment)
                        {
                            ShipViewControl* const View = Inventory->Ship.ShipView; // +0x330
                            if (View->FindHardpointForGood(GoodId, true) == nullptr)
                            {
                                MaxCount = 0;
                                Reason = 7;
                            }
                            else if (MaxCount > 0 &&
                                     View->FindHardpointForGood(GoodId, false) == nullptr)
                            {
                                MaxCount = 0;
                                Reason = 8;
                            }
                        }
                    }
                }
            }
            else
            {
                // The ship dealer -- the selected item fits the partner's
                // hold, and nanobots / shield batteries the new ship's limits.
                EquipDesc* const Item = GetSelectedItem();
                if (Item != nullptr && Inventory != nullptr)
                {
                    if (Item->ArchId != 0)
                    {
                        Archetype::Equipment* const Equipment = Archetype::GetEquipment(Item->ArchId);
                        if (Equipment != nullptr)
                        {
                            float const Volume = Equipment->Volume; // +0x5c
                            if (Volume > 0.0f)
                            {
                                float Capacity = 0.0f;
                                float Used = 0.0f;     // [ESP+0x28]
                                Inventory->Ship.GetCargoCapacityAndUsed(&Capacity, &Used); // FUN_004c63e0
                                int const Fit = static_cast<int>(
                                    (static_cast<double>(Capacity) - static_cast<double>(Used)) /
                                    static_cast<double>(Volume)); // ftol
                                if (MaxCount >= Fit)
                                    MaxCount = Fit;
                                if (MaxCount == 0)
                                    Reason = 4;
                            }
                        }
                    }

                    unsigned const ArchId = Item->ArchId;
                    if (ArchId == G_NanobotArchId || ArchId == G_ShieldBatteryArchId)
                    {
                        Archetype::Ship const* const ShipArch =
                            Archetype::GetShip(Inventory->Ship.ShipArchId); // +0x348
                        if (ShipArch != nullptr)
                        {
                            bool const bNanobots = ArchId == G_NanobotArchId;
                            EquipDesc const* const Carried =
                                Inventory->Ship.Loadout.find_matching_cargo(ArchId, 0, 1.0f);
                            int const CarriedCount = Carried != nullptr ? Carried->Count : 0;
                            int const Limit = bNanobots ? ShipArch->NanobotLimit       // +0x140
                                                        : ShipArch->ShieldBatteryLimit; // +0x144
                            int const Room = Limit - CarriedCount;
                            if (MaxCount > Room && Limit >= 0)
                                MaxCount = Room;
                            if (MaxCount <= 0)
                            {
                                MaxCount = 0;
                                Reason = bNanobots ? 9 : 10;
                            }
                        }
                    }
                }
            }

            if (MaxCount != 0)
            {
                // The slider's range, and the quantity capped by it.
                QuantitySlider->SetProperty(kScrollPropSetRange, 1, static_cast<uintptr_t>(MaxCount));
                if (Quantity > MaxCount)
                {
                    Quantity = MaxCount;
                    QuantitySlider->SetProperty(kScrollPropSetValue, static_cast<uintptr_t>(MaxCount), 0);
                    swprintf(ToolTipText, 0xff, L"%d", Quantity);
                    QuantityEdit->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
                }
            }
            else
            {
                // Why nothing can be bought.
                switch (Reason)
                {
                case 1: ShowErrorText(0x47e, 0); break;
                case 2: ShowErrorText(0x47f, 0); break;
                case 3: ShowErrorText(0x480, 0); break;
                case 4: ShowErrorText(0x490, 0); break;
                case 5: ShowErrorText(0x61b, static_cast<int>(RankRequired)); break; // ftol
                case 6: ShowErrorText(0x61c, 0); break;
                case 7: ShowErrorText(0x637, 0); break;
                case 8: ShowErrorText(0x63f, 0); break;
                case 9: ShowErrorText(0x3a4, 0); break;
                case 10: ShowErrorText(0x3a5, 0); break;
                case 11: ShowErrorText(0x3cf, 0); break;
                default: break;
                }
            }

            // The maximum, the edit box's limit, the prompt, and an engine
            // cannot be traded in at the ship dealer.
            swprintf(ToolTipText, 0xff, L"%d", MaxCount);
            TransferItemQuantity->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
            if (MaxCount > 1)
            {
                QuantityEdit->SetProperty(0x43, 1, 8);
                QuantityPrompt->SetProperty(kWinPropHide, 0, 0);
            }
            else
            {
                QuantityEdit->SetProperty(0x43, 0, 0);
                QuantityPrompt->SetProperty(kWinPropHide, 1, 0);
            }
            Archetype::Equipment* const Shown = Archetype::GetEquipment(TransferItemArchId);
            if (Shown != nullptr && Shown->get_class_type() == Archetype::AClassEngine &&
                TradeMode == kTradeModeShip)
                ShowErrorText(0x52d, 0);
        }
        else
        {
            // The partner's item is sold -- up to its whole stack.
            QuantitySlider->SetProperty(kScrollPropSetRange, 1, static_cast<uintptr_t>(TransferItemMaxCount));
            if ((TradeMode == kTradeModeCommodity || TradeMode == kTradeModeEquipment) && Inventory != nullptr)
            {
                EquipDesc* const PartnerItem = Inventory->GetSelectedItem();
                if (PartnerItem != nullptr && PartnerItem->bMission) // +0x18
                    ShowErrorText(0x575, 0);
            }
            Archetype::Equipment* const Shown = Archetype::GetEquipment(TransferItemArchId);
            if (Shown != nullptr && Shown->get_class_type() == Archetype::AClassEngine)
                ShowErrorText(TradeMode == kTradeModeShip ? 0x52d : 0x52c, 0);
            swprintf(ToolTipText, 0xff, L"%d", TransferItemMaxCount);
            TransferItemQuantity->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
            if (TransferItemMaxCount > 1)
                QuantityEdit->SetProperty(0x43, 1, 8);
            else
                QuantityEdit->SetProperty(0x43, 0, 0);
        }

        FormatGroupedNumber(ToolTipText, Quantity * TransferItemUnitPrice, -1);
        QuantityTotalText->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
    }

    // The ship dealer's help, price and "not enough credits" line. The
    // partner, +0x9f0 and (on the second path) +0x9e8 are not null-tested.
    if (TradeMode == kTradeModeShip)
    {
        if (SelectedIndex == -1 && Inventory->SelectedIndex == -1)
            WelcomeHelp->Flags = static_cast<unsigned char>(WelcomeHelp->Flags | 3);
        else
            WelcomeHelp->Flags = static_cast<unsigned char>(WelcomeHelp->Flags & 0xfc);

        int const OwnResale = NnLoadoutResaleValue(&Ship.Loadout, &Ship,
                                                   &Inventory->SavedLoadout); // FUN_004c6f70
        int const PartnerResale = NnLoadoutResaleValue(&Inventory->Ship.Loadout, &Inventory->Ship,
                                                       &Inventory->SavedLoadout);
        int ShipPrice = ShipHullPrice - OwnResale + PartnerResale;
        if (ShipPrice < 0) // JNS
            ShipPrice = 0;
        FormatGroupedNumber(ToolTipText, ShipPrice, 0x3c9);
        ShipDealerShipPrice->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);

        int const Cost = ShipHullPrice - PlayerShipTradeInValue - OwnResale + PartnerResale;
        if (ShipBuyProblem != nullptr && Cost > static_cast<int>(GetPlayerCreditsRetail())) // JLE
        {
            ContinueButton->SetProperty(kWinPropHide, 1, 0);
            ContinueText->SetProperty(kWinPropHide, 1, 0);
            FormatGroupedNumber(ToolTipText, Cost - static_cast<int>(GetPlayerCreditsRetail()), -1);
            GetIdsStringWWithRawFallback(0x630, G_NnTextScratchA, G_NnTextScratchCapacity);
            swprintf(G_NnTextScratchB, 0x1000, G_NnTextScratchA, ToolTipText);
            ShipBuyProblem->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
        }
        else
        {
            ContinueButton->SetProperty(kWinPropHide, 0, 0);
            ContinueText->SetProperty(kWinPropHide, 0, 0);
            ShipBuyProblem->SetProperty(kTextPropSetIds, 0, 0);
        }
    }

    // The partner just traded (+0x3b8) -- the buy / sell controls hide.
    if (Inventory->bTradePending != 0)
    {
        BuySellTradeButton->SetProperty(kWinPropHide, 1, 0);
        BuyText->SetProperty(kWinPropHide, 1, 0);
        BuyArrow->SetProperty(kWinPropHide, 1, 0);
        SellText->SetProperty(kWinPropHide, 1, 0);
        SellArrow->SetProperty(kWinPropHide, 1, 0);
    }

    if (Inventory->bCheckStockAfterBuy != 0 || Inventory->bCheckHardpointAfterBuy != 0)
        ErrorHelp->SetProperty(kTextPropSetIds, 0, 0);

    // The partner's pending good no longer fits -- drop the selection when
    // it is the one selected here and nothing can be bought.
    if (Inventory->bDeselectBoughtGoodIfEmpty == 0)
        return;
    Inventory->bDeselectBoughtGoodIfEmpty = 0;
    if (SelectedIndex == -1 || TradeMode == kTradeModeShip)
        return;
    unsigned const SelectedGood = GetSelectedMarketGood(); // FUN_00483580
    if (MaxCount != 0 || SelectedGood != Inventory->PendingBuyGoodId)
        return;
    ClearSelection();
    ErrorHelp->SetProperty(kTextPropSetIds, 0, 0);
}

intptr_t NnDealer::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_004822a0 (RET 0xc).
    switch (Msg)
    {
    case kWinNotifyHighlight:
    case kWinNotifyUnhighlight:
        // The row hover goes to NN_Inventory (FUN_00479d10).
        return NnInventory::SetProperty(Msg, A, B);

    case 0x15:
        // The ship view picked an instance -- select the row of its hardpoint.
        SelectHardpoint(Ship.ShipView->GetHardpointOfInstance(static_cast<long>(A))); // FUN_00585e20, FUN_0047e270
        return 0;

    case 0x16:
    {
        // An item dropped on hardpoint A. The name goes into the argument's slot.
        CacheString Hardpoint;
        Hardpoint.Value = StringAlloc(reinterpret_cast<char const*>(A), false);
        if (Inventory != nullptr && Inventory->GetSelectedItem() != nullptr)
        {
            // The partner's selected item, mounted on this window's ship.
            EquipDesc* const Item = Inventory->GetSelectedItem();
            if (Item == nullptr)
                return 0;
            Ship.ShipView->CollectHpTypes(Hardpoint.Value); // FUN_00586560
            int const Filter = NnItemFilterMode(Item->ArchId); // FUN_0047e460
            int Group = 0;
            if (Filter == kNnFilterWeapons)
                Group = 0;
            else if (Filter == kNnFilterEquipment)
                Group = 1;
            else
                return 0;
            if (!Ship.ShipView->HasHpTypeGroup(Group)) // FUN_00586800
                return 0;
            if (Ship.MountItem(Item, &Hardpoint, 0, true)) // FUN_004c6530
                Inventory->Ship.RemoveItem(Item, 0);       // FUN_004c6430
            ClearSelection();
            Inventory->ClearSelection();
            return 0;
        }

        // This window's selected item moves to the hardpoint.
        if (GetSelectedItem() == nullptr)
            return 0;
        EquipDesc* const Item = GetSelectedItem();
        if (Item == nullptr)
            return 0;
        Ship.ShipView->CollectHpTypes(Hardpoint.Value); // FUN_00586560
        int const Filter = NnItemFilterMode(Item->ArchId);
        int Group = 0;
        if (Filter == kNnFilterWeapons)
            Group = 0;
        else if (Filter == kNnFilterEquipment)
            Group = 1;
        else
            return 0;
        if (!Ship.ShipView->HasHpTypeGroup(Group)) // FUN_00586800
            return 0;
        Ship.SetItemHardpoint(Item, Hardpoint.Value); // FUN_004c6910
        ClearSelection();
        if (Inventory == nullptr)
            return 0;
        Inventory->ClearSelection();
        return 0;
    }

    case kWinNotifyScrollValue:
        // A slider moved -- the list scroll or the quantity slider.
        if (reinterpret_cast<WinControl*>(A) == InventoryScroll) // +0x3d0
        {
            FirstRow = static_cast<int>(B);
            return 0;
        }
        if (reinterpret_cast<WinControl*>(A) != QuantitySlider) // +0x9b8
            return 0;
        Quantity = static_cast<int>(B);
        if (Quantity < 0)
            Quantity = 0;
        swprintf(ToolTipText, 0xff, L"%d", Quantity); // PTR_DAT_005d0ec4
        QuantityEdit->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
        return 0;

    case 0x45:
    {
        // The quantity edit's text changed.
        wchar_t const* const Text = reinterpret_cast<wchar_t const*>(A);
        if (Text == nullptr)
        {
            QuantitySlider->SetProperty(kScrollPropSetValue, 1, 0);
            return 0;
        }
        int Value = 1; // the argument's slot, 1 before swscanf
        swscanf(Text, L"%d", &Value);
        if (Value < 1)
            Quantity = 1;
        else if (Value > TransferItemMaxCount)
            Quantity = TransferItemMaxCount;
        else
            Quantity = Value;
        swprintf(ToolTipText, 0xff, L"%d", Quantity);
        if (_wcsicmp(Text, ToolTipText) != 0)
            QuantityEdit->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
        QuantitySlider->SetProperty(kScrollPropSetValue, static_cast<uintptr_t>(Quantity), 0);
        return 0;
    }

    case 0x1a:
    case kWinNotifyClick:
    {
        WinControl* const Control = reinterpret_cast<WinControl*>(A);
        if (Control == CloseButton) // +0x4d8
        {
            NnMainButtonOnModalOpen(); // FUN_0045a460
            return 1;
        }
        if (Control == BackToShipDealer) // +0x9ec
        {
            NnMainButtonOnModalOpen(); // FUN_0045a460
            // FUN_00442200(7) -- the NavBar activates its ship dealer tab.
            NavBarOpenButton(7);
            return 1;
        }
        if (Control == ContinueButton && Inventory != nullptr) // +0x9e0
        {
            if (GetPlayerShipArchetypeIdRetail() == 0) // FUN_004c3e10
            {
                NnMainButtonOnModalOpen();
                return 1;
            }
            if (bPreviewingShip != 0)
                return 1;
            BuyShip(); // FUN_00483ae0
            return 1;
        }
        if (Control == BuySellTradeButton) // +0x9b0
        {
            if (TradeMode == kTradeModeShip)
            {
                if (SelectedIndex != -1)
                {
                    // The partner is not null-tested.
                    Inventory->TakeDealerSelection(); // FUN_0047e920
                }
                else
                {
                    // The partner's selected item goes back into this
                    // (the partner's dealer's) model.
                    NnInventory* const Partner = Inventory;
                    EquipDesc* const Item = Partner->GetSelectedItem();
                    if (Item != nullptr && Partner->Dealer != nullptr &&
                        Partner->Dealer->Quantity != 0)
                    {
                        int const Count = Partner->Dealer->Quantity;
                        Item->set_equipped(false);
                        if (Partner->Dealer->Ship.AddItem(Item, Count, true)) // FUN_004c64b0
                            Partner->Ship.RemoveItem(Item, Count);             // FUN_004c6430
                        Partner->ClearSelection();
                        Partner->SelectedItem = nullptr;         // +0x4f4
                        Partner->Dealer->SelectedItem = nullptr; // dealer +0x4f4
                    }
                }
            }
            else if (SelectedIndex != -1)
            {
                Inventory->BuyFromDealer(Quantity); // FUN_0047eab0
                Inventory->PendingMountHardpoint.clear();
            }
            else
            {
                Inventory->SellSelected(Quantity); // FUN_0047eea0
            }

            // The quantity goes back to 1.
            Quantity = 1;
            QuantitySlider->SetProperty(kScrollPropSetValue, 1, 0);
            swprintf(ToolTipText, 0xff, L"%d", Quantity);
            QuantityEdit->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);
            return 1;
        }

        // A control of a row (every row is tested; retail does not stop at
        // the first match). A click on the row's button also clears +0x984.
        for (int Row = 0; Row < kNnInventoryRows; ++Row)
        {
            if (Control != ItemButton[Row] && Control != ItemView[Row] &&
                Control != ItemGoodDeal[Row])
                continue;
            if (Control == ItemButton[Row])
                Field984 = 0;
            SelectDealerRow(Row, 1); // FUN_00482120
            bShowShipInfoCard = 0;
            if (Inventory != nullptr)
                Inventory->ClearSelection();
        }

        // The filter buttons (the partner follows this window's mode) and
        // the ship view's info button.
        int NewMode = -1;
        if (Control == FilterCargoBay)
            NewMode = kNnFilterCargoBay;
        else if (Control == FilterWeapons)
            NewMode = kNnFilterWeapons;
        else if (Control == FilterEquipment)
            NewMode = kNnFilterEquipment;
        else if (Control == FilterCommodity)
            NewMode = kNnFilterCommodity;
        else if (Control == FilterAmmo)
            NewMode = kNnFilterAmmo;
        if (NewMode != -1)
        {
            SetMode(NewMode, true); // FUN_0047f240
            if (Inventory != nullptr)
                Inventory->SetMode(Mode, true);
            return 0;
        }
        if (Control == Ship.InfoText) // +0x344 "ShipView_Info"
        {
            SelectDealerRow(-1, 1);
            bShowShipInfoCard = 1;
        }
        return 0;
    }

    default:
        return 0;
    }
}

void NnDealer::ShowErrorText(unsigned Ids, int Arg)
{
    // FUN_0047fd50 (RET 8). No control pointer is null-tested.
    ErrorHelp->SetProperty(kWinPropHide, 0, 0); // +0x9d4 shown

    // Hide the rest (property 1, 1).
    BuyText->SetProperty(kWinPropHide, 1, 0);            // +0x9a0
    BuyArrow->SetProperty(kWinPropHide, 1, 0);           // +0x9a4
    SellText->SetProperty(kWinPropHide, 1, 0);           // +0x9a8
    SellArrow->SetProperty(kWinPropHide, 1, 0);          // +0x9ac
    BuySellTradeButton->SetProperty(kWinPropHide, 1, 0); // +0x9b0
    QuantityPrompt->SetProperty(kWinPropHide, 1, 0);     // +0x9b4
    QuantitySlider->SetProperty(kWinPropHide, 1, 0);     // +0x9b8
    QuantityText->SetProperty(kWinPropHide, 1, 0);       // +0x9bc
    QuantityPrice->SetProperty(kWinPropHide, 1, 0);      // +0x9c0
    QuantityTotal->SetProperty(kWinPropHide, 1, 0);      // +0x9c4
    TransferPriceText->SetProperty(kWinPropHide, 1, 0);  // +0x380
    QuantityTotalText->SetProperty(kWinPropHide, 1, 0);  // +0x9c8
    QuantityEdit->SetProperty(kWinPropHide, 1, 0);       // +0x9cc
    PlusMinus->SetProperty(kWinPropHide, 1, 0);          // +0x9d8

    // No argument -- the ids goes to the control as is (property 0x1e).
    if (Arg == 0)
    {
        ErrorHelp->SetProperty(kTextPropSetIds, Ids, 0);
        return;
    }

    // The ids' string (FUN_004347e0, DAT_006119f8 wchars into
    // DAT_0066dc60) is the format of swprintf into DAT_0066fc60 (property 0x1d).
    GetIdsStringWWithRawFallback(Ids, G_NnTextScratchA, G_NnTextScratchCapacity);
    swprintf(G_NnTextScratchB, 0x1000, G_NnTextScratchA, Arg);
    ErrorHelp->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
}

void NnDealer::SelectDealerRow(int Row, int /*Unused*/)
{
    // FUN_00482120 (RET 8). The second argument is never read.
    SelectRow(Row, 1); // FUN_0047e0e0

    // Quantity 1 on the slider and the edit box.
    Quantity = 1;
    QuantitySlider->SetProperty(kScrollPropSetValue, 1, 0);
    swprintf(ToolTipText, 0xff, L"%d", Quantity); // PTR_DAT_005d0ec4, into +0x508
    QuantityEdit->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(ToolTipText), 0);

    // The row's item on the ship model; the flag byte reuses the first
    // argument's slot ([ESP+0x10]).
    bool const bFlag = TradeMode == kTradeModeShip || TradeMode == kTradeModeEquipment;
    EquipDesc* const Item = GetSelectedItem(); // FUN_0047e790
    Ship.ShowItem(Item, bFlag, Item != nullptr ? NnItemFilterMode(Item->ArchId) : kNnFilterNone); // FUN_004c6a50

    // A commodity dealer puts its partner on the cargo filter.
    if (Inventory != nullptr && TradeMode == kTradeModeCommodity && Inventory->Mode != 4)
        Inventory->SetMode(4, false); // FUN_0047f240

    // The partner's +0x394 names the hardpoint the selected good would go
    // on (this dealer's weapons / equipment filters).
    if (Inventory != nullptr)
    {
        Inventory->PendingMountHardpoint.clear();
        unsigned const GoodId = GetSelectedMarketGood(); // FUN_00483580
        if (GoodId != 0 && (Mode == 0 || Mode == 2))
        {
            char const* const Hardpoint =
                Inventory->Ship.ShipView->FindHardpointForGood(GoodId, false); // FUN_00585f20
            if (Hardpoint != nullptr)
                Inventory->PendingMountHardpoint.Value = StringAlloc(Hardpoint, false);
        }
    }

    // The partner's selection is cleared (the inlined sequence, including
    // its dealer's +0x4e8 / +0x4ec).
    if (Inventory != nullptr)
        Inventory->ClearSelection();
}

void NnDealer::BuyShip()
{
    // FUN_00483ae0. The cost: the package's price less the old ship's trade-in, less what the
    // market pays for this window's loadout, plus what it pays for the partner's.
    int const OwnResale = NnLoadoutResaleValue(&Ship.Loadout, &Ship,
                                               &Inventory->SavedLoadout); // FUN_004c6f70
    int const PartnerResale = NnLoadoutResaleValue(&Inventory->Ship.Loadout, &Inventory->Ship,
                                                   &Inventory->SavedLoadout);
    int const Cost = ShipHullPrice - PlayerShipTradeInValue - OwnResale + PartnerResale;
    if (static_cast<int>(GetPlayerCreditsRetail()) < Cost) // FUN_004c4ce0, signed (JGE)
    {
        // FUN_00489e80(DAT_00671c74, 0) -- the "not_enough_money" voice.
        NnVoiceEnqueue(G_NnVoiceNotEnoughMoneyId, 0);
        return;
    }

    // FUN_0043a510(DAT_0066873c) on DAT_00668708 (not null-tested), then
    // FUN_00438220(base, +0x97c, 1) -- the server is asked to sell the player the package.
    {
        unsigned const BaseId = GetClientLocationCurrentBase();
        ClientMarketRequestBuy(GetClientLocationBaseMarket(BaseId), BaseId,
                               static_cast<unsigned>(ShipPackageGoodId), 1);
    }

    // The new loadout is the partner's (it shows the new ship); the ship
    // archetype is this window's.
    EquipDescList NewLoadout = Inventory->Ship.Loadout; // std::list copy ctor
    unsigned const ShipArchId = Ship.ShipArchId;      // +0x348

    // The player's collision groups, every one at full health.
    std::list<CollisionGroupDesc> Groups;
    std::list<CollisionGroupDesc> const& PlayerGroups = GetPlayerCollisionGroupListRetail(); // FUN_004c3f00
    Groups.insert(Groups.begin(), PlayerGroups.begin(), PlayerGroups.end());
    for (std::list<CollisionGroupDesc>::iterator It = Groups.begin(); It != Groups.end(); ++It)
        It->Health = 1.0f; // +0xc of the node
    ReqPlayerCollisionGroupsRetail(&Groups); // FUN_004c3f10

    Ship.Clear();             // FUN_004c6250
    Inventory->Ship.Clear();

    ReqPlayerCashRetail(static_cast<int>(GetPlayerCreditsRetail()) - Cost); // FUN_004c4cc0
    ReqPlayerEquipmentRetail(&NewLoadout);  // FUN_004c4040
    ReqPlayerShipArchRetail(ShipArchId);    // FUN_004c3df0
    ReqPlayerHullStatusRetail(1.0f);        // FUN_004c4f50

    // FUN_004427e0(1) -- the NavBar's repair tab disabled (the new ship
    // is undamaged).
    NavBarRefreshRepairTab(true);

    G_WeaponGroups.Clear(); // FUN_004f6190
    HudPostCommand(USER_INVENTORY_CLOSE, 0); // FUN_005763b0

    // FUN_00489e80(DAT_00671c60, 0) -- the "ship_purchase_complete" voice.
    NnVoiceEnqueue(G_NnVoiceShipPurchaseCompleteId, 0);
}

void NnDealer::RefreshMarketList()
{
    // FUN_00482a90. FUN_0043a510(DAT_0066873c) on DAT_00668708 (not
    // null-tested: FUN_00437f70 runs on it as it is).
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());

    // The commodity dealer lists the cargo filter and collects category 0, the equipment dealer
    // category 1. Any other mode passes an uninitialised local in retail; the only callers are
    // Update (trade modes 1 and 3 only) and this function itself (same mode), so that path is
    // never taken.
    unsigned Category = 0;
    if (TradeMode == kTradeModeCommodity)
        Mode = 4; // +0x32c
    else if (TradeMode == kTradeModeEquipment)
        Category = 1;

    // The market's goods of that category (FUN_00437f70) into a local list,
    // then FUN_00483230 (mode 3) or FUN_00482f10 orders it.
    std::list<FClientMarketGood const*> Goods;
    ClientMarketCollectCategory(Market, &Goods, Category);
    if (TradeMode == kTradeModeEquipment)
        SortEquipmentMarketGoods(&Goods); // FUN_00483230
    else
        SortMarketGoodsByName(&Goods);    // FUN_00482f10

    // One row per entry in stock (+0x1c > 0; the signed clamp
    // is then always taken).
    int Counter = 0;                           // [ESP+0x14]
    FClientMarketGood const* Selected = nullptr; // [ESP+0x1c]
    for (std::list<FClientMarketGood const*>::iterator It = Goods.begin(); It != Goods.end(); ++It)
    {
        FClientMarketGood const* const Entry = *It;
        if (Entry->Stock <= 0)
            continue;
        int const Stock = Entry->Stock;

        // The filter -- the archetype's own in mode 3, otherwise 4, or 5
        // while docked at a base whose market prices the good at exactly 0.0 (FCOMP 0, TEST
        // AH,0x44 + JP: only "equal" falls through to the 5).
        unsigned const ArchId = Good2Arch(Entry->Info.GoodId);
        int Filter = kNnFilterCommodity;
        if (TradeMode == kTradeModeEquipment)
            Filter = NnItemFilterMode(ArchId); // FUN_0047e460
        else
        {
            unsigned const BaseId = GetClientLocationCurrentBase();
            if (BaseId != 0)
            {
                FClientMarket* const BaseMarket = GetClientLocationBaseMarket(BaseId);
                if (BaseMarket != nullptr &&
                    static_cast<double>(ClientMarketGetPrice(BaseMarket, Arch2Good(ArchId))) ==
                        0.0)
                    Filter = kNnFilterNone;
            }
        }

        // FillItemRow(&Counter, arch, filter, stock, package price, 0, 0,
        // 0, 0, the CanBuyGood byte, 0, 0, 0).
        int const Price = GetMarketPackagePrice(Entry->Info.GoodId); // FUN_0047f040
        unsigned char const bCanBuy = static_cast<unsigned char>(Entry->CanBuyFlags & 0xffu);
        bool const bRowSelected =
            FillItemRow(&Counter, ArchId, Filter, Stock, Price, 0, 0, 0, 0, bCanBuy, 0, 0, 0);

        // The first entry FillItemRow reports as the selected row.
        if (Selected == nullptr && bRowSelected)
            Selected = Entry;
    }

    // That entry's archetype on the info card.
    if (Selected != nullptr)
        ShowInfoCard(Good2Arch(Selected->Info.GoodId), 0); // FUN_0047ba90

    Field4e2 = 0;
    UpdateRowVisibility(Counter); // FUN_0047f3e0

    // The row count changed -- rescale the scroll bar, keep no gap below the
    // last row, build the list again.
    if (Counter != LastRowCount)
    {
        if (InventoryScroll != nullptr)
            InventoryScroll->SetProperty(kScrollPropSetRange, 0, static_cast<uintptr_t>(Counter - 1));
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
        RefreshMarketList();
    }

    // Unsigned compare.
    if (Counter == 0)
        SelectedIndex = -1;
    if (static_cast<unsigned>(SelectedIndex) >= static_cast<unsigned>(Counter))
        SelectedIndex = -1;

    // The equipment dealer points the partner's ship view at the good.
    if (TradeMode == kTradeModeEquipment)
    {
        unsigned const GoodId = GetSelectedMarketGood(); // FUN_00483580
        if (GoodId != 0 && Inventory != nullptr)
            Inventory->Ship.ShipView->SelectGood(GoodId, true); // FUN_00585d90
    }
}

bool NnDealer::CanBuyGood(unsigned GoodId)
{
    // FUN_00482e00. Callers: the market list sorts FUN_00482f10 and FUN_00483230.
    if (TradeMode != kTradeModeEquipment) // +0x978
        return true;

    // Not docked (DAT_0066873c), or no market (FUN_0043a510 on
    // DAT_00668708, null-tested) -- false.
    unsigned const BaseId = GetClientLocationCurrentBase();
    if (BaseId == 0)
        return false;
    FClientMarket* const Market = GetClientLocationBaseMarket(BaseId);
    if (Market == nullptr)
        return false;

    // The good's required rank, the player's rank, then the required feeling against +0x990:
    // x87 goes on when the requirement is below or equal (or unordered); above the base's
    // feeling -- false.
    float const RankRequired = ClientMarketGetMinLevel(Market, GoodId); // FUN_00438160
    int const Rank = GetPlayerRankLevelRetail();                        // FUN_004c55d0
    float const FeelingRequired = ClientMarketGetMinRep(Market, GoodId); // FUN_004381c0
    if (static_cast<double>(FeelingRequired) > static_cast<double>(BaseFeeling))
        return false;

    // A rank in 0..0x3e6 below the requirement (x87: only "less"), in single player (tested
    // last) -- false.
    if (Rank >= 0 && Rank < 0x3e7 &&
        static_cast<double>(Rank) < static_cast<double>(RankRequired) && SinglePlayer())
        return false;

    // The partner's weapons / equipment filters (+0x32c 0 or 2; the partner
    // is not null-tested): no hardpoint for the good at all (FUN_00585f20(GoodId, 1)) -- false.
    int const Filter = Inventory->Mode;
    if (Filter == kNnFilterWeapons || Filter == kNnFilterEquipment)
    {
        if (Inventory->Ship.ShipView->FindHardpointForGood(GoodId, true) == nullptr)
            return false;
    }

    // The ammo filter needs a launcher for it in the partner's loadout
    // (FUN_00482d80 on +0x34c with Good2Arch(GoodId)); every other case -- true.
    if (Inventory->Mode == 1)
        return NnListHasLauncherFor(&Inventory->Ship.Loadout, Good2Arch(GoodId));
    return true;
}

// The CanBuyGood byte the two sorts keep in the low byte of the market entry's +0x24. Retail
// writes through the list's entry pointer into the market itself; the port's list holds const
// pointers (ClientMarketCollectCategory), so the store casts the const away.
static void NnDealerSetCanBuyByte(FClientMarketGood const* Entry, bool bCanBuy)
{
    FClientMarketGood* const Writable = const_cast<FClientMarketGood*>(Entry);
    Writable->CanBuyFlags = (Writable->CanBuyFlags & ~0xffu) | (bCanBuy ? 1u : 0u);
}

static unsigned char NnDealerCanBuyByte(FClientMarketGood const* Entry)
{
    return static_cast<unsigned char>(Entry->CanBuyFlags & 0xffu);
}

// FUN_00482f10's 8-byte record: the entry and its index into the name buffer.
struct FNnDealerNameSortRecord
{
    FClientMarketGood const* Entry; // +0
    int NameIndex;                  // +4
};

void NnDealer::SortMarketGoodsByName(std::list<FClientMarketGood const*>* Goods)
{
    // FUN_00482f10.
    int const Count = static_cast<int>(Goods->size());
    std::vector<FNnDealerNameSortRecord> Records(static_cast<size_t>(Count));
    std::vector<wchar_t> Names(static_cast<size_t>(Count) * 0x40);

    // Per node, in list order: the record {entry, i}, the entry's CanBuyGood
    // byte (FUN_00482e00), and the name -- FUN_004347e0(DAT_0067eca8, Equipment +0x14, name,
    // 0x40) of GetEquipment(Good2Arch(good)), or an empty string without one.
    int Index = 0;
    for (std::list<FClientMarketGood const*>::iterator It = Goods->begin(); It != Goods->end();
         ++It, ++Index)
    {
        FClientMarketGood const* const Entry = *It;
        Records[Index].Entry = Entry;
        Records[Index].NameIndex = Index;
        NnDealerSetCanBuyByte(Entry, CanBuyGood(Entry->Info.GoodId));
        Archetype::Equipment const* const Equip =
            Archetype::GetEquipment(Good2Arch(Entry->Info.GoodId));
        wchar_t* const Name = &Names[static_cast<size_t>(Index) * 0x40];
        if (Equip == nullptr)
            Name[0] = L'\0';
        else
            GetIdsStringWWithRawFallback(static_cast<unsigned>(Equip->IdsName), Name, 0x40);
    }

    // Bubble passes until one makes no swap. A pair swaps when the first cannot be bought and
    // the second can, or both bytes are equal and _wcsicmp(first name, second name) > 0.
    bool bSorted = false;
    while (!bSorted)
    {
        bSorted = true;
        for (int Pair = 0; Pair < Count - 1; ++Pair)
        {
            FNnDealerNameSortRecord& A = Records[Pair];
            FNnDealerNameSortRecord& B = Records[Pair + 1];
            unsigned char const CanBuyA = NnDealerCanBuyByte(A.Entry);
            unsigned char const CanBuyB = NnDealerCanBuyByte(B.Entry);
            bool bSwap = false;
            if (CanBuyA == 0 && CanBuyB != 0)
                bSwap = true;
            else if (CanBuyA == CanBuyB &&
                     _wcsicmp(&Names[static_cast<size_t>(A.NameIndex) * 0x40],
                              &Names[static_cast<size_t>(B.NameIndex) * 0x40]) > 0)
                bSwap = true;
            if (bSwap)
            {
                FNnDealerNameSortRecord const Saved = A;
                A = B;
                B = Saved;
                bSorted = false;
            }
        }
    }

    // Every node erased (FUN_00516f70), then one node per record appended
    // in order; the two buffers freed.
    Goods->clear();
    for (int Out = 0; Out < Count; ++Out)
        Goods->push_back(Records[Out].Entry);
}

// FUN_00483230's 0x10-byte record.
struct FNnDealerEquipSortRecord
{
    FClientMarketGood const* Entry; // +0x0
    int Class;                      // +0x4 Equipment vt+0x10 (get_class_type), 0 without one
    int bSpecial;                   // +0x8 the byte flag, zero-extended
    float Price;                    // +0xc
};

void NnDealer::SortEquipmentMarketGoods(std::list<FClientMarketGood const*>* Goods)
{
    // FUN_00483230.
    int const Count = static_cast<int>(Goods->size());
    std::vector<FNnDealerEquipSortRecord> Records(static_cast<size_t>(Count));

    // Per node, in list order.
    int Index = 0;
    for (std::list<FClientMarketGood const*>::iterator It = Goods->begin(); It != Goods->end();
         ++It, ++Index)
    {
        FClientMarketGood const* const Entry = *It;
        bool bSpecial = false;
        int Class = 0;

        // The price as an unsigned 32-bit value, rounded to float.
        int const PackagePrice = GetMarketPackagePrice(Entry->Info.GoodId); // FUN_0047f040
        float const Price = static_cast<float>(static_cast<unsigned>(PackagePrice));

        NnDealerSetCanBuyByte(Entry, CanBuyGood(Entry->Info.GoodId));

        // The class and the flag.
        Archetype::Equipment const* const Equip =
            Archetype::GetEquipment(Good2Arch(Entry->Info.GoodId));
        if (Equip != nullptr)
        {
            Class = static_cast<int>(Equip->get_class_type()); // vt+0x10
            if (Class == Archetype::AClassGun) // 0x11
            {
                // A gun is special when it is an auto turret (+0xba), or when its projectile
                // (+0x98, not IsEmpty, GetEquipmentByName) is a munition (0x1f) with a seeker
                // (+0x90 != 0).
                Archetype::Gun const* const Gun = static_cast<Archetype::Gun const*>(Equip);
                if (Gun->bAutoTurret)
                    bSpecial = true;
                else if (!Gun->ProjectileArchetype.IsEmpty())
                {
                    Archetype::Equipment const* const Projectile =
                        Archetype::GetEquipmentByName(Gun->ProjectileArchetype);
                    if (Projectile != nullptr &&
                        Projectile->get_class_type() == Archetype::AClassMunition &&
                        static_cast<Archetype::Munition const*>(Projectile)->Seeker != 0)
                        bSpecial = true;
                }
            }
            else if (Class == Archetype::AClassMunition) // 0x1f
            {
                // A munition is special when its hp type (+0xa8) is 0xa or 0x23.
                unsigned const HpType = static_cast<Archetype::Munition const*>(Equip)->HpType;
                if (HpType == 0xa || HpType == 0x23)
                    bSpecial = true;
            }
        }

        Records[Index].Entry = Entry;
        Records[Index].Class = Class;
        Records[Index].bSpecial = bSpecial ? 1 : 0;
        Records[Index].Price = Price;
    }

    // Bubble passes until one makes no swap. A pair swaps when the first cannot be bought and the second can; with equal bytes,
    // when the first's class is greater (JG), or with equal classes its flag is greater (JG),
    // or with equal flags its price is greater (x87: less, equal and unordered keep the order).
    bool bSorted = false;
    while (!bSorted)
    {
        bSorted = true;
        for (int Pair = 0; Pair < Count - 1; ++Pair)
        {
            FNnDealerEquipSortRecord& A = Records[Pair];
            FNnDealerEquipSortRecord& B = Records[Pair + 1];
            unsigned char const CanBuyA = NnDealerCanBuyByte(A.Entry);
            unsigned char const CanBuyB = NnDealerCanBuyByte(B.Entry);
            bool bSwap = false;
            if (CanBuyA == 0 && CanBuyB != 0)
                bSwap = true;
            else if (CanBuyA == CanBuyB)
            {
                if (A.Class > B.Class)
                    bSwap = true;
                else if (A.Class == B.Class)
                {
                    if (A.bSpecial > B.bSpecial)
                        bSwap = true;
                    else if (A.bSpecial == B.bSpecial &&
                             static_cast<double>(A.Price) > static_cast<double>(B.Price))
                        bSwap = true;
                }
            }
            if (bSwap)
            {
                FNnDealerEquipSortRecord const Saved = A;
                A = B;
                B = Saved;
                bSorted = false;
            }
        }
    }

    // Every node unlinked and freed, one node per record appended in order;
    // the record buffer freed.
    Goods->clear();
    for (int Out = 0; Out < Count; ++Out)
        Goods->push_back(Records[Out].Entry);
}

int NnDealer::GetMaxBuyCount(unsigned GoodId, int* OutReason)
{
    // FUN_004837a0. Caller: Update.
    *OutReason = 0;
    if (GetPlayerShipArchetypeIdRetail() == 0) // FUN_004c3e10
    {
        *OutReason = 3;
        return 0;
    }
    if (GoodId == 0)
        return 0;

    // FUN_0043a510(DAT_0066873c) on DAT_00668708 (not null-tested) and its
    // entry FUN_00437d90(GoodId); no entry, or none in stock (+0x1c <= 0) -- 0.
    FClientMarketGood const* const Entry =
        ClientMarketFindEntry(GetClientLocationBaseMarket(GetClientLocationCurrentBase()), GoodId);
    if (Entry == nullptr || Entry->Stock <= 0)
        return 0;

    // n = credits / the package price (unsigned; a zero price faults as in retail); n <= 0
    // (signed) -- reason 2.
    unsigned const PackagePrice =
        static_cast<unsigned>(GetMarketPackagePrice(Entry->Info.GoodId)); // FUN_0047f040
    int Count = static_cast<int>(GetPlayerCreditsRetail() / PackagePrice); // FUN_004c4ce0
    if (Count <= 0)
    {
        *OutReason = 2;
        return 0;
    }

    // With a partner, refresh its hardpoint hint (+0x394) when it is empty:
    // clear it, then for this window's weapons / equipment filters (+0x32c 0 or 2) the
    // hardpoint FUN_00585f20(selected good, 0) finds. A named hardpoint means one at a time:
    // min(n, min(stock, 1)).
    if (Inventory != nullptr)
    {
        if (Inventory->PendingMountHardpoint.Value[0] == '\0')
        {
            Inventory->PendingMountHardpoint.clear();
            unsigned const Selected = GetSelectedMarketGood(); // FUN_00483580
            if (Selected != 0 && (Mode == 0 || Mode == 2))
            {
                char const* const Hardpoint =
                    Inventory->Ship.ShipView->FindHardpointForGood(Selected, false);
                if (Hardpoint != nullptr)
                    Inventory->PendingMountHardpoint.Value = StringAlloc(Hardpoint, false);
            }
        }
        if (Inventory->PendingMountHardpoint.Value[0] != '\0')
        {
            int Cap = Entry->Stock;
            if (Cap > 1)
                Cap = 1;
            return Count >= Cap ? Cap : Count;
        }
    }

    // The hold cap -- (hold size FUN_004c4610 - used FUN_004c4640) / the good's volume
    // (Equipment +0x5c) when Good2Arch and GetEquipment answer and the volume is not 0.0 (x87:
    // only "equal" takes the stock branch); otherwise the stock. ftol either way.
    unsigned const ArchId = Good2Arch(Entry->Info.GoodId);
    double HoldValue = static_cast<double>(Entry->Stock);
    if (ArchId != 0)
    {
        Archetype::Equipment const* const Equip = Archetype::GetEquipment(ArchId);
        if (Equip != nullptr && static_cast<double>(Equip->Volume) != 0.0)
        {
            float const HoldSize = GetPlayerShipHoldSizeRetail(); // FUN_004c4610
            float const Used =
                GetPlayerEquipDescListRetail().get_cargo_space_occupied(); // FUN_004c4640
            HoldValue = (static_cast<double>(HoldSize) - static_cast<double>(Used)) /
                        static_cast<double>(Equip->Volume);
        }
    }
    int const HoldCap = static_cast<int>(HoldValue); // ftol, [ESP+0x14]

    // n = min(n, hold cap, stock), at least 0.
    if (Count >= HoldCap)
        Count = HoldCap;
    if (Count >= Entry->Stock)
        Count = Entry->Stock;
    if (Count < 0)
        Count = 0;

    // The partner's weapons / equipment / cargo filters (+0x32c 0, 2, 3;
    // the partner is not null-tested from here on) take one at a time, except the nanobots
    // (DAT_00674a8c) and shield batteries (DAT_00674a88) under the cargo filter, which are
    // capped by the player ship's limit (Ship +0x140 / +0x144, a negative limit caps nothing)
    // less the count already carried (find_matching_cargo(arch, 0, 1.0f) on the partner's
    // loadout); none left -- reason 9 / 10 and n = 0.
    int const PartnerMode = Inventory->Mode;
    if (PartnerMode == 0 || PartnerMode == 2 || PartnerMode == 3)
    {
        bool const bPackedGood =
            PartnerMode == 3 && (ArchId == G_NanobotArchId || ArchId == G_ShieldBatteryArchId);
        if (!bPackedGood && Count > 1)
            Count = 1;

        if (bPackedGood)
        {
            Archetype::Ship const* const PlayerShipArch =
                Archetype::GetShip(GetPlayerShipArchetypeIdRetail()); // FUN_004c3e10
            if (PlayerShipArch != nullptr)
            {
                if (ArchId == G_NanobotArchId)
                {
                    EquipDesc const* const Carried =
                        Inventory->Ship.Loadout.find_matching_cargo(G_NanobotArchId, 0, 1.0f);
                    int const CarriedCount = Carried != nullptr ? Carried->Count : 0;
                    int const Limit = PlayerShipArch->NanobotLimit; // retail +0x140
                    int const Room = Limit - CarriedCount;
                    if (Count > Room && Limit >= 0)
                        Count = Room;
                    if (Count <= 0)
                    {
                        *OutReason = 9;
                        Count = 0;
                    }
                }
                else if (ArchId == G_ShieldBatteryArchId)
                {
                    EquipDesc const* const Carried =
                        Inventory->Ship.Loadout.find_matching_cargo(G_ShieldBatteryArchId, 0, 1.0f);
                    int const CarriedCount = Carried != nullptr ? Carried->Count : 0;
                    int const Limit = PlayerShipArch->ShieldBatteryLimit; // retail +0x144
                    int const Room = Limit - CarriedCount;
                    if (Count > Room && Limit >= 0)
                        Count = Room;
                    if (Count <= 0)
                    {
                        *OutReason = 10;
                        Count = 0;
                    }
                }
            }
        }
    }

    // The ammo filter (partner +0x32c == 1), with no reason yet: capped by
    // MAX_PLAYER_AMMO less the count carried; none left -- reason 11, 0. A reason already set
    // returns n as it stands.
    if (Inventory->Mode == 1)
    {
        if (*OutReason != 0)
            return Count;
        int Room = MAX_PLAYER_AMMO;
        EquipDesc const* const Carried = Inventory->Ship.Loadout.find_matching_cargo(ArchId, 0, 1.0f);
        if (Carried != nullptr)
            Room -= Carried->Count;
        if (Count > Room)
            Count = Room;
        if (Count <= 0)
        {
            *OutReason = 11;
            return 0;
        }
    }

    // A reason, or a positive hold cap, returns n; otherwise reason 1, 0.
    if (*OutReason != 0)
        return Count;
    if (HoldCap > 0)
        return Count;
    *OutReason = 1;
    return 0;
}

void NnDealer::RestorePlayerShipView()
{
    // FUN_00482040.
    bPreviewingShip = 0;
    if (Inventory == nullptr)
        return;

    ContinueText->SetProperty(kTextPropSetIds, 0x461, 0);
    ContinueButton->SetProperty(kWinPropToolTip, 0x330, 0);
    BuyText->SetProperty(kTextPropSetIds, 0x48a, 0);
    SellText->SetProperty(kTextPropSetIds, 0x48a, 0); // +0x9a8

    // +0x9fc = FUN_004c6e90(partner ship arch (+0x348), 1), the value of
    // the partner's ship at this base.
    PlayerShipTradeInValue = NnShipResaleValue(Inventory->Ship.ShipArchId, true);

    // The partner shows this dealer's ship and loadout.
    Inventory->Ship.ShipArchId = Ship.ShipArchId;
    Inventory->Ship.LoadShip(&Ship.Loadout, 1);
    Inventory->Ship.ShipView->BuildHardpointList(); // FUN_00586b70

    // Tail jump to FUN_0047af30 on the partner.
    Inventory->TransferEquipmentFromDealer();
}

void NnDealer::ShowShipPackage(unsigned GoodId)
{
    // FUN_00483d40 (RET 4). Only the ship dealer (trade mode 2) shows packages.
    if (TradeMode != kTradeModeShip)
        return;

    // No ship until the package resolves; the partner's filter mode.
    Ship.ShipArchId = 0;
    Mode = Inventory->Mode;
    GoodInfo const* const Package = GoodList::find_by_id(GoodId);
    if (Package == nullptr)
        return;

    // Remember the package, show its hull (the hull good is not
    // null-tested) with the package's first addon list (GoodInfo +0x94).
    ShipPackageGoodId = static_cast<int>(Package->Id);
    GoodInfo const* const Hull = GoodList::find_by_id(Package->HullGoodId);
    Ship.ShipArchId = Hull->ShipArchId;
    // Retail hands the GoodInfo's list itself to FUN_004c62b0; the port's GoodInfo keeps the
    // addons in a vector (common_goods.h), so they are copied into an EquipDescList first.
    EquipDescList Addons;
    Addons.Items.assign(Package->AddonLow.begin(), Package->AddonLow.end());
    Ship.LoadShip(&Addons, 0);

    // +0x9f8 = ftol(the hull's price in the market of the base the player
    // is at): FUN_0043a510(DAT_0066873c) on DAT_00668708 (null-tested), then
    // FUN_00438060(Hull->Id).
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    if (Market != nullptr)
        ShipHullPrice = static_cast<int>(ClientMarketGetPrice(Market, Hull->Id));
}

unsigned NnDealer::GetSelectedMarketGood()
{
    // FUN_00483580. A selection, and a commodity or equipment dealer.
    if (SelectedIndex == -1)
        return 0;
    if (TradeMode != kTradeModeCommodity && TradeMode != kTradeModeEquipment)
        return 0;

    // No room loaded (DAT_00668740) -- 0.
    if (GetClientLocationCurrentRoom() == 0)
        return 0;

    // FUN_0043a510(DAT_0066873c) on DAT_00668708 (not null-tested); its
    // goods of category TradeMode == kTradeModeEquipment (FUN_00437f70) into a local list, ordered by
    // FUN_00483230 (trade mode 3) or FUN_00482f10 -- the same list RefreshMarketList shows.
    FClientMarket* const Market = GetClientLocationBaseMarket(GetClientLocationCurrentBase());
    std::list<FClientMarketGood const*> Goods;
    ClientMarketCollectCategory(Market, &Goods, TradeMode == kTradeModeEquipment ? 1u : 0u);
    if (TradeMode == kTradeModeEquipment)
        SortEquipmentMarketGoods(&Goods); // FUN_00483230
    else
        SortMarketGoodsByName(&Goods);    // FUN_00482f10

    // Count the entries in stock (+0x1c > 0) -- in trade mode 3 only those whose
    // NnItemFilterMode(Good2Arch(good)) is this window's filter (+0x32c) -- and return the good
    // id of the SelectedIndex-th; 0 when the list runs out.
    int Row = 0;
    for (std::list<FClientMarketGood const*>::iterator It = Goods.begin(); It != Goods.end(); ++It)
    {
        FClientMarketGood const* const Entry = *It;
        if (Entry->Stock <= 0)
            continue;
        if (TradeMode == kTradeModeEquipment && NnItemFilterMode(Good2Arch(Entry->Info.GoodId)) != Mode)
            continue;
        if (Row == SelectedIndex)
            return Entry->Info.GoodId;
        ++Row;
    }
    return 0;
}

float NnDealer::GetBaseFactionFeeling()
{
    // FUN_00483df0 (cdecl).
    Universe::ISystem const* const System =
        Universe::get_system(GetPlayerStarSystemIdRetail()); // FUN_004c4830
    if (System == nullptr)
        return 1.0f; // DAT_005c75dc

    // The system object the player's base docks with.
    unsigned const BaseId = GetPlayerBaseIdRetail(); // *FUN_004c4900 = DAT_00673358
    Universe::FSystemObject Object;
    unsigned const Count = (unsigned)System->GetObjectCount(); // vt+0x3c
    for (unsigned Index = 0; Index < Count; ++Index)
    {
        if (!System->GetSystemObject(Index, &Object)) // vt+0x40
            continue;
        if (Object.DockWith != BaseId)
            continue;

        // The feeling of the player's group towards the object's
        // reputation group; a non-zero result means none, 1.0.
        int const PlayerGroup = GetPlayerRepVibeRetail(); // FUN_004c4670, DAT_0061226c
        TString<16> GroupName;
        GroupName = Object.Reputation.Data;
        unsigned const Group = Reputation::get_id(GroupName);
        float Feeling = 0.0f;
        if (Reputation::Vibe::GetGroupFeelingsTowards(PlayerGroup, Group, Feeling) != 0)
            return 1.0f;
        return Feeling;
    }
    return 1.0f;
}

void NnDealer::ApplyPlayerStatsFeeling(int Count, FPlayerStatsRepEntry const* Table)
{
    // FUN_00483f30 (cdecl; the only caller is FUN_004a8e40).
    NnDealer* const Dealer = static_cast<NnDealer*>(Win3DFindByName("NN_Dealer"));
    if (Dealer == nullptr)
        return;
    Universe::ISystem const* const System =
        Universe::get_system(GetPlayerStarSystemIdRetail()); // FUN_004c4830
    if (System == nullptr)
        return;

    // The first system object whose dock_with (+0x80) is the player's base; an index Slot16
    // refuses is passed over. The count is unsigned.
    unsigned const BaseId = GetPlayerBaseIdRetail(); // *FUN_004c4900
    Universe::FSystemObject Object;
    unsigned const ObjectCount = (unsigned)System->GetObjectCount(); // vt+0x3c
    unsigned Index = 0;
    for (;;)
    {
        if (Index >= ObjectCount)
            return;
        if (System->GetSystemObject(Index, &Object) && Object.DockWith == BaseId) // vt+0x40
            break;
        ++Index;
    }

    // Retail calls FUN_004c4670 and drops its result; it only reads DAT_0061226c, so the call
    // has no effect and is not ported. The nickname's length is clamped to 15.
    TString<16> GroupName;
    GroupName = Object.Reputation.Data;
    // -1 ends the call.
    unsigned const Group = Reputation::get_id(GroupName);
    if (Group == 0xffffffffu)
        return;

    // The first entry with that group id (signed count) hands
    // its value to the dealer's +0x990.
    for (int Entry = 0; Entry < Count; ++Entry)
    {
        if (Table[Entry].GroupId == Group)
        {
            Dealer->BaseFeeling = Table[Entry].Feeling;
            return;
        }
    }
}
