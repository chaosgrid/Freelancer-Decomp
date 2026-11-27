// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEMineDropper, CECounterMeasureDropper. Declared in common_cequip.h; helpers shared with the
// sibling TUs are in common_cequip_internal.h (namespace CmnCequip).
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

CEMineDropper::CEMineDropper(CEqObj* OwnerObj, unsigned short EquipId,
                             Archetype::MineDropper const* ArchObj, bool bMissionFlag)
    : CELauncher(EquipTypeMineDropper, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
}

CEMineDropper::CEMineDropper(CEMineDropper const& Other)
    : CELauncher(Other)
{
}

CEMineDropper::~CEMineDropper()
{
    // ICF-folded with ~CECounterMeasureDropper; the work is in ~CELauncher.
}

FireResult CEMineDropper::CanFire(Vector const& Target) const
{
    // RVA 0x3a270: no mine drops while the owning ship cruises.
    FireResult Result = CELauncher::CanFire(Target);
    if (Result == FireOk && Owner != nullptr &&
        (Owner->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
    {
        CShip* Ship = static_cast<CShip*>(Owner);
        if (Ship->is_cruise_active())
            return FireCruise;
    }
    return Result;
}

CEMineDropper* __cdecl CEMineDropper::cast(CEquip* Equip)
{
    // RVA 0x3a2d0
    if (Equip == nullptr || Equip->GetType() != EquipTypeMineDropper)
        return nullptr;
    return static_cast<CEMineDropper*>(Equip);
}

CEMineDropper const* __cdecl CEMineDropper::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast((CEquip*)Equip);
}

CECounterMeasureDropper::CECounterMeasureDropper(CEqObj* OwnerObj, unsigned short EquipId,
                                                 Archetype::CounterMeasureDropper const* ArchObj,
                                                 bool bMissionFlag)
    : CELauncher(EquipTypeCmDropper, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
}

CECounterMeasureDropper::CECounterMeasureDropper(CECounterMeasureDropper const& Other)
    : CELauncher(Other)
{
}

CECounterMeasureDropper::~CECounterMeasureDropper()
{
}

bool CECounterMeasureDropper::Update(float Dt, unsigned Flags)
{
    // FUN_0629a320: the first scanned missile targeting the owner within AiRangeSq fires
    // Notify(IncomingMissileDetected) and ends the walk. The tail is CELauncher::Update inlined.
    if (IsFunctioning() && (Flags & 0x20) == 0)
    {
        Archetype::CounterMeasureDropper const* CmArch = static_cast<Archetype::CounterMeasureDropper const*>(Arch);
        if (CmArch->AiRangeSq > 0.0f)
        {
            // FindFirst's result is used as the scanner with no cast and no null test.
            CEScanner const* Scanner = static_cast<CEScanner const*>(Owner->EquipManager.FindFirst(EquipTypeScanner));
            for (int Index = 0; Index < Scanner->Scans.Count; ++Index)
            {
                Watchable const* Watched = Scanner->Scans.Entries[Index].target();
                if (Watched == nullptr)
                    continue;
                // Retail tests the adjusted pointer (IObjRW's Watchable base is at +8).
                IObjRW* Tracked = reinterpret_cast<IObjRW*>(((char*)Watched - 8));
                if (Tracked == nullptr)
                    continue;

                unsigned Kind = 0;                  // retail leaves the slot unset before the call
                Tracked->get_type(Kind);
                if (Kind != OBJ_GUIDED)
                    continue;

                IObjRW* SubObj = nullptr;
                Tracked->get_target(SubObj);
                if (SubObj == nullptr)
                    continue;
                if (SubObj->get_id() != Owner->Id)
                    continue;

                Vector ShipPos;
                GetCenterOfMass(ShipPos);
                Vector const& TrackedPos = Tracked->get_position();

                // Kept on the x87 stack, summed as (dz^2 + dy^2) + dx^2.
                double const Dx = (double)TrackedPos.x - ShipPos.x;
                double const Dy = (double)TrackedPos.y - ShipPos.y;
                double const Dz = (double)TrackedPos.z - ShipPos.z;
                if (Dz * Dz + Dy * Dy + Dx * Dx < CmArch->AiRangeSq)
                {
                    Notify(INotify::IncomingMissileDetected, nullptr);
                    break;
                }
            }
        }
    }

    return CELauncher::Update(Dt, Flags);
}

CECounterMeasureDropper* __cdecl CECounterMeasureDropper::cast(CEquip* Equip)
{
    // RVA 0x3a4a0
    if (Equip == nullptr || Equip->GetType() != EquipTypeCmDropper)
        return nullptr;
    return static_cast<CECounterMeasureDropper*>(Equip);
}

CECounterMeasureDropper const* __cdecl CECounterMeasureDropper::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast((CEquip*)Equip);
}

Archetype::MineDropper const* CEMineDropper::MineDropperArch()
{
    return static_cast<Archetype::MineDropper const*>(Arch);
}

Archetype::Mine const* CEMineDropper::MineArch()
{
    // RVA 0x369c0 (ICF-shared).
    return static_cast<Archetype::Mine const*>(Projectile);
}

Archetype::CounterMeasureDropper const* CECounterMeasureDropper::CounterMeasureDropperArch()
{
    return static_cast<Archetype::CounterMeasureDropper const*>(Arch);
}

Archetype::CounterMeasure const* CECounterMeasureDropper::CounterMeasureArch()
{
    // RVA 0x369c0 (ICF-shared).
    return static_cast<Archetype::CounterMeasure const*>(Projectile);
}
