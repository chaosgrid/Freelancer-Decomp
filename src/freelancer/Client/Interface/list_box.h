// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail ListBox (RTTI .?AUListBox@@ : Win3D) -- a Win3D drawing up to 0x100 single-line
// Font::String rows with a bevelled frame and optional row rules. Base of UIListBox
// (0x005e5cb4); also embedded by value twice in NN_LoadSave (+0x5dc and +0xe98, see
// nn_load_save.h).
//
// vftable 0x005e5f9c (28 slots): Win3D's with the overrides below; slot 0 is FUN_005a20e0
// (scalar deleting dtor).
//
// Size 0x8b0, settled by NN_LoadSave: its two ListBoxes sit at +0x5dc and +0xe98 with a Vector
// written between them at +0xe8c (0x5dc + 0x8b0 = 0xe8c). The two floats UIListBox keeps at
// +0x8b0 / +0x8b4 are its own (UIListBox allocates 0x8b8; see ui_list_box.h).

#include "Common/Fonts/font_string.h"
#include "Interface/win3d.h"

#include <stddef.h>

class ListBox : public Win3D
{
public:
    // FUN_005a2100
    ListBox();
    // FUN_005a1fe0
    ~ListBox() override;

    // slot 4 FUN_005a26a0
    bool HitTest(float const* Ray) override;
    // slot 9 LAB_005a2070
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_005a2230
    void Update() override;
    // slot 13 LAB_005a2360
    void Draw() override;
    // slot 14 LAB_0059c8b0
    void MouseOver(float const* Ray) override;
    // slot 19 FUN_005a2710
    bool MouseEvent(FWin3DMouseEvent const* Event) override;

    // FUN_005a1eb0 (thiscall, RET 8) -- replace the text of an existing row.
    bool SetItemText(int Index, wchar_t const* Text);
    // FUN_005a1f20 (thiscall, RET 4) -- the data word of a row, 0 when out of range.
    int GetItemData(int Index);
    // FUN_005a1f50 (thiscall, RET 8) -- append a row; its index, or -1 when full.
    int AddItem(wchar_t const* Text, int Data);

    int HoverIndex;                         // +0x07c -- -1 = none, rebuilt by Update from the cursor
    // +0x080 -- drawn with the pulse colour; ListBox never sets it (NN_LoadSave copies the
    // hovered row into it).
    int HighlightIndex;
    IFont* Font;                            // +0x084 -- "ListText"
    float TextScale;                        // +0x088 -- 0.002f
    int ItemData[0x100];                    // +0x08c -- never initialised by the ctor
    FontStringChild* Items[0x100];          // +0x48c -- created on demand
    int FirstVisible;                       // +0x88c
    float Timer890;                         // +0x890 -- Update adds the frame time; MouseOver
    float Timer894;                         // +0x894 -- sets both to 10.0f; no reader seen
    float Width;                            // +0x898 -- world units
    float Height;                           // +0x89c
    unsigned char bDrawFrame;               // +0x8a0
    unsigned char bDrawRowRules;            // +0x8a1
    unsigned char bSelectable;              // +0x8a2
    unsigned char Pad8a3;                   // +0x8a3
    int ItemCount;                          // +0x8a4
    int VisibleRows;                        // +0x8a8
    int SelectedIndex;                      // +0x8ac

private:
    // The shared body of SetItemText / AddItem.
    void SetRowText(int Index, wchar_t const* Text);

    ListBox(ListBox const&) = delete;
    ListBox& operator=(ListBox const&) = delete;
};

static_assert(sizeof(void*) != 4 || sizeof(ListBox) == 0x8b0,
              "NN_LoadSave embeds ListBoxes at +0x5dc and +0xe98 with a Vector at +0xe8c");
static_assert(offsetof(ListBox, HoverIndex) == 0x7c, "ListBox +0x7c");
static_assert(offsetof(ListBox, Font) == 0x84, "ListBox +0x84 font");
static_assert(offsetof(ListBox, ItemData) == 0x8c, "ListBox +0x8c item data");
static_assert(offsetof(ListBox, Items) == 0x48c, "ListBox +0x48c rows");
static_assert(offsetof(ListBox, FirstVisible) == 0x88c, "ListBox +0x88c");
static_assert(offsetof(ListBox, Width) == 0x898, "ListBox +0x898");
static_assert(offsetof(ListBox, bDrawFrame) == 0x8a0, "ListBox +0x8a0");
static_assert(offsetof(ListBox, ItemCount) == 0x8a4, "ListBox +0x8a4");
static_assert(offsetof(ListBox, SelectedIndex) == 0x8ac, "ListBox +0x8ac");

// DAT_0067dd54 and DAT_0067dd50: the frame's dark and light RGB bytes.
extern unsigned char G_ListBoxShadeRgba[4];
extern unsigned char G_ListBoxLightRgba[4];
