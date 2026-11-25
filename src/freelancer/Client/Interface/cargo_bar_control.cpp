// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/cargo_bar_control.h"

#include "Common/Fonts/ids_strings.h"      // GetIdsStringWWithRawFallback (FUN_004347e0)
#include "NeuroNet/nn_ui_pulse.h"      // G_HudChromeColorRgba (DAT_00679bb4)
#include "Space/star_sphere.h"      // Win3DBindModelview (FUN_00422690)
#include "Interface/tooltip_hover.h"    // FToolTipHoverInfo, NnToolTipFeedHover (FUN_004be370)
#include "Interface/win3d_engine_submit.h" // Win3DRayHitsAabbHalfExtents (FUN_005a1130)
#include "Interface/win3d_immediate.h"
#include "Interface/win_factory.h"

#include "common_csys.h"      // Acquire3DMathEngine (FUN_005b73e0)
#include "common_ini.h"
#include "x86math.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

WINFACTORY<CargoBarControl> G_CargoBarControlFactory("CargoBarControl");

namespace
{
    // DAT_00679b4c -- SetOrientation's lazily acquired 3DMathEngine.
    I3DMathEngine* G_CargoBarMath = nullptr;

    void SetIdentity9(Matrix* M)
    {
        M->E[0][0] = 1.0f;
        M->E[0][1] = 0.0f;
        M->E[0][2] = 0.0f;
        M->E[1][0] = 0.0f;
        M->E[1][1] = 1.0f;
        M->E[1][2] = 0.0f;
        M->E[2][0] = 0.0f;
        M->E[2][1] = 0.0f;
        M->E[2][2] = 1.0f;
    }
} // namespace

// The start values after the FToolTipHoverInfo constructor. Retail leaves +0x48..+0x5f as
// stack garbage; the port's constructor zeroes them.
void ToolTipHoverInit(FToolTipHoverInfo* Info)
{
    Info->Ids = -1;
    Info->IdsAlt = 0;
    Info->AuxField = 0;
    Info->bLive = 0;
}

// FUN_004142a0(Length + 1) then wcsncpy + NUL.
void ToolTipHoverSetCaption(FToolTipHoverInfo* Info, wchar_t const* Text)
{
    unsigned const Length = static_cast<unsigned>(wcslen(Text));
    unsigned const Need = Length + 1;
    if (Need > Info->CaptionCapacity)
    {
        if (Info->CaptionW != nullptr)
        {
            Info->CaptionW = static_cast<wchar_t*>(realloc(Info->CaptionW, Need * sizeof(wchar_t)));
        }
        else
        {
            Info->CaptionW = static_cast<wchar_t*>(malloc(Need * sizeof(wchar_t)));
            Info->CaptionW[0] = L'\0';
        }
        Info->CaptionCapacity = Need;
    }
    wcsncpy(Info->CaptionW, Text, Length);
    Info->CaptionW[Length] = L'\0';
}

// FUN_00444ee0, the record's destructor: the Font::String at +0x10, then the caption buffer at
// +0x08. Runs as ~FToolTipHoverInfo, so the stack records below need no teardown call.
void ToolTipHoverDestroy(FToolTipHoverInfo* Info)
{
    FontStringDestroy(&Info->Text);
    if (Info->CaptionW != nullptr)
    {
        free(Info->CaptionW);
        Info->CaptionW = nullptr;
        Info->CaptionCapacity = 0;
    }
}

namespace
{

    // One filled quad in the current colour, z = 0, in the vertex order of every quad in Draw.
    void DrawBarQuad(float X0, float Y0, float Width, float Height)
    {
        Win3DBegin(kWin3DQuads);
        float const X1 = X0 + Width;
        float const Y1 = Y0 + Height;
        Win3DVertex3f(X0, Y0, 0.0f);
        Win3DVertex3f(X1, Y0, 0.0f);
        Win3DVertex3f(X1, Y1, 0.0f);
        Win3DVertex3f(X0, Y1, 0.0f);
        Win3DEnd();
    }
}

CargoBarControl::CargoBarControl()
{
    // FUN_0055e930, in store order.
    Color[3] = 0;
    Color[2] = 0;
    Color[1] = 0;
    Color[0] = 0;
    HighlightColor[3] = 0;
    HighlightColor[2] = 0;
    HighlightColor[1] = 0;
    HighlightColor[0] = 0;
    Capacity = 0.0f;
    Used = 0.0f;
    TailSegment = 0.0f;
    HeadSegment = 0.0f;
    bHorizontal = 0;
    bHovered = 0;
    Field34b = 0;
    bToolTip = 0;
    ToolTip2Ids = 0;
    bBorder = 1;
    bShow = 1;
    // After the vptrs the segment colour becomes 0xff,0xff,0,0xff.
    HighlightColor[2] = 0;
    HighlightColor[0] = 0xff;
    HighlightColor[1] = 0xff;
    HighlightColor[3] = 0xff;
    Pad34e[0] = 0;
    Pad34e[1] = 0;
}

int CargoBarControl::GetHitPriority()
{
    // LAB_0055e9e0.
    return 0x64;
}

void CargoBarControl::RefreshAttachment()
{
    // FUN_00401e90 (empty): the bar does not re-attach to a hardpoint.
}

void CargoBarControl::GetAttachOrientation(Matrix* OutOrientation)
{
    // LAB_0055ea20 (RET 4). Retail also returns the out pointer; the port's slot is void.
    Matrix Identity;
    SetIdentity9(&Identity);
    Matrix const* Source = &Identity;
    if (Parent != nullptr && Parent->AsWinControl() != nullptr)
        Source = &AttachOrientation;
    memcpy(OutOrientation, Source, sizeof(Matrix));
}

void CargoBarControl::GetAttachPosition(Vector* OutPosition)
{
    // FUN_0055eaf0 (RET 4).
    if (Parent != nullptr && Parent->AsWinControl() != nullptr)
    {
        OutPosition->x = AttachPosition.x;
        OutPosition->y = AttachPosition.y;
        OutPosition->z = AttachPosition.z;
        return;
    }
    OutPosition->x = 0.0f;
    OutPosition->y = 0.0f;
    OutPosition->z = 0.0f;
}

bool CargoBarControl::Create(Win3D* NewParent, void* Data)
{
    // LAB_0055eb40 (RET 8): Win3D's Create, then the INI keys.
    bool const bResult = Win3D::Create(NewParent, nullptr);
    INI_Reader* const Ini = static_cast<INI_Reader*>(Data);
    if (Ini == nullptr || !bResult)
        return bResult;

    while (Ini->read_value())
    {
        if (Ini->is_value("mesh"))
        {
            SetMesh(Ini->get_value_string(), 0);
        }
        // "offset" is tested even after "mesh" matched.
        if (Ini->is_value("offset"))
        {
            float const Y = static_cast<float>(Ini->get_value_float(1));
            float const X = static_cast<float>(Ini->get_value_float(0));
            float const Z = static_cast<float>(Ini->get_value_float(2));
            Offset[0] = X;
            Offset[1] = Y;
            Offset[2] = Z;
            continue;
        }
        if (Ini->is_value("hardpoint"))
        {
            // The NEW parent's hardpoint; neither +0x1d0 nor +0x328 is written.
            SetIdentity9(&AttachOrientation);
            AttachPosition.z = 0.0f;
            AttachPosition.y = 0.0f;
            AttachPosition.x = 0.0f;
            if (NewParent == nullptr)
                continue;
            WinControl* const ParentControl = NewParent->AsWinControl();
            if (ParentControl == nullptr)
                continue;
            ParentControl->GetHardpointTransform(Ini->get_value_string(), &AttachXform);
            continue;
        }
        if (Ini->is_value("mouse_size"))
        {
            float const Y = static_cast<float>(Ini->get_value_float(1));
            float const X = static_cast<float>(Ini->get_value_float(0));
            float const Z = static_cast<float>(Ini->get_value_float(2));
            HalfExtents[0] = X;
            HalfExtents[1] = Y;
            HalfExtents[2] = Z;
            continue;
        }
        if (Ini->is_value("horizontal"))
        {
            bHorizontal = (Ini->get_value_float(0) != 0.0) ? 1 : 0;
            continue;
        }
        if (Ini->is_value("tooltip"))
        {
            bToolTip = (Ini->get_value_float(0) != 0.0) ? 1 : 0;
            continue;
        }
        if (Ini->is_value("tooltip2"))
        {
            ToolTip2Ids = static_cast<int>(Ini->get_value_float(0));
            bToolTip = 1;
            continue;
        }
        if (Ini->is_value("color"))
        {
            // Components 2, 1, 0 are read first, then 3.
            double const V2 = Ini->get_value_float(2);
            double const V1 = Ini->get_value_float(1);
            double const V0 = Ini->get_value_float(0);
            unsigned char Rgba[4];
            Rgba[0] = static_cast<unsigned char>(static_cast<int>(V0));
            Rgba[1] = static_cast<unsigned char>(static_cast<int>(V1));
            Rgba[2] = static_cast<unsigned char>(static_cast<int>(V2));
            Rgba[3] = static_cast<unsigned char>(static_cast<int>(Ini->get_value_float(3)));
            memcpy(Color, Rgba, sizeof(Color));
            continue;
        }
        if (Ini->is_value("color_highlight"))
        {
            double const V2 = Ini->get_value_float(2);
            double const V1 = Ini->get_value_float(1);
            double const V0 = Ini->get_value_float(0);
            unsigned char Rgba[4];
            Rgba[0] = static_cast<unsigned char>(static_cast<int>(V0));
            Rgba[1] = static_cast<unsigned char>(static_cast<int>(V1));
            Rgba[2] = static_cast<unsigned char>(static_cast<int>(V2));
            Rgba[3] = static_cast<unsigned char>(static_cast<int>(Ini->get_value_float(3)));
            memcpy(HighlightColor, Rgba, sizeof(HighlightColor));
        }
    }
    return bResult;
}

void CargoBarControl::Init(char const* MeshPath, float const* NewOffset, char const* Hardpoint,
                           float const* NewHalfExtents, unsigned char bNewHorizontal,
                           int NewToolTip2Ids, unsigned NewColor, unsigned NewHighlightColor)
{
    // FUN_0055ee80 (RET 0x20).
    SetMesh(MeshPath, 0);
    Offset[0] = NewOffset[0];
    Offset[1] = NewOffset[1];
    Offset[2] = NewOffset[2];
    if (Hardpoint != nullptr)
    {
        SetIdentity9(&AttachOrientation);
        AttachPosition.z = 0.0f;
        AttachPosition.y = 0.0f;
        AttachPosition.x = 0.0f;
        if (Parent != nullptr)
        {
            WinControl* const ParentControl = Parent->AsWinControl();
            if (ParentControl != nullptr)
                ParentControl->GetHardpointTransform(Hardpoint, &AttachXform);
        }
    }
    HalfExtents[0] = NewHalfExtents[0];
    HalfExtents[1] = NewHalfExtents[1];
    HalfExtents[2] = NewHalfExtents[2];
    bHorizontal = bNewHorizontal;
    ToolTip2Ids = NewToolTip2Ids;
    bToolTip = (NewToolTip2Ids != 0) ? 1 : 0;
    memcpy(Color, &NewColor, sizeof(Color));
    memcpy(HighlightColor, &NewHighlightColor, sizeof(HighlightColor));
}

void CargoBarControl::Draw()
{
    // LAB_0055ef60.
    if (bShow == 0)
        return;

    WinControl::Draw();

    Transform Matrix;
    memcpy(&Matrix, &Orientation, 9 * sizeof(float));
    Matrix.Translation.x = Position.x + Offset[0];
    Matrix.Translation.y = Position.y + Offset[1];
    Matrix.Translation.z = Position.z + Offset[2];
    Win3DBindModelview(nullptr, &Matrix);
    Win3DDisable(kWin3DTexture2D);
    Win3DDisable(kWin3DDepthTest);

    unsigned char Chrome[4];
    memcpy(Chrome, G_HudChromeColorRgba, sizeof(Chrome));
    unsigned char Segment[4];
    memcpy(Segment, HighlightColor, sizeof(Segment));

    // Nothing but the border when Capacity == 0.
    if (Capacity != 0.0f)
    {
        float const Width2 = HalfExtents[0] + HalfExtents[0];
        float const Height2 = HalfExtents[1] + HalfExtents[1];

        if (bHorizontal != 0)
        {
            if (Width2 != 0.0f)
            {
                // The head segment from the left edge.
                float HeadLength = 0.0f;
                if (HeadSegment != 0.0f)
                {
                    HeadLength = HeadSegment / Capacity * Width2;
                    Win3DColor4ub(Segment[0], Segment[1], Segment[2], 0xff);
                    DrawBarQuad(-HalfExtents[0], -HalfExtents[1], HeadLength, Height2);
                }

                // The tail segment ends at Used. Unlike the head, it scales through a double
                // reciprocal, as retail.
                double const Inverse = 1.0 / Capacity;
                float const UsedLength = static_cast<float>(Inverse * Used * Width2);
                float TailLength = 0.0f;
                if (TailSegment != 0.0f)
                {
                    TailLength = static_cast<float>(Inverse * TailSegment * Width2);
                    Win3DColor4ub(Segment[0], Segment[1], Segment[2], 0xff);
                    float const TailStart = UsedLength - HalfExtents[0] - TailLength;
                    DrawBarQuad(TailStart, -HalfExtents[1], TailLength, Height2);
                }

                float const FillLength = UsedLength - HeadLength - TailLength;
                Win3DColor4ub(Color[0], Color[1], Color[2], Color[3]);
                DrawBarQuad(HeadLength - HalfExtents[0], -HalfExtents[1], FillLength, Height2);
            }
        }
        else if (Height2 != 0.0f)
        {
            // The same bottom-up along y, full width across.
            float HeadLength = 0.0f;
            if (HeadSegment != 0.0f)
            {
                HeadLength = HeadSegment / Capacity * Height2;
                Win3DColor4ub(Segment[0], Segment[1], Segment[2], 0xff);
                DrawBarQuad(-HalfExtents[0], -HalfExtents[1], Width2, HeadLength);
            }

            double const Inverse = 1.0 / Capacity;
            float const UsedLength = static_cast<float>(Inverse * Used * Height2);
            float TailLength = 0.0f;
            if (TailSegment != 0.0f)
            {
                TailLength = static_cast<float>(Inverse * TailSegment * Height2);
                Win3DColor4ub(Segment[0], Segment[1], Segment[2], 0xff);
                float const TailStart = UsedLength - HalfExtents[1] - TailLength;
                DrawBarQuad(-HalfExtents[0], TailStart, Width2, TailLength);
            }

            float const FillLength = UsedLength - HeadLength - TailLength;
            Win3DColor4ub(Color[0], Color[1], Color[2], Color[3]);
            DrawBarQuad(-HalfExtents[0], HeadLength - HalfExtents[1], Width2, FillLength);
        }
    }

    if (bBorder != 0)
    {
        Win3DColor4ub(Chrome[0], Chrome[1], Chrome[2], Chrome[3]);
        Win3DBegin(kWin3DLineLoop);
        Win3DVertex3f(-HalfExtents[0], -HalfExtents[1], 0.0f);
        Win3DVertex3f(HalfExtents[0], -HalfExtents[1], 0.0f);
        Win3DVertex3f(HalfExtents[0], HalfExtents[1], 0.0f);
        Win3DVertex3f(-HalfExtents[0], HalfExtents[1], 0.0f);
        Win3DEnd();
    }

    PostDraw();
}

void CargoBarControl::SetPosition(float const* NewPosition)
{
    // LAB_0055f5a0 (RET 4): WinControl's with the attach position added.
    float Attach[3];
    GetAttachPosition(reinterpret_cast<Vector*>(Attach));
    float Placed[3];
    Placed[0] = Attach[0] + NewPosition[0];
    Placed[1] = Attach[1] + NewPosition[1];
    Placed[2] = Attach[2] + NewPosition[2];
    WinControl::SetPosition(Placed);
}

void CargoBarControl::SetOrientation(Matrix const* NewOrientation)
{
    // LAB_0055f5f0 (RET 4): WinControl's on NewOrientation * the attach orientation.
    float Attach[9];
    GetAttachOrientation(reinterpret_cast<Matrix*>(Attach));
    if (G_CargoBarMath == nullptr)
        G_CargoBarMath = Acquire3DMathEngine();
    Matrix Product;
    G_CargoBarMath->MulMatrix(Product, *reinterpret_cast<Matrix const*>(NewOrientation),
                              *reinterpret_cast<Matrix const*>(Attach));
    float Copy[9];
    memcpy(Copy, &Product, sizeof(Copy));
    WinControl::SetOrientation(reinterpret_cast<Matrix const*>(Copy));
}

void CargoBarControl::Update()
{
    // LAB_0055f660.
    if (bToolTip != 0 && bHovered != 0)
    {
        if (ToolTip2Ids != 0)
        {
            // "<IDS> used/capacity", keyed by this control.
            int const UsedCount = static_cast<int>(Used);
            int const CapacityCount = static_cast<int>(Capacity);
            wchar_t Name[0x100];
            GetIdsStringWWithRawFallback(static_cast<unsigned>(ToolTip2Ids), Name, 0xff);
            wchar_t Text[0x100];
            swprintf(Text, 0x100, L"%s %d/%d", Name, UsedCount, CapacityCount);
            FToolTipHoverInfo Info;
            ToolTipHoverInit(&Info);
            ToolTipHoverSetCaption(&Info, Text);
            Info.Ids = static_cast<int>(reinterpret_cast<intptr_t>(this));
            Info.IdsAlt = 0;
            NnToolTipFeedHover(&Info);
        }
        else if (ToolTipIds != 0)
        {
            // The plain tooltip id, which is also the record's key.
            FToolTipHoverInfo Info;
            ToolTipHoverInit(&Info);
            Info.IdsAlt = ToolTipIds;
            Info.Ids = ToolTipIds;
            NnToolTipFeedHover(&Info);
        }
        else
        {
            // "used/capacity", keyed by this control.
            int const UsedCount = static_cast<int>(Used);
            int const CapacityCount = static_cast<int>(Capacity);
            wchar_t Text[0x100];
            swprintf(Text, 0x100, L"%d/%d", UsedCount, CapacityCount);
            FToolTipHoverInfo Info;
            ToolTipHoverInit(&Info);
            ToolTipHoverSetCaption(&Info, Text);
            Info.Ids = static_cast<int>(reinterpret_cast<intptr_t>(this));
            Info.IdsAlt = 0;
            NnToolTipFeedHover(&Info);
        }
    }

    Win3D::Update();
}

void CargoBarControl::MouseEnter(float const* Ray)
{
    // FUN_0055fa00 (RET 4): the parent WinControl hears 0x2c with this.
    (void)Ray;
    bHovered = 1;
    if (Parent == nullptr)
        return;
    WinControl* const ParentControl = Parent->AsWinControl();
    if (ParentControl == nullptr)
        return;
    ParentControl->SetProperty(kWinNotifyHighlight, reinterpret_cast<uintptr_t>(AsWinControl()), 0);
}

void CargoBarControl::MouseLeave(float const* Ray)
{
    // FUN_0055fa40 (RET 4).
    (void)Ray;
    bHovered = 0;
    if (Parent == nullptr)
        return;
    WinControl* const ParentControl = Parent->AsWinControl();
    if (ParentControl == nullptr)
        return;
    ParentControl->SetProperty(kWinNotifyUnhighlight, reinterpret_cast<uintptr_t>(AsWinControl()), 0);
}

bool CargoBarControl::HitTest(float const* Ray)
{
    // LAB_0055fa80 (RET 4). Unlike WinControl's there is no instance transform, no hit offset
    // and no parent notification.
    float Matrix[12];
    memcpy(Matrix, &Orientation, 9 * sizeof(float));
    Matrix[9] = Position.x + Offset[0];
    Matrix[10] = Position.y + Offset[1];
    Matrix[11] = Position.z + Offset[2];
    return Win3DRayHitsAabbHalfExtents(HalfExtents, Matrix, Ray);
}

intptr_t CargoBarControl::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_0055fb00 (RET 0xc): every case returns 0; nothing is forwarded to WinControl.
    switch (Msg)
    {
    case kWinPropHide:
        if (A != 0)
            Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));
        else
            Flags = static_cast<unsigned char>(Flags | kWin3DActive | kWin3DVisible);
        break;
    case kWinPropDisable:
        bShow = (A == 0) ? 1 : 0;
        break;
    case kWinPropToolTip:
        ToolTipIds = static_cast<int>(A);
        bToolTip = (A != 0) ? 1 : 0;
        break;
    case 0x19:
        bBorder = ((A & 0xffff0000u) != 0) ? 0 : 1;
        break;
    case kWinNotifyHighlight:
        Field34b = 1;
        break;
    case kWinNotifyUnhighlight:
        Field34b = 0;
        break;
    case 0x3f:
        memcpy(Color, &A, sizeof(Color));
        break;
    case 0x40:
        // The arguments convert as unsigned.
        Capacity = static_cast<float>(static_cast<unsigned>(A));
        Used = static_cast<float>(static_cast<unsigned>(B));
        break;
    case 0x41:
        TailSegment = static_cast<float>(static_cast<unsigned>(A));
        HeadSegment = static_cast<float>(static_cast<unsigned>(B));
        break;
    default:
        break;
    }
    return 0;
}

WinControl* WinControl::CreateCargoBarControl(char const* Name, char const* MeshPath,
                                              float const* NewOffset, char const* Hardpoint,
                                              float const* NewHalfExtents,
                                              unsigned char bHorizontal, int ToolTip2Ids,
                                              unsigned Color, unsigned HighlightColor)
{
    // FUN_005a1080 (RET 0x24).
    Win3D* const Created = WinFactoryCreate(Name, "CargoBarControl", this, nullptr);
    if (Created != nullptr)
    {
        static_cast<CargoBarControl*>(Created)->Init(MeshPath, NewOffset, Hardpoint,
                                                     NewHalfExtents, bHorizontal, ToolTip2Ids,
                                                     Color, HighlightColor);
    }
    return static_cast<WinControl*>(Created);
}
