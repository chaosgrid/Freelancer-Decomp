// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_network_operation.h"

#include "Common/Fonts/font_string.h"        // FFontString (+0x28 LayoutGen, +0x30 bMultiLine)
#include "Core/globals.h"            // PushClientTimingPause / PopClientTimingPause
#include "Space/HUD/hud_command_dispatch.h"
#include "Common/input_manager.h"      // MouseSetCursor (FUN_0041dde0)
#include "NeuroNet/nn_modal.h"
#include "NeuroNet/nn_text_control.h"    // NnTextControl::GetFontString (+0x334)
#include "Interface/win3d.h"
#include "Interface/win3d_renderables.h"  // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "Interface/win_factory.h"

// Retail DAT_0066dbc8 -- WINFACTORY<NN_NetworkOperation> static (LAB_00472120 -> FUN_00474600;
// Create FUN_00474670; atexit LAB_00472140 -> FUN_00579380).
WINFACTORY<NnNetworkOperation> G_NnNetworkOperationFactory("NN_NetworkOperation");

namespace
{
    char const kExitFrameMesh[] = "Interface\\NeuroNet\\Options\\alt-f4window.cmp"; // 005d04a8
}

NnNetworkOperation::NnNetworkOperation()
    : ResultFn(nullptr)
    , ResultArg(nullptr)
{
    // Inlined into FUN_00474670 (0x004746a9). DAT_0067dccc is the modal capture count.
    PushClientTimingPause();
    G_ModalCaptureCount = G_ModalCaptureCount + 1;
}

NnNetworkOperation::~NnNetworkOperation()
{
    // FUN_004721a0.
    G_ModalCaptureCount = G_ModalCaptureCount - 1;
    Win3DSetKeyFocus(nullptr);
    HudPopInputFocus(AsHudCmdHandler());
    PopClientTimingPause();
}

bool NnNetworkOperation::Create(Win3D* NewParent, void* Data)
{
    // FUN_00472230 (RET 8).
    MouseSetCursor("Arrow", false);

    Vector const Zero = {0.0f, 0.0f, 0.0f};

    Vector FramePosition = {0.0f, 0.0f, -1.0f};
    CreateStaticControl("ExitFrame", kExitFrameMesh, &FramePosition, nullptr, &Zero, 0, 0, 0, 1, 1,
                        0, 1, 0, 0, 0, &Zero);

    Vector TextPosition = {0.0f, NnFloatBits(0x3d71a9fc), -1.0f};
    ExitText = CreateTextControl("ExitText", "Normal", &TextPosition, nullptr,
                                 NnFloatBits(0x3eb851ec), NnFloatBits(0x3cf5c28f),
                                 NnFloatBits(0x3fa00000), 0x736, 1, 0, 0);

    // The text's Font::String (+0x334) goes multi-line (re-laid out via +0x28 = 0). No null test
    // on the text.
    FFontString* const Font = static_cast<NnTextControl*>(ExitText)->GetFontString();
    if (Font->bMultiLine != 1)
    {
        Font->bMultiLine = 1;
        Font->LayoutGen = 0;
    }

    DrawLayer = 0x6e;
    DrawSubLayer = 6;
    bCursorDrawFlag = NnModalPauseGame();
    Win3DSetKeyFocus(this);
    HudPushInputFocus(AsHudCmdHandler());
    RegisterEvent(USER_CANCEL);
    RegisterEvent(USER_ENTER);

    return Win3D::Create(NewParent, Data); // FUN_004b5060
}

void NnNetworkOperation::Close()
{
    NnModalResumeGame(bCursorDrawFlag);
    if (ResultFn != nullptr)
        ResultFn(ResultArg, 0);
    ReleaseMouse();
    Win3DDeleteTree(this); // FUN_0043d2a0, inlined in retail
}

void NnNetworkOperation::Draw()
{
    // FUN_00488960.
    Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0
    WinControl::Draw();            // tail call FUN_005a0760
}

bool NnNetworkOperation::KeyEvent(unsigned Key, unsigned char* Modifiers)
{
    // LAB_00472450 (RET 8): Escape with modifier bit 1 clear cancels the wait.
    if ((*Modifiers & 0x02) != 0)
        return false;
    if (Key != 0x1b)
        return false;
    Close();
    return true;
}

intptr_t NnNetworkOperation::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00472390 (RET 0xc): 0x42 recentres; every message returns 0 without reaching
    // WinControl's switch.
    (void)B;
    if (Msg == kWinPropPlaceAtOffset)
    {
        float const* const Offset3 = reinterpret_cast<float const*>(A);
        SetOffset(Offset3[0], Offset3[1], Offset3[2]);
        SetPosition(&Position);
    }
    return 0;
}

bool NnNetworkOperation::OnEvent(unsigned MsgId)
{
    // LAB_004723d0: 0xf closes the dialog; 0x11 is swallowed.
    if (MsgId == USER_CANCEL)
    {
        Close();
        return true;
    }
    if (MsgId == USER_ENTER)
        return true;
    return false;
}

void NnNetworkOperationOpen(unsigned Ids)
{
    // FUN_00473ad0.
    if (Win3DFindByClassName("NN_NetworkOperation") != nullptr)
        return;
    Win3D* const Created =
        WinFactoryCreate("NN_NetworkOperation", "NN_NetworkOperation", nullptr, nullptr);
    if (Created == nullptr)
        return;
    NnNetworkOperation* const Dialog = static_cast<NnNetworkOperation*>(Created);
    Vector const Zero = {0.0f, 0.0f, 0.0f};
    Dialog->SetProperty(kWinPropPlaceAtOffset, reinterpret_cast<uintptr_t>(&Zero), 0);
    if (Dialog->ExitText != nullptr)
        Dialog->ExitText->SetProperty(kTextPropSetIds, Ids, 0);
    Dialog->CaptureMouse();
}

void NnNetworkOperationClose()
{
    // FUN_00473b60.
    Win3D* const Found = Win3DFindByClassName("NN_NetworkOperation");
    if (Found == nullptr)
        return;
    static_cast<NnNetworkOperation*>(Found)->Close();
}

bool NnNetworkOperationIsOpen()
{
    // FUN_00473be0.
    return Win3DFindByClassName("NN_NetworkOperation") != nullptr;
}
