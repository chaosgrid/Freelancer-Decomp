// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/server_gameobject.h"
#include "../include/server_chat.h"
#include "../include/common_timing.h"  // Timing::read
#include "../include/server_sysdb.h"   // StarSystem::on_object_destroyed (GameObject::destroy)
#include "../include/common_misc.h"    // SUBOBJ_ID_NONE (Ship::allocate_equip_id)
#include "../include/common_cequip.h"  // CEquipTraverser / CEquip (get_damage_list)
#include "../include/common_goods.h"   // FindHardpoint / JETTISONED_CARGO_VELOCITY / EquipDesc (Ship::tick_jettison)
#include <cmath>
#include <windows.h>
#include "../include/common_ini.h"              // StringAlloc (EqObj::*_by_name / dump_cargo)
#include "../include/common_server.h"           // Players, Server_BroadcastGroupControl (EqObj::destroy)
#include "../include/common_server_playerdb.h"  // PlayerData::should_queue_visit / PlayerData::queue_visit
#include "../include/common_server_types.h"     // ServerStats (EqObj::destroy)
#include "../include/rpc_iclient_args.h"        // FServerBurnFuseInfo (EqObj::light_fuse)
#include <map>                                  // ServerFuseDB::Get (EqObj::light_fuse)
#include <cstring>                              // memcpy (EqObj::build_equip_loot)
#include "../include/common_server.h"
#include "../include/common_physys.h"
#include "../include/common_id.h"
#include "../include/common_server_playerdb.h"
#include "../include/common_ini.h"
#include "../include/common_misc.h"
#include "../include/x86math.h" // I3DMathEngine
#include "../include/common_universe.h"
#include "../include/server_sysdb.h"
#include "../include/server_gameobject.h" // GameObject's +0x154..+0x160 slots (StarSystem::remove_player/unload)
#include "../include/DALib.h"          // DALib::Data (SrvAsteroid's GetFileSysPath calls)
#include "../include/server_createinfo.h"
#include "../include/rpc_iclient.h"
#include <cstdlib>
#include <cstring>
#include <vector>
#include <list>
#include <map>
#include "server_internal.h"
// .data:06d8f998 (IServerImpl.cpp) -- EqObj::destroy bumps +0x10 (.text:06ce825f).
extern ServerStats GServerStats;

// ---------------------------------------------------------------------------
// EqObj::get_damage_list -- .text:06ceb210.
//
// Only EqObj's, Ship's and Solar's vtables (+0x1a8) hold it; declaring it on
// EqObj reproduces exactly that set.
//
//  1. The value compared (CEquip vt +0x44, GetRelativeHealth) and the value
//     recorded (vt +0x3c, GetHitPoints) are different accessors.
//  2. The traverser is the two-argument CEquipTraverser(int, bool) overload
//     with (-1, true): every type, destroyed items included -- not the
//     one-argument import the create builders use. The manager call is the
//     non-const Traverse overload (FUN_06d07690 uses the const one).
// ---------------------------------------------------------------------------
void EqObj::get_damage_list(DamageList& Out, float StatusCeiling) const
{
    // Direct (non-virtual) call to the base body FUN_06ceee20.
    GameObject::get_damage_list(Out, StatusCeiling);

    // The inlined accessor yields null unless (ObjectClass & 0x103) == 0x103, and
    // retail calls Traverse on null+0xe4 anyway; no early exit here either. The
    // wrappers carrying this override always hold a CEqObj.
    CEqObj* const Owner = ceqobj();

    CEquipTraverser Walk(-1, true);
    for (;;) {
        CEquip* const Equip = Owner->EquipManager.Traverse(Walk);
        if (Equip == nullptr)
            break;

        if (Equip->IsDestroyed()) {
            // The pushed 0 is a float bit pattern (the other arm FSTPs into the
            // same slot). SubObjFate(3) is HealthProxy.
            Out.add_damage_entry(Equip->Id, 0.0f,
                                 DamageEntry::SubObjFate::HealthProxy);
            continue;
        }

        // Compared unrounded (ST0 vs the float ceiling).
        if (!(Equip->GetRelativeHealth() < StatusCeiling))
            continue;

        // The id (word at CEquip+0x08) is read before the call.
        Out.add_damage_entry(Equip->Id, Equip->GetHitPoints(),
                             DamageEntry::SubObjFate::None);
    }

    // Same pass over the arch groups at CEqObj+0x144.
    CArchGrpTraverser Groups;
    for (;;) {
        CArchGroup* const Group = Owner->ArchGroups.Traverse(Groups);
        if (Group == nullptr)
            break;

        if (!(Group->GetRelativeHealth() < StatusCeiling))
            continue;

        Out.add_damage_entry(Group->GetID(), Group->GetHitPoints(),
                             DamageEntry::SubObjFate::None);
    }
}

// ---------------------------------------------------------------------------
// EqObj -- .text:06ce6620
// ---------------------------------------------------------------------------

// .text:06ce6620. List heads +0x60 DockQueue, +0x70 ServerEquips, +0x7c Fuses
// (node sizes 0x10 / 0xc / 0x44); +0x6c CargoDecayTimer, +0x88 bDeathFuseLit.
// Vtables .rdata:06d65244 / 06d651c4 / 06d651b8.
EqObj::EqObj()
    : CargoDecayTimer(0.0f)
    , bDeathFuseLit(0)
{
}

// .text:06ce6710 (behind the deleting dtor .text:06ce66e0). EqObj's vtables go
// back in first, so clear_server_equip is EqObj's own. Members (Fuses,
// ServerEquips, DockQueue) are then destroyed in reverse declaration order.
EqObj::~EqObj()
{
    DockQueue.clear();
    clear_server_equip();
}

// Retail FUN_06ce6870 (RET 0), the (& 0x103) narrowing both cargo paths use.
CEqObj* GameObject::ceqobj() const
{
    CObject* Obj = cobj();
    if (Obj == nullptr)
        return nullptr;
    // Retail's own narrowing test on this family is the ObjectClass mask, not
    // RTTI: Ship::create checks (ObjectClass & 0x503) == 0x503, Loot::create
    // (ObjectClass & 0x803) == 0x803. Everything CEqObj-derived carries 0x103.
    if ((static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        return nullptr;
    return static_cast<CEqObj*>(Obj);
}

// ---------------------------------------------------------------------------
// EqObj helpers shared by the damage / loot slots below.
// ---------------------------------------------------------------------------

// The "is this object a connected player's CURRENT ship" test retail inlines at
// every difficulty-scale site (damage_hull / _equip / _shield / _group /
// _power): get_id() first, then CSimple+0xb4 through the (ObjectClass & 3) guard
// (its null dereferenced, as in retail), PlayerDB::is_valid inlined, then
// PlayerData+0x3e4 against the id. Null when any of them fails.
PlayerData* EqObj::player_of_ship() const
{
    unsigned int const Id = get_id();
    unsigned int const Owner = csimple()->OwnerPlayer;
    if (!Players.is_valid(Owner))
        return nullptr;
    PlayerData& Data = Players[Owner];
    if (Data.ship_object_id != Id)
        return nullptr;
    return &Data;
}

// Retail's inlined CSolar narrowing ((ObjectClass & 0x303) == 0x303) at the
// loot-visit sites. Null for everything that is not a solar.
CSolar* EqObj::as_solar() const
{
    CObject* const Raw = cobj();
    if (Raw == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Raw->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
        return nullptr;
    return static_cast<CSolar*>(Raw);
}

// The ServerEquip listening to Equip (inlined walk of ServerEquips in retail).
// End of list -> nullptr and the "not found" flag; retail tells the two apart
// only by the jump taken.
static bool EqObj_FindServerEquip(std::list<ServerEquip*>& ServerEquips, CEquip const* Equip,
                                  ServerEquip*& Out)
{
    for (std::list<ServerEquip*>::iterator It = ServerEquips.begin(); It != ServerEquips.end(); ++It)
    {
        if ((*It)->Equip == Equip)
        {
            Out = *It;
            return true;
        }
    }
    Out = nullptr;
    return false;
}

// IObjAffect slot 13 (+0x34) -- FUN_06ce68a0.
// Every requested equip is looked up and handed to its ServerEquip, which retail
// calls as a SrvLauncher through slots 3/4/5 whatever it really is. Unguided
// guns that fired are collected and announced in one notify_fire_weapon.
int EqObj::fire_weapons(unsigned short const* EquipIds, unsigned Count, Vector const* TargetPos,
                   int SourceClient)
{
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    std::vector<unsigned short> Fired;
    int Result = 0;
    CSimple* LockTarget = nullptr;
    unsigned short LockSubId = SUBOBJ_ID_NONE;

    for (unsigned Index = 0; Index < Count; ++Index)
    {
        // No null test on ceqobj().
        CEquip* const Found = ceqobj()->EquipManager.FindByID(EquipIds[Index]);

        ServerEquip* Listener = nullptr;
        if (!EqObj_FindServerEquip(ServerEquips, Found, Listener) || Listener == nullptr)
        {
            Result = -2;
            continue;
        }

        CEquip* const Equip = Listener->Equip;
        if (Equip == nullptr)
            continue;

        SrvLauncher* const Launcher = static_cast<SrvLauncher*>(Listener);
        bool bFired;
        if (SourceClient != -1 && SourceClient != 0)
        {
            // A client's shot.
            bFired = Launcher->fire_unchecked(*TargetPos);
        }
        else
        {
            // CELauncher::cast is not null-tested. With an aim point and a
            // projectile without ForceGunOri (+0x79), fire at it.
            CELauncher* const Cast = CELauncher::cast(Equip);
            if (TargetPos != nullptr &&
                !static_cast<Archetype::Projectile const*>(Cast->Projectile)->bForceGunOri)
            {
                bFired = Launcher->fire(*TargetPos);
            }
            else
            {
                // Its answer is not read.
                Launcher->fire_forward(SourceClient);
                continue;
            }
        }

        // Only a gun that fired an unguided munition is collected.
        if (!bFired)
            continue;
        CEGun* const Gun = CEGun::cast(Equip);
        if (Gun == nullptr)
            continue;
        if (Gun->MunitionArch()->Seeker != Archetype::Munition::SeekerNone)
            continue;

        Fired.insert(Fired.end(), 1, Gun->Id);

        // The first gun with a target supplies it.
        if (LockTarget == nullptr)
            Gun->GetTarget(LockTarget, LockSubId);
    }

    if (!Fired.empty())
    {
        // Read without a null test.
        Vector Aim = *TargetPos;

        // Outside single player with a lock target, the aim point goes into the
        // target's frame (InverseTransform).
        if (!SinglePlayer() && LockTarget != nullptr)
        {
            Transform const TargetXform = *reinterpret_cast<Transform const*>(&LockTarget->Orientation);
            Vector Local;
            Server_MathEngine()->InverseTransform(Local, TargetXform, *TargetPos);
            Aim = Local;
        }

        unsigned int const ObjId = get_id();
        OwnerSystem->notify_fire_weapon(ObjId, Fired, Aim, SourceClient);   // FUN_06d0e2e0
    }

    return Result;
}

// IObjRW::cobject() under the (ObjectClass & 0x803) == 0x803 test -- the loot
// narrowing both tractor slots inline. Null when it is not loot.
static CLoot* EqObj_LootOf(IObjRW* Obj)
{
    CObject const* const Raw = Obj->cobject();
    if (Raw == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Raw->ObjectClass) & CObject::ClassLoot) != CObject::ClassLoot)
        return nullptr;
    return const_cast<CLoot*>(static_cast<CLoot const*>(Raw));
}

// IObjAffect slot 16 (+0x40) -- FUN_06ce6ba0.
int EqObj::tractor_single(unsigned short const* EquipId, IObjRW* Target, int SourceClient)
{
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    if (Target == nullptr)
        return -4;

    // An AI request (-1) is treated as the server's own (0).
    int Result = 0;
    if (SourceClient == -1)
        SourceClient = 0;

    // Single player lets a client's request straight through to the broadcast;
    // otherwise the tractor is driven here.
    if (!SinglePlayer() || SourceClient == 0)
    {
        CEquip* const Found = ceqobj()->EquipManager.FindByID(*EquipId);
        if (Found == nullptr)
        {
            Result = -2;
        }
        else
        {
            // Retail does not cast the equip to CETractor; any refusal is -5.
            if (static_cast<CETractor*>(Found)->AddTarget(EqObj_LootOf(Target)) != TractorOk)
                Result = -5;
        }

        // The server's own request stops here unless the tractor took.
        if (SourceClient == 0 && Result != 0)
            return Result;
    }

    XTractorObjects Info;
    Info.ObjectId = get_id();
    Info.TractorEquipId = *EquipId;
    Info.TargetIds.insert(Info.TargetIds.end(), 1, Target->get_id());

    unsigned int const SkipPlayerId = static_cast<unsigned int>(SourceClient);
    OwnerSystem->send_tractor_objects(SkipPlayerId, Info);   // FUN_06d0e7a0

    return Result;
}

// IObjAffect slot 15 (+0x3c) -- FUN_06ce6d80.
int EqObj::tractor_all(unsigned short const* EquipId, std::vector<IObjRW*> const& Targets,
                   int SourceClient)
{
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    int Result = 0;
    if (SourceClient == -1)
        SourceClient = 0;

    if (!SinglePlayer() || SourceClient == 0)
    {
        CETractor* const Tractor = CETractor::cast(ceqobj()->EquipManager.FindByID(*EquipId));
        if (Tractor == nullptr)
        {
            Result = -2;
        }
        else
        {
            // Every non-null target through AddTarget; none taken: -4.
            int Took = 0;
            for (std::vector<IObjRW*>::const_iterator It = Targets.begin(); It != Targets.end(); ++It)
            {
                if (*It == nullptr)
                    continue;
                if (Tractor->AddTarget(EqObj_LootOf(*It)) == TractorOk)
                    ++Took;
            }
            if (Took == 0)
                Result = -4;
        }

        if (SourceClient == 0 && Result != 0)
            return Result;
    }

    // Here without a null test on the targets.
    XTractorObjects Info;
    Info.ObjectId = get_id();
    Info.TractorEquipId = *EquipId;
    for (std::vector<IObjRW*>::const_iterator It = Targets.begin(); It != Targets.end(); ++It)
        Info.TargetIds.insert(Info.TargetIds.end(), 1, (*It)->get_id());

    unsigned int const SkipPlayerId = static_cast<unsigned int>(SourceClient);
    OwnerSystem->send_tractor_objects(SkipPlayerId, Info);

    return Result;
}

// IObjAffect slot 17 (+0x44) -- LAB_06ce6f60.
int EqObj::set_gun_target(unsigned short const* EquipId, IObjRW* Target, unsigned short SubTarget,
                   int SourceClient)
{
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    int Result = 0;
    if (SourceClient == -1)
        SourceClient = 0;

    bool bChanged = false;
    if (!SinglePlayer() || SourceClient == 0)
    {
        CEquip* const Found = ceqobj()->EquipManager.FindByID(*EquipId);
        if (Found == nullptr || Found->IsDestroyed())
        {
            Result = -2;
        }
        else
        {
            CEGun* const Gun = CEGun::cast(Found);
            if (Gun == nullptr)
            {
                Result = -4;
            }
            else
            {
                CSimple* Simple = nullptr;
                if (Target != nullptr)
                {
                    CObject const* const Raw = Target->cobject();
                    if (Raw != nullptr && (static_cast<unsigned>(Raw->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
                        Simple = const_cast<CSimple*>(static_cast<CSimple const*>(Raw));
                }
                bChanged = Gun->SetTarget(Simple, SubTarget);
            }
        }

        // The server's own request is only told when the target really
        // changed and nothing failed.
        if (SourceClient == 0 && (!bChanged || Result != 0))
            return Result;
    }

    unsigned int const ObjId = get_id();
    OwnerSystem->notify_set_target(ObjId, *EquipId, Target, SubTarget, SourceClient);   // FUN_06d0e670
    return Result;
}

// IObjAffect slot 19 (+0x4c) -- LAB_06ce70b0.
int EqObj::toggle_item(unsigned short const* EquipId, bool bActivate, int SourceClient)
{
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    int Result = 0;
    if (SourceClient == -1)
        SourceClient = 0;

    bool bChanged = false;
    if (!SinglePlayer() || SourceClient == 0)
    {
        CEquip* const Found = ceqobj()->EquipManager.FindByID(*EquipId);
        if (Found == nullptr || Found->IsDestroyed())
        {
            Result = -2;
        }
        else
        {
            bool bRefused = false;
            // Switching an engine (type 0x20000) off is refused (-5) while the
            // ship strafes or has a thruster on; the CShip narrowing is used
            // without a null test.
            if (!bActivate && Found->Type == EquipTypeEngine)
            {
                CObject* const Raw = cobj();
                CShip* Ship = nullptr;
                if (Raw != nullptr && (static_cast<unsigned>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                    Ship = static_cast<CShip*>(Raw);
                if (Ship->get_strafe_dir() != 0 || Ship->any_thrusters_on())
                {
                    Result = -5;
                    bRefused = true;
                }
            }

            if (!bRefused && Found->IsActive() != bActivate)
            {
                Found->Activate(bActivate);
                bChanged = true;
            }
        }

        if (SourceClient == 0 && (!bChanged || Result != 0))
            return Result;
    }

    unsigned int const ObjId = get_id();
    OwnerSystem->notify_activate_equip(ObjId, *EquipId, bActivate, SourceClient);   // FUN_06d0e500
    return Result;
}

// IObjAffect slot 22 (+0x58) -- LAB_06ce7210. SourceClient is not read.
int EqObj::use_item(unsigned short EquipId, unsigned Count, int SourceClient)
{
    (void)SourceClient;

    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    // The answer for an item that is neither kind.
    int Result = -5;

    CECargo* const Cargo = CECargo::cast(ceqobj()->EquipManager.FindByID(EquipId));
    if (Cargo == nullptr)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", "E:\\FL\\Scratch\\Source\\Server\\Space\\EqObj.cpp",
              0x18c, "EqObj::use_item() couldn't find target item on Client request.");
        return -2;
    }

    // At most the stack's own count (unsigned).
    unsigned Used = Count;
    if (Cargo->Count < Used)
        Used = Cargo->Count;

    // Repair kits and shield batteries each answer how many they used; only a
    // positive number is success.
    int const ClassType = static_cast<int>(Cargo->Arch->get_class_type());
    int Taken;
    if (ClassType == Archetype::AClassRepairKit)
    {
        Taken = use_repair_kits(EquipId, static_cast<Archetype::RepairKit const*>(Cargo->Arch),
                                static_cast<int>(Used));
    }
    else if (ClassType == Archetype::AClassShieldBattery)
    {
        Taken = use_shield_batteries(EquipId,
                                     static_cast<Archetype::ShieldBattery const*>(Cargo->Arch),
                                     static_cast<int>(Used));
    }
    else
    {
        return Result;
    }
    Result = Taken > 0 ? 0 : -5;
    if (Result != 0)
        return Result;

    Cargo->RemoveFromStack(static_cast<unsigned>(Taken));
    OwnerSystem->use_item(this, EquipId, Taken);   // FUN_06d0efe0
    return Result;
}

// FUN_06ce7350.
int EqObj::use_repair_kits(unsigned short EquipId, Archetype::RepairKit const* Kit, int Count)
{
    (void)EquipId;

    // Retail builds a DamageList (cause 0x12) that nothing ever fills and only
    // destroys on the way out.
    DamageList Unused;
    Unused.Count = 0;
    Unused.bDestroyed = false;
    Unused.Cause = DamageCause::RepairKit;
    Unused.InflictorId = 0;
    Unused.InflictorOwner = 0;
    (void)Unused;

    int Taken = 0;

    // Only a hull below full health is mended.
    unsigned short SubId = SUBOBJ_ID_NONE;
    if (ceqobj()->get_relative_health() < 1.0f)
        SubId = SUBOBJ_ID_ROOT;
    if (SubId == SUBOBJ_ID_NONE)
        return Taken;

    // The list that is applied: cause 0x12, this object as inflictor, its
    // OwnerPlayer (guarded CSimple, no null test) as the owner.
    DamageList Damage;
    Damage.Count = 0;
    Damage.bDestroyed = false;
    Damage.Cause = DamageCause::RepairKit;
    Damage.InflictorId = get_id();
    Damage.InflictorOwner = csimple()->OwnerPlayer;

    // The hull's own points through CSimple, any other sub-object through
    // CEqObj vt +0xe4 / +0xec.
    float HitPts;
    float MaxHitPts;
    if (SubId == SUBOBJ_ID_ROOT)
    {
        HitPts = ceqobj()->get_hit_pts();
        MaxHitPts = ceqobj()->get_max_hit_pts();
    }
    else
    {
        ceqobj()->get_sub_obj_hit_pts(SubId, HitPts);
        ceqobj()->get_sub_obj_max_hit_pts(SubId, MaxHitPts);
    }

    // Kits needed = (max - hp) / Kit->HitPts on the x87 stack, truncated by
    // _ftol, rounded up when the remainder is above 0.05f.
    double const Needed = (static_cast<double>(MaxHitPts) - HitPts) / Kit->HitPts;
    Taken = static_cast<int>(Needed);
    if (Needed - static_cast<double>(Taken) > 0.05f)
        ++Taken;

    // No more than asked (signed).
    if (Count < Taken)
        Taken = Count;

    // Capped at the max (>= and unordered take the max).
    if (Taken > 0)
    {
        double const Mended = static_cast<double>(Taken) * Kit->HitPts + HitPts;
        float NewHitPts;
        if (Mended < static_cast<double>(MaxHitPts))
            NewHitPts = static_cast<float>(Mended);
        else
            NewHitPts = MaxHitPts;
        Damage.add_damage_entry(SubId, NewHitPts, DamageEntry::SubObjFate::None);
        apply_damage_list(Damage);
    }

    return Taken;
}

// FUN_06ce7630.
int EqObj::use_shield_batteries(unsigned short EquipId, Archetype::ShieldBattery const* Battery,
                                int Count)
{
    (void)EquipId;

    // Over the shields (type 0x100): the one with the lowest relative health
    // strictly below 1.0f whose max hit points are above 0.0f.
    CEquipTraverser Walk(EquipTypeShield);
    float Lowest = 1.0f;
    CEquip* Weakest = nullptr;
    for (;;)
    {
        CEquip* const Equip = ceqobj()->EquipManager.Traverse(Walk);
        if (Equip == nullptr)
            break;
        // Retail keeps a rounded copy but compares the unrounded ST0.
        double const RelativeExact = Equip->GetRelativeHealth();
        float const Relative = (float)RelativeExact;
        if (!(RelativeExact < Lowest))
            continue;
        if (!(Equip->GetMaxHitPoints() > 0.0f))
            continue;
        Lowest = Relative;
        Weakest = Equip;
    }

    if (Weakest == nullptr)
        return 0;

    // Cause 0x13, this object as inflictor, its OwnerPlayer (no null test) as
    // the owner.
    DamageList Damage;
    Damage.Count = 0;
    Damage.bDestroyed = false;
    Damage.Cause = DamageCause::ShieldBattery;
    Damage.InflictorId = get_id();
    Damage.InflictorOwner = csimple()->OwnerPlayer;

    float const HitPts = Weakest->GetHitPoints();
    float const MaxHitPts = Weakest->GetMaxHitPoints();

    // The same count as the repair kits'.
    double const Needed = (static_cast<double>(MaxHitPts) - HitPts) / Battery->HitPts;
    int Taken = static_cast<int>(Needed);
    if (Needed - static_cast<double>(Taken) > 0.05f)
        ++Taken;

    if (Count < Taken)
        Taken = Count;

    // Charge the shield by its own id.
    if (Taken > 0)
    {
        double const Charged = static_cast<double>(Taken) * Battery->HitPts + HitPts;
        float NewHitPts;
        if (Charged < static_cast<double>(MaxHitPts))
            NewHitPts = static_cast<float>(Charged);
        else
            NewHitPts = MaxHitPts;
        Damage.add_damage_entry(Weakest->Id, NewHitPts, DamageEntry::SubObjFate::None);
        apply_damage_list(Damage);
    }

    return Taken;
}

// FUN_06ce7850: a tail jump to CEqObj vt +0xb4 through the (& 0x103)
// narrowing, no null test.
ObjActivateResult EqObj::activate(bool bOn, unsigned int DockIndex)
{
    return ceqobj()->activate(bOn, DockIndex);
}

// FUN_06ce7880.
void EqObj::queue_dock_request(int Port, unsigned int ShipId)
{
    FDockRequest Request;
    Request.Port = Port;
    Request.ShipId = ShipId;
    DockQueue.push_back(Request);

    OwnerSystem->add_docking_solar(this);   // FUN_06d0c6d0
}

// FUN_06ce78d0. Port < 0 (signed test) matches any port.
bool EqObj::dequeue_dock_request(int Port, unsigned int ShipId)
{
    for (std::list<FDockRequest>::iterator It = DockQueue.begin(); It != DockQueue.end(); ++It)
    {
        if (It->ShipId != ShipId)
            continue;
        if (Port >= 0 && It->Port != Port)
            continue;

        DockQueue.erase(It);

        OwnerSystem->remove_docking_solar(this);   // FUN_06d0c730
        return true;
    }

    // No match: the system is not told.
    return false;
}

// FUN_06ce7930 (Dt is not read). The dock-bay tick: every queued {port, ship}
// whose ship has reached the bay's docking sphere is handed on to the system,
// the rest wait.
void EqObj::process_dock_queue(float Dt)
{
    (void)Dt;

    // ceqobj() once, kept for the whole walk.
    CEqObj* const Ce = ceqobj();

    std::list<FDockRequest>::iterator It = DockQueue.begin();
    while (It != DockQueue.end())
    {
        GameObject* const ShipObj = OwnerSystem->find_object(It->ShipId, OBJ_SHIP_MASK);   // FUN_06d0c8d0
        if (ShipObj == nullptr)
        {
            // The ship is gone.
            It = DockQueue.erase(It);
            OwnerSystem->remove_docking_solar(this);
            continue;
        }

        // Archetype::EqObj +0x88 array, 0x10-byte entries: Radius +0, Type +0xc.
        int const Port = It->Port;
        Archetype::EqObj::DockingSphere const& Sphere = Ce->eqobjarch()->DockingSpheres[Port];

        Vector LaunchPos;
        Matrix LaunchOrient;
        Ce->launch_pos(LaunchPos, LaunchOrient, Port);

        // The ship's "hpmount" hardpoint in object space, on its engine index.
        long HpHandle = 0;
        HardpointInfo Hp;
        FindHardpoint_OS(ShipObj->get_index(), "hpmount", HpHandle, Hp);
        Transform HpXform;
        HpXform.Rotation = Hp.Orient;
        HpXform.Translation = Hp.Position;

        Transform const* const ShipXform = DALib::Engine->GetInstanceTransform(ShipObj->get_index());

        Transform Mounted;
        Server_MathEngine()->MulTransform(Mounted, *ShipXform, HpXform);
        // FUN_06ce7c30, a 12-dword copy.
        Transform const Copy = Mounted;

        // The mount point relative to the launch position.
        Vector const Delta = { Copy.Translation.x - LaunchPos.x,
                               Copy.Translation.y - LaunchPos.y,
                               Copy.Translation.z - LaunchPos.z };

        // Both sides stay on the x87 stack, so both are doubles here; <= and
        // unordered keep the ship queued.
        double const DistSq = (static_cast<double>(Delta.y) * Delta.y + static_cast<double>(Delta.z) * Delta.z) +
                              static_cast<double>(Delta.x) * Delta.x;
        if (!(static_cast<double>(Sphere.Radius) * Sphere.Radius > DistSq))
        {
            ++It;
            continue;
        }

        // This bay's own type bits.
        unsigned int const Bits = get_type_bits();
        if (Bits == (Bits & (OBJ_JUMP_GATE | OBJ_AIRLOCK_GATE)))
        {
            OwnerSystem->complete_dock(ShipObj, this);   // FUN_06d0c970
        }
        else if ((Bits & OBJ_JUMP_HOLE) != 0)
        {
            OwnerSystem->complete_dock(ShipObj, this);
        }
        else
        {
            // Mooring points 3/4/5 are not a landing (FUN_06d0d370). The CEqObj
            // is re-narrowed from the raw Object here.
            bool bLand = true;
            if (Sphere.Type == TERM_MOOR_SMALL || Sphere.Type == TERM_MOOR_MEDIUM || Sphere.Type == TERM_MOOR_LARGE)
                bLand = false;
            CEqObj* const Bay = ceqobj();
            OwnerSystem->queue_dock(ShipObj, this, &Bay->DockingPointBaseId, &bLand);
        }

        It = DockQueue.erase(It);
        OwnerSystem->remove_docking_solar(this);
    }
}

// FUN_06ce7c50.
bool EqObj::is_ship_queued(unsigned int ShipId) const
{
    for (std::list<FDockRequest>::const_iterator It = DockQueue.begin(); It != DockQueue.end(); ++It)
    {
        if (It->ShipId == ShipId)
            return true;
    }
    return false;
}

// +0x15c -- .text:06ce7c80.
void EqObj::destroy_notify()
{
    // StarSystem FUN_06d0c730(this), once per DockQueue entry.
    for (std::list<FDockRequest>::iterator It = DockQueue.begin(); It != DockQueue.end(); ++It)
        OwnerSystem->remove_docking_solar(this);

    // Unless a player's object in single player.
    if (!(SinglePlayer() && csimple()->OwnerPlayer != 0))
        ceqobj()->disconnect(static_cast<IObjRW*>(this));

    GameObject::destroy_notify();
}

// +0x1bc -- .text:06ce7d00.
void EqObj::check_fuses()
{
    rebuild_server_equip();                                        // vt +0x1dc

    // The archetype's fuse list (Archetype::EqObj +0x74): hull hit points <=
    // Value2 (unordered skips) lights the fuse at Value1.
    CEqObj* const Ce = ceqobj();
    std::vector<Archetype::FGroupFuse> const& ArchFuses = *Ce->eqobjarch()->Fuses;
    for (std::vector<Archetype::FGroupFuse>::const_iterator It = ArchFuses.begin();
         It != ArchFuses.end(); ++It)
    {
        if (Ce->get_hit_pts() > It->Value2)
            continue;
        light_fuse(0, It->Fuse, SUBOBJ_ID_ROOT, It->Value1, -1.0f); // vt +0x1e4
    }

    // Every arch group's own FuseIgnitions, same test on the group's hit
    // points, sub-object = the group's id.
    CArchGrpTraverser Walk;
    CArchGroupManager& Groups = Ce->ArchGroups;
    for (CArchGroup* Group = Groups.Traverse(Walk); Group != nullptr; Group = Groups.Traverse(Walk))
    {
        std::vector<Archetype::FGroupFuse> const& GroupFuses = *Group->FuseIgnitions().Items;
        for (std::vector<Archetype::FGroupFuse>::const_iterator It = GroupFuses.begin();
             It != GroupFuses.end(); ++It)
        {
            if (Group->GetHitPoints() > It->Value2)
                continue;
            light_fuse(0, It->Fuse, Group->Arch->Id, It->Value1, -1.0f);
        }
    }
}

// FUN_06ce7e20.
bool EqObj::update(float Dt)
{
    // Direct call to GameObject's gate (FUN_06cee9e0).
    if (!GameObject::update(Dt))
        return false;

    // Weapons-offline repair pass, only while CEqObj+0x15c is set (ceqobj() is
    // dereferenced without a null test).
    if (ceqobj()->bWeaponsOffline)
    {
        double const Now = Timing::read();
        DamageList Repair;
        Repair.Cause = DamageCause::WeaponRecovery;

        // Relative health <= 0 (unordered skips) and more than 60 s
        // (DAT_06d65530, double) since the last damage: root back to max.
        if (!(ceqobj()->get_relative_health() > 0.0) &&
            (Now - LastDamageTime) > 60.0)
        {
            Repair.add_damage_entry(SUBOBJ_ID_ROOT, ceqobj()->get_max_hit_pts(),
                                    DamageEntry::SubObjFate::None);
        }

        // Every disabled launcher not hit for more than 60 s gets its max hit
        // points back.
        for (std::list<ServerEquip*>::iterator It = ServerEquips.begin();
             It != ServerEquips.end(); ++It)
        {
            CEquip* const Equip = (*It)->Equip;
            if (CELauncher::cast(Equip) == nullptr)
                continue;
            if (!Equip->IsDisabled())
                continue;
            double const Since = Now - static_cast<SrvLauncher*>(*It)->LastDamageTime;
            if (!(Since > 60.0))
                continue;
            Repair.add_damage_entry(Equip->Id, Equip->GetMaxHitPoints(), DamageEntry::SubObjFate::None);
        }

        if (Repair.Count != 0)
            apply_damage_list(Repair);
    }

    process_dock_queue(Dt);                               // FUN_06ce7930

    for (std::list<ServerEquip*>::iterator It = ServerEquips.begin();
         It != ServerEquips.end(); ++It)
        (*It)->update(Dt);

    // 1.0f is DAT_06d651a4.
    CargoDecayTimer = Dt + CargoDecayTimer;
    if (CargoDecayTimer > 1.0f)
    {
        decay_cargo(CargoDecayTimer);                     // vt +0x24c
        CargoDecayTimer = 0.0f;
    }

    for (std::list<ServerFuse>::iterator It = Fuses.begin(); It != Fuses.end(); ++It)
        It->Burn(Dt);

    return !is_destroyed();
}

// +0x158 -- .text:06ce8080.
bool EqObj::destroy(int Kind, unsigned int ObjId)
{
    // The first destroy only: latch +0x88.
    if (!bDeathFuseLit)
    {
        bDeathFuseLit = 1;

        if (csimple()->OwnerPlayer == 0)
        {
            // An AI object killed outright (kind 1) first gets its death fuse,
            // if the controller hands out a lifetime.
            if (Kind == DESTROY_FUSE)
            {
                // Controller event 0x38.
                FDeathFuseEvent Ev;
                Ev.SystemId = 0;
                Ev.ObjId = 0;
                Ev.ObjType = 0;
                Ev.Lifetime = 0.0f;
                Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;     // FUN_06d06cd0
                Ev.ObjId = get_id();                                    // vt +0x20
                get_type(Ev.ObjType);                                   // vt +0x88
                Server_ControllerDispatch(0, pub::Controller::EMessage::DeathFuseLit, &Ev);

                if (Ev.Lifetime > 0.0f)
                {
                    if (light_fuse(ObjId, DEATH_COMM_FUSE, SUBOBJ_ID_NONE, 0.0f, Ev.Lifetime))
                    {
                        // Behavior manager event 7 with the own id (no null test
                        // on ceqobj()).
                        IBehaviorManager* const Behavior = ceqobj()->get_behavior_interface();
                        if (Behavior != nullptr)
                        {
                            unsigned int SelfId = get_id();
                            FBehaviorManagerEvent Event;
                            Event.Type = FBehaviorManagerEvent::kEventObjectDying;
                            Event.Data = &SelfId;
                            Behavior->OnEvent(&Event);
                        }

                        // Once per DockQueue entry.
                        for (std::list<FDockRequest>::iterator It = DockQueue.begin();
                             It != DockQueue.end(); ++It)
                            OwnerSystem->remove_docking_solar(this);

                        ceqobj()->disconnect(static_cast<IObjRW*>(this));
                        // Not destroyed yet: the fuse will do it.
                        return false;
                    }
                }
            }
        }
        else if (!SinglePlayer() && Kind == DESTROY_FUSE && is_player())
        {
            // A player's ship died in multiplayer: control message 0x14 with the
            // player's id to every active player (FUN_06d19110), counted in
            // ServerStats+0x10.
            Server_BroadcastGroupControl(kGroupMsgPlayerDied, owner_player());
            ++GServerStats.PlayerDeathCount;
        }
    }

    csimple()->set_hit_pts(0.0f);

    // Controller event 0x39.
    FObjectDestroyedEvent Ev;
    Ev.ObjId = get_id();
    Ev.KillerId = ObjId;
    Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Ev.OwnerPlayer = csimple()->OwnerPlayer;
    Ev.Kind = Kind;
    Ev.Position = get_position();                                  // vt +0x00
    if (Kind == DESTROY_FUSE)
        collect_droppable_equip(Ev.DroppedEquip);                  // vt +0x1f4
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDestroyed, &Ev);

    OwnerSystem->on_object_destroyed(this, Kind);                  // FUN_06d0b160
    destroy_notify();                                              // vt +0x15c

    if (Kind == DESTROY_FUSE)
    {
        explode();                                                 // vt +0x1f0
        if (SinglePlayer() || csimple()->OwnerPlayer == 0)
            drop_loot(Ev.DroppedEquip);                            // vt +0x1f8
    }
    return true;
}

// FUN_06ce83c0 (only caller EqObj::destroy): CSimple+0xb4 through the
// (ObjectClass & 3) guard; retail dereferences the null the guard produces.
unsigned int EqObj::owner_player() const
{
    return csimple()->OwnerPlayer;
}

// +0x1f0 -- .text:06ce8400.
void EqObj::explode()
{
    ID_String const& ExplosionArch = ceqobj()->Archetype->ExplosionArch;
    if (ExplosionArch.IsEmpty())
        return;

    Archetype::Explosion const* const Blast = Archetype::GetExplosion(ExplosionArch);
    if (Blast == nullptr)
        return;

    // Every test is against DOUBLE 0.0001 (DAT_06d65190): the radius must
    // exceed it, and so must one of impulse / hull damage / energy damage.
    if (!(static_cast<double>(Blast->Radius) > 0.0001))
        return;
    if (!(static_cast<double>(Blast->Impulse) > 0.0001) &&
        !(static_cast<double>(Blast->HullDamage) > 0.0001) &&
        !(static_cast<double>(Blast->EnergyDamage) > 0.0001))
        return;

    ExplosionDamageEvent Event;
    Event.ExplodingObjId = 0;
    Event.InflictorId = 0;
    Event.Cause = DamageCause::Explosion;
    Event.Explosion = nullptr;
    Event.SkipCollisionGroup = nullptr;
    Event.ExplodingObjId = get_id();
    Event.InflictorId = get_id();
    Event.Cause = DamageCause::Explosion;
    Event.Position = get_position();
    Event.Explosion = Blast;
    Event.SkipCollisionGroup = PhySys::GetCollisionGroup(Object);

    OwnerSystem->explode(Event);   // FUN_06d0b260
}

// +0x1f4 -- .text:06ce8510.
void EqObj::collect_droppable_equip(std::vector<EquipDesc>& Out)
{
    // One EquipDesc for the whole walk (GetEquipDesc refills it).
    EquipDesc Desc;
    CEquipTraverser Walk;
    for (;;)
    {
        // ceqobj() re-narrowed every iteration.
        CEquip* const Equip = ceqobj()->EquipManager.Traverse(Walk);
        if (Equip == nullptr)
            break;
        if (Equip->IsDestroyed())
            continue;
        if (Equip->Type == EquipTypeCargoPod)                                  // cargo pods stay
            continue;
        if (!Equip->IsLootable())
            continue;
        if (!Equip->GetEquipDesc(Desc))
            continue;
        Out.push_back(Desc);
    }
}

// +0x1f8 -- .text:06ce8760.
void EqObj::drop_loot(std::vector<EquipDesc> const& Descs)
{
    std::vector<FLootRecord> Loot;
    if (!Descs.empty())
        Loot.reserve(Descs.size());

    // build_loot (vt +0x244) per desc while fewer than six records' worth were
    // produced.
    int Total = 0;
    for (std::vector<EquipDesc>::const_iterator It = Descs.begin(); It != Descs.end(); ++It)
    {
        if (Total >= 6)
            break;
        Total += build_loot(*It, Loot);
    }

    // StarSystem FUN_06d08a40, mode 2.
    if (!Loot.empty())
        OwnerSystem->create_loot(get_id(), 2, Loot.data(), static_cast<int>(Loot.size()));
}

// +0x1dc -- .text:06ce88d0.
void EqObj::rebuild_server_equip()
{
    clear_server_equip();                                          // vt +0x1e0
    CEquipTraverser Walk;
    for (;;)
    {
        // ceqobj() re-narrowed every iteration.
        CEquip* const Equip = ceqobj()->EquipManager.Traverse(Walk);
        if (Equip == nullptr)
            break;
        create_server_equip(Equip->Id);                            // FUN_06ce8990
    }
}

// +0x1e0 -- .text:06ce8930.
void EqObj::clear_server_equip()
{
    for (std::list<ServerEquip*>::iterator It = ServerEquips.begin(); It != ServerEquips.end(); ++It)
    {
        if (*It != nullptr)
            delete *It;
        *It = nullptr;
    }
    ServerEquips.clear();
}

// FUN_06ce8990.
void EqObj::create_server_equip(unsigned short EquipId)
{
    // No null test on the equip.
    CEquip* const Equip = ceqobj()->EquipManager.FindByID(EquipId);

    // One wrapper per type, pushed at the end of +0x70 (retail inlines the
    // constructors here).
    switch (Equip->Type)
    {
    case EquipTypeGun:       // new(0x18), vt .rdata:06d655ac
        ServerEquips.push_back(new SrvGun(Equip, this));
        break;
    case EquipTypeCmDropper:       // new(0x18), vt .rdata:06d65590
        ServerEquips.push_back(new SrvCounterMeasureDropper(Equip, this));
        break;
    case EquipTypeMineDropper:       // new(0x18), vt .rdata:06d65574
        ServerEquips.push_back(new SrvMineDropper(Equip, this));
        break;
    case EquipTypeThruster:      // new(0x0c), vt .rdata:06d65564
        ServerEquips.push_back(new SrvThruster(Equip, this));
        break;
    case EquipTypeRepairDroid:   // new(0x14)
        ServerEquips.push_back(new SrvRepairDroid(Equip, this));
        break;
    case EquipTypeTractor:   // new(0x0c), vt .rdata:06d65544
        ServerEquips.push_back(new SrvTractor(Equip, this));
        break;
    default:         // no wrapper
        break;
    }
}

// +0x1c0 -- .text:06ce8c50.
void EqObj::inflict_hull_damage(float Damage, bool bCanKill, DamageList& Out)
{
    if (bCanKill)
    {
        // < and unordered skip.
        if (Damage >= 0.0f)
            damage_hull(Damage, Out);                              // vt +0x1b0
        return;
    }
    // Only while the hull stays above 0.
    if (ceqobj()->get_hit_pts() - Damage > 0.0f)
        damage_hull(Damage, Out);
}

// +0x1c8 -- .text:06ce8cd0.
bool EqObj::destroy_group_by_name(char const* GroupName, DamageEntry::SubObjFate Fate, DamageList& Out)
{
    CacheString Name;
    Name.Value = StringAlloc(GroupName, false);
    CArchGroup* const Group = ceqobj()->ArchGroups.FindByName(Name);
    if (Group == nullptr)
        return false;
    destroy_group(Group, Fate, Out, true);                         // vt +0x220
    return true;
}

// +0x1c4 -- .text:06ce8d40.
bool EqObj::inflict_group_damage(char const* GroupName, float Damage, bool bCanKill, DamageList& Out)
{
    CacheString Name;
    Name.Value = StringAlloc(GroupName, false);
    CArchGroup* const Group = ceqobj()->ArchGroups.FindByName(Name);
    if (Group == nullptr)
        return false;

    if (bCanKill)
    {
        // < and unordered skip.
        if (Damage >= 0.0f)
            damage_group(Group, Damage, Out);                      // vt +0x218
        return true;
    }

    // Retail quirk: the REMAINDER (hit points - Damage) is what is passed on,
    // and only while it stays above 0.
    float const Remainder = Group->GetHitPoints() - Damage;
    if (Remainder > 0.0f)
        damage_group(Group, Remainder, Out);
    return true;
}

// +0x1d0 -- .text:06ce8e10.
bool EqObj::destroy_equip_by_hardpoint(char const* Hardpoint, DamageEntry::SubObjFate Fate, DamageList& Out)
{
    CEqObj* const Ce = ceqobj();                                   // before StringAlloc
    CacheString Name;
    Name.Value = StringAlloc(Hardpoint, false);
    CExternalEquip* const Equip = Ce->EquipManager.FindByHardpoint(Name);
    if (Equip == nullptr)
        return false;
    destroy_equip(Equip, Fate, Out);                               // vt +0x224
    return true;
}

// +0x1cc -- .text:06ce8e90.
bool EqObj::inflict_equip_damage(char const* Hardpoint, float Damage, bool bCanKill, DamageList& Out)
{
    CEqObj* const Ce = ceqobj();
    CacheString Name;
    Name.Value = StringAlloc(Hardpoint, false);
    CExternalEquip* const Equip = Ce->EquipManager.FindByHardpoint(Name);
    if (Equip == nullptr)
        return false;

    if (bCanKill)
    {
        // No sign test on this path.
        damage_equip(Equip, Damage, Out);                          // vt +0x210
        return true;
    }

    // The remainder, only while above 0.
    float const Remainder = Equip->GetHitPoints() - Damage;
    if (Remainder > 0.0f)
        damage_equip(Equip, Remainder, Out);
    return true;
}

// +0x1d4 -- .text:06ce8f50.
bool EqObj::dump_cargo(char const* Hardpoint, DamageList& Out)
{
    bool bDumped = false;
    CEquipTraverser Walk(EquipTypeCargo);
    for (;;)
    {
        // ceqobj() re-narrowed every iteration.
        CEquip* const Equip = ceqobj()->EquipManager.Traverse(Walk);
        if (Equip == nullptr)
            break;

        // A stack already held in a pod (CECargo+0x20) stays.
        if (static_cast<CECargo*>(Equip)->CargoPod != nullptr)
            continue;

        EquipDesc Desc;
        if (!Equip->GetEquipDesc(Desc))
            continue;

        // Dumping at a solar for a valid inflicting player asks PlayerData
        // FUN_06d4b200 first; a refusal skips the item.
        unsigned int const Inflictor = Out.InflictorOwner;
        CSolar* const Solar = as_solar();
        if (Solar != nullptr && Players.is_valid(Inflictor))
        {
            if (!Players[Inflictor].should_queue_visit(Solar->Id, Solar->GetVisitValue()))
                continue;
        }

        bDumped = true;
        if (Hardpoint != nullptr)
        {
            CacheString Hp;
            Hp.Value = StringAlloc(Hardpoint, false);
            Desc.set_hardpoint(Hp);
        }

        std::vector<FLootRecord> Loot;
        if (build_loot(Desc, Loot) != 0)
            OwnerSystem->create_loot(get_id(), 2, Loot.data(), static_cast<int>(Loot.size()));

        // The stack itself is destroyed.
        Out.add_damage_entry(Equip->Id, 0.0f, DamageEntry::SubObjFate::Destroyed);

        // The visit is recorded (FUN_06d4b270), with the owner re-read.
        CSolar* const SolarAgain = as_solar();
        unsigned int const InflictorAgain = Out.InflictorOwner;
        if (SolarAgain != nullptr && Players.is_valid(InflictorAgain))
            Players[InflictorAgain].queue_visit(SolarAgain->Id, SolarAgain->GetVisitValue());
    }
    return bDumped;
}

// +0x1d8 -- .text:06ce91e0.
bool EqObj::drain_shields(DamageList& Out)
{
    CEquipTraverser Walk(EquipTypeShield);
    for (;;)
    {
        CEquip* const Equip = ceqobj()->EquipManager.Traverse(Walk);   // re-narrowed each pass
        if (Equip == nullptr)
            break;
        damage_shield(static_cast<CEShield*>(Equip), -1.0f, Out);  // vt +0x214
    }
    return true;
}

// +0x17c -- .text:06ce9250.
void EqObj::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    // The base body (FUN_06ceeb00) first, directly.
    GameObject::damage_sub_obj(Hit, Damage);

    if (Hit.SubObjId == SUBOBJ_ID_ROOT)
        return;

    if (SubObjectID::IsArchGroupID(Hit.SubObjId))
    {
        CArchGroup* const Group = ceqobj()->ArchGroups.FindByID(Hit.SubObjId);
        if (Group != nullptr)
            damage_group(Group, Hit.Damage, Damage);               // vt +0x218
        return;
    }

    if (!SubObjectID::IsEquipID(Hit.SubObjId))
        return;

    CEquip* const Equip = ceqobj()->EquipManager.FindByID(Hit.SubObjId);
    if (Equip == nullptr)
        return;
    if (Equip->Type == EquipTypeShield)
        damage_shield(static_cast<CEShield*>(Equip), Hit.Damage, Damage);   // vt +0x214
    else
        damage_equip(Equip, Hit.Damage, Damage);               // vt +0x210
}

// +0x180 -- .text:06ce9350.
void EqObj::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    // ceqobj() once, used for both lookups.
    CEqObj* const Ce = ceqobj();

    // Behavior event 1 with the caller's own record (the const_cast only fits
    // FBehaviorManagerEvent::Data).
    IBehaviorManager* const Behavior = Ce->get_behavior_interface();
    if (Behavior != nullptr)
    {
        FBehaviorManagerEvent Event;
        Event.Type = FBehaviorManagerEvent::kEventMunitionHit;
        Event.Data = const_cast<FMunitionImpact*>(&Hit);
        Behavior->OnEvent(&Event);
    }

    Archetype::Munition const* const Munition = Hit.Munition;

    if (Hit.SubObjId == SUBOBJ_ID_ROOT || Hit.SubObjId == SUBOBJ_ID_NONE)
    {
        damage_hull(Munition->HullDamage, Damage);                 // vt +0x1b0
    }
    else if (SubObjectID::IsArchGroupID(Hit.SubObjId))
    {
        CArchGroup* const Group = Ce->ArchGroups.FindByID(Hit.SubObjId);
        if (Group == nullptr)
            return;
        damage_group(Group, Munition->HullDamage, Damage);         // vt +0x218
    }
    else if (SubObjectID::IsEquipID(Hit.SubObjId))
    {
        CEquip* const Equip = Ce->EquipManager.FindByID(Hit.SubObjId);
        if (Equip == nullptr)
            return;
        if (Equip->Type == EquipTypeShield)
        {
            // The shield takes the whole hit; no energy pass.
            munition_hit_shield(static_cast<CEShield*>(Equip), Munition, Damage);   // vt +0x20c
            return;
        }
        damage_equip(Equip, Munition->HullDamage, Damage);         // vt +0x210
    }
    else
    {
        return;
    }

    // Munition+0x84.
    damage_power(Munition->EnergyDamage, Damage);                  // vt +0x21c
}

// +0x20c -- .text:06ce94b0.
void EqObj::munition_hit_shield(CEShield* Shield, Archetype::Munition const* Munition, DamageList& Out)
{
    // Hull * factor + Energy is rounded to float once; the unrounded weapon
    // modifier (Munition+0x88) times Total is rounded once more.
    float const Total = (float)((double)Munition->HullDamage * ShieldEquipConsts::HULL_DAMAGE_FACTOR
                                + Munition->EnergyDamage);
    damage_shield(Shield, (float)(Shield->GetWeaponModifier(Munition->WeaponType) * Total), Out);   // vt +0x214
}

// +0x184 -- .text:06ce9500.
void EqObj::explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage)
{
    // The shield pass first; a functioning shield absorbs the whole blast.
    if (explosion_hit_shield(Blast, Damage))                       // vt +0x204
        return;
    explosion_hit_hull(Blast, Damage);                             // vt +0x1fc
    explosion_hit_subobjs(Blast, Damage);                          // vt +0x200
    explosion_hit_power(Blast, Damage);                            // vt +0x208
}

// The distance bands the four explosion passes share (each inlines it):
// R^2 = (float)(Radius * Radius), Inner = R^2 * 1/9 (DAT_06d655d0), Middle =
// R^2 * 4/9 (DAT_06d655d4). D (squared centre distance less the target's
// squared radius) is floored at 0, then D < Inner -> 3, D < Middle -> 2,
// D < R^2 -> 1, else 0 (out of reach). Retail keeps D on the x87 stack and
// never stores it, so it is a double here; each damage amount is likewise
// rounded to float once.
static int EqObj_ExplosionBand(double Distance, float RadiusSq, float Middle, float Inner)
{
    if (!(Distance > 0.0))
        Distance = 0.0;
    if (Distance < Inner)
        return 3;
    if (Distance < Middle)
        return 2;
    if (Distance < RadiusSq)
        return 1;
    return 0;
}

// +0x1fc -- .text:06ce9550.
void EqObj::explosion_hit_hull(ExplosionDamageEvent const& Blast, DamageList& Out)
{
    CEqObj* const Ce = ceqobj();

    float const RadiusSq = Blast.Explosion->Radius * Blast.Explosion->Radius;
    float const Middle = RadiusSq * (4.0f / 9.0f);
    float const Inner = RadiusSq * (1.0f / 9.0f);

    float Radius = 0.0f;
    Vector Center;
    Ce->get_explosion_dmg_bounding_sphere(Radius, Center);

    // Summation order ((dz^2 + dx^2) + dy^2) as retail.
    double const Dx = static_cast<double>(Blast.Position.x) - Center.x;
    double const Dy = static_cast<double>(Blast.Position.y) - Center.y;
    double const Dz = static_cast<double>(Blast.Position.z) - Center.z;
    double const Distance = ((Dz * Dz + Dx * Dx) + Dy * Dy) - static_cast<double>(Radius) * Radius;

    int const Band = EqObj_ExplosionBand(Distance, RadiusSq, Middle, Inner);
    if (Band == 0)
        return;

    // Times the archetype's explosion_resistance (Root+0x28).
    float const Amount = static_cast<float>(static_cast<double>(Band) * (1.0f / 3.0f) *
                                            Blast.Explosion->HullDamage);
    damage_hull(Ce->get_archetype()->ExplosionResistance * Amount, Out);   // vt +0x1b0
}

// +0x200 -- .text:06ce9690.
void EqObj::explosion_hit_subobjs(ExplosionDamageEvent const& Blast, DamageList& Out)
{
    CEqObj* const Ce = ceqobj();

    float const RadiusSq = Blast.Explosion->Radius * Blast.Explosion->Radius;
    float const Middle = RadiusSq * (4.0f / 9.0f);
    float const Inner = RadiusSq * (1.0f / 9.0f);

    // The attached equipment (const traversal, mask 0x1ee0; the manager is
    // fixed before the loop).
    CEquipTraverser Walk(kEquipMaskAttachedNoShield);
    CEquipManager const& Equipment = Ce->EquipManager;
    for (CEquip const* Equip = Equipment.Traverse(Walk); Equip != nullptr; Equip = Equipment.Traverse(Walk))
    {
        CAttachedEquip const* const Attached = static_cast<CAttachedEquip const*>(Equip);
        float Radius = 0.0f;
        Vector Center;
        Attached->GetRadius(Radius);                               // vt +0x7c
        Attached->GetCenterOfMass(Center);                         // vt +0x70

        // Summation order differs from the hull pass: ((dz^2 + dy^2) + dx^2).
        double const Dx = static_cast<double>(Blast.Position.x) - Center.x;
        double const Dy = static_cast<double>(Blast.Position.y) - Center.y;
        double const Dz = static_cast<double>(Blast.Position.z) - Center.z;
        double const Distance = ((Dz * Dz + Dy * Dy) + Dx * Dx) - static_cast<double>(Radius) * Radius;

        int const Band = EqObj_ExplosionBand(Distance, RadiusSq, Middle, Inner);
        if (Band == 0)
            continue;
        float const Amount = static_cast<float>(static_cast<double>(Band) * (1.0f / 3.0f) *
                                            Blast.Explosion->HullDamage);
        damage_equip(const_cast<CEquip*>(Equip),
                     Attached->AttachedEquipArch()->ExplosionResistance * Amount, Out);   // vt +0x210
    }

    // Every arch group, the same formula.
    CArchGrpTraverser Groups;
    CArchGroupManager const& GroupManager = Ce->ArchGroups;
    for (CArchGroup const* Group = GroupManager.Traverse(Groups); Group != nullptr;
         Group = GroupManager.Traverse(Groups))
    {
        float Radius = 0.0f;
        Vector Center;
        Group->GetRadius(Radius);
        Group->GetCenterOfMass(Center);

        double const Dx = static_cast<double>(Blast.Position.x) - Center.x;
        double const Dy = static_cast<double>(Blast.Position.y) - Center.y;
        double const Dz = static_cast<double>(Blast.Position.z) - Center.z;
        double const Distance = ((Dz * Dz + Dy * Dy) + Dx * Dx) - static_cast<double>(Radius) * Radius;

        int const Band = EqObj_ExplosionBand(Distance, RadiusSq, Middle, Inner);
        if (Band == 0)
            continue;
        float const Amount = static_cast<float>(static_cast<double>(Band) * (1.0f / 3.0f) *
                                            Blast.Explosion->HullDamage);
        damage_group(const_cast<CArchGroup*>(Group), Group->ExplosionResistance() * Amount, Out);   // vt +0x218
    }
}

// +0x208 -- .text:06ce9940.
void EqObj::explosion_hit_power(ExplosionDamageEvent const& Blast, DamageList& Out)
{
    CEqObj* const Ce = ceqobj();

    // Out only on a negative MaxPower (+0x19c); 0 and unordered go on.
    if (Ce->MaxPower < 0.0f)
        return;
    if (Blast.Explosion->EnergyDamage == 0.0f)
        return;

    float const RadiusSq = Blast.Explosion->Radius * Blast.Explosion->Radius;
    float const Middle = RadiusSq * (4.0f / 9.0f);
    float const Inner = RadiusSq * (1.0f / 9.0f);

    Vector Center;
    float const Radius = Ce->get_physical_radius_r(Center);

    double const Dx = static_cast<double>(Blast.Position.x) - Center.x;
    double const Dy = static_cast<double>(Blast.Position.y) - Center.y;
    double const Dz = static_cast<double>(Blast.Position.z) - Center.z;
    double const Distance = ((Dz * Dz + Dx * Dx) + Dy * Dy) - static_cast<double>(Radius) * Radius;

    int const Band = EqObj_ExplosionBand(Distance, RadiusSq, Middle, Inner);
    if (Band == 0)
        return;

    float const Resistance = Ce->get_archetype()->ExplosionResistance;
    damage_power(static_cast<float>(static_cast<double>(Band) * (1.0f / 3.0f) *
                                    Blast.Explosion->EnergyDamage * Resistance), Out);   // vt +0x21c
}

// +0x204 -- .text:06ce9a90. True when any functioning shield was met.
bool EqObj::explosion_hit_shield(ExplosionDamageEvent const& Blast, DamageList& Out)
{
    bool bShielded = false;
    CEqObj* const Ce = ceqobj();

    float const RadiusSq = Blast.Explosion->Radius * Blast.Explosion->Radius;
    float const Middle = RadiusSq * (4.0f / 9.0f);
    float const Inner = RadiusSq * (1.0f / 9.0f);

    CEquipTraverser Walk(EquipTypeShield);
    CEquipManager const& Equipment = Ce->EquipManager;
    for (CEquip const* Equip = Equipment.Traverse(Walk); Equip != nullptr; Equip = Equipment.Traverse(Walk))
    {
        if (!Equip->IsFunctioning())
            continue;
        bShielded = true;

        CAttachedEquip const* const Attached = static_cast<CAttachedEquip const*>(Equip);
        float Radius = 0.0f;
        Vector Center;
        Attached->GetRadius(Radius);                               // vt +0x7c
        Attached->GetCenterOfMass(Center);                         // vt +0x70

        double const Dx = static_cast<double>(Blast.Position.x) - Center.x;
        double const Dy = static_cast<double>(Blast.Position.y) - Center.y;
        double const Dz = static_cast<double>(Blast.Position.z) - Center.z;
        double const Distance = ((Dz * Dz + Dx * Dx) + Dy * Dy) - static_cast<double>(Radius) * Radius;

        int const Band = EqObj_ExplosionBand(Distance, RadiusSq, Middle, Inner);
        if (Band == 0)
            continue;

        // Times the shield's explosion_resistance.
        float const Amount = static_cast<float>(
            (static_cast<double>(Blast.Explosion->HullDamage) * ShieldEquipConsts::HULL_DAMAGE_FACTOR +
             Blast.Explosion->EnergyDamage) *
            (static_cast<double>(Band) * (1.0f / 3.0f)));
        damage_shield(static_cast<CEShield*>(const_cast<CEquip*>(Equip)),
                      Attached->AttachedEquipArch()->ExplosionResistance * Amount, Out);   // vt +0x214
    }
    return bShielded;
}

// +0x228 -- .text:06ce9c50.
void EqObj::destroy_cargo(CEquip* Equip, DamageEntry::SubObjFate Fate, DamageList& Out)
{
    // The stack in the pod (CECargoPod+0x4c).
    CECargo* const Cargo = static_cast<CECargoPod*>(Equip)->Cargo;
    if (Cargo != nullptr)
    {
        int const FateValue = static_cast<int>(Fate);
        if (FateValue != 4 && FateValue != 5)
        {
            if (Fate == DamageEntry::SubObjFate::Separated)
            {
                // A separated pod drops its stack as loot (fate 4).
                destroy_equip(Cargo, DamageEntry::SubObjFate::Loot, Out);   // vt +0x224
            }
            else
            {
                Out.add_damage_entry(Cargo->Id, 0.0f, DamageEntry::SubObjFate::Destroyed);
            }
        }
        else
        {
            // At a solar, a valid inflicting player (PlayerDB::is_valid is a
            // real call here) that already has this visit gets no loot.
            bool bRefused = false;
            unsigned int const Inflictor = Out.InflictorOwner;
            CSolar* const Solar = as_solar();
            if (Solar != nullptr && Players.is_valid(Inflictor))
            {
                if (!Players[Inflictor].should_queue_visit(Solar->Id, Solar->GetVisitValue()))
                    bRefused = true;
            }

            if (!bRefused)
            {
                // Fate 4 asks the controller (event 0x13) and takes the desc it
                // hands back; fate 5 reads it.
                EquipDesc Desc;
                if (Fate == DamageEntry::SubObjFate::Loot)
                {
                    FEquipLootEvent Event;
                    Event.SystemId = 0;
                    Event.InflictorId = 0;
                    Event.ObjId = 0;
                    Event.Cause = DamageCause::Unknown;
                    Event.SystemId = OwnerSystem->SystemInfo->NicknameId;   // FUN_06d06cd0
                    Event.InflictorId = Out.InflictorId;
                    Event.ObjId = get_id();
                    Event.Cause = Out.Cause;
                    Cargo->GetEquipDesc(Event.Desc);                         // vt +0x28
                    Server_ControllerDispatch(0, pub::Controller::EMessage::EquipDropLoot, &Event);
                    Desc = Event.Desc;
                }
                else
                {
                    Cargo->GetEquipDesc(Desc);
                }

                // Nothing left: the pod is only separated.
                if (Desc.Count <= 0 || !(Desc.Health > 0.0f))
                {
                    Fate = DamageEntry::SubObjFate::Separated;
                }
                else
                {
                    // build_equip_loot (vt +0x248) on the pod.
                    FLootRecord Record;
                    Record.Id = 0;
                    Record.Arch = nullptr;
                    Record.HitPts = 0.0f;
                    Record.ContentsArch = nullptr;
                    Record.ContentsHitPts = 0.0f;
                    Record.Count = 0;
                    Record.MarkForPlayer = 0;
                    Record.NameId = 0;
                    Record.bCanAiTractor = 1;
                    Record.bMission = 0;
                    if (build_equip_loot(static_cast<CAttachedEquip const*>(Equip), Record))
                    {
                        // Cause 0x16 drops it in mode 1, anything else 2.
                        Record.ContentsHitPts = Desc.Health * Record.ContentsArch->HitPts;
                        Record.Count = Desc.Count;
                        int const Mode = (Out.Cause == DamageCause::CargoPodJettison) ? 1 : 2;
                        OwnerSystem->create_loot(get_id(), Mode, &Record, 1);

                        // The visit recorded (FUN_06d4b270), owner re-read.
                        CSolar* const SolarAgain = as_solar();
                        unsigned int const InflictorAgain = Out.InflictorOwner;
                        if (SolarAgain != nullptr && Players.is_valid(InflictorAgain))
                            Players[InflictorAgain].queue_visit(SolarAgain->Id,
                                                                SolarAgain->GetVisitValue());
                    }
                }
            }

            // The stack is always gone.
            Out.add_damage_entry(Cargo->Id, 0.0f, DamageEntry::SubObjFate::Destroyed);
        }
    }

    // The pod's own entry with the (possibly rewritten) fate.
    Out.add_damage_entry(Equip->Id, 0.0f, Fate);
}

// +0x224 -- .text:06ce9f70.
void EqObj::destroy_equip(CEquip* Equip, DamageEntry::SubObjFate Fate, DamageList& Out)
{
    if (Equip->IsDestroyed())
        return;

    // A cargo pod goes through destroy_cargo.
    if (Equip->Type == EquipTypeCargoPod)
    {
        destroy_cargo(Equip, Fate, Out);                           // vt +0x228
        return;
    }

    int const FateValue = static_cast<int>(Fate);
    if (FateValue == 4 || FateValue == 5)
    {
        if (!Equip->IsLootable() || Equip->IsDisabled())
        {
            Fate = DamageEntry::SubObjFate::Separated;
        }
        else
        {
            // The solar visit test (a real PlayerDB::is_valid call).
            bool bRefused = false;
            unsigned int const Inflictor = Out.InflictorOwner;
            CSolar* const Solar = as_solar();
            if (Solar != nullptr && Players.is_valid(Inflictor))
            {
                if (!Players[Inflictor].should_queue_visit(Solar->Id, Solar->GetVisitValue()))
                    bRefused = true;
            }

            if (bRefused)
            {
                Fate = DamageEntry::SubObjFate::Separated;
            }
            else
            {
                // Event 0x13 for fate 4.
                EquipDesc Desc;
                if (Fate == DamageEntry::SubObjFate::Loot)
                {
                    FEquipLootEvent Event;
                    Event.SystemId = 0;
                    Event.InflictorId = 0;
                    Event.ObjId = 0;
                    Event.Cause = DamageCause::Unknown;
                    Event.SystemId = OwnerSystem->SystemInfo->NicknameId;   // FUN_06d06cd0
                    Event.ObjId = get_id();
                    Event.InflictorId = Out.InflictorId;
                    Event.Cause = Out.Cause;
                    Equip->GetEquipDesc(Event.Desc);
                    Server_ControllerDispatch(0, pub::Controller::EMessage::EquipDropLoot, &Event);
                    Desc = Event.Desc;
                }
                else
                {
                    Equip->GetEquipDesc(Desc);
                }

                if (Desc.Count <= 0 || !(Desc.Health > 0.0f))
                {
                    Fate = DamageEntry::SubObjFate::Separated;
                }
                else
                {
                    CAttachedEquip const* const Attached = CAttachedEquip::cast(Equip);
                    if (Attached != nullptr)
                    {
                        // One record for the attached equip itself.
                        FLootRecord Record;
                        Record.Id = 0;
                        Record.Arch = nullptr;
                        Record.HitPts = 0.0f;
                        Record.ContentsArch = nullptr;
                        Record.ContentsHitPts = 0.0f;
                        Record.Count = 0;
                        Record.MarkForPlayer = 0;
                        Record.NameId = 0;
                        Record.bCanAiTractor = 1;
                        Record.bMission = 0;
                        if (build_equip_loot(Attached, Record))            // vt +0x248
                        {
                            // Both hit-point fields take Health * ContentsArch
                            // hit points.
                            float const HitPts = Desc.Health * Record.ContentsArch->HitPts;
                            Record.ContentsHitPts = HitPts;
                            Record.HitPts = HitPts;
                            int const Mode = (Out.Cause == DamageCause::CargoPodJettison) ? 1 : 2;
                            OwnerSystem->create_loot(get_id(), Mode, &Record, 1);
                        }
                    }
                    else
                    {
                        // Internal equipment becomes build_loot's records
                        // (mode 2).
                        std::vector<FLootRecord> Loot;
                        if (build_loot(Desc, Loot) != 0)                   // vt +0x244
                            OwnerSystem->create_loot(get_id(), 2, Loot.data(),
                                                     static_cast<int>(Loot.size()));
                    }

                    // On both paths, whether or not a record was built, the
                    // visit is recorded.
                    CSolar* const SolarAgain = as_solar();
                    unsigned int const InflictorAgain = Out.InflictorOwner;
                    if (SolarAgain != nullptr && Players.is_valid(InflictorAgain))
                        Players[InflictorAgain].queue_visit(SolarAgain->Id,
                                                            SolarAgain->GetVisitValue());
                }
            }
        }
    }

    Out.add_damage_entry(Equip->Id, 0.0f, Fate);
}

// +0x1b0 -- .text:06cea3a0.
bool EqObj::damage_hull(float Amount, DamageList& Damage)
{
    // Only a positive amount is scaled.
    if (Amount > 0.0f)
    {
        // A player's current ship takes the player's difficulty scale
        // (PlayerData+0x3c4).
        PlayerData* const Player = player_of_ship();
        if (Player != nullptr)
            Amount = Player->difficulty_scale * Amount;

        // Armour (type 0x1000000) divides by its hit_pts_scale (Armor+0x70).
        CEquip* const Armor = ceqobj()->EquipManager.FindFirst(EquipTypeArmor);
        if (Armor != nullptr)
            Amount = Amount / static_cast<CEArmor*>(Armor)->ArmorArch()->HitPtsScale;
    }

    // Direct call to the base body (FUN_06ceec90).
    return GameObject::damage_hull(Amount, Damage);
}

// +0x210 -- .text:06cea4a0.
bool EqObj::damage_equip(CEquip* Equip, float Damage, DamageList& Out)
{
    if (Damage == 0.0f)
        return false;
    if (Equip->IsDestroyed())
        return false;

    // What the list already holds for the equip, else its hit points.
    float Current = Out.get_hit_pts_left(Equip->Id);
    if (Current < 0.0f)
        Current = Equip->GetHitPoints();
    float NewHitPts = Current;

    if (Damage > 0.0f)
    {
        // An attached equip of an OWNED object divides by
        // PLAYER_ATTACHED_EQUIP_HIT_PTS_SCALE and, on the owner's current ship,
        // takes the difficulty scale. Unowned objects and internal equipment
        // take the damage as it is.
        double Applied = Damage;
        if (CAttachedEquip::cast(Equip) != nullptr && csimple()->OwnerPlayer != 0)
        {
            Damage = Damage / PLAYER_ATTACHED_EQUIP_HIT_PTS_SCALE;
            Applied = Damage;
            PlayerData* const Player = player_of_ship();
            if (Player != nullptr)
                Applied = static_cast<double>(Player->difficulty_scale) * Damage;
        }
        double const Left = Current - Applied;  // adj stays on the x87 stack
        NewHitPts = (Left > 0.0) ? static_cast<float>(Left) : 0.0f;
    }
    else if (Damage < 0.0f)
    {
        // A negative amount empties it.
        NewHitPts = 0.0f;
    }

    // A blocked list leaves the equip no lower than (1 - invincible timer) of
    // its max: already below the floor keeps the current value; a new value
    // below it is raised to Max * Floor. The current ratio tests Max for zero,
    // the new one does not.
    if (is_damage_blocked(Out))
    {
        float const Floor = 1.0f - get_max_hp_loss();
        float const Max = Equip->GetMaxHitPoints();
        double const RelCurrent = (Max != 0.0f) ? static_cast<double>(Current) / Max : 0.0;
        if (RelCurrent < Floor)
        {
            NewHitPts = Current;
        }
        else if (static_cast<double>(NewHitPts) / Max < Floor)
        {
            NewHitPts = Max * Floor;
        }
    }

    if (NewHitPts == Current)
        return false;

    if (NewHitPts > 0.0f)
    {
        Out.add_damage_entry(Equip->Id, NewHitPts, DamageEntry::SubObjFate::None);
        return false;
    }

    // Destroyed: separated, except a launcher of a weapons-offline object
    // (CEqObj+0x15c) outside single player, which keeps fate 0.
    DamageEntry::SubObjFate DeathFate = DamageEntry::SubObjFate::Separated;
    if (!SinglePlayer() && ceqobj()->bWeaponsOffline && CELauncher::cast(Equip) != nullptr)
        DeathFate = DamageEntry::SubObjFate::None;
    destroy_equip(Equip, DeathFate, Out);                          // vt +0x224
    return true;
}

// +0x214 -- .text:06cea740.
bool EqObj::damage_shield(CEShield* Shield, float Damage, DamageList& Out)
{
    if (Damage == 0.0f)
        return false;
    if (Shield->IsDestroyed())
        return false;

    float Current = Out.get_hit_pts_left(Shield->Id);
    if (Current < 0.0f)
        Current = Shield->GetHitPoints();
    float NewHitPts = Current;

    if (Damage > 0.0f)
    {
        // Only the difficulty scale here.
        double Applied = Damage;
        PlayerData* const Player = player_of_ship();
        if (Player != nullptr)
            Applied = static_cast<double>(Player->difficulty_scale) * Damage;
        double const Left = Current - Applied;  // adj stays on the x87 stack
        NewHitPts = (Left > 0.0) ? static_cast<float>(Left) : 0.0f;
    }
    else if (Damage < 0.0f)
    {
        NewHitPts = 0.0f;
    }

    // A blocked list: with no invincible timer left (<= 0 or unordered) the
    // shield keeps its current value; otherwise a new relative value below
    // F = 1 - MaxHpLoss is either held at the current value (when that one is below
    // F too) or raised to Max * F (GetMaxHitPoints re-read). Both ratios test
    // Max for zero here.
    if (is_damage_blocked(Out))
    {
        float const MaxLoss = get_max_hp_loss();
        if (!(MaxLoss > 0.0f))
        {
            NewHitPts = Current;
        }
        else
        {
            float const Max = Shield->GetMaxHitPoints();
            double const RelNew = (Max != 0.0f) ? static_cast<double>(NewHitPts) / Max : 0.0;  // kept on the x87 stack
            float const RelCurrent = (Max != 0.0f) ? Current / Max : 0.0f;  // stored as float
            float const Floor = 1.0f - MaxLoss;
            if (RelNew < Floor)
            {
                if (RelCurrent < Floor)
                    NewHitPts = Current;
                else
                    NewHitPts = Shield->GetMaxHitPoints() * Floor;
            }
        }
    }

    if (NewHitPts == Current)
        return false;

    Out.add_damage_entry(Shield->Id, NewHitPts, DamageEntry::SubObjFate::None);

    // True when the change crosses the shield's OfflineThreshold (+0x74): from
    // above to strictly below, or from at/below (or unordered) to strictly
    // above. The new side is New / Max (0 for a zero Max).
    float const Max = Shield->GetMaxHitPoints();
    float const RelNew = (Max != 0.0f) ? NewHitPts / Max : 0.0f;
    float const Threshold = Shield->OfflineThreshold;
    // The old ratio is compared unrounded, never stored.
    double const RelOld = Shield->GetRelativeHealth();
    if (RelOld > Threshold)
        return RelNew < Threshold;
    return RelNew > Threshold;
}

// +0x220 -- .text:06cea9f0.
void EqObj::destroy_group(CArchGroup* Group, DamageEntry::SubObjFate Fate, DamageList& Out,
                          bool bWithChildren)
{
    if (Group->State == CArchGroup::STATE_KILLED || Group->State == CArchGroup::STATE_DESTROYED)
        return;

    // A group with hit points left (or unordered) is destroyed regardless; an
    // empty one only when the fate changes.
    float const HitPts = Group->GetHitPoints();
    if (!(HitPts > 0.0f) && !(HitPts != HitPts) && Group->Fate == static_cast<int>(Fate))
        return;

    // A group that takes its parent with it only marks the list destroyed.
    if (Group->DestroysParent())
    {
        Out.bDestroyed = true;
        return;
    }

    damage_linked_equipment(Group, Out);                           // vt +0x22c
    if (bWithChildren)
        destroy_group_contents(Group, Out);                        // vt +0x230

    Out.add_damage_entry(Group->Arch->Id, 0.0f, Fate);
}

// +0x218 -- .text:06ceaa80.
bool EqObj::damage_group(CArchGroup* Group, float Damage, DamageList& Out)
{
    if (Damage == 0.0f)
        return false;
    // Killed (2) or destroyed (3) groups are done.
    if (Group->State == CArchGroup::STATE_KILLED || Group->State == CArchGroup::STATE_DESTROYED)
        return false;
    // Only a group with hit points above 0 (or unordered) takes damage.
    if (!(Group->GetHitPoints() > 0.0f) && !(Group->GetHitPoints() != Group->GetHitPoints()))
        return false;

    unsigned short const GroupId = Group->Arch->Id;
    float Current = Out.get_hit_pts_left(GroupId);
    if (Current < 0.0f)
        Current = Group->GetHitPoints();
    float NewHitPts = Current;

    if (Damage > 0.0f)
    {
        // Armour's hit_pts_scale (Arch+0x70) divides first; the quotient stays
        // on the FPU unrounded.
        double Scaled = Damage;
        CEquip* const Armor = ceqobj()->EquipManager.FindFirst(EquipTypeArmor);
        if (Armor != nullptr)
            Scaled = (double)Damage / static_cast<CEArmor*>(Armor)->ArmorArch()->HitPtsScale;

        // An OWNED object's group then divides by
        // PLAYER_COLLISION_GROUP_HIT_PTS_SCALE (the only rounding to float) and,
        // on the owner's current ship, takes the difficulty scale. An unowned
        // object subtracts the unrounded quotient directly.
        double Applied = Scaled;
        if (csimple()->OwnerPlayer != 0)
        {
            float const OwnedScaled = (float)(Scaled / PLAYER_COLLISION_GROUP_HIT_PTS_SCALE);
            Applied = OwnedScaled;
            PlayerData* const Player = player_of_ship();
            if (Player != nullptr)
                Applied = (double)Player->difficulty_scale * OwnedScaled;
        }
        double const Left = Current - Applied;  // adj stays on the x87 stack
        NewHitPts = (Left > 0.0) ? static_cast<float>(Left) : 0.0f;
    }
    else if (Damage < 0.0f)
    {
        NewHitPts = 0.0f;
    }

    // The invincibility floor, as damage_equip's but with Max tested for zero
    // in both ratios.
    if (is_damage_blocked(Out))
    {
        float const Floor = 1.0f - get_max_hp_loss();
        // The first zero test sees the unrounded GetMaxHitPoints, the divisions
        // and the second zero test the stored float.
        double const MaxExact = Group->GetMaxHitPoints();
        float const Max = (float)MaxExact;
        double const RelCurrent = (MaxExact != 0.0) ? (double)Current / Max : 0.0;
        if (RelCurrent < Floor)
        {
            NewHitPts = Current;
        }
        else
        {
            double const RelNew = (Max != 0.0f) ? static_cast<double>(NewHitPts) / Max : 0.0;
            if (RelNew < Floor)
                NewHitPts = Max * Floor;
        }
    }

    bool bDestroyed = false;
    if (NewHitPts != Current)
    {
        // A burning death fuse keeps low-cause damage from destroying the group
        // outright.
        bool const bFuseHolds = is_death_fuse_burning() && static_cast<int>(Out.Cause) < 0x10;

        // <= 0 AND unordered go to the destroy unless the fuse holds.
        if (!(NewHitPts > 0.0f) && !bFuseHolds)
        {
            destroy_group(Group, DamageEntry::SubObjFate::Separated, Out, true);   // vt +0x220
            bDestroyed = true;
        }
        else
        {
            // > and unordered are not "destroyed" here.
            Out.add_damage_entry(GroupId, NewHitPts, DamageEntry::SubObjFate::None);
            bDestroyed = (NewHitPts <= 0.0f);
        }
    }

    // A root-health-proxy group passes the ORIGINAL damage on to the hull; its
    // result counts as well.
    if (Group->IsRootHealthProxy() && damage_hull(Damage, Out))   // vt +0x1b0
        return true;
    return bDestroyed;
}

// +0x22c -- .text:06ceadc0.
void EqObj::damage_linked_equipment(CArchGroup* Group, DamageList& Out)
{
    unsigned int const LinkedType = Group->LinkedEquipmentType();
    if (LinkedType == 0)
        return;

    if (LinkedType == EquipTypeShield)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\Space\\EqObj.cpp", 0x91b,
              "Implement me!  Linked shield damage!");
        return;
    }

    // Every equip of that type takes LinkedEquipmentDamage * its max hit
    // points (const traversal; the manager is fixed before the loop).
    CEquipTraverser Walk(static_cast<int>(LinkedType));
    CEquipManager const& Equipment = ceqobj()->EquipManager;
    for (CEquip const* Equip = Equipment.Traverse(Walk); Equip != nullptr; Equip = Equipment.Traverse(Walk))
    {
        float const Max = Equip->GetMaxHitPoints();                // vt +0x38
        damage_equip(const_cast<CEquip*>(Equip), Group->LinkedEquipmentDamage() * Max, Out);   // vt +0x210
    }
}

// +0x230 -- .text:06ceaea0.
void EqObj::destroy_group_contents(CArchGroup* Group, DamageList& Out)
{
    CEqObj* const Ce = ceqobj();

    // External equipment (mask 0x1fe3) mounted under the group's instance,
    // except types 1 and 2, is destroyed (fate 1).
    CEquipTraverser Walk(kEquipMaskExternal);
    CEquipManager const& Equipment = Ce->EquipManager;
    for (CEquip const* Equip = Equipment.Traverse(Walk); Equip != nullptr; Equip = Equipment.Traverse(Walk))
    {
        // vt +0x64: CExternalEquip::GetParentConnector(false).
        long const Parent = static_cast<CExternalEquip const*>(Equip)->GetParentConnector(false);
        if (!Group->IsInstInGroup(Parent))
            continue;
        if (Equip->Type == EquipTypeLight || Equip->Type == EquipTypeAttachedFX)
            continue;
        destroy_equip(const_cast<CEquip*>(Equip), DamageEntry::SubObjFate::Destroyed, Out);   // vt +0x224
    }

    // Every OTHER group whose root instance lies in this one is destroyed too
    // (fate 1, without its own contents).
    CArchGrpTraverser Groups;
    CArchGroupManager const& GroupManager = Ce->ArchGroups;
    for (CArchGroup const* Child = GroupManager.Traverse(Groups); Child != nullptr;
         Child = GroupManager.Traverse(Groups))
    {
        if (Group->Arch->Id == Child->Arch->Id)
            continue;
        if (!Group->IsInstInGroup(Child->GetRootIndex()))
            continue;
        destroy_group(const_cast<CArchGroup*>(Child), DamageEntry::SubObjFate::Destroyed, Out, false);   // vt +0x220
    }
}

// +0x21c -- .text:06ceafc0.
bool EqObj::damage_power(float Damage, DamageList& Out)
{
    if (Damage == 0.0f)
        return false;

    // No power plant (MaxPower <= 0, CEqObj+0x19c; tested twice in retail)
    // takes no energy damage.
    CEqObj* const Ce = ceqobj();
    if (Ce->MaxPower <= 0.0f)
        return false;

    float Current = Out.get_hit_pts_left(SUBOBJ_ID_POWER);
    if (Current < 0.0f)
        Current = Ce->Power;                                       // +0x198
    float NewHitPts = Current;

    if (Damage > 0.0f)
    {
        double Applied = Damage;
        PlayerData* const Player = player_of_ship();
        if (Player != nullptr)
            Applied = static_cast<double>(Player->difficulty_scale) * Damage;
        double const Left = Current - Applied;  // adj stays on the x87 stack
        NewHitPts = (Left > 0.0) ? static_cast<float>(Left) : 0.0f;
    }
    else if (Damage < 0.0f)
    {
        NewHitPts = 0.0f;
    }

    // The invincibility floor over MaxPower, through its reciprocal (no zero
    // test).
    if (is_damage_blocked(Out))
    {
        float const Max = Ce->MaxPower;
        float const Floor = 1.0f - get_max_hp_loss();
        double const Inverse = 1.0 / Max;  // never stored
        if (Current * Inverse < Floor)
            NewHitPts = Current;
        else if (NewHitPts * Inverse < Floor)
            NewHitPts = Floor * Max;
    }

    if (NewHitPts == Current)
        return false;

    Out.add_damage_entry(SUBOBJ_ID_POWER, NewHitPts, DamageEntry::SubObjFate::None);
    // > and unordered answer false.
    return NewHitPts <= 0.0f;
}

bool EqObj::is_death_fuse_burning()
{
    for (std::list<ServerFuse>::iterator It = Fuses.begin(); It != Fuses.end(); ++It)
    {
        if (It->IsDeathFuse() && !It->IsSpent())
            return true;
    }
    return false;
}

// +0x188 -- .text:06ceb330.
void EqObj::apply_damage_list(DamageList& Damage)
{
    // Re-entry is reported, not refused.
    if (is_in_damage_callback())
    {
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: ID(0x%X) Arch(%s): Tried to apply damage of type %s within another damage callback.",
              "E:\\FL\\Scratch\\Source\\Server\\Space\\EqObj.cpp", 0x9d7,
              get_id(), Object->get_archetype()->Nickname.get_string(),
              DamageList::DmgCauseToString(Damage.Cause));
    }

    set_in_damage_callback(true);                                  // vt +0x190

    // Time of the last damage, while the hull is still above 0.0 (DAT_06d65538).
    if (ceqobj()->get_relative_health() > 0.0)
        LastDamageTime = Timing::read();

    if (Damage.Count != 0)
    {
        for (std::list<DamageEntry>::iterator It = Damage.Entries.begin();
             It != Damage.Entries.end(); ++It)
        {
            unsigned short const Id = It->SubObjId;
            if (Id == SUBOBJ_ID_ROOT)
                apply_root_entry(Damage, *It);                     // vt +0x234
            else if (Id == SUBOBJ_ID_POWER)
                apply_power_entry(Damage, *It);                    // vt +0x23c
            else if (SubObjectID::IsArchGroupID(Id))
                apply_group_entry(Damage, *It);                    // vt +0x240
            else if (SubObjectID::IsEquipID(Id))
                apply_equip_entry(Damage, *It);                    // vt +0x238
        }

        // Outside single player the dead sub-objects go at once.
        if (!SinglePlayer())
        {
            CEqObj* const Ce = ceqobj();
            Ce->destroy_sub_objs(Damage, false);
            Ce->EquipManager.CleanUp();
        }

        OwnerSystem->damage_object(this, &Damage);      // FUN_06d0b800
    }

    // A burning death fuse holds off causes below 0x10.
    if (Damage.bDestroyed &&
        !is_damage_blocked(Damage) &&                              // vt +0x1b8
        !(is_death_fuse_burning() && static_cast<int>(Damage.Cause) < 0x10) &&
        !is_destroyed() &&                                         // vt +0x160
        !ceqobj()->bWeaponsOffline)
    {
        destroy(DESTROY_FUSE, Damage.InflictorId);                            // vt +0x158
    }

    set_in_damage_callback(false);                                 // vt +0x190
}

// The light/unlight pass apply_root_entry and apply_group_entry share (both
// inline it): damage (hit points dropping from Before to After) lights every
// fuse whose threshold Value2 lies in [After, Before); healing unlights every
// fuse whose threshold lies in [Before, After). Before is re-read for every
// node (light_fuse / unlight_fuse may change it).
template <typename FReadHitPts>
void EqObj::sweep_fuses(std::vector<Archetype::FGroupFuse> const& List, FReadHitPts ReadHitPts,
                        float After, unsigned short SubObjId, unsigned int InflictorId)
{
    for (std::vector<Archetype::FGroupFuse>::const_iterator It = List.begin(); It != List.end(); ++It)
    {
        if (ReadHitPts() > After)
        {
            if (!(ReadHitPts() > It->Value2))
                continue;
            // Skips "less" and unordered alike.
            if (!(It->Value2 >= After))
                continue;
            light_fuse(InflictorId, It->Fuse, SubObjId, It->Value1, -1.0f);
        }
        else
        {
            if (!(It->Value2 < After))
                continue;
            // Skips "greater" and unordered alike.
            if (!(ReadHitPts() <= It->Value2))
                continue;
            unlight_fuse(It->Fuse, SubObjId, It->Value1);
        }
    }
}

// The behavior-manager half of the four apply_*_entry slots: the controller
// event 0xd record, re-ordered, as behavior event 0. EquipType and
// CollisionGroupType are passed in because each site writes one of them from
// the event and zeroes the other with an immediate.
static void EqObj_NotifyBehaviorOfDamage(IBehaviorManager* Behavior, FSubObjDamageEvent const& Ev,
                                         unsigned int EquipType, unsigned int CollisionGroupType)
{
    if (Behavior == nullptr)
        return;
    FBehaviorDamageEvent Record;
    Record.SystemId = Ev.SystemId;
    Record.InflictorId = Ev.InflictorId;
    Record.ObjId = Ev.ObjId;
    Record.EquipType = EquipType;
    Record.CollisionGroupType = CollisionGroupType;
    Record.Cause = static_cast<int>(Ev.Cause);
    Record.Damage = Ev.Damage;
    Record.RelativeDamage = Ev.RelativeDamage;
    Record.RelativeHealth = Ev.RelativeHealth;
    FBehaviorManagerEvent Event;
    Event.Type = FBehaviorManagerEvent::kEventDamageTaken;
    Event.Data = &Record;
    Behavior->OnEvent(&Event);
}

// +0x234 -- .text:06ceb550.
void EqObj::apply_root_entry(DamageList const& Damage, DamageEntry const& Entry)
{
    CEqObj* const Ce = ceqobj();

    // Controller event 0xd.
    FSubObjDamageEvent Ev;
    Ev.ObjId = 0;
    Ev.SystemId = 0;
    Ev.InflictorId = 0;
    Ev.EquipType = 0;
    Ev.CollisionGroupType = 0;
    Ev.Cause = DamageCause::Unknown;
    Ev.Damage = 0.0f;
    Ev.RelativeDamage = 0.0f;
    Ev.RelativeHealth = 0.0f;
    Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Ev.InflictorId = Damage.InflictorId;
    Ev.ObjId = get_id();
    Ev.Cause = Damage.Cause;
    Ev.Damage = Ce->get_hit_pts() - Entry.HitPts;
    // Root::HitPts (+0x1c), no zero test.
    Ev.RelativeDamage = Ev.Damage / Ce->Archetype->HitPts;
    Ev.RelativeHealth = Entry.HitPts / Ce->Archetype->HitPts;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Ev);

    EqObj_NotifyBehaviorOfDamage(Ce->get_behavior_interface(), Ev, 0, 0);

    // The archetype's fuses, when the hull's hit points change.
    if (Ce->get_hit_pts() != Entry.HitPts)
    {
        sweep_fuses(*ceqobj()->eqobjarch()->Fuses,
                    [Ce]() { return Ce->get_hit_pts(); },
                    Entry.HitPts, SUBOBJ_ID_ROOT, Damage.InflictorId);
    }

    // Direct call to FUN_06ceef70, not through the vtable.
    GameObject::apply_root_damage(Entry);
}

// +0x238 -- .text:06ceb7b0.
void EqObj::apply_equip_entry(DamageList const& Damage, DamageEntry const& Entry)
{
    CEqObj* const Ce = ceqobj();

    CEquip* const Equip = Ce->EquipManager.FindByID(Entry.SubObjId);
    if (Equip == nullptr || Equip->IsDestroyed())
        return;

    // A live launcher stamps the time on its ServerEquip, found by a second
    // FindByID and a walk of the +0x70 list.
    if (Equip->GetRelativeHealth() > 0.0 && CELauncher::cast(Equip) != nullptr)
    {
        CEquip* const Again = ceqobj()->EquipManager.FindByID(Entry.SubObjId);
        for (std::list<ServerEquip*>::iterator It = ServerEquips.begin();
             It != ServerEquips.end(); ++It)
        {
            if ((*It)->Equip != Again)
                continue;
            SrvLauncher* const Launcher = static_cast<SrvLauncher*>(*It);
            if (Launcher != nullptr)
                Launcher->LastDamageTime = Timing::read();
            break;
        }
    }

    // Controller event 0xd.
    FSubObjDamageEvent Ev;
    Ev.ObjId = 0;
    Ev.SystemId = 0;
    Ev.InflictorId = 0;
    Ev.EquipType = 0;
    Ev.CollisionGroupType = 0;
    Ev.Cause = DamageCause::Unknown;
    Ev.Damage = 0.0f;
    Ev.RelativeDamage = 0.0f;
    Ev.RelativeHealth = 0.0f;
    Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Ev.InflictorId = Damage.InflictorId;
    Ev.ObjId = get_id();
    Ev.Cause = Damage.Cause;
    Ev.EquipType = Equip->Type;
    Ev.Damage = Equip->GetHitPoints() - Entry.HitPts;
    float const Max = Equip->GetMaxHitPoints();
    // A zero max gives 0 for both ratios.
    if (Max != 0.0f)
        Ev.RelativeDamage = Ev.Damage / Max;
    else
        Ev.RelativeDamage = 0.0f;
    if (Max != 0.0f)
        Ev.RelativeHealth = Entry.HitPts / Max;
    else
        Ev.RelativeHealth = 0.0f;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Ev);

    EqObj_NotifyBehaviorOfDamage(Ce->get_behavior_interface(), Ev, Ev.EquipType, 0);

    // A shield-battery list (cause 0x13) boosts the shield instead of setting
    // its hit points.
    if (Damage.Cause == DamageCause::ShieldBattery)
    {
        static_cast<CEShield*>(Equip)->ShieldBatteryBoost(Entry.HitPts);
        return;
    }

    // A cargo stack (type 0x10000, Count +0x28 > 1 unsigned) loses one unit
    // when its entry reaches 0, otherwise takes the hit points.
    if (Equip->Type == EquipTypeCargo && static_cast<CECargo*>(Equip)->Count > 1)
    {
        if (Entry.HitPts == 0.0f)
        {
            static_cast<CECargo*>(Equip)->RemoveFromStack(1);
            return;
        }
        Equip->SetHitPoints(Entry.HitPts);
        return;
    }

    Equip->SetHitPoints(Entry.HitPts);
}

// +0x240 -- .text:06ceba40.
void EqObj::apply_group_entry(DamageList const& Damage, DamageEntry& Entry)
{
    CEqObj* const Ce = ceqobj();

    // No null test on the group.
    CArchGroup* const Group = Ce->ArchGroups.FindByID(Entry.SubObjId);

    // A group that dies empties the dock queue (FUN_06cf7720); a persistent
    // fate is reported.
    if (Entry.Fate != DamageEntry::SubObjFate::None)
    {
        if (ceqobj() != nullptr)
            DockQueue.clear();
        if (Entry.Fate == DamageEntry::SubObjFate::Loot)
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s",
                  "E:\\FL\\Scratch\\Source\\Server\\Space\\EqObj.cpp", 0xaf5,
                  "Persistent CArchGroups not supported.");
        }
    }

    // Controller event 0xd.
    FSubObjDamageEvent Ev;
    Ev.ObjId = 0;
    Ev.SystemId = 0;
    Ev.InflictorId = 0;
    Ev.EquipType = 0;
    Ev.CollisionGroupType = 0;
    Ev.Cause = DamageCause::Unknown;
    Ev.Damage = 0.0f;
    Ev.RelativeDamage = 0.0f;
    Ev.RelativeHealth = 0.0f;
    Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Ev.InflictorId = Damage.InflictorId;
    Ev.ObjId = get_id();
    Ev.CollisionGroupType = Group->GetCollisionGroupType();
    Ev.Cause = Damage.Cause;
    Ev.Damage = Group->GetHitPoints() - Entry.HitPts;
    // GetMaxHitPoints stays in ST0 unrounded for both zero tests and both
    // divisions; only the two quotients are rounded to float.
    double const Max = Group->GetMaxHitPoints();
    if (Max != 0.0)
        Ev.RelativeDamage = (float)(Ev.Damage / Max);
    else
        Ev.RelativeDamage = 0.0f;
    if (Max != 0.0)
        Ev.RelativeHealth = (float)(Entry.HitPts / Max);
    else
        Ev.RelativeHealth = 0.0f;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Ev);

    EqObj_NotifyBehaviorOfDamage(Ce->get_behavior_interface(), Ev, 0, Ev.CollisionGroupType);

    // The group's fuses when its hit points change.
    if (Group->GetHitPoints() != Entry.HitPts)
    {
        sweep_fuses(*Group->FuseIgnitions().Items,
                    [Group]() { return Group->GetHitPoints(); },
                    Entry.HitPts, Group->Arch->Id, Damage.InflictorId);
    }

    // A burning death fuse keeps low-cause groups attached: the entry's fate
    // is cleared.
    if (is_death_fuse_burning() && static_cast<int>(Damage.Cause) < 0x10)
        Entry.Fate = DamageEntry::SubObjFate::None;

    Group->SetHitPoints(Entry.HitPts);
}

// +0x23c -- .text:06cebd40.
void EqObj::apply_power_entry(DamageList const& Damage, DamageEntry const& Entry)
{
    CEqObj* const Ce = ceqobj();

    // Controller event 0xd; EquipType 0x40000, the ratios over
    // CEqObj::MaxPower (+0x19c) with no zero test.
    FSubObjDamageEvent Ev;
    Ev.ObjId = 0;
    Ev.SystemId = 0;
    Ev.InflictorId = 0;
    Ev.EquipType = 0;
    Ev.CollisionGroupType = 0;
    Ev.Cause = DamageCause::Unknown;
    Ev.Damage = 0.0f;
    Ev.RelativeDamage = 0.0f;
    Ev.RelativeHealth = 0.0f;
    Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Ev.InflictorId = Damage.InflictorId;
    Ev.ObjId = get_id();
    Ev.EquipType = EquipTypePower;
    Ev.Cause = Damage.Cause;
    Ev.Damage = Ce->Power - Entry.HitPts;
    Ev.RelativeDamage = Ev.Damage / Ce->MaxPower;
    Ev.RelativeHealth = Entry.HitPts / Ce->MaxPower;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Ev);

    EqObj_NotifyBehaviorOfDamage(Ce->get_behavior_interface(), Ev, Ev.EquipType, 0);

    Ce->Power = Entry.HitPts;
}

// +0x244 -- .text:06cebe80. The loot records one EquipDesc becomes; returns
// how many were appended.
int EqObj::build_loot(EquipDesc const& Desc, std::vector<FLootRecord>& Out)
{
    int Built = 0;

    Archetype::Equipment* const Arch = Archetype::GetEquipment(Desc.ArchId);
    if (Arch == nullptr)
        return 0;

    // The contents are the equip itself at Health * its hit points; the loot
    // object is get_loot_appearance (Equipment slot 7) at ITS hit points.
    FLootRecord Record;
    Record.Id = 0;
    Record.Arch = nullptr;
    Record.HitPts = 0.0f;
    Record.Count = 0;
    Record.MarkForPlayer = 0;
    Record.NameId = 0;
    Record.bCanAiTractor = 1;
    Record.bMission = 0;
    Record.ContentsArch = Arch;
    Record.ContentsHitPts = Desc.Health * Arch->HitPts;
    Archetype::Equipment* const Appearance = Arch->get_loot_appearance();
    Record.Arch = Appearance;
    Record.HitPts = Appearance->HitPts;
    Record.bMission = Desc.bMission ? 1 : 0;

    Record.Orientation = get_transform().Rotation;
    Record.Position = get_position();

    // Mounted equipment starts at its own centre: a cargo stack at its pod's
    // (CECargo+0x20), anything external at its own. Otherwise (not found, or
    // neither) the desc's hardpoint on this object.
    if (!Desc.is_internal())
    {
        bool bPlaced = false;
        CEquip* const Equip = ceqobj()->EquipManager.FindByID(Desc.Id);
        if (Equip != nullptr)
        {
            CExternalEquip const* Mounted = nullptr;
            if (Equip->Type == EquipTypeCargo)
                Mounted = static_cast<CExternalEquip const*>(static_cast<CECargo*>(Equip)->CargoPod);
            else
                Mounted = CExternalEquip::cast(Equip);
            if (Mounted != nullptr)
            {
                Mounted->GetCenterOfMass(Record.Position);
                bPlaced = true;
            }
        }
        if (!bPlaced)
            HardpointPosition(get_index(), Desc.Hardpoint, &Record.Position, nullptr);
    }

    // Equipment+0x64 units_per_container.
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();
    int Remaining = Desc.Count;
    int const PerContainer = Arch->UnitsPerContainer;
    if (Remaining <= 0)
        return Built;

    do
    {
        // At most six records unless it is a mission item.
        if (!Desc.bMission && Built > 5)
            return Built;

        // AllocateObjectID is inlined in retail. min(remaining, per container)
        // is compared UNSIGNED.
        Record.Id = Maker->AllocateObjectID();
        Record.Count = (static_cast<unsigned>(Remaining) > static_cast<unsigned>(PerContainer))
                           ? PerContainer
                           : Remaining;

        Record.Velocity = get_velocity();

        // A random push: Scale = rand() / 32768 (DAT_06d656b8, kept as a
        // double), a random unit vector times 2 * Scale (each component rounded
        // to float), times 15 (DAT_06d651a8), added.
        double const Scale = static_cast<double>(rand()) * 3.0517578125e-05;
        Vector const Direction = random_on_unit_sphere();
        Vector const Push = { static_cast<float>((Scale + Scale) * Direction.x),
                              static_cast<float>((Scale + Scale) * Direction.y),
                              static_cast<float>((Scale + Scale) * Direction.z) };
        Vector const Kick = { Push.x * 15.0f, Push.y * 15.0f, Push.z * 15.0f };
        Record.Velocity.x = Kick.x + Record.Velocity.x;
        Record.Velocity.y = Record.Velocity.y + Kick.y;
        Record.Velocity.z = Record.Velocity.z + Kick.z;

        // A random spin about the orientation's first and third columns:
        // A, B = rand() / 32768 * 0.8 - 0.4 (doubles DAT_06d656b0 / DAT_06d656a8,
        // each rounded to float); AngularVelocity = col0 * A + col2 * B.
        float const SpinA = static_cast<float>(static_cast<double>(rand()) * 3.0517578125e-05 *
                                               0.800000011920929 - 0.4000000059604645);
        float const Col0X = Record.Orientation.E[0][0] * SpinA;
        float const Col0Y = Record.Orientation.E[1][0] * SpinA;
        float const Col0Z = Record.Orientation.E[2][0] * SpinA;
        float const SpinB = static_cast<float>(static_cast<double>(rand()) * 3.0517578125e-05 *
                                               0.800000011920929 - 0.4000000059604645);
        // The x and y products stay on the x87 stack before the add; only z's
        // product is stored as float first.
        float const Col2Z = Record.Orientation.E[2][2] * SpinB;
        Record.AngularVelocity.x =
            static_cast<float>(static_cast<double>(Record.Orientation.E[0][2]) * SpinB + Col0X);
        Record.AngularVelocity.y =
            static_cast<float>(static_cast<double>(Record.Orientation.E[1][2]) * SpinB + Col0Y);
        Record.AngularVelocity.z = Col2Z + Col0Z;

        Out.push_back(Record);

        ++Built;
        Remaining -= Record.Count;
    } while (Remaining > 0);

    return Built;
}

// +0x248 -- .text:06cec260. One attached equip -> one loot record.
bool EqObj::build_equip_loot(CAttachedEquip const* Equip, FLootRecord& Out)
{
    Out.Id = CFLIDMaker::GetInstance()->AllocateObjectID();
    Out.Arch = Equip->Arch;
    float const HitPts = Equip->GetHitPoints();
    Out.HitPts = HitPts;

    if (Equip->Type == EquipTypeCargoPod)
    {
        // A cargo pod carries its stack.
        CECargo* const Cargo = CECargoPod::cast(Equip)->Cargo;
        Out.ContentsArch = Cargo->Arch;
        Out.ContentsHitPts = Cargo->GetHitPoints();
        Out.Count = static_cast<int>(Cargo->Count);
    }
    else
    {
        Out.ContentsHitPts = HitPts;
        Out.ContentsArch = Out.Arch;
        Out.Count = 1;
    }

    // Retail then overwrites this with the engine's instance position.
    Equip->GetCenterOfMass(Out.Position);

    // IEngine slots 36 (position) and 38 (orientation) on the root instance.
    long const Instance = Equip->GetRootIndex();
    Vector const* const Position = DALib::Engine->GetInstancePosition(Instance);
    Out.Position.x = Position->x;
    Out.Position.y = Position->y;
    Out.Position.z = Position->z;
    Matrix const* const Orientation = DALib::Engine->GetInstanceOrientation(Instance);
    Out.Orientation = *Orientation;

    // Only the second out vector is used.
    Vector Unused1;
    Vector Impulse;
    Vector Unused2;
    ComputeExplosiveImpulses(Instance, Unused1, Impulse, Unused2);

    // Scaled by child_impulse / mass (Attached +0x78 / Root +0x20).
    Archetype::AttachedEquipment const* const Arch = Equip->AttachedEquipArch();
    // The quotient stays on the x87 stack; each component is rounded once.
    double const ImpulseScale = static_cast<double>(Arch->ChildImpulse) / Equip->AttachedEquipArch()->Mass;
    Impulse.x = static_cast<float>(Impulse.x * ImpulseScale);
    Impulse.y = static_cast<float>(Impulse.y * ImpulseScale);
    Impulse.z = static_cast<float>(Impulse.z * ImpulseScale);

    // Doubles DAT_06d656d8 / DAT_06d656d0.
    double const Jitter = static_cast<double>(rand()) * 3.0517578125e-05 * 0.5 + 0.75;
    Impulse.x = static_cast<float>(Jitter * Impulse.x);
    Impulse.y = static_cast<float>(Jitter * Impulse.y);
    Impulse.z = static_cast<float>(Jitter * Impulse.z);

    Equip->GetVelocity(Out.Velocity);
    Out.Velocity.x = Impulse.x + Out.Velocity.x;
    Out.Velocity.y = Impulse.y + Out.Velocity.y;
    Out.Velocity.z = Impulse.z + Out.Velocity.z;

    // rand() order: the FIRST value goes to y, the second to x, the third to z.
    float const SpinY = static_cast<float>(rand()) * 3.0517578125e-05f;
    float const SpinX = static_cast<float>(rand()) * 3.0517578125e-05f;
    float const SpinZ = static_cast<float>(rand()) * 3.0517578125e-05f;
    Out.AngularVelocity.x = SpinX;
    Out.AngularVelocity.y = SpinY;
    Out.AngularVelocity.z = SpinZ;

    // Normalised and scaled to rand() / 32768 * 3 + 2 (doubles DAT_06d656c8 /
    // DAT_06d656c0); one rounding of the factor.
    double const Spin = static_cast<double>(rand()) * 3.0517578125e-05 * 3.0 + 2.0;
    double const Length = sqrt(static_cast<double>(SpinX) * SpinX +
                               static_cast<double>(SpinY) * SpinY +
                               static_cast<double>(SpinZ) * SpinZ);
    float const Factor = static_cast<float>(Spin / Length);
    Out.AngularVelocity.x = Factor * Out.AngularVelocity.x;
    Out.AngularVelocity.y = Factor * Out.AngularVelocity.y;
    Out.AngularVelocity.z = Factor * Out.AngularVelocity.z;

    // Record +0x69.
    Out.bMission = Equip->IsTemporary() ? 1 : 0;
    return true;
}

// +0x1e4 -- .text:06cec4d0.
bool EqObj::light_fuse(unsigned int SourceId, ID_String const& FuseId, unsigned short SubObjId,
                       float AtT, float Lifetime)
{
    bool bFound = false;
    bool bIgnited = false;

    // A copy of this fuse is already burning: re-ignite it. Lifetime is not
    // applied here; retail only rewrites it to -1.0f (DAT_06d6572c), and that
    // is what the broadcast below carries.
    for (std::list<ServerFuse>::iterator It = Fuses.begin(); It != Fuses.end() && !bFound; ++It)
    {
        if (It->Name().Id != FuseId.Id)
            continue;
        bFound = true;
        bIgnited = It->IgniteAt(SourceId, SubObjId, AtT);
        if (Lifetime != -1.0f)
            Lifetime = -1.0f;
    }

    if (!bFound)
    {
        // The prototype from ServerFuseDB (FUN_06cfd390 / FUN_06d15d10).
        // Deviation: retail reads the value slot of whatever node find()
        // returns (the head on a miss) and tests it for null; the port reads it
        // only for a real node.
        std::map<unsigned, ServerFuse*>& Db = ServerFuseDB::Get();
        std::map<unsigned, ServerFuse*>::iterator Found = Db.find(FuseId.Id);
        ServerFuse* const Proto = (Found != Db.end()) ? Found->second : nullptr;
        if (Proto == nullptr)
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Fuse(%s) not found in FuseDB. Aborting.",
                  "E:\\FL\\Scratch\\Source\\Server\\Space\\EqObj.cpp", 0xc73, FuseId.get_string());
        }
        else
        {
            bFound = true;
            bIgnited = true;

            // A stack copy is inserted at end() (FUN_06cecdf0) and destroyed.
            {
                ServerFuse Copy(*Proto);
                Fuses.push_back(Copy);
            }
            ServerFuse& Lit = Fuses.back();

            Lit.SetOwner(this);                                    // FUN_06cfd1e0
            Lit.RandomizeActions();

            if (!bDeathFuseLit && Lit.bHasDestroyRoot)
            {
                // The first fuse that destroys the root: controller event 0x38.
                bDeathFuseLit = 1;
                FDeathFuseEvent Ev;
                Ev.SystemId = 0;
                Ev.ObjId = 0;
                Ev.ObjType = 0;
                Ev.Lifetime = 0.0f;
                Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;
                Ev.ObjId = get_id();
                get_type(Ev.ObjType);
                Server_ControllerDispatch(0, pub::Controller::EMessage::DeathFuseLit, &Ev);

                // The fuse's own lifetime below the controller's: take the
                // controller's, which replaces the Lifetime argument as well.
                if (Lit.Lifetime() < Ev.Lifetime)
                {
                    Lifetime = Ev.Lifetime;
                    Lit.OverrideLifetime(Ev.Lifetime);
                }
            }
            else if (Lifetime != -1.0f)
            {
                Lit.OverrideLifetime(Lifetime);
            }

            // The result is not kept.
            Lit.IgniteAt(SourceId, SubObjId, AtT);
        }
    }

    // Tell the system (FUN_06d0e750).
    if (OwnerSystem != nullptr && bIgnited)
    {
        FServerBurnFuseInfo Info;
        Info.SourceId = SourceId;
        Info.ObjectId = get_id();
        Info.FuseId = FuseId.Id;
        Info.SubObjId = SubObjId;
        Info.Pad_0x0e = 0;
        Info.AtT = AtT;
        Info.Lifetime = Lifetime;
        Info.bLit = 1;
        Info.Pad_0x19[0] = 0;
        Info.Pad_0x19[1] = 0;
        Info.Pad_0x19[2] = 0;
        OwnerSystem->broadcast_fuse(Info);
    }
    return bFound;
}

// +0x1e8 -- .text:06cec7f0.
bool EqObj::unlight_fuse(ID_String const& FuseId, unsigned short SubObjId, float AtT)
{
    // The first burning copy of FuseId only.
    bool bFound = false;
    for (std::list<ServerFuse>::iterator It = Fuses.begin(); It != Fuses.end() && !bFound; ++It)
    {
        if (It->Name().Id != FuseId.Id)
            continue;
        bFound = true;
        if (!It->UnBurn(SubObjId, AtT))
            continue;
        if (OwnerSystem == nullptr)
            continue;

        FServerBurnFuseInfo Info;
        Info.SourceId = 0;
        Info.ObjectId = get_id();
        Info.FuseId = FuseId.Id;
        Info.SubObjId = SubObjId;
        Info.Pad_0x0e = 0;
        Info.AtT = AtT;
        Info.Lifetime = -1.0f;
        Info.bLit = 0;
        Info.Pad_0x19[0] = 0;
        Info.Pad_0x19[1] = 0;
        Info.Pad_0x19[2] = 0;
        OwnerSystem->broadcast_fuse(Info);
    }
    return bFound;
}

// +0x1ec -- .text:06cec8d0: a death fuse (Fuse+0x0c) that is not spent.
// IObjInspect slot 14 (+0x38) -- .text:06ce6890, a tail jump through vt +0x1ec.
// Retail's slot is not const-qualified; the const_cast only bridges the port's
// const IObjInspect declaration.
bool EqObj::is_dying() const
{
    return const_cast<EqObj*>(this)->is_death_fuse_burning();
}

// +0x24c -- .text:06cec910.
void EqObj::decay_cargo(float Elapsed)
{
    DamageList Decay;
    Decay.Cause = DamageCause::LootDecay;

    // Not a solar (0x303), and the equipment manager has decaying cargo
    // (CEquipManager+0x1c > 0, signed). The CEqObj null is dereferenced as in
    // retail.
    CEqObj* const Ce = ceqobj();
    bool bDecay = true;
    if (Ce != nullptr && (static_cast<unsigned>(Ce->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
        bDecay = false;
    if (!(Ce->EquipManager.DecayingCargoCount > 0))
        bDecay = false;

    // A valid owning player with an open trade (PlayerData+0x3cc) freezes the
    // decay.
    unsigned int const Owner = csimple()->OwnerPlayer;
    if (Players.is_valid(Owner) && Players[Owner].trade_offer != nullptr)
        return;
    if (!bDecay)
        return;

    // Every cargo stack (non-const traversal).
    CEquipTraverser Walk(EquipTypeCargo);
    for (CEquip* Equip = Ce->EquipManager.Traverse(Walk); Equip != nullptr; Equip = Ce->EquipManager.Traverse(Walk))
    {
        CECargo* const Cargo = static_cast<CECargo*>(Equip);
        if (Cargo->Count == 0)                                     // unsigned
            continue;

        float const Amount = Cargo->GetDecayDamagePerSecond() * Elapsed;
        if (!(Amount > 0.0f))
            continue;

        float NewHitPts = 0.0f;
        if (Cargo->GetHitPoints() - Amount > 0.0f)
        {
            // Hit points re-read; a non-zero result is a plain entry.
            NewHitPts = Cargo->GetHitPoints() - Amount;
            if (NewHitPts != 0.0f)
            {
                Decay.add_damage_entry(Cargo->Id, NewHitPts, DamageEntry::SubObjFate::None);
                continue;
            }
        }

        // A spent single item is destroyed; a spent stack only records 0.
        if (Cargo->Count == 1)
            Decay.add_damage_entry(Cargo->Id, NewHitPts, DamageEntry::SubObjFate::Destroyed);
        else
            Decay.add_damage_entry(Cargo->Id, NewHitPts, DamageEntry::SubObjFate::None);
    }

    if (Decay.Count != 0)
        apply_damage_list(Decay);
}
