// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstdint>
#include "server_exports.h"

namespace pub {
namespace Controller {

// Values and names from Server.dll's priority table at .data:06d8d080 (five 0x14-byte
// rows {char[16] name, int value}), read by FUN_06d43840 for the [Initial * DLLs]
// `DLL` rows; Controller::Create keeps the list ordered by it, lowest value first.
enum PRIORITY { HIGHEST = 0, ABOVE_NORMAL = 1, NORMAL = 2, BELOW_NORMAL = 3, LOWEST = 4 };

// The message id of _SendMessage and IDLL::HandleMessage (Content.dll's controllers). Retail
// has no names for these; names come from the sender (IServer / ContentCallback method), the
// Cnd_* key Content's mission triggers bind to the id (FUN_06ebb850), or the payload.
// 0x00..0x41 are sent by Server.dll (through FUN_06d43650 = _SendMessage(&0u, id, data),
// unless noted), 0x1000..0x1011 by Content.dll. Nothing sends 0x0e, 0x0f or 0x34.
// Each line: sender, then the payload.
enum class EMessage : int
{
    PlayerWorthChanged    = 0x00, // PlayerData cash / cargo / equipment setters: &client id
    BaseEnter             = 0x01, // IServerImpl::BaseEnter: {base, player}
    BaseExit              = 0x02, // IServerImpl::BaseExit: {base, player}
    CargoScanned          = 0x03, // IServerImpl::SPScanCargo: {scanner, target} (Cnd_CargoScanned)
    PlayerSave            = 0x04, // player save writer: {player, FILE*}
    PlayerLoad            = 0x05, // character load; Content Story: {player, INI_Reader*}
    PlayerLaunch          = 0x06, // IServerImpl::PlayerLaunch: {player, exited base} (Cnd_PlayerLaunch)
    PlayerLogin           = 0x07, // IServerImpl::Login (result 3), character select: &player
    PlayerLogout          = 0x08, // PlayerDB logout / character switch: &player
    GroupMemberLeft       = 0x09, // CPlayerGroup::DelMember: &player
    GroupMemberJoined     = 0x0a, // CPlayerGroup::AddMember: &player
    CommComplete          = 0x0b, // IServerImpl::CommComplete; Content Gcs resend: 5 dwords (Cnd_CommComplete)
    ControllerDestroyed   = 0x0c, // pub::Controller::Destroy (direct _SendMessage): &flid
    ObjectDamaged         = 0x0d, // damage paths: FSubObjDamageEvent / FSolarHitEvent (Cnd_HealthDec, Cnd_ProjHit)
    DockCancel            = 0x10, // Ship request cancel, type 0: {ship, target, arg}
    DockComplete          = 0x11, // StarSystem complete_dock: {ship, dock, system}
    DockRequest           = 0x12, // Ship request, type 0: {ship, target, system, byte}
    EquipDropLoot         = 0x13, // equipment about to drop as loot: FEquipLootEvent (receiver may rewrite Desc)
    TradelaneRequest      = 0x14, // Ship request, type 2: {ship, ring, second ring, system}
    TradelaneRelease      = 0x15, // Ship request cancel, type 2: {ship, target, system}
    TradelaneEnter        = 0x16, // StarSystem notify_go_tradelane: {ship, entrance, exit, system} (Cnd_TLEntered)
    TradelaneExit         = 0x17, // StarSystem notify_stop_tradelane: {ship, entrance, exit, system} (Cnd_TLExited)
    LootAcquired          = 0x18, // StarSystem tractor_loot: {loot, ship, good, sub-object, count} (Cnd_LootAcquired)
    FormationRequest      = 0x19, // Ship request, type 1: {ship, leader, system}
    FormationLeave        = 0x1a, // Ship request cancel, type 1: {ship, target, system}
    GFObjSelect           = 0x1b, // IServerImpl::GFObjSelect: {object, player} (Cnd_CharSelect)
    GFGoodBuy             = 0x1c, // IServerImpl::GFGoodBuy: SGFGoodBuyInfo dwords + player
    GFGoodSell            = 0x1d, // IServerImpl::GFGoodSell: SGFGoodSellInfo dwords + player
    GFGoodVaporized       = 0x1e, // IServerImpl::GFGoodVaporized: SGFGoodVaporizedInfo dwords + player
    Hail                  = 0x1f, // IServerImpl::Hail: {object, arg1, arg2}
    Heartbeat             = 0x20, // controller heartbeat walk (vtbl[0] direct, not _SendMessage): float elapsed
    InterfaceItemUsed     = 0x21, // IServerImpl::InterfaceItemUsed: {player, item}
    RtcDone               = 0x22, // IServerImpl::RTCDone: {player, rtc} (Cnd_RTCDone)
    CargoJettisoned       = 0x23, // Ship jettison request: {ship, word equip id, good, count}
    AbortMission          = 0x24, // IServerImpl::AbortMission: {player, mission}
    // TODO(name): sent by IServerImpl::SystemSwitchOutComplete as {ship, gate, system} and
    // for every ship that finished leaving through its dock (Ship::DockedObjId) as {ship,
    // dock object, system}; SpaceSupport destroys Msg[0] when it is one of its system's
    // objects, MissionObject treats it as the ship's jump.
    Msg0x25               = 0x25,
    JumpInComplete        = 0x26, // Ship (vtable slot 0x74): {arg, ship} (Cnd_JumpInComplete)
    // TODO(name): StarSystem dock-list update: {ship, base, target, byte land}; SpaceSupport
    // cancels the ship's docking when land != 0.
    Msg0x27               = 0x27,
    LaunchComplete        = 0x28, // Ship (vtable slot 0x70): {id, ship} (Cnd_LaunchComplete)
    LocationEnter         = 0x29, // IServerImpl::LocationEnter: {location, player} (Cnd_LocEnter)
    LocationExit          = 0x2a, // IServerImpl::LocationExit: {location, player} (Cnd_LocExit)
    LootSpawned           = 0x2b, // StarSystem create_loot: {loot, source object, good, count}
    MissionResponse       = 0x2c, // IServerImpl::MissionResponse; Content: FMissionOfferAcceptMsg {offer id, EMissionOfferKind, byte accepted, player} (Cnd_MsnResponse)
    PhantomEnter          = 0x2d, // phantom pump: {object, phantom, system}
    PhantomExit           = 0x2e, // phantom pump: {object, phantom, system}
    PlayerManeuver        = 0x2f, // IServerImpl::SetManeuver (single player): {player, object, value} (Cnd_PlayerManeuver)
    PopUpDialog           = 0x30, // IServerImpl::PopUpDialog: {player, button} (Cnd_PopUpDialog)
    RequestBestPath       = 0x31, // IServerImpl::RequestBestPath: 10 dwords
    FreeTerminalRequest   = 0x32, // launch position search: {player, dock target}
    RequestPlayerStats    = 0x33, // IServerImpl::RequestPlayerStats: &player
    PlayerShipArchSet     = 0x35, // PlayerData ship arch setter: &client id
    SpaceEnter            = 0x36, // IServerImpl::PlayerLaunch: {system, player, launch object, launch kind} (Cnd_SpaceEnter)
    SpaceExit             = 0x37, // StarSystem on_object_destroyed (the player's ship): {system, player} (Cnd_SpaceExit)
    DeathFuseLit          = 0x38, // death fuse: FDeathFuseEvent
    ObjectDestroyed       = 0x39, // FObjectDestroyedEvent (Cnd_Destroyed)
    SystemExit            = 0x3a, // {system, destination system, player} (Cnd_SystemExit)
    SystemEnter           = 0x3b, // {system, from system, player, arrival object, position} (Cnd_SystemEnter)
    SystemLoaded          = 0x3c, // StarSystem load: &system
    SystemUnloaded        = 0x3d, // StarSystem shutdown / last player out: &system
    TimerExpired          = 0x3e, // pub::SetTimer expiry (to the timer's own controller): TimerExpired
    TradeResponse         = 0x3f, // IServerImpl::TradeResponse: TradeResponseInfo members + player
    LoadGame              = 0x40, // server state load: INI_Reader*
    SaveGame              = 0x41, // server state save: FILE*
    MissionStart          = 0x1000, // Content Story (to the story mission): no payload
    CombatState           = 0x1001, // Content combat_state_notification: {type, param1, param2, system}
    CombatDrift           = 0x1002, // Content combat_drift_event: {ship, system}
    NpcTurnedHostile      = 0x1003, // Content attitude sweep: {object, other, system}
    FleeRequest           = 0x1004, // Content request_flee_destination: {ship, reason, system}
    DamageReputation      = 0x1005, // Content combat reputation: {provoked, attacker, victim, system}
    // TODO(name): Content FUN_06f34100, when an NPC ship's comm-line counter (+0x1b8,
    // counted down per finished comm line) reaches 0: {ship, a dword of its first directive
    // picked by directive type, 1} to the ship's controller.
    Msg0x1006             = 0x1006,
    JumpgateAct           = 0x1007, // Content dock terminal apply: &dock target (Cnd_JumpgateAct)
    LeadObject            = 0x1008, // Content lead_object_event: {object, object, progress, value, position}
    FormationLeaderChanged = 0x1009, // Content: {new leader, old leader}
    NpcSystemExit         = 0x100a, // Content NPC ship system change: {ship, old system} (Cnd_NPCSystemExit)
    PlayerMissionChanged  = 0x100b, // Content player state: {mission controller, player}
    EncounterLaunched     = 0x100c, // Content: std::list<uint> of encounter ids (Cnd_EncLaunched)
    RumorHeard            = 0x100d, // Content BaseSupport: {player, TString<32> role} (Cnd_RumorHeard)
    ShipInSights          = 0x100e, // Content ship_in_sights_event: {ship, object, system}
    NpcSystemEnter        = 0x100f, // never sent: a mission-trigger event made by Content: {ship, system, object} (Cnd_NPCSystemEnter)
    NpcShipCreated        = 0x1010, // Content: FContentNpcShip* by address
    TetherObject          = 0x1011, // Content tether_object_event (also a mission-trigger tick): {object, object, 0, 0} (Cnd_TetherBroke)
};

// Content.dll's StoryLauncher passes a polymorphic parameter object whose payload is the
// player ID read from message 7. Server forwards it unchanged.
struct CreateParms { virtual ~CreateParms() = default; };
struct FStoryCreateParms final : CreateParms { unsigned int PlayerId; };
struct TimerExpired { unsigned int Param1; unsigned int Param2; };

SERVER_API unsigned int __cdecl Create(const char* DllPath, const char* InstanceName, const CreateParms* Parms, PRIORITY Priority);
SERVER_API void __cdecl Destroy(unsigned int Flid);
SERVER_API int __cdecl SetHeartbeatInterval(const unsigned int& Flid, float Interval);
SERVER_API int __cdecl _SendMessage(const unsigned int& Flid, EMessage MsgId, const void* MsgData);

// What the offer id of IServer::MissionResponse / EMessage::MissionResponse identifies.
enum EMissionOfferKind : unsigned
{
    MissionOfferController   = 0, // a mission controller: a story script's offer, or a bar mission just created
    MissionOfferBarCharacter = 1, // a bar character (Content's generic bar scripts)
    MissionOfferVendor       = 2, // a mission vendor (job board) offer
};

} // namespace Controller

SERVER_API int __cdecl SetTimer(const unsigned int& Flid, const Controller::TimerExpired& Timer, float Timeout);

} // namespace pub

// Server.dll-internal: pub::Controller::_SendMessage's dispatch core (IServerImpl.cpp,
// 0x06d44080). Retail FUN_06d43650 is only _SendMessage(&0u, MsgId, MsgData), so Server.dll
// calls Server_ControllerDispatch(0, ...) for it.
extern "C" int __cdecl Server_ControllerDispatch(unsigned int Flid, pub::Controller::EMessage MsgId,
                                                 const void* MsgData);
