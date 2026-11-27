// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// The tractor beam effect -- retail Client/space (no source path string survives in the
// class's own code). Built only by the tractor equipment-FX wrapper's activation
// (FUN_0052fa10): EffectAlloc(0x84), the Effect base constructor
// with type 0x2d (EFT_TRACTORBEAM), +0x6c..+0x80 zeroed, vtable PTR_LAB_005ddcec.
//
// PTR_LAB_005ddcec (9 slots, the Effect table layout):
//   +0x00 LAB_004f7ab0  FEffectBase::Start          +0x04 FUN_004f7bd0 FEffectBase::Kill
//   +0x08 LAB_005133b0  source position / range  +0x0c OnAddHost (empty)
//   +0x10 FUN_004b6c50  returns 1                +0x14 LAB_005133f0 -> FUN_004f7b50
//   +0x18 LAB_0052fba0  returns 1                +0x1c LAB_00512da0 the beam draw
//   +0x20 FUN_0052fbb0  scalar deleting destructor (FUN_004f8dd0, FUN_004f79c0(this, 0x84))

#include "Space/Effects/effect_base.h"

struct FClientEquip;
struct FTexShape;

struct FCliTractorBeam : FEffectBase
{
    FCliTractorBeam();
    float GetWorldPos(Vector* Out) override;
    unsigned char IsAlive() override { return 1; }
    unsigned char UpdateVisibility(float Dt, FSpaceCamera* Cam) override;
    unsigned char TickDraw(float Dt, FSpaceCamera* Cam) override { return 1; }
    unsigned char BuildGeometry(FSpaceCamera* Cam) override;

    FClientEquip* Owner;   // +0x6c the tractor's equipment-FX wrapper (FUN_00512be0)
    float Color[3];        // +0x70 the CliEQTractor's color bytes / 255 (DAT_005c7f68)
    FTexShape* LineShape;  // +0x7c G_EffectShapeDB "line" record
    FTexShape* BulbShape;  // +0x80 G_EffectShapeDB "bulb" record

    // FUN_00512be0: Owner, colour, "line" / "bulb" shapes (each a ShapeDB find,
    // FDUMP on a miss, false when missing), then the type-bucket link FUN_004f7af0. True on
    // success.
    unsigned char Init(FClientEquip* Owner);
};
static_assert(sizeof(FCliTractorBeam) == 0x84, "CliTractorBeam 0x84");

// Inlined in FUN_0052fa10. Null when the allocation failed.
FCliTractorBeam* CliTractorBeamCreate();
