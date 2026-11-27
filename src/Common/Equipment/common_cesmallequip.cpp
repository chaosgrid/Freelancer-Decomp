// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEArmor, CECargoPod, CERepairDroid, CETradeLaneEquip. Declared in common_cequip.h; helpers
// shared with the sibling TUs are in common_cequip_internal.h (namespace CmnCequip).
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

CEArmor::CEArmor(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Armor const* ArchObj,
                 bool bMissionFlag)
    : CInternalEquip(EquipTypeArmor, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    Activate(true);
}

CEArmor::CEArmor(CEArmor const& Other)
    : CInternalEquip(Other)
{
}

CEArmor::~CEArmor()
{
    // RVA 0x351d0: the non-virtual CEquip::Destroy().
    CEquip::Destroy();
}

Archetype::Armor const* CEArmor::ArmorArch() const
{
    return static_cast<Archetype::Armor const*>(Arch);
}

CECargoPod::CECargoPod(CEqObj* OwnerObj, unsigned short EquipId, Archetype::CargoPod const* ArchObj,
                       bool bMissionFlag)
    : CAttachedEquip(EquipTypeCargoPod, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    Cargo = nullptr;
    Activate(true);
}

CECargoPod::CECargoPod(CECargoPod const& Other)
    : CAttachedEquip(Other)
    , Cargo(Other.Cargo)
{
}

CECargoPod::~CECargoPod()
{
    // RVA 0x358f0: EmptyPod inlined, then the non-virtual CExternalEquip::Destroy().
    EmptyPod();
    CExternalEquip::Destroy();
}

// RVA 0x35980; no null test.
void CECargoPod::FillPod(CECargo* CargoObj)
{
    Cargo = CargoObj;
    CargoObj->CargoPod = this;
}

void CECargoPod::EmptyPod()
{
    // RVA 0x35990
    if (Cargo != nullptr)
    {
        Cargo->CargoPod = nullptr;
        Cargo = nullptr;
    }
}

bool CECargoPod::IsLootable() const
{
    // RVA 0x35950
    return Cargo != nullptr && Cargo->Count != 0;
}

CECargoPod* __cdecl CECargoPod::cast(CEquip* Equip)
{
    // RVA 0x359a0
    if (Equip == nullptr || Equip->GetType() != EquipTypeCargoPod)
        return nullptr;
    return static_cast<CECargoPod*>(Equip);
}

CECargoPod const* __cdecl CECargoPod::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast((CEquip*)Equip);
}

CERepairDroid::CERepairDroid(CEqObj* OwnerObj, unsigned short EquipId,
                             Archetype::RepairDroid const* ArchObj, bool bMissionFlag)
    : CInternalEquip(EquipTypeRepairDroid, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    // RVA 0x3fbc0. Deviation: retail leaves HitPts unwritten; the port zeroes it.
    HitPts = 0.0f;
}

CERepairDroid::CERepairDroid(CERepairDroid const& Other)
    : CInternalEquip(Other)
    , HitPts(Other.HitPts)
{
    // RVA 0x34e00
}

CERepairDroid::~CERepairDroid()
{
    // RVA 0x3fbf0: the non-virtual CEquip::Destroy(), so CEquipManager's plain deletes still
    // notify.
    CEquip::Destroy();
}

float CERepairDroid::GetHitPoints() const
{
    // RVA 0x34df0
    return HitPts;
}

void CERepairDroid::SetHitPoints(float Hit)
{
    // RVA 0x3fc50: the virtual call is Activate(false) (vtable +0x30), not a recursive
    // SetHitPoints as Ghidra suggests.
    HitPts = Hit;
    if (Hit <= 0.0f)
        Activate(false);
}

CERepairDroid* __cdecl CERepairDroid::cast(CEquip* Equip)
{
    // RVA 0x3fc80
    if (Equip == nullptr || Equip->GetType() != EquipTypeRepairDroid)
        return nullptr;
    return static_cast<CERepairDroid*>(Equip);
}

CERepairDroid const* __cdecl CERepairDroid::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast((CEquip*)Equip);
}


// ============================================================
// CETradeLaneEquip — tradelane jump equipment
// ============================================================

CETradeLaneEquip::CETradeLaneEquip(CEqObj* OwnerObj, unsigned short EquipId,
                                   Archetype::TradeLaneEquip const* ArchObj, bool bMissionFlag)
    : CInternalEquip(EquipTypeTradeLane, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
}

CETradeLaneEquip::CETradeLaneEquip(CETradeLaneEquip const& Other)
    : CInternalEquip(Other)
{
}

CETradeLaneEquip::~CETradeLaneEquip()
{
    // FUN_06294ef0: the non-virtual CEquip::Destroy.
    CEquip::Destroy();
}

CETradeLaneEquip* __cdecl CETradeLaneEquip::cast(CEquip* Equip)
{
    // FUN_0629fca0, shared by both overloads.
    if (Equip == nullptr || Equip->GetType() != EquipTypeTradeLane)
        return nullptr;
    return static_cast<CETradeLaneEquip*>(Equip);
}

CETradeLaneEquip const* __cdecl CETradeLaneEquip::cast(CEquip const* Equip)
{
    return cast((CEquip*)Equip);
}

Archetype::TradeLaneEquip const* CETradeLaneEquip::TradeLaneArch() const
{
    return static_cast<Archetype::TradeLaneEquip const*>(Arch);
}

Archetype::RepairDroid const* CERepairDroid::RepairDroidArch() const
{
    return static_cast<Archetype::RepairDroid const*>(Arch);
}

CEArmor* __cdecl CEArmor::cast(CEquip* Equip)
{
    // RVA 0x35220
    if (Equip == nullptr || Equip->GetType() != EquipTypeArmor)
        return nullptr;
    return static_cast<CEArmor*>(Equip);
}

CEArmor const* __cdecl CEArmor::cast(CEquip const* Equip)
{
    return cast((CEquip*)Equip);
}

void CECargoPod::Destroy()
{
    // RVA 0x35970
    CExternalEquip::Destroy();
}

CECargo const* CECargoPod::GetContents() const
{
    // RVA 0x369c0 (ICF-shared).
    return Cargo;
}
