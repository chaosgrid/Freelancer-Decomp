// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// UIShipViewControl -- the PAGE_*.page ship-preview widget (retail UIWin.cpp): a ShipViewControl
// whose size comes from the page line ("UIShipViewControl name x y w h", FUiPayloadBounds) and
// whose view box follows the widget's transform.
//
// WINFACTORY<UIShipViewControl> DAT_0067da3c; Create FUN_0059ccf0 (operator new(0x57c)).
// vftable 0x005e59a4 (+0x7c face 0x005e5998) replaces ShipViewControl's slots 0 (FUN_0059bfd0),
// 8 and 9. Past ShipViewControl (0x570 bytes) only the view box's half extent at +0x570.

#include "Interface/ship_view_control.h"

class UiShipViewControl : public ShipViewControl
{
public:
    // Inlined into FUN_0059ccf0; +0x570..+0x578 are written by Create.
    UiShipViewControl();
    // FUN_0059bff0.
    ~UiShipViewControl() override;

    // slot 8 (+0x20) -- LAB_0059c100 (RET 4).
    void SetTransform(Transform const* NewXform) override;
    // slot 9 (+0x24) -- FUN_0059c010 (RET 8).
    bool Create(Win3D* NewParent, void* Payload) override;

    float HalfExtent[3]; // +0x570

private:
    UiShipViewControl(UiShipViewControl const&) = delete;
    UiShipViewControl& operator=(UiShipViewControl const&) = delete;
};
