// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// UITextEdit -- PAGE_*.txt edit field (RTTI .?AUUITextEdit@@ : RichTextEditWin : RichTextWin :
// BaseRichTextWin : WinControl; Event::Handler face at +0x498).
//
// WINFACTORY<UITextEdit> DAT_0067db18; Create FUN_0059c6f0 (operator new(0x538)). vftables
// 0x005e5bd4 / 0x005e5bc8 (+0x7c) / 0x005e5bc0 (+0x498); overrides slots 0 (FUN_0059cde0) and 9.
//
// Retail offsets are kept in the comments only: RichTextEditWin holds std containers, so the
// port's layout past +0x4c4 does not match retail's 0x538 bytes.

#include "Interface/rich_text_win.h"

class UiTextEdit : public RichTextEditWin
{
public:
    UiTextEdit();
    ~UiTextEdit() override;

    // slot 9 (+0x24) -- FUN_0059a410 (RET 8); Data is an FUiPayloadText*.
    bool Create(Win3D* NewParent, void* Data) override;

    float LocalWidth;      // +0x530 -- payload +0x00 (also +0x330)
    float LocalHeight;     // +0x534 -- payload +0x04 (also +0x334)
};
