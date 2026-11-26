// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEThruster. Declared in common_cequip.h; helpers shared with the sibling TUs are in
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
#include "ivp_physics/ivp_controller.h"  // IVP_Event_Sim (do_simulation_controller)

#include <cstdarg>
#include <cstdint>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Equipment/common_cequip_internal.h"

using namespace CmnCequip;

CEThruster::CEThruster(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Thruster const* ArchObj,
                       bool bMissionFlag)
    : CAttachedEquip(EquipTypeThruster, OwnerObj, EquipId, ArchObj, bMissionFlag)
    , CPhysControllerEquip()
{
    // FUN_0629d120: registers with the owner's physics at once (the non-virtual
    // ControlObject). The pad bytes +0x61..+0x63 are not written.
    bCouldThrust = false;
    CPhysControllerEquip::ControlObject(Owner);
}

CEThruster::~CEThruster()
{
    // FUN_0629d1b0: Destroy(), then the base destructors (~CPhysControllerEquip disables the
    // controller when enabled).
    CExternalEquip::Destroy();
}

float CEThruster::GetThrust() const
{
    // FUN_0629d2a0; no Arch test.
    return ThrusterArch()->MaxThrust;
}

float CEThruster::GetPowerDraw() const
{
    // FUN_0629d2b0; no Arch test.
    return ThrusterArch()->PowerUsage;
}

bool CEThruster::CouldThrust() const
{
    // FUN_0629d2c0
    return bCouldThrust;
}

void CEThruster::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // FUN_0629d2d0 (CoreList is never read; retail's `this` is the CPhysControllerEquip
    // subobject at +0x4c). The ship cast result is dereferenced with no null test. The cost
    // stays unrounded; above the pool or NaN notifies and returns. The push direction is read
    // from Owner, not the cast Ship.
    (void)CoreList;
    bCouldThrust = false;
    if (!IsFunctioning())
        return;

    CShip* Ship = nullptr;
    if (Owner != nullptr)
    {
        CObject* Candidate = Owner;
        if ((Candidate->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
            Ship = (CShip*)(Owner);
    }

    Archetype::Thruster const* ThrustArch = ThrusterArch();
    double const Cost = (double)ThrustArch->PowerUsage * (double)EventSim->delta_time;
    double const Pool = Ship->ThrustPower;
    if (!(Cost <= Pool))
    {
        Notify(INotify::NotEnoughPower, nullptr);
        return;
    }
    Ship->ThrustPower = (float)(Pool - Cost);

    Vector Dir;
    Dir.x = -Owner->Orientation.E[0][2];
    Dir.y = -Owner->Orientation.E[1][2];
    Dir.z = -Owner->Orientation.E[2][2];
    push(Dir, ThrustArch->MaxThrust * EventSim->delta_time);
    bCouldThrust = true;
}

bool CEThruster::IsControllerEnabled() const
{
    // FUN_0629d290: the CPhysControllerEquip base's flag.
    return bControllerEnabled;
}

bool CEThruster::Activate(bool bOn)
{
    // FUN_0629d240
    if (CEquip::Activate(bOn))
    {
        wakeup();
        return true;
    }
    bCouldThrust = false;
    return false;
}

bool CEThruster::EnableController()
{
    // FUN_0629d270: the virtual ControlObject of the CPhysControllerEquip base.
    return ControlObject(Owner);
}

bool CEThruster::DisableController()
{
    // FUN_0629d280 (this-adjusting thunk).
    return CPhysControllerEquip::DisableController();
}

CEThruster* __cdecl CEThruster::cast(CEquip* Equip)
{
    // RVA 0x3d3a0
    if (Equip == nullptr || Equip->GetType() != EquipTypeThruster)
        return nullptr;
    return (CEThruster*)(Equip);
}

CEThruster const* __cdecl CEThruster::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

Archetype::Thruster const* CEThruster::ThrusterArch() const
{
    return (Archetype::Thruster const*)(Arch);
}
