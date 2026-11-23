// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/ui_rich_text_win.h"

#include "Interface/ui_page_desc.h"
#include "Interface/win_factory.h"

UiRichTextWin::UiRichTextWin()
{
    // Inlined into FUN_0059cbd0.
}

UiRichTextWin::~UiRichTextWin()
{
    // FUN_0059bf80.
}

bool UiRichTextWin::Create(Win3D* NewParent, void* Data)
{
    // FUN_0059bfa0 (RET 8): RichTextWin's Create without a payload, then the page payload's
    // size into +0x330 / +0x334.
    bool const bCreated = RichTextWin::Create(NewParent, nullptr);
    if (bCreated && Data != nullptr)
    {
        FUiPayloadBounds const* const Bounds = static_cast<FUiPayloadBounds const*>(Data);
        NormWidth = Bounds->Right;
        NormHeight = Bounds->Bottom;
    }
    return bCreated;
}

// DAT_0067da68.
WINFACTORY<UiRichTextWin> G_UIRichTextWinFactory("UIRichTextWin");
