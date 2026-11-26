// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/server_iserverimpl.h"   // IServerImpl, Server
#include "../include/server_gameobject.h"
#include "../include/common_dataconfig.h" // DataConfig::startup / shutdown
#include "../include/common_timing.h" // Timing::Delta
#include "../include/common_id.h"
#include "../include/common_ini.h"
#include "../include/common_goods.h"
#include "../include/common_universe.h"
#include "../include/common_server_playerdb.h"
#include "../include/server_characterset.h" // CCharacterSet (CharacterInfoReq)
#include "../include/common_misc.h"      // safe_strcpy (CharacterSelect key)
#include "../include/common_server_controller.h"
#include "../include/server_sysdb.h"
#include "../include/server_basedb.h"
#include "../include/rpc_iclient.h"
#include "../include/rpc_iclient_args.h" // FServerSetReputationInfo / FServerCharSelectVerifiedInfo
#include "../include/DALib.h"           // DALib::Data (FUN_06d44ca0)
#include "../include/readfile.h"        // IFileSystem::GetFileSysPath (+0x84)
#include "../include/common_physys.h"
#include "../include/common_casteroid.h"   // CmnAsteroid::Update
#include "../include/common_animdb.h"     // AnimDB::Update (FUN_06d10320)
#include "server_internal.h"
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <list>
#include <cstring>

#pragma comment(lib, "winmm.lib")

// Retail exports IClient's default ctor, copy ctor and operator= (rpclocal ships
// ??0IClient@@QAE@XZ / ??4IClient@@QAEAAV0@ABV0@@Z beside ??_7IClient@@6B@; Server.dll
// re-exports all three from server_classes.cpp). The class is abstract and has no data, so
// all three are trivial.
IClient::IClient() {}
IClient::IClient(const IClient& Other) { (void)Other; }
IClient& IClient::operator=(const IClient& Other) { (void)Other; return *this; }

// .data:06d8fb14 -- accessed by SendGroupID/SendSystemID in server_playerdb.cpp
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
// it; Login bumps +0x04 / +0x0c, PlayerDB::logout +0x08 (server_playerdb.cpp),
// AbortMission +0x1c.
ServerStats GServerStats = {};
// .data:DAT_06d8f9c8 -- [Freelancer] initial_world. Read also by
// PlayerDB::create_restart_file, hence external linkage.
char g_InitialWorldPath[260] = {0};

static void Server_ReadWorldIni(const char* config_path);
static void Server_LoadWorldState();
unsigned int Server_ControllerCreate(const char* dll_path, const char* instance_name, void* parms, unsigned int priority);

PlayerSlot* GetPlayerBySession(unsigned int session_id) {
    return PlayerDB_GetPlayer(session_id);
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
        if (result == kLoginAccepted) Server_ControllerDispatch(0, pub::Controller::EMessage::PlayerLogin, &session_id);
        // IClient slot 12, FLPACKET_SERVER_LOGINRESPONSE; the status byte travels by reference.
        g_pClientInterface->SendServerLoginResponse(session_id, result);
        // Retail reads back the stored login result here (Ghidra shows it as the high byte of
        // param_2).
        if (result == kLoginAccepted) {
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
            Players[session_id].build_character_set(Set);

        // IClient slot 13, FLPACKET_SERVER_CHARACTERINFO; no null test on the interface.
        g_pClientInterface->SendServerCharacterInfo(session_id, Set);
    }

void IServerImpl::CharacterSelect(const CHARACTER_ID& char_id, unsigned int session_id) {
        // Retail .text:06d453f0.
        if (!Players.is_valid(session_id)) return;
        PlayerData& Slot = Players[session_id];

        // The worker FUN_06d4a5d0 (server_playerdb.cpp); a miss or a failed load ends here.
        if (!Slot.select_character(char_id, session_id)) return;

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
        unsigned int Affiliation = kNoAffiliation;
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

        // FUN_06d49f60, result ignored (server_playerdb.cpp).
        player->destroy_character(char_id, session_id);

        CharacterInfoReq(session_id, false);
    }

void IServerImpl::CharacterSkipAutosave(unsigned int session_id) {
        // Retail .text:06d45380: PlayerDB::is_valid inlined, then FUN_06d4c5f0(slot).
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        p->skip_next_autosave();
    }

    // The in-base request family, .text:06d17300 .. 06d1761b. Every member but
    // ReqHullStatus gates on `SinglePlayer() || entered_base_id != 0` (+0x3F4, an UNMASKED
    // dword test, the base BaseEnter wrote) on a slot indexed RAW -- no PlayerDB::is_valid,
    // no client-id test.

    // ReqShipArch -- retail .text:06d17300; FUN_06d4ec10 is PlayerData::set_ship_arch.
void IServerImpl::ReqShipArch(unsigned int arch_id, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        Players[session_id].set_ship_arch(arch_id);
    }
    // ReqHullStatus -- retail .text:06d17340, the one member of the family that validates
    // the session first. The x87 clamp keeps NaN (only a true `< 0` is zeroed).
    // FUN_06d4ed10 is PlayerData::set_relative_health.
void IServerImpl::ReqHullStatus(float hull_status, unsigned int session_id) {
        if (!Players.is_valid(session_id)) return;
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        if ((Players[session_id].current_base & 0x3fffffff) == 0) return;
        if (hull_status < 0.0f) hull_status = 0.0f;
        Players[session_id].set_relative_health(hull_status, true);
    }
    // ReqCollisionGroups -- retail .text:06d173e0; FUN_06d4f320.
void IServerImpl::ReqCollisionGroups(const std::list<CollisionGroupDesc>& groups, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        Players[session_id].set_collision_groups(groups);
    }
    // ReqEquipment -- retail .text:06d17420; FUN_06d4eeb0.
void IServerImpl::ReqEquipment(const EquipDescList& equipment, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        Players[session_id].load_equipment(equipment);
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
        Players[session_id].add_equipment(arch_id, Hp, count, health, b_mounted,
                                          /*bMission*/false, /*bNotify*/true);
    }
    // ReqRemoveItem -- retail .text:06d174e0, tail call to FUN_06d4f100 with bNotify = 0:
    // no IClient slot 27, the client asked for the removal and already knows.
void IServerImpl::ReqRemoveItem(unsigned short equip_id, int count, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        Players[session_id].remove_equipment(equip_id, count, /*bNotify*/false);
    }
    // ReqModifyItem -- retail .text:06d17530; FUN_06d4f170 takes six stack dwords but reads
    // five, so the port drops the trailing literal 0.
void IServerImpl::ReqModifyItem(unsigned short equip_id, const char* hardpoint, int count, float health,
                       unsigned char b_mounted, unsigned int session_id) {
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        CacheString Hp;
        Hp.Value = StringAlloc(hardpoint, false);
        Players[session_id].modify_equipment(equip_id, Hp, count, health, b_mounted);
    }

void IServerImpl::ReqSetCash(int cash, unsigned int session_id) {
        // Retail .text:06d175a0: the in-base gate, then FUN_06d4f480 (PlayerData::set_cash).
        // Cash is an int (?ReqSetCash@...@@UAEXHI@Z).
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        Players[session_id].set_cash(cash);
    }

void IServerImpl::ReqChangeCash(int delta, unsigned int session_id) {
        // Retail .text:06d175e0: the in-base gate, then FUN_06d4f4e0(delta, false) --
        // PlayerData::adjust_cash without the +0x320 total.
        if (!SinglePlayer() && Players[session_id].entered_base_id == 0) return;
        Players[session_id].adjust_cash(delta, false);
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
            Players[session_id].reset_total_earned();
            Players[session_id].snapshot_equipment();
        }
    }

    // IServerImpl::BaseExit -- retail .text:06d177b0. Retail inlines LocationInfoRequest
    // and LocationExit (same order, same arguments); the port calls them. The base id is
    // compared raw against entered_base_id. FUN_06d4c430 is the autosave.
void IServerImpl::BaseExit(unsigned int base_id, unsigned int session_id) {
        if (!Players.is_valid(session_id)) return;
        if ((Players[session_id].entered_base_id & 0x3fffffff) == 0) {
            Players[session_id].send_client_null_packet();
            return;
        }
        if (base_id != Players[session_id].entered_base_id) {
            Players[session_id].send_client_null_packet();
            return;
        }
        if ((Players[session_id].current_location & 0x3fffffff) != 0) {
            LocationInfoRequest(Players[session_id].current_location, session_id, false);
            LocationExit(Players[session_id].current_location, session_id);
        }

        unsigned int ExitEvent[2] = { base_id, session_id };
        Server_ControllerDispatch(0, pub::Controller::EMessage::BaseExit, ExitEvent);
        GetBaseDB()->unload_base(session_id, base_id);   // FUN_06d142c0
        Players[session_id].auto_save();

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
            Players[session_id].send_client_null_packet();
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

void IServerImpl::SaveGame(const CHARACTER_ID& char_id, const wchar_t* description,
                  unsigned int session_id) {
        // Retail .text:06d459d0: SP only, a live session, and NOT in a ship (no in-base
        // exception), then a tail call to FUN_06d4c800(slot, char_id, description, 0).
        if (!SinglePlayer()) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        if ((p->ship_object_id & 0x3fffffff) != 0) return;
        p->save_game(char_id.Buffer, description, 0);
    }
    // MissionSaveB -- retail .text:06d45a30: SP only, a live session, then
    // FUN_06d4c600(slot, mission_id) -- the mission id IS the save's descrip_strid.
void IServerImpl::MissionSaveB(unsigned int session_id, unsigned long mission_id) {
        if (!SinglePlayer()) return;
        unsigned int max_players = PlayerDB_GetMaxPlayers();
        if (session_id == 0 || (session_id - 1) >= max_players) return;
        PlayerSlot* p = GetPlayerBySession(session_id);
        if (!p) return;
        p->mission_save(static_cast<unsigned int>(mission_id));
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

    // Slot 92: PushToServer -- a no-op in Server.dll (shared empty body 0x06ce1050).
    // IServerImpl's own virtual, not an IServer override (IServer's table,
    // .rdata:06d65d4c, stops at slot 91). The packet queue lives in rpclocal
    // (IServerImpl::PushToServer 0x06d8f460).
void IServerImpl::PushToServer(CDAPacket* /*packet*/) {}

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

// SysDB -- per-system state. The table is the exported SysDB::SysMap (server_sysdb.h /
// server_classes.cpp).
static_assert(sizeof(FStarSystem) == 0x100, "FStarSystem must stay 0x100 — SysDB memset/map");

// DAT_06d88a80 -- see server_sysdb.h. Initial value 1 (.data:06d88a80).
unsigned char GbAllowPvp = 1;

// FUN_06ce3360 (thiscall on the StarSystem; the name is the port's). One side of a PhySys
// collision -> the GameObject wrapper and the sub-object it was hit on. Only caller:
// StarSystem::process_collisions.
void StarSystem::resolve_collision_side(CObject* Part, unsigned int PartId, GameObject** OutObj,
                                        unsigned short* OutSubObjId)
{
    *OutObj = nullptr;
    *OutSubObjId = SUBOBJ_ID_NONE;

    // The wrapper of the part's root object.
    GameObject* Found = nullptr;
    CObject* const Root = GetRoot(Part);
    if (Root != nullptr)
    {
        unsigned int const Class = static_cast<unsigned int>(Root->ObjectClass);
        if ((Class & CObject::ClassSimple) == CObject::ClassSimple)
        {
            if ((Class & CObject::ClassBeam) != CObject::ClassBeam)
            {
                Found = find_object(static_cast<CSimple*>(Root)->Id);   // FUN_06d0c840
            }
            else
            {
                // A beam: node->Data read without a null test, the first whose cobject()
                // is the collided part itself (not the root).
                FSystemListNode* Node = nullptr;
                while (Beams.traverse(Node, true))
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
    CEShield* const Shield = CEShield::cast(Eq->EquipManager.FindFirst(EquipTypeShield));
    if (Shield == nullptr)
        return;
    if (!Shield->IsFunctioning())
        return;
    *OutSubObjId = Shield->Id;
}

// FUN_06ce34d0 (thiscall on the StarSystem; the name is the port's). The system's collision
// pass: PhySys::GenerateCollisions once per beam, then every event PhySys::GetCollisions
// returns for this system is resolved on both sides and delivered to both wrappers'
// on_collision. Only caller: the multiplayer tail of StarSystem::update.
void StarSystem::process_collisions()
{
    // A node that does not make a CBeam passes null.
    FSystemListNode* Node = nullptr;
    while (Beams.traverse(Node, true))
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
    PhySys::GetCollisions(SystemInfo->NicknameId, Begin, End);

    // Retail takes the next element before the dispatch and compares the end after it.
    for (PhySys::CollisionEvent const* Event = Begin; Event != End; ++Event)
    {
        GameObject* ObjA = nullptr;
        unsigned short SubA = 0;
        resolve_collision_side(Event->ObjectA, Event->PartIdA, &ObjA, &SubA);
        if (ObjA == nullptr)
            continue;

        GameObject* ObjB = nullptr;
        unsigned short SubB = 0;
        resolve_collision_side(Event->ObjectB, Event->PartIdB, &ObjB, &SubB);
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

// Server_FormatTString256 -- retail FUN_06d5f040 (cdecl varargs). Only caller: the timer
// firing below. The vsprintf into the local buffer is unbounded, as in retail; the
// TString<256> assignment is inlined.
void Server_FormatTString256(TString<256>* Out, char const* Fmt, ...) {
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
