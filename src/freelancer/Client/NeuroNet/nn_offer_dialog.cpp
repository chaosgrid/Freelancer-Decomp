// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail NN_OfferDialog. See nn_offer_dialog.h.

#include "NeuroNet/nn_offer_dialog.h"

#include "Common/Fonts/ids_rdl.h"         // RdlAppendNode, AppendTextMaybeXmlRdl (FUN_0057e2c0)
#include "NeuroNet/Economy/knowledge_map.h"   // KnowledgeMapRevealFmtStr (FUN_00459950)
#include "Common/misc_util.h"       // FmtStrSprintf (FUN_0041c970)
#include "NeuroNet/nn_modal.h"        // G_bNnScreenUp (DAT_0067dcc8), NnFloatBits
#include "NeuroNet/nn_text_scratch.h" // G_NnTextScratchB (DAT_0066fc60), G_NnTextScratchCapacity
#include "Interface/rich_text_win.h"
#include "Interface/win3d.h"
#include "Interface/win_factory.h"
#include "NeuroNet/Chat/chat_init.h"        // EChatTextStyle

#include "common_text.h"     // FmtStr, RenderDisplayList, StyleNode

#include <stdint.h>
#include <wchar.h>

// Retail DAT_00672108 -- WINFACTORY<NN_OfferDialog> static (LAB_004a60f0 -> FUN_004a6960; Create
// FUN_004a69d0; atexit LAB_004a6110 -> FUN_00579380).
WINFACTORY<NnOfferDialog> G_NnOfferDialogFactory("NN_OfferDialog");

namespace
{
    char const kOfferDialogBackgroundMesh[] =
        "Interface\\NeuroNet\\Inventory\\CHAR_infowindow.cmp";            // 0x005ce994
    char const kOfferDialogCloseMesh[] = "Interface\\TextOffer\\x.3db";   // 0x005d4584
    char const kOfferDialogSelectSound[] = "ui_select_item";              // 0x005ced30

    // The Flags (+0x6c) edits FUN_004a6630 makes: AND 0xfc hides, OR 3 shows.
    void HideControl(WinControl* Control)
    {
        Control->Flags = static_cast<unsigned char>(Control->Flags & ~(kWin3DActive | kWin3DVisible));
    }

    void ShowControl(WinControl* Control)
    {
        Control->Flags = static_cast<unsigned char>(Control->Flags | kWin3DActive | kWin3DVisible);
    }
}

NnOfferDialog::NnOfferDialog()
    : Callback(nullptr)
    , Context(nullptr)
    , bSingleButton(0)
    , AcceptButton(nullptr)
    , AcceptText(nullptr)
    , CloseButton(nullptr)
    , Title(nullptr)
    , OfferText(nullptr)
    , Field_0x350(0)
    , DeclineButton(nullptr)
    , DeclineText(nullptr)
    , Close2Button(nullptr)
    , CloseText(nullptr)
{
    // Inlined into FUN_004a69d0 (0x004a6a09). Retail leaves the other members to Create / Open,
    // which write them before anything reads them; the port zeroes them here.
    Pad339[0] = 0;
    Pad339[1] = 0;
    Pad339[2] = 0;
    G_bNnScreenUp = true;
}

NnOfferDialog::~NnOfferDialog()
{
    // FUN_004a6140.
    G_bNnScreenUp = false;
}

bool NnOfferDialog::Create(Win3D* NewParent, void* Data)
{
    // FUN_004a61b0 (RET 8). NewParent is not read: Win3D::Create gets the window's own +0x04.
    (void)NewParent;
    Vector const Zero = {0.0f, 0.0f, 0.0f};

    CreateStaticControl("OfferDialogBackground", kOfferDialogBackgroundMesh, &Zero, nullptr, &Zero,
                        0, 0, 0, 1, 0, 0, 1, 0, 0, 0, &Zero);

    // {-0.418, 0.253, 0}, 0.839 x 0.06, 1.2.
    Vector const TitlePosition = {NnFloatBits(0xbed60419), NnFloatBits(0x3e818937), 0.0f};
    Title = CreateTextControl("OfferDialogTitle", "PageHeader", &TitlePosition, nullptr,
                              NnFloatBits(0x3f56c8b4), NnFloatBits(0x3d75c28f),
                              NnFloatBits(0x3f99999a), 0x3e8, 1, 0, 1);

    // 0.785 x 0.34 at {-0.4, 0.181, 0}.
    Vector const TextOffset = {NnFloatBits(0xbecccccd), NnFloatBits(0x3e395810), 0.0f};
    OfferText = CreateRichTextWin("OfferDialogOfferText", NnFloatBits(0x3f48f5c3),
                                  NnFloatBits(0x3eae147b), &TextOffset, true);

    // {-0.142, -0.21, 0}, half extents {0.09, 0.026, 0}.
    Vector const ButtonExtents = {NnFloatBits(0x3db851ec), NnFloatBits(0x3cd4fdf4), 0.0f};
    Vector const AcceptPosition = {NnFloatBits(0xbe116873), NnFloatBits(0xbe570a3d), 0.0f};
    AcceptButton = CreateWireButtonControl("OfferDialogAcceptButton", kOfferDialogSelectSound,
                                           &AcceptPosition, &ButtonExtents, 0, 0, 0x332, 0, 0);

    // {0.397, 0.231, 0}, half extents {0.018, 0.018, 0}.
    Vector const ClosePosition = {NnFloatBits(0x3ecb4396), NnFloatBits(0x3e6c8b44), 0.0f};
    Vector const CloseExtents = {NnFloatBits(0x3c9374bc), NnFloatBits(0x3c9374bc), 0.0f};
    CloseButton = CreateButtonControl("OfferDialogCloseButton", kOfferDialogCloseMesh, nullptr,
                                      kOfferDialogSelectSound, nullptr, &ClosePosition, nullptr,
                                      &CloseExtents, 0, 0, 0, 0, 0x409, 0, 0);

    // {0.13, -0.21, 0}
    Vector const DeclinePosition = {NnFloatBits(0x3e051eb8), NnFloatBits(0xbe570a3d), 0.0f};
    DeclineButton = CreateWireButtonControl("OfferDialogDeclineButton", kOfferDialogSelectSound,
                                            &DeclinePosition, &ButtonExtents, 0, 0, 0x333, 0, 0);

    // {-0.23, -0.188, 0}, 0.172 x 0.06, 1.5.
    Vector const AcceptTextPosition = {NnFloatBits(0xbe6b851f), NnFloatBits(0xbe408312), 0.0f};
    AcceptText = CreateTextControl("OfferDialogAcceptText", "ButtonText", &AcceptTextPosition,
                                   nullptr, NnFloatBits(0x3e3020c5), NnFloatBits(0x3d75c28f),
                                   NnFloatBits(0x3fc00000), 0x4ad, 1, 0, 1);

    // {0.041, -0.188, 0}, 0.177 x 0.06.
    Vector const DeclineTextPosition = {NnFloatBits(0x3d27ef9e), NnFloatBits(0xbe408312),
                                        0.0f};
    DeclineText = CreateTextControl("OfferDialogDeclineText", "ButtonText", &DeclineTextPosition,
                                    nullptr, NnFloatBits(0x3e353f7d), NnFloatBits(0x3d75c28f),
                                    NnFloatBits(0x3fc00000), 0x548, 1, 0, 1);

    Vector const Close2Position = {0.0f, NnFloatBits(0xbe570a3d), 0.0f};
    Close2Button = CreateWireButtonControl("OfferDialogClose2Button", kOfferDialogSelectSound,
                                           &Close2Position, &ButtonExtents, 0, 0, 0x334, 0, 0);

    // {-0.09, -0.19, 0}, 0.179 x 0.06.
    Vector const CloseTextPosition = {NnFloatBits(0xbdb851ec), NnFloatBits(0xbe428f5c), 0.0f};
    CloseText = CreateTextControl("OfferDialogCloseText", "ButtonText", &CloseTextPosition,
                                  nullptr, NnFloatBits(0x3e374bc7), NnFloatBits(0x3d75c28f),
                                  NnFloatBits(0x3fc00000), 0x3a1, 1, 0, 1);

    // {0, 0, -1.288}
    Vector const WindowPosition = {0.0f, 0.0f, NnFloatBits(0xbfa4dd2f)};
    SetPosition(&WindowPosition);

    RegisterEvent(USER_CANCEL);

    // FUN_004b5060 with the window's own +0x04 parent.
    return Win3D::Create(Parent, Data);
}

intptr_t NnOfferDialog::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_004a6590 (RET 0xc). Nothing reaches WinControl's switch.
    (void)B;
    if (Msg != kWinNotifyClick)
        return 0;
    if (A == reinterpret_cast<uintptr_t>(CloseButton) ||
        A == reinterpret_cast<uintptr_t>(DeclineButton) ||
        A == reinterpret_cast<uintptr_t>(Close2Button))
    {
        if (Callback != nullptr)
            Callback(Context, 1);
        return 1;
    }
    if (A != reinterpret_cast<uintptr_t>(AcceptButton))
        return 0;
    if (Callback != nullptr)
        Callback(Context, 0);
    return 1;
}

bool NnOfferDialog::OnEvent(unsigned MsgId)
{
    // LAB_004a6600 (RET 4).
    if (MsgId != USER_CANCEL)
        return false;
    if (bSingleButton != 0)
        return false;
    if (Callback != nullptr)
        Callback(Context, 1);
    return true;
}

void NnOfferDialog::Open(int Type, FmtStr const* Text, bool bInSingleButton,
                         FNnOfferDialogCallback InCallback, void* InContext)
{
    // FUN_004a6630 (thiscall, RET 0x14).
    // Every offer type but 1 marks the text's places known.
    if (Type != 1)
        KnowledgeMapRevealFmtStr(Text);
    Context = InContext;
    Callback = InCallback;

    // Retail's clone-into-list idiom: the list takes a clone of a clone, the first is deleted.
    FmtStrSprintf(Text, G_NnTextScratchB, G_NnTextScratchCapacity);
    RenderDisplayList Body;
    {
        StyleNode const Style(kTextStyleBody);
        RDLNode* const Temp = Style.Clone();
        RdlAppendNode(Body, Temp->Clone());
        delete Temp;
    }
    AppendTextMaybeXmlRdl(G_NnTextScratchB, static_cast<unsigned>(wcslen(G_NnTextScratchB)),
                          Body, false);
    OfferText->SetScrollOrigin(0, 0); // FUN_005798b0
    OfferText->ClearContent();        // FUN_0057b370
    OfferText->AppendContent(Body);   // FUN_0057b3e0

    // A stack table; Type is not range-checked.
    unsigned const TitleIds[5] = {0x546, 0x650, 0x547, 0x651, 0x651};
    Title->SetProperty(kTextPropSetIds, TitleIds[Type], 0);

    // Any other type leaves the buttons alone.
    if (Type == 0 || Type == 3 || Type == 1)
    {
        HideControl(Close2Button);
        HideControl(CloseText);
        ShowControl(AcceptButton);
        ShowControl(AcceptText);
        ShowControl(DeclineButton);
        ShowControl(DeclineText);
    }
    else if (Type == 2 || Type == 4)
    {
        HideControl(AcceptButton);
        HideControl(AcceptText);
        HideControl(DeclineButton);
        HideControl(DeclineText);
        ShowControl(Close2Button);
        ShowControl(CloseText);
    }

    // A single button: Accept moves to {0, -0.21, 0} (property 7 reads the old offset, 6 sets
    // the new) and its text by the same amount. Each difference and sum is stored as a float.
    if (bInSingleButton)
    {
        HideControl(DeclineButton);
        HideControl(DeclineText);
        HideControl(CloseButton);
        bSingleButton = 1;

        Vector AcceptOld;
        AcceptButton->SetProperty(kWinPropGetOffset, reinterpret_cast<uintptr_t>(&AcceptOld), 0);
        Vector const AcceptNew = {0.0f, NnFloatBits(0xbe570a3d), 0.0f};
        AcceptButton->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&AcceptNew), 0);
        float const DeltaZ = AcceptNew.z - AcceptOld.z;
        float const DeltaY = AcceptNew.y - AcceptOld.y;
        float const DeltaX = AcceptNew.x - AcceptOld.x;

        Vector TextOld;
        AcceptText->SetProperty(kWinPropGetOffset, reinterpret_cast<uintptr_t>(&TextOld), 0);
        Vector const TextNew = {DeltaX + TextOld.x, TextOld.y + DeltaY, TextOld.z + DeltaZ};
        AcceptText->SetProperty(kWinPropOffset, reinterpret_cast<uintptr_t>(&TextNew), 0);
    }
}
