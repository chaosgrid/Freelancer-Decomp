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

// FObserverTrackedList -- Observer+0x08, retail's std::list<SystemTrackedObject*>. See
// server_sysdb.h.

// Inside FUN_06cf51e0.
FObserverTrackedList::FObserverTrackedList()
{
    // Retail's allocator byte is a leftover argument byte with no value; the port writes 0.
    Alloc = 0;
    SystemTrackedNode* const Sentinel = new SystemTrackedNode;
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    Head = Sentinel;
    Size = 0;
}

// FUN_06cf51e0 -- Observer's constructor; members in retail's order. +0x04 and +0x2C are
// not written, in retail or here.
Observer::Observer(unsigned int InPlayerId)
    : Tracked()
    , PendingDamageIds()
{
    bShipGone = 0;
    ElapsedTime = 0.0;
    PlayerId = InPlayerId;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
}

// Inside FUN_06d0f6d0.
FObserverTrackedList::FObserverTrackedList(FObserverTrackedList const& Source)
{
    Alloc = Source.Alloc;
    SystemTrackedNode* const Sentinel = new SystemTrackedNode;
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    Head = Sentinel;
    Size = 0;

    // The record pointers are copied: the RECORD is shared, not duplicated.
    SystemTrackedNode* const SrcSentinel = Source.Head;
    for (SystemTrackedNode* Src = SrcSentinel->Next; Src != SrcSentinel; Src = Src->Next) {
        SystemTrackedNode* const Node = new SystemTrackedNode;
        Node->Next = Sentinel;
        Node->Prev = Sentinel->Prev;
        Sentinel->Prev = Node;
        Node->Prev->Next = Node;
        Node->Data = Src->Data;
        ++Size;
    }
}

// FUN_06d0f6d0 -- Observer's copy constructor, compiled as std::list's _Construct (a
// placement new). Callers: list::insert (StarSystem::add_player) and StarSystem's copy
// constructor (FUN_06d109c0). +0x04 and +0x2c are not written.
Observer::Observer(Observer const& Source)
    : Tracked(Source.Tracked)
    , PendingDamageIds(Source.PendingDamageIds)
{
    bShipGone = Source.bShipGone;
    ElapsedTime = Source.ElapsedTime;
    PlayerId = Source.PlayerId;
    Position = Source.Position;
}

// The tail of FUN_06cf52d0, MSVC6's ~list(). ~Observer's body has already emptied the
// list, so the walk finds nothing, in retail as here.
FObserverTrackedList::~FObserverTrackedList()
{
    SystemTrackedNode* const Sentinel = Head;
    SystemTrackedNode* Node = Sentinel->Next;
    while (Node != Sentinel) {
        SystemTrackedNode* const Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        --Size;
        Node = Next;
    }
    delete Sentinel;
    Head = nullptr;
    Size = 0;
}

// FUN_06cf52d0 -- ~Observer. Its scalar deleting destructor (FUN_06cf52b0) is vtable slot
// 0; every retail dispatch passes flag 0.
Observer::~Observer()
{
    // No null test on the list head; both constructors always allocate the sentinel.
    SystemTrackedNode* const Sentinel = Tracked.Head;
    SystemTrackedNode* Node;
    // Pass 1: every RECORD (its CRemotePhysicsSimulation destroyed with it).
    for (Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        delete Node->Data;
    }
    // Pass 2: every node erased.
    Node = Sentinel->Next;
    while (Node != Sentinel) {
        SystemTrackedNode* const Next = Node->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        --Tracked.Size;
        Node = Next;
    }
    // ~PendingDamageIds, then ~Tracked, follow.
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
        bool const bIsLoot = (Raw != nullptr) && (Cls & CObject::ClassLoot) == CObject::ClassLoot;
        bool const bIsShip = (Raw != nullptr) && (Cls & CObject::ClassShip) == CObject::ClassShip;
        bool const bIsCounter = (Raw != nullptr) && (Cls & CObject::ClassCounterMeasure) == CObject::ClassCounterMeasure;
        bool const bIsMine = (Raw != nullptr) && (Cls & CObject::ClassMine) == CObject::ClassMine;
        bool const bIsGuided = (Raw != nullptr) && (Cls & CObject::ClassGuided) == CObject::ClassGuided;

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

// FUN_06cf65b0 (thiscall on the view); only caller StarSystem::sp_obj_update. The update
// goes to the client only once the object was created there, but the simulation is
// always fed.
void Observer::send_object_update(FCommonUpdateObjectInfo const* Rec)
{
    if (SinglePlayer())
        return;

    for (SystemTrackedNode* Node = this->Tracked.Head->Next; Node != this->Tracked.Head; Node = Node->Next)
    {
        SystemTrackedObject* const Tracked = Node->Data;
        if (Tracked->Object->get_id() != Rec->ObjectId)
            continue;

        if (Tracked->bCreatedOnClient)
            ::g_pClientInterface->SendCommonUpdateObject(PlayerId, *Rec);

        Tracked->Simulation.RemoteUpdate(Rec->Position, Rec->Orientation,
                                         static_cast<double>(Rec->Timestamp));
        return;
    }
}

// ===========================================================================
// The five "create this object on that player's client" notifiers. Declared in
// server_sysdb.h. Retail's source file for all five is Observer.cpp (.rdata:06d669dc),
// not StarSys.cpp; RTTI .?AVObserver@@ names the class.
// ===========================================================================

static char const kObserverCppPath[] =
    "E:\\FL\\Scratch\\Source\\Server\\space\\Observer.cpp";

// FUN_06cf6630 -- CREATESHIP. The odd one out: it does NOT consult ShouldBeVisible or set
// bCreatedOnClient, and sends only in SINGLE player (consistent, since AddTrackedObject
// seeds the flag from SinglePlayer()). In multiplayer it only registers the ship and lets
// the per-frame range check send the create later.
void Observer::create_ship_for_player(CShipCreateInfo* Info, GameObject* Obj)
{
    // No test of View or Obj, nor on the (& 3) narrowing; every object here is a ship.
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;

    // A player is never told to create their own ship.
    if (OwnerPlayer == PlayerId)
        return;

    if (bShipGone != 0)
        return;

    if (Obj->is_destroyed()) {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Ship(%d) 'dead' before creation packet sent.",
              kObserverCppPath, 0x19b, Obj->get_id());
        return;
    }

    // The record is discarded here, unlike in the other four.
    add_tracked_object(Obj);

    if (!SinglePlayer())
        return;

    // Raw slot, no is_valid; the same value goes into CREATESOLAR's payload.
    Info->VibeOtherGroupId = Players[PlayerId].rep_vibe;

    // The player id goes by VALUE.
    g_pClientInterface->SendServerCreateShip(PlayerId, *Info);
}

// FUN_06cf66f0 (thiscall on the view). A dynamic solar is sent even out of range. Unlike
// the other notifiers a solar never gets a tracked record.
void Observer::create_solar_for_player(CSolarCreateInfo* Info, GameObject* Obj)
{
    if (bShipGone != 0)
        return;
    if (!should_be_visible(Obj) && Info->bDynamic == 0)
        return;
    g_pClientInterface->SendServerCreateSolar(PlayerId, *Info);
}

// FUN_06cf6730 (thiscall on the view): the ship's equipment list, only for a tracked
// object already created on the client. NO null test on the CShip narrowing: retail calls
// through it.
void Observer::send_remote_cargo_update(GameObject* Obj)
{
    SystemTrackedNode* const Head = Tracked.Head;
    SystemTrackedNode* Node = Head->Next;
    for (; Node != Head; Node = Node->Next)
    {
        if (Node->Data->Object == Obj)
            break;
    }
    if (Node == Head)
        return;
    if (!Node->Data->bCreatedOnClient)
        return;

    EquipDescVector Items;
    CObject const* const Cobj = Obj->cobject();
    CShip const* Ship = nullptr;
    if (Cobj != nullptr && (static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
        Ship = static_cast<CShip const*>(Cobj);
    Ship->get_equip_desc_list(Items);
    g_pClientInterface->SendServerRemoteObjectCargoUpdate(PlayerId, Ship->Id, Items, 1u);
}

// The mask FUN_06cf6820's third exit tests get_type_bits against: every OBJECT_TYPE bit
// below 0x10000 except WAYPOINT (0x200), per Common.dll's DecodeType table
// (.data:063ec810). So it reads "a solar, not a waypoint": static system furniture the
// client builds from the system ini, which is why retail still sends the destroy for an
// object never in this player's tracked list. Ships, projectiles, mines, loot and
// asteroids stay silent.
static unsigned int const kSolarTypeBits = OBJ_SOLAR_NO_WAYPOINT_MASK;

// FUN_06cf6820. Returns nothing in retail; the bool says whether the record was found and
// dropped, which the callers ignore.
bool Observer::on_object_destroyed(GameObject* Obj, int Kind)
{
    // No null test on View or Obj.

    // Built BEFORE the search and reused on every exit. Retail stores Kind as a whole
    // dword, so its upper three bytes land in the pad.
    FServerDestroyObjectInfo Wire;
    Wire.ObjectId = Obj->get_id();
    unsigned int const KindDword = (unsigned int)Kind;
    Wire.DestroyType = static_cast<unsigned char>(KindDword & 0xffu);
    Wire.Pad_0x05[0] = static_cast<unsigned char>((KindDword >> 8) & 0xffu);
    Wire.Pad_0x05[1] = static_cast<unsigned char>((KindDword >> 16) & 0xffu);
    Wire.Pad_0x05[2] = static_cast<unsigned char>((KindDword >> 24) & 0xffu);

    SystemTrackedNode* const Node = find_by_id(Wire.ObjectId);

    if (Node == nullptr) {
        // Not tracked. Retail still tells the client when the object belonged to this very
        // player (OwnerPlayer read through the narrowing with no null test)...
        unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;
        if (OwnerPlayer == PlayerId) {
            g_pClientInterface->SendServerDestroyObject(PlayerId, Wire);
            return false;
        }

        // ...or when it is a solar other than a waypoint (kSolarTypeBits): those exist on
        // the client whether or not this player ever tracked them.
        if ((Obj->get_type_bits() & kSolarTypeBits) != 0) {
            g_pClientInterface->SendServerDestroyObject(PlayerId, Wire);
        }
        return false;
    }

    // Tracked: notify only when the client was ever told the object exists. The record is
    // read with no null test here; the only null test is the one before the free below.
    SystemTrackedObject* const Record = Node->Data;
    if (Record->bCreatedOnClient) {
        g_pClientInterface->SendServerDestroyObject(PlayerId, Wire);
    }

    // delete runs ~CRemotePhysicsSimulation, as FUN_06cf6940 does before retail frees the
    // record.
    if (Record != nullptr) {
        delete Record;
        Node->Data = nullptr;
    }

    Node->Prev->Next = Node->Next;
    Node->Next->Prev = Node->Prev;
    delete Node;
    Tracked.Size--;
    return true;
}

// FUN_06cf6950 -- the per-player DAMAGEOBJECT gate, and the only writer of the deferred-
// damage set at View+0x14. The send takes `(PlayerId, ObjId, record)`, where `record` is
// the PRIMARY list on the visible path and the DEFERRED one on the deferred-but-non-empty
// path -- two arguments reaching one PUSH, which Ghidra's `piVar7` hides.
void Observer::damage_object(unsigned int ObjId, DamageList* PrimaryRecord,
                             DamageList* DeferredRecord)
{
    // No null test on View.
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    if (Obj == nullptr)
        return;

    // Solar only ((& 0x303), composed with cobj()'s (& 3)).
    CObject* const Raw = Obj->cobj();
    CSimple* Solar = nullptr;
    if (Raw != nullptr &&
        (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar) {
        Solar = Obj->csimple();
    }

    // Visible, or a tradelane ring (TypeBits 0x80): a ring's damage state has to reach the
    // client whether or not it is in range.
    bool bSend = is_object_visible(Obj);
    DamageList* Record = PrimaryRecord;
    if (!bSend && Solar != nullptr && (Solar->TypeBits & OBJ_TRADELANE_RING) != 0)
        bSend = true;

    if (!bSend) {
        // In single player there is nothing to defer TO.
        if (SinglePlayer())
            return;
        // A non-solar is dropped outright.
        if (Solar == nullptr)
            return;
        if (DeferredRecord->Count != 0) {
            // The caller's second record is already populated, so send that one instead of
            // remembering the id.
            Record = DeferredRecord;
            bSend = true;
        } else {
            PendingDamageIds.insert(ObjId);
            return;
        }
    }

    // The player id goes by VALUE.
    g_pClientInterface->SendServerDamageObject(PlayerId, ObjId, *Record);
}

// FUN_06cf6ac0 (thiscall on the view, 21 stack dwords; the Costume by value). Outside
// single player both sender and receiver must be visible (SinglePlayer() is called twice).
// Retail sets only the low byte of the own-ship flag; the marshaller reads & 0xff, so the
// port passes 0/1.
void Observer::send_comm(unsigned int SenderId, unsigned int ReceiverId, unsigned int VoiceId,
                         Costume const& Cost, unsigned int CaptionIds, unsigned int* TextIds,
                         int TextCount, unsigned int PermutationIds, float Duration)
{
    if (!SinglePlayer()) {
        GameObject* SenderObj = nullptr;
        StarSystem* SenderSystem = nullptr;
        if (!Server_FindObject(SenderId, SenderObj, SenderSystem))
            SenderObj = nullptr;
        if (!is_object_visible(SenderObj))
            return;
    }
    if (!SinglePlayer()) {
        GameObject* ReceiverObj = nullptr;
        StarSystem* ReceiverSystem = nullptr;
        if (!Server_FindObject(ReceiverId, ReceiverObj, ReceiverSystem))
            ReceiverObj = nullptr;
        if (!is_object_visible(ReceiverObj))
            return;
    }

    bool const bReceiverIsOwnShip =
        ReceiverId == Players[PlayerId].ship_object_id;
    static_assert(sizeof(Costume) == 13 * sizeof(unsigned int), "Costume is 13 dwords");
    g_pClientInterface->SendServerSendComm(PlayerId, SenderId, ReceiverId, VoiceId,
                                           Cost, CaptionIds, TextIds, TextCount, PermutationIds, Duration,
                                           bReceiverIsOwnShip);
}

// FUN_06cf6bc0 -- CREATECOUNTER.
void Observer::create_counter_for_player(void* Payload, GameObject* Obj)
{
    // No null test on View or Obj.
    if (bShipGone != 0)
        return;

    if (Obj->is_destroyed()) {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: CounterMeasure(%d) 'dead' before creation packet sent.",
              kObserverCppPath, 0x23c, Obj->get_id());
        return;
    }

    // The record is KEPT this time.
    SystemTrackedObject* const Record = add_tracked_object(Obj);

    // Outside single player the object has to be in range before the client hears of it.
    if (!SinglePlayer() && !should_be_visible(Obj))
        return;

    Record->bCreatedOnClient = true;

    // This slot takes the ADDRESS of the player id (const unsigned int&), unlike the
    // ship / loot / mine slots.
    g_pClientInterface->SendServerCreateCounter(
        PlayerId, *static_cast<const FServerCreateProjectileInfo*>(Payload));
}

// FUN_06cf6c50 -- CREATELOOT. The only notifier that tracks MORE THAN ONE object per call:
// it walks [First, Last) and sends one packet afterwards. No null tests on View, the
// elements or the payload.
void Observer::create_loot_for_player(FServerCreateLootRequest* Payload, GameObject* const* First,
                                      GameObject* const* Last)
{
    if (bShipGone != 0)
        return;

    // An empty range skips straight to the send; it does not return.
    for (GameObject* const* It = First; It != Last; ++It) {
        GameObject* const Obj = *It;

        // Retail ABANDONS the whole call on the first dead entry (not a `continue`), and
        // at ERROR level, unlike the other three.
        if (Obj->is_destroyed()) {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: Loot(%d) 'dead' before creation packet sent.",
                  kObserverCppPath, 0x256, Obj->get_id());
            return;
        }

        // Stored INSIDE the loop, so the last element wins -- retail's own behaviour; do
        // not hoist it.
        Payload->bMarkedBy = Obj->is_marked_by(PlayerId) ? 1u : 0u;

        add_tracked_object(Obj);
    }

    // The loot packet goes out ONLY in single player; there is no ShouldBeVisible arm and
    // bCreatedOnClient is left to AddTrackedObject's seeding.
    if (!SinglePlayer())
        return;

    g_pClientInterface->SendServerCreateLoot(PlayerId, *Payload);
}

// FUN_06cf6cf0 -- CREATEGUIDED. Takes the loot one's vector but uses ONLY its first element.
void Observer::create_guided_for_player(void* Payload, GameObject* const* First,
                                        GameObject* const* Last)
{
    // No null test on View, the element or the record; the null-first and empty-range
    // exits are retail's.
    if (bShipGone != 0)
        return;

    if (First == nullptr || First == Last)
        return;

    GameObject* const Obj = *First;

    // No is_destroyed() guard and no FDUMP in this one -- the only member of the family
    // without them. Do not add one.
    SystemTrackedObject* const Record =
        add_tracked_object(Obj);

    if (!SinglePlayer() && !should_be_visible(Obj))
        return;

    Record->bCreatedOnClient = true;

    // Like the countermeasure slot, this one takes the ADDRESS of the player id.
    g_pClientInterface->SendServerCreateGuided(
        PlayerId, *static_cast<const FServerCreateGuidedInfo*>(Payload));
}

// FUN_06cf6d60 -- CREATEMINE: the countermeasure one with a different FDUMP line and slot,
// and the player id passed BY VALUE.
void Observer::create_mine_for_player(void* Payload, GameObject* Obj)
{
    if (bShipGone != 0)
        return;

    if (Obj->is_destroyed()) {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Mine(%d) 'dead' before creation packet sent.",
              kObserverCppPath, 0x28a, Obj->get_id());
        return;
    }

    SystemTrackedObject* const Record =
        add_tracked_object(Obj);

    if (!SinglePlayer() && !should_be_visible(Obj))
        return;

    Record->bCreatedOnClient = true;

    g_pClientInterface->SendServerCreateMine(
        PlayerId, *static_cast<const FServerCreateProjectileInfo*>(Payload));
}

// FUN_06cf6df0 -- EXPLODEASTEROIDMINE; only caller StarSystem::send_mine_explosion.
// Neither the IClient pointer nor the record is tested.
void Observer::send_mine_explosion(FMineExplosion* Rec)
{
    unsigned int const MineId = Rec->MineId;
    if (!SinglePlayer()) {
        GameObject* const Obj = Server_LookupGameObject(MineId);
        if (!is_object_visible(Obj))
            return;
    }
    g_pClientInterface->SendServerExplodeAsteroidMine(PlayerId, *Rec);
}

// FUN_06cf6e60 -- "this object has left the system": the tracked-list drop, the
// deferred-damage set CLEAR, and the bShipGone latch.
void Observer::on_object_left_system(unsigned int const* ObjId, unsigned int const* Destination)
{
    // No null test on View, ObjId or Destination, nor on the sentinel, nodes, records or
    // their objects. The first hit ends the walk; +0x2f0 is read before the record's null
    // test, and size-- is unconditional.
    bool bWasCreatedOnClient = false;
    SystemTrackedNode* const Sentinel = Tracked.Head;
    for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        SystemTrackedObject* const Record = Node->Data;
        if (Record->Object->get_id() != *ObjId)
            continue;
        bWasCreatedOnClient = (Record->bCreatedOnClient != false);
        delete Record;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        --Tracked.Size;
        break;
    }

    PendingDamageIds.clear();

    // The latch: the player's own ship (raw slot, no is_valid).
    if (*ObjId == Players[PlayerId].ship_object_id)
        bShipGone = 1;

    // Sent when EITHER the dropped record had been created on the client OR bShipGone is set.
    if (bWasCreatedOnClient || bShipGone != 0) {
        FServerSystemSwitchOutInfo Wire;
        Wire.ShipId = *ObjId;
        Wire.GateId = *Destination;
        g_pClientInterface->SendServerSystemSwitchOut(PlayerId, Wire);
    }
}

// FUN_06cf6fc0 (thiscall on the view) -- "this object has landed". The LAND twin of the
// function above: same tracked-list drop, no set clear, no latch.
void Observer::on_object_landed(unsigned int const* ObjId, unsigned int const* Arg2,
                                unsigned int const* Arg3)
{
    // As above: one hit at most, +0x2f0 read before the null test, size-- unguarded.
    bool bWasCreatedOnClient = false;
    SystemTrackedNode* const Sentinel = Tracked.Head;
    for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        SystemTrackedObject* const Record = Node->Data;
        if (Record->Object->get_id() != *ObjId)
            continue;
        bWasCreatedOnClient = (Record->bCreatedOnClient != false);
        delete Record;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        delete Node;
        --Tracked.Size;
        break;
    }

    // Built before either test.
    FServerLandInfo Wire;
    Wire.ShipId = *ObjId;
    Wire.DockId = *Arg2;
    Wire.BaseId = *Arg3;

    // A record never created on the client only goes out for this player's own ship.
    PlayerData& Owner = Players[PlayerId];
    if (!bWasCreatedOnClient && Wire.ShipId != Owner.ship_object_id)
        return;

    // Outside single player, the player's own ship landing autosaves the slot.
    if (!SinglePlayer() && *ObjId == Owner.ship_object_id)
        Owner.auto_save();                                 // FUN_06d4c430

    g_pClientInterface->SendServerLand(PlayerId, Wire);
}

// FUN_06cf70c0 (thiscall on the view); only caller FUN_06d0f1c0. Tells this view's client
// the group id CPlayerGroup::AddMember / DelMember just stamped on a visible ship.
// Server_LookupGameObject's null covers both of retail's lookup early-outs.
void Observer::send_group_change(unsigned int ObjId)
{
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    if (Obj == nullptr)
        return;
    if (!is_object_visible(Obj))
        return;
    CObject const* const Cobj = Obj->cobject();
    if (Cobj == nullptr)
        return;
    if ((static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return;
    unsigned int const GroupId = static_cast<CShip const*>(Cobj)->GroupId;
    g_pClientInterface->SendServerMiscObjUpdateObjectGroup(PlayerId, ObjId, GroupId);
}

// FUN_06cf7140 -- the only producer the tracked-object list has in retail.
SystemTrackedObject* Observer::add_tracked_object(GameObject* Obj)
{
    // No null test on the view or its list head; every Observer ctor allocates the sentinel.

    // +0x04 is never written (see SystemTrackedObject in server_sysdb.h).
    SystemTrackedObject* const Record = new SystemTrackedObject;
    Record->Object = nullptr;
    Record->bCreatedOnClient = false;

    Record->Object = Obj;

    // Seeded from SinglePlayer(): in single player the client already has everything, so
    // the record counts as created the moment it exists.
    Record->bCreatedOnClient = SinglePlayer();

    SystemTrackedNode* const Sentinel = Tracked.Head;
    SystemTrackedNode* const Node = new SystemTrackedNode;
    Node->Next = Sentinel;
    Node->Prev = Sentinel->Prev;
    Node->Data = Record;
    Sentinel->Prev->Next = Node;
    Sentinel->Prev = Node;

    ++Tracked.Size;

    // Outside single player, prime the simulation with the object's current state.
    // SinglePlayer() is called a SECOND time, not reused from the flag above. The math
    // engine (DAT_06d8d7f8, shared with the observer update) is never null-checked.
    if (!SinglePlayer())
    {
        Vector const& Position = Obj->get_position();          // slot 0
        Transform const& Xform = Obj->get_transform();         // slot 3
        Quaternion Orientation;
        Server_MatrixToQuat(Orientation, Xform.Rotation);
        Record->Simulation.RemoteUpdate(Position, Orientation, ElapsedTime);
    }

    return Record;
}

// ===========================================================================
// The per-player object tracker that hangs off StarSystem+0x0C. Structures are in
// server_sysdb.h.
// ===========================================================================

// FUN_06cf7260's squared view radii: 2500^2, 5000^2 and 10000^2.
static float const kViewRadiusSqUnowned = 6.25e6f;    // .rdata:06d66aec

static float const kViewRadiusSqStranger = 2.5e7f;    // .rdata:06d66af0

static float const kViewRadiusSqGroupMate = 1.0e8f;   // .rdata:06d66af4

// FUN_06cf7260.
bool Observer::should_be_visible(GameObject* Obj)
{
    // A null object and single player are always "visible"; the tracker is a
    // multiplayer-only filter. No test on the view.
    if (Obj == nullptr)
        return true;
    if (SinglePlayer())
        return true;

    // OwnerPlayer is read through the (& 3) narrowing with no test; every tracked object
    // is a CSimple.
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;

    // Your own ship is never filtered.
    if (OwnerPlayer == PlayerId)
        return true;

    Vector const& ObjPos = Obj->get_position();

    // x87: retail keeps the squared distance on the FPU stack across the group test.
    float const Dx = ObjPos.x - Position.x;
    float const Dy = ObjPos.y - Position.y;
    float const Dz = ObjPos.z - Position.z;
    float const DistSq = Dx * Dx + Dy * Dy + Dz * Dz;

    // An object nobody owns gets the tightest radius. Each compare is "visible when
    // DistSq <= radius"; a NaN is not visible.
    if (OwnerPlayer == 0)
        return DistSq <= kViewRadiusSqUnowned;

    // Group mates see each other further. Both slots are indexed raw (no is_valid). A group
    // mate outside 10000 falls through to the stranger test, which it then fails as well.
    void const* const MyGroup = Players[PlayerId].group_ptr;
    if (MyGroup != nullptr && MyGroup == Players[OwnerPlayer].group_ptr) {
        if (DistSq <= kViewRadiusSqGroupMate)
            return true;
    }
    return DistSq <= kViewRadiusSqStranger;
}

// FUN_06cf7370. FUN_06d006c0 is Server_LookupGameObject here; its null on a miss is
// exactly what retail's miss path hands on.
bool Observer::is_object_id_visible(unsigned int ObjId)
{
    if (SinglePlayer())
        return true;
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    return is_object_visible(Obj);
}

SystemTrackedNode* Observer::find_by_id(unsigned int ObjId)
{
    SystemTrackedNode* const Sentinel = Tracked.Head;
    for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        if (Node->Data->Object->get_id() == ObjId)
            return Node;
    }
    return nullptr;
}

// Retail's inner-list search, inlined in FUN_06cf73d0 (by object POINTER) and FUN_06cf6820
// (by get_id()). For a live object those agree; the id form is what survives an object
// already being torn down, which is why the destroy path uses it. Neither tests the
// sentinel or the records.
SystemTrackedNode* Observer::find_by_pointer(GameObject* Obj)
{
    SystemTrackedNode* const Sentinel = Tracked.Head;
    for (SystemTrackedNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        if (Node->Data->Object == Obj)
            return Node;
    }
    return nullptr;
}

// FUN_06cf73d0.
bool Observer::is_object_visible(GameObject* Obj)
{
    if (Obj == nullptr)
        return true;
    if (SinglePlayer())
        return true;

    // Your own ship again; the (& 3) narrowing is read through unguarded.
    if (Obj->csimple()->OwnerPlayer == PlayerId)
        return true;

    // Already tracked: the record's "created on the client" byte, verbatim.
    SystemTrackedNode* const Node = find_by_pointer(Obj);
    if (Node != nullptr)
        return Node->Data->bCreatedOnClient;

    return should_be_visible(Obj);
}

// FUN_06cf7440. Only caller FUN_06d0f190 below (IServerImpl::SubmitChat's local chat).
void Observer::collect_created_owners(std::set<unsigned int>* Owners)
{
    for (SystemTrackedNode* Node = Tracked.Head->Next; Node != Tracked.Head;
         Node = Node->Next) {
        SystemTrackedObject* const Record = Node->Data;
        GameObject* const Obj = Record->Object;
        if (Obj == nullptr)
            continue;
        // OwnerPlayer read with NO null check on the (& 3) narrowing, as in retail.
        unsigned int const Owner = Obj->csimple()->OwnerPlayer;
        if (Owner == 0)
            continue;
        if (!Record->bCreatedOnClient)
            continue;
        Owners->insert(Owner);
    }
}
