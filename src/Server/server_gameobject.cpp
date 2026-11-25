// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// ===========================================================================
// Server.dll's IObjRW wrapper family: GameObject / EqObj / Ship / Solar / Loot.
// See src/include/server_gameobject.h for the RTTI evidence behind the
// hierarchy and for the list of things this port does NOT model yet.
// ===========================================================================

#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"   // StarSystem::on_object_destroyed (GameObject::destroy)
#include "../include/common_misc.h"    // SUBOBJ_ID_NONE (Ship::allocate_equip_id)
#include "../include/server_createinfo.h" // Server_Acquire3DMathEngine (DAT_06d8d71c)
#include "../include/x86math.h"          // I3DMathEngine::MatrixToQuaternion
#include <cmath>
#include <windows.h>
#include "../include/common_ini.h"              // StringAlloc (EqObj::*_by_name / dump_cargo)
#include "../include/common_server.h"           // Players, Server_BroadcastGroupControl (EqObj::destroy)

// ---------------------------------------------------------------------------
// ServerEquip family -- .text:06ced2d0 .. .text:06cee2e5
// ---------------------------------------------------------------------------

// .text:06ced2d0: vtable .rdata:06d65744; ConnectServerEquip with no null test.
ServerEquip::ServerEquip(CEquip* InEquip, EqObj* InOwner)
    : Equip(InEquip)
    , Owner(InOwner)
{
    Equip->ConnectServerEquip(this);
}

// .text:06ced330 (scalar deleting destructor .text:06ced300; the one every
// subclass table carries is .text:06ce8c20).
ServerEquip::~ServerEquip()
{
    if (Equip != nullptr)
        Equip->NotifyDisconnecting(this);
}

// slot 0 -- .text:06ced360. Only event 0xa (the equip is going away); no null
// test on the held equip.
void ServerEquip::notify(Event Code, void* Param)
{
    (void)Param;
    if (Code != EquipDestroying)
        return;
    Equip->NotifyDisconnecting(this);
    Equip = nullptr;
}

// slot 2 -- .text:06ced350; Dt is not read.
bool ServerEquip::update(float Dt)
{
    (void)Dt;
    return Equip != nullptr;
}

// new(0x0c), vtable .rdata:06d65564.
SrvThruster::SrvThruster(CEquip* InEquip, EqObj* InOwner)
    : ServerEquip(InEquip, InOwner)
{
}

// .text:06ced380.
void SrvThruster::notify(Event Code, void* Param)
{
    (void)Param;
    if (Code == NotEnoughPower)
    {
        // An AI-owned object (OwnerPlayer read through the unguarded narrowing)
        // switches its thrusters off: a tail jump into the owner's IObjAffect
        // slot +0x54 with both arguments rewritten to 0. A player's object
        // ignores the event.
        if (Owner->csimple()->OwnerPlayer == 0)
            Owner->toggle_thrusters(false, 0);
        return;
    }
    if (Code == EquipDestroying)
    {
        // The base body, inlined.
        Equip->NotifyDisconnecting(this);
        Equip = nullptr;
    }
}

// new(0x0c), vtable .rdata:06d65544. The notify body (.text:06d06a40) lives
// with the StarSystem code.
SrvTractor::SrvTractor(CEquip* InEquip, EqObj* InOwner)
    : ServerEquip(InEquip, InOwner)
{
}

// new(0x14), vtable .rdata:06d65554.
SrvRepairDroid::SrvRepairDroid(CEquip* InEquip, EqObj* InOwner)
    : ServerEquip(InEquip, InOwner)
    , RepairTimer(0.0f)
    , TargetId(SUBOBJ_ID_NONE)
{
}

// .text:06ced3f0.
bool SrvRepairDroid::update(float Dt)
{
    CEquip* const Droid = Equip;
    if (Droid == nullptr)
        return false;

    if (!Droid->IsActive())
        return true;

    if (TargetId == SUBOBJ_ID_NONE)
        TargetId = pick_target();
    if (TargetId == SUBOBJ_ID_NONE)
        return true;

    // Only past 2.0f (DAT_06d6573c); the timer keeps counting otherwise.
    RepairTimer = Dt + RepairTimer;
    if (!(RepairTimer > 2.0f))
        return true;

    // A failed lookup, or a target already at 1.0f or above (DAT_06d65520;
    // unordered too), drops the target.
    float Relative = 0.0f;
    if (!Droid->Owner->get_sub_obj_relative_health(TargetId, Relative) || !(Relative < 1.0f))
    {
        TargetId = SUBOBJ_ID_NONE;
        RepairTimer = 0.0f;
        return true;
    }

    // A cause-0x11 list from this droid's owner.
    DamageList Repair;
    Repair.Cause = static_cast<DamageCause>(0x11);
    Repair.InflictorId = Owner->get_id();
    Repair.InflictorOwner = Owner->csimple()->OwnerPlayer;

    // Failing the hit-point lookup, the target is dropped.
    float Current = 0.0f;
    if (Droid->Owner->get_sub_obj_hit_pts(TargetId, Current))
    {
        // The droid pays for the repair out of its own hit points: Amount =
        // repair_rate (RepairDroid+0x70) * timer, floored at 0 (GetHitPoints
        // re-read for the store).
        float const Amount = static_cast<CERepairDroid*>(Droid)->RepairDroidArch()->RepairRate * RepairTimer;
        float DroidHitPts = 0.0f;
        if (Droid->GetHitPoints() - Amount > 0.0f)
            DroidHitPts = Droid->GetHitPoints() - Amount;
        Repair.add_damage_entry(Droid->Id, DroidHitPts, DamageEntry::SubObjFate::None);

        // The target's max: current / relative when the relative health is
        // above 0, else get_sub_obj_max_hit_pts.
        float MaxHitPts = 0.0f;
        if (Relative > 0.0f)
            MaxHitPts = Current / Relative;
        else
            Droid->Owner->get_sub_obj_max_hit_pts(TargetId, MaxHitPts);

        // Capped at the max (>= and unordered take the max).
        double const Repaired = static_cast<double>(Amount) + Current;
        float NewHitPts = MaxHitPts;
        if (Repaired < MaxHitPts)
            NewHitPts = static_cast<float>(Repaired);
        Repair.add_damage_entry(TargetId, NewHitPts, DamageEntry::SubObjFate::None);

        Owner->apply_damage_list(Repair);
    }
    else
    {
        TargetId = SUBOBJ_ID_NONE;
    }

    RepairTimer = 0.0f;
    return true;
}

// FUN_06ced6a0.
unsigned short SrvRepairDroid::pick_target() const
{
    float Lowest = 1.0f;
    unsigned short Best = SUBOBJ_ID_NONE;

    // The equip owner's (CEquip+0x04) attached, non-shield equipment: a
    // relative health strictly below the best so far wins.
    CEqObj* const Ce = Equip->Owner;
    CEquipTraverser Walk;
    CEquipManager const& Equipment = Ce->EquipManager;
    for (CEquip const* Item = Equipment.Traverse(Walk); Item != nullptr; Item = Equipment.Traverse(Walk))
    {
        if (CAttachedEquip::cast(Item) == nullptr)
            continue;
        if (Item->Type == EquipTypeShield)
            continue;
        // Compared unrounded; only the winner is rounded to float.
        double const Relative = Item->GetRelativeHealth();
        if (Relative < Lowest)
        {
            Lowest = (float)Relative;
            Best = Item->Id;
        }
    }

    // Then every arch group the same way.
    CArchGrpTraverser Groups;
    CArchGroupManager const& GroupManager = Ce->ArchGroups;
    for (CArchGroup const* Group = GroupManager.Traverse(Groups); Group != nullptr;
         Group = GroupManager.Traverse(Groups))
    {
        double const Relative = Group->GetRelativeHealth();
        if (Relative < Lowest)
        {
            Lowest = (float)Relative;
            Best = Group->Arch->Id;
        }
    }
    return Best;
}

// The launcher wrappers' shared base. Retail has no constructor of its own:
// the factory stores +0x0c = 0 inline and never writes +0x10 (operator new
// garbage); deviation: the port zeroes both.
SrvLauncher::SrvLauncher(CEquip* InEquip, EqObj* InOwner)
    : ServerEquip(InEquip, InOwner)
    , FireCount(0)
    , LastDamageTime(0.0)
{
}

// slot 4 -- .text:06ced7a0, shared by the mine and countermeasure tables;
// SourceClient is not read. The barrels' average world position plus their
// average direction scaled to 1e10 (DAT_06d65750) over its length (on the x87
// stack), handed to slot 3.
bool SrvLauncher::fire_forward(int SourceClient)
{
    (void)SourceClient;
    CELauncher* const Launcher = static_cast<CELauncher*>(Equip);
    Vector const Position = Launcher->GetAvgBarrelPosWS();
    Vector Direction = Launcher->GetAvgBarrelDirWS();

    double const Scale = 10000000000.0 /
        sqrt((static_cast<double>(Direction.y) * Direction.y + static_cast<double>(Direction.z) * Direction.z) +
             static_cast<double>(Direction.x) * Direction.x);
    Direction.x = static_cast<float>(Direction.x * Scale);
    Direction.y = static_cast<float>(Direction.y * Scale);
    Direction.z = static_cast<float>(Direction.z * Scale);

    Vector const Target = { Position.x + Direction.x, Direction.y + Position.y, Direction.z + Position.z };
    return fire(Target);                                            // vt +0x0c
}

// FUN_06ced840 (the Vector is not read).
bool SrvLauncher::use_ammo(Vector const& Target)
{
    (void)Target;

    if (Equip == nullptr || Equip->IsDestroyed())
        return false;
    if (Owner->is_destroyed())
        return false;

    ++FireCount;
    CELauncher* const Launcher = static_cast<CELauncher*>(Equip);

    // Outside single player the ammunition must be there and is paid for.
    if (!SinglePlayer())
    {
        if (!Launcher->AmmoNeedsMet())
            return false;
        Launcher->ConsumeFireResources();
    }

    // A player's current ship (PlayerDB::is_valid inlined, PlayerData+0x3e4
    // against the id) with an ammunition-using projectile (Projectile+0x78)
    // loses one round of it from the player's cargo (FUN_06d4f260: the
    // projectile's nickname, 1, 1.0f, no notify).
    unsigned int const OwnerPlayer = Owner->csimple()->OwnerPlayer;
    unsigned int const OwnerId = Owner->get_id();
    if (Players.is_valid(OwnerPlayer))
    {
        PlayerData& Player = Players[OwnerPlayer];
        if (Player.ship_object_id == OwnerId)
        {
            Archetype::Projectile const* const Projectile = Launcher->ProjectileArch();
            if (Projectile->bRequiresAmmo)
                Player.remove_cargo(Projectile->Nickname.Id, 1, 1.0f, false);
        }
    }
    return true;
}

// Vtable .rdata:06d65574, new(0x18).
SrvMineDropper::SrvMineDropper(CEquip* InEquip, EqObj* InOwner)
    : SrvLauncher(InEquip, InOwner)
{
}

// slot 3 -- .text:06ced940.
bool SrvMineDropper::fire(Vector const& Target)
{
    if (Equip == nullptr || Equip->IsDestroyed())
        return false;
    if (Owner->is_destroyed())
        return false;
    CELauncher* const Launcher = static_cast<CELauncher*>(Equip);
    // FireOk is 9.
    if (Launcher->Fire(Target) != FireOk)
        return false;
    Launcher->ConsumeFireResources();                              // vt +0x94
    ++FireCount;
    launch(Target);                                                // FUN_06ced9d0
    return true;
}

// slot 5 -- .text:06ced9a0.
bool SrvMineDropper::fire_unchecked(Vector const& Target)
{
    if (!use_ammo(Target))
        return false;
    launch(Target);
    return true;
}

// Fills the per-shot part of a launch record from one ProjLaunchInfo.
static void SrvLauncher_CopyLaunchInfo(FServerLaunchRecord& Record, ProjLaunchInfo const& Info)
{
    Record.Position = Info.Pos;
    Record.Orientation = Info.Orient;
    Record.Velocity = Info.Velocity;
}

// FUN_06ced9d0.
void SrvMineDropper::launch(Vector const& Target)
{
    std::vector<ProjLaunchInfo> Shots;
    static_cast<CELauncher*>(Equip)->ComputeLaunchInfo(Shots, Target);   // vt +0x98
    if (Shots.empty())
        return;

    // Deviation: retail leaves +0x4c/+0x50 (guided-only) as stack garbage; the
    // port value-initialises.
    FServerLaunchRecord Record = {};
    Record.EquipId = SUBOBJ_ID_NONE;
    Record.OwnerPlayer = Owner->csimple()->OwnerPlayer;
    Record.OwnerId = Owner->get_id();
    Record.Arch = static_cast<CEMineDropper*>(Equip)->MineArch();
    Record.EquipId = Equip->Id;

    // StarSystem FUN_06d08ef0 per shot, the system re-read each time.
    for (std::vector<ProjLaunchInfo>::const_iterator It = Shots.begin(); It != Shots.end(); ++It)
    {
        SrvLauncher_CopyLaunchInfo(Record, *It);
        Owner->OwnerSystem->create_mine(Record);
    }
}

// Vtable .rdata:06d65590, new(0x18).
SrvCounterMeasureDropper::SrvCounterMeasureDropper(CEquip* InEquip, EqObj* InOwner)
    : SrvLauncher(InEquip, InOwner)
{
}

// slot 0 -- .text:06cedb30.
void SrvCounterMeasureDropper::notify(Event Code, void* Param)
{
    (void)Param;
    if (Code == IncomingMissileDetected)
    {
        // An AI owner drops a countermeasure through its own IObjAffect slot
        // +0x34: count 1, no target position, client -1.
        if (Owner->csimple()->OwnerPlayer != 0)
            return;
        unsigned short const EquipId = Equip->Id;
        Owner->fire_weapons(&EquipId, 1, nullptr, -1);
        return;
    }
    if (Code == EquipDestroying)
    {
        // The base body, inlined.
        Equip->NotifyDisconnecting(this);
        Equip = nullptr;
    }
}

// slot 3 -- .text:06cedba0.
bool SrvCounterMeasureDropper::fire(Vector const& Target)
{
    if (Equip == nullptr || Equip->IsDestroyed())
        return false;
    if (Owner->is_destroyed())
        return false;
    CELauncher* const Launcher = static_cast<CELauncher*>(Equip);
    if (Launcher->Fire(Target) != FireOk)
        return false;
    Launcher->ConsumeFireResources();
    ++FireCount;
    launch(Target);                                                // FUN_06cedc30
    return true;
}

// slot 5 -- .text:06cedc00.
bool SrvCounterMeasureDropper::fire_unchecked(Vector const& Target)
{
    if (!use_ammo(Target))
        return false;
    launch(Target);
    return true;
}

// FUN_06cedc30. SrvMineDropper::launch with the
// countermeasure archetype (CECounterMeasureDropper::CounterMeasureArch) and
// StarSystem FUN_06d092c0.
void SrvCounterMeasureDropper::launch(Vector const& Target)
{
    std::vector<ProjLaunchInfo> Shots;
    static_cast<CELauncher*>(Equip)->ComputeLaunchInfo(Shots, Target);
    if (Shots.empty())
        return;

    FServerLaunchRecord Record = {};
    Record.EquipId = SUBOBJ_ID_NONE;
    Record.OwnerPlayer = Owner->csimple()->OwnerPlayer;
    Record.OwnerId = Owner->get_id();
    Record.Arch = static_cast<CECounterMeasureDropper*>(Equip)->CounterMeasureArch();
    Record.EquipId = Equip->Id;

    for (std::vector<ProjLaunchInfo>::const_iterator It = Shots.begin(); It != Shots.end(); ++It)
    {
        SrvLauncher_CopyLaunchInfo(Record, *It);
        Owner->OwnerSystem->create_counter_measure(Record);
    }
}

// Vtable .rdata:06d655ac, new(0x18).
SrvGun::SrvGun(CEquip* InEquip, EqObj* InOwner)
    : SrvLauncher(InEquip, InOwner)
{
}

// slot 3 -- .text:06cedd90.
bool SrvGun::fire(Vector const& Target)
{
    if (Equip == nullptr || Equip->IsDestroyed())
        return false;
    if (Owner->is_destroyed())
        return false;
    CELauncher* const Launcher = static_cast<CELauncher*>(Equip);
    if (Launcher->Fire(Target) != FireOk)
        return false;
    Launcher->ConsumeFireResources();
    ++FireCount;

    // A seeking munition (Munition+0x90) goes out as guided missiles; anything
    // else as bullets, and only outside single player.
    if (static_cast<CEGun*>(Equip)->MunitionArch()->Seeker != 0)
    {
        launch_guided(Target);                                      // vt +0x1c
        return true;
    }
    if (!SinglePlayer())
        launch_bullets(Target);                                     // vt +0x18
    return true;
}

// slot 4 -- .text:06cede30.
bool SrvGun::fire_forward(int SourceClient)
{
    // The base aim, but summed (z^2 + y^2) + x^2 here.
    CELauncher* const Launcher = static_cast<CELauncher*>(Equip);
    Vector const Position = Launcher->GetAvgBarrelPosWS();
    Vector Direction = Launcher->GetAvgBarrelDirWS();

    double const Scale = 10000000000.0 /
        sqrt((static_cast<double>(Direction.z) * Direction.z + static_cast<double>(Direction.y) * Direction.y) +
             static_cast<double>(Direction.x) * Direction.x);
    Direction.x = static_cast<float>(Direction.x * Scale);
    Direction.y = static_cast<float>(Direction.y * Scale);
    Direction.z = static_cast<float>(Direction.z * Scale);
    Vector Target = { Position.x + Direction.x, Direction.y + Position.y, Direction.z + Position.z };

    bool const bFired = fire(Target);
    if (!bFired)
        return false;

    StarSystem* const System = Owner->OwnerSystem;
    if (System == nullptr)
        return bFired;

    // Outside single player, a locked target turns the aim point into the
    // target's frame (EngineObject+0x08 orientation, +0x2c position).
    if (!SinglePlayer())
    {
        CSimple* LockTarget = nullptr;
        unsigned short LockSubId = SUBOBJ_ID_NONE;
        static_cast<CEGun*>(Equip)->GetTarget(LockTarget, LockSubId);
        if (LockTarget != nullptr)
        {
            FVector Local;
            Server_MathEngine()->InverseTransform(
                Local, *reinterpret_cast<FTransform const*>(&LockTarget->Orientation),
                reinterpret_cast<FVector const&>(Target));
            Target = reinterpret_cast<Vector const&>(Local);
        }
    }

    std::vector<unsigned short> EquipIds;
    EquipIds.insert(EquipIds.begin(), 1, Equip->Id);
    unsigned int const OwnerId = Owner->get_id();
    System->notify_fire_weapon(OwnerId, EquipIds, Target, SourceClient);   // FUN_06d0e2e0
    return bFired;
}

// slot 5 -- .text:06cee000. No bullets here: only a seeking munition is
// launched after use_ammo.
bool SrvGun::fire_unchecked(Vector const& Target)
{
    if (!use_ammo(Target))
        return false;
    if (static_cast<CEGun*>(Equip)->MunitionArch()->Seeker != 0)
        launch_guided(Target);                                      // vt +0x1c
    return true;
}

// slot 6 -- .text:06cee1f0.
void SrvGun::launch_bullets(Vector const& Target)
{
    std::vector<ProjLaunchInfo> Shots;
    static_cast<CELauncher*>(Equip)->ComputeLaunchInfo(Shots, Target);   // vt +0x98

    // The system is read once here.
    StarSystem* const System = Owner->OwnerSystem;
    FServerBulletRecord Record = {};
    Record.OwnerId = Owner->get_id();
    Record.Arch = static_cast<CEGun*>(Equip)->MunitionArch();
    Record.EquipId = Equip->Id;

    // StarSystem FUN_06d099e0 per shot.
    for (std::vector<ProjLaunchInfo>::const_iterator It = Shots.begin(); It != Shots.end(); ++It)
    {
        Record.Position = It->Pos;
        Record.Orientation = It->Orient;
        Record.Velocity = It->Velocity;
        System->create_bullet(Record);
    }
}

// slot 7 -- .text:06cee040.
void SrvGun::launch_guided(Vector const& Target)
{
    std::vector<ProjLaunchInfo> Shots;
    static_cast<CELauncher*>(Equip)->ComputeLaunchInfo(Shots, Target);   // vt +0x98
    if (Shots.empty())
        return;

    FServerLaunchRecord Record = {};
    Record.EquipId = SUBOBJ_ID_NONE;
    Record.TargetId = 0;
    Record.TargetSubId = SUBOBJ_ID_NONE;
    Record.OwnerPlayer = Owner->csimple()->OwnerPlayer;
    Record.OwnerId = Owner->get_id();
    Record.Arch = static_cast<CEGun*>(Equip)->MunitionArch();
    Record.EquipId = Equip->Id;

    // Per shot: the lock target's id (CSimple+0xb0) or 0, its sub-object, and
    // StarSystem FUN_06d09640 (system re-read each time).
    for (std::vector<ProjLaunchInfo>::const_iterator It = Shots.begin(); It != Shots.end(); ++It)
    {
        SrvLauncher_CopyLaunchInfo(Record, *It);
        Record.TargetId = (It->LockTarget != nullptr) ? It->LockTarget->Id : 0;
        Record.TargetSubId = It->LockSubId;
        Owner->OwnerSystem->create_guided(Record);
    }
}
