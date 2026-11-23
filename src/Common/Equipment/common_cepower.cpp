// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEPower. Declared in common_cequip.h; helpers shared with the sibling TUs are in
// common_cequip_internal.h (namespace CmnCequip).
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"

#include "common_cobject.h"
#include "common_csys.h"
#include "common_fuses.h"
#include "common_iface.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_physys.h"
#include "common_spatial.h"
#include "common_timing.h"
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "ivp_physics/ivp_real_object.h"
#include "ivp_physics/ivp_controller.h"

#include <cstdarg>
#include <cstdint>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Equipment/common_cequip_internal.h"

using namespace CmnCequip;

CEPower::CEPower(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Power const* ArchObj,
                 bool bMissionFlag)
    : CInternalEquip(EquipTypePower, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    // The non-virtual CEquip::Activate (.text:0629f378).
    CEquip::Activate(true);
}

CEPower::CEPower(CEPower const& Other)
    : CInternalEquip(Other)
{
}

CEPower::~CEPower()
{
    // RVA 0x3f3b0: the non-virtual CEquip::Destroy(), so CEquipManager's plain deletes still
    // notify.
    CEquip::Destroy();
}

bool CEPower::Update(float Dt, unsigned Flags)
{
    // RVA 0x3f410: GetChargeRate inlined (IsDestroyed re-tested). The sums are compared
    // unrounded; equal, above or NaN store the cap.
    if (IsFunctioning())
    {
        float const ChargeRate = IsDestroyed() ? 0.0f : PowerArch()->ChargeRate;
        double const NewPower = (double)(ChargeRate) * Dt + Owner->Power;
        Owner->Power = (NewPower < Owner->MaxPower) ? (float)(NewPower) : Owner->MaxPower;

        // The thrust capacitor exists only on ships (Owner is null-tested only here).
        if (Owner != nullptr && (Owner->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
        {
            float const ThrustChargeRate =
                IsDestroyed() ? 0.0f : PowerArch()->ThrustChargeRate;
            CShip* Ship = (CShip*)(Owner);
            double const NewThrust = (double)ThrustChargeRate * Dt + Ship->ThrustPower;
            Ship->ThrustPower = (NewThrust < Ship->MaxThrustPower) ? (float)NewThrust : Ship->MaxThrustPower;
        }
    }
    return CEquip::Update(Dt, Flags);
}

float CEPower::GetChargeRate() const
{
    // FUN_06294b40; no Arch null or class test (likewise below).
    if (IsDestroyed())
        return 0.0f;
    return PowerArch()->ChargeRate;
}

float CEPower::GetCapacity() const
{
    // FUN_06294b60
    if (IsDestroyed())
        return 0.0f;
    return PowerArch()->Capacity;
}

float CEPower::GetThrustCapacity() const
{
    // FUN_06294ba0
    if (IsDestroyed())
        return 0.0f;
    return PowerArch()->ThrustCapacity;
}

float CEPower::GetThrustChargeRate() const
{
    // FUN_06294b80
    if (IsDestroyed())
        return 0.0f;
    return PowerArch()->ThrustChargeRate;
}

CEPower* __cdecl CEPower::cast(CEquip* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypePower)
        return nullptr;
    return (CEPower*)(Equip);
}

CEPower const* __cdecl CEPower::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

Archetype::Power const* CEPower::PowerArch() const
{
    return (Archetype::Power const*)(Arch);
}
