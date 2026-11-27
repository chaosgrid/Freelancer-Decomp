// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// rpc_packet.h -- the RPC packet base and the packet-type registry, shared by
// rpclocal.dll (src/rpclocal/) and remoteserver.dll (src/RemoteServer/).
//
// Retail compiles this code into both DLLs: remoteserver's IServerImpl::FireWeapon
// (.text:06bd8140) allocates through its own serializer at IServerImpl+0x08
// (`MOV ECX,[EBP+0x8]` / CALL FUN_06be1750), whose body is the same as rpclocal's
// FUN_06d96300 (null out -> 4, (Channel << 16 | Opcode) map lookup, miss -> 0x19,
// else the factory's answer). Member bodies for rpclocal are in
// src/rpclocal/rpc_transport.cpp; nothing here is exported -- each module links its
// own definitions.
//
// Ground truth: docs/re/rpclocal.md, EXE/Disasm/rpclocal.dll.txt.
#pragma once

#include <iosfwd>
#include <map>
#include <string>

class CDAPacket;

// ===========================================================================
// Protocol packet ids (all 161 unique opcodes / 162 packet classes)
//
// Channels:
//   Channel 1: Common (bidirectional) — 0x10000 | id  (21 opcodes)
//   Channel 2: Server -> Client       — 0x20000 | id  (81 opcodes)
//   Channel 3: Client -> Server       — 0x30000 | id  (60 opcodes)
// ===========================================================================
enum EFlPacket : unsigned int
{
    // -----------------------------------------------------------------------
    // Common (bidirectional) 0x10000 | id (21 packets)
    // -----------------------------------------------------------------------
    CYCLOPSMESSAGE_COMMON_UPDATEOBJECT      = 0x10001,
    FLPACKET_COMMON_UPDATEOBJECT            = 0x10001,
    FLPACKET_COMMON_FIREWEAPON              = 0x10002,
    FLPACKET_COMMON_SETTARGET               = 0x10004,
    FLPACKET_COMMON_CHATMSG                 = 0x10005,
    FLPACKET_COMMON_ACTIVATEEQUIP           = 0x10008,
    FLPACKET_COMMON_ACTIVATECRUISE          = 0x1000e,
    FLPACKET_COMMON_GOTRADELANE             = 0x1000f,
    FLPACKET_COMMON_STOPTRADELANE           = 0x10010,
    FLPACKET_COMMON_SET_WEAPON_GROUP        = 0x10011,
    FLPACKET_COMMON_PLAYER_TRADE            = 0x10012,
    FLPACKET_COMMON_SET_VISITED_STATE       = 0x10013,
    FLPACKET_COMMON_JETTISONCARGO           = 0x10014,
    FLPACKET_COMMON_ACTIVATETHRUSTERS       = 0x10015,
    FLPACKET_COMMON_REQUEST_BEST_PATH       = 0x10016,
    FLPACKET_COMMON_REQUEST_GROUP_POSITIONS = 0x10017,
    FLPACKET_COMMON_REQUEST_PLAYER_STATS    = 0x10018,
    FLPACKET_COMMON_SET_MISSION_LOG         = 0x10019,
    FLPACKET_COMMON_REQUEST_RANK_LEVEL      = 0x1001a,
    FLPACKET_COMMON_POP_UP_DIALOG           = 0x1001b,
    FLPACKET_COMMON_SET_INTERFACE_STATE     = 0x1001c,
    FLPACKET_COMMON_TRACTOROBJECTS          = 0x1001d,

    // -----------------------------------------------------------------------
    // Server -> Client 0x20000 | id (81 packets)
    // -----------------------------------------------------------------------
    FLPACKET_SERVER_CONNECTRESPONSE              = 0x20001,
    FLPACKET_SERVER_LOGINRESPONSE                = 0x20002,
    FLPACKET_SERVER_CHARACTERINFO                = 0x20003,
    FLPACKET_SERVER_CREATESHIP                   = 0x20004,
    FLPACKET_SERVER_DAMAGEOBJECT                 = 0x20005,
    FLPACKET_SERVER_DESTROYOBJECT                = 0x20006,
    FLPACKET_SERVER_LAUNCH                       = 0x20007,
    FLPACKET_SERVER_CHARSELECTVERIFIED           = 0x20008,
    FLPACKET_SERVER_ACTIVATEOBJECT               = 0x2000a,
    FLPACKET_SERVER_LAND                         = 0x2000b,
    FLPACKET_SERVER_SETSTARTROOM                 = 0x2000d,
    FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST  = 0x2000e,
    FLPACKET_SERVER_GFCOMPLETECHARLIST           = 0x2000f,
    FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST= 0x20010,
    FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST = 0x20011,
    FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST  = 0x20013,
    FLPACKET_SERVER_GFDESTROYNEWSBROADCAST       = 0x20014,
    FLPACKET_SERVER_GFDESTROYCHARACTER           = 0x20015,
    FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER     = 0x20016,
    FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR      = 0x20017,
    FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT       = 0x20019,
    FLPACKET_SERVER_GFSCRIPTBEHAVIOR             = 0x2001a,
    FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER      = 0x2001d,
    FLPACKET_SERVER_GFUPDATENEWSBROADCAST        = 0x2001e,
    FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT        = 0x2001f,
    FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE    = 0x20020,
    FLPACKET_SERVER_SYSTEM_SWITCH_OUT            = 0x20021,
    FLPACKET_SERVER_SYSTEM_SWITCH_IN             = 0x20022,
    FLPACKET_SERVER_SETSHIPARCH                  = 0x20023,
    FLPACKET_SERVER_SETEQUIPMENT                 = 0x20024,
    FLPACKET_SERVER_SETCARGO                     = 0x20025,
    FLPACKET_SERVER_GFUPDATECHAR                 = 0x20026,
    FLPACKET_SERVER_REQUESTCREATESHIPRESP        = 0x20027,
    FLPACKET_SERVER_CREATELOOT                   = 0x20028,
    FLPACKET_SERVER_SETREPUTATION                = 0x20029,
    FLPACKET_SERVER_ADJUSTATTITUDE               = 0x2002a,
    FLPACKET_SERVER_SETGROUPFEELINGS             = 0x2002b,
    FLPACKET_SERVER_CREATEMINE                   = 0x2002c,
    FLPACKET_SERVER_CREATECOUNTER                = 0x2002d,
    FLPACKET_SERVER_SETADDITEM                   = 0x2002e,
    FLPACKET_SERVER_SETREMOVEITEM                = 0x2002f,
    FLPACKET_SERVER_SETCASH                      = 0x20030,
    FLPACKET_SERVER_EXPLODEASTEROIDMINE          = 0x20031,
    FLPACKET_SERVER_REQUESTSPACESCRIPT           = 0x20032,
    FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE     = 0x20033,
    FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE      = 0x20034,
    FLPACKET_SERVER_SETMISSIONOBJECTIVES         = 0x20035,
    FLPACKET_SERVER_CREATEGUIDED                 = 0x20037,
    FLPACKET_SERVER_ITEMTRACTORED                = 0x20038,
    FLPACKET_SERVER_SCANNOTIFY                   = 0x20039,
    FLPACKET_SERVER_REPAIROBJECT                 = 0x2003c,
    FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE      = 0x2003d,
    FLPACKET_SERVER_SETNUMKILLS                  = 0x2003e,
    FLPACKET_SERVER_SETMISSIONSUCCESSES          = 0x2003f,
    FLPACKET_SERVER_SETMISSIONFAILURES           = 0x20040,
    FLPACKET_SERVER_BURNFUSE                     = 0x20041,
    FLPACKET_SERVER_CREATESOLAR                  = 0x20042,
    FLPACKET_SERVER_SET_STORY_CUE                = 0x20043,
    FLPACKET_SERVER_REQUEST_RETURNED             = 0x20044,
    FLPACKET_SERVER_SET_MISSION_MESSAGE          = 0x20045,
    FLPACKET_SERVER_MARKOBJ                      = 0x20046,
    FLPACKET_SERVER_CFGINTERFACENOTIFICATION     = 0x20047,
    FLPACKET_SERVER_SETCOLLISIONGROUPS           = 0x20048,
    FLPACKET_SERVER_SETHULLSTATUS                = 0x20049,
    FLPACKET_SERVER_SETGUIDEDTARGET              = 0x2004a,
    FLPACKET_SERVER_SET_CAMERA                   = 0x2004b,
    FLPACKET_SERVER_REVERT_CAMERA                = 0x2004c,
    FLPACKET_SERVER_LOADHINT                     = 0x2004d,
    FLPACKET_SERVER_SETDIRECTIVE                 = 0x2004e,
    FLPACKET_SERVER_SENDCOMM                     = 0x2004f,
    FLPACKET_SERVER_USE_ITEM                     = 0x20051,
    FLPACKET_SERVER_PLAYERLIST                   = 0x20052,
    FLPACKET_SERVER_FORMATION_UPDATE             = 0x20053,
    FLPACKET_SERVER_MISCOBJUPDATE                = 0x20054,
    FLPACKET_SERVER_OBJECTCARGOUPDATE            = 0x20055,
    FLPACKET_SERVER_SENDNNMESSAGE                = 0x20056,
    FLPACKET_SERVER_SET_MUSIC                    = 0x20057,
    FLPACKET_SERVER_CANCEL_MUSIC                 = 0x20058,
    FLPACKET_SERVER_PLAY_SOUND_EFFECT            = 0x20059,
    FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY      = 0x2005a,
    FLPACKET_SERVER_MISSIONSAVEA                 = 0x2005b,

    // -----------------------------------------------------------------------
    // Client -> Server 0x30000 | id (60 packets)
    // -----------------------------------------------------------------------
    FLPACKET_CLIENT_LOGIN                        = 0x30001,
    FLPACKET_CLIENT_MUNCOLLISION                 = 0x30003,
    FLPACKET_CLIENT_REQUESTLAUNCH                = 0x30004,
    FLPACKET_CLIENT_REQUESTCHARINFO              = 0x30005,
    FLPACKET_CLIENT_SELECTCHARACTER              = 0x30006,
    FLPACKET_CLIENT_ENTERBASE                    = 0x30007,
    FLPACKET_CLIENT_REQUESTBASEINFO              = 0x30008,
    FLPACKET_CLIENT_REQUESTLOCATIONINFO          = 0x30009,
    FLPACKET_CLIENT_GFREQUESTSHIPINFO            = 0x3000a,
    FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE   = 0x3000b,
    FLPACKET_CLIENT_OBJCOLLISION                 = 0x3000c,
    FLPACKET_CLIENT_EXITBASE                     = 0x3000d,
    FLPACKET_CLIENT_ENTERLOCATION                = 0x3000e,
    FLPACKET_CLIENT_EXITLOCATION                 = 0x3000f,
    FLPACKET_CLIENT_REQUESTCREATESHIP            = 0x30010,
    FLPACKET_CLIENT_GFGOODSELL                   = 0x30011,
    FLPACKET_CLIENT_GFGOODBUY                    = 0x30012,
    FLPACKET_CLIENT_GFSELECTOBJECT               = 0x30013,
    FLPACKET_CLIENT_MISSIONRESPONSE              = 0x30014,
    FLPACKET_CLIENT_REQSHIPARCH                  = 0x30015,
    FLPACKET_CLIENT_REQEQUIPMENT                 = 0x30016,
    FLPACKET_CLIENT_REQCARGO                     = 0x30017,
    FLPACKET_CLIENT_REQADDITEM                   = 0x30018,
    FLPACKET_CLIENT_REQREMOVEITEM                = 0x30019,
    FLPACKET_CLIENT_REQMODIFYITEM                = 0x3001a,
    FLPACKET_CLIENT_REQSETCASH                   = 0x3001b,
    FLPACKET_CLIENT_REQCHANGECASH                = 0x3001c,
    FLPACKET_CLIENT_SAVEGAME                     = 0x3001e,
    FLPACKET_CLIENT_MINEASTEROID                 = 0x30020,
    FLPACKET_CLIENT_DBGCREATESHIP                = 0x30022,
    FLPACKET_CLIENT_DBGLOADSYSTEM                = 0x30023,
    FLPACKET_CLIENT_DOCK                         = 0x30024,
    FLPACKET_CLIENT_DBGDESTROYOBJECT             = 0x30025,
    FLPACKET_CLIENT_TRADERESPONSE                = 0x30027,
    FLPACKET_CLIENT_CARGOSCAN                    = 0x3002b,
    FLPACKET_CLIENT_DBGCONSOLE                   = 0x3002d,
    FLPACKET_CLIENT_DBGFREESYSTEM                = 0x3002e,
    FLPACKET_CLIENT_SETMANEUVER                  = 0x3002f,
    FLPACKET_CLIENT_DBGRELOCATE_SHIP             = 0x30030,
    FLPACKET_CLIENT_REQUEST_EVENT                = 0x30031,
    FLPACKET_CLIENT_REQUEST_CANCEL               = 0x30032,
    FLPACKET_CLIENT_INTERFACEITEMUSED            = 0x30035,
    FLPACKET_CLIENT_REQCOLLISIONGROUPS           = 0x30036,
    FLPACKET_CLIENT_COMMCOMPLETE                 = 0x30037,
    FLPACKET_CLIENT_REQUESTNEWCHARINFO           = 0x30038,
    FLPACKET_CLIENT_CREATENEWCHAR                = 0x30039,
    FLPACKET_CLIENT_DESTROYCHAR                  = 0x3003a,
    FLPACKET_CLIENT_REQHULLSTATUS                = 0x3003b,
    FLPACKET_CLIENT_GFGOODVAPORIZED              = 0x3003c,
    FLPACKET_CLIENT_BADLANDSOBJCOLLISION         = 0x3003d,
    FLPACKET_CLIENT_LAUNCHCOMPLETE               = 0x3003e,
    FLPACKET_CLIENT_HAIL                         = 0x3003f,
    FLPACKET_CLIENT_REQUEST_USE_ITEM             = 0x30040,
    FLPACKET_CLIENT_ABORT_MISSION                = 0x30041,
    FLPACKET_CLIENT_SKIP_AUTOSAVE                = 0x30042,
    FLPACKET_CLIENT_JUMPINCOMPLETE               = 0x30043,
    FLPACKET_CLIENT_REQINVINCIBILITY             = 0x30044,
    FLPACKET_CLIENT_MISSIONSAVEB                 = 0x30045,
    FLPACKET_CLIENT_REQDIFFICULTYSCALE           = 0x30046,
    FLPACKET_CLIENT_RTCDONE                      = 0x30047,
};

// The three channels of the packet id (id >> 16): FLPACKET_COMMON_*, FLPACKET_SERVER_* and
// FLPACKET_CLIENT_*, the prefixes of the retail registration strings.
enum EFlPacketChannel : unsigned int
{
    kPacketChannelCommon = 1,
    kPacketChannelServer = 2,
    kPacketChannelClient = 3,
};

// The channel and the per-channel opcode halves of an EFlPacket id: the pair the registry
// key (Channel << 16 | Opcode), FPacketSerializer::Allocate and the packet dispatchers use.
constexpr unsigned int FlPacketChannel(EFlPacket Id) { return static_cast<unsigned int>(Id) >> 16; }
constexpr unsigned int FlPacketOpcode(EFlPacket Id) { return static_cast<unsigned int>(Id) & 0xffffu; }

// The per-equipment-record flag byte of the CREATESHIP / CREATESOLAR equipment lists (built by
// rpclocal 06da3ba3 and RemoteCommon 06c08ac6, taken apart at 06da4479 / 06c09399). Every bit is
// a wire-width or presence switch.
enum EEquipRecordFlag : unsigned char
{
    kEquipRecordEquipped = 0x01,
    kEquipRecordMission = 0x02,
    kEquipRecordWideCount = 0x04,      // the count is a dword (else a byte)
    kEquipRecordWideId = 0x08,         // the id is a word (else a byte)
    kEquipRecordHardpointText = 0x10,  // the hardpoint is a literal string (else a dictionary code)
    kEquipRecordNoHardpoint = 0x20,
    kEquipRecordFullHealth = 0x40,     // health == 1.0: no status byte follows
    kEquipRecordUnitCount = 0x80,      // the count is 1: no count follows
};

// The eight messages carried by FLPACKET_COMMON_PLAYER_TRADE, one bit each in the packet's first
// word (FPayloadCommonPlayerTrade::SubType / FRpcPlayerTradePacket::Action; the wire sends its low
// byte). Each is named after the IClient::SendCommonPlayerTrade* / IServer slot that sends or
// receives it (retail senders 0x06d8b8e0..0x06d8bb60, receiver sub-switch 0x06d8375d).
enum EPlayerTradeAction : unsigned int
{
    kPlayerTradeInitiateTrade = 0x01,
    kPlayerTradeTerminateTrade = 0x02,
    kPlayerTradeSetTradeMoney = 0x04,
    kPlayerTradeAddTradeEquip = 0x08,
    kPlayerTradeDelTradeEquip = 0x10,
    kPlayerTradeRequestTrade = 0x20,
    kPlayerTradeAcceptTrade = 0x40,
    kPlayerTradeStopTradeRequest = 0x80,
};

// Flag byte of FLPACKET_COMMON_FIREWEAPON in a multiplayer encode: one width bit per target
// component (small tier = byte, medium tier = word, else the raw float; z has no medium tier,
// and ZMedium is read by both codecs but written nowhere) and the one-hardpoint shortcut that
// leaves the hardpoint count out (rpclocal FUN_06da50e8 and the matching decoder).
enum EFireWeaponFlag : unsigned char
{
    kFireWeaponXSmall = 0x01,
    kFireWeaponXMedium = 0x02,
    kFireWeaponYSmall = 0x04,
    kFireWeaponYMedium = 0x08,
    kFireWeaponZSmall = 0x10,
    kFireWeaponZMedium = 0x20,
    kFireWeaponOneHardpoint = 0x40,
};

// The CREATESHIP tail's one flag byte (built at rpclocal 06d97d82, taken apart at 06d982d0).
// Bit 0x04 is a presence switch as well as a value bit: it decides between the wide display
// name and the two flattened FmtStrs.
enum ECreateShipFlag : unsigned char
{
    kCreateShipFlagUsingTradelane = 0x01,   // packet bUsingTradelane
    kCreateShipFlagMarkedBy = 0x02,         // packet bMarkedBy
    kCreateShipFlagOwnerPlayer = 0x04,      // packet OwnerPlayer != 0
};

// Presence word of FLPACKET_SERVER_MISCOBJUPDATE: one bit per field group, named after the
// packet's Set* method that raises it (RC 06b45830..06b45a60).
enum EMiscObjUpdateFlag : unsigned short
{
    kMiscObjUpdateFlagGroupId = 0x0001,
    kMiscObjUpdateFlagMission = 0x0002,
    kMiscObjUpdateFlagPlayerId = 0x0004,
    kMiscObjUpdateFlagObjectId = 0x0008,
    kMiscObjUpdateFlagText = 0x0010,
    kMiscObjUpdateFlagAffiliation = 0x0020,
    kMiscObjUpdateFlagRank = 0x0040,
    kMiscObjUpdateFlagSystemId = 0x0080,
    kMiscObjUpdateFlagIds = 0x0100,
};

// The five presence-flag constants the SETREPUTATION constructors seed into the packet
// (Mask* members); the codecs test the low byte of each against the packet's Flags byte.
enum ESetReputationMask : unsigned int
{
    kSetReputationMaskAffiliation = 0x01,
    kSetReputationMaskNames = 0x02,
    kSetReputationMaskDisplayName = 0x04,
    kSetReputationMaskRelationsByte = 0x08,     // relation count is a byte
    kSetReputationMaskRelationsWord = 0x10,     // relation count is a word
};

// ===========================================================================
// FRpcRefObject -- the refcounted root every packet (and the serializer) derives from.
//
// Vtable .rdata:06db3c54 has ONE slot, the scalar deleting dtor LAB_06d8f4d0, whose dtor
// FUN_06d8f4c0 only stores that vptr back. The use count sits at +0x04; every retail
// constructor that reaches this base writes 1 there (e.g. 06d9afb3 in FLPACKET_COMMON_
// FIREWEAPON's, 06d95e92 in the serializer's).
// ===========================================================================
class FRpcRefObject {
public:
    FRpcRefObject() : UsageCount(1) {}
    virtual ~FRpcRefObject();                   // FUN_06d8f4c0 / scalar LAB_06d8f4d0

    // FUN_06d8f4a0: a zero count is returned untouched; otherwise decrement, and at zero
    // call the scalar deleting dtor (delete flag) and return 0, else re-read the count.
    unsigned int Release();

    unsigned int UsageCount;                    // +0x04
};

// ===========================================================================
// FPacketBase -- retail CDAPacket, exactly 0x20 bytes.
//
// Vtable .rdata:06db4fbc = { 06d9c5d0 scalar dtor, 06da13d0 Encode, 06da1470 Decode,
// the shared no-op dump }. Its dtor FUN_06da1370 is `MOV [ECX],06db4fbc ; JMP
// FUN_06d8f4c0`. Every retail packet constructor makes the same five base stores
// (e.g. FUN_06d9c860, FLPACKET_SERVER_LOGINRESPONSE):
//     [+0x04] = 1 ; [+0x08] = 0 ; [+0x10] = 0 ; [+0x1c] = 0 ; [+0x0c] = PacketId
// and leaves +0x14 / +0x18 alone.
//
// The 162 concrete classes derive from this one -- see src/rpclocal/rpc_packet_registry.cpp. The
// payload is theirs and starts at +0x20.
//
// class CDAPacket in src/include/common_server_types.h is the plain-struct mirror of
// this layout, so a FPacketBase* and a CDAPacket* are interchangeable by cast.
// ===========================================================================
class FPacketBase : public FRpcRefObject {
public:
    explicit FPacketBase(unsigned int InPacketId);

    // --- packet vtable, 4 slots, retail order ---
    ~FPacketBase() override;                                                   // +0x00 FUN_06da1370
    // +0x04 -- marshal the payload out for the network transport. The first argument is a
    // pointer to the buffer pointer, which retail never writes back (the moving cursor is
    // packet+0x18). OutLength includes the two header bytes. CDAPacket's own body,
    // 06da13d0, writes the header and nothing else.
    virtual unsigned int Encode(unsigned char** BufferHolder, unsigned int Capacity,
                                unsigned int* OutLength);
    // +0x08 -- the inverse. Takes the buffer pointer directly, and OutLength excludes the
    // header, which the dispatcher has already consumed. CDAPacket's own body is 06da1470.
    virtual unsigned int Decode(const unsigned char* Buffer, unsigned int Size,
                                unsigned int* OutLength);
    // +0x0c dump/trace slot taking a std::ostream& (the twelve real bodies stream into
    // it). The other 150 types share one no-op body, ICF-folded onto
    // IServerImpl::GetServerStats; see src/rpclocal/rpc_packet_dump.cpp.
    virtual void Dump(std::ostream& Out) const;

    unsigned int GetChannel() const { return PacketId >> 16; }
    unsigned int GetOpcode() const { return PacketId & 0xfff; }

    // Payload starts at +0x20 and belongs to the concrete packet class.
    unsigned char* Payload() { return reinterpret_cast<unsigned char*>(this) + 0x20; }
    const unsigned char* Payload() const { return reinterpret_cast<const unsigned char*>(this) + 0x20; }
    // The payload struct a concrete packet class (or a codec shared by several) knows it has.
    template<class T> T* PayloadAs() { return reinterpret_cast<T*>(Payload()); }
    template<class T> const T* PayloadAs() const { return reinterpret_cast<const T*>(Payload()); }
    unsigned int* PayloadDwords() { return reinterpret_cast<unsigned int*>(Payload()); }
    const unsigned int* PayloadDwords() const { return reinterpret_cast<const unsigned int*>(Payload()); }

    void AddRef();

    CDAPacket* AsRaw() { return reinterpret_cast<CDAPacket*>(this); }

    // MarshalPtr holds an address (CDAPacket::BufferCursor is an unsigned char*); it stays an
    // integer because a pointer-typed member changes the reloads the codecs compile to.
    unsigned char* MarshalBuffer() const { return reinterpret_cast<unsigned char*>(MarshalPtr); }
    void SetMarshalBuffer(unsigned char* Buffer) { MarshalPtr = reinterpret_cast<unsigned int>(Buffer); }

    // --- layout, mirroring CDAPacket exactly (UsageCount +0x04 is FRpcRefObject's) ---
    // +0x08 -- every retail packet constructor zeroes it, but the connection queues (VC6
    // std::deque<CDAPacket*>, FPacketQueue in src/rpclocal/rpc_transport.h) do not use it.
    // TODO: no reader of this field has been found anywhere in rpclocal; semantics unknown.
    FPacketBase* Field_0x08;    // +0x08  purpose unproven, retail only ever writes 0
    unsigned int PacketId;      // +0x0c  Channel << 16 | Opcode
    // The marshalling scratch the codec slots run on. Encode's entry (e.g. 0x06d97d20) sets
    // {0, capacity, *buffer pointer, 0}, then FUN_06da3720 walks the payload, per field:
    //     Cursor += width; if (Cursor > Limit) Error = code; if (Error) return code;
    //     *Ptr = value; Ptr += width;
    // The decoder FUN_06da4020 is the same with reads and writes swapped and error 0x17
    // instead of 0x16. The constructor zeroes +0x10 and +0x1c and leaves +0x14 / +0x18 alone.
    unsigned int MarshalCursor;   // +0x10  bytes emitted/consumed so far
    unsigned int MarshalLimit;    // +0x14  capacity of the caller's buffer
    unsigned int MarshalPtr;      // +0x18  moving read/write pointer into that buffer
    unsigned int MarshalError;    // +0x1c  0 = ok, 0x16 = encode overflow, 0x17 = decode overrun
};

// The compiler must place the vptr first and pack the seven dwords behind it, so the
// payload really does begin at +0x20 exactly as retail's does.
static_assert(sizeof(FPacketBase) == 0x20, "FPacketBase must match the retail CDAPacket base");

// ===========================================================================
// The retail packet factory -- one per registered class.
//
// FLPACKET_COMMON_FIREWEAPON's, FUN_06d9af70, is the model for all 162 (__cdecl, one
// CDAPacket** argument): *out = 0; operator new(size) -- the size lives here, not in the
// registry; null -> 5; ctor; *out = packet; return 0. The generated body is
// RpcPacketFactory<T> in src/rpclocal/rpc_packet_registry.cpp.
typedef int (*FPacketFactoryFn)(FPacketBase** OutPacket);

// Retail's two factory return codes.
const int kPacketFactoryOk = 0;
const int kPacketFactoryNoMemory = 5;
// FUN_06d960a0's duplicate-id answer.
const int kPacketRegisterDuplicate = 0x42;
// FUN_06d96300's two failures: a null out pointer, and an id not in the map.
const int kPacketAllocateNullOut = 4;
const int kPacketAllocateUnknownType = 0x19;

// FPacketBase::MarshalError values, and what Encode / Decode return on failure (the codecs run
// on a sticky error: the first overflowing / overrunning field decides).
const unsigned int kPacketEncodeOverflow = 0x16;
const unsigned int kPacketDecodeOverrun = 0x17;
// What Decode returns when the operator new for a field it owns fails (the factory's
// kPacketFactoryNoMemory answer).
const unsigned int kPacketNoMemory = 5;

// ===========================================================================
// FPacketTypeInfo / FPacketSerializer -- the retail packet-type registry.
//
// Retail: FUN_06d960a0(Serializer, Channel << 16 | Opcode, "FLPACKET_...", Factory),
// 162 registrations spread over three registrators (FUN_06d9ace0 channel 1,
// FUN_06d9bc80 channel 2, FUN_06d9f100 channel 3). The serializer object itself lives
// at IServerImpl+0x08 (built by FUN_06d95e50) and at IClient+0x08.
//
// The table is a std::map keyed on the packet id, at serializer+0x08. The mapped value is
// a pointer to a 0x14-byte record: the factory at +0x00 and a MSVCP60 std::string name at
// +0x04. No size and no counter: the counters live in GRpcPacketStats
// (src/rpclocal/rpc_transport.h).
// ===========================================================================
struct FPacketTypeInfo {
    FPacketFactoryFn Factory;    // +0x00
    std::string Name;            // +0x04
};

// An FRpcRefObject of 0x1c bytes (FUN_06d95e50): use count 1, the map at +0x08, its own
// vptr 06db3e18 whose one slot is the scalar deleting destructor FUN_06d964c0, reached
// only through FRpcRefObject::Release from IServerImpl::Shutdown.
class FPacketSerializer : public FRpcRefObject {
public:
    FPacketSerializer();
    ~FPacketSerializer() override;                  // FUN_06d95f30 / scalar FUN_06d964c0

    // FUN_06d960a0, three stack arguments (no size): 0x42 for an id already in the map,
    // otherwise a fresh record inserted and 0.
    int RegisterType(unsigned int PacketId, const char* Name, FPacketFactoryFn Factory);

    // Retail FUN_06d96300(Serializer, Channel, Opcode, &OutPacket): 4 for a null out
    // pointer, 0x19 for an id not in the map, otherwise whatever the factory returns.
    int Allocate(unsigned int Channel, unsigned int Opcode, FPacketBase** OutPacket);

    // Retail FUN_06d961b0, reached from IServerImpl::DumpPacketStats and IClient slot 125.
    void DumpStats(const char* Name) const;

private:
    std::map<unsigned int, FPacketTypeInfo*> Types;   // serializer+0x08
};

// Compiled into each Remote DLL: raw lazy cache, no registration or AddRef.
FPacketSerializer* RpcGetRemotePacketSerializer();
