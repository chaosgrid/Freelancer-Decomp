// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// UIWinObject — PAGE_*.txt mesh widget (RTTI .?AUUIWinObject@@ : WinObject : Win3D).
//
// WINFACTORY<UIWinObject> DAT_0067dac0; Create FUN_0059c940 (operator new(0x100)).
// vftable 0x005e5d34 overrides WinObject's slots 0 (FUN_0059ce20), 5, 8, 9 and 13. Slot 13
// (0x0059ab50) is a dead debug frame (DAT_0067dbc4 has no writer) before WinObject's Draw, so
// the port inherits WinObject::Draw.

#include "Interface/win_object.h"

#include <stddef.h>

class UiWinObject : public WinObject
{
public:
    UiWinObject();
    ~UiWinObject() override;

    // slot 5 (+0x14) — LAB_0059ab10.
    void SetPosition(Vector const* NewPosition) override;
    // slot 8 (+0x20) — LAB_0059a900.
    void SetTransform(Transform const* NewXform) override;
    // slot 9 (+0x24) — FUN_0059a760: false when Payload is null or the mesh load fails.
    bool Create(Win3D* NewParent, void* Payload) override;

    FClientMeshHolder const& Mesh() const { return MeshHolder; }

private:
    float Width_;                           // +0xac — payload +0x00
    float Height_;                          // +0xb0 — payload +0x04
    float Depth_;                           // +0xb4 — payload +0x0c (5th float)
    float PageBlock_[12];                   // +0xb8 — SetTransform argument copy (ctor: identity)
    float Origin_[3];                       // +0xe8 — Block * (0, 0, 0)
    float Corner_[3];                       // +0xf4 — Block * (Width, -Height, 0)
};

static_assert(sizeof(UiWinObject) == 0x100, "retail UIWinObject is 0x100 bytes");
