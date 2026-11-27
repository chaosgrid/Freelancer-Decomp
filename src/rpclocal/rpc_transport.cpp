// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_transport.cpp -- packet base, packet-type registry and the CDAConnection queues.

#include "rpc_transport.h"
#include "rpc_payloads.h"
#include "rpc_containers.h"
#include "common_server_types.h"
#include "rpc_packet_classes.h"

#include <new>
#include <ostream>
#include <cstring>
#include <cstdio>

// ===========================================================================
// FRpcRefObject -- the refcounted root, vtable 06db3c54.
// ===========================================================================

// FUN_06d8f4c0: only restores the vtable.
FRpcRefObject::~FRpcRefObject() {
}

// FUN_06d8f4a0. Only the decrement that reaches zero destroys; a count already at zero is
// returned untouched.
unsigned int FRpcRefObject::Release() {
    if (UsageCount == 0) {
        return UsageCount;
    }
    --UsageCount;
    if (UsageCount != 0) {
        return UsageCount;
    }
    delete this;                                         // scalar deleting dtor, arg 1
    return 0;
}

// ===========================================================================
// FPacketBase -- retail CDAPacket, 0x20-byte base.
// ===========================================================================

// The five CDAPacket stores every retail packet constructor makes (e.g. FUN_06d9af10):
// +0x04 = 1 (FRpcRefObject's), +0x08 = 0, +0x10 = 0, +0x1c = 0, +0x0c = the packet id.
// +0x14 and +0x18 are left alone.
FPacketBase::FPacketBase(unsigned int InPacketId)
    : Field_0x08(nullptr), PacketId(InPacketId), MarshalCursor(0), MarshalError(0) {
}

// FUN_06da1370.
FPacketBase::~FPacketBase() {
}

// FUN_06da1380: the 16-bit wire header, opcode's low 12 bits | channel byte << 8.
unsigned short RpcPacketHeaderWord(unsigned int PacketId) {
    const unsigned int Opcode = PacketId & 0xfffu;
    const unsigned int ChannelByte = (PacketId >> 16) & 0xffu;
    return (unsigned short)(Opcode | (ChannelByte << 8));
}

// CDAPacket's own slot +0x04, 06da13d0: the header and nothing else. Five of the 162
// classes keep it (their vtables hold 06da13d0); the rest override it with their codec.
unsigned int FPacketBase::Encode(unsigned char** BufferHolder, unsigned int Capacity,
                                 unsigned int* OutLength) {
    return RpcEncodeWireEmptyInline(*this, BufferHolder, Capacity, OutLength);
}

// CDAPacket's own slot +0x08, 06da1470.
unsigned int FPacketBase::Decode(const unsigned char* Buffer, unsigned int Size,
                                 unsigned int* OutLength) {
    return RpcDecodeWireEmptyInline(*this, Buffer, Size, OutLength);
}

// Slot +0x0c: the shared `RET 0x4` body 150 classes keep. Twelve override it.
void FPacketBase::Dump(std::ostream&) const {
}

void FPacketBase::AddRef() {
    ++UsageCount;
}

// The two links every node starts with.
struct FRpcEmbeddedListNode {
    FRpcEmbeddedListNode* Next;     // +0x00
    FRpcEmbeddedListNode* Prev;     // +0x04
};

// The 0x0c MSVCP60 std::list header, as every embedded list in this module wears it.
// Type-agnostic on purpose: the node size comes from the caller, so one body covers
// node sizes 0x0c, 0x10, 0x14 and 0x50 alike.
struct FRpcEmbeddedListHeader {
    unsigned char Allocator;        // +0x00
    unsigned char Pad_0x01[3];      // +0x01
    FRpcEmbeddedListNode* Head;     // +0x04  the sentinel node
    unsigned int Size;              // +0x08
};

// As in FUN_06d9d4f0 (the CHARACTERINFO ctor; the other list-building ctors differ only in
// offset and node size). Nothing is tested, not even the operator new result; the
// sentinel's value bytes are left as operator new returned them.
void RpcPacketBuildListSentinel(void* ListHeader, unsigned int NodeSize) {
    FRpcEmbeddedListHeader* Header = static_cast<FRpcEmbeddedListHeader*>(ListHeader);
    Header->Allocator = 0;
    FRpcEmbeddedListNode* Sentinel =
        static_cast<FRpcEmbeddedListNode*>(::operator new(NodeSize));
    Sentinel->Next = Sentinel;
    Sentinel->Prev = Sentinel;
    Header->Size = 0;
    Header->Head = Sentinel;
}

// FUN_06d9d1f0's list half: FUN_06d901f0 (list::erase) in a loop, then the sentinel.
void RpcPacketDestroyEmbeddedList(void* ListHeader) {
    FRpcEmbeddedListHeader* Header = static_cast<FRpcEmbeddedListHeader*>(ListHeader);
    FRpcEmbeddedListNode* Sentinel = Header->Head;
    FRpcEmbeddedListNode* Node = Sentinel->Next;
    while (Node != Sentinel) {
        FRpcEmbeddedListNode* Erased = Node;
        Node = Node->Next;
        Erased->Prev->Next = Erased->Next;
        Erased->Next->Prev = Erased->Prev;
        ::operator delete(Erased);
        --Header->Size;
    }
    ::operator delete(Header->Head);
    Header->Head = nullptr;
    Header->Size = 0;
}

// ===========================================================================
// FRpcMarshalStream -- see the derivation in rpc_transport.h.
// ===========================================================================

const double FRpcMarshalStream::kOrientationScale = 127.0;
const double FRpcMarshalStream::kOrientationInvScale = 0.007874015748031496;
const double FRpcMarshalStream::kPercentScale = 255.0;
const double FRpcMarshalStream::kPercentInvScale = 0.00392156862745098;
// DAT_06db6554 is a FLOAT (cd cc cc 3d), widened by the FMUL; DAT_06db6558 is the
// double 14 14 14 14 14 14 a4 3f.
const double FRpcMarshalStream::kTenthScale = 0.10000000149011612;
const double FRpcMarshalStream::kTenthInvScale = 0.0392156862745098;

FRpcMarshalStream::FRpcMarshalStream(FPacketBase& InPacket, void* InBuffer,
                                     unsigned int InCapacity)
    : Packet(InPacket) {
    Packet.MarshalCursor = 0;
    Packet.MarshalLimit = InCapacity;
    Packet.SetMarshalBuffer(static_cast<unsigned char*>(InBuffer));
    Packet.MarshalError = 0;
}

// The shared prologue: advance, bounds-check, then short-circuit if the stream has already
// failed. Cursor == Limit is still in bounds (JBE).
bool FRpcMarshalStream::Advance(unsigned int Width, unsigned int ErrorCode) {
    Packet.MarshalCursor += Width;
    if (Packet.MarshalCursor > Packet.MarshalLimit) {
        Packet.MarshalError = ErrorCode;
    }
    return Packet.MarshalError == 0;
}

// The bare cursor bump, with no access and no pointer move. See the note on the
// declaration: ch1 0x12 PLAYER_TRADE is the codec that needs the two apart.
bool FRpcMarshalStream::Reserve(unsigned int Width, unsigned int ErrorCode) {
    return Advance(Width, ErrorCode);
}

// The JC variant: continues only while Cursor < Limit, where every other bounds check in
// the module lets Cursor == Limit through. The advanced cursor is stored before the test,
// so a failure still leaves it moved.
bool FRpcMarshalStream::AdvanceStrict(unsigned int Width, unsigned int ErrorCode) {
    Packet.MarshalCursor += Width;
    if (Packet.MarshalCursor >= Packet.MarshalLimit) {
        return false;
    }
    (void)ErrorCode;   // retail returns the code, it does not store it -- see the header
    return true;
}

// FUN_06da14f0.
bool FRpcMarshalStream::PutWideString(const wchar_t* Text) {
    if (!AdvanceStrict(2u, kEncodeOverflow)) {
        return false;
    }
    unsigned int Length = 0;
    if (Text != nullptr) {
        while (Text[Length] != 0) {
            ++Length;
        }
    }
    const unsigned short Count = (unsigned short)(Length & 0xffffu);
    unsigned char* Ptr = Packet.MarshalBuffer();
    memcpy(Ptr, &Count, 2u);
    // Retail writes the advanced pointer back only on the success path: both failure exits
    // leave it in place even though the length word is already written.
    unsigned char* Local = Ptr + 2u;
    const unsigned int Bytes = (unsigned int)Count * 2u;
    if (!AdvanceStrict(Bytes, kEncodeOverflow)) {
        return false;
    }
    if (Bytes != 0 && Text != nullptr) {
        memcpy(Local, Text, Bytes);
    }
    Packet.SetMarshalBuffer(Local + Bytes);
    return true;
}

// FUN_06da1580.
bool FRpcMarshalStream::GetWideString(wchar_t*& OutText) {
    if (!AdvanceStrict(2u, kDecodeOverrun)) {
        return false;
    }
    unsigned char* Ptr = Packet.MarshalBuffer();
    unsigned short Count = 0;
    memcpy(&Count, Ptr, 2u);
    unsigned char* After = Ptr + 2u;
    const unsigned int Bytes = (unsigned int)Count * 2u;
    if (!AdvanceStrict(Bytes, kDecodeOverrun)) {
        return false;
    }
    OutText = nullptr;
    if (Count == 0) {
        // Retail quirk: this path skips the only store of the advanced pointer, so the
        // length word's two bytes move the cursor but not MarshalBuffer(). Reproduced.
        return true;
    }
    wchar_t* Block = static_cast<wchar_t*>(
        ::operator new(Bytes + 2u, std::nothrow));                     // bare new in retail
    memcpy(Block, After, Bytes);
    Block[Count] = 0;
    OutText = Block;
    Packet.SetMarshalBuffer(After + Bytes);
    return true;
}

bool FRpcMarshalStream::PutDwordUncounted(unsigned int Value) {
    if (Packet.MarshalError != 0) {
        return false;
    }
    unsigned char* Ptr = Packet.MarshalBuffer();
    memcpy(Ptr, &Value, 4u);
    Packet.MarshalPtr += 4u;
    return true;
}

bool FRpcMarshalStream::GetDwordUncounted(unsigned int& OutValue) {
    if (Packet.MarshalError != 0) {
        return false;
    }
    const unsigned char* Ptr = Packet.MarshalBuffer();
    memcpy(&OutValue, Ptr, 4u);
    Packet.MarshalPtr += 4u;
    return true;
}

bool FRpcMarshalStream::PutDword(unsigned int Value) {
    if (!Advance(4u, kEncodeOverflow)) {
        return false;
    }
    unsigned char* Ptr = Packet.MarshalBuffer();
    memcpy(Ptr, &Value, 4u);
    Packet.MarshalPtr += 4u;
    return true;
}

bool FRpcMarshalStream::PutWord(unsigned short Value) {
    if (!Advance(2u, kEncodeOverflow)) {
        return false;
    }
    unsigned char* Ptr = Packet.MarshalBuffer();
    memcpy(Ptr, &Value, 2u);
    Packet.MarshalPtr += 2u;
    return true;
}

// See the note on the declaration. No Advance, no check; covers both the inlined
// LAB_06da13d0 and the out-of-line FUN_06da1380.
void FRpcMarshalStream::PutHeaderUnchecked(unsigned short Value) {
    unsigned char* Ptr = Packet.MarshalBuffer();
    memcpy(Ptr, &Value, 2u);
    Packet.MarshalPtr += 2u;
    // Set, not advanced -- the same only because the cursor is 0 at every site.
    Packet.MarshalCursor = 2u;
    // FUN_06da1380 itself never touches this field; its callers store its always-zero
    // return value here.
    Packet.MarshalError = 0u;
}

bool FRpcMarshalStream::PutByte(unsigned char Value) {
    if (!Advance(1u, kEncodeOverflow)) {
        return false;
    }
    unsigned char* Ptr = Packet.MarshalBuffer();
    *Ptr = Value;
    Packet.MarshalPtr += 1u;
    return true;
}

bool FRpcMarshalStream::PutFloat(float Value) {
    if (!Advance(4u, kEncodeOverflow)) {
        return false;
    }
    unsigned char* Ptr = Packet.MarshalBuffer();
    memcpy(Ptr, &Value, 4u);
    Packet.MarshalPtr += 4u;
    return true;
}

// The cursor is advanced by the whole run first, then the run is copied. A zero-length run
// cannot fail. The source is not tested.
bool FRpcMarshalStream::PutBytes(const void* Source, unsigned int Count) {
    if (!Advance(Count, kEncodeOverflow)) {
        return false;
    }
    if (Count != 0) {
        memcpy(Packet.MarshalBuffer(), Source, Count);
    }
    Packet.MarshalPtr += Count;
    return true;
}

// The cursor moves over Count bytes and nothing is written -- the three-byte holes of
// ch2 0x4e SETDIRECTIVE (see rpc_wire_closeout.cpp), which retail fills from its own
// uninitialised stack temporary. Same bounds check as PutBytes.
bool FRpcMarshalStream::PutGap(unsigned int Count) {
    if (!Advance(Count, kEncodeOverflow)) {
        return false;
    }
    Packet.MarshalPtr += Count;
    return true;
}

// Float times scale, MSVCRT ftol, one byte. ftol truncates toward zero, as the cast does.
bool FRpcMarshalStream::PutQuantized(float Value, double Scale) {
    const long Scaled = static_cast<long>(static_cast<double>(Value) * Scale);
    return PutByte(static_cast<unsigned char>(static_cast<unsigned long>(Scaled) & 0xffu));
}

// The FIREWEAPON middle tier: the same truncating ftol, stored as a word.
bool FRpcMarshalStream::PutQuantizedWord(float Value, double Scale) {
    const long Scaled = static_cast<long>(static_cast<double>(Value) * Scale);
    return PutWord(static_cast<unsigned short>(static_cast<unsigned long>(Scaled) & 0xffffu));
}

// The SENDCOMM tier: float times 0.1f, times 255.0, ftol, one byte. Two separate
// multiplies on purpose -- see the note on the declaration.
bool FRpcMarshalStream::PutQuantizedTwoStage(float Value, double Scale1, double Scale2) {
    const double Stage1 = static_cast<double>(Value) * Scale1;
    const long Scaled = static_cast<long>(Stage1 * Scale2);
    return PutByte(static_cast<unsigned char>(static_cast<unsigned long>(Scaled) & 0xffu));
}

bool FRpcMarshalStream::GetDword(unsigned int& OutValue) {
    if (!Advance(4u, kDecodeOverrun)) {
        return false;
    }
    const unsigned char* Ptr = Packet.MarshalBuffer();
    memcpy(&OutValue, Ptr, 4u);
    Packet.MarshalPtr += 4u;
    return true;
}

bool FRpcMarshalStream::GetWord(unsigned short& OutValue) {
    if (!Advance(2u, kDecodeOverrun)) {
        return false;
    }
    const unsigned char* Ptr = Packet.MarshalBuffer();
    memcpy(&OutValue, Ptr, 2u);
    Packet.MarshalPtr += 2u;
    return true;
}

bool FRpcMarshalStream::GetByte(unsigned char& OutValue) {
    if (!Advance(1u, kDecodeOverrun)) {
        return false;
    }
    const unsigned char* Ptr = Packet.MarshalBuffer();
    OutValue = *Ptr;
    Packet.MarshalPtr += 1u;
    return true;
}

bool FRpcMarshalStream::GetFloat(float& OutValue) {
    if (!Advance(4u, kDecodeOverrun)) {
        return false;
    }
    const unsigned char* Ptr = Packet.MarshalBuffer();
    memcpy(&OutValue, Ptr, 4u);
    Packet.MarshalPtr += 4u;
    return true;
}

// The destination is not tested either: retail decoders copy into it once the bounds check
// has passed.
bool FRpcMarshalStream::GetBytes(void* Dest, unsigned int Count) {
    if (!Advance(Count, kDecodeOverrun)) {
        return false;
    }
    if (Count != 0) {
        memcpy(Dest, Packet.MarshalBuffer(), Count);
    }
    Packet.MarshalPtr += Count;
    return true;
}

// The decode-side twin of PutGap: consumes Count bytes and stores none of them.
bool FRpcMarshalStream::GetGap(unsigned int Count) {
    if (!Advance(Count, kDecodeOverrun)) {
        return false;
    }
    Packet.MarshalPtr += Count;
    return true;
}

// The inverse of PutQuantized: the byte is sign-extended, then multiplied by the
// reciprocal (DAT_06db3fe0), so the 127.0 scale covers [-1,1].
bool FRpcMarshalStream::GetQuantizedSigned(float& OutValue, double InvScale) {
    unsigned char Raw = 0;
    if (!GetByte(Raw)) {
        return false;
    }
    const double Signed = static_cast<double>(static_cast<signed char>(Raw));
    OutValue = static_cast<float>(Signed * InvScale);
    return true;
}

// The word is sign-extended, so the 32767.0 scale covers [-1,+1].
bool FRpcMarshalStream::GetQuantizedWordSigned(float& OutValue, double InvScale) {
    unsigned short Raw = 0;
    if (!GetWord(Raw)) {
        return false;
    }
    const double Signed = static_cast<double>(static_cast<short>(Raw));
    OutValue = static_cast<float>(Signed * InvScale);
    return true;
}

// The 255.0 family (equipment status, collision-group health) widens without sign, so the
// byte covers [0,+1] and 0xff is +1.0.
bool FRpcMarshalStream::GetQuantizedUnsigned(float& OutValue, double InvScale) {
    unsigned char Raw = 0;
    if (!GetByte(Raw)) {
        return false;
    }
    const double Unsigned = static_cast<double>(static_cast<unsigned int>(Raw));
    OutValue = static_cast<float>(Unsigned * InvScale);
    return true;
}

// ===========================================================================
// FPacketSerializer -- the retail packet-type registry.
// Registration call: FUN_06d960a0(Serializer, Channel << 16 | Opcode, Name, Factory).
// ===========================================================================

// The body FUN_06d95e50 inlines after operator new(0x1c): the FRpcRefObject base, the empty
// map at +0x08, vtable 06db3e18, then FUN_06da2820 builds the hardpoint-name dictionary the
// CREATESHIP / CREATESOLAR equipment codec reads.
FPacketSerializer::FPacketSerializer() {
    RpcBuildHardpointDictionary();
}

// FUN_06d95f30: frees every record (and its std::string) and nulls the node's value; the
// std::map member and the base destructor do the rest. The scalar deleting destructor is
// FUN_06d964c0 (vtable 06db3e18 slot 0), reached through FRpcRefObject::Release.
FPacketSerializer::~FPacketSerializer() {
    for (std::map<unsigned int, FPacketTypeInfo*>::iterator It = Types.begin();
         It != Types.end(); ++It) {
        delete It->second;
        It->second = nullptr;
    }
}

// .data:DAT_06dbbe38 -- the serializer singleton, written once and never cleared.
static FPacketSerializer* GRpcPacketSerializer = nullptr;

// FUN_06d95e50: return the cached singleton, else construct and publish it (null included,
// since VC6 operator new returned null rather than throwing; nothrow new is the same
// contract).
FPacketSerializer* RpcGetPacketSerializer() {
    if (GRpcPacketSerializer != nullptr) {
        return GRpcPacketSerializer;
    }
    GRpcPacketSerializer = new (std::nothrow) FPacketSerializer();
    return GRpcPacketSerializer;
}

// FUN_06d960a0: lower_bound on the id (FUN_06d96e60); a duplicate returns 0x42, otherwise
// a 0x14-byte record (factory at +0x00, std::string name at +0x04) is inserted. Nothing is
// range-checked.
int FPacketSerializer::RegisterType(unsigned int PacketId, const char* Name,
                                    FPacketFactoryFn Factory) {
    std::map<unsigned int, FPacketTypeInfo*>::iterator It = Types.lower_bound(PacketId);
    if (It != Types.end() && It->first == PacketId) {
        return kPacketRegisterDuplicate;
    }
    FPacketTypeInfo* Info = new FPacketTypeInfo;
    Info->Factory = Factory;
    Info->Name = Name;
    Types.insert(It, std::map<unsigned int, FPacketTypeInfo*>::value_type(PacketId, Info));
    return 0;
}

// FUN_06d96300(Serializer, Channel, Opcode, &OutPacket): a null out returns 4; the id is
// (Channel word << 16) | Opcode word; an unknown id returns 0x19; otherwise the factory's
// own answer is returned. *OutPacket is not pre-cleared here: every factory does that.
int FPacketSerializer::Allocate(unsigned int Channel, unsigned int Opcode,
                                FPacketBase** OutPacket) {
    if (OutPacket == nullptr) {
        return kPacketAllocateNullOut;
    }
    const unsigned int PacketId = ((Channel & 0xffffu) << 16) | (Opcode & 0xffffu);
    std::map<unsigned int, FPacketTypeInfo*>::const_iterator It = Types.find(PacketId);
    if (It == Types.end()) {
        return kPacketAllocateUnknownType;
    }
    return It->second->Factory(OutPacket);
}

// FUN_06d961b0 -- IServerImpl::DumpPacketStats (0x06d8f480) and IClient slot 125 both land
// here. The argument is a file name passed straight to fopen(.., "w"); nothing happens when
// the open fails. Rows go in ascending id order; FUN_06da16b0 supplies the counters
// (inserting a zeroed record for an id never counted) and a row is skipped only when
// OutCount and InCount are both zero.
void FPacketSerializer::DumpStats(const char* Name) const {
    FILE* File = fopen(Name, "w");
    if (File == nullptr) {
        return;
    }
    fprintf(File,                                       // .rdata:06db3e40
            "Packet Name,In Count,In Max Size,In Avg Size,In Total Bytes,"
            "Exec Time Total,Exec Time Avg,"
            "Out Count,Out Max Size,Out Avg Size,Out Total Bytes\n\n");
    for (std::map<unsigned int, FPacketTypeInfo*>::const_iterator It = Types.begin();
         It != Types.end(); ++It) {
        const FPacketTypeInfo* Info = It->second;
        const FPacketWireStats* Stats = RpcFindWireStats(It->first);
        if (Stats->OutCount == 0 && Stats->InCount == 0) {
            continue;
        }
        // The format at .rdata:06db3e3c includes the comma.
        fprintf(File, "%s,", Info->Name.c_str());
        fprintf(File, "%u,%u,%u,%u,%f,%f,",
                Stats->InCount, Stats->InMaxSize, Stats->InAvgSize, Stats->InTotalBytes,
                Stats->ExecTimeTotal, Stats->ExecTimeAvg);
        fprintf(File, "%u,%u,%u,%u",
                Stats->OutCount, Stats->OutMaxSize, Stats->OutAvgSize, Stats->OutTotalBytes);
        fprintf(File, "\n");
    }
    fclose(File);
}

// ===========================================================================
// The codec statistics tails -- retail FUN_06da16b0 plus the inline accumulator every
// Encode and Decode slot ends with. See the derivation above FPacketWireStats.
// ===========================================================================

// retail .data:06dbbf4c
std::map<unsigned int, FPacketWireStats> GRpcPacketStats;

// FUN_06da16b0: FUN_06da1730 on the map at .data:06dbbf4c with the id as the key --
// std::map::operator[], so an id seen for the first time gets a zeroed record.
FPacketWireStats* RpcFindWireStats(unsigned int PacketId) {
    return &GRpcPacketStats[PacketId];
}

void RpcRecordEncodedPacket(unsigned int PacketId, unsigned int Length) {
    FPacketWireStats* Stats = RpcFindWireStats(PacketId);
    ++Stats->OutCount;
    Stats->OutTotalBytes += Length;
    if (Length > Stats->OutMaxSize) {
        Stats->OutMaxSize = Length;
    }
    Stats->OutAvgSize = Stats->OutTotalBytes / Stats->OutCount;
}

void RpcRecordDecodedPacket(unsigned int PacketId, unsigned int Length) {
    FPacketWireStats* Stats = RpcFindWireStats(PacketId);
    // The decoder's own length excludes the two header bytes, so every term gets +2.
    const unsigned int Accounted = Length + 2u;
    ++Stats->InCount;
    Stats->InTotalBytes += Accounted;
    // Retail quirk: compares the unadjusted length against Max + 2, then stores Length + 2,
    // so a new maximum has to beat the old one by more than four. Same in the CREATELOOT
    // decoder (06d9313b). Kept as written.
    if (Length > Stats->InMaxSize + 2u) {
        Stats->InMaxSize = Accounted;
    }
    Stats->InAvgSize = Stats->InTotalBytes / Stats->InCount;
}

// The three per-channel registrators live in rpc_packet_registry.cpp.

// ===========================================================================
// FPacketQueue / FRpcConnection -- retail CDAConnection (0x30 bytes) and its four queues.
// The queue stores the packet pointer by value in its own std::deque block (FUN_06d8cb70);
// CDAPacket+0x08 is not a link. Layout evidence is in the header.
// ===========================================================================

// --- FPacketQueue: retail's operator_new(0x30) queue object -------------------------
// The mode byte at +0x00 comes from FUN_06d85b60's argument; everything above it is an
// MSVC6 std::deque<CDAPacket*>.
FPacketQueue::FPacketQueue(unsigned char InMode) : Mode(InMode) {
}

void FPacketQueue::Push(FPacketBase* Packet) {
    // No null test in retail (FUN_06d85e00 / FUN_06d86150).
    Packets.push_back(Packet);
}

FPacketBase* FPacketQueue::Pop() {
    if (Packets.empty()) {
        return nullptr;
    }
    FPacketBase* Packet = Packets.front();
    Packets.pop_front();
    return Packet;
}

// One arm of retail FUN_06d857c0: pop and delete while the queue is non-empty; a null front
// stops it. The packet is deleted through slot 0, not released: its use count is ignored.
void FPacketQueue::DestroyAll() {
    while (!Packets.empty()) {
        FPacketBase* Packet = Packets.front();
        if (Packet == nullptr) {
            break;
        }
        Packets.pop_front();
        delete Packet;
    }
}

// --- FRpcConnection: retail's operator_new(0x30) CDAConnection ----------------------
// FUN_06d88280 zeroes +0x00..+0x10 and the byte at +0x14 and stops there.
FRpcConnection::FRpcConnection()
    : ServerBoundRead(nullptr)
    , ServerBoundWrite(nullptr)
    , ClientBoundRead(nullptr)
    , ClientBoundWrite(nullptr)
    , Mode(0)
    , bStarted(false) {
    // +0x18..+0x2f (Lock) is left as raw heap, as FUN_06d88280 leaves it.
}

// FUN_06d85b60. IServerImpl::Connect passes 1; the body tests bit 0 then bit 1.
unsigned int FRpcConnection::Start(unsigned int InMode) {
    if (bStarted) {
        return kStartAlreadyOn;
    }

    const unsigned char ModeByte = static_cast<unsigned char>(InMode);

    if ((InMode & kModeSingleBuffered) != 0) {
        // Two queues, at +0x00 and +0x08.
        ServerBoundRead = new (std::nothrow) FPacketQueue(ModeByte);
        ClientBoundRead = new (std::nothrow) FPacketQueue(ModeByte);
        if (ServerBoundRead == nullptr || ClientBoundRead == nullptr) {
            return kStartAllocFailed;
        }
    } else if ((InMode & kModeDoubleBuffered) != 0) {
        // Four queues, allocated in retail's order +0x00, +0x08, +0x0c, +0x04, all checked
        // before succeeding.
        ServerBoundRead  = new (std::nothrow) FPacketQueue(ModeByte);
        ClientBoundRead  = new (std::nothrow) FPacketQueue(ModeByte);
        ClientBoundWrite = new (std::nothrow) FPacketQueue(ModeByte);
        ServerBoundWrite = new (std::nothrow) FPacketQueue(ModeByte);
        if (ServerBoundRead == nullptr || ClientBoundRead == nullptr ||
            ClientBoundWrite == nullptr || ServerBoundWrite == nullptr) {
            return kStartAllocFailed;
        }
    } else {
        return kStartBadMode;
    }

    InitializeCriticalSection(&Lock);
    bStarted = true;
    Mode = InMode;
    return kStartOk;
}

// One arm of retail FUN_06d857c0: if the queue exists, drain it destroying every packet,
// then the deque destructor (FUN_06d85ac0) and the free of the queue. The member is not
// cleared -- the whole connection is freed right after.
void FRpcConnection::DestroyQueue(FPacketQueue* Queue) {
    if (Queue == nullptr) {
        return;
    }
    Queue->DestroyAll();
    delete Queue;
}

// FUN_06d857c0, run by IServerImpl::Shutdown before the free: DeleteCriticalSection first
// and unconditionally, then the queues in the order +0x0c, +0x04, +0x08, +0x00. It touches
// no other field.
FRpcConnection::~FRpcConnection() {
    DeleteCriticalSection(&Lock);
    DestroyQueue(ClientBoundWrite);
    DestroyQueue(ServerBoundWrite);
    DestroyQueue(ClientBoundRead);
    DestroyQueue(ServerBoundRead);
}

// FUN_06d86150: the write queue when there is one, otherwise the read queue; 4 when neither
// exists. IServerImpl::PushToServer (0x06d8f460) tail-jumps into it and drops the packet
// when DAT_06dbbd58 is null. No packet test, no lock.
unsigned int FRpcConnection::PushServerBound(FPacketBase* Packet) {
    FPacketQueue* Queue = (ServerBoundWrite != nullptr) ? ServerBoundWrite : ServerBoundRead;
    if (Queue == nullptr) {
        return kPushNoQueue;
    }
    Queue->Push(Packet);
    return kPushOk;
}

// FUN_06d86580 -- reads +0x00 only; drained by IClient slot 16 (0x06d8efc0). Null queue or
// size 0 -> 0. No lock.
FPacketBase* FRpcConnection::PopServerBound() {
    return (ServerBoundRead != nullptr) ? ServerBoundRead->Pop() : nullptr;
}

// FUN_06d85e00, called from IClient slot 9 (0x06d8f080): +0x0c, else +0x08, else 4. No
// packet test, no lock.
unsigned int FRpcConnection::PushClientBound(FPacketBase* Packet) {
    FPacketQueue* Queue = (ClientBoundWrite != nullptr) ? ClientBoundWrite : ClientBoundRead;
    if (Queue == nullptr) {
        return kPushNoQueue;
    }
    Queue->Push(Packet);
    return kPushOk;
}

// FUN_06d864a0 -- reads +0x08 only; drained by IServerImpl::Update. Null queue or size 0 ->
// 0. No lock.
FPacketBase* FRpcConnection::PopClientBound() {
    return (ClientBoundRead != nullptr) ? ClientBoundRead->Pop() : nullptr;
}

// IServerImpl::Update and IClient slot 16 read the count straight out of the read queue
// (queue+0x2c).
unsigned int FRpcConnection::NumServerBound() const {
    return (ServerBoundRead != nullptr) ? ServerBoundRead->Num() : 0u;
}

unsigned int FRpcConnection::NumClientBound() const {
    return (ClientBoundRead != nullptr) ? ClientBoundRead->Num() : 0u;
}

// ===========================================================================
// Module-wide transport state (retail keeps the same things in .data)
// ===========================================================================
FRpcConnection* GRpcConnection = nullptr;        // retail .data:DAT_06dbbb5c
IClient* GRealClient = nullptr;                  // retail .data:0x06dbbcd8
IChatClient* GRealChatClient = nullptr;          // retail .data:0x06dbbcd4
char GCallerModulePath[260] = { 0 };             // retail .data:0x06dbbbc0

// FUN_06d88280: return the cached singleton, creating it on the first call (caching null
// again when operator new fails). It does not start the connection.
FRpcConnection* RpcCreateConnection() {
    if (GRpcConnection == nullptr) {
        GRpcConnection = new (std::nothrow) FRpcConnection();
    }
    return GRpcConnection;
}
