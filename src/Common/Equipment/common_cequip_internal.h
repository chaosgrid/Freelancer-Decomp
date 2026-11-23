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
#include "DALib.h"            // DALib::Engine, G_EngineJointPayloadDwords (FJointRelative)

// DACOM.DLL!stricmp (Common IAT 0639902c), defined in common_dacom_stricmp.cpp.
int CallDacomStricmp(const char* Left, const char* Right);

namespace CmnCequip
{
    const char kCAttachedEquipSource[] = "E:\\FL\\Scratch\\Source\\Common\\CAttachedEquip.cpp";
    const unsigned kAttachedEquipMask = 0x1fe0u;
    const long kInvalidEngineInstance = -1;

    void* WatchedTarget(BaseWatcher const& Watcher);

    // An engine joint block: what IEngine::GetInstanceRelative returns and CreateJoint copies
    // (EngBase owns the storage). Layout from FUN_062940c0, which builds a revolute one, and
    // from CEGun's readers.
    struct FJointRelative
    {
        unsigned long Type;   // +0x00  2 = revolute
        Vector ParentPos;     // +0x04  the parent hardpoint position
        Vector ChildPos;      // +0x10  the child hardpoint position
        Matrix Orient;        // +0x1c  parent * child hardpoint orientation
        Vector Axis;          // +0x40  the rotation axis in that frame
        float Min;            // +0x4c  angle limits
        float Max;            // +0x50
        // TODO: +0x54/+0x58 are never written by FUN_062940c0 (stack garbage in retail); a
        // revolute joint does not use them.
        unsigned long Field_0x54;
        unsigned long Field_0x58;
    };
    static_assert(sizeof(FJointRelative) == G_EngineJointPayloadDwords * sizeof(unsigned long),
                  "FJointRelative is the whole IEngine joint block");

    // IEngine+0xc4 for an instance id (retail never tests the result for null where it reads it).
    inline FJointRelative const* InstanceRelative(long Instance)
    {
        return (FJointRelative const*)DALib::Engine->GetInstanceRelative((void*)Instance);
    }
}
