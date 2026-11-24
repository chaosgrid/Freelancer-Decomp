// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common.dll-internal helpers shared by common_behavior.cpp, common_basefollowerbehavior.cpp,
// common_formationbehavior.cpp and common_gunconvergence.cpp; defined in common_behavior.cpp.

#include "common_systemmanager.h" // SystemManager, IObjRW, CObject

struct CShip; // common_cobject.h

namespace CmnBehavior
{
    // IObjInspect::is_player on the owner (Sys->SelfTarget.Iface); no null tests, as retail.
    bool OwnerIsPlayer(SystemManager* Sys);

    // `[obj+0x4c] & 0x503 == 0x503` -- the object as a CShip if it is a ship, else null.
    CShip const* ShipOrNull(CObject const* Object);
}
