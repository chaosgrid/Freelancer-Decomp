// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/server_iserverimpl.h"   // IServerImpl, Server
#include "../include/server_gameobject.h"
#include "../include/common_tradeoffer.h" // CPlayerTradeOffer
#include "../include/common_id.h"
#include "../include/common_ini.h"
#include "../include/common_goods.h"
#include "../include/common_universe.h"
#include "../include/common_server_playerdb.h"
#include "../include/common_server_controller.h"
#include "../include/server_sysdb.h"
#include "../include/server_basedb.h"
#include "../include/rpc_iclient.h"
#include "../include/rpc_iclient_args.h" // FServerSetReputationInfo / FServerCharSelectVerifiedInfo
#include "../include/common_physys.h"
#include "../include/server_createinfo.h" // FShipCreateRequest, Server_Acquire3DMathEngine (PlayerLaunch)
#include "../include/x86math.h"           // I3DMathEngine (PlayerLaunch)
#include "server_internal.h"
#include <windows.h>
#include <vector>
#include <map>
#include <list>
#include <cstring>
#include <cstddef>

extern IClient* g_pClientInterface;

    // SystemSwitchOutComplete -- retail .text:06cef600: the player's ship has finished the
    // jump-out animation at a gate/hole; move it into the destination system at the arrival
    // object.
void IServerImpl::SystemSwitchOutComplete(unsigned int ship_id, unsigned int session_id) {
        // PlayerDB::is_valid inlined.
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        // Not in a base, in a ship.
        if ((p->current_base & 0x3fffffff) != 0) return;
        if ((p->ship_object_id & 0x3fffffff) == 0) return;

        // A miss or a null system (in transit) ends it. Obj is not null-tested.
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(ship_id, Obj, Sys)) return;
        if (Sys == nullptr) return;

        // The object's CSimple must be owned by this session. Retail reads +0xb4 off the
        // (& 3) accessor's result without a null test.
        CObject* const ShipRaw = Obj->Object;
        CSimple* const ShipSimple = (ShipRaw != nullptr &&
                                     (static_cast<unsigned int>(ShipRaw->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
                                        ? static_cast<CSimple*>(ShipRaw) : nullptr;
        if (ShipSimple->OwnerPlayer != session_id) return;

        // The gate is the ship's DockedObjId (+0x90, read with no class test; complete_dock
        // stores the jump object's id right after controller event 0x11), found under type
        // mask 0xc40; its CObject must be a CSolar (0x303).
        unsigned int* const GateIdField = &static_cast<Ship*>(Obj)->DockedObjId;
        GameObject* const Gate = Sys->find_object(*GateIdField, 0xc40);
        if (Gate == nullptr) return;
        CObject* const GateRaw = Gate->Object;
        if (GateRaw == nullptr) return;
        if ((static_cast<unsigned int>(GateRaw->ObjectClass) & CObject::ClassSolar) != CObject::ClassSolar) return;
        CSolar* const GateSolar = static_cast<CSolar*>(GateRaw);

        // Retail calls get_type_bits and drops the result.
        (void)Gate->get_type_bits();

        unsigned int const DestSystemId = GateSolar->DestSystem;
        ID_String DestGate = GateSolar->DestGate;

        // A miss FDUMPs (the gate's nickname is pushed though the format has no slot for
        // it) and returns.
        StarSystem* const DestSys = SysDB_LookupSystem(DestSystemId);
        if (DestSys == nullptr) {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: FATAL: Couldn't find gate destination system!",
                  "E:\\FL\\Scratch\\Source\\Server\\space\\IServerS.cpp", 0x6b,
                  GateSolar->Nickname.get_string());
            return;
        }

        // Controller event 0x25 {ship, gate, the system being left}.
        unsigned int Msg25[3];
        Msg25[0] = ship_id;
        Msg25[1] = *GateIdField;
        Msg25[2] = Sys->SystemInfo->NicknameId;
        Server_ControllerDispatch(0, pub::Controller::EMessage::Msg0x25, Msg25);

        // The transit CreateParms. The bytes retail never writes (+0x1f .. +0x25, +0x27) are
        // stack contents there; the port zero-initialises them.
        PhySys::CreateParms Parms = {};
        Parms.Mass = 1.0f;                                                 // 0x3f800000
        Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
        Parms.AngularDamping.x = PhySys::DEFAULT_ANGULAR_DAMPING.x;
        Parms.AngularDamping.y = PhySys::DEFAULT_ANGULAR_DAMPING.y;
        Parms.AngularDamping.z = PhySys::DEFAULT_ANGULAR_DAMPING.z;
        Parms.SystemId = 0;                                                // +0x14
        Parms.Type = 2;                                                    // +0x18
        Parms.bPhysicalUnmoveable = 0;
        Parms.bEnableCollisions = 1;
        Parms.Name[0] = '\0';                                              // +0x1e
        Parms.bHasInertia = 0;                                             // +0x26
        Parms.Inertia.x = 0.0f;
        Parms.Inertia.y = 0.0f;
        Parms.Inertia.z = 0.0f;

        Sys->remove_ship(Obj, &Parms);
        Sys->player_switched_out(session_id, DestSystemId);
        DestSys->add_player(session_id);

        // The arrival object picks the spot; without one, an FDUMP, the origin and the
        // identity orientation.
        Vector Pos;
        Matrix Orient;
        GameObject* const Arrival = DestSys->find_solar(&DestGate.Id);
        if (Arrival != nullptr) {
            static_cast<Solar*>(Arrival)->ChooseSystemArrivalLocation(Obj, Pos, Orient);
        } else {
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: Couldn't find arrival gate %s.",
                  "E:\\FL\\Scratch\\Source\\Server\\space\\IServerS.cpp", 0x8d,
                  DestGate.get_string());
            Pos.x = 0.0f;
            Pos.y = 0.0f;
            Pos.z = 0.0f;
            Orient = Matrix(1.0f, 0.0f, 0.0f,
                            0.0f, 1.0f, 0.0f,
                            0.0f, 0.0f, 1.0f);
        }

        // arrive_ship is handed the possibly-null arrival object.
        DestSys->add_ship(Obj, &Parms, Pos, Orient);
        DestSys->arrive_ship(Obj, Arrival, Pos, Orient);

        p->system_id = DestSystemId;

        // Controller event 0x3b {dest system, old system, sid, arrival object id, Pos}.
        // Retail calls the arrival object's get_id with no null test -- after the
        // "Couldn't find arrival gate" FDUMP it faults here, and so does the port.
        unsigned int Msg3b[7];
        Msg3b[0] = DestSys->SystemInfo->NicknameId;
        Msg3b[2] = session_id;
        Msg3b[1] = Sys->SystemInfo->NicknameId;
        Msg3b[3] = Arrival->get_id();
        std::memcpy(&Msg3b[4], &Pos.x, sizeof(float));
        std::memcpy(&Msg3b[5], &Pos.y, sizeof(float));
        std::memcpy(&Msg3b[6], &Pos.z, sizeof(float));
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemEnter, Msg3b);

        p->auto_save();
    }

    // RequestCreateShip -- retail .text:06cef9e0: reserves the ship id in +0x3E8
    // created_ship_id (which PlayerLaunch checks) and answers with IClient
    // SendServerRequestCreateShipResp, no null test. Retail's null-CFLIDMaker path stores
    // `sid & 0xffffff00` as both flid and flag; for every session id below 0x100 that is
    // the 0 / false the port passes.
void IServerImpl::RequestCreateShip(unsigned int session_id) {
        if (!Players.is_valid(session_id)) return;
        PlayerSlot* p = &Players[session_id];
        if ((p->created_ship_id & 0x3fffffff) != 0) {
            g_pClientInterface->SendServerRequestCreateShipResp(session_id, true, p->created_ship_id);
            return;
        }
        unsigned int flid = 0;
        unsigned int flag = 0;
        CFLIDMaker* Maker = CFLIDMaker::GetInstance();
        if (Maker != nullptr) {
            flid = Maker->AllocateObjectID();
            flag = (flid != 0) ? 1u : 0u;
        }
        p->created_ship_id = flid;
        g_pClientInterface->SendServerRequestCreateShipResp(session_id, flag != 0, flid);
    }

void IServerImpl::LaunchComplete(unsigned int base_obj_id, unsigned int object_id) {
        // Retail .text:06cefaa0. rpclocal serializes both arguments, so neither is an
        // implicit session id; base_obj_id goes to IObjAffect slot 28 (Ship::launch_complete
        // sends it as the first dword of controller event 0x28). No controller message of its
        // own.
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(object_id, Obj, Sys))
            return;
        if (Obj == nullptr)
            return;
        static_cast<IObjAffect*>(Obj)->launch_complete(base_obj_id);
    }

void IServerImpl::JumpInComplete(unsigned int system_id, unsigned int object_id) {
        // Retail .text:06cefae0, as LaunchComplete with IObjAffect slot 29 (Ship::jump_in_complete
        // sends system_id in controller event 0x26).
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(object_id, Obj, Sys))
            return;
        if (Obj == nullptr)
            return;
        static_cast<IObjAffect*>(Obj)->jump_in_complete(system_id);
    }

    // PlayerLaunch -- retail .text:06cefb20.
void IServerImpl::PlayerLaunch(unsigned int ship_id, unsigned int session_id) {
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        // Not in a ship, and ship_id must be the ship RequestCreateShip reserved (+0x3E8).
        if ((p->ship_object_id & 0x3fffffff) != 0) return; // Already in ship
        if ((p->created_ship_id & 0x3fffffff) == 0) return; // No ship reserved
        if (ship_id != p->created_ship_id) return; // Ship ID mismatch

        StarSystem* const Sys = SysDB_LookupSystem(p->system_id);
        if (Sys == nullptr) return;

        // Retail calls an empty function on the slot here (.text:06ce1090, an ICF fold).

        Sys->add_player(session_id);

        // Controller event 6 {sid, exited_base_id (the base the session just left)}.
        unsigned int msg6[2] = { session_id, p->exited_base_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLaunch, msg6);

        // FUN_06cf0330 (the CShipCreateInfo constructor).
        FShipCreateRequest Req;
        Req.EquipStateData = nullptr;
        Req.MarkPlayerId = 0;

        Req.ArchetypeNickname = p->ship_archetype;
        Req.GroupId = Players.GetGroupID(session_id);

        Archetype::Ship* const ShipArch = Archetype::GetShip(Req.ArchetypeNickname);
        ShipArch->load();

        // The result is not tested. Retail leaves HpPos as the stack held it when the call
        // fails; the port starts it at zero.
        Vector HpPos;
        HpPos.x = 0.0f;
        HpPos.y = 0.0f;
        HpPos.z = 0.0f;
        HardpointPositionA(ShipArch->EngineIndex, "HpMount", &HpPos, nullptr);

        // FUN_06d00830.
        unsigned int LaunchObjId = 0;
        int LaunchKind = -1;
        Server_GetLaunchPosition(session_id, &LaunchObjId, &LaunchKind, &Req.Position,
                                 &Req.Orientation);

        Matrix LaunchOrient;
        Server_MathEngine()->QuaternionToMatrix(LaunchOrient, Req.Orientation);
        Vector HpOffset;
        Server_MathEngine()->TransformMatrix(HpOffset, LaunchOrient, HpPos);

        // The ship's origin sits HpMount behind the launch point (float per component).
        Req.Position.x = Req.Position.x - HpOffset.x;
        Req.Position.y = Req.Position.y - HpOffset.y;
        Req.Position.z = Req.Position.z - HpOffset.z;

        FServerLaunchInfo Launch;
        Launch.ShipId = ship_id;
        Launch.LaunchObjId = LaunchObjId;
        Launch.LaunchPosIndex = static_cast<unsigned int>(LaunchKind);
        Launch.Orientation = Req.Orientation;
        Launch.Position = Req.Position;
        g_pClientInterface->SendServerLaunch(session_id, Launch);

        if (!SinglePlayer()) {
            g_pClientInterface->SendServerMiscObjUpdateObjectGroup(session_id, ship_id, Req.GroupId);
            unsigned int Affiliation = 0xffffffffu;
            int AffiliationVibe = p->rep_vibe;
            ::Reputation::Vibe::GetAffiliation(AffiliationVibe, Affiliation, false);
            g_pClientInterface->SendServerMiscObjUpdateAffiliation(session_id, ship_id, Affiliation);
        }

        // Retail stores the zero velocities again although the request already holds them.
        Req.Velocity.x = 0.0f;
        Req.Velocity.y = 0.0f;
        Req.Velocity.z = 0.0f;
        Req.ObjId = ship_id;
        Req.OwnerPlayer = session_id;
        Req.AngularVelocity.x = 0.0f;
        Req.AngularVelocity.y = 0.0f;
        Req.AngularVelocity.z = 0.0f;

        // The com_ costume; the accessory loop count is signed, no clamp.
        Req.VibeGroupId = p->rep_vibe;
        Req.CostumeHead = p->com_costume.PartIds[0];
        Req.CostumeBody = p->com_costume.PartIds[1];
        Req.CostumeLeftHand = p->com_costume.PartIds[2];
        Req.CostumeRightHand = p->com_costume.PartIds[3];
        Req.CostumeAccessoryCount = static_cast<int>(p->com_costume.AccessoryCount);
        for (int Index = 0; Index < Req.CostumeAccessoryCount; ++Index)
            Req.CostumeAccessory[Index] = p->com_costume.AccessoryIds[Index];

        Req.VoiceId = CreateID(p->voice);
        Req.HitPts = static_cast<int>(static_cast<double>(p->relative_health) *
                                      static_cast<double>(ShipArch->HitPts));

        Req.Equipment = EquipDescVector(p->equipment);
        Req.CollisionGroups = p->collision_groups;

        // Retail's Rank is not initialised before the call; the port starts it at 0.
        unsigned int Rank = 0;
        ::Reputation::Vibe::GetRank(Req.VibeGroupId, Rank);
        Req.Throttle = 0.2f;                                               // DAT_06d65bb8
        Req.ReputationRank = Rank;
        Req.MarkPlayerId = 0;

        ::Ship* const NewShip = Sys->create_ship(Req);                    // FUN_06d0a880
        ShipArch->Release();

        // Launched from an object: FUN_06d01110 on the new wrapper (not null-tested).
        if (LaunchObjId != 0)
            NewShip->apply_launch_invincibility();

        // Single player: the orientation matrix again and beam_object on the wrapper's raw
        // object under the (& 1) test -- called whatever that test yields, as in retail.
        if (SinglePlayer()) {
            Server_MathEngine()->QuaternionToMatrix(LaunchOrient, Req.Orientation);
            CObject* Raw = nullptr;
            if (NewShip != nullptr) {
                CObject* const Obj = NewShip->Object;
                if (Obj != nullptr && (static_cast<unsigned int>(Obj->ObjectClass) & CObject::ClassObject) == CObject::ClassObject)
                    Raw = Obj;
            }
            Raw->beam_object(Req.Position, LaunchOrient, false);
        }

        p->ship_object_id = Req.ObjId;

        // Controller event 0x36 {system, sid, launch object, launch_pos index}.
        unsigned int msg36[4];
        msg36[0] = Sys->SystemInfo->NicknameId;
        msg36[1] = session_id;
        msg36[2] = LaunchObjId;
        msg36[3] = static_cast<unsigned int>(LaunchKind);
        Server_ControllerDispatch(0, pub::Controller::EMessage::SpaceEnter, msg36);
    }

    // SPObjUpdate -- retail .text:06cf0620. An invalid session skips ONLY the bound check;
    // the update still goes to StarSystem::sp_obj_update (FUN_06d0bbc0).
void IServerImpl::SPObjUpdate(const SSPObjUpdateInfo& info, unsigned int session_id) {
        if (SinglePlayer()) return;
        GameObject* Obj = nullptr;
        StarSystem* System = nullptr;
        if (!Server_FindObject(info.ObjectId, Obj, System)) return;
        if (System == nullptr) return;
        if (Players.is_valid(session_id)) {
            // FUN_06d498f0 reads only its first stack argument; the port's helper takes
            // just that one (common_server_playerdb.h).
            if (!Players[session_id].check_position_bound(&info.Position))
                return;
        }
        System->sp_obj_update(info, session_id);
    }

    // SPMunitionCollision -- retail .text:06cf06c0; StarSystem::munition_collision is
    // FUN_06d0d500.
void IServerImpl::SPMunitionCollision(const SSPMunitionCollisionInfo& info, unsigned int session_id) {
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        if ((p->ship_object_id & 0x3fffffff) == 0) return;
        double const Health = static_cast<double>(p->relative_health);   // FUN_06d4ed00
        // x87 compare against 0.0001 (.rdata:06d65190): an unordered health continues.
        if (Health < 0.0001 || Health == 0.0001) return;
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(info.ObjectId, Obj, Sys))
            return;
        if (Sys == nullptr)
            return;
        Sys->munition_collision(info);
    }

    // SPObjCollision -- retail .text:06cf0750; no session test (session_id is not read).
    // StarSystem::collision_damage is FUN_06d0d730. SSPObjCollisionInfo IS the 0x14-byte
    // FCollisionDamage record GameObject::on_collision builds (+8 target id is the object
    // looked up here); the static_asserts pin the two layouts together and the pointer is
    // passed on unchanged, as retail does.
void IServerImpl::SPObjCollision(const SSPObjCollisionInfo& info, unsigned int session_id) {
        static_assert(sizeof(SSPObjCollisionInfo) == sizeof(FCollisionDamage),
                      "SSPObjCollisionInfo is the FCollisionDamage record");
        static_assert(offsetof(SSPObjCollisionInfo, ObjectId) == offsetof(FCollisionDamage, TargetId),
                      "+8 is the target FUN_06d006c0 resolves");
        static_assert(offsetof(SSPObjCollisionInfo, SubObjId) == offsetof(FCollisionDamage, TargetSubObjId),
                      "+0xc is the target's sub-object");
        static_assert(offsetof(SSPObjCollisionInfo, DamageBits) == offsetof(FCollisionDamage, Damage),
                      "+0x10 is the damage");
        (void)session_id;
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(info.ObjectId, Obj, Sys))
            return;
        if (Sys == nullptr)
            return;
        Sys->collision_damage(reinterpret_cast<FCollisionDamage const&>(info), Obj);
    }

    // SPBadLandsObjCollision -- retail .text:06cf0790; no session test (session_id is not
    // read). StarSystem::badlands_collision is FUN_06d0d6e0.
void IServerImpl::SPBadLandsObjCollision(const SSPBadLandsObjCollisionInfo& info, unsigned int session_id) {
        (void)session_id;
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(info.ObjectId, Obj, Sys))
            return;
        if (Sys == nullptr)
            return;
        Sys->badlands_collision(info);
    }

    // MineAsteroid -- retail .text:06cf07d0: the SysDB lookup is not null-tested;
    // StarSystem::spawn_mined_loot is FUN_06d0d9b0. session_id is never read; no controller
    // message.
void IServerImpl::MineAsteroid(unsigned int system_id, const Vector& position, unsigned int loot_id,
                      unsigned int secondary_loot, unsigned int quantity, unsigned int session_id) {
        (void)session_id;
        StarSystem* const System = SysDB_LookupSystem(system_id);
        System->spawn_mined_loot(position, loot_id, secondary_loot, static_cast<int>(quantity));
    }

    // Hail -- retail .text:06cf0800. Only the found flag is tested. All three arguments are
    // serialized by rpclocal -- no session id.
void IServerImpl::Hail(unsigned int object_id, unsigned int arg1, unsigned int arg2) {
        if (Server_LookupGameObject(object_id) == nullptr)
            return;
        unsigned int msg_data[3] = { object_id, arg1, arg2 };
        Server_ControllerDispatch(0, pub::Controller::EMessage::Hail, msg_data);
    }

    // CommComplete -- retail .text:06cf0850. The system comes from arg1's object, else
    // arg0's. When neither resolves, retail sends whatever the stack held in payload+4; the
    // port sends 0 (deviations.md, zeroed indeterminate stack bytes).
void IServerImpl::CommComplete(unsigned int arg0, unsigned int arg1, unsigned int arg2, CommResult result) {
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        unsigned int SystemRef = 0;
        if (Server_FindObject(arg1, Obj, Sys) && Sys != nullptr)
            SystemRef = Sys->SystemInfo->NicknameId;
        else if (Server_FindObject(arg0, Obj, Sys) && Sys != nullptr)
            SystemRef = Sys->SystemInfo->NicknameId;
        unsigned int msg_data[5] = { arg1, SystemRef, arg0, arg2, (unsigned int)result };
        Server_ControllerDispatch(0, pub::Controller::EMessage::CommComplete, msg_data);
    }

    // RequestEvent -- retail .text:06cf08e0: IObjAffect +0x5c with no RequestCB. session_id is
    // never read; no controller message.
void IServerImpl::RequestEvent(int event_type, unsigned int object_id, unsigned int target_id, unsigned int second_id, unsigned long b_flag, unsigned int session_id) {
        (void)session_id;
        GameObject* const Obj = Server_LookupGameObject(object_id);
        if (Obj == nullptr)
            return;
        static_cast<IObjAffect*>(Obj)->request_event(event_type, &target_id, &second_id,
                                                     static_cast<int>(b_flag), nullptr);
    }

    // RequestCancel -- retail .text:06cf0930: IObjAffect +0x60. session_id is never read; no
    // controller message.
void IServerImpl::RequestCancel(int event_type, unsigned int object_id, unsigned int target_id, unsigned long arg3, unsigned int session_id) {
        (void)session_id;
        GameObject* const Obj = Server_LookupGameObject(object_id);
        if (Obj == nullptr)
            return;
        static_cast<IObjAffect*>(Obj)->request_cancel(event_type, &target_id, static_cast<int>(arg3));
    }

    // Slot 1: ActivateEquip -- retail .text:06cf0980.
void IServerImpl::ActivateEquip(unsigned int player_id, const XActivateEquip& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        static_cast<IObjAffect*>(Obj)->toggle_item(&info.EquipId, info.bActivate,
                                               static_cast<int>(player_id));
    }

    // Slot 2: ActivateCruise -- retail .text:06cf09e0.
void IServerImpl::ActivateCruise(unsigned int player_id, const XActivateCruise& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        static_cast<IObjAffect*>(Obj)->toggle_cruise(info.bActivate, info.bDisruptCruise != 0,
                                               static_cast<int>(player_id));
    }

    // Slot 3: ActivateThrusters -- retail .text:06cf0a40.
void IServerImpl::ActivateThrusters(unsigned int player_id, const XActivateThrusters& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        static_cast<IObjAffect*>(Obj)->toggle_thrusters(info.bActivate, static_cast<int>(player_id));
    }

    // Slot 6: GoTradelane -- retail .text:06cf0a90. The ship id is read from the raw slot
    // with no PlayerDB::is_valid; the SYSTEM out is tested, not the object. Controller event
    // 0x16 and SendCommonGoTradeLane are StarSystem::notify_go_tradelane's (FUN_06d0df40) work.
void IServerImpl::GoTradelane(unsigned int player_id, const XGoTradelane& info) {
        if (info.ObjectId != Players[player_id].ship_object_id) return;
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(info.ObjectId, Obj, Sys))
            return;
        if (Sys == nullptr)
            return;
        Sys->notify_go_tradelane(info.ObjectId, info.TradelaneRing1, info.TradelaneRing2,
                                 info.bMidLane, 0.0f);
    }

    // Slot 7: StopTradelane -- retail .text:06cf0af0; as GoTradelane, ending in
    // StarSystem::notify_stop_tradelane (FUN_06d0e120).
void IServerImpl::StopTradelane(unsigned int session_id, unsigned int ship_id, unsigned int tradelane_ring1, unsigned int tradelane_ring2) {
        if (ship_id != Players[session_id].ship_object_id) return;
        GameObject* Obj = nullptr;
        StarSystem* Sys = nullptr;
        if (!Server_FindObject(ship_id, Obj, Sys))
            return;
        if (Sys == nullptr)
            return;
        Sys->notify_stop_tradelane(ship_id, tradelane_ring1, tradelane_ring2);
    }

    // Slot 5: TractorObjects -- retail .text:06cf0b50. The loop count is signed; missing
    // targets are skipped.
void IServerImpl::TractorObjects(unsigned int player_id, const XTractorObjects& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        std::vector<IObjRW*> Targets;
        int const Count = static_cast<int>(info.TargetIds.size());
        for (int Index = 0; Index < Count; ++Index)
        {
            IObjRW* const Target = Server_LookupGameObject(info.TargetIds[Index]);
            if (Target == nullptr)
                continue;
            Targets.push_back(Target);
        }
        static_cast<IObjAffect*>(Obj)->tractor_all(&info.TractorEquipId, Targets,
                                               static_cast<int>(player_id));
    }

    // Slot 4: SetTarget -- retail .text:06cf0c70. A missing target is passed on as null.
void IServerImpl::SetTarget(unsigned int player_id, const XSetTarget& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        IObjAffect* const Affect = static_cast<IObjAffect*>(Obj);
        if (info.SubObjId == SUBOBJ_ID_NONE)
        {
            IObjRW* const Target = Server_LookupGameObject(info.TargetObjectId);
            Affect->set_target(Target, info.TargetSubObjId, static_cast<int>(player_id));
            return;
        }
        IObjRW* const Target = Server_LookupGameObject(info.TargetObjectId);
        Affect->set_gun_target(&info.SubObjId, Target, info.TargetSubObjId,
                        static_cast<int>(player_id));
    }

    // Slot 8: JettisonCargo -- retail .text:06cf0d50.
void IServerImpl::JettisonCargo(unsigned int player_id, const XJettisonCargo& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        Players[player_id].terminate_trade();
        static_cast<IObjAffect*>(Obj)->jettison_cargo(info.ItemId, static_cast<int>(info.Count),
                                               static_cast<int>(player_id));
    }

    // Slots 0..8 -- the object-command handlers. Each opens with FUN_06d006c0
    // (Server_FindObject, server_gameobject.h): found flag, the IObjRW and its owning
    // StarSystem. Handlers that only keep the object use Server_LookupGameObject (null ==
    // not found); the ones that test the SYSTEM out call Server_FindObject itself (for an
    // in-transit object that out is 0, not OwnerSystem). The IObjAffect sub-object is at
    // obj + 4; its slots are declared in common_iface.h. None of these sends a
    // pub::Controller message.

    // Slot 0: FireWeapon -- retail .text:06cf0dc0. FUN_06d50af0 is terminate_trade.
void IServerImpl::FireWeapon(unsigned int player_id, const XFireWeaponInfo& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        Players[player_id].terminate_trade();
        static_cast<IObjAffect*>(Obj)->fire_weapons(info.Hardpoints.data(),
                                               static_cast<unsigned>(info.Hardpoints.size()),
                                               &info.TargetPos, static_cast<int>(player_id));
    }

    // SetWeaponGroup -- retail .text:06cf0e40.
void IServerImpl::SetWeaponGroup(unsigned int session_id, unsigned char* data, int size) {
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        // FUN_06d504f0 is the inlined string assign on player+0x380: strlen of the whole
        // buffer, no bound from `size` and no null check on `data`, as in retail.
        (void)size;
        p->weapon_group_name = reinterpret_cast<const char*>(data);
    }

    // SetVisitedState -- retail .text:06cf0e90: a signed count, then 5-byte records into
    // FUN_06d508a0. No `data` null test; data_len is never read.
void IServerImpl::SetVisitedState(unsigned int session_id, unsigned char* data, int data_len) {
        (void)data_len;
        if (!Players.is_valid(session_id)) return;
        PlayerSlot* p = &Players[session_id];

        int count = *(int*)data;
        unsigned char* ptr = data + 4;
        for (int i = 0; i < count; i++) {
            unsigned int system_id = *(unsigned int*)ptr;
            unsigned char visit_flags = ptr[4];
            ptr += 5;
            // FUN_06d508a0: OR the flags into the visited MAP at +0x3a0 (PlayerData::visit_map),
            // not the pending queue at +0x3b8 (see common_server_playerdb.h).
            std::map<unsigned int, unsigned char>::iterator Existing =
                p->visit_map.find(system_id);
            if (Existing != p->visit_map.end()) {
                Existing->second = (unsigned char)(Existing->second | visit_flags);
            } else {
                p->visit_map[system_id] = visit_flags;
            }
        }
    }

    // RequestBestPath -- retail .text:06cf0f00: packs nine fields from the buffer, message
    // 0x31. No test on `data`; data_len is never read.
void IServerImpl::RequestBestPath(unsigned int session_id, unsigned char* data, int data_len) {
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        (void)data_len;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;

        // Retail's field order.
        unsigned int msg_data[10];
        msg_data[0] = session_id;
        msg_data[1] = *(unsigned int*)(data + 0x00);
        msg_data[2] = *(unsigned int*)(data + 0x1c);
        msg_data[3] = *(unsigned int*)(data + 0x0c);
        msg_data[4] = *(unsigned int*)(data + 0x10);
        msg_data[5] = *(unsigned int*)(data + 0x14);
        msg_data[6] = *(unsigned int*)(data + 0x20);
        msg_data[7] = *(unsigned int*)(data + 0x24);
        msg_data[8] = *(unsigned int*)(data + 0x28);
        msg_data[9] = *(unsigned int*)(data + 0x30);
        Server_ControllerDispatch(0, pub::Controller::EMessage::RequestBestPath, msg_data);
    }

    // RequestPlayerStats — retail 0x10fa0: validates player, sends message 0x33
void IServerImpl::RequestPlayerStats(unsigned int session_id, unsigned char* data, int data_len) {
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        (void)data; (void)data_len;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        Server_ControllerDispatch(0, pub::Controller::EMessage::RequestPlayerStats, &session_id);
    }

    // PopUpDialog -- retail .text:06cf0ff0, unconditional; message 0x30.
void IServerImpl::PopUpDialog(unsigned int session_id, unsigned int buttons) {
        unsigned int msg_data[2] = { session_id, buttons };
        Server_ControllerDispatch(0, pub::Controller::EMessage::PopUpDialog, msg_data);
    }

    // RequestGroupPositions -- retail .text:06cf1020, ordinal 380. The reply buffer is a
    // count of the OTHER members followed by one 0x18-byte entry each:
    //   +0x00 float Pos[3]  (zero when the ship lookup fails; unwritten when in a base)
    //   +0x0c uint  BaseId  (zero when the member has a ship)
    //   +0x10 uint  SystemId
    //   +0x14 uint  Vibe
    // sent through IClient slot 96 (FLPACKET_COMMON_REQUEST_GROUP_POSITIONS). The buffer is
    // handed over: rpclocal's sender (0x06d8b7a0) marks the packet as owning it. The
    // request payload (data/size) is not read.
void IServerImpl::RequestGroupPositions(unsigned int session_id, unsigned char* data, int size) {
        (void)data; (void)size;
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;

        CPlayerGroup* Group = (CPlayerGroup*)p->group_ptr;
        if (!Group) {
            // A bare 4-byte block holding 0, handed over as above.
            unsigned int* Empty = static_cast<unsigned int*>(::operator new(4));
            *Empty = 0;
            g_pClientInterface->SendCommonRequestGroupPositions(
                session_id, reinterpret_cast<unsigned char*>(Empty), 4);
            return;
        }

        unsigned int Count = Group->GetMemberCount();
        // Signed test.
        unsigned int Others = (static_cast<int>(Count) > 0) ? (Count - 1) : Count;
        unsigned int BufSize = Others * 0x18 + 4;
        // Not initialised.
        unsigned char* Buf = static_cast<unsigned char*>(::operator new(BufSize));
        *(unsigned int*)Buf = Others;

        unsigned char* Entry = Buf + 4;
        for (int i = 0; i < static_cast<int>(Count); ++i) {
            unsigned int MemberId = Group->GetMember(i);
            if (MemberId == session_id) continue;
            // Retail indexes Players[member] directly, no logged-in test.
            PlayerSlot* Member = &Players[MemberId];
            *(unsigned int*)(Entry + 0x10) = Member->system_id;
            *(unsigned int*)(Entry + 0x14) = (unsigned int)Member->rep_vibe;
            if (Member->ship_object_id != 0) {
                *(unsigned int*)(Entry + 0x0c) = 0;
                // No null test on the system lookup.
                StarSystem* const MemberSystem =
                    SysDB_LookupSystem(*(unsigned int*)(Entry + 0x10));
                GameObject* const MemberShip =
                    MemberSystem->find_object(Member->ship_object_id, 0x7f0000u);
                if (MemberShip == nullptr) {
                    *(unsigned int*)(Entry + 0x00) = 0;
                    *(unsigned int*)(Entry + 0x04) = 0;
                    *(unsigned int*)(Entry + 0x08) = 0;
                } else {
                    Vector const& Pos = MemberShip->get_position();
                    std::memcpy(Entry, &Pos, 0x0c);
                }
            } else {
                // Only BaseId is written; Pos stays as operator new left it.
                *(unsigned int*)(Entry + 0x0c) = Member->current_base;
            }
            Entry += 0x18;
        }

        g_pClientInterface->SendCommonRequestGroupPositions(session_id, Buf, BufSize);
    }

    // SetMissionLog -- retail .text:06cf11c0: the player validation plus FUN_06d505a0 on the
    // std::list<FmtStr> at +0x390; no controller message, no check on data/data_len.
    // Op 0 clears the log; op 1 reads a signed count of entries and inserts each BEFORE
    // begin() (push_front); any other op does nothing.
void IServerImpl::SetMissionLog(unsigned int session_id, unsigned char* data, int data_len) {
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;

        int* const Ops = reinterpret_cast<int*>(data);
        if (Ops[0] == 0) {
            p->mission_log.clear();
        } else if (Ops[0] == 1) {
            data_len -= 8;
            int Count = Ops[1];
            unsigned char* Cursor = data + 8;
            for (; Count > 0; --Count) {
                FmtStr Entry;
                ExtractMissionLogData(&Entry, Cursor, data_len);
                p->mission_log.push_front(Entry);
            }
        }
    }

    // SetInterfaceState -- retail .text:06cf1210: FUN_06d50730 stores *data into +0x39C.
    // data_len is never read and nothing tests `data`.
void IServerImpl::SetInterfaceState(unsigned int session_id, unsigned char* data, int data_len) {
        (void)data_len;
        if (!Players.is_valid(session_id)) return;
        Players[session_id].interface_state = *(unsigned int*)data;
    }

    // RequestRankLevel -- retail .text:06cf1260: IClient slot 99
    // (FLPACKET_COMMON_REQUEST_RANK_LEVEL) with a 4-byte block holding rank_level (+0x284,
    // FUN_06d4ec00) in single player, else 0xffffffff. The block is handed over: rpclocal's
    // sender (0x06d8b890) marks the packet as owning it.
void IServerImpl::RequestRankLevel(unsigned int session_id, unsigned char* data, int data_len) {
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        unsigned int* const RankBlock = static_cast<unsigned int*>(::operator new(4));
        if (SinglePlayer()) {
            *RankBlock = p->rank_level;
        } else {
            *RankBlock = 0xFFFFFFFF;
        }
        g_pClientInterface->SendCommonRequestRankLevel(
            session_id, reinterpret_cast<unsigned char*>(RankBlock), 4);
    }

    // SPScanCargo -- retail .text:06cf12e0: no branch, never reads the session; message 3
    // with a TWO-dword payload.
void IServerImpl::SPScanCargo(const unsigned int& scanner_id, const unsigned int& target_id,
                     unsigned int session_id) {
        (void)session_id;
        unsigned int msg_data[2] = { scanner_id, target_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::CargoScanned, msg_data);
    }

    // SPRequestUseItem -- retail .text:06cf1310; no session test. Trade is terminated only
    // when use_item returns 0.
void IServerImpl::SPRequestUseItem(const SSPUseItem& info, unsigned int session_id) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        if (static_cast<IObjAffect*>(Obj)->use_item(info.ItemId, info.Count,
                                                   static_cast<int>(session_id)) != 0)
            return;
        Players[session_id].terminate_trade();
    }

    // SPRequestInvincibility -- retail .text:06cf1380. Raw (unmasked) ship id compare.
    // Turning invincibility on is honoured only for reason 2 (the ship is in some dockable
    // solar's dock queue, FUN_06d0cd60 / FUN_06ce7c50) or reason 5. The record's pad bytes
    // (+6/+7) are never written by retail; the port zeroes them (deviations.md).
void IServerImpl::SPRequestInvincibility(unsigned int ship_id, unsigned char b_invincible,
                                InvincibilityReason reason, unsigned int session_id) {
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        if (p->ship_object_id != ship_id) return;

        FInvincibility Cmd = {};
        Cmd.Type = static_cast<unsigned int>(reason);
        Cmd.bFromPlayers = b_invincible;
        Cmd.bProtect = b_invincible;
        if (!b_invincible)
        {
            Cmd.MaxHpLoss = 1.0f;
        }
        else
        {
            Cmd.MaxHpLoss = 0.0f;
            int const Reason = static_cast<int>(reason);
            if (Reason == 2)
            {
                StarSystem* const System = SysDB_LookupSystem(p->system_id);
                if (System == nullptr)
                    return;
                bool bQueued = false;
                std::list<GameObject*> Solars;
                System->collect_dockable_solars(Solars, 0);
                for (std::list<GameObject*>::const_iterator It = Solars.begin(); It != Solars.end(); ++It)
                {
                    if (static_cast<EqObj*>(*It)->is_ship_queued(ship_id))
                    {
                        bQueued = true;
                        break;
                    }
                }
                if (!bQueued)
                    return;
            }
            else if (Reason != 5)
            {
                return;
            }
        }

        GameObject* const Obj = Server_LookupGameObject(ship_id);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        Obj->set_invincible(Cmd);
    }

    // SetManeuver -- retail .text:06cf1540. The behavior manager's slot 0 (Common
    // LAB_062d3f90) gets the event {2, &info}. Every early out skips the single-player
    // message as well.
void IServerImpl::SetManeuver(unsigned int session_id, const XSetManeuver& info) {
        GameObject* Obj = nullptr;
        StarSystem* System = nullptr;
        if (!Server_FindObject(info.ObjectId, Obj, System)) return;
        if (Obj == nullptr) return;
        CObject* const Raw = Obj->Object;
        if (Raw == nullptr) return;
        if ((static_cast<unsigned int>(Raw->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj) return;
        IBehaviorManager* const Manager =
            static_cast<CEqObj*>(Raw)->get_behavior_interface();
        if (Manager != nullptr) {
            FBehaviorManagerEvent Event;
            Event.Type = 2;
            Event.Data = const_cast<XSetManeuver*>(&info);
            Manager->OnEvent(&Event);
        }
        if (SinglePlayer()) {
            unsigned int msg_data[3];
            msg_data[0] = session_id;
            msg_data[1] = info.ObjectId;
            msg_data[2] = info.Flag;
            Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerManeuver, msg_data);
        }
    }

    // Slot 91: Dock -- retail .text:06cf15f0, ?Dock@IServerImpl@@UAEXABI0@Z. rpclocal
    // serializes *BaseId only, so the session is the second argument.
    // pub::Player::ForceLand is the other caller. The base is entered later, by the
    // client's own BaseEnter after FLPACKET_SERVER_LAND.
void IServerImpl::Dock(const unsigned int& base_id, const unsigned int& session_id) {
        // No validation of sid. The system out-parameter is used below without a null test
        // (an in-transit ship leaves it 0, as in retail).
        PlayerData& Slot = Players[session_id];
        GameObject* ShipObj = nullptr;
        StarSystem* System = nullptr;
        if (!Server_FindObject(Slot.ship_object_id, ShipObj, System)) return;

        Universe::IBase const* const Base = Universe::get_base(base_id);
        if (Base == nullptr) return;

        // FUN_06d0d370. Retail's land flag is the caller's own param_2 slot, overwritten
        // with 1.
        bool const bLand = true;
        System->queue_dock(ShipObj, nullptr, &base_id, &bLand);

        // A base in the player's current system is landed by the next regular update.
        if (Base->SystemId == Slot.system_id) return;

        // Otherwise update the ship's system right now with 0.2f: the dock queue is
        // processed, the LAND goes out and the player leaves that system.
        System->update(0.2f);

        Players[session_id].flush_pending_visits();   // FUN_06d4b450

        unsigned int LeaveEvent[3];
        LeaveEvent[0] = System->SystemInfo->NicknameId;
        LeaveEvent[1] = Base->SystemId;
        LeaveEvent[2] = session_id;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, LeaveEvent);

        Players[session_id].system_id = LeaveEvent[1];

        unsigned int EnterEvent[7];
        EnterEvent[0] = LeaveEvent[1];
        EnterEvent[1] = System->SystemInfo->NicknameId;
        EnterEvent[2] = session_id;
        EnterEvent[3] = 0;
        EnterEvent[4] = 0;
        EnterEvent[5] = 0;
        EnterEvent[6] = 0;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemEnter, EnterEvent);
    }

    // Trade system. The offers are Common.dll's CPlayerTradeOffer (common_tradeoffer.h,
    // retail sizeof 0x24). Server.dll imports its ctor/dtor and setters and reads +8 / +0xc /
    // +0x10 / +0x1c / +0x20 inline; the port uses the exported accessors. No null test on
    // the IClient pointer anywhere in the family.

    // Slot 79: InitiateTrade -- retail .text:06cf1780. Only the TARGET is validated; a live
    // offer on the source is terminated and execution falls through. Retail stamps Server's
    // own copy of the offer vtable (.rdata:06d65f80, deleting dtor FUN_06cf1970). Retail
    // writes neither side's trade_request_partner here.
void IServerImpl::InitiateTrade(unsigned int session_id, unsigned int target_id) {
        PlayerSlot* const target = GetPlayerBySession(target_id);
        if (!target) {
            TerminateTrade(session_id, 2);
            return;
        }
        if (target->trade_request_partner != session_id) {
            TerminateTrade(session_id, 2);
            return;
        }
        if (session_id == target_id) {
            TerminateTrade(session_id, 2);
            return;
        }

        PlayerSlot* const p = &Players[session_id];            // raw
        if (p->trade_offer != nullptr)
            TerminateTrade(session_id, 4);                     // falls through

        if (target->trade_offer != nullptr) {
            g_pClientInterface->SendCommonPlayerTradeTerminateTrade(session_id, 6);
            return;
        }

        CPlayerTradeOffer* const SourceOffer = new CPlayerTradeOffer();
        CPlayerTradeOffer* const TargetOffer = new CPlayerTradeOffer();

        SourceOffer->SetSourcePlayerID(session_id);
        SourceOffer->SetTargetPlayerID(target_id);
        SourceOffer->SetTargetOffer(TargetOffer);
        TargetOffer->SetSourcePlayerID(target_id);
        TargetOffer->SetTargetPlayerID(session_id);
        TargetOffer->SetTargetOffer(SourceOffer);

        Players[session_id].trade_offer = SourceOffer;
        Players[target_id].trade_offer = TargetOffer;

        StopTradeRequest(target_id);

        g_pClientInterface->SendCommonPlayerTradeInitiateTrade(session_id, Players[target_id].ship_object_id);
        g_pClientInterface->SendCommonPlayerTradeInitiateTrade(target_id, Players[session_id].ship_object_id);
    }

    // Slot 80: TerminateTrade -- retail .text:06cf1990. The partner slot is indexed raw;
    // the requester is always notified, offer or not.
void IServerImpl::TerminateTrade(unsigned int session_id, int reason) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;

        CPlayerTradeOffer* const Offer = p->trade_offer;
        if (Offer != nullptr) {
            unsigned int const PartnerId = Offer->GetTargetPlayerID();
            p->trade_offer = nullptr;
            CPlayerTradeOffer* const PartnerOffer = Offer->GetTargetOffer();
            if (PartnerId != 0 && PartnerOffer != nullptr) {
                Players[PartnerId].trade_offer = nullptr;
                delete PartnerOffer;
                g_pClientInterface->SendCommonPlayerTradeTerminateTrade(PartnerId, reason);
            }
            delete Offer;
        }

        g_pClientInterface->SendCommonPlayerTradeTerminateTrade(session_id, reason);
    }

    // Slot 81: AcceptTrade -- retail .text:06cf1a40. The partner slot is indexed raw from
    // the offer and the partner's offer is used with NO null test. Money is compared
    // unsigned; each offer's items are matched against a COPY of the giver's cargo; free hold
    // is computed per side through ftol and a negative result ends the trade (reason 5). The
    // cash exchange is unconditional. sync_cargo is called on both narrowed ships, and the
    // system from the own ship's lookup is used, all without null tests.
    // FUN_06d4f460 = PlayerData::get_cash, FUN_06d4f4e0 = PlayerData::adjust_cash,
    // FUN_06d4f260 = PlayerData::remove_cargo, FUN_06d4f230 = PlayerData::add_equipment with
    // the cargo-bay hardpoint, FUN_06d0b110 = StarSystem::send_remote_cargo_update.
void IServerImpl::AcceptTrade(unsigned int session_id, bool b_accepted) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;
        CPlayerTradeOffer* const Offer = p->trade_offer;
        if (Offer == nullptr) return;

        Offer->SetAccepted(b_accepted);

        unsigned int const PartnerId = Offer->GetTargetPlayerID();
        PlayerSlot& Own = Players[session_id];
        PlayerSlot& Partner = Players[PartnerId];                                 // raw
        g_pClientInterface->SendCommonPlayerTradeAcceptTrade(PartnerId, Own.ship_object_id, b_accepted);

        CPlayerTradeOffer* const PartnerOffer = Partner.trade_offer;
        if (!PartnerOffer->GetAccepted()) return;
        if (!Offer->GetAccepted()) return;

        if (Offer->GetMoneyOffered() > static_cast<unsigned long>(Own.get_cash())) {
            TerminateTrade(session_id, 3);
            return;
        }
        if (PartnerOffer->GetMoneyOffered() > static_cast<unsigned long>(Partner.get_cash())) {
            TerminateTrade(session_id, 3);
            return;
        }

        GameObject* OwnObj = nullptr;
        GameObject* PartnerObj = nullptr;
        StarSystem* LookupSystem = nullptr;
        if (!Server_FindObject(Own.ship_object_id, OwnObj, LookupSystem))
            OwnObj = nullptr;
        if (!Server_FindObject(Partner.ship_object_id, PartnerObj, LookupSystem))
            PartnerObj = nullptr;

        CShip* OwnShip = nullptr;
        CBase* const OwnBase = OwnObj;
        if (OwnBase != nullptr) {
            CObject* const Cobj = OwnBase->Object;
            if (Cobj != nullptr && (static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                OwnShip = static_cast<CShip*>(Cobj);
        }
        CShip* PartnerShip = nullptr;
        CBase* const PartnerBase = PartnerObj;
        if (PartnerBase != nullptr) {
            CObject* const Cobj = PartnerBase->Object;
            if (Cobj != nullptr && (static_cast<unsigned int>(Cobj->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
                PartnerShip = static_cast<CShip*>(Cobj);
        }

        std::list<EquipDesc> const& OwnItems = Offer->GetList().Items;
        std::list<EquipDesc> const& PartnerItems = PartnerOffer->GetList().Items;
        std::list<EquipDesc>::const_iterator It;

        EquipDescList OwnCargo(Own.equipment);
        for (It = OwnItems.begin(); It != OwnItems.end(); ++It) {
            EquipDesc const* const Hit = OwnCargo.find_matching_cargo(It->ArchId, It->Count, It->Health);
            if (Hit == nullptr) {
                TerminateTrade(session_id, 3);
                return;
            }
            OwnCargo.remove_equipment_item(Hit->Id, It->Count);
        }

        EquipDescList PartnerCargo(Partner.equipment);
        for (It = PartnerItems.begin(); It != PartnerItems.end(); ++It) {
            EquipDesc const* const Hit = PartnerCargo.find_matching_cargo(It->ArchId, It->Count, It->Health);
            if (Hit == nullptr) {
                TerminateTrade(session_id, 3);
                return;
            }
            PartnerCargo.remove_equipment_item(Hit->Id, It->Count);
        }

        // The x87 difference is taken in double: both operands are floats, so the
        // subtraction is exact before ftol truncates it.
        int const OwnOfferVolume = static_cast<int>(Offer->GetList().get_cargo_space_occupied());
        int const PartnerOfferVolume = static_cast<int>(PartnerOffer->GetList().get_cargo_space_occupied());
        float const OwnHold =
            static_cast<float>(static_cast<int>(Archetype::GetShip(Own.ship_archetype)->HoldSize));
        int const OwnFree = static_cast<int>(static_cast<double>(OwnHold) -
                                             static_cast<double>(Own.equipment.get_cargo_space_occupied()))
                            - PartnerOfferVolume + OwnOfferVolume;
        float const PartnerHold =
            static_cast<float>(static_cast<int>(Archetype::GetShip(Partner.ship_archetype)->HoldSize));
        int const PartnerFree = static_cast<int>(static_cast<double>(PartnerHold) -
                                                 static_cast<double>(Partner.equipment.get_cargo_space_occupied()))
                                - OwnOfferVolume + PartnerOfferVolume;
        if (OwnFree < 0 || PartnerFree < 0) {
            TerminateTrade(session_id, 5);
            return;
        }

        Own.adjust_cash(static_cast<int>(PartnerOffer->GetMoneyOffered() - Offer->GetMoneyOffered()),
                        true);
        Partner.adjust_cash(static_cast<int>(Offer->GetMoneyOffered() - PartnerOffer->GetMoneyOffered()),
                            true);

        for (It = OwnItems.begin(); It != OwnItems.end(); ++It) {
            Own.remove_cargo(It->ArchId, It->Count, It->Health, true);
            Partner.add_equipment(It->ArchId, EquipDesc::CARGO_BAY_HP_NAME, It->Count,
                                  It->Health, /*bEquipped*/false, /*bMission*/false,
                                  /*bNotify*/true);
        }
        for (It = PartnerItems.begin(); It != PartnerItems.end(); ++It) {
            Partner.remove_cargo(It->ArchId, It->Count, It->Health, true);
            Own.add_equipment(It->ArchId, EquipDesc::CARGO_BAY_HP_NAME, It->Count,
                              It->Health, /*bEquipped*/false, /*bMission*/false,
                              /*bNotify*/true);
        }

        OwnShip->sync_cargo(Own.equipment);
        PartnerShip->sync_cargo(Partner.equipment);

        GameObject* ScratchObj = nullptr;
        StarSystem* ShipSystem = nullptr;
        if (!Server_FindObject(OwnShip->Id, ScratchObj, ShipSystem))
            ShipSystem = nullptr;
        ShipSystem->send_remote_cargo_update(OwnObj);
        ShipSystem->send_remote_cargo_update(PartnerObj);

        TerminateTrade(session_id, 1);
    }

    // Slot 82: SetTradeMoney -- retail .text:06cf2010. Any change clears both sides'
    // Accepted and is echoed to both players.
void IServerImpl::SetTradeMoney(unsigned int session_id, unsigned long money) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;
        CPlayerTradeOffer* const Offer = p->trade_offer;
        if (Offer == nullptr) {
            TerminateTrade(session_id, 2);
            return;
        }
        CPlayerTradeOffer* const PartnerOffer = Offer->GetTargetOffer();
        if (PartnerOffer != nullptr)
            PartnerOffer->SetAccepted(false);
        Offer->SetMoneyOffered(money);
        Offer->SetAccepted(false);
        g_pClientInterface->SendCommonPlayerTradeSetTradeMoney(
            session_id, Players[session_id].ship_object_id, money);
        g_pClientInterface->SendCommonPlayerTradeSetTradeMoney(
            Offer->GetTargetPlayerID(), Players[session_id].ship_object_id, money);
    }

    // Slot 83: AddTradeEquip -- retail .text:06cf20e0, as SetTradeMoney. The
    // add_equipment_item result is dropped.
void IServerImpl::AddTradeEquip(unsigned int session_id, const EquipDesc& equip) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;
        CPlayerTradeOffer* const Offer = p->trade_offer;
        if (Offer == nullptr) {
            TerminateTrade(session_id, 2);
            return;
        }
        CPlayerTradeOffer* const PartnerOffer = Offer->GetTargetOffer();
        if (PartnerOffer != nullptr)
            PartnerOffer->SetAccepted(false);
        Offer->GetList().add_equipment_item(equip, arch_is_combinable(equip.ArchId));
        Offer->SetAccepted(false);
        g_pClientInterface->SendCommonPlayerTradeAddTradeEquip(
            session_id, Players[session_id].ship_object_id, equip);
        g_pClientInterface->SendCommonPlayerTradeAddTradeEquip(
            Offer->GetTargetPlayerID(), Players[session_id].ship_object_id, equip);
    }

    // Slot 84: DelTradeEquip -- retail .text:06cf21c0, as AddTradeEquip.
void IServerImpl::DelTradeEquip(unsigned int session_id, const EquipDesc& equip) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;
        CPlayerTradeOffer* const Offer = p->trade_offer;
        if (Offer == nullptr) {
            TerminateTrade(session_id, 2);
            return;
        }
        CPlayerTradeOffer* const PartnerOffer = Offer->GetTargetOffer();
        if (PartnerOffer != nullptr)
            PartnerOffer->SetAccepted(false);
        EquipDescList& List = Offer->GetList();
        EquipDesc const* const Hit = List.find_matching_cargo(equip.ArchId, equip.Count, equip.Health);
        if (Hit != nullptr)
            List.remove_equipment_item(Hit->Id, equip.Count);
        Offer->SetAccepted(false);
        g_pClientInterface->SendCommonPlayerTradeDelTradeEquip(
            session_id, Players[session_id].ship_object_id, equip);
        g_pClientInterface->SendCommonPlayerTradeDelTradeEquip(
            Offer->GetTargetPlayerID(), Players[session_id].ship_object_id, equip);
    }

    // Slot 85: RequestTrade -- retail .text:06cf22c0. FUN_06d50920 is
    // PlayerData::set_trade_request. Retail's own body makes no IClient call.
void IServerImpl::RequestTrade(unsigned int session_id, unsigned int target_id) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;
        PlayerSlot* const target = GetPlayerBySession(target_id);
        if (!target) return;
        if (Players[session_id].trade_offer != nullptr) return;
        if (Players[target_id].trade_request_partner == session_id) {
            InitiateTrade(session_id, target_id);
            return;
        }
        Players[session_id].set_trade_request(target_id);
    }

    // Slot 86: StopTradeRequest -- retail .text:06cf2350. FUN_06d50a40(record, 1) is
    // PlayerData::cancel_trade_request with the partner-list unlink on.
void IServerImpl::StopTradeRequest(unsigned int session_id) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;
        g_pClientInterface->SendCommonPlayerTradeTerminateTrade(session_id, 0);
        Players[session_id].cancel_trade_request(true);
    }

    // DumpPacketStats -- retail .text:06cf23b0: forwards to IClient slot 125, the same code as
    // rpclocal's ?DumpPacketStats@IServerImpl@@UAEXPBD@Z; the null test is retail's.
void IServerImpl::DumpPacketStats(const char* name) {
        if (g_pClientInterface) g_pClientInterface->DumpPacketStats(name);
    }
