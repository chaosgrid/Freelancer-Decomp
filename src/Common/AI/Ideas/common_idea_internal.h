// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Port-only helpers shared by the BaseIdea family TUs (common_*idea.cpp); none is a
// retail function of its own. Consumers write `using namespace CmnIdea;` after their
// includes.

#include "common_ini.h"     // Vector
#include "common_goods.h"   // BaseWatcher
#include "common_iface.h"   // IObjInspect, IObjRW, ObjRwFromWatchable
#include "common_aisteer.h" // GetInterceptClearanceOfIface

#include <cstring>

namespace CmnIdea
{
    // Reinterpret a raw IEEE-754 bit pattern as float, for constants that do not
    // round-trip cleanly through a decimal literal (e.g. 0x3dc90fdb).
    inline float BitsToFloat(unsigned int Bits)
    {
        float Result;
        std::memcpy(&Result, &Bits, sizeof(Result));
        return Result;
    }

    // MoveIdea::SetPosition/SetWatchObject/Commit and SplineMoveIdea::SetContext/Commit
    // reach `*(BaseIdea::Context + 0xc)` (the IBehaviorManager's PathfindManager) and
    // write AvoidExcludeId and CachedTargetPosition in it. Retail dereferences both
    // levels unguarded (e.g. 062d584e/062d5865), and so do they.

    // BaseIdea::Handle is the owning SystemManager: retail reaches SelfTarget
    // (`[this+8] + 0x14fc`) and Thrust (`[this+8] + 0x18`) with plain ADDs and never
    // tests Handle.

    // FUN_062e4bb0 (GetInterceptClearanceOfIface, common_aiintercept.cpp). Both retail
    // sites (062d50a2, 062d92ea) pass SelfTarget and the watched object untested and
    // keep the ST0 return unrounded, so this forwards unguarded and returns double.
    inline double NavInterceptClearanceOfIface(FAiTarget* NavBlock, IObjInspect const* TargetIface)
    {
        return GetInterceptClearanceOfIface(NavBlock, TargetIface);
    }

    // The FDirectionPicker's watcher (this+0x28) downcast `EAX ? EAX - 8 : 0` from the
    // Watchable sub-object to the whole IObjRW.
    inline IObjRW* WatchedObjectOf(BaseWatcher const& Watcher)
    {
        return ObjRwFromWatchable(const_cast<Watchable*>(Watcher.target()));
    }
}
