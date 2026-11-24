// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/ui_static_text.h"

#include "Interface/ui_page.h"
#include "Interface/ui_page_desc.h"
#include "Interface/win_factory.h"

UiStaticText::UiStaticText()
    : LocalWidth(0.0f)
    , LocalHeight(0.0f)
{
    // Inlined into FUN_0059c3d0. Retail leaves +0xec / +0xf0 to Create.
}

UiStaticText::~UiStaticText()
{
    // FUN_0059cda0.
}

bool UiStaticText::Create(Win3D* NewParent, void* Payload)
{
    // FUN_00599ac0 (RET 8).
    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>((Flags & 0xfb) | kWin3DActive | kWin3DVisible);

    InitTextDefaults();

    Flags = static_cast<unsigned char>(Flags & 0xfe);
    Width = 0.1f;
    Height = 0.1f;
    bDrawBackground = 0;
    SetText(L"StaticText");

    if (Payload != nullptr)
    {
        FUiPayloadText const* TextPayload = static_cast<FUiPayloadText const*>(Payload);
        LocalWidth = TextPayload->Right;
        Width = TextPayload->Right;
        LocalHeight = TextPayload->Bottom;
        Height = TextPayload->Bottom;
        SetText(TextPayload->CaptionUtf16);
        BackgroundColor = 0xff000000u;
    }
    return true;
}

void UiStaticText::SetTransform(float const* Transform)
{
    // LAB_00599b70 (RET 4). Unlike the other UI* widgets, the projection's matrix is the
    // argument block itself, not this+0x3c.
    SetOrientation(Transform);
    SetPosition(Transform + 9);
    UiWidgetProjectExtent(Transform, Transform[11], LocalWidth, LocalHeight, &Width, &Height);
}

// DAT_0067db70.
WINFACTORY<UiStaticText> G_UIStaticTextFactory("UIStaticText");
