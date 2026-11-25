// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Interface/ui_scroll.h"

#include "common_csys.h"
#include "Core/game_options.h"
#include "Core/globals.h"
#include "Space/HUD/hud_target.h"
#include "NeuroNet/nn_ui_pulse.h"
#include "Space/star_sphere.h"
#include "Common/Graphics/texture_shapes.h"
#include "Interface/ui_page.h"
#include "Interface/ui_page_desc.h"
#include "Interface/win3d_immediate.h"
#include "Interface/win_factory.h"

#include "dacom.h"
#include "DALib.h"
#include "x86math.h"

#include <string.h>

namespace
{
    // DAT_0067d9fc -- UIScroll's own lazily acquired 3DMathEngine, not the UI_PAGE widgets'
    // DAT_0067dbc0.
    I3DMathEngine* G_UiScrollMath = nullptr;

    I3DMathEngine* UiScrollMath()
    {
        if (G_UiScrollMath == nullptr)
            G_UiScrollMath = Acquire3DMathEngine();
        return G_UiScrollMath;
    }

    // DAT_0067d9e0 — the one-time UITextures.ini load latch in the ctor.
    bool G_bScrollShapesLoaded = false;
    // DAT_0067d9e4 / DAT_0067d9e8 / DAT_0067d9ec.
    FTexShape const* G_ScrollArrowShape = nullptr;
    FTexShape const* G_ScrollTopBottomShape = nullptr;
    FTexShape const* G_ScrollMidShape = nullptr;

    // FUN_00595730 — ShapeDB::Find on DAT_0067d9c8: the named shape, or a warning and null.
    FTexShape const* ScrollShapeFind(char const* Name)
    {
        return G_UiShapeDB.Find(Name, kShapeDbHeaderLower);
    }

    // The thumb placement Relayout shares between both orientations. x87: Track is a float,
    // Pos stays double.
    void ScrollThumbSpan(float Track, float RangeMin, float RangeMax, float Value, float Page,
                         bool bStepMode, double* OutPos, float* OutThumb)
    {
        // Equal: thumb = whole track, position 0.
        if (RangeMax == RangeMin)
        {
            *OutPos = 0.0;
            *OutThumb = Track;
            return;
        }
        float const Range = static_cast<float>(static_cast<double>(RangeMax) - RangeMin);
        double Pos = (static_cast<double>(Value) - RangeMin) * Track / Range;
        float Thumb = static_cast<float>(static_cast<double>(Track) * Page / Range);
        if (Thumb < 0.01f)
            Thumb = 0.01f;
        if (bStepMode)
            Thumb = 0.0f;
        // Pos is kept when Thumb + Pos <= Track (or unordered).
        if (static_cast<double>(Thumb) + Pos > static_cast<double>(Track))
            Pos = static_cast<double>(Track) - Thumb;
        *OutPos = Pos;
        *OutThumb = Thumb;
    }

    // DAT_005d3e94 -- the thumb's end-cap length along the track.
    float const kScrollThumbCap = 0.002f;
    // The auto-repeat delay both MouseEvents arm after an arrow press.
    float const kScrollRepeatDelay = 0.3f;
    // UICycle's auto-repeat interval.
    float const kCycleRepeatInterval = 0.1f;
    // DAT_005d3ee0 -- the step-mode track click moves a tenth of the range.
    float const kScrollTrackStepFraction = 0.1f;

    // A point inside a part rect: X0 <= X <= X1 and Y1 <= Y <= Y0, false when unordered.
    bool ScrollRectContains(FScrollHitRect const& Rect, float X, float Y)
    {
        return X >= Rect.X0 && X <= Rect.X1 && Y <= Rect.Y0 && Y >= Rect.Y1;
    }

    // The control's own box: 0 <= X < Width and -Height < Y <= 0.
    bool ScrollInsideBox(float X, float Y, float Width, float Height)
    {
        if (!(X >= 0.0f))
            return false;
        if (!(X < Width))
            return false;
        if (!(Y <= 0.0f))
            return false;
        return -Height < Y;
    }
}

// FUN_00595400 -- called at WinMain shutdown. The latch itself is left set, as in retail.
void UiScrollClearShapes()
{
    if (G_bScrollShapesLoaded)
        G_UiShapeDB.Clear();
}

UiScroll::UiScroll()
    : bActive_(true)
    , bCustomColor_(false)
    , Width_(0.0f)
    , Height_(0.0f)
    , ArrowSize_(0.0f)
    , bStepMode_(false)
    , DragPart_(-1)
    , RepeatTimer_(0.0f)
    , RangeMin_(0.0f)
    , RangeMax_(0.0f)
    , Value_(0.0f)
    , Page_(0.0f)
    , Step_(0.0f)
    , bDragging_(false)
    , DragOffset_(0.0f)
    , DefaultPage_(0.0f)
    , bHorizontal_(false)
{
    // FUN_00595420.
    memcpy(CustomColor_, &G_NnPaletteGrey, sizeof(CustomColor_));
    FScrollHitRect* const Rects[5] = { &TopArrow_, &BottomArrow_, &TrackBefore_, &TrackAfter_,
                                       &Thumb_ };
    for (int Index = 0; Index < 5; ++Index)
    {
        Rects[Index]->X0 = 0.0f;
        Rects[Index]->Y0 = 0.0f;
        Rects[Index]->X1 = 0.0f;
        Rects[Index]->Y1 = 0.0f;
        Rects[Index]->bHot = false;
    }
    for (int Index = 0; Index < 12; ++Index)
        (&InverseLayout_.Rotation.E[0][0])[Index] = 0.0f;
    InverseLayout_.Rotation.E[0][0] = 1.0f;
    InverseLayout_.Rotation.E[1][1] = 1.0f;
    InverseLayout_.Rotation.E[2][2] = 1.0f;
    ScrollOffset_[0] = 0.0f;
    ScrollOffset_[1] = 0.0f;
    ScrollOffset_[2] = 0.0f;
    DefaultPage_ = Page_;

    if (!G_bScrollShapesLoaded)
    {
        G_bScrollShapesLoaded = true;
        G_UiShapeDB.Load("Interface\\UI\\UITextures.ini", DALib::Data);
        G_ScrollArrowShape = ScrollShapeFind("HUD_sliderarrow");
        G_ScrollTopBottomShape = ScrollShapeFind("HUD_slidertextopbottom");
        G_ScrollMidShape = ScrollShapeFind("HUD_slidertexmid");
    }

    unsigned long const IdleColor = G_NnPaletteCyan;
    ArrowIdleColor_ = IdleColor;
    ArrowHotColor_ = IdleColor;
    BarIdleColor_ = IdleColor;
    BarHotColor_ = IdleColor;

    HitRects_.reserve(5);
    HitRects_.push_back(&TopArrow_);
    HitRects_.insert(HitRects_.end(), 1, &BottomArrow_);
    HitRects_.insert(HitRects_.end(), 1, &Thumb_);
}

UiScroll::~UiScroll()
{
    // FUN_00595800.
}

void UiScroll::SetPosition(float const* NewPosition)
{
    // LAB_00595f90 (RET 4): NewPosition + (offset + scroll offset), then the inverse into
    // +0x3dc. x87: the X and Y sums stay double; Z's sum is rounded to float first.
    double const SumX = static_cast<double>(Offset[0]) + ScrollOffset_[0];
    double const SumY = static_cast<double>(Offset[1]) + ScrollOffset_[1];
    float const SumZ = static_cast<float>(static_cast<double>(Offset[2]) + ScrollOffset_[2]);
    Vector Local;
    Local.x = static_cast<float>(SumX + NewPosition[0]);
    Local.y = static_cast<float>(static_cast<double>(NewPosition[1]) + SumY);
    Local.z = static_cast<float>(static_cast<double>(NewPosition[2]) + SumZ);
    WinControl::SetPosition(&Local.x);

    Transform Inverse;
    float Scratch = 0.0f;
    UiScrollMath()->GeneralInverse(Inverse, Scratch,
                                   Placement);
    memcpy(&InverseLayout_, &Inverse, sizeof(InverseLayout_));
}

bool UiScroll::Create(Win3D* NewParent, void* Payload)
{
    // FUN_00595e90 (RET 8).
    bool const bResult = Win3D::Create(NewParent, nullptr);
    if (Payload != nullptr)
    {
        FUiPayloadScrollCycle const* Desc = static_cast<FUiPayloadScrollCycle const*>(Payload);
        Width_ = Desc->Right;
        Height_ = Desc->Bottom;
        RangeMin_ = Desc->Extra0;
        RangeMax_ = Desc->Extra1;
        Page_ = Desc->Extra2;
        Step_ = Desc->Extra3;
        bHorizontal_ = Desc->Flag != 0;
        DefaultPage_ = Desc->Extra2;
    }

    // Height when Height <= Width (or unordered), else Width; times 0.2 in double.
    float const Shorter = (Height_ > Width_) ? Width_ : Height_;
    ArrowSize_ = static_cast<float>(static_cast<double>(Shorter) * 0.2);
    Relayout();
    return bResult;
}

void UiScroll::Relayout()
{
    // FUN_00595840. Step mode shrinks the range by one while the thumb is placed and restores
    // it at the end.
    bool const bStep = bStepMode_;
    if (bHorizontal_)
    {
        // Arrows at both ends along X.
        float const Lead = static_cast<float>(static_cast<double>(Height_) + ArrowSize_);
        double const Tail = static_cast<double>(Width_) - Height_ - ArrowSize_;
        double Arrows;
        if (bStep)
        {
            RangeMax_ = static_cast<float>(static_cast<double>(RangeMax_) - 1.0);
            Arrows = static_cast<double>(Height_) * 4.0;
        }
        else
        {
            Arrows = static_cast<double>(Height_) + Height_;
        }
        float const Track = static_cast<float>((Width_ - Arrows) -
                                               (static_cast<double>(ArrowSize_) + ArrowSize_));
        double Pos = 0.0;
        float Thumb = 0.0f;
        ScrollThumbSpan(Track, RangeMin_, RangeMax_, Value_, Page_, bStep, &Pos, &Thumb);

        if (bStep)
        {
            Thumb_.X1 = static_cast<float>(static_cast<double>(Height_) + Height_ + Pos + Lead);
            Thumb_.X0 = static_cast<float>(Pos + Lead);
        }
        else
        {
            Thumb_.X0 = static_cast<float>(Lead + Pos);
            Thumb_.X1 = static_cast<float>(Thumb + Pos + Lead);
        }
        Thumb_.Y0 = 0.0f;
        Thumb_.Y1 = -Height_;

        TopArrow_.X0 = 0.0f;
        TopArrow_.X1 = Height_;
        TopArrow_.Y0 = 0.0f;
        TopArrow_.Y1 = -Height_;

        BottomArrow_.Y0 = 0.0f;
        BottomArrow_.X0 = static_cast<float>(static_cast<double>(Width_) - Height_);
        BottomArrow_.X1 = Width_;
        BottomArrow_.Y1 = -Height_;

        TrackBefore_.X0 = Lead;
        TrackBefore_.X1 = Thumb_.X0;
        TrackBefore_.Y0 = 0.0f;
        TrackBefore_.Y1 = -Height_;

        TrackAfter_.Y0 = 0.0f;
        TrackAfter_.X0 = Thumb_.X1;
        TrackAfter_.X1 = static_cast<float>(Tail);
        TrackAfter_.Y1 = -Height_;
    }
    else
    {
        // Arrows at both ends along -Y.
        float const Lead = static_cast<float>(static_cast<double>(Width_) + ArrowSize_);
        double const Tail = static_cast<double>(Height_) - Width_ - ArrowSize_;
        double Arrows;
        if (bStep)
        {
            RangeMax_ = static_cast<float>(static_cast<double>(RangeMax_) - 1.0);
            Arrows = static_cast<double>(Width_) * 4.0;
        }
        else
        {
            Arrows = static_cast<double>(Width_) + Width_;
        }
        float const Track = static_cast<float>((Height_ - Arrows) -
                                               (static_cast<double>(ArrowSize_) + ArrowSize_));
        double Pos = 0.0;
        float Thumb = 0.0f;
        ScrollThumbSpan(Track, RangeMin_, RangeMax_, Value_, Page_, bStep, &Pos, &Thumb);

        if (bStep)
        {
            Thumb_.X1 = Width_;
            Thumb_.Y1 = static_cast<float>(-(static_cast<double>(Width_) + Width_ + Pos + Lead));
            Thumb_.Y0 = static_cast<float>(-(Pos + Lead));
        }
        else
        {
            Thumb_.Y0 = static_cast<float>(-(Pos + Lead));
            Thumb_.X1 = Width_;
            Thumb_.Y1 = static_cast<float>(-(Thumb + Pos + Lead));
        }
        Thumb_.X0 = 0.0f;

        TopArrow_.X0 = 0.0f;
        TopArrow_.X1 = Width_;
        TopArrow_.Y0 = 0.0f;
        TopArrow_.Y1 = -Width_;

        BottomArrow_.X0 = 0.0f;
        BottomArrow_.Y0 = static_cast<float>(-(static_cast<double>(Height_) - Width_));
        BottomArrow_.X1 = Width_;
        BottomArrow_.Y1 = -Height_;

        TrackBefore_.X0 = 0.0f;
        TrackBefore_.Y0 = -Lead;
        TrackBefore_.X1 = Width_;
        TrackBefore_.Y1 = Thumb_.Y0;

        TrackAfter_.X0 = 0.0f;
        TrackAfter_.Y0 = Thumb_.Y1;
        TrackAfter_.X1 = Width_;
        TrackAfter_.Y1 = static_cast<float>(-Tail);
    }

    if (bStepMode_)
        RangeMax_ = static_cast<float>(static_cast<double>(RangeMax_) + 1.0);
}

void UiScroll::ClampValues()
{
    // FUN_00595ca0.
    if (RangeMin_ > RangeMax_)
        RangeMin_ = RangeMax_;
    double const Span = static_cast<double>(RangeMax_) - RangeMin_;
    if (Span < Page_)
    {
        if (bStepMode_)
            Page_ = 1.0f;
        else
            Page_ = static_cast<float>(Span);
    }
    if (Page_ < 0.0f)
        Page_ = 0.0f;
    if (Value_ < RangeMin_)
        Value_ = RangeMin_;
    double const Last = static_cast<double>(RangeMax_) - Page_;
    if (Last < Value_)
        Value_ = static_cast<float>(Last);
}

void UiScroll::SetRange(float Min, float Max)
{
    // FUN_00595d60.
    RangeMin_ = Min;
    RangeMax_ = Max;
    ClampValues();
    Relayout();
}

void UiScroll::GetRange(float* OutMin, float* OutMax) const
{
    // FUN_00595d90.
    *OutMin = RangeMin_;
    *OutMax = RangeMax_;
}

bool UiScroll::SetValue(float Value)
{
    // FUN_00595db0.
    Value_ = Value;
    ClampValues();
    Relayout();
    return true;
}

bool UiScroll::SetPage(float Page)
{
    // FUN_00595de0.
    Page_ = Page;
    DefaultPage_ = Page;
    ClampValues();
    Relayout();
    return true;
}

bool UiScroll::SetStep(float Step)
{
    // FUN_00595e10.
    Step_ = Step;
    ClampValues();
    Relayout();
    return true;
}

void UiScroll::SetSize(float Width, float Height)
{
    // FUN_00595e30.
    Width_ = Width;
    Height_ = Height;
    if (Height > Width)
        ArrowSize_ = static_cast<float>(static_cast<double>(Width) * 0.2);
    else
        ArrowSize_ = static_cast<float>(static_cast<double>(Height) * 0.2);
    Relayout();
}

void UiScroll::SetArrowColors(unsigned long const* Idle, unsigned long const* Hot)
{
    // FUN_00595f30.
    ArrowIdleColor_ = *Idle;
    ArrowHotColor_ = *Hot;
}

void UiScroll::SetBarColors(unsigned long const* Idle, unsigned long const* Hot)
{
    // FUN_00595f50.
    BarIdleColor_ = *Idle;
    BarHotColor_ = *Hot;
}

void UiScroll::SetScrollOffset(float const Offset[3])
{
    // FUN_005972d0: old position - old offset + new offset, handed to WinControl::SetPosition
    // directly (not through the vftable). x87: Z's difference is rounded to float first; X and
    // Y stay double.
    float const OldX = ScrollOffset_[0];
    float const OldY = ScrollOffset_[1];
    float const OldZ = ScrollOffset_[2];
    ScrollOffset_[0] = Offset[0];
    ScrollOffset_[1] = Offset[1];
    ScrollOffset_[2] = Offset[2];
    float const DeltaZ = static_cast<float>(static_cast<double>(Position.z) - OldZ);
    Vector Moved;
    Moved.x = static_cast<float>(static_cast<double>(Position.x) - OldX + ScrollOffset_[0]);
    Moved.y = static_cast<float>(static_cast<double>(Position.y) - OldY + ScrollOffset_[1]);
    Moved.z = static_cast<float>(static_cast<double>(DeltaZ) + ScrollOffset_[2]);
    WinControl::SetPosition(&Moved.x);
}

Vector UiScroll::RayToLocal(float const* Ray) const
{
    // The ray scaled onto the z = Position.z plane (the quotient stays double), then through the
    // +0x3dc inverse.
    double const Scale = static_cast<double>(Position.z) / Ray[2];
    Vector Scaled;
    Scaled.x = static_cast<float>(Ray[0] * Scale);
    Scaled.y = static_cast<float>(Ray[1] * Scale);
    Scaled.z = static_cast<float>(Ray[2] * Scale);
    Vector Local;
    UiScrollMath()->TransformXform(Local, InverseLayout_, Scaled);
    return Local;
}

Vector UiScroll::RayToLocalViaHud(float const* Ray) const
{
    // As RayToLocal, but the transform goes through HUD_Target_Base's math engine
    // (DAT_00674b1c), not DAT_0067d9fc.
    double const Scale = static_cast<double>(Position.z) / Ray[2];
    Vector Scaled;
    Scaled.x = static_cast<float>(Ray[0] * Scale);
    Scaled.y = static_cast<float>(Ray[1] * Scale);
    Scaled.z = static_cast<float>(Ray[2] * Scale);
    Vector Local;
    HudTransformPoint(&InverseLayout_, &Local, &Scaled);
    return Local;
}

void UiScroll::NotifyParent()
{
    // Inlined at every site in retail.
    if (Parent == nullptr)
        return;
    Parent->ProcessMessage(kWinMsgScroll, static_cast<unsigned>(reinterpret_cast<uintptr_t>(this)), 0u);
    WinControl* const Owner = Parent->AsWinControl();
    if (Owner == nullptr)
        return;
    Owner->SetProperty(kWinNotifyScrollValue, reinterpret_cast<uintptr_t>(this),
                       static_cast<uintptr_t>(static_cast<int>(Value_)));
}

bool UiScroll::HitTest(float const* Ray)
{
    // LAB_005971e0 (RET 4).
    Vector const Local = RayToLocal(Ray);
    if (!ScrollInsideBox(Local.x, Local.y, Width_, Height_))
        return false;
    if (Parent != nullptr)
    {
        WinControl* const Owner = Parent->AsWinControl();
        if (Owner != nullptr)
            Owner->SetProperty(kWinNotifyHover, reinterpret_cast<uintptr_t>(AsWinControl()), 0);
    }
    return true;
}

void UiScroll::Update()
{
    // LAB_00596050. A dragged thumb stays hot.
    Win3D::Update();
    float Ray[3];
    Win3DGetMouseRay(Ray);
    Vector const Local = RayToLocal(Ray);
    for (size_t Index = 0; Index < HitRects_.size(); ++Index)
    {
        FScrollHitRect* const Rect = HitRects_[Index];
        Rect->bHot = ScrollRectContains(*Rect, Local.x, Local.y);
    }
    if (bDragging_)
        Thumb_.bHot = true;
}

void UiScroll::BeginDraw()
{
    Win3DBindModelview(nullptr, &Placement);
    DALib::Pipeline->SetRenderState(D3DRS_ZENABLE, 0);
    DALib::Pipeline->SetRenderState(D3DRS_ZWRITEENABLE, 0);
    DALib::Pipeline->SetRenderState(D3DRS_CULLMODE, 1);
    DALib::Pipeline->SetRenderState(D3DRS_LIGHTING, 0);
    DALib::Pipeline->SetRenderState(D3DRS_SRCBLEND, 5);
    DALib::Pipeline->SetRenderState(D3DRS_DESTBLEND, 6);
    DALib::Pipeline->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
}

void UiScroll::DrawArrowVertical(FScrollHitRect const* Rect, bool bDraw, bool bUp)
{
    // FUN_00596140 (RET 0xc).
    Win3DBindTexture(TexShapeGetGpuHandle(G_ScrollArrowShape, 0));
    if (bDraw)
    {
        if (Rect->bHot)
        {
            unsigned long Pulse = BarHotColor_;
            NnUiPulseBlendFromArgb(&Pulse, Pulse);
            Win3DColor4ubv(reinterpret_cast<unsigned char const*>(&Pulse));
        }
        else
        {
            Win3DColor4ubv(reinterpret_cast<unsigned char const*>(&ArrowHotColor_));
        }
        Win3DBegin(kWin3DTriangles);
        float const MidX = static_cast<float>((static_cast<double>(Rect->X1) + Rect->X0) * 0.5);
        if (bUp)
        {
            Win3DTexCoord2f(1.0f, 1.0f);
            Win3DVertex3f(Rect->X1, Rect->Y1, 0.0f);
            Win3DTexCoord2f(0.0f, 1.0f);
            Win3DVertex3f(Rect->X0, Rect->Y1, 0.0f);
            Win3DTexCoord2f(1.0f, 0.0f);
            Win3DVertex3f(MidX, Rect->Y0, 0.0f);
        }
        else
        {
            Win3DTexCoord2f(0.0f, 1.0f);
            Win3DVertex3f(Rect->X0, Rect->Y0, 0.0f);
            Win3DTexCoord2f(1.0f, 0.0f);
            Win3DVertex3f(Rect->X1, Rect->Y0, 0.0f);
            Win3DTexCoord2f(1.0f, 1.0f);
            Win3DVertex3f(MidX, Rect->Y1, 0.0f);
        }
        Win3DEnd();
    }
    Win3DColor4ubv(reinterpret_cast<unsigned char const*>(Rect->bHot ? &BarIdleColor_
                                                                     : &ArrowIdleColor_));
}

void UiScroll::DrawArrowHorizontal(FScrollHitRect const* Rect, bool bDraw, bool bLeft)
{
    // FUN_005962c0 (RET 0xc).
    Win3DBindTexture(TexShapeGetGpuHandle(G_ScrollArrowShape, 0));
    if (bDraw)
    {
        if (Rect->bHot)
        {
            unsigned long Pulse = BarHotColor_;
            NnUiPulseBlendFromArgb(&Pulse, Pulse);
            Win3DColor4ubv(reinterpret_cast<unsigned char const*>(&Pulse));
        }
        else
        {
            Win3DColor4ubv(reinterpret_cast<unsigned char const*>(&ArrowHotColor_));
        }
        Win3DBegin(kWin3DTriangles);
        float const MidY = static_cast<float>((static_cast<double>(Rect->Y0) + Rect->Y1) * 0.5);
        if (bLeft)
        {
            Win3DTexCoord2f(1.0f, 1.0f);
            Win3DVertex3f(Rect->X1, Rect->Y0, 0.0f);
            Win3DTexCoord2f(0.0f, 1.0f);
            Win3DVertex3f(Rect->X1, Rect->Y1, 0.0f);
            Win3DTexCoord2f(1.0f, 0.0f);
            Win3DVertex3f(Rect->X0, MidY, 0.0f);
        }
        else
        {
            Win3DTexCoord2f(1.0f, 0.0f);
            Win3DVertex3f(Rect->X0, Rect->Y0, 0.0f);
            Win3DTexCoord2f(1.0f, 1.0f);
            Win3DVertex3f(Rect->X0, Rect->Y1, 0.0f);
            Win3DTexCoord2f(0.0f, 1.0f);
            Win3DVertex3f(Rect->X1, MidY, 0.0f);
        }
        Win3DEnd();
    }
    Win3DColor4ubv(reinterpret_cast<unsigned char const*>(Rect->bHot ? &BarIdleColor_
                                                                     : &ArrowIdleColor_));
}

void UiScroll::Draw()
{
    // LAB_00596450. Nothing with display option 0x16 off.
    if (GetGameOption(0x16) == 0)
        return;

    // The four colours are rewritten every frame: the custom colour, or DAT_00679bb0 (read four
    // times, as retail does).
    if (bCustomColor_)
    {
        unsigned long Custom = 0;
        memcpy(&Custom, CustomColor_, sizeof(Custom));
        ArrowIdleColor_ = Custom;
        ArrowHotColor_ = Custom;
        BarIdleColor_ = Custom;
        BarHotColor_ = Custom;
    }
    else
    {
        ArrowIdleColor_ = G_NnPaletteCyan;
        ArrowHotColor_ = G_NnPaletteCyan;
        BarIdleColor_ = G_NnPaletteCyan;
        BarHotColor_ = G_NnPaletteCyan;
    }
    BeginDraw();

    FScrollHitRect const* const Frame0 = &TrackBefore_;
    FScrollHitRect const* const Frame1 = &TrackAfter_;
    if (bHorizontal_)
    {
        DrawArrowHorizontal(&BottomArrow_, true, false);
        DrawArrowHorizontal(&TopArrow_, true, true);
    }
    else
    {
        DrawArrowVertical(&BottomArrow_, true, false);
        DrawArrowVertical(&TopArrow_, true, true);
        // The track frame, drawn before the thumb in the vertical layout.
        Win3DColor4ubv(reinterpret_cast<unsigned char const*>(&ArrowIdleColor_));
        Win3DBegin(kWin3DLineLoop);
        Win3DVertex3f(Frame1->X0, Frame0->Y0, 0.0f);
        Win3DVertex3f(Frame1->X1, Frame0->Y0, 0.0f);
        Win3DVertex3f(Frame1->X1, Frame1->Y1, 0.0f);
        Win3DVertex3f(Frame1->X0, Frame1->Y1, 0.0f);
        Win3DEnd();
    }

    // The thumb: +0x418 pulsed while hot and active, else +0x410.
    if (Thumb_.bHot && bActive_)
    {
        unsigned long Pulse = BarHotColor_;
        NnUiPulseBlendFromArgb(&Pulse, Pulse);
        Win3DColor4ubv(reinterpret_cast<unsigned char const*>(&Pulse));
    }
    else
    {
        Win3DColor4ubv(reinterpret_cast<unsigned char const*>(&ArrowHotColor_));
    }
    FScrollHitRect const Thumb = Thumb_;
    if (bHorizontal_)
    {
        // The middle between the two caps, then a cap at each end, each kScrollThumbCap long.
        float const Lead = static_cast<float>(static_cast<double>(Thumb.X0) + kScrollThumbCap);
        float const Tail = static_cast<float>(static_cast<double>(Thumb.X1) - kScrollThumbCap);

        Win3DBindTexture(TexShapeGetGpuHandle(G_ScrollMidShape, 0));
        Win3DBegin(kWin3DQuads);
        Win3DTexCoord2f(1.0f, 0.0f);
        Win3DVertex3f(Lead, Thumb.Y0, 0.0f);
        Win3DTexCoord2f(1.0f, 1.0f);
        Win3DVertex3f(Tail, Thumb.Y0, 0.0f);
        Win3DTexCoord2f(0.0f, 1.0f);
        Win3DVertex3f(Tail, Thumb.Y1, 0.0f);
        Win3DTexCoord2f(0.0f, 0.0f);
        Win3DVertex3f(Lead, Thumb.Y1, 0.0f);
        Win3DEnd();

        Win3DBindTexture(TexShapeGetGpuHandle(G_ScrollTopBottomShape, 0));
        Win3DBegin(kWin3DQuads);
        Win3DTexCoord2f(1.0f, 1.0f);
        Win3DVertex3f(Lead, Thumb.Y0, 0.0f);
        Win3DTexCoord2f(1.0f, 1.0f);
        Win3DVertex3f(Thumb.X0, Thumb.Y0, 0.0f);
        Win3DTexCoord2f(1.0f, 0.75f);
        Win3DVertex3f(Thumb.X0, Thumb.Y1, 0.0f);
        Win3DTexCoord2f(1.0f, 0.75f);
        Win3DVertex3f(Lead, Thumb.Y1, 0.0f);
        Win3DEnd();

        Win3DBindTexture(TexShapeGetGpuHandle(G_ScrollTopBottomShape, 0));
        Win3DBegin(kWin3DQuads);
        Win3DTexCoord2f(0.0f, 0.25f);
        Win3DVertex3f(Tail, Thumb.Y0, 0.0f);
        Win3DTexCoord2f(1.0f, 0.25f);
        Win3DVertex3f(Thumb.X1, Thumb.Y0, 0.0f);
        Win3DTexCoord2f(1.0f, 0.0f);
        Win3DVertex3f(Thumb.X1, Thumb.Y1, 0.0f);
        Win3DTexCoord2f(0.0f, 0.0f);
        Win3DVertex3f(Tail, Thumb.Y1, 0.0f);
        Win3DEnd();

        // The track frame, drawn after the thumb in the horizontal layout.
        Win3DColor4ubv(reinterpret_cast<unsigned char const*>(&ArrowIdleColor_));
        Win3DBegin(kWin3DLineLoop);
        Win3DVertex3f(Frame0->X0, Frame1->Y0, 0.0f);
        Win3DVertex3f(Frame1->X1, Frame1->Y0, 0.0f);
        Win3DVertex3f(Frame1->X1, Frame1->Y1, 0.0f);
        Win3DVertex3f(Frame0->X0, Frame1->Y1, 0.0f);
        Win3DEnd();
    }
    else
    {
        // The same three quads along -Y.
        float const Lead = static_cast<float>(static_cast<double>(Thumb.Y0) - kScrollThumbCap);
        float const Tail = static_cast<float>(static_cast<double>(Thumb.Y1) + kScrollThumbCap);

        Win3DBindTexture(TexShapeGetGpuHandle(G_ScrollMidShape, 0));
        Win3DBegin(kWin3DQuads);
        Win3DTexCoord2f(0.0f, 0.0f);
        Win3DVertex3f(Thumb.X0, Lead, 0.0f);
        Win3DTexCoord2f(1.0f, 0.0f);
        Win3DVertex3f(Thumb.X1, Lead, 0.0f);
        Win3DTexCoord2f(1.0f, 1.0f);
        Win3DVertex3f(Thumb.X1, Tail, 0.0f);
        Win3DTexCoord2f(0.0f, 1.0f);
        Win3DVertex3f(Thumb.X0, Tail, 0.0f);
        Win3DEnd();

        Win3DBindTexture(TexShapeGetGpuHandle(G_ScrollTopBottomShape, 0));
        Win3DBegin(kWin3DQuads);
        Win3DTexCoord2f(1.0f, 1.0f);
        Win3DVertex3f(Thumb.X0, Lead, 0.0f);
        Win3DTexCoord2f(1.0f, 1.0f);
        Win3DVertex3f(Thumb.X1, Lead, 0.0f);
        Win3DTexCoord2f(1.0f, 0.75f);
        Win3DVertex3f(Thumb.X1, Thumb.Y0, 0.0f);
        Win3DTexCoord2f(1.0f, 0.75f);
        Win3DVertex3f(Thumb.X0, Thumb.Y0, 0.0f);
        Win3DEnd();

        Win3DBindTexture(TexShapeGetGpuHandle(G_ScrollTopBottomShape, 0));
        Win3DBegin(kWin3DQuads);
        Win3DTexCoord2f(0.0f, 0.25f);
        Win3DVertex3f(Thumb.X0, Thumb.Y1, 0.0f);
        Win3DTexCoord2f(1.0f, 0.25f);
        Win3DVertex3f(Thumb.X1, Thumb.Y1, 0.0f);
        Win3DTexCoord2f(1.0f, 0.0f);
        Win3DVertex3f(Thumb.X1, Tail, 0.0f);
        Win3DTexCoord2f(0.0f, 0.0f);
        Win3DVertex3f(Thumb.X0, Tail, 0.0f);
        Win3DEnd();
    }

    // Auto-repeat of a held arrow. Below zero the timer steps the value once per
    // frame (it is never re-armed here); it counts down by the frame time either way.
    if (DragPart_ == -1)
        return;
    if (RepeatTimer_ < 0.0f)
    {
        bool bStepped = true;
        if (DragPart_ == 0)
            Value_ = static_cast<float>(static_cast<double>(Value_) - Step_);
        else if (DragPart_ == 1)
            Value_ = static_cast<float>(static_cast<double>(Step_) + Value_);
        else
            bStepped = false;
        if (bStepped)
        {
            ClampValues();
            Relayout();
            NotifyParent();
        }
    }
    RepeatTimer_ = static_cast<float>(static_cast<double>(RepeatTimer_) -
                                      GetClientFrameTimeCapped());
}

void UiScroll::MouseOver(float const* Ray)
{
    // LAB_00596b90 (RET 4): while the thumb is dragged, map the mouse along the track to a
    // value (step mode: no grab offset and a truncated value), and on a change apply and notify.
    if (!bDragging_ || !bActive_)
        return;
    Vector const Local = RayToLocal(Ray);
    double const Range = static_cast<double>(RangeMax_) - RangeMin_;
    double Numerator = 0.0;
    double Denominator = 0.0;
    if (bHorizontal_)
    {
        double const Along = bStepMode_ ? static_cast<double>(Local.x)
                                        : static_cast<double>(Local.x) + DragOffset_;
        Numerator = Range * (static_cast<double>(TrackBefore_.X0) - Along);
        Denominator = static_cast<double>(TrackBefore_.X0) - TrackAfter_.X1;
    }
    else
    {
        double const Along = bStepMode_ ? static_cast<double>(Local.y)
                                        : static_cast<double>(Local.y) + DragOffset_;
        Numerator = Range * (static_cast<double>(TrackBefore_.Y0) - Along);
        Denominator = static_cast<double>(TrackBefore_.Y0) - TrackAfter_.Y1;
    }
    double NewValue = Numerator / Denominator + RangeMin_;
    if (bStepMode_)
        NewValue = static_cast<double>(static_cast<int>(NewValue));
    if (NewValue == static_cast<double>(Value_))
        return;
    Value_ = static_cast<float>(NewValue);
    ClampValues();
    Relayout();
    NotifyParent();
}

void UiScroll::MouseLeave(float const* Ray)
{
    // LAB_00596d20 (RET 4).
    (void)Ray;
    DragPart_ = -1;
}

bool UiScroll::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_00596d30 (RET 4).
    if (!bActive_)
        return false;
    if (Event->Button != kWin3DMouseLeft)
        return false;

    bool bHandled = false;
    float Ray[3];
    Win3DGetMouseRay(Ray);
    Vector const Local = RayToLocalViaHud(Ray);
    DragPart_ = -1;
    int const Kind = Event->Action;
    bool const bPress = Kind == kWin3DMousePress || Kind == kWin3DMouseDoubleClick;
    if (!bPress && Parent != nullptr)
    {
        WinControl* const Owner = Parent->AsWinControl();
        if (Owner != nullptr)
            Owner->SetProperty(kWinNotifyRelease, reinterpret_cast<uintptr_t>(AsWinControl()), 0);
    }

    if (ScrollInsideBox(Local.x, Local.y, Width_, Height_))
    {
        if (ScrollRectContains(Thumb_, Local.x, Local.y))
        {
            // Press on the thumb: start dragging it, keeping the grab offset.
            if (bPress)
            {
                bDragging_ = true;
                DragOffset_ = bHorizontal_ ? Thumb_.X0 - Local.x : Thumb_.Y0 - Local.y;
                if (Win3DGetCapture() != this)
                    Win3DPushCapture(this);
            }
        }
        else if (bPress)
        {
            bool bChanged = true;
            double NewValue = 0.0;
            if (ScrollRectContains(TopArrow_, Local.x, Local.y))
            {
                DragPart_ = 0;
                NewValue = static_cast<double>(Value_) - Step_;
                RepeatTimer_ = kScrollRepeatDelay;
            }
            else if (ScrollRectContains(BottomArrow_, Local.x, Local.y))
            {
                NewValue = static_cast<double>(Step_) + Value_;
                DragPart_ = 1;
                RepeatTimer_ = kScrollRepeatDelay;
            }
            else if (ScrollRectContains(TrackBefore_, Local.x, Local.y))
            {
                if (bStepMode_)
                {
                    // A tenth of the range down, truncated; one step when that is no change.
                    double const Jump = static_cast<double>(static_cast<int>(
                        static_cast<double>(Value_) -
                        (static_cast<double>(RangeMax_) - RangeMin_) * kScrollTrackStepFraction));
                    if (Jump != static_cast<double>(Value_))
                        NewValue = Jump;
                    else
                        NewValue = static_cast<double>(Value_) - 1.0;
                }
                else
                {
                    NewValue = static_cast<double>(Value_) - Page_;
                }
            }
            else if (ScrollRectContains(TrackAfter_, Local.x, Local.y))
            {
                if (bStepMode_)
                {
                    double const Jump = static_cast<double>(static_cast<int>(
                        (static_cast<double>(RangeMax_) - RangeMin_) * kScrollTrackStepFraction +
                        Value_));
                    if (Jump != static_cast<double>(Value_))
                        NewValue = Jump;
                    else
                        NewValue = static_cast<double>(Value_) + 1.0;
                }
                else
                {
                    NewValue = static_cast<double>(Page_) + Value_;
                }
            }
            else
            {
                bChanged = false;
            }
            if (bChanged)
            {
                Value_ = static_cast<float>(NewValue);
                ClampValues();
                Relayout();
                NotifyParent();
            }
        }
        bHandled = true;
    }

    // A release ends a thumb drag.
    if (!bPress && bDragging_)
    {
        bDragging_ = false;
        if (Win3DGetCapture() == this)
            Win3DRemoveCapture(this);
        return true;
    }
    if (bHandled)
        return true;
    return WinControl::MouseEvent(Event);
}

intptr_t UiScroll::SetProperty(int Msg, uintptr_t A, uintptr_t B)
{
    // LAB_00597350 (RET 0xc).
    switch (Msg)
    {
    case kWinPropOffset:
    {
        // A -> float[3], the new scroll offset: move by its change (each difference and sum
        // rounded to float), then the inverse.
        float const* const NewOffset = reinterpret_cast<float const*>(A);
        float Old[3];
        Old[0] = ScrollOffset_[0];
        Old[1] = ScrollOffset_[1];
        Old[2] = ScrollOffset_[2];
        ScrollOffset_[0] = NewOffset[0];
        ScrollOffset_[1] = NewOffset[1];
        ScrollOffset_[2] = NewOffset[2];
        float Base[3];
        Base[0] = static_cast<float>(static_cast<double>(Position.x) - Old[0]);
        Base[1] = static_cast<float>(static_cast<double>(Position.y) - Old[1]);
        Base[2] = static_cast<float>(static_cast<double>(Position.z) - Old[2]);
        Vector Moved;
        Moved.x = static_cast<float>(static_cast<double>(Base[0]) + ScrollOffset_[0]);
        Moved.y = static_cast<float>(static_cast<double>(Base[1]) + ScrollOffset_[1]);
        Moved.z = static_cast<float>(static_cast<double>(Base[2]) + ScrollOffset_[2]);
        WinControl::SetPosition(&Moved.x);
        Transform Inverse;
        float Scratch = 0.0f;
        UiScrollMath()->GeneralInverse(Inverse, Scratch,
                                       Placement);
        memcpy(&InverseLayout_, &Inverse, sizeof(InverseLayout_));
        return 1;
    }
    case kScrollPropSetRange:
    {
        // (A, B) = the integer range. The page goes back to +0x428; a range shorter than the
        // page hides the bar (flags &= 0xfc), otherwise it is shown and the top grows by one.
        int const Min = static_cast<int>(A);
        int Max = static_cast<int>(B);
        Page_ = DefaultPage_;
        if (static_cast<double>(Max - Min) < static_cast<double>(Page_))
        {
            Flags = static_cast<unsigned char>(Flags & ~(kWin3DActive | kWin3DVisible));
        }
        else
        {
            Flags = static_cast<unsigned char>(Flags | kWin3DActive | kWin3DVisible);
            Max = static_cast<int>(static_cast<double>(Max) + 1.0);
        }
        RangeMin_ = static_cast<float>(Min);
        RangeMax_ = static_cast<float>(Max);
        ClampValues();
        Relayout();
        return 1;
    }
    case kScrollPropSetValue:
        Value_ = static_cast<float>(static_cast<int>(A));
        ClampValues();
        Relayout();
        return 1;
    case kScrollPropGetValue:
        return static_cast<int>(Value_);
    default:
        return WinControl::SetProperty(Msg, A, B);
    }
}

void UiCycle::Draw()
{
    // LAB_00597560: the two arrows only (no option gate, no colour refresh), then the held-arrow
    // auto-repeat every kCycleRepeatInterval. Step mode drops the top by one around the clamp.
    BeginDraw();
    if (bHorizontal_)
    {
        DrawArrowHorizontal(&BottomArrow_, true, false);
        DrawArrowHorizontal(&TopArrow_, true, true);
    }
    else
    {
        DrawArrowVertical(&BottomArrow_, true, false);
        DrawArrowVertical(&TopArrow_, true, true);
    }
    if (DragPart_ == -1)
        return;
    if (!(RepeatTimer_ < 0.0f))
    {
        RepeatTimer_ = static_cast<float>(static_cast<double>(RepeatTimer_) -
                                          GetClientFrameTimeCapped());
        return;
    }
    if (DragPart_ == 0)
    {
        Value_ = static_cast<float>(static_cast<double>(Value_) - Step_);
    }
    else if (DragPart_ == 1)
    {
        Value_ = static_cast<float>(static_cast<double>(Step_) + Value_);
    }
    else
    {
        RepeatTimer_ = kCycleRepeatInterval;
        return;
    }
    if (bStepMode_)
        RangeMax_ = static_cast<float>(static_cast<double>(RangeMax_) - 1.0);
    ClampValues();
    if (bStepMode_)
        RangeMax_ = static_cast<float>(static_cast<double>(RangeMax_) + 1.0);
    Relayout();
    NotifyParent();
    RepeatTimer_ = kCycleRepeatInterval;
}

bool UiCycle::MouseEvent(FWin3DMouseEvent const* Event)
{
    // LAB_00597760 (RET 4): a press (kind 2) on an arrow steps by +0x3d0 and arms the
    // auto-repeat; anything else inside is swallowed.
    if (!bActive_)
        return false;
    if (Event->Button != kWin3DMouseLeft)
        return false;
    float Ray[3];
    Win3DGetMouseRay(Ray);
    Vector const Local = RayToLocal(Ray);
    DragPart_ = -1;
    if (!ScrollInsideBox(Local.x, Local.y, Width_, Height_))
        return WinControl::MouseEvent(Event);
    if (Event->Action != kWin3DMousePress)
        return true;
    if (ScrollRectContains(TopArrow_, Local.x, Local.y))
    {
        DragPart_ = 0;
        Value_ = static_cast<float>(static_cast<double>(Value_) - Step_);
    }
    else if (ScrollRectContains(BottomArrow_, Local.x, Local.y))
    {
        DragPart_ = 1;
        Value_ = static_cast<float>(static_cast<double>(Step_) + Value_);
    }
    else
    {
        return true;
    }
    RepeatTimer_ = kScrollRepeatDelay;
    if (bStepMode_)
        RangeMax_ = static_cast<float>(static_cast<double>(RangeMax_) - 1.0);
    ClampValues();
    if (bStepMode_)
        RangeMax_ = static_cast<float>(static_cast<double>(RangeMax_) + 1.0);
    Relayout();
    NotifyParent();
    return true;
}

// DAT_0067d99c.
WINFACTORY<UiScroll> G_UIScrollFactory("UIScroll");

// DAT_0067d970.
WINFACTORY<UiCycle> G_UICycleFactory("UICycle");
