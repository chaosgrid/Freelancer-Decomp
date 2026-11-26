// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_error.h"

#include "Core/client_frame_tick.h"  // BeginClientRenderFrame / EndClientRenderFrame
#include "common_misc.h"        // WaitForClientRunPermission / GiveServerRunPermission
#include "common_server.h"      // IServer
#include "Common/Fonts/font_string.h"        // FFontString (+0x28 LayoutGen, +0x30 bMultiLine)
#include "Core/globals.h"            // PumpMessages, GetClientFrameTimeCapped, timing pause
#include "Space/HUD/hud_command_dispatch.h"
#include "Common/input_manager.h"      // MouseFrameUpdate (FUN_0041f150)
#include "Network/login_mgr.h"          // G_Server (DAT_0067ecd0)
#include "NeuroNet/nn_modal.h"
#include "NeuroNet/nn_text_control.h"    // NnTextControl::GetFontString (+0x334)
#include "Interface/win3d.h"
#include "Interface/win3d_renderables.h"  // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "Interface/win_factory.h"

#include <stdlib.h>
#include <windows.h>

// Retail DAT_0066db70 -- WINFACTORY<NN_Error> static (LAB_004724d0 -> FUN_00474880; Create
// FUN_004748f0; atexit LAB_004724f0 -> FUN_00579380).
WINFACTORY<NnError> G_NnErrorFactory("NN_Error");

namespace
{
    // DAT_0066dc04 -- zeroed by every NN_Error dismiss path and read by FUN_00473e00. Nothing
    // writes anything else to it, so FUN_00473e00's result is always true.
    int G_NnErrorPending = 0;

    char const kErrorFrameMesh[] = "Interface\\NeuroNet\\errorexplanation.cmp"; // 005d076c
}

NnError::NnError()
{
    // Inlined in FUN_004748f0.
    PushClientTimingPause();
    G_ModalCaptureCount = G_ModalCaptureCount + 1;
}

NnError::~NnError()
{
    // FUN_00472520.
    G_ModalCaptureCount = G_ModalCaptureCount - 1;
    Win3DSetKeyFocus(nullptr);
    HudPopInputFocus(AsHudCmdHandler());
    PopClientTimingPause();
}

bool NnError::Create(Win3D* NewParent, void* Data)
{
    // FUN_004725b0.
    MouseSetCursor("Arrow", false); // FUN_0041dde0

    Vector const Zero = {0.0f, 0.0f, 0.0f};

    Vector FramePosition = {0.0f, 0.0f, NnFloatBits(0xbfd9999a)};
    CreateStaticControl("Frame", kErrorFrameMesh, &FramePosition, nullptr, &Zero, 0, 0, 0, 1, 1, 0,
                        1, 0, 0, 0, &Zero);

    Vector TextPosition = {0.0f, 0.0f, -1.0f};
    Text = CreateTextControl("Text", "SubHeader", &TextPosition, nullptr, NnFloatBits(0x3f07ae14),
                             NnFloatBits(0x3e08b439), NnFloatBits(0x3fa00000), 0, 1, 0, 0);
    // Text's Font::String goes multi-line (re-laid out) unless it already is. No null test.
    FFontString* const TextFont = static_cast<NnTextControl*>(Text)->GetFontString();
    if (TextFont->bMultiLine != 1)
    {
        TextFont->bMultiLine = 1;
        TextFont->LayoutGen = 0;
    }

    Vector ButtonPosition = {NnFloatBits(0xbc449ba6), NnFloatBits(0xbd99999a), -1.0f};
    Vector ButtonExtents = {NnFloatBits(0x3e083127), NnFloatBits(0x3cd4fdf4), 0.0f};
    OkayButton = CreateWireButtonControl("ErrorOkayButton", "ui_select_item", &ButtonPosition,
                                         &ButtonExtents, 0, 0, 0, 0, 0);

    Vector OkayPosition = {NnFloatBits(0xbcbc6a7f), NnFloatBits(0xbdb851ec), -1.0f};
    OkayText = CreateTextControl("ErrorOkayText", "ButtonText", &OkayPosition, nullptr,
                                 NnFloatBits(0x3e21cac1), NnFloatBits(0x3d75c28f),
                                 NnFloatBits(0x3fa00000), 0x411, 1, 0, 0);

    Vector TitlePosition = {NnFloatBits(0xbe6c8b44), NnFloatBits(0x3def9db2), -1.0f};
    Title = CreateTextControl("ErrorDialogTitle", "PageHeader", &TitlePosition, nullptr,
                              NnFloatBits(0x3ee66666), NnFloatBits(0x3d75c28f),
                              NnFloatBits(0x3f99999a), 0x3e8, 1, 0, 1);
    // The same Font::String poke on the title.
    FFontString* const TitleFont = static_cast<NnTextControl*>(Title)->GetFontString();
    if (TitleFont->bMultiLine != 1)
    {
        TitleFont->bMultiLine = 1;
        TitleFont->LayoutGen = 0;
    }

    // No null test on the title.
    Title->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(L"Freelancer Encountered an Error"), 0);

    DrawLayer = 0x6e;
    DrawSubLayer = 6;
    bCursorDrawFlag = NnModalPauseGame();
    Win3DSetKeyFocus(this);
    HudPushInputFocus(AsHudCmdHandler());
    RegisterEvent(USER_CANCEL);
    RegisterEvent(USER_ENTER);

    return Win3D::Create(NewParent, Data); // FUN_004b5060
}

void NnError::Dismiss()
{
    G_NnErrorPending = 0;
    if (Parent != nullptr)
        Parent->ProcessMessage(kWinMsgConfirmAnswer, 0, 0);
    NnModalResumeGame(bCursorDrawFlag);
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

void NnError::Draw()
{
    // FUN_00488960.
    Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0
    WinControl::Draw();            // FUN_005a0760
}

bool NnError::KeyEvent(unsigned Key, unsigned char* Modifiers)
{
    // LAB_00472940: only with modifier bit 1 set, Escape or Enter dismisses.
    if ((*Modifiers & 0x02) == 0)
        return false;
    if (Key != 0x1b && Key != 0xd)
        return false;
    Dismiss();
    return true;
}

intptr_t NnError::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00472880: every message returns 0 without reaching WinControl's switch.
    (void)B;
    if (Msg == kWinNotifyClick)
    {
        if (A == reinterpret_cast<uintptr_t>(OkayButton))
            Dismiss();
        return 0;
    }
    if (Msg == kWinPropPlaceAtOffset)
    {
        float const* const Offset3 = reinterpret_cast<float const*>(A);
        SetOffset(Offset3[0], Offset3[1], Offset3[2]);
        SetPosition(&Position);
    }
    return 0;
}

bool NnError::OnEvent(unsigned MsgId)
{
    // LAB_004727f0: 0xf or 0x11 dismisses; anything else is not handled.
    if (MsgId != USER_CANCEL && MsgId != USER_ENTER)
        return false;
    Dismiss();
    return true;
}

namespace
{
    // The head FUN_00473c00 and FUN_00473cb0 share.
    void RaiseWithIds(unsigned Ids, Win3D* Parent)
    {
        if (Win3DFindByClassName("NN_Error") != nullptr)
            return;
        Win3D* const Created = WinFactoryCreate("NN_Error", "NN_Error", Parent, nullptr);
        if (Created == nullptr)
            return;
        NnError* const Dialog = static_cast<NnError*>(Created);
        Vector const Zero = {0.0f, 0.0f, 0.0f};
        Dialog->SetProperty(kWinPropPlaceAtOffset, reinterpret_cast<uintptr_t>(&Zero), 0);
        if (Dialog->Title != nullptr)
            Dialog->Title->Flags = static_cast<unsigned char>(Dialog->Title->Flags & 0xfc);
        if (Dialog->Text != nullptr)
            Dialog->Text->SetProperty(kTextPropSetIds, Ids, 0);
        Dialog->CaptureMouse();
    }
}

void NnErrorOpen(unsigned Ids, Win3D* Parent)
{
    // FUN_00473c00.
    RaiseWithIds(Ids, Parent);
}

Win3D* NnErrorFind()
{
    // FUN_00473ca0.
    return Win3DFindByClassName("NN_Error");
}

void NnErrorRunModal(unsigned Ids, bool bUpdateServer, Win3D* Parent)
{
    // FUN_00473cb0.
    RaiseWithIds(Ids, Parent);

    int ExitCode = 0;
    if (Win3DFindByClassName("NN_Error") == nullptr)
        return;
    do
    {
        Sleep(10);
        if (!PumpMessages(&ExitCode))
            return;
        if (bUpdateServer)
        {
            if (G_Server != nullptr)
                G_Server->Update(); // +0x2c
        }
        else
        {
            WaitForClientRunPermission();
        }
        Win3DUpdateAll(GetClientFrameTimeCapped()); // FUN_0059e080(FUN_0042d680())
        if (BeginClientRenderFrame())               // FUN_00424f30
        {
            Win3DDrawAll();         // FUN_0059e300
            EndClientRenderFrame(); // FUN_00425080 + FUN_00425140
        }
        if (!bUpdateServer)
            GiveServerRunPermission();
    } while (Win3DFindByClassName("NN_Error") != nullptr);
}

bool NnErrorRunModalMessage(char const* Message)
{
    // FUN_00473e00.
    if (Win3DFindByClassName("NN_Error") != nullptr)
        return true;

    // Retail repeats the lookup.
    if (Win3DFindByClassName("NN_Error") == nullptr)
    {
        Win3D* const Created = WinFactoryCreate("NN_Error", "NN_Error", nullptr, nullptr);
        if (Created != nullptr)
        {
            NnError* const Dialog = static_cast<NnError*>(Created);
            Vector const Zero = {0.0f, 0.0f, 0.0f};
            Dialog->SetProperty(kWinPropPlaceAtOffset, reinterpret_cast<uintptr_t>(&Zero), 0);
            if (Dialog->Title != nullptr)
                Dialog->Title->Flags = static_cast<unsigned char>(Dialog->Title->Flags | 0x03);
            wchar_t Wide[0x400];
            mbstowcs(Wide, Message, 0x400);
            if (Dialog->Text != nullptr)
                Dialog->Text->SetProperty(kTextPropSetText, reinterpret_cast<uintptr_t>(Wide), 0);
            Dialog->CaptureMouse();
        }
    }

    int ExitCode = 0;
    Sleep(10);
    if (PumpMessages(&ExitCode))
    {
        do
        {
            Win3DUpdateAll(GetClientFrameTimeCapped());
            Win3D* const Live = Win3DFindByClassName("NN_Error");
            if (Live == nullptr)
                break;
            if (BeginClientRenderFrame())
            {
                Win3DBeginDraw();              // FUN_0059e500
                Live->Draw();                  // slot 13
                Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0
                MouseFrameUpdate();            // FUN_0041f150
                EndClientRenderFrame();        // FUN_00425080 + FUN_00425140
            }
            Sleep(10);
        } while (PumpMessages(&ExitCode));
    }

    return G_NnErrorPending == 0;
}
