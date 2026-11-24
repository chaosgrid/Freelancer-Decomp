// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEEngine. Declared in common_cequip.h; helpers shared with the sibling TUs are in
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

CEEngine::CEEngine(CShip* OwnerObj, unsigned short EquipId, Archetype::Engine const* ArchObj,
                   bool bMissionFlag)
    : CInternalEquip(EquipTypeEngine, OwnerObj, EquipId, ArchObj, bMissionFlag)
    , CPhysControllerEquip()
{
    // FUN_06295c40. Deviation: retail leaves Opaque48 (+0x48) unwritten; the port zeroes it.
    LastWakeThrottle = 0.0f;
    memset(Opaque48, 0, sizeof(Opaque48));
    CruiseRampElapsed = 0.0f;
    CruiseChargeTimeElapsed = 0.0f;
    bCruiseActive = 0;

    // One ExhaustNozzleInfo per resolving "HpEngine0<N>" hardpoint, N = 1..NumExhaustNozzles.
    // The scratch name and handle are reset once, before the loop; the owner's archetype is
    // read with no null test.
    CacheString HpName;
    HpName.clear();
    long HpHandle = -1;
    HardpointInfo Info;
    Archetype::Ship* ShipArch = (Archetype::Ship*)(OwnerObj->Archetype);
    int const NumNozzles = ShipArch->NumExhaustNozzles;
    for (int Index = 1; Index <= NumNozzles; ++Index)
    {
        BuildNozzleHPName(Index, HpName);
        if (!FindHardpoint_OS(Owner->EngineInstanceId, HpName.Value, HpHandle, Info))
            continue;

        unsigned short SubObjId = OwnerObj->inst_to_subobj_id(HpHandle);

        // FUN_06296640 (vector insert), then the new element is filled in place.
        Nozzles.insert(Nozzles.end(), 1, ExhaustNozzleInfo());
        ExhaustNozzleInfo& NewRecord = Nozzles.back();
        NewRecord.SubObjId = SubObjId;
        NewRecord.Index = Index;
        NewRecord.ParentInstance = HpHandle;
        NewRecord.Offset.Rotation = Info.Orient;
        NewRecord.Offset.Translation = Info.Position;
    }

    // CEquip::Activate, not CEEngine::Activate.
    if (CEquip::Activate(true))
        EnableController();
    else
        DisableController();
}

// RVA 0x35a40: the bases' copies and the VC6 vector copy (exact-fit) inlined.
CEEngine::CEEngine(CEEngine const& Other)
    : CInternalEquip(Other)
    , CPhysControllerEquip(Other)
    , Nozzles(Other.Nozzles)
{
    LastWakeThrottle = Other.LastWakeThrottle;
    memcpy(Opaque48, Other.Opaque48, sizeof(Opaque48));
    CruiseRampElapsed = Other.CruiseRampElapsed;
    CruiseChargeTimeElapsed = Other.CruiseChargeTimeElapsed;
    bCruiseActive = Other.bCruiseActive;
}

CEEngine::~CEEngine()
{
    // FUN_06295e40: Destroy(), then the member and base destructors (~CPhysControllerEquip
    // disables the controller when enabled).
    Destroy();
}

CEEngine* __cdecl CEEngine::cast(CEquip* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeEngine)
        return nullptr;
    return (CEEngine*)(Equip);
}

CEEngine const* __cdecl CEEngine::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

void __cdecl CEEngine::BuildNozzleHPName(int Index, CacheString& Out)
{
    // FUN_06296600 -> FUN_06295e00 (vsprintf into a 0x100-byte buffer).
    char Scratch[0x100];
    sprintf(Scratch, "HpEngine0%d", Index);
    Out.Value = StringAlloc(Scratch, false);
}

// RVA 0x35ee0: with no nozzles left the engine works only when indestructible.
bool CEEngine::IsFunctioning() const
{
    bool bHasNozzleOrIndestructible;
    if (!Nozzles.empty())
    {
        bHasNozzleOrIndestructible = true;
    }
    else
    {
        Archetype::Engine* EngineArch = (Archetype::Engine*)(Arch);
        bHasNozzleOrIndestructible = EngineArch->bIndestructible;
    }
    return IsActive() && !IsDisabled() && !IsDestroyed() && IsControllerEnabled() &&
           bHasNozzleOrIndestructible;
}

// RVA 0x35f60
bool CEEngine::Activate(bool bOn)
{
    if (!bOn)
    {
        EngageCruise(false, false);
    }
    if (CEquip::Activate(bOn))
    {
        EnableController();
        return true;
    }
    DisableController();
    return false;
}

// FUN_062961a0
bool CEEngine::EnableController()
{
    if (!IsActive())
        return false;
    return ControlObject(Owner);
}

// FUN_062961c0 (this-adjusting thunk).
bool CEEngine::DisableController()
{
    return CPhysControllerEquip::DisableController();
}

namespace
{
    // The ship cast retail inlines before every CShip call in this file; callers use the result
    // without a null test.
    CShip* OwnerShipOrNull(CEqObj* Owner)
    {
        if (Owner != nullptr && (Owner->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
            return (CShip*)(Owner);
        return nullptr;
    }
}

bool CEEngine::Update(float Dt, unsigned Flags)
{
    // FUN_06295fa0. The compares below use the unrounded sums (x87); NaN takes the
    // not-less branches.
    if (IsFunctioning())
    {
        Archetype::Engine const* const EngineArch = (Archetype::Engine const*)(Arch);
        if (bCruiseActive)
        {
            if (CruiseChargeTimeElapsed < EngineArch->CruiseChargeTime)
            {
                float const Prev = CruiseChargeTimeElapsed;
                double const Next = (double)(Dt) + Prev;
                CruiseChargeTimeElapsed = (float)(Next);
                if (Prev < 0.0f)
                {
                    // Leaving the disrupted state: 2 (charging), or 3 for an instant charge.
                    if (Next >= 0.0)
                    {
                        int Code = 2;
                        if (!(EngineArch->CruiseChargeTime > 0.0f))
                            Code = 3;
                        Notify(INotify::EngineCruiseStateChanged, &Code);
                    }
                }
                else
                {
                    // Crossing the charge time: 3 (charged).
                    if (Prev < EngineArch->CruiseChargeTime &&
                        Next >= (double)(EngineArch->CruiseChargeTime))
                    {
                        int Code = 3;
                        Notify(INotify::EngineCruiseStateChanged, &Code);
                    }
                }
            }
        }
        else
        {
            // A disruption timer recovers towards 0.
            if (CruiseChargeTimeElapsed < 0.0f)
            {
                double const Next = (double)(Dt) + CruiseChargeTimeElapsed;
                CruiseChargeTimeElapsed = (Next < 0.0) ? (float)(Next) : 0.0f;
            }
        }

        // A changed, non-zero throttle wakes the controller.
        if ((CObject::NO_DYNAMICS & Flags) == 0)
        {
            float const Throttle = OwnerShipOrNull(Owner)->Throttle;
            if (Throttle != LastWakeThrottle && Throttle != 0.0f)
            {
                wakeup();
                LastWakeThrottle = Throttle;
            }
        }
    }
    return CEquip::Update(Dt, Flags);
}

bool CEEngine::EngageCruise(bool Engage, bool Instant)
{
    // FUN_06296250
    if (!IsActive() || (bool)(bCruiseActive) == Engage)
        return bCruiseActive != 0;
    bCruiseActive = Engage;

    Archetype::Engine const* const EngineArch = (Archetype::Engine const*)(Arch);
    if (Engage)
    {
        OwnerShipOrNull(Owner)->set_throttle(1.0f);
        if (Instant)
        {
            CruiseChargeTimeElapsed = EngineArch->CruiseChargeTime;
            int Code = 3;
            Notify(INotify::EngineCruiseStateChanged, &Code);
            wakeup();
            return bCruiseActive != 0;
        }
        // Kept on the x87 stack until the store.
        float const Thrust = EngineArch->ThrustMultiplier;
        double const Drag = (double)(OwnerShipOrNull(Owner)->get_linear_drag()) *
                            EngineEquipConsts::CRUISING_SPEED;
        CruiseRampElapsed = (float)(
            (double)(EngineEquipConsts::CRUISE_ACCEL_TIME) * Thrust / Drag);
        // A disrupted (negative) or NaN timer only wakes; no charge-time test.
        if (CruiseChargeTimeElapsed >= 0.0f)
        {
            int Code = 2;
            Notify(INotify::EngineCruiseStateChanged, &Code);
        }
        wakeup();
        return bCruiseActive != 0;
    }

    // Retail quirk: disengaging also sets the throttle to 1.0.
    OwnerShipOrNull(Owner)->set_throttle(1.0f);
    int Code;
    if (Instant)
    {
        CruiseChargeTimeElapsed = -EngineEquipConsts::CRUISE_DISRUPT_TIME;
        Code = 1;
    }
    else
    {
        if (CruiseChargeTimeElapsed > 0.0f)
            CruiseChargeTimeElapsed = 0.0f;
        Code = 0;
    }
    Notify(INotify::EngineCruiseStateChanged, &Code);
    CruiseRampElapsed = 0.0f;
    wakeup();
    return bCruiseActive != 0;
}

// RVA 0x36120: one Notify per removed nozzle.
void CEEngine::NotifyArchGroupDestroyed(unsigned short SubObjId)
{
    std::vector<ExhaustNozzleInfo>::iterator It = Nozzles.begin();
    while (It != Nozzles.end())
    {
        if (It->SubObjId == SubObjId)
        {
            It = Nozzles.erase(It);
            Notify(INotify::ArchGroupDestroyed, (void*)(uintptr_t)SubObjId);
        }
        else
        {
            ++It;
        }
    }
}

// RVA 0x361e0
float CEEngine::GetLinearDrag() const
{
    Archetype::Equipment* EquipmentArch = Arch;
    Archetype::Engine* EngineArch = (Archetype::Engine*)(EquipmentArch);
    return EngineArch->LinearDrag;
}

// FUN_062963e0. The sum stays unrounded in ST0 (CShip::get_linear_drag adds onto it), hence
// double.
double CEEngine::GetCruiseDrag() const
{
    Archetype::Engine const* const EngineArch = (Archetype::Engine const*)(Arch);
    return (double)(EngineArch->LinearDrag) + EngineEquipConsts::CRUISE_DRAG;
}

// FUN_062961f0. The products stay on the x87 stack until the float return.
float CEEngine::GetThrust(float Throttle) const
{
    Archetype::Engine const* const EngineArch = (Archetype::Engine const*)(Arch);
    double Result = Throttle;
    if (Throttle < 0.0f)
        Result = Result * EngineArch->ReverseFraction;
    return (float)(Result * EngineArch->ThrustMultiplier);
}

// FUN_06296220. The fallback is the constant 0.0 (a pre-staged stack slot), not the throttle.
float CEEngine::GetPowerDraw(float Throttle) const
{
    if (IsFunctioning())
    {
        Archetype::Equipment* EquipmentArch = Arch;
        Archetype::Engine* EngineArch = (Archetype::Engine*)(EquipmentArch);
        float AbsThrottle = Throttle;
        if (AbsThrottle < 0.0f)
            AbsThrottle = -AbsThrottle;
        return AbsThrottle * EngineArch->PowerDrawFactor;
    }
    return 0.0f;
}

// FUN_062963f0; no null test of the ship.
float CEEngine::GetCruiseThrust(float LinearDrag) const
{
    return (float)((double)(OwnerShipOrNull(Owner)->get_linear_drag()) *
                              EngineEquipConsts::CRUISING_SPEED * LinearDrag);
}

// RVA 0x36420
float CEEngine::GetCruisePowerDraw() const
{
    Archetype::Equipment* EquipmentArch = Arch;
    Archetype::Engine* EngineArch = (Archetype::Engine*)(EquipmentArch);
    return EngineArch->CruisePowerUsage;
}

bool CEEngine::IsCruiseEngaged() const
{
    // RVA 0x35a10
    return bCruiseActive != 0;
}

bool CEEngine::IsControllerEnabled() const
{
    // FUN_062961d0: the CPhysControllerEquip base's flag.
    return bControllerEnabled;
}

float CEEngine::GetCruiseChargeTimeElapsed() const
{
    // RVA 0x3ba30
    return CruiseChargeTimeElapsed;
}

// RVA 0x35a20
float CEEngine::GetCruiseChargeTime() const
{
    Archetype::Equipment* EquipmentArch = Arch;
    Archetype::Engine* EngineArch = (Archetype::Engine*)(EquipmentArch);
    return EngineArch->CruiseChargeTime;
}

void CEEngine::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // FUN_06296430 (CoreList is never read; retail's `this` is the CPhysControllerEquip
    // subobject). No null test of the ship. Without enough power for the step nothing is
    // stored or pushed. Products stay on the x87 stack until each float store.
    (void)CoreList;
    if (!IsFunctioning())
        return;

    CShip* const Ship = OwnerShipOrNull(Owner);

    Archetype::Engine const* EngineArch = (Archetype::Engine const*)(Arch);
    float const Dt = EventSim->delta_time;
    float Force;
    double PowerRate;
    if (bCruiseActive)
    {
        if (CruiseChargeTimeElapsed < EngineArch->CruiseChargeTime)
        {
            double Thrust = Ship->Throttle;
            if (Ship->Throttle < 0.0f)
                Thrust = Thrust * EngineArch->ReverseFraction;
            Force = (float)(Thrust * EngineArch->ThrustMultiplier);
        }
        else
        {
            // Charged: CruiseRampElapsed (+0x4c) advances. The sum is stored but compared and
            // divided unrounded.
            float Ramp = 1.0f;
            double const Accel = (double)(CruiseRampElapsed) + Dt;
            CruiseRampElapsed = (float)(Accel);
            if (Accel < EngineEquipConsts::CRUISE_ACCEL_TIME)
                Ramp = (float)(Accel / EngineEquipConsts::CRUISE_ACCEL_TIME);
            // The throttle is read before the call.
            float const Throttle = Ship->Throttle;
            Force = (float)((double)(OwnerShipOrNull(Owner)->get_linear_drag()) *
                                       EngineEquipConsts::CRUISING_SPEED * Throttle * Ramp);
        }
        PowerRate = EngineArch->CruisePowerUsage;
    }
    else
    {
        // GetPowerDraw inlined, unrounded.
        float const Throttle = Ship->Throttle;
        PowerRate = 0.0;
        if (IsFunctioning())
            PowerRate = (double)(fabsf(Throttle)) * EngineArch->PowerDrawFactor;
        double Thrust = Throttle;
        if (Throttle < 0.0f)
            Thrust = Thrust * EngineArch->ReverseFraction;
        Force = (float)(Thrust * EngineArch->ThrustMultiplier);
    }

    double const NewPower = (double)(Ship->Power) - PowerRate * Dt;
    if (NewPower < 0.0 || NewPower != NewPower)
        return;
    Ship->Power = (float)(NewPower);
    if (Force == 0.0f)
        return;

    Vector Dir;
    Dir.x = Ship->Orientation.E[0][2];
    Dir.y = Ship->Orientation.E[1][2];
    Dir.z = Ship->Orientation.E[2][2];
    push(Dir, -(Force * Dt));
}

Archetype::Engine const* CEEngine::EngineArch() const
{
    return (Archetype::Engine const*)(Arch);
}
