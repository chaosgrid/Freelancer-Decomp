// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"   // StarSystem::on_object_destroyed (GameObject::destroy)
#include "../include/common_misc.h"    // SUBOBJ_ID_NONE (Ship::allocate_equip_id)
#include "../include/common_cequip.h"  // CEquipTraverser / CEquip (get_damage_list)
#include "../include/server_createinfo.h" // Server_Acquire3DMathEngine (DAT_06d8d71c)
#include "../include/x86math.h"          // I3DMathEngine::MatrixToQuaternion
#include "../include/common_physys.h"   // PhySys::RCBegin/RCEnd, Collision::CalcCollisionDamage
#include <cmath>
#include <windows.h>
#include "../include/rpc_iclient.h"   // IClient (Ship::request_returned / set_formation_*)
#include "../include/DALib.h"         // DALib::Engine (Ship::create / Solar::create, IEngine +0x6c)
#include "../include/common_ini.h"              // StringAlloc (EqObj::*_by_name / dump_cargo)
#include "../include/common_server.h"           // Players, Server_BroadcastGroupControl (EqObj::destroy)
#include "server_internal.h"

// ---------------------------------------------------------------------------
// GameObject
// ---------------------------------------------------------------------------

// .text:06cee360. The IObjRW / IObjInspectImpl prefix is built by the base
// initialiser; this sets GameObject's own state: MarkedByPlayers (+0x14),
// LastDamageTime (+0x20) = 0, OwnerSystem (+0x28) = 0, Invincibilities (+0x2c),
// +0x38 / +0x39 = 0, +0x3c = 1.0f, bDestroyed (+0x40) / bInDamageCallback
// (+0x41) = 0, PendingDamage (+0x44) with Cause (+0x54) = 0x18.
GameObject::GameObject()
    : LastDamageTime(0.0)
    , bInvincible(0)
    , bInvincibleFlag(0)
    , MaxHpLoss(1.0f)
    , bDestroyed(0)
    , bInDamageCallback(0)
    , OwnerSystem(nullptr)
{
    PendingDamage.Cause = DamageCause::Pending;
}

// FUN_06cee4a0, entered with ECX on the CBase sub-object at +0x0c (the scalar
// deleting destructor FUN_06cee470 is the CBase vtable's slot 0, .rdata:06d65764).
// Retail: advise(false), then CObject::Release on CBase::Object with no null test.
// The reference released is the one the wrapper took when bound: CObject::Alloc
// returns a count of 1 and CObject::Find adds one, so create() and find() both
// leave the wrapper owning exactly one reference that only this destructor gives
// back. The compiler then destroys PendingDamage (+0x44), Invincibilities (+0x2c)
// and MarkedByPlayers (+0x14) and the bases in the same order as retail.
GameObject::~GameObject()
{
    advise(false);
    Object->Release();
}

// +0x158 -- .text:06cee630
bool GameObject::destroy(int Kind, unsigned int ObjId)
{
    // No null test on the guarded CSimple in retail.
    csimple()->set_hit_pts(0.0f);

    // FUN_06d0b160 (StarSystem::on_object_destroyed). Only Kind is passed on;
    // ObjId is unused. OwnerSystem is not null-tested.
    OwnerSystem->on_object_destroyed(this, Kind);
    (void)ObjId;

    destroy_notify();   // own vtable +0x15c
    return true;
}

// +0x15c -- .text:06cee670
void GameObject::destroy_notify()
{
    bDestroyed = 1;

    // In single player an object owned by a player (CSimple+0xb4) stays
    // connected. The guarded CSimple is read without a null test.
    if (SinglePlayer())
    {
        if (csimple()->OwnerPlayer != 0)
            return;
    }

    // CSimple::disconnect (CObject vt +0x94) with OwnerSystem as the IObjDB*;
    // no null test on either.
    csimple()->disconnect(OwnerSystem);
}

// +0x160 -- .text:06cee6d0
bool GameObject::is_destroyed() const
{
    return bDestroyed != 0;
}

// ---------------------------------------------------------------------------
// GameObject's extension slots +0x170 .. +0x1b8 (declaration order == retail
// slot order, see server_gameobject.h).
// ---------------------------------------------------------------------------

// +0x170 -- .text:06cee6e0.
void GameObject::enter_system(StarSystem* Sys, PhySys::CreateParms const* Parms)
{
    OwnerSystem = Sys;

    // Uses the raw CBase Object (no class guard).
    if (Parms != nullptr)
        Object->remake_physical(*Parms, 0.0f);

    // Outside single player a player-owned object is put under remote
    // correction. The owner test reads the guarded CSimple without a null test.
    if (!SinglePlayer() && csimple()->OwnerPlayer != 0)
    {
        Quaternion Orientation;
        Server_MathEngine()->MatrixToQuaternion(Orientation, Object->Orientation);

        // Angular velocity is fetched before velocity, both from the raw Object.
        CSimple* const Simple = csimple();
        Vector const AngularVelocity = Object->get_angular_velocity();
        Vector const Velocity = Object->get_velocity();
        PhySys::RCBegin(Simple, Object->WorldPosition, Velocity, Orientation, AngularVelocity);
    }

    // connect(Sys) unless a player's object in single player. Retail's
    // null-guard arm calls through a null pointer; there is no extra test.
    if (SinglePlayer() && csimple()->OwnerPlayer != 0)
        return;
    csimple()->connect(Sys);
}

// +0x174 -- .text:06cee810.
void GameObject::leave_system(PhySys::CreateParms* OutParms)
{
    // Every physics read below goes through the raw Object (no guard).
    CObject* const Raw = Object;

    // FUN_06d0cfa0: every ship and solar of the system hears that this object
    // is leaving (their +0x178).
    OwnerSystem->notify_object_left(csimple()->Id);

    if (!SinglePlayer() && csimple()->OwnerPlayer != 0)
        PhySys::RCEnd(csimple());

    // The caller's CreateParms, from the live body.
    if (OutParms != nullptr)
    {
        OutParms->Mass = Raw->get_mass();
        OutParms->bHasInertia = 1;
        // BodyPtr (+0x54) picks the IVP moment of inertia or a zero vector.
        Vector Inertia;
        if (Raw->BodyPtr != nullptr)
            Inertia = PhySys::GetMomentOfInertia(Raw);
        else
        {
            Inertia.x = 0.0f;
            Inertia.y = 0.0f;
            Inertia.z = 0.0f;
        }
        OutParms->Inertia = Inertia;
        OutParms->bEnableCollisions = PhySys::GetCollisionState(Raw) ? 1 : 0;
        PhySys::GetDamping(Raw, OutParms->LinearDamping,
                           OutParms->AngularDamping);
        // Unbounded byte copy of the collision group name into +0x1e, terminator
        // included (retail's inlined strcpy).
        char const* Src = PhySys::GetCollisionGroup(Raw);
        char* Dst = OutParms->Name;
        char Ch;
        do
        {
            Ch = *Src++;
            *Dst++ = Ch;
        } while (Ch != 0);
    }

    Raw->unmake_physical();

    // disconnect unless a player's object in single player; OwnerSystem is
    // cleared either way.
    if (!(SinglePlayer() && csimple()->OwnerPlayer != 0))
        csimple()->disconnect(OwnerSystem);
    OwnerSystem = nullptr;
}

// +0x178 -- .text:06cee980.
void GameObject::on_object_left_system(unsigned int ObjId)
{
    // Only a CEqObj (class mask 0x103) does anything.
    CObject* const Obj = Object;
    if (Obj == nullptr)
        return;
    if ((static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        return;
    IBehaviorManager* const Behavior = static_cast<CEqObj*>(Obj)->get_behavior_interface();
    if (Behavior == nullptr)
        return;

    // IBehaviorManager vt slot 0 (Common LAB_062d3f90, the notification
    // switch); event type 3 is "an object left the system".
    FBehaviorLeftSystemEvent Payload;
    Payload.GateId = 0;
    Payload.GateType = 0;
    Payload.ShipId = ObjId;
    FBehaviorManagerEvent Event;
    Event.Type = FBehaviorManagerEvent::kEventObjectLeftSystem;
    Event.Data = &Payload;
    Behavior->OnEvent(&Event);
}

// FUN_06cee9e0. The float argument is ignored.
bool GameObject::update(float Dt)
{
    (void)Dt;
    return !is_destroyed();
}

// +0x1a4 -- .text:06cee9f0.
void GameObject::on_collision(FCollisionEvent const& Hit)
{
    // Nothing for an id-less partner or a player-owned partner (its CSimple
    // read without a null test).
    if (Hit.Other->get_id() == 0)
        return;
    if (Hit.Other->csimple()->OwnerPlayer != 0)
        return;

    // The partner's velocity is fetched first.
    Vector const OtherVelocity = Hit.Other->get_velocity();
    Vector const OwnVelocity = get_velocity();
    float const Rx = OwnVelocity.x - OtherVelocity.x;
    float const Ry = OwnVelocity.y - OtherVelocity.y;
    float const Rz = OwnVelocity.z - OtherVelocity.z;

    float const OwnMass = get_mass();
    float const OtherMass = Hit.Other->get_mass();
    // Squares, sum and FSQRT stay on the x87 stack; one rounding to float.
    float const Speed = static_cast<float>(sqrt((static_cast<double>(Rx) * Rx
                                                 + static_cast<double>(Rz) * Rz)
                                                + static_cast<double>(Ry) * Ry));
    float const Amount = Collision::CalcCollisionDamage(Speed, OtherMass, OwnMass);

    // The record FUN_06d0d730 reads.
    FCollisionDamage Record;
    Record.SourceId = get_id();
    Record.SourceSubObjId = Hit.SubObjId;
    Record.TargetId = Hit.Other->get_id();
    Record.TargetSubObjId = Hit.OtherSubObjId;
    Record.Pad0e = 0;
    Record.Damage = Amount;
    OwnerSystem->collision_damage(Record, Hit.Other);
}

// +0x17c -- .text:06ceeb00.
void GameObject::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    if (Hit.SubObjId == SUBOBJ_ID_ROOT || Hit.SubObjId == SUBOBJ_ID_NONE)
        damage_hull(Hit.Damage, Damage);
}

// +0x180 -- .text:06ceeb40.
void GameObject::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    if (Hit.SubObjId == SUBOBJ_ID_ROOT || Hit.SubObjId == SUBOBJ_ID_NONE)
        damage_hull(Hit.Munition->HullDamage, Damage);     // Munition+0x80
}

// +0x184 -- .text:06ceeb80.
void GameObject::explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage)
{
    // Inner bands are the squares of 2/3 and 1/3 of the radius
    // (DAT_06d655d4 = 4/9, DAT_06d655d0 = 1/9).
    float const Radius = Blast.Explosion->Radius;
    float const Radius2 = Radius * Radius;
    float const Band2 = Radius2 * (4.0f / 9.0f);
    float const Band3 = Radius2 * (1.0f / 9.0f);

    Vector Center;
    float const ObjRadius = Object->get_physical_radius_r(Center);

    // The differences and the result never leave the x87 stack, so they are
    // doubles here; retail's summation order is kept.
    double const Dx = static_cast<double>(Blast.Position.x) - Center.x;
    double const Dy = static_cast<double>(Blast.Position.y) - Center.y;
    double const Dz = static_cast<double>(Blast.Position.z) - Center.z;
    double Dist2 = ((Dz * Dz + Dx * Dx) + Dy * Dy) - static_cast<double>(ObjRadius) * ObjRadius;
    if (!(Dist2 > 0.0))
        Dist2 = 0.0;

    // Each band is a strict "<" (>= and unordered skip).
    int Factor;
    if (Dist2 < Band3)
        Factor = 3;
    else if (Dist2 < Band2)
        Factor = 2;
    else if (Dist2 < Radius2)
        Factor = 1;
    else
        return;

    // 1/3f is DAT_06d655cc; ExplosionResistance is Root+0x28. One rounding to
    // float at the end.
    float const Resistance = Object->get_archetype()->ExplosionResistance;
    float const Amount = static_cast<float>(
        ((static_cast<double>(Factor) * (1.0f / 3.0f)) * Blast.Explosion->HullDamage) * Resistance);
    damage_hull(Amount, Damage);
}

// +0x1b0 -- .text:06ceec90.
bool GameObject::damage_hull(float Amount, DamageList& Damage)
{
    if (Amount == 0.0f)
        return false;

    // What the list already says, else the hull.
    float Current = Damage.get_hit_pts_left(SUBOBJ_ID_ROOT);
    if (Current < 0.0f)
        Current = csimple()->get_hit_pts();

    float NewHitPts = Current;
    if (Amount > 0.0f)
    {
        float const Left = Current - Amount;
        NewHitPts = (Left > 0.0f) ? Left : 0.0f;
    }
    else if (Amount < 0.0f)
    {
        // Retail quirk: a negative amount zeroes the hull.
        NewHitPts = 0.0f;
    }

    // Invincibility floor: the hull may not go below (1 - MaxHpLoss) of
    // its maximum. Already below it -> unchanged; crossing it -> clamped onto it.
    if (is_damage_blocked(Damage))
    {
        // Max and Floor are stored floats; 1/Max stays on the x87 stack for both
        // products, so it is a double here.
        float const MaxHitPts = csimple()->get_max_hit_pts();
        float const Floor = 1.0f - get_max_hp_loss();
        double const InvMax = 1.0 / static_cast<double>(MaxHitPts);
        if (Current * InvMax < Floor)
            NewHitPts = Current;
        else if (NewHitPts * InvMax < Floor)
            NewHitPts = Floor * MaxHitPts;
    }

    if (NewHitPts == Current)
        return false;

    Damage.add_damage_entry(SUBOBJ_ID_ROOT, NewHitPts, DamageEntry::SubObjFate::None);
    if (NewHitPts <= 0.0f)
    {
        Damage.bDestroyed = true;
        return true;
    }
    return false;
}

void GameObject::attach(CObject* Obj)
{
    Object = Obj;
    advise(true);
}

CSimple* GameObject::csimple() const
{
    return static_cast<CSimple*>(cobj());
}

unsigned char GameObject::get_invincible_flag() const
{
    return bInvincibleFlag;
}

unsigned char GameObject::is_invincible() const
{
    return bInvincible;
}

float GameObject::get_max_hp_loss() const
{
    return MaxHpLoss;
}

// ---------------------------------------------------------------------------
// Retail slot +0x1a8: a DamageList of every sub-object whose relative health is
// below StatusCeiling.
//
// FUN_06ceee20 is the base body that ten of the thirteen wrapper classes carry
// unchanged; EqObj, Ship and Solar override it with .text:06ceb210, which calls
// this one first. The base only reports the hull (SUBOBJ_ID_ROOT).
//
// x87 predicate (FCOMP / TEST AH,0x5 / JP): the entry is added only when health
// is strictly below the ceiling; equal and unordered both skip. That is why the
// observer's 2.0f means "everything".
void GameObject::get_damage_list(DamageList& Out, float StatusCeiling) const
{
    CSimple* const Simple = csimple();
    if (Simple == nullptr)
        return;
    if (!(Simple->get_relative_health() < StatusCeiling))
        return;
    Out.add_damage_entry(SUBOBJ_ID_ROOT, Simple->get_hit_pts(),
                         DamageEntry::SubObjFate::None);
}

// +0x188 -- .text:06ceee70.
void GameObject::apply_damage_list(DamageList& Damage)
{
    // Re-entry is reported, not refused.
    if (is_in_damage_callback())
    {
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: ID(0x%X) Arch(%s): Tried to apply damage of type %s within another damage callback.",
              "E:\\FL\\Scratch\\Source\\Server\\space\\Gameobj.cpp", 0x1cf,
              get_id(), Object->get_archetype()->Nickname.get_string(),
              DamageList::DmgCauseToString(Damage.Cause));
    }

    set_in_damage_callback(true);

    // Only a non-empty list: every ROOT entry goes through vt +0x1b4, then the
    // system broadcast (FUN_06d0b800).
    if (Damage.Count != 0)
    {
        for (std::list<DamageEntry>::iterator It = Damage.Entries.begin();
             It != Damage.Entries.end(); ++It)
        {
            if (It->SubObjId == SUBOBJ_ID_ROOT)
                apply_root_damage(*It);
        }
        OwnerSystem->damage_object(this, &Damage);
    }

    if (Damage.bDestroyed && !is_destroyed() && !is_damage_blocked(Damage))
        destroy(DESTROY_FUSE, Damage.InflictorId);

    set_in_damage_callback(false);
}

// +0x1b4 -- .text:06ceef70: tail-jump into CSimple vt +0x98.
void GameObject::apply_root_damage(DamageEntry const& Entry)
{
    csimple()->set_hit_pts(Entry.HitPts);
}

// +0x18c -- .text:06ce6190.
bool GameObject::is_in_damage_callback() const
{
    return bInDamageCallback != 0;
}

// The flush that set_in_damage_callback (+0x190) and set_relative_health (+0x1ac)
// both inline: a fresh DamageList gets a copy of PendingDamage's entries and its
// four scalars; PendingDamage's entries are erased (Count back to 0, bDestroyed
// NOT reset), and the copy goes through apply_damage_list (vt +0x188).
void GameObject::flush_pending_damage(DamageList& Pending)
{
    DamageList Local;
    Local.Entries.insert(Local.Entries.begin(), Pending.Entries.begin(), Pending.Entries.end());
    Local.Count = Pending.Count;
    Local.bDestroyed = Pending.bDestroyed;
    Local.Cause = Pending.Cause;
    Local.InflictorId = Pending.InflictorId;
    Local.InflictorOwner = Pending.InflictorOwner;
    Pending.Entries.clear();
    Pending.Count = 0;
    apply_damage_list(Local);
}

// +0x190 -- .text:06ceefa0.
void GameObject::set_in_damage_callback(bool bIn)
{
    bInDamageCallback = bIn ? 1 : 0;
    // Flush only when clearing, and only with a non-empty PendingDamage.
    if (bIn || PendingDamage.Count == 0)
        return;
    flush_pending_damage(PendingDamage);
}

// +0x1b8 -- .text:06cef0b0.
bool GameObject::is_damage_blocked(DamageList const& Damage) const
{
    // Signed compare of the cause (JGE).
    if (static_cast<int>(Damage.Cause) >= 0x10)
        return false;
    if (Damage.InflictorOwner != 0)
        return get_invincible_flag();
    return is_invincible();
}

// +0x194 -- .text:06cef0f0.
void GameObject::set_invincible(FInvincibility const& Cmd)
{
    // The command's own values start the reduction.
    bInvincible = Cmd.bProtect;
    bInvincibleFlag = Cmd.bFromPlayers;
    MaxHpLoss = Cmd.MaxHpLoss;

    // One pass over every node, no early exit.
    bool bNew = true;
    for (std::list<FInvincibility>::iterator It = Invincibilities.begin();
         It != Invincibilities.end(); ++It)
    {
        if (It->Type == Cmd.Type)
        {
            // Replaced, not folded (its new values already are).
            *It = Cmd;
            bNew = false;
            continue;
        }
        if (bInvincible == 0)
            bInvincible = It->bProtect;
        if (bInvincibleFlag == 0)
            bInvincibleFlag = It->bFromPlayers;
        // Takes the node's value on <= or unordered: the minimum.
        float const Current = MaxHpLoss;
        float const Node = It->MaxHpLoss;
        MaxHpLoss = (Node > Current) ? Current : Node;
    }

    if (bNew)
        Invincibilities.push_back(Cmd);
}

// Retail inlines this guard at every call site: load CBase::Object and null it
// unless (ObjectClass & 3) == 3 (e.g. .text:06cef1c0, .text:06cf9069).
CObject* GameObject::cobj() const
{
    CObject* Obj = Object;
    if (Obj == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
        return nullptr;
    return Obj;
}

// ---------------------------------------------------------------------------
// The four retail slots between IObjInspect's last and create(). See the
// header for the vtable arithmetic that pins them down.
// ---------------------------------------------------------------------------

// +0x154 -- .text:06cef1c0
unsigned int GameObject::get_type_bits() const
{
    // Retail reads through the guarded pointer with no null test or default.
    return csimple()->TypeBits;
}

// +0x1ac -- .text:06cef1e0.
void GameObject::set_relative_health(float Relative)
{
    PendingDamage.add_damage_entry(SUBOBJ_ID_ROOT, csimple()->get_max_hit_pts() * Relative,
                                   DamageEntry::SubObjFate::None);
    // > and unordered skip.
    if (Relative <= 0.0f)
        PendingDamage.bDestroyed = true;
    // Applied at once unless inside a callback.
    if (is_in_damage_callback())
        return;
    flush_pending_damage(PendingDamage);
}

int GameObject::set_formation_leader(IObjRW*) { return -8; }                         // 06cef340

int GameObject::set_formation_offset(Vector const&) { return -8; }                   // 06cef340

int GameObject::add_formation_follower(IObjRW*) { return -8; }                       // 06cef340

int GameObject::remove_formation_follower(IObjRW*) { return -8; }                    // 06cef340

int GameObject::set_follow_leader(IObjRW*) { return -8; }                            // 06cef340

int GameObject::set_follow_offset(Vector const&) { return -8; }                      // 06cef340

int GameObject::add_follow_follower(IObjRW*) { return -8; }                          // 06cef340

int GameObject::remove_follow_follower(IObjRW*) { return -8; }                       // 06cef340

int GameObject::get_formation_follower_count() { return 0; }                         // 06d567c0

int GameObject::toggle_thrusters(bool, int) { return -3; }                                    // 06cef350

int GameObject::slot_6c(int, int) { return -3; }                                     // 06cef350

int GameObject::request_event(int, unsigned const*, unsigned const*, int, RequestCB*) { return -3; }  // 06cef360

int GameObject::jettison_cargo(unsigned short, int, int) { return -3; }                     // 06cef370

int GameObject::tractor_all(unsigned short const*, std::vector<IObjRW*> const&, int) { return -3; }  // 06cef370

int GameObject::tractor_single(unsigned short const*, IObjRW*, int) { return -3; }          // 06cef370

int GameObject::set_target(IObjRW*, unsigned short, int) { return -3; }              // 06cef370

int GameObject::toggle_item(unsigned short const*, bool, int) { return -3; }             // 06cef370

int GameObject::toggle_cruise(bool, bool, int) { return -3; }                              // 06cef370

int GameObject::use_item(unsigned short, unsigned, int) { return -3; }                // 06cef370

int GameObject::request_cancel(int, unsigned const*, int) { return -3; }                    // 06cef370

int GameObject::fire_weapons(unsigned short const*, unsigned, Vector const*, int) { return -3; }  // 06cef380

int GameObject::set_gun_target(unsigned short const*, IObjRW*, unsigned short, int) { return -3; }  // 06cef380

int GameObject::go_tradelane(IObjInspect const*, IObjInspect const*, bool, float) { return -3; }  // 06cef380

int GameObject::stop_tradelane() { return -3; }                                      // 06cef390

// Ship::create's body (.text:06d01040). Only Ship uses it: Solar::create opens
// before it advises and Loot::create has a different order and return value, so
// those two spell their own sequence out below. The import at .rdata:06d6418c
// is Root::create_instance (one argument), not Root::AddRef. No null checks:
// retail dereferences Arch and Object unconditionally.
bool GameObject::CreateOfClass(CObject::Class ObjectClass, Archetype::Root* Arch)
{
    Object = CObject::Alloc(ObjectClass);

    Arch->create_instance(static_cast<IEngineInstance*>(Object));

    // IEngine::UpdateInstance (DALib::Engine vt +0x6c), as in Loot/Mine::create.
    DALib::Engine->UpdateInstance(Object->EngineInstanceId, 0, nullptr);

    advise(true);

    Object->open(Arch);

    return Object != nullptr;
}

// Ship::find at .text:06d010a0 -- the same shape with CObject::Find instead of
// CObject::Alloc, no archetype and no engine touch.
bool GameObject::FindOfClass(CObject::Class ObjectClass, unsigned int const& Id)
{
    Object = CObject::Find(Id, ObjectClass);
    advise(true);
    return Object != nullptr;
}

// ---------------------------------------------------------------------------
// IObjAffect -- GameObject's own table, .rdata:06d6576c (stored at +4 by the ctor).
// Every GameObject entry is a one-instruction error stub (-3, -8, or 0 for
// .text:06d567c0, ICF-folded with pub::Player::SetCostume); eleven of the thirteen
// Server wrappers keep it slot for slot (table dumps in common_iface.h). The real
// bodies are Ship's (.rdata:06d6709c) and EqObj/Solar's (.rdata:06d651c4 /
// .rdata:06d673ec) overrides: EqObj::slot_34/3c/40/44/4c/58 below and the Ship
// items .text:06d01f30..06d02ad0.
// ---------------------------------------------------------------------------
int GameObject::set_throttle(float) { return -3; }                                   // 06cef3a0

int GameObject::set_axis_throttle(Vector const&) { return -3; }                      // 06cef3a0

int GameObject::set_nudge_vec(Vector const&) { return -3; }                          // 06cef3a0

int GameObject::set_strafe_dir(StrafeDir) { return -3; }                             // 06cef3a0

int GameObject::launch_complete(unsigned) { return -3; }                             // 06cef3a0

int GameObject::jump_in_complete(unsigned) { return -3; }                            // 06cef3a0

// FUN_06cef3b0. See the declaration for why the element is a player id.
void GameObject::mark_for_player(unsigned int PlayerId, int MarkType)
{
    // The search runs before MarkType is looked at and stops on the first match.
    std::list<unsigned int>::iterator It = MarkedByPlayers.begin();
    while (It != MarkedByPlayers.end() && *It != PlayerId)
        ++It;

    // Retail compares against the literals 1 and 0; any other MarkType does
    // nothing. Do not turn this into a bool.
    if (MarkType == 1) {
        if (It == MarkedByPlayers.end())
            MarkedByPlayers.push_back(PlayerId);
        return;
    }
    if (MarkType == 0) {
        if (It != MarkedByPlayers.end())
            MarkedByPlayers.erase(It);
    }
}

// FUN_06cef450.
bool GameObject::is_marked_by(unsigned int PlayerId) const
{
    for (std::list<unsigned int>::const_iterator It = MarkedByPlayers.begin();
         It != MarkedByPlayers.end(); ++It) {
        if (*It == PlayerId)
            return true;
    }
    return false;
}
