// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Retail IServerImpl::HandlePacket (0x06d81170), the whole server -> client half of
// rpclocal.
//
// Channel = header >> 16, opcode = header & 0xfff. Channel 1 switches over opcodes
// 1..0x1d, channel 2 over 1..0x5b; anything else is dropped. Each case deserializes the
// payload that starts at packet+0x20 and calls the real IClient inside freelancer.exe
// (.data:0x06dbbcd8), or the chat client (.data:0x06dbbcd4) for channel 1 opcode 5. The
// cases themselves are the Deliver column of GChannel1Codecs / GChannel2Codecs
// (rpc_payload_ch1.cpp, rpc_payload_ch2.cpp).

#include "rpc_transport.h"
#include "rpc_payloads.h"
#include "rpc_iclient.h"
#include "common_server_types.h"

// ---------------------------------------------------------------------------
// retail IServerImpl::HandlePacket (0x06d81170)
// ---------------------------------------------------------------------------
bool RpcHandleIncomingPacket(FPacketBase* Packet) {
    // No packet test. Jump tables: channel 1 at .text:06d83cb0, channel 2 at .text:06d83b44;
    // every slot holding 06d83b1f is the default branch.
    //
    // Every non-default slot of both tables has exactly one Deliver in GChannel1Codecs /
    // GChannel2Codecs, and none of the default slots has one (ch1 defaults 3,6,7,9,a,b,c,d;
    // ch2 defaults 9,c,12,18,1b,1c,36,3a,3b,50; 21 + 81 = 102 codecs). A missing codec
    // therefore IS the default branch. Each case calls through the real client untested,
    // so no null test here either.
    const FRpcPayloadCodec* Codec = RpcFindPayloadCodec(Packet->GetChannel(), Packet->GetOpcode());
    if (Codec == nullptr) {
        return false;
    }
    return Codec->Deliver(Packet, GRealClient, GRealChatClient);
}
