// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/fl_ime_input_box.h"

#include "Interface/client_ime_focus.h"   // ClientConfirmPlayerFont, ClientIme*Key, ClientImeSwapFontMaterialKey
#include "font.h"               // IFont::GetLogFont
#include "Common/Fonts/font_atlas.h"         // FontAtlasSetDrawColor (FUN_00417780)
#include "Common/Fonts/font_string.h"        // FontStringChild, FFontStringDrawOrigin
#include "Interface/rich_text_win.h"      // RichTextEditWin::SetText
#include "Common/Graphics/screen_quad_state.h"  // ScreenQuadFlush (FUN_00403ea0)

#include "DALib.h"              // DALib::Pipeline

FLImeInputBox::FLImeInputBox(RichTextEditWin* InOwner)
    // FUN_00564280.
    : IMEInputBox(ClientConfirmPlayerFont()->GetLogFont())
    , Owner(InOwner)
    , TextString(nullptr)
    , CompositionString(nullptr)
{
    TextString = new FontStringChild();
    TextString->BindFont(ClientConfirmPlayerFont());
    TextString->SetShadow(0);
    CompositionString = new FontStringChild();
    CompositionString->BindFont(ClientConfirmPlayerFont());
    CompositionString->SetShadow(0);
}

FLImeInputBox::~FLImeInputBox()
{
    // FUN_005641f0.
    if (TextString != nullptr)
        TextString->Release();
    if (CompositionString != nullptr)
        CompositionString->Release();
}

void FLImeInputBox::Resize(const RECT* pRect)
{
    // LAB_005643c0.
    RECT Local;
    Local.left = 0;
    Local.top = 0;
    Local.right = pRect->right;
    Local.bottom = pRect->bottom;
    IMEInputBox::Resize(&Local);
}

void FLImeInputBox::UpdatedText(const unsigned short* pText)
{
    // LAB_00564390.
    wchar_t const* const Text = reinterpret_cast<wchar_t const*>(pText);
    Owner->SetText(Text, nullptr);
    CompositionString->SetText(Text);
}

void FLImeInputBox::Draw(void* /*pContext*/)
{
    // LAB_0053e4a0.
}

void FLImeInputBox::DrawString(int X, int Y, unsigned Color, const wchar_t* Text)
{
    // FUN_005643f0 (RET 0x10).
    unsigned PreviousKey = 0;
    ClientImeSwapFontMaterialKey(&PreviousKey, static_cast<unsigned>(ClientImeTextKey()));

    // The composed text's width comes off X.
    int Size[2];
    CompositionString->GetSize(Size);
    int const Left = X - Size[0];

    // 0xAARRGGBB -> R, G, B, A in memory.
    unsigned char Rgba[4];
    Rgba[0] = static_cast<unsigned char>((Color >> 16) & 0xffu);
    Rgba[1] = static_cast<unsigned char>((Color >> 8) & 0xffu);
    Rgba[2] = static_cast<unsigned char>(Color & 0xffu);
    Rgba[3] = static_cast<unsigned char>((Color >> 24) & 0xffu);
    unsigned Packed = 0;
    Packed |= static_cast<unsigned>(Rgba[0]);
    Packed |= static_cast<unsigned>(Rgba[1]) << 8;
    Packed |= static_cast<unsigned>(Rgba[2]) << 16;
    Packed |= static_cast<unsigned>(Rgba[3]) << 24;
    FontAtlasSetDrawColor(Packed);

    TextString->SetText(Text);

    // The sums convert as unsigned 32-bit values. Retail's origin record tail is stack residue;
    // the port's Paint reads X and Y only, so the rest is zero.
    unsigned const SumX = static_cast<unsigned>(m_dwMarginX) + static_cast<unsigned>(Left);
    unsigned const SumY = static_cast<unsigned>(m_dwMarginY) + static_cast<unsigned>(Y);
    FFontStringDrawOrigin Origin;
    Origin.X = static_cast<float>(static_cast<double>(SumX));
    Origin.Y = static_cast<float>(static_cast<double>(SumY));
    Origin.Scale = 0.0f;
    Origin.Height = 0.0f;
    Origin.HAlign = 0;
    Origin.VAlign = 0;
    TextString->Paint(&Origin, nullptr, nullptr);

    unsigned Discard = 0;
    ClientImeSwapFontMaterialKey(&Discard, PreviousKey);
    ScreenQuadFlush(DALib::Pipeline, ClientImeBackgroundKey());
    ScreenQuadFlush(DALib::Pipeline, ClientImeTextKey());
}
