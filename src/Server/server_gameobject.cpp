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
#include "../include/common_timing.h"  // Timing::read
#include "../include/server_sysdb.h"   // StarSystem::on_object_destroyed (GameObject::destroy)
#include "../include/common_misc.h"    // SUBOBJ_ID_NONE (Ship::allocate_equip_id)
#include "../include/common_cequip.h"  // CEquipTraverser / CEquip (get_damage_list)
#include "../include/dacom.h"          // FDUMP (Ship::allocate_equip_id)
#include "../include/common_goods.h"   // FindHardpoint / JETTISONED_CARGO_VELOCITY / EquipDesc (Ship::tick_jettison)
#include "../include/common_id.h"      // CFLIDMaker (Ship::tick_jettison)
#include "../include/server_createinfo.h" // Server_Acquire3DMathEngine (DAT_06d8d71c)
#include "../include/x86math.h"          // I3DMathEngine::MatrixToQuaternion
#include "../include/common_physys.h"   // PhySys::RCBegin/RCEnd, Collision::CalcCollisionDamage
#include "../include/common_universe.h" // Universe::IZone (CSimple::ZoneAtA4)
#include <cmath>
#include <windows.h>
#include <cstdio>
#include <cstdlib>   // rand (Ship::tick_jettison / compute_jettison_launch)
#include "../include/rpc_iclient.h"   // IClient (Ship::request_returned / set_formation_*)
#include "../include/DALib.h"         // DALib::Engine (Ship::create / Solar::create, IEngine +0x6c)
#include "../include/common_ini.h"              // StringAlloc (EqObj::*_by_name / dump_cargo)
#include "../include/common_server.h"           // Players, Server_BroadcastGroupControl (EqObj::destroy)
#include "../include/common_server_playerdb.h"  // PlayerData_ShouldQueueVisit / PlayerData_QueueVisit
#include "../include/common_server_types.h"     // ServerStats (EqObj::destroy)
#include "../include/rpc_iclient_args.h"        // FServerBurnFuseInfo (EqObj::light_fuse)
#include <map>                                  // ServerFuseDB::Get (EqObj::light_fuse)
#include <cstring>                              // memcpy (EqObj::build_equip_loot)

// DAT_06d8fb14 (IServerImpl.cpp) and FUN_06d43650 (pub::Controller::_SendMessage
// with a zero id; Server_ControllerDispatch, common_server_controller.h) -- the Ship
// IObjAffect bodies use both.
extern IClient* g_pClientInterface;
// .data:06d8f998 (IServerImpl.cpp) -- EqObj::destroy bumps +0x10 (.text:06ce825f).
extern ServerStats GServerStats;

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
    , InvincibleTimer(1.0f)
    , bDestroyed(0)
    , bInDamageCallback(0)
    , OwnerSystem(nullptr)
{
    PendingDamage.Cause = static_cast<DamageCause>(0x18);
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

// FUN_06cee9e0. The float argument is ignored.
bool GameObject::update(float Dt)
{
    (void)Dt;
    return !is_destroyed();
}

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

// Retail inlines this guard at every call site: load CBase::Object and null it
// unless (ObjectClass & 3) == 3 (e.g. .text:06cef1c0, .text:06cf9069).
CObject* GameObject::cobj() const
{
    CObject* Obj = Object;
    if (Obj == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Obj->ObjectClass) & 3u) != 3u)
        return nullptr;
    return Obj;
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

float GameObject::get_invincible_timer() const
{
    return InvincibleTimer;
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
// The four retail slots between IObjInspect's last and create(). See the
// header for the vtable arithmetic that pins them down.
// ---------------------------------------------------------------------------

// +0x154 -- .text:06cef1c0
unsigned int GameObject::get_type_bits() const
{
    // Retail reads through the guarded pointer with no null test or default.
    return csimple()->TypeBits;
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

// DAT_06d8d71c -- GameObj.cpp's own lazily fetched math engine (FUN_06d5ff70).
static I3DMathEngine* GGameObjectMathEngine = nullptr;

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
        if (GGameObjectMathEngine == nullptr)
            GGameObjectMathEngine = Server_Acquire3DMathEngine();
        Quaternion Orientation;
        GGameObjectMathEngine->MatrixToQuaternion(reinterpret_cast<FQuaternion&>(Orientation),
                                                  reinterpret_cast<FMatrix const&>(Object->Orientation));

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
                           *reinterpret_cast<Vector*>(OutParms->AngularDamping));
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
    if ((static_cast<unsigned>(Obj->ObjectClass) & 0x103u) != 0x103u)
        return;
    IBehaviorManager* const Behavior = static_cast<CEqObj*>(Obj)->get_behavior_interface();
    if (Behavior == nullptr)
        return;

    // IBehaviorManager vt slot 0 (Common LAB_062d3f90, the notification
    // switch); event type 3 is "an object left the system".
    unsigned int Payload[3];
    Payload[0] = 0;
    Payload[1] = 0;
    Payload[2] = ObjId;
    FBehaviorManagerEvent Event;
    Event.Type = 3;
    Event.Data = Payload;
    Behavior->OnEvent(&Event);
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

// +0x188 -- .text:06ceee70.
void GameObject::apply_damage_list(DamageList& Damage)
{
    // Re-entry is reported, not refused.
    if (is_in_damage_callback())
    {
        FDUMP(0x100001,
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
        StarSystem_DamageObject(OwnerSystem, this, &Damage);
    }

    if (Damage.bDestroyed && !is_destroyed() && !is_damage_blocked(Damage))
        destroy(1, Damage.InflictorId);

    set_in_damage_callback(false);
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
static void GameObject_FlushPendingDamage(GameObject* Obj, DamageList& Pending)
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
    Obj->apply_damage_list(Local);
}

// +0x190 -- .text:06ceefa0.
void GameObject::set_in_damage_callback(bool bIn)
{
    bInDamageCallback = bIn ? 1 : 0;
    // Flush only when clearing, and only with a non-empty PendingDamage.
    if (bIn || PendingDamage.Count == 0)
        return;
    GameObject_FlushPendingDamage(this, PendingDamage);
}

// +0x194 -- .text:06cef0f0.
void GameObject::set_invincible(FInvincibility const& Cmd)
{
    // The command's own values start the reduction.
    bInvincible = Cmd.bProtect;
    bInvincibleFlag = Cmd.bFromPlayers;
    InvincibleTimer = Cmd.MaxHpLoss;

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
        float const Current = InvincibleTimer;
        float const Node = It->MaxHpLoss;
        InvincibleTimer = (Node > Current) ? Current : Node;
    }

    if (bNew)
        Invincibilities.push_back(Cmd);
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
    GameObject_FlushPendingDamage(this, PendingDamage);
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

    // Invincibility floor: the hull may not go below (1 - InvincibleTimer) of
    // its maximum. Already below it -> unchanged; crossing it -> clamped onto it.
    if (is_damage_blocked(Damage))
    {
        // Max and Floor are stored floats; 1/Max stays on the x87 stack for both
        // products, so it is a double here.
        float const MaxHitPts = csimple()->get_max_hit_pts();
        float const Floor = 1.0f - get_invincible_timer();
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

// +0x1b4 -- .text:06ceef70: tail-jump into CSimple vt +0x98.
void GameObject::apply_root_damage(DamageEntry const& Entry)
{
    csimple()->set_hit_pts(Entry.HitPts);
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
    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Object->EngineInstanceId), 0, nullptr);

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
int GameObject::set_formation_leader(IObjRW*) { return -8; }                         // 06cef340
int GameObject::set_formation_offset(Vector const&) { return -8; }                   // 06cef340
int GameObject::add_formation_follower(IObjRW*) { return -8; }                       // 06cef340
int GameObject::remove_formation_follower(IObjRW*) { return -8; }                    // 06cef340
int GameObject::get_formation_follower_count() { return 0; }                         // 06d567c0
int GameObject::set_follow_leader(IObjRW*) { return -8; }                            // 06cef340
int GameObject::set_follow_offset(Vector const&) { return -8; }                      // 06cef340
int GameObject::add_follow_follower(IObjRW*) { return -8; }                          // 06cef340
int GameObject::remove_follow_follower(IObjRW*) { return -8; }                       // 06cef340
int GameObject::fire_weapons(unsigned short const*, unsigned, Vector const*, int) { return -3; }  // 06cef380
int GameObject::jettison_cargo(unsigned short, int, int) { return -3; }                     // 06cef370
int GameObject::tractor_all(unsigned short const*, std::vector<IObjRW*> const&, int) { return -3; }  // 06cef370
int GameObject::tractor_single(unsigned short const*, IObjRW*, int) { return -3; }          // 06cef370
int GameObject::set_gun_target(unsigned short const*, IObjRW*, unsigned short, int) { return -3; }  // 06cef380
int GameObject::set_target(IObjRW*, unsigned short, int) { return -3; }              // 06cef370
int GameObject::toggle_item(unsigned short const*, bool, int) { return -3; }             // 06cef370
int GameObject::toggle_cruise(bool, bool, int) { return -3; }                              // 06cef370
int GameObject::toggle_thrusters(bool, int) { return -3; }                                    // 06cef350
int GameObject::use_item(unsigned short, unsigned, int) { return -3; }                // 06cef370
int GameObject::request_event(int, unsigned const*, unsigned const*, int, RequestCB*) { return -3; }  // 06cef360
int GameObject::request_cancel(int, unsigned const*, int) { return -3; }                    // 06cef370
int GameObject::go_tradelane(IObjInspect const*, IObjInspect const*, bool, float) { return -3; }  // 06cef380
int GameObject::stop_tradelane() { return -3; }                                      // 06cef390
int GameObject::slot_6c(int, int) { return -3; }                                     // 06cef350
int GameObject::slot_70(unsigned) { return -3; }                                     // 06cef3a0
int GameObject::slot_74(unsigned) { return -3; }                                     // 06cef3a0

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

CEqObj* EqObj::ceqobj() const
{
    CObject* Obj = cobj();
    if (Obj == nullptr)
        return nullptr;
    // Retail's own narrowing test on this family is the ObjectClass mask, not
    // RTTI: Ship::create checks (ObjectClass & 0x503) == 0x503, Loot::create
    // (ObjectClass & 0x803) == 0x803. Everything CEqObj-derived carries 0x103.
    if ((static_cast<unsigned>(Obj->ObjectClass) & 0x103u) != 0x103u)
        return nullptr;
    return static_cast<CEqObj*>(Obj);
}

// FUN_06ce83c0 (only caller EqObj::destroy): CSimple+0xb4 through the
// (ObjectClass & 3) guard; retail dereferences the null the guard produces.
static unsigned int EqObj_OwnerPlayer(EqObj const* Obj)
{
    return Obj->csimple()->OwnerPlayer;
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
        Repair.Cause = static_cast<DamageCause>(0x1a);

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
            if (Kind == 1)
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
                            Event.Type = 7;
                            Event.Data = &SelfId;
                            Behavior->OnEvent(&Event);
                        }

                        // Once per DockQueue entry.
                        for (std::list<FDockRequest>::iterator It = DockQueue.begin();
                             It != DockQueue.end(); ++It)
                            OwnerSystem->remove_from_list_d4(this);

                        ceqobj()->disconnect(static_cast<IObjRW*>(this));
                        // Not destroyed yet: the fuse will do it.
                        return false;
                    }
                }
            }
        }
        else if (!SinglePlayer() && Kind == 1 && is_player())
        {
            // A player's ship died in multiplayer: control message 0x14 with the
            // player's id to every active player (FUN_06d19110), counted in
            // ServerStats+0x10.
            Server_BroadcastGroupControl(0x14, EqObj_OwnerPlayer(this));
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
    if (Kind == 1)
        collect_droppable_equip(Ev.DroppedEquip);                  // vt +0x1f4
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDestroyed, &Ev);

    OwnerSystem->on_object_destroyed(this, Kind);                  // FUN_06d0b160
    destroy_notify();                                              // vt +0x15c

    if (Kind == 1)
    {
        explode();                                                 // vt +0x1f0
        if (SinglePlayer() || csimple()->OwnerPlayer == 0)
            drop_loot(Ev.DroppedEquip);                            // vt +0x1f8
    }
    return true;
}

// +0x15c -- .text:06ce7c80.
void EqObj::destroy_notify()
{
    // StarSystem FUN_06d0c730(this), once per DockQueue entry.
    for (std::list<FDockRequest>::iterator It = DockQueue.begin(); It != DockQueue.end(); ++It)
        OwnerSystem->remove_from_list_d4(this);

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
            FDUMP(0x100002, "%s(%d) : *** WARNING: Fuse(%s) not found in FuseDB. Aborting.",
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
        FDUMP(0x100001,
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

        StarSystem_DamageObject(OwnerSystem, this, &Damage);      // FUN_06d0b800
    }

    // A burning death fuse holds off causes below 0x10.
    if (Damage.bDestroyed &&
        !is_damage_blocked(Damage) &&                              // vt +0x1b8
        !(is_death_fuse_burning() && static_cast<int>(Damage.Cause) < 0x10) &&
        !is_destroyed() &&                                         // vt +0x160
        !ceqobj()->bWeaponsOffline)
    {
        destroy(1, Damage.InflictorId);                            // vt +0x158
    }

    set_in_damage_callback(false);                                 // vt +0x190
}

// The light/unlight pass apply_root_entry and apply_group_entry share (both
// inline it): damage (hit points dropping from Before to After) lights every
// fuse whose threshold Value2 lies in [After, Before); healing unlights every
// fuse whose threshold lies in [Before, After). Before is re-read for every
// node (light_fuse / unlight_fuse may change it).
template <typename FReadHitPts>
static void EqObj_SweepFuses(EqObj* Obj, std::vector<Archetype::FGroupFuse> const& List,
                             FReadHitPts ReadHitPts, float After, unsigned short SubObjId,
                             unsigned int InflictorId)
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
            Obj->light_fuse(InflictorId, It->Fuse, SubObjId, It->Value1, -1.0f);
        }
        else
        {
            if (!(It->Value2 < After))
                continue;
            // Skips "greater" and unordered alike.
            if (!(ReadHitPts() <= It->Value2))
                continue;
            Obj->unlight_fuse(It->Fuse, SubObjId, It->Value1);
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
    Event.Type = 0;
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
    Ev.Cause = static_cast<DamageCause>(0);
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
        EqObj_SweepFuses(this, *ceqobj()->eqobjarch()->Fuses,
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
    Ev.Cause = static_cast<DamageCause>(0);
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
    if (static_cast<int>(Damage.Cause) == 0x13)
    {
        static_cast<CEShield*>(Equip)->ShieldBatteryBoost(Entry.HitPts);
        return;
    }

    // A cargo stack (type 0x10000, Count +0x28 > 1 unsigned) loses one unit
    // when its entry reaches 0, otherwise takes the hit points.
    if (Equip->Type == 0x10000 && static_cast<CECargo*>(Equip)->Count > 1)
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
    Ev.Cause = static_cast<DamageCause>(0);
    Ev.Damage = 0.0f;
    Ev.RelativeDamage = 0.0f;
    Ev.RelativeHealth = 0.0f;
    Ev.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Ev.InflictorId = Damage.InflictorId;
    Ev.ObjId = get_id();
    Ev.EquipType = 0x40000;
    Ev.Cause = Damage.Cause;
    Ev.Damage = Ce->Power - Entry.HitPts;
    Ev.RelativeDamage = Ev.Damage / Ce->MaxPower;
    Ev.RelativeHealth = Entry.HitPts / Ce->MaxPower;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Ev);

    EqObj_NotifyBehaviorOfDamage(Ce->get_behavior_interface(), Ev, Ev.EquipType, 0);

    Ce->Power = Entry.HitPts;
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
        if (static_cast<int>(Entry.Fate) == 4)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
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
    Ev.Cause = static_cast<DamageCause>(0);
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
        EqObj_SweepFuses(this, *Group->FuseIgnitions().Items,
                         [Group]() { return Group->GetHitPoints(); },
                         Entry.HitPts, Group->Arch->Id, Damage.InflictorId);
    }

    // A burning death fuse keeps low-cause groups attached: the entry's fate
    // is cleared.
    if (is_death_fuse_burning() && static_cast<int>(Damage.Cause) < 0x10)
        Entry.Fate = DamageEntry::SubObjFate::None;

    Group->SetHitPoints(Entry.HitPts);
}

// ---------------------------------------------------------------------------
// EqObj helpers shared by the damage / loot slots below.
// ---------------------------------------------------------------------------

// The "is this object a connected player's CURRENT ship" test retail inlines at
// every difficulty-scale site (damage_hull / _equip / _shield / _group /
// _power): get_id() first, then CSimple+0xb4 through the (ObjectClass & 3) guard
// (its null dereferenced, as in retail), PlayerDB::is_valid inlined, then
// PlayerData+0x3e4 against the id. Null when any of them fails.
static PlayerData* EqObj_PlayerOfShip(EqObj const* Obj)
{
    unsigned int const Id = Obj->get_id();
    unsigned int const Owner = Obj->csimple()->OwnerPlayer;
    if (!Players.is_valid(Owner))
        return nullptr;
    PlayerData& Data = Players[Owner];
    if (Data.ship_object_id != Id)
        return nullptr;
    return &Data;
}

// Retail's inlined CSolar narrowing ((ObjectClass & 0x303) == 0x303) at the
// loot-visit sites. Null for everything that is not a solar.
static CSolar* EqObj_Solar(EqObj const* Obj)
{
    CObject* const Raw = Obj->cobj();
    if (Raw == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Raw->ObjectClass) & 0x303u) != 0x303u)
        return nullptr;
    return static_cast<CSolar*>(Raw);
}

// DAT_06d8d6d4 -- EqObj.cpp's own lazily fetched math engine (FUN_06d5ff70).
static I3DMathEngine* GEqObjMathEngine = nullptr;

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
        if (Gun->MunitionArch()->Seeker != 0)
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
            FTransform const TargetXform = *reinterpret_cast<FTransform const*>(&LockTarget->Orientation);
            if (GEqObjMathEngine == nullptr)
                GEqObjMathEngine = Server_Acquire3DMathEngine();
            FVector Local;
            GEqObjMathEngine->InverseTransform(Local, TargetXform,
                                               *reinterpret_cast<FVector const*>(TargetPos));
            Aim = reinterpret_cast<Vector const&>(Local);
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
    if ((static_cast<unsigned>(Raw->ObjectClass) & 0x803u) != 0x803u)
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
                    if (Raw != nullptr && (static_cast<unsigned>(Raw->ObjectClass) & 3u) == 3u)
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
            if (!bActivate && Found->Type == 0x20000u)
            {
                CObject* const Raw = cobj();
                CShip* Ship = nullptr;
                if (Raw != nullptr && (static_cast<unsigned>(Raw->ObjectClass) & 0x503u) == 0x503u)
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
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", "E:\\FL\\Scratch\\Source\\Server\\Space\\EqObj.cpp",
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
    Unused.Cause = static_cast<DamageCause>(0x12);
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
    Damage.Cause = static_cast<DamageCause>(0x12);
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
    CEquipTraverser Walk(0x100);
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
    Damage.Cause = static_cast<DamageCause>(0x13);
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

    OwnerSystem->add_to_list_d4(this);   // FUN_06d0c6d0
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

        OwnerSystem->remove_from_list_d4(this);   // FUN_06d0c730
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
        GameObject* const ShipObj = OwnerSystem->find_object(It->ShipId, 0x7f0000u);   // FUN_06d0c8d0
        if (ShipObj == nullptr)
        {
            // The ship is gone.
            It = DockQueue.erase(It);
            OwnerSystem->remove_from_list_d4(this);
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

        float* const ShipXform =
            DALib::Engine->GetInstanceTransform(reinterpret_cast<void*>(ShipObj->get_index()));

        Transform Mounted;
        if (GEqObjMathEngine == nullptr)
            GEqObjMathEngine = Server_Acquire3DMathEngine();
        GEqObjMathEngine->MulTransform(reinterpret_cast<FTransform&>(Mounted),
                                       *reinterpret_cast<FTransform const*>(ShipXform),
                                       reinterpret_cast<FTransform const&>(HpXform));
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
        if (Bits == (Bits & 0x440u))
        {
            OwnerSystem->complete_dock(ShipObj, this);   // FUN_06d0c970
        }
        else if ((Bits & 0x800u) != 0)
        {
            OwnerSystem->complete_dock(ShipObj, this);
        }
        else
        {
            // Mooring points 3/4/5 are not a landing (FUN_06d0d370). The CEqObj
            // is re-narrowed from the raw Object here.
            bool bLand = true;
            if (Sphere.Type == 3 || Sphere.Type == 4 || Sphere.Type == 5)
                bLand = false;
            CEqObj* const Bay = ceqobj();
            OwnerSystem->queue_dock(ShipObj, this, &Bay->DockingPointBaseId, &bLand);
        }

        It = DockQueue.erase(It);
        OwnerSystem->remove_from_list_d4(this);
    }
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
    Event.Cause = static_cast<DamageCause>(4);
    Event.Explosion = nullptr;
    Event.SkipCollisionGroup = nullptr;
    Event.ExplodingObjId = get_id();
    Event.InflictorId = get_id();
    Event.Cause = static_cast<DamageCause>(4);
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
        if (Equip->Type == 0x800)                                  // cargo pods stay
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
    case 0x80:       // new(0x18), vt .rdata:06d655ac
        ServerEquips.push_back(new SrvGun(Equip, this));
        break;
    case 0x40:       // new(0x18), vt .rdata:06d65590
        ServerEquips.push_back(new SrvCounterMeasureDropper(Equip, this));
        break;
    case 0x20:       // new(0x18), vt .rdata:06d65574
        ServerEquips.push_back(new SrvMineDropper(Equip, this));
        break;
    case 0x400:      // new(0x0c), vt .rdata:06d65564
        ServerEquips.push_back(new SrvThruster(Equip, this));
        break;
    case 0x200000:   // new(0x14)
        ServerEquips.push_back(new SrvRepairDroid(Equip, this));
        break;
    case 0x100000:   // new(0x0c), vt .rdata:06d65544
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
    CEquipTraverser Walk(0x10000);
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
        CSolar* const Solar = EqObj_Solar(this);
        if (Solar != nullptr && Players.is_valid(Inflictor))
        {
            if (!PlayerData_ShouldQueueVisit(Players[Inflictor], Solar->Id, Solar->GetVisitValue()))
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
        CSolar* const SolarAgain = EqObj_Solar(this);
        unsigned int const InflictorAgain = Out.InflictorOwner;
        if (SolarAgain != nullptr && Players.is_valid(InflictorAgain))
            PlayerData_QueueVisit(Players[InflictorAgain], SolarAgain->Id, SolarAgain->GetVisitValue());
    }
    return bDumped;
}

// +0x1d8 -- .text:06ce91e0.
bool EqObj::drain_shields(DamageList& Out)
{
    CEquipTraverser Walk(0x100);
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

    if (SubObjectID::IsEquipID(Hit.SubObjId))
    {
        CEquip* const Equip = ceqobj()->EquipManager.FindByID(Hit.SubObjId);
        if (Equip == nullptr)
            return;
        if (Equip->Type == 0x100)
            damage_shield(static_cast<CEShield*>(Equip), Hit.Damage, Damage);   // vt +0x214
        else
            damage_equip(Equip, Hit.Damage, Damage);               // vt +0x210
    }
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
        Event.Type = 1;
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
        if (Equip->Type == 0x100)
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
    CEquipTraverser Walk(0x1ee0);
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

    CEquipTraverser Walk(0x100);
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
                destroy_equip(Cargo, static_cast<DamageEntry::SubObjFate>(4), Out);   // vt +0x224
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
            CSolar* const Solar = EqObj_Solar(this);
            if (Solar != nullptr && Players.is_valid(Inflictor))
            {
                if (!PlayerData_ShouldQueueVisit(Players[Inflictor], Solar->Id, Solar->GetVisitValue()))
                    bRefused = true;
            }

            if (!bRefused)
            {
                // Fate 4 asks the controller (event 0x13) and takes the desc it
                // hands back; fate 5 reads it.
                EquipDesc Desc;
                if (static_cast<int>(Fate) == 4)
                {
                    FEquipLootEvent Event;
                    Event.SystemId = 0;
                    Event.InflictorId = 0;
                    Event.ObjId = 0;
                    Event.Cause = static_cast<DamageCause>(0);
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
                        int const Mode = (static_cast<int>(Out.Cause) == 0x16) ? 1 : 2;
                        OwnerSystem->create_loot(get_id(), Mode, &Record, 1);

                        // The visit recorded (FUN_06d4b270), owner re-read.
                        CSolar* const SolarAgain = EqObj_Solar(this);
                        unsigned int const InflictorAgain = Out.InflictorOwner;
                        if (SolarAgain != nullptr && Players.is_valid(InflictorAgain))
                            PlayerData_QueueVisit(Players[InflictorAgain], SolarAgain->Id,
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
    if (Equip->Type == 0x800)
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
            CSolar* const Solar = EqObj_Solar(this);
            if (Solar != nullptr && Players.is_valid(Inflictor))
            {
                if (!PlayerData_ShouldQueueVisit(Players[Inflictor], Solar->Id, Solar->GetVisitValue()))
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
                if (static_cast<int>(Fate) == 4)
                {
                    FEquipLootEvent Event;
                    Event.SystemId = 0;
                    Event.InflictorId = 0;
                    Event.ObjId = 0;
                    Event.Cause = static_cast<DamageCause>(0);
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
                            int const Mode = (static_cast<int>(Out.Cause) == 0x16) ? 1 : 2;
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
                    CSolar* const SolarAgain = EqObj_Solar(this);
                    unsigned int const InflictorAgain = Out.InflictorOwner;
                    if (SolarAgain != nullptr && Players.is_valid(InflictorAgain))
                        PlayerData_QueueVisit(Players[InflictorAgain], SolarAgain->Id,
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
        PlayerData* const Player = EqObj_PlayerOfShip(this);
        if (Player != nullptr)
            Amount = Player->difficulty_scale * Amount;

        // Armour (type 0x1000000) divides by its hit_pts_scale (Armor+0x70).
        CEquip* const Armor = ceqobj()->EquipManager.FindFirst(0x1000000u);
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
            PlayerData* const Player = EqObj_PlayerOfShip(this);
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
        float const Floor = 1.0f - get_invincible_timer();
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
        PlayerData* const Player = EqObj_PlayerOfShip(this);
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
    // F = 1 - timer is either held at the current value (when that one is below
    // F too) or raised to Max * F (GetMaxHitPoints re-read). Both ratios test
    // Max for zero here.
    if (is_damage_blocked(Out))
    {
        float const Timer = get_invincible_timer();
        if (!(Timer > 0.0f))
        {
            NewHitPts = Current;
        }
        else
        {
            float const Max = Shield->GetMaxHitPoints();
            double const RelNew = (Max != 0.0f) ? static_cast<double>(NewHitPts) / Max : 0.0;  // kept on the x87 stack
            float const RelCurrent = (Max != 0.0f) ? Current / Max : 0.0f;  // stored as float
            float const Floor = 1.0f - Timer;
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

// +0x218 -- .text:06ceaa80.
bool EqObj::damage_group(CArchGroup* Group, float Damage, DamageList& Out)
{
    if (Damage == 0.0f)
        return false;
    // Killed (2) or destroyed (3) groups are done.
    if (Group->State == 2 || Group->State == 3)
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
        CEquip* const Armor = ceqobj()->EquipManager.FindFirst(0x1000000u);
        if (Armor != nullptr)
            Scaled = (double)Damage / ((CEArmor*)Armor)->ArmorArch()->HitPtsScale;

        // An OWNED object's group then divides by
        // PLAYER_COLLISION_GROUP_HIT_PTS_SCALE (the only rounding to float) and,
        // on the owner's current ship, takes the difficulty scale. An unowned
        // object subtracts the unrounded quotient directly.
        double Applied = Scaled;
        if (csimple()->OwnerPlayer != 0)
        {
            float const OwnedScaled = (float)(Scaled / PLAYER_COLLISION_GROUP_HIT_PTS_SCALE);
            Applied = OwnedScaled;
            PlayerData* const Player = EqObj_PlayerOfShip(this);
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
        float const Floor = 1.0f - get_invincible_timer();
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
        PlayerData* const Player = EqObj_PlayerOfShip(this);
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
        float const Floor = 1.0f - get_invincible_timer();
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

// +0x220 -- .text:06cea9f0.
void EqObj::destroy_group(CArchGroup* Group, DamageEntry::SubObjFate Fate, DamageList& Out,
                          bool bWithChildren)
{
    if (Group->State == 2 || Group->State == 3)
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

// +0x22c -- .text:06ceadc0.
void EqObj::damage_linked_equipment(CArchGroup* Group, DamageList& Out)
{
    unsigned int const LinkedType = Group->LinkedEquipmentType();
    if (LinkedType == 0)
        return;

    if (LinkedType == 0x100)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
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
    CEquipTraverser Walk(0x1fe3);
    CEquipManager const& Equipment = Ce->EquipManager;
    for (CEquip const* Equip = Equipment.Traverse(Walk); Equip != nullptr; Equip = Equipment.Traverse(Walk))
    {
        // vt +0x64: CExternalEquip::GetParentConnector(false).
        long const Parent = static_cast<CExternalEquip const*>(Equip)->GetParentConnector(false);
        if (!Group->IsInstInGroup(Parent))
            continue;
        if (Equip->Type == 1 || Equip->Type == 2)
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
            if (Equip->Type == 0x10000)
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

    if (Equip->Type == 0x800)
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
    float const* const Position =
        DALib::Engine->GetInstancePosition(reinterpret_cast<void*>(Instance));
    Out.Position.x = Position[0];
    Out.Position.y = Position[1];
    Out.Position.z = Position[2];
    float const* const Orientation =
        DALib::Engine->GetInstanceOrientation(reinterpret_cast<void*>(Instance));
    memcpy(&Out.Orientation, Orientation, sizeof(Out.Orientation));

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

// +0x24c -- .text:06cec910.
void EqObj::decay_cargo(float Elapsed)
{
    DamageList Decay;
    Decay.Cause = static_cast<DamageCause>(0x14);

    // Not a solar (0x303), and the equipment manager has decaying cargo
    // (CEquipManager+0x1c > 0, signed). The CEqObj null is dereferenced as in
    // retail.
    CEqObj* const Ce = ceqobj();
    bool bDecay = true;
    if (Ce != nullptr && (static_cast<unsigned>(Ce->ObjectClass) & 0x303u) == 0x303u)
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
    CEquipTraverser Walk(0x10000);
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

// ---------------------------------------------------------------------------
// ServerEquip family -- .text:06ced2d0 .. .text:06cee2e5
// ---------------------------------------------------------------------------

// DAT_06d8d6f8 -- SrvGun::fire_forward's own lazily fetched math engine.
static I3DMathEngine* GSrvGunMathEngine = nullptr;

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
        if (Item->Type == 0x100)
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
                PlayerData_RemoveCargo(Player, Projectile->Nickname.Id, 1, 1.0f, false);
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
    if (Code == Event_0x19)
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
            if (GSrvGunMathEngine == nullptr)
                GSrvGunMathEngine = Server_Acquire3DMathEngine();
            FVector Local;
            GSrvGunMathEngine->InverseTransform(
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

// ---------------------------------------------------------------------------
// Ship -- .text:06d00e80 / create .text:06d01040
// ---------------------------------------------------------------------------

// .text:06d00e80. The Jettison EquipDescList (+0xA0..+0xA8) is built before
// this body and the EquipIdMaker at +0xC4 last. See server_gameobject.h for the
// layout.
Ship::Ship()
{
    DeathSequenceObjId = 0;
    DeathSequenceTimer = 0.0f;
    bDockInUse = 0;
    PendingRequest = nullptr;
    JettisonTimer = 0.0f;
    PendingBayCloses = 0;
    // Deviation: retail's constructor never writes +0xB4; zeroed here anyway.
    EquipStateData = nullptr;
    bLaunchInvincible = 0;
    AccumulatedDamage = 0.0f;
    AccumulatedDamageTime = 0.0f;
}

// .text:06d00f70. When reading that listing: it is reached through the CBase
// vtable at Ship+0x0C, so its ECX is `object + 0x0C` and every offset in it is
// 0x0C LOW ([ESI+0x98] / [ESI+0x9c] are Ship+0xA4 / +0xA8, the jettison list's
// sentinel and size -- not bDockInUse and PendingRequest).
Ship::~Ship()
{
    // FUN_06d00180: drop this ship from the dying-ship list.
    Ship_ForgetDying(this);

    // Through the just re-stamped Ship vtable, i.e. this class's own.
    destroy_notify();

    // The Jettison member's destructor (the inlined EquipDescList one) runs
    // after this body and before ~EqObj.
}

// ---------------------------------------------------------------------------
// The dying-ship list (server_gameobject.h). FUN_06d00090 / FUN_06d000d0 are
// this definition's construction and atexit destruction.
// ---------------------------------------------------------------------------
std::list<Ship*> GDyingShips;

// FUN_06d00130 (size at DAT_06d8d8e8).
void Ship_QueueDying(Ship* DyingShip)
{
    GDyingShips.push_back(DyingShip);
}

// FUN_06d00180: every matching node is removed -- the walk does not stop at
// the first match.
void Ship_ForgetDying(Ship* DyingShip)
{
    std::list<Ship*>::iterator It = GDyingShips.begin();
    while (It != GDyingShips.end()) {
        if (*It == DyingShip)
            It = GDyingShips.erase(It);
        else
            ++It;
    }
}

// .text:06d01040.
bool Ship::create(Archetype::Root* Arch)
{
    return CreateOfClass(CObject::ClassShip, Arch);
}

// .text:06d02ae0.
int Ship::get_data(void const*& OutData) const
{
    OutData = EquipStateData;
    return 0;
}

// .text:06d010a0.
bool Ship::find(unsigned int const& Id)
{
    return FindOfClass(CObject::ClassShip, Id);
}

// .text:06d01c30.
unsigned short Ship::allocate_equip_id()
{
    // No null test on the guarded CSimple in retail.
    if (csimple()->OwnerPlayer != 0)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\ship.cpp", 0x1e5,
              "Ship::allocate_equip_id (): Player tried to allocate id!");
        return SUBOBJ_ID_NONE;
    }
    // A tail jump into EquipIdMaker::CreateEquipID on this + 0xc4.
    return EquipIds.CreateEquipID();
}

CShip* Ship::cship() const
{
    CObject* Obj = cobj();
    if (Obj == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Obj->ObjectClass) & 0x503u) != 0x503u)
        return nullptr;
    return static_cast<CShip*>(Obj);
}

// FUN_06d010e0. check_fuses is the EqObj body FUN_06ce7d00, called directly.
void Ship::set_equip_state(FEquipState const& State)
{
    EqObj::check_fuses();
    EquipIds = State.EquipIds;
    EquipStateData = State.Data;
}

// FUN_06d01110. Latched once by +0xb8.
void Ship::apply_launch_invincibility()
{
    if (bLaunchInvincible != 0)
        return;
    bLaunchInvincible = 1;
    FInvincibility Cmd = {};
    Cmd.Type = 1u;
    Cmd.bProtect = true;
    Cmd.bFromPlayers = true;
    Cmd.MaxHpLoss = 0.0f;
    set_invincible(Cmd);
}

// FUN_06d01150. Uses the raw CBase::Object with no class or null test.
void Ship::tick_zone_damage(float Dt)
{
    CObject* const Raw = Object;
    CSimple* const Simple = static_cast<CSimple*>(Raw);

    if (is_invincible())
        return;

    // The zone CSimple::update_zones cached (+0xa4); GetDamage is Common
    // .text:0633ad80, the zone's damage rate (+0x60).
    Universe::IZone* const Zone = Simple->ZoneAtA4;
    if (Zone != nullptr)
    {
        float const Rate = Zone->GetDamage();
        if (Rate > 0.0f)
        {
            AccumulatedDamage = Rate * Simple->ScannerInterference * Dt + AccumulatedDamage;
        }
    }

    AccumulatedDamageTime = Dt + AccumulatedDamageTime;

    if (AccumulatedDamage == 0.0f)
        return;
    if (AccumulatedDamageTime <= 1.0f)                 // .rdata:06d67088
        return;

    // CEqObj::get_total_max_hit_pts (Common CShip vt +0xd4) on the raw object.
    float const Ratio = AccumulatedDamage / static_cast<CEqObj*>(Raw)->get_total_max_hit_pts();
    if (Ratio <= 0.01f)                                // .rdata:06d6708c
        return;

    // Retail builds the list inline and stores the cause directly. Cause 8 has
    // no name in DamageCause (common_misc.h names 0..5 only).
    DamageList Damage;
    Damage.Cause = static_cast<DamageCause>(8);

    apply_damage_ratio(Ratio, Damage);             // FUN_06d012e0
    apply_damage_list(Damage);                     // vt +0x188
    AccumulatedDamage = 0.0f;
}

// FUN_06d012e0. Mostly through the raw CBase::Object.
bool Ship::apply_damage_ratio(float Ratio, DamageList& Damage)
{
    CObject* const Raw = Object;

    if (Ratio > 1.0f)                              // .rdata:06d65520
    {
        // Through ceqobj() (FUN_06ce6870); damage_hull's result is ignored.
        damage_hull(ceqobj()->get_hit_pts(), Damage);
        Damage.bDestroyed = true;
        return true;
    }

    bool bAnyDestroyed = false;

    if (damage_hull(static_cast<CSimple*>(Raw)->get_max_hit_pts() * Ratio, Damage))
        bAnyDestroyed = true;

    // Equipment pass only while the list does not say destroyed.
    if (!Damage.bDestroyed)
    {
        CEquipTraverser Walk(0x1ee0);
        CEquipManager& Equipment = static_cast<CEqObj*>(Raw)->EquipManager;
        for (CEquip* Equip = Equipment.Traverse(Walk); Equip != nullptr;
             Equip = Equipment.Traverse(Walk))
        {
            if (damage_equip(Equip, Equip->GetMaxHitPoints() * Ratio, Damage))
                bAnyDestroyed = true;
        }
    }

    // Re-read.
    if (!Damage.bDestroyed)
    {
        CArchGrpTraverser Walk;
        CArchGroupManager& Groups = static_cast<CEqObj*>(Raw)->ArchGroups;
        for (CArchGroup* Group = Groups.Traverse(Walk); Group != nullptr;
             Group = Groups.Traverse(Walk))
        {
            // The product is formed on the unrounded max and rounded once.
            if (damage_group(Group, (float)(Group->GetMaxHitPoints() * Ratio), Damage))
                bAnyDestroyed = true;
        }
    }

    return bAnyDestroyed;
}

// FUN_06d01550.
void Ship::tick_jettison(float Dt)
{
    if (Jettison.Items.size() == 0u)
    {
        if (JettisonTimer == 0.0f)
            return;
        // Return while it is still > 0 (or unordered).
        JettisonTimer = JettisonTimer - Dt;
        if (!(JettisonTimer <= 0.0f))
            return;
        // The owed bay closes on the raw CShip (close_bay's float result is
        // dropped).
        while (PendingBayCloses > 0)
        {
            static_cast<CShip*>(Object)->close_bay();
            --PendingBayCloses;
        }
        JettisonTimer = 0.0f;
        return;
    }

    // Only a running timer counts down; clamped to 0 once it is <= 0.
    if (JettisonTimer != 0.0f)
    {
        JettisonTimer = JettisonTimer - Dt;
        if (JettisonTimer <= 0.0f)
            JettisonTimer = 0.0f;
    }
    if (JettisonTimer != 0.0f)
        return;
    // The raw CShip's BayState (+0x2ac) must be 2.
    if (static_cast<CShip*>(Object)->BayState != 2)
        return;

    EquipDesc& Desc = Jettison.Items.front();

    FLootRecord Record;
    Record.Id = 0u;
    Record.Arch = nullptr;
    Record.HitPts = 0.0f;
    Record.ContentsArch = nullptr;
    Record.ContentsHitPts = 0.0f;
    Record.Count = 0;
    Record.MarkForPlayer = 0u;
    Record.NameId = 0u;
    Record.bCanAiTractor = 1;
    Record.bMission = 0;

    // AllocateObjectID is inlined in retail.
    Record.Id = CFLIDMaker::GetInstance()->AllocateObjectID();

    Archetype::Equipment* const Contents = Archetype::GetEquipment(Desc.ArchId);
    Record.ContentsArch = Contents;
    Record.ContentsHitPts = Desc.Health * Contents->HitPts;
    Archetype::Equipment* const LootArch = Contents->get_loot_appearance();
    Record.Arch = LootArch;
    // Same nickname -> the contents' hit points, else the loot archetype's own.
    if (LootArch->Nickname.Id == Contents->Nickname.Id)
        Record.HitPts = Record.ContentsHitPts;
    else
        Record.HitPts = LootArch->HitPts;

    int const Count = Desc.Count;
    Record.Count = Count;

    compute_jettison_launch(&Record.Position, &Record.Orientation, &Record.Velocity);   // FUN_06d01860

    // The spin: two rand() draws, each rand() * 2^-15 (.rdata:06d656b8) *
    // (double)0.8f (.rdata:06d656b0) - (double)0.4f (.rdata:06d656a8), stored
    // as a float; the first scales the orientation's column 0, the second its
    // column 2.
    Matrix const& M = Record.Orientation;
    float const A = static_cast<float>(
        static_cast<double>(static_cast<float>(rand()) * 3.0517578125e-05f) *
        static_cast<double>(0.8f) - static_cast<double>(0.4f));
    float const Ax = M.E[0][0] * A;
    float const Ay = M.E[1][0] * A;
    float const Az = M.E[2][0] * A;
    float const B = static_cast<float>(
        static_cast<double>(static_cast<float>(rand()) * 3.0517578125e-05f) *
        static_cast<double>(0.8f) - static_cast<double>(0.4f));
    Record.AngularVelocity.x = M.E[0][2] * B + Ax;
    Record.AngularVelocity.y = M.E[1][2] * B + Ay;
    Record.AngularVelocity.z = M.E[2][2] * B + Az;

    OwnerSystem->create_loot(get_id(), 1, &Record, 1);   // FUN_06d08a40

    Desc.set_count(Count - Record.Count);

    // An emptied stack is dropped, and one more bay close is owed.
    if (Desc.Count == 0)
    {
        ++PendingBayCloses;
        Jettison.Items.erase(Jettison.Items.begin());
    }

    JettisonTimer = CShip::JETTISON_CLEARANCE_TIME;
}

// FUN_06d01860. The raw CBase::Object's archetype (+0x88) is read as
// Archetype::Ship with no test.
void Ship::compute_jettison_launch(Vector* OutPos, Matrix* OutOrient, Vector* OutVel)
{
    Archetype::Ship const* const Arch =
        static_cast<Archetype::Ship const*>(Object->Archetype);

    // Both bay hardpoints (HpBaySurface +0xe0, HpBayExternal +0xe4) must exist
    // on the engine instance.
    long Surface = 0;
    long External = 0;
    Vector Dir;
    if (FindHardpoint(get_index(), Arch->HpBaySurface.Value, Surface) &&
        FindHardpoint(get_index(), Arch->HpBayExternal.Value, External))
    {
        HardpointPosition(Surface, Arch->HpBaySurface.Value, OutPos, OutOrient);
        Vector ExternalPos;
        HardpointPosition(External, Arch->HpBayExternal.Value, &ExternalPos, nullptr);
        // Exterior minus surface.
        Dir.x = ExternalPos.x - OutPos->x;
        Dir.y = ExternalPos.y - OutPos->y;
        Dir.z = ExternalPos.z - OutPos->z;
    }
    else
    {
        // The raw object's position and orientation; minus column 1.
        *OutPos = Object->WorldPosition;
        *OutOrient = Object->Orientation;
        Dir.x = -OutOrient->E[0][1];
        Dir.y = -OutOrient->E[1][1];
        Dir.z = -OutOrient->E[2][1];
    }

    *OutVel = Dir;

    // Doubles .rdata:06d656d8 / .rdata:06d656d0.
    double const Speed =
        (static_cast<double>(static_cast<float>(rand()) * 3.0517578125e-05f) * 0.5 + 0.75) *
        static_cast<double>(JETTISONED_CARGO_VELOCITY);
    // The scale Speed / |OutVel| is stored as a float.
    double const Length = sqrt(
        (static_cast<double>(OutVel->x) * OutVel->x + static_cast<double>(OutVel->y) * OutVel->y) +
        static_cast<double>(OutVel->z) * OutVel->z);
    float const Scale = static_cast<float>(Speed / Length);
    OutVel->x = Scale * OutVel->x;
    OutVel->y = Scale * OutVel->y;
    OutVel->z = Scale * OutVel->z;

    Vector const Velocity = get_velocity();
    OutVel->x = Velocity.x + OutVel->x;
    OutVel->y = Velocity.y + OutVel->y;
    OutVel->z = Velocity.z + OutVel->z;
}

// LAB_06d01450 -- Ship vtable +0x16c.
bool Ship::update(float Dt)
{
    if (!EqObj::update(Dt))                                     // FUN_06ce7e20
        return false;

    if (DeathSequenceObjId == 0u)
    {
        // OwnerPlayer is read through the (& 3) narrowing with NO null test;
        // CObject::update itself goes through the raw Object.
        CObject* const Raw = Object;
        CSimple* Simple = nullptr;
        if (Raw != nullptr &&
            (static_cast<unsigned>(Raw->ObjectClass) & 0x3u) == 0x3u)
            Simple = static_cast<CSimple*>(Raw);

        if (Simple->OwnerPlayer == 0u)
        {
            Raw->update(Dt, CObject::NO_SCANNER);
        }
        else if (SinglePlayer() == 0)
        {
            Object->update(Dt, CObject::UPDATE_DUMB);
        }
        // else: owned + single player skips CObject::update.

        tick_jettison(Dt);
        tick_zone_damage(Dt);
        return true;
    }

    // Death-sequence arm: the raw CBase::Object, no class or null test.
    CObject* const DeathObj = Object;
    if (SinglePlayer() != 0)
    {
        // CShip::bJumpedOut (+0x214).
        if (((CShip*)DeathObj)->bJumpedOut == 0)
            return true;
        Ship_QueueDying(this);                                 // FUN_06d00130
        DeathSequenceObjId = 0u;
        return true;
    }

    // MP: bail while CShip+0x2bd (bIsPlayer) is set.
    if (static_cast<CShip*>(DeathObj)->bIsPlayer != 0)
        return true;

    // Leave unless <= 0.
    DeathSequenceTimer -= Dt;
    if (DeathSequenceTimer <= 0.0f)
    {
        DeathObj->enable_collisions_r(true);
        Ship_QueueDying(this);
        DeathSequenceTimer = 0.0f;
        DeathSequenceObjId = 0u;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Ship's GameObject / EqObj overrides and its IObjAffect table. In every
// IObjAffect body retail runs with ECX = the IObjAffect sub-object (this + 4),
// so `[ECX+0xc]` is the raw CBase::Object (+0x10), `[ECX+0x24]` is OwnerSystem
// (+0x28) and `ECX - 4` is the wrapper; the port's `this` is the wrapper itself.
// ---------------------------------------------------------------------------

// .text:06d01a60. GameObject's body (FUN_06cee6e0) is called directly.
void Ship::enter_system(StarSystem* Sys, PhySys::CreateParms const* Parms)
{
    GameObject::enter_system(Sys, Parms);
    DeathSequenceObjId = 0u;
}

// .text:06d01a10. OwnerPlayer is read unguarded; the EqObj body (FUN_06ceb330)
// is called directly.
void Ship::apply_damage_list(DamageList& Damage)
{
    if (Damage.Cause == static_cast<DamageCause>(6) && csimple()->OwnerPlayer == 0u)
        toggle_cruise(false, true, 0);
    EqObj::apply_damage_list(Damage);
}

// .text:06d01a90. The EqObj body (FUN_06cec4d0) is called directly and its
// result returned on every path. When DEATH_COMM_FUSE is lit, every follower
// in CShip's formation (+0x1d4) and follow (+0x1f8) lists that is not a player
// gets behavior event 7 with this ship's id: "my leader's death fuse is lit".
// The (& 0x503) narrowing of the follower is NOT tested before
// get_behavior_interface.
bool Ship::light_fuse(unsigned int SourceId, ID_String const& FuseId,
                      unsigned short SubObjId, float AtT, float Lifetime)
{
    bool const bLit = EqObj::light_fuse(SourceId, FuseId, SubObjId, AtT, Lifetime);
    if (!bLit)
        return bLit;
    if (FuseId.Id != DEATH_COMM_FUSE.Id)
        return bLit;

    // Raw CBase::Object as CShip, no test.
    CShip* const Raw = static_cast<CShip*>(Object);

    // Retail repeats this body for both loops. *Entry is read again for the
    // cobject() call, and the ship's own id is fetched per follower.
    auto NotifyFollower = [this](FObjRWWatcher const& Entry)
    {
        // W != 0 -> W - 8, tested for 0 (CShip's typed watcher, common_cobject.h).
        IObjRW* const Follower = Entry.get();
        if (Follower == nullptr)
            return;
        if (Follower->is_player())
            return;
        IObjRW* const FollowerAgain = Entry.get();
        CObject* const FollowerObj = const_cast<CObject*>(FollowerAgain->cobject());
        CEqObj* const FollowerEqObj =
            (FollowerObj != nullptr &&
             (static_cast<unsigned int>(FollowerObj->ObjectClass) & 0x503u) == 0x503u)
                ? static_cast<CEqObj*>(FollowerObj) : nullptr;
        IBehaviorManager* const Behavior = FollowerEqObj->get_behavior_interface();
        if (Behavior == nullptr)
            return;
        // IBehaviorManager slot 0 (Common LAB_062d3f90).
        unsigned int LeaderId = get_id();
        FBehaviorManagerEvent FuseEvent;
        FuseEvent.Type = 7;
        FuseEvent.Data = &LeaderId;
        Behavior->OnEvent(&FuseEvent);
    };

    // The end of each vector is re-read after every entry.
    for (std::vector<FObjRWWatcher>::const_iterator Entry = Raw->FormationFollowers.begin();
         Entry != Raw->FormationFollowers.end(); ++Entry)
        NotifyFollower(*Entry);
    for (std::vector<FObjRWWatcher>::const_iterator Entry = Raw->FollowFollowers.begin();
         Entry != Raw->FollowFollowers.end(); ++Entry)
        NotifyFollower(*Entry);
    return bLit;
}

// .text:06d01c90: the EqObj body (FUN_06ce8510) directly, then every queued
// jettison item appended.
void Ship::collect_droppable_equip(std::vector<EquipDesc>& Out)
{
    EqObj::collect_droppable_equip(Out);
    for (std::list<EquipDesc>::const_iterator It = Jettison.Items.begin();
         It != Jettison.Items.end(); ++It)
        Out.push_back(*It);
}

// FUN_06d01e80. A PendingRequest (+0x9c) hears the result first (vtable slot
// 0) and is dropped when it answers false; with none, the client is told
// (IClient +0x14c, OwnerPlayer read unguarded) and bDockInUse (+0x98) becomes
// (RequestType == 0 && Response == 3).
void Ship::request_returned(int RequestType, int Response, int Arg3)
{
    if (PendingRequest != nullptr)
    {
        if (!PendingRequest->OnResult(RequestType, Response, Arg3))
        {
            PendingRequest = nullptr;
            bDockInUse = 0;
        }
        return;
    }
    unsigned int OwnerPlayer = csimple()->OwnerPlayer;
    unsigned int const Id = get_id();
    g_pClientInterface->SendServerRequestReturned(OwnerPlayer, Id, RequestType, Response, Arg3);
    bDockInUse = 0;
    if (RequestType == 0 && Response == 3)
        bDockInUse = 1;
}

// .text:06d01f30.
int Ship::set_throttle(float Throttle)
{
    static_cast<CShip*>(Object)->set_throttle(Throttle);
    return 0;
}

// .text:06d01f50: CShip+0x230 is the CSteering.
int Ship::set_axis_throttle(Vector const& AxisThrottle)
{
    static_cast<CShip*>(Object)->Steering.SetAxisThrottle(AxisThrottle);
    return 0;
}

// .text:06d01f70.
int Ship::set_nudge_vec(Vector const& NudgeVec)
{
    static_cast<CShip*>(Object)->set_nudge_vec(NudgeVec);
    return 0;
}

// FUN_06d01f90. Strafing switches an inactive engine (type 0x20000, used
// untested) on first. Always returns 0.
int Ship::set_strafe_dir(StrafeDir Dir)
{
    CShip* const Raw = static_cast<CShip*>(Object);
    if (Raw->get_strafe_dir() == Dir)
        return 0;
    CEquip* const Engine = Raw->EquipManager.FindFirst(0x20000u);
    if (!Engine->IsActive())
    {
        unsigned short const EngineId = Engine->GetID();
        toggle_item(&EngineId, true, 0);
    }
    static_cast<CShip*>(Object)->set_strafe_dir(Dir);
    return 0;
}

// FUN_06d029c0. CShip::FormationLeader (+0x1b8) is a BaseWatcher (Leader + 8);
// BaseWatcher::set is the compare + set_pointer retail inlines. A player's ship
// (OwnerPlayer read unguarded) then tells its client (IClient +0x1e4,
// FORMATION_UPDATE).
int Ship::set_formation_leader(IObjRW* Leader)
{
    CShip* const Raw = static_cast<CShip*>(Object);
    Watchable const* const Watched = (Leader != nullptr) ? static_cast<Watchable const*>(Leader)
                                                         : nullptr;
    Raw->FormationLeader.set(Watched);

    unsigned int const Owner = csimple()->OwnerPlayer;
    if (Owner != 0u)
    {
        Vector Offset;
        get_formation_offset(Offset);
        unsigned int const LeaderId = (Leader != nullptr) ? Leader->get_id() : 0u;
        g_pClientInterface->SendServerFormationUpdate(Owner, LeaderId, Offset);
    }
    return 0;
}

// .text:06d02940. CShip+0x1c0 on the raw object, untested; then the same
// client update as set_formation_leader.
int Ship::set_formation_offset(Vector const& Offset)
{
    static_cast<CShip*>(Object)->FormationOffset = Offset;
    unsigned int const Owner = csimple()->OwnerPlayer;
    if (Owner != 0u)
    {
        IObjRW* Leader = nullptr;
        get_formation_leader(Leader);
        unsigned int const LeaderId = (Leader != nullptr) ? Leader->get_id() : 0u;
        g_pClientInterface->SendServerFormationUpdate(Owner, LeaderId, Offset);
    }
    return 0;
}

// .text:06d02a50 / 06d02a60 / 06d02a70 / 06d02ac0 / 06d02ad0 -- each a tail
// jump into the CShip export, whose FORMATION_RTYPE (or count) is the result.
int Ship::add_formation_follower(IObjRW* Follower)
{
    return static_cast<int>(static_cast<CShip*>(Object)->add_formation_follower(Follower));
}

int Ship::remove_formation_follower(IObjRW* Follower)
{
    return static_cast<int>(static_cast<CShip*>(Object)->remove_formation_follower(Follower));
}

int Ship::get_formation_follower_count()
{
    return static_cast<CShip*>(Object)->get_formation_follower_count();
}

int Ship::add_follow_follower(IObjRW* Follower)
{
    return static_cast<int>(static_cast<CShip*>(Object)->add_follow_follower(Follower));
}

int Ship::remove_follow_follower(IObjRW* Follower)
{
    return static_cast<int>(static_cast<CShip*>(Object)->remove_follow_follower(Follower));
}

// .text:06d02aa0.
int Ship::set_follow_leader(IObjRW* Leader)
{
    static_cast<CShip*>(Object)->set_follow_leader(Leader);
    return 0;
}

// .text:06d02a80.
int Ship::set_follow_offset(Vector const& Offset)
{
    static_cast<CShip*>(Object)->set_follow_offset(Offset);
    return 0;
}

// .text:06d020d0, the jettison request. Controller event 0x23 first; a stack
// in a pod (+0x20) is dropped by destroying the pod (fate 4, cause 0x16);
// otherwise the desc is queued for the bay (+0xa4), the bay opened (its float
// result dropped), and the system told (FUN_06d0f0c0).
int Ship::jettison_cargo(unsigned short EquipId, int Count, int SourceClient)
{
    (void)SourceClient;   // retail never reads the third argument
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;

    CECargo* const Cargo =
        CECargo::cast(static_cast<CEqObj*>(Object)->EquipManager.FindByID(EquipId));
    if (Cargo == nullptr)
        return -2;

    // Unsigned min.
    unsigned int Amount = static_cast<unsigned int>(Count);
    if (!(Amount < Cargo->Count))
        Amount = Cargo->Count;

    struct FControllerMsg23
    {
        unsigned int ObjId;
        unsigned short EquipId;
        unsigned short Pad;       // not written by retail
        unsigned int GoodId;
        unsigned int Count;
    } Msg;
    Msg.ObjId = get_id();
    Msg.EquipId = EquipId;
    Msg.Pad = 0;
    Msg.GoodId = Arch2Good(Cargo->EquipArch()->get_id());
    Msg.Count = Amount;
    Server_ControllerDispatch(0, pub::Controller::EMessage::CargoJettisoned, &Msg);

    CEquip* const Pod = Cargo->CargoPod;
    if (Pod != nullptr)
    {
        // InflictorOwner read through the unguarded (& 3) narrowing.
        DamageList Damage;
        Damage.bDestroyed = false;
        Damage.Cause = static_cast<DamageCause>(0x16);
        Damage.InflictorId = get_id();
        Damage.InflictorOwner = csimple()->OwnerPlayer;
        destroy_equip(Pod, static_cast<DamageEntry::SubObjFate>(4), Damage);
        apply_damage_list(Damage);
        return 0;
    }

    EquipDesc Desc;
    if (!Cargo->GetEquipDesc(Desc))
        return -5;
    Desc.set_count(static_cast<int>(Amount));

    // Node copy is FUN_06ced210.
    Jettison.Items.push_back(Desc);

    static_cast<CShip*>(Object)->open_bay();
    if (!SinglePlayer())
        Cargo->RemoveFromStack(Amount);
    OwnerSystem->notify_jettison(this, EquipId, static_cast<int>(Amount));
    return 0;
}

// .text:06d02340. Single player with a SourceClient goes straight to the
// notification (FUN_06d0e670 with SUBOBJ_ID_NONE as the equip); the server's
// own unchanged request is not announced.
int Ship::set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient)
{
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;
    if (SourceClient == -1)
        SourceClient = 0;

    if (!(SinglePlayer() && SourceClient != 0))
    {
        bool bChanged = false;
        CShip* const Raw = static_cast<CShip*>(Object);
        if (Raw->get_target() != Target || Raw->get_sub_target() != SubTarget)
        {
            bChanged = true;
            Raw->set_target(Target);
            Raw->set_sub_target(SubTarget);
        }
        if (SourceClient == 0 && !bChanged)
            return 0;
    }

    unsigned int const Id = get_id();
    OwnerSystem->notify_set_target(Id, SUBOBJ_ID_NONE, Target, SubTarget, SourceClient);
    return 0;
}

// .text:06d02410. The engine (CEEngine::cast of FindFirst(0x20000)) is used
// untested; engaging cruise from the server switches the thrusters off first.
// Announced through FUN_06d0e580.
int Ship::toggle_cruise(bool bEngage, bool bArg2, int SourceClient)
{
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;
    if (SourceClient == -1)
        SourceClient = 0;

    if (!(SinglePlayer() && SourceClient != 0))
    {
        bool bChanged = false;
        CEEngine* const Engine =
            CEEngine::cast(static_cast<CEqObj*>(Object)->EquipManager.FindFirst(0x20000u));
        if (Engine->bCruiseActive != static_cast<unsigned char>(bEngage))
        {
            bChanged = true;
            if (bEngage && SourceClient == 0)
                toggle_thrusters(false, 0);
            Engine->EngageCruise(bEngage, bArg2);
        }
        if (SourceClient == 0 && !bChanged)
            return 0;
    }

    unsigned int const Id = get_id();
    OwnerSystem->notify_cruise(Id, bEngage, bArg2, SourceClient);
    return 0;
}

// .text:06d02500. The engine (FindFirst(0x20000)) is used untested; the
// server's own request to activate is refused (-5) under cruise and switches
// an inactive engine on. Announced through FUN_06d0e600.
int Ship::toggle_thrusters(bool bActivate, int SourceClient)
{
    if (OwnerSystem == nullptr)
        return -5;
    if (is_destroyed())
        return -5;
    int const Result = 0;
    if (SourceClient == -1)
        SourceClient = 0;

    if (!(SinglePlayer() && SourceClient != 0))
    {
        bool bChanged = false;
        CEqObj* const Raw = static_cast<CEqObj*>(Object);
        CEquip* const Engine = Raw->EquipManager.FindFirst(0x20000u);
        unsigned char const bCruiseActive = static_cast<CEEngine*>(Engine)->bCruiseActive;
        bool const bEngineActive = Engine->IsActive();
        if (SourceClient == 0 && bActivate)
        {
            if (bCruiseActive != 0)
                return -5;
            if (!bEngineActive)
            {
                unsigned short const EngineId = Engine->GetID();
                toggle_item(&EngineId, true, 0);
            }
        }

        CEquipTraverser Walk(0x400);
        for (CEquip* Thruster = Raw->EquipManager.Traverse(Walk); Thruster != nullptr;
             Thruster = Raw->EquipManager.Traverse(Walk))
        {
            if (Thruster->IsActive() != bActivate)
            {
                bChanged = true;
                Thruster->Activate(bActivate);
            }
        }
        if (SourceClient == 0 && !bChanged)
            return Result;
    }

    unsigned int const Id = get_id();
    OwnerSystem->notify_thrusters(Id, bActivate, SourceClient);
    return Result;
}

// .text:06d02670. PendingRequest and bDockInUse are set BEFORE the type is
// looked at; an unknown type clears bDockInUse but PendingRequest keeps the
// callback. Deviation: for type 0 retail never writes bytes +0xd..+0xf of the
// message (stack garbage); the port zeroes them. The system id is
// FUN_06d06cd0. Every path returns 0.
int Ship::request_event(int RequestType, unsigned const* TargetId, unsigned const* SecondId,
                  int bFlag, RequestCB* Callback)
{
    if (bDockInUse != 0)
        return 0;
    PendingRequest = Callback;
    bDockInUse = 1;

    if (RequestType == 0)
    {
        unsigned int Msg[4] = { 0u, 0u, 0u, 0u };
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = OwnerSystem->SystemInfo->NicknameId;
        reinterpret_cast<unsigned char*>(&Msg[3])[0] = (bFlag == 1) ? 1u : 0u;
        Server_ControllerDispatch(0, pub::Controller::EMessage::DockRequest, Msg);
        return 0;
    }
    if (RequestType == 1)
    {
        unsigned int Msg[3];
        Msg[2] = OwnerSystem->SystemInfo->NicknameId;
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::FormationRequest, Msg);
        return 0;
    }
    if (RequestType == 2)
    {
        unsigned int Msg[4];
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = *SecondId;
        Msg[3] = OwnerSystem->SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::TradelaneRequest, Msg);
        return 0;
    }
    bDockInUse = 0;
    return 0;
}

// .text:06d027a0 (FUN_06d027ac is the same body). Every path ends in
// set_invincible({2, 0, 0, 1.0f}).
int Ship::request_cancel(int RequestType, unsigned const* TargetId, int Arg3)
{
    PendingRequest = nullptr;
    bDockInUse = 0;

    if (RequestType == 0)
    {
        unsigned int Msg[3];
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = static_cast<unsigned int>(Arg3);
        Server_ControllerDispatch(0, pub::Controller::EMessage::DockCancel, Msg);
    }
    else if (RequestType == 1)
    {
        unsigned int Msg[3];
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = OwnerSystem->SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::FormationLeave, Msg);
    }
    else if (RequestType == 2)
    {
        unsigned int Msg[3];
        Msg[0] = get_id();
        Msg[1] = *TargetId;
        Msg[2] = OwnerSystem->SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::TradelaneRelease, Msg);
    }

    FInvincibility Cmd = {};
    Cmd.Type = 2u;
    Cmd.bProtect = false;
    Cmd.bFromPlayers = false;
    Cmd.MaxHpLoss = 1.0f;
    set_invincible(Cmd);
    return 0;
}

// .text:06d028c0. EntranceRing is untested; announced through FUN_06d0df40.
int Ship::go_tradelane(IObjInspect const* EntranceRing, IObjInspect const* ExitRing,
                       bool bFastExit, float Speed)
{
    if (static_cast<CShip*>(Object)->go_tradelane(EntranceRing, ExitRing, this, bFastExit, Speed) != 0)
        return -5;
    unsigned int const ExitId = (ExitRing != nullptr) ? ExitRing->get_id() : 0u;
    unsigned int const EntranceId = EntranceRing->get_id();
    unsigned int const ShipId = get_id();
    OwnerSystem->notify_go_tradelane(ShipId, EntranceId, ExitId, bFastExit, Speed);
    return 0;
}

// FUN_06d02880. The exit ring is read before the entrance ring; announced
// through FUN_06d0e120.
int Ship::stop_tradelane()
{
    CShip* const Raw = static_cast<CShip*>(Object);
    Raw->stop_tradelane();
    unsigned int const ExitId = Raw->get_tradelane_exit_ring();
    unsigned int const EntranceId = static_cast<CShip*>(Object)->get_tradelane_entrance_ring();
    unsigned int const ShipId = get_id();
    OwnerSystem->notify_stop_tradelane(ShipId, EntranceId, ExitId);
    return 0;
}

// .text:06d02000. Lifts the launch grant apply_launch_invincibility made and
// sends controller event 0x28 (LaunchComplete).
int Ship::slot_70(unsigned Id)
{
    if (bLaunchInvincible != 0)
    {
        bLaunchInvincible = 0;
        FInvincibility Cmd = {};
        Cmd.Type = 1u;
        Cmd.bProtect = false;
        Cmd.bFromPlayers = false;
        Cmd.MaxHpLoss = 1.0f;
        set_invincible(Cmd);
        unsigned int Msg[2];
        Msg[0] = Id;
        Msg[1] = get_id();
        Server_ControllerDispatch(0, pub::Controller::EMessage::LaunchComplete, Msg);
    }
    return 0;
}

// .text:06d02070. Controller event 0x26 (JumpInComplete), then lifts the type-2
// invincibility.
int Ship::slot_74(unsigned Arg)
{
    unsigned int Msg[2];
    Msg[0] = Arg;
    Msg[1] = get_id();
    Server_ControllerDispatch(0, pub::Controller::EMessage::JumpInComplete, Msg);
    FInvincibility Cmd = {};
    Cmd.Type = 2u;
    Cmd.bProtect = false;
    Cmd.bFromPlayers = false;
    Cmd.MaxHpLoss = 1.0f;
    set_invincible(Cmd);
    return 0;
}

// ---------------------------------------------------------------------------
// Solar -- create .text:06d02c50
// ---------------------------------------------------------------------------

Solar::Solar()
{
}

Solar::~Solar()
{
    // Solar's +0x15c entry is EqObj's destroy_notify (.text:06ce7c80); ~EqObj
    // (FUN_06ce6710) follows as the base destructor.
    destroy_notify();
}

// FUN_06d02c20. AddRef on Obj untested.
bool Solar::attach_existing(CObject* Obj)
{
    Object = Obj;
    Obj->AddRef();
    advise(true);
    return Object != nullptr;
}

// +0x16c .text:06d02cc0. The EqObj body (FUN_06ce7e20) directly; the 0x303
// narrowing is not null-tested and CObject::update's result is dropped.
bool Solar::update(float Dt)
{
    if (!EqObj::update(Dt))
        return false;
    csolar()->update(Dt, CObject::UPDATE_DEFAULT);
    return true;
}

// +0x158 .text:06d02d10. The EqObj body (FUN_06ce8080) directly; on success the
// owning system drops the solar (FUN_06d08940).
bool Solar::destroy(int Kind, unsigned int ObjId)
{
    if (!EqObj::destroy(Kind, ObjId))
        return false;
    OwnerSystem->remove_solar(this);
    return true;
}

// +0x17c .text:06d02d40. A solar that is not destructible (CSolar +0x1a8 clear,
// read through the unguarded 0x303 narrowing) ignores a hit on its root;
// everything else is EqObj's (FUN_06ce9250, a tail jump).
void Solar::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    if (!csolar()->bDestructible && Hit.SubObjId == SUBOBJ_ID_ROOT)
        return;
    EqObj::damage_sub_obj(Hit, Damage);
}

// The 0x24-byte record Solar's two root-hit overrides hand to controller event
// 0xd: nine dwords zeroed, then the solar's id, its system's id, the
// DamageList's inflictor (+0x14) and cause (+0x10), and 1.0f in the last one.
struct FSolarHitEvent
{
    unsigned int ObjId;        // +0x00  get_id()
    unsigned int SystemId;     // +0x04  FUN_06d06cd0 on OwnerSystem
    unsigned int InflictorId;  // +0x08  DamageList +0x14
    unsigned int Zero0c;       // +0x0c
    unsigned int Zero10;       // +0x10
    unsigned int Cause;        // +0x14  DamageList +0x10
    unsigned int Zero18;       // +0x18
    unsigned int Zero1c;       // +0x1c
    float Scale;               // +0x20  1.0f
};
static_assert(sizeof(FSolarHitEvent) == 0x24, "Solar root-hit event is 0x24 bytes");

// +0x180 .text:06d02d90. A destructible solar, or a hit on a sub-object, takes
// the EqObj body (FUN_06ce9350). A root hit on an indestructible solar only
// reports: FSolarHitEvent (system id read first) to controller event 0xd, the
// same nine dwords as a type-0 behavior event (through the unguarded 0x303
// narrowing), then damage_power with the munition's EnergyDamage.
void Solar::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    if (csolar()->bDestructible)
    {
        EqObj::munition_hit(Hit, Damage);
        return;
    }
    if (Hit.SubObjId != SUBOBJ_ID_ROOT)
    {
        EqObj::munition_hit(Hit, Damage);
        return;
    }

    FSolarHitEvent Event = {};
    Event.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Event.InflictorId = Damage.InflictorId;
    Event.ObjId = get_id();
    Event.Cause = static_cast<unsigned int>(Damage.Cause);
    Event.Scale = 1.0f;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Event);

    IBehaviorManager* const Behavior = static_cast<CEqObj*>(csolar())->get_behavior_interface();
    if (Behavior != nullptr)
    {
        // The 0xd record reordered into FBehaviorDamageEvent:
        // {system, inflictor, id, 0, 0, cause, 0, 0, 1.0f}.
        FBehaviorDamageEvent Payload;
        Payload.SystemId = Event.SystemId;
        Payload.InflictorId = Event.InflictorId;
        Payload.ObjId = Event.ObjId;
        Payload.Cause = static_cast<int>(Event.Cause);
        std::memcpy(&Payload.Damage, &Event.Zero18, sizeof(unsigned int));
        std::memcpy(&Payload.RelativeDamage, &Event.Zero1c, sizeof(unsigned int));
        Payload.RelativeHealth = Event.Scale;
        Payload.EquipType = 0u;
        Payload.CollisionGroupType = 0u;
        FBehaviorManagerEvent DamageEvent;
        DamageEvent.Type = 0;
        DamageEvent.Data = &Payload;
        Behavior->OnEvent(&DamageEvent);
    }

    damage_power(Hit.Munition->EnergyDamage, Damage);
}

// +0x1fc .text:06d02f20. A destructible solar (CSolar +0x1a8) takes EqObj's
// hull pass (FUN_06ce9550). An indestructible one only reports the blast, as
// munition_hit does -- no damage at all; Blast is not read on that path.
void Solar::explosion_hit_hull(ExplosionDamageEvent const& Blast, DamageList& Out)
{
    if (csolar()->bDestructible)
    {
        EqObj::explosion_hit_hull(Blast, Out);
        return;
    }

    FSolarHitEvent Event = {};
    Event.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Event.InflictorId = Out.InflictorId;
    Event.ObjId = get_id();
    Event.Cause = static_cast<unsigned int>(Out.Cause);
    Event.Scale = 1.0f;
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDamaged, &Event);

    IBehaviorManager* const Behavior = static_cast<CEqObj*>(csolar())->get_behavior_interface();
    if (Behavior != nullptr)
    {
        FBehaviorDamageEvent Payload;
        Payload.SystemId = Event.SystemId;
        Payload.InflictorId = Event.InflictorId;
        Payload.ObjId = Event.ObjId;
        Payload.Cause = static_cast<int>(Event.Cause);
        std::memcpy(&Payload.Damage, &Event.Zero18, sizeof(unsigned int));
        std::memcpy(&Payload.RelativeDamage, &Event.Zero1c, sizeof(unsigned int));
        Payload.RelativeHealth = Event.Scale;
        Payload.EquipType = 0u;
        Payload.CollisionGroupType = 0u;
        FBehaviorManagerEvent DamageEvent;
        DamageEvent.Type = 0;
        DamageEvent.Data = &Payload;
        Behavior->OnEvent(&DamageEvent);
    }
}

// DAT_06d8d97c -- Solar.cpp's own lazily fetched math engine.
static I3DMathEngine* GSolarMathEngine = nullptr;

// +0x250 .text:06d03070. The type bits pick the branch:
//   0x40 or 0x800 -- somewhere on a 500-unit ring 350 units behind the solar.
//     vt +0x10 is ICF-folded with get_transform, so the copy is the whole
//     Transform. 2pi is the double .rdata:06d66b80.
//   0x400 -- in place, turned round: a half turn about the local Y axis with
//     cos/sin(pi) on the x87 (pi double .rdata:06d67728), both rows from the
//     old values.
//   anything else -- FDUMP, {35000, 0, 35000} and identity.
void Solar::ChooseSystemArrivalLocation(GameObject* Arriving, Vector& OutPos, Matrix& OutOrient)
{
    (void)Arriving;   // pushed by both callers, never read
    unsigned int const TypeBits = get_type_bits();

    if ((TypeBits & 0x40u) != 0 || (TypeBits & 0x800u) != 0)
    {
        Transform const Xform = reinterpret_cast<Transform const&>(get_orientation());
        int const Random = rand();
        double const Angle = static_cast<double>(static_cast<float>(Random) * 3.0517578125e-05f)
                           * 6.2831854820251465;
        Vector Local;
        Local.x = static_cast<float>(cos(Angle) * 500.0);
        Local.y = static_cast<float>(sin(Angle) * 500.0);
        Local.z = -350.0f;
        if (GSolarMathEngine == nullptr)
            GSolarMathEngine = Server_Acquire3DMathEngine();
        Vector Out;
        GSolarMathEngine->TransformXform(reinterpret_cast<FVector&>(Out),
                                         reinterpret_cast<FTransform const&>(Xform),
                                         reinterpret_cast<FVector const&>(Local));
        OutPos = Out;
        OutOrient = Xform.Rotation;
        return;
    }

    if ((TypeBits & 0x400u) != 0)
    {
        OutPos = get_position();
        OutOrient = get_transform().Rotation;
        double const Pi = 3.1415927410125732;
        double const C = cos(Pi);
        double const S = sin(Pi);
        for (int Column = 0; Column < 3; ++Column)
        {
            double const Row0 = OutOrient.E[0][Column];
            double const Row2 = OutOrient.E[2][Column];
            OutOrient.E[0][Column] = static_cast<float>(Row0 * C + S * Row2);
            OutOrient.E[2][Column] = static_cast<float>(C * Row2 - Row0 * S);
        }
        return;
    }

    FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
          "E:\\FL\\Scratch\\Source\\Server\\Space\\Solar.cpp", 0xeb,
          "Invalid type in ChooseSystemArrivalLocation()");
    OutPos.x = 35000.0f;
    OutPos.y = 0.0f;
    OutPos.z = 35000.0f;
    OutOrient.E[0][0] = 1.0f; OutOrient.E[0][1] = 0.0f; OutOrient.E[0][2] = 0.0f;
    OutOrient.E[1][0] = 0.0f; OutOrient.E[1][1] = 1.0f; OutOrient.E[1][2] = 0.0f;
    OutOrient.E[2][0] = 0.0f; OutOrient.E[2][1] = 0.0f; OutOrient.E[2][2] = 1.0f;
}

bool Solar::create(Archetype::Root* Arch)
{
    // .text:06d02c50. Same steps as Ship::create (CreateOfClass) except that
    // CObject::open runs BEFORE CBase::advise(true). No null checks, as in
    // retail.
    Object = CObject::Alloc(CObject::ClassSolar);
    Arch->create_instance(static_cast<IEngineInstance*>(Object));
    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Object->EngineInstanceId), 0, nullptr);
    Object->open(Arch);
    advise(true);
    return Object != nullptr;
}

bool Solar::find(unsigned int const& Id)
{
    // .text:06d02cb0, the "return false" ICF body Loot/Bullet/Mine's find and
    // MedAsteroid's create share: a Solar is never found by id. Single player
    // wraps an existing CSolar through attach_existing (FUN_06d02c20) instead.
    // Ship's is the only EqObj-family find that calls CObject::Find.
    (void)Id;
    return false;
}

CSolar* Solar::csolar() const
{
    CObject* Obj = cobj();
    if (Obj == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Obj->ObjectClass) & 0x303u) != 0x303u)
        return nullptr;
    return static_cast<CSolar*>(Obj);
}

// ---------------------------------------------------------------------------
// Loot -- create .text:06cf2440
// ---------------------------------------------------------------------------

Loot::Loot()
    : CheckTimer(0.0f)
    , UnseenLife(0.0f)
    , DecayAccum(0.0f)
{
    // Retail has no Loot constructor of its own: the spawner FUN_06d08a40 runs
    // GameObject's (FUN_06cee360), zeroes +0x60/+0x64/+0x68 and stores the
    // three Loot vtables.
}

Loot::~Loot()
{
}

// Loot::create (.text:06cf2440) lives in server_loot.cpp.

bool Loot::find(unsigned int const& Id)
{
    // .text:06d02cb0, the shared "return false" ICF body: a Loot is never found
    // by id, it is only ever created.
    (void)Id;
    return false;
}

// .text:06d08eb0.
unsigned int Loot::get_type_bits() const
{
    return 0x10000000u;
}

CLoot* Loot::cloot() const
{
    CObject* Obj = cobj();
    if (Obj == nullptr)
        return nullptr;
    if ((static_cast<unsigned>(Obj->ObjectClass) & 0x803u) != 0x803u)
        return nullptr;
    return static_cast<CLoot*>(Obj);
}
