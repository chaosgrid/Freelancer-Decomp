// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "NeuroNet/nn_tooltip.h"

#include "Common/Fonts/font_atlas.h"
#include "Common/Fonts/font_imp.h"
#include "Core/game_options.h"
#include "Core/globals.h"
#include "Common/Fonts/ids_strings.h"
#include "Common/input_manager.h"
#include "Common/Fonts/menu_fonts_load.h"
#include "Interface/menu_keymap_config.h"
#include "NeuroNet/nn_rollover.h"   // NnRolloverFeedHoverGlobal (FUN_004b62b0)
#include "NeuroNet/nn_ui_pulse.h"
#include "Core/perf_options.h"
#include "Space/Cameras/space_camera.h"
#include "Space/space_letterbox.h"
#include "Interface/win3d.h"              // Win3DGetMouseRay / Win3DScreenToNearPlane / Win3DTransformByMatrix
#include "Interface/win3d_engine_submit.h"

#include "common_csys.h"        // Acquire3DMathEngine (FUN_005b73e0)
#include "x86math.h"            // I3DMathEngine, FVector, FMatrix
#include "Interface/win3d_immediate.h"
#include "Interface/win3d_renderables.h"

#include <cmath>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace
{
    NnToolTipControl* G_ToolTipSingleton = nullptr; // DAT_0067244c
    int G_TipCursorX = 0;                           // DAT_00672450
    int G_TipCursorY = 0;                           // DAT_00672454

    // DAT_00672448 -- this file's 3DMathEngine, FUN_005b73e0 on first use.
    I3DMathEngine* G_ToolTipMath = nullptr;

    struct FProjectedPoint
    {
        float X;
        float Y;
    };

    FProjectedPoint ProjectUiPoint(float const Position[3])
    {
        FProjectedPoint Point;
        float Depth;
        int const Result = AsCamera(GetWin3DSceneCamera())->Project(
            &Point.X, &Point.Y, &Depth, Position);
        if (static_cast<unsigned char>(Result) == 0)
        {
            Point.X = 0.0f;
            Point.Y = 0.0f;
        }
        return Point;
    }
}

NnToolTipControl::NnToolTipControl()
    : bBuilt_(0)
    , bHasText_(0)
    , bFedThisFrame_(0)
    , DelayRemain_(0.0f)
    , DelayAccum_(0.0f)
    , DelayThreshold_(0.25f)
    , OffsetX_(4.0f)
    , OffsetY_(-42.0f)
{
    // Inlined into FUN_004be420 (the +0xf4 record at 0x004be4cc: FUN_004963a0's body). Retail
    // leaves the world position and half extents as operator new returned them; the port
    // zeroes them.
    ToolTipHoverReset(&Hover_);
    InitToolTipDefaults();
}

NnToolTipControl::~NnToolTipControl()
{
    // FUN_004bd690; the member and base destructors do the rest.
    if (G_ToolTipSingleton == this)
        G_ToolTipSingleton = nullptr;
}

void NnToolTipControl::InitToolTipDefaults()
{
    // FUN_004bd770. The flags &= 0xfd after it belongs to the factory FUN_004be420.
    DrawLayer = 10;
    DelayRemain_ = 0.0f;
    DelayAccum_ = 0.0f;
    bBuilt_ = 0;
    bHasText_ = 0;
    bFedThisFrame_ = 0;
    DelayThreshold_ = 0.25f;
    OffsetX_ = 4.0f;
    OffsetY_ = -42.0f;

    // The Font::String was just constructed, so retail's child notify loop walks nothing.
    IFont* const TipFont = FindMenuFontImp("ToolTip");
    if (Text.FontIface != TipFont)
    {
        Text.FontIface = TipFont;
        Text.LayoutGen = 0;
    }
    if (Text.bMultiLine != 1)
    {
        Text.bMultiLine = 1;
        Text.LayoutGen = 0;
    }
}

bool NnToolTipControl::HitTest(float const* /*Ray*/)
{
    // slot 4 FUN_0053f330.
    return false;
}

bool NnToolTipControl::Create(Win3D* NewParent, void* /*Data*/)
{
    // FUN_004bd830 (RET 8).
    Win3DReparent(this, NewParent);
    Win3DRegister(this);
    Flags = static_cast<unsigned char>((Flags & 0xfb) | kWin3DActive | kWin3DVisible);
    InitTextDefaults(); // FUN_005a1c50
    Width = 0.1f;
    Height = 0.1f;
    Flags = static_cast<unsigned char>(Flags & 0xfd);
    DrawSubLayer = 7;
    return true;
}

void NnToolTipControl::RepositionAfterCaption(FToolTipHoverInfo const* Info)
{
    // FUN_004bda70 (thiscall, RET 4). Every camera read is off the Win3D scene camera
    // DAT_0067dbf8.
    FSpaceCamera* const Cam = GetWin3DSceneCamera();

    // The viewport's top-left corner on the near plane (FUN_004bdd40).
    FVector TopLeftLocal;
    Win3DScreenToNearPlane(Cam, TopLeftLocal, static_cast<float>(Cam->Viewport[0]),
                           static_cast<float>(Cam->Viewport[1]));

    // Through this file's own engine (DAT_00672448).
    FMatrix Orientation;
    memcpy(&Orientation, Cam->World, sizeof(Orientation));
    if (G_ToolTipMath == nullptr)
        G_ToolTipMath = Acquire3DMathEngine();
    FVector TopLeft;
    G_ToolTipMath->TransformMatrix(TopLeft, Orientation, TopLeftLocal);

    // The bottom-right corner, inlined with the other operation order, then FUN_00423b90.
    FVector BottomRightLocal;
    BottomRightLocal.X = static_cast<float>(
        (static_cast<double>(Cam->Viewport[2]) - Cam->ScreenCenterX) / Cam->XScale * Cam->ZNear);
    BottomRightLocal.Y = static_cast<float>(
        (static_cast<double>(Cam->Viewport[3]) - Cam->ScreenCenterY) / Cam->YScale * Cam->ZNear);
    BottomRightLocal.Z = -Cam->ZNear;
    memcpy(&Orientation, Cam->World, sizeof(Orientation));
    FVector BottomRight;
    Win3DTransformByMatrix(BottomRight, Orientation, BottomRightLocal);

    float Ray[3];
    Win3DGetMouseRay(Ray); // FUN_0059da70

    // The ray scaled onto z = -1; the reciprocal stays unrounded for all three products.
    double const RayScale = -1.0 / static_cast<double>(Ray[2]);
    float PosX = static_cast<float>(static_cast<double>(Ray[0]) * RayScale);
    float PosY = static_cast<float>(static_cast<double>(Ray[1]) * RayScale);
    float PosZ = static_cast<float>(RayScale * static_cast<double>(Ray[2]));

    // Only the left edge of the top-left corner and the right / bottom of the bottom-right
    // corner are projected.
    float const BoundLeft =
        static_cast<float>(static_cast<double>(TopLeft.X) * (-1.0 / static_cast<double>(TopLeft.Z)));
    double const BoundScale = -1.0 / static_cast<double>(BottomRight.Z);
    float const BoundRight = static_cast<float>(static_cast<double>(BottomRight.X) * BoundScale);
    float const BoundVert = static_cast<float>(static_cast<double>(BottomRight.Y) * BoundScale);

    // A live hover anchors at its world box's corner instead. The reciprocal rounded to a float
    // scales Y and Z; X uses the unrounded one.
    if (Info->bLive != 0)
    {
        double const Scale = -1.0 / Info->WorldPos[2];
        float const StoredScale = static_cast<float>(Scale);
        PosX = static_cast<float>((static_cast<double>(Info->WorldPos[0]) - Info->HalfExtents[0]) * Scale);
        PosY = static_cast<float>((static_cast<double>(Info->WorldPos[1]) - Info->HalfExtents[1]) * StoredScale);
        PosZ = static_cast<float>(static_cast<double>(Info->WorldPos[2]) * StoredScale);
        PosX = static_cast<float>(static_cast<double>(PosX) + 0.004f);
        PosY = static_cast<float>(static_cast<double>(PosY) - 0.03f);
    }

    // The caption's font scale at the CURRENT depth, then its extent.
    FontStringSetScale(&Text, 0.35f, Position[2]);
    int Extent[2] = {0, 0};
    FontStringGetExtent(&Text, Extent);

    // (width + 4), widened as unsigned, over (near/near)*xscale.
    double const ExtentW = static_cast<unsigned>(Extent[0]) + 4u;
    double const Width = ExtentW / ((static_cast<double>(Cam->ZNear) / Cam->ZNear) * Cam->XScale);
    // The clamps are ordered compares: unordered values pass.
    if (PosX + Width > BoundRight)
        PosX = static_cast<float>(BoundRight - Width);
    if (PosX < BoundLeft)
        PosX = static_cast<float>(static_cast<double>(BoundLeft) + 0.004f);

    double const VertBias = static_cast<double>(OffsetY_) * TextScale;
    if (PosY + VertBias < BoundVert)
        PosY = static_cast<float>(PosY - VertBias * 1.5f);

    float const NewPosition[3] = {PosX, PosY, PosZ};
    SetPosition(NewPosition);
}

void NnToolTipControl::Draw()
{
    // LAB_004bdfe0.
    if (GetGameOption(kGameOptInterfaceGroup) == 0)
    {
        return;
    }
    if (GetGameOption(kGameOptDrawTextControls) == 0)
    {
        return;
    }
    if ((Flags & 0x02) == 0)
    {
        return;
    }
    if (IsLetterboxVisible()) // FUN_0041a3e0
    {
        return;
    }

    Win3DFlushBeforeOverlayDraw(); // FUN_0045a9f0

    Win3DSetDepthWrite(0);
    Win3DDisable(kWin3DDepthTest);
    Win3DDisable(kWin3DCullFace);

    FontStringSetScale(&Text, 0.35f, Position[2]);
    Bounds[3] = -Height;
    Bounds[2] = Width;
    Bounds[0] = 0.0f;
    Bounds[1] = 0.0f;
    int Extent[2];
    FontStringGetExtent(&Text, Extent);
    Width = static_cast<float>(static_cast<unsigned>(Extent[0]));
    Height = static_cast<float>(static_cast<unsigned>(Extent[1]));

    FProjectedPoint const Screen = ProjectUiPoint(Position);
    if (FlagE1 != 0)
    {
        float const PadW = Width + 3.0f;
        float const PadH = Height + 2.0f;
        float const X0 = static_cast<float>(std::floor(static_cast<double>(Screen.X)));
        float const Y0 = static_cast<float>(std::floor(static_cast<double>(Screen.Y)));
        float const X1 = X0 + PadW;
        float const Y1 = Y0 + PadH;

        Win3DPushProjection();
        Win3DSetOrtho2D();

        unsigned char FillRgba[4] = {
            static_cast<unsigned char>(ColorD8 & 0xff),
            static_cast<unsigned char>((ColorD8 >> 8) & 0xff),
            static_cast<unsigned char>((ColorD8 >> 16) & 0xff),
            static_cast<unsigned char>((ColorD8 >> 24) & 0xff)};
        Win3DColor4ubv(FillRgba);
        Win3DDisable(kWin3DTexture2D);
        Win3DBegin(kWin3DQuads);
        Win3DVertex3f(X0, Y0, 0.0f);
        Win3DVertex3f(X1, Y0, 0.0f);
        Win3DVertex3f(X1, Y1, 0.0f);
        Win3DVertex3f(X0, Y1, 0.0f);
        Win3DEnd();

        // The outline in DAT_00679bb4.
        Win3DColor4ubv(G_HudChromeColorRgba);
        Win3DBegin(kWin3DLineLoop);
        Win3DVertex3f(X0, Y0, 0.0f);
        Win3DVertex3f(X1, Y0, 0.0f);
        Win3DVertex3f(X1, Y1, 0.0f);
        Win3DVertex3f(X0, Y1, 0.0f);
        Win3DEnd();

        Win3DPopProjection();
    }

    FontAtlasSetDrawColor(ColorIdle); // FUN_00417780

    FFontStringDrawOrigin Origin;
    Origin.X = Screen.X + 1.0f;
    Origin.Y = Screen.Y;
    Origin.Scale = Width;
    Origin.Height = Height;
    Origin.HAlign = 1;
    Origin.VAlign = 0;
    FontStringDrawChildren(&Origin, &Text, nullptr, nullptr);

    Win3DEnable(kWin3DDepthTest);
    Win3DSetDepthWrite(1);
}

void NnToolTipControl::ApplyHover(FToolTipHoverInfo const* Info)
{
    // FUN_004bd880 (thiscall, RET 4). No test of Info. Only the record's live fields; the
    // caption, Font::String and AuxField of Hover_ stay untouched.
    Hover_.Ids = Info->Ids;
    Hover_.IdsAlt = Info->IdsAlt;
    Hover_.WorldPos[0] = Info->WorldPos[0];
    Hover_.WorldPos[1] = Info->WorldPos[1];
    Hover_.WorldPos[2] = Info->WorldPos[2];
    Hover_.HalfExtents[0] = Info->HalfExtents[0];
    Hover_.HalfExtents[1] = Info->HalfExtents[1];
    Hover_.HalfExtents[2] = Info->HalfExtents[2];
    Hover_.bLive = Info->bLive;

    // 0x004bd8dc..0x004bd949: three byte-built colour dwords into +0xd8 / +0xd4 / +0xcc, the
    // two flag bytes +0xe1 / +0xe0 = 1 and the text scale +0xb0.
    ColorD8 = 0xFF706143u; // bytes 43 61 70 ff
    ColorD4 = 0x19e0c387u; // bytes 87 c3 e0 19
    ColorIdle = 0xFFFFF7B9u; // bytes b9 f7 ff ff
    TextScale = 0.0011f;
    FlagE0 = 1;
    FlagE1 = 1;

    wchar_t Scratch[0x100];
    wchar_t Resolved[0x800];
    if (Info->CaptionW == nullptr)
    {
        // The stored IdsAlt with a capacity of 0x80; on a miss the scratch is emptied, not
        // blanked.
        unsigned const N =
            GetIdsStringWWithRawFallback(static_cast<unsigned>(Hover_.IdsAlt), Scratch, 0x80);
        if (N == 0)
            Scratch[0] = L'\0';
        wcscpy(Resolved, Scratch);
    }
    else
    {
        wcscpy(Resolved, Info->CaptionW);
    }

    // A caption of exactly " " (DAT_005cef90) hides the tip.
    if (_wcsicmp(Resolved, L" ") == 0)
    {
        bHasText_ = 0;
        Flags = static_cast<unsigned char>(Flags & 0xfd);
        return;
    }

    // AuxField is a KeyCmd NicknameId: FUN_00577dd0 finds the binding, FUN_00577f50 formats
    // it, and " (" / name / ")" (DAT_005d5ff4 / DAT_005d5ff0) are appended.
    if (Info->AuxField != 0)
    {
        FKeyBindingLookup Lookup = {};
        if (KeyMapFindActiveBinding(static_cast<unsigned>(Info->AuxField), &Lookup)
            && Lookup.VirtualKey != 0)
        {
            KeyMapFormatBindingName(&Lookup, Scratch, 0x100);
            wcscat(Resolved, L" (");
            wcscat(Resolved, Scratch);
            wcscat(Resolved, L")");
        }
    }

    SetText(Resolved);            // TextWin slot 28
    RepositionAfterCaption(Info); // FUN_004bda70
}

void NnToolTipControl::UpdateFromHover(FToolTipHoverInfo const* Info)
{
    // FUN_004bdea0. No null test on Info (the rollover's FUN_004b5ca0 reads it first).
    bFedThisFrame_ = 1;
    if (GetClientMouseCursorDrawFlag() == 0)
    {
        bHasText_ = 0;
        Flags = static_cast<unsigned char>(Flags & 0xfd);
        return;
    }

    NnRolloverFeedHoverGlobal(Info); // FUN_004b62b0

    // The applied value of "enable_tooltips" against 0.999 (DAT_005c8b68, a double): only a
    // value below it bails. No null test.
    FPerfOption* Opt = FindPerfOption("enable_tooltips");
    if (static_cast<double>(Opt->Applied) < 0.999)
        return;

    if (bHasText_ != 0)
    {
        bool const bSameIds = (Hover_.Ids == Info->Ids);
        bool const bSameLive = (Hover_.bLive == Info->bLive);
        bool bSamePos = true;
        if (Hover_.bLive != 0)
            bSamePos = (Hover_.WorldPos[0] == Info->WorldPos[0]
                        && Hover_.WorldPos[1] == Info->WorldPos[1]);
        if (bSameIds && bSameLive && bSamePos)
        {
            Flags = static_cast<unsigned char>(Flags & 0xfd);
            return;
        }
        bHasText_ = 0;
        DelayRemain_ = 0.0f;
        DelayAccum_ = 0.0f;
    }

    bool const bNeedRebuild =
        (Hover_.Ids != Info->Ids) || (Hover_.bLive != Info->bLive)
        || (Hover_.bLive != 0
            && (Hover_.WorldPos[0] != Info->WorldPos[0]
                || Hover_.WorldPos[1] != Info->WorldPos[1]))
        || (bBuilt_ == 0);
    if (bNeedRebuild)
    {
        if (DelayRemain_ <= 0.0f && DelayAccum_ < DelayThreshold_)
            return;
        ApplyHover(Info);
    }
    Flags = static_cast<unsigned char>(Flags | 0x03);
}

void NnToolTipControl::TickToolTip(double DeltaSeconds)
{
    // FUN_004bdda0.
    if (bFedThisFrame_ == 0)
    {
        Flags = static_cast<unsigned char>(Flags & 0xfd);
        bHasText_ = 0;
    }
    bFedThisFrame_ = 0;

    if (GetClientMouseCursorDrawFlag() == 0)
    {
        bHasText_ = 0;
        Flags = static_cast<unsigned char>(Flags & 0xfd);
        return;
    }

    unsigned char FlagsByte = Flags;
    if ((FlagsByte & 0x02) == 0)
    {
        Flags = static_cast<unsigned char>(FlagsByte & 0xfe);
        if (DelayRemain_ > 0.0f)
        {
            DelayRemain_ = static_cast<float>(DelayRemain_ - DeltaSeconds);
            if (DelayRemain_ < 0.0f)
                DelayRemain_ = 0.0f;
        }
    }
    else
    {
        DelayRemain_ = DelayThreshold_;
    }

    bBuilt_ = static_cast<unsigned char>((Flags >> 1) & 1);
    Flags = static_cast<unsigned char>(Flags & 0xfd);

    int CursorX = 0;
    int CursorY = 0;
    int CursorZ = 0;
    GetClientSoftwareCursor(&CursorX, &CursorY, &CursorZ);
    if (G_TipCursorX == CursorX && G_TipCursorY == CursorY)
        DelayAccum_ += DeltaSeconds;
    else
    {
        G_TipCursorX = CursorX;
        G_TipCursorY = CursorY;
        DelayAccum_ = 0.0f;
    }

    // With a mouse button down (DAT_00616850 & 3) the +0x159 latch goes up, so FUN_004bdea0
    // keeps the tip hidden for the hover it was clicked on.
    if ((GetClientMouseButtons() & 3u) != 0)
        bHasText_ = 1;
}

void NnToolTipControl::HideNow()
{
    // FUN_004be390.
    Flags = static_cast<unsigned char>(Flags & 0xfd);
    bHasText_ = 0;
}

void NnToolTipFeedHover(FToolTipHoverInfo const* Info)
{
    // FUN_004be370: only the singleton is tested; Info is passed on as is.
    if (G_ToolTipSingleton == nullptr)
        return;
    G_ToolTipSingleton->UpdateFromHover(Info);
}

void NnToolTipControl::ReadHover(FToolTipHoverInfo* Out) const
{
    // FUN_004be2f0's body (0x004be2f9).
    Out->Ids = Hover_.Ids;
    Out->IdsAlt = Hover_.IdsAlt;
    Out->WorldPos[0] = Hover_.WorldPos[0];
    Out->WorldPos[1] = Hover_.WorldPos[1];
    Out->WorldPos[2] = Hover_.WorldPos[2];
    Out->HalfExtents[0] = Hover_.HalfExtents[0];
    Out->HalfExtents[1] = Hover_.HalfExtents[1];
    Out->HalfExtents[2] = Hover_.HalfExtents[2];
    Out->bLive = Hover_.bLive;
}

bool NnToolTipReadHover(FToolTipHoverInfo* Out)
{
    // FUN_004be2f0.
    if (G_ToolTipSingleton == nullptr)
        return false;
    G_ToolTipSingleton->ReadHover(Out);
    return true;
}

void ToolTipHoverReset(FToolTipHoverInfo* Info)
{
    // FUN_004963a0. No null test on Info.
    Info->IdsAlt = 0;
    Info->Ids = -1;
    Info->AuxField = 0;
    if (Info->CaptionW != nullptr && Info->CaptionW[0] != L'\0')
        Info->CaptionW[0] = L'\0';
    Info->bLive = 0;
}

void NnToolTipTickGlobal(double DeltaSeconds)
{
    // FUN_004be2b0.
    if (G_ToolTipSingleton == nullptr)
        return;
    G_ToolTipSingleton->TickToolTip(DeltaSeconds);
}

void NnToolTipHideGlobal()
{
    if (G_ToolTipSingleton == nullptr)
        return;
    G_ToolTipSingleton->HideNow();
}

bool NnToolTipWasFedThisFrame()
{
    // FUN_004be2d0: the singleton's +0x164 byte, false without one.
    if (G_ToolTipSingleton == nullptr)
        return false;
    return G_ToolTipSingleton->bFedThisFrame() != 0;
}

NnToolTipControl* GetNnToolTipSingleton()
{
    return G_ToolTipSingleton;
}

void SetNnToolTipSingleton(NnToolTipControl* Tip)
{
    G_ToolTipSingleton = Tip;
}

// DAT_00672410 -- WINFACTORY<ToolTip>, static init LAB_004be280 (FUN_004be3b0("ToolTip")).
ToolTipFactory G_ToolTipFactory("ToolTip");

Win3D* ToolTipFactory::Create(Win3D* Parent, void* Payload)
{
    // FUN_004be420 (RET 8): operator new(0x170); the singleton is DAT_0067244c.
    NnToolTipControl* const Tip = new NnToolTipControl();
    Tip->Flags = static_cast<unsigned char>(Tip->Flags & 0xfd);
    G_ToolTipSingleton = Tip;
    Tip->ClassName = ClassName;
    if (!Tip->Create(Parent, Payload))
    {
        delete Tip;
        return nullptr;
    }
    return Tip;
}
