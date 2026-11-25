// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_LoadSave (NN_LoadSave.cpp). See nn_load_save.h.

#include "NeuroNet/nn_load_save.h"

#include "Core/client_state.h"          // QueueLoadGameCharacter (FUN_005b2c70)
#include "Common/Graphics/fade_to_black.h"         // FadeIn (FUN_004120d0)
#include "Space/HUD/hud_command_dispatch.h"  // HudPostCommand (FUN_005763b0)
#include "Common/Fonts/ids_strings.h"           // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Common/input_manager.h"         // Get/SetClientMouseCursorDrawFlag (FUN_0041dd90 / FUN_0041dda0)
#include "Network/login_mgr.h"             // G_LoginMgr (DAT_0067e7b8), G_Server (DAT_0067ecd0)
#include "NeuroNet/nn_button_control.h"     // NnButtonControl::SetMontageMode (FUN_0055bfc0)
#include "NeuroNet/nn_confirm.h"            // NnConfirmOpen (FUN_004889e0)
#include "NeuroNet/nn_error.h"              // NnErrorOpen (FUN_00473c00)
#include "NeuroNet/nn_modal.h"              // NnModalPauseGame / NnModalResumeGame, NnFloatBits
#include "NeuroNet/nn_text_control.h"       // NnTextControl (+0x381 / +0x385)
#include "NeuroNet/nn_ui_pulse.h"           // G_HudChromeColorRgba (DAT_00679bb4), G_NnPaletteAmber
#include "Space/player_spawn.h"          // GetPlayerConnectionId (DAT_00673344)
#include "Interface/rich_text_win.h"         // RichTextEditWin
#include "Interface/ui_scroll.h"             // UiScroll::SetRange (FUN_00595d60)
#include "Interface/win3d.h"
#include "Interface/win3d_renderables.h"     // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "Interface/win_factory.h"

#include "common_archetype.h"      // Archetype::GetShip
#include "common_misc.h"           // filetime_to_wstring
#include "common_server.h"         // IServer::SaveGame
#include "common_universe.h"       // Universe::get_system
#include "dacom.h"                 // FDUMP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

// Retail DAT_00671f14 -- WINFACTORY<NN_LoadSave> static (LAB_00485930 -> FUN_00488ca0; Create
// FUN_00488d10; atexit LAB_00485950 -> FUN_00579380).
WINFACTORY<NnLoadSave> G_NnLoadSaveFactory("NN_LoadSave");

wchar_t G_NnLoadSaveText[0x100];

namespace
{
    char const kNnLoadSaveCpp[] = "E:\\FL\\Scratch\\Source\\Client\\nn\\NN_LoadSave.cpp"; // 005d1e40
    char const kBlackPlaneMesh[] = "Interface\\NeuroNet\\MISC_bigblackplane.3db";         // 005d21b0
    char const kFrameMesh[] = "Interface\\LoadSave\\LS_background.cmp";                   // 005d2174
    char const kButtonMesh[] = "Interface\\LoadSave\\FRONT_button.cmp";                   // 005d2130
    char const kInfoFrameMesh[] = "Interface\\LoadSave\\FRONT_infowindow.cmp";            // 005d2018
    char const kDescripButtonMesh[] = "Interface\\LoadSave\\LS_bottomframe.3db";          // 005d1ecc
    char const kCloseMesh[] = "Interface\\NeuroNet\\Inventory\\x.3db";                    // 005d01d0
    // DAT_005c8ce0 -- an empty wide string.
    wchar_t const kEmptyText[] = L"";

    // Win3D +0x6c bits 0..1 (FUN_005a0c30's pair): set = shown.
    void ShowControl(Win3D* Control)
    {
        Control->Flags = static_cast<unsigned char>(Control->Flags | 3);
    }

    void HideControl(Win3D* Control)
    {
        Control->Flags = static_cast<unsigned char>(Control->Flags & 0xfc);
    }
}

NnLoadSave::NnLoadSave()
    : bCursorDrawFlag(0)
    , Mode(0)
    , ConfirmMode(0)
    , bExitToMainMenu(0)
    , bSaveAllowed(1)
    , bListRefreshed(1)
    , Pad33f(0)
    , Byte340(0)
    , SaveNamePrefixLength(0)
    , NextSaveNumber(0)
    , Title(nullptr)
    , VisibleRows(0)
    , Scroll(nullptr)
    , DescripText(nullptr)
    , DescripButton(nullptr)
    , DescripEdit(nullptr)
    , CloseButton(nullptr)
{
    // FUN_00485960. Retail leaves the rest to Create, which writes it before it is read; the
    // port zeroes it here.
    Pad331[0] = 0;
    Pad331[1] = 0;
    Pad331[2] = 0;
    SaveName[0] = 0;
    for (int Index = 0; Index < 5; ++Index)
    {
        Buttons[Index] = nullptr;
        ButtonTexts[Index] = nullptr;
        bButtonEnabled[Index] = 0;
    }
    Pad5d9[0] = 0;
    Pad5d9[1] = 0;
    Pad5d9[2] = 0;
    for (int Index = 0; Index < 3; ++Index)
    {
        NameListOffset[Index] = 0.0f;
        TimeListOffset[Index] = 0.0f;
    }
    for (int Index = 0; Index < 4; ++Index)
    {
        InfoTitles[Index] = nullptr;
        InfoValues[Index] = nullptr;
    }
}

NnLoadSave::~NnLoadSave()
{
    // FUN_00485a60; the member and base destructors do the work.
}

bool NnLoadSave::Create(Win3D* NewParent, void* Data)
{
    // LAB_00485be0 (RET 8, 0x26c-byte frame). No null test on Data (0x00485c07).
    Mode = *static_cast<int const*>(Data);

    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    float const ZFar = NnFloatBits(0xbf866666); // -1.05

    float const PlanePosition[3] = {0.0f, 0.0f, -1.0f};
    CreateStaticControl("LoadSave_BlackPlane", kBlackPlaneMesh, PlanePosition, nullptr, Zero, 0, 0,
                        0, 1, 1, 0, 1, 0, 0, 0, Zero); // FUN_005a0db0

    // {0.14, 0, -1.05}
    float const FramePosition[3] = {NnFloatBits(0x3e0f5c29), 0.0f, ZFar};
    CreateStaticControl("LoadSave_Frame", kFrameMesh, FramePosition, nullptr, Zero, 0, 0, 0, 1, 1,
                        0, 1, 0, 0, 0, Zero);

    // {0.145, 0.25, -1.05}, 0.27 x 0.03 (FUN_005a0e30).
    float const TitlePosition[3] = {NnFloatBits(0x3e147ae1), NnFloatBits(0x3e800000), ZFar};
    Title = CreateTextControl("LoadSave_Title", "PageHeader", TitlePosition, nullptr,
                              NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f), 1.0f, 0, 1, 0, 0);
    Title->SetProperty(kTextPropSetIds, 0x586, 0);

    // The five buttons (FUN_005a0c90). Save and Load share {-0.33, 0.27, -1.05}; Delete, Main
    // Menu and Resume step down by 0.0916 / 0.1832 / 0.2748 (one float subtraction each). Half
    // extents {0.089, 0.021, 0}.
    float const ButtonX = NnFloatBits(0xbea8f5c2);
    float const ButtonY = NnFloatBits(0x3e8a3d71);
    float const ButtonHalf[3] = {NnFloatBits(0x3db645a2), NnFloatBits(0x3cac0831), 0.0f};
    float const SavePosition[3] = {ButtonX, ButtonY, ZFar};
    Buttons[0] = CreateButtonControl("LoadSave_SaveGame", kButtonMesh, nullptr, nullptr, nullptr,
                                     SavePosition, nullptr, ButtonHalf, 0, 0, 1, 0, 0x58c, 0, 0);
    Buttons[1] = CreateButtonControl("LoadSave_LoadGame", kButtonMesh, nullptr, nullptr, nullptr,
                                     SavePosition, nullptr, ButtonHalf, 0, 0, 1, 0, 0x58d, 0, 0);
    float const DeletePosition[3] = {ButtonX, ButtonY - NnFloatBits(0x3dbb98c8), ZFar};
    Buttons[2] = CreateButtonControl("LoadSave_Delete", kButtonMesh, nullptr, nullptr, nullptr,
                                     DeletePosition, nullptr, ButtonHalf, 0, 0, 1, 0, 0x58e, 0, 0);
    float const MainMenuPosition[3] = {ButtonX, ButtonY - NnFloatBits(0x3e3b98c8), ZFar};
    Buttons[3] = CreateButtonControl("LoadSave_MainMenu", kButtonMesh, nullptr, nullptr, nullptr,
                                     MainMenuPosition, nullptr, ButtonHalf, 0, 0, 1, 0, 0x58f, 0,
                                     0);
    float const ResumePosition[3] = {ButtonX, ButtonY - NnFloatBits(0x3e8cb296), ZFar};
    Buttons[4] = CreateButtonControl("LoadSave_ResumeGame", kButtonMesh, nullptr, nullptr, nullptr,
                                     ResumePosition, nullptr, ButtonHalf, 0, 0, 1, 0, 0x590, 0, 0);

    // No null test.
    for (int Index = 0; Index < 5; ++Index)
        static_cast<NnButtonControl*>(Buttons[Index])->SetMontageMode(1);

    // The captions (FUN_005a0e30): Y = the button row's Y + 0.0018 (Save, Load), - 0.0898,
    // - 0.1814, - 0.273.
    float const SaveTextPosition[3] = {ButtonX, ButtonY + NnFloatBits(0x3aebedfa), ZFar};
    ButtonTexts[0] = CreateTextControl("LoadSave_SaveGameText", "ButtonText", SaveTextPosition,
                                       nullptr, NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f),
                                       1.0f, 0, 1, 0, 0);
    ButtonTexts[1] = CreateTextControl("LoadSave_LoadGameText", "ButtonText", SaveTextPosition,
                                       nullptr, NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f),
                                       1.0f, 0, 1, 0, 0);
    float const DeleteTextPosition[3] = {ButtonX, ButtonY - NnFloatBits(0x3db7e910), ZFar};
    ButtonTexts[2] = CreateTextControl("LoadSave_DeleteText", "ButtonText", DeleteTextPosition,
                                       nullptr, NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f),
                                       1.0f, 0, 1, 0, 0);
    float const MainMenuTextPosition[3] = {ButtonX, ButtonY - NnFloatBits(0x3e39c0ec), ZFar};
    ButtonTexts[3] = CreateTextControl("LoadSave_MainMenuText", "ButtonText",
                                       MainMenuTextPosition, nullptr, NnFloatBits(0x3e8a3d71),
                                       NnFloatBits(0x3cf5c28f), 1.0f, 0, 1, 0, 0);
    float const ResumeTextPosition[3] = {ButtonX, ButtonY - NnFloatBits(0x3e8bc6a8), ZFar};
    ButtonTexts[4] = CreateTextControl("LoadSave_ResumeGameText", "ButtonText",
                                       ResumeTextPosition, nullptr, NnFloatBits(0x3e8a3d71),
                                       NnFloatBits(0x3cf5c28f), 1.0f, 0, 1, 0, 0);

    for (int Index = 0; Index < 5; ++Index)
        ButtonTexts[Index]->SetProperty(kTextPropSetIds, static_cast<uintptr_t>(0x587 + Index), 0);

    // Each caption's +0x381 = DAT_00679bb4 and +0x385 = DAT_00679ba4 (two unaligned dwords).
    for (int Index = 0; Index < 5; ++Index)
        bButtonEnabled[Index] = 0;
    for (int Index = 0; Index < 5; ++Index)
    {
        NnTextControl* const Text = static_cast<NnTextControl*>(ButtonTexts[Index]);
        memcpy(Text->BlinkColourOn, G_HudChromeColorRgba, 4);
        memcpy(Text->BlinkColourOff, &G_NnPaletteAmber, 4);
    }

    // {-0.33, -0.2, -1.05}
    float const InfoFramePosition[3] = {ButtonX, NnFloatBits(0xbe4ccccd), ZFar};
    CreateStaticControl("LoadSave_InfoFrame", kInfoFrameMesh, InfoFramePosition, nullptr, Zero, 0,
                        0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);

    // The info texts, from {-0.32, -0.125, -1.05} down by 0, 0.02, 0.0425, 0.0625, 0.085, 0.105,
    // 0.1275, 0.1475; created title, data, title, data...
    float const InfoX = NnFloatBits(0xbea3d70a);
    float const InfoY = NnFloatBits(0xbe000000);
    float const InfoWidth = NnFloatBits(0x3e800000);
    float const InfoHeight = NnFloatBits(0x3ca3d70a);
    char const* const InfoTitleNames[4] = {"LoadSave_InfoLocationTitle",
                                           "LoadSave_InfoCreditsTitle", "LoadSave_InfoRankTitle",
                                           "LoadSave_InfoShipTitle"};
    char const* const InfoDataNames[4] = {"LoadSave_InfoLocationData", "LoadSave_InfoCreditsData",
                                          "LoadSave_InfoRankData", "LoadSave_InfoShipData"};
    {
        // Spelled out per text so every subtraction is retail's single float FSUB.
        float const LocationTitlePosition[3] = {InfoX, InfoY, ZFar};
        InfoTitles[0] = CreateTextControl(InfoTitleNames[0], "Normal", LocationTitlePosition,
                                          nullptr, InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
        float const LocationDataPosition[3] = {InfoX, InfoY - NnFloatBits(0x3ca3d70a), ZFar};
        InfoValues[0] = CreateTextControl(InfoDataNames[0], "Normal", LocationDataPosition,
                                          nullptr, InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
        float const CreditsTitlePosition[3] = {InfoX, InfoY - NnFloatBits(0x3d2e147b), ZFar};
        InfoTitles[1] = CreateTextControl(InfoTitleNames[1], "Normal", CreditsTitlePosition,
                                          nullptr, InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
        float const CreditsDataPosition[3] = {InfoX, InfoY - NnFloatBits(0x3d800000), ZFar};
        InfoValues[1] = CreateTextControl(InfoDataNames[1], "Normal", CreditsDataPosition,
                                          nullptr, InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
        float const RankTitlePosition[3] = {InfoX, InfoY - NnFloatBits(0x3dae147b), ZFar};
        InfoTitles[2] = CreateTextControl(InfoTitleNames[2], "Normal", RankTitlePosition, nullptr,
                                          InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
        float const RankDataPosition[3] = {InfoX, InfoY - NnFloatBits(0x3dd70a3e), ZFar};
        InfoValues[2] = CreateTextControl(InfoDataNames[2], "Normal", RankDataPosition, nullptr,
                                          InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
        float const ShipTitlePosition[3] = {InfoX, InfoY - NnFloatBits(0x3e028f5c), ZFar};
        InfoTitles[3] = CreateTextControl(InfoTitleNames[3], "Normal", ShipTitlePosition, nullptr,
                                          InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
        float const ShipDataPosition[3] = {InfoX, InfoY - NnFloatBits(0x3e170a3e), ZFar};
        InfoValues[3] = CreateTextControl(InfoDataNames[3], "Normal", ShipDataPosition, nullptr,
                                          InfoWidth, InfoHeight, 1.0f, 0, 0, 0, 0);
    }

    InfoTitles[0]->SetProperty(kTextPropSetIds, 0x593, 0);
    InfoTitles[1]->SetProperty(kTextPropSetIds, 0x594, 0);
    InfoTitles[2]->SetProperty(kTextPropSetIds, 0x5ec, 0);
    InfoTitles[3]->SetProperty(kTextPropSetIds, 0x591, 0);
    for (int Index = 0; Index < 4; ++Index)
        InfoValues[Index]->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(kEmptyText), 0);

    // Height and text scale are computed in double (DAT_005d1f10 / DAT_005d1f08) and stored as
    // float.
    VisibleRows = Mode == kNnLoadSaveModeSave ? 0x10 : 0x14;
    float const ListHeight = static_cast<float>(0.032538 * static_cast<double>(VisibleRows));
    float const ListTextScale = static_cast<float>(0.01773 / static_cast<double>(VisibleRows));
    float ListY = NnFloatBits(0x3de147ae);
    if (Mode != kNnLoadSaveModeSave)
        ListY = NnFloatBits(0x3e570a3d);

    // Width 0.4, offset {-0.142, Y, -1.05}.
    NameList.Create(this, nullptr);
    NameList.Height = ListHeight;
    NameList.VisibleRows = VisibleRows;
    NameList.bDrawFrame = 0;
    NameList.bDrawRowRules = 1;
    NameList.Width = NnFloatBits(0x3ecccccd);
    NameList.TextScale = ListTextScale;
    NameListOffset[0] = NnFloatBits(0xbe116873);
    NameListOffset[1] = ListY;
    NameListOffset[2] = ZFar;

    // Width 0.14, offset {0.258, Y, -1.05}.
    TimeList.Create(this, nullptr);
    TimeList.Height = ListHeight;
    TimeList.bDrawFrame = 0;
    TimeList.bDrawRowRules = 1;
    TimeList.Width = NnFloatBits(0x3e0f5c2a);
    TimeList.VisibleRows = VisibleRows;
    TimeList.TextScale = ListTextScale;
    TimeListOffset[0] = NnFloatBits(0x3e841894);
    TimeListOffset[1] = ListY;
    TimeListOffset[2] = ZFar;

    if (Mode == kNnLoadSaveModeSave)
    {
        // FUN_005a1900; the edit is not null-tested.
        float const DescripX = NnFloatBits(0x3e000000);
        float const DescripY = NnFloatBits(0x3e21cac1);
        float const EditPosition[3] = {DescripX - NnFloatBits(0x3e851eb8),
                                       DescripY + NnFloatBits(0x3c75c28f), ZFar};
        DescripEdit = CreateUiTextEdit("LoadSave_Edit2", EditPosition, NnFloatBits(0x3f051eb8),
                                       NnFloatBits(0x3cf5c28f), const_cast<wchar_t*>(kEmptyText));
        DescripEdit->SetMaxLength(0x1e);
        DescripEdit->Activate();

        float const DescripPosition[3] = {DescripX, DescripY, ZFar};
        float const DescripHalf[3] = {NnFloatBits(0x3e8a3d71), NnFloatBits(0x3ced9168), 0.0f};
        DescripButton = CreateButtonControl("LoadSave_DescripButton", kDescripButtonMesh, nullptr,
                                            nullptr, nullptr, DescripPosition, nullptr,
                                            DescripHalf, 0, 0, 1, 0, 0x596, 0, 0);
        static_cast<NnButtonControl*>(DescripButton)->SetMontageMode(1);
        DescripButton->SetProperty(0x32, 1, 0);

        float const DescripTextPosition[3] = {NnFloatBits(0xbc1374bc), NnFloatBits(0x3e50e560),
                                              ZFar};
        DescripText = CreateTextControl("LoadSave_DescripText", "ListText", DescripTextPosition,
                                        nullptr, NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f),
                                        1.0f, 0, 0, 0, 0);
        DescripText->SetProperty(kTextPropSetIds, 0x592, 0);
    }
    else
    {
        DescripEdit = nullptr;
        DescripButton = nullptr;
        DescripText = nullptr;
    }

    // FUN_005a17e0; Y / H 0.107 / 0.37 in save mode, else 0.21 / 0.465.
    float ScrollY = NnFloatBits(0x3e570a3d);
    float ScrollHeight = NnFloatBits(0x3eee147b);
    if (Mode == kNnLoadSaveModeSave)
    {
        ScrollY = NnFloatBits(0x3ddb22d1);
        ScrollHeight = NnFloatBits(0x3ebd70a4);
    }
    float const ScrollPosition[3] = {NnFloatBits(0x3ecfdf3c), ScrollY, ZFar};
    Scroll = CreateUIScroll("LoadSave_Scroll", ScrollPosition, NnFloatBits(0x3ca3d70a),
                            ScrollHeight, 0, 100, VisibleRows, 0);

    // {0.415, 0.26, -1.05}
    float const ClosePosition[3] = {NnFloatBits(0x3ed47ae2), NnFloatBits(0x3e851eb8), ZFar};
    float const CloseHalf[3] = {NnFloatBits(0x3ca3d70a), NnFloatBits(0x3ca3d70a), 0.0f};
    CloseButton = CreateButtonControl("LoadSave_Close", kCloseMesh, nullptr, nullptr, nullptr,
                                      ClosePosition, nullptr, CloseHalf, 0, 1, 0, 0, 0x409, 0, 0);
    static_cast<NnButtonControl*>(CloseButton)->SetMontageMode(1);

    // The buffer is used even after a failed lookup.
    wchar_t PrefixText[0x100];
    if (GetIdsStringWWithRawFallback(0x65d, PrefixText, 0x100) == 0)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Failed to find string resource %d\n", kNnLoadSaveCpp,
              0x18e, 0x65d);
    }
    SaveNamePrefix.assign(PrefixText, wcslen(PrefixText));
    SaveNamePrefixLength = static_cast<int>(wcslen(SaveNamePrefix.c_str()));
    NextSaveNumber = 1;

    if (Mode == kNnLoadSaveModeLoad)
    {
        Title->SetProperty(kTextPropSetIds, 0x588, 0);
        HideControl(Buttons[0]);
        ShowControl(Buttons[1]);
        HideControl(ButtonTexts[0]);
        ShowControl(ButtonTexts[1]);
    }
    else if (Mode == kNnLoadSaveModeSave)
    {
        Title->SetProperty(kTextPropSetIds, 0x587, 0);
        ShowControl(Buttons[0]);
        HideControl(Buttons[1]);
        ShowControl(ButtonTexts[0]);
        HideControl(ButtonTexts[1]);
    }
    else if (Mode == kNnLoadSaveModeStoryStar)
    {
        Title->SetProperty(kTextPropSetIds, 0x588, 0);
        HideControl(Buttons[0]);
        ShowControl(Buttons[1]);
        HideControl(ButtonTexts[0]);
        ShowControl(ButtonTexts[1]);
        HideControl(Buttons[2]);
        HideControl(Buttons[4]);
        HideControl(ButtonTexts[2]);
        HideControl(ButtonTexts[4]);
        HideControl(CloseButton);
    }
    else
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Invalid load/save mode %d\n", kNnLoadSaveCpp, 0x1bc,
              Mode);
    }

    DrawLayer = 0x6e;
    DrawSubLayer = 6;
    bCursorDrawFlag = NnModalPauseGame(); // FUN_004a7670
    RegisterEvent(USER_CANCEL); // FUN_005760d0 on the +0x32c face
    RefreshSaveList();
    SelectSave(-1);
    return Win3D::Create(NewParent, Data); // FUN_004b5060
}

void NnLoadSave::ResumeAndDelete()
{
    NnModalResumeGame(bCursorDrawFlag);
    NameList.Destroy();
    TimeList.Destroy();
    ReleaseMouse();
    Win3DDeleteTree(this); // FUN_0043d2a0, inlined in retail
}

void NnLoadSave::ClearListsAndSelection()
{
    NameList.ItemCount = 0;
    NameList.SelectedIndex = -1;
    TimeList.ItemCount = 0;
    TimeList.SelectedIndex = -1;
    SelectSave(-1);
}

WinControl* NnLoadSave::ButtonTextFor(uintptr_t Button) const
{
    // Port-only helper for the lookup retail inlines at 0x00487067 and 0x004870f0.
    for (int Index = 0; Index < 5; ++Index)
    {
        if (Button == reinterpret_cast<uintptr_t>(Buttons[Index]))
            return ButtonTexts[Index];
    }
    return nullptr;
}

bool NnLoadSave::OnEvent(unsigned MsgId)
{
    // LAB_00486b80 (RET 4): only USER_CANCEL (0xf), which is consumed either way; mode 3 keeps
    // the window.
    if (MsgId != USER_CANCEL)
        return false;
    if (Mode != kNnLoadSaveModeStoryStar)
        ResumeAndDelete();
    return true;
}

void NnLoadSave::Draw()
{
    // FUN_00486c10: the software cursor is forced on.
    if (GetClientMouseCursorDrawFlag() == 0)
        SetClientMouseCursorDrawFlag(1);
    Win3DFlushBeforeOverlayDraw();
    WinControl::Draw();
    if (bListRefreshed != 0)
        bSaveAllowed = 1;
}

bool NnLoadSave::ProcessMessage(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_00486c50 (RET 0xc): the list clicks (0xf001 / 0xf002), NN_Confirm's answer (0x3006)
    // and USER_CANCEL / USER_ENTER relays (0xf003 / 0xf004) go on to slot 42; always false.
    if (Msg == kWinMsgPress || Msg == kWinMsgDoubleClick || Msg == kWinMsgConfirmAnswer || Msg == kWinMsgEscape || Msg == kWinMsgEnter)
        SetProperty(static_cast<int>(Msg), A, B);
    return false;
}

intptr_t NnLoadSave::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00486ca0 (RET 0xc). Every path returns 0; nothing reaches WinControl's switch.
    wchar_t Format[0x400];
    wchar_t Text[0x400];

    if (static_cast<unsigned>(Msg) > 0x3006)
    {
        // 0xf001..0xf004 (jump table 0x00487534).
        int const Row = static_cast<int>(B);
        switch (static_cast<unsigned>(Msg))
        {
        case kWinMsgPress:
        {
            // A list click; the date list follows the name list.
            if (Row >= -1)
            {
                if (Row < NameList.ItemCount)
                    NameList.SelectedIndex = Row;
                if (Row >= -1 && Row < TimeList.ItemCount)
                    TimeList.SelectedIndex = Row;
            }
            int const Selected = NameList.SelectedIndex;
            if (Selected >= -1 && Selected < TimeList.ItemCount)
                TimeList.SelectedIndex = Selected;
            SelectSave(Selected);
            return 0;
        }
        case kWinMsgDoubleClick:
        {
            // A double click loads, in modes 1 and 3.
            if (Mode != kNnLoadSaveModeLoad && Mode != kNnLoadSaveModeStoryStar)
                return 0;
            if (Row >= -1)
            {
                if (Row < NameList.ItemCount)
                    NameList.SelectedIndex = Row;
                if (Row >= -1 && Row < TimeList.ItemCount)
                    TimeList.SelectedIndex = Row;
            }
            int const Selected = NameList.SelectedIndex;
            if (Selected >= -1 && Selected < TimeList.ItemCount)
                TimeList.SelectedIndex = Selected;
            SelectSave(Selected);
            if (Selected == -1)
                return 0;
            LoadSave(Selected);
            return 0;
        }
        case kWinMsgEscape:
            // USER_CANCEL through the Event::Handler face's slot 0.
            OnEvent(0xf);
            return 0;
        case kWinMsgEnter:
            if (bSaveAllowed != 0 && Mode == kNnLoadSaveModeSave)
                SaveGame();
            return 0;
        default:
            return 0;
        }
    }

    if (Msg == kWinMsgConfirmAnswer)
    {
        // NN_Confirm answered (A == 1 yes) the question +0x338 names.
        switch (ConfirmMode)
        {
        case 1:
            // Delete the selected save (FUN_005aa8f0 on DAT_0067e7b8).
            if (A == 1 && NameList.SelectedIndex != -1)
            {
                CHARACTER_ID Id;
                memcpy(&Id, &Entries[NameList.SelectedIndex].Info->Id, sizeof(CHARACTER_ID));
                Entries.clear();
                ClearListsAndSelection();
                if (G_LoginMgr.DestroyCharacter(Id) != 1)
                {
                    FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kNnLoadSaveCpp, 0x3ab,
                          "Failed to delete character\n");
                }
                RefreshSaveList();
            }
            break;
        case 2:
            // Back to the main menu at the next Update.
            if (A == 1)
                bExitToMainMenu = 1;
            break;
        case 3:
            break;
        case 4:
            // Overwrite: the save FUN_00487550 would have made, with +0x368 as the character.
            if (A == 1)
            {
                CHARACTER_ID Id;
                memcpy(&Id, &OverwriteId, sizeof(CHARACTER_ID));
                Entries.clear(); // FUN_00488c20
                ClearListsAndSelection();
                bSaveAllowed = 0;
                bListRefreshed = 0;
                if (G_Server != nullptr)
                {
                    G_Server->SaveGame(Id, SaveName, GetPlayerConnectionId()); // +0xe8
                }
                else
                {
                    FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kNnLoadSaveCpp, 0x366,
                          "No server, cannot save\n");
                }
                RefreshSaveList();
                bListRefreshed = 1;
            }
            break;
        default:
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: Invalid confirmation mode %d\n", kNnLoadSaveCpp,
                  0x2dc, ConfirmMode);
            break;
        }
        ConfirmMode = 0;
        if (DescripEdit != nullptr)
            DescripEdit->Activate();
        NameList.bSelectable = 1;
        TimeList.bSelectable = 1;
        return 0;
    }

    switch (Msg)
    {
    case kWinPropPlaceAtOffset:
    {
        // Each list goes to the window position plus its offset (float adds).
        float const* const Offset3 = reinterpret_cast<float const*>(A);
        SetOffset(Offset3[0], Offset3[1], Offset3[2]);
        SetPosition(Position);
        float const NamePosition[3] = {Position[0] + NameListOffset[0],
                                       Position[1] + NameListOffset[1],
                                       Position[2] + NameListOffset[2]};
        NameList.SetPosition(NamePosition);
        float const TimePosition[3] = {Position[0] + TimeListOffset[0],
                                       Position[1] + TimeListOffset[1],
                                       Position[2] + TimeListOffset[2]};
        TimeList.SetPosition(TimePosition);
        return 0;
    }
    case kWinNotifyClick:
    {
        // A button was pressed (A = the sender).
        if (A == reinterpret_cast<uintptr_t>(Buttons[0]))
        {
            if (bButtonEnabled[0] != 0)
                SaveGame();
            return 0;
        }
        if (A == reinterpret_cast<uintptr_t>(Buttons[1]))
        {
            if (bButtonEnabled[1] == 0 || NameList.SelectedIndex == -1)
                return 0;
            LoadSave(NameList.SelectedIndex);
            return 0;
        }
        if (A == reinterpret_cast<uintptr_t>(Buttons[2]))
        {
            // "Delete <name>?"
            if (bButtonEnabled[2] == 0 || NameList.SelectedIndex == -1)
                return 0;
            wchar_t const* const Name = Entries[NameList.SelectedIndex].Name.c_str();
            ConfirmMode = 1;
            if (DescripEdit != nullptr)
                DescripEdit->Deactivate(false); // FUN_0057c7d0(0)
            GetIdsStringWWithRawFallback(0x75c, Format, 0x400);
            // MSVCRT swprintf has no count in retail; the port bounds it by the buffer.
            swprintf(Text, 0x400, Format, Name);
            NnConfirmOpen(this, Text, "ConfirmPlayerText");
            return 0;
        }
        if (A == reinterpret_cast<uintptr_t>(Buttons[3]))
        {
            // "Quit to the main menu?"; both lists stop taking clicks while it is up.
            if (bButtonEnabled[3] == 0)
                return 0;
            GetIdsStringWWithRawFallback(0x75b, Text, 0x400);
            ConfirmMode = 2;
            if (DescripEdit != nullptr)
                DescripEdit->Deactivate(false);
            NameList.bSelectable = 0;
            TimeList.bSelectable = 0;
            NnConfirmOpen(this, Text, nullptr);
            return 0;
        }
        if (A == reinterpret_cast<uintptr_t>(Buttons[4]))
        {
            // Resume: ResumeAndDelete's steps; here retail calls FUN_0043d2a0(this) rather than
            // inlining it.
            if (bButtonEnabled[4] == 0)
                return 0;
            NnModalResumeGame(bCursorDrawFlag);
            NameList.Destroy();
            TimeList.Destroy();
            ReleaseMouse();
            Win3DDeleteTree(this);
            return 0;
        }
        if (A == reinterpret_cast<uintptr_t>(DescripButton))
            return 0;
        if (A == reinterpret_cast<uintptr_t>(CloseButton))
        {
            ResumeAndDelete();
            return 0;
        }
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kNnLoadSaveCpp, 0x260, "Unknown button\n");
        return 0;
    }
    case kWinNotifyHighlight:
    {
        // The pointer entered a button: its caption, when shown, highlights.
        WinControl* const Caption = ButtonTextFor(A);
        if (Caption == nullptr || (Caption->Flags & 1) == 0)
            return 0;
        Caption->SetProperty(kWinPropBlink, 1, 0);
        return 0;
    }
    case kWinNotifyUnhighlight:
    {
        // It left: the colour goes back to DAT_00679bb4.
        WinControl* const Caption = ButtonTextFor(A);
        if (Caption == nullptr || (Caption->Flags & 1) == 0)
            return 0;
        Caption->SetProperty(kWinPropBlink, 0, 0);
        Caption->SetProperty(kWinPropColour, reinterpret_cast<uintptr_t>(G_HudChromeColorRgba), 0);
        return 0;
    }
    case kWinNotifyScrollValue:
        // The scroll bar moved: B is the first visible row of both lists.
        NameList.FirstVisible = static_cast<int>(B);
        TimeList.FirstVisible = static_cast<int>(B);
        return 0;
    default:
        return 0;
    }
}

void NnLoadSave::SaveGame()
{
    // FUN_00487550.
    FNnLoadSaveEntry const* Selected = nullptr;
    if (NameList.SelectedIndex != -1)
    {
        Selected = &Entries[NameList.SelectedIndex];
        OverwriteId = Selected->Info->Id;
    }

    if (DescripEdit != nullptr)
        DescripEdit->GetText(SaveName, 0x1f);
    else
        SaveName[0] = 0;

    // A different name than the selected row's makes a new save.
    if (Selected != nullptr)
    {
        if (wcscmp(Selected->Name.c_str(), SaveName) != 0)
            OverwriteId.Buffer[0] = 0;
    }

    // No name: the prefix and the next number.
    if (SaveName[0] == 0)
    {
        wcscpy(SaveName, SaveNamePrefix.c_str());
        _ltow(NextSaveNumber, SaveName + SaveNamePrefixLength, 10);
    }

    if (OverwriteId.Buffer[0] != 0)
    {
        // "Overwrite?"; retail inlines FUN_004889e0 here.
        wchar_t Text[0x400];
        GetIdsStringWWithRawFallback(0x706, Text, 0x400);
        ConfirmMode = 4;
        if (DescripEdit != nullptr)
            DescripEdit->Deactivate(false);
        NameList.bSelectable = 0;
        TimeList.bSelectable = 0;
        NnConfirmOpen(this, Text, "ConfirmText");
        return;
    }

    // IServer::SaveGame is slot +0xe8.
    CHARACTER_ID Id;
    memcpy(&Id, &OverwriteId, sizeof(CHARACTER_ID));
    Entries.clear();
    ClearListsAndSelection();
    bSaveAllowed = 0;
    bListRefreshed = 0;
    if (G_Server != nullptr)
    {
        G_Server->SaveGame(Id, SaveName, GetPlayerConnectionId());
    }
    else
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kNnLoadSaveCpp, 0x366,
              "No server, cannot save\n");
    }
    RefreshSaveList();
    bListRefreshed = 1;
}

void NnLoadSave::RefreshSaveList()
{
    // FUN_004877f0.
    Entries.clear();

    if (G_LoginMgr.RequestCharacterInfo() == 1)
    {
        int Count = 0;
        MetaNode<CCharacterSet::CharacterInfo>* Node = nullptr;
        while (G_LoginMgr.Characters.traverse(Node, true))
            Count = Count + 1;
        if (Entries.capacity() < static_cast<size_t>(Count))
            Entries.reserve(static_cast<size_t>(Count));

        // One row per record that is not the restart save (+0x200).
        for (int Index = 0; Index < Count; ++Index)
        {
            CCharacterSet::CharacterInfo* const Info = G_LoginMgr.CharacterAt(Index);
            if (Info->bIsRestart != 0)
                continue;
            FNnLoadSaveEntry Entry;
            Entry.Info = Info;
            wchar_t const* const EntryText = NnLoadSaveEntryText(Info);
            Entry.Name.assign(EntryText, wcslen(EntryText));
            wchar_t const* const TimeText =
                filetime_to_wstring(*reinterpret_cast<_FILETIME const*>(&Info->TimestampLow));
            Entry.Time.assign(TimeText, wcslen(TimeText));
            Entries.push_back(Entry);
        }

        // Saves failed while away (DAT_0067e984).
        if (G_LoginMgr.SaveFailureCount != 0)
        {
            ConfirmMode = 3;
            if (DescripEdit != nullptr)
                DescripEdit->Deactivate(false);
            NnErrorOpen(0x5cd, this);
        }
    }
    else
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kNnLoadSaveCpp, 0x446,
              "Failed to retrieve available save games\n");
    }

    // The next generated number: the largest N among rows named <prefix>N (N <= 1000000000),
    // plus 1.
    int Largest = 0;
    for (size_t Index = 0; Index < Entries.size(); ++Index)
    {
        wchar_t const* const Name = Entries[Index].Name.c_str();
        if (wcsncmp(Name, SaveNamePrefix.c_str(), static_cast<size_t>(SaveNamePrefixLength)) != 0)
            continue;
        wchar_t* End = nullptr;
        long const Value = wcstol(Name + SaveNamePrefixLength, &End, 10);
        if (End == nullptr || *End != 0)
            continue;
        if (Value > 1000000000)
            continue;
        if (Value <= Largest)
            continue;
        Largest = static_cast<int>(Value);
    }
    NextSaveNumber = Largest + 1;

    int Count = static_cast<int>(Entries.size());
    if (Count > 0x100)
        Count = 0x100;
    NameList.ItemCount = Count;
    TimeList.ItemCount = Count;
    for (int Index = 0; Index < Count; ++Index)
    {
        NameList.SetItemText(Index, Entries[Index].Name.c_str());
        TimeList.SetItemText(Index, Entries[Index].Time.c_str());
    }

    if (Count - VisibleRows + 1 <= 1)
    {
        Scroll->SetRange(0.0f, 1.0f);
        HideControl(Scroll);
    }
    else
    {
        Scroll->SetRange(0.0f, static_cast<float>(Count));
        ShowControl(Scroll);
    }
}

void NnLoadSave::SelectSave(int Index)
{
    // FUN_00487cb0 (RET 4).
    if (Index == -1)
    {
        for (int Value = 0; Value < 4; ++Value)
            InfoValues[Value]->SetProperty(kTextPropSetIds, 0x595, 0);
        if (DescripEdit != nullptr)
            DescripEdit->ClearText();
        ButtonTexts[0]->SetProperty(kWinPropDisable, 0, 0);
        bButtonEnabled[0] = 1;
        ButtonTexts[1]->SetProperty(kWinPropDisable, 1, 0);
        bButtonEnabled[1] = 0;
        ButtonTexts[2]->SetProperty(kWinPropDisable, 1, 0);
        bButtonEnabled[2] = 0;
        ButtonTexts[3]->SetProperty(kWinPropDisable, 0, 0);
        bButtonEnabled[3] = 1;
        ButtonTexts[4]->SetProperty(kWinPropDisable, 0, 0);
        bButtonEnabled[4] = 1;
        return;
    }

    int const Count = static_cast<int>(Entries.size());
    if (Index < 0 || Index >= Count)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Invalid index %d for savegame\n", kNnLoadSaveCpp,
              0x4ea, Index);
        return;
    }

    CCharacterSet::CharacterInfo const* const Info = Entries[Index].Info;

    Universe::ISystem const* const System = Universe::get_system(Info->SystemId);
    if (System != nullptr)
        InfoValues[0]->SetProperty(kTextPropSetIds, (uintptr_t)System->GetStridName(), 0);
    else
        InfoValues[0]->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(kEmptyText), 0);

    // L"%d" is 005d0ec4.
    wchar_t Number[0x400];
    swprintf(Number, 0x400, L"%d", Info->Money);
    InfoValues[1]->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(Number), 0);
    swprintf(Number, 0x400, L"%d", Info->Rank);
    InfoValues[2]->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(Number), 0);

    // GetShip is not null-tested.
    if (Info->ShipArchetype != 0)
    {
        Archetype::Ship const* const Ship = Archetype::GetShip(Info->ShipArchetype);
        InfoValues[3]->SetProperty(kTextPropSetIds, static_cast<uintptr_t>(Ship->IdsName), 0);
    }
    else
    {
        InfoValues[3]->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(kEmptyText), 0);
    }

    // NnLoadSaveEntryText's body, inlined in retail.
    if (DescripEdit != nullptr)
    {
        wchar_t const* Description = nullptr;
        if (Info->DescripStrId != 0)
        {
            if (GetIdsStringWWithRawFallback(Info->DescripStrId, G_NnLoadSaveText, 0x100) == 0)
            {
                FDUMP(kFdumpError, "%s(%d) : *** ERROR: Failed to find string resource %d\n",
                      kNnLoadSaveCpp, 0x64d, Info->DescripStrId);
            }
            Description = G_NnLoadSaveText;
        }
        else
        {
            Description = Info->Description.c_str();
        }
        DescripEdit->ClearText();
        DescripEdit->SetText(Description, nullptr);
    }

    // The restart / autosave records (+0x200 / +0x201) cannot be saved over or deleted.
    if (Info->bIsRestart == 0 && Info->bIsAutoSave == 0)
    {
        for (int Button = 0; Button < 5; ++Button)
        {
            ButtonTexts[Button]->SetProperty(kWinPropDisable, 0, 0);
            bButtonEnabled[Button] = 1;
        }
        return;
    }
    ButtonTexts[0]->SetProperty(kWinPropDisable, 1, 0);
    bButtonEnabled[0] = 0;
    ButtonTexts[1]->SetProperty(kWinPropDisable, 0, 0);
    bButtonEnabled[1] = 1;
    ButtonTexts[2]->SetProperty(kWinPropDisable, 1, 0);
    bButtonEnabled[2] = 0;
    ButtonTexts[3]->SetProperty(kWinPropDisable, 0, 0);
    bButtonEnabled[3] = 1;
    ButtonTexts[4]->SetProperty(kWinPropDisable, 0, 0);
    bButtonEnabled[4] = 1;
}

void NnLoadSave::LoadSave(int Index)
{
    // FUN_004880f0 (RET 4).
    int const Count = static_cast<int>(Entries.size());
    if (Index < 0 || Index >= Count)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Invalid index %d for savegame\n", kNnLoadSaveCpp,
              0x502, Index);
        return;
    }
    CCharacterSet::CharacterInfo const* const Info = Entries[Index].Info;
    QueueLoadGameCharacter(&Info->Id, Info->bIsAutoSave != 0);
    ResumeAndDelete();
    FadeIn(true);
}

void NnLoadSave::Update()
{
    // FUN_004881f0.
    WinControl::Update();

    // Outside the main-menu question, the row under the pointer in either list is highlighted
    // in both (ListBox +0x80).
    if (ConfirmMode != 2)
    {
        int Row = -1;
        if (NameList.HoverIndex != -1)
            Row = NameList.HoverIndex;
        if (TimeList.HoverIndex != -1)
            Row = TimeList.HoverIndex;
        if (Row >= -1)
        {
            if (Row < NameList.ItemCount)
                NameList.HighlightIndex = Row;
            if (Row >= -1 && Row < TimeList.ItemCount)
                TimeList.HighlightIndex = Row;
        }
    }

    // Main menu confirmed: USER_EXIT_GAME (0xc5), then close.
    if (bExitToMainMenu == 0)
        return;
    bExitToMainMenu = 0;
    HudPostCommand(USER_EXIT_GAME, 0);
    ResumeAndDelete();
}

void OpenNnLoadSave(int Mode)
{
    // FUN_00488970.
    if (Win3DFindByClassName("NN_LoadSave") != nullptr)
        return;
    Win3D* const Created = WinFactoryCreate("NN_LoadSave", "NN_LoadSave", nullptr, &Mode);
    if (Created == nullptr)
        return;
    NnLoadSave* const LoadSave = static_cast<NnLoadSave*>(Created);
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    LoadSave->SetProperty(kWinPropPlaceAtOffset, reinterpret_cast<uintptr_t>(Zero), 0);
    LoadSave->CaptureMouse();
}

wchar_t const* NnLoadSaveEntryText(CCharacterSet::CharacterInfo const* Info)
{
    // FUN_00488b70 (cdecl).
    if (Info->DescripStrId != 0)
    {
        if (GetIdsStringWWithRawFallback(Info->DescripStrId, G_NnLoadSaveText, 0x100) == 0)
        {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: Failed to find string resource %d\n",
                  kNnLoadSaveCpp, 0x64d, Info->DescripStrId);
        }
        return G_NnLoadSaveText;
    }
    return Info->Description.c_str();
}
