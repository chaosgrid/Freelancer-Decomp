// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpclocal.cpp -- IServerImpl, the freelancer.exe-facing half of rpclocal.dll.
//
// Retail rpclocal is a bidirectional RPC proxy: freelancer.exe asks it for IServer,
// Server.dll asks it for IClient, and every call in either direction becomes a CDAPacket on
// one of three channels. The transport is in rpc_transport.{h,cpp}, the 162 packet types in
// rpc_packet_registry.cpp, the exported 126-slot IClient in rpc_client_proxy.cpp and
// HandlePacket in rpc_dispatch.cpp.
//
// All 81 serializing IServerImpl slots (those calling FUN_06d96300) build a packet and hand
// it to PushToServer; the pump in Update() drains the server-bound queue into Server.dll
// through RpcDispatchToServer. Startup, Shutdown, ElapseTime, Connect and DisConnect call
// Server.dll directly, as in retail (ElapseTime 0x06d8f370 is a tail jump into the real
// IServer).

#include "common_server.h"
#include "rpc_transport.h"      // FPacketBase, the registry and the queues
#include "rpc_payloads.h"       // GRealServer / GRealChatServer, read by RpcDispatchToServer
#include "rpc_iclient.h"        // the real 126-slot IClient / IChatClient
#include "rpc_iserver.h"        // IServerImpl, Server, Get*Interface -- the exported ABI
#include "common_misc.h"       // GiveServerRunPermission / WaitForClientRunPermission
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <new>

// ============================================================================
// Global state
// ============================================================================
static HMODULE g_hServerDll = nullptr;
static IServer* g_pServerInterface = nullptr;
static void* g_pChatServerInterface = nullptr;
// .data:0x06dbbd58 -- IServerImpl's own copy of the CDAConnection pointer, written only by
// Connect and cleared only by Shutdown; read by Update and PushToServer. See
// rpc_transport.h for the other two copies.
static FRpcConnection* GServerConnection = nullptr;

// DAT_06dbbccc: our own HMODULE, captured in DllMain(DLL_PROCESS_ATTACH).
// IServerImpl::Startup (0x06d8f1e0) needs it for GetModuleFileNameA.
static HMODULE g_hSelfModule = nullptr;

// CDAPacket queueing lives in rpc_transport.cpp: retail's CDAConnection queues packet
// pointers (std::deque<CDAPacket*>) and never copies packet bytes.

// ============================================================================
// The IServer-direction marshallers.
//
// Channel 3 is client -> server only, so its codec table (GChannel3Codecs) is used as-is.
// Channel 1 is bidirectional and its codecs come from the IClient senders; the types whose
// IServerImpl senders write a different payload get a marshaller here. The rest share the
// codec (e.g. ACTIVATEEQUIP 0x06d8e6c0 vs 0x06d8bbb0, GOTRADELANE 0x06d8e710 vs 0x06d8bcb0,
// SETTARGET 0x06d8ea30 vs 0x06d8bd50).
// ============================================================================

// Packet-owned blocks come from RpcCopyBlock (rpc_payloads.h). Where retail tests the source
// or the length first (CHATMSG), the caller carries that test.

// The argument arrays carry raw dwords, as retail's stack frame does, so a float argument
// travels as its bit pattern.
static unsigned int RpcFloatBits(float Value) {
    unsigned int Bits = 0;
    const unsigned char* Src = reinterpret_cast<const unsigned char*>(&Value);
    unsigned char* Dst = reinterpret_cast<unsigned char*>(&Bits);
    for (unsigned int i = 0; i < 4u; ++i) {
        Dst[i] = Src[i];
    }
    return Bits;
}

// ---------------------------------------------------------------------------
// ch1 0x01 FLPACKET_COMMON_UPDATEOBJECT -- sender IServerImpl::SPObjUpdate 0x06d8dc30.
//
// The IClient sender reads a 0x2c-byte struct with two floats at +0x20 / +0x24; this one
// reads SSPObjUpdateInfo (0x30 bytes) with a double at +0x20 that it narrows to float:
//   SSPObjUpdateInfo +0x2c (byte)    -> payload+0x00
//   +0x00                            -> payload+0x04
//   +0x14..+0x1c (FUN_06d8f510)      -> payload+0x08
//   +0x04..+0x10 (FUN_06d8f4f0)      -> payload+0x14
//   +0x20 double, FLD/FSTP to float  -> payload+0x24
//   +0x28 dword                      -> payload+0x28
// ---------------------------------------------------------------------------
static bool MarshalServerObjUpdate(FPacketBase* Packet, const unsigned int* Args,
                                   unsigned int ArgCount) {
    // The argument is dereferenced untested.
    const SSPObjUpdateInfo* Info = reinterpret_cast<const SSPObjUpdateInfo*>(Args[1]);
    FPayloadCommonUpdateObject Wire;
    Wire.State = Info->State;
    Wire.Pad_0x01[0] = 0;
    Wire.Pad_0x01[1] = 0;
    Wire.Pad_0x01[2] = 0;
    Wire.ObjectId = Info->ObjectId;
    Wire.Position = Info->Position;
    Wire.Orientation = Info->Orientation;
    Wire.Timestamp = (float)Info->Timestamp;
    Wire.Throttle = 0.0f;
    RpcPayloadWrite(Packet, 0, Wire);
    // payload+0x28 is a plain dword copy in retail; the wire field is typed float only
    // because the other direction's receiver reads it with FLD.
    const unsigned int ThrottleBits = Info->ThrottleBits;
    return RpcPayloadWrite(Packet, 0x28, ThrottleBits);
}

// ---------------------------------------------------------------------------
// ch1 0x05 FLPACKET_COMMON_CHATMSG -- sender IServerImpl::SubmitChat 0x06d8ecd0.
//
// Called on the IChatServer sub-object (IServerImpl+0x04), so retail reaches the serializer
// and PushToServer through an adjusted this.
//
//   argument 2, the length (a byte count)      -> payload+0x08
//   argument 3, the body, copied into a block  -> payload+0x0c
//     (both zero when either pointer or length is zero)
//   argument 4, the target id                  -> payload+0x00
// payload+0x04 is never written. Arguments 1 (sender id) and 5 are not serialized; the
// receiver rebuilds them from the session id and -1.
// Args: { sender, length, body, target, system }.
// ---------------------------------------------------------------------------
static bool MarshalServerChatMsg(FPacketBase* Packet, const unsigned int* Args,
                                 unsigned int ArgCount) {
    FPayloadCommonChatMsg Wire;
    Wire.TargetId = Args[3];
    Wire.SenderId = 0;
    Wire.Size = 0;
    Wire.Buffer = nullptr;
    if (Args[2] != 0 && Args[1] != 0) {
        Wire.Size = Args[1];
        Wire.Buffer = RpcCopyBlock(reinterpret_cast<const void*>(Args[2]), Args[1]);
    }
    return RpcPayloadWrite(Packet, 0, Wire);
}

// ---------------------------------------------------------------------------
// The buffer family in the IServer direction differs from the IClient direction (which
// stores the caller's raw pair, docs/re/deviations.md): the IServerImpl senders 0x06d8e000
// (0x13), 0x06d8e080 (0x16), 0x06d8e100 (0x18), 0x06d8e1c0 (0x17), 0x06d8e240 (0x19),
// 0x06d8e2c0 (0x1c) and 0x06d8e340 (0x1a) copy the bytes into a packet-owned block
// (payload+0x00), store the length (+0x04) and set the owns flag (+0x08 = 1); retail
// allocates only when the slot is still empty. SET_WEAPON_GROUP (0x06d8dfb0) stores the raw
// pair without the flag, so the shared MarshalSetWeaponGroup is correct for it.
// ---------------------------------------------------------------------------
static bool MarshalServerBufferCopy(FPacketBase* Packet, const unsigned int* Args,
                                    unsigned int ArgCount) {
    FPayloadCommonBuffer Wire;
    Wire.Buffer = RpcCopyBlock(reinterpret_cast<const void*>(Args[1]), Args[2]);
    Wire.Size = Args[2];
    Wire.bOwnsBuffer = (unsigned char)1;
    Wire.Pad_0x09[0] = 0;
    Wire.Pad_0x09[1] = 0;
    Wire.Pad_0x09[2] = 0;
    return RpcPayloadWrite(Packet, 0, Wire);
}

// IServerImpl::StopTradelane 06d8e770 writes ship, entrance and exit; IClient's mirror
// 06d8bd10 only writes the ship id.
static bool MarshalServerStopTradelane(FPacketBase* Packet, const unsigned int* Args,
                                      unsigned int ArgCount) {
    FPayloadCommonStopTradelane Wire;
    Wire.ShipId = Args[1];
    Wire.TradelaneRing1 = Args[2];
    Wire.TradelaneRing2 = Args[3];
    return RpcPayloadWrite(Packet, 0, Wire);
}

// ---------------------------------------------------------------------------
// ch1 0x1b FLPACKET_COMMON_POP_UP_DIALOG -- sender IServerImpl::PopUpDialog 0x06d8e180.
//
// Unlike its IClient counterpart, it stores only its second argument at payload+0x10
// (?PopUpDialog@IServerImpl@@UAEXII@Z); the FmtStr blocks belong to the other direction,
// so payload+0x00..+0x0f stay zero.
// ---------------------------------------------------------------------------
static bool MarshalServerPopUpDialog(FPacketBase* Packet, const unsigned int* Args,
                                     unsigned int ArgCount) {
    const unsigned int Value = Args[1];
    return RpcPayloadWrite(Packet, 0x10, Value);
}

// ============================================================================
// IServerImpl -- the retail 92-slot IServer, IServerImpl's own PushToServer (slot 92),
// plus IChatServer.
//
// Every serializing slot allocates a CDAPacket through the serializer at IServerImpl+0x08
// (FUN_06d96300, untested; a refused allocation is dropped silently), writes the payload
// from packet+0x20 and pushes it through PushToServer (vtable +0x170), e.g.
// IServerImpl::BaseEnter (0x06d8d480).
//
// Nothing is sent back: every serializing export returns void; only the non-RPC exports
// Startup, Update, SwapConnections, GetServerID and GetServerSig return a value.
//
// The session id is never on the wire, with one retail exception: StopTradeRequest
// (0x06d8e620) puts its single argument at payload+0x08, the target-id slot of its
// siblings (reproduced by the receiver, rpc_payload_ch3.cpp sub-type 0x80).
// ============================================================================
// -----------------------------------------------------------------------
// Allocate, marshal, push. MarshalOverride is the IServer-direction marshaller where one
// is needed; nullptr takes the codec table's (all of channel 3, most of channel 1). Every
// (channel, opcode) an IServerImpl slot allocates has a Marshal, so the codec lookups are
// not null-tested. The Marshal-refused branch is the FRpcMarshalFn contract (unreachable
// given the rpc_payloads.h static_asserts).
// -----------------------------------------------------------------------
static void RpcSendToServer(IServerImpl* Self, unsigned int Channel, unsigned int Opcode,
                            const unsigned int* Args, unsigned int ArgCount, FRpcMarshalFn MarshalOverride) {
    FRpcMarshalFn Marshal = MarshalOverride;
    if (Marshal == nullptr) {
        if (Channel == 3u) {
            Marshal = RpcFindServerCodec(3u, Opcode)->Marshal;
        } else {
            Marshal = RpcFindPayloadCodec(Channel, Opcode)->Marshal;
        }
    }

    // this->Serializer, untested.
    FPacketBase* Packet = nullptr;
    if (Self->Serializer->Allocate(Channel, Opcode, &Packet) != 0) {
        return;
    }

    if (!Marshal(Packet, Args, ArgCount)) {
        Packet->Release();
        return;
    }

    // Always through the vtable (+0x170).
    Self->PushToServer(Packet->AsRaw());
}

// The common case: the codec table knows this type.
static void RpcSendToServer(IServerImpl* Self, unsigned int Channel, unsigned int Opcode,
                            const unsigned int* Args, unsigned int ArgCount) {
    RpcSendToServer(Self, Channel, Opcode, Args, ArgCount, nullptr);
}

// FLPACKET_COMMON_PLAYER_TRADE: eight messages under one opcode, told apart by the bitmask
// at payload+0x00. MarshalPlayerTrade takes { SubType, A1, A2 }; on this side A1 is often
// zero, because the IServerImpl senders write fewer fields than their IClient twins.
static void RpcSendTradeToServer(IServerImpl* Self, unsigned int SubType, unsigned int A1, unsigned int A2) {
    const unsigned int Args[3] = { SubType, A1, A2 };
    RpcSendToServer(Self, 1u, 0x12u, Args, 3u, nullptr);
}

// === Slots 0-8: combat and ship control (channel 1) ===
void IServerImpl::FireWeapon(unsigned int player_id, const XFireWeaponInfo& info) {
    // retail 0x06d8ddd0: ch1 op 0x02 FLPACKET_COMMON_FIREWEAPON
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x02u, Args, 2u);
}
void IServerImpl::ActivateEquip(unsigned int player_id, const XActivateEquip& info) {
    // retail 0x06d8e6c0: ch1 op 0x08 FLPACKET_COMMON_ACTIVATEEQUIP
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x08u, Args, 2u);
}
void IServerImpl::ActivateCruise(unsigned int player_id, const XActivateCruise& info) {
    // retail 0x06d8e7c0: ch1 op 0x0e FLPACKET_COMMON_ACTIVATECRUISE
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x0eu, Args, 2u);
}
void IServerImpl::ActivateThrusters(unsigned int player_id, const XActivateThrusters& info) {
    // retail 0x06d8e810: ch1 op 0x15 FLPACKET_COMMON_ACTIVATETHRUSTERS
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x15u, Args, 2u);
}
void IServerImpl::SetTarget(unsigned int player_id, const XSetTarget& info) {
    // retail 0x06d8ea30: ch1 op 0x04 FLPACKET_COMMON_SETTARGET
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x04u, Args, 2u);
}
void IServerImpl::TractorObjects(unsigned int player_id, const XTractorObjects& info) {
    // retail 0x06d8e860: ch1 op 0x1d FLPACKET_COMMON_TRACTOROBJECTS
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x1du, Args, 2u);
}
void IServerImpl::GoTradelane(unsigned int player_id, const XGoTradelane& info) {
    // retail 0x06d8e710: ch1 op 0x0f FLPACKET_COMMON_GOTRADELANE
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x0fu, Args, 2u);
}
void IServerImpl::StopTradelane(unsigned int session_id, unsigned int ship_id, unsigned int entrance_ring_id, unsigned int exit_ring_id) {
    // retail 0x06d8e770: ch1 op 0x10. Serializes the three arguments after the session id.
    const unsigned int Args[4] = { session_id, ship_id, entrance_ring_id, exit_ring_id };
    RpcSendToServer(this, 1u, 0x10u, Args, 4u, MarshalServerStopTradelane);
}
void IServerImpl::JettisonCargo(unsigned int player_id, const XJettisonCargo& info) {
    // retail 0x06d8ea90: ch1 op 0x14 FLPACKET_COMMON_JETTISONCARGO
    const unsigned int Args[2] = { player_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x14u, Args, 2u);
}

// === Slots 9-12: core lifecycle (NOT serialized -- direct Server.dll calls) ===
bool IServerImpl::Startup(const SStartupInfo& info) {
    // Retail 0x06d8f1e0. strncpy into the 0x104-byte .data:0x06dbbbc0 with n = the whole
    // buffer, so a path of 260+ characters leaves it unterminated, as in retail. IClient
    // slot 10 LoadLibraryA's this path to reach the real client.
    strncpy(GCallerModulePath, info.ModulePath, 0x104u);

    typedef IServer* (*GetServerInterfaceFn)();
    typedef void* (*GetChatServerInterfaceFn)();

    // HMODULE in .data:DAT_06dbbd64.
    g_hServerDll = LoadLibraryA("server.dll");
    if (g_hServerDll != nullptr) {
        // Results to DAT_06dbbd5c / DAT_06dbbd60; neither GetProcAddress result is tested.
        GetServerInterfaceFn GetServer =
            (GetServerInterfaceFn)GetProcAddress(g_hServerDll, "GetServerInterface");
        g_pServerInterface = GetServer();
        GetChatServerInterfaceFn GetChat =
            (GetChatServerInterfaceFn)GetProcAddress(g_hServerDll, "GetChatServerInterface");
        g_pChatServerInterface = GetChat();
    } else {
        // DACOM FDUMP (.rdata:06db311c), format .rdata:06db3bb4, file .rdata:06db3bf0.
        FDUMP(0x100001, "%s(%d) : *** ERROR: Connect - failed to LoadLibrary(%s)\n",
              "E:\\FL\\Scratch\\Source\\RPCLocal\\RPCServer.cpp", 0x3e, "server.dll");
    }

    // Nothing is freed or cleared on this path.
    if (g_pServerInterface == nullptr) {
        return false;
    }

    // A stack copy of the 0x214-byte SStartupInfo whose ModulePath is replaced with
    // rpclocal.dll's own path (DAT_06dbbccc): Server.dll is told rpclocal is the loading
    // module; IniPath (+0x104) passes through.
    SStartupInfo Forwarded = info;
    GetModuleFileNameA(g_hSelfModule, Forwarded.ModulePath, 0x104u);

    // A refusal is returned as is: the library stays loaded and both interface pointers
    // stay set.
    const bool bStarted = g_pServerInterface->Startup(Forwarded);
    if (!bStarted) {
        return false;
    }

    // DAT_06dbbd24 / DAT_06dbbd20: the copies RpcDispatchToServer (FUN_06d86a70) reads.
    GRealServer = g_pServerInterface;
    GRealChatServer = static_cast<IChatServer*>(g_pChatServerInterface);

    // The serializer singleton (FUN_06d95e50), stored whatever it is. Server.dll's Startup
    // has usually created it already through IClient slot 10, which registers channels 2
    // and 1.
    Serializer = RpcGetPacketSerializer();
    // Channel 3 (FUN_06d9f100), then channel 1 (FUN_06d9ace0). Every path returns true.
    if (Serializer != nullptr && RpcRegisterChannel3Types(Serializer) == 0) {
        RpcRegisterChannel1Types(Serializer);
    }
    return true;
}

// Retail 0x06d8f300. The whole body is guarded by the connection: with none, Shutdown does
// nothing (no Server.dll call, no FreeLibrary, no serializer release). Otherwise: the real
// IServer::Shutdown, FreeLibrary (clearing the HMODULE and chat server), DAT_06dbbd5c = 0,
// destroy and free the connection, release the serializer, clear the connected byte.
//
// Retail quirk, reproduced: the serializer's use count is 1 and never raised, so the
// release destroys it while the IClient proxy (Client+0x08) and DAT_06dbbe38 still hold
// it. Retail also leaves GRealServer / GRealChatServer set and does not call IClient slot
// 15 here (Server.dll's own Shutdown does).
void IServerImpl::Shutdown() {
    if (GServerConnection == nullptr) {
        return;
    }

    g_pServerInterface->Shutdown();

    if (g_hServerDll != nullptr) {
        FreeLibrary(g_hServerDll);
        g_hServerDll = nullptr;
        g_pChatServerInterface = nullptr;
    }

    g_pServerInterface = nullptr;

    // Only this copy is cleared; GRpcConnection and the IClient proxy's copy keep the freed
    // pointer, as in retail (see rpc_transport.h).
    if (GServerConnection != nullptr) {
        delete GServerConnection;
    }
    GServerConnection = nullptr;

    Serializer->Release();                                          // FUN_06d8f4a0

    GbRpcConnected = false;                                         // DAT_06dbbd19
}

// Retail 0x06d8f400: COMMON.DLL GiveServerRunPermission (IAT .rdata:06db30fc), drain the
// client-bound read queue through HandlePacket (which releases each packet), COMMON.DLL
// WaitForClientRunPermission (IAT .rdata:06db30f8), then the IClient pump FUN_06d8efc0;
// returns 1.
//
// DAT_06dbbd58 is read untested, as in retail: Update before Connect faults. freelancer.exe
// connects in the synchronous sequence after Startup (RunConnectFlow ->
// RunPostConnectGate -> InvokeServerBeginLogin) before the frame tick reaches Update. The
// pointer is re-read for every pop.
int IServerImpl::Update() {
    GiveServerRunPermission();

    FPacketBase* Packet = GServerConnection->PopClientBound();
    while (Packet != nullptr) {
        HandlePacket(Packet->AsRaw());   // releases the packet
        Packet = GServerConnection->PopClientBound();
    }

    WaitForClientRunPermission();

    // FUN_06d8efc0(&Client) -- IClient slot 16, the client-side pump
    RpcClientPump();
    return 1;
}

// Retail 0x06d8f370: a tail jump into the real IServer, no test.
void IServerImpl::ElapseTime(float dt) {
    g_pServerInterface->ElapseTime(dt);
}

// === Slots 13-14: connection swap ===
// Retail 0x06d8eae0 is a single RET shared by three exported names:
// ?OnConnect@, ?GetServerStats@ (non-virtual) and ?SetActiveConnection@.
void IServerImpl::SetActiveConnection(EFLConnection conn) {
}
// Retail 0x06d8f3f0: always true, never forwarded.
bool IServerImpl::SwapConnections(EFLConnection conn_a, EFLConnection conn_b) {
    return true;
}

// === Slots 15-17: connections ===
void IServerImpl::Connect(const char* server_name, unsigned short* port) {
    // Retail 0x06d8f390. An existing connection is reused, not restarted, and the
    // connected byte left as it was. Otherwise create (FUN_06d88280) and Start(1); only a
    // zero return sets the connected byte. Always forwarded to the real IServer.
    if (GServerConnection == nullptr) {
        GServerConnection = RpcCreateConnection();
        if (GServerConnection != nullptr) {
            if (GServerConnection->Start(FRpcConnection::kModeSingleBuffered) ==
                FRpcConnection::kStartOk) {
                GbRpcConnected = true;
            }
        }
    }
    g_pServerInterface->Connect(server_name, port);
}
// Retail 0x06d8f3d0: forward, then clear the connected byte. No test.
void IServerImpl::DisConnect(unsigned int session_id, EFLConnection type) {
    g_pServerInterface->DisConnect(session_id, type);
    GbRpcConnected = false;
}
// Retail 0x06d8eae0 -- the same shared RET as SetActiveConnection. A no-op.
void IServerImpl::OnConnect(unsigned int /*session_id*/) {
}
// Retail 0x06d8eae0 again (ordinal 38, public NON-virtual): the out-parameter is never
// written.
void IServerImpl::GetServerStats(ServerStats& /*Stats*/) {
}

// === Slots 18-24: authentication and character ===
void IServerImpl::Login(const SLoginInfo& info, unsigned int session_id) {
    // retail 0x06d8cc70: ch3 op 0x01 FLPACKET_CLIENT_LOGIN
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x01u, Args, 2u);
}
void IServerImpl::CharacterInfoReq(unsigned int session_id, bool bSendEmpty) {
    // retail 0x06d8cd00: ch3 op 0x05, payload 0 -- the flag is not serialized
    const unsigned int Args[2] = { session_id, bSendEmpty ? 1u : 0u };
    RpcSendToServer(this, 3u, 0x05u, Args, 2u);
}
void IServerImpl::CharacterSelect(const CHARACTER_ID& char_id, unsigned int session_id) {
    // retail 0x06d8cd60: ch3 op 0x06, serializes the CHARACTER_ID string
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&char_id), session_id };
    RpcSendToServer(this, 3u, 0x06u, Args, 2u);
}
void IServerImpl::NewCharacterInfoReq(unsigned int session_id) {
    // retail 0x06d8cdc0: ch3 op 0x38, payload 0
    const unsigned int Args[1] = { session_id };
    RpcSendToServer(this, 3u, 0x38u, Args, 1u);
}
void IServerImpl::CreateNewCharacter(const SCreateCharacterInfo& info, unsigned int session_id) {
    // retail 0x06d8cdf0: ch3 op 0x39, wcsncpy of info.Name plus the four dwords at +0x30
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x39u, Args, 2u);
}
void IServerImpl::DestroyCharacter(const CHARACTER_ID& char_id, unsigned int session_id) {
    // retail 0x06d8ce90: ch3 op 0x3a
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&char_id), session_id };
    RpcSendToServer(this, 3u, 0x3au, Args, 2u);
}
void IServerImpl::CharacterSkipAutosave(unsigned int session_id) {
    // retail 0x06d8cd30: ch3 op 0x42, payload 0
    const unsigned int Args[1] = { session_id };
    RpcSendToServer(this, 3u, 0x42u, Args, 1u);
}

// === Slots 25-32: ship and equipment requests ===
void IServerImpl::ReqShipArch(unsigned int arch_id, unsigned int session_id) {
    // retail 0x06d8ced0: ch3 op 0x15, serializes arch_id only
    const unsigned int Args[2] = { arch_id, session_id };
    RpcSendToServer(this, 3u, 0x15u, Args, 2u);
}
void IServerImpl::ReqHullStatus(float hull_status, unsigned int session_id) {
    // retail 0x06d8cf10: ch3 op 0x3b, one dword -- a float, read back with FLD
    const unsigned int Args[2] = { RpcFloatBits(hull_status), session_id };
    RpcSendToServer(this, 3u, 0x3bu, Args, 2u);
}
void IServerImpl::ReqCollisionGroups(const std::list<CollisionGroupDesc>& groups, unsigned int session_id) {
    // retail 0x06d8cf50: ch3 op 0x36; FUN_06d8f710 deep-copies the list node by node
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&groups), session_id };
    RpcSendToServer(this, 3u, 0x36u, Args, 2u);
}
void IServerImpl::ReqEquipment(const EquipDescList& equipment, unsigned int session_id) {
    // retail 0x06d8cf90: ch3 op 0x16; walks the list and copies 0x18 bytes per element
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&equipment), session_id };
    RpcSendToServer(this, 3u, 0x16u, Args, 2u);
}
void IServerImpl::ReqCargo(const EquipDescList& cargo, unsigned int session_id) {
    // retail 0x06d8d0e0: ch3 op 0x17. This is the REAL slot 29 -- the old "Connect2"
    // entry here was a fabricated duplicate of Connect.
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&cargo), session_id };
    RpcSendToServer(this, 3u, 0x17u, Args, 2u);
}
void IServerImpl::ReqAddItem(unsigned int arch_id, const char* hardpoint, int count, float health,
                unsigned char b_mounted, unsigned int session_id) {
    // retail 0x06d8d230: ch3 op 0x18
    const unsigned int Args[6] = {
        arch_id, reinterpret_cast<unsigned int>(hardpoint), (unsigned int)count,
        RpcFloatBits(health), static_cast<unsigned int>(b_mounted), session_id
    };
    RpcSendToServer(this, 3u, 0x18u, Args, 6u);
}
void IServerImpl::ReqRemoveItem(unsigned short equip_id, int count, unsigned int session_id) {
    // retail 0x06d8d2b0: ch3 op 0x19
    const unsigned int Args[3] = { (unsigned int)equip_id, (unsigned int)count, session_id };
    RpcSendToServer(this, 3u, 0x19u, Args, 3u);
}
void IServerImpl::ReqModifyItem(unsigned short equip_id, const char* hardpoint, int count, float health,
                   unsigned char b_mounted, unsigned int session_id) {
    // retail 0x06d8d300: ch3 op 0x1a
    const unsigned int Args[6] = {
        (unsigned int)equip_id, reinterpret_cast<unsigned int>(hardpoint), (unsigned int)count,
        RpcFloatBits(health), static_cast<unsigned int>(b_mounted), session_id
    };
    RpcSendToServer(this, 3u, 0x1au, Args, 6u);
}

// === Slots 33-34: cash (int, not float) ===
void IServerImpl::ReqSetCash(int cash, unsigned int session_id) {
    // retail 0x06d8d380: ch3 op 0x1b
    const unsigned int Args[2] = { (unsigned int)cash, session_id };
    RpcSendToServer(this, 3u, 0x1bu, Args, 2u);
}
void IServerImpl::ReqChangeCash(int delta, unsigned int session_id) {
    // retail 0x06d8d3c0: ch3 op 0x1c
    const unsigned int Args[2] = { (unsigned int)delta, session_id };
    RpcSendToServer(this, 3u, 0x1cu, Args, 2u);
}

// === Slots 35-40: base and location ===
void IServerImpl::BaseEnter(unsigned int base_id, unsigned int session_id) {
    // retail 0x06d8d480: ch3 op 0x07, serializes base_id only
    const unsigned int Args[2] = { base_id, session_id };
    RpcSendToServer(this, 3u, 0x07u, Args, 2u);
}
void IServerImpl::BaseExit(unsigned int base_id, unsigned int session_id) {
    // retail 0x06d8d4c0: ch3 op 0x0d
    const unsigned int Args[2] = { base_id, session_id };
    RpcSendToServer(this, 3u, 0x0du, Args, 2u);
}
void IServerImpl::LocationEnter(unsigned int location_id, unsigned int session_id) {
    // retail 0x06d8d400: ch3 op 0x0e
    const unsigned int Args[2] = { location_id, session_id };
    RpcSendToServer(this, 3u, 0x0eu, Args, 2u);
}
void IServerImpl::LocationExit(unsigned int location_id, unsigned int session_id) {
    // retail 0x06d8d440: ch3 op 0x0f
    const unsigned int Args[2] = { location_id, session_id };
    RpcSendToServer(this, 3u, 0x0fu, Args, 2u);
}
void IServerImpl::BaseInfoRequest(unsigned int base_id, unsigned int session_id, bool b_full) {
    // retail 0x06d8d500: ch3 op 0x08, serializes base_id and b_full
    const unsigned int Args[3] = { base_id, session_id, b_full ? 1u : 0u };
    RpcSendToServer(this, 3u, 0x08u, Args, 3u);
}
void IServerImpl::LocationInfoRequest(unsigned int location_id, unsigned int session_id, bool b_full) {
    // retail 0x06d8d550: ch3 op 0x09
    const unsigned int Args[3] = { location_id, session_id, b_full ? 1u : 0u };
    RpcSendToServer(this, 3u, 0x09u, Args, 3u);
}

// === Slots 41-46: GF events ===
void IServerImpl::GFObjSelect(unsigned int object_id, unsigned int session_id) {
    // retail 0x06d8da40: ch3 op 0x13, serializes object_id only
    const unsigned int Args[2] = { object_id, session_id };
    RpcSendToServer(this, 3u, 0x13u, Args, 2u);
}
void IServerImpl::GFGoodVaporized(const SGFGoodVaporizedInfo& info, unsigned int session_id) {
    // retail 0x06d8d650: ch3 op 0x3c, the struct's first three dwords
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x3cu, Args, 2u);
}
void IServerImpl::MissionResponse(unsigned int OfferId, unsigned long OfferKind, unsigned char bAccepted, unsigned int SessionId) {
    // retail 0x06d8da80: ch3 op 0x14
    const unsigned int Args[4] = {
        OfferId, static_cast<unsigned int>(OfferKind), bAccepted, SessionId
    };
    RpcSendToServer(this, 3u, 0x14u, Args, 4u);
}
void IServerImpl::TradeResponse(const unsigned char* data, int size, unsigned int session_id) {
    // retail 0x06d8dad0: ch3 op 0x27, the blob is copied into the packet
    const unsigned int Args[3] = {
        reinterpret_cast<unsigned int>(data), (unsigned int)size, session_id
    };
    RpcSendToServer(this, 3u, 0x27u, Args, 3u);
}
void IServerImpl::GFGoodBuy(const SGFGoodBuyInfo& info, unsigned int session_id) {
    // retail 0x06d8d5a0: ch3 op 0x12, the struct's first four dwords
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x12u, Args, 2u);
}
void IServerImpl::GFGoodSell(const SGFGoodSellInfo& info, unsigned int session_id) {
    // retail 0x06d8d600: ch3 op 0x11, the struct's first three dwords
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x11u, Args, 2u);
}

// === Slots 47-51: system and launch ===
void IServerImpl::SystemSwitchOutComplete(unsigned int ship_object_id, unsigned int session_id) {
    // retail 0x06d8d6a0: ch3 op 0x0b
    const unsigned int Args[2] = { ship_object_id, session_id };
    RpcSendToServer(this, 3u, 0x0bu, Args, 2u);
}
void IServerImpl::PlayerLaunch(unsigned int ship_id, unsigned int session_id) {
    // retail 0x06d8da00: ch3 op 0x04
    const unsigned int Args[2] = { ship_id, session_id };
    RpcSendToServer(this, 3u, 0x04u, Args, 2u);
}
void IServerImpl::LaunchComplete(unsigned int base_obj_id, unsigned int ship_id) {
    // retail 0x06d8d8e0: ch3 op 0x3e, serializes BOTH arguments
    const unsigned int Args[2] = { base_obj_id, ship_id };
    RpcSendToServer(this, 3u, 0x3eu, Args, 2u);
}
void IServerImpl::JumpInComplete(unsigned int system_id, unsigned int ship_id) {
    // retail 0x06d8d930: ch3 op 0x43, serializes BOTH arguments
    const unsigned int Args[2] = { system_id, ship_id };
    RpcSendToServer(this, 3u, 0x43u, Args, 2u);
}
void IServerImpl::Hail(unsigned int ship_id, unsigned int target_id, unsigned int system_id) {
    // retail 0x06d8d980: ch3 op 0x3f, serializes all three
    const unsigned int Args[3] = { ship_id, target_id, system_id };
    RpcSendToServer(this, 3u, 0x3fu, Args, 3u);
}

// === Slots 52-55: SP collisions ===
void IServerImpl::SPObjUpdate(const SSPObjUpdateInfo& info, unsigned int session_id) {
    // retail 0x06d8dc30: ch1 op 0x01 FLPACKET_COMMON_UPDATEOBJECT. The argument struct
    // is NOT the one the IClient sender reads, so this type marshals its own way.
    const unsigned int Args[2] = { session_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 1u, 0x01u, Args, 2u, MarshalServerObjUpdate);
}
void IServerImpl::SPMunitionCollision(const SSPMunitionCollisionInfo& info, unsigned int session_id) {
    // retail 0x06d8d6e0: ch3 op 0x03
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x03u, Args, 2u);
}
void IServerImpl::SPBadLandsObjCollision(const SSPBadLandsObjCollisionInfo& info, unsigned int session_id) {
    // retail 0x06d8d760: ch3 op 0x3d
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x3du, Args, 2u);
}
void IServerImpl::SPObjCollision(const SSPObjCollisionInfo& info, unsigned int session_id) {
    // retail 0x06d8d7b0: ch3 op 0x0c
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x0cu, Args, 2u);
}

// === Slots 56-59: SP requests ===
void IServerImpl::SPRequestUseItem(const SSPUseItem& info, unsigned int session_id) {
    // retail 0x06d8ec20: ch3 op 0x40
    const unsigned int Args[2] = { reinterpret_cast<unsigned int>(&info), session_id };
    RpcSendToServer(this, 3u, 0x40u, Args, 2u);
}
void IServerImpl::SPRequestInvincibility(unsigned int ship_id, unsigned char b_invincible,
                            InvincibilityReason reason, unsigned int session_id) {
    // retail 0x06d8ec80: ch3 op 0x44; the reason and the flag are written as single bytes
    const unsigned int Args[4] = {
        ship_id, b_invincible, (unsigned int)reason, session_id
    };
    RpcSendToServer(this, 3u, 0x44u, Args, 4u);
}
void IServerImpl::SaveGame(const CHARACTER_ID& char_id, const wchar_t* description, unsigned int session_id) {
    // retail 0x06d8db10: ch3 op 0x1e
    const unsigned int Args[3] = {
        reinterpret_cast<unsigned int>(&char_id),
        reinterpret_cast<unsigned int>(description), session_id
    };
    RpcSendToServer(this, 3u, 0x1eu, Args, 3u);
}
void IServerImpl::MissionSaveB(unsigned int session_id, unsigned long mission_id) {
    // retail 0x06d8db70: ch3 op 0x45, serializes mission_id only
    const unsigned int Args[2] = { session_id, (unsigned int)mission_id };
    RpcSendToServer(this, 3u, 0x45u, Args, 2u);
}

// === Slots 60-63: events and mining ===
void IServerImpl::RequestEvent(int event_type, unsigned int ship_id, unsigned int target_id, unsigned int second_id,
                  unsigned long b_flag, unsigned int session_id) {
    // retail 0x06d8eaf0: ch3 op 0x31
    const unsigned int Args[6] = {
        (unsigned int)event_type, ship_id, target_id, second_id, (unsigned int)b_flag, session_id
    };
    RpcSendToServer(this, 3u, 0x31u, Args, 6u);
}
void IServerImpl::RequestCancel(int event_type, unsigned int ship_id, unsigned int target_id, unsigned long arg3,
                   unsigned int session_id) {
    // retail 0x06d8eb60: ch3 op 0x32
    const unsigned int Args[5] = {
        (unsigned int)event_type, ship_id, target_id, (unsigned int)arg3, session_id
    };
    RpcSendToServer(this, 3u, 0x32u, Args, 5u);
}
void IServerImpl::MineAsteroid(unsigned int system_id, const Vector& position, unsigned int loot_id,
                  unsigned int secondary_loot, unsigned int quantity, unsigned int session_id) {
    // retail 0x06d8dcf0: ch3 op 0x20
    const unsigned int Args[6] = {
        system_id, reinterpret_cast<unsigned int>(&position), loot_id,
        secondary_loot, quantity, session_id
    };
    RpcSendToServer(this, 3u, 0x20u, Args, 6u);
}
void IServerImpl::CommComplete(unsigned int sender_id, unsigned int receiver_id, unsigned int msg_id, CommResult result) {
    // retail 0x06d8dd70: ch3 op 0x37
    const unsigned int Args[4] = { sender_id, receiver_id, msg_id, (unsigned int)result };
    RpcSendToServer(this, 3u, 0x37u, Args, 4u);
}

// === Slots 64-66 ===
void IServerImpl::RequestCreateShip(unsigned int session_id) {
    // retail 0x06d8d9d0: ch3 op 0x10, payload 0
    const unsigned int Args[1] = { session_id };
    RpcSendToServer(this, 3u, 0x10u, Args, 1u);
}
void IServerImpl::SPScanCargo(const unsigned int& scanner_id, const unsigned int& target_id, unsigned int session_id) {
    // retail 0x06d8ebd0: ch3 op 0x2b, serializes *scanner_id and *target_id
    const unsigned int Args[3] = {
        reinterpret_cast<unsigned int>(&scanner_id),
        reinterpret_cast<unsigned int>(&target_id), session_id
    };
    RpcSendToServer(this, 3u, 0x2bu, Args, 3u);
}
void IServerImpl::SetManeuver(unsigned int session_id, const XSetManeuver& info) {
    // retail 0x06d8e670: ch3 op 0x2f -- the session comes FIRST in this one
    const unsigned int Args[2] = { session_id, reinterpret_cast<unsigned int>(&info) };
    RpcSendToServer(this, 3u, 0x2fu, Args, 2u);
}

// === Slots 67-70: interface and mission ===
void IServerImpl::InterfaceItemUsed(unsigned int session_id, unsigned int item_id) {
    // retail 0x06d8d820: ch3 op 0x35, serializes item_id only
    const unsigned int Args[2] = { session_id, item_id };
    RpcSendToServer(this, 3u, 0x35u, Args, 2u);
}
void IServerImpl::AbortMission(unsigned int session_id, unsigned int mission_id) {
    // retail 0x06d8d8a0: ch3 op 0x41, serializes mission_id only
    const unsigned int Args[2] = { session_id, mission_id };
    RpcSendToServer(this, 3u, 0x41u, Args, 2u);
}
void IServerImpl::RTCDone(unsigned int session_id, unsigned int rtc_id) {
    // retail 0x06d8d860: ch3 op 0x47, serializes rtc_id only
    const unsigned int Args[2] = { session_id, rtc_id };
    RpcSendToServer(this, 3u, 0x47u, Args, 2u);
}
void IServerImpl::SetWeaponGroup(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8dfb0: ch1 op 0x11. The one buffer-family member that stores the
    // caller's pointer on this side too, so the shared codec is right; the packet is
    // queued with the caller's pointer inside it, as in retail.
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x11u, Args, 3u);
}

// === Slots 71-78: buffer-parameter methods (channel 1) ===
//
// Seven of the eight copy on this side (see MarshalServerBufferCopy). 0x16 and 0x18 copy on
// the IClient side as well and keep the shared codec; the other five use the copying
// marshal. The packet class's destructor frees the block when bOwnsBuffer is set
// (FUN_06d9b1a0 and siblings).
void IServerImpl::SetVisitedState(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8e000: ch1 op 0x13, operator_new + REP MOVS
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x13u, Args, 3u, MarshalServerBufferCopy);
}
void IServerImpl::RequestBestPath(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8e080: ch1 op 0x16, copies in both directions
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x16u, Args, 3u);
}
void IServerImpl::RequestPlayerStats(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8e100: ch1 op 0x18, copies in both directions
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x18u, Args, 3u);
}
void IServerImpl::PopUpDialog(unsigned int session_id, unsigned int button) {
    // retail 0x06d8e180: ch1 op 0x1b -- one dword at payload+0x10 and nothing else
    const unsigned int Args[2] = { session_id, button };
    RpcSendToServer(this, 1u, 0x1bu, Args, 2u, MarshalServerPopUpDialog);
}
void IServerImpl::RequestGroupPositions(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8e1c0: ch1 op 0x17, operator_new + REP MOVS
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x17u, Args, 3u, MarshalServerBufferCopy);
}
void IServerImpl::SetMissionLog(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8e240: ch1 op 0x19, operator_new + REP MOVS
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x19u, Args, 3u, MarshalServerBufferCopy);
}
void IServerImpl::SetInterfaceState(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8e2c0: ch1 op 0x1c, operator_new + REP MOVS
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x1cu, Args, 3u, MarshalServerBufferCopy);
}
void IServerImpl::RequestRankLevel(unsigned int session_id, unsigned char* data, int size) {
    // retail 0x06d8e340: ch1 op 0x1a, operator_new + REP MOVS
    const unsigned int Args[3] = {
        session_id, reinterpret_cast<unsigned int>(data), (unsigned int)size
    };
    RpcSendToServer(this, 1u, 0x1au, Args, 3u, MarshalServerBufferCopy);
}

// === Slots 79-86: trade (all ch1 op 0x12, distinguished by the bitmask at packet+0x20) ===
void IServerImpl::InitiateTrade(unsigned int session_id, unsigned int target_session_id) {
    // retail 0x06d8e3c0: subtype 0x01 at packet+0x20, target id at packet+0x28
    (void)session_id;
    RpcSendTradeToServer(this, 0x01u, target_session_id, 0u);
}
void IServerImpl::TerminateTrade(unsigned int session_id, int reason) {
    // retail 0x06d8e410: subtype 0x02, reason at packet+0x24 (MarshalPlayerTrade's A1).
    (void)session_id;
    RpcSendTradeToServer(this, 0x02u, (unsigned int)reason, 0u);
}
void IServerImpl::AcceptTrade(unsigned int session_id, bool b_accepted) {
    // retail 0x06d8e460: subtype 0x40, the flag BYTE at packet+0x30 and nothing at
    // +0x28 -- the IClient sender writes a target id there, this one does not.
    (void)session_id;
    RpcSendTradeToServer(this, 0x40u, 0u, b_accepted ? 1u : 0u);
}
void IServerImpl::SetTradeMoney(unsigned int session_id, unsigned long money) {
    // retail 0x06d8e4b0: subtype 0x04, money at packet+0x2c, nothing at +0x28.
    (void)session_id;
    RpcSendTradeToServer(this, 0x04u, 0u, (unsigned int)money);
}
void IServerImpl::AddTradeEquip(unsigned int session_id, const EquipDesc& equip) {
    // retail 0x06d8e500: subtype 0x08. Reads EquipDesc ArchId (+0x04), Count (+0x14),
    // Health (+0x10, FLD/FSTP) and the word Id (+0x02).
    (void)session_id;
    RpcSendTradeToServer(this, 0x08u, 0u, reinterpret_cast<unsigned int>(&equip));
}
void IServerImpl::DelTradeEquip(unsigned int session_id, const EquipDesc& equip) {
    // retail 0x06d8e570: subtype 0x10, the same three minus the float at +0x3c.
    (void)session_id;
    RpcSendTradeToServer(this, 0x10u, 0u, reinterpret_cast<unsigned int>(&equip));
}
void IServerImpl::RequestTrade(unsigned int session_id, unsigned int target_session_id) {
    // retail 0x06d8e5d0: subtype 0x20, target id at packet+0x28
    (void)session_id;
    RpcSendTradeToServer(this, 0x20u, target_session_id, 0u);
}
void IServerImpl::StopTradeRequest(unsigned int session_id) {
    // retail 0x06d8e620: subtype 0x80; its one argument goes to packet+0x28 -- the only
    // place in the module where a session id reaches the wire. The receiver passes it on
    // instead of its own session id.
    RpcSendTradeToServer(this, 0x80u, session_id, 0u);
}

// === Slots 87-92: tail ===
void IServerImpl::ReqDifficultyScale(float scale, unsigned int session_id) {
    // retail 0x06d8dbf0: ch3 op 0x46, one dword -- a float, read back with FLD
    const unsigned int Args[2] = { RpcFloatBits(scale), session_id };
    RpcSendToServer(this, 3u, 0x46u, Args, 2u);
}
// Retail 0x06d8cda0: always zero, never forwarded to Server.dll.
unsigned int IServerImpl::GetServerID() {
    return 0;
}
// Retail 0x06d8cdb0 returns .data:DAT_06dbbbb0, an 8-byte buffer nothing writes, so the
// local server's signature is the empty string.
const char* IServerImpl::GetServerSig() {
    static const char kEmptySig[8] = {0};
    return kEmptySig;
}
void IServerImpl::DumpPacketStats(const char* name) {
    // Retail 0x06d8f480: FUN_06d961b0 on the singleton (FUN_06d95e50), not
    // this->Serializer -- asked afresh (a dump before Startup builds an empty registry) and
    // untested; never forwarded to Server.dll.
    RpcGetPacketSerializer()->DumpStats(name);
}
void IServerImpl::Dock(const unsigned int& base_id, const unsigned int& session_id) {
    // retail 0x06d8dbb0: ch3 op 0x24, serializes *base_id only
    const unsigned int Args[2] = {
        reinterpret_cast<unsigned int>(&base_id),
        reinterpret_cast<unsigned int>(&session_id)
    };
    RpcSendToServer(this, 3u, 0x24u, Args, 2u);
}
// Retail 0x06d8f460: tail jump to FUN_06d86150, queuing the packet pointer server-bound.
// Retail quirk: with no connection (before Connect, after Shutdown) the packet is dropped
// silently and never released (leaked); FUN_06d86150's answer is ignored too.
void IServerImpl::PushToServer(CDAPacket* packet) {
    if (GServerConnection != nullptr) {
        GServerConnection->PushServerBound(reinterpret_cast<FPacketBase*>(packet));
    }
}

// IChatServer::SubmitChat -- retail 0x06d8ecd0 sends ch1 op 0x05 FLPACKET_COMMON_CHATMSG
// through the same serializer and PushToServer, reached from the IChatServer sub-object.
void IServerImpl::SubmitChat(CHAT_ID sender_id, unsigned long size, const void* msg_data, CHAT_ID target_id, int system_id) {
    const unsigned int Args[5] = {
        sender_id.Id, (unsigned int)size, reinterpret_cast<unsigned int>(msg_data),
        target_id.Id, (unsigned int)system_id
    };
    RpcSendToServer(this, 1u, 0x05u, Args, 5u, MarshalServerChatMsg);
}

// === Internal packet handling ===
//
// IServerImpl::HandlePacket (0x06d81170) is the whole server -> client half: channel =
// header >> 16, opcode = header & 0xfff, then a channel-1 switch (op 1..0x1d) or channel-2
// switch (op 1..0x5b). Each case deserializes the payload at packet+0x20 and calls one
// IClient slot (DAT_06dbbcd8), or IChatClient (DAT_06dbbcd4) for channel 1 op 5. The case
// bodies are the Deliver halves in rpc_payload_ch1.cpp / rpc_payload_ch2.cpp, selected by
// RpcHandleIncomingPacket. Every path, the default included, releases the packet
// (FUN_06d8f4a0).
void IServerImpl::HandlePacket(CDAPacket* pkt) {
    FPacketBase* Packet = reinterpret_cast<FPacketBase*>(pkt);
    RpcHandleIncomingPacket(Packet);
    Packet->Release();
}

// ordinal 41, 0x06d84490. HandlePacket's caseD_3 calls it with (packet, the real client,
// 1); the body is beside the CHARACTERINFO codec in rpc_payload_ch2.cpp.
bool IServerImpl::HandleCharacterInfo(CDAPCharacterInfo* Packet, IClient* Client, unsigned int Arg) {
    return RpcHandleCharacterInfo(reinterpret_cast<const FPacketBase*>(Packet), Client, Arg);
}

// ordinal 42, 0x06d84c60. Not called by HandlePacket (caseD_5 inlines the same logic);
// exported only. Body in rpc_payload_ch2.cpp.
bool IServerImpl::HandleDamageObject(CDAPDamageSpaceObj* Packet, IClient* Client, unsigned int Arg) {
    return RpcHandleDamageObject(reinterpret_cast<const FPacketBase*>(Packet), Client, Arg);
}

// ============================================================================
// Special members and the exported singleton.
//
// Retail ??0IServerImpl@@QAE@XZ (0x06d81110) nulls +0x08 and sets the two vptrs;
// ??0IServerImpl@@QAE@ABU0@@Z (0x06d81130) and ??4IServerImpl@@QAEAAU0@ABU0@@Z (0x06d81160)
// copy the serializer pointer without an AddRef, as the defaulted members do.
// ============================================================================
IServerImpl::IServerImpl() : Serializer(nullptr) {
}
IServerImpl::IServerImpl(const IServerImpl& Other) = default;
IServerImpl& IServerImpl::operator=(const IServerImpl& Other) = default;

// .data:Server (0x06dbbd28), export ordinal 113, built by LAB_06d8f1a0 with the default
// constructor's three stores inlined.
IServerImpl Server;

// ============================================================================
// Exported functions
// ============================================================================

extern "C" {

// Retail ordinal 112, 0x06d8f1c0.
IServer* __cdecl GetServerInterface() {
    return &Server;
}

// Retail ordinal 110, 0x06d8f1d0: .data:0x06dbbd2c (Server + 4).
void* __cdecl GetChatServerInterface() {
    return static_cast<IChatServer*>(&Server);
}

// Retail ordinal 111, 0x06d8eeb0: .data:Client, the object Server.dll receives (Startup
// hands it rpclocal's own module path). The proxy is in rpc_client_proxy.cpp.
IClient* __cdecl GetClientInterface() {
    return RpcGetClientProxy();
}

// Retail ordinal 109: &DAT_06dbbce0 == Client + 4 -- the IChatClient sub-object of the
// very same instance, one slot wide.
void* __cdecl GetChatClientInterface() {
    return RpcGetChatClientProxy();
}

} // extern "C"

// ============================================================================
// DllMain
// ============================================================================
// Retail 0x06d8ed90: only DLL_PROCESS_ATTACH does anything (store our HMODULE in
// DAT_06dbbccc, DisableThreadLibraryCalls); returns 1. PROCESS_DETACH is a no-op.
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)lpReserved;
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_hSelfModule = hModule;              // DAT_06dbbccc
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}
