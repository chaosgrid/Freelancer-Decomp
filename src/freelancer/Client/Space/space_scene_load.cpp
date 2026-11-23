// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/space_scene_load.h"
#include "Space/Effects/jump_effect.h"
#include "Space/star_sphere.h"

#include "common_ini.h"

// The part of FUN_0054ab40 that precedes the system enter: FUN_005020a0 (jump-gate FX INI)
// and FUN_00510f90 (solar star archetypes). The system INI load itself is StarSys.cpp's
// (FUN_005545a0 / FUN_00553740, star_sys.cpp).

bool LoadSpaceSceneSlice()
{
    // FUN_005020a0 -- the jump-gate FX INI (JumpGateEffect / JumpShipEffect sections).
    JumpEffectIniLoad();
    ::LoadSolarStarArchIni(); // FUN_00510f90
    RefreshLoadingProgress();
    return true;
}
