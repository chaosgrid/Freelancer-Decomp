// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// UITextMsgButton -- PAGE_*.txt text button (RTTI .?AUUITextMsgButton@@ : RichTextWin :
// BaseRichTextWin : WinControl; IDataViewOwner face at +0x7c).
//
// WINFACTORY<UITextMsgButton> DAT_0067db44; Create FUN_0059c580 (operator new(0x4c8)).
// vftable 0x005e5af4 (+0x7c face 0x005e5ae4) overrides RichTextWin's slots 0 (FUN_0059cdc0),
// 5, 7, 9, 13, 15 and 19. Fields past RichTextWin's 0x498 as below; size 0x4c8.

#include "Interface/rich_text_win.h"

#include <stddef.h>

class UiTextMsgButton : public RichTextWin
{
public:
    UiTextMsgButton();
    // FUN_0059ce50
    ~UiTextMsgButton() override;

    // slot 5 LAB_00599eb0 (RET 4) -- places the style mesh, then the base SetPosition.
    void SetPosition(float const* NewPosition) override;
    // slot 7 LAB_00599e00 (RET 4) -- keeps the style mesh upright.
    void SetOrientation(Matrix const* NewOrientation) override;
    // slot 9 FUN_00599f60 (RET 8); Data is an FUiPayloadTextMsgButton*.
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 13 LAB_0059a260
    void Draw() override;
    // slot 15 LAB_0059a240 (RET 4) -- an interactive button plays the rollover sound.
    void MouseEnter(float const* Ray) override;
    // slot 19 LAB_0059a3d0 (RET 4) -- a click sends 0x3004 (MessageArg) to the parent.
    bool MouseEvent(FWin3DMouseEvent const* Event) override;

    // FUN_0059a110 -- rebuild the content from the caption and text lists.
    void ApplyCaptionLayout();

    unsigned char bInteractive;            // +0x498 -- Create writes 1
    unsigned char Pad499[3];               // +0x499
    float LocalWidth;                      // +0x49c
    float LocalHeight;                     // +0x4a0
    int MessageArg;                        // +0x4a4
    RenderDisplayList CaptionBodyList;     // +0x4a8
    RenderDisplayList TextBodyList;        // +0x4b4
    TextRenderAttributes CaptionAttrs;     // +0x4c0
    unsigned Field4c4;                     // +0x4c4 -- never written

private:
    UiTextMsgButton(UiTextMsgButton const&) = delete;
    UiTextMsgButton& operator=(UiTextMsgButton const&) = delete;
};

static_assert(sizeof(UiTextMsgButton) == 0x4c8, "FUN_0059c580 allocates 0x4c8");
static_assert(offsetof(UiTextMsgButton, bInteractive) == 0x498, "UITextMsgButton +0x498");
static_assert(offsetof(UiTextMsgButton, MessageArg) == 0x4a4, "UITextMsgButton +0x4a4");
static_assert(offsetof(UiTextMsgButton, CaptionBodyList) == 0x4a8, "UITextMsgButton +0x4a8");
static_assert(offsetof(UiTextMsgButton, TextBodyList) == 0x4b4, "UITextMsgButton +0x4b4");
static_assert(offsetof(UiTextMsgButton, CaptionAttrs) == 0x4c0, "UITextMsgButton +0x4c0");
