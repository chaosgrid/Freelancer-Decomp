// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- RepairManager and its PotionHelper / HullHelper / ShieldHelper (SystemManager+0x14A8).

#include "common_systemmanager.h"
#include "common_aisteer.h"
#include "common_behavior.h"
#include "common_misc.h"      // SUBOBJ_ID_NONE
#include "common_iface.h"     // IObjInspect, ObjRwFromWatchable
#include "common_pathfind.h"
#include "common_iustategraph.h"
#include "common_cobject.h"    // CEqObj::get_behavior_interface
#include "common_cequip.h"
#include "common_cobjdb.h"
#include "dacom.h"             // DACOM_API

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <new>

// ============================================================================
// RepairManager / PotionHelper / HullHelper / ShieldHelper -- SystemManager+0x14A8.
// RTTI-attested (see common_systemmanager.h).
// ============================================================================
PotionHelper::PotionHelper()
    : State(kPotionStateArmed)  // +0x04
    , Type(0)                   // not written by retail's ctor (see header)
    , EquipId(SUBOBJ_ID_NONE)   // +0x0C
    , Count(0)                  // +0x10
    , PreUseDelay(2.0f)
    , PostUseDelay(5.0f)
    , UseAtRatio(0.35f)
    , Timer(0.0f)               // +0x20
{
    Pad0E[0] = 0;
    Pad0E[1] = 0;
}

// LAB_062e6400
void HullHelper::SetType()
{
    Type = Archetype::AClassRepairKit;
}

// FUN_062e6410. get_status's return code is ignored; 1.0f comes back when it writes nothing.
float HullHelper::GetRatio(IObjRW const* Object)
{
    float Health = 1.0f;
    (void)static_cast<IObjInspect const*>(Object)->get_status(Health);
    return Health;
}

// LAB_062e6430
void ShieldHelper::SetType()
{
    Type = Archetype::AClassShieldBattery;
}

// LAB_062e6440. get_shield_status is IObjInspect slot 18; its return code is ignored.
float ShieldHelper::GetRatio(IObjRW const* Object)
{
    bool bActive = false;
    float Health = 1.0f;
    (void)static_cast<IObjInspect const*>(Object)->get_shield_status(Health, bActive);
    return Health;
}

// FUN_062eceb0. States: 0 waits until GetRatio <= UseAtRatio (unordered does not fire),
// 1 waits PreUseDelay then uses the item, 2 waits PostUseDelay. Players are skipped with
// Timer untouched.
void PotionHelper::Update(float Dt, IObjRW* Object)
{
    if (static_cast<IObjInspect*>(Object)->is_player())
        return;

    // FST keeps the unrounded sum in ST0 for the state-1/2 compares; MSVCRT runs the FPU at
    // 53-bit precision, so double reproduces it and the float store rounds as FST does.
    double const Elapsed = static_cast<double>(Dt) + static_cast<double>(Timer);
    Timer = static_cast<float>(Elapsed);

    switch (State)
    {
    case kPotionStateArmed:
        if (Count <= 0)
            return;
        if (!(GetRatio(Object) <= UseAtRatio))
            return;
        Timer = 0.0f;
        State = kPotionStatePreUseDelay;
        return;
    case kPotionStatePreUseDelay:
        // Only greater-or-equal falls through (unordered returns).
        if (!(Elapsed >= PreUseDelay))
            return;
        static_cast<IObjAffect*>(Object)->use_item(EquipId, 1, -1);   // IObjAffect slot 22
        Timer = 0.0f;
        State = kPotionStatePostUseDelay;
        return;
    case kPotionStatePostUseDelay:
        if (!(Elapsed >= PostUseDelay))
            return;
        Timer = 0.0f;
        State = kPotionStateArmed;
        return;
    default:
        return;
    }
}

RepairManager::RepairManager()
    : Owner(nullptr)        // not written by retail's ctor (FUN_062e6490 writes it)
    , Hull()
    , Shield()
    , CheckTimer(0.0f)      // not written by retail's ctor (FUN_062e6490 zeroes it)
{
}

// FUN_062ecf60. Cargo enumerator callback: tallies repair kits and shield batteries; never stops
// the walk. Arch is the cargo's archetype (enumerate_cargo pushes CEquip+0xc).
bool RepairManager::enumerate(unsigned short const& Id, Archetype::Equipment const* Arch, int InCount)
{
    int const ClassType = static_cast<int>(Arch->get_class_type());
    if (ClassType == Archetype::AClassRepairKit)
    {
        Hull.EquipId = Id;
        Hull.Count = Hull.Count + InCount;
        return true;
    }
    if (ClassType == Archetype::AClassShieldBattery)

    {
        Shield.EquipId = Id;
        Shield.Count = Shield.Count + InCount;
    }
    return true;
}

// ============================================================================
// FUN_062ecfb0. Copies the repair timings from the personality of the formation leader (when
// bLeaderMakesMeTougher applies) or else of the owner. Structural twin of
// RefreshWeaponFireControlConfig (FUN_062eb6c0, common_aifirecontrol.cpp), including the
// Owner->bLeaderParamsBorrowed = 1 latch. Retail does not test ExternalIface or the get_sys() result.
// Ghidra names the get_sys() call CPlayerTradeOffer::GetTargetPlayerID (an ICF fold).
// Returns true on every path.
// ============================================================================
bool RepairManager::RefreshFromPersonality()
{
    SystemManager* const Owner = this->Owner;
    if (Owner == nullptr)
        return true;

    SystemManager* Source = Owner;

    if (!Owner->SelfTarget.Iface->is_player()
        && Owner->PersonalityData.FormationParams.bLeaderMakesMeTougher != 0
        && Owner->FormationLeaders.bLeaderSuppressed == 0)
    {
        // +0x1370 watcher; the node is tested against null and the literal 8 (node - 8).
        Watchable const* const Node = Owner->FormationLeaders.FormationLeaderWatcher.target();
        if (Node != nullptr && Node != reinterpret_cast<Watchable const*>(8))
        {
            IObjRW* const Handle = ObjRwFromWatchable(const_cast<Watchable*>(Node));
            CObject const* const Obj = Handle->cobject();   // IObjInspect slot 84

            if (Obj != nullptr && (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
            {
                IBehaviorManager* const Iface =
                    const_cast<CEqObj*>(static_cast<CEqObj const*>(Obj))->get_behavior_interface();
                if (Iface != nullptr)
                {
                    Source = Iface->get_sys();
                    Owner->bLeaderParamsBorrowed = 1;   // on the owner, not on Source
                }
            }
        }
    }

    // Retail's copy order.
    pub::AI::Personality::RepairUseStruct const& R = Source->PersonalityData.RepairParams;
    Hull.PreUseDelay = R.UseHullRepairPreDelay;                // [EDI+0x16f4] -> [ESI+0x1c]
    Hull.PostUseDelay = R.UseHullRepairPostDelay;              // [EDI+0x16fc] -> [ESI+0x20]
    Hull.UseAtRatio = R.UseHullRepairAtDamagePercent;          // [EDI+0x16f8] -> [ESI+0x24]
    Shield.PreUseDelay = R.UseShieldRepairPreDelay;            // [EDI+0x16e8] -> [ESI+0x40]
    Shield.PostUseDelay = R.UseShieldRepairPostDelay;          // [EDI+0x16f0] -> [ESI+0x44]
    Shield.UseAtRatio = R.UseShieldRepairAtDamagePercent;      // [EDI+0x16ec] -> [ESI+0x48]

    return true;
}
