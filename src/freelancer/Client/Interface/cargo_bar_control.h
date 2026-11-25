// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail CargoBarControl (RTTI .?AUCargoBarControl@@ : WinControl) -- the hold bar of the
// NeuroNet inventory and dealer windows and the HUD cargo gauge: a quad strip filled to
// Used / Capacity, with an optional highlighted head (+0x33c) and tail (+0x338) segment, a
// line-loop border and a "used/capacity" tooltip.
//
// WINFACTORY<CargoBarControl> "CargoBarControl": ctor FUN_0055fcb0, Create FUN_0055fd20
// (operator new(0x350)). Also built directly by WinControl::CreateCargoBarControl.
//
// vftable 0x005e1924 (45 slots) -- WinControl's with the overrides declared below; slot 0 is
// FUN_0055e9f0 (scalar deleting dtor, WinControl's body only), slots 6 and 19 stay Win3D's /
// WinControl's.
//
// "color" (+0x340) and "color_highlight" (+0x344) are RGBA bytes; the ctor sets the highlight
// to 0xff,0xff,0,0xff. +0x34b is set and cleared by properties 0x2c / 0x2d.

#include "Interface/wincontrol_base.h"

#include <stddef.h>
#include <stdint.h>

class CargoBarControl : public WinControl
{
public:
    // FUN_0055e930
    CargoBarControl();

    // FUN_0055ee80 (RET 0x20) -- the argument list of FUN_005a1080 after the name.
    void Init(char const* MeshPath, float const* NewOffset, char const* Hardpoint,
              float const* NewHalfExtents, unsigned char bNewHorizontal, int NewToolTip2Ids,
              unsigned NewColor, unsigned NewHighlightColor);

    // --- Win3D / WinControl overrides ---
    bool HitTest(float const* Ray) override;                       // slot 4  LAB_0055fa80
    void SetPosition(float const* NewPosition) override;           // slot 5  LAB_0055f5a0
    void SetOrientation(Matrix const* NewOrientation) override;     // slot 7  LAB_0055f5f0
    bool Create(Win3D* NewParent, void* Data) override;            // slot 9  LAB_0055eb40
    void Update() override;                                        // slot 11 LAB_0055f660
    void Draw() override;                                          // slot 13 LAB_0055ef60
    void MouseEnter(float const* Ray) override;                    // slot 15 FUN_0055fa00
    void MouseLeave(float const* Ray) override;                    // slot 16 FUN_0055fa40
    int GetHitPriority() override;                                 // slot 27 LAB_0055e9e0
    void RefreshAttachment() override;                             // slot 35 FUN_00401e90
    void GetAttachOrientation(Matrix* OutOrientation) override;    // slot 40 LAB_0055ea20
    void GetAttachPosition(Vector* OutPosition) override;          // slot 41 FUN_0055eaf0
    intptr_t SetProperty(int Msg, uintptr_t A, uintptr_t B) override; // slot 42 LAB_0055fb00

    int ToolTip2Ids;                  // +0x32c -- "tooltip2", the "%s %d/%d" caption prefix
    float Capacity;                   // +0x330
    float Used;                       // +0x334
    float TailSegment;                // +0x338
    float HeadSegment;                // +0x33c
    unsigned char Color[4];           // +0x340
    unsigned char HighlightColor[4];  // +0x344
    unsigned char bHorizontal;        // +0x348
    unsigned char bHovered;           // +0x349
    unsigned char bToolTip;           // +0x34a
    unsigned char Field34b;           // +0x34b ctor 0; property 0x2c 1, 0x2d 0; not read by the class
    unsigned char bBorder;            // +0x34c
    unsigned char bShow;              // +0x34d
    unsigned char Pad34e[2];          // +0x34e

private:
    CargoBarControl(CargoBarControl const&) = delete;
    CargoBarControl& operator=(CargoBarControl const&) = delete;
};

static_assert(offsetof(CargoBarControl, ToolTip2Ids) == 0x32c, "CargoBarControl +0x32c");
static_assert(offsetof(CargoBarControl, Color) == 0x340, "CargoBarControl +0x340");
static_assert(offsetof(CargoBarControl, bShow) == 0x34d, "CargoBarControl +0x34d");
static_assert(sizeof(CargoBarControl) == 0x350, "retail CargoBarControl is 0x350 bytes");

// The tooltip hover record (tooltip_hover.h) set-up retail inlines at each hover site: the
// per-site start values after construction and the caption copy. Construction and teardown
// (FUN_00444ee0) are the record's own ctor / dtor. NN_Inventory's SetProperty 0x4f uses these
// too.
struct FToolTipHoverInfo;
void ToolTipHoverInit(FToolTipHoverInfo* Info);
void ToolTipHoverSetCaption(FToolTipHoverInfo* Info, wchar_t const* Text);
