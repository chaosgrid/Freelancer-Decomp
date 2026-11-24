// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/server_iserverimpl.h"   // IServerImpl, Server
#include "../include/server_cobject.h"
#include "../include/server_gameobject.h"
#include "../include/common_dataconfig.h" // DataConfig::startup / shutdown
#include "../include/common_timing.h" // Timing::Delta
#include "../include/common_tradeoffer.h" // CPlayerTradeOffer
#include "../include/common_id.h"
#include "../include/common_ini.h"
#include "../include/common_goods.h"
#include "../include/common_universe.h"
#include "../include/common_server_playerdb.h"
#include "../include/server_savegame.h"
#include "../include/server_characterset.h" // CCharacterSet (CharacterInfoReq)
#include "../include/common_misc.h"      // safe_strcpy (CharacterSelect key)
#include "../include/common_server_controller.h"
#include "../include/content.h"   // IDLL, CreateInstance / DestroyInstance (controller DLL entry points)
#include "../include/server_sysdb.h"
#include "../include/server_basedb.h"
#include "../include/server_fuse.h"     // ServerFuseDB (FUN_06cfd4b0 / FUN_06cfd3a0)
#include "../include/rpc_iclient.h"
#include "../include/rpc_iclient_args.h" // FServerSetReputationInfo / FServerCharSelectVerifiedInfo
#include "../include/DALib.h"           // DALib::Data (FUN_06d44ca0)
#include "../include/readfile.h"        // IFileSystem::GetFileSysPath (+0x84)
#include "../include/common_physys.h"
#include "../include/common_casteroid.h"   // CmnAsteroid::Update
#include "../include/common_animdb.h"     // AnimDB::Update (FUN_06d10320)
#include "../include/server_createinfo.h" // FShipCreateRequest, Server_Acquire3DMathEngine (PlayerLaunch)
#include "../include/x86math.h"           // I3DMathEngine (PlayerLaunch)
#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <list>
#include <stdexcept>
#include <cstring>
#include <cstddef>

#pragma comment(lib, "winmm.lib")

// Server_ControllerDispatch (common_server_controller.h), pub::Controller::_SendMessage's
// dispatch core, is defined further down this file. Retail's FUN_06d43650 (exported by the
// port from Common.dll as Server_SendMessage) is only `_SendMessage(0 /* broadcast */,
// MsgId, MsgData)`, so call sites call the dispatcher directly.
extern "C" int __cdecl Server_ControllerSetHeartbeat(unsigned int Flid, float Interval);
extern "C" int __cdecl Server_ControllerSetTimer(unsigned int Flid, unsigned int Param1,
                                                 unsigned int Param2, float Timeout);
// IServerImpl::SubmitChat's body (retail FLChatServer.cpp), src/Server/server_chat.cpp.
void Server_SubmitChat(CHAT_ID Sender, unsigned long Size, const void* Data, CHAT_ID Target, int SystemId);

// Retail exports IClient's default ctor, copy ctor and operator= (rpclocal ships
// ??0IClient@@QAE@XZ / ??4IClient@@QAEAAV0@ABV0@@Z beside ??_7IClient@@6B@; Server.dll
// re-exports all three from server_classes.cpp). The class is abstract and has no data, so
// all three are trivial.
IClient::IClient() {}
IClient::IClient(const IClient& Other) { (void)Other; }
IClient& IClient::operator=(const IClient& Other) { (void)Other; return *this; }

// Retail keeps TWO pairs of client-interface pointers:
//   .data:06d8f618  HMODULE of the client DLL (FUN_06d43340 / FUN_06d43390)
//   .data:06d8f620  IClient*     as loaded
//   .data:06d8f624  IChatClient* as loaded
//   .data:06d8fb14  IClient*     live, used by every caller; zeroed by Shutdown
//   .data:06d8fb18  IChatClient* live, used by every caller; never zeroed
// Startup copies the loaded pair into the live pair through FUN_06d433d0 /
// FUN_06d433c0; FreeClientDll clears only the loaded pair.
static HMODULE g_hClientDll = nullptr;
static IClient* GLoadedClientInterface = nullptr;          // .data:06d8f620
static IChatClient* GLoadedChatClientInterface = nullptr;  // .data:06d8f624
// .data:06d8fb14 -- accessed by SendGroupID/SendSystemID in server_classes.cpp
IClient* g_pClientInterface = nullptr;
// .data:06d8fb18 -- the IChatClient half of rpclocal's Client object; CPlayerGroup's
// group/chat senders call its single slot (see server_classes.cpp). Nothing in retail
// writes it after Startup, so it outlives Shutdown with its stale value.
IChatClient* g_pChatClientInterface = nullptr;
// Retail's server clock is TWO doubles plus the Delta at .data:06d8f970: FUN_06d447b0
// returns DAT_06d8fb28 + DAT_06d8fb20. g_ElapsedTimeAccum (DAT_06d8fb28) is the running sum
// ElapseTime adds read_dt() into, never reset; GServerTimeBase (DAT_06d8fb20) is the base a
// loaded game or world file sets (FUN_06d44810, FUN_06d44ca0) and Startup zeroes.
static Timing::Delta g_TimingDelta;
double g_ElapsedTimeAccum = 0.0;
double GServerTimeBase = 0.0;

// Server_LoadSinglePlayerState -- retail FUN_06d44810 (cdecl). Called only by the
// CharacterSelect worker FUN_06d4a5d0 in single player, on the reader it opened over the
// selected character file. The read_value result is ignored.
void Server_LoadSinglePlayerState(INI_Reader& Reader) {
    INI_Reader::State Saved;
    Reader.get_state(Saved);
    double Time = 0.0;
    if (Reader.read_header()) {
        do {
            if (Reader.is_header("Time")) {
                Reader.read_value();
                Time = Reader.get_value_float(0);
                break;
            }
        } while (Reader.read_header());
    }
    Reader.set_state(Saved);
    GServerTimeBase = Time;
    g_TimingDelta.start();
    ::Reputation::FreeFeelings();
    ::Reputation::LoadFeelings(Reader);
    Server_ControllerDispatch(0, pub::Controller::EMessage::LoadGame, &Reader);
}

// .data:06d8f998, the 11-dword counter block GetServerStats copies out. Startup zero-fills
// it; Login bumps +0x04 / +0x0c, PlayerDB::logout +0x08 (server_classes.cpp),
// AbortMission +0x1c.
ServerStats GServerStats = {};
// .data:DAT_06d8d758 -- IServerS.cpp's own lazily acquired I3DMathEngine (FUN_06d5ff70),
// used only by PlayerLaunch.
static I3DMathEngine* GIServerSMathEngine = nullptr;
// .data:DAT_06d8f9c8 -- [Freelancer] initial_world. Read also by
// PlayerDB::create_restart_file, hence external linkage.
char g_InitialWorldPath[260] = {0};

static IClient* Server_LoadClientDll(const char* module_path);
static void Server_FreeClientDll();
static IChatClient* Server_GetLoadedChatClientInterface();
static IClient* Server_GetLoadedClientInterface();
static void Server_ReadWorldIni(const char* config_path);
static void Server_LoadWorldState();
unsigned int Server_ControllerCreate(const char* dll_path, const char* instance_name, void* parms, unsigned int priority);
static void Server_LoadInitialDlls(const char* IniPath);
static void Server_PlayerSubsystemInit(unsigned int max_players, unsigned int allow_pvp);
static void Server_CleanupPhysics();
static void Server_ControllerCleanup();
static void Server_PhysicsUpdate(float dt);

// FUN_06d0be50 -- one system's update; defined next to Server_PhysicsUpdate.
static unsigned int StarSystem_Update(StarSystem* Sys, float Dt);
static void Server_TimerTick(float dt);
static void Server_TimerFiring(float dt);

static PlayerSlot* GetPlayerBySession(unsigned int session_id) {
    return PlayerDB_GetPlayer(session_id);
}

// PlayerData member FUN_06d48d20 (thiscall on the slot): outside single player,
// IClient::PushToClient(client_id, NULL packet), slot 9, with no null test on the interface.
// Callers: BaseExit's two refusals, the inlined LocationExit refusal inside BaseExit and
// LocationExit's own. What the MP transport does with a NULL packet is not recovered, so the
// name only says what the code does.
static void PlayerData_SendClientNullPacket(PlayerData& Slot) {
    if (SinglePlayer()) return;
    ::g_pClientInterface->PushToClient(Slot.client_id, nullptr);
}

// IServerImpl and the exported object `Server` (ordinal 524) are declared in
// src/include/server_iserverimpl.h; `Server` is defined at the end of this file.

    // Retail .text:06d44b70. Retail writes no boot diagnostics here.
bool IServerImpl::Startup(const SStartupInfo& info) {
        GServerTimeBase = 0.0;             // the base only; g_ElapsedTimeAccum is not reset
        g_TimingDelta.start();
        srand(timeGetTime());
        RefreshLoadingProgress();
        if (CreateID("Freelancer") == 0)
            return false;
        Players.init(info.MaxPlayers, info.bAllowNewPlayers != 0);
        RefreshLoadingProgress();
        // The MODULE path (first 0x104 bytes of SStartupInfo), not the INI path at +0x104.
        Server_LoadClientDll(info.ModulePath);
        g_pClientInterface = Server_GetLoadedClientInterface();
        g_pChatClientInterface = Server_GetLoadedChatClientInterface();
        RefreshLoadingProgress();
        memset(&GServerStats, 0, sizeof(GServerStats));
        // DataConfig::startup returns 0 on success (common_dataconfig.cpp keeps that
        // convention); a failure skips the whole init block and is what Startup returns.
        // DataConfig::startup and FUN_06d448d0 take the INI path.
        const bool bConfigOk = (DataConfig::startup(info.IniPath, nullptr, nullptr) == 0);
        if (bConfigOk) {
            Server_ReadWorldIni(info.IniPath);
            Server_LoadInitialDlls(info.IniPath);
            RefreshLoadingProgress();
            // MP only: FUN_06d44ca0.
            if (!SinglePlayer())
                Server_LoadWorldState();
            RefreshLoadingProgress();
            Server_SubsystemInit();
            RefreshLoadingProgress();
            Server_PlayerSubsystemInit(info.MaxPlayers, info.bAllowPvp);
            RefreshLoadingProgress();
        }
        // IClient slot 10 (session init), reached on BOTH paths and called with no null test.
        g_pClientInterface->Startup(info.MaxPlayers, info.Port);
        RefreshLoadingProgress();
        return bConfigOk;
    }

void IServerImpl::Shutdown() {
        Players.logout_all();
        Server_CleanupPhysics();
        CObject::ReleaseAll();
        Server_SubsystemShutdown();
        Server_ControllerCleanup();
        DataConfig::shutdown();
        if (g_pClientInterface) { // retail's own null test (06d451eb)
            // IClient slot 15 (+0x3c): tears the real client interface down.
            g_pClientInterface->Shutdown();
            Server_FreeClientDll();
            g_pClientInterface = nullptr;
        }
        Players.free();
    }

int IServerImpl::Update() {
        // IClient slot 16 (+0x40): the client-side pump, with no null test.
        g_pClientInterface->Update();
        return 1;
    }

void IServerImpl::ElapseTime(float dt) {
        double real_dt = g_TimingDelta.read_dt();
        if (real_dt < 0.0) real_dt = 0.0;
        g_ElapsedTimeAccum += real_dt;
        Server_PhysicsUpdate(dt);
        Server_SubsystemUpdate(dt);                // FUN_06d18860, given the dt argument
        Server_TimerTick(dt);
        Server_TimerFiring(dt);
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
        PlayerData_TerminateTrade(Players[player_id]);
        static_cast<IObjAffect*>(Obj)->fire_weapons(info.Hardpoints.data(),
                                               static_cast<unsigned>(info.Hardpoints.size()),
                                               &info.TargetPos, static_cast<int>(player_id));
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
    // Slot 8: JettisonCargo -- retail .text:06cf0d50.
void IServerImpl::JettisonCargo(unsigned int player_id, const XJettisonCargo& info) {
        GameObject* const Obj = Server_LookupGameObject(info.ObjectId);
        if (Obj == nullptr)
            return;
        if (Obj->is_destroyed())
            return;
        PlayerData_TerminateTrade(Players[player_id]);
        static_cast<IObjAffect*>(Obj)->jettison_cargo(info.ItemId, static_cast<int>(info.Count),
                                               static_cast<int>(player_id));
    }
    // Slot 13: a no-op in retail (shared empty body 0x06ce1050).
void IServerImpl::SetActiveConnection(EFLConnection /*conn*/) {}
    // Slot 14: returns true (rpclocal 0x06d8f3f0; Server.dll 0x06d519e0).
bool IServerImpl::SwapConnections(EFLConnection /*conn_a*/, EFLConnection /*conn_b*/) { return true; }
void IServerImpl::Connect(const char* server_name, unsigned short* port) {}

    // IServerImpl::DisConnect -- retail .text:06d44fd0. Only the masked ids are tested; the
    // second call of each pair re-reads the id. `type` is ignored, as in retail.
void IServerImpl::DisConnect(unsigned int session_id, EFLConnection type) {
        (void)type;
        if (!Players.is_valid(session_id)) return;
        // Retail calls through the global object, not `this`.
        if ((Players[session_id].current_location & 0x3fffffff) != 0) {
            Server.LocationInfoRequest(Players[session_id].current_location, session_id, false);
            Server.LocationExit(Players[session_id].current_location, session_id);
        }
        if ((Players[session_id].current_base & 0x3fffffff) != 0) {
            Server.BaseInfoRequest(Players[session_id].current_base, session_id, false);
            Server.BaseExit(Players[session_id].current_base, session_id);
        }
        Players.logout(session_id);
    }

    // IServerImpl::OnConnect -- retail .text:06d45090: IClient slot 115 with &Players.m_MOTD
    // (.data:06d90280 = Players + 0x20), no null test on the interface. PlayerDB::SetMOTD
    // sends the same pair to every connected session.
void IServerImpl::OnConnect(unsigned int session_id) {
        g_pClientInterface->SendServerMiscObjUpdateMotd(session_id, Players.GetMOTD());
    }

void IServerImpl::Login(const SLoginInfo& info, unsigned int session_id) {
        // Outside single player, a slot that already has a client (+0x348 client_id) is a
        // duplicate login and is dropped.
        bool is_sp = SinglePlayer();
        if (!is_sp) {
            PlayerSlot* player = GetPlayerBySession(session_id);
            if (player && player->client_id != 0) return;
        }
        unsigned char result = Players.login(info, session_id);
        if (result == 3) Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLogin, &session_id);
        // IClient slot 12, FLPACKET_SERVER_LOGINRESPONSE; the status byte travels by reference.
        g_pClientInterface->SendServerLoginResponse(session_id, result);
        // Retail reads back the stored login result here (Ghidra shows it as the high byte of
        // param_2).
        if (result == 3) {
            GServerStats.LoginSuccessCount++;
        } else {
            GServerStats.LoginFailCount++;
        }
    }

void IServerImpl::CharacterInfoReq(unsigned int session_id, bool bSendEmpty) {
        // Retail .text:06d45220.
        if (!Players.is_valid(session_id)) return;

        CCharacterSet Set;

        // Only when the bool is false does the slot fill it (FUN_06d499c0); with the bool
        // set (CreateNewCharacter's failure path), the list goes out empty.
        if (!bSendEmpty)
            PlayerData_BuildCharacterSet(Players[session_id], Set);

        // IClient slot 13, FLPACKET_SERVER_CHARACTERINFO; no null test on the interface.
        g_pClientInterface->SendServerCharacterInfo(session_id, Set);
    }

void IServerImpl::CharacterSelect(const CHARACTER_ID& char_id, unsigned int session_id) {
        // Retail .text:06d453f0.
        if (!Players.is_valid(session_id)) return;
        PlayerData& Slot = Players[session_id];

        // The worker FUN_06d4a5d0 (server_classes.cpp); a miss or a failed load ends here.
        if (!PlayerData_SelectCharacter(Slot, char_id, session_id)) return;

        // The SETREPUTATION argument. +0x04 is never written by retail (stack residue);
        // zeroed here.
        FServerSetReputationInfo Info;
        Info.Attitude = 0.0f;
        Info.Affiliation = 0;
        Info.Rank = 0;
        Info.Relations = nullptr;
        Info.RelationCount = 0;
        Info.Pad_0x15[0] = 0;
        Info.Pad_0x15[1] = 0;
        Info.Pad_0x15[2] = 0;
        Info.DisplayName[0] = L'\0';
        Info.VibeHandle = Slot.rep_vibe;
        Info.Relations = static_cast< ::Reputation::Relation*>(
            ::operator new(::Reputation::group_count() * 8));
        wchar_t const* VibeName = nullptr;
        ::Reputation::Vibe::Get(Info.VibeHandle, Info.Affiliation, Info.Rank,
                                Info.RelationCount, Info.Relations, Info.NameA,
                                Info.NameB, VibeName);
        if (VibeName == nullptr) {
            Info.DisplayName[0] = L'\0';
        } else {
            wcscpy(Info.DisplayName, VibeName);                          // unbounded, as retail
        }

        // To every active slot.
        for (PlayerData* Active = Players.traverse_active(nullptr); Active != nullptr;
             Active = Players.traverse_active(Active)) {
            g_pClientInterface->SendServerSetReputation(Active->client_id, Info);
        }

        unsigned int EnterEvent[7];
        EnterEvent[0] = Slot.system_id;
        EnterEvent[1] = 0;
        EnterEvent[2] = session_id;
        EnterEvent[3] = 0;
        EnterEvent[4] = 0;
        EnterEvent[5] = 0;
        EnterEvent[6] = 0;
        Server_ControllerDispatch(0, pub::Controller::EMessage::SystemEnter, EnterEvent);

        // +0x04 is unwritten in retail; the time is FUN_06d447b0's sum.
        FServerCharSelectVerifiedInfo Verified;
        Verified.VibeHandle = Info.VibeHandle;
        Verified.Unused_0x04 = 0;
        Verified.ServerTime = g_ElapsedTimeAccum + GServerTimeBase;
        g_pClientInterface->SendServerCharSelectVerified(session_id, Verified);

        // The result code is not tested.
        int AffiliationVibe = Slot.rep_vibe;
        unsigned int Affiliation = 0xffffffffu;
        ::Reputation::Vibe::GetAffiliation(AffiliationVibe, Affiliation, false);

        g_pClientInterface->SendServerMiscObjUpdateAffiliation(session_id, Slot.ship_object_id, Affiliation);

        ::operator delete(Info.Relations);
    }

    // Slot 21: a no-op in Server.dll (shared empty body 0x06ce1050); rpclocal 0x06d8cdc0
    // sends FLPACKET_CLIENT_REQUESTNEWCHARINFO.
void IServerImpl::NewCharacterInfoReq(unsigned int /*session_id*/) {}

void IServerImpl::CreateNewCharacter(const SCreateCharacterInfo& info, unsigned int session_id) {
        // Retail .text:06d45670, no guards.
        if (Players.create_new_character(info, session_id)) {
            CharacterInfoReq(session_id, false);
        } else {
            CharacterInfoReq(session_id, true);
        }
    }

void IServerImpl::DestroyCharacter(const CHARACTER_ID& char_id, unsigned int session_id) {
        // Retail .text:06d456b0.
        if (!Players.is_valid(session_id)) return;
        PlayerSlot* player = &Players[session_id];

        // FUN_06d49f60, result ignored (server_classes.cpp).
        PlayerData_DestroyCharacter(*player, char_id, session_id);

        CharacterInfoReq(session_id, false);
    }

void IServerImpl::CharacterSkipAutosave(unsigned int session_id) {
        // Retail .text:06d45380: PlayerDB::is_valid inlined, then FUN_06d4c5f0(slot).
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        PlayerData_SkipNextAutosave(*p);
    }

    // The in-base request family, .text:06d17300 .. 06d1761b. Every member but
    // ReqHullStatus gates on `SinglePlayer() || entered_base_id != 0` (+0x3F4, an UNMASKED
    // dword test, the base BaseEnter wrote) on a slot indexed RAW -- no PlayerDB::is_valid,
    // no client-id test.

    // ReqShipArch -- retail .text:06d17300; FUN_06d4ec10 is PlayerData_SetShipArch.
void IServerImpl::ReqShipArch(unsigned int arch_id, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        PlayerData_SetShipArch(Players[session_id], arch_id);
    }
    // ReqHullStatus -- retail .text:06d17340, the one member of the family that validates
    // the session first. The x87 clamp keeps NaN (only a true `< 0` is zeroed).
    // FUN_06d4ed10 is PlayerData_SetRelativeHealth.
void IServerImpl::ReqHullStatus(float hull_status, unsigned int session_id) {
        if (!Players.is_valid(session_id)) return;
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        if ((Players[session_id].current_base & 0x3fffffff) == 0) return;
        if (hull_status < 0.0f) hull_status = 0.0f;
        PlayerData_SetRelativeHealth(Players[session_id], hull_status, true);
    }
    // ReqCollisionGroups -- retail .text:06d173e0; FUN_06d4f320.
void IServerImpl::ReqCollisionGroups(const std::list<CollisionGroupDesc>& groups, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        PlayerData_SetCollisionGroups(Players[session_id], groups);
    }
    // ReqEquipment -- retail .text:06d17420; FUN_06d4eeb0.
void IServerImpl::ReqEquipment(const EquipDescList& equipment, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        PlayerData_LoadEquipment(Players[session_id], equipment);
    }

    // Slot 29: ReqCargo -- retail .text:06d28880 is empty (ordinal 366, ICF-folded with
    // Connect, ordinal 83; vtable .rdata:06d65bcc + 0x74). The packet is dead on the server
    // side: no EquipDescList change, no broadcast. Cargo state arrives through ReqAddItem /
    // ReqRemoveItem / ReqEquipment.
void IServerImpl::ReqCargo(const EquipDescList& cargo, unsigned int session_id) {
        (void)cargo;
        (void)session_id;
    }
    // ReqAddItem -- retail .text:06d17460; FUN_06d4efc0 with a CacheString built on the
    // stack. hardpoint goes to StringAlloc unchecked (the port's StringAlloc maps null to
    // its empty string).
void IServerImpl::ReqAddItem(unsigned int arch_id, const char* hardpoint, int count, float health,
                    unsigned char b_mounted, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        CacheString Hp;
        Hp.Value = StringAlloc(hardpoint, false);
        PlayerData_AddEquipment(Players[session_id], arch_id, Hp, count, health, b_mounted,
                                /*bMission*/false, /*bNotify*/true);
    }
    // ReqRemoveItem -- retail .text:06d174e0, tail call to FUN_06d4f100 with bNotify = 0:
    // no IClient slot 27, the client asked for the removal and already knows.
void IServerImpl::ReqRemoveItem(unsigned short equip_id, int count, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        PlayerData_RemoveEquipment(Players[session_id], equip_id, count, /*bNotify*/false);
    }
    // ReqModifyItem -- retail .text:06d17530; FUN_06d4f170 takes six stack dwords but reads
    // five, so the port drops the trailing literal 0.
void IServerImpl::ReqModifyItem(unsigned short equip_id, const char* hardpoint, int count, float health,
                       unsigned char b_mounted, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        CacheString Hp;
        Hp.Value = StringAlloc(hardpoint, false);
        PlayerData_ModifyEquipment(Players[session_id], equip_id, Hp, count, health, b_mounted);
    }

void IServerImpl::ReqSetCash(int cash, unsigned int session_id) {
        // Retail .text:06d175a0: the in-base gate, then FUN_06d4f480 (PlayerData_SetCash).
        // Cash is an int (?ReqSetCash@...@@UAEXHI@Z).
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        PlayerData_SetCash(Players[session_id], cash);
    }

void IServerImpl::ReqChangeCash(int delta, unsigned int session_id) {
        // Retail .text:06d175e0: the in-base gate, then FUN_06d4f4e0(delta, false) --
        // PlayerData_AdjustCash without the +0x320 total.
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        PlayerData_AdjustCash(Players[session_id], delta, false);
    }

    // IServerImpl::BaseEnter -- retail .text:06d17630. The system change is keyed on
    // +0x410 exited_base_id, not on comparing systems; 0x3b carries the same seven-dword
    // shape CharacterSelect and RequestCreateShip send. Outside single player:
    // FUN_06d4f560 then FUN_06d4fc30.
void IServerImpl::BaseEnter(unsigned int base_id, unsigned int session_id) {
        if (!Players.is_valid(session_id)) return;
        PlayerData& Slot = Players[session_id];
        if ((Slot.entered_base_id & 0x3fffffff) != 0) return;

        unsigned int const ExitedBase = Slot.exited_base_id;
        Universe::IBase const* Base = Universe::get_base(base_id);
        if (Base == nullptr) return;

        if (ExitedBase != 0 && base_id != 0 && ExitedBase != base_id) {
            unsigned int const OldSystem = Slot.system_id;
            unsigned int const NewSystem = Base->SystemId;   // IBase vtable +0x10
            unsigned int LeaveEvent[3] = { OldSystem, NewSystem, session_id };
            Server_ControllerDispatch(0, pub::Controller::EMessage::SystemExit, LeaveEvent);
            Slot.system_id = NewSystem;
            unsigned int EnterEvent[7] = { NewSystem, OldSystem, session_id, 0, 0, 0, 0 };
            Server_ControllerDispatch(0, pub::Controller::EMessage::SystemEnter, EnterEvent);
        }

        Slot.current_base = base_id;
        Slot.last_base_id = base_id;
        Slot.entered_base_id = base_id;

        GetBaseDB()->load_base(session_id, base_id);   // FUN_06d14230

        unsigned int BaseEvent[2] = { base_id, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::BaseEnter, BaseEvent);

        if (!SinglePlayer()) {
            PlayerData_ResetTotalEarned(Players[session_id]);
            PlayerData_SnapshotEquipment(Players[session_id]);
        }
    }

    // IServerImpl::BaseExit -- retail .text:06d177b0. Retail inlines LocationInfoRequest
    // and LocationExit (same order, same arguments); the port calls them. The base id is
    // compared raw against entered_base_id. FUN_06d4c430 is the autosave.
void IServerImpl::BaseExit(unsigned int base_id, unsigned int session_id) {
        if (!Players.is_valid(session_id)) return;
        if ((Players[session_id].entered_base_id & 0x3fffffff) == 0) {
            PlayerData_SendClientNullPacket(Players[session_id]);
            return;
        }
        if (base_id != Players[session_id].entered_base_id) {
            PlayerData_SendClientNullPacket(Players[session_id]);
            return;
        }
        if ((Players[session_id].current_location & 0x3fffffff) != 0) {
            LocationInfoRequest(Players[session_id].current_location, session_id, false);
            LocationExit(Players[session_id].current_location, session_id);
        }

        unsigned int ExitEvent[2] = { base_id, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::BaseExit, ExitEvent);
        GetBaseDB()->unload_base(session_id, base_id);   // FUN_06d142c0
        PlayerData_AutoSave(Players[session_id]);

        Players[session_id].exited_base_id = Players[session_id].current_base;
        Players[session_id].current_base = 0;
        Players[session_id].entered_base_id = 0;
    }

    // IServerImpl::LocationEnter -- retail .text:06d17970. A find_location miss FDUMPs
    // inside BaseDB.
void IServerImpl::LocationEnter(unsigned int location_id, unsigned int session_id) {
        if (!Players.is_valid(session_id)) return;
        if ((Players[session_id].current_location & 0x3fffffff) != 0) return;
        if ((Players[session_id].current_base & 0x3fffffff) == 0) return;
        if (GetBaseDB()->find_location(location_id) == nullptr) return;
        Players[session_id].current_location = location_id;
        GetBaseDB()->enter_location(session_id, location_id);
        unsigned int RoomEvent[2] = { location_id, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::LocationEnter, RoomEvent);
    }

    // IServerImpl::LocationExit -- retail .text:06d17a10 (BaseExit inlines it). The id is
    // compared raw. BaseDB::exit_location tears down the NPC characters; the content
    // unsubscribe is LocationInfoRequest(.., false)'s job.
void IServerImpl::LocationExit(unsigned int location_id, unsigned int session_id) {
        if (!Players.is_valid(session_id)) return;
        if ((Players[session_id].current_base & 0x3fffffff) == 0 ||
            (Players[session_id].current_location & 0x3fffffff) == 0 ||
            Players[session_id].current_location != location_id) {
            PlayerData_SendClientNullPacket(Players[session_id]);
            return;
        }
        unsigned int RoomEvent[2] = { location_id, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::LocationExit, RoomEvent);
        GetBaseDB()->exit_location(session_id, location_id);   // FUN_06d143e0
        Players[session_id].current_location = 0;
    }

    // IServerImpl::BaseInfoRequest -- retail .text:06d17ad0, then FUN_06d14570.
    // b_full == true subscribes the session, false unsubscribes it.
void IServerImpl::BaseInfoRequest(unsigned int base_id, unsigned int session_id, bool b_full) {
        if (!Players.is_valid(session_id)) return;
        PlayerSlot* player = &Players[session_id];
        // A subscribe is honoured only for the base the player is in (raw compare, no
        // 0x3fffffff mask).
        if (b_full && base_id != player->current_base) return;

        // Retail tests the accessor's result (FUN_06d15650 returns a static's address, so
        // this never fails).
        if (GetBaseDB() == nullptr) return;
        GetBaseDB()->base_info(session_id, base_id, b_full);
    }
    // IServerImpl::LocationInfoRequest -- retail .text:06d17b40, then FUN_06d146e0. A room
    // miss FDUMPs and returns.
void IServerImpl::LocationInfoRequest(unsigned int location_id, unsigned int session_id, bool b_full) {
        if (!Players.is_valid(session_id)) return;
        PlayerSlot* player = &Players[session_id];
        // Unmasked compare.
        if (b_full && location_id != player->current_location) return;

        if (GetBaseDB() == nullptr) return;
        GetBaseDB()->location_info(session_id, location_id, b_full);
    }
    // IServerImpl::GFObjSelect -- retail .text:06d45710. Slot indexed raw, no session
    // validation at all.
void IServerImpl::GFObjSelect(unsigned int object_id, unsigned int session_id) {
        unsigned int const CurrentBase = Players[session_id].current_base;
        if ((CurrentBase & 0x3fffffff) == 0) return;
        if (Universe::get_base(CurrentBase) == nullptr) return;
        unsigned int SelectEvent[2] = { object_id, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::GFObjSelect, SelectEvent);
    }
    // GFGoodVaporized -- retail .text:06d17c30: no session test; the struct's three dwords
    // plus the session id go out as message 0x1e. The field semantics are unidentified.
void IServerImpl::GFGoodVaporized(const SGFGoodVaporizedInfo& info, unsigned int session_id) {
        unsigned int msg_data[4] = { info.Field_0x00, info.Field_0x04, info.Field_0x08, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::GFGoodVaporized, msg_data);
    }
// Retail .text:06d45770 writes a BYTE at +8; bytes +9..+b stay unwritten.
void IServerImpl::MissionResponse(unsigned int OfferId, unsigned long OfferKind,
                                 unsigned char bAccepted, unsigned int SessionId) {
    struct FMissionResponseMessage {
        unsigned int OfferId;
        unsigned long OfferKind;
        unsigned char bAccepted;
        unsigned char Padding[3];
        unsigned int SessionId;
    };
    static_assert(sizeof(FMissionResponseMessage) == 0x10, "MissionResponse message size");
    FMissionResponseMessage Message;
    Message.OfferKind = OfferKind;
    Message.OfferId = OfferId;
    Message.bAccepted = bAccepted;
    Message.SessionId = SessionId;
    Server_ControllerDispatch(0, pub::Controller::EMessage::MissionResponse, &Message);
}
    // TradeResponse -- retail .text:06d45840. No session or size test. Message 0x3f carries
    // a TradeResponseInfo's members followed by the session id; the pad bytes after
    // bFlag08 are never written.
void IServerImpl::TradeResponse(const unsigned char* data, int size, unsigned int session_id) {
        MarshalBuffer Buffer;
        Buffer.use_read_buffer(data, size);
        TradeResponseInfo Info;
        Info.unmarshal(&Buffer);

        struct FTradeResponseMessage {
            int Field00;                  // +0x00
            int Field04;                  // +0x04
            bool bFlag08;                 // +0x08
            GoodDescList OfferedGoods;    // +0x0c
            GoodDescList RequestedGoods;  // +0x20
            unsigned int SessionId;       // +0x34
        };
        FTradeResponseMessage Message;
        Message.Field00 = Info.Field00;
        Message.bFlag08 = Info.bFlag08;
        Message.Field04 = Info.Field04;
        Message.OfferedGoods = Info.OfferedGoods;                     // appending operator=
        Message.RequestedGoods = Info.RequestedGoods;
        Message.SessionId = session_id;
        Server_ControllerDispatch(0, pub::Controller::EMessage::TradeResponse, &Message);
    }

    // GFGoodBuy -- retail .text:06d17bb0: no session test; message 0x1c.
void IServerImpl::GFGoodBuy(const SGFGoodBuyInfo& info, unsigned int session_id) {
        unsigned int msg_data[5] = { info.BaseId, info.Null04, info.GoodId,
                                     info.Count, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::GFGoodBuy, msg_data);
    }
    // GFGoodSell -- retail .text:06d17bf0: no session test; message 0x1d.
void IServerImpl::GFGoodSell(const SGFGoodSellInfo& info, unsigned int session_id) {
        unsigned int msg_data[4] = { info.BaseId, info.GoodId, info.Count, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::GFGoodSell, msg_data);
    }

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
            FDUMP(0x100001, "%s(%d) : *** ERROR: FATAL: Couldn't find gate destination system!",
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
        Parms.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
        Parms.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
        Parms.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
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
            FDUMP(0x100001, "%s(%d) : *** ERROR: Couldn't find arrival gate %s.",
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

        PlayerData_AutoSave(*p);
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

        // The file's lazily acquired math engine (DAT_06d8d758).
        if (GIServerSMathEngine == nullptr)
            GIServerSMathEngine = Server_Acquire3DMathEngine();
        Matrix LaunchOrient;
        GIServerSMathEngine->QuaternionToMatrix(reinterpret_cast<FMatrix&>(LaunchOrient),
                                                reinterpret_cast<FQuaternion const&>(Req.Orientation));
        if (GIServerSMathEngine == nullptr)
            GIServerSMathEngine = Server_Acquire3DMathEngine();
        Vector HpOffset;
        GIServerSMathEngine->TransformMatrix(reinterpret_cast<FVector&>(HpOffset),
                                             reinterpret_cast<FMatrix const&>(LaunchOrient),
                                             reinterpret_cast<FVector const&>(HpPos));

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
            if (GIServerSMathEngine == nullptr)
                GIServerSMathEngine = Server_Acquire3DMathEngine();
            GIServerSMathEngine->QuaternionToMatrix(reinterpret_cast<FMatrix&>(LaunchOrient),
                                                    reinterpret_cast<FQuaternion const&>(Req.Orientation));
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

    // Hail -- retail .text:06cf0800. Only the found flag is tested. All three arguments are
    // serialized by rpclocal -- no session id.
void IServerImpl::Hail(unsigned int object_id, unsigned int arg1, unsigned int arg2) {
        if (Server_LookupGameObject(object_id) == nullptr)
            return;
        unsigned int msg_data[3] = { object_id, arg1, arg2 };
        Server_ControllerDispatch(0, pub::Controller::EMessage::Hail, msg_data);
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
            if (!PlayerData_CheckPositionBound(Players[session_id], &info.Position))
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
        PlayerData_TerminateTrade(Players[session_id]);
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
void IServerImpl::SaveGame(const CHARACTER_ID& char_id, const wchar_t* description,
                  unsigned int session_id) {
        // Retail .text:06d459d0: SP only, a live session, and NOT in a ship (no in-base
        // exception), then a tail call to FUN_06d4c800(slot, char_id, description, 0).
        if (!SinglePlayer()) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        if ((p->ship_object_id & 0x3fffffff) != 0) return;
        PlayerData_SaveGame(*p, char_id.Buffer, description, 0);
    }
    // MissionSaveB -- retail .text:06d45a30: SP only, a live session, then
    // FUN_06d4c600(slot, mission_id) -- the mission id IS the save's descrip_strid.
void IServerImpl::MissionSaveB(unsigned int session_id, unsigned long mission_id) {
        if (!SinglePlayer()) return;
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        PlayerData_MissionSave(*p, static_cast<unsigned int>(mission_id));
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
    // MineAsteroid -- retail .text:06cf07d0: the SysDB lookup is not null-tested;
    // StarSystem::spawn_mined_loot is FUN_06d0d9b0. session_id is never read; no controller
    // message.
void IServerImpl::MineAsteroid(unsigned int system_id, const Vector& position, unsigned int loot_id,
                      unsigned int secondary_loot, unsigned int quantity, unsigned int session_id) {
        (void)session_id;
        StarSystem* const System = SysDB_LookupSystem(system_id);
        System->spawn_mined_loot(position, loot_id, secondary_loot, static_cast<int>(quantity));
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
    // SPScanCargo -- retail .text:06cf12e0: no branch, never reads the session; message 3
    // with a TWO-dword payload.
void IServerImpl::SPScanCargo(const unsigned int& scanner_id, const unsigned int& target_id,
                     unsigned int session_id) {
        (void)session_id;
        unsigned int msg_data[2] = { scanner_id, target_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::CargoScanned, msg_data);
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
    // InterfaceItemUsed -- retail .text:06d457b0: no branch; message 0x21.
void IServerImpl::InterfaceItemUsed(unsigned int session_id, unsigned int item_id) {
        unsigned int msg_data[2] = { session_id, item_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::InterfaceItemUsed, msg_data);
    }
void IServerImpl::AbortMission(unsigned int session_id, unsigned int mission_id) {
        // Retail .text:06d45810.
        unsigned int msg_data[2] = { session_id, mission_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::AbortMission, msg_data);
        GServerStats.AbortMissionCount++;                  // .data:06d8f9b4
    }
void IServerImpl::RTCDone(unsigned int session_id, unsigned int rtc_id) {
        unsigned int msg_data[2] = { session_id, rtc_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::RtcDone, msg_data);
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
    // FUN_06d4f460 = PlayerData_GetCash, FUN_06d4f4e0 = PlayerData_AdjustCash,
    // FUN_06d4f260 = PlayerData_RemoveCargo, FUN_06d4f230 = PlayerData_AddEquipment with
    // the cargo-bay hardpoint, FUN_06d0b110 = StarSystem_SendRemoteCargoUpdate.
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

        if (Offer->GetMoneyOffered() > static_cast<unsigned long>(PlayerData_GetCash(Own))) {
            TerminateTrade(session_id, 3);
            return;
        }
        if (PartnerOffer->GetMoneyOffered() > static_cast<unsigned long>(PlayerData_GetCash(Partner))) {
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

        PlayerData_AdjustCash(Own, static_cast<int>(PartnerOffer->GetMoneyOffered() - Offer->GetMoneyOffered()),
                              true);
        PlayerData_AdjustCash(Partner, static_cast<int>(Offer->GetMoneyOffered() - PartnerOffer->GetMoneyOffered()),
                              true);

        for (It = OwnItems.begin(); It != OwnItems.end(); ++It) {
            PlayerData_RemoveCargo(Own, It->ArchId, It->Count, It->Health, true);
            PlayerData_AddEquipment(Partner, It->ArchId, EquipDesc::CARGO_BAY_HP_NAME, It->Count,
                                    It->Health, /*bEquipped*/false, /*bMission*/false,
                                    /*bNotify*/true);
        }
        for (It = PartnerItems.begin(); It != PartnerItems.end(); ++It) {
            PlayerData_RemoveCargo(Partner, It->ArchId, It->Count, It->Health, true);
            PlayerData_AddEquipment(Own, It->ArchId, EquipDesc::CARGO_BAY_HP_NAME, It->Count,
                                    It->Health, /*bEquipped*/false, /*bMission*/false,
                                    /*bNotify*/true);
        }

        OwnShip->sync_cargo(Own.equipment);
        PartnerShip->sync_cargo(Partner.equipment);

        GameObject* ScratchObj = nullptr;
        StarSystem* ShipSystem = nullptr;
        if (!Server_FindObject(OwnShip->Id, ScratchObj, ShipSystem))
            ShipSystem = nullptr;
        StarSystem_SendRemoteCargoUpdate(*ShipSystem, OwnObj);
        StarSystem_SendRemoteCargoUpdate(*ShipSystem, PartnerObj);

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
    // PlayerData_SetTradeRequest. Retail's own body makes no IClient call.
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
        PlayerData_SetTradeRequest(Players[session_id], target_id);
    }

    // Slot 86: StopTradeRequest -- retail .text:06cf2350. FUN_06d50a40(record, 1) is
    // PlayerData_CancelTradeRequest with the partner-list unlink on.
void IServerImpl::StopTradeRequest(unsigned int session_id) {
        PlayerSlot* const p = GetPlayerBySession(session_id);
        if (!p) return;
        g_pClientInterface->SendCommonPlayerTradeTerminateTrade(session_id, 0);
        PlayerData_CancelTradeRequest(Players[session_id], true);
    }
    // ReqDifficultyScale -- retail .text:06d45a90: SP only; FUN_06d4ed40 clamps to
    // [0.0, 2.0] and stores at player+0x3C4.
void IServerImpl::ReqDifficultyScale(float scale, unsigned int session_id) {
        if (!SinglePlayer()) return;
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        // The x87 tests are written as negations so a NaN goes the same way as in retail:
        // below, equal or unordered takes the 0.0f arm (.rdata:06d65188); not-below or
        // unordered then takes the 2.0f arm (.rdata:06d6b734).
        if (!(scale > 0.0f)) scale = 0.0f;
        if (!(scale < 2.0f)) scale = 2.0f;
        p->difficulty_scale = scale;
    }
    // Retail .text:06d453d0 ignores `this` and reads Players + 0x8 (PlayerDB::m_ServerID)
    // through an immediate global address -- retail's IServerImpl embeds no PlayerDB.
unsigned int IServerImpl::GetServerID() { return Players.GetServerID(); }
    // Retail .text:06d453e0 tail-jumps into CPlayerAccount::GetAccountNameSigChar on
    // Players + 0x10.
const char* IServerImpl::GetServerSig() { return Players.GetServerSig(); }
    // DumpPacketStats -- retail .text:06cf23b0: forwards to IClient slot 125, the same code as
    // rpclocal's ?DumpPacketStats@IServerImpl@@UAEXPBD@Z; the null test is retail's.
void IServerImpl::DumpPacketStats(const char* name) {
        if (g_pClientInterface) g_pClientInterface->DumpPacketStats(name);
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
        StarSystem_Update(System, 0.2f);

        PlayerData_FlushPendingVisits(Players[session_id]);   // FUN_06d4b450

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

    // Slot 92: PushToServer -- a no-op in Server.dll (shared empty body 0x06ce1050).
    // IServerImpl's own virtual, not an IServer override (IServer's table,
    // .rdata:06d65d4c, stops at slot 91). The packet queue lives in rpclocal
    // (IServerImpl::PushToServer 0x06d8f460).
void IServerImpl::PushToServer(CDAPacket* /*packet*/) {}

    // IChatServer::SubmitChat -- retail 0x06d191b0. Retail's FLChatServer.cpp lives in
    // server_chat.cpp; the body touches no member of IServerImpl.
void IServerImpl::SubmitChat(CHAT_ID Sender, unsigned long Size, const void* Data, CHAT_ID Target, int SystemId) {
        Server_SubmitChat(Sender, Size, Data, Target, SystemId);
    }

    // Non-virtual export ?GetServerStats@IServerImpl@@QAEXAAUServerStats@@@Z, .text:06d45af0.
void IServerImpl::GetServerStats(ServerStats& OutStats) {
        OutStats = GServerStats;
        unsigned int PlayerCount = 0;
        PlayerData* Slot = Players.traverse_active(nullptr);
        while (Slot != nullptr) {
            ++PlayerCount;
            Slot = Players.traverse_active(Slot);
        }
        OutStats.Reserved3 = 0;
        OutStats.Reserved2 = 0;
        OutStats.Reserved1 = 0;
        OutStats.PlayerCount = PlayerCount;
    }

    // Retail IServerImpl is twelve bytes and holds no player storage: IServer vptr at +0,
    // IChatServer vptr at +4, one dword (Field_0x8, server_iserverimpl.h) at +8 (ctor
    // .text:06cef560; copy ctor 06cef580 and operator= 06cef5b0 copy that dword; `Server`,
    // ordinal 524, .data:06d8facc). The player table is the exported global `Players`.

// FUN_06d43340. Retail keeps the module even when LoadLibraryA fails, and on success calls
// BOTH GetProcAddress results with no null test. Startup ignores the return value.
static IClient* Server_LoadClientDll(const char* module_path) {
    g_hClientDll = LoadLibraryA(module_path);
    if (g_hClientDll) {
        typedef IClient* (__cdecl *GetClientInterfaceFn)();
        typedef IChatClient* (__cdecl *GetChatClientInterfaceFn)();
        GetClientInterfaceFn pfn = (GetClientInterfaceFn)GetProcAddress(g_hClientDll, "GetClientInterface");
        GLoadedClientInterface = pfn();
        GetChatClientInterfaceFn pfn2 = (GetChatClientInterfaceFn)GetProcAddress(g_hClientDll, "GetChatClientInterface");
        GLoadedChatClientInterface = pfn2();
    }
    return GLoadedClientInterface;
}

// FUN_06d43390. Only the LOADED pair is cleared; Shutdown zeroes the live IClient, the
// live IChatClient is never zeroed in retail. Shutdown pushes an argument the body never
// reads, so the port takes none.
static void Server_FreeClientDll() {
    if (g_hClientDll) {
        FreeLibrary(g_hClientDll);
        g_hClientDll = nullptr;
        GLoadedClientInterface = nullptr;
        GLoadedChatClientInterface = nullptr;
    }
}

// FUN_06d433c0; sole caller Startup.
static IChatClient* Server_GetLoadedChatClientInterface() {
    return GLoadedChatClientInterface;
}

// FUN_06d433d0; sole caller Startup.
static IClient* Server_GetLoadedClientInterface() {
    return GLoadedClientInterface;
}

// FUN_06d448d0 -- reads [Freelancer] initial_world from freelancer.ini.
static void Server_ReadWorldIni(const char* config_path) {
    INI_Reader reader;
    if (!reader.open(config_path, false)) return;
    while (reader.read_header()) {
        if (reader.is_header("Freelancer")) {
            while (reader.read_value()) {
                if (reader.is_value("initial_world")) {
                    // No null test on the returned pointer. Retail's copy is unbounded;
                    // the port truncates at the buffer size. Retail stops reading once
                    // found.
                    const char* val = reader.get_value_string();
                    strncpy_s(g_InitialWorldPath, sizeof(g_InitialWorldPath), val, _TRUNCATE);
                    return;
                }
            }
        }
    }
}

// Server_LoadWorldState -- retail FUN_06d44ca0, called only from Startup in multiplayer.
// The MP twin of Server_LoadSinglePlayerState over the initial_world file; the open result
// is not tested.
static void Server_LoadWorldState() {
    // Retail builds the path in place (same buffer as dir and output); the
    // modern CRT's _makepath may pre-fill its output in debug builds, so the
    // dir half gets its own buffer. The resulting string is the same.
    char DataPath[0x104];
    DALib::Data->GetFileSysPath(DataPath, 0x104);
    char WorldPath[0x104];
    _makepath(WorldPath, nullptr, DataPath, g_InitialWorldPath, nullptr);

    INI_Reader Reader;
    Reader.open(WorldPath, false);

    INI_Reader::State Saved;
    Reader.get_state(Saved);
    double Time = 0.0;
    if (Reader.read_header()) {
        do {
            if (Reader.is_header("Time")) {
                Reader.read_value();
                Time = Reader.get_value_float(0);
                break;
            }
        } while (Reader.read_header());
    }
    Reader.set_state(Saved);
    GServerTimeBase = Time;
    g_TimingDelta.start();
    ::Reputation::FreeFeelings();
    ::Reputation::LoadFeelings(Reader);
    Server_ControllerDispatch(0, pub::Controller::EMessage::LoadGame, &Reader);
}

// .data:06d8f650 / 06d8f654 -- a TString<0x104> {length, chars}: the `path` value of the
// [Initial SP/MP DLLs] section, seeded empty by the static initializer 0x06d43410.
// pub::DLL_LoadLibrary (server_pub.cpp) reads it as the directory for every relative
// controller DLL name, so it keeps its value for the rest of the session.
unsigned int GDllPathLength = 0;
char GDllPath[0x104] = {0};

// FUN_06d43840 (cdecl). Startup passes the INI path. The priority word is looked up in
// the 5-row table at .data:06d8d080 (char[16] name, int value). No match leaves the
// priority holding whatever it held before: the previous row's priority, or
// uninitialised stack for the first row; the port starts it at 0 (HIGHEST).
static void Server_LoadInitialDlls(const char* IniPath) {
    struct FPriorityRow { const char* Name; unsigned int Value; };
    static const FPriorityRow PriorityTable[5] = {
        { "HIGHEST", 0 }, { "ABOVE_NORMAL", 1 }, { "NORMAL", 2 },
        { "BELOW_NORMAL", 3 }, { "LOWEST", 4 },
    };
    // FUN_06d03cf0: copy min(strlen, Size-1) chars, terminate, return the length. Its
    // null arm is inlined by every caller here.
    struct FBounded {
        static unsigned int Copy(char* Dst, unsigned int Size, const char* Src) {
            unsigned int Len = static_cast<unsigned int>(strlen(Src));
            if (!(Len < Size)) Len = Size - 1;
            memcpy(Dst, Src, Len);
            Dst[Len] = '\0';
            return Len;
        }
    };

    INI_Reader Reader;
    unsigned int Priority = 0;
    if (Reader.open(IniPath, false) && Reader.read_header()) {
        do {
            RefreshLoadingProgress();
            if (((Reader.is_header("Initial SP DLLs") && SinglePlayer()) ||
                 (Reader.is_header("Initial MP DLLs") && !SinglePlayer())) &&
                Reader.read_value()) {
                do {
                    if (Reader.is_value("path")) {
                        const char* PathValue = Reader.get_value_string();
                        if (PathValue == nullptr) {
                            GDllPathLength = 0;
                            GDllPath[0] = '\0';
                        } else {
                            GDllPathLength = FBounded::Copy(GDllPath, sizeof(GDllPath), PathValue);
                        }
                    } else if (Reader.is_value("DLL")) {
                        char DllName[0x104];
                        char InterfaceName[0x104];
                        char PriorityName[0x20];
                        const char* Value = Reader.get_value_string(0);
                        DllName[0] = '\0';
                        if (Value != nullptr) FBounded::Copy(DllName, sizeof(DllName), Value);
                        Value = Reader.get_value_string(1);
                        InterfaceName[0] = '\0';
                        if (Value != nullptr) FBounded::Copy(InterfaceName, sizeof(InterfaceName), Value);
                        Value = Reader.get_value_string(2);
                        PriorityName[0] = '\0';
                        if (Value != nullptr) FBounded::Copy(PriorityName, sizeof(PriorityName), Value);
                        for (unsigned int Row = 0; Row < 5; ++Row) {
                            if (_stricmp(PriorityTable[Row].Name, PriorityName) == 0) {
                                Priority = PriorityTable[Row].Value;
                                break;
                            }
                        }
                        Server_ControllerCreate(DllName, InterfaceName, nullptr, Priority);
                    }
                } while (Reader.read_value());
            }
        } while (Reader.read_header());
    }
}

// Controller / timer / physics infrastructure.

// Controller node, 0x20 bytes.
struct FControllerNode {
    FControllerNode* Prev;        // +0x00
    FControllerNode* Next;        // +0x04
    IDLL* InterfacePtr;           // +0x08  CreateInstance result (Content.dll's IDLL)
    HMODULE ModuleHandle;         // +0x0C
    float HeartbeatPeriod;        // +0x10  seconds between Heartbeat messages; < 0 = none
    float HeartbeatAccum;         // +0x14  time accumulated towards the next one
    unsigned int FLID;            // +0x18  unique controller ID
    unsigned int Priority;        // +0x1C  sort order
};
static_assert(sizeof(FControllerNode) == 0x20, "FControllerNode is 0x20 bytes");
static_assert(offsetof(FControllerNode, HeartbeatPeriod) == 0x10, "SetHeartbeatInterval stores the period at +0x10");

// Timer node of the FUN_06d5df60 timer list.
struct FTimerNode {
    FTimerNode* Prev;             // +0x00
    FTimerNode* Next;             // +0x04
    unsigned int TargetController; // +0x08  target controller FLID
    // +0x0c/+0x10 are pub::Controller::TimerExpired's two dwords, copied by pub::SetTimer
    // and handed back to the controller as the message payload when the timer fires.
    unsigned int Param1;          // +0x0C
    unsigned int Param2;          // +0x10
    float RemainingTime;          // +0x14  countdown
};

static FControllerNode g_ControllerListSentinel = { &g_ControllerListSentinel, &g_ControllerListSentinel, nullptr, nullptr, 0, 0, 0, 0 };
static FControllerNode g_DeferredListSentinel = { &g_DeferredListSentinel, &g_DeferredListSentinel, nullptr, nullptr, 0, 0, 0, 0 };
// Retail DAT_06d8f628/62c/630: a std::list of POINTERS to active-list nodes (0xc-byte
// list nodes, ctor 0x06d43560 / dtor 0x06d435a0). pub::Controller::Destroy only queues
// here; the heartbeat walk (FUN_06d43680) does the real teardown next tick.
static std::list<FControllerNode*> g_DestroyPending;
static unsigned int g_ControllerCount = 0;       // DAT_06d8f648
static unsigned int g_DeferredCount = 0;         // DAT_06d8f63c
static unsigned int g_ReentrancyGuard = 0;       // DAT_06d8f764

// Controller DLL entry points CreateInstance(Flid, Name, Parms) and
// DestroyInstance(Interface), both cdecl. Retail resolves them with GetProcAddress; the
// only controller DLL ever named is Content.dll, so the pointer types come from content.h.
typedef decltype(&::CreateInstance) FControllerCreateInstanceFn;
typedef decltype(&::DestroyInstance) FControllerDestroyInstanceFn;

// Timer list for FUN_06d5df60
static FTimerNode g_TimerListSentinel = { &g_TimerListSentinel, &g_TimerListSentinel, 0, 0, 0, 0.0f };
static unsigned int g_TimerCount = 0;            // DAT_06d9046c

// Retail 0x06d5df00 -- the timer list's destructor, registered through _onexit by the
// list's static initializer 0x06d5dec0. Retail's head is a 0x18-byte heap node that is
// freed and nulled; the port's head is the static sentinel, relinked to itself instead.
struct FTimerListTeardown {
    ~FTimerListTeardown() {
        FTimerNode* Cur = g_TimerListSentinel.Next;
        while (Cur != &g_TimerListSentinel) {
            FTimerNode* Node = Cur;
            Cur = Cur->Next;
            Node->Prev->Next = Node->Next;
            Node->Next->Prev = Node->Prev;
            free(Node);
            g_TimerCount--;
        }
        g_TimerListSentinel.Prev = &g_TimerListSentinel;
        g_TimerListSentinel.Next = &g_TimerListSentinel;
        g_TimerCount = 0;
    }
};
static FTimerListTeardown g_TimerListTeardown;


// Linked list helpers
static void ListInsertBefore(FControllerNode* sentinel, FControllerNode* node) {
    node->Next = sentinel;
    node->Prev = sentinel->Prev;
    sentinel->Prev->Next = node;
    sentinel->Prev = node;
}

static void ListRemove(FControllerNode* node) {
    node->Prev->Next = node->Next;
    node->Next->Prev = node->Prev;
    node->Prev = node;
    node->Next = node;
}

static void TimerListInsertBefore(FTimerNode* sentinel, FTimerNode* node) {
    node->Next = sentinel;
    node->Prev = sentinel->Prev;
    sentinel->Prev->Next = node;
    sentinel->Prev = node;
}

static void TimerListRemove(FTimerNode* node) {
    node->Prev->Next = node->Next;
    node->Next->Prev = node->Prev;
    node->Prev = node;
    node->Next = node;
}

// pub::Controller::_SendMessage's dispatch core -- retail 0x06d44080. FUN_06d43650
// (Server_SendMessage) only forwards with Flid 0. A zero (masked) Flid broadcasts to every
// controller under the re-entrancy guard (DAT_06d8f764), draining the deferred-create
// queue when it drops back to zero; otherwise the target is looked up (-2 when missing).
// The interface is Content.dll's IDLL (content.h); slot 0, IDLL::HandleMessage, is the
// only entry point Server.dll calls on it. Teardown goes through DestroyInstance, never
// slot 1 (the destructor).

static void Server_ControllerDrainDeferred();

// FUN_06d43600: the ACTIVE list (DAT_06d8f644), then the DEFERRED-create list
// (DAT_06d8f638), so a controller created during a broadcast is already addressable.
// Retail returns &node+8 (the record); the port returns the node.
static FControllerNode* Server_ControllerFind(unsigned int Flid) {
    FControllerNode* Node = g_ControllerListSentinel.Next;
    while (Node != &g_ControllerListSentinel) {
        if (Node->FLID == Flid) return Node;
        Node = Node->Next;
    }
    Node = g_DeferredListSentinel.Next;
    while (Node != &g_DeferredListSentinel) {
        if (Node->FLID == Flid) return Node;
        Node = Node->Next;
    }
    return nullptr;
}

// FUN_06d43820 -- is this Flid a live or pending controller. Sole caller:
// pub::SpaceObj::Create, passing &ShipInfo, whose first dword is therefore the Flid.
extern "C" bool __cdecl Server_ControllerExists(const unsigned int& Flid) {
    return Server_ControllerFind(Flid) != nullptr;
}

// Neither arm tests the record's interface pointer before calling it.
extern "C" int __cdecl Server_ControllerDispatch(unsigned int Flid, pub::Controller::EMessage MsgId,
                                                 const void* MsgData) {
    RefreshLoadingProgress();
    if ((Flid & 0x3fffffff) == 0) {
        g_ReentrancyGuard++;
        FControllerNode* cur = g_ControllerListSentinel.Next;
        while (cur != &g_ControllerListSentinel) {
            cur->InterfacePtr->HandleMessage(MsgId, MsgData);
            cur = cur->Next;
        }
        g_ReentrancyGuard--;
        if (g_ReentrancyGuard == 0)
            Server_ControllerDrainDeferred();
        return 0;
    }
    FControllerNode* node = Server_ControllerFind(Flid);
    if (node == nullptr) return -2;
    node->InterfacePtr->HandleMessage(MsgId, MsgData);
    return 0;
}

// pub::Controller::SetHeartbeatInterval's body -- retail 0x06d44030: node+0x10 = Interval
// (the period, also the gate: the heartbeat walk FUN_06d43680 runs it when >= 0.0), node+0x14
// = rand() / 32768 * Interval, so freshly configured controllers do not all fire on the
// same frame; -2 when not found.
extern "C" int __cdecl Server_ControllerSetHeartbeat(unsigned int Flid, float Interval) {
    FControllerNode* node = Server_ControllerFind(Flid);
    if (node == nullptr) return -2;
    node->HeartbeatPeriod = Interval;
    node->HeartbeatAccum = (float)rand() * (1.0f / 32768.0f) * Interval;
    return 0;
}

// pub::SetTimer's body -- retail 0x06d5ee00: a 0x18-byte node pushed at the END of the
// timer list. Retail checks ONLY Timeout < 0 (-4); neither the Flid nor the allocation is
// checked.
extern "C" int __cdecl Server_ControllerSetTimer(unsigned int Flid, unsigned int Param1,
                                                 unsigned int Param2, float Timeout) {
    if (Timeout < 0.0f) return -4;
    FTimerNode* node = (FTimerNode*)malloc(sizeof(FTimerNode));
    node->TargetController = Flid;
    node->Param1 = Param1;
    node->Param2 = Param2;
    node->RemainingTime = Timeout;
    TimerListInsertBefore(&g_TimerListSentinel, node);
    g_TimerCount++;
    return 0;
}

// pub::Controller::Create -- retail 0x06d43ce0 (ordinal 85), cdecl. A failed
// DLL_LoadLibrary FDUMPs and carries on. During a broadcast the record goes to the
// DEFERRED list, else into the ACTIVE list before the first node with priority >= this one
// (signed). GetModuleFileNameA's result is unused. Neither GetProcAddress result is tested
// before the call; a null instance is destroyed, unloaded and unlinked, returning 0.
unsigned int Server_ControllerCreate(const char* dll_path, const char* instance_name,
                                             void* parms, unsigned int priority) {
    unsigned int flid = CFLIDMaker::GetInstance()->AllocateObjectID();
    if (flid == 0) return 0;

    HMODULE hMod = pub::DLL_LoadLibrary(dll_path);
    if (hMod == nullptr) {
        FDUMP(0x100001, "%s(%d) : *** ERROR: failed create DLL instance '%s' (make sure file exists)\n",
              "E:\\FL\\Scratch\\Source\\Server\\dlls.cpp", 0xa7, dll_path);
    }

    FControllerNode* node = (FControllerNode*)malloc(sizeof(FControllerNode));
    node->InterfacePtr = nullptr;
    node->ModuleHandle = hMod;
    node->HeartbeatAccum = 0.0f;
    node->HeartbeatPeriod = -1.0f;
    node->FLID = flid;
    node->Priority = priority;

    const bool bDeferred = (g_ReentrancyGuard != 0);
    if (bDeferred) {
        ListInsertBefore(&g_DeferredListSentinel, node);
        g_DeferredCount++;
    } else {
        FControllerNode* cur = g_ControllerListSentinel.Next;
        while (cur != &g_ControllerListSentinel) {
            if (static_cast<int>(cur->Priority) >= static_cast<int>(priority)) break;
            cur = cur->Next;
        }
        ListInsertBefore(cur, node);
        g_ControllerCount++;
    }

    RefreshLoadingProgress();
    char ModuleName[0x104];
    GetModuleFileNameA(node->ModuleHandle, ModuleName, 0x104);
    FControllerCreateInstanceFn CreateInstance =
        (FControllerCreateInstanceFn)GetProcAddress(node->ModuleHandle, "CreateInstance");
    node->InterfacePtr = CreateInstance(flid, instance_name, parms);
    if (node->InterfacePtr != nullptr) return flid;

    FControllerDestroyInstanceFn DestroyInstance =
        (FControllerDestroyInstanceFn)GetProcAddress(node->ModuleHandle, "DestroyInstance");
    DestroyInstance(node->InterfacePtr);
    GetModuleFileNameA(node->ModuleHandle, ModuleName, 0x104);
    FreeLibrary(node->ModuleHandle);
    ListRemove(node);
    if (bDeferred) g_DeferredCount--;
    else g_ControllerCount--;
    free(node);
    return 0;
}

// pub::Controller::Destroy -- retail 0x06d43f70 (ordinal 103), cdecl. Queues every ACTIVE
// node with that Flid and broadcasts 0x0c. Nothing is unlinked or unloaded here -- that
// happens at the top of the next heartbeat walk (FUN_06d43680), so a broadcast in progress
// never sees a freed node. The deferred-create list is not searched.
void Server_ControllerDestroy(unsigned int flid) {
    for (std::list<FControllerNode*>::iterator It = g_DestroyPending.begin();
         It != g_DestroyPending.end(); ++It) {
        if ((*It)->FLID == flid) return;
    }
    FControllerNode* cur = g_ControllerListSentinel.Next;
    while (cur != &g_ControllerListSentinel) {
        if (cur->FLID == flid) {
            g_DestroyPending.push_back(cur);
            unsigned int DestroyedFlid = cur->FLID;
            Server_ControllerDispatch(0, pub::Controller::EMessage::ControllerDestroyed, &DestroyedFlid);
        }
        cur = cur->Next;
    }
}

// SysDB -- per-system state. The table is the exported SysDB::SysMap (server_sysdb.h /
// server_classes.cpp).
static_assert(sizeof(FStarSystem) == 0x100, "FStarSystem must stay 0x100 — SysDB memset/map");

// FUN_06d00280 -- look up a system in SysMap by id.
FStarSystem* SysDB_LookupSystem(unsigned int system_id) {
    if ((system_id & 0x3fffffff) == 0) return nullptr;
    SysDB::SysMapType::iterator it = SysDB::SysMap.find(system_id);
    if (it != SysDB::SysMap.end()) return &it->second;
    return nullptr;
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

// Mark system as loaded/active
void SysDB_SetSystemLoaded(unsigned int system_id, bool loaded) {
    FStarSystem* sys = SysDB_LookupSystem(system_id);
    if (sys) sys->bLoaded = loaded ? 1 : 0;
}

// DAT_06d88a80 -- see server_sysdb.h. Initial value 1 (.data:06d88a80).
unsigned char GbAllowPvp = 1;

// FUN_06d006c0's one-entry lookup cache -- see server_sysdb.h. LAB_06cfff90 / FUN_06cfffb0
// are the watcher's CRT constructor and atexit destructor.
BaseWatcher GLookupCacheObject;           // DAT_06d8d8ec
StarSystem* GLookupCacheSystem = nullptr; // DAT_06d8d92c
unsigned int GLookupCacheHits = 0;        // DAT_06d8d930
unsigned int GLookupCacheMisses = 0;      // DAT_06d8d934

// FUN_06cfffd0 (cdecl), called only from IServerImpl::Startup. SysDB_Initialize never reads
// the argument retail pushes; FUN_06cfd4b0 is the fuse DB load.
static void Server_PlayerSubsystemInit(unsigned int max_players, unsigned int allow_pvp) {
    (void)max_players;
    GbAllowPvp = static_cast<unsigned char>(allow_pvp);
    SysDB_Initialize();
    ServerFuseDB::LoadAll();
}

// FUN_06cffff0, called only by IServerImpl::Shutdown. Only the StarSystem half of the
// lookup cache is cleared; the watcher half is left as it is.
static void Server_CleanupPhysics() {
    ServerFuseDB::Clear();
    SysDB_Shutdown();
    GLookupCacheSystem = nullptr;
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
static void StarSystem_SendChangedAffiliations(StarSystem* Sys)
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
        unsigned int Affiliation = 0xffffffffu;
        ::Reputation::Vibe::GetAffiliation(Vibe, Affiliation, false);

        if (Ship != 0)
        {
            GameObject* const Obj = Server_LookupGameObject(Ship);
            SystemPlayerNode* const Head = static_cast<SystemPlayerNode*>(Sys->PlayerListHead);
            for (SystemPlayerNode* Node = Head->Next; Node != Head; Node = Node->Next)
            {
                if (SystemPlayerView_IsObjectVisible(&Node->View, Obj))
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
static void StarSystem_UpdateNearShips(StarSystem* Sys, float Step)
{
    void* const Bucket = Sys->GetBucket(0xfdffu);
    if (Bucket == nullptr)
        return;

    struct FScanList {
        unsigned int Capacity;
        unsigned int Count;
        void* Items[0x800];
    };
    struct FScanSphere {
        Vector Center;
        float Radius;
    };
    FScanList Scan;                     // on retail's 0x20a0-byte frame
    Scan.Capacity = 0x800;
    Scan.Count = 0;

    // ISpatialPartition +0x1c (sphere query); FScanSphere / FScanList have the layouts of
    // Geometry::Sphere and FSpatialQueryResult.
    ISpatialPartition* const Partition = static_cast<ISpatialPartition*>(Bucket);

    for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(Sys->PlayerListHead)->Next;
         Node != static_cast<SystemPlayerNode*>(Sys->PlayerListHead);
         Node = Node->Next)
    {
        unsigned int const ShipId = Players[Node->View.PlayerId].ship_object_id;
        GameObject* const Ship = Server_LookupGameObject(ShipId);
        if (Ship == nullptr)
            continue;
        FScanSphere Sphere;
        Sphere.Radius = 10000.0f;       // DAT_06d88af0
        Sphere.Center = Ship->get_position();
        Partition->QuerySphere(reinterpret_cast<Geometry::Sphere const*>(&Sphere),
                               reinterpret_cast<FSpatialQueryResult*>(&Scan));
    }

    FSystemListNode* It = nullptr;
    while (Sys->DockingSolars.traverse(It, true))
    {
        if (It->Data == nullptr)
            continue;
        if (Scan.Count < Scan.Capacity)
        {
            Scan.Items[Scan.Count] = static_cast<IObjRW*>(It->Data);
            ++Scan.Count;
        }
    }

    if (Scan.Count > 1)
    {
        int const Count = static_cast<int>(Scan.Count);
        int Kept = 0;
        for (int Index = 0; Index < Count; ++Index)
        {
            void* const Item = Scan.Items[Index];
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
            dynamic_cast<GameObject*>(static_cast<IObjRW*>(Scan.Items[Index]));
        if (Obj != nullptr)
            Obj->update(Step);
    }
}

// FUN_06ce3360 (thiscall on the StarSystem; the name is the port's). One side of a PhySys
// collision -> the GameObject wrapper and the sub-object it was hit on. Only caller:
// StarSystem_ProcessCollisions.
static void StarSystem_ResolveCollisionSide(StarSystem* Sys, CObject* Part, unsigned int PartId,
                                            GameObject** OutObj, unsigned short* OutSubObjId)
{
    *OutObj = nullptr;
    *OutSubObjId = SUBOBJ_ID_NONE;

    // The wrapper of the part's root object.
    GameObject* Found = nullptr;
    CObject* const Root = GetRoot(Part);
    if (Root != nullptr)
    {
        unsigned int const Class = static_cast<unsigned int>(Root->ObjectClass);
        if ((Class & 3u) == 3u)
        {
            if ((Class & 0x67u) != 0x67u)
            {
                Found = Sys->find_object(static_cast<CSimple*>(Root)->Id);   // FUN_06d0c840
            }
            else
            {
                // A beam: node->Data read without a null test, the first whose cobject()
                // is the collided part itself (not the root).
                FSystemListNode* Node = nullptr;
                while (Sys->Beams.traverse(Node, true))
                {
                    GameObject* const Data = Node->Data;
                    if (Data->cobject() == Part)
                    {
                        Found = Data;
                        break;
                    }
                }
            }
        }
    }

    *OutObj = Found;
    if (Found == nullptr)
        return;

    // Anything but a CEqObj was hit on its root.
    CObject* const Obj = Found->Object;
    if (Obj == nullptr || (static_cast<unsigned int>(Obj->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
    {
        *OutSubObjId = SUBOBJ_ID_ROOT;
        return;
    }

    // CObject::part_to_inst inlined on the PART; -1 falls back to its own engine instance.
    long Inst = Part->part_to_inst(PartId);
    if (Inst == -1)
        Inst = Part->EngineInstanceId;

    CEqObj* const Eq = static_cast<CEqObj*>(Obj);
    unsigned short const SubObjId = Eq->inst_to_subobj_id(Inst);
    *OutSubObjId = SubObjId;

    if (SubObjectID::IsShieldEquipID(SubObjId))
        return;

    // A functioning shield takes the hit instead.
    CEShield* const Shield = CEShield::cast(Eq->EquipManager.FindFirst(0x100));
    if (Shield == nullptr)
        return;
    if (!Shield->IsFunctioning())
        return;
    *OutSubObjId = Shield->Id;
}

// FUN_06ce34d0 (thiscall on the StarSystem; the name is the port's). The system's collision
// pass: PhySys::GenerateCollisions once per beam, then every event PhySys::GetCollisions
// returns for this system is resolved on both sides and delivered to both wrappers'
// on_collision. Only caller: the multiplayer tail of StarSystem_Update.
static void StarSystem_ProcessCollisions(StarSystem* Sys)
{
    // A node that does not make a CBeam passes null.
    FSystemListNode* Node = nullptr;
    while (Sys->Beams.traverse(Node, true))
    {
        CBeam* Beam = nullptr;
        GameObject* const Data = Node->Data;
        if (Data != nullptr)
        {
            CObject* const BeamObject = Data->Object;
            if (BeamObject != nullptr &&
                (static_cast<unsigned int>(BeamObject->ObjectClass) & CObject::ClassBeam) == CObject::ClassBeam)
                Beam = static_cast<CBeam*>(BeamObject);
        }
        PhySys::GenerateCollisions(Beam);
    }

    PhySys::CollisionEvent const* Begin = nullptr;
    PhySys::CollisionEvent const* End = nullptr;
    PhySys::GetCollisions(Sys->SystemInfo->NicknameId, Begin, End);

    // Retail takes the next element before the dispatch and compares the end after it.
    for (PhySys::CollisionEvent const* Event = Begin; Event != End; ++Event)
    {
        GameObject* ObjA = nullptr;
        unsigned short SubA = 0;
        StarSystem_ResolveCollisionSide(Sys, Event->ObjectA, Event->PartIdA, &ObjA, &SubA);
        if (ObjA == nullptr)
            continue;

        GameObject* ObjB = nullptr;
        unsigned short SubB = 0;
        StarSystem_ResolveCollisionSide(Sys, Event->ObjectB, Event->PartIdB, &ObjB, &SubB);
        if (ObjB == nullptr)
            continue;

        // The two pad words are never written by retail (stack contents); the port stores 0.
        FCollisionEvent HitA;
        HitA.SubObjId = SubA;
        HitA.Pad02 = 0;
        HitA.Other = ObjB;
        HitA.OtherSubObjId = SubB;
        HitA.Pad0a = 0;
        HitA.Point = Event->Point;
        HitA.SurfaceNormal = Event->SurfaceNormal;
        HitA.RelativeVelocity = Event->RelativeVelocity;
        ObjA->on_collision(HitA);

        // Side B: the partners swapped, the point shared, both vectors negated.
        FCollisionEvent HitB;
        HitB.SubObjId = SubB;
        HitB.Pad02 = 0;
        HitB.Other = ObjA;
        HitB.OtherSubObjId = SubA;
        HitB.Pad0a = 0;
        HitB.Point = Event->Point;
        HitB.SurfaceNormal.x = -Event->SurfaceNormal.x;
        HitB.SurfaceNormal.y = -Event->SurfaceNormal.y;
        HitB.SurfaceNormal.z = -Event->SurfaceNormal.z;
        HitB.RelativeVelocity.x = -Event->RelativeVelocity.x;
        HitB.RelativeVelocity.y = -Event->RelativeVelocity.y;
        HitB.RelativeVelocity.z = -Event->RelativeVelocity.z;
        ObjB->on_collision(HitB);
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
static unsigned int StarSystem_Update(StarSystem* Sys, float Dt)
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
            ObjectList* const Deferred = &Sys->Deferred;
            StarSystem_TickObjectList(&Sys->Ships, CurStep, Deferred);
            StarSystem_TickObjectList(&Sys->GuidedMunitions, CurStep, Deferred);

            StarSystem_UpdateNearShips(Sys, CurStep);

            StarSystem_TickObjectList(&Sys->Loot, CurStep, Deferred);
            StarSystem_TickObjectList(&Sys->Beams, CurStep, Deferred);
            StarSystem_TickObjectList(&Sys->Mines, CurStep, Deferred);
            StarSystem_TickObjectList(&Sys->CounterMeasures, CurStep, Deferred);

            // COMMON.DLL::CmnAsteroid::Update (cdecl): StarSystem is the IObjDB it walks.
            CmnAsteroid::Update(Sys, CurStep);

            // FUN_06d03480: runs FUN_06d04480 on each field of this system's entry in the
            // SrvAsteroid::SrvAsteroidSystem map at DAT_06d8d980 (server_classes.cpp).
            SrvAsteroid::SrvAsteroid_UpdateSystem(Sys);

            StarSystem_TickObjectList(&Sys->MedAsteroids, CurStep, Deferred);

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
            if (Sys->DockQueueSize != 0)
            {
                // The players whose ships land this step.
                std::list<unsigned int> Landed;

                // The end test re-reads the head every pass.
                SystemDockNode* Node = static_cast<SystemDockNode*>(Sys->DockQueueHead)->Next;
                while (Node != static_cast<SystemDockNode*>(Sys->DockQueueHead))
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
                        SystemPlayerNode* View = static_cast<SystemPlayerNode*>(Sys->PlayerListHead)->Next;
                        while (View != static_cast<SystemPlayerNode*>(Sys->PlayerListHead))
                        {
                            unsigned int ObjId = Obj->get_id();
                            SystemPlayerView_OnObjectLanded(&View->View, &ObjId, &TargetId,
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
                            Obj->destroy(0, 0);
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
                    Sys->remove_player(PlayerId);
                    PlayerData_FlushPendingVisits(Players[PlayerId]);
                    if (SinglePlayer())
                        Sys->shutdown();
                }

                Landed.clear();
                SystemDockNode* const DockHead = static_cast<SystemDockNode*>(Sys->DockQueueHead);
                Sys->erase_docks(DockHead->Next, DockHead);   // FUN_06d0f410
            }

            // Multiplayer only, and only while the system has players. SinglePlayer() is
            // called again here, not reused from the clamp.
            if (!SinglePlayer() && Sys->PlayerListSize != 0)
            {
                // CBeam::move is called even when the node does not narrow to a CBeam --
                // retail has no test, so every object on this block is a beam there.
                FSystemListNode* BeamNode = nullptr;
                while (Sys->Beams.traverse(BeamNode, true))
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

                PhySys::Update(Sys->SystemInfo->NicknameId, CurStep);

                StarSystem_ProcessCollisions(Sys);
            }

            if (Target - Accum < static_cast<double>(CurStep))
                CurStep = static_cast<float>(Target - Accum);
        } while (Accum < Target);
    }

    // After the step loop (or the skip): every player view gets FUN_06cf53e0 with the
    // original dt as a double.
    if (Sys->PlayerListSize != 0)
    {
        for (SystemPlayerNode* Node = static_cast<SystemPlayerNode*>(Sys->PlayerListHead)->Next;
             Node != static_cast<SystemPlayerNode*>(Sys->PlayerListHead); Node = Node->Next)
        {
            SystemPlayerView_Update(&Node->View, Target);
        }
    }

    StarSystem_SendChangedAffiliations(Sys);

    return Iterations;
}

// FUN_06d10320 (cdecl) -- the per-frame walk. Outside single player AnimDB::Update runs
// first. A function-local static vector (DAT_06d8da18) remembers the systems already
// stepped this frame; every active player's system is stepped once, with no loaded test
// and no cap, as in retail (retail's redundant third find has no effect). The vector is
// emptied, its storage kept.
static void Server_StepSystems(float dt) {
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
                StarSystem_Update(&It->second, dt);
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

// FUN_06d001d0 (cdecl), called only by ElapseTime: step the systems, pump the phantom
// events (FUN_06cffdc0), then controller event 0x25 for every dock-exited ship (DAT_06d8d8e4)
// and empty that list. The system id is read BEFORE get_id.
static void Server_PhysicsUpdate(float dt) {
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

// FUN_06d43c00, called by Shutdown: sort the active list by Flid (FUN_06d441c0, unsigned,
// stable), Destroy every controller from the LAST node back, then one heartbeat walk with
// 0.0 tears down everything Destroy just queued. The deferred-create list is left alone.
static void Server_ControllerCleanup() {
    // FUN_06d441c0: stable ascending sort by Flid. A stable insertion into a fresh
    // chain gives the same order as the retail merge sort.
    if (g_ControllerListSentinel.Next != &g_ControllerListSentinel) {
        FControllerNode* Pending = g_ControllerListSentinel.Next;
        g_ControllerListSentinel.Prev->Next = nullptr;
        g_ControllerListSentinel.Next = &g_ControllerListSentinel;
        g_ControllerListSentinel.Prev = &g_ControllerListSentinel;
        while (Pending != nullptr) {
            FControllerNode* NextPending = Pending->Next;
            FControllerNode* Before = g_ControllerListSentinel.Prev;
            while (Before != &g_ControllerListSentinel && Pending->FLID < Before->FLID)
                Before = Before->Prev;
            ListInsertBefore(Before->Next, Pending);
            Pending = NextPending;
        }
    }

    FControllerNode* cur = &g_ControllerListSentinel;
    if (cur != cur->Next) {
        do {
            Server_ControllerDestroy(cur->Prev->FLID);
            cur = cur->Prev;
        } while (cur != g_ControllerListSentinel.Next);
    }
    Server_TimerTick(0.0f);
}

// FUN_06d43680 (cdecl): the controller heartbeat walk, called every ElapseTime and once by
// the cleanup. First the queued Destroys are torn down (GetModuleFileNameA's result
// unused), then, under the re-entrancy guard, every node with a period >= 0 accumulates dt
// and gets 0x20 Heartbeat with the whole periods elapsed.
static void Server_TimerTick(float dt) {
    for (std::list<FControllerNode*>::iterator It = g_DestroyPending.begin();
         It != g_DestroyPending.end(); ++It) {
        FControllerNode* Node = *It;
        FControllerDestroyInstanceFn DestroyInstance =
            (FControllerDestroyInstanceFn)GetProcAddress(Node->ModuleHandle, "DestroyInstance");
        DestroyInstance(Node->InterfacePtr);
        char ModuleName[0x104];
        GetModuleFileNameA(Node->ModuleHandle, ModuleName, 0x104);
        FreeLibrary(Node->ModuleHandle);
        ListRemove(Node);
        free(Node);
        g_ControllerCount--;
    }
    g_DestroyPending.clear();

    g_ReentrancyGuard++;
    FControllerNode* cur = g_ControllerListSentinel.Next;
    while (cur != &g_ControllerListSentinel) {
        if (cur->HeartbeatPeriod >= 0.0f) {
            // x87: the compare and the divide use the unrounded sum; only the store is
            // rounded.
            const double Sum = static_cast<double>(dt) + static_cast<double>(cur->HeartbeatAccum);
            cur->HeartbeatAccum = static_cast<float>(Sum);
            const double Period = static_cast<double>(cur->HeartbeatPeriod);
            if (Sum >= Period) {
                float WrapTime = static_cast<float>(floor(Sum / Period) * Period);
                cur->InterfacePtr->HandleMessage(pub::Controller::EMessage::Heartbeat, &WrapTime);
                cur->HeartbeatAccum = cur->HeartbeatAccum - WrapTime;
            }
        }
        cur = cur->Next;
    }
    g_ReentrancyGuard--;
    if (g_ReentrancyGuard == 0)
        Server_ControllerDrainDeferred();
}

// Deferred-create drain, shared by the heartbeat walk and by the broadcast arm of
// Server_ControllerDispatch (retail inlines it in both places).
static void Server_ControllerDrainDeferred() {
    FControllerNode* cur = g_DeferredListSentinel.Next;
    while (cur != &g_DeferredListSentinel) {
        FControllerNode* next = cur->Next;
        ListRemove(cur);
        g_DeferredCount--;
        FControllerNode* insert_cur = g_ControllerListSentinel.Next;
        while (insert_cur != &g_ControllerListSentinel) {
            if (cur->Priority <= insert_cur->Priority) break;
            insert_cur = insert_cur->Next;
        }
        cur->Next = insert_cur;
        cur->Prev = insert_cur->Prev;
        insert_cur->Prev->Next = cur;
        insert_cur->Prev = cur;
        g_ControllerCount++;
        cur = next;
    }
}

// Server_FormatTString256 -- retail FUN_06d5f040 (cdecl varargs). Only caller: the timer
// firing below. The vsprintf into the local buffer is unbounded, as in retail; the
// TString<256> assignment is inlined.
static void Server_FormatTString256(TString<256>* Out, char const* Fmt, ...) {
    char Local[0x100];
    va_list Args;
    va_start(Args, Fmt);
    vsprintf(Local, Fmt, Args);
    va_end(Args);
    unsigned int Len = static_cast<unsigned int>(strlen(Local));
    if (Len >= 0x100)
        Len = 0xff;
    memcpy(Out->Data, Local, Len);
    Out->Data[Len] = '\0';
    Out->Length = Len;
}

// FUN_06d5df60 -- timer firing.
static void Server_TimerFiring(float dt) {
    FTimerNode* expired[128];
    unsigned int expired_count = 0;

    // Every timer loses dt; while fewer than 0x80 are collected, one whose remaining time
    // is below 0.0 is collected.
    FTimerNode* cur = g_TimerListSentinel.Next;
    while (cur != &g_TimerListSentinel) {
        cur->RemainingTime -= dt;
        if (expired_count < 128 && cur->RemainingTime < 0.0f) {
            expired[expired_count] = cur;
            expired_count++;
            // Filling the 128th entry warns once (retail's text says 64).
            if (expired_count == 128) {
                TString<256> Text;
                Text.Length = 0;
                Text.Data[0] = '\0';
                Server_FormatTString256(&Text,
                    "%x timers are trying to fire on the same frame.  Only 64 will fire now.  "
                    "The next ones will fire on the next frame.",
                    g_TimerCount);
                FDUMP(0x100002, "%s(%d) : *** WARNING: %s",
                      "E:\\FL\\Scratch\\Source\\Server\\pubMisc.cpp", 0x3c, Text.Data);
            }
        }
        cur = cur->Next;
    }

    for (unsigned int i = 0; i < expired_count; i++) {
        FTimerNode* timer = expired[i];
        // Delivered to the timer's own controller, not broadcast.
        Server_ControllerDispatch(timer->TargetController, pub::Controller::EMessage::TimerExpired,
                                  &timer->Param1);
        TimerListRemove(timer);
        g_TimerCount--;
        free(timer);
    }
}

// `Server` -- retail export ordinal 524 (.data:06d8facc), UNDECORATED, so it is an
// extern "C" data symbol and cannot carry the G prefix. The one twelve-byte IServerImpl
// object, built by the inlined default ctor LAB_06d44740. FLServer.exe and
// RemoteClient.dll import it by name and call members on it directly; inside Server.dll
// retail calls through the global directly, the port's other TUs through
// GetServerInterface()/GetChatServerInterface() (same dynamic type, same member).
// Declared in src/include/server_iserverimpl.h.
extern "C" SERVER_API IServerImpl Server;
IServerImpl Server;

extern "C" {
    // 0x06d44760
    SERVER_API IServer* __cdecl GetServerInterface() { return &Server; }
    // 0x06d44770 -- Server + 4, the IChatServer subobject (second base).
    SERVER_API void* __cdecl GetChatServerInterface() {
        return static_cast<IChatServer*>(&Server);
    }
}

static HMODULE g_hInstance = nullptr;

BOOL WINAPI DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) { g_hInstance = hModule; DisableThreadLibraryCalls(hModule); }
    return TRUE;
}
