// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/common_net.h"
#include "../include/common_physys.h"
#include "../include/common_id.h"
#include "../include/common_server_playerdb.h"
#include "../include/common_ini.h"
#include "../include/common_misc.h"
#include "../include/x86math.h" // I3DMathEngine
#include "../include/common_universe.h"
#include "../include/server_sysdb.h"
#include "../include/server_gameobject.h" // GameObject's +0x154..+0x160 slots (StarSystem::remove_player/unload)
#include "../include/server_createinfo.h"
#include "../include/rpc_iclient.h"
#include "server_internal.h"
#include <windows.h>
#include <cmath>
#include <list>
#include "../include/server_iserverimpl.h"   // IServerImpl, Server
#include "../include/common_server_controller.h"
#include "../include/server_fuse.h"     // ServerFuseDB (FUN_06cfd4b0 / FUN_06cfd3a0)

// FUN_06cfffd0 (cdecl), called only from IServerImpl::Startup. SysDB_Initialize never reads
// the argument retail pushes; FUN_06cfd4b0 is the fuse DB load.
void Server_PlayerSubsystemInit(unsigned int max_players, unsigned int allow_pvp) {
    (void)max_players;
    GbAllowPvp = static_cast<unsigned char>(allow_pvp);
    SysDB_Initialize();
    ServerFuseDB::LoadAll();
}

StarSystem* GLookupCacheSystem = nullptr; // DAT_06d8d92c

// FUN_06cffff0, called only by IServerImpl::Shutdown. Only the StarSystem half of the
// lookup cache is cleared; the watcher half is left as it is.
void Server_CleanupPhysics() {
    ServerFuseDB::Clear();
    SysDB_Shutdown();
    GLookupCacheSystem = nullptr;
}

// The two teardown steps PlayerDB::cleanup and PlayerDB::logout share, retail FUN_06d00010
// (leave the system, via FUN_06d08890) and FUN_06d18880 (leave the base).
//
// FUN_06d00010 / FUN_06d08890: StarSystem::remove_player, flush the pending visits, event
// 0x3a when system_id was set, and in single player the last player out unloads the
// system (event 0x3d, StarSystem::unload).
//
// THE MISS ARM: on a SysMap miss retail runs FUN_06d08890 on the head node's value slot,
// which SysMap's constructor (FUN_06d104a0) never constructs, so it reads stale heap bytes
// (player-list head, ISystem*, loaded byte) and normally faults at .text:06d0874c. A miss
// needs a system id with no SysMap entry: +0x3e0 can come from IBase::SystemId, a bare
// CreateID of the base's `system` key that Common never checks against the system list.
// MSVC's std::map head is equally unconstructed (_Buyheadnode), so the port addresses that
// storage through the iterator's node pointer (MSVC-STL specific: _Ptr, _Myval) and runs
// the same body on it.
void PlayerDB_LeaveSystem(PlayerData& Slot, unsigned int SessionId) {
    unsigned int const SystemId = Slot.system_id;
    if ((SystemId & 0x3fffffff) == 0) {
        return;
    }

    // A key above the id is a miss too.
    SysDB::SysMapType::iterator It = SysDB::SysMap.lower_bound(SystemId);
    if (It == SysDB::SysMap.end() || SystemId < It->first) {
        It = SysDB::SysMap.end();
    }
    // KNOWN CRASH (intentional, retail-faithful, owner's decision): on a miss System is the
    // unconstructed storage of SysMap's head node -- the player's system id has no SysMap
    // entry; check universe.ini. See THE MISS ARM above.
    StarSystem& System = It._Ptr->_Myval.second; // KNOWN CRASH on SysMap miss, see above

    System.remove_player(SessionId);                               // FUN_06d08740

    Slot.flush_pending_visits();

    if (Slot.system_id != 0) {
        Slot.system_id = 0;
        unsigned int Payload[3];
        // KNOWN CRASH site on SysMap miss (garbage SystemInfo) -- see System above.
        Payload[0] = System.SystemInfo->NicknameId;
        Payload[1] = 0;
        Payload[2] = SessionId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, Payload);
    }

    // Single player: the last player out turns the system off.
    if (SinglePlayer() && System.bLoaded != 0) {
        System.bLoaded = 0;
        // KNOWN CRASH site on SysMap miss (garbage SystemInfo) -- see System above.
        unsigned int SystemNickname = System.SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemUnloaded, &SystemNickname);
        System.unload();                                           // FUN_06d072c0
    }
}

// ---------------------------------------------------------------------------
// The dock-exited ship list (server_gameobject.h). FUN_06d00090 / FUN_06d000d0 are
// this definition's construction and atexit destruction.
// ---------------------------------------------------------------------------
std::list<Ship*> GDockExitedShips;

// FUN_06d00130 (size at DAT_06d8d8e8).
void Ship_QueueDockExited(Ship* ExitedShip)
{
    GDockExitedShips.push_back(ExitedShip);
}

// FUN_06d00180: every matching node is removed -- the walk does not stop at
// the first match.
void Ship_ForgetDockExited(Ship* ExitedShip)
{
    std::list<Ship*>::iterator It = GDockExitedShips.begin();
    while (It != GDockExitedShips.end()) {
        if (*It == ExitedShip)
            It = GDockExitedShips.erase(It);
        else
            ++It;
    }
}

// FUN_06d001d0 (cdecl), called only by ElapseTime: step the systems, pump the phantom
// events (FUN_06cffdc0), then controller event 0x25 for every dock-exited ship (DAT_06d8d8e4)
// and empty that list. The system id is read BEFORE get_id.
void Server_PhysicsUpdate(float dt) {
    Server_StepSystems(dt);

    Server_PumpPhantomEvents();

    for (std::list<Ship*>::iterator It = GDockExitedShips.begin(); It != GDockExitedShips.end(); ++It) {
        Ship* const Exited = *It;
        unsigned int Event[3];
        Event[2] = Exited->OwnerSystem->SystemInfo->NicknameId;
        Event[0] = Exited->get_id();
        Event[1] = Exited->DockedObjId;                           // +0x90, the dock object
        Server_ControllerDispatch(0, pub::Controller::EMessage::Msg0x25, Event);
    }
    GDockExitedShips.clear();
}

// FUN_06d00280 -- look up a system in SysMap by id.
FStarSystem* SysDB_LookupSystem(unsigned int system_id) {
    if ((system_id & 0x3fffffff) == 0) return nullptr;
    SysDB::SysMapType::iterator it = SysDB::SysMap.find(system_id);
    if (it != SysDB::SysMap.end()) return &it->second;
    return nullptr;
}

// ---------------------------------------------------------------------------
// The objects between systems and the object lookup by id. See server_gameobject.h.
// ---------------------------------------------------------------------------

// Retail's CRT init LAB_06d002e0 / atexit FUN_06d00320 pair is this global std::list's
// constructor and destructor.
std::list<FInTransitObject> GInTransitObjects;

// FUN_06d006c0's one-entry lookup cache -- see server_sysdb.h. LAB_06cfff90 / FUN_06cfffb0
// are the watcher's CRT constructor and atexit destructor.
BaseWatcher GLookupCacheObject;           // DAT_06d8d8ec

// FUN_06d00380 (cdecl). The cache's system half is left alone.
bool Server_TakeInTransit(GameObject* Obj, PhySys::CreateParms& OutParms)
{
    std::list<FInTransitObject>::iterator It = GInTransitObjects.begin();
    while (It != GInTransitObjects.end() && It->Obj != Obj)
        ++It;
    if (It == GInTransitObjects.end())
        return false;

    OutParms = It->Parms;
    GInTransitObjects.erase(It);

    // set(nullptr) is set_pointer(0): something is watched on this path.
    Watchable const* const Watched = GLookupCacheObject.target();
    if (Watched != nullptr &&
        static_cast<GameObject const*>(static_cast<IObjRW const*>(Watched)) == Obj)
        GLookupCacheObject.set(nullptr);
    return true;
}

// FUN_06d00410 (cdecl); only caller pub::SpaceObj::Destroy. Controller message 0x39 with
// every field 0 but the id, then the scalar deleting destructor.
bool Server_DestroyInTransit(GameObject* Obj)
{
    std::list<FInTransitObject>::iterator It = GInTransitObjects.begin();
    while (It != GInTransitObjects.end() && It->Obj != Obj)
        ++It;
    if (It == GInTransitObjects.end())
        return false;

    GInTransitObjects.erase(It);

    Watchable const* const Watched = GLookupCacheObject.target();
    GameObject const* const Cached = (Watched != nullptr)
        ? static_cast<GameObject const*>(static_cast<IObjRW const*>(Watched))
        : nullptr;
    if (Cached == Obj)
        GLookupCacheObject.set(nullptr);

    FObjectDestroyedEvent Event;
    Event.KillerId = 0;
    Event.SystemId = 0;
    Event.OwnerPlayer = 0;
    Event.Kind = 0;
    Event.Position.x = 0.0f;
    Event.Position.y = 0.0f;
    Event.Position.z = 0.0f;
    Event.ObjId = Obj->get_id();
    Server_ControllerDispatch(0, pub::Controller::EMessage::ObjectDestroyed, &Event);

    delete Obj;
    return true;
}

// FUN_06d00560 (cdecl); only caller the system-transfer path when the target system is null.
void Server_PushInTransit(GameObject* Obj, PhySys::CreateParms const& Parms)
{
    FInTransitObject Entry;
    Entry.Parms = Parms;
    Entry.Obj = Obj;
    GInTransitObjects.push_back(Entry);
}

// FUN_06d005d0 (cdecl). See server_sysdb.h. Forgets the lookup cache when it holds ObjId
// (the cached object's CSimple id read unguarded).
void Server_LookupCacheForget(unsigned int const& ObjId)
{
    Watchable const* const Watched = GLookupCacheObject.target();
    if (Watched == nullptr)
        return;
    GameObject const* const Obj =
        static_cast<GameObject const*>(static_cast<IObjRW const*>(Watched));
    if (Obj->csimple()->Id != ObjId)
        return;
    GLookupCacheObject.set(nullptr);
    GLookupCacheSystem = nullptr;
}

unsigned int GLookupCacheHits = 0;        // DAT_06d8d930

unsigned int GLookupCacheMisses = 0;      // DAT_06d8d934

// FUN_06d006c0 (cdecl). The cache first (CSimple id read with no null test), then the
// cached system, then the in-transit list (no break: the LAST match stays, with OutSys 0),
// then every other system in SysMap order. On a miss OutSys keeps what the cached-system
// step wrote. A hit refreshes the cache.
bool Server_FindObject(unsigned int const& Id, GameObject*& OutObj, StarSystem*& OutSys)
{
    OutObj = nullptr;
    OutSys = nullptr;

    Watchable const* const Watched = GLookupCacheObject.target();
    if (Watched != nullptr)
    {
        GameObject* const Cached =
            static_cast<GameObject*>(static_cast<IObjRW*>(const_cast<Watchable*>(Watched)));
        if (Cached->csimple()->Id == Id)
        {
            ++GLookupCacheHits;
            OutObj = Cached;
            OutSys = GLookupCacheSystem;
            return true;
        }
    }
    ++GLookupCacheMisses;

    if (OutObj == nullptr)
    {
        if (GLookupCacheSystem != nullptr)
        {
            OutObj = GLookupCacheSystem->find_object(Id);
            OutSys = GLookupCacheSystem;
        }
        if (OutObj == nullptr)
        {
            for (std::list<FInTransitObject>::iterator It = GInTransitObjects.begin();
                 It != GInTransitObjects.end(); ++It)
            {
                if (It->Obj->get_id() == Id)
                {
                    OutObj = It->Obj;
                    OutSys = nullptr;
                }
            }
            if (OutObj == nullptr)
            {
                for (SysDB::SysMapType::iterator SysIt = SysDB::SysMap.begin();
                     SysIt != SysDB::SysMap.end(); ++SysIt)
                {
                    if (&SysIt->second == GLookupCacheSystem)
                        continue;
                    OutObj = SysIt->second.find_object(Id);
                    if (OutObj != nullptr)
                    {
                        OutSys = &SysIt->second;
                        break;
                    }
                }
                if (OutObj == nullptr)
                    return false;
            }
        }
    }

    GLookupCacheObject.set(static_cast<Watchable const*>(OutObj));
    GLookupCacheSystem = OutSys;
    return true;
}

// FUN_06d00630 (cdecl). A destroyed object counts as missing; OutSys is left as written.
bool Server_FindLiveObject(unsigned int Id, GameObject*& OutObj, StarSystem*& OutSys)
{
    if (Server_FindObject(Id, OutObj, OutSys) && !OutObj->is_destroyed())
        return true;
    OutObj = nullptr;
    return false;
}

// Object lookup over Server_FindObject (FUN_06d006c0): its one-entry cache, the cached
// system's blocks, the in-transit list, then every system in SysDB::SysMap. An object is
// found only once it is in a StarSystem block. Returns the GameObject wrapper:
// pub::SpaceObj::GetCenterOfMass (.text:06cf9069) reads CBase::Object at +0x0c+4 of it.
GameObject* Server_LookupGameObject(unsigned int ObjId) {
    GameObject* Obj = nullptr;
    StarSystem* Sys = nullptr;
    Server_FindObject(ObjId, Obj, Sys);
    return Obj;
}

// Retail FUN_06d00670, cdecl, Flid by value: look an object up by FLID (Server_FindObject,
// FUN_06d006c0) and reject it when destroyed. Not related to the local player.
// The liveness test is slot +0x160 of the wrapper's own vtable (GameObject::is_destroyed,
// .text:06cee6d0 in all thirteen Server.dll vtables), not an IObjInspect slot (those end
// at +0x150). Retail does not test the CObject's class here; callers that need it apply
// the guard themselves. Declared in server_gameobject.h.
GameObject* Server_LookupLiveObject(unsigned int Flid) {
    GameObject* Obj = Server_LookupGameObject(Flid);
    if (Obj == nullptr) return nullptr;
    if (Obj->is_destroyed()) return nullptr;
    return Obj;
}

// Server_GetLaunchPosition -- retail FUN_06d00830 (cdecl).
// - Leaving a base: the first dockable solar of that base. A docking ring (type 0x20) is
//   kind 1; otherwise controller event 0x32 asks for a free terminal through the
//   GReportedFreeTerminal mailbox (-1 read back as 0). No ring at all FDUMPs and places
//   the ship 2.2 radii out from the base, facing away from the origin.
// - Otherwise the starting_ring solar, kind from its type bit 0x20; or, with no ring, the
//   slot's initial position and orientation.
// No object or system pointer is tested where retail does not test it.
void Server_GetLaunchPosition(unsigned int PlayerId, unsigned int* OutObjId, int* OutKind,
                              Vector* OutPos, Quaternion* OutOrient)
{
    *OutObjId = 0u;
    *OutKind = -1;

    if ((Players[PlayerId].exited_base_id & 0x3fffffffu) != 0u)
    {
        std::list<GameObject*> Rings;
        StarSystem* const System = SysDB_LookupSystem(Players[PlayerId].system_id);
        System->collect_dockable_solars(Rings, Players[PlayerId].exited_base_id);

        if (Rings.size() == 0u)
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: No docking rings for base %X",
                  "E:\\FL\\Scratch\\Source\\Server\\space\\ServerSpace.cpp", 0x1a3,
                  Players[PlayerId].exited_base_id);

            GameObject* const Base = System->find_object(Players[PlayerId].exited_base_id);
            CObject* const BaseRaw = (Base != nullptr) ? Base->Object : nullptr;
            CSimple* const BaseSimple =
                (BaseRaw != nullptr && (static_cast<unsigned int>(BaseRaw->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
                    ? static_cast<CSimple*>(BaseRaw) : nullptr;
            Vector const& BasePos = BaseSimple->WorldPosition;

            // x87: each component stored as a float.
            Vector Dir;
            Dir.x = -BasePos.x;
            Dir.y = -BasePos.y;
            Dir.z = -BasePos.z;
            double const Len = std::sqrt(static_cast<double>(Dir.x) * Dir.x +
                                         static_cast<double>(Dir.y) * Dir.y +
                                         static_cast<double>(Dir.z) * Dir.z);
            double const Inv = 1.0 / Len;
            Dir.x = static_cast<float>(Dir.x * Inv);
            Dir.y = static_cast<float>(Dir.y * Inv);
            Dir.z = static_cast<float>(Dir.z * Inv);

            Vector Center;
            float const Radius = BaseSimple->get_physical_radius_r(Center);

            // Two FUN_06cf4a10 scalings, each stored as a float.
            Vector Scaled;
            Scaled.x = static_cast<float>(static_cast<double>(Radius) * Dir.x);
            Scaled.y = static_cast<float>(static_cast<double>(Radius) * Dir.y);
            Scaled.z = static_cast<float>(static_cast<double>(Radius) * Dir.z);
            Vector Off;
            Off.x = static_cast<float>(static_cast<double>(2.2f) * Scaled.x);
            Off.y = static_cast<float>(static_cast<double>(2.2f) * Scaled.y);
            Off.z = static_cast<float>(static_cast<double>(2.2f) * Scaled.z);

            OutPos->x = static_cast<float>(static_cast<double>(BasePos.x) + Off.x);
            OutPos->y = static_cast<float>(static_cast<double>(BasePos.y) + Off.y);
            OutPos->z = static_cast<float>(static_cast<double>(BasePos.z) + Off.z);

            Matrix const Look = LookMatrix(Dir);
            Quaternion Orient;
            Server_MathEngine()->MatrixToQuaternion(Orient, Look);
            *OutOrient = Orient;
        }
        else
        {
            GameObject* const Ring = Rings.front();
            *OutObjId = Ring->get_id();
            if ((Ring->get_type_bits() & 0x20u) != 0u)
            {
                *OutKind = 1;
            }
            else
            {
                GReportedFreeTerminal = -1;
                unsigned int Msg[2];
                Msg[1] = *OutObjId;
                Msg[0] = PlayerId;
                Server_ControllerDispatch(0, pub::Controller::EMessage::FreeTerminalRequest, Msg);
                int const Reported = GReportedFreeTerminal;
                *OutKind = (Reported == -1) ? 0 : Reported;
            }

            CObject* const RingRaw = Ring->Object;
            CEqObj* const RingEq =
                (RingRaw != nullptr && (static_cast<unsigned int>(RingRaw->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
                    ? static_cast<CEqObj*>(RingRaw) : nullptr;
            Matrix LaunchOrient;
            RingEq->launch_pos(*OutPos, LaunchOrient, *OutKind);

            Quaternion Orient;
            Server_MathEngine()->MatrixToQuaternion(Orient, LaunchOrient);
            *OutOrient = Orient;
        }

        Players[PlayerId].exited_base_id = 0u;
        return;                                  // the list's erase + head free (~list)
    }

    // +0x414 is an ID_String in retail (ID_String::IsEmpty is called on it); the
    // port's PlayerData declares it as its one dword.
    if (!reinterpret_cast<ID_String const&>(Players[PlayerId].starting_ring).IsEmpty())
    {
        StarSystem* const System = SysDB_LookupSystem(Players[PlayerId].system_id);
        GameObject* const Found = System->find_solar(&Players[PlayerId].starting_ring);
        CObject* const FoundRaw = (Found != nullptr) ? Found->Object : nullptr;
        CSolar* const Solar =
            (FoundRaw != nullptr && (static_cast<unsigned int>(FoundRaw->ObjectClass) & CObject::ClassSolar) == CObject::ClassSolar)
                ? static_cast<CSolar*>(FoundRaw) : nullptr;
        *OutObjId = Solar->Id;
        int const Kind = static_cast<int>(
            (reinterpret_cast<unsigned char const*>(&Solar->TypeBits)[0] >> 5) & 1u);
        *OutKind = Kind;
        Matrix LaunchOrient;
        Solar->launch_pos(*OutPos, LaunchOrient, Kind);

        Quaternion Orient;
        Server_MathEngine()->MatrixToQuaternion(Orient, LaunchOrient);
        *OutOrient = Orient;
        return;
    }

    PlayerData& Slot = Players[PlayerId];
    OutPos->x = Slot.initial_pos.x;
    OutPos->y = Slot.initial_pos.y;
    OutPos->z = Slot.initial_pos.z;
    Quaternion Orient;
    Server_MathEngine()->MatrixToQuaternion(
        Orient, Slot.initial_ornt);
    *OutOrient = Orient;
}
