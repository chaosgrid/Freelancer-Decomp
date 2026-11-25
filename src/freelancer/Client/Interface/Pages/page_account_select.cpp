// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/Pages/page_account_select.h"

#include "Core/globals.h"          // GetClientFrameTimeCapped (FUN_0042d680)
#include "Interface/menu_ui_init.h"     // PlayUiSoundSlot (FUN_00564570), PlayOrRewindUiSoundSlot
#include "Interface/Pages/intro_page_util.h" // AppendNodeClone, LoadStyledIds, HideWin3D / ShowWin3D
#include "NeuroNet/nn_error.h"         // NnErrorRunModal (FUN_00473cb0)
#include "NeuroNet/nn_ui_pulse.h"      // G_HudChromeColorRgba (DAT_00679bb4)
#include "Interface/tooltip_hover.h"    // FToolTipHoverInfo
#include "Interface/ui_rich_text_win.h"
#include "Interface/ui_text_edit.h"
#include "Interface/ui_text_msg_button.h"
#include "Interface/win3d_immediate.h"  // Win3DVec3Add (FUN_00423b30)
#include "Interface/menu_enter_state.h"
#include "NeuroNet/Chat/chat_init.h"        // EChatTextStyle

#include "common_net.h"       // CPlayerAccount
#include "common_text.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <vector>

namespace
{
    // DAT_0067aa10.
    PageAccountSelect G_PageAccountSelect;

    // u"0123456789-ABCDEF" (0x00614c74), handed to FUN_0057c860 with a count of 0x12 -- the
    // seventeen characters and the terminator.
    wchar_t const kAccountIdChars[] = L"0123456789-ABCDEF";
}

using namespace IntroPageUtil;

PageAccountSelect::PageAccountSelect()
    : UIPage::MasterWin(Desc.GetPageData())
    , Result(0)
    , SlideT(0.0)
    , bLeaving(0)
    , LeaveDirection(0)
    , BoxHome{0.0f, 0.0f, 0.0f}
    , AccountId(nullptr)
    , SelectServer(nullptr)
    , Back(nullptr)
    , Explain(nullptr)
    , AccountBox(nullptr)
{
    // FUN_005726c0. The page data pointer is taken before the descriptor (+0x8c, FUN_00591e30)
    // is built, as here. +0xb8..+0xdc are not written; the static object starts zeroed, which
    // is what the initialisers above reproduce.
}

PageAccountSelect::~PageAccountSelect()
{
    // FUN_00573470.
}

bool PageAccountSelect::Create(Win3D* NewParent, void* /*Payload*/)
{
    // FUN_005727f0. MasterWin::Create (FUN_00591cd0) with no payload.
    if (!UIPage::MasterWin::Create(NewParent, nullptr))
        return false;
    Result = 0;

    // Through the IUIPage face (slot 4).
    AccountId = static_cast<UiTextEdit*>(FindWidget("AccountID"));
    SelectServer = static_cast<UiTextMsgButton*>(FindWidget("SelectServer"));
    Back = static_cast<UiTextMsgButton*>(FindWidget("Back"));
    Explain = static_cast<UiRichTextWin*>(FindWidget("Explain"));
    AccountBox = FindWidget("AccountBoxShort");

    // Widget slot 46; IdsAlt is +4 of the record it returns.
    AccountId->GetToolTipHoverInfo()->IdsAlt = 0xaca;
    SelectServer->GetToolTipHoverInfo()->IdsAlt = 0xace;
    Back->GetToolTipHoverInfo()->IdsAlt = 0xac6;

    RegisterEvent(USER_CANCEL); // FUN_005760d0 on the Event::Handler face

    // Inlined twice (the list clear FUN_0046c860, StyleNode 0x9ca4, FUN_0057da40, the cut).
    LoadStyledIds(ExplainText, kTextStyleBody, 0x60005);
    LoadStyledIds(EsrbNoticeText, kTextStyleBody, 0x601eb);

    // The key field: the hex-and-dash allow list shown upper case, '-' for anything refused.
    AccountId->SetFixedStyle(true);   // FUN_0057bdd0(1)
    AccountId->SetImeEnabled(false);  // FUN_0057be30(0)
    {
        StyleNode const Style(kTextStyleBody);
        AppendNodeClone(AccountId->FixedStyle, Style); // +0x4fc
    }
    {
        JustifyNode const Justify(TextJustifyCenter);  // JustifyNode(1)
        AppendNodeClone(AccountId->Prefix, Justify);    // +0x4dc
    }
    {
        TextNode const Space(L" ", -1);                  // DAT_005cef90
        AppendNodeClone(AccountId->Prefix, Space);
    }
    AccountId->SetMaxLength(0x28);                                                    // FUN_0057bdb0
    AccountId->SetDrawFrame(true);                                                    // FUN_00579860
    AccountId->SetFrameColor(reinterpret_cast<unsigned long const*>(G_HudChromeColorRgba)); // FUN_00579870
    AccountId->SetAlignMode(2);                                                       // FUN_00579a60
    AccountId->AddFilterChars(kAccountIdChars, 0x12);                                 // FUN_0057c860
    AccountId->SetReplacementChar(L'-');                                              // FUN_0057c920
    AccountId->SetCaseMode(1);                                                        // FUN_0057c840
    AccountId->SetFilterIsAllowList(true);                                            // FUN_0057c850

    // The explanation shows the +0xf4 list by reference.
    Explain->ClearContent(); // FUN_0057b370
    {
        RDLRefNode Ref(ExplainText);
        Explain->AppendNode(Ref); // FUN_0057b470
    }
    Explain->SetAutoScrollbar(true); // FUN_005799f0(1)

    // Retail's wide buffer is _alloca'd. DEVIATION: retail's frame slot for the key is 0x28
    // bytes, so a key of 0x28 characters puts its terminator in the next local; the port's
    // array holds it.
    char TextKey[0x28 + 1];
    if (CPlayerAccount::GenerateTextKey(TextKey))
    {
        size_t const Count = strlen(TextKey) + 1;
        std::vector<wchar_t> Wide(Count);
        mbstowcs(Wide.data(), TextKey, Count);
        AccountId->SetText(Wide.data(), nullptr); // FUN_0057c0e0(text, 0)
    }
    AccountId->Activate(); // FUN_0057c750

    SlideT = -1.5;
    bLeaving = 0;
    LeaveDirection = 0;
    Back->SetSkipUpload(true);          // FUN_00579a90(1)
    SelectServer->SetSkipUpload(true);
    HideWin3D(AccountId);
    HideWin3D(Explain);
    memcpy(BoxHome, AccountBox->Position, sizeof(BoxHome));
    return true;
}

void PageAccountSelect::Destroy()
{
    // FUN_005727b0. FUN_005760f0 on the Event::Handler face, MasterWin::Destroy FUN_00591de0.
    UnregisterEvent(USER_CANCEL);
    UIPage::MasterWin::Destroy();
    AccountId = nullptr;
    SelectServer = nullptr;
    Back = nullptr;
    Explain = nullptr;
    AccountBox = nullptr;
}

void PageAccountSelect::Update()
{
    // LAB_00572cc0. A NaN SlideT still runs the slide-in.
    if (!(SlideT > 1.0) && bLeaving == 0)
    {
        double Dt = GetClientFrameTimeCapped();
        if (Dt > 0.5)
            Dt = 0.5;
        SlideT = Dt * 1.5 + SlideT;

        if (SlideT < 0.0)
        {
            // Slot 42 property 6 on the buttons, slot 5 on the box.
            float const T = static_cast<float>(SlideT);
            float BackOffset[3] = {T, 0.0f, 0.0f};
            float ServerOffset[3] = {static_cast<float>(-SlideT), 0.0f, 0.0f};
            float const Lift[3] = {0.0f, T, 0.0f};
            Back->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(BackOffset), 0);
            SelectServer->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(ServerOffset), 0);
            float BoxPosition[3];
            Win3DVec3Add(BoxPosition, BoxHome, Lift);
            AccountBox->SetPosition(BoxPosition);
        }
        else
        {
            float ZeroOffset[3] = {0.0f, 0.0f, 0.0f};
            Back->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(ZeroOffset), 0);
            SelectServer->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(ZeroOffset), 0);
            AccountBox->SetPosition(BoxHome);
            ShowWin3D(Explain);
            ShowWin3D(AccountId);
            Back->SetSkipUpload(false);
            SelectServer->SetSkipUpload(false);
            AccountId->SetToolTipEnabled(true); // FUN_00579890(1)
            SelectServer->SetToolTipEnabled(true);
            Back->SetToolTipEnabled(true);
            SlideT = 2.0;
        }

        if (SlideT > -0.8 && SlideT < -0.1)
            PlayUiSoundSlot(0x1b); // FUN_00564570(0x1b)
    }

    // Leaving: the frame time is not capped here.
    if (bLeaving != 0 && LeaveDirection != 0)
    {
        SlideT = SlideT - GetClientFrameTimeCapped() * 1.5;
        float const T = static_cast<float>(SlideT);
        float BackOffset[3] = {T, 0.0f, 0.0f};
        float ServerOffset[3] = {static_cast<float>(-SlideT), 0.0f, 0.0f};
        Back->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(BackOffset), 0);
        SelectServer->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(ServerOffset), 0);
        float BoxPosition[3] = {BoxHome[0], T + BoxHome[1], BoxHome[2]}; // FUN_00422b80
        AccountBox->SetPosition(BoxPosition);

        if (SlideT <= -0.9)
        {
            HideWin3D(Back);
            HideWin3D(Explain);
            HideWin3D(SelectServer);
            HideWin3D(AccountId);
            HideWin3D(AccountBox);
            if (LeaveDirection == 1)
                Result = kMenuStateServerSelect;
            else if (LeaveDirection == -1)
                Result = kMenuStateMainMenu;
            bLeaving = 0;
        }
    }

    // Win3D's child walk (FUN_005a17b0 inlined).
    Win3D::Update();
}

bool PageAccountSelect::ProcessMessage(unsigned Msg, unsigned A, unsigned /*B*/)
{
    // FUN_00573010.
    if (Msg == kWinMsgCommand)
    {
        PlayOrRewindUiSoundSlot(0); // FUN_005645c0(0)
        if (A == 0x3e9) // "Back"
        {
            BeginLeave(-1);
            return true;
        }
        if (A != 0x3ea) // "SelectServer"
            return false;
        return SubmitTypedKey();
    }
    if (Msg == kWinMsgEscape)
    {
        BeginLeave(-1);
        return true;
    }
    if (Msg == kWinMsgEnter)
        return SubmitTypedKey();
    return false;
}

bool PageAccountSelect::SubmitTypedKey()
{
    // Inlined at both of FUN_00573010's submit paths; retail's narrow buffer is _alloca'd.
    wchar_t Typed[0x2a];
    AccountId->GetText(Typed, 0x29); // FUN_0057c2c0
    for (wchar_t* Ch = Typed; *Ch != 0; ++Ch)
        *Ch = static_cast<wchar_t>(towlower(*Ch));
    size_t const Count = wcslen(Typed) + 1;
    std::vector<char> Narrow(Count);
    wcstombs(Narrow.data(), Typed, Count);
    if (!CPlayerAccount::SetTextKey(Narrow.data()))
    {
        // The "invalid key" error, run modally; the page stays.
        NnErrorRunModal(0x76a, false, nullptr); // FUN_00473cb0
        LeaveDirection = 0;
        return true;
    }
    BeginLeave(1);
    return true;
}

bool PageAccountSelect::OnEvent(unsigned MsgId)
{
    // LAB_005733b0 (this = the +0x80 face): event 0xf leaves back to the main menu; anything
    // else is FUN_0053f330 (false).
    if (MsgId != USER_CANCEL)
        return UIPage::MasterWin::OnEvent(MsgId);
    BeginLeave(-1);
    return true;
}

void PageAccountSelect::SetToolTipsEnabled(bool bEnabled)
{
    // FUN_00573370.
    AccountId->SetToolTipEnabled(bEnabled);
    SelectServer->SetToolTipEnabled(bEnabled);
    Back->SetToolTipEnabled(bEnabled);
}

void PageAccountSelect::BeginLeave(int Direction)
{
    bLeaving = 1;
    SlideT = 0.0;
    Back->bInteractive = 0;          // +0x498
    SelectServer->bInteractive = 0;
    Back->SetSkipUpload(true);       // FUN_00579a90(1)
    SelectServer->SetSkipUpload(true);
    HideWin3D(AccountId);
    HideWin3D(Explain);
    PlayUiSoundSlot(0x1b);           // FUN_00564570(0x1b)
    SetToolTipsEnabled(false);       // FUN_00573370(0)
    LeaveDirection = Direction;
}

PageAccountSelect& GetPageAccountSelect()
{
    return G_PageAccountSelect;
}
