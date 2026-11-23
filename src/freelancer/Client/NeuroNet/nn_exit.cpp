// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_exit.h"

#include "Core/globals.h"            // g_client.window (DAT_0067eca0)
#include "Space/HUD/hud_command_dispatch.h"
#include "Common/input_manager.h"      // MouseSetCursor, Get/SetClientMouseCursorDrawFlag
#include "GameFlow/nav_bar.h"            // NavBarCloseService (FUN_004424f0)
#include "NeuroNet/nn_pause.h"           // NnPause::CloseNow (FUN_004a7560)
#include "NeuroNet/nn_main_button.h"     // G_bNnKeepPanelOpen (DAT_0066d359)
#include "NeuroNet/nn_modal.h"
#include "NeuroNet/nn_story_star.h"      // StoryStarOnMissionMessage (FUN_004b9c60)
#include "Interface/win3d.h"
#include "Interface/win3d_renderables.h"  // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "Interface/win_factory.h"

#include "common_text.h"        // FmtStr

#include <stdio.h>
#include <windows.h>

// Retail DAT_0066db08 -- WINFACTORY<NN_Exit> static (LAB_00471740 -> FUN_00471fb0; Create
// FUN_00472020; atexit LAB_00471760 -> FUN_00579380).
WINFACTORY<NnExit> G_NnExitFactory("NN_Exit");

namespace
{
    char const kExitFrameMesh[] = "Interface\\NeuroNet\\Options\\alt-f4window.cmp";     // 005d04a8
    char const kExitTextFrameMesh[] = "Interface\\NeuroNet\\Options\\alt-f4textplain.3db"; // 005d046c
}

NnExit::NnExit()
    : ResultFn(nullptr)
    , ResultArg(nullptr)
    , bImmediate(0)
{
    // Inlined in FUN_00472020. Nothing else is written before Create.
}

NnExit::~NnExit()
{
    // FUN_00471790.
    HudPopInputFocus(AsHudCmdHandler());
}

bool NnExit::Create(Win3D* NewParent, void* Data)
{
    // FUN_00471810.
    //
    // Close NN_MainButton's panel unless something holds it open (DAT_0066d359).
    if (G_bNnKeepPanelOpen == 0)
        NnMainButtonOnModalOpen();
    NavBarCloseService(true); // FUN_004424f0(1)

    NnPause* const Pause = static_cast<NnPause*>(Win3DFindByName("NN_Pause"));
    if (Pause != nullptr)
        Pause->CloseNow();   // FUN_004a7560

    // Lower-case in retail; the lookup is stricmp.
    MouseSetCursor("arrow", false);

    float const Zero[3] = {0.0f, 0.0f, 0.0f};

    float FramePosition[3] = {0.0f, 0.0f, -1.0f};
    CreateStaticControl("ExitFrame", kExitFrameMesh, FramePosition, nullptr, Zero, 0, 0, 0, 1, 1,
                        0, 1, 0, 0, 0, Zero);

    float TextFramePosition[3] = {0.0f, NnFloatBits(0x3cf5c288), -1.0f};
    CreateStaticControl("ExitTextFrame", kExitTextFrameMesh, TextFramePosition, nullptr, Zero, 0,
                        0, 1, 0, 1, 0, 1, 0, 0, 0, Zero);

    float TextPosition[3] = {0.0f, NnFloatBits(0x3d0b4394), -1.0f};
    ExitText = CreateTextControl("ExitText", "PageHeader", TextPosition, nullptr,
                                 NnFloatBits(0x3eb851ec), NnFloatBits(0x3cf5c28f),
                                 NnFloatBits(0x3fc00000), 0, 1, 0, 0);

    // Two buttons side by side. The row coordinate i * step - origin is rounded to float; the
    // box and the caption add their offsets to it as the X component.
    float const kRowStep = NnFloatBits(0x3e4ccccd);     // DAT_005d043c 0.2
    float const kRowOrigin = NnFloatBits(0x3e6b851e);   // DAT_005d0438 0.23
    float const kBoxOffset = NnFloatBits(0x3e051eb8);   // DAT_005d0434 0.13
    float const kCaptionOffset = NnFloatBits(0x3e051eb9); // DAT_005d041c
    for (int Index = 0; Index < 2; ++Index)
    {
        float const Row = static_cast<float>(Index) * kRowStep - kRowOrigin;
        char Name[0x100];

        sprintf(Name, "ExitButtonBox%d", Index);
        float BoxPosition[3] = {Row + kBoxOffset, NnFloatBits(0xbd99999a), -1.0f};
        float BoxExtents[3] = {NnFloatBits(0x3db645a2), NnFloatBits(0x3cac0831), 0.0f};
        ButtonBox[Index] = CreateWireButtonControl(Name, "ui_select_item", BoxPosition,
                                                   BoxExtents, 0, 0, 0, 0, 0);

        sprintf(Name, "ExitButtonText%d", Index);
        float CaptionPosition[3] = {Row + kCaptionOffset, NnFloatBits(0xbd916873), -1.0f};
        ButtonText[Index] = CreateTextControl(Name, "ButtonText", CaptionPosition, nullptr,
                                              NnFloatBits(0x3e8a3d71), NnFloatBits(0x3cf5c28f),
                                              NnFloatBits(0x3fc00000), 0, 1, 0, 0);
    }

    // No null test on the three captions.
    ExitText->SetProperty(0x1e, 0x726, 0);
    ButtonText[0]->SetProperty(0x1e, 0x727, 0);
    ButtonText[1]->SetProperty(0x1e, 0x728, 0);

    DrawLayer = 0x6e;
    DrawSubLayer = 6;
    bCursorDrawFlag = NnModalPauseGame();
    HudPushInputFocus(AsHudCmdHandler());
    RegisterEvent(0x13);
    RegisterEvent(0x12);
    RegisterEvent(0xf);
    RegisterEvent(0x11);

    return Win3D::Create(NewParent, Data); // FUN_004b5060
}

void NnExit::Close(int Result)
{
    NnModalResumeGame(bCursorDrawFlag);
    if (ResultFn != nullptr)
        ResultFn(ResultArg, Result);
    ReleaseMouse();
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        Win3D* const Next = Child->NextSibling;
        Win3DDeleteTree(Child);
        Child = Next;
    }
    Destroy();
    delete this;
}

void NnExit::Update()
{
    // FUN_00471e00 -- no WinControl Update call. With +0x338 set the dialog confirms at once:
    // the same sequence as the confirm button, DestroyWindow included.
    if (bImmediate == 0)
        return;
    Close(0);
    DestroyWindow(g_client.window);
}

void NnExit::Draw()
{
    // FUN_00471b60.
    if (GetClientMouseCursorDrawFlag() == 0)
        SetClientMouseCursorDrawFlag(1);
    Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0
    WinControl::Draw();            // FUN_005a0760
}

intptr_t NnExit::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00471cb0. Nothing reaches WinControl's switch: every path returns 0 here.
    (void)B;
    if (Msg == 0x26)
    {
        int Index = 0;
        while (Index < 2)
        {
            if (A == reinterpret_cast<uintptr_t>(ButtonBox[Index]))
                break;
            Index = Index + 1;
        }
        if (Index == 2)
            return 0;
        // Box 0 confirms and closes the game window, box 1 cancels.
        if (Index == 0)
        {
            Close(0);
            DestroyWindow(g_client.window);
            return 0;
        }
        Close(1);
        return 0;
    }
    if (Msg == 0x42)
    {
        float const* const Offset3 = reinterpret_cast<float const*>(A);
        SetOffset(Offset3[0], Offset3[1], Offset3[2]);
        SetPosition(Position);
        return 0;
    }
    return 0;
}

bool NnExit::OnEvent(unsigned MsgId)
{
    // LAB_00471b90.
    switch (MsgId)
    {
    case 0xf:
    case 0x12:
        Close(1);
        return true;
    case 0x11:
    case 0x13:
        Close(0);
        DestroyWindow(g_client.window);
        return true;
    default:
        return false;
    }
}

void NnExitPostShipLostMessage()
{
    // FUN_00471e80.
    FmtStr Text;
    Text.begin_mad_lib(0x723);
    Text.end_mad_lib();
    StoryStarOnMissionMessage(&Text, 5);
}

void NnExitOpen(bool bImmediate)
{
    // FUN_00471f30.
    if (Win3DFindByClassName("NN_Exit") != nullptr)
        return;
    Win3D* const Created = WinFactoryCreate("NN_Exit", "NN_Exit", nullptr, nullptr);
    if (Created == nullptr)
        return;
    NnExit* const Exit = static_cast<NnExit*>(Created);
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    Exit->SetProperty(0x42, reinterpret_cast<uintptr_t>(Zero), 0);
    Exit->bImmediate = bImmediate ? 1 : 0;
    Exit->CaptureMouse();
}
