// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// UIListBox -- PAGE_*.txt list widget (RTTI .?AUUIListBox@@ : ListBox : Win3D).
//
// WINFACTORY<UIListBox> DAT_0067daec (static init LAB_0059c270); Create FUN_0059c810
// (operator new(0x8b8)). UIListBox has no constructor body of its own. ListBox is 0x8b0 bytes
// (list_box.h), so the two floats at +0x8b0 / +0x8b4 -- written only by Create and read only by
// SetTransform -- are UIListBox's; retail's ctor leaves them unset.
//
// vftable 0x005e5cb4 overrides ListBox's slots 0 (FUN_0059ce00), 8, 9 and 13. Slot 13
// (0x0059a6a0) draws a debug frame guarded by DAT_0067dbc4, then ListBox::Draw; DAT_0067dbc4 has
// no writer in the PE, so the byte stays 0 and the port does not override slot 13.

#include "Interface/list_box.h"

class UiListBox : public ListBox
{
public:
    UiListBox();
    ~UiListBox() override;

    // slot 8 (+0x20) -- LAB_0059a530.
    void SetTransform(Transform const* NewXform) override;
    // slot 9 (+0x24) -- FUN_0059a490.
    bool Create(Win3D* NewParent, void* Payload) override;

    float GetWidth() const { return Width; }
    float GetHeight() const { return Height; }

    float LocalWidth;                       // +0x8b0 -- unprojected width (slot 9 payload)
    float LocalHeight;                      // +0x8b4 -- unprojected height (slot 9 payload)

private:
    UiListBox(UiListBox const&) = delete;
    UiListBox& operator=(UiListBox const&) = delete;
};

static_assert(sizeof(UiListBox) == 0x8b8, "FUN_0059c810 allocates 0x8b8");
static_assert(offsetof(UiListBox, LocalWidth) == 0x8b0, "UIListBox +0x8b0");
