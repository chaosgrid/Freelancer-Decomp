// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// NN_Trade -- see nn_trade.h. Retail freelancer.exe 0x004bfb60..0x004c3908 plus the static
// initializers LAB_004be670 / LAB_004be690 / LAB_004be6a0.

#include "NeuroNet/nn_trade.h"

#include "Interface/cargo_bar_control.h"      // CTradeEquipListLine::HealthBar
#include "NeuroNet/Chat/chat_init.h"              // ChatPlaceMsgWinForTrade (FUN_0046b450)
#include "Interface/Pages/character_select_equip.h" // GetPlayerEquipDescListRetail (FUN_004c4060),
                                    // FindPlayerNonMissionEquipRetail (FUN_004c45d0),
                                    // GetPlayerCharacterNameRetail (DAT_006732f0)
#include "Space/client_gameobj.h"         // FClientGameobj -> IObjRW
#include "Common/Graphics/client_mesh_load.h"       // ClientMeshHolderHasInstance (FUN_00420d60)
#include "Space/client_objdb.h"           // ClientObjDb_FindObject (FUN_005416c0)
#include "Core/globals.h"                // GetClientFrameTimeCapped (FUN_0042d680)
#include "Space/HUD/hud_status_core.h"        // GetHudStatus (DAT_00674aa0), G_HudTargetPanel (DAT_00674b04)
#include "Space/HUD/hud_target.h"             // HudShipOfObjRw (FUN_00543320), HudTargetBase
#include "Common/Fonts/ids_rdl.h"                // LoadIdsIntoRdl (FUN_0057da40), RdlAppendNode
#include "Network/login_mgr.h"              // G_Server (DAT_0067ecd0)
#include "NeuroNet/nn_button_control.h"
#include "NeuroNet/nn_inventory.h"           // G_NanobotArchId (DAT_00674a8c), G_ShieldBatteryArchId (DAT_00674a88)
#include "NeuroNet/nn_messaging.h"           // NnVoiceEnqueue (FUN_00489e80)
#include "NeuroNet/nn_static_control.h"      // StaticControl +0x348 / +0x410 / +0x414
#include "NeuroNet/nn_text_control.h"
#include "Space/player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "Space/player_spawn.h"           // GetPlayerConnectionId (DAT_00673344),
                                    // GetPlayerShipHoldSizeRetail (FUN_004c4610)
#include "Interface/rich_text_win.h"
#include "Interface/scroll_control.h"
#include "Interface/ship_view_control.h"
#include "Space/star_sphere.h"            // Win3DIdentityRotation3x3, Win3DBindModelview (FUN_00422690)
#include "Interface/win3d.h"
#include "Interface/win3d_immediate.h"        // Win3DBegin / Win3DVertex3f / Win3DEnd / ... (OpenGL.cpp)
#include "Interface/win3d_renderables.h"      // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "Interface/win_factory.h"

#include "DALib.h"
#include "dacom.h"
#include "render/IRenderPipeline.h"
#include "render/rendcomp.h"

#include "common_archetype.h"
#include "common_cobject.h"         // CShip
#include "common_goods.h"
#include "common_id.h"              // CreateID
#include "common_server.h"          // IServer
#include "common_text.h"            // RenderDisplayList, StyleNode

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <string>

NnTrade* G_NnTrade = nullptr; // DAT_006724b4

// Retail DAT_0067245c -- WINFACTORY<NN_Trade> static (LAB_004be670 -> FUN_004c37a0; Create
// FUN_004c3810; atexit LAB_004be690 -> FUN_00579380).
WINFACTORY<NnTrade> G_NnTradeFactory("NN_Trade");

namespace
{
    // DAT_00672458 (static init LAB_004be6a0), read by SetMode.
    unsigned const G_InsufficientCargoSpaceVoiceId = CreateID("insufficient_cargo_space");

    // DAT_006728bc (.bss float, 0.0) -- the x of the window's offset; only Create reads it.
    float G_NnTradeOffsetX = 0.0f;
    // DAT_00612188 (.data float) -- the y of the window's offset.
    float G_NnTradeOffsetY = -0.257f;
    // DAT_006728c0 (.bss float) -- the quantity caret's blink timer.
    float G_NnTradeCaretTimer = 0.0f;
    // DAT_0061218c (.data byte, 1) -- the caret is shown.
    unsigned char G_bNnTradeCaretShown = 1;

    // The window's meshes.
    char const kBackgroundMesh[] = "interface\\multiplayer\\trade\\TRADE_background.cmp";
    char const kTitleBoxMesh[] = "Interface\\multiplayer\\trade\\TRADE_tradetitlebox.3db";
    char const kTitleArrowMesh[] = "Interface\\multiplayer\\trade\\TRADE_transactiondirection.3db";
    char const kCargoFullMesh[] = "Interface\\multiplayer\\trade\\TRADE_cargofull.3db";
    char const kCargoEmptyMesh[] = "Interface\\multiplayer\\trade\\TRADE_cargoempty.3db";
    char const kQuantityBoxMesh[] = "Interface\\multiplayer\\trade\\TRADE_quantitybox.3db";
    char const kExecuteMesh[] = "interface\\multiplayer\\trade\\TRADE_executetransaction.3db";
    char const kCloseMesh[] = "interface\\multiplayer\\trade\\TRADE_closeinterfacex.3db";
    char const kQuantityBarMesh[] = "interface\\multiplayer\\trade\\TRADE_quantityscrollbar.3db";
    char const kRightArrowMesh[] = "interface\\multiplayer\\trade\\TRADE_rightarrow.3db";
    char const kLeftArrowMesh[] = "interface\\multiplayer\\trade\\TRADE_leftarrow.3db";
    char const kQuantityHandleMesh[] = "interface\\multiplayer\\trade\\TRADE_quantityscrollhandle.3db";
    char const kAcceptTradeMesh[] = "interface\\multiplayer\\trade\\TRADE_accepttradebox.3db";
    char const kInfoFrameMesh[] = "Interface\\multiplayer\\trade\\TRADE_infoframe.3db";

    // PTR_DAT_005d0ec4 -- L"%d"; DAT_005d6b44 -- L"Target Name".
    wchar_t const kQuantityFormat[] = L"%d";
    wchar_t const kTargetNamePlaceholder[] = L"Target Name";

    // What an unlimited (-1) nanobot / battery limit becomes.
    int const kUnlimitedAmmo = 50000000;

    uintptr_t PointerArg(void const* Pointer)
    {
        return reinterpret_cast<uintptr_t>(Pointer);
    }

    // FUN_004a1ad0 (thiscall, RET 0x10) -- four bytes in argument order; the trade window stores
    // the result as the TextControl's +0x348 dword.
    unsigned long TradeColour(unsigned char B0, unsigned char B1, unsigned char B2,
                              unsigned char B3)
    {
        return static_cast<unsigned long>(B0) | (static_cast<unsigned long>(B1) << 8) |
               (static_cast<unsigned long>(B2) << 16) | (static_cast<unsigned long>(B3) << 24);
    }

    NnTextControl* AsText(WinControl* Control)
    {
        return static_cast<NnTextControl*>(Control);
    }

    // The cargo bars come from WinControl::CreateStaticControl (FUN_005a0db0), which returns the
    // StaticControl it made.
    NnStaticControl* AsStatic(WinControl* Control)
    {
        return static_cast<NnStaticControl*>(Control);
    }

    // Port-only helper for the colours Create stores twice (0x004c0a15, 0x004c0ba7).
    void SetCargoBarColours(NnStaticControl* Bar)
    {
        Bar->MeshColour[0] = 0xff;
        Bar->MeshColour[1] = 0;
        Bar->MeshColour[2] = 0;
        Bar->MeshColour[3] = 0xc0;
        Bar->WireColour[0] = 0xff;
        Bar->WireColour[1] = 0;
        Bar->WireColour[2] = 0;
        Bar->WireColour[3] = 0xc0;
    }

    // Byte +0 of a list entry is the trade selection mark (retail writes it as a byte; the port's
    // EquipDesc keeps the word +0 as Cached).
    void SetTradeMark(EquipDesc* Item, unsigned char bMarked)
    {
        Item->Cached = static_cast<unsigned short>((Item->Cached & 0xff00) | bMarked);
    }

    // Port-only helper for the scroll range and Refresh(-1) FUN_004c2220 / FUN_004c2380 /
    // FUN_004c2d50 inline (e.g. 0x004c22a4).
    void RefreshTradeListRange(CTradeEquipList* List)
    {
        int Range = static_cast<int>(List->Items.Items.size()) - List->VisibleRows;
        if (Range < 0)
            Range = 0;
        List->ScrollBar->SetProperty(kScrollPropSetRange, 0, static_cast<uintptr_t>(Range));
        List->Refresh(-1);
    }

    // FUN_004bff30 (cdecl) -- blink a '|' caret at the end of Text every half second.
    void BlinkQuantityCaret(NnTextControl* Text)
    {
        // The sum is compared before it is rounded to the float.
        double const Timer = GetClientFrameTimeCapped() + static_cast<double>(G_NnTradeCaretTimer);
        G_NnTradeCaretTimer = static_cast<float>(Timer);
        if (Timer > 0.5) // DAT_005c75e8
        {
            G_bNnTradeCaretShown = (G_bNnTradeCaretShown == 0) ? 1 : 0;
            G_NnTradeCaretTimer = 0.0f;
        }

        // Nothing to do when the text is empty or already agrees.
        wchar_t const* const Current = Text->GetFontString()->Text; // FUN_0058a520
        int const Last = static_cast<int>(wcslen(Current)) - 1;
        if (Last < 0)
            return;
        unsigned char const bHasCaret = (Current[Last] == L'|') ? 1 : 0;
        if (bHasCaret == G_bNnTradeCaretShown)
            return;

        // A copy with the '|' appended or its last character cut, through FUN_0058a530.
        std::wstring Copy(Current);
        if (G_bNnTradeCaretShown != 0)
            Copy.append(L"|");
        else
            Copy.resize(Copy.size() - 1);
        Text->SetText(Copy.c_str());
    }

    // Port-only helper for Draw's two bars (0x004c01a9, 0x004c0362): the dark quad over the part
    // of a cargo bar right of X in the bar's frame, then the bar's own mesh.
    void DrawCargoFill(WinControl* Bar, float X)
    {
        IRenderPipeline* const Pipe = DALib::Pipeline;
        Pipe->SetRenderState(D3DRS_ZENABLE, 1);
        Pipe->SetRenderState(D3DRS_ZFUNC, 8);
        Pipe->SetRenderState(D3DRS_ZWRITEENABLE, 1);
        Transform Transform;
        memcpy(&Transform, DALib::Engine->GetInstanceTransform(Bar->MeshHolder.EngineInstance),
               sizeof(Transform)); // IEngine +0xa0
        Win3DBindModelview(nullptr, &Transform); // FUN_00422690(0, &Transform)
        Win3DDisable(kWin3DTexture2D);          // FUN_00421ed0(0xde1)
        Win3DEnable(kWin3DBlend);               // FUN_00421dd0(0xbe2)
        Win3DSetBlendFunc(2, 4);                // FUN_004220c0
        Win3DColor4ub(0, 0, 0, 0x0a);           // FUN_00421b70
        Win3DBegin(kWin3DQuads);                // FUN_00421670(8)
        float const HalfX = Bar->HalfExtents[0];
        float const HalfY = Bar->HalfExtents[1];
        Win3DVertex3f(HalfX, HalfY, 0.0f);      // FUN_00421cc0
        Win3DVertex3f(HalfX, -HalfY, 0.0f);
        Win3DVertex3f(X, -HalfY, 0.0f);
        Win3DVertex3f(X, HalfY, 0.0f);
        Win3DEnd();                             // FUN_00421690
        Pipe->SetRenderState(D3DRS_ZFUNC, 4);
        DALib::Renderer->RenderInstance(Win3DGetCamera(), Bar->MeshHolder.EngineInstance, 1.0f, 0, nullptr,
                               nullptr); // IRenderer2 +0x1c, &DAT_0067dbf8
        Win3DDisable(kWin3DBlend);               // FUN_00421ed0(0xbe2)
    }

    // Kept at full precision and rounded to a float at the vertex.
    float CargoFillX(WinControl const* Bar, double Ratio)
    {
        double X = static_cast<double>(Bar->HalfExtents[0]) * Ratio;
        X = X + X;
        X = X - static_cast<double>(Bar->HalfExtents[0]);
        return static_cast<float>(X);
    }

    // The CShip of the other player's object, or of none (FUN_005416c0(Id, 1) + FUN_00543320).
    FClientGameobj* FindTradeObject(unsigned ObjectId)
    {
        return ClientObjDb_FindObject(ObjectId, 1);
    }
}

bool NnTradeIsOpen()
{
    return G_NnTrade != nullptr;
}

// ---------------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------------

NnTrade::NnTrade()
    : TargetList(nullptr)
    , CenterList(nullptr)
    , PlayerList(nullptr)
    , QuantityLine(nullptr)
    , TradeClose(nullptr)
    , Field344(0)
    , QuantityTitle(nullptr)
    , QuantityBar(nullptr)
    , QuantityFrame(nullptr)
    , QuantityAccept(nullptr)
    , QuantityReject(nullptr)
    , QuantityText(nullptr)
    , bQuantityTyped(0)
    , Quantity(0)
    , PlayerCardFrame(nullptr)
    , PlayerCardClose(nullptr)
    , PlayerCardModel(nullptr)
    , PlayerCardTitle(nullptr)
    , PlayerCardInfo(nullptr)
    , TargetCardFrame(nullptr)
    , TargetCardClose(nullptr)
    , TargetCardModel(nullptr)
    , TargetCardTitle(nullptr)
    , TargetCardInfo(nullptr)
    , Mode(0)
    , PlayerName(nullptr)
    , TargetName(nullptr)
    , TitleFrame(nullptr)
    , TitleArrow(nullptr)
    , TitleText(nullptr)
    , Field3ac(0)
    , Field3b0(0)
    , PlayerCargoText(nullptr)
    , PlayerCargoFull(nullptr)
    , PlayerCargoEmpty(nullptr)
    , PlayerCargoUsed(0.0)
    , PlayerNanobots(0)
    , PlayerNanobotLimit(0)
    , PlayerBatteries(0)
    , PlayerBatteryLimit(0)
    , TargetCargoText(nullptr)
    , TargetCargoFull(nullptr)
    , TargetCargoEmpty(nullptr)
    , TargetCargoUsed(0.0)
    , TargetHoldSize(0.0)
    , TargetNanobots(0)
    , TargetNanobotLimit(0)
    , TargetBatteries(0)
    , TargetBatteryLimit(0)
    , NanobotsOffered(0)
    , BatteriesOffered(0)
    , PlayerAcceptedText(nullptr)
    , TargetAcceptedText(nullptr)
    , Byte418(0)
    , Byte419(0)
    , AcceptTradeFrame(nullptr)
    , AcceptTradeText(nullptr)
    , SelectedLine(nullptr)
    , SelectedItem(nullptr)
    , TargetObjectId(0xffffffffu)
    , TargetOwnerPlayer(0)
    , bHudTargetWasExpanded(0)
    , bHudStatusWasExpanded(0)
{
    // FUN_004bfb60. +0x394, +0x3e4, +0x438 and +0x43c are left as allocated.
    G_NnTrade = this;
}

NnTrade::~NnTrade()
{
    // FUN_004bfd40 (scalar deleting FUN_004bfd20).
    if (TargetList != nullptr)
    {
        delete TargetList;
        TargetList = nullptr;
    }
    if (CenterList != nullptr)
    {
        delete CenterList;
        CenterList = nullptr;
    }
    if (PlayerList != nullptr)
    {
        delete PlayerList;
        PlayerList = nullptr;
    }
    G_NnTrade = nullptr;
}

bool NnTrade::OnEvent(unsigned MsgId)
{
    // LAB_0043d8e0.
    (void)MsgId;
    return true;
}

bool NnTrade::GetHardpointTransform(char const* Name, Transform* OutXform)
{
    // LAB_004bfe00 (RET 8).
    if (ClientMeshHolderHasInstance(&MeshHolder)) // FUN_00420d60
    {
        const ArchetypeIndex Archetype = DALib::Engine->GetInstanceArchetype(MeshHolder.EngineInstance);
        DALib::Engine->ReleaseHandle(Archetype);
        FHardpointKey Key;
        Key.Name = Name;
        Key.Crc = DACOM_CRC::GetCRC32(Name);
        unsigned char Blob[0x48];
        if (static_cast<unsigned char>(DALib::Hardpoint->GetHardpoint(Archetype, &Key, Blob)) != 0)
        {
            // Blob +0x10 orientation, +0x04 position; this window's +0xa8 is added, then the z
            // translation is overwritten with 0.
            Transform Local;
            memcpy(&Local.Rotation, Blob + 0x10, sizeof(Local.Rotation));
            memcpy(&Local.Translation, Blob + 0x04, sizeof(Local.Translation));
            memcpy(OutXform, &Local, sizeof(Local));
            OutXform->Translation.x = Offset[0] + OutXform->Translation.x;
            OutXform->Translation.y = Offset[1] + OutXform->Translation.y;
            OutXform->Translation.z = 0.0f;
            return true;
        }
    }

    // Identity, no translation, no offset.
    OutXform->Rotation.E[2][2] = 1.0f;
    OutXform->Rotation.E[1][1] = 1.0f;
    OutXform->Rotation.E[0][0] = 1.0f;
    OutXform->Rotation.E[2][1] = 0.0f;
    OutXform->Rotation.E[2][0] = 0.0f;
    OutXform->Rotation.E[1][2] = 0.0f;
    OutXform->Rotation.E[1][0] = 0.0f;
    OutXform->Rotation.E[0][2] = 0.0f;
    OutXform->Rotation.E[0][1] = 0.0f;
    OutXform->Translation.z = 0.0f;
    OutXform->Translation.y = 0.0f;
    OutXform->Translation.x = 0.0f;
    return false;
}

bool NnTrade::Create(Win3D* NewParent, void* Data)
{
    // LAB_004c04f0 (RET 8).
    if (!Win3D::Create(NewParent, Data)) // FUN_004b5060
        return false;

    // What the player carries of nanobots and shield batteries.
    EquipDesc* const Nanobots = FindPlayerNonMissionEquipRetail(G_NanobotArchId);    // FUN_004c45d0
    EquipDesc* const Batteries = FindPlayerNonMissionEquipRetail(G_ShieldBatteryArchId);
    if (Nanobots != nullptr)
        PlayerNanobots = Nanobots->Count;
    if (Batteries != nullptr)
        PlayerBatteries = Batteries->Count;

    // The player's ship (FUN_00543320 inlined) and its limits.
    CShip* const Ship = HudShipOfObjRw(static_cast<IObjRW*>(GetPlayerObjRW())); // FUN_0054baf0
    if (Ship != nullptr)
    {
        Archetype::Ship const* const ShipArch = Ship->shiparch(); // CObject +0x88
        PlayerNanobotLimit = ShipArch->NanobotLimit;
        PlayerBatteryLimit = ShipArch->ShieldBatteryLimit;
        if (PlayerBatteryLimit == -1)
            PlayerBatteryLimit = kUnlimitedAmmo;
        if (PlayerNanobotLimit == -1)
            PlayerNanobotLimit = kUnlimitedAmmo;
    }
    PlayerShip = Ship;
    PlayerCargoUsed = GetPlayerEquipDescListRetail().get_cargo_space_occupied(); // FUN_004c4640

    Offset[0] = G_NnTradeOffsetX;
    Offset[1] = G_NnTradeOffsetY;
    Offset[2] = 0.0f;
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    SetMesh(kBackgroundMesh, 0); // slot 38
    WinControlHalfExtentsFromMesh(HalfExtents, &MeshHolder);

    FormatTradeWidgetName("TitleFrame");
    TitleFrame = CreateStaticControl(G_TradeWidgetName, kTitleBoxMesh, Zero, "hp_title_frame", Zero,
                                     1, 1, 1, 0, 0, 0, 1, 0, 0, 0, Zero);
    TitleFrame->SetProperty(kWinPropHide, 1, 0);

    FormatTradeWidgetName("TitleArrow");
    TitleArrow = CreateStaticControl(G_TradeWidgetName, kTitleArrowMesh, Zero, "hp_title_arrow",
                                     Zero, 1, 1, 0, 1, 0, 0, 1, 0, 0, 0, Zero);

    FormatTradeWidgetName("TitleText");
    TitleText = CreateTextControl(G_TradeWidgetName, "PageHeader", Zero, "hp_title_text", 0.172f,
                                  0.051f, 2.0f, 0x754, 1, 0, 0);

    FormatTradeWidgetName("PlayerName");
    PlayerName = CreateTextControl(G_TradeWidgetName, "TradePlayerName", Zero, "hp_player_name",
                                   0.415f, 0.039f, 2.0f, 0, 1, 0, 0);
    FormatTradeWidgetName("TargetName");
    TargetName = CreateTextControl(G_TradeWidgetName, "TradePlayerName", Zero, "hp_target_name",
                                   0.415f, 0.039f, 2.0f, 0, 1, 0, 0);
    PlayerName->SetProperty(kTextPropSetText, PointerArg(GetPlayerCharacterNameRetail()), 0); // DAT_006732f0
    TargetName->SetProperty(kTextPropSetText, PointerArg(kTargetNamePlaceholder), 0);

    // The two "accepted" texts start as red "not accepted" (ids 0x75a).
    FormatTradeWidgetName("PlayerAcceptedText");
    PlayerAcceptedText = AsText(CreateTextControl(G_TradeWidgetName, "SubHeader", Zero,
                                                  "hp_player_accepted", 0.415f, 0.029f, 1.5f, 0,
                                                  1, 0, 0));
    FormatTradeWidgetName("TargetAcceptedText");
    TargetAcceptedText = AsText(CreateTextControl(G_TradeWidgetName, "SubHeader", Zero,
                                                  "hp_target_accepted", 0.415f, 0.029f, 1.5f, 0,
                                                  1, 0, 0));
    PlayerAcceptedText->SetProperty(kTextPropSetIds, 0x75a, 0);
    PlayerAcceptedText->TextColour = TradeColour(0xff, 0, 0, 0xc0);
    TargetAcceptedText->SetProperty(kTextPropSetIds, 0x75a, 0);
    TargetAcceptedText->TextColour = TradeColour(0xff, 0, 0, 0xc0);

    // The player's full cargo bar, its half extents from the mesh.
    FormatTradeWidgetName("PlayerCargoBarFull");
    PlayerCargoFull = CreateStaticControl(G_TradeWidgetName, kCargoFullMesh, Zero,
                                          "hp_player_cargo_capacity", Zero, 1, 1, 0, 1, 0, 0, 1,
                                          0, 0, 0, Zero);
    WinControlHalfExtentsFromMesh(PlayerCargoFull->HalfExtents, &PlayerCargoFull->MeshHolder);
    SetCargoBarColours(AsStatic(PlayerCargoFull));

    // Its caption, as wide as the bar.
    FormatTradeWidgetName("PlayerCargoText");
    PlayerCargoText = CreateTextControl(G_TradeWidgetName, "SubHeader", Zero, "hp_player_cargo_text",
                                        PlayerCargoFull->HalfExtents[0] + PlayerCargoFull->HalfExtents[0],
                                        0.051f, 1.25f, 0x755, 1, 0, 0);

    // The empty bar under the same name and hardpoint; the full one is hidden.
    FormatTradeWidgetName("PlayerCargoBarFull");
    PlayerCargoEmpty = CreateStaticControl(G_TradeWidgetName, kCargoEmptyMesh, Zero,
                                           "hp_player_cargo_capacity", Zero, 1, 1, 0, 1, 0, 0, 1,
                                           0, 0, 0, Zero);
    PlayerCargoFull->SetProperty(kWinPropHide, 1, 0);

    // The other player's full bar with the same colours, hidden.
    FormatTradeWidgetName("TargetCargoBarFull");
    TargetCargoFull = CreateStaticControl(G_TradeWidgetName, kCargoFullMesh, Zero,
                                          "hp_target_cargo_capacity", Zero, 1, 1, 0, 1, 0, 0, 1,
                                          0, 0, 0, Zero);
    WinControlHalfExtentsFromMesh(TargetCargoFull->HalfExtents, &TargetCargoFull->MeshHolder);
    SetCargoBarColours(AsStatic(TargetCargoFull));
    TargetCargoFull->SetProperty(kWinPropHide, 1, 0);

    // Its caption (retail's hardpoint name is spelt "traget").
    FormatTradeWidgetName("TargetCargoText");
    TargetCargoText = CreateTextControl(G_TradeWidgetName, "SubHeader", Zero, "hp_traget_cargo_text",
                                        TargetCargoFull->HalfExtents[0] + TargetCargoFull->HalfExtents[0],
                                        0.051f, 1.25f, 0x755, 1, 0, 0);

    FormatTradeWidgetName("TargetCargoBarFull");
    TargetCargoEmpty = CreateStaticControl(G_TradeWidgetName, kCargoEmptyMesh, Zero,
                                           "hp_target_cargo_capacity", Zero, 1, 1, 0, 1, 0, 0, 1,
                                           0, 0, 0, Zero);

    // The quantity panel's item row, 0.015 right and 0.032 down (DAT_005d1f04, DAT_005d69c0) of
    // the centre list's hardpoint.
    Transform Centre;
    Win3DIdentityRotation3x3(&Centre.Rotation);
    Centre.Translation.x = 0.0f;
    Centre.Translation.y = 0.0f;
    Centre.Translation.z = 0.0f;
    GetHardpointTransform("hp_center_inventory_list", &Centre);
    float RowAt[3];
    RowAt[0] = Centre.Translation.x + 0.015f;
    RowAt[1] = Centre.Translation.y - 0.032f;
    RowAt[2] = Centre.Translation.z;
    QuantityLine = new CTradeEquipListLine(nullptr); // operator_new(0x2c), ctor inlined
    QuantityLine->Init(this, RowAt);

    FormatTradeWidgetName("QuantTitTxt");
    QuantityTitle = CreateTextControl(G_TradeWidgetName, "ItemName", Zero, "hp_quantity_title",
                                      0.415f, 0.051f, 1.5f, 0, 1, 0, 0);

    FormatTradeWidgetName("QuantFrame");
    QuantityFrame = CreateStaticControl(G_TradeWidgetName, kQuantityBoxMesh, Zero,
                                        "hp_quantity_number_frame", Zero, 1, 1, 1, 0, 0, 0, 1, 0,
                                        0, 0, Zero);
    WinControlHalfExtentsFromMesh(QuantityFrame->HalfExtents, &QuantityFrame->MeshHolder);

    // The number, 0.01 up (DAT_005c99c8), as wide as the box and as high as its half height.
    float const NumberAt[3] = {Zero[0], Zero[1] + 0.01f, Zero[2]};
    FormatTradeWidgetName("QuantText");
    QuantityText = AsText(CreateTextControl(
        G_TradeWidgetName, "ItemName", NumberAt, "hp_quantity_number_frame",
        QuantityFrame->HalfExtents[0] + QuantityFrame->HalfExtents[0], QuantityFrame->HalfExtents[1],
        1.5f, 0, 1, 0, 0));
    SetQuantity(0);

    FormatTradeWidgetName("QuantAccept");
    QuantityAccept = CreateButtonControl(G_TradeWidgetName, kExecuteMesh, kExecuteMesh, nullptr,
                                         nullptr, Zero, "hp_center_add_accept", Zero, 0, 0, 0, 1,
                                         0, 0, 0);
    WinControlHalfExtentsFromMesh(QuantityAccept->HalfExtents, &QuantityAccept->MeshHolder);
    FormatTradeWidgetName("QuantReject");
    QuantityReject = CreateButtonControl(G_TradeWidgetName, kCloseMesh, kCloseMesh, nullptr, nullptr,
                                         Zero, "hp_center_add_cancel", Zero, 0, 0, 0, 1, 0, 0, 0);
    WinControlHalfExtentsFromMesh(QuantityReject->HalfExtents, &QuantityReject->MeshHolder);

    // The quantity bar [0, 100], page 1.
    FormatTradeWidgetName("QuantScrollBar");
    QuantityBar = static_cast<ScrollControl*>(CreateScrollControl(
        G_TradeWidgetName, kQuantityBarMesh, Zero, "hp_center_quantity_bar", Zero, true,
        kRightArrowMesh, Zero, "hp_center_quantity_right", Zero, false, kLeftArrowMesh, Zero,
        "hp_center_quantity_left", Zero, false, kQuantityHandleMesh, Zero, Zero,
        "hp_center_quantity_thumb_left", "hp_center_quantity_thumb_right", Zero, false, 0, 100, 1,
        false, false));
    WinControlHalfExtentsFromMesh(&QuantityBar->Back.MouseSize.x, &QuantityBar->Back.Holder);
    WinControlHalfExtentsFromMesh(&QuantityBar->Next.MouseSize.x, &QuantityBar->Next.Holder);
    WinControlHalfExtentsFromMesh(&QuantityBar->Prev.MouseSize.x, &QuantityBar->Prev.Holder);
    WinControlHalfExtentsFromMesh(&QuantityBar->ThumbMouse.x, &QuantityBar->ThumbHolder);

    // The "accept trade" box (+0x331 bit 0 set) and its caption filling it.
    FormatTradeWidgetName("AcceptTradeFrame");
    AcceptTradeFrame = CreateButtonControl(G_TradeWidgetName, kAcceptTradeMesh, kAcceptTradeMesh,
                                           nullptr, nullptr, Zero, "hp_center_trade_accept", Zero,
                                           1, 0, 0, 1, 0, 0, 0);
    WinControlHalfExtentsFromMesh(AcceptTradeFrame->HalfExtents, &AcceptTradeFrame->MeshHolder);
    FormatTradeWidgetName("AcceptTradeText");
    AcceptTradeText = CreateTextControl(
        G_TradeWidgetName, "ButtonText", Zero, "hp_center_trade_accept",
        AcceptTradeFrame->HalfExtents[0] + AcceptTradeFrame->HalfExtents[0],
        AcceptTradeFrame->HalfExtents[1] + AcceptTradeFrame->HalfExtents[1], 2.0f, 0x756, 1, 0, 0);

    // The player's info card, hidden: frame, close button, a turning ship view (+0x518 bit 7
    // cleared), the item's name and a RichTextWin for the info text.
    float const CardFrameAt[3] = {0.002f, -0.007f, 0.0f};
    FormatTradeWidgetName("PICFrame");
    PlayerCardFrame = CreateStaticControl(G_TradeWidgetName, kInfoFrameMesh, CardFrameAt,
                                          "hp_player_infocard_center", Zero, 0, 1, 1, 0, 0, 0, 1,
                                          0, 0, 0, Zero);
    PlayerCardFrame->SetProperty(kWinPropHide, 1, 0);

    FormatTradeWidgetName("PICClose");
    PlayerCardClose = CreateButtonControl(G_TradeWidgetName, kCloseMesh, kCloseMesh, nullptr,
                                          nullptr, Zero, "hp_player_infocard_close", Zero, 0, 0, 0,
                                          1, 0, 0, 0);
    WinControlHalfExtentsFromMesh(PlayerCardClose->HalfExtents, &PlayerCardClose->MeshHolder);
    PlayerCardClose->SetProperty(kWinPropHide, 1, 0);

    unsigned const NoColour = 0;
    float const ModelViewSize[3] = {0.13f, -0.047f, 0.0f};
    float const ModelYpr[3] = {0.0f, 270.0f, 0.0f};
    float const ModelRotSpeed[3] = {0.0f, 0.0f, 30.0f};
    FormatTradeWidgetName("PICModel");
    PlayerCardModel = static_cast<ShipViewControl*>(CreateShipViewControl(
        G_TradeWidgetName, ModelRotSpeed, Zero, ModelYpr, ModelViewSize, "hp_player_infocard_model",
        false, false, &NoColour, &NoColour));
    PlayerCardModel->SetProperty(kWinPropHide, 1, 0);
    PlayerCardModel->ViewFlags &= 0x7f;

    float const PlayerTitleAt[3] = {0.095f, 0.0125f, 0.0f};
    FormatTradeWidgetName("PICTitle");
    PlayerCardTitle = CreateTextControl(G_TradeWidgetName, "SubHeader", PlayerTitleAt,
                                        "hp_player_infocard_itemname", 0.255f, 0.01f, 1.5f, 0, 0,
                                        0, 0);
    PlayerCardTitle->SetProperty(kWinPropHide, 1, 0);
    PlayerCardTitle->SetProperty(0x22, 0, 1);

    FormatTradeWidgetName("PICInfo");
    PlayerCardInfo = static_cast<RichTextWin*>(
        WinFactoryCreate(G_TradeWidgetName, "RichTextWin", PlayerCardFrame, nullptr));
    PlayerCardInfo->SetNormSize(0.23f, 0.205f);             // FUN_00579cc0
    float const PlayerInfoAt[3] = {-0.124f, 0.0325f, 0.0f};
    PlayerCardInfo->SetAttachOffset(PlayerInfoAt);          // FUN_00579c40
    PlayerCardInfo->Flags &= 0xfc;

    // The other player's info card, the same, a little further right.
    float const TargetFrameAt[3] = {0.01f, -0.007f, 0.0f};
    FormatTradeWidgetName("TICFrame");
    TargetCardFrame = CreateStaticControl(G_TradeWidgetName, kInfoFrameMesh, TargetFrameAt,
                                          "hp_target_infocard_center", Zero, 0, 1, 1, 0, 0, 0, 1,
                                          0, 0, 0, Zero);
    TargetCardFrame->SetProperty(kWinPropHide, 1, 0);

    float const TargetCloseAt[3] = {0.01f, 0.0f, 0.0f};
    FormatTradeWidgetName("TICClose");
    TargetCardClose = CreateButtonControl(G_TradeWidgetName, kCloseMesh, kCloseMesh, nullptr,
                                          nullptr, TargetCloseAt, "hp_target_infocard_close", Zero,
                                          0, 0, 0, 1, 0, 0, 0);
    WinControlHalfExtentsFromMesh(TargetCardClose->HalfExtents, &TargetCardClose->MeshHolder);
    TargetCardClose->SetProperty(kWinPropHide, 1, 0);

    float const TargetModelAt[3] = {0.01f, 0.0f, 0.0f};
    FormatTradeWidgetName("TICModel");
    TargetCardModel = static_cast<ShipViewControl*>(CreateShipViewControl(
        G_TradeWidgetName, ModelRotSpeed, TargetModelAt, ModelYpr, ModelViewSize,
        "hp_target_infocard_model", false, false, &NoColour, &NoColour));
    TargetCardModel->SetProperty(kWinPropHide, 1, 0);
    TargetCardModel->ViewFlags &= 0x7f;

    float const TargetTitleAt[3] = {0.105f, 0.0125f, 0.0f};
    FormatTradeWidgetName("TICTitle");
    TargetCardTitle = CreateTextControl(G_TradeWidgetName, "SubHeader", TargetTitleAt,
                                        "hp_target_infocard_itemname", 0.255f, 0.01f, 1.5f, 0, 0,
                                        0, 0);
    TargetCardTitle->SetProperty(kWinPropHide, 1, 0);
    TargetCardTitle->SetProperty(0x22, 0, 1);

    FormatTradeWidgetName("TICInfo");
    TargetCardInfo = static_cast<RichTextWin*>(
        WinFactoryCreate(G_TradeWidgetName, "RichTextWin", TargetCardFrame, nullptr));
    TargetCardInfo->SetNormSize(0.23f, 0.205f);
    float const TargetInfoAt[3] = {-0.117f, 0.0325f, 0.0f};
    TargetCardInfo->SetAttachOffset(TargetInfoAt);
    TargetCardInfo->Flags &= 0xfc;

    FormatTradeWidgetName("TradeClose");
    TradeClose = CreateButtonControl(G_TradeWidgetName, kCloseMesh, kCloseMesh, nullptr, nullptr,
                                     Zero, "hp_tradecancel", Zero, 0, 0, 0, 1, 0, 0, 0);
    WinControlHalfExtentsFromMesh(TradeClose->HalfExtents, &TradeClose->MeshHolder);

    // The three lists; the player's is filled at once.
    TargetList = new CTradeEquipList(this, "hp_target_inventory_list", "hp_target_scroll_bar",
                                     "hp_target_scroll_up", "hp_target_scroll_down",
                                     "hp_target_thumb_top", "hp_target_thumb_bottom", 5, 1, 0);
    PlayerList = new CTradeEquipList(this, "hp_player_inventory_list", "hp_player_scroll_bar",
                                     "hp_player_scroll_up", "hp_player_scroll_down",
                                     "hp_player_scroll_thumb_top", "hp_player_scroll_thumb_bottom",
                                     5, 1, 1);
    PlayerList->Rebuild(); // FUN_004bf720
    CenterList = new CTradeEquipList(this, "hp_center_inventory_list", "hp_center_scroll_bar",
                                     "hp_center_scroll_up", "hp_center_scroll_down",
                                     "hp_center_thumb_top", "hp_center_thumb_down", 5, 0, 1);

    // Closed, then browsing; drawn above the other windows.
    SetMode(1);
    SetMode(2);
    DrawLayer = 1;

    // The status and target panels are collapsed while trading; what they were is kept for the
    // close (0xef02).
    HUD_Status* const Status = GetHudStatus(); // DAT_00674aa0
    if (Status != nullptr)
    {
        bHudStatusWasExpanded = Status->bExpanded;
        if (bHudStatusWasExpanded != 0)
            Status->ToggleExpanded(); // HUDItem vt+0x30
    }
    HudTargetBase* const Target = G_HudTargetPanel; // DAT_00674b04
    if (Target != nullptr)
    {
        bHudTargetWasExpanded = Target->bExpanded;
        if (bHudTargetWasExpanded != 0)
            Target->ToggleExpanded();
    }

    ChatPlaceMsgWinForTrade(1); // FUN_0046b450(1)
    return true;
}

// ---------------------------------------------------------------------------------------------
// Per frame
// ---------------------------------------------------------------------------------------------

void NnTrade::Draw()
{
    // LAB_004c0120.
    Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0
    BlinkQuantityCaret(QuantityText); // FUN_004bff30
    WinControl::Draw();               // FUN_005a0760

    // The player's bar, shaded right of the used share of the hold.
    if (PlayerCargoFull != nullptr)
    {
        double const Hold = GetPlayerShipHoldSizeRetail(); // FUN_004c4610
        double Ratio;
        if (PlayerCargoUsed > 1e-5) // DAT_005cbfb0
        {
            Ratio = PlayerCargoUsed / Hold;
            if (Ratio > 1.0)                // DAT_005c89b8
                Ratio = 1.0;
        }
        else
        {
            Ratio = 0.0; // DAT_005c8ba8
        }
        AsStatic(PlayerCargoFull)->bUseColours = 1;
        // The quad and the mesh are both the (hidden) full bar's.
        DrawCargoFill(PlayerCargoFull, CargoFillX(PlayerCargoFull, Ratio));
    }

    // The other player's bar.
    if (TargetCargoFull != nullptr)
    {
        double Ratio;
        if (TargetCargoUsed > 1e-5)
        {
            Ratio = TargetCargoUsed / TargetHoldSize;
            if (Ratio > 1.0)
                Ratio = 1.0;
        }
        else
        {
            Ratio = 0.0;
        }
        AsStatic(TargetCargoFull)->bUseColours = 1;
        DrawCargoFill(TargetCargoFull, CargoFillX(TargetCargoFull, Ratio));
    }
}

bool NnTrade::KeyEvent(unsigned Key, unsigned char* Modifiers)
{
    // FUN_004c2c70 (RET 8). Only the quantity panel takes keys, and only with modifier bit 0
    // (key down) or bit 1.
    bool bHandled = false;
    if (Mode != 3)
        return false;
    unsigned char const Mods = *Modifiers;
    unsigned char const bDown = static_cast<unsigned char>(Mods & 1);
    if (bDown == 0 && (Mods & 2) == 0)
        return false;

    int const Code = static_cast<int>(Key);
    if (Code == 8)
    {
        // Backspace drops the last digit.
        if (bDown != 0)
            Quantity = Quantity / 10;
        bHandled = true;
    }
    else if (Code >= '0' && Code <= '9')
    {
        // The first digit typed replaces the value; a value outside the bar's range is ignored.
        bHandled = true;
        if (bDown != 0)
        {
            if (bQuantityTyped == 0)
                Quantity = 0;
            int const Typed = Code + Quantity * 10 - '0';
            if (Typed >= QuantityBar->MinRange && Typed <= QuantityBar->MaxRange)
                Quantity = Typed;
        }
    }

    bQuantityTyped = 1;
    if (bHandled)
    {
        QuantityBar->SetProperty(kScrollPropSetValue, static_cast<uintptr_t>(Quantity), 0);
        SetQuantity(Quantity);
    }
    return bHandled;
}

// ---------------------------------------------------------------------------------------------
// Quantity panel and modes
// ---------------------------------------------------------------------------------------------

void NnTrade::SetQuantity(int NewQuantity)
{
    // FUN_004c1a10 (RET 4).
    if (QuantityBar != nullptr)
    {
        if (NewQuantity > QuantityBar->MaxRange)
            NewQuantity = QuantityBar->MaxRange;
        else if (NewQuantity < QuantityBar->MinRange)
            NewQuantity = QuantityBar->MinRange;
    }

    // "%d" into the first 32 characters of the buffer, then copied backwards into the second
    // half with a ',' after every third digit from the right.
    wchar_t Buffer[64];
    swprintf(Buffer, 32, kQuantityFormat, NewQuantity);
    int Remaining = static_cast<int>(wcslen(Buffer));
    wchar_t* Out = &Buffer[32 + Remaining + Remaining / 3];
    *Out = L'\0';
    wchar_t const* In = &Buffer[Remaining - 1];
    int Group = 0;
    while (Remaining != 0)
    {
        wchar_t const Digit = *In;
        --Out;
        --Remaining;
        ++Group;
        --In;
        *Out = Digit;
        if (Group == 3)
        {
            if (Remaining == 0)
                break;
            --Out;
            *Out = L',';
            Group = 0;
        }
    }

    QuantityText->SetProperty(kTextPropSetText, PointerArg(Out), 0);
    Quantity = NewQuantity;
}

void NnTrade::SetMode(int NewMode)
{
    // FUN_004c1ae0 (RET 4).
    int Previous = Mode;
    if (NewMode == 4)
    {
        Previous = 4;
        NewMode = 3;
    }
    Mode = NewMode;

    if (NewMode == 1)
    {
        // Everything but the lists hidden.
        TitleFrame->SetProperty(kWinPropHide, 1, 0);
        TitleArrow->SetProperty(kWinPropHide, 1, 0);
        TitleText->SetProperty(kWinPropHide, 1, 0);
        QuantityLine->Hide(); // FUN_004bed40 inlined
        QuantityTitle->SetProperty(kWinPropHide, 1, 0);
        QuantityFrame->SetProperty(kWinPropHide, 1, 0);
        QuantityAccept->SetProperty(kWinPropHide, 1, 0);
        QuantityReject->SetProperty(kWinPropHide, 1, 0);
        QuantityText->SetProperty(kWinPropHide, 1, 0);
        QuantityBar->SetProperty(kWinPropHide, 1, 0);
        for (size_t Index = 0; Index < CenterList->Lines.size(); ++Index)
            CenterList->Lines[Index]->Hide();
        CenterList->ScrollBar->SetProperty(kWinPropHide, 1, 0);
        AcceptTradeFrame->SetProperty(kWinPropHide, 1, 0);
        AcceptTradeText->SetProperty(kWinPropHide, 1, 0);
        return;
    }

    if (NewMode == 2)
    {
        // The arrow, the title, the offer list and "accept trade".
        TitleFrame->SetProperty(kWinPropHide, 1, 0);
        TitleArrow->SetProperty(kWinPropHide, 0, 0);
        TitleText->SetProperty(kWinPropHide, 0, 0);
        QuantityLine->Hide(); // FUN_004bed40 inlined
        QuantityTitle->SetProperty(kWinPropHide, 1, 0);
        QuantityFrame->SetProperty(kWinPropHide, 1, 0);
        QuantityAccept->SetProperty(kWinPropHide, 1, 0);
        QuantityReject->SetProperty(kWinPropHide, 1, 0);
        QuantityText->SetProperty(kWinPropHide, 1, 0);
        QuantityBar->SetProperty(kWinPropHide, 1, 0);
        for (size_t Index = 0; Index < CenterList->Lines.size(); ++Index)
            CenterList->Lines[Index]->Show(); // FUN_004bedc0
        CenterList->ScrollBar->SetProperty(kWinPropHide, 0, 0);
        CenterList->Refresh(-1);
        AcceptTradeFrame->SetProperty(kWinPropHide, 0, 0);
        AcceptTradeText->SetProperty(kWinPropHide, 0, 0);
        return;
    }

    if (NewMode != 3)
        return;

    // The quantity panel replaces the offer list.
    for (size_t Index = 0; Index < CenterList->Lines.size(); ++Index)
        CenterList->Lines[Index]->Hide();
    CenterList->ScrollBar->SetProperty(kWinPropHide, 1, 0);
    AcceptTradeFrame->SetProperty(kWinPropHide, 1, 0);
    AcceptTradeText->SetProperty(kWinPropHide, 1, 0);
    TitleFrame->SetProperty(kWinPropHide, 1, 0);
    TitleArrow->SetProperty(kWinPropHide, 0, 0);
    TitleText->SetProperty(kWinPropHide, 0, 0);
    QuantityLine->SetItem(SelectedItem); // FUN_004bef00
    QuantityLine->Show();                // FUN_004bedc0
    QuantityTitle->SetProperty(kWinPropHide, 0, 0);
    QuantityFrame->SetProperty(kWinPropHide, 0, 0);
    QuantityAccept->SetProperty(kWinPropHide, 0, 0);
    QuantityReject->SetProperty(kWinPropHide, 0, 0);
    QuantityText->SetProperty(kWinPropHide, 0, 0);
    QuantityBar->SetProperty(kWinPropHide, 0, 0);

    // The other hold's free space is 1000 when the other ship's hold size is unknown.
    EquipDesc Unit = *SelectedItem;
    Unit.set_count(1);
    bool const bFromPlayer = SelectedLine->Owner == PlayerList;
    double const UnitVolume = Unit.get_cargo_space_occupied();
    double Free;
    if (TargetHoldSize != 0.0)
        Free = TargetHoldSize - TargetCargoUsed;
    else
        Free = 1000.0;

    // The most the other side can take. Held is 0 again by the clamp, so the clamp and the
    // bar's minimum are 0.
    int Held = 0;
    int Most;
    if (Unit.ArchId == G_ShieldBatteryArchId) // DAT_00674a88
    {
        Most = TargetBatteryLimit - TargetBatteries - (bFromPlayer ? BatteriesOffered : 0);
    }
    else if (Unit.ArchId == G_NanobotArchId) // DAT_00674a8c
    {
        Most = TargetNanobotLimit - TargetNanobots - (bFromPlayer ? NanobotsOffered : 0);
    }
    else if (PlayerShip->get_ammo_capacity_remaining(Unit.ArchId) > -1)
    {
        // Ammo: the other ship's room less what is already offered.
        if (bFromPlayer)
        {
            EquipDesc const* const Offered = CenterList->Items.find_matching_cargo(Unit.ArchId, 1, 1.0f);
            if (Offered != nullptr)
                Held = Offered->Count;
        }
        Most = TargetShip->get_ammo_capacity_remaining(Unit.ArchId) - Held;
        Held = 0;
    }
    else if (UnitVolume != 0.0)
    {
        Most = static_cast<int>(Free / UnitVolume); // ftol
    }
    else
    {
        Most = SelectedItem->Count;
    }
    if (Most < Held)
        Most = 0;
    if (!bFromPlayer)
        Most = SelectedItem->Count;

    // A single unit of the player's has no bar.
    QuantityBar->MinRange = Held;
    QuantityBar->MaxRange = (SelectedItem->Count < Most) ? SelectedItem->Count : Most;
    if (bFromPlayer && SelectedItem->Count == 1)
        QuantityBar->SetProperty(kWinPropHide, 1, 0);

    // Re-entered (4) with a value that no longer fits: the voice line; a fresh panel forgets
    // the typed digits.
    if (Previous == 4)
    {
        if (QuantityBar->Value > Most)
            NnVoiceEnqueue(G_InsufficientCargoSpaceVoiceId, 0); // FUN_00489e80(DAT_00672458, 0)
    }
    else
    {
        bQuantityTyped = 0;
    }

    // Start at the most; the title says which way it goes.
    QuantityBar->SetProperty(kScrollPropSetValue, static_cast<uintptr_t>(Most), 0);
    SetQuantity(Most);
    QuantityTitle->SetProperty(kTextPropSetIds, (SelectedLine->Owner == PlayerList) ? 0x757 : 0x758, 0);

    // Nothing fits: the voice line and back to browsing.
    if (Most >= 1)
        return;
    NnVoiceEnqueue(G_InsufficientCargoSpaceVoiceId, 0);
    CTradeEquipList* const Owner = SelectedLine->Owner;
    SetTradeMark(SelectedItem, 0);
    SelectedLine = nullptr;
    SelectedItem = nullptr;
    Owner->Refresh(-1);
    SetMode(2);
}

// ---------------------------------------------------------------------------------------------
// The other player's side (IClient slots)
// ---------------------------------------------------------------------------------------------

void NnTrade::AddTargetItem(EquipDesc const* Item)
{
    // FUN_004c2220 (RET 4).
    if (TargetList != nullptr)
    {
        // A copy without the selection mark, merged when combinable.
        EquipDesc Copy = *Item;
        SetTradeMark(&Copy, 0);
        TargetList->Items.add_equipment_item(Copy, arch_is_combinable(Copy.ArchId));
        TargetList->CargoVolume = TargetList->Items.get_cargo_space_occupied();
        RefreshTradeListRange(TargetList);

        // The other hold as it will be after the trade.
        FClientGameobj* const Object = FindTradeObject(TargetObjectId);
        if (Object != nullptr)
        {
            CShip* const Ship = HudShipOfObjRw(Object); // FUN_00543320, not null-tested after
            double const TargetOffer = TargetList->CargoVolume;
            double const PlayerOffer = CenterList->CargoVolume;
            TargetCargoUsed = (TargetHoldSize - Ship->get_cargo_hold_remaining()) - TargetOffer +
                              PlayerOffer;
        }

        double const PlayerOffer = CenterList->CargoVolume;
        double const TargetOffer = TargetList->CargoVolume;
        PlayerCargoUsed = GetPlayerEquipDescListRetail().get_cargo_space_occupied() - PlayerOffer +
                          TargetOffer; // FUN_004c4640
    }

    // An open quantity panel re-checks its limits.
    if (Mode == 3)
        SetMode(4);
}

void NnTrade::RemoveTargetItem(EquipDesc const* Item)
{
    // FUN_004c2380 (RET 4).
    if (TargetList != nullptr)
    {
        EquipDescList& Items = TargetList->Items;
        EquipDesc* const Before = Items.find_equipment_item(Item->Id);
        Items.remove_equipment_item(Item->Id, Item->Count);
        TargetList->CargoVolume = Items.get_cargo_space_occupied();
        EquipDesc* const After = Items.find_equipment_item(Item->Id);
        RefreshTradeListRange(TargetList);
        EquipDesc* const Kept = (After != nullptr) ? Before : nullptr;

        // The two holds (see AddTargetItem).
        FClientGameobj* const Object = FindTradeObject(TargetObjectId);
        if (Object != nullptr)
        {
            CShip* const Ship = HudShipOfObjRw(Object);
            double const TargetOffer = TargetList->CargoVolume;
            double const PlayerOffer = CenterList->CargoVolume;
            TargetCargoUsed = (TargetHoldSize - Ship->get_cargo_hold_remaining()) - TargetOffer +
                              PlayerOffer;
        }
        double const PlayerOffer = CenterList->CargoVolume;
        double const TargetOffer = TargetList->CargoVolume;
        PlayerCargoUsed = GetPlayerEquipDescListRetail().get_cargo_space_occupied() - PlayerOffer +
                          TargetOffer;

        // The entry shown on the other player's card changed: close the card and drop the
        // selection. (A removed entry leaves Kept null, which matches a list with nothing
        // selected.)
        if (Kept == TargetList->SelectedItem)
        {
            ShowInfoCard(TargetList, 0);
            if (TargetList->SelectedItem != nullptr)
            {
                SetTradeMark(TargetList->SelectedItem, 0);
                TargetList->SelectedItem = nullptr;
            }
            TargetList->Refresh(-1);
        }
    }

    if (Mode == 3)
        SetMode(4);
}

void NnTrade::SetTarget(unsigned ObjectId)
{
    // FUN_004c2510 (RET 4).
    FClientGameobj* const Object = FindTradeObject(ObjectId);
    TargetObjectId = ObjectId;
    if (Object == nullptr)
        return;

    CShip* const Ship = HudShipOfObjRw(Object); // FUN_00543320, not null-tested after
    TargetShip = Ship;
    Archetype::Ship const* const ShipArch = Ship->shiparch(); // CObject +0x88
    TargetHoldSize = ShipArch->HoldSize;
    TargetCargoUsed = TargetHoldSize - Ship->get_cargo_hold_remaining();
    TargetName->SetProperty(kTextPropSetText, PointerArg(Ship->get_pilot_name()), 0);
    TargetOwnerPlayer = Ship->OwnerPlayer; // CSimple +0xb4
    TargetNanobotLimit = ShipArch->NanobotLimit;
    if (TargetNanobotLimit == -1)
        TargetNanobotLimit = kUnlimitedAmmo;
    TargetNanobots = ShipArch->NanobotLimit - Ship->get_repair_kit_capacity_remaining();
    TargetBatteryLimit = ShipArch->ShieldBatteryLimit;
    if (TargetBatteryLimit == -1)
        TargetBatteryLimit = kUnlimitedAmmo;
    TargetBatteries = ShipArch->ShieldBatteryLimit - Ship->get_shield_battery_capacity_remaining();
}

void NnTrade::SetTargetAccepted(unsigned char bAccepted)
{
    // FUN_004c2620 (RET 4): ids 0x759 on green, else 0x75a on red; alpha 0xc0.
    if (bAccepted == 1)
    {
        TargetAcceptedText->SetProperty(kTextPropSetIds, 0x759, 0);
        TargetAcceptedText->TextColour = TradeColour(0, 0xff, 0, 0xc0);
    }
    else
    {
        TargetAcceptedText->SetProperty(kTextPropSetIds, 0x75a, 0);
        TargetAcceptedText->TextColour = TradeColour(0xff, 0, 0, 0xc0);
    }
}

void NnTrade::SetPlayerAccepted(unsigned char bAccepted)
{
    // FUN_004c2690 (RET 4) -- FUN_004c2620 on +0x410.
    if (bAccepted == 1)
    {
        PlayerAcceptedText->SetProperty(kTextPropSetIds, 0x759, 0);
        PlayerAcceptedText->TextColour = TradeColour(0, 0xff, 0, 0xc0);
    }
    else
    {
        PlayerAcceptedText->SetProperty(kTextPropSetIds, 0x75a, 0);
        PlayerAcceptedText->TextColour = TradeColour(0xff, 0, 0, 0xc0);
    }
}

void NnTrade::SetTargetMoney(int Money)
{
    // FUN_004c2700 (RET 4) -- the body of FUN_004bf620 on +0x330, inlined in retail.
    TargetList->SetMoney(Money);
}

void NnTrade::ShowInfoCard(CTradeEquipList* List, unsigned char bShow)
{
    // FUN_004c2800 (RET 8).
    CacheString Mesh;
    CacheString Material;
    Mesh.clear();
    Material.clear();
    // Retail lends the bShow argument slot to FUN_004be6c0 as the name id and a stack local as
    // the info id; FUN_004be6c0 leaves both untouched only for an entry with no archetype at all.
    int IdsName = static_cast<int>(bShow);
    int IdsInfo = 0;
    if (bShow != 0)
        GetTradeItemVisuals(List->SelectedItem, &Mesh, &Material, &IdsName, &IdsInfo, true);

    if (List == PlayerList)
    {
        List->bSelectMode = bShow;
        List->Refresh(-1);
        uintptr_t const bHide = (bShow == 0) ? 1 : 0;
        PlayerCardFrame->SetProperty(kWinPropHide, bHide, 0);
        PlayerCardClose->SetProperty(kWinPropHide, bHide, 0);
        PlayerCardModel->SetProperty(kWinPropHide, bHide, 0);
        PlayerCardTitle->SetProperty(kWinPropHide, bHide, 0);
        if (bShow != 0)
        {
            // Retail's FUN_0053e4a0(1) on the ship view is a bare RET 4 and is not ported.
            if (Mesh.Value != nullptr)
                PlayerCardModel->LoadMesh(Mesh.Value, Material.Value, false,
                                          false); // FUN_00585890
            PlayerCardTitle->SetProperty(kTextPropSetIds, static_cast<uintptr_t>(IdsName), 0);
            RenderDisplayList Card;
            {
                StyleNode Style(kTextStyleChatSystem);
                RdlAppendNode(Card, Style.Clone());
            }
            LoadIdsIntoRdl(static_cast<unsigned>(IdsInfo), Card); // FUN_0057da40
            PlayerCardInfo->ClearContent();                      // FUN_0057b370
            PlayerCardInfo->AppendContent(Card);                 // FUN_0057b3e0
            PlayerCardInfo->Flags |= 3;
            PlayerCardInfo->ShowScrollBarV(true);                // FUN_00579bd0
        }
        else
        {
            PlayerCardInfo->Flags &= 0xfc;
            PlayerCardInfo->ShowScrollBarV(false);
        }
    }
    else if (List == TargetList)
    {
        // The same for the other player's card, except that the ship view is loaded without
        // testing the mesh (0x004c2aad).
        List->bSelectMode = bShow;
        List->Refresh(-1);
        uintptr_t const bHide = (bShow == 0) ? 1 : 0;
        TargetCardFrame->SetProperty(kWinPropHide, bHide, 0);
        TargetCardClose->SetProperty(kWinPropHide, bHide, 0);
        TargetCardModel->SetProperty(kWinPropHide, bHide, 0);
        TargetCardTitle->SetProperty(kWinPropHide, bHide, 0);
        if (bShow != 0)
        {
            TargetCardModel->LoadMesh(Mesh.Value, Material.Value, false, false);
            TargetCardTitle->SetProperty(kTextPropSetIds, static_cast<uintptr_t>(IdsName), 0);
            RenderDisplayList Card;
            {
                StyleNode Style(kTextStyleChatSystem);
                RdlAppendNode(Card, Style.Clone());
            }
            LoadIdsIntoRdl(static_cast<unsigned>(IdsInfo), Card);
            TargetCardInfo->ClearContent();
            TargetCardInfo->AppendContent(Card);
            TargetCardInfo->Flags |= 3;
            TargetCardInfo->ShowScrollBarV(true);
        }
        else
        {
            TargetCardInfo->Flags &= 0xfc;
            TargetCardInfo->ShowScrollBarV(false);
        }
    }

    // The quantity panel's item follows the card of its list.
    if (SelectedLine != nullptr && SelectedLine->Owner == List)
        SetTradeMark(SelectedItem, bShow);
}

// ---------------------------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------------------------

intptr_t NnTrade::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // FUN_004c2d50 (RET 0xc).
    WinControl* const Control = reinterpret_cast<WinControl*>(A);

    if (Msg == kWinNotifyHighlight || Msg == kWinNotifyUnhighlight)
    {
        // Mouse over (0x2c) / out (0x2d): a row's frames follow the hovered button's bit 2, get
        // bit 0 (pressed-look) set or cleared and the row is lifted: layer 2 when selected
        // (bit 6), else 1 while hovered. Only ButtonControls send these (0x0055b8cb,
        // 0x0055bbf9); the rows' buttons carry the row in +0x2f0.
        CTradeEquipListLine* const Line = reinterpret_cast<CTradeEquipListLine*>(Control->OwnerData);
        if (Line == nullptr)
            return 0;
        if (Control == Line->InfoCard)
            return 0;
        unsigned char const Bit2 =
            static_cast<unsigned char>(static_cast<NnButtonControl*>(Control)->GetButtonBits() & 0x04);
        unsigned char Flags = Line->IconFrame->GetButtonBits();
        Line->IconFrame->SetButtonBits(static_cast<unsigned char>(((Flags ^ Bit2) & 0x04) ^ Flags));
        Flags = Line->InfoFrame->GetButtonBits();
        Line->InfoFrame->SetButtonBits(static_cast<unsigned char>(((Flags ^ Bit2) & 0x04) ^ Flags));
        unsigned Layer;
        if (Msg == kWinNotifyHighlight)
        {
            Line->IconFrame->SetButtonBits(static_cast<unsigned char>(Line->IconFrame->GetButtonBits() | 1));
            Line->InfoFrame->SetButtonBits(static_cast<unsigned char>(Line->InfoFrame->GetButtonBits() | 1));
            Layer = 1;
        }
        else
        {
            Line->IconFrame->SetButtonBits(static_cast<unsigned char>(Line->IconFrame->GetButtonBits() & 0xfe));
            Line->InfoFrame->SetButtonBits(static_cast<unsigned char>(Line->InfoFrame->GetButtonBits() & 0xfe));
            Layer = 0;
        }
        if ((Line->IconFrame->GetButtonBits() & 0x40) != 0)
            Layer = 2;
        Line->IconFrame->DrawLayer = Layer;
        Line->IconMesh->DrawLayer = Layer;
        Line->InfoFrame->DrawLayer = Layer;
        Line->HealthBar->DrawLayer = Layer;
        Line->ItemCount->DrawLayer = Layer;
        if (Line->InfoCard != nullptr)
            Line->InfoCard->DrawLayer = Layer;
        return 0;
    }

    if (Msg == kWinNotifyScrollValue)
    {
        // The quantity bar moved, or a list's bar (its +0x2f0 is the list) scrolled.
        if (Control == QuantityBar)
        {
            if (Quantity != static_cast<int>(B))
                SetQuantity(static_cast<int>(B));
            return 0;
        }
        CTradeEquipList* const List = reinterpret_cast<CTradeEquipList*>(Control->OwnerData);
        if (List != nullptr && List->TopIndex != static_cast<int>(B))
        {
            List->TopIndex = static_cast<int>(B);
            List->Refresh(-1);
        }
        return 0;
    }

    if (Msg == kWinMsgHide)
    {
        // Close: the HUD panels the window collapsed come back, then the window takes itself
        // apart.
        if (bHudStatusWasExpanded != 0)
        {
            HUD_Status* const Status = GetHudStatus();
            if (Status != nullptr && Status->bExpanded == 0)
                Status->ToggleExpanded();
        }
        if (bHudTargetWasExpanded != 0)
        {
            HudTargetBase* const Target = G_HudTargetPanel;
            if (Target != nullptr && Target->bExpanded == 0)
                Target->ToggleExpanded();
        }
        ChatPlaceMsgWinForTrade(0); // FUN_0046b450(0)
        Win3DReparent(this, nullptr); // FUN_0059dd00
        Win3DDeleteTree(this); // FUN_0043d2a0, inlined in retail
        return 0;
    }

    if (Msg != kWinNotifyClick)
        return 0;

    // --- 0x26: a click ---
    if (Control == TradeClose)
    {
        // IServer +0x140, then the button is disabled.
        G_Server->TerminateTrade(GetPlayerConnectionId(), 0);
        TradeClose->SetProperty(kWinPropDisable, 1, 0);
        return 0;
    }

    if (Control == PlayerCardClose || Control == TargetCardClose)
    {
        CTradeEquipList* const List = (Control == PlayerCardClose) ? PlayerList : TargetList;
        ShowInfoCard(List, 0);
        if (List->SelectedItem != nullptr)
        {
            SetTradeMark(List->SelectedItem, 0);
            List->SelectedItem = nullptr;
        }
        List->Refresh(-1);
        return 0;
    }

    if (Control == QuantityAccept)
    {
        // Move the chosen quantity. From the offer (+0x334) the value is what stays offered;
        // from the player's cargo (+0x338) it is what is added to the offer.
        EquipDesc* Unmark = SelectedItem; // cleared when its entry is gone
        EquipDesc* const Item = SelectedItem;
        unsigned const Session = GetPlayerConnectionId(); // DAT_00673344

        if (Item->ArchId == 0)
        {
            // Credits.
            if (SelectedLine->Owner == CenterList)
            {
                int const Value = QuantityBar->Value;
                if (Value < Item->Count)
                {
                    int const Back = Item->Count - Value;
                    if (CenterList->SetMoney(CenterList->CreditsTemplate.Count - Back) == 0)
                        Unmark = nullptr;
                    PlayerList->SetMoney(PlayerList->CreditsTemplate.Count + Back);
                    G_Server->SetTradeMoney(
                        Session, static_cast<unsigned long>(CenterList->CreditsTemplate.Count));
                }
            }
            else if (QuantityBar->Value != 0)
            {
                G_Server->SetTradeMoney(Session, static_cast<unsigned long>(
                                                     CenterList->CreditsTemplate.Count +
                                                     QuantityBar->Value));
                CenterList->SetMoney(CenterList->CreditsTemplate.Count + QuantityBar->Value);
                if (PlayerList->SetMoney(PlayerList->CreditsTemplate.Count - QuantityBar->Value) == 0)
                    Unmark = nullptr;
            }
        }
        else if (SelectedLine->Owner == CenterList)
        {
            // Back from the offer to the player.
            int const Value = QuantityBar->Value;
            if (Value < Item->Count)
            {
                EquipDesc Moved = *Item;
                int const Back = Moved.Count - Value;
                if (Moved.ArchId == G_ShieldBatteryArchId)
                    BatteriesOffered = Value;
                else if (Moved.ArchId == G_NanobotArchId)
                    NanobotsOffered = Value;
                if (Back > 0)
                {
                    Moved.set_count(Back);
                    G_Server->DelTradeEquip(Session, Moved); // IServer +0x150

                    EquipDescList& Offer = CenterList->Items;
                    EquipDesc* const Before = Offer.find_equipment_item(Moved.Id);
                    Offer.remove_equipment_item(Moved.Id, Moved.Count);
                    CenterList->CargoVolume = Offer.get_cargo_space_occupied();
                    EquipDesc* const After = Offer.find_equipment_item(Moved.Id);
                    RefreshTradeListRange(CenterList);
                    Unmark = (After != nullptr) ? Before : nullptr;

                    EquipDesc Copy(Moved);
                    SetTradeMark(&Copy, 0);
                    PlayerList->Items.add_equipment_item(Copy, arch_is_combinable(Copy.ArchId));
                    PlayerList->CargoVolume = PlayerList->Items.get_cargo_space_occupied();
                    RefreshTradeListRange(PlayerList);

                    FClientGameobj* const Object = FindTradeObject(TargetObjectId);
                    if (Object != nullptr)
                    {
                        CShip* const Ship = HudShipOfObjRw(Object);
                        double const TargetOffer = TargetList->CargoVolume;
                        double const PlayerOffer = CenterList->CargoVolume;
                        TargetCargoUsed = (TargetHoldSize - Ship->get_cargo_hold_remaining()) -
                                          TargetOffer + PlayerOffer;
                    }
                    double const PlayerOffer = CenterList->CargoVolume;
                    double const TargetOffer = TargetList->CargoVolume;
                    PlayerCargoUsed = GetPlayerEquipDescListRetail().get_cargo_space_occupied() -
                                      PlayerOffer + TargetOffer;
                }
            }
        }
        else
        {
            // From the player's cargo into the offer.
            if (QuantityBar->Value != 0)
            {
                EquipDesc Moved = *Item;
                Moved.set_count(QuantityBar->Value);
                G_Server->AddTradeEquip(Session, Moved); // IServer +0x14c
                if (Moved.ArchId == G_ShieldBatteryArchId)
                    BatteriesOffered += QuantityBar->Value;
                else if (Moved.ArchId == G_NanobotArchId)
                    NanobotsOffered += QuantityBar->Value;

                EquipDesc Copy(Moved);
                SetTradeMark(&Copy, 0);
                CenterList->Items.add_equipment_item(Copy, arch_is_combinable(Copy.ArchId));
                CenterList->CargoVolume = CenterList->Items.get_cargo_space_occupied();
                RefreshTradeListRange(CenterList);

                EquipDescList& Cargo = PlayerList->Items;
                EquipDesc* const Before = Cargo.find_equipment_item(Moved.Id);
                Cargo.remove_equipment_item(Moved.Id, Moved.Count);
                PlayerList->CargoVolume = Cargo.get_cargo_space_occupied();
                EquipDesc* const After = Cargo.find_equipment_item(Moved.Id);
                RefreshTradeListRange(PlayerList);
                Unmark = (After != nullptr) ? Before : nullptr;

                FClientGameobj* const Object = FindTradeObject(TargetObjectId);
                if (Object != nullptr)
                {
                    CShip* const Ship = HudShipOfObjRw(Object);
                    double const TargetOffer = TargetList->CargoVolume;
                    double const PlayerOffer = CenterList->CargoVolume;
                    TargetCargoUsed = (TargetHoldSize - Ship->get_cargo_hold_remaining()) -
                                      TargetOffer + PlayerOffer;
                }
                double const PlayerOffer = CenterList->CargoVolume;
                double const TargetOffer = TargetList->CargoVolume;
                PlayerCargoUsed = GetPlayerEquipDescListRetail().get_cargo_space_occupied() -
                                  PlayerOffer + TargetOffer;
            }
        }

        // The panel closes.
        CTradeEquipList* const Owner = SelectedLine->Owner;
        if (Unmark != nullptr)
            SetTradeMark(Unmark, 0);
        SelectedLine = nullptr;
        SelectedItem = nullptr;
        Owner->Refresh(-1);
        SetMode(2);
        return 0;
    }

    if (Control == QuantityReject)
    {
        CTradeEquipList* const Owner = SelectedLine->Owner;
        SetTradeMark(SelectedItem, 0);
        SelectedLine = nullptr;
        SelectedItem = nullptr;
        Owner->Refresh(-1);
        SetMode(2);
        return 0;
    }

    if (Control == AcceptTradeFrame)
    {
        // The player accepts: green text, IServer +0x144.
        PlayerAcceptedText->SetProperty(kTextPropSetIds, 0x759, 0);
        PlayerAcceptedText->TextColour = TradeColour(0, 0xff, 0, 0xc0);
        G_Server->AcceptTrade(GetPlayerConnectionId(), true);
        return 0;
    }

    // A row's button.
    CTradeEquipListLine* const Line = reinterpret_cast<CTradeEquipListLine*>(Control->OwnerData);
    if (Line == nullptr || Line->bInUse == 0)
        return 0;
    CTradeEquipList* const List = Line->Owner;
    if (Control != Line->InfoCard)
    {
        if (List == nullptr)
            return 0;
        if (List->bSelectMode == 0)
        {
            if (List == TargetList)
                return 0;

            // Open the quantity panel on the row's item.
            if (Line->Item == SelectedItem)
                return 0;
            if (SelectedItem != nullptr)
                SetTradeMark(SelectedItem, 0);
            SelectedLine = Line;
            SelectedItem = Line->Item;
            SetTradeMark(SelectedItem, 1);
            List->Refresh(-1);
            SetMode(3);
            return 0;
        }
    }

    // The "?" button, or any button of a list in select mode: the row's item goes onto the
    // list's info card. Retail: List is not tested on the "?" path (0x004c3519 jumps straight
    // to 0x004c3540).
    if (List->SelectedItem != nullptr)
    {
        SetTradeMark(List->SelectedItem, 0);
        List->SelectedItem = nullptr;
    }
    EquipDesc* const Item = Line->Item;
    if (Item != nullptr)
    {
        List->SelectedItem = Item;
        SetTradeMark(Item, 1);
    }
    ShowInfoCard(List, 1);
    List->Refresh(-1);
    return 0;
}
