// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CECloakingDevice. Declared in common_cequip.h; helpers shared with the sibling TUs are in
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

CECloakingDevice::CECloakingDevice(CEqObj* OwnerObj, unsigned short EquipId,
                                   Archetype::CloakingDevice const* ArchObj, bool bMissionFlag)
    : CAttachedEquip(EquipTypeCloak, OwnerObj, EquipId, ArchObj, bMissionFlag)
    , FadeTarget(0)
    , BaselineState(0)
{
    // FUN_06294460. The pad bytes +0x52/+0x53 are not written; the timer is set only by
    // Activate and then overwritten with CloakOutTime.
    CECloakingDevice::Activate(true);
    CloakTimer = CloakArch()->CloakOutTime;
}

CECloakingDevice::CECloakingDevice(CECloakingDevice const& Other)
    : CAttachedEquip(Other)
    , CloakTimer(Other.CloakTimer)
    , FadeTarget(Other.FadeTarget)
    , BaselineState(Other.BaselineState)
{
    // RVA 0x32fb0. The pad bytes +0x52/+0x53 are not copied.
}

CECloakingDevice::~CECloakingDevice()
{
    // RVA 0x34520: the non-virtual CExternalEquip::Destroy(); ~CAttachedEquip does the rest.
    CExternalEquip::Destroy();
}

bool CECloakingDevice::Update(float Dt, unsigned Flags)
{
    // FUN_06294660. The fade compare uses the unrounded sum (x87); only a strictly greater,
    // ordered sum settles the fade. The tail is CAttachedEquip::Update inlined.
    if (!IsDestroyed() && FadeTarget != BaselineState)
    {
        Archetype::CloakingDevice const* CloakingArch = CloakArch();
        float const Total = (FadeTarget != 0) ? CloakingArch->CloakOutTime : CloakingArch->CloakInTime;
        double const Elapsed = (double)Dt + (double)CloakTimer;
        CloakTimer = (float)Elapsed;
        if (Elapsed > Total)
        {
            BaselineState = FadeTarget;
            CloakTimer = 0.0f;
        }
        if (Owner != nullptr)
            Owner->CloakPercent = (float)cloak_percent();
    }
    if (IsDestroyed() && Debris != nullptr)
        return true;
    return CExternalEquip::Update(Dt, Flags);
}

bool CECloakingDevice::Activate(bool bActive)
{
    // FUN_06294770. Mid-fade, the timer is rescaled from the unrounded cloak_percent (x87).
    if (IsActive() == bActive)
        return bActive;

    bool const Result = CEquip::Activate(bActive);
    unsigned char const NewFadeTarget = this->bActive; // CEquip::bActive, post-toggle
    unsigned char const OldFadeTarget = FadeTarget;
    if (OldFadeTarget == NewFadeTarget)
        return Result;
    if (OldFadeTarget == BaselineState)
    {
        FadeTarget = NewFadeTarget;
        CloakTimer = 0.0f;
        return Result;
    }
    Archetype::CloakingDevice const* CloakingArch = CloakArch();
    double const Frac = cloak_percent();
    if (OldFadeTarget != 0)
        CloakTimer = (float)((1.0 - Frac) * (double)CloakingArch->CloakInTime);
    else
        CloakTimer = (float)(Frac * (double)CloakingArch->CloakOutTime);
    FadeTarget = NewFadeTarget;
    return Result;
}

double CECloakingDevice::cloak_percent() const
{
    // FUN_062945d0. A fade time not greater than 0.001 (or NaN) counts as instant. The result
    // is left unrounded (x87).
    if (FadeTarget == BaselineState)
        return (BaselineState != 0) ? 1.0f : 0.0f;
    Archetype::CloakingDevice const* CloakingArch = CloakArch();
    if (FadeTarget != 0)
    {
        if (CloakingArch->CloakOutTime > 0.001)
            return (double)CloakTimer / (double)CloakingArch->CloakOutTime;
        return 1.0f;
    }
    double Frac = 0.0f;
    if (CloakingArch->CloakInTime > 0.001)
        Frac = (double)CloakTimer / (double)CloakingArch->CloakInTime;
    return 1.0 - Frac;
}

Archetype::CloakingDevice const* CECloakingDevice::CloakArch() const
{
    return (Archetype::CloakingDevice const*)(Arch);
}

CECloakingDevice* __cdecl CECloakingDevice::cast(CEquip* Equip)
{
    // RVA 0x34860
    if (Equip == nullptr || Equip->GetType() != EquipTypeCloak)
        return nullptr;
    return (CECloakingDevice*)(Equip);
}

CECloakingDevice const* __cdecl CECloakingDevice::cast(CEquip const* Equip)
{
    return cast(const_cast<CEquip*>(Equip));
}
