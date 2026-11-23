// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail UITable (RTTI .?AUUITable@@ : Win3D, vftable 0x005e5744) -- a PAGE_*.txt grid: row and
// column edges the mouse can drag, one optional BaseRichTextWin per cell laid out on the grid,
// a selectable / hoverable row highlight and a line frame.
//
// WINFACTORY<UITable> DAT_0067da00, Create FUN_005992c0 (operator new(0x13c)). vftable
// 0x005e5744 overrides Win3D's slots 0 (FUN_00597eb0), 4, 5, 8, 9, 11, 13 and 19.
//
// Retail layout is 0x13c; the port keeps the members (offsets below), not the VC6 vector
// layout. Cells are Rows * Columns, index Columns * Row + Col; row edges are y, 0 at the top
// going negative; column edges x, 0 at the left. +0x0c8 / +0x0cc are never written.

#include "Interface/ui_fast_table.h"
#include "Interface/win3d.h"

#include <windows.h>

#include <vector>

class BaseRichTextWin;

// One +0x0b4 element (8 bytes). Retail quirk kept: the copy constructor (FUN_005999d0) copies
// byte 1 and the handle but NOT byte 0, so an element built by copy (every element
// vector::resize adds, and every element moved by a reallocation) keeps whatever byte 0 the
// allocator returned. The copy assignment copies both bytes and the handle.
struct FUiTableRow
{
    // Create's prototype row: {1, 1, null}.
    FUiTableRow(unsigned char InHoverable, unsigned char InSelectable)
        : bHoverable(InHoverable)
        , bSelectable(InSelectable)
    {
    }

    FUiTableRow(FUiTableRow const& Other)
        : bSelectable(Other.bSelectable)
        , Payload(Other.Payload)
    {
        }

    FUiTableRow& operator=(FUiTableRow const& Other)
    {
        bHoverable = Other.bHoverable;
        bSelectable = Other.bSelectable;
        Payload = Other.Payload;
        return *this;
    }

    // FUN_005987a0 -- the handle's release.
    ~FUiTableRow()
    {
    }

    unsigned char bHoverable;  // +0x00 -- Update only hovers rows with this set
    unsigned char bSelectable; // +0x01 -- MouseEvent selects only rows with this set
    UiFastTableRefPtr Payload; // +0x04
};

class UiTable : public Win3D
{
public:
    // FUN_00597cd0
    UiTable();
    // FUN_00597ed0 (scalar deleting FUN_00597eb0)
    ~UiTable() override;

    // slot 4 LAB_00599160
    bool HitTest(float const* Ray) override;
    // slot 5 LAB_00598850
    void SetPosition(float const* NewPosition) override;
    // slot 8 LAB_005987e0
    void SetTransform(float const* Transform) override;
    // slot 9 LAB_00598490 (RET 8)
    bool Create(Win3D* NewParent, void* Data) override;
    // slot 11 LAB_005988c0
    void Update() override;
    // slot 13 FUN_00598c40
    void Draw() override;
    // slot 19 LAB_00599090 (RET 4)
    bool MouseEvent(FWin3DMouseEvent const* Event) override;

    // FUN_00597ff0 (RET 8) -- move column edge Index (1..count-1) to X, clamped between its
    // neighbours; false for edge 0 or an index out of range.
    bool SetColumnEdge(unsigned Index, float X);
    // FUN_00598100 (RET 8) -- the same for row edge Index and Y.
    bool SetRowEdge(unsigned Index, float Y);
    // FUN_00598210 (RET 0x10) -- place and size the cells of rows [RowStart, RowStart + RowCount)
    // and columns [ColStart, ColStart + ColCount); a zero count means "to the end".
    void Layout(unsigned RowStart, unsigned RowCount, unsigned ColStart, unsigned ColCount);

    float Width;                          // +0x07c -- payload [0]
    float Height;                         // +0x080 -- payload [1]
    std::vector<BaseRichTextWin*> Cells;  // +0x084
    std::vector<float> RowEdges;          // +0x094
    std::vector<float> ColumnEdges;       // +0x0a4
    std::vector<FUiTableRow> Rows;        // +0x0b4
    unsigned char FieldC4;                // +0x0c4 -- ctor 1, not read by UITable
    unsigned char bSelected;              // +0x0d0
    unsigned SelectedRow;                 // +0x0d4
    unsigned char bHovered;               // +0x0d8
    unsigned HoveredRow;                  // +0x0dc
    unsigned char FrameRgba[4];           // +0x0e0
    unsigned char SelectedRgba[4];        // +0x0e4
    unsigned char HoveredRgba[4];         // +0x0e8
    float GrabDistance;                   // +0x0ec -- 0.004f
    unsigned char bColumnsResizable;      // +0x0f0 -- ctor 1, nothing clears it
    unsigned char bRowsResizable;         // +0x0f1 -- ctor 1, nothing clears it
    float InverseTransform[12];           // +0x0f4 -- inverse of the +0x3c transform
    HCURSOR RowResizeCursor;              // +0x124 -- IDC_SIZENS
    HCURSOR ColumnResizeCursor;           // +0x128 -- IDC_SIZEWE
    HCURSOR DefaultCursor;                // +0x12c -- the cursor at construction
    unsigned char bDragArmed;             // +0x130
    unsigned char bDragging;              // +0x131
    unsigned char bDragColumn;            // +0x132
    unsigned DragEdge;                    // +0x134
    float DragOffset;                     // +0x138

private:
    UiTable(UiTable const&) = delete;
    UiTable& operator=(UiTable const&) = delete;
};
