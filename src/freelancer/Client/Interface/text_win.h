// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail TextWin (RTTI .?AUTextWin@@ : Win3D) -- a Win3D that draws one Font::String at its
// position. Base of Rollover (0x005d5454), ToolTip (0x005d5f7c), TextMsgButton (0x005e2844)
// and StaticText -> UIStaticText (0x005e5a64).
//
// vftable 0x005d54cc (29 slots): Win3D's 28 with these overrides --
//   0 FUN_004b5240 scalar deleting dtor around FUN_005a1af0,
//   4 FUN_004b51d0 HitTest (the ray's point at this object's depth inside the text box),
//   9 FUN_004b5180 Create, 10 FUN_005a1c10 Destroy, 13 FUN_005a1d40 Draw
// -- and one slot of its own, 28 LAB_005a1cb0 SetText.
//
// The TextWin constructor has no body of its own in the exe: every derived constructor
// inlines it (the Win3D part, the Font::String, the five colours zeroed); the rest is left for
// Create's InitTextDefaults.

#include "Common/Fonts/font_string.h"
#include "Interface/win3d.h"

#include <stddef.h>

class TextWin : public Win3D
{
public:
    TextWin();
    // FUN_005a1af0
    ~TextWin() override;

    // slot 4 FUN_004b51d0
    bool HitTest(float const* Ray) override;
    // slot 9 FUN_004b5180
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 10 FUN_005a1c10
    void Destroy() override;
    // slot 13 FUN_005a1d40
    void Draw() override;

    // slot 28 (+0x70) LAB_005a1cb0
    virtual void SetText(wchar_t const* NewText);

    // FUN_005a1c50 (thiscall) -- the text defaults Create installs.
    void InitTextDefaults();

    // FUN_005a1cc0 (thiscall, RET 8) -- a new font is handed to every child and resets the
    // layout generation; TextScale = Scale on both paths.
    void SetFont(char const* FontName, float Scale);

    FFontString Text;               // +0x07c (0x34)
    float TextScale;                // +0x0b0 -- 0.002f
    float Width;                    // +0x0b4 -- the text box, in world units at this depth
    float Height;                   // +0x0b8
    float Bounds[4];                // +0x0bc -- {0, 0, width, -height}, rebuilt by Draw
    unsigned long ColorIdle;        // +0x0cc -- DAT_00679bb0
    unsigned long ColorPulse;       // +0x0d0 -- pulse base, DAT_00679ba4
    unsigned long ColorD4;          // +0x0d4 -- zeroed; only ToolTip ApplyHover writes it, no reader
    unsigned long BackgroundColor;  // +0x0d8 -- the box Rollover / ToolTip Draw fill
    unsigned long ColorInactive;    // +0x0dc -- DAT_00679b94
    // +0xe0 / +0xe2 / +0xe3 / +0xe4 / +0xe5: written (InitTextDefaults, Rollover, ToolTip,
    // TextMsgButton::SetFlagsE4E5), never read in the exe.
    unsigned char FlagE0;           // +0x0e0
    unsigned char bDrawBackground;  // +0x0e1 -- Rollover / ToolTip Draw: the filled, outlined box
    unsigned char FlagE2;           // +0x0e2
    unsigned char FlagE3;           // +0x0e3
    unsigned char FieldE4;          // +0x0e4 -- not written by TextWin
    unsigned char FieldE5;          // +0x0e5 -- not written by TextWin
    unsigned char bEnabled;         // +0x0e6 -- Draw: idle / pulse colour, else ColorInactive
    unsigned char PadE7;            // +0x0e7
    unsigned HAlign;                // +0x0e8 -- Font::String draw-origin HAlign (1 centre, 2 right)

private:
    TextWin(TextWin const&) = delete;
    TextWin& operator=(TextWin const&) = delete;
};

static_assert(sizeof(TextWin) == 0xec, "retail TextWin is 0xec bytes");
static_assert(offsetof(TextWin, Text) == 0x7c, "TextWin +0x7c Font::String");
static_assert(offsetof(TextWin, TextScale) == 0xb0, "TextWin +0xb0 scale");
static_assert(offsetof(TextWin, Width) == 0xb4, "TextWin +0xb4 width");
static_assert(offsetof(TextWin, Bounds) == 0xbc, "TextWin +0xbc bounds");
static_assert(offsetof(TextWin, ColorIdle) == 0xcc, "TextWin +0xcc idle colour");
static_assert(offsetof(TextWin, ColorInactive) == 0xdc, "TextWin +0xdc inactive colour");
static_assert(offsetof(TextWin, FlagE0) == 0xe0, "TextWin +0xe0 flags");
static_assert(offsetof(TextWin, bEnabled) == 0xe6, "TextWin +0xe6");
static_assert(offsetof(TextWin, HAlign) == 0xe8, "TextWin +0xe8");

// FUN_004176c0 (cdecl, 8-byte return in EAX:EDX) -- a width/height at depth Z in world units
// to screen units through the Win3D camera.
void TextWinProjectExtent(float Width, float Height, float Z, float* OutWidth,
                          float* OutHeight);

// FUN_00417730 (cdecl, 8-byte return) -- a world point to its screen position through the
// Win3D camera; {0, 0} when it is behind the near plane.
void TextWinProjectPoint(float const* World, float* OutX, float* OutY);
