// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_wire_fields.cpp -- the PLAIN FIELD-LIST packet codec slots.
//
// Seventeen Encode/Decode vtable pairs covering twenty-three packet types. Every body is
// the same skeleton -- routing header, a fixed list of fixed-width fields, the statistics
// tail -- and differs only in the width and order of that list. None allocates, calls out
// or branches beyond the per-field bounds check.
//
// Bodies are tied to types by the vtable each registry constructor stores (the XREF header
// under-reports): slot +0x04 is the encoder, +0x08 the decoder for all 162 types. Slot
// +0x00 has 51 distinct values (FUN_06d9af50, the shared scalar deleting destructor, fills
// 111); slot +0x0c has 12 (150 types share 06d8eae0, a bare `RET 0x4` dump the linker
// folded onto IServerImpl::GetServerStats; the rest are real ostream dumps, e.g.
// LAB_06dafab0 for ch3 0x06 / 0x3a).
//
// Near-identical bodies are not folded: seven 133-instruction bodies differ from
// LAB_06dae620 only in field widths, and two of them (LAB_06da67a0, LAB_06dab200) swap
// their fields on the wire relative to the payload struct.
//
// Every encoder's `if (result) return 0x16` after FUN_06da1380 is dead in retail; the port
// uses PutHeaderUnchecked with no check after it.
//
// In single player packets are queued by pointer and neither slot is called.

#include "rpc_containers.h"
#include "rpc_payloads.h"

// ===========================================================================
// dword, word, dword -- LAB_06dae620 / LAB_06da78d0.
//   ch1 0x14 FLPACKET_COMMON_JETTISONCARGO        (FPayloadCommonJettisonCargo)
//   ch3 0x3d FLPACKET_CLIENT_BADLANDSOBJCOLLISION (FPayloadClientBadLandsObjCollision)
// Different fields, same layout, so one retail body serves both; marshalled through the
// ch1 struct.
// ===========================================================================
unsigned int RpcEncodeWireDwordWordDword(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadCommonJettisonCargo* Wire =
        reinterpret_cast<const FPayloadCommonJettisonCargo*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->ObjectId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutWord(Wire->ItemId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->Count);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeWireDwordWordDword(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength) {
    FPayloadCommonJettisonCargo* Wire =
        reinterpret_cast<FPayloadCommonJettisonCargo*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int ScratchDword = 0;
    unsigned short ScratchWord = 0;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->ObjectId = ScratchDword;
    if (!Stream.GetWord(ScratchWord)) {
        return Stream.GetError();
    }
    Wire->ItemId = ScratchWord;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->Count = ScratchDword;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// dword, dword, word -- LAB_06daaf90 / LAB_06dacea0.
//   ch2 0x33 FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE (FPayloadServerSetMissionObjectiveState)
//   ch2 0x4a FLPACKET_SERVER_SETGUIDEDTARGET          (FPayloadServerSetGuidedTarget)
// ===========================================================================
unsigned int RpcEncodeWireDwordDwordWord(FPacketBase& Packet, unsigned char** BufferHolder,
                                         unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerSetMissionObjectiveState* Wire =
        reinterpret_cast<const FPayloadServerSetMissionObjectiveState*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->MissionId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->ObjectiveIndex);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutWord(Wire->State);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeWireDwordDwordWord(FPacketBase& Packet, const unsigned char* Buffer,
                                         unsigned int Size, unsigned int* OutLength) {
    FPayloadServerSetMissionObjectiveState* Wire =
        reinterpret_cast<FPayloadServerSetMissionObjectiveState*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int ScratchDword = 0;
    unsigned short ScratchWord = 0;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->MissionId = ScratchDword;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->ObjectiveIndex = ScratchDword;
    if (!Stream.GetWord(ScratchWord)) {
        return Stream.GetError();
    }
    Wire->State = ScratchWord;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// dword, word, word -- LAB_06db19b0 / LAB_06dad690.
//   ch2 0x51 FLPACKET_SERVER_USE_ITEM         (FPayloadServerUseItem)
//   ch3 0x40 FLPACKET_CLIENT_REQUEST_USE_ITEM (FPayloadClientUseItem)
// ===========================================================================
unsigned int RpcEncodeWireDwordWordWord(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerUseItem* Wire =
        reinterpret_cast<const FPayloadServerUseItem*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->ObjectId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutWord(Wire->EquipId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutWord(Wire->Count);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeWireDwordWordWord(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength) {
    FPayloadServerUseItem* Wire =
        reinterpret_cast<FPayloadServerUseItem*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int ScratchDword = 0;
    unsigned short ScratchWord = 0;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->ObjectId = ScratchDword;
    if (!Stream.GetWord(ScratchWord)) {
        return Stream.GetError();
    }
    Wire->EquipId = ScratchWord;
    if (!Stream.GetWord(ScratchWord)) {
        return Stream.GetError();
    }
    Wire->Count = ScratchWord;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// dword, dword, word, word -- LAB_06da8b50 / LAB_06da8cd0.
//   ch1 0x04 FLPACKET_COMMON_SETTARGET     (FPayloadCommonSetTarget)
//   ch2 0x38 FLPACKET_SERVER_ITEMTRACTORED (FPayloadServerItemTractored)
// The 32-bit ids lead and the 16-bit sub-object ids follow (the reorder recorded on
// FPayloadCommonSetTarget).
// ===========================================================================
unsigned int RpcEncodeWireDwordDwordWordWord(FPacketBase& Packet, unsigned char** BufferHolder,
                                             unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadCommonSetTarget* Wire =
        reinterpret_cast<const FPayloadCommonSetTarget*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->ObjectId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->TargetObjectId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutWord(Wire->SubObjId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutWord(Wire->TargetSubObjId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeWireDwordDwordWordWord(FPacketBase& Packet, const unsigned char* Buffer,
                                             unsigned int Size, unsigned int* OutLength) {
    FPayloadCommonSetTarget* Wire =
        reinterpret_cast<FPayloadCommonSetTarget*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int ScratchDword = 0;
    unsigned short ScratchWord = 0;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->ObjectId = ScratchDword;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->TargetObjectId = ScratchDword;
    if (!Stream.GetWord(ScratchWord)) {
        return Stream.GetError();
    }
    Wire->SubObjId = ScratchWord;
    if (!Stream.GetWord(ScratchWord)) {
        return Stream.GetError();
    }
    Wire->TargetSubObjId = ScratchWord;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// four flat dwords -- LAB_06daffc0 / LAB_06d9ab00.
//   ch3 0x12 FLPACKET_CLIENT_GFGOODBUY    (FPayloadClientDwordQuad)
//   ch3 0x37 FLPACKET_CLIENT_COMMCOMPLETE (FPayloadClientDwordQuad)
// Not the same body as RpcEncodeWireDwordBlock12 (ch2 0x31 / 0x53 / ch3 0x30, LAB_06db14f0
// / LAB_06dad820), which is three dwords behind a leading id.
// ===========================================================================
unsigned int RpcEncodeWireFourDwords(FPacketBase& Packet, unsigned char** BufferHolder,
                                     unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadClientDwordQuad* Wire =
        reinterpret_cast<const FPayloadClientDwordQuad*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->Field_0x00);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->Field_0x04);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->Field_0x08);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->Field_0x0c);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeWireFourDwords(FPacketBase& Packet, const unsigned char* Buffer,
                                     unsigned int Size, unsigned int* OutLength) {
    FPayloadClientDwordQuad* Wire =
        reinterpret_cast<FPayloadClientDwordQuad*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x00 = Scratch;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x04 = Scratch;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x08 = Scratch;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x0c = Scratch;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// dword, byte, byte -- LAB_06db1b60 / LAB_06da6a50.
//   ch1 0x0e FLPACKET_COMMON_ACTIVATECRUISE   (FPayloadCommonActivateCruise)
//   ch3 0x44 FLPACKET_CLIENT_REQINVINCIBILITY (FPayloadClientInvincibility)
// The two bytes go out in payload order, +0x04 then +0x05, keeping REQINVINCIBILITY's
// reason/bool crossing intact on the wire.
// ===========================================================================
unsigned int RpcEncodeWireDwordTwoBytes(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadCommonActivateCruise* Wire =
        reinterpret_cast<const FPayloadCommonActivateCruise*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->ObjectId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutByte(Wire->bActivate);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutByte(Wire->bDisruptCruise);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeWireDwordTwoBytes(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength) {
    FPayloadCommonActivateCruise* Wire =
        reinterpret_cast<FPayloadCommonActivateCruise*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int ScratchDword = 0;
    unsigned char ScratchByte = 0;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->ObjectId = ScratchDword;
    if (!Stream.GetByte(ScratchByte)) {
        return Stream.GetError();
    }
    Wire->bActivate = ScratchByte;
    if (!Stream.GetByte(ScratchByte)) {
        return Stream.GetError();
    }
    Wire->bDisruptCruise = ScratchByte;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch1 0x08 FLPACKET_COMMON_ACTIVATEEQUIP -- LAB_06da67a0 / LAB_06da68f0.
//
// The wire reorders: payload {ObjectId +0x00, bActivate +0x04, EquipId +0x06} goes out as
// ObjectId, EquipId, bActivate; the decoder reads the same order.
// ===========================================================================
unsigned int RpcEncodeCommonActivateEquip(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadCommonActivateEquip* Wire =
        reinterpret_cast<const FPayloadCommonActivateEquip*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->ObjectId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutWord(Wire->EquipId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutByte(Wire->bActivate);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeCommonActivateEquip(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength) {
    FPayloadCommonActivateEquip* Wire =
        reinterpret_cast<FPayloadCommonActivateEquip*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int ScratchDword = 0;
    unsigned short ScratchWord = 0;
    unsigned char ScratchByte = 0;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->ObjectId = ScratchDword;
    if (!Stream.GetWord(ScratchWord)) {
        return Stream.GetError();
    }
    Wire->EquipId = ScratchWord;
    if (!Stream.GetByte(ScratchByte)) {
        return Stream.GetError();
    }
    Wire->bActivate = ScratchByte;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch1 0x0f FLPACKET_COMMON_GOTRADELANE -- LAB_06da6b80 / LAB_06da6cf0.
// Three dwords then a byte, payload order throughout.
// ===========================================================================
unsigned int RpcEncodeCommonGoTradelane(FPacketBase& Packet, unsigned char** BufferHolder,
                                        unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadCommonGoTradelane* Wire =
        reinterpret_cast<const FPayloadCommonGoTradelane*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->ObjectId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->TradelaneRing1);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->TradelaneRing2);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutByte(Wire->bMidLane);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeCommonGoTradelane(FPacketBase& Packet, const unsigned char* Buffer,
                                        unsigned int Size, unsigned int* OutLength) {
    FPayloadCommonGoTradelane* Wire =
        reinterpret_cast<FPayloadCommonGoTradelane*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int ScratchDword = 0;
    unsigned char ScratchByte = 0;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->ObjectId = ScratchDword;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->TradelaneRing1 = ScratchDword;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->TradelaneRing2 = ScratchDword;
    if (!Stream.GetByte(ScratchByte)) {
        return Stream.GetError();
    }
    Wire->bMidLane = ScratchByte;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x0a FLPACKET_SERVER_ACTIVATEOBJECT -- LAB_06da8e30 / LAB_06da8f80.
// dword, byte, dword -- payload order.
// ===========================================================================
unsigned int RpcEncodeServerActivateObject(FPacketBase& Packet, unsigned char** BufferHolder,
                                           unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerActivateObject* Wire =
        reinterpret_cast<const FPayloadServerActivateObject*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->ObjectId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutByte(Wire->bActivate);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->ActivateParam);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerActivateObject(FPacketBase& Packet, const unsigned char* Buffer,
                                           unsigned int Size, unsigned int* OutLength) {
    FPayloadServerActivateObject* Wire =
        reinterpret_cast<FPayloadServerActivateObject*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int ScratchDword = 0;
    unsigned char ScratchByte = 0;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->ObjectId = ScratchDword;
    if (!Stream.GetByte(ScratchByte)) {
        return Stream.GetError();
    }
    Wire->bActivate = ScratchByte;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->ActivateParam = ScratchDword;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x47 FLPACKET_SERVER_CFGINTERFACENOTIFICATION -- LAB_06dab200 / LAB_06dab350.
//
// The second reordering body: payload {dword +0x00, dword +0x04, byte +0x08} goes out as
// +0x00, +0x08, +0x04 (the byte overtakes the second dword); the decoder mirrors it.
// ===========================================================================
unsigned int RpcEncodeServerCfgInterfaceNotification(FPacketBase& Packet,
                                                     unsigned char** BufferHolder,
                                                     unsigned int Capacity,
                                                     unsigned int* OutLength) {
    const FPayloadServerCfgInterfaceNotification* Wire =
        reinterpret_cast<const FPayloadServerCfgInterfaceNotification*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->NotifType);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutByte(Wire->bEnable);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->Param);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerCfgInterfaceNotification(FPacketBase& Packet,
                                                     const unsigned char* Buffer,
                                                     unsigned int Size,
                                                     unsigned int* OutLength) {
    FPayloadServerCfgInterfaceNotification* Wire =
        reinterpret_cast<FPayloadServerCfgInterfaceNotification*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int ScratchDword = 0;
    unsigned char ScratchByte = 0;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->NotifType = ScratchDword;
    if (!Stream.GetByte(ScratchByte)) {
        return Stream.GetError();
    }
    Wire->bEnable = ScratchByte;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->Param = ScratchDword;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x14 FLPACKET_CLIENT_MISSIONRESPONSE -- LAB_06daeec0 / LAB_06daf010.
// dword, dword, byte -- payload order.
// ===========================================================================
unsigned int RpcEncodeClientMissionResponse(FPacketBase& Packet, unsigned char** BufferHolder,
                                            unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadClientMissionResponse* Wire =
        reinterpret_cast<const FPayloadClientMissionResponse*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->OfferId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->MissionId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutByte(Wire->bAccepted);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeClientMissionResponse(FPacketBase& Packet, const unsigned char* Buffer,
                                            unsigned int Size, unsigned int* OutLength) {
    FPayloadClientMissionResponse* Wire =
        reinterpret_cast<FPayloadClientMissionResponse*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int ScratchDword = 0;
    unsigned char ScratchByte = 0;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->OfferId = ScratchDword;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->MissionId = ScratchDword;
    if (!Stream.GetByte(ScratchByte)) {
        return Stream.GetError();
    }
    Wire->bAccepted = ScratchByte;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x44 FLPACKET_SERVER_REQUEST_RETURNED -- LAB_06daa820 / LAB_06daa990.
// dword then three consecutive bytes (+0x04 / +0x05 / +0x06), raw on the wire; the
// signed/unsigned split is the receiver's.
// ===========================================================================
unsigned int RpcEncodeServerRequestReturned(FPacketBase& Packet, unsigned char** BufferHolder,
                                            unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerRequestReturned* Wire =
        reinterpret_cast<const FPayloadServerRequestReturned*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->ShipId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutByte(Wire->RequestType);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutByte((unsigned char)Wire->Response);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutByte((unsigned char)Wire->Extra);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerRequestReturned(FPacketBase& Packet, const unsigned char* Buffer,
                                            unsigned int Size, unsigned int* OutLength) {
    FPayloadServerRequestReturned* Wire =
        reinterpret_cast<FPayloadServerRequestReturned*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int ScratchDword = 0;
    unsigned char ScratchByte = 0;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->ShipId = ScratchDword;
    if (!Stream.GetByte(ScratchByte)) {
        return Stream.GetError();
    }
    Wire->RequestType = ScratchByte;
    if (!Stream.GetByte(ScratchByte)) {
        return Stream.GetError();
    }
    Wire->Response = (signed char)ScratchByte;
    if (!Stream.GetByte(ScratchByte)) {
        return Stream.GetError();
    }
    Wire->Extra = (signed char)ScratchByte;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x32 FLPACKET_CLIENT_REQUEST_CANCEL -- LAB_06daa560 / LAB_06daa6d0.
// byte, dword, dword, byte -- payload order; the leading field is the one-byte truncation
// recorded in rpc_payloads.h.
// ===========================================================================
unsigned int RpcEncodeClientRequestCancel(FPacketBase& Packet, unsigned char** BufferHolder,
                                          unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadClientRequestCancel* Wire =
        reinterpret_cast<const FPayloadClientRequestCancel*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutByte(Wire->EventType);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->ShipId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->TargetId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutByte(Wire->Field_0x0c);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeClientRequestCancel(FPacketBase& Packet, const unsigned char* Buffer,
                                          unsigned int Size, unsigned int* OutLength) {
    FPayloadClientRequestCancel* Wire =
        reinterpret_cast<FPayloadClientRequestCancel*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int ScratchDword = 0;
    unsigned char ScratchByte = 0;
    if (!Stream.GetByte(ScratchByte)) {
        return Stream.GetError();
    }
    Wire->EventType = ScratchByte;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->ShipId = ScratchDword;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->TargetId = ScratchDword;
    if (!Stream.GetByte(ScratchByte)) {
        return Stream.GetError();
    }
    Wire->Field_0x0c = ScratchByte;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x27 FLPACKET_SERVER_REQUESTCREATESHIPRESP -- LAB_06da7fe0 / LAB_06da8100.
// byte then dword.
// ===========================================================================
unsigned int RpcEncodeServerRequestCreateShipResp(FPacketBase& Packet,
                                                  unsigned char** BufferHolder,
                                                  unsigned int Capacity,
                                                  unsigned int* OutLength) {
    const FPayloadServerRequestCreateShipResp* Wire =
        reinterpret_cast<const FPayloadServerRequestCreateShipResp*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutByte(Wire->bSuccess);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutDword(Wire->ShipId);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerRequestCreateShipResp(FPacketBase& Packet,
                                                  const unsigned char* Buffer,
                                                  unsigned int Size,
                                                  unsigned int* OutLength) {
    FPayloadServerRequestCreateShipResp* Wire =
        reinterpret_cast<FPayloadServerRequestCreateShipResp*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int ScratchDword = 0;
    unsigned char ScratchByte = 0;
    if (!Stream.GetByte(ScratchByte)) {
        return Stream.GetError();
    }
    Wire->bSuccess = ScratchByte;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->ShipId = ScratchDword;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x25 FLPACKET_CLIENT_DBGDESTROYOBJECT -- LAB_06db12c0 / LAB_06db13e0.
// dword then word. No sender in release (only the ctor FUN_06da0f90 writes 0x30025; the
// dispatcher answers with an FDUMP), so only widths are known. The remaining two payload
// bytes are never touched.
// ===========================================================================
unsigned int RpcEncodeClientDbgDestroyObject(FPacketBase& Packet, unsigned char** BufferHolder,
                                             unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadClientDbgDestroyObject* Wire =
        reinterpret_cast<const FPayloadClientDbgDestroyObject*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->Field_0x00);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutWord(Wire->Field_0x04);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeClientDbgDestroyObject(FPacketBase& Packet, const unsigned char* Buffer,
                                             unsigned int Size, unsigned int* OutLength) {
    FPayloadClientDbgDestroyObject* Wire =
        reinterpret_cast<FPayloadClientDbgDestroyObject*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int ScratchDword = 0;
    unsigned short ScratchWord = 0;
    if (!Stream.GetDword(ScratchDword)) {
        return Stream.GetError();
    }
    Wire->Field_0x00 = ScratchDword;
    if (!Stream.GetWord(ScratchWord)) {
        return Stream.GetError();
    }
    Wire->Field_0x04 = ScratchWord;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x2a FLPACKET_SERVER_ADJUSTATTITUDE -- LAB_06daa0c0 encode only.
//
// The decoder slot is LAB_06d95d40, the dword-pair family's RpcDecodeWireDwordPair, so
// only the encoder is here. It equals the family's encoder LAB_06da9110 except that the
// second field moves through FLD/FSTP: Attitude is a float.
// ===========================================================================
unsigned int RpcEncodeServerAdjustAttitude(FPacketBase& Packet, unsigned char** BufferHolder,
                                           unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadServerAdjustAttitude* Wire =
        reinterpret_cast<const FPayloadServerAdjustAttitude*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->VibeHandle);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutFloat(Wire->Attitude);                                 // FLD / 06daa16a FSTP
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch2 0x08 FLPACKET_SERVER_CHARSELECTVERIFIED -- LAB_06dabdf0 / LAB_06dabf10.
//
// The payload hole at +0x04 is absent from the wire: dword +0x00, then eight bytes from
// +0x08 (2 + 4 + 8 = 14 bytes). The encoder moves the eight bytes as a double through
// FLD/FSTP (which quiets a signalling NaN); the decoder reads them back as two plain
// dwords. Both reproduced.
// ===========================================================================
unsigned int RpcEncodeServerCharSelectVerified(FPacketBase& Packet,
                                               unsigned char** BufferHolder,
                                               unsigned int Capacity,
                                               unsigned int* OutLength) {
    const FPayloadServerCharSelectVerified* Wire =
        reinterpret_cast<const FPayloadServerCharSelectVerified*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->VibeHandle);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    // payload+0x08..+0x0f, reserved in one advance of 8, then written without a second
    // check.
    if (!Stream.Reserve(8, FRpcMarshalStream::kEncodeOverflow)) {
        return Stream.GetError();
    }
    // Retail's FLD/FSTP double copy, kept so a signalling NaN is quieted as in retail.
    const void* DoubleSource = &Wire->ServerTimeLo;
    void* DoubleDest = reinterpret_cast<void*>(Packet.MarshalPtr);
    __asm {
        mov eax, DoubleSource
        fld qword ptr [eax]
        mov eax, DoubleDest
        fstp qword ptr [eax]
    }
    Packet.MarshalPtr += 8u;
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

unsigned int RpcDecodeServerCharSelectVerified(FPacketBase& Packet,
                                               const unsigned char* Buffer,
                                               unsigned int Size,
                                               unsigned int* OutLength) {
    FPayloadServerCharSelectVerified* Wire =
        reinterpret_cast<FPayloadServerCharSelectVerified*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->VibeHandle = Scratch;
    // One eight-byte reservation, then both dwords out of that window; the second has no
    // bounds check of its own.
    if (!Stream.Reserve(8, FRpcMarshalStream::kDecodeOverrun)) {
        return Stream.GetError();
    }
    if (!Stream.GetDwordUncounted(Scratch)) {
        return Stream.GetError();
    }
    Wire->ServerTimeLo = Scratch;
    if (!Stream.GetDwordUncounted(Scratch)) {
        return Stream.GetError();
    }
    Wire->ServerTimeHi = Scratch;
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// ===========================================================================
// ch3 0x2d FLPACKET_CLIENT_DBGCONSOLE -- {dword, 128 raw bytes}.
// Encoder LAB_06db1670 (122 instructions), decoder LAB_06db17a0 (108).
//
// The pair is unshared (.rdata:06db5db8 / 06db5dbc are its only referents). Two other
// bodies do serve two types each: LAB_06daf830 (ch3 0x3a and 0x06) and LAB_06dacca0 (ch2
// 0x48 and ch3 0x36), both in rpc_wire_varlen.cpp.
//
// The 128 bytes are one reservation and one copy, not 32 dword fields, so a short buffer
// fails before any byte is written. The trailing FUN_06da13c0 footer cannot fail.
// ===========================================================================
unsigned int RpcEncodeClientDbgConsole(FPacketBase& Packet, unsigned char** BufferHolder,
                                       unsigned int Capacity, unsigned int* OutLength) {
    const FPayloadClientDbgConsole* Wire =
        reinterpret_cast<const FPayloadClientDbgConsole*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, *BufferHolder, Capacity);
    Stream.PutHeaderUnchecked(RpcPacketHeaderWord(Packet.PacketId));
    Stream.PutDword(Wire->Field_0x00);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    Stream.PutBytes(Wire->Text_0x04, 128u);                            // / 06db1728
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordEncodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}

// LAB_06db17a0 -- Decode. The mirror, with kDecodeOverrun (0x17); its length excludes the
// two routing bytes the dispatcher consumed, so the stats tail adds 2 back.
unsigned int RpcDecodeClientDbgConsole(FPacketBase& Packet, const unsigned char* Buffer,
                                       unsigned int Size, unsigned int* OutLength) {
    FPayloadClientDbgConsole* Wire =
        reinterpret_cast<FPayloadClientDbgConsole*>(Packet.Payload());

    FRpcMarshalStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    unsigned int Scratch = 0;
    if (!Stream.GetDword(Scratch)) {
        return Stream.GetError();
    }
    Wire->Field_0x00 = Scratch;
    if (!Stream.GetBytes(Wire->Text_0x04, 128u)) {                     // 06db1815 / 06db1846
        return Stream.GetError();
    }
    if (Stream.Failed()) {
        return Stream.GetError();
    }
    *OutLength = Stream.Length();
    RpcRecordDecodedPacket(Packet.PacketId, Stream.Length());
    return 0;
}
