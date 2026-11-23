// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// UIScroll / UICycle — PAGE_*.txt and RichTextWin scrollbars (RTTI .?AUUIScroll@@ : WinControl,
// .?AUUICycle@@ : UIScroll; IDataViewOwner face at +0x7c).
//
// WINFACTORY<UIScroll> DAT_0067d99c, Create FUN_00597ab0; WINFACTORY<UICycle> DAT_0067d970,
// Create FUN_00597bc0 (vftables 0x005e5684 / 0x005e5674). Both operator new(0x430); UICycle adds
// no field. UIScroll's vftable 0x005e55ac overrides WinControl's slots 0 (FUN_005957e0), 4, 5,
// 9, 11, 13, 14, 16, 19 and 42.
//
// Parent notifications (thumb drag, input, the draw's auto-repeat): ProcessMessage(0xf005,
// this, 0), then the parent's WinControl SetProperty(0x3e, this, ftol(value)). HitTest sends
// SetProperty(0x25, this, 0), MouseEvent SetProperty(0x29, this, 0).

#include "Interface/wincontrol_base.h"

#include "x86math.h"

#include <vector>

struct FTexShape;

// One scrollbar part in local coordinates (0x14 bytes): X0/Y0 top-left, X1/Y1 bottom-right
// (Y grows downwards as negative values), bHot = the mouse is over it (slot 11).
struct FScrollHitRect
{
    float X0;          // +0x00
    float Y0;          // +0x04
    float X1;          // +0x08
    float Y1;          // +0x0c
    bool bHot;         // +0x10
};

class UiScroll : public WinControl
{
public:
    // FUN_00595420.
    UiScroll();
    // FUN_00595800.
    ~UiScroll() override;

    // slot 4 (+0x10) — LAB_005971e0 (RET 4). Inside (0..W, 0..-H) tells the parent.
    bool HitTest(float const* Ray) override;
    // slot 5 (+0x14) — LAB_00595f90 (RET 4). Adds the offsets and rebuilds the inverse.
    void SetPosition(float const* NewPosition) override;

    // slot 9 (+0x24) — FUN_00595e90 (RET 8); args (Parent, FUiPayloadScrollCycle*).
    bool Create(Win3D* NewParent, void* Payload) override;

    // slot 11 (+0x2c) — LAB_00596050. Then the hit rects' hover flags from the cached mouse ray.
    void Update() override;
    // slot 13 (+0x34) — LAB_00596450.
    void Draw() override;
    // slot 14 (+0x38) — LAB_00596b90 (RET 4): thumb drag.
    void MouseOver(float const* Ray) override;
    // slot 16 (+0x40) — LAB_00596d20 (RET 4).
    void MouseLeave(float const* Ray) override;
    // slot 19 (+0x4c) — LAB_00596d30 (RET 4).
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
    // slot 42 (+0xa8) — LAB_00597350 (RET 0xc): 6 scroll offset, 0x37 range, 0x38 value,
    // 0x3b get value, else WinControl's.
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override;

    // FUN_00595840 — rebuild the five part rects from the size, range and value.
    void Relayout();
    // FUN_00595ca0 — clamp min <= max, page, value into range.
    void ClampValues();
    // FUN_00595d60 (RET 8) — +0x3c0 = Min, +0x3c4 = Max, clamp, relayout.
    void SetRange(float Min, float Max);
    // FUN_00595d90 (RET 8).
    void GetRange(float* OutMin, float* OutMax) const;
    // FUN_00595db0 (RET 4) — +0x3c8 = Value, clamp, relayout; returns 1.
    bool SetValue(float Value);
    // FUN_00595de0 (RET 4) — +0x3cc = +0x428 = Page, clamp, relayout; returns 1.
    bool SetPage(float Page);
    // FUN_00595e10 (RET 4) — +0x3d0 = Step, clamp, relayout; returns 1.
    bool SetStep(float Step);
    // FUN_00595e30 (RET 8) — +0x334 / +0x338, arrow size = min * 0.2, relayout.
    void SetSize(float Width, float Height);
    // FUN_00595f30 (RET 8) — +0x40c / +0x410.
    void SetArrowColors(unsigned long const* Idle, unsigned long const* Hot);
    // FUN_00595f50 (RET 8) — +0x414 / +0x418.
    void SetBarColors(unsigned long const* Idle, unsigned long const* Hot);
    // FUN_005972d0 (RET 4) — move by the change of the scroll offset (+0x41c).
    void SetScrollOffset(float const Offset[3]);

    // +0x3b4 — retail callers write the byte directly.
    void SetStepMode(bool bStepMode) { bStepMode_ = bStepMode; }
    // +0x3cc alone -- NN_Inventory writes the float directly, without SetPage's +0x428 copy,
    // clamp and relayout.
    void SetPageField(float Page) { Page_ = Page; }
    // +0x32c / +0x32d -- NN_Preferences::RefreshRows writes both bytes directly (greyed: 0 / 1;
    // live: 1 / 0).
    void SetActiveField(bool bActive) { bActive_ = bActive; }
    void SetCustomColorField(bool bCustomColor) { bCustomColor_ = bCustomColor; }

    // FUN_00595dd0
    float GetValue() const { return Value_; }
    // FUN_00597550 (RET 4) — +0x42c = the byte argument; no relayout.
    void SetHorizontal(bool bHorizontal) { bHorizontal_ = bHorizontal; }

protected:
    // FUN_00596140 (RET 0xc) — vertical arrow triangle in Rect, pointing up when bUp; drawn only
    // when bDraw. Always leaves the current colour at +0x414 (hot) or +0x40c.
    void DrawArrowVertical(FScrollHitRect const* Rect, bool bDraw, bool bUp);
    // FUN_005962c0 (RET 0xc) — the same for a horizontal scrollbar, pointing left when bLeft.
    void DrawArrowHorizontal(FScrollHitRect const* Rect, bool bDraw, bool bLeft);
    // The model-view bind and the seven render states both draws open with.
    void BeginDraw();
    // The parent notifications every value change ends with (after ClampValues and Relayout).
    void NotifyParent();
    // The mouse ray on the scrollbar plane in local coordinates (+0x3dc inverse), through the
    // TU's math engine DAT_0067d9fc.
    FVector RayToLocal(float const* Ray) const;
    // The same, but MouseEvent transforms through FUN_004e7090 (DAT_00674b1c).
    FVector RayToLocalViaHud(float const* Ray) const;

    bool bActive_;                     // +0x32c — ctor 1; gates the draw hover tint and the drag
    bool bCustomColor_;                // +0x32d — ctor 0; draw uses +0x32e for every part
    unsigned char CustomColor_[4];     // +0x32e — ctor DAT_00679b94 (80 80 80 ff)
    float Width_;                      // +0x334 — payload +0x00
    float Height_;                     // +0x338 — payload +0x04
    FScrollHitRect TopArrow_;          // +0x33c — up (left) arrow
    FScrollHitRect BottomArrow_;       // +0x350 — down (right) arrow
    FScrollHitRect TrackBefore_;       // +0x364 — track before the thumb
    FScrollHitRect TrackAfter_;        // +0x378 — track after the thumb
    FScrollHitRect Thumb_;             // +0x38c
    // +0x3a0 allocator byte, +0x3a4..+0x3ac vector — {&TopArrow_, &BottomArrow_, &Thumb_}.
    std::vector<FScrollHitRect*> HitRects_;
    float ArrowSize_;                  // +0x3b0 — min(Width, Height) * 0.2
    bool bStepMode_;                   // +0x3b4 — ctor 0; set by owners (see SetStepMode)
    int DragPart_;                     // +0x3b8 — ctor / MouseLeave -1; 0 = up arrow held,
                                       //          1 = down arrow held (auto-repeat in the draw)
    float RepeatTimer_;                // +0x3bc — MouseEvent sets 0.3f, the draw counts it
                                       //          down by the frame time and repeats below 0
    float RangeMin_;                   // +0x3c0 — payload +0x08
    float RangeMax_;                   // +0x3c4 — payload +0x0c
    float Value_;                      // +0x3c8
    float Page_;                       // +0x3cc — payload +0x10
    float Step_;                       // +0x3d0 — payload +0x14
    bool bDragging_;                   // +0x3d4
    float DragOffset_;                 // +0x3d8
    float InverseLayout_[12];          // +0x3dc — ctor identity; SetPosition inverse of +0x3c
    unsigned long ArrowIdleColor_;     // +0x40c — DAT_00679bb0
    unsigned long ArrowHotColor_;      // +0x410 — DAT_00679bb0
    unsigned long BarIdleColor_;       // +0x414 — DAT_00679bb0
    unsigned long BarHotColor_;        // +0x418 — DAT_00679bb0
    float ScrollOffset_[3];            // +0x41c
    float DefaultPage_;                // +0x428 — ctor +0x3cc (0), payload +0x10, SetPage
    bool bHorizontal_;                 // +0x42c — payload +0x18
};

// UICycle — the same object with its own draw and input: only the two arrows, stepping by
// +0x3d0. Every other slot is UIScroll's.
class UiCycle : public UiScroll
{
public:
    // slot 13 (+0x34) — LAB_00597560.
    void Draw() override;
    // slot 19 (+0x4c) — LAB_00597760 (RET 4).
    bool MouseEvent(FWin3DMouseEvent const* Event) override;
};

// FUN_00595400 -- clear the UI ShapeDB (DAT_0067d9c8) if UITextures.ini was loaded.
void UiScrollClearShapes();
