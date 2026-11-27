// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CECargo (retail CECargo.cpp). Declared in common_cequip.h; helpers shared with the sibling
// TUs are in common_cequip_internal.h (namespace CmnCequip).
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
#include "Equipment/common_cequip_internal.h"   // CallDacomStricmp

using namespace CmnCequip;

namespace
{
    const char kCECargoSource[] = "E:\\FL\\Scratch\\Source\\Common\\CECargo.cpp";
}

CECargo::CECargo(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Equipment const* ArchObj,
                 bool bMissionFlag)
    : CInternalEquip(EquipTypeCargo, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    CargoPod = nullptr;
    HitPts = 0.0f;
    Count = 0;
    CEquip::Activate(false);   // non-virtual
}

CECargo::CECargo(CECargo const& Other)
    : CInternalEquip(Other)
    , CargoPod(Other.CargoPod)
    , HitPts(Other.HitPts)
    , Count(Other.Count)
{
}

CECargo::~CECargo()
{
    // RVA 0x354b0: the same pod unlink as Destroy(), then the non-virtual CEquip::Destroy().
    if (CargoPod != nullptr)
        CargoPod->EmptyPod();
    CEquip::Destroy();
}

bool CECargo::IsDestroyed() const
{
    if (Count != 0)
        return CEquip::IsDestroyed();
    return true;
}

void CECargo::Destroy()
{
    // RVA 0x357f0: EmptyPod() clears both sides of the link.
    if (CargoPod != nullptr)
        CargoPod->EmptyPod();
    CEquip::Destroy();
}

void CECargo::Init(float HealthFrac, unsigned StackCount, CacheString const& Hardpoint)
{
    // FUN_06295510
    HitPts = GetMaxHitPoints() * HealthFrac;
    Count = StackCount;

    char const* Hp = Hardpoint.Value;
    if (Hp[0] == '\0')
        return;
    if (CallDacomStricmp(Hp, EquipDesc::CARGO_BAY_HP_NAME.Value) == 0)
        return;

    // The first connected cargo pod on Hp; the walk ends at the first equip the cast rejects.
    CEquipTraverser Walk(EquipTypeCargoPod);
    CECargoPod* Pod = CECargoPod::cast(Owner->EquipManager.Traverse(Walk));
    while (Pod != nullptr)
    {
        if (Pod->IsConnected() && CallDacomStricmp(Pod->GetParentHPName().Value, Hp) == 0)
        {
            CargoPod = Pod;
            Pod->FillPod(this);
            break;
        }
        Pod = CECargoPod::cast(Owner->EquipManager.Traverse(Walk));
    }

    if (CargoPod != nullptr)
        return;

    FDUMP(kFdumpError,
          "%s(%d) : *** ERROR: %s cannot find a cargo pod attached to hardpoint %s for %s.",
          kCECargoSource, 0x39, IdentifyEquipment(), Hp,
          Owner->Archetype->Nickname.get_string());
}

void CECargo::AddToStack(unsigned Amount, float HealthFrac)
{
    // FUN_06295630. GetRelativeHealth is left unrounded (x87). NaN takes the `!(x < 1)` branch.
    if (!(GetRelativeHealth() < 1.0))
    {
        Count += Amount;
    }
    else
    {
        if (!(HealthFrac < 1.0f))
        {
            Count += Amount;                            // no SetHitPoints
            return;
        }
        HealthFrac = (float)(GetRelativeHealth() + HealthFrac);
        if (!(HealthFrac < 1.0f))
        {
            Count += Amount;
            HealthFrac -= 1.0f;
        }
        else
        {
            Count += Amount - 1;
        }
    }
    SetHitPoints(GetMaxHitPoints() * HealthFrac);
}

void CECargo::RemoveFromStack(unsigned Amount)
{
    if (Count < Amount)
        Count = 0;
    else
        Count -= Amount;
    SetHitPoints(GetMaxHitPoints());
}

float CECargo::GetHitPoints() const
{
    // RVA 0x35810
    return HitPts;
}

void CECargo::SetHitPoints(float Hit)
{
    // RVA 0x35820; overrides the base CEquip no-op.
    HitPts = Hit;
}

bool CECargo::GetEquipDesc(EquipDesc& Desc) const
{
    // FUN_06295750. Unlike CEquip::GetEquipDesc: fails on Count == 0, not equipped, the
    // hardpoint from the linked pod (else internal). No Arch null test; the health is the
    // inline HitPts / Arch->HitPts, not the virtual GetRelativeHealth.
    if (Count == 0)
        return false;
    Desc.set_arch_id(Arch->get_id());
    Desc.set_id(Id);
    Desc.set_status(HitPts / Arch->HitPts);
    Desc.Count = (int)Count;
    Desc.set_equipped(false);
    Desc.bMission = bMission;
    if (CargoPod != nullptr)
    {
        Desc.set_hardpoint(CargoPod->GetParentHPName());
        return true;
    }
    Desc.make_internal();
    return true;
}

unsigned CECargo::GetCount() const
{
    // RVA 0xdbaf0
    return Count;
}

void CECargo::SetCount(unsigned NewCount)
{
    // RVA 0xdbae0
    Count = NewCount;
}

float CECargo::GetDecayDamagePerSecond() const
{
    // FUN_06295710; no Arch null test.
    if (Arch->get_class_type() != Archetype::AClassCommodity)
        return 0.0f;
    return ((Archetype::Commodity*)Arch)->get_decay_per_second();
}

CECargo* __cdecl CECargo::cast(CEquip* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeCargo)
        return nullptr;
    return (CECargo*)Equip;
}

CECargo const* __cdecl CECargo::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast((CEquip*)Equip);
}

unsigned CECargo::GetType() const
{
    // RVA 0x35740
    return EquipTypeCargo;
}

CECargoPod const* CECargo::GetContainer() const
{
    // RVA 0x3d880 (ICF-shared).
    return CargoPod;
}
