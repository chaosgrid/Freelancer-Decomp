// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_observer.cpp -- the per-frame observer update and the damage fan-out:
//   FUN_06cf53e0  Observer::update
//   FUN_06d0b800  StarSystem::damage_object
// plus the PlayerData helper FUN_06d4f430 the second one needs. These are the only two
// callers of the Observer entry points.
// ---------------------------------------------------------------------------

#include "../include/common_server.h"
#include "../include/common_server_playerdb.h"
#include "../include/common_misc.h"       // DamageList / DamageEntry, SUBOBJ_ID_NONE
#include "../include/common_physys.h"     // Quaternion
#include "../include/common_csys.h"       // Matrix
#include "../include/x86math.h"           // I3DMathEngine::MatrixToQuaternion
#include "../include/common_cobject.h"
#include "../include/common_cequip.h"
#include "../include/common_archetype.h"
#include "../include/server_createinfo.h"
#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"
#include "../include/rpc_iclient.h"
#include "../include/rpc_iclient_args.h"
#include "../include/common_server_types.h"   // XSetTarget (IClient::SendCommonSetTarget)

#include <cstdlib>
#include <list>
#include <vector>

extern IClient* g_pClientInterface;

// ===========================================================================
// The bandwidth governor. Every phase of the observer update re-asks
// IClient::GetLinkSaturation (vt+0x50) and stops once the answer is >= 0.9
// (six sites, all against the double 0.9 at .rdata:06d66990; compared in double
// precision). rpclocal's IClient answers a constant 0.0 (.text:06d8f0b0), so
// against the local transport the gate never trips.
// ===========================================================================
static double const kBandwidthCeiling = 0.9;

// g_pClientInterface (.data:DAT_06d8fb14) is called through without a null test.
static bool HasBandwidth(unsigned int PlayerId)
{
    return g_pClientInterface->GetLinkSaturation(PlayerId) < kBandwidthCeiling;
}

// ---------------------------------------------------------------------------
// Wire records sent from here (layouts from the raw stores):
//
// IClient +0x010 SendCommonSetTarget (ch1 op 0x04, packet 0x2c): XSetTarget
//   {ObjectId, SubObjId, Pad, TargetObjectId, TargetSubObjId, Pad}, the same 0x10 bytes
//   IServer::SetTarget takes. A `PUSH EAX` at .text:06cf591c shifts ESP mid-build, so
//   the store that looks like it hits +0x08 actually writes SubObjId at +0x04.
//
// IClient +0x0dc SendServerDestroyObject (ch2 op 0x06, 0x28): FServerDestroyObjectInfo,
//   +0x00 object id, byte +0x04 kind (always 0 here) plus pad. A `PUSH EDX` at
//   .text:06cf5cad shifts ESP, so the id goes first despite the face-value offsets.
//
// IClient +0x0d8 SendCommonUpdateObject (ch1 op 0x01, 0x4c): FCommonUpdateObjectInfo,
//   +0x00 id, +0x04..+0x10 quaternion, +0x14..+0x1c position, +0x20 (float) ElapsedTime,
//   +0x24 CShip::Throttle (+0x29c; moved as a dword but a float), +0x28 byte seeded 0xff
//   and replaced by get_behavior_id() (vt+0x148) only for ships.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// .text:06cf58de .. .text:06cf5a20 -- what the CREATESHIP branch does after the create
// packet: tell the client what the ship, then each of its guns, is aiming at.
// ---------------------------------------------------------------------------
static void SendShipTargets(unsigned int PlayerId, CShip* Ship)
{
    // No null test on the ship; the only caller passes an already narrowed, non-null one.

    // The sub-target is read even when the target is null.
    IObjRW* const Target = Ship->get_target();
    unsigned short const SubTarget = Ship->get_sub_target();
    if (Target != nullptr) {
        XSetTarget Msg;
        Msg.ObjectId = Ship->Id;                 // CShip+0xb0
        Msg.SubObjId = SUBOBJ_ID_NONE;
        Msg.Pad_0x06 = 0u;
        Msg.TargetObjectId = Target->get_id();
        Msg.TargetSubObjId = SubTarget;
        Msg.Pad_0x0e = 0u;
        g_pClientInterface->SendCommonSetTarget(PlayerId, Msg);
    }

    // The ship's CEquipManager (+0xe4) with mask 0x80. The loop ends on the first null
    // CEGun::cast (end of traversal or a non-gun), it does not skip and keep walking.
    CEquipTraverser Walk(EquipTypeGun);
    for (;;) {
        CEGun* const Gun = CEGun::cast(Ship->EquipManager.Traverse(Walk));
        if (Gun == nullptr)
            break;

        // The out-parameters are seeded before the call.
        CSimple* GunTarget = nullptr;
        unsigned short GunSub = SUBOBJ_ID_NONE;
        Gun->GetTarget(GunTarget, GunSub);
        if (GunTarget == nullptr)
            continue;

        XSetTarget Msg;
        // Retail reads the CREATESHIP record's +0x04, which FUN_06d07690 fills from
        // CShip+0xb0 -- the same ship id.
        Msg.ObjectId = Ship->Id;
        Msg.SubObjId = Gun->Id;                  // CEquip+0x08
        Msg.Pad_0x06 = 0u;
        Msg.TargetObjectId = GunTarget->Id;
        Msg.TargetSubObjId = GunSub;
        Msg.Pad_0x0e = 0u;
        g_pClientInterface->SendCommonSetTarget(PlayerId, Msg);
    }
}

// ===========================================================================
// FUN_06cf53e0 -- Observer::update(double Dt).
// ===========================================================================
void Observer::update(double Dt)
{
    // No test on the view (thiscall on the list element).

    // Multiplayer only. This returns before the clock at +0x30 is touched, so
    // ElapsedTime never advances in single player.
    if (SinglePlayer())
        return;

    unsigned int const PlayerId = this->PlayerId;

    ElapsedTime += Dt;

    // Refresh the observer's position (View+0x3c) from the player's ship
    // (PlayerData::ship_object_id). No PlayerDB::is_valid: the slot is indexed raw off
    // Players. The lookup is FUN_06d006c0; a failed lookup skips the copy.
    {
        unsigned int const ShipId = Players[PlayerId].ship_object_id;
        GameObject* const Self = Server_LookupGameObject(ShipId);
        if (Self != nullptr)
            this->Position = Self->get_position();
    }

    // Three empty working lists (retail: FUN_06cfc340, MSVC6 std::list::_Buynode).
    std::list<SystemTrackedObject*> Uncreated;   // not on the client yet
    std::list<SystemTrackedObject*> Created;     // already on the client
    std::list<SystemTrackedObject*> Pending;     // needs an UPDATEOBJECT

    // -----------------------------------------------------------------------
    // Phase 1 -- split the tracked list on bCreatedOnClient (record +0x2f0); the
    // created half also gets its CRemotePhysicsSimulation stepped with the caller's Dt.
    // -----------------------------------------------------------------------
    {
        // Neither a node nor its record is null-tested.
        SystemTrackedNode* const Sentinel = Tracked.Head;
        for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel;
             Node = Node->Next) {
            SystemTrackedObject* const Record = Node->Data;
            if (Record->bCreatedOnClient) {
                Created.push_back(Record);
                Record->Simulation.Doit(Dt);
            } else {
                Uncreated.push_back(Record);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Phase 2 -- for every record the client does not have yet, send the matching
    // CREATE* packet if the player can see it now.
    // -----------------------------------------------------------------------
    if (!HasBandwidth(PlayerId))
        return;

    for (std::list<SystemTrackedObject*>::iterator It = Uncreated.begin();
         It != Uncreated.end(); ++It) {
        // No null test on the record.
        SystemTrackedObject* const Record = *It;
        GameObject* const Obj = Record->Object;

        // FUN_06cf7260.
        if (!should_be_visible(Obj))
            continue;
        // The gauge is re-checked before every send.
        if (!HasBandwidth(PlayerId))
            return;

        // A null object is not skipped: FUN_06cf7260 answers true for null, every
        // narrowing below fails, and the record is still marked created. Skipping it
        // would leave the record in the uncreated half for ever.

        // Narrowings of the same CObject, tested in retail order Loot (0x803), Ship
        // (0x503), CounterMeasure (0x17), Mine (0xf), Guided (0xa7).
        CObject* const Raw = (Obj != nullptr) ? Obj->cobj() : nullptr;
        unsigned int const Cls =
            (Raw != nullptr) ? static_cast<unsigned int>(Raw->ObjectClass) : 0u;
        bool const bIsLoot = (Raw != nullptr) && (Cls & 0x803u) == 0x803u;
        bool const bIsShip = (Raw != nullptr) && (Cls & 0x503u) == 0x503u;
        bool const bIsCounter = (Raw != nullptr) && (Cls & 0x17u) == 0x17u;
        bool const bIsMine = (Raw != nullptr) && (Cls & 0x0fu) == 0x0fu;
        bool const bIsGuided = (Raw != nullptr) && (Cls & 0xa7u) == 0xa7u;

        if (bIsLoot) {
            // CREATELOOT, IClient +0x0c0: a 0x48-byte LootDesc filled by FUN_06d07b90
            // and the 0x14-byte request that points at it. The caller pre-zeroes
            // +0x00..+0x18 and seeds +0x44 = 1, +0x45 = 0, +0x46 = 0; the builder then
            // overwrites +0x45 and the caller +0x46.
            CLoot* const Loot = static_cast<CLoot*>(Raw);
            FServerLootDesc Desc;
            Desc.LootId = 0u;
            Desc.ContentsNickname = 0u;
            Desc.ContentsHitPts = 0.0f;
            Desc.Units = 0u;
            Desc.ContainerNickname = 0u;
            Desc.HitPts = 0.0f;
            Desc.NameOverride = 0u;
            Desc.Field_0x44 = 1;
            Desc.bTemporary = 0;
            Desc.bMarkedBy = 0;
            Desc.Pad47 = 0;
            ServerLootDesc_Fill(&Desc, Loot);
            // The first of two is_marked_by calls lands in the record.
            Desc.bMarkedBy =
                static_cast<unsigned char>(Obj->is_marked_by(PlayerId) ? 1 : 0);

            FServerCreateLootRequest Req;
            Req.OwnerId = Loot->OwnerId;    // CLoot+0xe4
            Req.Kind = 5u;
            Req.Items = &Desc;
            Req.Count = 1;
            Req.Pad0a[0] = 0;
            Req.Pad0a[1] = 0;
            Req.Pad11[0] = 0;
            Req.Pad11[1] = 0;
            Req.Pad11[2] = 0;
            // Retail calls is_marked_by a second time with the same argument.
            Req.bMarkedBy =
                static_cast<unsigned char>(Obj->is_marked_by(PlayerId) ? 1 : 0);
            Req.bTemporary = static_cast<unsigned char>(Loot->bTemporary ? 1 : 0);

            g_pClientInterface->SendServerCreateLoot(PlayerId, Req);
        } else if (bIsShip) {
            // CREATESHIP, IClient +0x0bc: the 0xe0-byte CShipCreateInfo (base ctor
            // FUN_06cf63f0, filled by FUN_06d07690, destroyed by FUN_06cf64d0).
            CShip* const Ship = static_cast<CShip*>(Raw);
            CShipCreateInfo Info;
            CShipCreateInfo_Fill(&Info, Ship);

            // Record +0xd4 = PlayerData::rep_vibe (Players + id*0x418 - 0x100), indexed
            // raw without PlayerDB::is_valid.
            Info.VibeOtherGroupId = Players[PlayerId].rep_vibe;
            // Record +0xa8 (FUN_06cef450).
            Info.bMarkedBy =
                static_cast<unsigned char>(Obj->is_marked_by(PlayerId) ? 1 : 0);

            g_pClientInterface->SendServerCreateShip(PlayerId, Info);

            // Not part of the record: the ship's current target, then one SETTARGET per gun.
            SendShipTargets(PlayerId, Ship);
        } else if (bIsCounter) {
            // CREATECOUNTER, IClient +0x0cc, filled by FUN_06d07c80. The caller zeroes
            // +0x00..+0x08, writes SUBOBJ_ID_NONE at +0x0c and zero-constructs Vectors at
            // +0x10, +0x2c and +0x38 (the last is how +0x38 is known to be a Vector).
            FServerCreateProjectileInfo Info;
            Info.ObjId = 0u;
            Info.ArchetypeNickname = 0u;
            Info.OwnerId = 0u;
            Info.SubObjId = SUBOBJ_ID_NONE;
            Info.Pad0e = 0u;
            Info.Position.x = 0.0f;
            Info.Position.y = 0.0f;
            Info.Position.z = 0.0f;
            Info.Velocity.x = 0.0f;
            Info.Velocity.y = 0.0f;
            Info.Velocity.z = 0.0f;
            Info.AngularVelocity.x = 0.0f;
            Info.AngularVelocity.y = 0.0f;
            Info.AngularVelocity.z = 0.0f;
            Info.OwnerPlayer = 0u;
            ServerCreateProjectileInfo_Fill(&Info, static_cast<CProjectile*>(Raw));

            // SendServerCreateCounter and SendServerCreateGuided take the player id by
            // reference (&View->PlayerId, as the notifier FUN_06cf6bc0 also passes); the
            // other create slots take a value.
            g_pClientInterface->SendServerCreateCounter(this->PlayerId, Info);
        } else if (bIsMine) {
            // CREATEMINE, IClient +0x0c4: the same preparation and builder (FUN_06d07c80)
            // as the countermeasure, but a by-value player id.
            FServerCreateProjectileInfo Info;
            Info.ObjId = 0u;
            Info.ArchetypeNickname = 0u;
            Info.OwnerId = 0u;
            Info.SubObjId = SUBOBJ_ID_NONE;
            Info.Pad0e = 0u;
            Info.Position.x = 0.0f;
            Info.Position.y = 0.0f;
            Info.Position.z = 0.0f;
            Info.Velocity.x = 0.0f;
            Info.Velocity.y = 0.0f;
            Info.Velocity.z = 0.0f;
            Info.AngularVelocity.x = 0.0f;
            Info.AngularVelocity.y = 0.0f;
            Info.AngularVelocity.z = 0.0f;
            Info.OwnerPlayer = 0u;
            ServerCreateProjectileInfo_Fill(&Info, static_cast<CProjectile*>(Raw));

            g_pClientInterface->SendServerCreateMine(PlayerId, Info);
        } else if (bIsGuided) {
            // CREATEGUIDED, IClient +0x0c8: the 0x38-byte record filled by FUN_06d07d40.
            // Retail pre-initialises nothing; the builder writes every field except the
            // two 2-byte pads, which the port zeroes.
            CGuided* const Guided = static_cast<CGuided*>(Raw);
            FServerCreateGuidedInfo Info;
            Info.Pad26 = 0u;
            Info.Pad36 = 0u;
            ServerCreateGuidedInfo_Fill(&Info, Guided);

            // By-reference player id, as for CREATECOUNTER.
            g_pClientInterface->SendServerCreateGuided(this->PlayerId, Info);

            // One SETTARGET for the missile's own target (CGuided+0xb0 id, +0xf8
            // sub-target). Retail loads the sub-target before the null test.
            IObjRW* const Target = static_cast<IObjRW*>(Guided->get_target());
            if (Target != nullptr) {
                XSetTarget Msg;
                Msg.ObjectId = Guided->Id;
                Msg.SubObjId = SUBOBJ_ID_NONE;
                Msg.Pad_0x06 = 0u;
                Msg.TargetObjectId = Target->get_id();
                Msg.TargetSubObjId = Guided->SubTarget;
                Msg.Pad_0x0e = 0u;
                // By-value player id here.
                g_pClientInterface->SendCommonSetTarget(PlayerId, Msg);
            }
        }

        // Whatever branch ran, including none matching (LAB_06cf5bff), the record
        // is marked created and its simulation reset.
        Record->bCreatedOnClient = true;
        Created.push_back(Record);
        Record->Simulation.Reset();
    }

    // -----------------------------------------------------------------------
    // Phase 3 -- for everything the client already has: destroy it on the client
    // when it went out of view, otherwise ask the simulation whether it needs a resync.
    // -----------------------------------------------------------------------
    for (std::list<SystemTrackedObject*>::iterator It = Created.begin();
         It != Created.end(); ++It) {
        if (!HasBandwidth(PlayerId))
            return;

        // No null test on the record or its object anywhere in this phase.
        SystemTrackedObject* const Record = *It;
        GameObject* const Obj = Record->Object;

        if (!should_be_visible(Obj)) {
            // Clearing the flag lets the next frame's phase 2 create it again.
            FServerDestroyObjectInfo Msg;
            Msg.ObjectId = Obj->get_id();
            Msg.DestroyType = 0u;               // retail stores a dword 0 at +0x04
            Msg.Pad_0x05[0] = 0u;
            Msg.Pad_0x05[1] = 0u;
            Msg.Pad_0x05[2] = 0u;
            g_pClientInterface->SendServerDestroyObject(PlayerId, Msg);
            Record->bCreatedOnClient = false;
            continue;
        }

        Vector const& Position = Obj->get_position();
        Quaternion Orient;
        Server_MatrixToQuat(Orient, Obj->get_orientation());

        // Only unowned objects are resynced. Retail reads +0xb4 through the CSimple
        // narrowing even when it is null (.text:06cf5d15); the port does the same.
        unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;
        if (OwnerPlayer != 0u)
            continue;

        // Retail tests for exactly 1.
        if (!Record->Simulation.CheckForSync(this->Position, Position, Orient))
            continue;

        // Insert into Pending in descending GetSimulationTime() order (simulation
        // +0x2c0, record +0x2c8), so the record longest without an update is served
        // first from the remaining bandwidth. No slot found appends.
        double const Ours = Record->Simulation.GetSimulationTime();
        std::list<SystemTrackedObject*>::iterator Where = Pending.begin();
        while (Where != Pending.end()) {
            // The neighbour record is read unguarded.
            SystemTrackedObject* const Other = *Where;
            if (Other->Simulation.GetSimulationTime() <= Ours)
                break;
            ++Where;
        }
        Pending.insert(Where, Record);
    }

    // -----------------------------------------------------------------------
    // Phase 4 -- drain Pending into UPDATEOBJECT packets until the gauge fills.
    // -----------------------------------------------------------------------
    for (std::list<SystemTrackedObject*>::iterator It = Pending.begin();
         It != Pending.end(); ++It) {
        if (!HasBandwidth(PlayerId))
            return;

        // Record and object read unguarded.
        SystemTrackedObject* const Record = *It;
        GameObject* const Obj = Record->Object;

        Vector const& Position = Obj->get_position();
        Quaternion Orient;
        Server_MatrixToQuat(Orient, Obj->get_orientation());

        FCommonUpdateObjectInfo Msg;
        Msg.ObjectId = Obj->get_id();
        Msg.Throttle = 0.0f;         // the non-ship default
        Msg.Pad_0x29[0] = 0u;
        Msg.Pad_0x29[1] = 0u;
        Msg.Pad_0x29[2] = 0u;
        Msg.Orientation = Orient;
        Msg.Position = Position;
        // The observer clock crosses the wire narrowed to a float.
        Msg.Timestamp = static_cast<float>(ElapsedTime);
        // Only the ship path replaces this.
        Msg.State = 0xffu;

        CObject* const Raw = Obj->cobj();
        CShip* const Ship =
            (Raw != nullptr &&
             (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                ? static_cast<CShip*>(Raw) : nullptr;
        if (Ship != nullptr) {
            Msg.Throttle = Ship->Throttle;   // CShip+0x29c
            // get_behavior_id (vt+0x148), stored as a byte.
            Msg.State =
                static_cast<unsigned char>(Obj->get_behavior_id());
        }

        g_pClientInterface->SendCommonUpdateObject(PlayerId, Msg);

        // The same sample goes to the record's own simulation (CRemotePhysicsSimulation
        // vt+4) as (position, quaternion, ElapsedTime). The decompiler shows the
        // orientation as arg 0; a stack-adjusted read at .text:06cf5fdf makes it the
        // position. The quaternion is the one the packet carries; the clock goes as a
        // double here, unlike the float on the wire.
        Record->Simulation.RemoteUpdate(Position, Orient, ElapsedTime);
    }

    // -----------------------------------------------------------------------
    // Phase 5 -- the deferred-damage retry.
    // -----------------------------------------------------------------------
    if (!HasBandwidth(PlayerId))
        return;

    // Collect the pending ids whose objects are visible again.
    std::vector<GameObject*> Retry;
    for (std::set<unsigned int>::const_iterator It = PendingDamageIds.begin();
         It != PendingDamageIds.end(); ++It) {
        // FUN_06d006c0; a failed lookup becomes a null object, rejected here.
        GameObject* const Obj = Server_LookupGameObject(*It);
        if (Obj == nullptr)
            continue;
        if (!should_be_visible(Obj))
            continue;
        Retry.push_back(Obj);
    }

    for (std::vector<GameObject*>::iterator It = Retry.begin();
         It != Retry.end(); ++It) {
        if (!HasBandwidth(PlayerId))
            return;

        GameObject* const Obj = *It;
        if (Obj == nullptr)
            continue;

        // Retail builds the DamageList in place: +0x0c = 0, +0x10 cause = 0x19,
        // +0x14 = 0, +0x18 = 0.
        DamageList Damage;
        Damage.set_destroyed(false);
        // Retail's DmgCauseToString returns "???" for every cause (.text:062bbce0); the
        // enumerator name is the port's (common_misc.h).
        Damage.set_cause(DamageCause::InitialSync);
        Damage.set_inflictor_id(0u);
        Damage.set_inflictor_owner_player(0u);

        // vt+0x1a8; the pushed 0x40000000 is the float 2.0f, not a bitmask (see
        // GameObject::get_damage_list in server_gameobject.h).
        Obj->get_damage_list(Damage, 2.0f);

        if (!Damage.Entries.empty()) {
            g_pClientInterface->SendServerDamageObject(PlayerId, Obj->get_id(), Damage);
        }

        // Retail re-asks get_id(). The erase is unconditional, so an object with no
        // damage to report leaves the queue too.
        PendingDamageIds.erase(Obj->get_id());
    }

    // Retail: FUN_06d07690 builds a DamageList of every shield's hit points
    // (CEquipTraverser 0x100, CEquip vt+0x3c) and destroys it without storing it, so
    // CEqObjCreateInfo's DamageList (+0x8c) crosses the wire empty from all three callers
    // (.text:06cf588a, .text:06d0843e, .text:06d0d256). A joining client gets item health
    // from the record's EquipDesc vector (+0x70, get_equip_desc_list) and group health
    // from its CollisionGroupDesc list (+0x80); shield hit points arrive only with the
    // object's next DAMAGEOBJECT. Solars are re-synced by StarSystem::add_player
    // (cause 0x19, .text:06d07f56) and by the deferred-damage retry above.
}

// ===========================================================================
// FUN_06d4f430 -- PlayerData::set_collision_group_health. Health is the
// group's relative health (sole caller .text:06d0ba27 passes
// CArchGroup::GetRelativeHealth rounded to float).
// ===========================================================================
void PlayerData::set_collision_group_health(unsigned short GroupId, float Health)
{
    for (std::list<CollisionGroupDesc>::iterator It = collision_groups.begin();
         It != collision_groups.end(); ++It) {
        if (It->Id == GroupId) {
            It->Health = Health;
            return;
        }
    }
    // Retail falls off the end of the walk with no diagnostic at all.
}

// ===========================================================================
// FUN_06d0b800 -- StarSystem::damage_object(IObjRW*, DamageList&). Three callers,
// all thiscall on a StarSystem: .text:06ceb4bf, .text:06ceef18, .text:06cf28bb.
// Retail reuses the incoming param_1 stack slot as scratch for the owner id it hands
// to PlayerDB::is_valid, and the allocator byte read from the param_2 slot is an
// MSVC6 allocator<> temporary with no value.
// ===========================================================================
void StarSystem::damage_object(GameObject* Obj, DamageList* Damage)
{
    // No null test on the system, the object or the damage list.

    if (Damage->Entries.empty())
        return;

    unsigned int const ObjId = Obj->get_id();

    // -----------------------------------------------------------------------
    // Part 1 -- mirror the damage into the owning player's PlayerData, only when
    // the damaged object is that player's current ship.
    // -----------------------------------------------------------------------
    // Read through the CSimple narrowing with no null test.
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;

    CObject* const Raw = Obj->cobj();
    // 0x103 is common to ClassSolar, ClassShip and ClassLoot and absent from
    // ClassAsteroid: "is a CEqObj".
    CEqObj* const EqObj =
        (Raw != nullptr &&
         (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
            ? static_cast<CEqObj*>(Raw) : nullptr;

    bool const bMirror =
        Players.is_valid(OwnerPlayer) &&
        Players[OwnerPlayer].ship_object_id == ObjId &&
        EqObj != nullptr;

    if (bMirror) {
        PlayerSlot& Slot = Players[OwnerPlayer];
        for (std::list<DamageEntry>::const_iterator It = Damage->Entries.begin();
             It != Damage->Entries.end(); ++It) {
            DamageEntry const& Entry = *It;

            if (SubObjectID::IsEquipID(Entry.SubObjId)) {
                // DamageEntry::Fate (entry +0x08): still there, or gone.
                if (Entry.Fate == DamageEntry::SubObjFate::None) {
                    // The live equip describes itself into the player's own list.
                    EquipDesc Desc;
                    CEquip* const Equip =
                        EqObj->EquipManager.FindByID(Entry.SubObjId);
                    if (Equip == nullptr)
                        continue;
                    // CEquip::GetEquipDesc (vt+0x28).
                    if (!Equip->GetEquipDesc(Desc))
                        continue;
                    // The fifth argument is Desc+0x0c (bEquipped), so damage keeps the
                    // mount state; retail's sixth argument is an unused zero, omitted.
                    Slot.modify_equipment(Desc.get_id(), Desc.get_hardpoint(), Desc.get_count(),
                                          Desc.get_status(), Desc.is_equipped());
                } else {
                    Slot.remove_equipment(Entry.SubObjId, -1,
                                          /*bNotify*/false);
                }
            } else if (SubObjectID::IsArchGroupID(Entry.SubObjId)) {
                // A collision group stores the group's current relative health
                // (CArchGroup::GetRelativeHealth, rounded to float), not the entry's
                // hit points.
                CArchGroup* const Group =
                    EqObj->ArchGroups.FindByID(Entry.SubObjId);
                if (Group == nullptr)
                    continue;
                Slot.set_collision_group_health(Entry.SubObjId,
                                                (float)Group->GetRelativeHealth());
            } else if (Entry.SubObjId == SUBOBJ_ID_ROOT) {
                // The hull lands in relative_health.
                Slot.set_relative_health(EqObj->get_relative_health(),
                                         /*bNotify*/false);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Part 2 -- build the reduced list and tell every player in this system.
    // -----------------------------------------------------------------------

    // Empty; the cause (+0x10) stays 0 here, unlike the 0x19 the observer update writes.
    DamageList Reduced;

    // Only a solar (ClassSolar, 0x303) gets a reduced list: the entries whose
    // sub-object was actually destroyed or separated.
    bool const bIsSolar =
        Raw != nullptr &&
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar;
    if (bIsSolar) {
        for (std::list<DamageEntry>::const_iterator It = Damage->Entries.begin();
             It != Damage->Entries.end(); ++It) {
            if (It->Fate != DamageEntry::SubObjFate::None)
                Reduced.Entries.push_back(*It);
        }
        Reduced.Count = static_cast<int>(Reduced.Entries.size());
    }

    // Fan out over StarSystem::PlayerListHead (+0xc); each node's Observer is
    // embedded at node+0x08 (an address, not a load). No null test on the head or a
    // node; the walk stops only at the sentinel.
    SystemPlayerNode* const Head =
        reinterpret_cast<SystemPlayerNode*>(PlayerListHead);
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next) {
        Node->View.damage_object(ObjId, Damage, &Reduced);
    }
}

// ===========================================================================
// FUN_06cf4f40 (thiscall on the list) -- update every object (vt+0x16c); one that
// answers false is unlinked (MetaList::remove inlined) and appended to Deferred
// (FUN_06d11850 is Deferred's alloc_node). No null test on Data.
// ===========================================================================
void ObjectList::tick(float Dt, ObjectList* Deferred)
{
    FSystemListNode* Node = nullptr;
    while (traverse(Node, true))
    {
        if (Node->Data->update(Dt))
            continue;
        GameObject* const Data = Node->Data;
        remove(Node);
        Deferred->append(Data);
    }
}
