// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "rpc_packet_codec.h"
#include "common_misc.h" // SinglePlayer selects retail's two FIREWEAPON formats.
#include "common_net.h" // PLAYER_TRADE counts its trailer by IsMPServer direction.
#include "common_text.h" // Canonical FmtStr::Val size calculation.

#include <cfloat>
#include <cwchar>
#include <cstdio>
#include <ostream>

namespace {
// A distinct private global in each Remote DLL, as in both retail images.
std::map<unsigned int, FRpcWireStats> GRemoteWireStats;
}

std::map<unsigned int, FRpcWireStats>& RpcRemoteWireStats() {
    return GRemoteWireStats;
}

FRpcWireStats* RpcFindRemoteWireStats(unsigned int PacketId) {
    return &GRemoteWireStats[PacketId];
}

void FPacketSerializer::DumpStats(const char* Name) const {
    FILE* File = std::fopen(Name, "w");
    if (File == nullptr) return;
    std::fprintf(File, "Packet Name,In Count,In Max Size,In Avg Size,In Total Bytes,Exec Time Total,Exec Time Avg,Out Count,Out Max Size,Out Avg Size,Out Total Bytes\n\n");
    for (std::map<unsigned int, FPacketTypeInfo*>::const_iterator It = Types.begin();
        It != Types.end(); ++It) {
        FRpcWireStats* Stats = RpcFindRemoteWireStats(It->first);
        if (Stats->EncodedCount == 0 && Stats->DecodedCount == 0) continue;
        std::fprintf(File, "%s,", It->second->Name.c_str());
        std::fprintf(File, "%u,%u,%u,%u,%f,%f,", Stats->DecodedCount,
            Stats->DecodedMaximum, Stats->DecodedAverage, Stats->DecodedBytes,
            Stats->ExecTimeTotal, Stats->ExecTimeAvg);
        std::fprintf(File, "%u,%u,%u,%u", Stats->EncodedCount,
            Stats->EncodedMaximum, Stats->EncodedAverage, Stats->EncodedBytes);
        std::fprintf(File, "\n");
    }
    std::fclose(File);
}

FRpcSaveGamePacket::FRpcSaveGamePacket() : FPacketBase(FLPACKET_CLIENT_SAVEGAME) {
    // Native writes only CharacterId.Buffer[0]; canonical CHARACTER_ID also
    // initializes its unused tail. Description and Count stay uninitialized.
}

FRpcSaveGamePacket::~FRpcSaveGamePacket() {
    if (Description != nullptr) ::operator delete(Description);
}

void FRpcSaveGamePacket::CopyDescription(const wchar_t* Source, unsigned int Count) {
    if (Count == 0 || Source == nullptr) return;
    const unsigned int ByteCount = Count * 2u;
    Description = static_cast<wchar_t*>(::operator new(ByteCount, std::nothrow));
    if (ByteCount != 0) std::memcpy(Description, Source, ByteCount);
    DescriptionCount = Count;
}

unsigned int FRpcSaveGamePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b55690 / RS06be3920.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&DescriptionCount, 4u)) return kPacketEncodeOverflow;
    if (DescriptionCount != 0) {
        const unsigned int ByteCount = DescriptionCount * 2u;
        if (!Stream.Reserve(ByteCount, kPacketEncodeOverflow)) return kPacketEncodeOverflow;
        Stream.PutReserved(Description, ByteCount);
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcSaveGamePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    unsigned int Count;
    if (!Stream.Get(&Count, 4u)) return MarshalError;
    DescriptionCount = Count;
    if (DescriptionCount != 0) {
        const unsigned int ByteCount = DescriptionCount * 2u;
#if defined(REMOTECLIENT_EXPORTS)
        // RC preflights without committing cursor; RS allocates before bounds.
        if (MarshalCursor + ByteCount > MarshalLimit) {
            MarshalError = kPacketDecodeOverrun;
            return MarshalError;
        }
#endif
        Description = static_cast<wchar_t*>(::operator new(ByteCount, std::nothrow));
#if defined(REMOTECLIENT_EXPORTS)
        if (Description == nullptr) return kPacketNoMemory;
#endif
        if (!Stream.Get(Description, ByteCount)) return MarshalError;
    }
    return Stream.Finish(OutLength, false);
}

FRpcTradeResponsePacket::FRpcTradeResponsePacket()
    : FPacketBase(FLPACKET_CLIENT_TRADERESPONSE), DataSize(0), Data(nullptr) {}

FRpcTradeResponsePacket::~FRpcTradeResponsePacket() {
    ::operator delete(Data);
    Data = nullptr;
}

void FRpcTradeResponsePacket::CopyData(const unsigned char* Source, unsigned int Size) {
    Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
    if (Size != 0) std::memcpy(Data, Source, Size);
    DataSize = Size;
}

unsigned int FRpcTradeResponsePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b54510 / RS06be3660.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&DataSize, 4u)) return kPacketEncodeOverflow;
    // Retail sends the pointer DWORD (+0x24) itself, not the pointed-to buffer's bytes.
    if (!Stream.Put(&Data, 4u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcTradeResponsePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
#if defined(REMOTECLIENT_EXPORTS)
    unsigned int Count;
    if (!Stream.Get(&Count, 4u)) return MarshalError;
    DataSize = Count;
    if (MarshalCursor + DataSize > MarshalLimit) {
        MarshalError = kPacketDecodeOverrun;
        return MarshalError;
    }
    Data = static_cast<unsigned char*>(::operator new(DataSize, std::nothrow));
    if (!Stream.Get(Data, DataSize)) return MarshalError;
#else
    if (!Stream.Get(&DataSize, 4u)) return MarshalError;
    Data = static_cast<unsigned char*>(::operator new(DataSize, std::nothrow));
    if (!Stream.Reserve(4u, kPacketDecodeOverrun)) return MarshalError;
    unsigned int PointerBits;
    std::memcpy(&PointerBits, reinterpret_cast<const void*>(MarshalPtr), 4u);
    // Native overwrites and leaks the fresh allocation with the wire DWORD.
    Data = reinterpret_cast<unsigned char*>(PointerBits);
    Stream.CommitReadPointer(4u);
#endif
    return Stream.Finish(OutLength, false);
}

void FRpcConnectResponsePacket::Dump(std::ostream& Out) const {
    Out << "   Client ID: " << Value << "\n";
}

void FRpcLoginResponsePacket::Dump(std::ostream& Out) const {
    // Retail selects the unsigned-character inserter, not a decimal integer.
    Out << "   Result   : " << Value << "\n";
}

unsigned int FRpcMissionResponsePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b54220 / RS06c02750.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&OfferId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&OfferKind, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bAccepted, 1u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcMissionResponsePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    unsigned int First;
    if (!Stream.Get(&First, 4u)) return MarshalError;
    OfferId = First;
    unsigned long Second;
    if (!Stream.Get(&Second, 4u)) return MarshalError;
    OfferKind = Second;
    if (!Stream.Get(&bAccepted, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int RpcGetFlattenedFmtStrSize(const FmtStr& Text) {
    unsigned int Size = 6u;
    for (const FmtStr::Val* Node = Text.Head; Node != nullptr; Node = Node->Next) {
        Size += Node->get_flattened_size();
    }
    return Size;
}

// RC06b3a180 / RS06bda4e0: operate on the caller's existing marshal state.
unsigned int RpcWireEncodeCollisionGroups(FRpcWireStream& Stream,
    const std::list<CollisionGroupDesc>& Groups) {
    const unsigned short Count = static_cast<unsigned short>(Groups.size());
    if (!Stream.Put(&Count, 2u)) return kPacketEncodeOverflow;
    if (Count != 0) {
        for (std::list<CollisionGroupDesc>::const_iterator It = Groups.begin();
            It != Groups.end(); ++It) {
            if (!Stream.Put(&It->Id, 2u)) return kPacketEncodeOverflow;
            if (!Stream.PutFloat(It->Health)) return kPacketEncodeOverflow;
        }
    }
    return 0;
}

// RC06b3a230 / RS06bda590: clear before even checking the count prefix.
unsigned int RpcWireDecodeCollisionGroups(FPacketBase& Packet, FRpcWireStream& Stream,
    std::list<CollisionGroupDesc>& Groups) {
    Groups.clear();
    unsigned short Count;
    if (!Stream.Get(&Count, 2u)) return Packet.MarshalError;
    for (unsigned int Index = 0; Index < Count; ++Index) {
        // Native appends a copy of two uninitialized stack DWORDs before the bounds check;
        // the port value-initializes the node instead.
        Groups.emplace_back();
        CollisionGroupDesc& Group = Groups.back();
        if (!Stream.Get(&Group.Id, 2u)) return Packet.MarshalError;
        if (!Stream.Get(&Group.Health, 4u)) return Packet.MarshalError;
    }
    return 0;
}

namespace {
template<typename T>
bool RpcWireGetThenStore(FRpcWireStream& Stream, T& Field) {
    T Value;
    if (!Stream.Get(&Value, sizeof(T))) return false;
    Field = Value;
    return true;
}

bool RpcWirePutVector(FRpcWireStream& Stream, const Vector& Value) {
    if (!Stream.Reserve(12u, kPacketEncodeOverflow)) return false;
    // Integer copy (06b3a160), no x87.
    unsigned int Bits[3];
    std::memcpy(&Bits[0], &Value.x, 4u);
    std::memcpy(&Bits[1], &Value.y, 4u);
    std::memcpy(&Bits[2], &Value.z, 4u);
    Stream.PutReserved(Bits, 12u);
    return true;
}

bool RpcWireGetVector(FPacketBase& Packet, FRpcWireStream& Stream, Vector& Value) {
    if (!Stream.Reserve(12u, kPacketDecodeOverrun)) return false;
    // RC06b3a160 / RS06bda4c0: integer moves, no x87.
    std::memcpy(&Value.x, reinterpret_cast<const void*>(Packet.MarshalPtr), 4u);
    std::memcpy(&Value.y, reinterpret_cast<const void*>(Packet.MarshalPtr + 4u), 4u);
    std::memcpy(&Value.z, reinterpret_cast<const void*>(Packet.MarshalPtr + 8u), 4u);
    Stream.CommitReadPointer(12u);
    return true;
}
}

unsigned int FRpcRequestCollisionGroupsPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b5a830 / RS06c078c0: a nonzero helper result is returned unchanged.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    const unsigned int Result = RpcWireEncodeCollisionGroups(Stream, Groups);
    if (Result != 0) return Result;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcRequestCollisionGroupsPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    const unsigned int Result = RpcWireDecodeCollisionGroups(*this, Stream, Groups);
    if (Result != 0) return Result;
    return Stream.Finish(OutLength, false);
}

FRpcDebugCreateShipPacket::FRpcDebugCreateShipPacket()
    : FPacketBase(FLPACKET_CLIENT_DBGCREATESHIP), Field_0x14{0.0f, 0.0f, 0.0f},
      Field_0x20{0.0f, 0.0f, 0.0f}, Orientation{1.0f, 0.0f, 0.0f, 0.0f},
      Field_0x3c(0), WideData(nullptr), WideCount(0), DataA(nullptr), DataSizeA(0),
      DataB(nullptr), DataSizeB(0), Field_0x50(0), Field_0x54(0), Field_0x58(0),
      Field_0x5c(0), ArrayCount(0), ArrayData(nullptr) {}

FRpcDebugCreateShipPacket::~FRpcDebugCreateShipPacket() {
    if (WideData != nullptr) ::operator delete(WideData);
    if (DataA != nullptr) ::operator delete(DataA);
    if (DataB != nullptr) ::operator delete(DataB);
    if (ArrayData != nullptr) ::operator delete(ArrayData);
}

unsigned int FRpcDebugCreateShipPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b58290 / RS06c05c80.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Field_0x04, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Field_0x08, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Field_0x0c, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Field_0x10, 4u)) return kPacketEncodeOverflow;
    if (!RpcWirePutVector(Stream, Field_0x14)) return kPacketEncodeOverflow;
    if (!RpcWirePutVector(Stream, Field_0x20)) return kPacketEncodeOverflow;
    if (!Stream.Reserve(16u, kPacketEncodeOverflow)) return kPacketEncodeOverflow;
    // W/X/Y/Z as integer DWORDs (06b39ec0).
    unsigned int QuaternionBits[4];
    std::memcpy(&QuaternionBits[0], &Orientation.w, 4u);
    std::memcpy(&QuaternionBits[1], &Orientation.x, 4u);
    std::memcpy(&QuaternionBits[2], &Orientation.y, 4u);
    std::memcpy(&QuaternionBits[3], &Orientation.z, 4u);
    Stream.PutReserved(QuaternionBits, 16u);
    if (!Stream.Put(&Field_0x3c, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&WideCount, 4u)) return kPacketEncodeOverflow;
    if (WideCount != 0) {
        const unsigned int Width = WideCount * 2u;
        if (!Stream.Reserve(Width, kPacketEncodeOverflow)) return kPacketEncodeOverflow;
        // The unsigned multiply may wrap to zero, as in retail; nothing is copied then.
        Stream.PutReserved(WideData, Width);
    }
    if (!Stream.Put(&DataSizeA, 4u)) return kPacketEncodeOverflow;
    if (DataSizeA != 0) {
        if (!Stream.PutBlock(DataA, DataSizeA)) return kPacketEncodeOverflow;
    }
    if (!Stream.Put(&DataSizeB, 4u)) return kPacketEncodeOverflow;
    if (DataSizeB != 0) {
        if (!Stream.PutBlock(DataB, DataSizeB)) return kPacketEncodeOverflow;
    }
    if (!Stream.Put(&Field_0x50, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Field_0x54, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Field_0x58, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Field_0x5c, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ArrayCount, 4u)) return kPacketEncodeOverflow;
    if (ArrayCount != 0) {
        const unsigned int Width = ArrayCount * 4u;
        if (!Stream.Reserve(Width, kPacketEncodeOverflow)) return kPacketEncodeOverflow;
        Stream.PutReserved(ArrayData, Width);
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcDebugCreateShipPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, Field_0x00)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x04)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x08)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x0c)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x10)) return MarshalError;
    if (!RpcWireGetVector(*this, Stream, Field_0x14)) return MarshalError;
    if (!RpcWireGetVector(*this, Stream, Field_0x20)) return MarshalError;
    if (!Stream.Reserve(16u, kPacketDecodeOverrun)) return MarshalError;
    std::memcpy(&Orientation.w, reinterpret_cast<const void*>(MarshalPtr), 4u);
    std::memcpy(&Orientation.x, reinterpret_cast<const void*>(MarshalPtr + 4u), 4u);
    std::memcpy(&Orientation.y, reinterpret_cast<const void*>(MarshalPtr + 8u), 4u);
    std::memcpy(&Orientation.z, reinterpret_cast<const void*>(MarshalPtr + 12u), 4u);
    Stream.CommitReadPointer(16u);
    if (!RpcWireGetThenStore(Stream, Field_0x3c)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, WideCount)) return MarshalError;
    if (WideCount != 0) {
        const unsigned int Width = WideCount * 2u;
#if defined(REMOTECLIENT_EXPORTS)
        // RC has a noncommitting preflight before allocation; RS allocates first.
        if (MarshalCursor + Width > MarshalLimit) {
            MarshalError = kPacketDecodeOverrun;
            return MarshalError;
        }
#endif
        WideData = static_cast<wchar_t*>(::operator new(Width, std::nothrow));
        if (!Stream.Reserve(Width, kPacketDecodeOverrun)) return MarshalError;
        if (Width != 0) std::memcpy(WideData, reinterpret_cast<const void*>(MarshalPtr), Width);
        Stream.CommitReadPointer(Width);
    }
    if (!RpcWireGetThenStore(Stream, DataSizeA)) return MarshalError;
    if (DataSizeA != 0) {
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + DataSizeA > MarshalLimit) {
            MarshalError = kPacketDecodeOverrun;
            return MarshalError;
        }
#endif
        DataA = static_cast<unsigned char*>(::operator new(DataSizeA, std::nothrow));
        if (!Stream.Reserve(DataSizeA, kPacketDecodeOverrun)) return MarshalError;
        std::memcpy(DataA, reinterpret_cast<const void*>(MarshalPtr), DataSizeA);
        Stream.CommitReadPointer(DataSizeA);
    }
    if (!RpcWireGetThenStore(Stream, DataSizeB)) return MarshalError;
    if (DataSizeB != 0) {
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + DataSizeB > MarshalLimit) {
            MarshalError = kPacketDecodeOverrun;
            return MarshalError;
        }
#endif
        DataB = static_cast<unsigned char*>(::operator new(DataSizeB, std::nothrow));
        if (!Stream.Reserve(DataSizeB, kPacketDecodeOverrun)) return MarshalError;
        std::memcpy(DataB, reinterpret_cast<const void*>(MarshalPtr), DataSizeB);
        Stream.CommitReadPointer(DataSizeB);
    }
    // Native alternates store-before-pointer and pointer-before-store here.
    if (!Stream.Get(&Field_0x50, 4u)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x54)) return MarshalError;
    if (!Stream.Get(&Field_0x58, 4u)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, Field_0x5c)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, ArrayCount)) return MarshalError;
    if (ArrayCount != 0) {
        const unsigned int Width = ArrayCount * 4u;
#if defined(REMOTECLIENT_EXPORTS)
        if (MarshalCursor + Width > MarshalLimit) {
            MarshalError = kPacketDecodeOverrun;
            return MarshalError;
        }
#endif
        ArrayData = static_cast<unsigned int*>(::operator new(Width, std::nothrow));
        if (!Stream.Reserve(Width, kPacketDecodeOverrun)) return MarshalError;
        if (Width != 0) std::memcpy(ArrayData, reinterpret_cast<const void*>(MarshalPtr), Width);
        Stream.CommitReadPointer(Width);
    }
    return Stream.Finish(OutLength, false);
}

FRpcDebugLoadSystemPacket::FRpcDebugLoadSystemPacket()
    : FPacketBase(FLPACKET_CLIENT_DBGLOADSYSTEM), Data(nullptr), DataSize(0) {}

FRpcDebugLoadSystemPacket::~FRpcDebugLoadSystemPacket() {
    if (Data != nullptr) ::operator delete(Data);
}

unsigned int FRpcDebugLoadSystemPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b58e80 / RS06c06800.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&DataSize, 4u)) return kPacketEncodeOverflow;
    // Retail rereads the size after the prefix store and reserves the body unconditionally
    // (a zero size copies nothing).
    if (!Stream.Reserve(DataSize, kPacketEncodeOverflow)) return kPacketEncodeOverflow;
    Stream.PutReserved(Data, DataSize);
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcDebugLoadSystemPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, DataSize)) return MarshalError;
    if (DataSize != 0) {
#if defined(REMOTECLIENT_EXPORTS)
        // RC checks the body without committing its cursor; RS allocates first. Neither
        // decoder has an OOM-return guard.
        if (MarshalCursor + DataSize > MarshalLimit) {
            MarshalError = kPacketDecodeOverrun;
            return MarshalError;
        }
#endif
        Data = static_cast<char*>(::operator new(DataSize, std::nothrow));
        if (!Stream.Reserve(DataSize, kPacketDecodeOverrun)) return MarshalError;
        std::memcpy(Data, reinterpret_cast<const void*>(MarshalPtr), DataSize);
        Stream.CommitReadPointer(DataSize);
    }
    return Stream.Finish(OutLength, false);
}

void FRpcDebugLoadSystemPacket::CopyString(const char* Text) {
    if (Text == nullptr)
        return;
    const unsigned int Length = static_cast<unsigned int>(std::strlen(Text)) + 1u;
    Data = static_cast<char*>(::operator new(Length, std::nothrow));
    std::strcpy(Data, Text);
    DataSize = Length;
}

unsigned int FRpcDebugDestroyObjectPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b59530 / RS06c06e90.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Field_0x04, 2u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcDebugDestroyObjectPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, Field_0x00)) return MarshalError;
    if (!Stream.Get(&Field_0x04, 2u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcDebugConsolePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b59a00 / RS06c07360: the command is a fixed 0x80-byte block.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return kPacketEncodeOverflow;
    if (!Stream.PutBlock(Command, sizeof(Command))) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcDebugConsolePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 4u)) return MarshalError;
    if (!Stream.Get(Command, sizeof(Command))) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcDebugRelocateShipPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b59760 / RS06c070c0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Field_0x00, 4u)) return kPacketEncodeOverflow;
    if (!RpcWirePutVector(Stream, Position)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcDebugRelocateShipPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Field_0x00, 4u)) return MarshalError;
    if (!RpcWireGetVector(*this, Stream, Position)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcDebugFreeSystemPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b591b0 / RS06c06b10.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcDebugFreeSystemPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    return Stream.Finish(OutLength, false);
}

FRpcMunitionCollisionPacket::FRpcMunitionCollisionPacket()
    : FPacketBase(FLPACKET_CLIENT_MUNCOLLISION), ProjectileArchId(0), AttackerId(0), ObjectId(0),
      SubObjId(SUBOBJ_ID_NONE), HitPosition{0.0f, 0.0f, 0.0f} {}

FRpcObjectCollisionPacket::FRpcObjectCollisionPacket()
    : FPacketBase(FLPACKET_CLIENT_OBJCOLLISION), TargetObjectId(0), TargetSubObjId(0), ObjectId(0),
      SubObjId(0), Damage(0.0f) {}

FRpcBadlandsObjectCollisionPacket::FRpcBadlandsObjectCollisionPacket()
    : FPacketBase(FLPACKET_CLIENT_BADLANDSOBJCOLLISION), BadLandsObjId(0), SubObjId(0), ObjectId(0) {}

FRpcMineAsteroidPacket::FRpcMineAsteroidPacket()
    : FPacketBase(FLPACKET_CLIENT_MINEASTEROID), Position{0.0f, 0.0f, 0.0f} {}

FRpcSetManeuverPacket::FRpcSetManeuverPacket()
    : FPacketBase(FLPACKET_CLIENT_SETMANEUVER), ShipFrom(0), ObjectId(0), Flag(0) {}

FRpcRequestEventPacket::FRpcRequestEventPacket() : FPacketBase(FLPACKET_CLIENT_REQUEST_EVENT) {}
FRpcRequestCancelPacket::FRpcRequestCancelPacket() : FPacketBase(FLPACKET_CLIENT_REQUEST_CANCEL) {}

unsigned int FRpcMunitionCollisionPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b532f0 / RS06c01820.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ProjectileArchId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&AttackerId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&SubObjId, 2u)) return kPacketEncodeOverflow;
    if (!RpcWirePutVector(Stream, HitPosition)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcMunitionCollisionPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, ProjectileArchId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, AttackerId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, ObjectId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, SubObjId)) return MarshalError;
    if (!RpcWireGetVector(*this, Stream, HitPosition)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcObjectCollisionPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b53910 / RS06c01e40.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&SubObjId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TargetObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TargetSubObjId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.PutFloat(Damage)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcObjectCollisionPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, ObjectId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, SubObjId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, TargetObjectId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, TargetSubObjId)) return MarshalError;
    if (!Stream.Get(&Damage, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcMineAsteroidPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b53c60 / RS06c02190.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&SystemId, 4u)) return kPacketEncodeOverflow;
    if (!RpcWirePutVector(Stream, Position)) return kPacketEncodeOverflow;
    if (!Stream.Put(&LootId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&SecondaryLoot, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Quantity, 4u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcMineAsteroidPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&SystemId, 4u)) return MarshalError;
    if (!RpcWireGetVector(*this, Stream, Position)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, LootId)) return MarshalError;
    if (!Stream.Get(&SecondaryLoot, 4u)) return MarshalError;
    if (!Stream.Get(&Quantity, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcSetManeuverPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    const unsigned int* Fields[3] = { &ShipFrom, &ObjectId, &Flag };
    return RpcWireEncodeDwords(*this, Fields, 3u, BufferHolder, Capacity, OutLength);
}

unsigned int FRpcSetManeuverPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    unsigned int* Fields[3] = { &ShipFrom, &ObjectId, &Flag };
    return RpcWireDecodeDwords(*this, Fields, 3u, Buffer, Size, OutLength);
}

unsigned int FRpcRequestEventPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b59f60 / RS06bf2440.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&EventType, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ShipId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TargetId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&SecondId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Field_0x10, 1u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcRequestEventPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, EventType)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, ShipId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, TargetId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, SecondId)) return MarshalError;
    if (!Stream.Get(&Field_0x10, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcRequestCancelPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b5a280 / RS06bf2760.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&EventType, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ShipId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TargetId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Field_0x0c, 1u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcRequestCancelPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, EventType)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, ShipId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, TargetId)) return MarshalError;
    if (!Stream.Get(&Field_0x0c, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcBadlandsObjectCollisionPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b53690 / RS06c01bc0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&SubObjId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&BadLandsObjId, 4u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcBadlandsObjectCollisionPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!RpcWireGetThenStore(Stream, ObjectId)) return MarshalError;
    if (!RpcWireGetThenStore(Stream, SubObjId)) return MarshalError;
    if (!Stream.Get(&BadLandsObjId, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int RpcWireEncodeDwords(FPacketBase& Packet, const unsigned int* const* Fields,
    unsigned int FieldCount, unsigned char** BufferHolder, unsigned int Capacity,
    unsigned int* OutLength) {
    // Shared by the DWORD-record encoders (e.g. CLIENT 0x2b RC06b3ada0, 0x37 RC06b559c0,
    // 0x2f RC06b59cb0).
    FRpcWireStream Stream(Packet, BufferHolder, Capacity);
    Stream.PutHeader();
    for (unsigned int Index = 0; Index < FieldCount; ++Index) {
        if (!Stream.Put(Fields[Index], 4u)) return kPacketEncodeOverflow;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int RpcWireDecodeDwords(FPacketBase& Packet, unsigned int* const* Fields,
    unsigned int FieldCount, const unsigned char* Buffer, unsigned int Size,
    unsigned int* OutLength) {
    FRpcWireStream Stream(Packet, const_cast<unsigned char*>(Buffer), Size);
    for (unsigned int Index = 0; Index < FieldCount; ++Index) {
        if (Index + 1u < FieldCount) {
            // Native commits the moving pointer before storing every non-final
            // payload field; the final field is stored before pointer commit.
            unsigned int Value;
            if (!Stream.Get(&Value, 4u)) return Packet.MarshalError;
            *Fields[Index] = Value;
        } else {
            if (!Stream.Get(Fields[Index], 4u)) return Packet.MarshalError;
        }
    }
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcRequestInvincibilityPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ShipId, 4u)) return MarshalError;
    if (!Stream.Get(&Reason, 1u)) return MarshalError;
    if (!Stream.Get(&bInvincible, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcRequestInvincibilityPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b5b150 / RS06c081e0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ShipId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Reason, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bInvincible, 1u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcRequestUseItemPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&ItemId, 2u)) return MarshalError;
    if (!Stream.Get(&Count, 2u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcRequestUseItemPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b5ae70 / RS06c07f00.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ItemId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Count, 2u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcRequestRemoveItemPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&EquipId, 4u)) return MarshalError;
    if (!Stream.Get(&Count, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcRequestRemoveItemPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b52590 / RS06c01400: failure returns 0x16 without rewriting the sticky Error.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&EquipId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Count, 4u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

FRpcRequestInvincibilityPacket::FRpcRequestInvincibilityPacket()
    : FPacketBase(FLPACKET_CLIENT_REQINVINCIBILITY), ShipId(0), Reason(0), bInvincible(0) {}

FRpcRequestUseItemPacket::FRpcRequestUseItemPacket()
    : FPacketBase(FLPACKET_CLIENT_REQUEST_USE_ITEM), ObjectId(0), ItemId(0), Count(0) {}

FRpcRequestRemoveItemPacket::FRpcRequestRemoveItemPacket()
    : FPacketBase(FLPACKET_CLIENT_REQREMOVEITEM), EquipId(0), Count(-1) {}

FRpcItemChangePacketBase::FRpcItemChangePacketBase(unsigned int PacketTypeId)
    : FPacketBase(PacketTypeId), Count(0), Health(0.0f), bMounted(0),
      HardpointSize(0), Hardpoint(nullptr) {}

FRpcItemChangePacketBase::~FRpcItemChangePacketBase() {
    if (Hardpoint != nullptr) ::operator delete(Hardpoint);
}

void FRpcItemChangePacketBase::CopyHardpoint(const void* Data, unsigned int Size) {
    if (Size == 0 || Data == nullptr)
        return;
    // Replacing an existing pointer without freeing it is native behavior.
    Hardpoint = static_cast<char*>(::operator new(Size, std::nothrow));
    std::memcpy(Hardpoint, Data, Size);
    HardpointSize = Size;
}

unsigned int FRpcItemChangePacketBase::EncodeFields(const unsigned int& Selector,
    unsigned char** BufferHolder, unsigned int Capacity, unsigned int* OutLength) {
    // RC06b52820 / RS06be2d50 (ADDITEM) and RC06b52cc0 / RS06be31e0 (MODIFYITEM).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Selector, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Count, 4u)) return kPacketEncodeOverflow;
    if (!Stream.PutFloat(Health)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bMounted, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&HardpointSize, 4u)) return kPacketEncodeOverflow;
    if (HardpointSize != 0 && !Stream.PutBlock(Hardpoint, HardpointSize)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcItemChangePacketBase::DecodeFields(unsigned int& Selector,
    const unsigned char* Buffer, unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Selector, 4u)) return MarshalError;
    if (!Stream.Get(&Count, 4u)) return MarshalError;
    if (!Stream.Get(&Health, 4u)) return MarshalError;
    if (!Stream.Get(&bMounted, 1u)) return MarshalError;
    if (!Stream.Get(&HardpointSize, 4u)) return MarshalError;
    if (HardpointSize != 0) {
#if defined(REMOTECLIENT_EXPORTS)
        // RC's preflight does not commit the body cursor. RS allocates before the bounds
        // check and has no allocation-failure return.
        if (MarshalCursor + HardpointSize > MarshalLimit) {
            MarshalError = kPacketDecodeOverrun;
            return MarshalError;
        }
#endif
        Hardpoint = static_cast<char*>(::operator new(HardpointSize, std::nothrow));
#if defined(REMOTECLIENT_EXPORTS)
        if (Hardpoint == nullptr) return kPacketNoMemory;
#endif
        if (!Stream.Get(Hardpoint, HardpointSize)) return MarshalError;
    }
    return Stream.Finish(OutLength, false);
}

FRpcClientInfoRequestPacketBase::FRpcClientInfoRequestPacketBase(unsigned int PacketTypeId)
    : FPacketBase(PacketTypeId) {}

unsigned int FRpcClientInfoRequestPacketBase::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b54f70 / RS06c03190 (BASEINFO) and RC06b551d0 / RS06c033f0 (LOCATIONINFO).
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&Id, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bFull, 1u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcClientInfoRequestPacketBase::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&Id, 4u)) return MarshalError;
    if (!Stream.Get(&bFull, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

void FRpcRequestLaunchPacket::Dump(std::ostream& Out) const {
    Out << "   Object ID: " << Value << "\n";
}

FRpcCreateNewCharacterPacket::FRpcCreateNewCharacterPacket()
    : FPacketBase(FLPACKET_CLIENT_CREATENEWCHAR) {}

FRpcCharacterIdPacketBase::FRpcCharacterIdPacketBase(unsigned int Id)
    : FPacketBase(Id) {
    // Native sets only Buffer[0]; the port's CHARACTER_ID also initializes the unused tail,
    // which the strlen-based wire body never reaches.
    CharacterId.Buffer[0] = '\0';
}

unsigned int FRpcCharacterIdPacketBase::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b56560 / RS06c03f80 (SELECTCHARACTER) and RC06b562a0 / RS06c03cd0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    const unsigned short Length = static_cast<unsigned short>(
        std::strlen(CharacterId.Buffer));
    if (!Stream.Put(&Length, 2u)) return kPacketEncodeOverflow;
    if (!Stream.PutBlock(CharacterId.Buffer, Length)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcCharacterIdPacketBase::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    unsigned short Length;
    if (!Stream.Get(&Length, 2u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
    // Only the RC body has this explicit error; the prefix pointer is committed
    // already, MarshalError remains zero and the body length is not reserved.
    if (Length >= 0x200u) return 0x4au;
#endif
    if (!Stream.Get(CharacterId.Buffer, Length)) return MarshalError;
    CharacterId.Buffer[Length] = '\0';
    return Stream.Finish(OutLength, false);
}

// These string helpers return an error without writing MarshalError. Their
// strict capacity comparison and delayed pointer commit differ from scalar IO.
static unsigned int RpcEncodeWideString(FPacketBase& Packet, const wchar_t* Value) {
    unsigned char* Destination = reinterpret_cast<unsigned char*>(Packet.MarshalPtr);
    Packet.MarshalCursor += 2u;
    if (Packet.MarshalCursor >= Packet.MarshalLimit) return kPacketEncodeOverflow;
    const unsigned short Length = static_cast<unsigned short>(std::wcslen(Value));
    std::memcpy(Destination, &Length, 2u);
    const unsigned int ByteCount = static_cast<unsigned int>(Length) * 2u;
    Packet.MarshalCursor += ByteCount;
    Destination += 2u;
    if (Packet.MarshalCursor >= Packet.MarshalLimit) return kPacketEncodeOverflow;
    std::memcpy(Destination, Value, ByteCount);
    Packet.MarshalPtr = reinterpret_cast<unsigned int>(Destination + ByteCount);
    return 0;
}

static unsigned int RpcDecodeWideString(FPacketBase& Packet, wchar_t** Value) {
    const unsigned char* Source = reinterpret_cast<const unsigned char*>(Packet.MarshalPtr);
    Packet.MarshalCursor += 2u;
    if (Packet.MarshalCursor >= Packet.MarshalLimit) return kPacketDecodeOverrun;
    unsigned short Length;
    std::memcpy(&Length, Source, 2u);
    Source += 2u;
    const unsigned int ByteCount = static_cast<unsigned int>(Length) * 2u;
    Packet.MarshalCursor += ByteCount;
    if (Packet.MarshalCursor >= Packet.MarshalLimit) return kPacketDecodeOverrun;
    *Value = nullptr;
    if (Length != 0) {
        *Value = static_cast<wchar_t*>(::operator new(ByteCount + 2u, std::nothrow));
        // Native has no OOM branch. A zero-length value leaves the input pointer untouched.
        std::memcpy(*Value, Source, ByteCount);
        (*Value)[Length] = L'\0';
        Packet.MarshalPtr = reinterpret_cast<unsigned int>(Source + ByteCount);
    }
    return 0;
}

unsigned int FRpcCreateNewCharacterPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b55eb0 / RS06c038b0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (RpcEncodeWideString(*this, Name) != 0) return kPacketEncodeOverflow;
    if (!Stream.Put(&FactionId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&BaseId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&PackageId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&PilotId, 4u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcCreateNewCharacterPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    wchar_t* Temporary;
    if (RpcDecodeWideString(*this, &Temporary) != 0) return kPacketDecodeOverrun;
    if (Temporary != nullptr) {
#if defined(REMOTECLIENT_EXPORTS)
        std::wcsncpy(Name, Temporary, 24u);
        Name[23] = L'\0';
#else
        // RS uses an unbounded wcscpy.
        std::wcscpy(Name, Temporary);
#endif
        ::operator delete(Temporary);
    }
    if (!Stream.Get(&FactionId, 4u)) return MarshalError;
    if (!Stream.Get(&BaseId, 4u)) return MarshalError;
    if (!Stream.Get(&PackageId, 4u)) return MarshalError;
    if (!Stream.Get(&PilotId, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

void FRpcCharacterIdPacketBase::Dump(std::ostream& Out) const {
    Out << "   Character: " << CharacterId.Buffer << "\n";
}

void FRpcCreateNewCharacterPacket::Dump(std::ostream& Out) const {
    // Retail (both DLLs) inserts the name's address via operator<<(const void*), not its text.
    Out << "   Name: " << static_cast<const void*>(Name)
        << "   Fac: " << FactionId << "   Base: " << BaseId
        << "   Pkg: " << PackageId << "   Pilot: " << PilotId << "\n";
}

FRpcUpdateObjectPacket::FRpcUpdateObjectPacket() : FPacketBase(FLPACKET_COMMON_UPDATEOBJECT) {
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    Orientation.w = 1.0f;
    Orientation.x = 0.0f;
    Orientation.y = 0.0f;
    Orientation.z = 0.0f;
}

FRpcPlayerTradePacket::FRpcPlayerTradePacket()
    : FPacketBase(FLPACKET_COMMON_PLAYER_TRADE), Action(0), Reason(0), TargetPlayer(0), Money(0),
      bAccepted(0), EquipArchId(0xffffffffu), EquipCount(0), EquipHealth(0.0f) {}

unsigned int FRpcPlayerTradePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b68c90 / RS06bfff80.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    const unsigned char ActionByte = static_cast<unsigned char>(Action);
    if (!Stream.Put(&ActionByte, 1u)) return kPacketEncodeOverflow;
    const bool bIsMPServer = IsMPServer();
    bool bCountTarget = bIsMPServer;
    bool bNeedTarget = true;
    // The selector is the full DWORD, even though only its low byte was emitted.
    switch (Action) {
    case kPlayerTradeInitiateTrade:
    case kPlayerTradeRequestTrade:
    case kPlayerTradeStopTradeRequest:
        bCountTarget = true;
        break;
    case kPlayerTradeTerminateTrade: {
        const unsigned char ReasonByte = static_cast<unsigned char>(Reason);
        if (!Stream.Put(&ReasonByte, 1u)) return kPacketEncodeOverflow;
        bNeedTarget = false;
        break;
    }
    case kPlayerTradeSetTradeMoney:
        if (!Stream.Put(&Money, 4u)) return kPacketEncodeOverflow;
        break;
    case kPlayerTradeAddTradeEquip:
        if (!Stream.Put(&EquipArchId, 4u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&EquipCount, 4u)) return kPacketEncodeOverflow;
        if (!Stream.PutFloat(EquipHealth)) return kPacketEncodeOverflow;
        if (!Stream.Put(&EquipId, 2u)) return kPacketEncodeOverflow;
        break;
    case kPlayerTradeDelTradeEquip:
        if (!Stream.Put(&EquipArchId, 4u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&EquipCount, 4u)) return kPacketEncodeOverflow;
        if (!Stream.Put(&EquipId, 2u)) return kPacketEncodeOverflow;
        break;
    case kPlayerTradeAcceptTrade:
        if (!Stream.Put(&bAccepted, 1u)) return kPacketEncodeOverflow;
        break;
    default:
        bNeedTarget = false;
        break;
    }
    if (bNeedTarget) {
        // The target DWORD is counted only in this direction, but the sticky Error is tested
        // on every path before it is written.
        if (bCountTarget) Stream.Reserve(4u, kPacketEncodeOverflow);
        if (MarshalError != 0) return kPacketEncodeOverflow;
        Stream.PutReserved(&TargetPlayer, 4u);
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcPlayerTradePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    unsigned char ActionByte;
    if (!Stream.Get(&ActionByte, 1u)) return MarshalError;
    Action = ActionByte;
    const bool bCountDirectedTarget = !IsMPServer();
    bool bCountTarget = bCountDirectedTarget;
    bool bNeedTarget = true;
    switch (Action) {
    case kPlayerTradeInitiateTrade:
    case kPlayerTradeRequestTrade:
    case kPlayerTradeStopTradeRequest:
        bCountTarget = true;
        break;
    case kPlayerTradeTerminateTrade: {
        signed char ReasonByte;
        if (!Stream.Get(&ReasonByte, 1u)) return MarshalError;
        Reason = ReasonByte;
        bNeedTarget = false;
        break;
    }
    case kPlayerTradeSetTradeMoney:
        if (!Stream.Get(&Money, 4u)) return MarshalError;
        break;
    case kPlayerTradeAddTradeEquip:
        if (!Stream.Get(&EquipArchId, 4u)) return MarshalError;
        if (!Stream.Get(&EquipCount, 4u)) return MarshalError;
        if (!Stream.Get(&EquipHealth, 4u)) return MarshalError; // Native integer bit-copy.
        if (!Stream.Get(&EquipId, 2u)) return MarshalError;
        break;
    case kPlayerTradeDelTradeEquip:
        if (!Stream.Get(&EquipArchId, 4u)) return MarshalError;
        if (!Stream.Get(&EquipCount, 4u)) return MarshalError;
        if (!Stream.Get(&EquipId, 2u)) return MarshalError;
        break;
    case kPlayerTradeAcceptTrade: {
        unsigned char Accepted;
        if (!Stream.Get(&Accepted, 1u)) return MarshalError;
        bAccepted = static_cast<unsigned char>(Accepted != 0);
        if (bCountDirectedTarget && !Stream.Reserve(4u, kPacketEncodeOverflow)) return MarshalError;
        // Native decoder bug kept: writes the packet's TargetPlayer INTO the input instead of
        // reading it (RS06c004ab / RC06b691bb). The target stays unchanged; the input advances.
        Stream.PutUncounted(&TargetPlayer, 4u);
        bNeedTarget = false;
        break;
    }
    default:
        bNeedTarget = false;
        break;
    }
    if (bNeedTarget) {
        if (bCountTarget && !Stream.Reserve(4u, kPacketDecodeOverrun)) return MarshalError;
        Stream.GetUncounted(&TargetPlayer, 4u);
    }
    return Stream.Finish(OutLength, false);
}

FRpcChatMsgPacket::FRpcChatMsgPacket()
    : FPacketBase(FLPACKET_COMMON_CHATMSG), TargetId(0), SenderId(0), DataSize(0), Data(nullptr) {}

FRpcChatMsgPacket::~FRpcChatMsgPacket() {
    if (Data != nullptr) ::operator delete(Data);
}

unsigned int FRpcChatMsgPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b678c0 / RS06bfebd0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&DataSize, 4u)) return kPacketEncodeOverflow;
    if (!Stream.PutBlock(Data, DataSize)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TargetId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&SenderId, 4u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcChatMsgPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&DataSize, 4u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
    if (MarshalCursor + DataSize > MarshalLimit) {
        MarshalError = kPacketDecodeOverrun;
        return MarshalError;
    }
#endif
    // Both native decoders replace the pointer unconditionally (leaking any previous one).
    Data = static_cast<unsigned char*>(::operator new(DataSize, std::nothrow));
#if defined(REMOTECLIENT_EXPORTS)
    if (Data == nullptr) return kPacketNoMemory; // MarshalError and the decoded size remain intact.
#endif
    if (!Stream.Get(Data, DataSize)) return MarshalError;
    if (!Stream.Get(&TargetId, 4u)) return MarshalError;
    if (!Stream.Get(&SenderId, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

FRpcPopUpDialogPacket::FRpcPopUpDialogPacket()
    : FPacketBase(FLPACKET_COMMON_POP_UP_DIALOG), SizeA(0), DataA(nullptr), SizeB(0), DataB(nullptr), Buttons(0) {}

FRpcPopUpDialogPacket::~FRpcPopUpDialogPacket() {
    if (DataB != nullptr) ::operator delete(DataB);
    if (DataA != nullptr) ::operator delete(DataA);
}

unsigned int FRpcPopUpDialogPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b66a10 / RS06bfdde0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&SizeA, 4u)) return kPacketEncodeOverflow;
    if (!Stream.PutBlock(DataA, SizeA)) return kPacketEncodeOverflow;
    if (!Stream.Put(&SizeB, 4u)) return kPacketEncodeOverflow;
    // Native announces SizeB but reserves and copies SizeA from the second block.
    if (!Stream.PutBlock(DataB, SizeA)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Buttons, 4u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

FRpcCommonBufferPacketBase::FRpcCommonBufferPacketBase(unsigned int Id,
    bool bInNullTerminate)
    : FPacketBase(Id), Data(nullptr), bOwnsData(false), bNullTerminate(bInNullTerminate) {}

FRpcCommonBufferPacketBase::~FRpcCommonBufferPacketBase() {
    if (bOwnsData && Data != nullptr) {
        ::operator delete(Data);
        Data = nullptr;
    }
}

void FRpcCommonBufferPacketBase::BorrowData(unsigned char* InData, unsigned int Size) {
    Data = InData;
    DataSize = Size;
}

void FRpcCommonBufferPacketBase::AdoptData(unsigned char* InData, unsigned int Size) {
    Data = InData;
    DataSize = Size;
    bOwnsData = true;
}

void FRpcCommonBufferPacketBase::CopyData(const void* InData, unsigned int Size) {
    Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
    std::memcpy(Data, InData, Size);
    DataSize = Size;
    bOwnsData = true;
}

void FRpcCommonBufferPacketBase::CopyDataIfNull(const void* InData, unsigned int Size) {
    if (Data == nullptr) {
        Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
        bOwnsData = true;
    }
    std::memcpy(Data, InData, Size);
    DataSize = Size;
}

unsigned int FRpcCommonBufferPacketBase::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // Eight COMMON bodies, e.g. COMMON 0x11 RC06b65e50 / RS06bfd2d0: the size is reread after
    // the prefix store and reserved unconditionally; failure returns 0x16 without rewriting the
    // sticky Error.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&DataSize, 4u)) return kPacketEncodeOverflow;
    if (!Stream.PutBlock(Data, DataSize)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcCommonBufferPacketBase::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&DataSize, 4u)) return MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
    // RC (06b66006) preflights without committing the cursor; RS (06bfd480) allocates before
    // its only body check.
    if (MarshalCursor + DataSize > MarshalLimit) {
        MarshalError = kPacketDecodeOverrun;
        return MarshalError;
    }
#endif
    if (Data == nullptr) {
        // Only SET_WEAPON_GROUP reserves a terminator. Allocation precedes the
        // payload bounds check, and an existing pointer is never resized.
        const unsigned int AllocationSize = DataSize + (bNullTerminate ? 1u : 0u);
        Data = static_cast<unsigned char*>(::operator new(AllocationSize, std::nothrow));
#if defined(REMOTECLIENT_EXPORTS)
        // RC returns allocation error 5 with MarshalError still zero and without
        // changing ownership; RS has no allocation-failure branch here.
        if (Data == nullptr) return kPacketNoMemory;
#endif
        bOwnsData = true;
    }
    if (!Stream.Get(Data, DataSize)) return MarshalError;
    if (bNullTerminate) Data[DataSize] = 0;
    return Stream.Finish(OutLength, false);
}

unsigned int RpcWireDecodeOwnedBlock(FPacketBase& Packet, FRpcWireStream& Stream,
    unsigned int& BlockSize, unsigned char*& Data) {
    if (!Stream.Get(&BlockSize, 4u)) return Packet.MarshalError;
#if defined(REMOTECLIENT_EXPORTS)
    if (Packet.MarshalCursor + BlockSize > Packet.MarshalLimit) {
        Packet.MarshalError = kPacketDecodeOverrun;
        return Packet.MarshalError;
    }
#endif
    Data = static_cast<unsigned char*>(::operator new(BlockSize, std::nothrow));
#if defined(REMOTECLIENT_EXPORTS)
    if (Data == nullptr) {
        BlockSize = 0;
        return kPacketNoMemory; // MarshalError remains zero; each caller handles this return.
    }
#endif
    if (!Stream.Reserve(BlockSize, kPacketDecodeOverrun)) return Packet.MarshalError;
    // A zero-size copy touches neither pointer (RS may get null from operator new(0));
    // allocation is still unconditional.
    if (BlockSize != 0)
        std::memcpy(Data, reinterpret_cast<const void*>(Packet.MarshalPtr), BlockSize);
    Stream.CommitReadPointer(BlockSize);
    return 0;
}

namespace {
const double GFireSmallLimit = 64.0;             // PE 06c0e338
const double GFireMediumLimit = 8192.0;          // PE 06c0e330
const float GFireSmallPreScale = 0.015625f;       // PE 06c0e32c: 0000803c
const float GFireMediumPreScale = 0.0001220703125f; // PE 06c0e328: 00000039
const double GFireSmallScale = 127.0;            // PE 06c0df40
const double GFireMediumScale = 32767.0;          // PE 06c0e320
const double GFireSmallInverse = 64.0 / 127.0;    // PE 06c0e348: 080402814020e03f
const double GFireMediumInverse = 8192.0 / 32767.0; // PE 06c0e340: 800040002000d03f
const double GUpdateInverseScale = 1.0 / 127.0;   // PE 06c0df48: 080402814020803f

// UPDATEOBJECT performs FLD/FMUL before committing the position or W pointer, and only then
// calls _ftol; the ordering matters around an unmasked FP exception. The commit stores the
// destination cached before the preceding payload write plus its width; it never rereads Ptr.
unsigned char RpcUpdateQuantize(const float& Value, unsigned int* PointerToCommit,
    unsigned int CommittedPointer) {
    const float* Source = &Value;
    unsigned short OldControl;
    unsigned short TruncateControl;
    __int64 Quantized;
    __asm {
        mov ecx, Source
        fld dword ptr [ecx]
        fmul qword ptr GFireSmallScale
        mov ecx, PointerToCommit
        test ecx, ecx
        jz UpdateConversion
        mov eax, CommittedPointer
        mov dword ptr [ecx], eax
    UpdateConversion:
        fnstcw OldControl
        mov ax, OldControl
        or ax, 0c00h
        mov TruncateControl, ax
        fldcw TruncateControl
        fistp qword ptr Quantized
        fldcw OldControl
    }
    return static_cast<unsigned char>(Quantized);
}

void RpcUpdateExpand(signed char Encoded, float& Value) {
    const int Quantized = Encoded;
    float* Destination = &Value;
    __asm {
        mov ecx, Destination
        fild dword ptr Quantized
        fmul qword ptr GUpdateInverseScale
        fstp dword ptr [ecx]
    }
}

// Retail's x87 test sends equal, greater and unordered (NaN) on to the next tier; only
// strictly-less selects a tier. Z has only the small comparison in retail.
unsigned char RpcFirePickTier(const float& Value, bool bHasMediumTier) {
    const float* Source = &Value;
    unsigned char Tier;
    if (bHasMediumTier) {
        __asm {
            mov ecx, Source
            xor edx, edx
            fld dword ptr [ecx]
            fabs
            fcom qword ptr GFireSmallLimit
            fnstsw ax
            test ah, 5
            jp FireTryMedium
            fstp st(0)
            mov dl, 1
            jmp FireTierDone
        FireTryMedium:
            fcomp qword ptr GFireMediumLimit
            fnstsw ax
            test ah, 5
            jp FireTierDone
            mov dl, 2
        FireTierDone:
            mov Tier, dl
        }
    } else {
        __asm {
            mov ecx, Source
            xor edx, edx
            fld dword ptr [ecx]
            fabs
            fcomp qword ptr GFireSmallLimit
            fnstsw ax
            test ah, 5
            jp FireSmallDone
            mov dl, 1
        FireSmallDone:
            mov Tier, dl
        }
    }
    return Tier;
}

// Native FMUL float followed by FMUL double, then _ftol's truncating signed
// integer conversion. Tier selection bounds the finite result to +/-32767.
unsigned int RpcFireQuantize(const float& Value, bool bMedium) {
    const float* Source = &Value;
    const float* PreScale = bMedium ? &GFireMediumPreScale : &GFireSmallPreScale;
    const double* Scale = bMedium ? &GFireMediumScale : &GFireSmallScale;
    unsigned short OldControl;
    unsigned short TruncateControl;
    __int64 Quantized;
    __asm {
        mov ecx, Source
        mov eax, PreScale
        mov edx, Scale
        fld dword ptr [ecx]
        fmul dword ptr [eax]
        fmul qword ptr [edx]
        fnstcw OldControl
        mov ax, OldControl
        or ax, 0c00h
        mov TruncateControl, ax
        fldcw TruncateControl
        fistp qword ptr Quantized
        fldcw OldControl
    }
    return static_cast<unsigned int>(Quantized);
}

bool RpcFirePutComponent(FRpcWireStream& Stream, const float& Value,
    unsigned char Flags, unsigned char SmallBit, unsigned char MediumBit) {
    if ((Flags & SmallBit) != 0) {
        const unsigned char Quantized = static_cast<unsigned char>(RpcFireQuantize(Value, false));
        return Stream.Put(&Quantized, 1u);
    }
    if ((Flags & MediumBit) != 0) {
        const unsigned short Quantized = static_cast<unsigned short>(RpcFireQuantize(Value, true));
        return Stream.Put(&Quantized, 2u);
    }
    return Stream.PutFloat(Value);
}

bool RpcFireGetComponent(FRpcWireStream& Stream, float& Value,
    unsigned char Flags, unsigned char SmallBit, unsigned char MediumBit,
    bool bSmallCommitBefore, bool bMediumCommitBefore) {
    int Quantized;
    unsigned int Width;
    bool bCommitBefore;
    const double* Scale;
    if ((Flags & SmallBit) != 0) {
        signed char Encoded;
        if (!Stream.GetBeforePointerCommit(&Encoded, 1u)) return false;
        Quantized = Encoded;
        Width = 1u;
        bCommitBefore = bSmallCommitBefore;
        Scale = &GFireSmallInverse;
    } else if ((Flags & MediumBit) != 0) {
        short Encoded;
        if (!Stream.GetBeforePointerCommit(&Encoded, 2u)) return false;
        Quantized = Encoded;
        Width = 2u;
        bCommitBefore = bMediumCommitBefore;
        Scale = &GFireMediumInverse;
    } else {
        // Uncompressed decoding copies integer bits, unlike float encoding.
        return Stream.Get(&Value, 4u);
    }
    if (bCommitBefore) Stream.CommitReadPointer(Width);
    float* Destination = &Value;
    __asm {
        mov eax, Scale
        mov ecx, Destination
        fild dword ptr Quantized
        fmul qword ptr [eax]
        fstp dword ptr [ecx]
    }
    if (!bCommitBefore) Stream.CommitReadPointer(Width);
    return true;
}
} // namespace

unsigned int FRpcUpdateObjectPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b64c70 / RS06bfc0f0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&State, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Reserve(12u, kPacketEncodeOverflow)) return kPacketEncodeOverflow;
    // Integer copy of the position; the pointer commit is delayed past the next FMUL.
    unsigned int PositionBits[3];
    std::memcpy(&PositionBits[0], &Position.x, 4u);
    std::memcpy(&PositionBits[1], &Position.y, 4u);
    std::memcpy(&PositionBits[2], &Position.z, 4u);
    const unsigned int PositionDestination = MarshalPtr;
    std::memcpy(reinterpret_cast<void*>(PositionDestination), PositionBits, 12u);
    // All four conversions precede the first quaternion-byte capacity check.
    const unsigned char X = RpcUpdateQuantize(Orientation.x, &MarshalPtr,
        PositionDestination + 12u);
    const unsigned char Y = RpcUpdateQuantize(Orientation.y, nullptr, 0u);
    const unsigned char Z = RpcUpdateQuantize(Orientation.z, nullptr, 0u);
    const unsigned char W = RpcUpdateQuantize(Orientation.w, nullptr, 0u);
    if (!Stream.Put(&X, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Y, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Z, 1u)) return kPacketEncodeOverflow;
    // W is stored at the cached destination, the extra value's FLD/FMUL follows, then the
    // pointer commit, then _ftol.
    if (!Stream.Reserve(1u, kPacketEncodeOverflow)) return kPacketEncodeOverflow;
    const unsigned int WDestination = MarshalPtr;
    std::memcpy(reinterpret_cast<void*>(WDestination), &W, 1u);
    const unsigned char Extra = RpcUpdateQuantize(Throttle, &MarshalPtr, WDestination + 1u);
    if (!Stream.Put(&Extra, 1u)) return kPacketEncodeOverflow;
    if (!Stream.PutFloat(UpdateTime)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcUpdateObjectPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&State, 1u)) return MarshalError;
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&Position, 12u)) return MarshalError;
    signed char X;
    signed char Y;
    signed char Z;
    signed char W;
    // Native reads and commits all four bytes before modifying the quaternion.
    if (!Stream.Get(&X, 1u)) return MarshalError;
    if (!Stream.Get(&Y, 1u)) return MarshalError;
    if (!Stream.Get(&Z, 1u)) return MarshalError;
    if (!Stream.Get(&W, 1u)) return MarshalError;
    RpcUpdateExpand(X, Orientation.x);
    RpcUpdateExpand(Y, Orientation.y);
    RpcUpdateExpand(Z, Orientation.z);
    RpcUpdateExpand(W, Orientation.w);
    signed char Extra;
    if (!Stream.Get(&Extra, 1u)) return MarshalError;
    RpcUpdateExpand(Extra, Throttle);
    if (!Stream.Get(&UpdateTime, 4u)) return MarshalError; // Integer bit-copy, no x87.
    return Stream.Finish(OutLength, false);
}

unsigned int FRpcPopUpDialogPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    // Retail ignores both helper results, including RC's allocation error 5.
    // A sticky marshal error still advances each later checked field's cursor.
    RpcWireDecodeOwnedBlock(*this, Stream, SizeA, DataA);
    RpcWireDecodeOwnedBlock(*this, Stream, SizeB, DataB);
    if (!Stream.Get(&Buttons, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

// CLIENT_LOGIN: the native constructor leaves both string buffers and the creation byte
// uninitialized.
FRpcClientLoginPacket::FRpcClientLoginPacket() : FPacketBase(FLPACKET_CLIENT_LOGIN) {}

unsigned int FRpcClientLoginPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b56850 / RS06c04260.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&CreateFlag, 1u)) return kPacketEncodeOverflow;
    const unsigned char AccountLength = static_cast<unsigned char>(std::wcslen(AccountName));
    const unsigned char SecondLength = static_cast<unsigned char>(std::wcslen(Password));
    if (!Stream.Put(&AccountLength, 1u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&SecondLength, 1u)) return kPacketEncodeOverflow;
    if (AccountLength != 0) {
        // This mutation precedes the account-buffer capacity check in retail.
        _wcslwr(AccountName);
        if (!Stream.PutBlock(AccountName, static_cast<unsigned int>(AccountLength) * 2u)) return kPacketEncodeOverflow;
    }
    if (SecondLength != 0) {
        if (!Stream.PutBlock(Password, static_cast<unsigned int>(SecondLength) * 2u)) return kPacketEncodeOverflow;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcClientLoginPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    // Native clears exactly 0x402 bytes, including CreateFlag and one pad byte.
    std::memset(AccountName, 0, sizeof(AccountName));
    std::memset(Password, 0, sizeof(Password));
    CreateFlag = 0;
    Padding[0] = 0;
    if (!Stream.Get(&CreateFlag, 1u)) return MarshalError;
    unsigned char AccountLength;
    unsigned char SecondLength;
    if (!Stream.Get(&AccountLength, 1u)) return MarshalError;
    if (!Stream.Get(&SecondLength, 1u)) return MarshalError;
    if (AccountLength != 0) {
        if (!Stream.Get(AccountName, static_cast<unsigned int>(AccountLength) * 2u)) return MarshalError;
        AccountName[AccountLength] = L'\0';
    }
    if (SecondLength != 0) {
        if (!Stream.Get(Password, static_cast<unsigned int>(SecondLength) * 2u)) return MarshalError;
        Password[SecondLength] = L'\0';
    }
    return Stream.Finish(OutLength, false);
}

void FRpcClientLoginPacket::Dump(std::ostream& Out) const {
    Out << "   Account: " << static_cast<const void*>(AccountName) << "\n";
}

FRpcFireWeaponPacket::FRpcFireWeaponPacket() : FPacketBase(FLPACKET_COMMON_FIREWEAPON), ObjectId(0) {
    TargetPos.x = FLT_MAX;
    TargetPos.y = FLT_MAX;
    TargetPos.z = FLT_MAX;
}

unsigned int FRpcFireWeaponPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b651f0 / RS06bfc670.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (SinglePlayer()) {
        if (!RpcWirePutVector(Stream, TargetPos)) return kPacketEncodeOverflow;
        const unsigned char Count = static_cast<unsigned char>(Hardpoints.size());
        if (!Stream.Put(&Count, 1u)) return kPacketEncodeOverflow;
    } else {
        unsigned char Flags = RpcFirePickTier(TargetPos.x, true);
        Flags |= static_cast<unsigned char>(RpcFirePickTier(TargetPos.y, true) << 2);
        Flags |= static_cast<unsigned char>(RpcFirePickTier(TargetPos.z, false) << 4);
        if (Hardpoints.size() == 1u) Flags |= kFireWeaponOneHardpoint;
        if (!Stream.Put(&Flags, 1u)) return kPacketEncodeOverflow;
        if (!RpcFirePutComponent(Stream, TargetPos.x, Flags, kFireWeaponXSmall, kFireWeaponXMedium)) return kPacketEncodeOverflow;
        if (!RpcFirePutComponent(Stream, TargetPos.y, Flags, kFireWeaponYSmall, kFireWeaponYMedium)) return kPacketEncodeOverflow;
        if (!RpcFirePutComponent(Stream, TargetPos.z, Flags, kFireWeaponZSmall, kFireWeaponZMedium)) return kPacketEncodeOverflow;
        const unsigned char Count = static_cast<unsigned char>(Hardpoints.size());
        // The full-size one-element flag and this low-byte test differ at 257.
        if (Count != 1u && !Stream.Put(&Count, 1u)) return kPacketEncodeOverflow;
    }
    for (std::vector<unsigned short>::const_iterator It = Hardpoints.begin(); It != Hardpoints.end(); ++It) {
        if (!Stream.Put(&*It, 2u)) return kPacketEncodeOverflow;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcFireWeaponPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    unsigned char Count;
    if (SinglePlayer()) {
        if (!Stream.Get(&TargetPos, 12u)) return MarshalError;
        if (!Stream.Get(&Count, 1u)) return MarshalError;
    } else {
        unsigned char Flags;
        if (!Stream.Get(&Flags, 1u)) return MarshalError;
        // Native commits its stream pointer on different sides of FSTP for
        // these branches; preserve that ordering even for an FP exception.
        if (!RpcFireGetComponent(Stream, TargetPos.x, Flags, kFireWeaponXSmall, kFireWeaponXMedium, false, true)) return MarshalError;
        if (!RpcFireGetComponent(Stream, TargetPos.y, Flags, kFireWeaponYSmall, kFireWeaponYMedium, true, false)) return MarshalError;
        if (!RpcFireGetComponent(Stream, TargetPos.z, Flags, kFireWeaponZSmall, kFireWeaponZMedium, true, false)) return MarshalError;
        if ((Flags & kFireWeaponOneHardpoint) != 0) Count = 1u;
        else if (!Stream.Get(&Count, 1u)) return MarshalError;
    }
    for (unsigned int Index = 0; Index < Count; ++Index) {
        unsigned short Hardpoint;
        if (!Stream.Get(&Hardpoint, 2u)) return MarshalError;
        Hardpoints.push_back(Hardpoint);
    }
    return Stream.Finish(OutLength, false);
}

FRpcTractorObjectsPacket::FRpcTractorObjectsPacket()
    : FPacketBase(FLPACKET_COMMON_TRACTOROBJECTS), ObjectId(0), TractorEquipId(0) {}

unsigned int FRpcTractorObjectsPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b697b0 / RS06c00aa0.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TractorEquipId, 2u)) return kPacketEncodeOverflow;
    const unsigned short Count = static_cast<unsigned short>(TargetIds.size());
    if (!Stream.Put(&Count, 2u)) return kPacketEncodeOverflow;
    // Retail truncates the count to a WORD but emits every element, independently.
    for (std::vector<unsigned int>::const_iterator It = TargetIds.begin(); It != TargetIds.end(); ++It) {
        if (!Stream.Put(&*It, 4u)) return kPacketEncodeOverflow;
    }
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcTractorObjectsPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&TractorEquipId, 2u)) return MarshalError;
    unsigned short Count;
    if (!Stream.Get(&Count, 2u)) return MarshalError;
    for (unsigned int Index = 0; Index < Count; ++Index) {
        unsigned int TargetId;
        if (!Stream.Get(&TargetId, 4u)) return MarshalError;
        TargetIds.push_back(TargetId); // Decode appends to existing contents.
    }
    return Stream.Finish(OutLength, false);
}

FRpcGoTradeLanePacket::FRpcGoTradeLanePacket() : FPacketBase(FLPACKET_COMMON_GOTRADELANE) {}

unsigned int FRpcGoTradeLanePacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b68180 / RS06bff470.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TradelaneRing1, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&TradelaneRing2, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&bMidLane, 1u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcGoTradeLanePacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&TradelaneRing1, 4u)) return MarshalError;
    if (!Stream.Get(&TradelaneRing2, 4u)) return MarshalError;
    if (!Stream.Get(&bMidLane, 1u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}

FRpcJettisonCargoPacket::FRpcJettisonCargoPacket()
    : FPacketBase(FLPACKET_COMMON_JETTISONCARGO), ObjectId(0), ItemId(0), Count(0) {}

unsigned int FRpcJettisonCargoPacket::Encode(unsigned char** BufferHolder,
    unsigned int Capacity, unsigned int* OutLength) {
    // RC06b69530 / RS06c00820.
    FRpcWireStream Stream(*this, BufferHolder, Capacity);
    Stream.PutHeader();
    if (!Stream.Put(&ObjectId, 4u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&ItemId, 2u)) return kPacketEncodeOverflow;
    if (!Stream.Put(&Count, 4u)) return kPacketEncodeOverflow;
    return Stream.FinishEncodeSuccess(OutLength);
}

unsigned int FRpcJettisonCargoPacket::Decode(const unsigned char* Buffer,
    unsigned int Size, unsigned int* OutLength) {
    FRpcWireStream Stream(*this, const_cast<unsigned char*>(Buffer), Size);
    if (!Stream.Get(&ObjectId, 4u)) return MarshalError;
    if (!Stream.Get(&ItemId, 2u)) return MarshalError;
    if (!Stream.Get(&Count, 4u)) return MarshalError;
    return Stream.Finish(OutLength, false);
}
