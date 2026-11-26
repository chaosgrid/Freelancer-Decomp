// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common-internal helpers shared by the Equipment TUs (namespace CmnCequip); the functions are
// defined in common_cequip.cpp. Not part of the public ABI. Retail calls the IEngine slots
// directly at every site, with no validity or null wrappers.

#include "common_cequip.h"
#include "common_cobject.h"   // CEqObj
#include "common_goods.h"     // BaseWatcher (WatchedTarget)
#include "DALib.h"            // DALib::Engine, FEngineJointBlock

// DACOM.DLL!stricmp (Common IAT 0639902c), defined in common_dacom_stricmp.cpp.
int CallDacomStricmp(const char* Left, const char* Right);

namespace CmnCequip
{
    const char kCAttachedEquipSource[] = "E:\\FL\\Scratch\\Source\\Common\\CAttachedEquip.cpp";
    const unsigned kAttachedEquipMask = 0x1fe0u;
    const long kInvalidEngineInstance = -1;

    void* WatchedTarget(BaseWatcher const& Watcher);

    // IEngine+0xc4 for an instance id (retail never tests the result for null where it reads it).
    inline FEngineJointBlock const* InstanceRelative(long Instance)
    {
        return DALib::Engine->GetInstanceRelative(Instance);
    }
}
