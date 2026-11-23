// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// SubObjectID: collision-group / equipment / shield sub-object id makers and range tests.
#include "common_cobject.h"

// ============================================================================
// SubObjectID implementation
// ============================================================================

namespace SubObjectID
{
    // FUN_06334550. Collision-group ids start at 3 (0/1/2 are reserved).
    CollGroupIdMaker::CollGroupIdMaker()
        : NextId(3)
    {
    }

    unsigned short CollGroupIdMaker::CreateGroupID()
    {
        // FUN_06334560, ICF-folded with CreateEquipID/CreateShieldID. Pre-increment.
        ++NextId;
        return NextId;
    }

    void CollGroupIdMaker::Reset()
    {
        // FUN_06334570
        NextId = 3;
    }

    // FUN_06334580
    EquipIdMaker::EquipIdMaker()
        : NextId(0x21)
    {
    }

    unsigned short EquipIdMaker::CreateEquipID()
    {
        // FUN_06334560 (ICF-folded, see CreateGroupID)
        ++NextId;
        return NextId;
    }

    void EquipIdMaker::Reset()
    {
        // FUN_06334590
        NextId = 0x21;
    }

    // FUN_063345a0
    ShieldIdMaker::ShieldIdMaker()
        : NextId(0xfff0)
    {
    }

    unsigned short ShieldIdMaker::CreateShieldID()
    {
        // FUN_06334560 (ICF-folded, see CreateGroupID)
        ++NextId;
        return NextId;
    }

    void ShieldIdMaker::Reset()
    {
        // FUN_063345b0
        NextId = 0xfff0;
    }

    bool IsArchGroupID(unsigned short Id)
    {
        // FUN_063344f0. Range checks, not bit tests.
        return Id >= 3 && Id < 0x21;
    }

    bool IsEquipID(unsigned short Id)
    {
        // FUN_06334510. Also covers the shield sub-range; callers test IsShieldEquipID first
        // to tell shields apart.
        return Id >= 0x21 && Id < 0xffff;
    }

    bool IsShieldEquipID(unsigned short Id)
    {
        // FUN_06334530
        return Id >= 0xfff0 && Id < 0xffff;
    }
}
