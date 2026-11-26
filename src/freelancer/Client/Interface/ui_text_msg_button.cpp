// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/ui_text_msg_button.h"

#include "Common/Graphics/client_mesh_load.h"
#include "Common/Fonts/ids_rdl.h"
#include "Interface/menu_ui_init.h"
#include "NeuroNet/nn_ui_pulse.h"
#include "Interface/ui_page.h"
#include "Interface/ui_page_desc.h"
#include "Interface/win_factory.h"
#include "NeuroNet/Chat/chat_init.h"        // EChatTextStyle

#include "DALib.h"
#include "common_csys.h"
#include "engine/engbase.h"
#include "x86math.h"

#include <string.h>

namespace
{
    // DAT_0067dbac (static init LAB_00599da0) -- the idle caption attributes (the hover colour
    // is computed per frame).
    TextRenderAttributes const G_UiTextMsgButtonIdleAttrs = {0xe0c38708ul};

    // DAT_0067dba4 (LAB_00599dc0) -- the caption of a non-interactive button.
    TextRenderAttributes const G_UiTextMsgButtonInactiveAttrs = {0x80808008ul};

    // .rdata 0x005d4f58, the "<default>" style mesh.
    char const kUiTextMsgButtonDefaultStyle[] = "Interface\\NeuroNet\\Inventory\\FRONT_button.cmp";

}

UiTextMsgButton::UiTextMsgButton()
    : bInteractive(0)
    , LocalWidth(0.0f)
    , LocalHeight(0.0f)
    , MessageArg(0)
    , Field4c4(0)
{
    // Inlined into FUN_0059c580. Retail leaves +0x498..+0x4a4 and +0x4c0 to Create.
    CaptionAttrs.Packed = 0;
    Pad499[0] = 0;
    Pad499[1] = 0;
    Pad499[2] = 0;
}

UiTextMsgButton::~UiTextMsgButton()
{
    // FUN_0059ce50.
}

void UiTextMsgButton::SetPosition(Vector const* NewPosition)
{
    // LAB_00599eb0 (RET 4). The style mesh sits at the button's centre: half the payload size
    // along the control's axes, scaled onto the z = -1 plane and written straight into the
    // WinControl offset +0xa8..+0xb0.
    I3DMathEngine* const Math = UiWidgetMathEngine();
    Vector Half;
    Half.x = LocalWidth * 0.5f;
    Half.y = LocalHeight * -0.5f;
    Half.z = 0.0f;
    Vector Centre;
    Math->TransformMatrix(Centre, Orientation, Half);
    double const SumX = static_cast<double>(Centre.x) + NewPosition->x;
    double const SumY = static_cast<double>(Centre.y) + NewPosition->y;
    double const Scale = -1.0 / NewPosition->z;
    Offset.x = static_cast<float>(SumX * Scale - NewPosition->x);
    Offset.y = static_cast<float>(SumY * Scale - NewPosition->y);
    Offset.z = -1.0f - NewPosition->z;
    BaseRichTextWin::SetPosition(NewPosition);
}

void UiTextMsgButton::SetOrientation(Matrix const* NewOrientation)
{
    // LAB_00599e00 (RET 4): with a style mesh bound, the instance is reset to the identity
    // orientation and updated.
    WinControl::SetOrientation(NewOrientation);
    if (!ClientMeshHolderHasInstance(&MeshHolder))
        return;
    Matrix Identity;
    Identity.E[0][0] = 1.0f;
    Identity.E[0][1] = 0.0f;
    Identity.E[0][2] = 0.0f;
    Identity.E[1][0] = 0.0f;
    Identity.E[1][1] = 1.0f;
    Identity.E[1][2] = 0.0f;
    Identity.E[2][0] = 0.0f;
    Identity.E[2][1] = 0.0f;
    Identity.E[2][2] = 1.0f;
    DALib::Engine->SetInstanceOrientation(MeshHolder.EngineInstance, &Identity);
    DALib::Engine->UpdateInstance(MeshHolder.EngineInstance, 0, nullptr);
}

void UiTextMsgButton::ApplyCaptionLayout()
{
    // FUN_0059a110.
    ClearContent();

    JustifyNode Justify(TextJustifyCenter);
    AppendNode(Justify);

    RDLRefNode CaptionRef(CaptionBodyList);
    AppendNode(CaptionRef);

    StyleNode Style(kTextStyleButton);
    AppendNode(Style);

    TRANode Tra(CaptionAttrs, 0xffffff00u, 0u);
    AppendNode(Tra);

    RDLRefNode TextRef(TextBodyList);
    AppendNode(TextRef);
}

bool UiTextMsgButton::Create(Win3D* NewParent, void* Data)
{
    // FUN_00599f60 (RET 8).
    bool const bResult = RichTextWin::Create(NewParent, nullptr);
    bInteractive = 1;
    CaptionAttrs = G_UiTextMsgButtonIdleAttrs;

    FUiPayloadTextMsgButton const* const Payload =
        static_cast<FUiPayloadTextMsgButton const*>(Data);
    if (Payload != nullptr)
    {
        LocalWidth = Payload->Right;
        NormWidth = Payload->Right;
        LocalHeight = Payload->Bottom;
        NormHeight = Payload->Bottom;

        if (Payload->ExtraId != 0)
        {
            // Appends, with no trailing-paragraph trim here.
            LoadIdsIntoRdl(static_cast<unsigned>(Payload->ExtraId), TextBodyList);
        }
        else
        {
            TextNode Text(Payload->CaptionUtf16, -1);
            RdlAppendNode(TextBodyList, Text.Clone());
        }
        MessageArg = Payload->ExtraInt;
    }

    SetAlignMode(2);

    // Retail: the payload is read here on both paths, even when it was null above (0059a098
    // `mov eax,[edi+14h]` after the 0059a08f null jump).
    char const* const StyleName = Payload->StyleName;
    if (StyleName != nullptr && strcmp(StyleName, "<none>") != 0)
    {
        if (strcmp(StyleName, "<default>") == 0)
            SetMesh(kUiTextMsgButtonDefaultStyle, 0);
        else
            SetMesh(StyleName, 0);
        Field2d2 = 1;
    }

    ApplyCaptionLayout();
    return bResult;
}

void UiTextMsgButton::Draw()
{
    // LAB_0059a260. The caption follows three states: hovered (the pulse around the amber),
    // idle and non-interactive. Only the colour bytes +0x4c1..+0x4c3 are compared; a change
    // rebuilds the content.
    bool const bHovered = Win3DGetHover() == this;
    unsigned long PulseArgb = 0;
    NnUiPulseBlendFromArgb(&PulseArgb, G_NnPaletteAmber);
    unsigned char* const Attrs = reinterpret_cast<unsigned char*>(&CaptionAttrs.Packed);

    bool bRelayout = false;
    if (bInteractive != 0)
    {
        if (bHovered)
        {
            unsigned char const R = static_cast<unsigned char>(PulseArgb & 0xff);
            unsigned char const G = static_cast<unsigned char>((PulseArgb >> 8) & 0xff);
            unsigned char const B = static_cast<unsigned char>((PulseArgb >> 16) & 0xff);
            if (Attrs[1] != R || Attrs[2] != G || Attrs[3] != B)
            {
                Attrs[1] = R;
                Attrs[2] = G;
                Attrs[3] = B;
                bRelayout = true;
            }
        }
        else if (((CaptionAttrs.Packed ^ G_UiTextMsgButtonIdleAttrs.Packed) & 0xffffff00u) != 0)
        {
            CaptionAttrs = G_UiTextMsgButtonIdleAttrs;
            bRelayout = true;
        }
    }
    else if (((CaptionAttrs.Packed ^ G_UiTextMsgButtonInactiveAttrs.Packed) & 0xffffff00u) != 0)
    {
        CaptionAttrs = G_UiTextMsgButtonInactiveAttrs;
        bRelayout = true;
    }
    if (bRelayout)
        ApplyCaptionLayout();

    // Retail's red debug outline here is gated on DAT_0067dbc4, which has no writer in the PE:
    // never drawn, not ported.

    BaseRichTextWin::Draw();
}

void UiTextMsgButton::MouseEnter(Vector const* /*Ray*/)
{
    // LAB_0059a240 (RET 4).
    if (bInteractive != 0)
        PlayUiRolloverSound();
}

bool UiTextMsgButton::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_0059a3d0 (RET 4): a single left press on an interactive button tells the parent and
    // is consumed.
    if (Event->Action != kWin3DMousePress)
        return false;
    if (Event->Button != kWin3DMouseLeft)
        return false;
    if (bInteractive == 0)
        return false;
    if (Parent != nullptr)
        Parent->ProcessMessage(kWinMsgCommand, static_cast<unsigned>(MessageArg), 0u);
    return true;
}

// DAT_0067db44.
WINFACTORY<UiTextMsgButton> G_UITextMsgButtonFactory("UITextMsgButton");
