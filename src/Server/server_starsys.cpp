// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/common_net.h"
#include "../include/common_physys.h"
#include "../include/common_ivp_real_object.h" // IVP_Real_Object +0x1c/+0xa4 (StarSystem_MpCollisionFilter)
#include "../include/common_cequip.h" // TractorArm
#include "../include/common_casteroid.h" // CmnAsteroid::CAsteroidSystem (SrvAsteroidSystem AddRef/Release/get_sys_id)
#include "../include/common_id.h"
#include "../include/common_server_playerdb.h"
#include "../include/common_ini.h"
#include "../include/common_misc.h"
#include "../include/common_universe.h"
#include "../include/common_reputation.h"
#include "../include/server_sysdb.h"
#include "../include/server_gameobject.h" // GameObject's +0x154..+0x160 slots (StarSystem::remove_player/unload)
#include "../include/common_cobjdb.h"  // IObjDB::GetZoneDb (SrvAsteroid_UpdateSystem, FUN_06d03480)
#include "../include/DALib.h"          // DALib::Data (SrvAsteroid's GetFileSysPath calls)
#include "../include/server_createinfo.h"
#include "../include/rpc_iclient.h"
#include "../include/rpc_iclient_args.h" // FServerSetReputationInfo / FPayloadServerActivateObject (add_player MP walk)
#include "server_internal.h"
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <new>          // placement new: Observer is constructed into a raw
                        // player-list node, as std::list's _Construct does
#include <algorithm>
#include "../include/server_iserverimpl.h"   // IServerImpl, Server
#include "../include/common_server_controller.h"
#include "../include/common_animdb.h"     // AnimDB::Update (FUN_06d10320)

// The IClient / IChatClient halves of rpclocal's Client object (.data:DAT_06d8fb14 /
// DAT_06d8fb18), defined in IServerImpl.cpp.
extern IClient* g_pClientInterface;

// LAB_06d06a40 -- SrvTractor's vtable slot 0 (.rdata:06d65544).
void SrvTractor::notify(Event Code, void* Param)
{
    // Event 0x18: TractorArm::Update's consume timer ran out; Param is the TractorArm.
    if (Code == TractorArmAcquireTimeUp)
    {
        CLoot* const Target = static_cast<TractorArm*>(Param)->GetTarget();
        if (Target != nullptr)
        {
            // The owner's CShip, not null-tested before the call.
            CObject* const Raw = (Owner != nullptr) ? Owner->Object : nullptr;
            CShip* const ShipCobj =
                (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                    ? static_cast<CShip*>(Raw) : nullptr;

            int const Space = ShipCobj->get_space_for_cargo_type(Target->ContentsArch);
            if (Space != 0)
            {
                // The mask only matches Loot::get_type_bits.
                GameObject* const LootObj = Owner->OwnerSystem->find_object(Target->Id, OBJ_LOOT);
                if (LootObj != nullptr && !LootObj->is_destroyed())
                {
                    Owner->OwnerSystem->tractor_loot(static_cast<::Loot*>(LootObj), Owner,
                                                     static_cast<unsigned int>(Space), Equip->Id);
                }
            }
        }
    }

    // Always the base body.
    ServerEquip::notify(Code, Param);
}

// LAB_06d06c30 -- the multiplayer collision filter StarSystem::init installs into
// PhySys::m_pCollisionFilter. Collides when either side's client object is (& 0x27) (a
// projectile type). Retail calls GetRoot on both and DISCARDS the results.
static IVP_BOOL __cdecl StarSystem_MpCollisionFilter(IVP_Real_Object* A, IVP_Real_Object* B)
{
    if (SinglePlayer())
        return IVP_TRUE;
    if (A == nullptr)
        return IVP_TRUE;
    if (B == nullptr)
        return IVP_TRUE;

    CObject* ClientA = static_cast<CObject*>(A->client_data);
    if (ClientA == nullptr && A->controller_phantom != nullptr)
        return IVP_TRUE;
    if (B->client_data == nullptr && B->controller_phantom != nullptr)
        return IVP_TRUE;

    if (ClientA != nullptr)
        GetRoot(ClientA);
    CObject* ClientB = static_cast<CObject*>(B->client_data);
    if (ClientB != nullptr)
        GetRoot(ClientB);

    if (ClientA != nullptr && (static_cast<unsigned int>(ClientA->ObjectClass) & 0x27u) != 0x27u)
        ClientA = nullptr;
    if (ClientB != nullptr && (static_cast<unsigned int>(ClientB->ObjectClass) & 0x27u) != 0x27u)
        ClientB = nullptr;

    if (ClientA != nullptr || ClientB != nullptr)
        return IVP_TRUE;
    return IVP_FALSE;
}

// ===========================================================================
// StarSystem's constructors, destructor and SysDB init.
// ===========================================================================

// FUN_06d0f3e0(0, 0): the player list's self-linked sentinel. MSVC6's _Buynode allocates a
// full node (0x50 in retail; sizeof here, since the port's Observer embeds a CRT-sized
// std::set).
static SystemPlayerNode* StarSystem_NewPlayerSentinel()
{
    SystemPlayerNode* const Node =
        static_cast<SystemPlayerNode*>(::operator new(sizeof(SystemPlayerNode)));
    Node->Next = Node;
    Node->Prev = Node;
    return Node;
}

// The dock queue's self-linked 0x20-byte sentinel (also FUN_06d10aa0 in the copy).
static SystemDockNode* StarSystem_NewDockSentinel()
{
    SystemDockNode* const Node =
        static_cast<SystemDockNode*>(::operator new(sizeof(SystemDockNode)));
    Node->Next = Node;
    Node->Prev = Node;
    return Node;
}

// FUN_06d06ce0. The ten ObjectLists get bUseHeap = 1. Retail's allocator bytes are copies
// of an uninitialised stack byte and +0x04 is not written at all (FUN_06d06ea0 sets it on
// the map's copy first); the port writes zero instead of copying indeterminate memory.
StarSystem::StarSystem()
    : SystemInfo(nullptr),
      ListAlloc(0),
      PlayerListHead(StarSystem_NewPlayerSentinel()),
      PlayerListSize(0),
      DockQueueAlloc(0),
      DockQueueHead(StarSystem_NewDockSentinel()),
      DockQueueSize(0),
      Spatial(nullptr),
      VectorAlloc(0),
      VectorBegin(nullptr),
      VectorEnd(nullptr),
      VectorCapacity(nullptr),
      bLoaded(0)
{
    Pad1[0] = Pad1[1] = Pad1[2] = 0;
    Pad2[0] = Pad2[1] = Pad2[2] = 0;
    Pad3[0] = Pad3[1] = Pad3[2] = 0;
    Pad4[0] = Pad4[1] = Pad4[2] = 0;
}

// Slot 2 -- .text:06ce10f0 (ICF-shared with CPlayerGroup::GetID).
Universe::ISystem* __thiscall StarSystem::GetZoneDb()
{
    return SystemInfo;
}

// Slot 3 -- LAB_06d06e90. The mask is ignored.
ISpatialPartition* __thiscall StarSystem::GetBucket(unsigned TypeMask)
{
    (void)TypeMask;
    return Spatial;
}

// FUN_06d06ea0.
void StarSystem::init(unsigned int SystemId)
{
    SystemInfo = const_cast<Universe::ISystem*>(Universe::get_system(SystemId));
    if (!SinglePlayer()) {
        PhySys::m_pCollisionFilter = &StarSystem_MpCollisionFilter;
    }
}

// StarSystem::load -- retail FUN_06d06ed0, a thiscall with no stack arguments (Ghidra's
// void(void) is wrong). See server_sysdb.h.
void StarSystem::load()
{
    // Tests for 1, not non-zero (add_player's guard is the one that tests for zero).
    if (bLoaded == 1)
        return;

    // ISystem slot 0 (NicknameId in this port's flat record); no null test on SystemInfo.
    unsigned int const SystemId = SystemInfo->NicknameId;

    PhySys::Startup(SystemId);

    // Single player wraps the solars the client already built and mirrors its asteroid
    // fields.
    if (SinglePlayer()) {
        load_solars();
        SrvAsteroid::SrvAsteroid_MapSystem(SystemId);
    } else {
        char SystemIniPath[260];
        Universe::get_filename(SystemIniPath, SystemInfo->File);

        // Both results dropped.
        CmnAsteroid::Load(SystemId);
        SrvAsteroid::SrvAsteroid_LoadSystem(SystemId, SystemIniPath);

        // Every [Object] section goes to load_object (FUN_06d09d60), its result dropped.
        INI_Reader Reader;
        if (Reader.open(SystemIniPath, false)) {
            while (Reader.read_header()) {
                if (Reader.is_header("Object"))
                    load_object(Reader);
            }
        }
    }

    // The BSP spatial partition over the solars. The array is sized by count_type (which
    // filters) but filled for EVERY node of the list (which does not).
    int const SolarCount = static_cast<int>(Solars.count_type(0xffffffffu));
    Spatial = NewSpatial("BSP");
    if (Spatial != nullptr) {
        FSpatialEntry* const Entries = static_cast<FSpatialEntry*>(
            ::operator new(static_cast<size_t>(SolarCount) * sizeof(FSpatialEntry)));
        FSpatialEntry* Out = Entries;
        FSystemListNode* Node = nullptr;
        for (;;) {
            Node = (Node == nullptr) ? Solars.Head : Node->Next;
            if (Node == nullptr)
                break;
            GameObject* const Obj = Node->Data;
            Out->Object = static_cast<IObjRW*>(Obj);
            Out->Position = Obj->get_position();
            Out->Radius = Obj->cobject()->Radius;
            ++Out;
        }
        // ISpatialPartition +0x14: AddArray(entries, count).
        Spatial->AddArray(Entries, static_cast<unsigned>(SolarCount));
        ::operator delete(Entries);
    }

    bLoaded = 1;

    // "This system is now loaded"; retail re-reads the same id off SystemInfo.
    unsigned int EventData = SystemId;
    Server_ControllerDispatch(0u, pub::Controller::EMessage::SystemLoaded, &EventData);
}

// FUN_06d07130. See server_sysdb.h.
void StarSystem::shutdown()
{
    if (bLoaded == 0)
        return;

    // Cleared before the event goes out.
    bLoaded = 0;

    // No null test on SystemInfo.
    unsigned int SystemId = SystemInfo->NicknameId;
    Server_ControllerDispatch(0u, pub::Controller::EMessage::SystemUnloaded, &SystemId);

    unload();
}

// StarSystem::load_solars -- retail FUN_06d07170. See server_sysdb.h.
void StarSystem::load_solars()
{
    int Count = 0;
    CObject* Obj = CObject::FindFirst(CObject::ClassSolar);
    if (Obj != nullptr)
    {
        do
        {
            // The walk's own reference; attach_existing takes the wrapper's.
            Obj->AddRef();
            ::Solar* const Wrapper = new ::Solar();                 // FUN_06d02b70

            CObject* const Attached =
                ((static_cast<unsigned int>(Obj->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar) ? Obj : nullptr;
            Wrapper->attach_existing(Attached);                     // FUN_06d02c20

            Wrapper->enter_system(this, nullptr);
            Wrapper->check_fuses();
            Obj->Release();

            Solars.append(Wrapper);
            ++Count;

            Obj = CObject::FindNext();
        } while (Obj != nullptr);

        if (Count != 0)
            return;
    }

    FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s",
          "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x129,
          "failed to find any solar objects\n");
}

// ===========================================================================
// StarSystem::unload -- retail FUN_06d072c0. Single player only, after event 0x3d, when the
// last player leaves a system. See the declaration for the order.
// ===========================================================================

// Retail's per-object disposal: the scalar deleting destructor (flag 1) of the wrapper's
// CBase sub-object at +0x0c, which destroys and frees the whole wrapper; null Data skipped.
static void StarSystem_DeleteObject(GameObject* Obj)
{
    delete static_cast<CBase*>(Obj);
}

// StarSystem::add_player -- retail FUN_06d07e20. See server_sysdb.h.
void StarSystem::add_player(unsigned int SessionId)
{
    // Tests for zero here, while load()'s own guard compares against 1.
    if (bLoaded == 0)
        load();

    // The id is tested against 0x3fffffff, not zero: an id with only the top two bits set
    // is rejected too.
    if ((SessionId & 0x3fffffffu) == 0)
        return;

    // A push_back; no null test on the sentinel (the StarSystem ctor always allocates it).
    SystemPlayerNode* const Sentinel = PlayerListHead;

    // Retail builds the Observer on the stack, copies it into the node and destroys the
    // temporary at the end of this block.
    {
        Observer Temp(SessionId);

        // FUN_06d0f330 (list::insert), inlined, including MSVC6 _Buynode's null-Prev
        // fallback.
        SystemPlayerNode* Prev = Sentinel->Prev;
        SystemPlayerNode* const Node =
            static_cast<SystemPlayerNode*>(::operator new(sizeof(SystemPlayerNode)));
        Node->Next = Sentinel;
        if (Prev == nullptr)
            Prev = Node;
        Node->Prev = Prev;
        Sentinel->Prev = Node;
        Node->Prev->Next = Node;
        ::new (static_cast<void*>(&Node->View)) Observer(Temp);
        ++PlayerListSize;
    }

    // The live element, kept for every pass below.
    Observer* const View = &Sentinel->Prev->View;

    // The multiplayer-only head is SKIPPED in single player.
    bool const bSingle = SinglePlayer();

    if (!bSingle) {
        // Retail pushes id-then-session, so the session is the FIRST parameter. SystemInfo
        // is read unguarded.
        Players.SendSystemID(SessionId, SystemInfo->NicknameId);

        // Unchecked raw slot; read back only by the AttitudeTowards call below.
        int const PlayerRep = Players[SessionId].rep_vibe;

        // The walk is skipped when count_type finds nothing.
        FSystemListNode* const SolarHead = Solars.Head;
        if (Solars.count_type(0xffffffffu) != 0) {
            for (FSystemListNode* SolarNode = SolarHead; SolarNode != nullptr;
                 SolarNode = SolarNode->Next) {
                // node->Data dereferenced unguarded.
                GameObject* const Obj = SolarNode->Data;

                CObject* Raw = const_cast<CObject*>(Obj->cobject());
                CSolar* Solar = nullptr;
                if (Raw != nullptr &&
                    (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
                    Solar = static_cast<CSolar*>(Raw);

                if (Solar != nullptr && Solar->bDynamic) {
                    // A DYNAMIC solar gets CREATESOLAR and skips the damage list.
                    CSolarCreateInfo Info;
                    CSolarCreateInfo_Fill(&Info, Solar);                // FUN_06d07950
                    Info.bMarkedBy = Obj->is_marked_by(SessionId) ? 1u : 0u;
                    ::g_pClientInterface->SendServerCreateSolar(SessionId, Info);
                } else {
                    // Everything else, a static solar included: its damage list, sent
                    // only when non-empty.
                    DamageList Damage;
                    Damage.set_destroyed(false);
                    // The same cause the observer writes.
                    Damage.set_cause(DamageCause::InitialSync);
                    Damage.set_inflictor_id(0u);
                    Damage.set_inflictor_owner_player(0u);
                    Obj->get_damage_list(Damage, 1.0f);
                    if (!Damage.Entries.empty()) {
                        ::g_pClientInterface->SendServerDamageObject(SessionId, Obj->get_id(), Damage);
                    }
                }

                // The rest only for a solar, dynamic or not.
                if (Solar == nullptr)
                    continue;

                // SETREPUTATION for the solar's vibe. +0x0c is never written (stack residue
                // in retail, zeroed here).
                FServerSetReputationInfo Rep;
                Rep.Rank = 0u;
                Rep.Relations = nullptr;
                Rep.RelationCount = 0;
                Rep.DisplayName[0] = L'\0';
                Rep.VibeHandle = static_cast<int>(Solar->VibeId);
                ::Reputation::Vibe::AttitudeTowards(Rep.VibeHandle, PlayerRep,
                                                    Rep.Attitude);
                ::Reputation::Vibe::GetAffiliation(Rep.VibeHandle, Rep.Affiliation,
                                                   false);
                ::g_pClientInterface->SendServerSetReputation(SessionId, Rep);

                // Each functioning gun's target. No CEGun::cast: the traverser's mask
                // already selects guns.
                CEquipTraverser Walk(EquipTypeGun);
                for (CEquip* Equip = Solar->EquipManager.Traverse(Walk);
                     Equip != nullptr;
                     Equip = Solar->EquipManager.Traverse(Walk)) {
                    if (!Equip->IsFunctioning())
                        continue;
                    CSimple* Target = nullptr;
                    unsigned short TargetSub = SUBOBJ_ID_NONE;
                    static_cast<CEGun*>(Equip)->GetTarget(Target, TargetSub);
                    if (Target == nullptr)
                        continue;
                    // Retail never writes the two pad words.
                    XSetTarget Msg;
                    Msg.ObjectId = Solar->Id;
                    Msg.SubObjId = Equip->Id;
                    Msg.Pad_0x06 = 0u;
                    Msg.TargetObjectId = Target->Id;
                    Msg.TargetSubObjId = TargetSub;
                    Msg.Pad_0x0e = 0u;
                    ::g_pClientInterface->SendCommonSetTarget(SessionId, Msg);
                }

                // One ACTIVATEOBJECT per active entry.
                std::vector<bool> States;
                if (Solar->get_activate_state(States)) {
                    for (unsigned int Index = 0;
                         Index < static_cast<unsigned int>(States.size()); ++Index) {
                        if (!States[Index])
                            continue;
                        FPayloadServerActivateObject Activate;
                        Activate.ObjectId = Solar->Id;
                        Activate.bActivate = 1;
                        Activate.Pad_0x05[0] = 0;
                        Activate.Pad_0x05[1] = 0;
                        Activate.Pad_0x05[2] = 0;
                        Activate.ActivateParam = Index;
                        ::g_pClientInterface->SendServerActivateObject(SessionId, Activate);
                    }
                }
            }
        }
    }

    // Ships: single player sends CREATESHIP at once; multiplayer only tracks the live ones.
    {
        FSystemListNode* Node2 = Ships.Head;
        while (Node2 != nullptr) {
            FSystemListNode* const Next = Node2->Next;
            GameObject* const Obj = Node2->Data;
            if (bSingle) {
                CObject* const Raw = Obj->cobj();
                CShip* const Ship =
                    (Raw != nullptr &&
                     (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                        ? static_cast<CShip*>(Raw)
                        : nullptr;
                if (Ship != nullptr) {
                    CShipCreateInfo Info;
                    CShipCreateInfo_Fill(&Info, Ship);
                    Info.bMarkedBy =
                        Obj->is_marked_by(SessionId) ? 1u : 0u;
                    View->create_ship_for_player(&Info, Obj);
                }
            } else if (!Obj->is_destroyed()) {
                View->add_tracked_object(Obj);           // FUN_06cf7140
            }
            Node2 = Next;
        }
    }

    // Loot, counted the same way; both branches return at once on a zero count.
    {
        FSystemListNode* const LootHead = Loot.Head;
        unsigned int const LootCount = Loot.count_type(0xffffffffu);
        if (LootCount == 0)
            return;

        if (bSingle) {
            // Single player: one CREATELOOT for all of it. OwnerId stays 0 here (the
            // observer's per-object path is the one that copies CLoot+0xe4).
            std::vector<GameObject*> LootObjs;
            LootObjs.reserve(LootCount);
            FServerLootDesc* const Descs = static_cast<FServerLootDesc*>(
                ::operator new(sizeof(FServerLootDesc) * LootCount));
            if (Descs == nullptr)
                return;
            unsigned int Filled = 0u;
            for (FSystemListNode* LootNode = LootHead; LootNode != nullptr;
                 LootNode = LootNode->Next) {
                if (Filled >= LootCount)
                    break;
                GameObject* const Obj = LootNode->Data;
                CObject* const Raw = Obj->cobj();
                CLoot* const Loot =
                    (Raw != nullptr &&
                     (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassLoot) == CObject::ClassLoot)
                        ? static_cast<CLoot*>(Raw)
                        : nullptr;
                FServerLootDesc* const Desc = &Descs[Filled];
                std::memset(Desc, 0, sizeof(*Desc));
                Desc->Field_0x44 = 1;
                ServerLootDesc_Fill(Desc, Loot);
                Desc->bMarkedBy = Obj->is_marked_by(SessionId) ? 1u : 0u;
                LootObjs.push_back(Obj);
                ++Filled;
            }

            FServerCreateLootRequest Req;
            Req.OwnerId = 0u;
            Req.Kind = 5u;
            Req.bMarkedBy = 0;
            Req.bTemporary = 0;
            Req.Pad0a[0] = 0;
            Req.Pad0a[1] = 0;
            Req.Items = Descs;
            Req.Count = static_cast<unsigned char>(Filled);
            Req.Pad11[0] = 0;
            Req.Pad11[1] = 0;
            Req.Pad11[2] = 0;

            if (Filled > 0u) {
                View->create_loot_for_player(&Req, &LootObjs[0], &LootObjs[0] + LootObjs.size());
            }
            ::operator delete(Descs);
        } else {
            FSystemListNode* Node2 = LootHead;
            while (Node2 != nullptr) {
                FSystemListNode* const Next = Node2->Next;
                GameObject* const Obj = Node2->Data;
                if (!Obj->is_destroyed()) {
                    View->add_tracked_object(Obj);
                }
                Node2 = Next;
            }
        }
    }
}

// StarSystem::remove_player -- retail FUN_06d08740, the first half of both system-exit
// entry points (FUN_06d087f0 and FUN_06d08890).
void StarSystem::remove_player(unsigned int SessionId)
{
    // (a) Unlink this session's player node (one match only). Retail dereferences the
    //     head at once, with no null test.
    SystemPlayerNode* const Sentinel = PlayerListHead;
    // KNOWN CRASH (intentional, retail-faithful): a fault on a garbage Sentinel coming from
    // PlayerDB_LeaveSystem means `this` is SysMap's unconstructed head node -- a SysMap
    // miss (bad universe.ini system id). See PlayerDB_LeaveSystem.
    for (SystemPlayerNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        if (Node->View.PlayerId == SessionId) {
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            // The element is embedded IN the node at +0x08 (a LEA, despite Ghidra's
            // rendering); retail calls its scalar deleting destructor with 0 -- destroy in
            // place, do not free -- and frees the node separately.
            Observer* const Element = &Node->View;
            Element->~Observer();
            ::operator delete(Node);
            --PlayerListSize;
            break;
        }
    }

    // (b) Destroy every live ship this session owns. The next node is read AFTER the
    //     destroy. Retail reads CSimple::OwnerPlayer (+0xb4) with no test after the (& 3)
    //     narrowing, so a non-CSimple object faults; every object on this list is a
    //     CSimple there, and the port does the same unguarded read.
    FSystemListNode* Node = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->csimple()->OwnerPlayer != SessionId)
            continue;
        if (Obj->is_destroyed())
            continue;
        Obj->destroy(DESTROY_VANISH, Obj->get_id());
    }
}

// StarSystem::player_switched_out -- retail FUN_06d087f0. See server_sysdb.h.
void StarSystem::player_switched_out(unsigned int SessionId, unsigned int DestSystemId)
{
    remove_player(SessionId);

    // The raw slot.
    Players[SessionId].flush_pending_visits();

    if (DestSystemId != 0u)
    {
        unsigned int Payload[3];
        Payload[0] = SystemInfo->NicknameId;
        Payload[1] = DestSystemId;
        Payload[2] = SessionId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
    }

    // Single player: the system turns itself off.
    if (SinglePlayer() && bLoaded != 0)
    {
        bLoaded = 0;
        unsigned int SystemNickname = SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemUnloaded, &SystemNickname);
        unload();                                                   // FUN_06d072c0
    }
}

// FUN_06d08940. See server_sysdb.h.
bool StarSystem::remove_solar(GameObject* Obj)
{
    unsigned int const ObjId = Obj->get_id();
    Server_LookupCacheForget(ObjId);

    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Solars.Head : Node->Next;
        if (Node == nullptr)
            return false;
        if (Node->Data == Obj)
            break;
    }

    Solars.remove(Node);

    // Out of the BSP the load pass bulk-added it to.
    ISpatialPartition* const Bucket = GetBucket(0xfdffu);
    if (Bucket != nullptr)
    {
        Bucket->Remove(static_cast<IObjRW*>(Obj));
    }

    // Onto the Deferred list (+0xe8).
    Deferred.append(Obj);
    return true;
}

// StarSystem::create_loot -- retail FUN_06d08a40.
void StarSystem::create_loot(unsigned int SourceObjId, int Mode, FLootRecord const* Records, int Count)
{
    FServerLootDesc* const Items =
        static_cast<FServerLootDesc*>(operator new(static_cast<size_t>(Count) * sizeof(FServerLootDesc)));
    for (int Index = 0; Index < Count; ++Index)
        Items[Index].init();

    // The CREATELOOT request every player view gets at the end.
    FServerCreateLootRequest Request = {};
    Request.OwnerId = SourceObjId;
    Request.Kind = static_cast<unsigned int>(Mode);
    Request.bMarkedBy = 0;
    Request.bTemporary = 0;
    Request.Items = Items;
    Request.Count = static_cast<unsigned char>(Count);

    std::vector<GameObject*> Spawned;

    // An unsigned test: nothing to spawn skips straight to the player walk (which still
    // runs).
    if (static_cast<unsigned int>(Count) != 0u) {
        Spawned.reserve(static_cast<size_t>(Count >= 0 ? Count : 0));

        FLootRecord const* Record = Records;
        FServerLootDesc* Item = Items;
        for (unsigned int Left = static_cast<unsigned int>(Count); Left != 0u; --Left) {
            ::Loot* const Obj = new ::Loot();

            // The result is not tested.
            Obj->create(const_cast<Archetype::Equipment*>(Record->Arch));

            CLoot* const LootObject = Obj->cloot();

            // FUN_06d08e20's defaults come from the member initialisers.
            CLoot::CreateParms Parms;
            Parms.Pad58 = 0u;
            Parms.Id = Record->Id;
            Parms.ObjDb = this;
            Parms.HitPts = Record->HitPts;
            Parms.Pos = Record->Position;
            Parms.OwnerPlayer = 0u;
            Parms.Orient = Record->Orientation;
            Parms.Velocity = Record->Velocity;
            Parms.AngularVelocity = Record->AngularVelocity;
            Parms.OwnerId = SourceObjId;
            Parms.Units = static_cast<unsigned int>(Record->Count);
            Parms.NameId = static_cast<int>(Record->NameId);
            Parms.ContentsArch = Record->ContentsArch;
            Parms.bTemporary = Record->bMission != 0;
            Parms.ContentsHitPts = Record->ContentsHitPts;
            Parms.bCanAiTractor = Record->bCanAiTractor != 0;

            LootObject->init(Parms);

            if ((Record->MarkForPlayer & 0x3fffffffu) != 0u)
                Obj->mark_for_player(Record->MarkForPlayer, 1);

            Obj->enter_system(this, nullptr);

            Loot.append(Obj);

            Spawned.push_back(Obj);

            // Controller message 0x2b. The field names are what the stores say; retail
            // names none.
            struct FControllerMsg2B {
                unsigned int LootId;
                unsigned int SourceObjId;
                unsigned int GoodId;
                int Count;
            } Msg;
            Msg.LootId = Parms.Id;
            Msg.SourceObjId = SourceObjId;
            Msg.GoodId = Arch2Good(Record->ContentsArch->get_id());
            Msg.Count = Record->Count;
            Server_ControllerDispatch(0, pub::Controller::EMessage::LootSpawned, &Msg);

            ServerLootDesc_Fill(Item, LootObject);                  // FUN_06d07b90

            ++Item;
            ++Record;
        }
    }

    SystemPlayerNode* Node = PlayerListHead->Next;
    while (Node != PlayerListHead) {
        GameObject* const* const First = Spawned.empty() ? nullptr : &Spawned[0];
        GameObject* const* const Last = First + Spawned.size();
        Node->View.create_loot_for_player(&Request, First, Last);
        Node = Node->Next;
    }

    operator delete(Items);
}

// FUN_06d08e80 -- FServerLootDesc's constructor. Position, orientation, velocity and +0x47
// are left alone.
void FServerLootDesc::init()
{
    LootId = 0u;
    ContentsNickname = 0u;
    ContentsHitPts = 0.0f;
    Units = 0u;
    ContainerNickname = 0u;
    HitPts = 0.0f;
    NameOverride = 0u;
    Field_0x44 = 1;
    bTemporary = 0;
    bMarkedBy = 0;
}

// StarSystem::create_mine -- retail FUN_06d08ef0. One mine per launch record, from
// SrvMineDropper::launch.
void StarSystem::create_mine(FServerLaunchRecord const& Rec)
{
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();
    if (Maker == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x3aa,
              "Failed to get instance of global ID maker.\n");
        return;
    }

    ::Mine* const Obj = new ::Mine();                               // FUN_06cf3330

    Archetype::Equipment* const Arch = const_cast<Archetype::Equipment*>(Rec.Arch);
    Arch->load();
    Obj->create(Arch);
    Arch->Release();

    CMine* const MineObject = Obj->cmine();

    // FUN_06d08e20's defaults are the member initialisers.
    CMine::CreateParms Parms;

    Parms.Id = Maker->AllocateObjectID();
    if ((Parms.Id & 0x3fffffffu) == 0u)
    {
        // The mine wrapper built above is neither entered nor freed (retail).
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x3c2,
              "Failed to allocate valid ID for new mine.\n");
        return;
    }

    Parms.ObjDb = this;
    Parms.OwnerPlayer = 0u;
    Parms.Pos = Rec.Position;
    Parms.Orient = Rec.Orientation;
    Parms.Velocity = Rec.Velocity;

    // random_inside_sphere(|Velocity|). x87: (x*x + y*y) + z*z at double precision, the
    // root passed as a float.
    double LengthSq = static_cast<double>(Rec.Velocity.x) * static_cast<double>(Rec.Velocity.x) +
                      static_cast<double>(Rec.Velocity.y) * static_cast<double>(Rec.Velocity.y);
    LengthSq += static_cast<double>(Rec.Velocity.z) * static_cast<double>(Rec.Velocity.z);
    float const Speed = static_cast<float>(std::sqrt(LengthSq));
    Parms.AngularVelocity = random_inside_sphere(Speed);
    Parms.OwnerId = Rec.OwnerId;

    MineObject->init(Parms);
    Obj->enter_system(this, nullptr);
    Obj->arm();                                                     // FUN_06cf3560

    Mines.append(Obj);

    // The CREATEMINE record; every default of retail's inlined ctor but +0x44 is
    // overwritten.
    FServerCreateProjectileInfo Info;
    Info.ObjId = Parms.Id;
    Info.ArchetypeNickname = Arch->get_id();
    Info.OwnerId = Rec.OwnerId;
    Info.SubObjId = Rec.EquipId;
    Info.Pad0e = 0;
    Info.Position = Parms.Pos;
    Server_MatrixToQuat(Info.Orientation, Parms.Orient);
    Info.Velocity = Parms.Velocity;
    Info.AngularVelocity = Parms.AngularVelocity;
    Info.OwnerPlayer = 0u;

    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
        Player->View.create_mine_for_player(&Info, Obj);
}

SrvAsteroid::MedMine* StarSystem::create_med_mine(unsigned int const& ObjId)
{
    SrvAsteroid::MedMine* const Obj = new SrvAsteroid::MedMine();
    MedAsteroids.append(Obj);
    Obj->find(ObjId);
    Obj->enter_system(this, nullptr);
    return Obj;
}

// StarSystem::send_mine_explosion -- retail FUN_06d09270, on a local copy of the record.
void StarSystem::send_mine_explosion(FMineExplosion const& Rec)
{
    FMineExplosion Local = Rec;
    SystemPlayerNode* Node = PlayerListHead->Next;
    while (Node != PlayerListHead) {
        Node->View.send_mine_explosion(&Local);
        Node = Node->Next;
    }
}

// StarSystem::create_counter_measure -- retail FUN_06d092c0, from
// SrvCounterMeasureDropper::launch. create_mine's body with different FDUMP lines (the id
// text still says "new mine", retail's string), the (& 0x17) narrowing, divert_missiles
// after enter_system, CounterMeasures, and the countermeasure notifier.
void StarSystem::create_counter_measure(FServerLaunchRecord const& Rec)
{
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();
    if (Maker == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x401,
              "Failed to get instance of global ID maker.\n");
        return;
    }

    ::CounterMeasure* const Obj = new ::CounterMeasure();          // FUN_06ce6170

    Archetype::Equipment* const Arch = const_cast<Archetype::Equipment*>(Rec.Arch);
    Arch->load();
    Obj->create(Arch);
    Arch->Release();

    CCounterMeasure* const CmObject = Obj->ccountermeasure();

    CCounterMeasure::CreateParms Parms;

    Parms.Id = Maker->AllocateObjectID();
    if ((Parms.Id & 0x3fffffffu) == 0u)
    {
        // The wrapper built above is neither entered nor freed.
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x419,
              "Failed to allocate valid ID for new mine.\n");
        return;
    }

    Parms.ObjDb = this;
    Parms.OwnerPlayer = 0u;
    Parms.Pos = Rec.Position;
    Parms.Orient = Rec.Orientation;
    Parms.Velocity = Rec.Velocity;

    // As in create_mine (x87, double precision).
    double LengthSq = static_cast<double>(Rec.Velocity.x) * static_cast<double>(Rec.Velocity.x) +
                      static_cast<double>(Rec.Velocity.y) * static_cast<double>(Rec.Velocity.y);
    LengthSq += static_cast<double>(Rec.Velocity.z) * static_cast<double>(Rec.Velocity.z);
    float const Speed = static_cast<float>(std::sqrt(LengthSq));
    Parms.AngularVelocity = random_inside_sphere(Speed);
    Parms.OwnerId = Rec.OwnerId;

    CmObject->init(Parms);
    Obj->enter_system(this, nullptr);
    Obj->divert_missiles();                                         // FUN_06ce6300

    CounterMeasures.append(Obj);

    FServerCreateProjectileInfo Info;
    Info.ObjId = Parms.Id;
    Info.ArchetypeNickname = Arch->get_id();
    Info.OwnerId = Rec.OwnerId;
    Info.SubObjId = Rec.EquipId;
    Info.Pad0e = 0;
    Info.Position = Parms.Pos;
    Server_MatrixToQuat(Info.Orientation, Parms.Orient);
    Info.Velocity = Parms.Velocity;
    Info.AngularVelocity = Parms.AngularVelocity;
    Info.OwnerPlayer = 0u;

    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
        Player->View.create_counter_for_player(&Info, Obj);
}

// StarSystem::create_guided -- retail FUN_06d09640, from SrvGun::launch_guided. Unlike
// create_mine, the id is allocated and the target looked up BEFORE the wrapper is built.
void StarSystem::create_guided(FServerLaunchRecord const& Rec)
{
    CFLIDMaker* const Maker = CFLIDMaker::GetInstance();
    if (Maker == nullptr)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x446,
              "Failed to get instance of global ID maker.\n");
        return;
    }

    unsigned int const Id = Maker->AllocateObjectID();
    if ((Id & 0x3fffffffu) == 0u)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x44d,
              "Failed to allocate valid, dynamic ID for guided object.\n");
        return;
    }

    // The lock target.
    GameObject* const Target = find_object(Rec.TargetId);

    ::Guided* const Obj = new ::Guided();                           // FUN_06cf3ce0

    Archetype::Equipment* const Arch = const_cast<Archetype::Equipment*>(Rec.Arch);
    Arch->load();
    Obj->create(Arch);
    Arch->Release();

    CGuided* const GuidedObject = Obj->cguided();

    CGuided::CreateParms Parms;
    Parms.Id = Id;
    Parms.ObjDb = this;
    Parms.OwnerPlayer = 0u;
    Parms.Pos = Rec.Position;
    Parms.Orient = Rec.Orientation;
    Parms.Velocity = Rec.Velocity;
    Parms.OwnerId = Rec.OwnerId;
    Parms.Target = static_cast<IObjRW*>(Target);
    Parms.SubTarget = Rec.TargetSubId;

    GuidedObject->init(Parms);

    GuidedMunitions.append(Obj);

    Obj->enter_system(this, nullptr);
    Obj->arm();

    std::vector<GameObject*> Spawned;
    Spawned.push_back(Obj);

    FServerCreateGuidedInfo Info;
    Info.ObjId = Parms.Id;
    Info.Position = Parms.Pos;
    Server_MatrixToQuat(Info.Orientation, Parms.Orient);
    Info.TargetId = Rec.TargetId;
    Info.TargetSubObjId = Rec.TargetSubId;
    Info.Pad26 = 0;
    Info.ArchetypeNickname = Arch->get_id();
    Info.OwnerPlayer = 0u;
    Info.OwnerId = Rec.OwnerId;
    Info.SubObjId = Rec.EquipId;
    Info.Pad36 = 0;

    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        GameObject* const* const First = &Spawned[0];
        Player->View.create_guided_for_player(&Info, First, First + Spawned.size());
    }

    // A live CEqObj target with a behavior interface hears of the incoming missile (event 6,
    // {owner id, missile id}).
    if (Target != nullptr && !Target->is_destroyed())
    {
        CObject* const Raw = Target->Object;
        if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
        {
            IBehaviorManager* const Behavior = static_cast<CEqObj*>(Raw)->get_behavior_interface();
            if (Behavior != nullptr)
            {
                unsigned int MissileBlock[2];
                MissileBlock[0] = Rec.OwnerId;
                MissileBlock[1] = Id;
                FBehaviorManagerEvent MissileEvent;
                MissileEvent.Type = FBehaviorManagerEvent::kEventMissileLaunched;
                MissileEvent.Data = MissileBlock;
                Behavior->OnEvent(&MissileEvent);
            }
        }
    }
}

// StarSystem::create_bullet -- retail FUN_06d099e0, one gun round from
// SrvGun::launch_bullets. No player view or controller hears of it here: the client builds
// its own rounds from the FIREWEAPON notify (FUN_06d0e2e0).
void StarSystem::create_bullet(FServerBulletRecord const& Rec)
{
    ::Bullet* const Obj = new ::Bullet();

    Archetype::Munition* const Arch = const_cast<Archetype::Munition*>(Rec.Arch);
    Arch->load();
    Obj->create(Arch);                                              // FUN_06cf4d40
    Arch->Release();

    CBeam* const Beam = Obj->cbeam();

    // Every field is written here; retail inlines the ctor away.
    CProjectile::CreateParms Parms;
    Parms.Id = 0u;
    Parms.ObjDb = this;
    Parms.OwnerPlayer = 0u;
    Parms.HitPts = -1.0f;
    Parms.Pos = Rec.Position;
    Parms.Orient = Rec.Orientation;
    Parms.Velocity = Rec.Velocity;
    Parms.AngularVelocity = Vector{0.0f, 0.0f, 0.0f};
    Parms.Pad58 = 0u;
    Parms.OwnerId = Rec.OwnerId;
    // Rec.EquipId is not read.

    Beam->init(Parms);
    Obj->enter_system(this, nullptr);

    Beams.append(Obj);
}

// StarSystem::create_med_asteroid / create_med_mine -- retail FUN_06d09bb0 / FUN_06d09c80.
// See server_sysdb.h. No test of find()'s answer: the object is entered either way.
SrvAsteroid::MedAsteroid* StarSystem::create_med_asteroid(unsigned int const& ObjId)
{
    SrvAsteroid::MedAsteroid* const Obj = new SrvAsteroid::MedAsteroid();
    MedAsteroids.append(Obj);
    Obj->find(ObjId);
    Obj->enter_system(this, nullptr);
    return Obj;
}

// StarSystem::load_object -- retail FUN_06d09d60. See server_sysdb.h.
// Retail inlines the CreateParms ctor and dtor. Fields the inlined ctor leaves alone are
// zeroed here (retail leaves stack garbage for CSolar::ReadObj to overwrite).
// ContinualAnims (+0xe4) is filled by ReadObj, one name per "anim" key, and freed by the
// parms' destructor after CSolar::init.
GameObject* StarSystem::load_object(INI_Reader& Reader)
{
    CSolar::CreateParms Parms;
    Parms.Pad58 = 0;
    Parms.DockingBaseId = 0;
    Parms.VibeId = 0;
    Parms.SpaceCostume.PartIds[0] = 0;                             // FUN_06d0a060
    Parms.SpaceCostume.PartIds[1] = 0;
    Parms.SpaceCostume.PartIds[2] = 0;
    Parms.SpaceCostume.PartIds[3] = 0;
    for (int Index = 0; Index < 8; ++Index)
        Parms.SpaceCostume.AccessoryIds[Index] = 0;
    Parms.SpaceCostume.AccessoryCount = 0;
    Parms.VoiceId = 0;
    Parms.NicknameSrc.Id = 0;                                      // ID_String::clear
    Parms.VisitValue = 0;
    Parms.ParentNicknameId = 0;                                    // ID_String::clear
    Parms.bDestructible = 0;
    Parms.bDynamic = 0;
    Parms.DestSystem = 0;
    Parms.DestGateId = 0;                                          // ID_String::clear
    Parms.PrevRing = 0;
    Parms.NextRing = 0;
    Parms.TradelaneSpaceNameId = 0;
    Parms.AtmosphereRange = 0.0f;
    Parms.DockWithBaseId = 0;

    // The bool result is not read.
    Archetype::Solar* Arch = nullptr;
    CSolar::ReadObj(Reader, Arch, Parms);
    // Set after ReadObj and before the archetype test; CSolar::init's connect() hands it to
    // init_physics.
    Parms.ObjDb = this;

    ::Solar* Wrapper = nullptr;
    if (Arch != nullptr)
    {
        Arch->load();
        Wrapper = new ::Solar();                                   // FUN_06d02b70
        Wrapper->create(Arch);
        Wrapper->csolar()->init(Parms);

        Solars.append(Wrapper);

        Arch->Release();
        Wrapper->enter_system(this, nullptr);
        Wrapper->check_fuses();
    }

    return Wrapper;
}

// StarSystem::create_solar -- retail FUN_06d0a200. See server_sysdb.h.
::Solar* StarSystem::create_solar(CSolarCreateInfo& Req)
{
    ::Solar* const Wrapper = new ::Solar();

    // No null test on the archetype.
    Archetype::Solar* const Arch = Archetype::GetSolar(Req.ArchetypeNickname);
    Arch->load();
    Wrapper->create(Arch);
    Arch->Release();

    CObject* const Raw = Wrapper->Object;
    CSolar* const Cs =
        (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
            ? static_cast<CSolar*>(Raw) : nullptr;

    // FUN_06d0a770's values: VibeId -1, the accessory ids (+0x74..+0x93) not written.
    CSolar::CreateParms Parms;
    Parms.Pad58 = 0u;
    Parms.DockingBaseId = 0u;
    Parms.VibeId = 0xffffffffu;
    Parms.SpaceCostume.PartIds[0] = 0u;
    Parms.SpaceCostume.PartIds[1] = 0u;
    Parms.SpaceCostume.PartIds[2] = 0u;
    Parms.SpaceCostume.PartIds[3] = 0u;
    Parms.SpaceCostume.AccessoryCount = 0;
    Parms.VoiceId = 0u;

    Parms.NicknameSrc.clear();
    Parms.VisitValue = 0u;
    reinterpret_cast<ID_String&>(Parms.ParentNicknameId).clear();
    Parms.bDestructible = 0u;
    Parms.bDynamic = 0u;
    Parms.DestSystem = 0u;
    reinterpret_cast<ID_String&>(Parms.DestGateId).clear();
    Parms.PrevRing = 0u;
    Parms.NextRing = 0u;
    Parms.TradelaneSpaceNameId = 0u;
    Parms.AtmosphereRange = 0.0f;
    Parms.DockWithBaseId = 0u;

    Parms.HitPts = static_cast<float>(Req.HitPts);
    Parms.Id = Req.ObjId;
    Parms.ObjDb = this;
    Parms.Pos = Req.Position;
    Parms.OwnerPlayer = 0u;
    Server_QuatToMatrix(Parms.Orient, Req.Orientation);            // DAT_06d8da08 vt +0x54

    // Req.DockingBaseId feeds both DockingBaseId and DockWithBaseId.
    Parms.VisitValue = Req.VisitValue;
    Parms.DockingBaseId = Req.DockingBaseId;
    Parms.DockWithBaseId = Req.DockingBaseId;
    Parms.VibeId = static_cast<unsigned int>(Req.VibeGroupId);
    Parms.SpaceCostume.PartIds[0] = Req.CostumeHead;
    Parms.VoiceId = Req.VoiceId;
    Parms.SpaceCostume.PartIds[1] = Req.CostumeBody;
    Parms.SpaceCostume.PartIds[2] = Req.CostumeLeftHand;
    Parms.SpaceCostume.PartIds[3] = Req.CostumeRightHand;
    Parms.SpaceCostume.AccessoryCount = Req.CostumeAccessoryCount;
    // Signed loop, no clamp to the 8 slots.
    for (int Index = 0; Index < (int)Parms.SpaceCostume.AccessoryCount; ++Index)
        Parms.SpaceCostume.AccessoryIds[Index] = Req.CostumeAccessory[Index];

    // VC6's vector operator= into the still-empty Equipment: the reallocating branch for a
    // non-empty source. Loadout::Get's result is not tested.
    if (Req.LoadoutId != 0u)
    {
        Loadout::Map const* const Map = Loadout::Get(Req.LoadoutId);
        int const Count = static_cast<int>(Map->Items.size());
        if (Count > 0)
        {
            EquipDesc* const Block =
                static_cast<EquipDesc*>(::operator new(static_cast<size_t>(Count) * sizeof(EquipDesc)));
            EquipDesc* Dest = Block;
            for (int Index = 0; Index < Count; ++Index)
            {
                new (Dest) EquipDesc(Map->Items[static_cast<size_t>(Index)]);
                ++Dest;
            }
            Parms.Equipment.Begin = Block;
            Parms.Equipment.End = Dest;
            Parms.Equipment.Cap = Dest;
        }
    }
    Parms.Equipment.append(Req.Equipment);

    Parms.ArchGroupDescs = Req.CollisionGroups;

    Parms.NicknameSrc = Req.NicknameSrc;
    Parms.bDestructible = 1u;
    Parms.bDynamic = Req.bDynamic;
    Cs->init(Parms);

    if ((Req.MarkPlayerId & 0x3fffffffu) != 0u)
        Wrapper->mark_for_player(Req.MarkPlayerId, 1);

    Wrapper->enter_system(this, nullptr);
    Wrapper->check_fuses();

    Solars.append(Wrapper);

    // The spatial partition's single add, with the record load() bulk-adds.
    ISpatialPartition* const Bucket = GetBucket(0xfdffu);
    if (Bucket != nullptr)
    {
        CObject* const BucketRaw = Wrapper->Object;
        CSolar* const BucketSolar =
            (BucketRaw != nullptr && (static_cast<unsigned int>(BucketRaw->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
                ? static_cast<CSolar*>(BucketRaw) : nullptr;
        float const Radius = BucketSolar->Radius;
        Vector const& Position = Wrapper->get_position();
        FSpatialEntry Entry;
        Entry.Position = Position;
        Entry.Radius = Radius;
        Entry.Object = static_cast<IObjRW*>(Wrapper);
        Bucket->Add(&Entry);
    }

    Cs->enable_collisions_r(true);

    // Every player of this system; the request is patched per player.
    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        Req.bMarkedBy = Wrapper->is_marked_by(Player->View.PlayerId) ? 1u : 0u;
        Req.VibeOtherGroupId = Players[Player->View.PlayerId].rep_vibe;
        Player->View.create_solar_for_player(&Req, Wrapper);
    }

    return Wrapper;
}

// StarSystem::create_ship -- retail FUN_06d0a880. See server_sysdb.h.
::Ship* StarSystem::create_ship(FShipCreateRequest const& Req)
{
    ::Ship* const Wrapper = new ::Ship();                           // FUN_06d00e80

    CShipCreateInfo Copy(static_cast<CShipCreateInfo const&>(Req));

    CShip* Cs = nullptr;
    if (Copy.OwnerPlayer != 0u && SinglePlayer())
    {
        // Single player: the client's own ship already exists; find it by the request's id
        // (not the copy's).
        Wrapper->find(Req.ObjId);
        CObject* const Raw = Wrapper->Object;
        Cs = (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                 ? static_cast<CShip*>(Raw) : nullptr;
    }
    else
    {
        // No null test on the archetype.
        Archetype::Ship* const Arch = Archetype::GetShip(Copy.ArchetypeNickname);
        Arch->load();
        Wrapper->create(Arch);
        Arch->Release();

        CObject* const Raw = Wrapper->Object;
        Cs = (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                 ? static_cast<CShip*>(Raw) : nullptr;

        // FUN_06d0a770's values (see create_solar), then the CShip tail. The CEqObj tail
        // +0xb8 .. +0xe4 is stack garbage in retail; CShip::init does not read it.
        CShip::CreateParms Parms;
        Parms.Pad58 = 0u;
        Parms.DockingBaseId = 0u;
        Parms.VibeId = 0xffffffffu;
        Parms.SpaceCostume.PartIds[0] = 0u;
        Parms.SpaceCostume.PartIds[1] = 0u;
        Parms.SpaceCostume.PartIds[2] = 0u;
        Parms.SpaceCostume.PartIds[3] = 0u;
        Parms.SpaceCostume.AccessoryCount = 0;
        Parms.VoiceId = 0u;
        Parms.FieldE8 = 0u;
        Parms.GroupNameId = 0u;
        Parms.Rank = 0u;
        Parms.bUsingTradelane = 0u;

        Parms.HitPts = static_cast<float>(Copy.HitPts);
        Parms.OwnerPlayer = Copy.OwnerPlayer;
        Parms.Id = Copy.ObjId;
        Parms.Pos = Copy.Position;
        Parms.ObjDb = this;
        Server_QuatToMatrix(Parms.Orient, Copy.Orientation);
        Parms.Velocity = Copy.Velocity;
        Parms.AngularVelocity = Copy.AngularVelocity;
        Parms.VibeId = static_cast<unsigned int>(Copy.VibeGroupId);
        Parms.bUsingTradelane = Copy.bUsingTradelane;
        Parms.SpaceCostume.PartIds[0] = Copy.CostumeHead;
        Parms.VoiceId = Copy.VoiceId;
        Parms.SpaceCostume.PartIds[3] = Copy.CostumeRightHand;
        Parms.Rank = Copy.ReputationRank;
        Parms.SpaceCostume.PartIds[2] = Copy.CostumeLeftHand;
        Parms.SpaceCostume.PartIds[1] = Copy.CostumeBody;
        Parms.SpaceCostume.AccessoryCount = Copy.CostumeAccessoryCount;
        // Signed loop, no clamp to the 8 slots.
        for (int Index = 0; Index < (int)Parms.SpaceCostume.AccessoryCount; ++Index)
            Parms.SpaceCostume.AccessoryIds[Index] = Copy.CostumeAccessory[Index];

        // As in create_solar; no test on Loadout::Get.
        if (Copy.LoadoutId != 0u)
        {
            Loadout::Map const* const Map = Loadout::Get(Copy.LoadoutId);
            int const Count = static_cast<int>(Map->Items.size());
            if (Count > 0)
            {
                EquipDesc* const Block =
                    static_cast<EquipDesc*>(::operator new(static_cast<size_t>(Count) * sizeof(EquipDesc)));
                EquipDesc* Dest = Block;
                for (int Index = 0; Index < Count; ++Index)
                {
                    new (Dest) EquipDesc(Map->Items[static_cast<size_t>(Index)]);
                    ++Dest;
                }
                Parms.Equipment.Begin = Block;
                Parms.Equipment.End = Dest;
                Parms.Equipment.Cap = Dest;
            }
        }
        Parms.Equipment.append(Copy.Equipment);

        Parms.ArchGroupDescs = Copy.CollisionGroups;

        Parms.GroupNameId = Copy.GroupNameId;
        Parms.DockingBaseId = Copy.DockingBaseId;
        Cs->init(Parms);

        // The owner's player group id into CShip::GroupId.
        if (Copy.OwnerPlayer != 0u)
        {
            CPlayerGroup* const Group =
                Players[Copy.OwnerPlayer].group_ptr;
            if (Group != nullptr)
            {
                unsigned int const GroupId = Group->GetID();
                Cs->GroupId = GroupId;
            }
        }
    }

    ::Ship::FEquipState State;
    State.EquipIds = Req.EquipIds;
    State.Data = Req.EquipStateData;

    if ((Req.MarkPlayerId & 0x3fffffffu) != 0u)
        Wrapper->mark_for_player(Req.MarkPlayerId, 1);

    Wrapper->enter_system(this, nullptr);
    Wrapper->set_equip_state(State);                               // FUN_06d010e0

    Ships.append(Wrapper);

    if (!SinglePlayer() || Copy.OwnerPlayer == 0u)
        Cs->enable_collisions_r(true);

    // Every player of this system but the owner.
    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (Player->View.PlayerId == Copy.OwnerPlayer)
            continue;
        Copy.bMarkedBy = Wrapper->is_marked_by(Player->View.PlayerId) ? 1u : 0u;
        Player->View.create_ship_for_player(&Copy, Wrapper);
    }

    return Wrapper;
}

// FUN_06d0b110 (thiscall on the StarSystem): every player but the owner (OwnerPlayer read
// with no null test on the narrowing).
void StarSystem::send_remote_cargo_update(GameObject* Obj)
{
    for (SystemPlayerNode* Node = PlayerListHead->Next;
         Node != PlayerListHead; Node = Node->Next)
    {
        if (Obj->csimple()->OwnerPlayer != Node->View.PlayerId)
            Node->View.send_remote_cargo_update(Obj);
    }
}

// StarSystem::on_object_destroyed -- retail FUN_06d0b160. See server_sysdb.h.
void StarSystem::on_object_destroyed(GameObject* Obj, int Kind)
{
    // No null test on Obj; OwnerPlayer read through the (& 3) narrowing unguarded.
    unsigned int const SessionId = Obj->csimple()->OwnerPlayer;

    if (SessionId != 0)
    {
        // Computed BEFORE the slot lookup.
        unsigned int const ObjId = Obj->get_id();

        PlayerSlot* const Slot = PlayerDB_GetPlayer(SessionId);

        // Only when the destroyed object IS the session's current ship.
        if (Slot != nullptr && Slot->ship_object_id == ObjId)
        {
            Slot->terminate_trade();
            // Retail passes true (lost as `unaff_BL` in Ghidra): the partner's inbound
            // entry is dropped too.
            Slot->cancel_trade_request(/*bDropPartnerEntry*/true);
            // Only Kind == 1 zeroes the health.
            if (Kind == DESTROY_FUSE)
                Slot->set_relative_health(0.0f, /*bNotify*/false);
            Slot->ship_object_id = 0;
            unsigned int Payload[2];
            Payload[0] = Slot->system_id;
            Payload[1] = SessionId;
            Server_ControllerDispatch(0, pub::Controller::EMessage::SpaceExit, Payload);
        }
    }

    // Unconditionally, every player in this system is told. No null test on the sentinel
    // or a node.
    {
        for (SystemPlayerNode* Node = PlayerListHead->Next;
             Node != PlayerListHead; Node = Node->Next) {
            Node->View.on_object_destroyed(Obj, Kind);
        }
    }
}

// StarSystem::explode -- retail FUN_06d0b260. See server_sysdb.h for the outline.
void StarSystem::explode(ExplosionDamageEvent const& Blast)
{
    GameObject* const Inflictor = find_object(Blast.InflictorId);

    // Up to 0x3c parts in the blast sphere.
    CObject* Parts[0x3c];
    int const Count = PhySys::FindSphereCollisions(SystemInfo->NicknameId, Blast.Position,
                                                   Blast.Explosion->Radius, Parts, 0x3c);

    // Reserve only for a non-zero count; a negative count clamps to 0.
    std::vector<CObject*> Roots;
    if (Count != 0)
        Roots.reserve(Count < 0 ? 0u : static_cast<size_t>(Count));

    // Parts of the skipped collision group are left out; the rest go in by root.
    for (int Index = 0; Index < Count; ++Index) {
        char const* const Group = PhySys::GetCollisionGroup(Parts[Index]);
        if (Blast.SkipCollisionGroup != nullptr && Group != nullptr &&
            _stricmp(Blast.SkipCollisionGroup, Group) == 0)
            continue;
        Roots.push_back(GetRoot(Parts[Index]));
    }

    Archetype::Explosion const* const Explosion = Blast.Explosion;

    // The damage pass, when there is hull or energy damage.
    if (Explosion->HullDamage > 0.0f || Explosion->EnergyDamage > 0.0f) {
        // The ids already damaged.
        std::vector<unsigned int> Damaged;

        for (std::vector<CObject*>::iterator It = Roots.begin(); It != Roots.end(); ++It) {
            CObject* const Root = *It;
            if (Root == nullptr || (static_cast<unsigned int>(Root->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
                continue;
            unsigned int const Id = static_cast<CSimple*>(Root)->Id;
            // Not the exploding object itself.
            if (Id == Blast.ExplodingObjId)
                continue;
            // Each object once.
            if (std::find(Damaged.begin(), Damaged.end(), Id) != Damaged.end())
                continue;
            Damaged.push_back(Id);

            GameObject* const Obj = find_object(Id);
            if (Obj == nullptr)
                continue;
            if (Obj->is_destroyed())
                continue;
            // Player-on-player damage is skipped unless DAT_06d88a80 is set.
            if (GbAllowPvp == 0 && Inflictor != nullptr && Inflictor->is_player() &&
                Obj->is_player())
                continue;

            DamageList Damage;
            Damage.set_cause(Blast.Cause);
            Damage.set_inflictor_id(Blast.InflictorId);
            // Read through the (& 3) narrowing with no null test, as retail does.
            if (Inflictor != nullptr)
                Damage.set_inflictor_owner_player(Inflictor->csimple()->OwnerPlayer);

            Obj->explosion_hit(Blast, Damage);
            Obj->apply_damage_list(Damage);
        }
    }

    // The impulse pass; skipped only when the impulse is exactly 0.
    if (!(Explosion->Impulse == 0.0f)) {
        std::vector<unsigned int> Pushed;

        for (std::vector<CObject*>::iterator It = Roots.begin(); It != Roots.end(); ++It) {
            CObject* const Root = *It;
            if (Root == nullptr || (static_cast<unsigned int>(Root->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
                continue;
            CSimple* const Simple = static_cast<CSimple*>(Root);
            // Players' objects are not pushed.
            if (Simple->OwnerPlayer != 0)
                continue;
            // Unlike the damage pass the insert's answer is NOT tested, so an object whose
            // several parts were in range is pushed once per part (retail).
            if (std::find(Pushed.begin(), Pushed.end(), Simple->Id) == Pushed.end())
                Pushed.push_back(Simple->Id);

            Vector const Center = Root->get_center_of_mass();
            Vector Dir;
            Dir.x = Center.x - Blast.Position.x;
            Dir.y = Center.y - Blast.Position.y;
            Dir.z = Center.z - Blast.Position.z;

            // Retail's order: y*y + z*z, then + x*x. A zero length gets a random direction.
            float const LengthSq = (Dir.y * Dir.y + Dir.z * Dir.z) + Dir.x * Dir.x;
            if (!(LengthSq == 0.0f)) {
                float const Inverse = 1.0f / sqrtf(LengthSq);
                Dir.x = Dir.x * Inverse;
                Dir.y = Dir.y * Inverse;
                Dir.z = Dir.z * Inverse;
            } else {
                Dir = random_on_unit_sphere();
            }

            float const Impulse = Blast.Explosion->Impulse;
            Dir.x = Dir.x * Impulse;
            Dir.y = Dir.y * Impulse;
            Dir.z = Dir.z * Impulse;

            // Only with a physics body.
            if (Root->BodyPtr != nullptr)
                PhySys::AddImpulseAtPoint(Root, Dir, Blast.Position);
        }
    }
}

// FUN_06d0bbc0. See server_sysdb.h for the map.
void StarSystem::sp_obj_update(SSPObjUpdateInfo const& Info, unsigned int SessionId)
{
    // Find the ship; its CSimple Id is read without a test on the (& 3) narrowing.
    CSimple* Ship = nullptr;
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            return;
        Ship = (Node->Data != nullptr) ? Node->Data->csimple() : nullptr;
        if (Ship->Id == Info.ObjectId)
            break;
    }

    PhySys::RCSet(Ship, Info.Position, Info.Orientation, Info.Timestamp);

    // The timestamp narrowed to a float, the throttle a raw dword copy; the pad bytes are
    // never written.
    FCommonUpdateObjectInfo Rec;
    Rec.ObjectId = Info.ObjectId;
    Rec.Orientation = Info.Orientation;
    Rec.Position = Info.Position;
    Rec.Timestamp = static_cast<float>(Info.Timestamp);
    std::memcpy(&Rec.Throttle, &Info.ThrottleBits, sizeof(Rec.Throttle));
    Rec.State = Info.State;

    // Every player but the sender.
    for (SystemPlayerNode* Player = PlayerListHead->Next;
         Player != PlayerListHead; Player = Player->Next)
    {
        if (Player->View.PlayerId == SessionId)
            continue;
        Player->View.send_object_update(&Rec);
    }
}

// The `Server` export (ordinal 524) is the IServerImpl object, defined in IServerImpl.cpp.

// StarSystem::count_players -- retail RVA 0x2BCB0, ordinal 476: the players whose id
// (node+0x40) is not ExcludeId. No null test on the sentinel.
unsigned int StarSystem::count_players(unsigned int ExcludeId) const
{
    unsigned int Count = 0;
    SystemPlayerNode const* const Sentinel = PlayerListHead;
    for (SystemPlayerNode const* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next) {
        if (Node->View.PlayerId != ExcludeId) {
            Count++;
        }
    }
    return Count;
}

// FUN_06d0bcd0 (0x201c-byte frame). See server_sysdb.h for the map.
unsigned int StarSystem::scan_objects(unsigned int* OutIds, unsigned int MaxCount, Vector const* Pos,
                                      float Radius, unsigned int TypeMask)
{
    unsigned int Count = 0;

    // Ships first.
    if ((TypeMask & OBJ_SHIP_MASK) != 0u)
        Count = Ships.collect_in_sphere(OutIds, MaxCount, Pos, Radius, TypeMask);

    if ((TypeMask & OBJ_SOLAR_NO_WAYPOINT_MASK) == 0u)
        return Count;

    // A radius that is not strictly positive walks the solar list instead of the spatial
    // bucket, into what is left of the buffer.
    if (!(Radius > 0.0f))
    {
        Count += Solars.collect_in_sphere(OutIds + Count, MaxCount - Count, Pos, Radius, TypeMask);
        return Count;
    }

    ISpatialPartition* const Bucket = GetBucket(0xfdffu);
    if (Bucket == nullptr)
        return Count;

    // Up to 0x800 hits; the query's return value is not used, the count is read back from
    // the block.
    Geometry::Sphere Sphere;
    Sphere.Center = *Pos;
    Sphere.Radius = Radius;
    struct FBucketResult
    {
        unsigned int Capacity;
        unsigned int Count;
        IObjRW* Data[0x800];
    } Found;
    Found.Capacity = 0x800u;
    Found.Count = 0u;
    // ISpatialPartition +0x1c (sphere query); the stack result has the layout of
    // FSpatialQueryResult.
    Bucket->QuerySphere(&Sphere,
                                                         reinterpret_cast<FSpatialQueryResult*>(&Found));

    // Each hit while the buffer has room; a non-zero get_type result skips it.
    for (unsigned int Index = 0; Index < Found.Count; ++Index)
    {
        if (Count >= MaxCount)
            return Count;
        IObjRW* const Hit = (Index < Found.Count) ? Found.Data[Index] : nullptr;
        unsigned int HitType;
        if (Hit->get_type(HitType) != 0)
            continue;
        if ((HitType & TypeMask) == 0u)
            continue;
        OutIds[Count] = Hit->get_id();
        ++Count;
    }
    return Count;
}

// Mark system as loaded/active
void SysDB_SetSystemLoaded(unsigned int system_id, bool loaded) {
    FStarSystem* sys = SysDB_LookupSystem(system_id);
    if (sys) sys->bLoaded = loaded ? 1 : 0;
}

// FUN_06d0be50's payload for controller event 0x27 (land, or dock without landing). The
// three bytes after the flag are never written.
struct FDockEventPayload {
    unsigned int ObjectId;    // +0x00
    unsigned int BaseId;      // +0x04
    unsigned int TargetId;    // +0x08
    bool bLand;               // +0x0C
};

// FUN_06d0be50's tail (after the per-player view update): consume Common's
// changed-affiliation map (null while empty; the old affiliation is never read). A client
// in a ship is announced to every view of THIS system that sees the ship
// (FUN_06cf73d0); one without a ship gets (Client, 0, Aff). The map is Common's single
// global, so the first system updated in a frame consumes it and the others see null, as
// in retail.
void StarSystem::send_changed_affiliations()
{
    std::map<unsigned int, unsigned int>* const Changed =
        ::Reputation::GetChangedAffiliationClientMap();
    if (Changed == nullptr)
        return;

    std::map<unsigned int, unsigned int>::const_iterator It = Changed->begin();
    for (; It != Changed->end(); ++It)
    {
        unsigned int const Client = It->first;
        PlayerData& Slot = Players[Client];
        unsigned int const Ship = Slot.ship_object_id;
        int const Vibe = Slot.rep_vibe;
        unsigned int Affiliation = kNoAffiliation;
        ::Reputation::Vibe::GetAffiliation(Vibe, Affiliation, false);

        if (Ship != 0)
        {
            GameObject* const Obj = Server_LookupGameObject(Ship);
            SystemPlayerNode* const Head = PlayerListHead;
            for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
            {
                if (Node->View.is_object_visible(Obj))
                    g_pClientInterface->SendServerMiscObjUpdateAffiliation(Node->View.PlayerId, Ship, Affiliation);
            }
        }
        else
        {
            g_pClientInterface->SendServerMiscObjUpdateAffiliation(Client, 0, Affiliation);
        }
    }

    Changed->clear();
}

// FUN_06d0be50's ship-proximity pass, run once per step between the +0x70 and +0x48
// ticks: every object within 10000 (DAT_06d88af0) of a player's ship, plus every object
// of the block at +0xd4, gets update(Step) once. The de-duplication zeroes only the FIRST
// later duplicate of each item, as retail does.
void StarSystem::update_near_ships(float Step)
{
    ISpatialPartition* const Bucket = GetBucket(0xfdffu);
    if (Bucket == nullptr)
        return;

    struct FScanList {
        unsigned int Capacity;
        unsigned int Count;
        IObjRW* Items[0x800];
    };
    FScanList Scan;                     // on retail's 0x20a0-byte frame
    Scan.Capacity = 0x800;
    Scan.Count = 0;

    // ISpatialPartition +0x1c (sphere query); FScanList has the layout of FSpatialQueryResult.
    ISpatialPartition* const Partition = Bucket;

    for (SystemPlayerNode* Node = PlayerListHead->Next;
         Node != PlayerListHead;
         Node = Node->Next)
    {
        unsigned int const ShipId = Players[Node->View.PlayerId].ship_object_id;
        GameObject* const Ship = Server_LookupGameObject(ShipId);
        if (Ship == nullptr)
            continue;
        Geometry::Sphere Sphere;
        Sphere.Radius = 10000.0f;       // DAT_06d88af0
        Sphere.Center = Ship->get_position();
        Partition->QuerySphere(&Sphere, reinterpret_cast<FSpatialQueryResult*>(&Scan));
    }

    FSystemListNode* It = nullptr;
    while (DockingSolars.traverse(It, true))
    {
        if (It->Data == nullptr)
            continue;
        if (Scan.Count < Scan.Capacity)
        {
            Scan.Items[Scan.Count] = It->Data;
            ++Scan.Count;
        }
    }

    if (Scan.Count > 1)
    {
        int const Count = static_cast<int>(Scan.Count);
        int Kept = 0;
        for (int Index = 0; Index < Count; ++Index)
        {
            IObjRW* const Item = Scan.Items[Index];
            if (Item == nullptr)
                continue;
            for (int Later = Index + 1; Later < Count; ++Later)
            {
                if (Scan.Items[Later] == Item)
                {
                    Scan.Items[Later] = nullptr;
                    break;
                }
            }
            Scan.Items[Kept] = Scan.Items[Index];
            ++Kept;
        }
        Scan.Count = static_cast<unsigned int>(Kept);
    }

    for (unsigned int Index = 0; Index < Scan.Count; ++Index)
    {
        GameObject* const Obj =
            dynamic_cast<GameObject*>(Scan.Items[Index]);
        if (Obj != nullptr)
            Obj->update(Step);
    }
}

// FUN_06d0be50 -- StarSystem's update, thiscall with one float. The name is the port's;
// it is a file-scope function because everything it calls lives here.
// Target is the double of the original dt; in MP the step is clamped to 0.2 (double
// DAT_06d68298). The loop runs while 0.0 < Target (NaN skips), adding the float step into a
// double accumulator and shortening the last step to the remainder.
// No iteration cap is needed: freelancer.exe caps ElapseTime dt at 1 s (DAT_00610a08), SP
// runs exactly one step, MP at most five, and the double add always moves the
// accumulator forward. A hang or many iterations means an upstream bad dt.
// Callers: Server_PhysicsUpdate (FUN_06d10320) and IServerImpl::Dock (0.2f).
// Returns the number of step iterations, a port diagnostic; retail returns nothing.
unsigned int StarSystem::update(float Dt)
{
    float Step = Dt;
    if (!SinglePlayer() && static_cast<double>(Step) > 0.2)
        Step = 0.2f;

    const double Target = static_cast<double>(Dt);
    unsigned int Iterations = 0;

    if (Target > 0.0)
    {
        double Accum = 0.0;
        float CurStep = Step;
        do
        {
            Accum = static_cast<double>(CurStep) + Accum;   // a double add
            ++Iterations;

            // Every FUN_06cf4f40 call gets the block at +0xe8 as the deferred list.
            ObjectList* const Deferred = &this->Deferred;
            Ships.tick(CurStep, Deferred);
            GuidedMunitions.tick(CurStep, Deferred);

            update_near_ships(CurStep);

            Loot.tick(CurStep, Deferred);
            Beams.tick(CurStep, Deferred);
            Mines.tick(CurStep, Deferred);
            CounterMeasures.tick(CurStep, Deferred);

            // COMMON.DLL::CmnAsteroid::Update (cdecl): StarSystem is the IObjDB it walks.
            CmnAsteroid::Update(this, CurStep);

            // FUN_06d03480: runs FUN_06d04480 on each field of this system's entry in the
            // SrvAsteroid::SrvAsteroidSystem map at DAT_06d8d980 (server_srvasteroid.cpp).
            SrvAsteroid::SrvAsteroid_UpdateSystem(this);

            MedAsteroids.tick(CurStep, Deferred);

            // (a) Drain the deferred block, which the passes above fill with the objects
            // whose update() answered false. The node removed is always the head, so the
            // next traverse starts from the head again.
            {
                FSystemListNode* It = nullptr;
                while (Deferred->traverse(It, true))
                {
                    GameObject* const Data = It->Data;
                    if (Data != nullptr)
                    {
                        // The deleting destructor through the CBase sub-object.
                        delete static_cast<CBase*>(Data);
                    }
                    It->Data = nullptr;
                    Deferred->remove(It);
                }
            }

            // (b) The dock queue at +0x14 (server_sysdb.h, SystemDockNode), skipped while
            // empty.
            if (DockQueueSize != 0)
            {
                // The players whose ships land this step.
                std::list<unsigned int> Landed;

                // The end test re-reads the head every pass.
                SystemDockNode* Node = DockQueueHead->Next;
                while (Node != DockQueueHead)
                {
                    GameObject* const Obj = FSystemDockEntry::WatchedObject(Node->Entry.Object);
                    GameObject* const DockTarget = FSystemDockEntry::WatchedObject(Node->Entry.Target);

                    // The target's id, but only when its CEqObj has no DockingBaseId
                    // (+0x160). Retail reads +0x160 without a null test on the narrowed
                    // CEqObj; every producer passes a CEqObj-backed wrapper or no target.
                    unsigned int TargetId = 0;
                    if (DockTarget != nullptr)
                    {
                        CObject* const TargetObject = DockTarget->Object;
                        CEqObj* TargetEq = nullptr;
                        if (TargetObject != nullptr &&
                            (static_cast<unsigned int>(TargetObject->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
                            TargetEq = static_cast<CEqObj*>(TargetObject);
                        if (TargetEq->DockingBaseId == 0)
                            TargetId = DockTarget->get_id();
                    }

                    if (Node->Entry.bLand)
                    {
                        // Every player in the system is told (FLPACKET_SERVER_LAND, and the
                        // multiplayer autosave of the owner's slot). ObjId is re-read for
                        // each view.
                        SystemPlayerNode* View = PlayerListHead->Next;
                        while (View != PlayerListHead)
                        {
                            unsigned int ObjId = Obj->get_id();
                            View->View.on_object_landed(&ObjId, &TargetId,
                                                        &Node->Entry.BaseId);
                            View = View->Next;
                        }

                        FDockEventPayload Event;
                        Event.ObjectId = Obj->get_id();
                        Event.BaseId = Node->Entry.BaseId;
                        Event.TargetId = TargetId;
                        Event.bLand = true;
                        Server_ControllerDispatch(0, pub::Controller::EMessage::Msg0x27, &Event);

                        // CSimple::OwnerPlayer read without a null test.
                        unsigned int const Owner = Obj->csimple()->OwnerPlayer;
                        if (Owner == 0)
                        {
                            // Nobody owns it, it just goes.
                            Obj->destroy(DESTROY_VANISH, 0);
                        }
                        else
                        {
                            Landed.push_back(Owner);
                        }
                    }
                    else
                    {
                        FDockEventPayload Event;
                        Event.ObjectId = Obj->get_id();
                        Event.BaseId = Node->Entry.BaseId;
                        Event.TargetId = TargetId;
                        Event.bLand = false;
                        Server_ControllerDispatch(0, pub::Controller::EMessage::Msg0x27, &Event);
                    }

                    Node = Node->Next;
                }

                // Every collected player leaves the system; in single player the system
                // is also shut down.
                for (std::list<unsigned int>::iterator It = Landed.begin(); It != Landed.end(); ++It)
                {
                    unsigned int const PlayerId = *It;
                    remove_player(PlayerId);
                    Players[PlayerId].flush_pending_visits();
                    if (SinglePlayer())
                        shutdown();
                }

                Landed.clear();
                SystemDockNode* const DockHead = DockQueueHead;
                erase_docks(DockHead->Next, DockHead);   // FUN_06d0f410
            }

            // Multiplayer only, and only while the system has players. SinglePlayer() is
            // called again here, not reused from the clamp.
            if (!SinglePlayer() && PlayerListSize != 0)
            {
                // CBeam::move is called even when the node does not narrow to a CBeam --
                // retail has no test, so every object on this block is a beam there.
                FSystemListNode* BeamNode = nullptr;
                while (Beams.traverse(BeamNode, true))
                {
                    GameObject* const BeamWrapper = BeamNode->Data;
                    CObject* const BeamObject =
                        (BeamWrapper != nullptr) ? BeamWrapper->Object : nullptr;
                    CBeam* Beam = nullptr;
                    if (BeamObject != nullptr &&
                        (static_cast<unsigned int>(BeamObject->ObjectClass) & CObject::ClassBeam) == CObject::ClassBeam)
                        Beam = static_cast<CBeam*>(BeamObject);
                    Beam->move(CurStep);
                }

                PhySys::Update(SystemInfo->NicknameId, CurStep);

                process_collisions();
            }

            if (Target - Accum < static_cast<double>(CurStep))
                CurStep = static_cast<float>(Target - Accum);
        } while (Accum < Target);
    }

    // After the step loop (or the skip): every player view gets FUN_06cf53e0 with the
    // original dt as a double.
    if (PlayerListSize != 0)
    {
        for (SystemPlayerNode* Node = PlayerListHead->Next;
             Node != PlayerListHead; Node = Node->Next)
        {
            Node->View.update(Target);
        }
    }

    send_changed_affiliations();

    return Iterations;
}

// FUN_06d0c6d0: solars only.
void StarSystem::add_docking_solar(GameObject* Obj)
{
    if ((Obj->get_type_bits() & OBJ_SOLAR_NO_WAYPOINT_MASK) == 0u)
        return;
    DockingSolars.append(Obj);
}

// FUN_06d0c730: the first match only.
void StarSystem::remove_docking_solar(GameObject* Obj)
{
    MetaNode<GameObject>* Node = nullptr;
    while (DockingSolars.traverse(Node, true))
    {
        if (Node->Data == Obj)
        {
            DockingSolars.remove(Node);
            return;
        }
    }
}

// StarSystem::find_solar -- retail FUN_06d0c7e0. See server_sysdb.h.
GameObject* StarSystem::find_solar(unsigned int const* NicknameId) const
{
    GameObject* Found = nullptr;
    FSystemListNode* Node = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? Solars.Head : Node->Next;
        if (Node == nullptr || Found != nullptr)
            break;
        GameObject* const Obj = Node->Data;
        CObject* const Raw = const_cast<CObject*>(Obj->cobject());
        CSolar* Solar = nullptr;
        if (Raw != nullptr &&
            (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
            Solar = static_cast<CSolar*>(Raw);
        // Retail reads the nickname even off the zeroed pointer; the solar list never
        // holds anything else.
        if (Solar->Nickname.Id == *NicknameId)
            Found = Obj;
    }
    return Found;
}

// FUN_06d0c840: the lists in retail's order, first hit wins. Beams (+0x84) is not searched.
GameObject* StarSystem::find_object(unsigned int Id)
{
    if ((Id & 0x3fffffffu) == 0)
        return nullptr;
    FSystemListNode* Node = Ships.find_by_id(Id);
    if (Node == nullptr)
        Node = Loot.find_by_id(Id);
    if (Node == nullptr)
        Node = GuidedMunitions.find_by_id(Id);
    if (Node == nullptr)
        Node = Mines.find_by_id(Id);
    if (Node == nullptr)
        Node = CounterMeasures.find_by_id(Id);
    if (Node == nullptr)
        Node = MedAsteroids.find_by_id(Id);
    if (Node == nullptr)
        Node = Solars.find_by_id(Id);
    return (Node != nullptr) ? Node->Data : nullptr;
}

// FUN_06d0c8d0: a mask of -1 searches everything; otherwise the FIRST matching test picks
// one list.
GameObject* StarSystem::find_object(unsigned int Id, unsigned int TypeMask)
{
    if ((Id & 0x3fffffffu) == 0)
        return nullptr;
    if (TypeMask == 0xffffffffu)
        return find_object(Id);

    ObjectList* List = nullptr;
    if ((TypeMask & OBJ_SHIP_MASK) != 0)
        List = &Ships;
    else if ((TypeMask & OBJ_LOOT) != 0)
        List = &Loot;
    else if ((TypeMask & OBJ_SOLAR_NO_WAYPOINT_MASK) != 0)
        List = &Solars;
    else if ((TypeMask & OBJ_MINE) != 0)
        List = &Mines;
    else if ((TypeMask & OBJ_GUIDED) != 0)
        List = &GuidedMunitions;
    else if ((TypeMask & OBJ_COUNTERMEASURE) != 0)
        List = &CounterMeasures;
    else if ((TypeMask & (OBJ_ASTEROID | OBJ_DYNAMIC_ASTEROID)) != 0)
        List = &MedAsteroids;
    else
        return nullptr;

    FSystemListNode* const Node = List->find_by_id(Id);
    return (Node != nullptr) ? Node->Data : nullptr;
}

// FUN_06d0c970. See server_sysdb.h.
void StarSystem::complete_dock(GameObject* ShipObj, GameObject* Dock)
{
    ::Ship* const DockingShip = static_cast< ::Ship*>(ShipObj);
    if (DockingShip->DockedObjId != 0)
        return;

    // Every unowned ship's behavior manager hears of the dock (OwnerPlayer read unguarded,
    // no test on the 0x503 narrowing).
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->csimple()->OwnerPlayer != 0u)
            continue;
        CObject* const Raw = (Obj != nullptr) ? Obj->Object : nullptr;
        CEqObj* const Other =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                ? static_cast<CEqObj*>(Raw) : nullptr;
        IBehaviorManager* const Behavior = Other->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        // Event 3, {dock id, dock type, ship id}; the calls in retail order.
        unsigned int LeftBlock[3];
        LeftBlock[2] = ShipObj->get_id();
        LeftBlock[0] = Dock->get_id();
        unsigned int DockType;
        Dock->get_type(DockType);
        LeftBlock[1] = DockType;
        FBehaviorManagerEvent LeftEvent;
        LeftEvent.Type = FBehaviorManagerEvent::kEventObjectLeftSystem;
        LeftEvent.Data = LeftBlock;
        Behavior->OnEvent(&LeftEvent);
    }

    unsigned int Msg[3];
    Msg[0] = ShipObj->get_id();
    Msg[1] = Dock->get_id();
    Msg[2] = SystemInfo->NicknameId;
    Server_ControllerDispatch(0, pub::Controller::EMessage::DockComplete, Msg);

    // Every player view hears that the ship left for the dock; both ids fetched per player.
    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        unsigned int const DockId = Dock->get_id();
        unsigned int const ShipId = ShipObj->get_id();
        Player->View.on_object_left_system(&ShipId, &DockId);
    }

    DockingShip->DockedObjId = Dock->get_id();

    if (ShipObj->csimple()->OwnerPlayer != 0u)
    {
        // A jump gate / jump hole protects the player's ship while it goes through.
        if ((Dock->get_type_bits() & OBJ_JUMP_GATE) != 0u || (Dock->get_type_bits() & OBJ_JUMP_HOLE) != 0u)
        {
            FInvincibility Cmd;
            Cmd.Type = 2;
            Cmd.bProtect = true;
            Cmd.bFromPlayers = true;
            Cmd.Pad06[0] = 0;
            Cmd.Pad06[1] = 0;
            Cmd.MaxHpLoss = 0.0f;
            ShipObj->set_invincible(Cmd);
        }
        return;
    }

    // An AI ship's CShip (not null-tested after the narrowing).
    CObject* const Raw = ShipObj->Object;
    CShip* const Cs =
        (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            ? static_cast<CShip*>(Raw) : nullptr;

    if (SinglePlayer())
    {
        Cs->bJumpedOut = 0;
        return;
    }

    // A gate or hole gives the ship 12 seconds to go through.
    Cs->enable_collisions_r(false);
    if ((Dock->get_type_bits() & OBJ_JUMP_GATE) != 0u || (Dock->get_type_bits() & OBJ_JUMP_HOLE) != 0u)
        DockingShip->DockExitTimer = 12.0f;                // .rdata:06d67cc0
    else
        DockingShip->DockExitTimer = 0.0f;
}

// FUN_06d0cc00. See server_sysdb.h.
void StarSystem::arrive_ship(GameObject* Obj, GameObject* JumpObj, Vector const& Pos, Matrix const& Orient)
{
    unsigned int Bits = 0u;
    if (JumpObj != nullptr)
        Bits = JumpObj->get_type_bits();

    // OwnerPlayer read unguarded.
    if (Obj->csimple()->OwnerPlayer == 0u)
    {
        // Handed on without a null test.
        CObject* const Raw = (Obj != nullptr) ? Obj->Object : nullptr;
        CShip* const Cs =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                ? static_cast<CShip*>(Raw) : nullptr;

        if ((Bits & OBJ_JUMP_GATE) != 0u || (Bits & OBJ_JUMP_HOLE) != 0u)
        {
            // Retail passes Obj itself as the jump object.
            Cs->jump_in(static_cast<IObjRW*>(Obj), Pos, Orient, 0.5f);
        }
        else if ((Bits & OBJ_AIRLOCK_GATE) != 0u)
        {
            Cs->airlock_in(Pos, Orient);
        }
    }

    FPayloadServerSystemSwitchIn Record;
    Record.ShipId = Obj->get_id();
    Record.JumpObjTypeBits = Bits;
    Record.Position = Pos;
    Server_MatrixToQuat(Record.Orientation, Orient);

    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (!Node->View.is_object_visible(Obj))
            continue;
        g_pClientInterface->SendServerSystemSwitchIn(Node->View.PlayerId, Record);
    }
}

// FUN_06d0cd60: solars docking at BaseId (any base when 0), plus, for BaseId 0, baseless
// solars whose type bits lie within 0xc40 (jump gate / airlock / jump hole).
void StarSystem::collect_dockable_solars(std::list<GameObject*>& Out, unsigned int BaseId)
{
    MetaNode<GameObject>* Node = nullptr;
    while (Solars.traverse(Node, true))
    {
        GameObject* const Obj = Node->Data;
        CObject* const Cobj = Obj->cobj();
        CEqObj* Eq = nullptr;
        if (Cobj != nullptr && (static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
            Eq = static_cast<CEqObj*>(Cobj);
        // Retail reads +0x164 through the narrowed pointer without testing it.
        unsigned int const Base = Eq->DockingPointBaseId;
        if (Base != 0)
        {
            if (BaseId != 0 && Base != BaseId)
                continue;
            Out.push_back(Obj);
            continue;
        }
        unsigned int const Bits = Eq->TypeBits;
        if ((Bits & (OBJ_JUMP_GATE | OBJ_AIRLOCK_GATE | OBJ_JUMP_HOLE)) != Bits)
            continue;
        if (BaseId != 0)
            continue;
        Out.push_back(Obj);
    }
}

// FUN_06d0cee0. See server_sysdb.h.
void StarSystem::launch(GameObject* ShipObj, GameObject* Dock, int DockPortIndex)
{
    if (ShipObj->csimple()->OwnerPlayer != 0u)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0x9f2,
              "StarSystem::launch() called on a player!");
        return;
    }

    CObject* const DockRaw = (Dock != nullptr) ? Dock->Object : nullptr;
    CEqObj* const DockEq =
        (DockRaw != nullptr && (static_cast<unsigned int>(DockRaw->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
            ? static_cast<CEqObj*>(DockRaw) : nullptr;

    // Not tested afterwards.
    CObject* const ShipRaw = (ShipObj != nullptr) ? ShipObj->Object : nullptr;
    CShip* const Cs =
        (ShipRaw != nullptr && (static_cast<unsigned int>(ShipRaw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            ? static_cast<CShip*>(ShipRaw) : nullptr;

    Cs->launch(static_cast<IObjRW*>(ShipObj), DockEq, DockPortIndex);
    static_cast< ::Ship*>(ShipObj)->apply_launch_invincibility();  // FUN_06d01110
}

// Four StarSystem members GameObject / EqObj reach through OwnerSystem; see server_sysdb.h.

// FUN_06d0cfa0: ships first, then solars.
void StarSystem::notify_object_left(unsigned int ObjId)
{
    MetaNode<GameObject>* Node = nullptr;
    while (Ships.traverse(Node, true))
        Node->Data->on_object_left_system(ObjId);
    Node = nullptr;
    while (Solars.traverse(Node, true))
        Node->Data->on_object_left_system(ObjId);
}

// FUN_06d0d000. See server_sysdb.h.
void StarSystem::remove_ship(GameObject* Obj, PhySys::CreateParms* OutParms)
{
    // Read unguarded.
    unsigned int const Owner = Obj->csimple()->OwnerPlayer;

    // Every view but the owner's.
    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (Player->View.PlayerId == Owner)
            continue;
        Player->View.on_object_destroyed(Obj, 0);
    }

    unsigned int const ObjId = Obj->get_id();
    Server_LookupCacheForget(ObjId);

    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        if (Node->Data == Obj)
        {
            Ships.remove(Node);
            break;
        }
    }

    // On both paths.
    Obj->leave_system(OutParms);
}

// FUN_06d0d100. See server_sysdb.h.
void StarSystem::add_ship(GameObject* Obj, PhySys::CreateParms* Parms, Vector const& Pos, Matrix const& Orient)
{
    Parms->SystemId = SystemInfo->NicknameId;

    // The (& 1) narrowing; the writes below do not test the result.
    CObject* const Raw =
        (Obj != nullptr && Obj->Object != nullptr &&
         (static_cast<unsigned int>(Obj->Object->ObjectClass) & CObject::ClassObject) == CObject::ClassObject)
            ? Obj->Object : nullptr;
    Raw->WorldPosition = Pos;
    Raw->Orientation = Orient;

    DALib::Engine->UpdateInstance(Raw->EngineInstanceId, 0, nullptr);

    Obj->enter_system(this, Parms);

    Ships.append(Obj);

    CObject* const ShipRaw = (Obj != nullptr) ? Obj->Object : nullptr;
    CShip* const Cs =
        (ShipRaw != nullptr && (static_cast<unsigned int>(ShipRaw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            ? static_cast<CShip*>(ShipRaw) : nullptr;

    // Filled only when the object is a CShip.
    CShipCreateInfo Info;
    if (Cs != nullptr)
        CShipCreateInfo_Fill(&Info, Cs);                            // FUN_06d07690

    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        Info.bMarkedBy = Obj->is_marked_by(Player->View.PlayerId) ? 1 : 0;
        Player->View.create_ship_for_player(&Info, Obj);
    }
}

// ===========================================================================
// The dock queue at StarSystem+0x14 (server_sysdb.h, SystemDockNode).
// ===========================================================================

// Retail's `[watcher] - 8` back to the wrapper, with its null test. The Watchable is a
// non-virtual base of IObjRW, so the downcast is a static one.
GameObject* FSystemDockEntry::WatchedObject(BaseWatcher const& Watcher)
{
    Watchable const* const Target = Watcher.target();
    if (Target == nullptr)
        return nullptr;
    return static_cast<GameObject*>(static_cast<IObjRW*>(const_cast<Watchable*>(Target)));
}

// FUN_06d0d370. See server_sysdb.h.
void StarSystem::queue_dock(GameObject* Object, GameObject* Target,
                            unsigned int const* BaseId, bool const* bLand)
{
    // BaseWatcher::set is retail's compare-then-set_pointer.
    FSystemDockEntry Element;
    Element.Object.set(Object != nullptr ? static_cast<Watchable const*>(Object) : nullptr);
    Element.Target.set(Target != nullptr ? static_cast<Watchable const*>(Target) : nullptr);
    Element.BaseId = *BaseId;
    Element.bLand = *bLand;

    // A push_back, with MSVC6 _Buynode's null-Prev fallback.
    SystemDockNode* const Sentinel = DockQueueHead;
    SystemDockNode* const Node =
        static_cast<SystemDockNode*>(::operator new(sizeof(SystemDockNode)));
    Node->Next = Sentinel;
    Node->Prev = Sentinel->Prev != nullptr ? Sentinel->Prev : Node;
    Sentinel->Prev = Node;
    Node->Prev->Next = Node;

    new (&Node->Entry) FSystemDockEntry(Element);                   // FUN_06d0f830

    ++DockQueueSize;
}

// FUN_06d0d500 -- StarSystem::munition_collision. See server_sysdb.h for the record.
void StarSystem::munition_collision(SSPMunitionCollisionInfo const& Info)
{
    GameObject* const Target = find_object(Info.ObjectId);
    GameObject* const Shooter = find_object(Info.AttackerId);

    // Without PvP (DAT_06d88a80), a player's shot at a missing or player target ends here.
    if (GbAllowPvp == 0 && Shooter != nullptr && Shooter->is_player())
    {
        if (Target == nullptr)
            return;
        if (Target->is_player())
            return;
    }

    if (Target == nullptr)
        return;
    if (Target->is_destroyed())
        return;

    // GetEquipment's result is not tested.
    FMunitionImpact Impact;
    Impact.ShooterId = 0u;
    Impact.Munition = nullptr;
    Impact.SubObjId = SUBOBJ_ID_NONE;
    Archetype::Equipment* const MunitionArch = Archetype::GetEquipment(Info.ProjectileArchId);
    Impact.Munition = static_cast<Archetype::Munition const*>(MunitionArch);
    Impact.ShooterId = Info.AttackerId;
    Impact.SubObjId = Info.SubObjId;
    std::memcpy(&Impact.HitPos, &Info.HitPosX, sizeof(Vector));

    DamageList Damage;

    // The cause from the munition's type: BULLET (0x2000000) -> 2, GUIDED (0x1000000) -> 3,
    // anything else -> 1.
    unsigned int const ObjectType = MunitionArch->ObjectType;
    if (ObjectType == OBJ_BULLET)
        Damage.set_cause(DamageCause::Gun);
    else
        Damage.set_cause((ObjectType == OBJ_GUIDED) ? DamageCause::GuidedHit : DamageCause::Collision);

    Damage.set_inflictor_id(Info.AttackerId);

    // Not null-tested after the (& 3) narrowing.
    if (Shooter != nullptr)
        Damage.set_inflictor_owner_player(Shooter->csimple()->OwnerPlayer);

    Target->munition_hit(Impact, Damage);
    Target->apply_damage_list(Damage);
}

// FUN_06d0d6e0.
void StarSystem::badlands_collision(SSPBadLandsObjCollisionInfo const& Info)
{
    GameObject* const Obj = find_object(Info.ObjectId);
    if (Obj == nullptr)
        return;
    if (Obj->is_destroyed())
        return;
    if (Obj->is_invincible())
        return;
    Obj->destroy(DESTROY_FUSE, Info.BadLandsObjId);
}

// FUN_06d0d730.
void StarSystem::collision_damage(FCollisionDamage const& Rec, GameObject* Target)
{
    GameObject* const Source = find_object(Rec.SourceId);

    // Without PvP (DAT_06d88a80), a player source never damages a player target.
    if (GbAllowPvp == 0 && Source != nullptr && Source->is_player())
    {
        if (Target == nullptr)
            return;
        if (Target->is_player())
            return;
    }
    if (Target == nullptr)
        return;
    if (Target->is_destroyed())
        return;

    // Byte +0x0c is the low byte of this function's own first argument slot, i.e. of the
    // Rec POINTER; retail never writes a value of its own there. Reproduced as read.
    FSubObjDamage Hit;
    Hit.InflictorId = Rec.SourceId;
    Hit.SubObjId = Rec.TargetSubObjId;
    Hit.Damage = Rec.Damage;
    Hit.Field_0x0c = static_cast<unsigned char>(reinterpret_cast<uintptr_t>(&Rec) & 0xffu);

    // Collision; the owner player read through the narrowing without a null test.
    DamageList Damage;
    Damage.Cause = DamageCause::Collision;
    Damage.InflictorId = Rec.SourceId;
    Damage.InflictorOwner = 0;
    if (Source != nullptr)
        Damage.InflictorOwner = Source->csimple()->OwnerPlayer;

    Target->damage_sub_obj(Hit, Damage);
    Target->apply_damage_list(Damage);
}

// FUN_06d0d8a0. See server_sysdb.h for the map.
bool StarSystem::activate_object(GameObject* Obj, bool bOn, unsigned int Index)
{
    EqObj* const Eq = dynamic_cast<EqObj*>(Obj);
    if (Eq == nullptr)
        return false;

    // FUN_06ce7850: the (& 0x103) narrowing of the raw object (not null-tested), then
    // CEqObj::activate.
    ObjActivateResult const Result = Eq->activate(bOn, Index);

    // Only an actual change is announced.
    if (Result == ObjActivateFailed)
        return false;
    if (Result != ObjActivateChanged)
        return true;

    // A solar is announced to every player, anything else only where seen.
    CObject const* Solar = Obj->cobject();
    if (Solar != nullptr &&
        (static_cast<unsigned int>(Solar->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar)
        Solar = nullptr;

    // The pad bytes are never written.
    FPayloadServerActivateObject Rec;
    Rec.ObjectId = Obj->get_id();
    Rec.bActivate = static_cast<unsigned char>(bOn);
    Rec.ActivateParam = Index;

    SystemPlayerNode* const Sentinel = PlayerListHead;
    for (SystemPlayerNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
    {
        if (Solar == nullptr && !Node->View.is_object_visible(Obj))
            continue;
        ::g_pClientInterface->SendServerActivateObject(Node->View.PlayerId, Rec);
    }

    return true;
}

// FUN_06d0d9b0. See server_sysdb.h for the map.
void StarSystem::spawn_mined_loot(Vector const& Pos, unsigned int ContainerArchId,
                                  unsigned int ContentsArchId, int Count)
{
    // Bytes +0x6a/+0x6b of the record are never written.
    FLootRecord Rec;
    Rec.Arch = nullptr;
    Rec.HitPts = 0.0f;
    Rec.ContentsHitPts = 0.0f;
    Rec.Count = 0;
    Rec.MarkForPlayer = 0u;
    Rec.NameId = 0u;
    Rec.bCanAiTractor = 1;
    Rec.bMission = 0;

    // Allocated before any archetype is looked up, so spent even on the early outs.
    Rec.Id = CFLIDMaker::GetInstance()->AllocateObjectID();

    Archetype::Equipment* const Contents = Archetype::GetEquipment(ContentsArchId);
    if (Contents == nullptr)
        return;
    Rec.ContentsArch = Contents;
    Rec.ContentsHitPts = Contents->HitPts;
    Rec.Count = Count;

    // A zero or unknown container falls back to the contents' loot appearance.
    Archetype::Equipment* Container = nullptr;
    if (ContainerArchId != 0u)
        Container = Archetype::GetEquipment(ContainerArchId);
    if (Container == nullptr)
    {
        Container = Contents->get_loot_appearance();
        if (Container == nullptr)
            return;
    }
    Rec.Arch = Container;
    Rec.HitPts = Container->HitPts;

    Rec.Position = Pos;

    Rec.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                             0.0f, 1.0f, 0.0f,
                             0.0f, 0.0f, 1.0f);

    Rec.Velocity = random_inside_sphere(20.0f);

    Rec.AngularVelocity = random_inside_sphere(3.0f);

    create_loot(0u, 3, &Rec, 1);                                    // FUN_06d08a40
}

// StarSystem::send_comm -- FUN_06d0db30. With no sender (id & 0x3fffffff == 0) every valid
// player gets it in single player, when addressed, or on a broadcast. Otherwise the
// addressee gets it (and a non-broadcast stops there), and a broadcast reaches every
// player within 10000 of the sender (an unordered compare sends). x87: the squared
// distance (dz^2 + dx^2) + dy^2 is taken in double, as at MSVC's default precision.
void StarSystem::send_comm(unsigned int SenderId, unsigned int ReceiverId, unsigned int VoiceId,
                           Costume const& Cost, unsigned int CaptionIds, unsigned int* TextIds,
                           int TextCount, unsigned int PermutationIds, float Duration, bool bBroadcast)
{
    bool bNoSender = false;
    Vector SenderPos = {};
    if ((SenderId & 0x3fffffffu) == 0) {
        bNoSender = true;
    } else {
        GameObject* const Sender = find_object(SenderId);
        if (Sender == nullptr)
            return;
        SenderPos = Sender->get_position();
    }

    for (SystemPlayerNode* Node = PlayerListHead->Next;
         Node != PlayerListHead; Node = Node->Next)
    {
        unsigned int const PlayerId = Node->View.PlayerId;
        if (!Players.is_valid(PlayerId))
            continue;
        unsigned int const ShipId = Players[PlayerId].ship_object_id;

        if (bNoSender) {
            if (SinglePlayer() || ReceiverId == ShipId || bBroadcast)
                Node->View.send_comm(SenderId, ReceiverId, VoiceId, Cost,
                                     CaptionIds, TextIds, TextCount, PermutationIds, Duration);
            continue;
        }

        if (ReceiverId == ShipId) {
            Node->View.send_comm(SenderId, ReceiverId, VoiceId, Cost,
                                 CaptionIds, TextIds, TextCount, PermutationIds, Duration);
            if (!bBroadcast)
                return;
            continue;
        }

        if (!bBroadcast)
            continue;
        GameObject* const Ship = find_object(ShipId);
        if (Ship == nullptr)
            continue;
        Vector const& Pos = Ship->get_position();
        double const Dx = static_cast<double>(Pos.x) - static_cast<double>(SenderPos.x);
        double const Dy = static_cast<double>(Pos.y) - static_cast<double>(SenderPos.y);
        double const Dz = static_cast<double>(Pos.z) - static_cast<double>(SenderPos.z);
        double const DistSq = (Dz * Dz + Dx * Dx) + Dy * Dy;
        if (DistSq > static_cast<double>(1.0e8f))
            continue;
        Node->View.send_comm(SenderId, ReceiverId, VoiceId, Cost,
                             CaptionIds, TextIds, TextCount, PermutationIds, Duration);
    }
}

// Slot 1 -- LAB_06d0dd40, a tail jump into find_object.
IObjRW* __thiscall StarSystem::FindById(unsigned const* ObjectId)
{
    return find_object(*ObjectId);
}

// ===========================================================================
// StarSystem as IObjDB -- the four slots of .rdata:06d67cd0.
// ===========================================================================

// The walk state Common hands to slot 0 ({0, 0, mask} at the start): the node last
// returned, its list, and the type mask, which slot 0 re-reads before every list.
struct FStarSystemObjDbCursor {
    FSystemListNode* Node;             // +0x00
    ObjectList* List;                  // +0x04
    unsigned int Mask;                 // +0x08
};

// FUN_06d0dd50 (cdecl). A walk already running in ANOTHER list returns null at once,
// cursor untouched. With bFilter, a non-zero get_type result or no common bit skips the
// node (retail uses the bFilter stack slot as get_type's out parameter). A null Data ends
// this list's walk like running off its end.
static GameObject* StarSystem_ObjDbStep(unsigned int Mask, ObjectList* List,
                                        FSystemListNode** CursorNode,
                                        ObjectList** CursorList, bool bFilter)
{
    if (*CursorList != nullptr && *CursorList != List)
        return nullptr;

    FSystemListNode* Node = *CursorNode;
    GameObject* Data = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? List->Head : Node->Next;
        if (Node == nullptr) {
            Data = nullptr;
            break;
        }
        Data = Node->Data;
        if (bFilter) {
            unsigned int Bits = 0;
            if (Data->get_type(Bits) != 0)
                continue;
            if ((Bits & Mask) == 0)
                continue;
        }
        if (Data != nullptr) {
            *CursorNode = Node;
            *CursorList = List;
            return Data;
        }
        break;
    }
    *CursorNode = nullptr;
    *CursorList = nullptr;
    return Data;
}

// Slot 0 -- FUN_06d0dde0. Each list is tried when its mask test passes, in this order; the
// first object found wins. Ships and solars are filtered unless the mask covers their
// whole group; the rest are unfiltered.
IObjRW* __thiscall StarSystem::GetNext(void* IterState)
{
    FStarSystemObjDbCursor* const Cursor = static_cast<FStarSystemObjDbCursor*>(IterState);
    GameObject* Found = nullptr;

    unsigned int Mask = Cursor->Mask;
    if ((Mask & OBJ_SHIP_MASK) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Ships, &Cursor->Node, &Cursor->List,
                                     (Mask & OBJ_SHIP_MASK) != OBJ_SHIP_MASK);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & OBJ_SOLAR_NO_WAYPOINT_MASK) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Solars, &Cursor->Node, &Cursor->List,
                                     (Mask & OBJ_SOLAR_NO_WAYPOINT_MASK) != OBJ_SOLAR_NO_WAYPOINT_MASK);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & OBJ_LOOT) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Loot, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & OBJ_GUIDED) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &GuidedMunitions, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & OBJ_BULLET) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Beams, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & OBJ_MINE) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &Mines, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & OBJ_COUNTERMEASURE) != 0) {
        Found = StarSystem_ObjDbStep(Mask, &CounterMeasures, &Cursor->Node, &Cursor->List, false);
        if (Found != nullptr)
            return Found;
    }
    Mask = Cursor->Mask;
    if ((Mask & OBJ_ASTEROID) != 0)
        Found = StarSystem_ObjDbStep(Mask, &MedAsteroids, &Cursor->Node, &Cursor->List, false);
    return Found;
}

// FUN_06d0df40. Callers: IServerImpl::GoTradelane and Ship go_tradelane.
// Pass 1 over the ships: the lane ship itself fires controller event 0x16 and
// use_tradelane(true), and moves on; every other unowned ship's behavior manager gets a
// type-4 "entered a lane" event (get_behavior_interface called with no test on the
// narrowing). Pass 2 sends GOTRADELANE to every player but the owner who can see the ship.
void StarSystem::notify_go_tradelane(unsigned int ShipId, unsigned int EntranceId,
                                     unsigned int ExitId, bool bFastExit, float Speed)
{
    unsigned int SkipPlayer = 0u;
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->get_id() == ShipId)
        {
            SkipPlayer = Obj->csimple()->OwnerPlayer;
            unsigned int Msg[4];
            Msg[0] = ShipId;
            Msg[1] = EntranceId;
            Msg[2] = ExitId;
            Msg[3] = SystemInfo->NicknameId;
            Server_ControllerDispatch(0, pub::Controller::EMessage::TradelaneEnter, Msg);
            CObject* const Raw = Obj->Object;
            if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                static_cast<CShip*>(Raw)->use_tradelane(true);
            continue;
        }

        if (Obj->csimple()->OwnerPlayer != 0u)
            continue;
        CObject* const Raw = Obj->Object;
        CEqObj* const Ship =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                ? static_cast<CEqObj*>(Raw) : nullptr;
        IBehaviorManager* const Behavior = Ship->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        // Byte +3 is never written.
        FBehaviorTradelaneEvent Payload;
        Payload.EntranceId = EntranceId;
        Payload.bFastExit = static_cast<unsigned char>(bFastExit);
        Payload.Speed = Speed;
        Payload.bExitRequested = 0u;
        Payload.bEnteringLane = 1u;
        Payload.Field_0x08 = 0u;
        Payload.ExitId = ExitId;
        Payload.ShipId = ShipId;
        FBehaviorManagerEvent LaneEvent;
        LaneEvent.Type = FBehaviorManagerEvent::kEventTradelane;
        LaneEvent.Data = &Payload;
        Behavior->OnEvent(&LaneEvent);
    }

    XGoTradelane Record = {};
    Record.ObjectId = ShipId;
    Record.TradelaneRing1 = EntranceId;
    Record.TradelaneRing2 = ExitId;
    Record.bMidLane = bFastExit;
    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (SkipPlayer == Player->View.PlayerId)
            continue;
        if (!Player->View.is_object_id_visible(ShipId))
            continue;
        g_pClientInterface->SendCommonGoTradeLane(Player->View.PlayerId, Record);
    }
}

// FUN_06d0e120. Callers: IServerImpl::StopTradelane and Ship stop_tradelane. NOT the same
// shape as notify_go_tradelane: after event 0x17 and use_tradelane(false) the lane ship
// FALLS THROUGH into the second test, so every live unowned ship, the matched one
// included, gets the type-4 "left the lane" event.
void StarSystem::notify_stop_tradelane(unsigned int ShipId, unsigned int EntranceId,
                                       unsigned int ExitId)
{
    unsigned int SkipPlayer = 0u;
    FSystemListNode* Node = nullptr;
    for (;;)
    {
        Node = (Node == nullptr) ? Ships.Head : Node->Next;
        if (Node == nullptr)
            break;
        GameObject* const Obj = Node->Data;
        if (Obj->get_id() == ShipId)
        {
            SkipPlayer = Obj->csimple()->OwnerPlayer;
            unsigned int Msg[4];
            Msg[0] = ShipId;
            Msg[1] = EntranceId;
            Msg[2] = ExitId;
            Msg[3] = SystemInfo->NicknameId;
            Server_ControllerDispatch(0, pub::Controller::EMessage::TradelaneExit, Msg);
            CObject* const Raw = Obj->Object;
            if (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                static_cast<CShip*>(Raw)->use_tradelane(false);
        }

        if (Obj->csimple()->OwnerPlayer != 0u)
            continue;
        if (Obj->is_destroyed())
            continue;
        CObject* const Raw = Obj->Object;
        CEqObj* const Ship =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                ? static_cast<CEqObj*>(Raw) : nullptr;
        IBehaviorManager* const Behavior = Ship->get_behavior_interface();
        if (Behavior == nullptr)
            continue;
        // The "left the lane" form; byte +3 is never written.
        FBehaviorTradelaneEvent Payload;
        Payload.EntranceId = EntranceId;
        Payload.bFastExit = 0u;
        Payload.Speed = 0.0f;
        Payload.ExitId = 0u;
        Payload.bEnteringLane = 0u;
        Payload.bExitRequested = 0u;
        Payload.Field_0x08 = 0u;
        Payload.ShipId = ShipId;
        FBehaviorManagerEvent LaneEvent;
        LaneEvent.Type = FBehaviorManagerEvent::kEventTradelane;
        LaneEvent.Data = &Payload;
        Behavior->OnEvent(&LaneEvent);
    }

    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (SkipPlayer == Player->View.PlayerId)
            continue;
        if (!Player->View.is_object_id_visible(ShipId))
            continue;
        g_pClientInterface->SendCommonStopTradeLane(Player->View.PlayerId, ShipId, EntranceId, ExitId);
    }
}

// StarSystem::notify_fire_weapon -- retail FUN_06d0e2e0, from SrvGun::fire_forward.
void StarSystem::notify_fire_weapon(unsigned int const& ObjId, std::vector<unsigned short> const& EquipIds,
                                    Vector const& Target, int SourceClient)
{
    XFireWeaponInfo Info;
    Info.ObjectId = ObjId;
    Info.TargetPos = Target;

    // VC6's operator= into an empty vector, as std::vector's.
    Info.Hardpoints = EquipIds;

    // Every player but SourceClient that can see the shooter.
    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Player = Head->Next; Player != Head; Player = Player->Next)
    {
        if (Player->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!Player->View.is_object_id_visible(ObjId))
            continue;
        g_pClientInterface->SendCommonFireWeapon(Player->View.PlayerId, Info);
    }
}

// ===========================================================================
// The IObjAffect notifiers. Each walks the player list with no null test on the head and
// calls g_pClientInterface unguarded. The player-skip forms compare the view's player with
// *SourceClient, the client that asked for the change, so it is not told of its own action.
// ===========================================================================

// FUN_06d0e500; only caller EqObj slot_4c. The record's pad byte is uninitialised in
// retail, zero here.
void StarSystem::notify_activate_equip(unsigned int const& ObjId, unsigned short const& EquipId,
                                       bool bActivate, int const& SourceClient)
{
    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!Node->View.is_object_id_visible(ObjId))
            continue;
        XActivateEquip Record = {};
        Record.ObjectId = ObjId;
        Record.EquipId = EquipId;
        Record.bActivate = bActivate;
        g_pClientInterface->SendCommonActivateEquip(Node->View.PlayerId, Record);
    }
}

// FUN_06d0e580; only caller Ship slot_50.
void StarSystem::notify_cruise(unsigned int const& ObjId, bool bEngage, bool bArg2,
                               int const& SourceClient)
{
    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!Node->View.is_object_id_visible(ObjId))
            continue;
        XActivateCruise Record = {};
        Record.ObjectId = ObjId;
        Record.bActivate = bEngage;
        Record.bDisruptCruise = bArg2 ? 1u : 0u;
        g_pClientInterface->SendCommonActivateCruise(Node->View.PlayerId, Record);
    }
}

// FUN_06d0e600; only caller Ship slot_54.
void StarSystem::notify_thrusters(unsigned int const& ObjId, bool bActivate, int const& SourceClient)
{
    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (!Node->View.is_object_id_visible(ObjId))
            continue;
        XActivateThrusters Record = {};
        Record.ObjectId = ObjId;
        Record.bActivate = bActivate;
        g_pClientInterface->SendCommonActivateThrusters(Node->View.PlayerId, Record);
    }
}

// FUN_06d0e670. Callers: Ship set_target, EqObj slot_44, Guided set_target. A missing
// object is tested per node, not once. A solar type (& 0xfdff) is sent without the
// visibility test; anything else must be visible.
void StarSystem::notify_set_target(unsigned int const& ObjId, unsigned short const& SubObjId,
                                   IObjRW* Target, unsigned short const& SubTarget,
                                   int const& SourceClient)
{
    GameObject* const Obj = Server_LookupGameObject(ObjId);
    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (Node->View.PlayerId == static_cast<unsigned int>(SourceClient))
            continue;
        if (Obj == nullptr)
            continue;
        unsigned int Type = 0u;
        Obj->get_type(Type);
        if ((Type & OBJ_SOLAR_NO_WAYPOINT_MASK) == 0u && !Node->View.is_object_visible(Obj))
            continue;
        XSetTarget Record = {};
        Record.ObjectId = ObjId;
        Record.SubObjId = SubObjId;
        Record.TargetObjectId = (Target != nullptr) ? Target->get_id() : 0u;
        Record.TargetSubObjId = SubTarget;
        g_pClientInterface->SendCommonSetTarget(Node->View.PlayerId, Record);
    }
}

// FUN_06d0e750.
void StarSystem::broadcast_fuse(FServerBurnFuseInfo const& Info)
{
    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (!Node->View.is_object_id_visible(Info.ObjectId))
            continue;
        g_pClientInterface->SendServerBurnFuse(Node->View.PlayerId, Info);
    }
}

// FUN_06d0e7a0. See server_sysdb.h for the map.
void StarSystem::send_tractor_objects(unsigned int const& SkipPlayerId, XTractorObjects const& Info)
{
    // Every player but SkipPlayerId that sees the tractoring object gets the target ids it
    // can see (sent even when none survive). The pad word +0x06 is never written.
    for (SystemPlayerNode* Node = PlayerListHead->Next;
         Node != PlayerListHead; Node = Node->Next)
    {
        if (Node->View.PlayerId == SkipPlayerId)
            continue;

        if (!Node->View.is_object_id_visible(Info.ObjectId))
            continue;

        XTractorObjects Msg;
        Msg.ObjectId = Info.ObjectId;
        Msg.TractorEquipId = Info.TractorEquipId;

        int const Count = static_cast<int>(Info.TargetIds.size());
        for (int Index = 0; Index < Count; ++Index)
        {
            if (Node->View.is_object_id_visible(Info.TargetIds[Index]))
                Msg.TargetIds.push_back(Info.TargetIds[Index]);
        }

        ::g_pClientInterface->SendCommonTractorObjects(Node->View.PlayerId, Msg);
    }
}

// StarSystem::add_cargo -- retail FUN_06d0e8d0.
unsigned short StarSystem::add_cargo(GameObject* Obj, unsigned int ArchId,
                                     unsigned int Count, float Health, bool bMission)
{
    // No null test on Obj.
    CECargo* Stack = nullptr;
    bool bCombined = false;

    // The mission flag SUPPRESSES the combine, so such an item always becomes its own entry.
    if (arch_is_combinable(ArchId) && !bMission)
    {
        // No test of the narrowing.
        CEqObj* const EqObj = Obj->ceqobj();
        {
            CEquipTraverser Walk(EquipTypeCargo);
            CEquipManager& Mgr = EqObj->EquipManager;
            for (CEquip* Item = Mgr.Traverse(Walk); Item != nullptr;
                 Item = Mgr.Traverse(Walk))
            {
                // No test of Arch.
                if (Item->Arch->get_id() != ArchId)
                    continue;

                // Retail trusts the traverser's cargo mask and reads Count with no cast;
                // CECargo::cast cannot disagree for that mask.
                CECargo* const Cargo = CECargo::cast(Item);
                if (Cargo != nullptr && Cargo->Count != 0)
                {
                    Cargo->AddToStack(Count, Health);
                    Stack = Cargo;
                    bCombined = true;
                }
                else
                {
                    // Then fall through to the "make a new one" path below.
                    Mgr.CleanUp();
                }
                break;
            }
        }
    }

    // The owning player's slot, resolved the same way on both paths: a live client whose
    // ship is this object. OwnerPlayer read unguarded.
    unsigned int const SessionId = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();
    PlayerSlot* Slot = (SessionId != 0) ? PlayerDB_GetPlayer(SessionId) : nullptr;
    if (Slot != nullptr && (Slot->client_id == 0 || Slot->ship_object_id != ObjId))
        Slot = nullptr;

    if (bCombined)
    {
        // FUN_06d4f230: add_equipment with the cargo-bay hardpoint baked in. The combine
        // path forces bMission and bNotify both to zero.
        if (Slot != nullptr)
        {
            Slot->add_equipment(ArchId, EquipDesc::CARGO_BAY_HP_NAME,
                                (int)Count, Health, /*bEquipped*/false,
                                /*bMission*/false, /*bNotify*/false);
        }
    }
    else
    {
        // Here the mission flag DOES travel on.
        unsigned short EquipId = SUBOBJ_ID_NONE;
        if (Slot != nullptr)
        {
            EquipId = Slot->add_equipment(ArchId,
                                          EquipDesc::CARGO_BAY_HP_NAME,
                                          (int)Count, Health, /*bEquipped*/false,
                                          bMission, /*bNotify*/false);
        }
        else
        {
            // No class test: retail assumes anything reaching here without a player slot
            // is a Ship (FUN_06d01c30).
            EquipId = static_cast<Ship*>(Obj)->allocate_equip_id();
        }

        // Built in retail's own order.
        EquipDesc Desc;
        Desc.set_id(EquipId);
        Desc.set_arch_id(ArchId);
        Desc.set_count((int)Count);
        Desc.set_status(Health);
        Desc.set_equipped(false);
        // A direct byte store in retail (no setter import), so the in-space path DOES put
        // AddCargo's mission flag into EquipDesc::bMission.
        Desc.bMission = bMission;
        Desc.make_internal();

        // No test of the narrowing.
        CEqObj* const EqObj = Obj->ceqobj();
        if (EqObj->add_item(Desc))
        {
            Stack = CECargo::cast(EqObj->EquipManager.FindByID(EquipId));
        }
    }

    // The broadcast, from a SECOND EquipDesc filled by the cargo's own GetEquipDesc; with
    // no cargo or no description retail FDUMPs and sends nothing.
    EquipDesc Wire;
    bool const bHaveDesc = (Stack != nullptr) && Stack->GetEquipDesc(Wire);
    if (bHaveDesc)
    {
        // Every player that can see the object; no null tests.
        SystemPlayerNode* const Sentinel = PlayerListHead;
        {
            for (SystemPlayerNode* Node = Sentinel->Next;
                 Node != Sentinel; Node = Node->Next) {
                if (!Node->View.is_object_visible(Obj))
                    continue;
                // The first argument goes by ADDRESS.
                g_pClientInterface->SendServerObjectCargoUpdate(Node->View.PlayerId,
                                                                Obj->get_id(), Wire);
            }
        }
    } else {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Server\\space\\StarSys.cpp", 0xd8e,
              "StarSys::add_cargo().  Failed to get client cargo description.");
    }

    return (Stack != nullptr) ? Stack->Id : SUBOBJ_ID_NONE;
}

// StarSystem::remove_cargo -- retail FUN_06d0ebf0.
void StarSystem::remove_cargo(GameObject* Obj, unsigned short EquipId, int Count)
{
    // No test on the narrowing; only FindByID's result is tested.
    CEqObj* const EqObj = Obj->ceqobj();
    CEquip* const Item = EqObj->EquipManager.FindByID(EquipId);
    if (Item == nullptr)
        return;

    // Unlike add_cargo, the remove path really casts: FindByID is not filtered by type.
    CECargo* const Cargo = CECargo::cast(Item);
    if (Cargo == nullptr)
        return;
    // An empty stack is left alone entirely.
    if (Cargo->Count == 0)
        return;

    // Filled from the cargo first...
    EquipDesc Wire;
    Cargo->GetEquipDesc(Wire);

    // Ghidra passes param_2 here; the raw bytes load the THIRD argument.
    Cargo->RemoveFromStack((unsigned)Count);

    // ...then patched with what is left.
    Wire.set_count((int)Cargo->Count);
    Wire.set_status((float)Cargo->GetRelativeHealth());

    // OwnerPlayer read unguarded; the player's copy loses the items without the notify.
    unsigned int const SessionId = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();
    if (SessionId != 0u && Players.is_valid_ship_owner(SessionId, ObjId))
        Players[SessionId].remove_equipment(EquipId, Count, /*bNotify*/false);

    for (SystemPlayerNode* Node = PlayerListHead->Next;
         Node != PlayerListHead; Node = Node->Next) {
        if (!Node->View.is_object_visible(Obj))
            continue;
        // The player id goes by ADDRESS.
        g_pClientInterface->SendServerObjectCargoUpdate(Node->View.PlayerId, Obj->get_id(), Wire);
    }
}

// FUN_06d0ed40. See server_sysdb.h for the map.
void StarSystem::tractor_loot(::Loot* LootObj, EqObj* Ship, unsigned int Space, unsigned short TractorId)
{
    // Not null-tested before use.
    CLoot* const Cl = (LootObj != nullptr) ? LootObj->cloot() : nullptr;

    // Take = min(Space, Units), unsigned.
    Archetype::Equipment const* const Contents = Cl->ContentsArch;
    unsigned int const Units = Cl->Units;
    unsigned int const ContentsArchId = Contents->get_id();
    unsigned int Take = Space;
    if (Take > Units)
        Take = Units;

    float Health = 1.0f;

    // The carried health.
    if (!arch_is_combinable(ContentsArchId))
    {
        Health = Cl->ContentsHitPts / Contents->HitPts;
    }
    else if (Contents->get_class_type() == Archetype::AClassCommodity)
    {
        // Only a strictly positive decay, and only when the whole stack is taken.
        Archetype::Commodity const* const CommodityArch =
            static_cast<Archetype::Commodity const*>(Contents);
        if (CommodityArch->get_decay_per_second() > 0.0f && Take == Units)
        {
            // x87: the difference stays at extended precision until the final float store.
            double Remaining = static_cast<double>(Cl->ContentsHitPts) -
                               static_cast<double>(LootObj->DecayAccum);
            if (!(Remaining > 0.0))
                Remaining = 0.0;
            Health = static_cast<float>(Remaining / static_cast<double>(Contents->HitPts));
        }
    }

    // The CLoot's bTemporary is the mission flag.
    unsigned short const SubObjId = add_cargo(Ship, ContentsArchId, Take, Health, Cl->bTemporary);

    // Controller event 0x18. The word at +0x0a is never written.
    struct FControllerMsg18 {
        unsigned int LootId;          // +0x00
        unsigned int ShipId;          // +0x04
        unsigned short SubObjId;      // +0x08  add_cargo's result
        unsigned short Pad0a;         // +0x0a
        unsigned int GoodId;          // +0x0c
        unsigned int Count;           // +0x10  Take
    } Msg18;
    Msg18.LootId = LootObj->get_id();
    Msg18.ShipId = Ship->get_id();
    Msg18.GoodId = Arch2Good(ContentsArchId);
    Msg18.SubObjId = SubObjId;
    Msg18.Count = Take;
    Server_ControllerDispatch(0, pub::Controller::EMessage::LootAcquired, &Msg18);

    // The inlined constructor's defaults, then overwritten field by field.
    FPayloadServerItemTractored Tractored;
    Tractored.LootId = 0u;
    Tractored.ShipId = 0u;
    Tractored.TractorEquipId = SUBOBJ_ID_NONE;
    Tractored.UnitsTaken = 0;
    Tractored.LootId = LootObj->get_id();
    Tractored.ShipId = Ship->get_id();
    Tractored.TractorEquipId = TractorId;
    Tractored.UnitsTaken = static_cast<unsigned short>(Take);

    // Every player that sees the ship.
    for (SystemPlayerNode* Node = PlayerListHead->Next;
         Node != PlayerListHead; Node = Node->Next)
    {
        if (!Node->View.is_object_id_visible(Tractored.ShipId))
            continue;
        ::g_pClientInterface->SendServerItemTractored(Node->View.PlayerId, Tractored);
    }

    // Part of the stack: the rest stays in space.
    if (Take != Units)
    {
        Cl->Units = Units - Take;
        return;
    }

    // A player's ship in single player keeps its tractor target (CSimple not null-tested).
    if (!(Ship->csimple()->OwnerPlayer != 0 && SinglePlayer()))
    {
        // The CShip narrowing is not null-tested before its EquipManager is used.
        CObject* const Raw = Ship->Object;
        CShip* const ShipCobj =
            (Raw != nullptr && (static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                ? static_cast<CShip*>(Raw) : nullptr;
        CETractor* const Tractor = CETractor::cast(ShipCobj->EquipManager.FindByID(TractorId));
        if (Tractor != nullptr)
            Tractor->RemoveTarget(LootObj->get_id());
    }

    unsigned int const ShipId = Ship->get_id();
    LootObj->destroy(DESTROY_TRACTORED, ShipId);

    Cl->Units = 0u;
}

// FUN_06d0efe0. See server_sysdb.h for the map.
void StarSystem::use_item(GameObject* Obj, unsigned short EquipId, int Count)
{
    // The (& 3) narrowing is not null-tested.
    unsigned int const OwnerPlayer = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();

    if (OwnerPlayer != 0u && Players.is_valid_ship_owner(OwnerPlayer, ObjId))
        Players[OwnerPlayer].remove_equipment(EquipId, Count, false);

    // get_id() is called again here.
    FPayloadServerUseItem Rec;
    Rec.ObjectId = Obj->get_id();
    Rec.EquipId = EquipId;
    Rec.Count = static_cast<unsigned short>(Count);

    for (SystemPlayerNode* Node = PlayerListHead->Next;
         Node != PlayerListHead; Node = Node->Next)
    {
        if (!Node->View.is_object_id_visible(Rec.ObjectId))
            continue;
        ::g_pClientInterface->SendServerUseItem(Node->View.PlayerId, Rec);
    }
}

// FUN_06d0f0c0; only caller Ship slot_38 (the jettison). OwnerPlayer is read unguarded;
// a player's ship loses the items without the notify. No player skip here.
void StarSystem::notify_jettison(GameObject* Obj, unsigned short EquipId, int Count)
{
    unsigned int const Owner = Obj->csimple()->OwnerPlayer;
    unsigned int const ObjId = Obj->get_id();
    if (Players.is_valid_ship_owner(Owner, ObjId))
        Players[Owner].remove_equipment(EquipId, Count, false);

    XJettisonCargo Record = {};
    Record.ObjectId = ObjId;
    Record.ItemId = EquipId;
    Record.Count = static_cast<unsigned int>(Count);

    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
    {
        if (!Node->View.is_object_id_visible(ObjId))
            continue;
        g_pClientInterface->SendCommonJettisonCargo(Node->View.PlayerId, Record);
    }
}

// FUN_06d0f190 (thiscall on the StarSystem).
void StarSystem::collect_created_owners(unsigned int PlayerId, std::set<unsigned int>* Owners)
{
    SystemPlayerNode* const Head = PlayerListHead;
    for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next) {
        if (Node->View.PlayerId == PlayerId) {
            Node->View.collect_created_owners(Owners);
            return;
        }
    }
}

// FUN_06d0f1c0 (thiscall on the StarSystem). Callers CPlayerGroup::AddMember and DelMember.
void StarSystem::send_group_change(unsigned int ObjId)
{
    SystemPlayerNode* const Sentinel = PlayerListHead;
    for (SystemPlayerNode* Node = Sentinel->Next; Node != Sentinel; Node = Node->Next)
        Node->View.send_group_change(ObjId);
}

// The two passes retail runs over each list: pass 1 deletes every non-null Data (without
// clearing it), pass 2 frees the nodes and zeroes Head/Tail.
static void StarSystem_DeleteBlock(ObjectList& List)
{
    FSystemListNode* Node = nullptr;
    for (;;) {
        Node = (Node == nullptr) ? List.Head : Node->Next;
        if (Node == nullptr)
            break;
        if (Node->Data != nullptr)
            StarSystem_DeleteObject(Node->Data);
    }

    Node = List.Head;
    while (Node != nullptr) {
        FSystemListNode* const Current = Node;
        Node = Node->Next;
        List.free_node(Current);
    }
    List.Tail = nullptr;
    List.Head = nullptr;
}

void StarSystem::unload()
{
    // FUN_06d0f390: erase the whole player list (each view destroyed in place, flag 0).
    SystemPlayerNode* const Sentinel = PlayerListHead;
    SystemPlayerNode* View = Sentinel->Next;
    while (View != Sentinel) {
        SystemPlayerNode* const Node = View;
        View = View->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        Observer* const Element = &Node->View;
        Element->~Observer();
        ::operator delete(Node);
        --PlayerListSize;
    }

    // Six lists in retail's order.
    StarSystem_DeleteBlock(Ships);           // +0x34
    StarSystem_DeleteBlock(Loot);            // +0x48
    StarSystem_DeleteBlock(GuidedMunitions); // +0x70
    StarSystem_DeleteBlock(Beams);           // +0x84
    StarSystem_DeleteBlock(Mines);           // +0x98
    StarSystem_DeleteBlock(CounterMeasures); // +0xac

    // +0xe8: a forward traverse deleting every non-null Data, then MetaList::free.
    {
        FSystemListNode* Node = nullptr;
        while (Deferred.traverse(Node, true)) {
            if (Node->Data != nullptr)
                StarSystem_DeleteObject(Node->Data);
        }
        Deferred.free();
    }

    // FUN_06d07570: the solars, then the partition.
    StarSystem_DeleteBlock(Solars);
    if (Spatial != nullptr) {
        FreeSpatial(Spatial);
        Spatial = nullptr;
    }

    // FUN_06d075f0: drop one reference on the asteroid system (result dropped), then the
    // asteroid objects.
    unsigned int const SystemId = SystemInfo->NicknameId;
    SrvAsteroid::SrvAsteroid_UnloadSystem(SystemId);
    CmnAsteroid::Unload(SystemId);
    StarSystem_DeleteBlock(MedAsteroids);

    // FUN_06d0f2f0: clear the 0xC-byte POD vector.
    VectorEnd = VectorBegin;

    PhySys::Shutdown(SystemInfo->NicknameId);
}

// FUN_06d0f410. See server_sysdb.h.
SystemDockNode* StarSystem::erase_docks(SystemDockNode* First, SystemDockNode* Last)
{
    while (First != Last) {
        SystemDockNode* const Node = First;
        First = First->Next;
        Node->Prev->Next = Node->Next;
        Node->Next->Prev = Node->Prev;
        Node->Entry.~FSystemDockEntry();             // FUN_06d0d4a0
        ::operator delete(Node);
        --DockQueueSize;
    }
    return First;
}

// FUN_06d0fb40 -- SysDB initialization: a StarSystem per Universe system, inserted as a
// copy of a temporary and then init()ed (FUN_06d06ea0). The map holds the COPY, so every
// entry's ObjectLists have bUseHeap = 0 (server_sysdb.h). No "already initialised" latch,
// as in retail; its only caller runs once per server startup.
void SysDB_Initialize() {

    Universe::ISystem* sys = Universe::GetFirstSystem();
    while (sys != nullptr) {
        unsigned int const SysId = sys->NicknameId;
        StarSystem Temp;
        std::pair<SysDB::SysMapType::iterator, bool> const Inserted =
            SysDB::SysMap.insert(SysDB::SysMapType::value_type(SysId, Temp));
        Inserted.first->second.init(sys->NicknameId);

        sys = Universe::GetNextSystem();
    }
}

// FUN_06d10030 (the map pair's destructor inlines it). The ObjectLists' own destructors run
// afterwards in member order. Nothing deletes the objects the lists point at, and the
// partition is not freed here: unload() does both.
StarSystem::~StarSystem()
{
    ::operator delete(VectorBegin);
    VectorBegin = nullptr;
    VectorEnd = nullptr;
    VectorCapacity = nullptr;

    SystemDockNode* const DockHead = DockQueueHead;
    erase_docks(DockHead->Next, DockHead);
    ::operator delete(DockQueueHead);
    DockQueueHead = nullptr;
    DockQueueSize = 0;

    // FUN_06d0f230.
    SystemPlayerNode* const Sentinel = PlayerListHead;
    SystemPlayerNode* Node = Sentinel->Next;
    while (Node != Sentinel) {
        SystemPlayerNode* const Current = Node;
        Node = Node->Next;
        Current->Prev->Next = Current->Next;
        Current->Next->Prev = Current->Prev;
        Observer* const Element = &Current->View;
        Element->~Observer();
        ::operator delete(Current);
        --PlayerListSize;
    }
    ::operator delete(PlayerListHead);
    PlayerListHead = nullptr;
    PlayerListSize = 0;
    // The ten ObjectLists are destroyed after this body; retail runs them first. The
    // order is not observable: a list's destructor frees only its own nodes.
}

// FUN_06d10200 -- SysDB teardown, called only by Server_CleanupPhysics (FUN_06cffff0):
// StarSystem::shutdown (FUN_06d07130) on every loaded system, then the inlined
// map erase(begin, end), i.e. ~StarSystem (FUN_06d10030) per node. (FUN_06d0fa60 is
// SysMap's atexit destructor, not this.)
void SysDB_Shutdown() {
    for (SysDB::SysMapType::iterator It = SysDB::SysMap.begin();
         It != SysDB::SysMap.end(); ++It) {
        if (It->second.bLoaded != 0)
            It->second.shutdown();
    }
    SysDB::SysMap.clear();
}

// FUN_06d10320 (cdecl) -- the per-frame walk. Outside single player AnimDB::Update runs
// first. A function-local static vector (DAT_06d8da18) remembers the systems already
// stepped this frame; every active player's system is stepped once, with no loaded test
// and no cap, as in retail (retail's redundant third find has no effect). The vector is
// emptied, its storage kept.
void Server_StepSystems(float dt) {
    if (!SinglePlayer())
        AnimDB::Update(dt);

    static std::vector<unsigned int> SteppedSystems;

    PlayerData* Player = Players.traverse_active(nullptr);
    while (Player != nullptr)
    {
        const unsigned int SysId = Player->system_id;

        bool bAlready = false;
        for (size_t i = 0; i < SteppedSystems.size(); ++i)
        {
            if (SteppedSystems[i] == SysId)
            {
                bAlready = true;
                break;
            }
        }

        if (!bAlready)
        {
            SysDB::SysMapType::iterator It = SysDB::SysMap.find(SysId);
            if (It != SysDB::SysMap.end())
            {
                It->second.update(dt);
            }

            bool bPresent = false;
            for (size_t i = 0; i < SteppedSystems.size(); ++i)
            {
                if (SteppedSystems[i] == SysId)
                {
                    bPresent = true;
                    break;
                }
            }
            if (!bPresent)
                SteppedSystems.push_back(SysId);
        }

        Player = Players.traverse_active(Player);
    }

    SteppedSystems.clear();
}

// FUN_06d10520; FUN_06d11330 inlines the same body for the map node. Both player views and
// dock entries are copied; the partition POINTER is shared; the vector is copied with a
// block allocated even for 0 elements; the ten ObjectLists come out EMPTY with bUseHeap = 0
// (nothing of Other's is copied).
StarSystem::StarSystem(StarSystem const& Other)
    : IObjDB(),
      SystemInfo(Other.SystemInfo),
      ListAlloc(Other.ListAlloc),
      PlayerListHead(StarSystem_NewPlayerSentinel()),
      PlayerListSize(0),
      DockQueueAlloc(Other.DockQueueAlloc),
      DockQueueHead(StarSystem_NewDockSentinel()),
      DockQueueSize(0),
      Spatial(Other.Spatial),
      VectorAlloc(Other.VectorAlloc),
      VectorBegin(nullptr),
      VectorEnd(nullptr),
      VectorCapacity(nullptr),
      Ships(Other.Ships),
      Loot(Other.Loot),
      Solars(Other.Solars),
      GuidedMunitions(Other.GuidedMunitions),
      Beams(Other.Beams),
      Mines(Other.Mines),
      CounterMeasures(Other.CounterMeasures),
      MedAsteroids(Other.MedAsteroids),
      DockingSolars(Other.DockingSolars),
      Deferred(Other.Deferred),
      bLoaded(Other.bLoaded)
{
    Pad1[0] = Pad1[1] = Pad1[2] = 0;
    Pad2[0] = Pad2[1] = Pad2[2] = 0;
    Pad3[0] = Pad3[1] = Pad3[2] = 0;
    Pad4[0] = Pad4[1] = Pad4[2] = 0;

    // FUN_06d109c0: push_back of a copy of every player view.
    {
        SystemPlayerNode* const End = PlayerListHead;
        SystemPlayerNode* const SrcEnd = Other.PlayerListHead;
        for (SystemPlayerNode* Src = SrcEnd->Next; Src != SrcEnd; Src = Src->Next) {
            SystemPlayerNode* const Node =
                static_cast<SystemPlayerNode*>(::operator new(sizeof(SystemPlayerNode)));
            Node->Next = End;
            Node->Prev = (End->Prev != nullptr) ? End->Prev : Node;
            End->Prev = Node;
            Node->Prev->Next = Node;
            ::new (static_cast<void*>(&Node->View)) Observer(Src->View);   // FUN_06d0f6d0
            ++PlayerListSize;
        }
    }

    // FUN_06d10a30: the same for the dock queue (FUN_06d0f830 copies each entry).
    {
        SystemDockNode* const End = DockQueueHead;
        SystemDockNode* const SrcEnd = Other.DockQueueHead;
        for (SystemDockNode* Src = SrcEnd->Next; Src != SrcEnd; Src = Src->Next) {
            SystemDockNode* const Node =
                static_cast<SystemDockNode*>(::operator new(sizeof(SystemDockNode)));
            Node->Next = End;
            Node->Prev = (End->Prev != nullptr) ? End->Prev : Node;
            End->Prev = Node;
            Node->Prev->Next = Node;
            ::new (static_cast<void*>(&Node->Entry)) FSystemDockEntry(Src->Entry);
            ++DockQueueSize;
        }
    }

    // The 0xC-byte-element vector.
    int Count = 0;
    if (Other.VectorBegin != nullptr) {
        Count = static_cast<int>(
            (static_cast<char const*>(Other.VectorEnd) -
             static_cast<char const*>(Other.VectorBegin)) / 0xC);
    }
    if (Count < 0)
        Count = 0;
    VectorBegin = ::operator new(static_cast<size_t>(Count) * 0xC);
    size_t const Bytes = static_cast<size_t>(
        static_cast<char const*>(Other.VectorEnd) - static_cast<char const*>(Other.VectorBegin));
    if (Bytes != 0)
        memcpy(VectorBegin, Other.VectorBegin, Bytes);
    VectorEnd = static_cast<char*>(VectorBegin) + Bytes;
    VectorCapacity = VectorEnd;
}
