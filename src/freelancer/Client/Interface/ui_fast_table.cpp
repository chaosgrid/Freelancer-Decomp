// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/ui_fast_table.h"

#include "Common/Fonts/font_string.h"
#include "Common/Fonts/ids_rdl.h"
#include "Common/input_manager.h"
#include "NeuroNet/nn_ui_pulse.h"
#include "Core/reg_key_stack.h"
#include "Space/star_sphere.h"
#include "Interface/ui_scroll.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win_factory.h"

#include "DALib.h"
#include "common_csys.h"
#include "dacom.h"
#include "render/IRenderPipeline.h"
#include "x86math.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

namespace
{
    // DAT_0067d95c -- this TU's lazily acquired 3DMathEngine (FUN_005b73e0).
    I3DMathEngine* G_UiFastTableMath = nullptr;

    I3DMathEngine* UiFastTableMath()
    {
        if (G_UiFastTableMath == nullptr)
            G_UiFastTableMath = Acquire3DMathEngine();
        return G_UiFastTableMath;
    }

    char const kUiFastTableSource[] = "E:\\FL\\Scratch\\Source\\Client\\Interface\\UIFastTable.cpp";

    // .data DAT_00615550 / DAT_00615554 / DAT_00615558, read only by the constructor.
    unsigned long const kUiFastTableFrameRgba = 0xff00ff00ul;
    unsigned long const kUiFastTableSelectedRgba = 0xa00000a0ul;
    unsigned long const kUiFastTableHoveredRgba = 0x80808080ul;

    // The seven render states both halves of Draw set.
    void UiFastTableSetDrawStates()
    {
        DALib::Pipeline->SetRenderState(D3DRS_ZENABLE, 0);
        DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);
        DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
        DALib::Pipeline->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        DALib::Pipeline->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        DALib::Pipeline->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
    }

    // The Font::String copy FUN_00590b40 inlines.
    void UiFastTableCopyFontString(FFontString* Self, FFontString const* Other)
    {
        Self->Text = nullptr;
        Self->Capacity = 0;
        if (Other->Text != nullptr)
        {
            unsigned const Length = static_cast<unsigned>(wcslen(Other->Text));
            // FUN_004142a0(Length + 1): the fresh buffer is empty, so malloc, first char 0.
            unsigned const Need = Length + 1;
            if (Need > Self->Capacity)
            {
                if (Self->Text != nullptr)
                {
                    Self->Text = static_cast<wchar_t*>(realloc(Self->Text, Need * sizeof(wchar_t)));
                }
                else
                {
                    Self->Text = static_cast<wchar_t*>(malloc(Need * sizeof(wchar_t)));
                    Self->Text[0] = L'\0';
                }
                Self->Capacity = Need;
            }
            wcsncpy(Self->Text, Other->Text, Length);
            Self->Text[Length] = L'\0';
        }
        Self->MaxLines = Other->MaxLines;
        // +0x0c is the child vector's allocator byte; +0x0d..+0x0f are not written by retail.
        Self->ChildAlloc = Other->ChildAlloc;
        Self->Pad0D[0] = 0;
        Self->Pad0D[1] = 0;
        Self->Pad0D[2] = 0;

        // A zero count still allocates. Each slot copies the pointer and the kind byte and
        // AddRefs a set pointer.
        int Count = 0;
        if (Other->ChildBegin != nullptr)
            Count = static_cast<int>(Other->ChildEnd - Other->ChildBegin);
        if (Count < 0)
            Count = 0;
        FFontStringChildSlot* const Buffer = static_cast<FFontStringChildSlot*>(
            operator new[](static_cast<unsigned>(Count) * sizeof(FFontStringChildSlot)));
        for (int Index = 0; Index < Count; ++Index)
        {
            FFontStringChildSlot const& From = Other->ChildBegin[Index];
            FFontStringChildSlot& To = Buffer[Index];
            To.Child = From.Child;
            To.Kind = From.Kind;
            if (To.Child != nullptr)
                To.Child->AddRef();
        }
        Self->ChildBegin = Buffer;
        Self->ChildEnd = Buffer + Count;
        Self->ChildCap = Buffer + Count;

        Self->FontIface = Other->FontIface;
        Self->ExtentW = Other->ExtentW;
        Self->ExtentH = Other->ExtentH;
        Self->LayoutGen = Other->LayoutGen;
        Self->ScaleKey = Other->ScaleKey;
        Self->bMultiLine = Other->bMultiLine;
        Self->Pad31[0] = 0;
        Self->Pad31[1] = 0;
        Self->Pad31[2] = 0;
    }
}

// DAT_0067d924.
WINFACTORY<UiFastTable> G_UIFastTableFactory("UIFastTable");

// DAT_0067d8f8.
WINFACTORY<UiScrollTable> G_UIScrollTableFactory("UIScrollTable");

// ---------------------------------------------------------------------------------------------
// UIFastTable::RefCounter and its handle
// ---------------------------------------------------------------------------------------------

UiFastTableRefCounter::~UiFastTableRefCounter()
{
    // FUN_00467ee0.
}

unsigned UiFastTableRefCounter::Release()
{
    // FUN_00599980
    if (RefCount > 0)
        --RefCount;
    if (RefCount != 0)
        return RefCount;
    RefCount = 1;
    delete this;
    return 0;
}

UiFastTableRefPtr::UiFastTableRefPtr(UiFastTableRefPtr const& Other)
    : Object(nullptr)
{
    // FUN_005999d0.
    Object = Other.Object;
    if (Object != nullptr)
        Object->AddRef();
}

UiFastTableRefPtr::~UiFastTableRefPtr()
{
    Reset();
}

UiFastTableRefPtr& UiFastTableRefPtr::operator=(UiFastTableRefPtr const& Other)
{
    // FUN_00590400
    if (Object != nullptr)
    {
        Object->Release();
        Object = nullptr;
    }
    Object = Other.Object;
    if (Object != nullptr)
        Object->AddRef();
    return *this;
}

UiFastTableRefCounter* UiFastTableRefPtr::Assign(UiFastTableRefCounter* Raw)
{
    // FUN_00575520 (Release inlined).
    if (Object != Raw)
    {
        if (Object != nullptr)
        {
            Object->Release();
            Object = nullptr;
        }
        Object = Raw;
        if (Raw != nullptr)
            Raw->AddRef();
    }
    return Object;
}

void UiFastTableRefPtr::Reset()
{
    // FUN_00599a60
    if (Object == nullptr)
        return;
    Object->Release();
    Object = nullptr;
}

// ---------------------------------------------------------------------------------------------
// Tool-tip records
// ---------------------------------------------------------------------------------------------

FUiFastTableTip::FUiFastTableTip()
{
    // The caption and the Font::String (FUN_004144b0): FToolTipHoverInfo's own ctor.
    ToolTipHoverReset(&Info); // FUN_004963a0
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Info.WorldPos[Axis] = 0.0f;
        Info.HalfExtents[Axis] = 0.0f;
    }
}

FUiFastTableTip::FUiFastTableTip(FUiFastTableTip const& Other)
{
    // FUN_00590b40. Info starts empty (FToolTipHoverInfo's ctor: caption null, the
    // Font::String's fields reset); the copy below overwrites every field retail writes.
    Info.Ids = Other.Info.Ids;
    Info.IdsAlt = Other.Info.IdsAlt;
    if (Other.Info.CaptionW != nullptr)
    {
        unsigned const Length = static_cast<unsigned>(wcslen(Other.Info.CaptionW));
        unsigned const Need = Length + 1;
        if (Need > Info.CaptionCapacity)
        {
            if (Info.CaptionW != nullptr)
            {
                Info.CaptionW =
                    static_cast<wchar_t*>(realloc(Info.CaptionW, Need * sizeof(wchar_t)));
            }
            else
            {
                Info.CaptionW = static_cast<wchar_t*>(malloc(Need * sizeof(wchar_t)));
                Info.CaptionW[0] = L'\0';
            }
            Info.CaptionCapacity = Need;
        }
        wcsncpy(Info.CaptionW, Other.Info.CaptionW, Length);
        Info.CaptionW[Length] = L'\0';
    }
    UiFastTableCopyFontString(&Info.Text, &Other.Info.Text);
    Info.AuxField = Other.Info.AuxField;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Info.WorldPos[Axis] = Other.Info.WorldPos[Axis];
        Info.HalfExtents[Axis] = Other.Info.HalfExtents[Axis];
    }
    Info.bLive = Other.Info.bLive;
}

FUiFastTableTip::~FUiFastTableTip()
{
    // FUN_00444ee0: the Font::String at +0x10 (FUN_00444e50 inlined), then the caption --
    // ~FToolTipHoverInfo on the Info member.
}

// ---------------------------------------------------------------------------------------------
// UIFastTable
// ---------------------------------------------------------------------------------------------

UiFastTable::UiFastTable()
    : Width(0.0f)
    , Height(0.0f)
    , bSelected(0)
    , SelectedRow(0)
    , HoverColumn(0xffffffffu)
    , HoverRow(0xffffffffu)
    , bHovered(0)
    , HoveredRow(0)
    , FrameRgba(kUiFastTableFrameRgba)
    , SelectedRgba(kUiFastTableSelectedRgba)
    , HoveredRgba(kUiFastTableHoveredRgba)
    , GrabDistance(0.004f)
    , bColumnsResizable(1)
    , bRowsResizable(1)
    , bDragArmed(0)
    , bDragging(0)
    , bDragColumn(0)
    , DragEdge(0)
    , DragOffset(0.0f)
    , bLayoutFlag(0)
    , bDrawRowLines(1)
    , bDrawColumnLines(1)
    , bDrawBorder(1)
    , bBorderTop(1)
    , bBorderBottom(1)
    , bBorderLeft(1)
    , bBorderRight(1)
    , bRowHighlights(1)
    , HeaderRows(0)
    , TipMode(kTableTipSingle)
    , CellMarginX(2)
    , CellMarginY(2)
{
    // FUN_0058c6e0 after BaseRichTextWin's ctor (FUN_005793e0): empty vectors, +0x504 identity
    // with zero translation, the stores above (+0x548 is written as the integer 0), then an
    // inlined erase of the whole (empty) tip vector -- a no-op -- and +0x570 = +0x574 = 2.
    float* const InverseFloats = &InverseTransform.Rotation.E[0][0];
    for (int Index = 0; Index < 12; ++Index)
        InverseFloats[Index] = 0.0f;
    InverseTransform.Rotation.E[0][0] = 1.0f;
    InverseTransform.Rotation.E[1][1] = 1.0f;
    InverseTransform.Rotation.E[2][2] = 1.0f;
}

UiFastTable::~UiFastTable()
{
    // FUN_0058ca00: every tip (FUN_00444ee0) and the tip storage, every row handle
    // (FUN_00599a60) and the row storage, the column then the row edges, every cell
    // (FUN_0058e360) and the cell storage -- the members' own destructors in reverse declaration
    // order -- then BaseRichTextWin's (FUN_00579620).
}

Vector UiFastTable::RayToLocal(Vector const* Ray) const
{
    // Position.z / Ray.z stays on the x87 stack; the three products are stored as floats.
    double const Scale = static_cast<double>(Position.z) / Ray->z;
    Vector Scaled;
    Scaled.x = static_cast<float>(Ray->x * Scale);
    Scaled.y = static_cast<float>(Ray->y * Scale);
    Scaled.z = static_cast<float>(Ray->z * Scale);
    Vector Local;
    UiFastTableMath()->TransformXform(Local, InverseTransform, Scaled);
    return Local;
}

void UiFastTable::SetDrawColumnLines(bool bDraw)
{
    // FUN_0058cb70
    bDrawColumnLines = bDraw ? 1 : 0;
}

void UiFastTable::SetBorders(bool bLeft, bool bRight, bool bTop, bool bBottom)
{
    // FUN_0058cb80
    bBorderTop = bTop ? 1 : 0;
    bBorderBottom = bBottom ? 1 : 0;
    bBorderLeft = bLeft ? 1 : 0;
    bBorderRight = bRight ? 1 : 0;
}

void UiFastTable::SetHeaderRows(unsigned Rows)
{
    // FUN_0058cbb0
    if (!RowEdges.empty() && Rows < RowEdges.size())
        HeaderRows = Rows;
}

void UiFastTable::SetRowHighlights(bool bQuads)
{
    // FUN_0058cbe0
    bRowHighlights = bQuads ? 1 : 0;
}

bool UiFastTable::ClearCell(unsigned Row, unsigned Column)
{
    // FUN_0058cbf0
    if (RowEdges.empty() || Row >= RowEdges.size())
        return false;
    if (ColEdges.empty() || Column >= ColEdges.size())
        return false;
    unsigned const Index = static_cast<unsigned>(ColEdges.size()) * Row + Column;
    if (Cells.empty() || Index >= Cells.size())
        return false;
    RdlClear(Cells[Index].Source);
    MarkDirty(true, true);
    return true;
}

bool UiFastTable::GetCell(unsigned Row, unsigned Column, FUiFastTableCell** OutCell)
{
    // FUN_0058cd00
    if (RowEdges.empty() || Row >= RowEdges.size())
        return false;
    if (ColEdges.empty() || Column >= ColEdges.size())
        return false;
    unsigned const Index = static_cast<unsigned>(ColEdges.size()) * Row + Column;
    if (Cells.empty() || Index >= Cells.size())
        return false;
    *OutCell = &Cells[Index];
    return true;
}

void UiFastTable::InvalidateCell(unsigned Row, unsigned Column)
{
    // FUN_0058cdb0 -- no test against the cell count; tail jump into FUN_0057a620(1, 1).
    if (RowEdges.empty() || Row >= RowEdges.size())
        return;
    if (ColEdges.empty() || Column >= ColEdges.size())
        return;
    MarkDirty(true, true);
}

void UiFastTable::SetCellSpanToEnd(unsigned Row, unsigned Column, bool bSpan)
{
    // FUN_0058ce10
    if (RowEdges.empty() || Row >= RowEdges.size())
        return;
    if (ColEdges.empty() || Column >= ColEdges.size())
        return;
    unsigned const Index = static_cast<unsigned>(ColEdges.size()) * Row + Column;
    if (Cells.empty() || Index >= Cells.size())
        return;
    Cells[Index].bSpanToEnd = bSpan ? 1 : 0;
}

bool UiFastTable::SetColumnEdge(unsigned Index, float X)
{
    // FUN_0058ceb0. Both limits are rounded to float before the tests.
    if (Index == 0 || ColEdges.empty() || Index >= ColEdges.size())
        return false;
    double const Gap2 = static_cast<double>(GrabDistance) + GrabDistance;
    unsigned const Prev = Index - 1;
    float const Lo = static_cast<float>(Gap2 + ColEdges[Prev]);
    float const Next = Index + 1 == ColEdges.size() ? Width : ColEdges[Index + 1];
    float const Hi = static_cast<float>(Next - Gap2);
    if (X < Lo)
        X = Lo;
    else if (X > Hi)
        X = Hi;
    // An unordered X is stored.
    if (X != ColEdges[Index])
    {
        ColEdges[Index] = X;
        if (!RowEdges.empty() && RowEdges.size() > 0 && !ColEdges.empty()
            && Prev < ColEdges.size())
        {
            MarkDirty(true, true);
        }
    }
    return true;
}

bool UiFastTable::SetRowEdge(unsigned Index, float Y)
{
    // FUN_0058cfb0
    if (Index == 0 || RowEdges.empty() || Index >= RowEdges.size())
        return false;
    double const Gap2 = static_cast<double>(GrabDistance) + GrabDistance;
    unsigned const Prev = Index - 1;
    float const Lo = static_cast<float>(RowEdges[Prev] - Gap2);
    // The lower limit stays double. Retail bug kept: for the last edge retail adds +Height, not
    // -Height (the same as UITable's FUN_00598100), so the limit is above every edge, the first
    // test always clamps to it and the second clamps to Lo.
    double const Hi = static_cast<double>(Index + 1 == RowEdges.size() ? Height
                                                                        : RowEdges[Index + 1])
                      + Gap2;
    if (Y < Hi)
        Y = static_cast<float>(Hi);
    else if (Y > Lo)
        Y = Lo;
    if (RowEdges[Index] != Y)
    {
        RowEdges[Index] = Y;
        if (!RowEdges.empty() && Prev < RowEdges.size() && !ColEdges.empty()
            && ColEdges.size() > 0)
        {
            MarkDirty(true, true);
        }
    }
    return true;
}

// FUN_0058c370. Each vector is copied into an _alloca buffer first.
bool FUiFastTableEdges::SaveToRegistry(FRegKeyStack* Stack) const
{
    size_t const ColBytes = Cols.size() * sizeof(float);
    size_t const RowBytes = Rows.size() * sizeof(float);
    if (ColBytes != 0)
    {
        std::vector<float> Buffer(Cols);
        RegSetValueExA(Stack->Top(), "Columns", 0, REG_BINARY,
                       reinterpret_cast<BYTE const*>(Buffer.data()),
                       static_cast<DWORD>(ColBytes));
    }
    if (RowBytes != 0)
    {
        std::vector<float> Buffer(Rows);
        RegSetValueExA(Stack->Top(), "Rows", 0, REG_BINARY,
                       reinterpret_cast<BYTE const*>(Buffer.data()),
                       static_cast<DWORD>(RowBytes));
    }
    return true;
}

namespace
{
    // One half of FUN_0058c470 ("Columns", then "Rows"): size query, _alloca((size + 3) & ~3),
    // the read, and -- only when both queries agree -- the vector emptied, resized to size / 4
    // (unsigned) and filled. Retail: a failed query or a type other than REG_BINARY counts as
    // size -1 on both reads, so a missing value compares equal and resizes to 0x3fffffff floats
    // (0058c514 `cmp eax,esi; jnz`, no other test); that allocation fails.
    void LoadEdgeVector(FRegKeyStack* Stack, char const* Name, std::vector<float>& Edges)
    {
        DWORD Type = 0;
        DWORD Size = 0;
        DWORD Expected = static_cast<DWORD>(-1);
        if (RegQueryValueExA(Stack->Top(), Name, nullptr, &Type, nullptr, &Size) == ERROR_SUCCESS &&
            Type == REG_BINARY)
            Expected = Size;

        std::vector<unsigned char> Buffer((Expected + 3) & ~3u);
        Size = Expected;
        DWORD Actual = static_cast<DWORD>(-1);
        if (RegQueryValueExA(Stack->Top(), Name, nullptr, &Type, Buffer.data(), &Size) ==
                ERROR_SUCCESS &&
            Type == REG_BINARY)
            Actual = Size;
        if (Actual != Expected)
            return;

        unsigned const Count = Expected >> 2;
        Edges.clear();
        Edges.resize(Count, 0.0f);
        float const* const Values = reinterpret_cast<float const*>(Buffer.data());
        for (int Index = 0; Index < static_cast<int>(Count); ++Index)
            Edges[Index] = Values[Index];
    }
}

// FUN_0058c470.
bool FUiFastTableEdges::LoadFromRegistry(FRegKeyStack* Stack)
{
    LoadEdgeVector(Stack, "Columns", Cols);
    LoadEdgeVector(Stack, "Rows", Rows);
    return true;
}

FUiFastTableRow* UiFastTable::GetRow(unsigned Row)
{
    // FUN_0058d0b0
    if (RowEdges.empty() || Row >= RowEdges.size())
        return nullptr;
    return &Rows[Row];
}

void UiFastTable::GetEdges(FUiFastTableEdges* Out)
{
    // FUN_0058d0f0: the column vector's assignment inline (skipped for self-assignment), then
    // FUN_005595c0 for the rows.
    Out->Cols = ColEdges;
    Out->Rows = RowEdges;
}

void UiFastTable::SetEdges(FUiFastTableEdges const* In)
{
    // FUN_0058d250
    if (In->Cols.size() != ColEdges.size() || In->Rows.size() != RowEdges.size())
    {
        FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s", kUiFastTableSource, 0x1c9,
              "Table state does not match table.\n");
        return;
    }
    // Retail compares the first column edges twice, the same way (unordered counts as unequal),
    // and reads both without an emptiness test.
    float const* const InCols = In->Cols.data();
    float const* const OwnCols = ColEdges.data();
    if (InCols[0] != OwnCols[0] || InCols[0] != OwnCols[0])
    {
        FDUMP(kFdumpNotice, "%s(%d) : NOTICE:General:%s", kUiFastTableSource, 0x1d0,
              "Table state does not match table.\n");
        return;
    }
    ColEdges = In->Cols;
    RowEdges = In->Rows;
    if (!RowEdges.empty() && RowEdges.size() > 0 && !ColEdges.empty() && ColEdges.size() > 0)
        MarkDirty(true, true);
}

void UiFastTable::ClearSelection()
{
    // FUN_0058d4a0
    bSelected = 0;
}

bool UiFastTable::SelectRow(unsigned Row)
{
    // FUN_0058d4b0
    if (RowEdges.empty() || Row >= RowEdges.size())
        return false;
    FUiFastTableRow const& State = Rows[Row];
    if (State.bSelectable == 0 || State.bHoverable == 0)
        return false;
    bool const bChanged = !(bSelected != 0 && SelectedRow == Row);
    SelectedRow = Row;
    bSelected = 1;
    if (Parent != nullptr && bChanged)
        Parent->ProcessMessage(kWinMsgSelectionChanged, static_cast<unsigned>(reinterpret_cast<uintptr_t>(this)), Row);
    return true;
}

bool UiFastTable::GetSelectedRow(unsigned* OutRow)
{
    // FUN_0058d540
    if (bSelected == 0)
        return false;
    *OutRow = SelectedRow;
    return true;
}

void UiFastTable::SetFrameColor(unsigned long const* Rgba)
{
    // FUN_0058d560
    FrameRgba = *Rgba;
}

void UiFastTable::SetGrabDistance(float Distance)
{
    // FUN_0058d570: the store is skipped while the CURRENT distance is below zero (or unordered).
    if (GrabDistance >= 0.0f)
        GrabDistance = Distance;
}

void UiFastTable::SetResizable(bool bRows, bool bColumns)
{
    // FUN_0058d590
    bRowsResizable = bRows ? 1 : 0;
    bColumnsResizable = bColumns ? 1 : 0;
}

void UiFastTable::SetCellMargins(int X, int Y)
{
    // FUN_0058d5b0
    CellMarginX = X;
    CellMarginY = Y;
}

void UiFastTable::SetLayoutFlag(bool bFlag)
{
    // FUN_0058d5d0
    bLayoutFlag = bFlag ? 1 : 0;
}

void UiFastTable::SetTipMode(int Mode)
{
    // FUN_0058d5e0
    if (Mode == TipMode)
        return;
    TipMode = Mode;
    Tips.clear();

    unsigned Count = 0;
    bool bResize = true;
    switch (TipMode)
    {
    case kTableTipColumn:
        Count = static_cast<unsigned>(ColEdges.size());
        break;
    case kTableTipRow:
        Count = static_cast<unsigned>(RowEdges.size());
        break;
    case kTableTipCell:
        Count = static_cast<unsigned>(Cells.size());
        break;
    default:
        bResize = false;
        break;
    }
    if (bResize)
    {
        // The vector is empty here, so resize only inserts.
        FUiFastTableTip const Prototype;
        Tips.resize(Count, Prototype);
    }

    // Every record reset, keyed this + 1 + index.
    int Key = static_cast<int>(reinterpret_cast<intptr_t>(this)) + 1;
    for (std::vector<FUiFastTableTip>::iterator It = Tips.begin(); It != Tips.end(); ++It)
    {
        ToolTipHoverReset(&It->Info);
        It->Info.Ids = Key;
        It->Info.IdsAlt = 0;
        ++Key;
    }
}

FToolTipHoverInfo* UiFastTable::GetToolTipHoverInfo()
{
    // LAB_0058d8f0
    if (TipMode == kTableTipSingle)
        return &HoverInfo;
    return nullptr;
}

FToolTipHoverInfo* UiFastTable::GetRowTip(unsigned Row)
{
    // FUN_0058d910
    if (TipMode != kTableTipRow || Tips.empty() || Row >= Tips.size())
        return nullptr;
    return &Tips[Row].Info;
}

FToolTipHoverInfo* UiFastTable::GetCellTip(unsigned Row, unsigned Column)
{
    // FUN_0058d960
    if (TipMode != kTableTipCell)
        return nullptr;
    unsigned const Index = static_cast<unsigned>(ColEdges.size()) * Row + Column;
    if (Tips.empty() || Index >= Tips.size())
        return nullptr;
    return &Tips[Index].Info;
}

bool UiFastTable::Relayout()
{
    // FUN_0058d9d0. Called by RebuildLayoutIfDirty right after it emptied LayoutList.
    int const ColCount = static_cast<int>(ColEdges.size());
    int const RowCount = static_cast<int>(RowEdges.size());
    // Two _alloca arrays in retail: rows + 1 then columns + 1 ints.
    std::vector<int> PixRows(static_cast<unsigned>(RowCount + 1));
    std::vector<int> PixCols(static_cast<unsigned>(ColCount + 1));

    float M[16];
    DALib::Pipeline->GetProjection(M);
    int ViewX = 0;
    int ViewY = 0;
    int ViewW = 0;
    int ViewH = 0;
    DALib::Pipeline->GetViewportRect(&ViewX, &ViewY, &ViewW, &ViewH);

    Transform const& Xform = Placement;

    // Column edges: (edge, 0, 0) into world space, then the projection's x and w rows.
    for (int Index = 0; Index < ColCount; ++Index)
    {
        Vector Local;
        Local.x = ColEdges[static_cast<unsigned>(Index)];
        Local.y = 0.0f;
        Local.z = 0.0f;
        Vector World;
        UiFastTableMath()->TransformXform(World, Xform, Local);
        // w = (M12 x + M14 z) + M13 y + M15, rounded to float.
        float const W = static_cast<float>(
            (static_cast<double>(M[12]) * World.x + static_cast<double>(M[14]) * World.z)
            + static_cast<double>(M[13]) * World.y + M[15]);
        // x = (M2 z + M1 y) + M0 x + M3, kept double.
        double const ClipX =
            (static_cast<double>(M[2]) * World.z + static_cast<double>(M[1]) * World.y)
            + static_cast<double>(M[0]) * World.x + M[3];
        if (W == 0.0f)
        {
            PixCols[static_cast<unsigned>(Index)] =
                Index > 0 ? PixCols[static_cast<unsigned>(Index - 1)] : 0;
            continue;
        }
        // Minus the first column's pixel for every later column; truncated.
        double const Ndc = ClipX * (1.0 / W);
        double Pixel = (1.0 - Ndc) * ViewW * 0.5 + ViewX;
        if (Index > 0)
            Pixel -= PixCols[0];
        PixCols[static_cast<unsigned>(Index)] = static_cast<int>(Pixel);
    }
    PixCols[static_cast<unsigned>(ColCount)] = PixelWidth;
    PixCols[0] = 0;

    // Row edges: (0, edge, 0), the projection's y and w rows.
    for (int Index = 0; Index < RowCount; ++Index)
    {
        Vector Local;
        Local.x = 0.0f;
        Local.y = RowEdges[static_cast<unsigned>(Index)];
        Local.z = 0.0f;
        Vector World;
        UiFastTableMath()->TransformXform(World, Xform, Local);
        // w = (M12 x + M13 y) + M14 z + M15, rounded to float (a different order from x's).
        float const W = static_cast<float>(
            (static_cast<double>(M[12]) * World.x + static_cast<double>(M[13]) * World.y)
            + static_cast<double>(M[14]) * World.z + M[15]);
        // y = (M4 x + M5 y) + M6 z + M7.
        double const ClipY =
            (static_cast<double>(M[4]) * World.x + static_cast<double>(M[5]) * World.y)
            + static_cast<double>(M[6]) * World.z + M[7];
        if (W == 0.0f)
        {
            PixRows[static_cast<unsigned>(Index)] =
                Index > 0 ? PixRows[static_cast<unsigned>(Index - 1)] : 0;
            continue;
        }
        // Minus the first row's pixel for every later row; truncated.
        double const Ndc = ClipY * (1.0 / W);
        double Pixel = (Ndc + 1.0) * ViewH * 0.5 + ViewY;
        if (Index > 0)
            Pixel -= PixRows[0];
        PixRows[static_cast<unsigned>(Index)] = static_cast<int>(Pixel);
    }
    PixRows[static_cast<unsigned>(RowCount)] = PixelHeight;
    PixRows[0] = 0;

    // Every cell: its laid list emptied, a ClipNode for the cell rectangle inside the margins,
    // the source laid out after it, and a reference to the laid list appended to LayoutList.
    // The cell is addressed straight from the vector's storage, without a range test.
    FUiFastTableCell* const CellData = Cells.data();
    for (int Row = 0; Row < RowCount; ++Row)
    {
        for (int Column = 0; Column < ColCount; ++Column)
        {
            FUiFastTableCell& Cell = CellData[Row * ColCount + Column];
            RdlClear(Cell.Laid);

            tagRECT Rect;
            Rect.top = PixRows[static_cast<unsigned>(Row)] + CellMarginY;
            Rect.bottom = PixRows[static_cast<unsigned>(Row + 1)] - CellMarginY;
            Rect.left = PixCols[static_cast<unsigned>(Column)] + CellMarginX;
            if (Cell.bSpanToEnd != 0)
                Rect.right = PixCols[static_cast<unsigned>(ColCount)] - CellMarginX;
            else
                Rect.right = PixCols[static_cast<unsigned>(Column + 1)] - CellMarginX;

            {
                ClipNode Clip(Rect);
                RdlAppendNode(Cell.Laid, Clip.Clone());
            }
            LayoutRichText(Trc, Cell.Source, Rect.right - Rect.left, Cell.Laid, Rect.left,
                           Rect.top, bLayoutFlag != 0, nullptr, true);
            {
                RDLRefNode Ref(Cell.Laid);
                RdlAppendNode(LayoutList, Ref.Clone());
            }
        }
    }
    return true;
}

bool UiFastTable::Create(Win3D* NewParent, void* Data)
{
    // FUN_0058e000 -- BaseRichTextWin's Create with NO payload.
    bool const bResult = BaseRichTextWin::Create(NewParent, nullptr);
    if (Data != nullptr)
    {
        // FUiPayloadTable (ui_page_desc.h): {Width, Height, rows, columns}.
        float const* const Size = static_cast<float const*>(Data);
        unsigned const* const Counts = static_cast<unsigned const*>(Data);
        Width = Size[0];
        Height = Size[1];
        RowEdges.resize(Counts[2], 0.0f);
        ColEdges.resize(Counts[3], 0.0f);
        FUiFastTableCell const CellPrototype;
        Cells.resize(Counts[2] * Counts[3], CellPrototype);
        FUiFastTableRow const RowPrototype;
        Rows.resize(Counts[2], RowPrototype);
    }

    // Evenly spaced edges: rows going down from 0, columns going right from 0.
    if (ColEdges.size() > 0 && !RowEdges.empty() && RowEdges.size() > 0)
    {
        float const RowStep = static_cast<float>(
            Height / static_cast<double>(static_cast<unsigned>(RowEdges.size())));
        float const ColumnStep = static_cast<float>(
            Width / static_cast<double>(static_cast<unsigned>(ColEdges.size())));
        for (unsigned Index = 0; !RowEdges.empty() && Index < RowEdges.size(); ++Index)
            RowEdges[Index] = static_cast<float>(-(static_cast<double>(Index) * RowStep));
        for (unsigned Index = 0; !ColEdges.empty() && Index < ColEdges.size(); ++Index)
            ColEdges[Index] = static_cast<float>(static_cast<double>(Index) * ColumnStep);
    }
    SetNormSize(Width, Height);   // FUN_00579cc0
    return bResult;
}

void UiFastTable::SetTransform(Transform const* NewXform)
{
    // LAB_0058e470: slot 7 and slot 5 (both virtual), +0x504 = GeneralInverse(+0x3c) (the W
    // out-parameter lands in the argument slot), then the layout dirtied when the grid is not
    // empty.
    SetOrientation(&NewXform->Rotation);
    SetPosition(&NewXform->Translation);
    float W = 0.0f;
    Transform Inverse;
    UiFastTableMath()->GeneralInverse(Inverse, W,
                                      Placement);
    InverseTransform = Inverse;
    if (!RowEdges.empty() && RowEdges.size() > 0 && !ColEdges.empty() && ColEdges.size() > 0)
        MarkDirty(true, true);
}

void UiFastTable::SetPosition(Vector const* NewPosition)
{
    // LAB_0058e510: BaseRichTextWin's SetPosition (FUN_0057b0a0), then the same inverse and dirty
    // test as SetTransform.
    BaseRichTextWin::SetPosition(NewPosition);
    float W = 0.0f;
    Transform Inverse;
    UiFastTableMath()->GeneralInverse(Inverse, W,
                                      Placement);
    InverseTransform = Inverse;
    if (!RowEdges.empty() && RowEdges.size() > 0 && !ColEdges.empty() && ColEdges.size() > 0)
        MarkDirty(true, true);
}

void UiFastTable::Update()
{
    // FUN_0058e5a0. The cursor-shape calls sit where retail makes them.
    BaseRichTextWin::Update();

    if (Win3DGetCapture() == this && bDragging != 0)
    {
        // Dragging an edge: follow the mouse with the grab offset kept.
        Vector Ray;
        Win3DGetMouseRay(&Ray);
        Vector const Local = RayToLocal(&Ray);
        if (bDragColumn != 0)
        {
            SetColumnEdge(DragEdge, static_cast<float>(static_cast<double>(Local.x) + DragOffset));
            MouseSetCursor("Stretch", false);
        }
        else
        {
            SetRowEdge(DragEdge, static_cast<float>(static_cast<double>(Local.y) + DragOffset));
            MouseSetCursor("Arrow", false);
        }
        return;
    }
    if (Win3DGetCapture() != nullptr)
        return;

    Vector Ray;
    Win3DGetMouseRay(&Ray);
    Vector const Local = RayToLocal(&Ray);
    HoverColumn = 0xffffffffu;
    HoverRow = 0xffffffffu;
    bHovered = 0;
    bDragArmed = 0;
    bDragging = 0;
    if (!(Local.x >= 0.0f))
        return;
    if (!(Local.x < Width))
        return;
    if (!(Local.y <= 0.0f))
        return;
    float const Bottom = -Height;
    if (!(Local.y > Bottom))
        return;

    // The column under x. The first edge is read without an emptiness test; the count is taken
    // once. Unordered moves on.
    float const* const ColData = ColEdges.data();
    float ColPrev = ColData[0];
    float ColNext = 0.0f;
    unsigned const ColCount = static_cast<unsigned>(ColEdges.size());
    for (unsigned Index = 0; Index < ColCount; ++Index)
    {
        ColNext = Index + 1 < ColCount ? ColData[Index + 1] : Width;
        if (Local.x >= ColPrev && Local.x < ColNext)
        {
            HoverColumn = Index;
            break;
        }
        ColPrev = ColNext;
    }

    // The row under y, the same way (edges go down).
    float const* const RowData = RowEdges.data();
    float RowPrev = RowData[0];
    float RowNext = 0.0f;
    unsigned const RowCount = static_cast<unsigned>(RowEdges.size());
    for (unsigned Index = 0; Index < RowCount; ++Index)
    {
        RowNext = Index + 1 < RowCount ? RowData[Index + 1] : Bottom;
        if (Local.y <= RowPrev && Local.y > RowNext)
        {
            HoverRow = Index;
            break;
        }
        RowPrev = RowNext;
    }

    // The ToolTip record for what is under the mouse.
    if (bToolTipEnabled != 0)
    {
        FToolTipHoverInfo* Info = nullptr;
        unsigned Index = 0;
        bool bFromTips = true;
        switch (TipMode)
        {
        case kTableTipColumn:
            Index = HoverColumn;
            break;
        case kTableTipRow:
            Index = HoverRow;
            break;
        case kTableTipCell:
            Index = HoverRow * static_cast<unsigned>(ColEdges.size()) + HoverColumn;
            break;
        default:
            Info = GetToolTipHoverInfo();
            bFromTips = false;
            break;
        }
        if (bFromTips)
        {
            if (!Tips.empty() && Index < Tips.size())
                Info = &Tips[Index].Info;
            else
                Info = nullptr;
        }
        // Retail: no null test -- an out-of-range index reads address 4 (0058e8fc
        // `cmp [ecx+4],ebx` with ecx = 0).
        if (Info->IdsAlt != 0)
            NnToolTipFeedHover(Info);
    }

    // Column edges: only in rows at or below the header rows.
    if (bColumnsResizable != 0 && HoverRow >= HeaderRows)
    {
        unsigned const Column = HoverColumn;
        if (Column > 0 && static_cast<double>(Local.x) - ColPrev < GrabDistance)
        {
            // The column's left edge.
            MouseSetCursor("Stretch", false);
            DragOffset = static_cast<float>(static_cast<double>(ColPrev) - Local.x);
            bDragArmed = 1;
            DragEdge = Column;
            bDragColumn = 1;
            return;
        }
        if (bDragArmed == 0)
        {
            unsigned const Count = static_cast<unsigned>(ColEdges.size());
            if (Column < Count - 2)
            {
                // The column's right edge; the offset keeps the distance rounded to float.
                double const Distance = static_cast<double>(ColNext) - Local.x;
                if (Distance < GrabDistance)
                {
                    MouseSetCursor("Stretch", false);
                    bDragArmed = 1;
                    DragEdge = Column + 1;
                    DragOffset = static_cast<float>(Distance);
                    bDragColumn = 1;
                    return;
                }
            }
        }
    }

    if (bRowsResizable != 0)
    {
        unsigned const Row = HoverRow;
        if (Row > 0)
        {
            // The row's top edge.
            double const Distance = static_cast<double>(RowPrev) - Local.y;
            if (Distance < GrabDistance)
            {
                MouseSetCursor("Stretch", false);
                bDragColumn = 0;
                bDragArmed = 1;
                DragEdge = Row;
                DragOffset = static_cast<float>(Distance);
                return;
            }
        }
        if (bDragArmed == 0)
        {
            unsigned const Count = static_cast<unsigned>(RowEdges.size());
            if (Row < Count - 2)
            {
                // The row's bottom edge. Retail bug kept: the offset is y - edge, the opposite
                // sign of the three other cases, so a drag of this edge starts twice the grab
                // distance away from it.
                double const Distance = static_cast<double>(Local.y) - RowNext;
                if (Distance < GrabDistance)
                {
                    MouseSetCursor("Stretch", false);
                    bDragColumn = 0;
                    bDragArmed = 1;
                    DragEdge = Row + 1;
                    DragOffset = static_cast<float>(Distance);
                    return;
                }
            }
        }
    }

    unsigned const Row = HoverRow;
    if (!RowEdges.empty() && Row < RowEdges.size() && Rows[Row].bHoverable != 0)
    {
        HoveredRow = Row;
        bHovered = 1;
    }
    MouseSetCursor("Arrow", false);
}

void UiFastTable::DrawRowQuad(unsigned long const* Rgba, unsigned Row) const
{
    Win3DColor4ubv(reinterpret_cast<unsigned char const*>(Rgba));
    Win3DBegin(kWin3DQuads);
    Win3DVertex3f(ColEdges[0], RowEdges[Row], 0.0f);
    Win3DVertex3f(Width, RowEdges[Row], 0.0f);
    if (Row + 1 < RowEdges.size())
    {
        Win3DVertex3f(Width, RowEdges[Row + 1], 0.0f);
        Win3DVertex3f(ColEdges[0], RowEdges[Row + 1], 0.0f);
    }
    else
    {
        Win3DVertex3f(Width, -Height, 0.0f);
        Win3DVertex3f(ColEdges[0], -Height, 0.0f);
    }
    Win3DEnd();
}

void UiFastTable::Draw()
{
    // LAB_0058eb30
    Win3DBindModelview(nullptr, &Placement);   // FUN_00422690(0, this + 0x3c)
    UiFastTableSetDrawStates();
    if (bRowHighlights != 0)
    {
        if (bSelected != 0)
            DrawRowQuad(&SelectedRgba, SelectedRow);
        if (bHovered != 0)
            DrawRowQuad(&HoveredRgba, HoveredRow);
    }

    BaseRichTextWin::Draw();   // FUN_0057ab10

    Win3DBindModelview(nullptr, &Placement);
    UiFastTableSetDrawStates();
    Win3DColor4ubv(reinterpret_cast<unsigned char const*>(&FrameRgba));
    if (bDrawBorder != 0)
    {
        Win3DBegin(kWin3DLines);
        if (bBorderTop != 0)
        {
            Win3DVertex3f(0.0f, 0.0f, 0.0f);
            Win3DVertex3f(Width, 0.0f, 0.0f);
        }
        if (bBorderBottom != 0)
        {
            Win3DVertex3f(0.0f, -Height, 0.0f);
            Win3DVertex3f(Width, -Height, 0.0f);
        }
        if (bBorderLeft != 0)
        {
            Win3DVertex3f(0.0f, 0.0f, 0.0f);
            Win3DVertex3f(0.0f, -Height, 0.0f);
        }
        if (bBorderRight != 0)
        {
            Win3DVertex3f(Width, 0.0f, 0.0f);
            Win3DVertex3f(Width, -Height, 0.0f);
        }
        Win3DEnd();
    }

    Win3DBegin(kWin3DLines);
    if (bDrawRowLines != 0)
    {
        for (unsigned Index = 1; !RowEdges.empty() && Index < RowEdges.size(); ++Index)
        {
            if (Rows[Index].bRowLine == 0)
                continue;
            Win3DVertex3f(0.0f, RowEdges[Index], 0.0f);
            Win3DVertex3f(Width, RowEdges[Index], 0.0f);
        }
    }
    if (bDrawColumnLines != 0)
    {
        // Column lines start below the header rows (no range test).
        float Top = 0.0f;
        if (HeaderRows > 0)
            Top = RowEdges[HeaderRows];
        for (unsigned Index = 1; !ColEdges.empty() && Index < ColEdges.size(); ++Index)
        {
            Win3DVertex3f(ColEdges[Index], Top, 0.0f);
            Win3DVertex3f(ColEdges[Index], -Height, 0.0f);
        }
    }
    Win3DEnd();

    if (bRowHighlights != 0 || bHovered == 0)
        return;

    // Without quads the hovered row is outlined -- together with the neighbouring rows of the
    // same non-zero group -- in the pulsing amber.
    unsigned First = HoveredRow;
    unsigned Last = HoveredRow;
    unsigned const Group = Rows[HoveredRow].GroupId;
    if (Group != 0)
    {
        while (First > 0 && Rows[First - 1].GroupId == Group)
            --First;
        while (!RowEdges.empty() && Last + 1 < RowEdges.size() && Rows[Last + 1].GroupId == Group)
            ++Last;
    }
    unsigned long Color = 0;
    NnUiPulseBlendFromArgb(&Color, G_NnPaletteAmber);
    Win3DColor4ubv(reinterpret_cast<unsigned char const*>(&Color));
    Win3DBegin(kWin3DLineLoop);
    Win3DVertex3f(ColEdges[0], RowEdges[First], 0.0f);
    Win3DVertex3f(Width, RowEdges[First], 0.0f);
    if (Last + 1 < RowEdges.size())
    {
        Win3DVertex3f(Width, RowEdges[Last + 1], 0.0f);
        Win3DVertex3f(ColEdges[0], RowEdges[Last + 1], 0.0f);
    }
    else
    {
        Win3DVertex3f(Width, -Height, 0.0f);
        Win3DVertex3f(ColEdges[0], -Height, 0.0f);
    }
    Win3DEnd();
}

void UiFastTable::MouseLeave(Vector const* /*Ray*/)
{
    // LAB_0058f210 (RET 4).
    MouseSetCursor("Arrow", false);
}

bool UiFastTable::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_0058f230
    if (Event->Button != kWin3DMouseLeft)
        return WinControl::MouseEvent(Event);

    if (Event->Action == kWin3DMouseDoubleClick)
    {
        // Double click on a selectable, hoverable hovered row: select it (0x300d to the parent
        // on a change) and always send 0xf002. Consumed either way.
        if (bHovered != 0)
        {
            unsigned const Row = HoveredRow;
            if (Row < RowEdges.size() && Rows[Row].bSelectable != 0 && Rows[Row].bHoverable != 0)
            {
                bool const bChanged = !(bSelected != 0 && SelectedRow == Row);
                SelectedRow = Row;
                bSelected = 1;
                unsigned const Self = static_cast<unsigned>(reinterpret_cast<uintptr_t>(this));
                if (Parent != nullptr && bChanged)
                    Parent->ProcessMessage(kWinMsgSelectionChanged, Self, Row);
                if (Parent != nullptr)
                    Parent->ProcessMessage(kWinMsgDoubleClick, Self, Row);
            }
        }
        return true;
    }

    if (Event->Action == kWin3DMousePress)
    {
        unsigned const Self = static_cast<unsigned>(reinterpret_cast<uintptr_t>(this));
        if (bHovered != 0)
        {
            unsigned const Row = HoveredRow;
            if (Row < RowEdges.size() && Rows[Row].bSelectable != 0 && Rows[Row].bHoverable != 0)
            {
                bool const bChanged = !(bSelected != 0 && SelectedRow == Row);
                SelectedRow = Row;
                bSelected = 1;
                if (Parent != nullptr && bChanged)
                    Parent->ProcessMessage(kWinMsgSelectionChanged, Self, Row);
                return true;
            }
        }
        else if (bDragArmed != 0)
        {
            Win3DPushCapture(this);
            bDragging = 1;
            return true;
        }
        // The press lands on a cell that is not selected: (row << 16) | column to the parent,
        // which is read without a null test.
        Parent->ProcessMessage(kWinMsgPress, Self, (HoverRow << 16) | HoverColumn);
        return true;
    }

    if (Win3DGetCapture() == this && bDragging != 0)
    {
        bDragging = 0;
        Win3DRemoveCapture(this);
        return true;
    }
    return WinControl::MouseEvent(Event);
}

bool UiFastTable::HitTest(Vector const* Ray)
{
    // LAB_0058f3e0: the given ray on the table plane, in table space, inside
    // [0, Width) x (-Height, 0].
    Vector const Local = RayToLocal(Ray);
    if (!(Local.x >= 0.0f))
        return false;
    if (!(Local.x < Width))
        return false;
    if (!(Local.y <= 0.0f))
        return false;
    if (!(-Height < Local.y))
        return false;
    return true;
}

// ---------------------------------------------------------------------------------------------
// UIScrollTable
// ---------------------------------------------------------------------------------------------

UiScrollTable::UiScrollTable()
    : Callback(nullptr)
    , ItemCount(0)
    , FixedRows(0)
    , CallbackContext(nullptr)
    , ScrollBar(nullptr)
{
}

UiScrollTable::~UiScrollTable()
{
    // FUN_0058f4d0.
}

bool UiScrollTable::Create(Win3D* NewParent, void* Data)
{
    // FUN_0058f7a0
    if (!UiFastTable::Create(NewParent, Data))
        return false;
    ScrollBar = CreateScrollBarV();
    SetItemCount(static_cast<unsigned>(RowEdges.size()), 0);
    ScrollBar->SetValue(0.0f);
    return true;
}

void UiScrollTable::SetItemCount(unsigned NewItemCount, unsigned NewFixedRows)
{
    // FUN_0058f640
    ItemCount = NewItemCount;
    FixedRows = NewFixedRows;
    unsigned const Visible = static_cast<unsigned>(RowEdges.size()) - NewFixedRows;
    if (NewItemCount <= Visible)
    {
        // Everything fits: value 0 and the bar hidden.
        ScrollBar->SetValue(0.0f);
        ScrollBar->Flags =
            static_cast<unsigned char>(ScrollBar->Flags & ~(kWin3DActive | kWin3DVisible));
        MarkDirty(true, true);
        return;
    }
    ScrollBar->Flags = static_cast<unsigned char>(ScrollBar->Flags | kWin3DActive | kWin3DVisible);
    // The range converts as signed, the page as unsigned.
    ScrollBar->SetRange(0.0f, static_cast<float>(static_cast<int>(NewItemCount)));
    ScrollBar->SetPage(static_cast<float>(
        static_cast<unsigned>(RowEdges.size()) - FixedRows));
    ScrollBar->SetStep(1.0f);
    MarkDirty(true, true);
}

int UiScrollTable::GetFirstItem()
{
    // FUN_0058f730
    return static_cast<int>(ScrollBar->GetValue());
}

void UiScrollTable::SetCallback(FUiScrollTableCallback NewCallback, void* Context)
{
    // FUN_0058f740
    Callback = NewCallback;
    CallbackContext = Context;
    if (NewCallback == nullptr)
        return;
    int const First = static_cast<int>(ScrollBar->GetValue());
    NewCallback(CallbackContext, First, static_cast<unsigned>(RowEdges.size()) - FixedRows);
}

void UiScrollTable::SetScrollBarGap(float Gap)
{
    // FUN_0058f810.
    ScrollBarGap = Gap;
    if (ScrollBar == nullptr)
        return;
    Vector Local;
    Local.x = static_cast<float>(static_cast<double>(Gap) + NormWidth);
    Local.y = 0.0f;
    Local.z = 0.0f;
    Vector Offset;
    UiFastTableMath()->TransformMatrix(Offset, Orientation,
                                       Local);
    ScrollBar->SetOffset(Offset.x, Offset.y, Offset.z);
    ScrollBar->SetTransform(&Placement);
}

bool UiScrollTable::ProcessMessage(unsigned Msg, unsigned A, unsigned B)
{
    // LAB_0058f8b0: the own scrollbar's value change goes to the callback; the base class's
    // horizontal bar (+0x3f4) is swallowed; anything else is BaseRichTextWin's.
    if (Msg == kWinMsgScroll)
    {
        if (A == static_cast<unsigned>(reinterpret_cast<uintptr_t>(ScrollBar)))
        {
            if (Callback != nullptr)
            {
                int const First = static_cast<int>(ScrollBar->GetValue());
                Callback(CallbackContext, First,
                         static_cast<unsigned>(RowEdges.size()) - FixedRows);
            }
            return true;
        }
        if (A == static_cast<unsigned>(reinterpret_cast<uintptr_t>(ScrollBarH)))
            return true;
    }
    return BaseRichTextWin::ProcessMessage(Msg, A, B);
}
