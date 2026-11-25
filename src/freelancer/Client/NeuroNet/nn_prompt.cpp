// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_prompt.h"

#include "Core/client_state.h"       // CallClientStateUpdateSlot (DAT_0067ecac vftable[14])
#include "common_misc.h"        // WaitForClientRunPermission / GiveServerRunPermission
#include "common_server.h"      // IServer
#include "common_text.h"        // RenderDisplayList, StyleNode, JustifyNode
#include "Core/globals.h"            // PumpMessages, UpdateClientTiming, GetClientFrameTimeCapped
#include "Common/Fonts/ids_rdl.h"            // LoadIdsIntoRdl (FUN_0057da40), RdlAppendNode
#include "Common/input_manager.h"      // MouseSetCursor (FUN_0041dde0)
#include "Network/login_mgr.h"          // G_Server (DAT_0067ecd0)
#include "NeuroNet/nn_modal.h"           // NnFloatBits
#include "Interface/rich_text_win.h"
#include "Interface/win3d.h"
#include "Interface/win3d_renderables.h"  // Win3DFlushBeforeOverlayDraw (FUN_0045a9f0)
#include "Interface/win_factory.h"
#include "Interface/wire_controls.h"
#include "NeuroNet/Chat/chat_init.h"        // EChatTextStyle

#include <wchar.h>
#include <windows.h>

// Retail DAT_0066db44 -- WINFACTORY<NN_Prompt> static (LAB_004729d0 -> FUN_004749c0; Create
// FUN_00474a30; atexit LAB_004729f0 -> FUN_00579380).
WINFACTORY<NnPrompt> G_NnPromptFactory("NN_Prompt");

namespace
{
    char const kPromptFrameMesh[] = "Interface\\NeuroNet\\errorexplanation.cmp";
    char const kPromptExitMesh[] = "Interface\\NeuroNet\\Inventory\\X.3db"; // 005cf1f4
    wchar_t const kPromptEditSeed[] = L" ";                                  // DAT_005d0514
}

NnPrompt::NnPrompt()
    : Byte330(0)
    , Byte331(0)
    , Result(nullptr)
    , EditText(nullptr)
    , Mode(0)
    , PromptText(nullptr)
    , PromptEdit(nullptr)
    , PromptExit(nullptr)
    , PromptNo(nullptr)
    , PromptYes(nullptr)
{
    // Inlined into FUN_00474a30 (0x00474a68). Retail does not write +0x330; the port zeroes it.
    Pad332[0] = 0;
    Pad332[1] = 0;
    G_ModalCaptureCount = G_ModalCaptureCount + 1;
}

NnPrompt::~NnPrompt()
{
    // FUN_00472a20.
    G_ModalCaptureCount = G_ModalCaptureCount - 1;
    Win3DSetKeyFocus(nullptr);
}

bool NnPrompt::Create(Win3D* NewParent, void* Data)
{
    // FUN_00472ab0 (RET 8). Win3D's Create runs first, with a null payload.
    if (!Win3D::Create(NewParent, nullptr))
        return false;

    MouseSetCursor("Arrow", false);

    FNnPromptPayload const* const Payload = static_cast<FNnPromptPayload const*>(Data);
    // The payload is not null-tested.
    Mode = Payload->Mode;

    // (0, 0, -1.7), NN_Error's frame mesh and argument list.
    float const Zero[3] = {0.0f, 0.0f, 0.0f};
    float FramePosition[3] = {0.0f, 0.0f, NnFloatBits(0xbfd9999a)};
    CreateStaticControl("PromptFrame", kPromptFrameMesh, FramePosition, nullptr, Zero, 0, 0, 0, 1,
                        1, 0, 1, 0, 0, 0, Zero);

    // 0.7 x 0.15 at {-0.35, 0.12, -1}.
    float TextOffset[3] = {NnFloatBits(0xbeb33333), NnFloatBits(0x3df5c28f), -1.0f};
    PromptText = CreateRichTextWin("PromptText", NnFloatBits(0x3f333333),
                                   NnFloatBits(0x3e19999a), TextOffset, true);

    if (Mode == 0)
        PromptEdit = static_cast<RichTextEditWin*>(
            WinFactoryCreate("PromptEdit", "RichTextEditWin", this, nullptr));
    else
        PromptEdit = nullptr;

    if (PromptEdit != nullptr)
    {
        // Offset {-0.3, -0.015, -1}.
        PromptEdit->SetNormSize(NnFloatBits(0x3f19999a), NnFloatBits(0x3cf5c28f));
        float EditOffset[3] = {NnFloatBits(0xbe99999a), NnFloatBits(0xbc75c28f), -1.0f};
        PromptEdit->SetAttachOffset(EditOffset);
        PromptEdit->SetDrawFrame(false);
        PromptEdit->SetPasswordMode(true);   // FUN_0057be00(1)
        PromptEdit->SetFixedStyle(true);     // FUN_0057bdd0(1)
        PromptEdit->SetImeEnabled(false);    // FUN_0057be30(0)

        // Retail's clone-into-list idiom (FUN_0046c800 clones again; the first clone is
        // deleted), onto the +0x4fc fixed-style list.
        {
            StyleNode const Style(kTextStyleBody);
            RDLNode* const Temp = Style.Clone();
            RdlAppendNode(PromptEdit->FixedStyle, Temp->Clone());
            delete Temp;
        }

        // The +0x4dc prefix list.
        RdlClear(PromptEdit->Prefix);

        {
            JustifyNode const Justify(TextJustifyCenter);
            RDLNode* const Temp = Justify.Clone();
            RdlAppendNode(PromptEdit->Prefix, Temp->Clone());
            delete Temp;
        }

        {
            TextPtrNode const Space(kPromptEditSeed, -1);
            RDLNode* const Temp = Space.Clone();
            RdlAppendNode(PromptEdit->Prefix, Temp->Clone());
            delete Temp;
        }

        PromptEdit->RebuildDisplay();        // FUN_0057d330
        PromptEdit->Activate();              // FUN_0057c750
    }

    // A non-zero payload[0] replaces the default OK / Yes tool tip.
    int YesToolTip = (Mode == 0) ? 0xd48 : 0;
    if (Payload->YesToolTipIds != 0)
        YesToolTip = Payload->YesToolTipIds;
    int const NoToolTip = Payload->NoToolTipIds;

    // The boxes (FUN_005a1950), half extents {0.05, 0.017, 0}.
    float const ButtonExtents[3] = {NnFloatBits(0x3d4ccccd), NnFloatBits(0x3c8b4396), 0.0f};
    if (Mode == 0)
    {
        float OkPosition[3] = {0.0f, NnFloatBits(0xbd851eb8), -1.0f};
        PromptYes = static_cast<RichTextWireButtonControl*>(CreateRichTextWireButtonControl(
            "PromptOk", "ui_select_item", OkPosition, ButtonExtents, 0, 0, YesToolTip, 0x411, 0,
            0));
    }
    else if (Mode == 1 || Mode == 2)
    {
        float YesPosition[3] = {NnFloatBits(0xbd8f5c29), NnFloatBits(0xbd851eb8), -1.0f};
        unsigned const YesCaption = (Mode == 1) ? 0x4dcu : 0u;
        PromptYes = static_cast<RichTextWireButtonControl*>(CreateRichTextWireButtonControl(
            "PromptYes", "ui_select_item", YesPosition, ButtonExtents, 0, 0, YesToolTip,
            YesCaption, 0, 0));

        float NoPosition[3] = {NnFloatBits(0x3d8f5c29), NnFloatBits(0xbd851eb8), -1.0f};
        unsigned const NoCaption = (Mode == 1) ? 0x4ddu : 0u;
        PromptNo = static_cast<RichTextWireButtonControl*>(CreateRichTextWireButtonControl(
            "PromptNo", "ui_select_item", NoPosition, ButtonExtents, 0, 0, NoToolTip, NoCaption,
            0, 0));
    }

    // (0.345, 0.105, -1), half extents (0.02, 0.02, 0).
    float ExitPosition[3] = {NnFloatBits(0x3eb0a3d7), NnFloatBits(0x3dd70a3d), -1.0f};
    float ExitExtents[3] = {NnFloatBits(0x3ca3d70a), NnFloatBits(0x3ca3d70a), 0.0f};
    PromptExit = CreateButtonControl("PromptExit", kPromptExitMesh, nullptr, "ui_select_item",
                                     nullptr, ExitPosition, nullptr, ExitExtents, 0, 0, 0, 0, 0,
                                     0, 0);

    DrawLayer = 0x6e;
    DrawSubLayer = 6;

    if (Mode == 1)
    {
        RegisterEvent(USER_YES);
        RegisterEvent(USER_NO);
        RegisterEvent(USER_CANCEL);
    }
    else if (Mode == 2)
    {
        RegisterEvent(USER_CANCEL);
    }

    Win3DSetKeyFocus(this);
    return true;
}

void NnPrompt::Draw()
{
    // FUN_00488960.
    Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0
    WinControl::Draw();            // tail call FUN_005a0760
}

void NnPrompt::CopyEditText()
{
    // Port-only helper for the copy retail inlines at six sites (e.g. 0x004730a4), in three
    // spellings that all make *EditText the buffer.
    if (PromptEdit == nullptr || EditText == nullptr)
        return;
    wchar_t Buffer[0x400];
    PromptEdit->GetText(Buffer, 0x400);
    EditText->assign(Buffer, wcslen(Buffer));
}

void NnPrompt::Close()
{
    ReleaseMouse(); // slot 33 (+0x84)
    Win3DDeleteTree(this); // FUN_0043d2a0, inlined in retail
}

bool NnPrompt::OnEvent(unsigned MsgId)
{
    // LAB_00473070 (RET 4).
    int Answer = 0;
    if (MsgId == USER_YES)
        Answer = 1;
    else if (MsgId != USER_CANCEL && MsgId != USER_NO)
        return false;

    CopyEditText();
    if (Result != nullptr)
        *Result = Answer;
    Close();
    return true;
}

bool NnPrompt::ProcessMessage(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_00473240 (RET 0xc).
    int Answer = 0;
    if (Msg == kWinMsgEnter)
        Answer = 1;
    else if (Msg != kWinMsgEscape)
        return WinControl::ProcessMessage(Msg, A, B);

    CopyEditText();
    if (Result != nullptr)
        *Result = Answer;
    Close();
    return true;
}

intptr_t NnPrompt::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00473420 (RET 0xc): every path returns 0 and none reaches WinControl's switch.
    (void)B;
    if (Msg == kWinPropPlaceAtOffset)
    {
        float const* const Offset3 = reinterpret_cast<float const*>(A);
        SetOffset(Offset3[0], Offset3[1], Offset3[2]);
        SetPosition(&Position.x);
        return 0;
    }
    if (Msg != kWinNotifyClick)
        return 0;

    if (A == reinterpret_cast<uintptr_t>(PromptExit))
    {
        CopyEditText();
        if (Result != nullptr)
            *Result = 0;
        Close();
        return 0;
    }

    if (A == reinterpret_cast<uintptr_t>(PromptNo))
    {
        CopyEditText();
        if (Result != nullptr)
            *Result = (Mode == 2) ? 3 : 0;
    }
    else if (A == reinterpret_cast<uintptr_t>(PromptYes))
    {
        // In mode 2 Yes closes like the X box.
        CopyEditText();
        if (Mode == 2)
        {
            if (Result != nullptr)
                *Result = 2;
            Close();
            return 0;
        }
        if (Result != nullptr)
            *Result = 1;
    }
    else
    {
        return 0;
    }

    // Close's steps; here retail calls FUN_0043d2a0(this) rather than inlining it.
    ReleaseMouse();
    Win3DDeleteTree(this);
    return 0;
}

void NnPrompt::SetPromptIds(unsigned Ids, unsigned short StyleId)
{
    // FUN_00473770 (RET 8).
    if (PromptText == nullptr || Ids == 0)
        return;

    RenderDisplayList Text;
    StyleNode Style(StyleId);
    RdlAppendNode(Text, Style.Clone());

    // No trailing-paragraph trim.
    if (LoadIdsIntoRdl(Ids, Text))
    {
        PromptText->ClearContent();
        PromptText->AppendContent(Text);
    }
}

void NnPrompt::SetPromptRdl(RenderDisplayList const* Text, unsigned short StyleId)
{
    // FUN_004738e0 (RET 8). Text is not null-tested.
    if (PromptText == nullptr)
        return;
    PromptText->ClearContent();      // FUN_0057b370
    StyleNode Style(StyleId);
    PromptText->AppendNode(Style);   // FUN_0057b470
    PromptText->AppendContent(*Text); // FUN_0057b3e0
}

void NnPrompt::SetButtonRdl(int Which, RenderDisplayList const* Caption)
{
    // FUN_00473970 (RET 8). Neither the box nor its caption window (*(box + 0x394)) is
    // null-tested.
    RichTextWireButtonControl* Button = nullptr;
    if (Which == 0)
        Button = PromptYes;
    else if (Which == 1)
        Button = PromptNo;
    else
        return;

    RichTextWin* const CaptionWin = *Button->TextWinRef;
    CaptionWin->ClearContent();                   // FUN_0057b370
    StyleNode Style(kTextStyleButton);
    CaptionWin->AppendNode(Style);                // FUN_0057b470
    JustifyNode Justify(TextJustifyCenter);       // JustifyNode(1)
    CaptionWin->AppendNode(Justify);              // FUN_0057b470
    CaptionWin->AppendContent(*Caption);          // FUN_0057b3e0
}

namespace
{
    // Port-only helper for the head the three entry points inline (e.g. 0x00473f7c). A prompt
    // that is already up is reused as it is; the payload then goes nowhere.
    NnPrompt* RaisePrompt(FNnPromptPayload* Payload)
    {
        Win3D* Found = Win3DFindByClassName("NN_Prompt");
        if (Found == nullptr)
            Found = WinFactoryCreate("NN_Prompt", "NN_Prompt", nullptr, Payload);
        return static_cast<NnPrompt*>(Found);
    }

    void CentrePrompt(NnPrompt* Dialog)
    {
        float const Zero[3] = {0.0f, 0.0f, 0.0f};
        Dialog->SetProperty(kWinPropPlaceAtOffset, reinterpret_cast<uintptr_t>(Zero), 0);
    }

    // Port-only helper for the modal pump the three entry points inline (e.g. 0x00474034).
    bool PumpWhilePromptUp(bool bUpdateServer)
    {
        int ExitCode = 0;
        if (Win3DFindByClassName("NN_Prompt") == nullptr)
            return true;
        do
        {
            Sleep(10);
            UpdateClientTiming(); // FUN_005b2360
            if (!PumpMessages(&ExitCode))
                return false;
            if (bUpdateServer)
                G_Server->Update(); // +0x2c, no null test
            else
                WaitForClientRunPermission();
            double const Delta = GetClientFrameTimeCapped(); // FUN_0042d680
            Win3DUpdateAll(Delta);                           // FUN_0059e080
            CallClientStateUpdateSlot(static_cast<float>(Delta));
            if (!bUpdateServer)
                GiveServerRunPermission();
        } while (Win3DFindByClassName("NN_Prompt") != nullptr);
        return true;
    }
}

bool NnPromptRunTextEntry(unsigned Ids, std::wstring* Text, bool bUpdateServer,
                          int OkToolTipIds)
{
    // FUN_00473f70.
    FNnPromptPayload Payload = {OkToolTipIds, 0, 0};
    NnPrompt* const Dialog = RaisePrompt(&Payload);
    int Result = 0;
    if (Dialog != nullptr)
    {
        CentrePrompt(Dialog);
        Dialog->SetPromptIds(Ids, 0x5c12); // FUN_00473770
        // Text is not null-tested.
        wchar_t const* const Seed = Text->c_str();
        if (Dialog->PromptEdit != nullptr)
            Dialog->PromptEdit->SetText(Seed, nullptr);
        Dialog->EditText = Text;
        Dialog->Result = &Result;
        Dialog->CaptureMouse(); // slot 32
    }

    if (!PumpWhilePromptUp(bUpdateServer))
        return false;
    return Result == 1;
}

bool NnPromptRunYesNo(unsigned Ids, unsigned short StyleId, bool bUpdateServer,
                      int YesToolTipIds, int NoToolTipIds)
{
    // FUN_004740f0.
    FNnPromptPayload Payload = {YesToolTipIds, NoToolTipIds, 1};
    NnPrompt* const Dialog = RaisePrompt(&Payload);
    int Result = 0;
    if (Dialog != nullptr)
    {
        CentrePrompt(Dialog);
        Dialog->SetPromptIds(Ids, StyleId); // FUN_00473770
        Dialog->Result = &Result;
        Dialog->CaptureMouse();
    }

    if (!PumpWhilePromptUp(bUpdateServer))
        return false;
    return Result == 1;
}

bool NnPromptRunChoice(RenderDisplayList const* Text, unsigned short StyleId,
                       RenderDisplayList const* YesCaption, RenderDisplayList const* NoCaption,
                       int* OutChoice, bool bUpdateServer)
{
    // FUN_00474260.
    FNnPromptPayload Payload = {0, 0, 2};
    NnPrompt* const Dialog = RaisePrompt(&Payload);
    int Result = 0;
    if (Dialog != nullptr)
    {
        CentrePrompt(Dialog);
        Dialog->SetPromptRdl(Text, StyleId);     // FUN_004738e0
        Dialog->Result = &Result;
        Dialog->SetButtonRdl(0, YesCaption);     // FUN_00473970
        Dialog->SetButtonRdl(1, NoCaption);
        Dialog->CaptureMouse();
    }

    if (!PumpWhilePromptUp(bUpdateServer))
        return false;

    if (Result == 0)
        return false;
    if (Result == 2)
    {
        *OutChoice = 0;
        return true;
    }
    if (Result == 3)
        *OutChoice = 1;
    return true;
}
