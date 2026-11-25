// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// server_internal.h -- helpers one Server.dll source file defines and another calls. Not
// part of Server.dll's public headers (src/include), which FLServer.exe also includes.
#pragma once

#include "../include/common_server.h"

struct PlayerData;
struct CEqObj;
class GameObject;
class RenderDisplayList;
class RDLNode;

// FLChatServer.cpp (server_chat.cpp)
unsigned int MakeChatId(unsigned int Id);
// FUN_06d1b1b0's port (shared with the cheat report).
void Server_RdlAppend(RenderDisplayList& Rdl, const RDLNode& Node);

// The 8-byte block FUN_06d190d0 sends through IChatClient::SendCommonChatMsg: the message
// type and its ushort argument zero-extended.
struct FGroupMsgPayload {
    unsigned int MsgType;
    unsigned int Param;
};

// ServerSpace.cpp (server_serverspace.cpp)
void PlayerDB_LeaveSystem(PlayerData& Slot, unsigned int SessionId);

// server_classes.cpp
void PlayerDB_LeaveBase(PlayerData& Slot, unsigned int SessionId);

// PlayerDB.cpp (server_playerdb.cpp)
unsigned short AccountNameFileXorKey();

// EqObj.cpp (server_eqobj.cpp)
CEqObj* GameObject_ceqobj(GameObject* Obj);

// SrvMisc.cpp (server_srvmisc.cpp)
namespace pub {
namespace SpaceObj {
void SpaceObj_BuildTransportLoadout(Archetype::Ship const* Arch,
                                    SubObjectID::EquipIdMaker* IdMaker,
                                    OwnerList<CargoDesc> const* Cargo,
                                    EquipDescVector* Out);
} // namespace SpaceObj
} // namespace pub

// server_pub.cpp
void Server_SaveSinglePlayerState(ISave* Writer);

// The controller list (dlls.cpp, server_dlls.cpp) and pub::SetTimer's body (pubMisc.cpp,
// server_pubmisc.cpp). Server_ControllerDispatch, pub::Controller::_SendMessage's dispatch
// core, is declared in common_server_controller.h.
extern "C" int __cdecl Server_ControllerSetHeartbeat(unsigned int Flid, float Interval);
extern "C" int __cdecl Server_ControllerSetTimer(unsigned int Flid, unsigned int Param1,
                                                 unsigned int Param2, float Timeout);
// FUN_06d43820: is this FLID a live or pending controller.
extern "C" bool __cdecl Server_ControllerExists(const unsigned int& Flid);

// dlls.cpp (server_dlls.cpp): the client DLL and the controller list, driven by
// IServerImpl::Startup / Shutdown / Update.
IClient* Server_LoadClientDll(const char* module_path);
void Server_FreeClientDll();
IChatClient* Server_GetLoadedChatClientInterface();
IClient* Server_GetLoadedClientInterface();
void Server_LoadInitialDlls(const char* IniPath);
void Server_ControllerCleanup();
void Server_TimerTick(float dt);

// ServerSpace.cpp (server_serverspace.cpp)
void Server_PlayerSubsystemInit(unsigned int max_players, unsigned int allow_pvp);
void Server_CleanupPhysics();
void Server_PhysicsUpdate(float dt);

// StarSys.cpp (server_starsys.cpp)
void Server_StepSystems(float dt);

// pubMisc.cpp (server_pubmisc.cpp)
void Server_TimerFiring(float dt);

// IServerImpl.cpp
PlayerSlot* GetPlayerBySession(unsigned int session_id);
void Server_FormatTString256(TString<256>* Out, char const* Fmt, ...);
