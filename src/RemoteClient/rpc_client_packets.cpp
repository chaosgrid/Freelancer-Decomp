// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "rpc_packet.h"
#include "rpc_packet_codec.h"
#include "rpc_server_channel_codec.h"
#include "common_misc.h" // SinglePlayer
#include <map>
#include <string>
#include <vector>
#include <list>
#include <iostream>

namespace {
// Native raw cache: Shutdown does not clear it or add another owning reference.
FPacketSerializer* GRemotePacketSerializer = nullptr;
}

// =================================================================================================
// Base classes (RemoteClient's own copies; it does not link rpclocal)
// =================================================================================================

FRpcRefObject::~FRpcRefObject() {}

unsigned int FRpcRefObject::Release() {
    if (UsageCount == 0) return 0;
    if (--UsageCount == 0) {
        delete this;
        return 0;
    }
    return UsageCount;
}

FPacketBase::FPacketBase(unsigned int InPacketId) {
    Field_0x08 = nullptr;
    PacketId = InPacketId;
    MarshalCursor = 0;
    MarshalError = 0;
}

FPacketBase::~FPacketBase() {}

unsigned int FPacketBase::Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) {
    // Retail 06b4e7b0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    return Stream.FinishEncodeSuccess(OutLength);
}
unsigned int FPacketBase::Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    return Stream.Finish(OutLength, false);
}

void FPacketBase::Dump(std::ostream& Out) const {}

FPacketSerializer* RpcGetRemotePacketSerializer() {
    if (GRemotePacketSerializer == nullptr) {
        GRemotePacketSerializer = new (std::nothrow) FPacketSerializer;
    }
    return GRemotePacketSerializer;
}

// =================================================================================================
// FPacketSerializer Implementation
// =================================================================================================

FPacketSerializer::FPacketSerializer() {
    // Native's dictionary call is inside construction: a throw unwinds the
    // already-constructed Types map, refcount base, then new-expression storage.
    RpcServerBuildHardpointDictionary();
}

FPacketSerializer::~FPacketSerializer() {
    for (auto& Pair : Types) {
        delete Pair.second;
    }
    Types.clear();
}

int FPacketSerializer::RegisterType(unsigned int PacketId, const char* Name, FPacketFactoryFn Factory) {
    if (Types.find(PacketId) != Types.end()) {
        return kPacketRegisterDuplicate;
    }

    FPacketTypeInfo* Info = new FPacketTypeInfo();
    Info->Factory = Factory;
    Info->Name = Name ? Name : "";

    Types[PacketId] = Info;
    return 0;
}

int FPacketSerializer::Allocate(unsigned int Channel, unsigned int Opcode, FPacketBase** OutPacket) {
    if (!OutPacket) return kPacketAllocateNullOut;

    const unsigned int PacketId = ((Channel & 0xffffu) << 16) | (Opcode & 0xffffu);
    std::map<unsigned int, FPacketTypeInfo*>::iterator It = Types.find(PacketId);
    if (It == Types.end()) {
        return kPacketAllocateUnknownType;
    }

    return It->second->Factory(OutPacket);
}
