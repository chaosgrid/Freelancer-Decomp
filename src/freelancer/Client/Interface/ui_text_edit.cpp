// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/ui_text_edit.h"

#include "Interface/ui_page_desc.h"
#include "Interface/win_factory.h"

UiTextEdit::UiTextEdit()
    : LocalWidth(0.0f)
    , LocalHeight(0.0f)
{
    // Inlined into FUN_0059c6f0; retail writes no field of its own here.
}

UiTextEdit::~UiTextEdit()
{
    // FUN_0059cf50.
}

bool UiTextEdit::Create(Win3D* NewParent, void* Data)
{
    // FUN_0059a410 (RET 8): RichTextWin's Create called directly.
    bool const bResult = RichTextWin::Create(NewParent, nullptr);
    ClearText();
    SetText(L"TextEdit", nullptr);

    if (Data != nullptr)
    {
        FUiPayloadText const* const Payload = static_cast<FUiPayloadText const*>(Data);
        LocalWidth = Payload->Right;
        NormWidth = Payload->Right;
        LocalHeight = Payload->Bottom;
        NormHeight = Payload->Bottom;
        // Retail does not test the caption pointer.
        ClearText();
        SetText(Payload->CaptionUtf16, nullptr);
    }

    SetEditable(false);
    return bResult;
}

// DAT_0067db18.
WINFACTORY<UiTextEdit> G_UITextEditFactory("UITextEdit");
