// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail UIRichTextWin (RTTI .?AUUIRichTextWin@@ : RichTextWin : BaseRichTextWin : WinControl :
// Win3D). vftable 0x005e58d4 (48 slots) + IDataViewOwner face 0x005e58c8 at +0x7c.
// Overrides RichTextWin's slots 0 (FUN_0059bf60) and 9. WINFACTORY<UIRichTextWin> DAT_0067da68,
// Create FUN_0059cbd0 (operator new(0x498)). No fields of its own.

#include "Interface/rich_text_win.h"

class UiRichTextWin : public RichTextWin
{
public:
    UiRichTextWin();
    // FUN_0059bf80
    ~UiRichTextWin() override;

    // slot 9 (+0x24) -- FUN_0059bfa0 (RET 8); Data is an FUiPayloadBounds*.
    bool Create(Win3D* NewParent, void* Data) override;
};

static_assert(sizeof(UiRichTextWin) == 0x498, "FUN_0059cbd0 allocates 0x498");
