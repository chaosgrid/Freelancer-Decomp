// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "rpc_packet.h"
#include "rpc_packet_codec.h"
#include "rpc_server_channel_codec.h"
#include <map>
#include "common_ini.h"
#include "common_physys.h"

int RegisterChannel1Packets(FPacketSerializer* Serializer) {
    return RpcRegisterCommonScalarPackets(Serializer);
}

// Serializer and packet-base members that RemoteServer defines itself (rpclocal does not export
// them).

int FPacketSerializer::RegisterType(unsigned int PacketId, const char* Name, FPacketFactoryFn Factory) {
    std::map<unsigned int, FPacketTypeInfo*>::iterator It = Types.lower_bound(PacketId);
    if (It != Types.end() && It->first == PacketId) {
        return 0x42; // kPacketRegisterDuplicate
    }
    FPacketTypeInfo* Info = new FPacketTypeInfo;
    Info->Factory = Factory;
    Info->Name = Name;
    Types.insert(It, std::map<unsigned int, FPacketTypeInfo*>::value_type(PacketId, Info));
    return 0;
}

FPacketBase::FPacketBase(unsigned int InPacketId) : PacketId(InPacketId) {
    MarshalCursor = 0;
    MarshalError = 0;
    Field_0x08 = nullptr;
}
FPacketBase::~FPacketBase() {}
void FPacketBase::Dump(std::ostream& Out) const {}
unsigned int FPacketBase::Encode(unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) {
    // Retail 06bebec0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    return Stream.FinishEncodeSuccess(OutLength);
}
unsigned int FPacketBase::Decode(const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    return Stream.Finish(OutLength, false);
}

int RegisterChannel3Packets(FPacketSerializer* Serializer) {
    return RpcRegisterClientScalarPackets(Serializer);
}

int RegisterChannel2Packets(FPacketSerializer* Serializer) {
    return RpcRegisterRemoteServerServerPackets(Serializer);
}
