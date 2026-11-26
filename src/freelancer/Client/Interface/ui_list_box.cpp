// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/ui_list_box.h"

#include "Interface/ui_page.h"
#include "Interface/ui_page_desc.h"
#include "Interface/win_factory.h"

UiListBox::UiListBox()
    : LocalWidth(0.0f)
    , LocalHeight(0.0f)
{
    // Inlined into FUN_0059c810. +0x8b0 / +0x8b4 are left unset by retail; the port zeroes them.
}

UiListBox::~UiListBox()
{
    // FUN_0059ce00.
}

bool UiListBox::Create(Win3D* NewParent, void* Payload)
{
    // FUN_0059a490 (RET 8): ListBox's Create inlined, then the payload's width / height into
    // both the unprojected and the projected extent.
    SelectedIndex = -1;
    HoverIndex = -1;
    HighlightIndex = -1;
    ItemCount = 0;
    VisibleRows = 0x10;
    FirstVisible = 0;
    bDrawFrame = 1;
    bDrawRowRules = 0;
    bSelectable = 1;
    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>((Flags & ~kWin3DUnregistered) | kWin3DActive | kWin3DVisible);

    if (Payload != nullptr)
    {
        FUiPayloadBounds const* Desc = static_cast<FUiPayloadBounds const*>(Payload);
        LocalWidth = Desc->Right;
        Width = Desc->Right;
        LocalHeight = Desc->Bottom;
        Height = Desc->Bottom;
    }
    return true;
}

void UiListBox::SetTransform(Transform const* NewXform)
{
    // LAB_0059a530 (RET 4): then the shared extent projection (DAT_0067dbc0) with the new Z.
    SetOrientation(&NewXform->Rotation);
    SetPosition(&NewXform->Translation);
    UiWidgetProjectExtent(&Orientation, NewXform->Translation.z, LocalWidth, LocalHeight, &Width,
                          &Height);
}

// DAT_0067daec.
WINFACTORY<UiListBox> G_UIListBoxFactory("UIListBox");
