// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail FLImeInputBox (RTTI .?AUFLImeInputBox@@ : IMEInputBox : CInputBox), vftable 0x005e2250,
// operator new(0xa0). A RichTextEditWin builds one at +0x52c while an IMEFocus singleton exists
// and routes keys and characters through it; the box writes the composed text back into its
// owner (UpdatedText) and draws the composition through two Font::Strings.
//
// vftable slots not overridden here stay IMEInputBox's / CInputBox's; slot 0 is FUN_00564260
// (scalar deleting dtor).

#include "imeui.h"

class FontStringChild;
class RichTextEditWin;

struct FLImeInputBox : public IMEInputBox
{
public:
    // FUN_00564280 (RET 4)
    explicit FLImeInputBox(RichTextEditWin* InOwner);
    // FUN_005641f0
    ~FLImeInputBox() override;

    // slot 3 LAB_005643c0 (RET 4) -- IMEInputBox::Resize with {0, 0, right, bottom}.
    void Resize(const RECT* pRect) override;
    // slot 6 LAB_00564390 (RET 4).
    void UpdatedText(const unsigned short* pText) override;
    // slot 11 LAB_0053e4a0 -- empty.
    void Draw(void* pContext) override;
    // slot 12 FUN_005643f0 (RET 0x10)
    void DrawString(int X, int Y, unsigned Color, const wchar_t* Text) override;

    RichTextEditWin* Owner;             // +0x94
    FontStringChild* TextString;        // +0x98 -- the text DrawString paints
    FontStringChild* CompositionString; // +0x9c -- the composed text, measured for the X offset

private:
    FLImeInputBox(FLImeInputBox const&) = delete;
    FLImeInputBox& operator=(FLImeInputBox const&) = delete;
};
