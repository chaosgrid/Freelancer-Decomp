// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_transport.h -- the rpclocal RPC transport foundation.
//
// See docs/re/rpclocal.md. Retail rpclocal.dll is a bidirectional proxy, not a wrapper:
//   * freelancer.exe asks it for IServer  -> IServerImpl serializes each call into a
//     CDAPacket (channel 1 or 3) and pushes it through PushToServer (vtable +0x170);
//   * Server.dll asks it for IClient      -> the .data:Client object serializes each
//     call into a CDAPacket (channel 1 or 2) and hands it to IClient slot 9 (+0x24).
// IServerImpl::Update drains the client-bound queue and IServerImpl::HandlePacket
// (0x06d81170) re-dispatches each packet into the real IClient inside freelancer.exe
// (.data:0x06dbbcd8) or the chat client (.data:0x06dbbcd4).
//
// Routing header: the dword at CDAPacket+0x0c.
//   Channel = header >> 16 ; Opcode = header & 0xfff ; payload starts at +0x20.
#pragma once

#include <windows.h>
#include <cstdio>
#include <deque>
#include <iosfwd>
#include <map>
#include <string>

class CDAPacket;
class IClient;
class IChatClient;

// FRpcRefObject, FPacketBase, FPacketFactoryFn, the factory / registry return codes,
// FPacketTypeInfo and FPacketSerializer are shared with remoteserver.dll (its
// IServerImpl allocates packets the same way) and live in src/include/rpc_packet.h.
#include "rpc_packet.h"

// Stores one field of 1, 2 or 4 bytes at a packet-relative Offset, unchecked -- one
// retail constructor store.
void RpcPacketStoreField(FPacketBase* Packet, unsigned int Offset, unsigned int Width,
                         unsigned int Value);

// Builds one MSVCP60 std::list embedded in a packet, as eight constructors do inline (e.g.
// CHARACTERINFO's): allocator byte, operator new(8 + sizeof(T)) sentinel linked to itself,
// Size 0, Head = sentinel. CREATESHIP and CREATESOLAR reach the same result through
// COMMON.DLL's std::list<CollisionGroupDesc>::list and DamageList::DamageList. Nothing is
// tested. The allocator byte is stored as 0: retail copies an uninitialised stack byte that
// nothing reads.
void RpcPacketBuildListSentinel(FPacketBase* Packet, unsigned int Offset,
                                unsigned int NodeSize);

// Tears down one list built by RpcPacketBuildListSentinel, as every retail packet
// destructor does (e.g. CREATELOOT's FUN_06d9d1f0): unlink and free each node, then the
// sentinel; Head = 0, Size = 0. Node values are not touched; a destructor that owns them
// frees them first.
void RpcPacketDestroyEmbeddedList(FPacketBase* Packet, unsigned int Offset);

// ===========================================================================
// FRpcMarshalStream -- the cursor the packet codec slots run on.
//
// Modelled on the CREATESHIP/CREATESOLAR codec pair (encoder FUN_06da3720, decoder
// FUN_06da4020, driven from the Encode/Decode slots 0x06d97d20 / 0x06d98250 and 0x06dac7c0 /
// 0x06daca80).
//
// The state is not a separate object: it is the four dwords at packet+0x10..+0x1c
// (FPacketBase::Marshal*: Cursor = 0, Limit = capacity, Ptr = buffer, Error = 0), set up by
// the slot and addressed off `this` by the codec. Every field follows one idiom:
//     Cursor += Width;
//     if (Cursor > Limit) Error = Code;     // Cursor == Limit is OK (JBE)
//     if (Error) return Code;
//     <write *Ptr / read *Ptr>; Ptr += Width;
// The cursor is checked before the access, and once Error is set every later field
// short-circuits, so a codec never partially writes past the end.
//
// Error codes are per direction and double as the codec's return value: 0x16 encode, 0x17
// decode.
// ===========================================================================
class FRpcMarshalStream {
public:
    // Retail's two direction codes, used both as the sticky error and as the value the
    // codec returns to its caller.
    static const unsigned int kEncodeOverflow = 0x16;
    static const unsigned int kDecodeOverrun = 0x17;

    // The quantizer constants from .rdata. Encode multiplies by the scale; decode multiplies
    // by a separate precomputed reciprocal rather than dividing.
    //   DAT_06db3fd8 = 00 00 00 00 00 c0 5f 40 = 127.0
    //   DAT_06db3fe0 = 08 04 02 81 40 20 80 3f = 0.007874015748031496
    //   DAT_06db60f0 = 00 00 00 00 00 e0 6f 40 = 255.0
    //   DAT_06db60f8 = 10 10 10 10 10 10 70 3f = 0.00392156862745098
    static const double kOrientationScale;      // 127.0
    static const double kOrientationInvScale;   // 1/127
    static const double kPercentScale;          // 255.0
    static const double kPercentInvScale;       // 1/255
    // A third tier (ch2 0x4f SENDCOMM): two multiplies before the ftol, the float 0.1f
    // (DAT_06db6554, cd cc cc 3d) then 255.0, so the field covers [0,10] in 255 steps. The
    // decoder uses a single constant, DAT_06db6558 = 14 14 14 14 14 14 a4 3f = 1/25.5 --
    // ten times kPercentInvScale and a different .rdata slot, so not interchangeable.
    static const double kTenthScale;            // 0.1f widened to double
    static const double kTenthInvScale;         // 1/25.5

    // Mirrors the slot prologue. Buffer/Capacity are the codec's own two arguments.
    FRpcMarshalStream(FPacketBase& InPacket, void* InBuffer, unsigned int InCapacity);

    unsigned int GetError() const { return Packet.MarshalError; }
    bool Failed() const { return Packet.MarshalError != 0; }
    // Bytes emitted or consumed so far -- what the slot reports back through its
    // out-parameter.
    unsigned int Length() const { return Packet.MarshalCursor; }

    // --- encode side, sticky error kEncodeOverflow ---
    bool PutDword(unsigned int Value);
    bool PutWord(unsigned short Value);
    bool PutByte(unsigned char Value);
    bool PutFloat(float Value);
    bool PutBytes(const void* Source, unsigned int Count);
    // Advances over Count bytes without writing them (a hole retail leaves unwritten).
    bool PutGap(unsigned int Count);
    // The two header bytes written without a bounds check, cursor set to 2 and the sticky
    // error cleared. Retail's FUN_06da1380 (and its inlined twin LAB_06da13d0) contains no
    // compare at all: it ignores the Limit argument, writes unconditionally and returns 0,
    // so every encoder's `if (result) return 0x16` is unreachable. The call sites, not the
    // callee, store that zero into the sticky error. Every header write is the first write
    // on its stream, so "set cursor to 2" equals "advance by 2".
    //
    // Reproduced on purpose: with a Capacity below 2, retail writes the two routing bytes
    // past the end of the buffer and reports success (docs/re/deviations.md).
    void PutHeaderUnchecked(unsigned short Value);
    // ftol(Value * Scale) truncated into one byte. ftol truncates toward zero, as a plain
    // cast to long does.
    bool PutQuantized(float Value, double Scale);
    // The same into two bytes; retail's only user is the middle precision tier of ch1 0x02
    // FIREWEAPON (DAT_06db6120, DAT_06db6118).
    bool PutQuantizedWord(float Value, double Scale);
    // ftol(Value * Scale1 * Scale2) into one byte. Not folded into one product: x87 at
    // 53-bit precision rounds each FMUL to double, so the chain really is two multiplies.
    // Retail's only user is the SENDCOMM tier described above.
    bool PutQuantizedTwoStage(float Value, double Scale1, double Scale2);

    // --- decode side, sticky error kDecodeOverrun ---
    // Out-parameters are references (never null-tested) and are left untouched when the
    // stream has already failed.
    bool GetDword(unsigned int& OutValue);
    bool GetWord(unsigned short& OutValue);
    bool GetByte(unsigned char& OutValue);
    bool GetFloat(float& OutValue);
    bool GetBytes(void* Dest, unsigned int Count);
    // Consumes Count bytes without storing them.
    bool GetGap(unsigned int Count);
    // InvScale is the reciprocal constant (kOrientationInvScale / kPercentInvScale).
    //
    // The two extensions are not interchangeable (the wrong one silently corrupts one bit
    // per byte). In FUN_06da4020 the orientation quadruple sign-extends, so 127.0 covers
    // [-1,+1]; every 255.0-scaled percentage (hull fraction, equipment status,
    // collision-group health) zero-extends and covers [0,+1].
    bool GetQuantizedSigned(float& OutValue, double InvScale);
    bool GetQuantizedUnsigned(float& OutValue, double InvScale);
    // The 16-bit inverse of PutQuantizedWord; retail sign-extends the word. There is no
    // unsigned 16-bit form in retail.
    bool GetQuantizedWordSigned(float& OutValue, double InvScale);

    // --- the cursor and the buffer pointer, moved separately ---
    //
    // Most codecs move both together. ch1 0x12 PLAYER_TRADE does not: its trailing dword is
    // always written and read, but the cursor bump for it depends on COMMON.DLL::IsMPServer,
    // and the encoder and decoder disagree on which way round (see rpc_containers.cpp).
    //
    // Reserve is the bare advance-and-check; ErrorCode is a parameter because retail's
    // PLAYER_TRADE decoder stores the encoder's 0x16 at one of its bounds checks
    // (06da7541), reproduced.
    bool Reserve(unsigned int Width, unsigned int ErrorCode);

    // --- the wide-string pair, FUN_06da14f0 / FUN_06da1580 ---
    //
    // A separate primitive from RpcPutSizedBlock's {dword size, bytes}; only ch3 0x39
    // CREATENEWCHAR uses them. Differences from everything else here:
    //  * the count is a word, wcslen without the terminator;
    //  * the bounds test is JC, not JBE, so Cursor == Limit is an error and a string that
    //    exactly fills the buffer is rejected;
    //  * a failure does not set the sticky error: retail returns 0x16 / 0x17 straight out
    //    of the helper, so the caller must return on false (Failed() stays false).
    //
    // GetWideString allocates 2*len+2 and NUL-terminates; the caller owns the block. When
    // len is zero it yields null and leaves the buffer pointer un-advanced although the
    // cursor moved by 2 -- a retail asymmetry, reproduced.
    bool PutWideString(const wchar_t* Text);
    bool GetWideString(wchar_t*& OutText);
    // Store / load four bytes at the buffer pointer and move it on, leaving the cursor
    // alone. Both refuse to touch memory once the stream has failed, as retail's
    // `if (packet+0x1c) return` gate does.
    bool PutDwordUncounted(unsigned int Value);
    bool GetDwordUncounted(unsigned int& OutValue);

private:
    // The advance-and-check prologue every field shares.
    bool Advance(unsigned int Width, unsigned int ErrorCode);
    // The same, with the JC comparison of the wide-string helpers: reaching the limit
    // exactly is already an error.
    bool AdvanceStrict(unsigned int Width, unsigned int ErrorCode);

    // No state beyond the packet's own four dwords, as in retail.
    FPacketBase& Packet;
};

// The two bytes every retail encoder puts first, from FUN_06da1380:
//     header = (word)(*(ushort*)(packet+0x0c) & 0xfff) | (*(uchar*)(packet+0x0e) << 8)
// A plain OR of two overlapping ranges, not a bitfield pack: bits 8..11 come from both the
// opcode and the channel byte.
unsigned short RpcPacketHeaderWord(unsigned int PacketId);

// ===========================================================================
// FPacketWireStats -- the per-packet-type byte counters every codec feeds.
//
// Field names come from retail's own CSV header (.rdata:06db3e40, written by FUN_06d961b0
// DumpPacketStats); the fprintf argument order fixes the offsets:
//   "%u,%u,%u,%u,%f,%f,"  <- +0x04, +0x14, +0x0c, +0x1c, (double)+0x20, (double)+0x28
//   "%u,%u,%u,%u"         <- +0x00, +0x10, +0x08, +0x18
// The 0x30-byte size matches the zeroed 12-dword temp FUN_06da1730 copies into a new entry.
//
// Retail keeps these in a std::map<unsigned int, FPacketWireStats> at .data:06dbbf4c,
// reached through FUN_06da16b0 -> FUN_06da1730 (operator[]: an unseen id gets a zeroed
// record). RpcFindWireStats is that pair; the map is GRpcPacketStats.
//
// ExecTimeTotal / ExecTimeAvg are never written by rpclocal.dll (the codec tails touch
// only the six dwords); they are carried so the CSV keeps retail's columns, and stay zero.
struct FPacketWireStats {
    unsigned int OutCount;        // +0x00  bumped by every encoder tail
    unsigned int InCount;         // +0x04  bumped by every decoder tail
    unsigned int OutAvgSize;      // +0x08  OutTotalBytes / OutCount, INTEGER division
    unsigned int InAvgSize;       // +0x0c  InTotalBytes / InCount
    unsigned int OutMaxSize;      // +0x10
    unsigned int InMaxSize;       // +0x14
    unsigned int OutTotalBytes;   // +0x18
    unsigned int InTotalBytes;    // +0x1c
    double ExecTimeTotal;         // +0x20  never written inside this module
    double ExecTimeAvg;           // +0x28  never written inside this module
};

// The tail every encoder and decoder runs (e.g. 06da4c48 encode, 06da4edc decode):
//   * the encoder accounts the cursor as it stands (its length includes the header);
//   * the decoder adds 2 to the total, to the max before the compare and to the new max,
//     because its length excludes the header the dispatcher consumed;
//   * the max compare is strict, so an equal length does not rewrite the max.
// The average is recomputed after each packet as an unsigned integer division, truncating.
void RpcRecordEncodedPacket(unsigned int PacketId, unsigned int Length);
void RpcRecordDecodedPacket(unsigned int PacketId, unsigned int Length);

// FUN_06da16b0 -> FUN_06da1730: the record for PacketId, inserted zeroed when missing.
FPacketWireStats* RpcFindWireStats(unsigned int PacketId);
// retail .data:06dbbf4c
extern std::map<unsigned int, FPacketWireStats> GRpcPacketStats;


// FUN_06da2820: builds the CREATESHIP / CREATESOLAR hardpoint-name dictionary
// (.data:06dbbf64 map, .data:06dbbf78 count). The serializer's construction calls it last,
// as in retail. Defined in rpc_containers.cpp.
void RpcBuildHardpointDictionary();


// FUN_06d95e50: the serializer singleton over .data:DAT_06dbbe38. Returns the cached object
// when there is one (use count not incremented), otherwise builds and publishes a new one
// (null included). Retail quirk: nothing ever clears DAT_06dbbe38, so after
// IServerImpl::Shutdown releases the object the cache dangles; harmless because
// freelancer.exe FreeLibrary()s the module right after Shutdown (ShutdownLocalServer in
// src/freelancer/Client/server_connect.cpp).
// Callers: IServerImpl::Startup and IClient slot 10 (storing it at this+0x08), and
// IServerImpl::DumpPacketStats / IClient slot 125.
FPacketSerializer* RpcGetPacketSerializer();

// The three retail registrators (rpc_packet_registry.cpp), 162 entries in total. __cdecl,
// one argument; 4 when it is null, 0 otherwise.
int RpcRegisterChannel1Types(FPacketSerializer* Serializer);   // retail FUN_06d9ace0
int RpcRegisterChannel2Types(FPacketSerializer* Serializer);   // retail FUN_06d9bc80
int RpcRegisterChannel3Types(FPacketSerializer* Serializer);   // retail FUN_06d9f100

// ===========================================================================
// FPacketQueue / FRpcConnection -- the retail CDAConnection.
//
// The connection is the 0x30-byte object FUN_06d88280 allocates (+0x00..+0x10 and the byte
// at +0x14 zeroed):
//   +0x00  FPacketQueue*  server-bound read  queue   (FUN_06d86580 pops from it)
//   +0x04  FPacketQueue*  server-bound write queue   (FUN_06d86150 tries it first, then +0x00)
//   +0x08  FPacketQueue*  client-bound read  queue   (FUN_06d864a0 pops; IServerImpl::Update)
//   +0x0c  FPacketQueue*  client-bound write queue   (FUN_06d85e00 tries it first, then +0x08)
//   +0x10  unsigned int   the Start() mode
//   +0x14  bool           started
//   +0x18  CRITICAL_SECTION (0x18 bytes, completing the 0x30)
//
// Four queues in two read/write pairs. FUN_06d85b60 (Start) allocates +0x00 and +0x08 for
// mode 1, all four for mode 2; returns 3 when already started, 1 for a bad mode, 2 when an
// allocation failed, 0 on success. IServerImpl::Connect passes mode 1, so the write slots
// stay null and both directions push into the queue they pop from.
//
// Each queue is a separate operator_new(0x30) object: a plain MSVC6 std::deque<CDAPacket*>
// with one leading byte (the Start() mode) -- _First at +0x04, _Last +0x14, _Map +0x24,
// _Mapsize +0x28, _Size +0x2c, 0x1000-byte blocks. The pointer is copied into the deque
// block (FUN_06d8cb70), so CDAPacket+0x08 is never touched by the queue.
//
// The port uses std::deque<FPacketBase*>. Retail's push/pop take no lock, and neither do
// ours; the CRITICAL_SECTION is only ever initialised (Start) and deleted (FUN_06d857c0).
// ===========================================================================
class FPacketQueue {
public:
    explicit FPacketQueue(unsigned char InMode);

    void Push(FPacketBase* Packet);     // retail deque::push_back
    FPacketBase* Pop();                 // retail front() + pop_front()
    void DestroyAll();                  // one arm of FUN_06d857c0
    unsigned int Num() const { return static_cast<unsigned int>(Packets.size()); }
    unsigned char GetMode() const { return Mode; }

private:
    unsigned char Mode;                 // +0x00
    std::deque<FPacketBase*> Packets;   // +0x04 .. +0x2f
};

class FRpcConnection {
public:
    // FUN_06d85b60 return codes.
    enum EStartResult {
        kStartOk            = 0,
        kStartBadMode       = 1,
        kStartAllocFailed   = 2,
        kStartAlreadyOn     = 3
    };
    // The two mode bits FUN_06d85b60 tests.
    enum EStartMode {
        kModeSingleBuffered = 1,    // two queues; what IServerImpl::Connect passes
        kModeDoubleBuffered = 2     // four queues, separate read and write sides
    };
    // FUN_06d85e00 / FUN_06d86150 return 4 when the target queue pointer is null.
    enum EPushResult {
        kPushOk             = 0,
        kPushNoQueue        = 4
    };

    FRpcConnection();                   // the stores FUN_06d88280 makes
    ~FRpcConnection();                  // retail FUN_06d857c0; `delete` adds the free after it

    unsigned int Start(unsigned int InMode);    // retail FUN_06d85b60
    unsigned int GetMode() const { return Mode; }

    unsigned int PushServerBound(FPacketBase* Packet);  // retail FUN_06d86150
    FPacketBase* PopServerBound();                      // retail FUN_06d86580
    unsigned int PushClientBound(FPacketBase* Packet);  // retail FUN_06d85e00
    FPacketBase* PopClientBound();                      // retail FUN_06d864a0

    unsigned int NumServerBound() const;
    unsigned int NumClientBound() const;

private:
    void DestroyQueue(FPacketQueue* Queue);             // one arm of FUN_06d857c0

    FPacketQueue* ServerBoundRead;      // +0x00
    FPacketQueue* ServerBoundWrite;     // +0x04  null in mode 1
    FPacketQueue* ClientBoundRead;      // +0x08
    FPacketQueue* ClientBoundWrite;     // +0x0c  null in mode 1
    unsigned int  Mode;                 // +0x10
    bool          bStarted;             // +0x14
    // +0x18. Initialised only on Start's success path, deleted unconditionally by the
    // destructor, never entered. Retail defect, reproduced: a connection destroyed without
    // a successful Start deletes an uninitialised section (in practice only Shutdown
    // destroys it, after Connect started it).
    CRITICAL_SECTION Lock;
};

// ===========================================================================
// Module-wide transport state (retail keeps the same things in .data)
// ===========================================================================
// The CDAConnection. Retail keeps three .data pointers to the same singleton:
//   .data:DAT_06dbbb5c  FUN_06d88280's own cache                -> GRpcConnection (here)
//   .data:DAT_06dbbd58  the server-side pointer (IServerImpl)   -> GServerConnection (rpclocal.cpp)
//   .data:DAT_06dbbd1c  the client-side pointer (IClient proxy) -> GClientConnection
//                                                                  (rpc_client_proxy.cpp)
// Retail defect, reproduced: IServerImpl::Shutdown frees the object and clears only
// DAT_06dbbd58, so a second Connect in the same load would get the freed pointer back.
// freelancer.exe never does that: it FreeLibrary()s the module right after Shutdown
// (ShutdownLocalServer, src/freelancer/Client/server_connect.cpp).
extern FRpcConnection* GRpcConnection;
// The packet-type registry is not a module global: IServerImpl and the IClient proxy each
// hold it at this+0x08, filled from RpcGetPacketSerializer().
// .data:0x06dbbcd8 / 0x06dbbcd4 -- the real client interfaces inside freelancer.exe
extern IClient* GRealClient;
extern IChatClient* GRealChatClient;
// .data:0x06dbbbc0 -- the caller's module path, saved by IServerImpl::Startup
extern char GCallerModulePath[260];
// .data:0x06dbbd19 -- the "connected" byte: set by IServerImpl::Connect once the connection
// started, cleared by DisConnect, Shutdown and IClient slot 17, read by the pump (IClient
// slot 16). Defined in rpc_client_proxy.cpp.
extern bool GbRpcConnected;

// FUN_06d88280: return the singleton, creating it on first call. It does not start it --
// IServerImpl::Connect starts it with FUN_06d85b60(conn, 1); IClient slot 16 only fetches
// it. A packet pushed before Connect is dropped by PushToServer or IClient slot 9, as in
// retail.
FRpcConnection* RpcCreateConnection();

// ===========================================================================
// Incoming dispatch -- retail IServerImpl::HandlePacket (0x06d81170)
// ===========================================================================
// Returns true when the packet was routed into a real IClient / IChatClient slot.
bool RpcHandleIncomingPacket(FPacketBase* Packet);

// ===========================================================================
// The IClient proxy rpclocal exports (retail .data:Client, 0x06dbbcdc).
// ===========================================================================
IClient* RpcGetClientProxy();
IChatClient* RpcGetChatClientProxy();
// IClient slot 16 (0x06d8efc0): the client-side pump IServerImpl::Update runs.
void RpcClientPump();

