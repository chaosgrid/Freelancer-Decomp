// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/rich_text_win.h"

#include "Interface/fl_ime_input_box.h"
#include "Core/globals.h"
#include "Space/HUD/hud_command_dispatch.h"
#include "Common/Fonts/ids_rdl.h"
#include "Common/Graphics/pixel_format.h"   // PixelFormatFromD3D / PixelFormatFind
#include "Common/Graphics/screen_quad_state.h"   // ScreenQuadDrawBigImageTile
#include "Space/star_sphere.h"
#include "Interface/ui_page_desc.h"
#include "Interface/ui_scroll.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win_factory.h"

#include "DALib.h"
#include "common_csys.h"
#include "dacom.h"
#include "dacom_iids.h"
#include "render/IBigImage2.h"
#include "render/IRenderPipeline.h"
#include "x86math.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

namespace
{
    // .rdata 0x005e46a8.
    char const* const kRichDisplayCpp = "E:\\FL\\Scratch\\Source\\Client\\Interface\\RichDisplay.cpp";

    // DAT_0067c480 -- the lazily acquired 3DMathEngine every RichDisplay function tests and
    // fills in place.
    I3DMathEngine* G_RichTextMathEngine = nullptr;

    I3DMathEngine* RichTextMathEngine()
    {
        if (G_RichTextMathEngine == nullptr)
            G_RichTextMathEngine = Acquire3DMathEngine();
        return G_RichTextMathEngine;
    }

    // DAT_00615174 -- "ScrollV_%d" serial, .data initial value 1.
    int G_ScrollVSerial = 1;

    // FUN_00579390 (thiscall on +0x3d4). The dtor and EnsureSurface inline the same body.
    void ReleaseRichTextSurface(FRichTextSurface* Surface)
    {
        if (Surface->Dc != nullptr)
        {
            if (Surface->PreviousObject != nullptr)
                SelectObject(Surface->Dc, Surface->PreviousObject);
            if (Surface->Bitmap != nullptr)
                DeleteObject(Surface->Bitmap);
            DeleteDC(Surface->Dc);
        }
        Surface->Height = -1;
        Surface->Width = -1;
        Surface->PreviousObject = nullptr;
        Surface->Bitmap = nullptr;
        Surface->Dc = nullptr;
    }

    // Inlined twice in EnsureSurface (width, then height): a negative size becomes 1, zero stays
    // 0, a power of two stays itself, anything else rounds up to the next one.
    int RoundSurfaceSize(int Size)
    {
        if (Size < 0)
            return 1;
        int Result = 1;
        int Rest = Size;
        if ((Size & (Size - 1)) == 0)
        {
            while (Rest != 0)
            {
                Rest >>= 1;
                Result <<= 1;
            }
            return Result >> 1;
        }
        while (Rest != 0)
        {
            Rest >>= 1;
            Result <<= 1;
        }
        return Result;
    }
}

// ---------------------------------------------------------------------------------------------
// BaseRichTextWin
// ---------------------------------------------------------------------------------------------

BaseRichTextWin::BaseRichTextWin()
{
    // FUN_005793e0.
    Surface.Height = -1;
    Surface.Width = -1;
    Surface.Dc = nullptr;
    Surface.Bitmap = nullptr;
    Surface.PreviousObject = nullptr;
    Image = nullptr;

    // The hover record is set up twice, as retail.
    HoverInfo.IdsAlt = 0;
    HoverInfo.Ids = -1;
    HoverInfo.AuxField = 0;
    HoverInfo.bLive = 0;

    DebugWin = nullptr;
    bDrawFrame = 0;
    bSkipUpload = 0;
    // DAT_00615170: 00 ff 00 ff.
    FrameColor[0] = 0x00;
    FrameColor[1] = 0xff;
    FrameColor[2] = 0x00;
    FrameColor[3] = 0xff;
    ScrollY = 0;
    ScrollX = 0;
    Trc.set_origin_v(0);
    Trc.set_origin_h(0);
    ScrollBarH = nullptr;
    ScrollBarV = nullptr;
    bAutoScrollbar = 0;
    ScrollBarWidth = 0.02f;
    ScrollBarGap = 0.002f;
    LayoutHeight = 0;
    Pitch = 0;
    AlignMode = 0;
    bNeverHit = 0;
    bToolTipEnabled = 0;

    // The second time the key ends as this control.
    HoverInfo.IdsAlt = 0;
    HoverInfo.Ids = -1;
    HoverInfo.AuxField = 0;
    if (HoverInfo.CaptionW != nullptr && HoverInfo.CaptionW[0] != L'\0')
        HoverInfo.CaptionW[0] = L'\0';
    HoverInfo.bLive = 0;
    HoverInfo.Ids = static_cast<int>(reinterpret_cast<intptr_t>(this));
    HoverInfo.IdsAlt = 0;

    bKeepScrolledToEnd = 0;
    NormHeight = 0.0f;
    NormWidth = 0.0f;
    WorldRect[3] = 0.0f;
    WorldRect[2] = 0.0f;
    WorldRect[1] = 0.0f;
    WorldRect[0] = 0.0f;
    PixelHeight = 0;
    PixelWidth = 0;
    ScreenRect[3] = 0;
    ScreenRect[1] = 0;
    ScreenRect[2] = 0;
    ScreenRect[0] = 0;
    Alpha = 1.0f;
    AttachOffset[2] = 0.0f;
    AttachOffset[1] = 0.0f;
    AttachOffset[0] = 0.0f;
    bLayoutDirty = 1;
    bSurfaceDirty = 1;
}

BaseRichTextWin::~BaseRichTextWin()
{
    // FUN_00579620. The DebugWindow branch (DestroyWindow, delete) is not ported: +0x32c is only
    // ever written null (see the header), so it never runs.

    // The hover record goes first in retail; it is torn down here because the port releases
    // the members below it by hand in this body. ~FToolTipHoverInfo then finds it empty.
    ToolTipHoverDestroy(&HoverInfo);

    if (Image != nullptr)
    {
        Image->Release();
        Image = nullptr;
    }

    ReleaseRichTextSurface(&Surface);
}

bool BaseRichTextWin::HitTest(float const* Ray)
{
    // LAB_0057aa80 (RET 4). The y of the ray's point is rounded to float; the x difference
    // stays double.
    if (bNeverHit != 0)
        return false;
    double const T = static_cast<double>(Position.z) / Ray[2];
    float const PointY = static_cast<float>(T * Ray[1]);
    double const DeltaX = T * Ray[0] - Position.x;
    if (DeltaX < 0.0)
        return false;
    if (!(DeltaX <= static_cast<double>(WorldRect[2]) - WorldRect[0]))
        return false;
    double const DeltaY = static_cast<double>(Position.y) - PointY;
    if (DeltaY < 0.0)
        return false;
    if (!(DeltaY <= static_cast<double>(WorldRect[1]) - WorldRect[3]))
        return false;
    return true;
}

void BaseRichTextWin::SetPosition(float const* NewPosition)
{
    // FUN_0057b0a0 (RET 4).
    float Moved[3];
    Moved[0] = AttachOffset[0] + NewPosition[0];
    Moved[1] = AttachOffset[1] + NewPosition[1];
    Moved[2] = AttachOffset[2] + NewPosition[2];
    WinControl::SetPosition(Moved);
}

bool BaseRichTextWin::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_0057a810 (RET 8). Win3D's Create always gets a null payload.
    bool const bResult = Win3D::Create(NewParent, nullptr);
    WorldRect[3] = 0.0f;
    WorldRect[2] = 0.0f;
    WorldRect[1] = 0.0f;
    WorldRect[0] = 0.0f;
    PixelHeight = 0;
    PixelWidth = 0;
    ScreenRect[3] = 0;
    ScreenRect[1] = 0;
    ScreenRect[2] = 0;
    ScreenRect[0] = 0;
    AttachOffset[2] = 0.0f;
    AttachOffset[1] = 0.0f;
    AttachOffset[0] = 0.0f;
    bLayoutDirty = 1;
    bSurfaceDirty = 1;
    NormWidth = 0.4f;
    NormHeight = 0.3f;
    EnsureSurface();
    // Retail's DebugWindow window creation (class "RichDisplayDebugWindow", wndproc
    // LAB_0057a9c0) here is dead and not ported: +0x32c is always null.
    return bResult;
}

void BaseRichTextWin::FeedToolTipIfHovered()
{
    // Inlined in three Update bodies in retail.
    if (bToolTipEnabled == 0)
        return;
    FToolTipHoverInfo* const Info = GetToolTipHoverInfo();
    if (Info == nullptr)
        return;
    float Ray[3];
    Win3DGetMouseRay(Ray);
    if (HitTest(Ray))
        NnToolTipFeedHover(Info);
}

void BaseRichTextWin::Update()
{
    // FUN_0057b0e0.
    FeedToolTipIfHovered();
    Win3D::Update();
}

void BaseRichTextWin::UpdateInactive()
{
    // LAB_0057b140. The next sibling is read after the call.
    FeedToolTipIfHovered();
    Win3D* Child = ChildHead;
    while (Child != nullptr)
    {
        if ((Child->Flags & 1) == 0)
            Child->UpdateInactive();
        Child = Child->NextSibling;
    }
}

void BaseRichTextWin::Draw()
{
    // FUN_0057ab10.
    if ((Flags & 2) == 0)
        return;

    EnsureSurface();

    if ((bSurfaceDirty != 0 || bLayoutDirty != 0) && PixelWidth > 0 && PixelHeight > 0)
    {
        SelectClipRgn(Surface.Dc, nullptr);
        RECT Fill;
        Fill.left = 0;
        Fill.top = 0;
        Fill.right = Surface.Width;
        Fill.bottom = Surface.Height;
        HBRUSH const Brush = CreateSolidBrush(0);
        FillRect(Surface.Dc, &Fill, Brush);
        if (Brush != nullptr)
            DeleteObject(Brush);

        if (ScrollBarV != nullptr)
        {
            ScrollBarV->SetPage(static_cast<float>(PixelHeight));
            ScrollBarV->SetStep(static_cast<float>(PixelHeight / 10));
        }

        RebuildLayoutIfDirty();

        switch (AlignMode)
        {
        case 1:
            Trc.set_origin_v(0);
            Trc.set_origin_h(-ScrollX);
            break;
        case 2:
            Trc.set_origin_v((PixelHeight - LayoutHeight) / 2);
            Trc.set_origin_h(-ScrollX);
            break;
        case 3:
            Trc.set_origin_v(PixelHeight - LayoutHeight);
            Trc.set_origin_h(-ScrollX);
            break;
        default:
            break;
        }

        Trc.reset_to_default();
        Trc.set_pos_v(0);
        Trc.set_pos_h(0);
        LayoutList.execute(Trc, true);

        // Read the 16-bit DIB back out of the bitmap: deselect it, GetDIBits, reselect it.
        unsigned int const BufferBytes = static_cast<unsigned int>(Pitch * Surface.Height);
        unsigned char* const Buffer = static_cast<unsigned char*>(operator new(BufferBytes));
        struct FDibInfo
        {
            BITMAPINFOHEADER Header;
            RGBQUAD Colors[1];
        };
        FDibInfo Info;
        memset(&Info, 0, sizeof(Info));
        Info.Header.biSize = 0x28;
        Info.Header.biWidth = Surface.Width;
        Info.Header.biHeight = -Surface.Height;
        Info.Header.biPlanes = 1;
        Info.Header.biBitCount = 0x10;
        Info.Header.biSizeImage = BufferBytes;
        SelectObject(Surface.Dc, Surface.PreviousObject);
        int const Lines = GetDIBits(Surface.Dc, Surface.Bitmap, 0,
                                    static_cast<UINT>(Surface.Height), Buffer,
                                    reinterpret_cast<BITMAPINFO*>(&Info), 0);
        SelectObject(Surface.Dc, Surface.Bitmap);

        // Every drawn (non-black) pixel gets the A1 bit.
        if (Lines != 0)
        {
            unsigned char* Row = Buffer;
            for (int Y = 0; Y < Surface.Height; ++Y)
            {
                unsigned short* const Pixels = reinterpret_cast<unsigned short*>(Row);
                for (int X = 0; X < Surface.Width; ++X)
                {
                    if (Pixels[X] != 0)
                        Pixels[X] = static_cast<unsigned short>(Pixels[X] | 0x8000);
                }
                Row += Pitch;
            }
        }

        if (bSkipUpload == 0 && Image != nullptr)
        {
            // A1R5G5B5; the lookup's result is not tested.
            unsigned long Format = static_cast<unsigned long>(PixelFormatFromD3D(0));
            PixelFormatFind(&Format, 0x10, 5, 5, 5, 1);
            GENRESULT const Result = Image->SetFromBuffer(
                Buffer, &Format, static_cast<unsigned long>(Surface.Width),
                static_cast<unsigned long>(Surface.Height), static_cast<unsigned long>(Pitch));
            if (Result == 0)
                bSurfaceDirty = 0;
            else
                FDUMP(kFdumpError, "%s(%d) : ERROR:General:%s", kRichDisplayCpp, 0x5a1,
                      "Failed to set BigImage from a buffer.\n");
        }
        operator delete(Buffer);
    }

    DrawStaticMesh();

    if (Image != nullptr)
    {
        Image->SetAlpha(Alpha);
        float const White[3] = {1.0f, 1.0f, 1.0f};
        Image->SetColor(White);
        RECT Src;
        Src.left = 0;
        Src.top = 0;
        Src.right = ScreenRect[2] - ScreenRect[0] + 1;
        Src.bottom = ScreenRect[3] - ScreenRect[1] + 1;
        RECT Dest;
        Dest.left = ScreenRect[0];
        Dest.top = ScreenRect[1];
        Dest.right = ScreenRect[2] + 1;
        Dest.bottom = ScreenRect[3] + 1;
        Image->Draw(&Src, &Dest);

        if (bDrawFrame != 0)
        {
            Win3DDisable(kWin3DDepthTest);
            Win3DDisable(kWin3DCullFace);
            DALib::Pipeline->SetRenderState(D3DRS_ZENABLE, 0);
            DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);
            DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, 1);
            Win3DBindModelview(nullptr, &Placement);
            Win3DColor4ubv(FrameColor);
            Win3DDisable(kWin3DTexture2D);
            Win3DBegin(kWin3DLineLoop);
            Win3DVertex3f(0.0f, 0.0f, 0.0f);
            Win3DVertex3f(NormWidth, 0.0f, 0.0f);
            Win3DVertex3f(NormWidth, -NormHeight, 0.0f);
            Win3DVertex3f(0.0f, -NormHeight, 0.0f);
            Win3DEnd();
            Win3DEnable(kWin3DDepthTest);
            Win3DSetDepthWrite(1);
        }
    }

    Win3DDrawChildren(this);
    PostDraw();
}

bool BaseRichTextWin::ProcessMessage(unsigned Msg, unsigned A, unsigned B)
{
    // FUN_0057afd0 (RET 0xc). 0xf005 is a scrollbar's value change; A is the sender.
    if (Msg == kWinMsgScroll)
    {
        if (A == static_cast<unsigned>(reinterpret_cast<uintptr_t>(ScrollBarV)))
        {
            int const OriginX = ScrollX;
            SetScrollOrigin(OriginX, static_cast<int>(ScrollBarV->GetValue()));
            return true;
        }
        if (A == static_cast<unsigned>(reinterpret_cast<uintptr_t>(ScrollBarH)))
        {
            int OriginY;
            switch (AlignMode)
            {
            case 0:
                OriginY = ScrollY;
                break;
            case 1:
                OriginY = 0;
                break;
            case 2:
                OriginY = -((PixelHeight - LayoutHeight) / 2);
                break;
            case 3:
                OriginY = LayoutHeight - PixelHeight;
                break;
            default:
                // Retail passes the message id itself here.
                OriginY = static_cast<int>(Msg);
                break;
            }
            SetScrollOrigin(static_cast<int>(ScrollBarH->GetValue()), OriginY);
            return true;
        }
    }
    return WinControl::ProcessMessage(Msg, A, B);
}

intptr_t BaseRichTextWin::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_0057ebb0 (RET 0xc): property 6 is SetAttachOffset inlined.
    if (Msg != kWinPropOffset)
        return WinControl::SetProperty(Msg, A, B);
    SetAttachOffset(reinterpret_cast<float const*>(A));
    return 1;
}

bool BaseRichTextWin::Relayout()
{
    // FUN_004b6c50.
    return true;
}

FToolTipHoverInfo* BaseRichTextWin::GetToolTipHoverInfo()
{
    // LAB_005798a0.
    return &HoverInfo;
}

void BaseRichTextWin::SetDrawFrame(bool bDraw)
{
    bDrawFrame = bDraw ? 1 : 0;
}

void BaseRichTextWin::SetFrameColor(unsigned long const* Rgba)
{
    memcpy(FrameColor, Rgba, 4);
}

void BaseRichTextWin::SetNeverHit(bool bNever)
{
    bNeverHit = bNever ? 1 : 0;
}

void BaseRichTextWin::SetToolTipEnabled(bool bEnabled)
{
    bToolTipEnabled = bEnabled ? 1 : 0;
}

void BaseRichTextWin::SetScrollOrigin(int NewScrollX, int NewScrollY)
{
    // FUN_005798b0 (RET 8): a coordinate moves only to a new, non-negative value.
    bool bChanged = false;
    if (NewScrollX != ScrollX && NewScrollX >= 0)
    {
        ScrollX = NewScrollX;
        bChanged = true;
    }
    if (NewScrollY != ScrollY && NewScrollY >= 0)
        ScrollY = NewScrollY;
    else if (!bChanged)
        return;

    Trc.set_origin_v(-ScrollY);
    Trc.set_origin_h(-ScrollX);
    bLayoutDirty = bLayoutDirty != 0 ? 1 : 0;
    bSurfaceDirty = 1;
    if (ScrollBarV != nullptr)
        ScrollBarV->SetValue(static_cast<float>(ScrollY));
    if (ScrollBarH != nullptr)
        ScrollBarH->SetValue(static_cast<float>(ScrollX));
}

void BaseRichTextWin::GetScrollOrigin(int* OutScrollX, int* OutScrollY)
{
    // FUN_00579970 (RET 8): y as the alignment reports it; mode > 3 leaves it untouched.
    *OutScrollX = ScrollX;
    switch (AlignMode)
    {
    case 0:
        *OutScrollY = ScrollY;
        break;
    case 1:
        *OutScrollY = 0;
        break;
    case 2:
        *OutScrollY = -((PixelHeight - LayoutHeight) / 2);
        break;
    case 3:
        *OutScrollY = LayoutHeight - PixelHeight;
        break;
    default:
        break;
    }
}

void BaseRichTextWin::SetAutoScrollbar(bool bAuto)
{
    // FUN_005799f0.
    bAutoScrollbar = bAuto ? 1 : 0;
    bLayoutDirty = 1;
    bSurfaceDirty = 1;
}

void BaseRichTextWin::ScrollToEnd()
{
    // FUN_00579a10.
    if (LayoutHeight > PixelHeight)
        SetScrollOrigin(ScrollX, LayoutHeight - PixelHeight);
    else
        SetScrollOrigin(ScrollX, 0);
}

void BaseRichTextWin::ScrollToTop()
{
    // FUN_00579a40.
    SetScrollOrigin(ScrollX, 0);
}

void BaseRichTextWin::SetKeepScrolledToEnd(bool bKeep)
{
    // FUN_00579a50.
    bKeepScrolledToEnd = bKeep ? 1 : 0;
}

void BaseRichTextWin::SetAlignMode(int Mode)
{
    // FUN_00579a60: the layout flag is only normalised, the surface flag raised.
    AlignMode = Mode;
    bLayoutDirty = bLayoutDirty != 0 ? 1 : 0;
    bSurfaceDirty = 1;
}

void BaseRichTextWin::SetSkipUpload(bool bSkip)
{
    // FUN_00579a90.
    bSkipUpload = bSkip ? 1 : 0;
}

UiScroll* BaseRichTextWin::CreateScrollBarV()
{
    // FUN_00579aa0. The payload is the UIScroll descriptor (width, height, min, max, page, step,
    // horizontal flag).
    FUiPayloadScrollCycle Payload;
    Payload.Right = ScrollBarWidth;
    Payload.Bottom = NormHeight;
    Payload.Extra0 = 0.0f;
    int const Range = LayoutHeight > PixelHeight ? LayoutHeight : PixelHeight;
    Payload.Extra1 = static_cast<float>(Range);
    Payload.Extra2 = static_cast<float>(PixelHeight);
    Payload.Extra3 = static_cast<float>(PixelHeight / 10);
    Payload.Flag = 0;

    char Name[0x2000];
    int const Serial = G_ScrollVSerial;
    G_ScrollVSerial = Serial + 1;
    sprintf(Name, "ScrollV_%d", Serial);

    UiScroll* const Created =
        static_cast<UiScroll*>(WinFactoryCreate(Name, "UIScroll", this, &Payload));
    if (Created != nullptr)
    {
        // Beside the text: the gap plus the width along the control's x axis.
        Vector Along;
        Along.x = ScrollBarGap + NormWidth;
        Along.y = 0.0f;
        Along.z = 0.0f;
        Vector Offset;
        RichTextMathEngine()->TransformMatrix(Offset, Orientation,
                                              Along);
        Created->SetOffset(Offset.x, Offset.y, Offset.z);
        Created->SetTransform(&Placement);
    }
    return Created;
}

void BaseRichTextWin::ShowScrollBarV(bool bShow)
{
    // FUN_00579bd0 (RET 4).
    if (bShow)
    {
        if (ScrollBarV != nullptr)
            return;
        ScrollBarV = CreateScrollBarV();
        if (ScrollBarV != nullptr)
            ScrollBarV->SetValue(static_cast<float>(ScrollY));
        return;
    }
    if (ScrollBarV != nullptr)
    {
        delete ScrollBarV;
        ScrollBarV = nullptr;
    }
}

float BaseRichTextWin::GetScrollBarGap()
{
    // FUN_00579c30.
    return ScrollBarGap;
}

void BaseRichTextWin::SetAttachOffset(float const* NewOffset)
{
    // FUN_00579c40 (RET 4): move by the change of the offset. x and y stay double between the
    // subtraction and the addition; z is rounded to float in between.
    float const Old[3] = {AttachOffset[0], AttachOffset[1], AttachOffset[2]};
    AttachOffset[0] = NewOffset[0];
    AttachOffset[1] = NewOffset[1];
    AttachOffset[2] = NewOffset[2];
    Vector Moved;
    float const DeltaZ = Position.z - Old[2];
    Moved.x = static_cast<float>(static_cast<double>(Position.x) - Old[0] + AttachOffset[0]);
    Moved.y = static_cast<float>(static_cast<double>(Position.y) - Old[1] + AttachOffset[1]);
    Moved.z = DeltaZ + AttachOffset[2];
    WinControl::SetPosition(&Moved.x);
}

void BaseRichTextWin::SetNormSize(float Width, float Height)
{
    // FUN_00579cc0 (RET 8). No dirty flags: the next Draw's EnsureSurface notices the new size.
    NormHeight = Height;
    NormWidth = Width;
    if (ScrollBarV != nullptr)
    {
        ScrollBarV->SetSize(ScrollBarWidth, Height);
        Transform Beside;
        memset(&Beside, 0, sizeof(Beside));
        Beside.Rotation.E[0][0] = 1.0f;
        Beside.Rotation.E[1][1] = 1.0f;
        Beside.Rotation.E[2][2] = 1.0f;
        Beside.Translation.x = NormWidth;
        Transform Placed;
        RichTextMathEngine()->MulTransform(Placed, Placement,
                                           Beside);
        ScrollBarV->SetTransform(&Placed);
    }
    if (ScrollBarH != nullptr)
    {
        ScrollBarH->SetSize(NormWidth, ScrollBarWidth);
        Transform Below;
        memset(&Below, 0, sizeof(Below));
        Below.Rotation.E[0][0] = 1.0f;
        Below.Rotation.E[1][1] = 1.0f;
        Below.Rotation.E[2][2] = 1.0f;
        Below.Translation.y = NormHeight;
        Transform Placed;
        RichTextMathEngine()->MulTransform(Placed, Placement,
                                           Below);
        ScrollBarH->SetTransform(&Placed);
    }
}

void BaseRichTextWin::EnsureSurface()
{
    // FUN_00579e90.
    int ViewX = 0;
    int ViewY = 0;
    int ViewW = 0;
    int ViewH = 0;
    DALib::Pipeline->GetViewportRect(&ViewX, &ViewY, &ViewW, &ViewH);
    float M[16];
    DALib::Pipeline->GetProjection(M);

    Transform const& Xform = Placement;

    // The top-left corner (0, 0, 0) into world space, then through the projection; each row sums
    // z, y, x, then the constant, as retail.
    Vector Corner0;
    Corner0.x = 0.0f;
    Corner0.y = 0.0f;
    Corner0.z = 0.0f;
    Vector World0;
    RichTextMathEngine()->TransformXform(World0, Xform, Corner0);
    float Clip0[4];
    for (int Row = 0; Row < 4; ++Row)
    {
        double Sum = static_cast<double>(M[Row * 4 + 2]) * World0.z;
        Sum += static_cast<double>(M[Row * 4 + 1]) * World0.y;
        Sum += static_cast<double>(M[Row * 4 + 0]) * World0.x;
        Sum += M[Row * 4 + 3];
        Clip0[Row] = static_cast<float>(Sum);
    }
    if (Clip0[3] == 0.0f)
        return;
    double const InvW0 = 1.0 / Clip0[3];
    float const NdcX0 = static_cast<float>(Clip0[0] * InvW0);
    float const NdcY0 = static_cast<float>(InvW0 * Clip0[1]);

    // The bottom-right corner (w, -h, 0). The w row sums y, x, z here; the other rows z, y, x.
    Vector Corner1;
    Corner1.x = NormWidth;
    Corner1.y = -NormHeight;
    Corner1.z = 0.0f;
    Vector World1;
    RichTextMathEngine()->TransformXform(World1, Xform, Corner1);
    float Clip1[4];
    for (int Row = 0; Row < 3; ++Row)
    {
        double Sum = static_cast<double>(M[Row * 4 + 2]) * World1.z;
        Sum += static_cast<double>(M[Row * 4 + 1]) * World1.y;
        Sum += static_cast<double>(M[Row * 4 + 0]) * World1.x;
        Sum += M[Row * 4 + 3];
        Clip1[Row] = static_cast<float>(Sum);
    }
    {
        double Sum = static_cast<double>(M[13]) * World1.y;
        Sum += static_cast<double>(M[12]) * World1.x;
        Sum += static_cast<double>(M[14]) * World1.z;
        Sum += M[15];
        Clip1[3] = static_cast<float>(Sum);
    }
    if (Clip1[3] == 0.0f)
        return;
    double const InvW1 = 1.0 / Clip1[3];

    WorldRect[0] = World0.x;
    WorldRect[1] = World0.y;
    WorldRect[2] = World1.x;
    WorldRect[3] = World1.y;

    double const NdcX1 = Clip1[0] * InvW1;
    float const NdcY1 = static_cast<float>(InvW1 * Clip1[1]);

    // DAT_005c75dc = 1.0f, DAT_005c75e0 = 0.5f; the conversions truncate.
    double const WidthD = static_cast<double>(ViewW);
    double const XD = static_cast<double>(ViewX);
    float const HeightF = static_cast<float>(ViewH);
    float const YF = static_cast<float>(ViewY);
    int const Left = static_cast<int>((1.0 - NdcX0) * WidthD * 0.5 + XD);
    ScreenRect[0] = Left;
    int const Top = static_cast<int>((static_cast<double>(NdcY0) + 1.0) * HeightF * 0.5 + YF);
    ScreenRect[1] = Top;
    int const Right = static_cast<int>((1.0 - NdcX1) * WidthD * 0.5 + XD);
    ScreenRect[2] = Right;
    int const Bottom = static_cast<int>((static_cast<double>(NdcY1) + 1.0) * HeightF * 0.5 + YF);

    int const OldPixelWidth = PixelWidth;
    int const OldPixelHeight = PixelHeight;
    ScreenRect[3] = Bottom;
    PixelWidth = Right - Left;
    PixelHeight = Bottom - Top;

    int const SurfaceW = RoundSurfaceSize(PixelWidth);
    int const SurfaceH = RoundSurfaceSize(PixelHeight);
    if (SurfaceH > 0 && SurfaceW > 0 && (Surface.Height != SurfaceH || Surface.Width != SurfaceW))
    {
        // The release inline, then the release again once the size differs from the -1 it just
        // wrote (always, since both sizes are positive), as retail.
        ReleaseRichTextSurface(&Surface);
        if (SurfaceW != -1 || SurfaceH != -1)
        {
            ReleaseRichTextSurface(&Surface);
            Surface.Width = SurfaceW;
            Surface.Height = SurfaceH;
            HDC const ScreenDc = GetDC(nullptr);
            Surface.Dc = CreateCompatibleDC(ScreenDc);
            if (Surface.Dc != nullptr)
            {
                Surface.Bitmap = CreateCompatibleBitmap(ScreenDc, Surface.Width, Surface.Height);
                if (Surface.Bitmap != nullptr)
                    Surface.PreviousObject = SelectObject(Surface.Dc, Surface.Bitmap);
            }
            ReleaseDC(nullptr, ScreenDc);
        }

        Pitch = Surface.Width * 2;
        bSurfaceDirty = 1;
        if ((Pitch & 3) != 0)
            Pitch = Pitch - (Pitch & 3) + 4;

        unsigned int const BufferBytes = static_cast<unsigned int>(Pitch * Surface.Height);
        unsigned char* const Buffer = static_cast<unsigned char*>(operator new(BufferBytes));
        memset(Buffer, 0, BufferBytes);

        // Both A1R5G5B5; results not tested.
        unsigned long DstFormat = static_cast<unsigned long>(PixelFormatFromD3D(0));
        unsigned long SrcFormat = static_cast<unsigned long>(PixelFormatFromD3D(0));
        PixelFormatFind(&DstFormat, 0x10, 5, 5, 5, 1);
        PixelFormatFind(&SrcFormat, 0x10, 5, 5, 5, 1);
        FBigImageDesc Desc;
        Desc.Size = 0x2c;
        Desc.Name = "BigImage";
        Desc.Pipeline = DALib::Pipeline;
        Desc.ImageSource = nullptr;
        Desc.SrcFormat = &SrcFormat;
        Desc.DstFormat = &DstFormat;
        Desc.Pixels = Buffer;
        Desc.Width = static_cast<unsigned long>(Surface.Width);
        Desc.Height = static_cast<unsigned long>(Surface.Height);
        Desc.Pitch = static_cast<unsigned long>(Pitch);
        Desc.DrawTile = &ScreenQuadDrawBigImageTile;

        void* Instance = nullptr;
        if (DALib::Dacom->CreateInstance(reinterpret_cast<AGGDESC*>(&Desc), &Instance) == 0)
        {
            if (Image != nullptr)
            {
                Image->Release();
                Image = nullptr;
            }
            if (static_cast<IDAComponent*>(Instance)->QueryInterface(
                    Iids::BigImage2, reinterpret_cast<void**>(&Image)) != 0)
                FDUMP(kFdumpError, "%s(%d) : ERROR:General:%s", kRichDisplayCpp, 0x3c7,
                      "Failed to query IBigImage2 interface.\n");
        }
        else
        {
            FDUMP(kFdumpError, "%s(%d) : ERROR:General:%s", kRichDisplayCpp, 0x3cc,
                  "Failed to create BigImage instance.\n");
        }
        operator delete(Buffer);
        if (Instance != nullptr)
        {
            static_cast<IDAComponent*>(Instance)->Release();
            Instance = nullptr;
        }
    }

    if (OldPixelWidth != PixelWidth || OldPixelHeight != PixelHeight)
    {
        bLayoutDirty = 1;
        bSurfaceDirty = 1;
    }
    Trc.bind_to_dc(Surface.Dc);
}

DeviceTRC* BaseRichTextWin::GetDeviceTRC()
{
    // FUN_0057a610.
    return &Trc;
}

void BaseRichTextWin::MarkDirty(bool bSurface, bool bLayout)
{
    // FUN_0057a620 (RET 8): a dirty layout always dirties the surface.
    bLayoutDirty = (bLayoutDirty != 0 || bLayout) ? 1 : 0;
    if (bLayoutDirty != 0)
    {
        bSurfaceDirty = 1;
        return;
    }
    bSurfaceDirty = (bSurfaceDirty != 0 || bSurface) ? 1 : 0;
}

void BaseRichTextWin::RebuildLayoutIfDirty()
{
    // FUN_0057a680.
    if (Surface.Dc == nullptr || bLayoutDirty == 0)
        return;

    Trc.reset_to_default();
    Trc.set_pos_v(0);
    Trc.set_pos_h(0);
    RdlClear(LayoutList);
    LayoutHeight = PixelHeight;
    Relayout();

    if (bAutoScrollbar != 0)
    {
        if (ScrollBarV != nullptr)
        {
            if (LayoutHeight <= PixelHeight)
            {
                delete ScrollBarV;
                ScrollBarV = nullptr;
            }
        }
        else if (LayoutHeight > PixelHeight)
        {
            ScrollBarV = CreateScrollBarV();
            if (ScrollBarV != nullptr)
                ScrollBarV->SetValue(static_cast<float>(ScrollY));
        }
    }

    if (ScrollBarV != nullptr)
    {
        int const Range = LayoutHeight > PixelHeight ? LayoutHeight : PixelHeight;
        ScrollBarV->SetRange(0.0f, static_cast<float>(Range));
    }

    if (bKeepScrolledToEnd != 0)
    {
        // DAT_005d3e4c = 60.0f: follow the end while the view is within 60 pixels of it.
        int const Overflow = LayoutHeight - PixelHeight;
        if (static_cast<double>(Overflow) - ScrollY <= 60.0)
        {
            if (LayoutHeight > PixelHeight)
                SetScrollOrigin(ScrollX, Overflow);
            else
                SetScrollOrigin(ScrollX, 0);
        }
    }

    bLayoutDirty = 0;
}

// ---------------------------------------------------------------------------------------------
// RichTextWin
// ---------------------------------------------------------------------------------------------

RichTextWin::RichTextWin()
{
    // FUN_0057b1b0.
    bSurfaceDirty = 0;
    bLayoutDirty = 0;
    LayoutFlag = 1;
}

RichTextWin::~RichTextWin()
{
    // FUN_0057b260.
}

bool RichTextWin::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_0057b5a0 (RET 8): BaseRichTextWin::Create inlined, then the Content emptied without
    // touching the dirty flags.
    bool const bResult = Win3D::Create(NewParent, nullptr);
    WorldRect[3] = 0.0f;
    WorldRect[2] = 0.0f;
    WorldRect[1] = 0.0f;
    WorldRect[0] = 0.0f;
    PixelHeight = 0;
    PixelWidth = 0;
    ScreenRect[3] = 0;
    ScreenRect[1] = 0;
    ScreenRect[2] = 0;
    ScreenRect[0] = 0;
    AttachOffset[2] = 0.0f;
    AttachOffset[1] = 0.0f;
    AttachOffset[0] = 0.0f;
    bLayoutDirty = 1;
    bSurfaceDirty = 1;
    NormWidth = 0.4f;
    NormHeight = 0.3f;
    EnsureSurface();
    // The dead DebugWindow creation (FUN_0057b700 registers its class) is not ported.
    RdlClear(Content);
    return bResult;
}

void RichTextWin::Update()
{
    // FUN_0057b530: the source list animated by the capped frame time rounded to float.
    Content.update(static_cast<float>(GetClientFrameTimeCapped()));
    FeedToolTipIfHovered();
    Win3D::Update();
}

bool RichTextWin::Relayout()
{
    // LAB_0057b320.
    LayoutRichText(Trc, Content, PixelWidth - 4, LayoutList, 2, 0, false, &LayoutHeight,
                   LayoutFlag != 0);
    return true;
}

void RichTextWin::CreateWithText(Win3D* NewParent, wchar_t const* Text, float Width,
                                 float Height)
{
    // FUN_0057b7b0 (RET 0x10). No dirty flags beyond what Create sets.
    Create(NewParent, nullptr);
    NormWidth = Width;
    NormHeight = Height;
    TextNode Empty;
    RDLNode* const Clone = Empty.Clone();
    RdlAppendNode(Content, Clone->Clone());
    delete Clone;
    // The tail cell's node (sentinel[1][2]) gets the text.
    void** const Sentinel = (void**)Content.NodeList;
    RDLNode* const Back = static_cast<RDLNode*>(static_cast<void**>(Sentinel[1])[2]);
    static_cast<TextNode*>(Back)->set_text(Text, -1);
}

void RichTextWin::ClearContent()
{
    // FUN_0057b370: only a non-empty list raises the dirty flags.
    if (Content.Count == 0)
        return;
    RdlClear(Content);
    bLayoutDirty = 1;
    bSurfaceDirty = 1;
}

void RichTextWin::AppendContent(RenderDisplayList const& Source)
{
    // FUN_0057b3e0 (RET 4).
    if (Source.Count == 0)
        return;
    void** const Sentinel = (void**)Source.NodeList;
    for (void** Cell = static_cast<void**>(Sentinel[0]); Cell != Sentinel;
         Cell = static_cast<void**>(Cell[0]))
    {
        RdlAppendNode(Content, static_cast<RDLNode*>(Cell[2])->Clone());
    }
    bLayoutDirty = 1;
    bSurfaceDirty = 1;
}

void RichTextWin::AppendNode(RDLNode const& Node)
{
    // FUN_0057b470 (RET 4): the clone is cloned again into the new element, the first clone
    // deleted.
    RDLNode* const Temp = Node.Clone();
    RdlAppendNode(Content, Temp->Clone());
    delete Temp;
    bLayoutDirty = 1;
    bSurfaceDirty = 1;
}

void RichTextWin::SetLayoutFlag(bool bFlag)
{
    // FUN_0057b520.
    LayoutFlag = bFlag ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------
// RichTextEditWin
// ---------------------------------------------------------------------------------------------

namespace
{
    // DAT_0067c40c -- the default text attributes (static initializer LAB_0057b8b0: byte 0 = 0,
    // bytes 1..3 = 0xff).
    TextRenderAttributes const G_RichEditDefaultAttr = {0xffffff00u};

    // The clone-into-list idiom every RDL append here uses (FUN_0046c800 / FUN_0046bb00, or the
    // same steps inline).
    void AppendNodeClone(RenderDisplayList& List, RDLNode const& Node)
    {
        RDLNode* const Temp = Node.Clone();
        RdlAppendNode(List, Temp->Clone());
        delete Temp;
    }
}

RichTextEditWin::RichTextEditWin()
    // FUN_0057b8d0.
    : Caret(0)
    , Field_0x4a0(0)
    , Field_0x4a4(0)
    , Field_0x4a8(4)
    , bCaretVisible(1)
    , CaretTimer(0.5f)                       // DAT_005e4564
    , CaretPeriod(0.5f)                      // DAT_005e4564
    , CaretHeightScale(1.0f)                 // DAT_005e4568
    , bEditable(1)
    , CurrentAttr(G_RichEditDefaultAttr)
    , FormatMask(-1)
    , bFixedStyle(0)
    , bPasswordMode(0)
    , CaseMode(0)
    , MaxLength(0)
    , ReplacementChar(L'_')
    , bFilterIsAllowList(0)
    , bImeEnabled(1)
    , ImeBox(nullptr)
{
    CaretColor[0] = 0xff;
    CaretColor[1] = 0xff;
    CaretColor[2] = 0xff;
    CaretColor[3] = 0xff;

    // An IME box only while an IMEFocus singleton exists.
    if (IMEFocus::GetSingleton() != nullptr)
        ImeBox = new FLImeInputBox(this);
}

RichTextEditWin::~RichTextEditWin()
{
    // FUN_0057bbf0.
    if (ImeBox != nullptr)
        delete ImeBox;
}

void RichTextEditWin::Update()
{
    // FUN_0057c360.
    Content.update(static_cast<float>(GetClientFrameTimeCapped()));
    FeedToolTipIfHovered();
    Win3D::Update();

    // The caret blink, on a second read of the frame time. The double value is tested before
    // it is rounded: only <= 0 goes on (unordered leaves); then the shown flag flips and a value
    // still below zero gets the period added until it is not.
    double Remaining = static_cast<double>(CaretTimer) - GetClientFrameTimeCapped();
    CaretTimer = static_cast<float>(Remaining);
    if (!(Remaining <= 0.0))
        return;
    bCaretVisible = (bCaretVisible == 0) ? 1 : 0;
    if (!(Remaining < 0.0))
        return;
    double const Period = static_cast<double>(CaretPeriod);
    do
    {
        Remaining += Period;
    } while (Remaining < 0.0);
    CaretTimer = static_cast<float>(Remaining);
}

void RichTextEditWin::Draw()
{
    // FUN_0057c440.
    BaseRichTextWin::Draw();

    // The IME box gets the input size (the max length, or 0x50 when it is not positive) and the
    // screen rectangle.
    if (ImeBox != nullptr)
    {
        RECT Rect;
        Rect.left = ScreenRect[0];
        Rect.top = ScreenRect[1];
        Rect.right = ScreenRect[2];
        Rect.bottom = ScreenRect[3];
        int BufferSize = MaxLength;
        if (!(BufferSize > 0))
            BufferSize = 0x50;
        ImeBox->SetInputBufferSize(static_cast<DWORD>(BufferSize));
        ImeBox->Resize(&Rect);
    }

    // The caret only while shown, holding the input focus and in its visible blink phase.
    if ((Flags & 2) == 0)
        return;
    if (HudGetInputFocus() != AsHudCmdHandler())
        return;
    if (bCaretVisible == 0)
        return;

    // Retail's glMatrixMode(0x1701) here is an empty stub.
    Win3DSetDepthWrite(0);
    Win3DDisable(kWin3DDepthTest);
    Win3DDisable(kWin3DCullFace);
    Win3DDisable(kWin3DBlend);
    Win3DPushProjection();
    Win3DSetOrtho2D();

    // The laid-out position of the element after the prefix list and Caret characters.
    Trc.reset_to_default();
    Trc.set_pos_v(0);
    Trc.set_pos_h(0);
    float PosH = 0.0f;
    float PosV = 0.0f;
    float Depth = 0.0f;
    int const Index = Prefix.get_element_count() + Caret;
    if (LayoutList.get_element_pos(Trc, Index, PosH, PosV, Depth))
    {
        // Deviation: for a mode above 3 retail reads an unwritten stack local (SetAlignMode does
        // not range-check); the port reads 0.
        int OffsetV = 0;
        switch (AlignMode)
        {
        case 0:
            OffsetV = ScrollY;
            break;
        case 1:
            OffsetV = 0;
            break;
        case 2:
            OffsetV = -((PixelHeight - LayoutHeight) / 2);
            break;
        case 3:
            OffsetV = LayoutHeight - PixelHeight;
            break;
        default:
            break;
        }

        // Retail subtracts 2.0 twice from the scaled height. Each is stored back as float.
        PosH = static_cast<float>(static_cast<double>(PosH) - static_cast<double>(ScrollX));
        Depth = static_cast<float>(static_cast<double>(Depth) * static_cast<double>(CaretHeightScale) -
                                   (2.0 + 2.0));
        PosV = static_cast<float>(static_cast<double>(PosV) - static_cast<double>(OffsetV) + 2.0);

        // Inside the window; every unordered compare skips.
        float const HeightF = static_cast<float>(PixelHeight);
        if (PosH >= 0.0f && static_cast<double>(PixelWidth) > static_cast<double>(PosH) &&
            PosV < HeightF && -Depth < PosV)
        {
            // Clip the line to the window.
            if (PosV < 0.0f)
            {
                Depth = static_cast<float>(static_cast<double>(PosV) + static_cast<double>(Depth));
                PosV = 0.0f;
            }
            else if (!(static_cast<double>(PosV) + static_cast<double>(Depth) < static_cast<double>(HeightF)))
            {
                Depth = static_cast<float>(static_cast<double>(HeightF) - static_cast<double>(PosV));
            }

            PosH = static_cast<float>(static_cast<double>(ScreenRect[0]) + static_cast<double>(PosH));
            PosV = static_cast<float>(static_cast<double>(ScreenRect[1]) + static_cast<double>(PosV));

            Win3DColor4ubv(CaretColor);
            Win3DDisable(kWin3DTexture2D);
            Win3DBegin(kWin3DLines);
            Win3DVertex3f(PosH, PosV, 0.0f);
            Win3DVertex3f(PosH, static_cast<float>(static_cast<double>(PosV) + static_cast<double>(Depth)),
                          0.0f);
            Win3DEnd();
        }
    }

    Win3DPopProjection();
    Win3DEnable(kWin3DDepthTest);
    Win3DSetDepthWrite(1);
}

bool RichTextEditWin::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_0057d1d0.
    Win3D* const Up = Parent;
    if (Up != nullptr)
        return Up->MouseEvent(Event);
    return WinControl::MouseEvent(Event);
}

void RichTextEditWin::CreateSized(Win3D* NewParent, float Width, float Height)
{
    // FUN_0057c330 (RET 0xc).
    Create(NewParent, nullptr);
    NormWidth = Width;
    NormHeight = Height;
}

bool RichTextEditWin::OnKeyDown(FHudCmdRecord const* Record)
{
    // FUN_0057c9b0 (RET 4).
    if ((Flags & 1) == 0)
        return false;

    // A key-up leaves at once, without the IME forward.
    bool bResult = false;
    if (Record->bKeyDown == 0)
        return bResult;

    // Nothing is edited unless +0x4a0 == +0x4a4.
    if (Field_0x4a0 == Field_0x4a4)
    {
        unsigned const Key = Record->Key;
        bResult = true;
        switch (Key)
        {
        case 0x08:
            // Backspace: one to the left, then the cell there erased.
            if (Caret > 0)
            {
                SetCaret(Caret - 1);
                int const At = ClampedCellIndex(Caret);
                Cells.erase(Cells.begin() + At);
                RebuildDisplay();
            }
            break;
        case 0x09:
        case 0x2d:
            // Tab / Insert: false; with an IME box it returns before the forward.
            if (ImeBox != nullptr)
                return false;
            bResult = false;
            break;
        case 0x0d:
            // Enter: the IME box sees the key, the parent gets 0xf004.
            if (ImeBox != nullptr)
                ImeBox->HandleKey(Key);
            if (Parent != nullptr)
                Parent->ProcessMessage(kWinMsgEnter, static_cast<unsigned>(reinterpret_cast<uintptr_t>(this)), 0u);
            return true;
        case 0x1b:
            // Escape: the same with 0xf003.
            if (ImeBox != nullptr)
                ImeBox->HandleKey(Key);
            if (Parent != nullptr)
                Parent->ProcessMessage(kWinMsgEscape, static_cast<unsigned>(reinterpret_cast<uintptr_t>(this)), 0u);
            return true;
        case 0x23:
            // End.
            if (ImeBox != nullptr)
                return false;
            SetCaret(static_cast<int>(Cells.size()));
            break;
        case 0x24:
            // Home.
            if (ImeBox != nullptr)
                return false;
            SetCaret(0);
            break;
        case 0x25:
            // Left.
            if (ImeBox != nullptr)
                return false;
            SetCaret(Caret - 1);
            break;
        case 0x27:
            // Right.
            if (ImeBox != nullptr)
                return false;
            SetCaret(Caret + 1);
            break;
        case 0x2e:
            // Delete: the cell at the caret erased, the caret re-applied.
            if (!Cells.empty())
            {
                int const At = ClampedCellIndex(Caret);
                if (At != static_cast<int>(Cells.size()))
                {
                    Cells.erase(Cells.begin() + At);
                    SetCaret(Caret);
                    RebuildDisplay();
                }
            }
            break;
        case 0x42:
            // Ctrl+B: bit 0 of the attributes toggled when allowed.
            if (bFixedStyle == 0 && (FormatMask & 1) != 0 && (Record->Modifiers & 0xc) != 0)
                CurrentAttr.Packed ^= 0x01u;
            else
                bResult = false;
            break;
        case 0x49:
            // Ctrl+I: bit 1.
            if (bFixedStyle == 0 && (FormatMask & 2) != 0 && (Record->Modifiers & 0xc) != 0)
                CurrentAttr.Packed ^= 0x02u;
            else
                bResult = false;
            break;
        case 0x55:
            // Ctrl+U: bit 2.
            if (bFixedStyle == 0 && (FormatMask & 4) != 0 && (Record->Modifiers & 0xc) != 0)
                CurrentAttr.Packed ^= 0x04u;
            else
                bResult = false;
            break;
        case 0x6b:
            // Ctrl+numpad plus: the size field (bits 3..7) up one, capped at 0xf8.
            if (bFixedStyle == 0 && (FormatMask & 0xf8) != 0 && (Record->Modifiers & 0xc) != 0)
            {
                unsigned char const Low = static_cast<unsigned char>(CurrentAttr.Packed & 0xffu);
                if ((Low & 0xf8u) < 0xf8u)
                {
                    unsigned char const NewLow =
                        static_cast<unsigned char>((((Low & 0xf8u) + 8u) & 0xffu) ^ (Low & 0x07u));
                    CurrentAttr.Packed = (CurrentAttr.Packed & ~0xffu) | NewLow;
                }
            }
            else
            {
                bResult = false;
            }
            break;
        case 0x6d:
            // Ctrl+numpad minus: the size field down, stopping at 0. Retail's arithmetic (minus
            // 1, xor with the inverted low bits) is kept as is.
            if (bFixedStyle == 0 && (FormatMask & 0xf8) != 0 && (Record->Modifiers & 0xc) != 0)
            {
                unsigned char const Low = static_cast<unsigned char>(CurrentAttr.Packed & 0xffu);
                if ((Low & 0xf8u) != 0)
                {
                    unsigned char const NewLow = static_cast<unsigned char>(
                        (((Low & 0xf8u) - 1u) & 0xffu) ^ (static_cast<unsigned>(~Low) & 0x07u));
                    CurrentAttr.Packed = (CurrentAttr.Packed & ~0xffu) | NewLow;
                }
            }
            else
            {
                bResult = false;
            }
            break;
        default:
            bResult = false;
            break;
        }
    }

    // The IME box sees every key-down that reaches here.
    if (ImeBox != nullptr)
        ImeBox->HandleKey(Record->Key);
    return bResult;
}

bool RichTextEditWin::OnChar(FHudCmdRecord const* Record)
{
    // LAB_0057ceb0 (RET 4).
    if ((Flags & 1) == 0)
        return false;

    // The character is the record's wParam word; a control character is consumed and nothing
    // else happens.
    unsigned Ch = static_cast<unsigned short>(Record->Arg10);
    if (iswcntrl(static_cast<wint_t>(Ch)))
        return true;

    if (CaseMode == 1)
        Ch = towupper(static_cast<wint_t>(Ch));
    else if (CaseMode == 2)
        Ch = towlower(static_cast<wint_t>(Ch));

    // An allow list replaces what it does not hold, a deny list what it does. Only the low word
    // is replaced.
    wchar_t const Lookup = static_cast<wchar_t>(Ch & 0xffffu);
    bool const bInSet = FilterChars.find(Lookup) != FilterChars.end();
    if (bFilterIsAllowList != 0 ? !bInSet : bInSet)
        Ch = (Ch & 0xffff0000u) | static_cast<unsigned short>(ReplacementChar);

    // Inserted at the caret with the current attributes, unless +0x4a0 != +0x4a4 or the text is
    // at its (non-zero) maximum length.
    if (Field_0x4a0 == Field_0x4a4 &&
        !(MaxLength != 0 && static_cast<unsigned>(Cells.size()) >= static_cast<unsigned>(MaxLength)))
    {
        FRichEditCell Cell;
        Cell.Ch = static_cast<wchar_t>(Ch & 0xffffu);
        Cell.Attr = CurrentAttr;
        int const At = ClampedCellIndex(Caret);
        Cells.insert(Cells.begin() + At, Cell);
        SetCaret(Caret + 1);
        RebuildDisplay();
    }

    // The IME box sees only the character's low byte.
    if (ImeBox != nullptr)
        ImeBox->HandleMultiByteChar(static_cast<char>(Ch & 0xffu));
    return true;
}

void RichTextEditWin::OnActivated()
{
    // FUN_00401e90.
}

void RichTextEditWin::OnDeactivating()
{
    // FUN_00401e90.
}

bool RichTextEditWin::OnEvent(unsigned MsgId)
{
    // FUN_0057c930 (RET 4, this = the +0x498 face).
    if (MsgId == USER_KEY)
        return OnKeyDown(HudCommandRecord());
    if (MsgId == USER_CHAR)
        return OnChar(HudCommandRecord());
    if (MsgId == USER_CANCEL)
    {
        // With a raw window message in the record, USER_CANCEL is posted again and the event
        // consumed.
        if (HudCommandRecord()->WindowMessage != 0)
        {
            HudPostCommand(USER_CANCEL, 0);
            return true;
        }
    }
    return false;
}

void RichTextEditWin::SetMaxLength(int MaxChars)
{
    // FUN_0057bdb0 (RET 4)
    if (MaxChars >= 0)
        MaxLength = MaxChars;
}

void RichTextEditWin::SetFixedStyle(bool bFixed)
{
    // FUN_0057bdd0 (RET 4)
    unsigned char const Value = bFixed ? 1 : 0;
    bool const bChanged = Value != bFixedStyle;
    bFixedStyle = Value;
    if (bChanged)
        RebuildDisplay();
}

void RichTextEditWin::SetPasswordMode(bool bPassword)
{
    // FUN_0057be00 (RET 4)
    unsigned char const Value = bPassword ? 1 : 0;
    bool const bChanged = Value != bPasswordMode;
    bPasswordMode = Value;
    if (bChanged)
        RebuildDisplay();
}

void RichTextEditWin::SetImeEnabled(bool bEnabled)
{
    // FUN_0057be30 (RET 4).
    bImeEnabled = bEnabled ? 1 : 0;
    if (bEnabled)
    {
        // A box only when there is none and an IMEFocus singleton exists; then ClearText's body
        // inline, which rebuilds the box once more.
        if (ImeBox != nullptr)
            return;
        if (IMEFocus::GetSingleton() == nullptr)
            return;
        ImeBox = new FLImeInputBox(this);
        ClearText();
        return;
    }
    if (ImeBox != nullptr)
    {
        delete ImeBox;
        ImeBox = nullptr;
    }
}

void RichTextEditWin::SetEditable(bool bNewEditable)
{
    // FUN_0057bfa0 (RET 4). The 0xf (USER_CANCEL) registration follows only while this box is
    // the input focus.
    unsigned char const Value = bNewEditable ? 1 : 0;
    if (bEditable == Value)
        return;
    bEditable = Value;
    if (HudGetInputFocus() != AsHudCmdHandler())
        return;
    if (bEditable != 0)
        UnregisterEvent(USER_CANCEL);
    else
        RegisterEvent(USER_CANCEL);
}

void RichTextEditWin::ClearText()
{
    // FUN_0057bff0. An existing IME box is deleted and built again.
    Cells.clear();
    Field_0x4a4 = 0;
    Field_0x4a0 = 0;
    Caret = 0;
    RebuildDisplay();
    if (ImeBox != nullptr)
    {
        delete ImeBox;
        ImeBox = new FLImeInputBox(this);
    }
}

void RichTextEditWin::SetText(wchar_t const* Text, TextRenderAttributes const* Attr)
{
    // FUN_0057c0e0 (RET 8).
    Cells.clear();
    TextRenderAttributes const RunAttr = (Attr != nullptr) ? *Attr : CurrentAttr;

    // With a non-zero maximum, stop when the count reaches it (unsigned compare). Unlike OnChar,
    // any case mode other than 0 and 1 lower-cases.
    wchar_t const* P = Text;
    while (*P != L'\0')
    {
        if (MaxLength != 0 && static_cast<unsigned>(Cells.size()) >= static_cast<unsigned>(MaxLength))
            break;
        wchar_t Ch;
        if (CaseMode == 0)
            Ch = *P;
        else if (CaseMode == 1)
            Ch = static_cast<wchar_t>(towupper(static_cast<wint_t>(*P)));
        else
            Ch = static_cast<wchar_t>(towlower(static_cast<wint_t>(*P)));
        bool const bInSet = FilterChars.find(Ch) != FilterChars.end();
        if (bFilterIsAllowList != 0 ? !bInSet : bInSet)
            Ch = ReplacementChar;
        FRichEditCell Cell;
        Cell.Ch = Ch;
        Cell.Attr = RunAttr;
        Cells.push_back(Cell);
        ++P;
    }

    int const Count = static_cast<int>(Cells.size());
    Field_0x4a4 = Count;
    Field_0x4a0 = Count;
    Caret = Count;
    RebuildDisplay();
}

int RichTextEditWin::GetText(wchar_t* Buffer, int MaxChars)
{
    // FUN_0057c2c0 (RET 8).
    int const Count = static_cast<int>(Cells.size());

    if (MaxChars <= 0)
        return Count;

    int Limit = Count;
    if (Limit > MaxChars - 1)
        Limit = MaxChars - 1;
    int Index = 0;
    while (Index < Limit)
    {
        Buffer[Index] = Cells[Index].Ch;
        ++Index;
    }
    Buffer[Index] = L'\0';
    return Index + 1;
}

void RichTextEditWin::Activate()
{
    // FUN_0057c750.
    Flags = static_cast<unsigned char>(Flags | kWin3DActive | kWin3DVisible);
    HudPushInputFocus(AsHudCmdHandler());
    RegisterEvent(USER_KEY);
    RegisterEvent(USER_CHAR);
    if (bEditable != 0)
        UnregisterEvent(USER_CANCEL);
    else
        RegisterEvent(USER_CANCEL);
    if (ImeBox != nullptr)
    {
        IMEFocus* const Focus = IMEFocus::GetSingleton();
        if (Focus != nullptr)
            Focus->SetFocus(ImeBox, false);
    }
    OnActivated();
}

void RichTextEditWin::Deactivate(bool bHide)
{
    // FUN_0057c7d0 (RET 4). The IME focus is cleared only while the IME is enabled.
    OnDeactivating();
    UnregisterEvent(USER_KEY);
    UnregisterEvent(USER_CHAR);
    UnregisterEvent(USER_CANCEL);
    HudPopInputFocus(AsHudCmdHandler());
    if (bHide)
        Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));
    if (bImeEnabled != 0)
    {
        IMEFocus* const Focus = IMEFocus::GetSingleton();
        if (Focus != nullptr)
            Focus->SetFocus(nullptr, false);
    }
}

void RichTextEditWin::SetCaseMode(int Mode)
{
    // FUN_0057c840 (RET 4)
    CaseMode = Mode;
}

void RichTextEditWin::SetFilterIsAllowList(bool bAllowList)
{
    // FUN_0057c850 (RET 4)
    bFilterIsAllowList = bAllowList ? 1 : 0;
}

void RichTextEditWin::AddFilterChars(wchar_t const* Chars, int Count)
{
    // FUN_0057c860 (RET 8): from the last character back.
    if (Chars == nullptr || Count == 0)
        return;
    wchar_t const* P = Chars + Count;
    int Remaining = Count;
    do
    {
        --P;
        FilterChars.insert(*P);
        --Remaining;
    } while (Remaining != 0);
}

void RichTextEditWin::SetReplacementChar(wchar_t Ch)
{
    // FUN_0057c920 (RET 4)
    ReplacementChar = Ch;
}

void RichTextEditWin::RebuildDisplay()
{
    // FUN_0057d330.
    if (Content.Count != 0)
    {
        RdlClear(Content);
        MarkDirty(true, true);
    }

    {
        RDLRefNode const Ref(Prefix);
        AppendNodeClone(Content, Ref);
        MarkDirty(true, true);
    }
    {
        RDLRefNode const Ref(TextDisplay);
        AppendNodeClone(Content, Ref);
        MarkDirty(true, true);
    }

    RdlClear(TextDisplay);
    if (Cells.empty())
        return;

    // The first cell's attributes open the run, as a reference to the fixed-style list or as a
    // TRANode.
    TextRenderAttributes RunAttr = Cells[0].Attr;
    if (bFixedStyle != 0)
    {
        RDLRefNode const Ref(FixedStyle);
        AppendNodeClone(TextDisplay, Ref);
    }
    else
    {
        TRANode const Node(RunAttr, 0xffffffffu, 0u);
        AppendNodeClone(TextDisplay, Node);
    }

    // Without a fixed style, every change of attributes closes the run before it and opens a
    // new TRANode. The count is re-read every pass.
    int Start = 0;
    if (bFixedStyle == 0)
    {
        int Index = 0;
        while (!Cells.empty() && static_cast<unsigned>(Index) < static_cast<unsigned>(Cells.size()))
        {
            if (Cells[Index].Attr.Packed != RunAttr.Packed)
            {
                AppendTextRun(Start, Index - Start);
                RunAttr = Cells[Index].Attr;
                TRANode const Node(RunAttr, 0xffffffffu, 0u);
                AppendNodeClone(TextDisplay, Node);
                Start = Index;
            }
            ++Index;
        }
    }

    if (!Cells.empty() && static_cast<unsigned>(Start) < static_cast<unsigned>(Cells.size()))
        AppendTextRun(Start, static_cast<int>(Cells.size()) - Start);
}

void RichTextEditWin::SetCaret(int Position)
{
    // FUN_0057d6a0 (RET 4).
    Caret = Position;
    if (Position < 0)
    {
        Caret = 0;
    }
    else if (static_cast<unsigned>(Position) > static_cast<unsigned>(Cells.size()))
    {
        Caret = static_cast<int>(Cells.size());
    }

    // The current attributes follow the cell before the caret; at 0, the first cell.
    if (Cells.empty())
        return;
    int const Count = static_cast<int>(Cells.size());
    if (Caret == Count)
        CurrentAttr = Cells[Count - 1].Attr;
    else if (Caret > 0)
        CurrentAttr = Cells[Caret - 1].Attr;
    else
        CurrentAttr = Cells[0].Attr;
}

void RichTextEditWin::AppendTextRun(int Start, int Count)
{
    // FUN_0057d1f0 (RET 8). Retail uses an _alloca buffer.
    if (Count <= 0)
        return;
    std::vector<wchar_t> Buffer(static_cast<size_t>(Count) + 1);
    if (bPasswordMode != 0)
    {
        int Index = 0;
        while (Index < Count)
        {
            Buffer[Index] = L'*';
            ++Index;
        }
    }
    else
    {
        int Index = 0;
        while (Index < Count)
        {
            Buffer[Index] = Cells[Start + Index].Ch;
            ++Index;
        }
    }
    Buffer[Count] = L'\0';
    TextNode const Node(Buffer.data(), -1);
    AppendNodeClone(TextDisplay, Node);
}

int RichTextEditWin::ClampedCellIndex(int Position) const
{
    // begin() advanced while (it != end && n > 0).
    int const Count = static_cast<int>(Cells.size());
    int Index = 0;
    int Remaining = Position;
    while (Index != Count && Remaining > 0)
    {
        ++Index;
        --Remaining;
    }
    return Index;
}

// DAT_0067c440 (static init LAB_00579320, atexit LAB_00579340); Create FUN_0057ecc0,
// operator new(0x498).
WINFACTORY<RichTextWin> G_RichTextWinFactory("RichTextWin");

// DAT_0067c414 (static init LAB_00579350, atexit LAB_00579370); Create FUN_0057ee20,
// operator new(0x530).
WINFACTORY<RichTextEditWin> G_RichTextEditWinFactory("RichTextEditWin");
