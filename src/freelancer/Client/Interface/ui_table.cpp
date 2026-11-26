// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/ui_table.h"

#include "Interface/rich_text_win.h"
#include "Space/star_sphere.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win_factory.h"

#include "DALib.h"
#include "common_csys.h"
#include "x86math.h"

#include <math.h>

namespace
{
    // DAT_0067da38 -- this TU's lazily acquired 3DMathEngine.
    I3DMathEngine* G_UiTableMath = nullptr;

    I3DMathEngine* UiTableMath()
    {
        if (G_UiTableMath == nullptr)
            G_UiTableMath = Acquire3DMathEngine();
        return G_UiTableMath;
    }

    // .data DAT_006156a8 / DAT_006156ac / DAT_006156b0, read only by the constructor and never
    // written. R, G, B, A byte order.
    unsigned char const kUiTableFrameRgba[4] = {0x00, 0xff, 0x00, 0xff};
    unsigned char const kUiTableSelectedRgba[4] = {0xa0, 0x00, 0x00, 0xa0};
    unsigned char const kUiTableHoveredRgba[4] = {0x80, 0x80, 0x80, 0x80};

    // The seven render states both halves of Draw set.
    void UiTableSetDrawStates()
    {
        DALib::Pipeline->SetRenderState(D3DRS_ZENABLE, 0);
        DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);
        DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, 1);
        DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
        DALib::Pipeline->SetRenderState(D3DRS_SRCBLEND, 5);
        DALib::Pipeline->SetRenderState(D3DRS_DESTBLEND, 6);
        DALib::Pipeline->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
    }

    // The mouse ray scaled onto the table's z plane (the quotient stays double), then taken into
    // table space through the +0xf4 inverse.
    Vector UiTableRayToLocal(UiTable const* Table, Vector const* Ray)
    {
        double const Scale = static_cast<double>(Table->Position.z) / Ray->z;
        Vector Scaled;
        Scaled.x = static_cast<float>(Ray->x * Scale);
        Scaled.y = static_cast<float>(Ray->y * Scale);
        Scaled.z = static_cast<float>(Ray->z * Scale);
        Vector Local;
        UiTableMath()->TransformXform(Local, Table->InverseTransform, Scaled);
        return Local;
    }

    // Draw's highlight quad for row Row.
    void UiTableDrawRowQuad(UiTable const* Table, unsigned char const* Rgba, unsigned Row)
    {
        Win3DColor4ubv(Rgba);
        Win3DBegin(kWin3DQuads);
        Win3DVertex3f(Table->ColumnEdges[0], Table->RowEdges[Row], 0.0f);
        Win3DVertex3f(Table->Width, Table->RowEdges[Row], 0.0f);
        if (Row + 1 < Table->RowEdges.size())
        {
            Win3DVertex3f(Table->Width, Table->RowEdges[Row + 1], 0.0f);
            Win3DVertex3f(Table->ColumnEdges[0], Table->RowEdges[Row + 1], 0.0f);
        }
        else
        {
            Win3DVertex3f(Table->Width, -Table->Height, 0.0f);
            Win3DVertex3f(Table->ColumnEdges[0], -Table->Height, 0.0f);
        }
        Win3DEnd();
    }
}

// DAT_0067da00.
WINFACTORY<UiTable> G_UITableFactory("UITable");

UiTable::UiTable()
    : Width(0.0f)
    , Height(0.0f)
    , FieldC4(1)
    , bSelected(0)
    , SelectedRow(0)
    , bHovered(0)
    , HoveredRow(0)
    , GrabDistance(0.004f)
    , bColumnsResizable(1)
    , bRowsResizable(1)
    , bDragArmed(0)
    , bDragging(0)
    , bDragColumn(0)
    , DragEdge(0)
    , DragOffset(0.0f)
{
    // FUN_00597cd0.
    Win3DIdentityRotation3x3(&InverseTransform.Rotation);
    InverseTransform.Translation.x = 0.0f;
    InverseTransform.Translation.y = 0.0f;
    InverseTransform.Translation.z = 0.0f;
    for (int I = 0; I < 4; ++I)
    {
        FrameRgba[I] = kUiTableFrameRgba[I];
        HoveredRgba[I] = kUiTableHoveredRgba[I];
        SelectedRgba[I] = kUiTableSelectedRgba[I];
    }
    RowResizeCursor = static_cast<HCURSOR>(
        LoadImageA(nullptr, MAKEINTRESOURCEA(32645), IMAGE_CURSOR, 0, 0, LR_SHARED));
    ColumnResizeCursor = static_cast<HCURSOR>(
        LoadImageA(nullptr, MAKEINTRESOURCEA(32644), IMAGE_CURSOR, 0, 0, LR_SHARED));
    DefaultCursor = GetCursor();
}

UiTable::~UiTable()
{
    // FUN_00597ed0.
}

bool UiTable::SetColumnEdge(unsigned Index, float X)
{
    // FUN_00597ff0
    if (Index == 0 || ColumnEdges.empty() || Index >= ColumnEdges.size())
        return false;
    unsigned const Prev = Index - 1;
    unsigned Count = 2;
    if (Prev + 2 > ColumnEdges.size())
        Count = static_cast<unsigned>(ColumnEdges.size()) - Prev;
    float const Gap2 = GrabDistance + GrabDistance;
    float const Lo = Gap2 + ColumnEdges[Prev];
    float const Next = Index + 1 == ColumnEdges.size() ? Width : ColumnEdges[Index + 1];
    float const Hi = Next - Gap2;
    if (X < Lo)
        X = Lo;
    else if (X > Hi)
        X = Hi;
    if (X != ColumnEdges[Index])
    {
        ColumnEdges[Index] = X;
        Layout(0, 0, Prev, Count);
    }
    return true;
}

bool UiTable::SetRowEdge(unsigned Index, float Y)
{
    // FUN_00598100
    if (Index == 0 || RowEdges.empty() || Index >= RowEdges.size())
        return false;
    unsigned const Prev = Index - 1;
    unsigned Count = 2;
    if (Prev + 2 > RowEdges.size())
        Count = static_cast<unsigned>(RowEdges.size()) - Prev;
    float const Gap2 = GrabDistance + GrabDistance;
    float const Lo = RowEdges[Prev] - Gap2;
    // x87: the lower limit stays double. Retail bug kept: for the last edge retail adds +Height,
    // not -Height, so the limit is above every edge, the first test always clamps to it and the
    // second clamps to Lo; the last row edge only ever lands on Lo.
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
        Layout(Prev, Count, 0, 0);
    }
    return true;
}

void UiTable::Layout(unsigned RowStart, unsigned RowCount, unsigned ColStart, unsigned ColCount)
{
    // FUN_00598210
    if (RowEdges.empty() || RowStart >= RowEdges.size())
        return;
    if (ColumnEdges.empty() || ColStart >= ColumnEdges.size())
        return;
    if (RowCount == 0)
        RowCount = static_cast<unsigned>(RowEdges.size()) - RowStart;
    if (ColCount == 0)
        ColCount = static_cast<unsigned>(ColumnEdges.size()) - ColStart;
    unsigned const RowEnd = RowStart + RowCount;
    unsigned const ColEnd = ColStart + ColCount;
    for (unsigned Row = RowStart; Row < RowEnd; ++Row)
    {
        // Height of the row: to the next edge, the last one to -Height.
        float const RowHeight = Row < static_cast<unsigned>(RowEdges.size()) - 1
                                    ? RowEdges[Row] - RowEdges[Row + 1]
                                    : RowEdges[Row] + Height;
        for (unsigned Col = ColStart; Col < ColEnd; ++Col)
        {
            // The element address, or 0 when either index or the flat index is out of range.
            // Retail: the element is then read through that address without a test (0059837b
            // `xor esi,esi`, 0059837d `cmp [esi],0`).
            BaseRichTextWin** Cell = nullptr;
            if (Row < RowEdges.size() && Col < ColumnEdges.size())
            {
                unsigned const Flat = static_cast<unsigned>(ColumnEdges.size()) * Row + Col;
                if (Flat < Cells.size())
                    Cell = &Cells[Flat];
            }
            if (*Cell == nullptr)
                continue;

            // Cell transform = this transform * (identity, (column edge, row edge, 0)).
            Transform Offset;
            Win3DIdentityRotation3x3(&Offset.Rotation);
            Offset.Translation.x = ColumnEdges[Col];
            Offset.Translation.y = RowEdges[Row];
            Offset.Translation.z = 0.0f;
            Transform World;
            UiTableMath()->MulTransform(World, Placement,
                                        Offset);
            Transform const Copy = World;
            (*Cell)->SetTransform(&Copy);

            float const ColumnWidth = Col < static_cast<unsigned>(ColumnEdges.size()) - 1
                                          ? ColumnEdges[Col + 1] - ColumnEdges[Col]
                                          : Width - ColumnEdges[Col];
            (*Cell)->SetNormSize(ColumnWidth, RowHeight);
            (*Cell)->EnsureSurface();
        }
    }
}

bool UiTable::Create(Win3D* NewParent, void* Data)
{
    // LAB_00598490
    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>((Flags & 0xfb) | 3);
    if (Data != nullptr)
    {
        // FUiPayloadTable: {Right, Bottom, rows, columns}.
        float const* const Size = static_cast<float const*>(Data);
        int const* const Counts = static_cast<int const*>(Data);
        Width = Size[0];
        Height = Size[1];
        RowEdges.resize(static_cast<unsigned>(Counts[2]), 0.0f);
        ColumnEdges.resize(static_cast<unsigned>(Counts[3]), 0.0f);
        Cells.resize(static_cast<unsigned>(Counts[3]) * static_cast<unsigned>(Counts[2]),
                     nullptr);
        FUiTableRow const Prototype(1, 1);
        Rows.resize(static_cast<unsigned>(Counts[2]), Prototype);
    }

    // Evenly spaced edges: rows going down from 0, columns going right from 0. No Layout here.
    if (ColumnEdges.size() > 0 && !RowEdges.empty() && RowEdges.size() > 0)
    {
        float const RowStep = static_cast<float>(
            Height / static_cast<double>(static_cast<unsigned>(RowEdges.size())));
        float const ColumnStep = static_cast<float>(
            Width / static_cast<double>(static_cast<unsigned>(ColumnEdges.size())));
        for (unsigned I = 0; !RowEdges.empty() && I < RowEdges.size(); ++I)
            RowEdges[I] = static_cast<float>(-(static_cast<double>(I) * RowStep));
        for (unsigned I = 0; !ColumnEdges.empty() && I < ColumnEdges.size(); ++I)
            ColumnEdges[I] = static_cast<float>(static_cast<double>(I) * ColumnStep);
    }
    return true;
}

void UiTable::SetTransform(Transform const* NewXform)
{
    // LAB_005987e0: both setters are virtual calls; then the inverse and a full Layout.
    SetOrientation(&NewXform->Rotation);
    SetPosition(&NewXform->Translation);
    float W = 0.0f;
    Transform Inverse;
    UiTableMath()->GeneralInverse(Inverse, W, Placement);
    InverseTransform = Inverse;
    Layout(0, 0, 0, 0);
}

void UiTable::SetPosition(Vector const* NewPosition)
{
    // LAB_00598850.
    Position.x = NewPosition->x;
    Position.y = NewPosition->y;
    Position.z = NewPosition->z;
    float W = 0.0f;
    Transform Inverse;
    UiTableMath()->GeneralInverse(Inverse, W, Placement);
    InverseTransform = Inverse;
    Layout(0, 0, 0, 0);
}

void UiTable::Update()
{
    // LAB_005988c0.
    Win3D::Update();

    if (Win3DGetCapture() == this && bDragging != 0)
    {
        // Dragging an edge: follow the mouse with the grab offset kept.
        Vector Ray;
        Win3DGetMouseRay(&Ray);
        Vector const Local = UiTableRayToLocal(this, &Ray);
        if (bDragColumn != 0)
        {
            SetColumnEdge(DragEdge, Local.x + DragOffset);
            SetCursor(ColumnResizeCursor);
        }
        else
        {
            SetRowEdge(DragEdge, Local.y + DragOffset);
            SetCursor(RowResizeCursor);
        }
        return;
    }
    if (Win3DGetCapture() != nullptr)
        return;

    Vector Ray;
    Win3DGetMouseRay(&Ray);
    Vector const Local = UiTableRayToLocal(this, &Ray);
    bHovered = 0;
    bDragArmed = 0;
    bDragging = 0;
    // Outside the table: nothing more, no SetCursor either.
    if (!(Local.x >= 0.0f))
        return;
    if (!(Local.x < Width))
        return;
    if (!(Local.y <= 0.0f))
        return;
    float const Bottom = -Height;
    if (!(Local.y > Bottom))
        return;

    if (bColumnsResizable != 0)
    {
        for (unsigned I = 1; !ColumnEdges.empty() && I < ColumnEdges.size(); ++I)
        {
            float const Edge = ColumnEdges[I];
            if (GrabDistance > fabs(static_cast<double>(Local.x) - Edge))
            {
                SetCursor(ColumnResizeCursor);
                DragEdge = I;
                bDragArmed = 1;
                DragOffset = Edge - Local.x;
                bDragColumn = 1;
                return;
            }
        }
    }

    unsigned I = 0;
    while (!RowEdges.empty() && I < RowEdges.size())
    {
        float const Edge = RowEdges[I];
        float const Next = I + 1 < RowEdges.size() ? RowEdges[I + 1] : Bottom;
        if (bRowsResizable != 0 && I > 0
            && GrabDistance > fabs(static_cast<double>(Local.y) - Edge))
        {
            SetCursor(RowResizeCursor);
            DragEdge = I;
            bDragColumn = 0;
            DragOffset = Edge - Local.y;
            bDragArmed = 1;
            return;
        }
        // Above this edge, at or below the next one, or unordered: next row.
        if (!(Local.y <= Edge) || !(Local.y > Next))
        {
            ++I;
            continue;
        }
        if (!RowEdges.empty() && I < RowEdges.size() && Rows[I].bHoverable != 0)
        {
            HoveredRow = I;
            bHovered = 1;
        }
        break;
    }
    SetCursor(DefaultCursor);
}

void UiTable::Draw()
{
    // FUN_00598c40
    Win3DBindModelview(nullptr, &Placement);
    UiTableSetDrawStates();
    if (bSelected != 0)
        UiTableDrawRowQuad(this, SelectedRgba, SelectedRow);
    if (bHovered != 0)
        UiTableDrawRowQuad(this, HoveredRgba, HoveredRow);

    Win3DDrawChildren(this);

    Win3DBindModelview(nullptr, &Placement);
    UiTableSetDrawStates();
    Win3DColor4ubv(FrameRgba);
    Win3DBegin(kWin3DLineLoop);
    Win3DVertex3f(0.0f, 0.0f, 0.0f);
    Win3DVertex3f(Width, 0.0f, 0.0f);
    Win3DVertex3f(Width, -Height, 0.0f);
    Win3DVertex3f(0.0f, -Height, 0.0f);
    Win3DEnd();
    Win3DBegin(kWin3DLines);
    for (unsigned I = 0; !RowEdges.empty() && I < RowEdges.size(); ++I)
    {
        Win3DVertex3f(0.0f, RowEdges[I], 0.0f);
        Win3DVertex3f(Width, RowEdges[I], 0.0f);
    }
    for (unsigned I = 0; !ColumnEdges.empty() && I < ColumnEdges.size(); ++I)
    {
        Win3DVertex3f(ColumnEdges[I], 0.0f, 0.0f);
        Win3DVertex3f(ColumnEdges[I], -Height, 0.0f);
    }
    Win3DEnd();
}

bool UiTable::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_00599090
    if (Event->Button == kWin3DMouseLeft)
    {
        if (Event->Action == kWin3DMousePress)
        {
            if (bHovered != 0)
            {
                // Press on the hovered row: select it when it is both selectable and hoverable.
                unsigned const Row = HoveredRow;
                if (!RowEdges.empty() && Row < RowEdges.size() && Rows[Row].bSelectable != 0
                    && Rows[Row].bHoverable != 0)
                {
                    SelectedRow = Row;
                    bSelected = 1;
                }
                return true;
            }
            if (bDragArmed != 0)
            {
                // Press on an edge: take the capture and start dragging.
                Win3DPushCapture(this);
                bDragging = 1;
                return true;
            }
        }
        else if (Win3DGetCapture() == this && bDragging != 0)
        {
            bDragging = 0;
            Win3DRemoveCapture(this);
            return true;
        }
    }
    // Bubble to the parent, false at the root.
    if (Parent != nullptr)
        return Parent->MouseEvent(Event);
    return false;
}

bool UiTable::HitTest(Vector const* Ray)
{
    // LAB_00599160: the given ray on the table plane, in table space, inside
    // [0, Width) x (-Height, 0].
    Vector const Local = UiTableRayToLocal(this, Ray);
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
