// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_Pause (NN_Pause.cpp). See nn_pause.h.

#include "NeuroNet/nn_pause.h"

#include "Core/globals.h"               // IsClientServerTickPaused (FUN_0042d730), SetClientServerTickPause
#include "GameFlow/Thorn/gf_script_object.h"      // G_GfScriptManager (DAT_0066d2d0), CanSkipAll (FUN_004568a0)
#include "Space/HUD/hud_command_dispatch.h"  // HudPushInputFocus / HudPopInputFocus / HudPostCommand
#include "Space/HUD/hud_target.h"
#include "Common/Fonts/ids_strings.h"           // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "Common/input_manager.h"         // MouseSetCursor, Get/SetClientMouseCursorDrawFlag
#include "Interface/wire_controls.h"          // WireButtonControl::WireState2 (+0x35d)
#include "Interface/menu_enter_state.h"      // MenuNavSetIntroModeRequest (FUN_00564b10, DAT_0067c200)
#include "NeuroNet/nn_load_save.h"           // OpenNnLoadSave (FUN_00488970)
#include "GameFlow/menu_universe_base.h"    // GetClientLocationCurrentBase (DAT_0066873c)
#include "GameFlow/nav_bar.h"               // NavBarCloseService (FUN_004424f0)
#include "NeuroNet/nn_confirm.h"            // NnConfirmOpen (FUN_004889e0)
#include "NeuroNet/nn_inventory.h"          // IsPlayerDockedRetail (FUN_004c4810)
#include "NeuroNet/nn_modal.h"              // G_bNnScreenUp, NnMainButtonOnModalOpen, NnFloatBits
#include "NeuroNet/nn_rollover.h"           // NnRolloverHideGlobal (FUN_004b65a0)
#include "Space/player_behavior.h"       // GetPlayerObjRW (FUN_0054baf0)
#include "Interface/win3d.h"
#include "Interface/win3d_renderables.h"     // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "Interface/win_factory.h"

#include "common_misc.h"           // SinglePlayer
#include "dacom.h"                 // FDUMP

#include <stdio.h>
#include <windows.h>

// Retail DAT_00672144 -- WINFACTORY<NN_Pause> static (LAB_004a6ad0 -> FUN_004a76b0; Create
// FUN_004a7720; atexit LAB_004a6af0 -> FUN_00579380).
WINFACTORY<NnPause> G_NnPauseFactory("NN_Pause");

namespace
{
    char const kPauseBlackPlaneMesh[] = "Interface\\NeuroNet\\MISC_bigblackplane.3db"; // 005d21b0
    char const kPauseFrameMesh[] = "Interface\\NeuroNet\\Options\\escapewindow2.cmp";  // 005d4720
}

NnPause::NnPause()
    : bCursorDrawFlag(0)
    , bClosePending(0)
    , Title(nullptr)
{
    // Inlined into FUN_004a7720 (0x004a7759). Retail leaves the other members to Create, which
    // writes them before anything reads them; the port zeroes them here.
    Pad332[0] = 0;
    Pad332[1] = 0;
    for (int Index = 0; Index < 5; ++Index)
    {
        ButtonBox[Index] = nullptr;
        ButtonText[Index] = nullptr;
        ButtonAction[Index] = 0;
    }
}

NnPause::~NnPause()
{
    // FUN_004a6b20.
    HudPopInputFocus(AsHudCmdHandler());
}

bool NnPause::Create(Win3D* NewParent, void* Data)
{
    // FUN_004a6ba0 (RET 8).
    // No DAT_0066d359 test before closing NN_MainButton's panel here, unlike NN_Exit.
    NnMainButtonOnModalOpen();
    NavBarCloseService(true); // FUN_004424f0(1)
    NnRolloverHideGlobal();
    MouseSetCursor("arrow", false);

    float const Zero[3] = {0.0f, 0.0f, 0.0f};

    // Flags: NoZEnable 0, ZClear 0, NoMeshRender 0, NoWireRender 1, NoColoring 1,
    // RenderToWindow 0, NoClearRender 1.
    float const PlanePosition[3] = {0.0f, 0.0f, -1.0f};
    CreateStaticControl("Pause_BlackPlane", kPauseBlackPlaneMesh, PlanePosition, nullptr, Zero, 0,
                        0, 0, 1, 1, 0, 1, 0, 0, 0, Zero);

    // {0, 0.009, -1}
    float const FramePosition[3] = {0.0f, NnFloatBits(0x3c1374c0), -1.0f};
    CreateStaticControl("PauseFrame", kPauseFrameMesh, FramePosition, nullptr, Zero, 0, 0, 0, 1, 1,
                        0, 1, 0, 0, 0, Zero);

    // {-1.49e-8 (0xb2800000), 0.22, -1}, 0.35 x 0.03, 2.7.
    float const TitlePosition[3] = {NnFloatBits(0xb2800000), NnFloatBits(0x3e6147af), -1.0f};
    Title = CreateTextControl("PauseTitle", "PageHeader", TitlePosition, nullptr,
                              NnFloatBits(0x3eb33333), NnFloatBits(0x3cf5c28f),
                              NnFloatBits(0x402ccccd), 0, 1, 0, 0);

    // The button actions; the first is always 1 (resume). FUN_004568a0 (CanSkipAll) is asked
    // before the docked byte is tested (0x004a6ce2).
    int Count = 0;
    if (SinglePlayer())
    {
        bool const bDocked = GetClientLocationCurrentBase() != 0;
        bool const bCanSkip = G_GfScriptManager.CanSkipAll();
        if (bCanSkip && bDocked)
        {
            ButtonAction[1] = 2;
            ButtonAction[2] = 3;
            ButtonAction[3] = 4;
            ButtonAction[4] = 5;
            Count = 5;
        }
        else
        {
            ButtonAction[1] = 3;
            ButtonAction[2] = 4;
            ButtonAction[3] = 5;
            Count = 4;
        }
    }
    else
    {
        ButtonAction[1] = 6;
        ButtonAction[2] = 7;
        ButtonAction[3] = 4;
        ButtonAction[4] = 5;
        Count = 5;
    }
    ButtonAction[0] = 1;

    // Row origin / step: 0.149 / 0.0825, or 0.128375 / 0.09625 for four rows.
    float RowOrigin = NnFloatBits(0x3e189375);
    float RowStep = NnFloatBits(0x3da8f5c3);
    if (Count == 4)
    {
        RowOrigin = NnFloatBits(0x3e0374bd);
        RowStep = NnFloatBits(0x3dc51eb9);
    }

    // Y = origin - i * step, kept in the x87 registers and stored once as float.
    for (int Index = 0; Index < Count; ++Index)
    {
        float const RowY = static_cast<float>(static_cast<double>(RowOrigin) -
                                              static_cast<double>(Index) *
                                                  static_cast<double>(RowStep));
        char Name[0x100];

        // Half extents {0.175, 0.028, 0}.
        sprintf(Name, "PauseButtonBox%d", Index);
        float const BoxPosition[3] = {0.0f, RowY, -1.0f};
        float const BoxExtents[3] = {NnFloatBits(0x3e333333), NnFloatBits(0x3ce56042), 0.0f};
        ButtonBox[Index] = CreateWireButtonControl(Name, nullptr, BoxPosition, BoxExtents, 0, 0, 0,
                                                   0, 0);
        // The last box flushes the line batchers after its Draw (+0x35d |= 1, no null test).
        if (Index == Count - 1)
        {
            WireButtonControl* const Wire = static_cast<WireButtonControl*>(ButtonBox[Index]);
            Wire->WireState2 = static_cast<unsigned char>(Wire->WireState2 | kWire2FlushBatchers);
        }

        // The 0.005 is DAT_005d3e44.
        sprintf(Name, "PauseButtonText%d", Index);
        float const TextY = static_cast<float>(static_cast<double>(RowY) +
                                               static_cast<double>(NnFloatBits(0x3ba3d70a)));
        float const TextPosition[3] = {NnFloatBits(0x32300000), TextY, -1.0f};
        ButtonText[Index] = CreateTextControl(Name, "ButtonText", TextPosition, nullptr,
                                              NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f),
                                              NnFloatBits(0x3fc00000), 0, 1, 0, 0);
    }

    for (int Index = Count; Index < 5; ++Index)
    {
        ButtonBox[Index] = nullptr;
        ButtonText[Index] = nullptr;
        ButtonAction[Index] = 0;
    }

    // No null test on the title.
    if (SinglePlayer())
        Title->SetProperty(kTextPropSetIds, 0x71c, 0);
    else
        Title->SetProperty(kTextPropSetIds, 0x724, 0);

    // Each row's caption and tooltip by its action.
    for (int Index = 0; Index < Count; ++Index)
    {
        unsigned CaptionIds = 0;
        unsigned ToolTipIds = 0;
        switch (ButtonAction[Index])
        {
        case 1: CaptionIds = 0x71d; ToolTipIds = 0x33a; break;
        case 2: CaptionIds = 0x71e; ToolTipIds = 0x33b; break;
        case 3: CaptionIds = 0x71f; ToolTipIds = 0x33c; break;
        case 4: CaptionIds = 0x720; ToolTipIds = 0x33d; break;
        case 5: CaptionIds = 0x721; ToolTipIds = 0x33e; break;
        case 6: CaptionIds = 0x752; ToolTipIds = 0x33f; break;
        case 7: CaptionIds = 0x725; ToolTipIds = 0x340; break;
        default: continue;
        }
        ButtonText[Index]->SetProperty(kTextPropSetIds, CaptionIds, 0);
        ButtonBox[Index]->SetProperty(kWinPropToolTip, ToolTipIds, 0);
    }

    DrawLayer = 0x64;
    DrawSubLayer = 6;
    // FUN_004a7670's body, inlined in retail.
    bCursorDrawFlag = NnModalPauseGame();
    HudPushInputFocus(AsHudCmdHandler()); // FUN_00577b40
    RegisterEvent(USER_ENTER);
    RegisterEvent(USER_CANCEL);
    RegisterEvent(USER_PAUSE);

    return Win3D::Create(NewParent, Data); // FUN_004b5060
}

void NnPause::Update()
{
    // LAB_004a70d0: the software cursor is forced on, then a pending close deletes the window.
    WinControl::Update();
    if (GetClientMouseCursorDrawFlag() == 0)
        SetClientMouseCursorDrawFlag(1);
    if (bClosePending == 0)
        return;
    Win3DDeleteTree(this); // FUN_0043d2a0, inlined in retail
}

void NnPause::Draw()
{
    // FUN_00488960.
    Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0
    WinControl::Draw();            // tail call FUN_005a0760
}

bool NnPause::ProcessMessage(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_004a70a0 (RET 0xc): NN_Confirm's answer (0x3006) goes on to slot 42.
    if (Msg == kWinMsgConfirmAnswer)
        SetProperty(kWinMsgConfirmAnswer, A, B);
    return false;
}

intptr_t NnPause::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004a7130 (RET 0xc). Nothing reaches WinControl's switch: every path returns 0 here.
    (void)B;
    if (Msg == kWinNotifyClick)
    {
        int Index = 0;
        while (Index < 5)
        {
            if (A == reinterpret_cast<uintptr_t>(ButtonBox[Index]))
                break;
            Index = Index + 1;
        }
        if (Index == 5)
            return 0;
        // Retail's -1 test (0x004a71f4) cannot hold here.
        DoAction(ButtonAction[Index]);
        return 0;
    }
    if (Msg == kWinPropPlaceAtOffset)
    {
        float const* const Offset3 = reinterpret_cast<float const*>(A);
        SetOffset(Offset3[0], Offset3[1], Offset3[2]);
        SetPosition(&Position.x);
        return 0;
    }
    if (Msg == kWinMsgConfirmAnswer && A == 1)
    {
        // NN_Confirm said yes: USER_EXIT_GAME (0xc5); the window goes at the next Update.
        HudPostCommand(USER_EXIT_GAME, 0);
        NnModalResumeGame(bCursorDrawFlag);
        ReleaseMouse();
        bClosePending = 1;
        return 0;
    }
    return 0;
}

bool NnPause::OnEvent(unsigned MsgId)
{
    // LAB_004a7070 (RET 4).
    if (MsgId == USER_CANCEL || MsgId == USER_ENTER || MsgId == USER_PAUSE)
    {
        DoAction(1);
        return true;
    }
    return false;
}

void NnPause::ResumeAndDelete()
{
    NnModalResumeGame(bCursorDrawFlag);
    ReleaseMouse();
    Win3DDeleteTree(this); // FUN_0043d2a0, inlined in retail
}

void NnPause::DoAction(int Action)
{
    // FUN_004a7210 (thiscall, RET 4).
    wchar_t Text[0x400];
    switch (Action)
    {
    case 0:
    case 1:
        ResumeAndDelete();
        return;
    case 2:
    case 3:
    {
        // Save / load. The window is deleted before the load/save one opens.
        SetClientServerTickPause(true);
        ResumeAndDelete();
        OpenNnLoadSave(Action == 2 ? 2 : 1);
        SetClientServerTickPause(false);
        return;
    }
    case 4:
    {
        SetClientServerTickPause(true);
        ResumeAndDelete();
        Win3D* const Preferences = WinFactoryCreate("NN_Preferences", "NN_Preferences", nullptr,
                                                    nullptr);
        if (Preferences != nullptr)
            static_cast<WinControl*>(Preferences)->CaptureMouse();
        SetClientServerTickPause(false);
        return;
    }
    case 5:
        // "Quit?"; the answer comes back through slot 23 (0x3006).
        GetIdsStringWWithRawFallback(0x75b, Text, 0x400);
        NnConfirmOpen(this, Text, nullptr);
        return;
    case 6:
        MenuNavSetIntroModeRequest(2); // DAT_0067c200
        HudPostCommand(USER_EXIT_GAME, 0);
        ResumeAndDelete();
        return;
    case 7:
        MenuNavSetIntroModeRequest(3);
        GetIdsStringWWithRawFallback(0x76b, Text, 0x400);
        NnConfirmOpen(this, Text, nullptr);
        return;
    default:
        return;
    }
}

void NnPause::CloseNow()
{
    // FUN_004a7560.
    ResumeAndDelete();
}

void NnPauseOpen()
{
    // FUN_004a75c0.
    if (G_bNnScreenUp)
        return;
    if (IsClientServerTickPaused())
        return;
    if (IsClientLocationInTransition()) // DAT_00668761
        return;
    if (GetClientMouseCursorDrawFlag() != 0)
    {
        if (!IsPlayerDockedRetail() && GetPlayerObjRW() == nullptr)
            return;
    }
    if (Win3DFindByClassName("NN_Pause") != nullptr)
        return;
    Win3D* const Created = WinFactoryCreate("NN_Pause", "NN_Pause", nullptr, nullptr);
    if (Created == nullptr)
        return;
    NnPause* const Pause = static_cast<NnPause*>(Created);
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    Pause->SetProperty(kWinPropPlaceAtOffset, reinterpret_cast<uintptr_t>(Zero), 0);
    Pause->CaptureMouse();
}
