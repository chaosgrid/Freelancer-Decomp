// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail ServerFilterDialog (RTTI .?AVServerFilterDialog@@ : WinControl, COL 0x005efe30) -- the
// modal box PageServerSelect's "SetFilter" button opens over the server list. It edits the
// page's ServerFilter in place and keeps a copy to put back on cancel.
//
// WINFACTORY<ServerFilterDialog> DAT_0067a444 "ServerFilterDialog": static init LAB_005626e0,
// Create FUN_00563900 (operator new(0x39c), the ctor inlined).
//
// vftable 0x005e20ec (45 slots) -- WinControl's with the overrides below; slot 0 is
// FUN_00562f40 (scalar deleting dtor). The IDataViewOwner face (+0x7c, 0x005e20e0) keeps
// WinControl's entries.
//
// Only row 0 ("ping") has an edit box.

#include "Interface/server_filter.h"
#include "Interface/wincontrol_base.h"

#include <stddef.h>

class RichTextEditWin;
class RichTextWin;

// DAT_00614828 -- one 0x20-byte description per row, five rows (filled mostly by the static
// initializer LAB_00562750). The toggle is placed at {X, Y, Z}, the label at
// {LabelX, Y + 0.015, Z}, the edit box at {DataX, Y + 0.015, Z}, each plus the dialog's +0xa8
// offset.
struct FServerFilterRowDesc
{
    float Y;                  // +0x00
    float Z;                  // +0x04
    float X;                  // +0x08
    float LabelX;             // +0x0c
    float DataX;              // +0x10
    unsigned char bHasData;   // +0x14 -- build "FilterData%d"; the label is 0.28 wide, else 0.38
    unsigned LabelIds;        // +0x18
    int ToolTipIds;           // +0x1c -- the toggle's two tooltip ids and the label's IdsAlt
};

// +0x338 element, 0xc bytes (ctor LAB_00562a40 / dtor LAB_00562a50 zero all three).
struct FServerFilterDialogRow
{
    // FUN_00562a60 (thiscall on the row, RET 8) -- "FilterCheck%d" / "FilterLabel%d" /
    // "FilterData%d" under Dialog, then the row counter DAT_006148c8 raised.
    void Build(WinControl* Dialog, FServerFilterRowDesc const& Desc);

    WinControl* Check = nullptr;      // +0x0 -- ToggleButtonControl
    RichTextWin* Label = nullptr;     // +0x4
    RichTextEditWin* Data = nullptr;  // +0x8 -- null unless the row description has one
};

class ServerFilterDialog : public WinControl
{
public:
    // Inlined into FUN_00563900.
    ServerFilterDialog();
    // FUN_00562f60.
    ~ServerFilterDialog() override;

    // slot 9 FUN_005631c0 (RET 8); Data is the ServerFilter* to edit.
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 19 FUN_005635a0 (RET 4)
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // slot 20 FUN_00563530 (RET 8)
    bool KeyEvent(unsigned Key, unsigned char* Modifiers) override;
    // slot 23 FUN_00563630 (RET 0xc)
    bool ProcessMessage(unsigned Msg, unsigned A, unsigned B) override;
    // slot 42 FUN_005636d0 (RET 0xc)
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    WinControl* OkButton;                 // +0x32c "FilterOk" (RichTextWireButtonControl)
    WinControl* CancelButton;             // +0x330 "FilterCancel" (ButtonControl)
    RichTextWin* Title;                   // +0x334 "FilterTitle"
    FServerFilterDialogRow Rows[5];       // +0x338
    ServerFilter* Target;                 // +0x374 the caller's filter, edited live
    ServerFilter Backup;                  // +0x378 assigned back on every cancel path

private:
    // FUN_00562fd0 -- the five toggles and the ping box into *Target.
    void ReadControls();
    // FUN_005630f0 -- *Target into the five toggles and the ping box.
    void WriteControls();
    // The close every exit path inlines in retail (four copies).
    void Close();

    ServerFilterDialog(ServerFilterDialog const&) = delete;
    ServerFilterDialog& operator=(ServerFilterDialog const&) = delete;
};

static_assert(sizeof(void*) != 4 || sizeof(ServerFilterDialog) == 0x39c,
              "retail ServerFilterDialog is 0x39c bytes (operator_new 0x00563919)");
static_assert(sizeof(void*) != 4 || offsetof(ServerFilterDialog, Rows) == 0x338,
              "ServerFilterDialog +0x338");
static_assert(sizeof(void*) != 4 || offsetof(ServerFilterDialog, Target) == 0x374,
              "ServerFilterDialog +0x374");
static_assert(sizeof(void*) != 4 || offsetof(ServerFilterDialog, Backup) == 0x378,
              "ServerFilterDialog +0x378");

// FUN_005637b0 (cdecl) -- build a ServerFilterDialog under Parent for Filter, centre it, capture
// the mouse and pump frames while one is registered. False only when the message pump sees
// WM_QUIT; true otherwise, including when the factory failed. Caller: PageServerSelect.
bool ServerFilterDialogRun(Win3D* Parent, ServerFilter* Filter);
