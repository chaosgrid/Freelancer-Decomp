// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "rpc_packet.h"
#include "rpc_equipment_codec.h" // One shared owning equipment handle and packet base.
#include "common_archetype.h" // Canonical CollisionGroupDesc.
#include "common_ini.h" // Vector; packet fields use native C++ containers.
#include "common_physys.h" // Quaternion; shared Common math type.
#include "common_server_types.h" // CHARACTER_ID has one canonical declaration.
#include <cstring>
#include <intrin.h> // __movsd/__movsb: native REP MOVS encode block copies.
#include <new>
#include <list>
#include <vector>

struct FmtStr;

// Shared native implementation of the wire operations compiled into both remote
// DLLs. Only scalar wire fields are copied: STL object representations never enter
// the stream. RemoteClient 06b4e740 / RemoteServer 06bebe50 write the same header.
struct FRpcWireStats {
    unsigned int EncodedCount = 0;
    unsigned int DecodedCount = 0;
    unsigned int EncodedAverage = 0;
    unsigned int DecodedAverage = 0;
    unsigned int EncodedMaximum = 0;
    unsigned int DecodedMaximum = 0;
    unsigned int EncodedBytes = 0;
    unsigned int DecodedBytes = 0;
    // Native initializes both doubles to zero; neither Remote DLL writes them.
    double ExecTimeTotal = 0.0;
    double ExecTimeAvg = 0.0;
};

std::map<unsigned int, FRpcWireStats>& RpcRemoteWireStats();
FRpcWireStats* RpcFindRemoteWireStats(unsigned int PacketId);

// RemoteClient 06b5bf53..06b5bf89 and 06b5c021..06b5c05e.
inline void RpcWireRecord(unsigned int PacketId, unsigned int Length, bool bEncode) {
    FRpcWireStats& Stats = *RpcFindRemoteWireStats(PacketId);
    if (bEncode) {
        ++Stats.EncodedCount;
        Stats.EncodedBytes += Length;
        if (Length > Stats.EncodedMaximum) Stats.EncodedMaximum = Length;
        Stats.EncodedAverage = Stats.EncodedBytes / Stats.EncodedCount;
    } else {
        ++Stats.DecodedCount;
        Stats.DecodedBytes += Length + 2u;
        // Preserve retail's comparison: payload length is compared with maximum+2.
        if (Length > Stats.DecodedMaximum + 2u) Stats.DecodedMaximum = Length + 2u;
        Stats.DecodedAverage = Stats.DecodedBytes / Stats.DecodedCount;
    }
}

class FRpcWireStream {
public:
    FRpcWireStream(FPacketBase& InPacket, void* Buffer, unsigned int Capacity)
        : Packet(InPacket) {
        Packet.MarshalCursor = 0;
        Packet.MarshalLimit = Capacity;
        Packet.MarshalPtr = reinterpret_cast<unsigned int>(Buffer);
        Packet.MarshalError = 0;
    }

    // Encode entry, identical in every native Encode body (e.g. RC06b55430..06b5544e,
    // RS06c03650.., base RC06b4e7b0..06b4e7c8): Limit is stored BEFORE *BufferHolder
    // is read, then Ptr, Cursor=0 and Error=0. Decode keeps the constructor above.
    FRpcWireStream(FPacketBase& InPacket, unsigned char** BufferHolder, unsigned int Capacity)
        : Packet(InPacket) {
        Packet.MarshalLimit = Capacity;
        Packet.MarshalPtr = reinterpret_cast<unsigned int>(*BufferHolder);
        Packet.MarshalCursor = 0;
        Packet.MarshalError = 0;
    }

    // Header helper has no capacity check in retail (06b4e740 / 06bebe50). It reads
    // PacketId, snapshots the destination once, writes the WORD, stores Cursor=2 and
    // commits Ptr=cached+2; its literal zero result is then stored to MarshalError by
    // every caller (e.g. RC06b55465/06b55467), LAST. The transport supplies room for
    // this fixed header before entering a codec.
    void PutHeader() {
        const unsigned short Header = static_cast<unsigned short>(
            (Packet.PacketId & 0xfffu) | (((Packet.PacketId >> 16) & 0xffu) << 8));
        const unsigned int Destination = Packet.MarshalPtr;
        std::memcpy(reinterpret_cast<void*>(Destination), &Header, 2u);
        Packet.MarshalCursor = 2u;
        Packet.MarshalPtr = Destination + 2u;
        Packet.MarshalError = 0;
    }

    // Native inline field write (e.g. RC06b55477..06b554b2): Advance, then snapshot the
    // destination, copy, and commit Ptr=cached+Width. The cursor is never restored.
    bool Put(const void* Value, unsigned int Width) {
        if (!Advance(Width, kPacketEncodeOverflow)) return false;
        const unsigned int Destination = Packet.MarshalPtr;
        if (Width != 0) std::memcpy(reinterpret_cast<void*>(Destination), Value, Width);
        Packet.MarshalPtr = Destination + Width;
        return true;
    }

    // PLAYER_TRADE may count a DWORD conditionally while physically accessing it
    // in both directions. These operations do not introduce a synthetic check.
    bool Reserve(unsigned int Width, unsigned int Error) { return Advance(Width, Error); }
    // Encode-only copy into a span reserved by a separate Reserve (e.g. SAVEGAME
    // RC06b55748..06b55772): snapshot the destination once, copy forward as native
    // REP MOVSD of Width/4 DWORDs then REP MOVSB of Width&3 bytes (exact even when
    // source and destination overlap), then commit Ptr=cached+Width. A zero width
    // performs no access.
    void PutReserved(const void* Value, unsigned int Width) {
        const unsigned int Destination = Packet.MarshalPtr;
        const unsigned int DwordBytes = Width & ~3u;
        __movsd(reinterpret_cast<unsigned long*>(Destination),
            static_cast<const unsigned long*>(Value), Width >> 2);
        __movsb(reinterpret_cast<unsigned char*>(Destination + DwordBytes),
            static_cast<const unsigned char*>(Value) + DwordBytes, Width & 3u);
        Packet.MarshalPtr = Destination + Width;
    }

    // Variable-length encode block (e.g. COMMON11 RC06b65ed2..06b65f25): reserve,
    // then the cached REP copy and commit above. Fixed scalar fields use Put.
    bool PutBlock(const void* Value, unsigned int Width) {
        if (!Advance(Width, kPacketEncodeOverflow)) return false;
        PutReserved(Value, Width);
        return true;
    }
    void PutUncounted(const void* Value, unsigned int Width) {
        std::memcpy(reinterpret_cast<void*>(Packet.MarshalPtr), Value, Width);
        Packet.MarshalPtr += Width;
    }
    void GetUncounted(void* Value, unsigned int Width) {
        std::memcpy(Value, reinterpret_cast<const void*>(Packet.MarshalPtr), Width);
        Packet.MarshalPtr += Width;
    }

    // SETHULLSTATUS uses FLD/FSTP on encode (06b62d62/65,06bf8f42/45),
    // whereas its decoder uses integer MOV. Keep x87 signaling-NaN quieting and
    // exception behavior as well as ordinary float32 bytes.
    bool PutFloat(const float& Value) {
        if (!Advance(4u, kPacketEncodeOverflow)) return false;
        const float* Source = &Value;
        const unsigned int Cached = Packet.MarshalPtr;
        float* Destination = reinterpret_cast<float*>(Cached);
        __asm {
            mov eax, Source
            mov edx, Destination
            fld dword ptr [eax]
            fstp dword ptr [edx]
        }
        Packet.MarshalPtr = Cached + 4u;
        return true;
    }

    bool Get(void* Value, unsigned int Width) {
        if (!Advance(Width, kPacketDecodeOverrun)) return false;
        if (Width != 0) std::memcpy(Value, reinterpret_cast<const void*>(Packet.MarshalPtr), Width);
        Packet.MarshalPtr += Width;
        return true;
    }

    // Some FIREWEAPON x87 branches commit the moving pointer only after FSTP;
    // others commit before it. Keep that ordering on an unmasked FP exception.
    bool GetBeforePointerCommit(void* Value, unsigned int Width) {
        if (!Advance(Width, kPacketDecodeOverrun)) return false;
        std::memcpy(Value, reinterpret_cast<const void*>(Packet.MarshalPtr), Width);
        return true;
    }

    void CommitReadPointer(unsigned int Width) { Packet.MarshalPtr += Width; }

    unsigned int Finish(unsigned int* OutLength, bool bEncode) {
        if (Packet.MarshalError != 0) return Packet.MarshalError;
        *OutLength = Packet.MarshalCursor;
        RpcWireRecord(Packet.PacketId, Packet.MarshalCursor, bEncode);
        return 0;
    }

    // Native Encode success tail (RC trailer 06b4e790 / RS 06bebea0 is XOR EAX,EAX;
    // RET 0c, e.g. RC06b554b4..06b554d7): MarshalError = 0 unconditionally, then
    // *OutLength = Cursor, statistics, literal 0. Finish reads Error only before
    // the OutLength store, so OutLength aliasing Error still records and returns 0.
    unsigned int FinishEncodeSuccess(unsigned int* OutLength) {
        Packet.MarshalError = 0;
        return Finish(OutLength, true);
    }

private:
    bool Advance(unsigned int Width, unsigned int Error) {
        Packet.MarshalCursor += Width;
        if (Packet.MarshalCursor > Packet.MarshalLimit) Packet.MarshalError = Error;
        return Packet.MarshalError == 0;
    }

    FPacketBase& Packet;
};

// The caller has already initialized Stream for Packet. These helpers never
// reset marshal state; error handling and ownership remain with each caller.
unsigned int RpcWireDecodeOwnedBlock(FPacketBase& Packet, FRpcWireStream& Stream,
    unsigned int& BlockSize, unsigned char*& Data);
unsigned int RpcGetFlattenedFmtStrSize(const FmtStr& Text);
unsigned int RpcWireEncodeCollisionGroups(FRpcWireStream& Stream,
    const std::list<CollisionGroupDesc>& Groups);
unsigned int RpcWireDecodeCollisionGroups(FPacketBase& Packet, FRpcWireStream& Stream,
    std::list<CollisionGroupDesc>& Groups);

template<typename T>
inline unsigned int RpcWireEncodeScalar(FPacketBase& Packet, const T& Value,
    unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) {
    // RC06b55430 / RS06c03650 (CLIENT04 and the other integer scalars): a failed
    // reservation returns literal 0x16 without rewriting the sticky Error.
    FRpcWireStream Stream(Packet, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Value, sizeof(T))) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

template<>
inline unsigned int RpcWireEncodeScalar<float>(FPacketBase& Packet, const float& Value,
    unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) {
    // RC06b5aa00 / RS06c07a90 (REQHULLSTATUS): FLD/FSTP into the cached destination.
    FRpcWireStream Stream(Packet, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.PutFloat(Value)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

template<typename T>
inline unsigned int RpcWireDecodeScalar(FPacketBase& Packet, T& Value,
    const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    Stream.Get(&Value, sizeof(T));
    return Stream.Finish(OutLength, false);
}

template<typename T>
inline int RpcWirePacketFactory(FPacketBase** OutPacket) {
    *OutPacket = nullptr;
    T* Packet = new (std::nothrow) T;
    if (Packet == nullptr) return kPacketFactoryNoMemory;
    *OutPacket = Packet;
    return kPacketFactoryOk;
}

// The two remote DLLs instantiate these same native classes. Scalar payloads
// deliberately have no STL layout or packet-relative container representation.
template<unsigned int Id, typename T, bool bZeroInitialValue = true>
class FRpcScalarPacket : public FPacketBase {
public:
    FRpcScalarPacket() : FPacketBase(Id) {
        if (bZeroInitialValue) Value = T();
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override {
        return RpcWireEncodeScalar(*this, Value, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override {
        return RpcWireDecodeScalar(*this, Value, Buffer, Size, OutLength);
    }
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcScalarPacket>(OutPacket);
    }
    T Value;
};

class FRpcConnectResponsePacket : public FRpcScalarPacket<FLPACKET_SERVER_CONNECTRESPONSE, unsigned int> {
public:
    void Dump(std::ostream& Out) const override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcConnectResponsePacket>(OutPacket);
    }
};
class FRpcLoginResponsePacket : public FRpcScalarPacket<FLPACKET_SERVER_LOGINRESPONSE, unsigned char> {
public:
    void Dump(std::ostream& Out) const override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcLoginResponsePacket>(OutPacket);
    }
};
using FRpcSetShipArchPacket = FRpcScalarPacket<FLPACKET_SERVER_SETSHIPARCH, unsigned int>;
using FRpcSetHullStatusPacket = FRpcScalarPacket<FLPACKET_SERVER_SETHULLSTATUS, float>;
using FRpcRequestChangeCashPacket = FRpcScalarPacket<FLPACKET_CLIENT_REQCHANGECASH, int, false>;
using FRpcRequestSetCashPacket = FRpcScalarPacket<FLPACKET_CLIENT_REQSETCASH, int, false>;

class FRpcSaveGamePacket : public FPacketBase {
public:
    FRpcSaveGamePacket();
    ~FRpcSaveGamePacket() override;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    void CopyDescription(const wchar_t* Source, unsigned int Count);
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcSaveGamePacket>(OutPacket);
    }
    CHARACTER_ID CharacterId;
    // Retail deliberately leaves both primitive fields uninitialized.
    wchar_t* Description;
    unsigned int DescriptionCount;
};

class FRpcTradeResponsePacket : public FPacketBase {
public:
    FRpcTradeResponsePacket();
    ~FRpcTradeResponsePacket() override;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    void CopyData(const unsigned char* Source, unsigned int Size);
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcTradeResponsePacket>(OutPacket);
    }
    unsigned int DataSize;
    unsigned char* Data;
};

class FRpcMissionResponsePacket : public FPacketBase {
public:
    FRpcMissionResponsePacket() : FPacketBase(FLPACKET_CLIENT_MISSIONRESPONSE) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcMissionResponsePacket>(OutPacket);
    }
    // TODO: recover the semantic name of the first native DWORD argument.
    unsigned int OfferId;
    unsigned long OfferKind;
    unsigned char bAccepted;
};

using FRpcGfSelectObjectPacket = FRpcScalarPacket<FLPACKET_CLIENT_GFSELECTOBJECT, unsigned int, false>;
using FRpcAbortMissionPacket = FRpcScalarPacket<FLPACKET_CLIENT_ABORT_MISSION, unsigned int, false>;
using FRpcRtcDonePacket = FRpcScalarPacket<FLPACKET_CLIENT_RTCDONE, unsigned int>;
using FRpcMissionSaveBPacket = FRpcScalarPacket<FLPACKET_CLIENT_MISSIONSAVEB, unsigned int, false>;

// Each field has its own cursor reservation and can be committed before a later
// field fails. Pointer lists describe native scalar fields, never object layout.
unsigned int RpcWireEncodeDwords(FPacketBase& Packet, const unsigned int* const* Fields,
    unsigned int FieldCount, unsigned char** BufferHolder, unsigned int Capacity,
    unsigned int* OutLength);
unsigned int RpcWireDecodeDwords(FPacketBase& Packet, unsigned int* const* Fields,
    unsigned int FieldCount, const unsigned char* Buffer, unsigned int Size,
    unsigned int* OutLength);

template<unsigned int Id, bool bZeroFields = false>
class FRpcClientDwordPairPacket : public FPacketBase {
public:
    FRpcClientDwordPairPacket() : FPacketBase(Id) {
        if (bZeroFields) { Field_0x00 = 0; Field_0x04 = 0; }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override {
        const unsigned int* Fields[2] = { &Field_0x00, &Field_0x04 };
        return RpcWireEncodeDwords(*this, Fields, 2u, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override {
        unsigned int* Fields[2] = { &Field_0x00, &Field_0x04 };
        return RpcWireDecodeDwords(*this, Fields, 2u, Buffer, Size, OutLength);
    }
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcClientDwordPairPacket>(OutPacket);
    }
    // TODO: recover semantic names; the native DWORD fields and order are proven.
    unsigned int Field_0x00;
    unsigned int Field_0x04;
};

template<unsigned int Id, bool bZeroFields = false>
class FRpcClientDwordTriplePacket : public FPacketBase {
public:
    FRpcClientDwordTriplePacket() : FPacketBase(Id) {
        if (bZeroFields) { Field_0x00 = 0; Field_0x04 = 0; Field_0x08 = 0; }
    }
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override {
        const unsigned int* Fields[3] = { &Field_0x00, &Field_0x04, &Field_0x08 };
        return RpcWireEncodeDwords(*this, Fields, 3u, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override {
        unsigned int* Fields[3] = { &Field_0x00, &Field_0x04, &Field_0x08 };
        return RpcWireDecodeDwords(*this, Fields, 3u, Buffer, Size, OutLength);
    }
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcClientDwordTriplePacket>(OutPacket);
    }
    // TODO: recover semantic names; no inferred equipment/ship struct is blitted.
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned int Field_0x08;
};

template<unsigned int Id>
class FRpcClientDwordQuadPacket : public FPacketBase {
public:
    FRpcClientDwordQuadPacket() : FPacketBase(Id) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override {
        const unsigned int* Fields[4] = { &Field_0x00, &Field_0x04, &Field_0x08, &Field_0x0c };
        return RpcWireEncodeDwords(*this, Fields, 4u, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override {
        unsigned int* Fields[4] = { &Field_0x00, &Field_0x04, &Field_0x08, &Field_0x0c };
        return RpcWireDecodeDwords(*this, Fields, 4u, Buffer, Size, OutLength);
    }
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcClientDwordQuadPacket>(OutPacket);
    }
    // TODO: recover semantic names of the native record fields.
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned int Field_0x08;
    unsigned int Field_0x0c;
};

using FRpcLaunchCompletePacket = FRpcClientDwordPairPacket<FLPACKET_CLIENT_LAUNCHCOMPLETE>;
using FRpcJumpInCompletePacket = FRpcClientDwordPairPacket<FLPACKET_CLIENT_JUMPINCOMPLETE, true>;
using FRpcGfGoodSellPacket = FRpcClientDwordTriplePacket<FLPACKET_CLIENT_GFGOODSELL>;
using FRpcHailPacket = FRpcClientDwordTriplePacket<FLPACKET_CLIENT_HAIL, true>;
using FRpcGfGoodBuyPacket = FRpcClientDwordQuadPacket<FLPACKET_CLIENT_GFGOODBUY>;
using FRpcCommCompletePacket = FRpcClientDwordQuadPacket<FLPACKET_CLIENT_COMMCOMPLETE>;

using FRpcDockPacket = FRpcScalarPacket<FLPACKET_CLIENT_DOCK, unsigned int, false>;
using FRpcCargoScanPacket = FRpcClientDwordPairPacket<FLPACKET_CLIENT_CARGOSCAN>;
using FRpcGfGoodVaporizedPacket = FRpcClientDwordTriplePacket<FLPACKET_CLIENT_GFGOODVAPORIZED>;

class FRpcMunitionCollisionPacket : public FPacketBase {
public:
    FRpcMunitionCollisionPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcMunitionCollisionPacket>(OutPacket);
    }
    // TODO: semantic names of the neutral native fields are not yet proven.
    unsigned int ProjectileArchId;
    unsigned int AttackerId;
    unsigned int ObjectId;
    unsigned short SubObjId;
    Vector HitPosition;
};

class FRpcObjectCollisionPacket : public FPacketBase {
public:
    FRpcObjectCollisionPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcObjectCollisionPacket>(OutPacket);
    }
    // TODO: semantic names of the neutral native fields are not yet proven.
    unsigned int TargetObjectId;
    unsigned short TargetSubObjId;
    unsigned int ObjectId;
    unsigned short SubObjId;
    float Damage;
};

class FRpcBadlandsObjectCollisionPacket : public FPacketBase {
public:
    FRpcBadlandsObjectCollisionPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcBadlandsObjectCollisionPacket>(OutPacket);
    }
    // TODO: semantic names of the neutral native fields are not yet proven.
    unsigned int BadLandsObjId;
    unsigned short SubObjId;
    unsigned int ObjectId;
};

class FRpcMineAsteroidPacket : public FPacketBase {
public:
    FRpcMineAsteroidPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcMineAsteroidPacket>(OutPacket);
    }
    unsigned int SystemId;
    Vector Position;
    unsigned int LootId;
    unsigned int SecondaryLoot;
    unsigned int Quantity;
};

class FRpcSetManeuverPacket : public FPacketBase {
public:
    FRpcSetManeuverPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcSetManeuverPacket>(OutPacket);
    }
    // TODO: semantic names of the neutral native fields are not yet proven.
    unsigned int ShipFrom;
    unsigned int ObjectId;
    unsigned int Flag;
};

class FRpcRequestEventPacket : public FPacketBase {
public:
    FRpcRequestEventPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcRequestEventPacket>(OutPacket);
    }
    // TODO: semantic names of the neutral native fields are not yet proven.
    unsigned char EventType;
    unsigned int ShipId;
    unsigned int TargetId;
    unsigned int SecondId;
    unsigned char Field_0x10;
};

class FRpcRequestCancelPacket : public FPacketBase {
public:
    FRpcRequestCancelPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcRequestCancelPacket>(OutPacket);
    }
    // TODO: semantic names of the neutral native fields are not yet proven.
    unsigned char EventType;
    unsigned int ShipId;
    unsigned int TargetId;
    unsigned char Field_0x0c;
};


class FRpcRequestEquipmentPacket : public FRpcServerEquipListPacketBase {
public:
    FRpcRequestEquipmentPacket() : FRpcServerEquipListPacketBase(FLPACKET_CLIENT_REQEQUIPMENT) {}
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcRequestEquipmentPacket>(OutPacket);
    }
};

class FRpcRequestCargoPacket : public FRpcServerEquipListPacketBase {
public:
    FRpcRequestCargoPacket() : FRpcServerEquipListPacketBase(FLPACKET_CLIENT_REQCARGO) {}
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcRequestCargoPacket>(OutPacket);
    }
};

class FRpcRequestCollisionGroupsPacket : public FPacketBase {
public:
    FRpcRequestCollisionGroupsPacket() : FPacketBase(FLPACKET_CLIENT_REQCOLLISIONGROUPS) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcRequestCollisionGroupsPacket>(OutPacket);
    }
    // Modern allocation and value-initialized partial nodes differ from retail's
    // uninitialized two-DWORD node copy, including Pad02; retain this explicit gap.
    std::list<CollisionGroupDesc> Groups;
};

using FRpcRequestShipArchPacket = FRpcScalarPacket<FLPACKET_CLIENT_REQSHIPARCH, unsigned int, false>;
using FRpcRequestHullStatusPacket = FRpcScalarPacket<FLPACKET_CLIENT_REQHULLSTATUS, float>;
using FRpcInterfaceItemUsedPacket = FRpcScalarPacket<FLPACKET_CLIENT_INTERFACEITEMUSED, unsigned int, false>;
class FRpcRequestDifficultyScalePacket : public FRpcScalarPacket<FLPACKET_CLIENT_REQDIFFICULTYSCALE, float, false> {
public:
    FRpcRequestDifficultyScalePacket() { Value = 1.0f; }
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcRequestDifficultyScalePacket>(OutPacket);
    }
};

class FRpcRequestRemoveItemPacket : public FPacketBase {
public:
    FRpcRequestRemoveItemPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcRequestRemoveItemPacket>(OutPacket);
    }
    unsigned int EquipId;
    int Count;
};

class FRpcRequestUseItemPacket : public FPacketBase {
public:
    FRpcRequestUseItemPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcRequestUseItemPacket>(OutPacket);
    }
    unsigned int ObjectId;
    unsigned short ItemId;
    unsigned short Count;
};

class FRpcRequestInvincibilityPacket : public FPacketBase {
public:
    FRpcRequestInvincibilityPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcRequestInvincibilityPacket>(OutPacket);
    }
    unsigned int ShipId;
    unsigned char Reason;
    unsigned char bInvincible;
};

// Own the exact byte block the native request carries, including a terminator
// only when its producer supplies one. It is not an implicit C++ string codec.
class FRpcItemChangePacketBase : public FPacketBase {
public:
    explicit FRpcItemChangePacketBase(unsigned int PacketTypeId);
    ~FRpcItemChangePacketBase() override;
    void CopyHardpoint(const void* Data, unsigned int Size);
    int Count;
    float Health;
    unsigned char bMounted;
    unsigned int HardpointSize;
    char* Hardpoint;
protected:
    unsigned int EncodeFields(const unsigned int& Selector, unsigned char** BufferHolder,
        unsigned int Capacity, unsigned int* OutLength);
    unsigned int DecodeFields(unsigned int& Selector, const unsigned char* Buffer,
        unsigned int Size, unsigned int* OutLength);
};

class FRpcRequestAddItemPacket : public FRpcItemChangePacketBase {
public:
    FRpcRequestAddItemPacket() : FRpcItemChangePacketBase(FLPACKET_CLIENT_REQADDITEM), ArchId(0) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override {
        return EncodeFields(ArchId, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override {
        return DecodeFields(ArchId, Buffer, Size, OutLength);
    }
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcRequestAddItemPacket>(OutPacket);
    }
    unsigned int ArchId;
};

class FRpcRequestModifyItemPacket : public FRpcItemChangePacketBase {
public:
    FRpcRequestModifyItemPacket() : FRpcItemChangePacketBase(FLPACKET_CLIENT_REQMODIFYITEM), EquipId(0) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override {
        return EncodeFields(EquipId, BufferHolder, Capacity, OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override {
        return DecodeFields(EquipId, Buffer, Size, OutLength);
    }
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcRequestModifyItemPacket>(OutPacket);
    }
    unsigned int EquipId;
};

using FRpcEnterBasePacket = FRpcScalarPacket<FLPACKET_CLIENT_ENTERBASE, unsigned int, false>;
using FRpcExitBasePacket = FRpcScalarPacket<FLPACKET_CLIENT_EXITBASE, unsigned int, false>;
using FRpcEnterLocationPacket = FRpcScalarPacket<FLPACKET_CLIENT_ENTERLOCATION, unsigned int, false>;
using FRpcExitLocationPacket = FRpcScalarPacket<FLPACKET_CLIENT_EXITLOCATION, unsigned int, false>;
using FRpcSystemSwitchOutCompletePacket = FRpcScalarPacket<FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE, unsigned int, false>;

class FRpcRequestLaunchPacket : public FRpcScalarPacket<FLPACKET_CLIENT_REQUESTLAUNCH, unsigned int, false> {
public:
    void Dump(std::ostream& Out) const override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcRequestLaunchPacket>(OutPacket);
    }
};

class FRpcClientInfoRequestPacketBase : public FPacketBase {
public:
    explicit FRpcClientInfoRequestPacketBase(unsigned int PacketTypeId);
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    unsigned int Id;
    unsigned char bFull; // Native wire byte is retained without normalization.
};
template<unsigned int PacketTypeId>
class FRpcClientInfoRequestPacket : public FRpcClientInfoRequestPacketBase {
public:
    FRpcClientInfoRequestPacket() : FRpcClientInfoRequestPacketBase(PacketTypeId) {}
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcClientInfoRequestPacket>(OutPacket);
    }
};
using FRpcRequestBaseInfoPacket = FRpcClientInfoRequestPacket<FLPACKET_CLIENT_REQUESTBASEINFO>;
using FRpcRequestLocationInfoPacket = FRpcClientInfoRequestPacket<FLPACKET_CLIENT_REQUESTLOCATIONINFO>;

template<unsigned int Id>
class FRpcEmptyPacket : public FPacketBase {
public:
    FRpcEmptyPacket() : FPacketBase(Id) {}
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcEmptyPacket>(OutPacket);
    }
};
using FRpcRequestCharInfoPacket = FRpcEmptyPacket<FLPACKET_CLIENT_REQUESTCHARINFO>;
using FRpcRequestNewCharInfoPacket = FRpcEmptyPacket<FLPACKET_CLIENT_REQUESTNEWCHARINFO>;
using FRpcSkipAutosavePacket = FRpcEmptyPacket<FLPACKET_CLIENT_SKIP_AUTOSAVE>;
using FRpcRequestCreateShipPacket = FRpcEmptyPacket<FLPACKET_CLIENT_REQUESTCREATESHIP>;
using FRpcGfRequestShipInfoPacket = FRpcEmptyPacket<FLPACKET_CLIENT_GFREQUESTSHIPINFO>;

class FRpcDebugCreateShipPacket : public FPacketBase {
public:
    FRpcDebugCreateShipPacket();
    ~FRpcDebugCreateShipPacket() override;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcDebugCreateShipPacket>(OutPacket);
    }

    // TODO: establish the meaning of these native fields. Offsets describe the
    // retail payload; this modern owning representation is never copied to wire.
    unsigned int Field_0x00;
    unsigned int Field_0x04;
    unsigned int Field_0x08;
    unsigned int Field_0x0c;
    unsigned int Field_0x10;
    Vector Field_0x14;
    Vector Field_0x20;
    Quaternion Orientation;
    unsigned int Field_0x3c;
    wchar_t* WideData;
    unsigned int WideCount;
    unsigned char* DataA;
    unsigned int DataSizeA;
    unsigned char* DataB;
    unsigned int DataSizeB;
    unsigned int Field_0x50;
    unsigned int Field_0x54;
    unsigned int Field_0x58;
    unsigned int Field_0x5c;
    unsigned int ArrayCount;
    unsigned int* ArrayData;
};

class FRpcDebugLoadSystemPacket : public FPacketBase {
public:
    FRpcDebugLoadSystemPacket();
    ~FRpcDebugLoadSystemPacket() override;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    void CopyString(const char* Text);
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcDebugLoadSystemPacket>(OutPacket);
    }
    char* Data;
    unsigned int DataSize;
};

class FRpcDebugDestroyObjectPacket : public FPacketBase {
public:
    FRpcDebugDestroyObjectPacket() : FPacketBase(FLPACKET_CLIENT_DBGDESTROYOBJECT) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcDebugDestroyObjectPacket>(OutPacket);
    }
    unsigned int Field_0x00; // TODO: native debug payload meaning.
    unsigned short Field_0x04; // TODO: native debug payload meaning.
};

class FRpcDebugConsolePacket : public FPacketBase {
public:
    FRpcDebugConsolePacket() : FPacketBase(FLPACKET_CLIENT_DBGCONSOLE), Field_0x00(0) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcDebugConsolePacket>(OutPacket);
    }
    unsigned int Field_0x00; // TODO: native console packet's leading DWORD meaning.
    unsigned char Command[128];
};

class FRpcDebugRelocateShipPacket : public FPacketBase {
public:
    FRpcDebugRelocateShipPacket()
        : FPacketBase(FLPACKET_CLIENT_DBGRELOCATE_SHIP), Position{0.0f, 0.0f, 0.0f} {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcDebugRelocateShipPacket>(OutPacket);
    }
    unsigned int Field_0x00; // TODO: native relocation packet's leading DWORD meaning.
    Vector Position;
};

class FRpcDebugFreeSystemPacket : public FPacketBase {
public:
    FRpcDebugFreeSystemPacket() : FPacketBase(FLPACKET_CLIENT_DBGFREESYSTEM) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcDebugFreeSystemPacket>(OutPacket);
    }
};

class FRpcCharacterIdPacketBase : public FPacketBase {
public:
    explicit FRpcCharacterIdPacketBase(unsigned int Id);
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    void Dump(std::ostream& Out) const override;
    CHARACTER_ID CharacterId;
};
template<unsigned int Id>
class FRpcCharacterIdPacket : public FRpcCharacterIdPacketBase {
public:
    FRpcCharacterIdPacket() : FRpcCharacterIdPacketBase(Id) {}
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcCharacterIdPacket>(OutPacket);
    }
};
using FRpcSelectCharacterPacket = FRpcCharacterIdPacket<FLPACKET_CLIENT_SELECTCHARACTER>;
using FRpcDestroyCharacterPacket = FRpcCharacterIdPacket<FLPACKET_CLIENT_DESTROYCHAR>;

class FRpcCreateNewCharacterPacket : public FPacketBase {
public:
    FRpcCreateNewCharacterPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    void Dump(std::ostream& Out) const override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcCreateNewCharacterPacket>(OutPacket);
    }
    // Retail RC uses 24 wchar_t, RS 64. The codec preserves each copy policy;
    // these fields express values, never offsets in a VC6 object representation.
    wchar_t Name[64];
    unsigned int FactionId;
    unsigned int BaseId;
    unsigned int PackageId;
    unsigned int PilotId;
};

// These eight retail classes share the byte-buffer wire body, but their senders
// have different ownership contracts. The pointer remains a pointer, never an STL
// object representation in the packet stream. The destructor is its RAII owner.
class FRpcCommonBufferPacketBase : public FPacketBase {
public:
    FRpcCommonBufferPacketBase(unsigned int Id, bool bInNullTerminate);
    ~FRpcCommonBufferPacketBase() override;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;

    // The two copy forms preserve the different native RC/RS ownership order.
    void BorrowData(unsigned char* InData, unsigned int Size);
    void AdoptData(unsigned char* InData, unsigned int Size);
    void CopyData(const void* InData, unsigned int Size);
    void CopyDataIfNull(const void* InData, unsigned int Size);

    unsigned char* Data;
    unsigned int DataSize; // Native constructor deliberately leaves this unset.
    bool bOwnsData;
private:
    const bool bNullTerminate;
};

template<unsigned int Id, bool bTerminatePayloadData = false>
class FRpcCommonBufferPacket : public FRpcCommonBufferPacketBase {
public:
    FRpcCommonBufferPacket() : FRpcCommonBufferPacketBase(Id, bTerminatePayloadData) {}
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcCommonBufferPacket>(OutPacket);
    }
};

using FRpcSetWeaponGroupPacket = FRpcCommonBufferPacket<FLPACKET_COMMON_SET_WEAPON_GROUP, true>;
using FRpcSetVisitedStatePacket = FRpcCommonBufferPacket<FLPACKET_COMMON_SET_VISITED_STATE>;
using FRpcRequestBestPathPacket = FRpcCommonBufferPacket<FLPACKET_COMMON_REQUEST_BEST_PATH>;
using FRpcRequestPlayerStatsPacket = FRpcCommonBufferPacket<FLPACKET_COMMON_REQUEST_PLAYER_STATS>;
using FRpcRequestGroupPositionsPacket = FRpcCommonBufferPacket<FLPACKET_COMMON_REQUEST_GROUP_POSITIONS>;
using FRpcSetMissionLogPacket = FRpcCommonBufferPacket<FLPACKET_COMMON_SET_MISSION_LOG>;
using FRpcSetInterfaceStatePacket = FRpcCommonBufferPacket<FLPACKET_COMMON_SET_INTERFACE_STATE>;
using FRpcRequestRankLevelPacket = FRpcCommonBufferPacket<FLPACKET_COMMON_REQUEST_RANK_LEVEL>;

class FRpcUpdateObjectPacket : public FPacketBase {
public:
    FRpcUpdateObjectPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcUpdateObjectPacket>(OutPacket);
    }
    unsigned char State; // TODO: object-update flag semantics.
    unsigned int ObjectId;
    Vector Position;
    Quaternion Orientation;
    float UpdateTime;
    float Throttle; // TODO: meaning of the fifth quantized float.
};

class FRpcPlayerTradePacket : public FPacketBase {
public:
    FRpcPlayerTradePacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcPlayerTradePacket>(OutPacket);
    }
    // Fields coexist as in retail; a variant would discard untouched decode state.
    unsigned int Action;
    int Reason;
    unsigned int TargetPlayer;
    int Money;
    unsigned char bAccepted;
    unsigned int EquipArchId;
    int EquipCount;
    float EquipHealth;
    unsigned short EquipId;
};

class FRpcChatMsgPacket : public FPacketBase {
public:
    FRpcChatMsgPacket();
    ~FRpcChatMsgPacket() override;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcChatMsgPacket>(OutPacket);
    }
    unsigned int TargetId;
    unsigned int SenderId; // TODO: meaning of the second chat DWORD.
    unsigned int DataSize;
    unsigned char* Data;
};

class FRpcPopUpDialogPacket : public FPacketBase {
public:
    FRpcPopUpDialogPacket();
    ~FRpcPopUpDialogPacket() override;
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcPopUpDialogPacket>(OutPacket);
    }
    unsigned int SizeA;
    unsigned char* DataA;
    unsigned int SizeB;
    unsigned char* DataB;
    unsigned int Buttons; // TODO: semantics of the final dialog DWORD.
};

class FRpcClientLoginPacket : public FPacketBase {
public:
    FRpcClientLoginPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    void Dump(std::ostream& Out) const override;
    static int Factory(FPacketBase** OutPacket) {
        return RpcWirePacketFactory<FRpcClientLoginPacket>(OutPacket);
    }
    wchar_t AccountName[256];
    wchar_t Password[256]; // TODO: semantic identity of the second login string.
    unsigned char CreateFlag;
    unsigned char Padding[3];
};

class FRpcFireWeaponPacket : public FPacketBase {
public:
    FRpcFireWeaponPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    unsigned int ObjectId;
    Vector TargetPos;
    std::vector<unsigned short> Hardpoints;
};

class FRpcTractorObjectsPacket : public FPacketBase {
public:
    FRpcTractorObjectsPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    unsigned int ObjectId;
    unsigned short TractorEquipId;
    std::vector<unsigned int> TargetIds;
};

class FRpcGoTradeLanePacket : public FPacketBase {
public:
    FRpcGoTradeLanePacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    unsigned int ObjectId;
    unsigned int TradelaneRing1;
    unsigned int TradelaneRing2;
    unsigned char bMidLane; // TODO: semantics of the fourth value, passed as a bool.
};

class FRpcJettisonCargoPacket : public FPacketBase {
public:
    FRpcJettisonCargoPacket();
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override;
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override;
    unsigned int ObjectId;
    unsigned short ItemId;
    unsigned int Count;
};

class FRpcActivateEquipPacket : public FPacketBase {
public:
    FRpcActivateEquipPacket() : FPacketBase(FLPACKET_COMMON_ACTIVATEEQUIP), EquipId(0) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override {
        // RC06b67c60 / RS06bfef50: every failed reservation returns literal 0x16.
        FRpcWireStream Stream(*this, BufferHolder, Capacity);
        Stream.PutHeader();
        if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&EquipId, 2u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&bActive, 1u)) return kPacketEncodeOverflow;
        return Stream.FinishEncodeSuccess(OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override {
        FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
        if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
        if (!Stream.Get(&EquipId, 2u)) return MarshalError;
        if (!Stream.Get(&bActive, 1u)) return MarshalError;
        return Stream.Finish(OutLength, false);
    }
    unsigned int ObjectId;
    unsigned char bActive;
    unsigned char Padding;
    unsigned short EquipId;
};

class FRpcActivateCruisePacket : public FPacketBase {
public:
    FRpcActivateCruisePacket() : FPacketBase(FLPACKET_COMMON_ACTIVATECRUISE) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override {
        // RC06b67f10 / RS06bff200: every failed reservation returns literal 0x16.
        FRpcWireStream Stream(*this, BufferHolder, Capacity);
        Stream.PutHeader();
        if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&bActive, 1u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&bDisruptCruise, 1u)) return kPacketEncodeOverflow;
        return Stream.FinishEncodeSuccess(OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override {
        FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
        if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
        if (!Stream.Get(&bActive, 1u)) return MarshalError;
        if (!Stream.Get(&bDisruptCruise, 1u)) return MarshalError;
        return Stream.Finish(OutLength, false);
    }
    unsigned int ObjectId;
    unsigned char bActive;
    unsigned char bDisruptCruise; // TODO: second cruise flag semantics beyond bool normalization.
};

class FRpcActivateThrustersPacket : public FPacketBase {
public:
    FRpcActivateThrustersPacket() : FPacketBase(FLPACKET_COMMON_ACTIVATETHRUSTERS) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override {
        // RC06b686f0 / RS06bff9e0: every failed reservation returns literal 0x16.
        FRpcWireStream Stream(*this, BufferHolder, Capacity);
        Stream.PutHeader();
        if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&bActive, 1u)) return kPacketEncodeOverflow;
        return Stream.FinishEncodeSuccess(OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override {
        FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
        if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
        if (!Stream.Get(&bActive, 1u)) return MarshalError;
        return Stream.Finish(OutLength, false);
    }
    unsigned int ObjectId;
    unsigned char bActive;
};

class FRpcSetTargetPacket : public FPacketBase {
public:
    FRpcSetTargetPacket() : FPacketBase(FLPACKET_COMMON_SETTARGET), SubObjId(0), TargetSubObjId(0) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override {
        // RC06b68980 / RS06bffc70: every failure returns EBP=0x16 (06b689d7).
        FRpcWireStream Stream(*this, BufferHolder, Capacity);
        Stream.PutHeader();
        if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&TargetId, 4u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&SubObjId, 2u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&TargetSubObjId, 2u)) return kPacketEncodeOverflow;
        return Stream.FinishEncodeSuccess(OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override {
        FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
        if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
        if (!Stream.Get(&TargetId, 4u)) return MarshalError;
        if (!Stream.Get(&SubObjId, 2u)) return MarshalError;
        if (!Stream.Get(&TargetSubObjId, 2u)) return MarshalError;
        return Stream.Finish(OutLength, false);
    }
    unsigned int ObjectId;
    unsigned int TargetId;
    unsigned short SubObjId;
    unsigned short TargetSubObjId;
};

class FRpcStopTradeLanePacket : public FPacketBase {
public:
    FRpcStopTradeLanePacket() : FPacketBase(FLPACKET_COMMON_STOPTRADELANE), ShipId(0), TradeLaneRing1(0), TradeLaneRing2(0) {}
    unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
        unsigned int* OutLength) override {
        // RC06b68440 / RS06bff730: every failed reservation returns literal 0x16.
        FRpcWireStream Stream(*this, BufferHolder, Capacity);
        Stream.PutHeader();
        if (!Stream.Put(&ShipId, 4u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&TradeLaneRing1, 4u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&TradeLaneRing2, 4u)) return kPacketEncodeOverflow;
        return Stream.FinishEncodeSuccess(OutLength);
    }
    unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
        unsigned int* OutLength) override {
        FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
        if (!Stream.Get(&ShipId, 4u)) return MarshalError;
        if (!Stream.Get(&TradeLaneRing1, 4u)) return MarshalError;
        if (!Stream.Get(&TradeLaneRing2, 4u)) return MarshalError;
        return Stream.Finish(OutLength, false);
    }
    unsigned int ShipId;
    unsigned int TradeLaneRing1;
    unsigned int TradeLaneRing2;
};

// All 21 native COMMON packet types use concrete classes shared by both DLLs.
// The registrator's native null/return contract is recovered separately.
inline int RpcRegisterCommonScalarPackets(FPacketSerializer* Serializer) {
    if (Serializer == nullptr) return 4;
    Serializer->RegisterType(FLPACKET_COMMON_FIREWEAPON, "FLPACKET_COMMON_FIREWEAPON",
        &RpcWirePacketFactory<FRpcFireWeaponPacket>);
    Serializer->RegisterType(FLPACKET_COMMON_UPDATEOBJECT, "FLPACKET_COMMON_UPDATEOBJECT",
        &FRpcUpdateObjectPacket::Factory);
    Serializer->RegisterType(FLPACKET_COMMON_CHATMSG, "FLPACKET_COMMON_CHATMSG",
        &FRpcChatMsgPacket::Factory);
    Serializer->RegisterType(FLPACKET_COMMON_ACTIVATEEQUIP, "FLPACKET_COMMON_ACTIVATEEQUIP",
        &RpcWirePacketFactory<FRpcActivateEquipPacket>);
    Serializer->RegisterType(FLPACKET_COMMON_SETTARGET, "FLPACKET_COMMON_SETTARGET",
        &RpcWirePacketFactory<FRpcSetTargetPacket>);
    Serializer->RegisterType(FLPACKET_COMMON_ACTIVATECRUISE, "FLPACKET_COMMON_ACTIVATECRUISE",
        &RpcWirePacketFactory<FRpcActivateCruisePacket>);
    Serializer->RegisterType(FLPACKET_COMMON_ACTIVATETHRUSTERS, "FLPACKET_COMMON_ACTIVATETHRUSTERS",
        &RpcWirePacketFactory<FRpcActivateThrustersPacket>);
    Serializer->RegisterType(FLPACKET_COMMON_GOTRADELANE, "FLPACKET_COMMON_GOTRADELANE",
        &RpcWirePacketFactory<FRpcGoTradeLanePacket>);
    Serializer->RegisterType(FLPACKET_COMMON_STOPTRADELANE, "FLPACKET_COMMON_STOPTRADELANE",
        &RpcWirePacketFactory<FRpcStopTradeLanePacket>);
    Serializer->RegisterType(FLPACKET_COMMON_SET_WEAPON_GROUP, "FLPACKET_COMMON_SET_WEAPON_GROUP",
        &FRpcSetWeaponGroupPacket::Factory);
    Serializer->RegisterType(FLPACKET_COMMON_PLAYER_TRADE, "FLPACKET_COMMON_PLAYER_TRADE",
        &FRpcPlayerTradePacket::Factory);
    Serializer->RegisterType(FLPACKET_COMMON_SET_VISITED_STATE, "FLPACKET_COMMON_SET_VISITED_STATE",
        &FRpcSetVisitedStatePacket::Factory);
    Serializer->RegisterType(FLPACKET_COMMON_JETTISONCARGO, "FLPACKET_COMMON_JETTISONCARGO",
        &RpcWirePacketFactory<FRpcJettisonCargoPacket>);
    Serializer->RegisterType(FLPACKET_COMMON_REQUEST_BEST_PATH, "FLPACKET_COMMON_REQUEST_BEST_PATH",
        &FRpcRequestBestPathPacket::Factory);
    Serializer->RegisterType(FLPACKET_COMMON_REQUEST_PLAYER_STATS, "FLPACKET_COMMON_REQUEST_PLAYER_STATS",
        &FRpcRequestPlayerStatsPacket::Factory);
    Serializer->RegisterType(FLPACKET_COMMON_POP_UP_DIALOG, "FLPACKET_COMMON_POP_UP_DIALOG",
        &FRpcPopUpDialogPacket::Factory);
    Serializer->RegisterType(FLPACKET_COMMON_REQUEST_GROUP_POSITIONS, "FLPACKET_COMMON_REQUEST_GROUP_POSITIONS",
        &FRpcRequestGroupPositionsPacket::Factory);
    Serializer->RegisterType(FLPACKET_COMMON_SET_MISSION_LOG, "FLPACKET_COMMON_SET_MISSION_LOG",
        &FRpcSetMissionLogPacket::Factory);
    Serializer->RegisterType(FLPACKET_COMMON_REQUEST_RANK_LEVEL, "FLPACKET_COMMON_REQUEST_RANK_LEVEL",
        &FRpcRequestRankLevelPacket::Factory);
    Serializer->RegisterType(FLPACKET_COMMON_SET_INTERFACE_STATE, "FLPACKET_COMMON_SET_INTERFACE_STATE",
        &FRpcSetInterfaceStatePacket::Factory);
    Serializer->RegisterType(FLPACKET_COMMON_TRACTOROBJECTS, "FLPACKET_COMMON_TRACTOROBJECTS",
        &RpcWirePacketFactory<FRpcTractorObjectsPacket>);
    return 0;
}

inline int RpcRegisterClientScalarPackets(FPacketSerializer* Serializer) {
    if (Serializer == nullptr) return 4;
    Serializer->RegisterType(FLPACKET_CLIENT_REQCHANGECASH, "FLPACKET_CLIENT_REQCHANGECASH",
        &FRpcRequestChangeCashPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQSETCASH, "FLPACKET_CLIENT_REQSETCASH",
        &FRpcRequestSetCashPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQREMOVEITEM, "FLPACKET_CLIENT_REQREMOVEITEM",
        &FRpcRequestRemoveItemPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQMODIFYITEM, "FLPACKET_CLIENT_REQMODIFYITEM",
        &FRpcRequestModifyItemPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQCARGO, "FLPACKET_CLIENT_REQCARGO",
        &FRpcRequestCargoPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQADDITEM, "FLPACKET_CLIENT_REQADDITEM",
        &FRpcRequestAddItemPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQSHIPARCH, "FLPACKET_CLIENT_REQSHIPARCH",
        &FRpcRequestShipArchPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQEQUIPMENT, "FLPACKET_CLIENT_REQEQUIPMENT",
        &FRpcRequestEquipmentPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_MUNCOLLISION, "FLPACKET_CLIENT_MUNCOLLISION",
        &FRpcMunitionCollisionPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_BADLANDSOBJCOLLISION, "FLPACKET_CLIENT_BADLANDSOBJCOLLISION",
        &FRpcBadlandsObjectCollisionPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_OBJCOLLISION, "FLPACKET_CLIENT_OBJCOLLISION",
        &FRpcObjectCollisionPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQUEST_EVENT, "FLPACKET_CLIENT_REQUEST_EVENT",
        &FRpcRequestEventPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQUEST_CANCEL, "FLPACKET_CLIENT_REQUEST_CANCEL",
        &FRpcRequestCancelPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_CARGOSCAN, "FLPACKET_CLIENT_CARGOSCAN",
        &FRpcCargoScanPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQUESTLAUNCH, "FLPACKET_CLIENT_REQUESTLAUNCH",
        &FRpcRequestLaunchPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQUESTCHARINFO, "FLPACKET_CLIENT_REQUESTCHARINFO",
        &FRpcRequestCharInfoPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_SKIP_AUTOSAVE, "FLPACKET_CLIENT_SKIP_AUTOSAVE",
        &FRpcSkipAutosavePacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_SELECTCHARACTER, "FLPACKET_CLIENT_SELECTCHARACTER",
        &FRpcSelectCharacterPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_COMMCOMPLETE, "FLPACKET_CLIENT_COMMCOMPLETE",
        &FRpcCommCompletePacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQUESTNEWCHARINFO, "FLPACKET_CLIENT_REQUESTNEWCHARINFO",
        &FRpcRequestNewCharInfoPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_CREATENEWCHAR, "FLPACKET_CLIENT_CREATENEWCHAR",
        &FRpcCreateNewCharacterPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_DESTROYCHAR, "FLPACKET_CLIENT_DESTROYCHAR",
        &FRpcDestroyCharacterPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_LOGIN, "FLPACKET_CLIENT_LOGIN", &FRpcClientLoginPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_ENTERBASE, "FLPACKET_CLIENT_ENTERBASE",
        &FRpcEnterBasePacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQUESTBASEINFO, "FLPACKET_CLIENT_REQUESTBASEINFO",
        &FRpcRequestBaseInfoPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQUESTLOCATIONINFO, "FLPACKET_CLIENT_REQUESTLOCATIONINFO",
        &FRpcRequestLocationInfoPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_GFREQUESTSHIPINFO, "FLPACKET_CLIENT_GFREQUESTSHIPINFO",
        &FRpcGfRequestShipInfoPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE, "FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE",
        &FRpcSystemSwitchOutCompletePacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_EXITBASE, "FLPACKET_CLIENT_EXITBASE",
        &FRpcExitBasePacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_ENTERLOCATION, "FLPACKET_CLIENT_ENTERLOCATION",
        &FRpcEnterLocationPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_EXITLOCATION, "FLPACKET_CLIENT_EXITLOCATION",
        &FRpcExitLocationPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQUESTCREATESHIP, "FLPACKET_CLIENT_REQUESTCREATESHIP",
        &FRpcRequestCreateShipPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_INTERFACEITEMUSED, "FLPACKET_CLIENT_INTERFACEITEMUSED",
        &FRpcInterfaceItemUsedPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_RTCDONE, "FLPACKET_CLIENT_RTCDONE",
        &FRpcRtcDonePacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_ABORT_MISSION, "FLPACKET_CLIENT_ABORT_MISSION",
        &FRpcAbortMissionPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_LAUNCHCOMPLETE, "FLPACKET_CLIENT_LAUNCHCOMPLETE",
        &FRpcLaunchCompletePacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_JUMPINCOMPLETE, "FLPACKET_CLIENT_JUMPINCOMPLETE",
        &FRpcJumpInCompletePacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_HAIL, "FLPACKET_CLIENT_HAIL",
        &FRpcHailPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_GFGOODBUY, "FLPACKET_CLIENT_GFGOODBUY",
        &FRpcGfGoodBuyPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_GFGOODSELL, "FLPACKET_CLIENT_GFGOODSELL",
        &FRpcGfGoodSellPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_GFGOODVAPORIZED, "FLPACKET_CLIENT_GFGOODVAPORIZED",
        &FRpcGfGoodVaporizedPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_GFSELECTOBJECT, "FLPACKET_CLIENT_GFSELECTOBJECT",
        &FRpcGfSelectObjectPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_SAVEGAME, "FLPACKET_CLIENT_SAVEGAME",
        &FRpcSaveGamePacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_MINEASTEROID, "FLPACKET_CLIENT_MINEASTEROID",
        &FRpcMineAsteroidPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_MISSIONRESPONSE, "FLPACKET_CLIENT_MISSIONRESPONSE",
        &FRpcMissionResponsePacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_TRADERESPONSE, "FLPACKET_CLIENT_TRADERESPONSE",
        &FRpcTradeResponsePacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_DBGCREATESHIP, "FLPACKET_CLIENT_DBGCREATESHIP",
        &FRpcDebugCreateShipPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_DBGLOADSYSTEM, "FLPACKET_CLIENT_DBGLOADSYSTEM",
        &FRpcDebugLoadSystemPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_DBGFREESYSTEM, "FLPACKET_CLIENT_DBGFREESYSTEM",
        &FRpcDebugFreeSystemPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_DOCK, "FLPACKET_CLIENT_DOCK",
        &FRpcDockPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_DBGCONSOLE, "FLPACKET_CLIENT_DBGCONSOLE",
        &FRpcDebugConsolePacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_DBGDESTROYOBJECT, "FLPACKET_CLIENT_DBGDESTROYOBJECT",
        &FRpcDebugDestroyObjectPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_DBGRELOCATE_SHIP, "FLPACKET_CLIENT_DBGRELOCATE_SHIP",
        &FRpcDebugRelocateShipPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_SETMANEUVER, "FLPACKET_CLIENT_SETMANEUVER",
        &FRpcSetManeuverPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQCOLLISIONGROUPS, "FLPACKET_CLIENT_REQCOLLISIONGROUPS",
        &FRpcRequestCollisionGroupsPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQHULLSTATUS, "FLPACKET_CLIENT_REQHULLSTATUS",
        &FRpcRequestHullStatusPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQUEST_USE_ITEM, "FLPACKET_CLIENT_REQUEST_USE_ITEM",
        &FRpcRequestUseItemPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQINVINCIBILITY, "FLPACKET_CLIENT_REQINVINCIBILITY",
        &FRpcRequestInvincibilityPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_MISSIONSAVEB, "FLPACKET_CLIENT_MISSIONSAVEB",
        &FRpcMissionSaveBPacket::Factory);
    Serializer->RegisterType(FLPACKET_CLIENT_REQDIFFICULTYSCALE, "FLPACKET_CLIENT_REQDIFFICULTYSCALE",
        &FRpcRequestDifficultyScalePacket::Factory);
    return 0;
}
