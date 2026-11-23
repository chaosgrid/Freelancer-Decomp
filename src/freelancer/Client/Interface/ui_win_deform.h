// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// UIWinDeform -- the PAGE_*.page character-preview widget (RTTI .?AUUIWinDeform@@ : Win3D, COL
// 0x005f0f64; retail UIWin.cpp). It shows one EngineCharacter body (a Costume's deformable plus
// its accessories) through a camera of its own laid over the widget's screen rectangle.
//
// WINFACTORY<UIWinDeform> DAT_0067da94; Create 0x0059cac0 (operator new(0x3f4)).
// vftable 0x005e5804 overrides Win3D's slots 0 (FUN_0059ae40), 5 (with the body's pose),
// 8 (with the camera viewport), 9, 11 (the body advanced in 0.2 s steps) and 13. The body build
// is FUN_0059b220 (FEngineCharacterBody::Build, engine_character.cpp).
//
// Layout past Win3D: +0x07c a std::string with no reader or writer anywhere in the exe;
// +0x08c the body (0x298); +0x324 the preview camera (0xc0); +0x3e4 / +0x3e8 width / height in
// world units; +0x3ec the pose Z (ctor -5.0f); +0x3f0 the body's height (ctor -1.0f).

#include "Common/engine_character.h"
#include "Space/Cameras/space_camera.h"
#include "Interface/win3d.h"

#include <string>

class UiWinDeform : public Win3D
{
public:
    // FUN_0059aca0.
    UiWinDeform();
    // FUN_0059ae60.
    ~UiWinDeform() override;

    // slot 5 (+0x14) -- LAB_0059bb40.
    void SetPosition(float const* NewPosition) override;
    // slot 8 (+0x20) -- LAB_0059b8f0.
    void SetTransform(float const* Transform) override;
    // slot 9 (+0x24) -- LAB_0059b8a0.
    bool Create(Win3D* NewParent, void* Payload) override;
    // slot 11 (+0x2c) -- LAB_0059bc30.
    void Update() override;
    // slot 13 (+0x34) -- LAB_0059bda0.
    void Draw() override;

    // FUN_0059b020 (thiscall, RET 4) -- the body rebuilt from the costume called Name, then
    // posed; a failed build FDUMPs a notice. Caller: PageCharacterCreate.
    void SetCostume(char const* Name);

    std::string Field_0x7c;       // +0x07c
    FEngineCharacterBody Body;    // +0x08c
    FSpaceCamera Camera;          // +0x324
    float Width;                  // +0x3e4
    float Height;                 // +0x3e8
    float PoseZ;                  // +0x3ec
    float PoseHeight;             // +0x3f0

private:
    // The body's pose that SetCostume and SetPosition both spell inline: a rotation of pi about
    // Y, the height (only with a deformable), position {0, 0, PoseZ}, then the rotation.
    void PoseBody();

    UiWinDeform(UiWinDeform const&) = delete;
    UiWinDeform& operator=(UiWinDeform const&) = delete;
};
