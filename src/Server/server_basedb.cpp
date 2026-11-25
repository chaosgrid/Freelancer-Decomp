// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// gf/BaseDB.cpp -- Server.dll's runtime base / room database.
// See src/include/server_basedb.h for the object model and the RTTI evidence.

#include "../include/server_basedb.h"
#include "../include/common_server.h"          // Players, PlayerDB
#include "../include/common_server_playerdb.h" // PlayerData
#include "../include/common_reputation.h"      // Reputation::Vibe::GetName
#include "../include/common_misc.h"            // malloc_strcpy, malloc_wstrcpy
#include "../include/common_net.h"             // MarshalBuffer
#include "../include/common_id.h"              // CreateID
#include "../include/common_text.h"            // FmtStr
#include "../include/rpc_iclient.h"            // IClient
#include "../include/dacom.h"                  // FDUMP
#include <cstring>
#include <stdexcept>

// DAT_06d8fb14, owned by IServerImpl.cpp.
extern IClient* g_pClientInterface;

namespace
{
    char const* const GBaseDBFile = "E:\\FL\\Scratch\\Source\\Server\\gf\\BaseDB.cpp";

    // The five handle pools (see FHandlePool). Static storage is zero-filled,
    // which is all their __xc initializers LAB_06d118a0 / 06d122d0 / 06d12500 /
    // 06d13050 / 06d13330 do.
    FHandlePool GCharacterHandles;     // DAT_06d8e328
    FHandlePool GBehaviorHandles;      // DAT_06d8e120
    FHandlePool GAmbientScriptHandles; // DAT_06d8df18
    FHandlePool GMissionVendorHandles; // DAT_06d8dd10
    FHandlePool GNewsHandles;          // DAT_06d8db08

    // FUN_06d15f90: std::bitset<4096>'s _Xran -- builds
    // std::out_of_range("invalid bitset<N> position") and throws it (ThrowInfo
    // DAT_06d727ac). It never returns.
    void ThrowBitsetPosition()
    {
        throw std::out_of_range("invalid bitset<N> position");
    }

}

// ============================================================================
// FSessionList
// ============================================================================

// See FSessionList::free in server_basedb.h (.text:06ce57a0, ICF-folded
// OwnerList<CargoDesc>::free).
void FSessionList::free()
{
    // Every payload is deleted and its slot nulled.
    MetaNode<unsigned int>* Node = nullptr;
    while (List.traverse(Node, true))
    {
        delete Node->Data;
        Node->Data = nullptr;
    }
    List.free();
}

// FUN_06d11670.
bool FSessionList::contains(unsigned int SessionId) const
{
    MetaNode<unsigned int>* Node = List.get_head();
    while (Node != nullptr)
    {
        if (*Node->Data == SessionId)
            break;
        Node = Node->Next;
    }
    return Node != nullptr;
}

// FUN_06d116b0.
void FSessionList::add(unsigned int SessionId)
{
    if (contains(SessionId))
        return;
    unsigned int* Payload = new unsigned int;
    *Payload = SessionId;
    List.append(Payload);
}

// FUN_06d11720: the payload is deleted before the node is unlinked.
void FSessionList::remove(unsigned int SessionId)
{
    MetaNode<unsigned int>* Node = List.get_head();
    while (Node != nullptr)
    {
        if (*Node->Data == SessionId)
            break;
        Node = Node->Next;
    }
    if (Node == nullptr)
        return;
    delete Node->Data;
    List.remove(Node);
}

// ============================================================================
// Record vtable slots. The IClient pointer is called through with no null test in all
// ten.
// ============================================================================

// FUN_06d118c0 (IClient +0x78).
void CharacterRecord::send_update(unsigned int SessionId)
{
    MarshalBuffer Buf;
    Buf.use_default();
    Props.marshal(&Buf);
    g_pClientInterface->SendServerGFUpdateChar(SessionId, Buf.get_buffer(), (unsigned int)Buf.get_num_written());
}

// LAB_06d11910 (IClient +0x74).
void CharacterRecord::send_destroy(unsigned int SessionId)
{
    g_pClientInterface->SendServerGFDestroyCharacter(SessionId, Props.RoomId, Props.Key);
}

// FUN_06d11950 (IClient +0x80).
void BehaviorRecord::send_update(unsigned int SessionId)
{
    MarshalBuffer Buf;
    Buf.use_default();
    Props.marshal(&Buf);
    g_pClientInterface->SendServerGFScriptBehavior(SessionId, Buf.get_buffer(), (unsigned int)Buf.get_num_written());
}

// LAB_06d119a0 (IClient +0x84).
void BehaviorRecord::send_destroy(unsigned int SessionId)
{
    g_pClientInterface->SendServerGFDestroyScriptBehavior(SessionId, Props.LocationId, (unsigned int)Props.BehaviorId);
}

// FUN_06d119d0 (IClient +0x8c).
void AmbientScriptRecord::send_update(unsigned int SessionId)
{
    MarshalBuffer Buf;
    Buf.use_default();
    Props.marshal(&Buf);
    g_pClientInterface->SendServerGFUpdateAmbientScript(SessionId, Buf.get_buffer(), (unsigned int)Buf.get_num_written());
}

// LAB_06d11a20 (IClient +0x90).
void AmbientScriptRecord::send_destroy(unsigned int SessionId)
{
    g_pClientInterface->SendServerGFDestroyAmbientScript(SessionId, Props.RoomId, (unsigned int)Props.Key);
}

// FUN_06d11a50 (IClient +0x9c).
void MissionComputerRecord::send_update(unsigned int SessionId)
{
    MarshalBuffer Buf;
    Buf.use_default();
    Props.marshal(&Buf);
    g_pClientInterface->SendServerGFUpdateMissionComputer(SessionId, Buf.get_buffer(), (unsigned int)Buf.get_num_written());
}

// LAB_06d11aa0 (IClient +0x98).
void MissionComputerRecord::send_destroy(unsigned int SessionId)
{
    g_pClientInterface->SendServerGFDestroyMissionComputer(SessionId, Props.BaseId, (unsigned int)Props.Key);
}

// FUN_06d11ad0 (IClient +0xb0).
void NewsBroadcastRecord::send_update(unsigned int SessionId)
{
    MarshalBuffer Buf;
    Buf.use_default();
    Props.marshal(&Buf);
    g_pClientInterface->SendServerGFUpdateNewsBroadcast(SessionId, Buf.get_buffer(), (unsigned int)Buf.get_num_written());
}

// LAB_06d11b20 (IClient +0xac).
void NewsBroadcastRecord::send_destroy(unsigned int SessionId)
{
    g_pClientInterface->SendServerGFDestroyNewsBroadcast(SessionId, Props.BaseId, (unsigned int)Props.Key);
}

// FUN_06d167a0.
void FAmbientScriptList::send_all(unsigned int SessionId, unsigned int RoomId)
{
    MetaNode<AmbientScriptRecord>* Node = nullptr;
    while (Records.traverse(Node, true))
    {
        AmbientScriptRecord* Record = Node->Data;
        if (BroadcastRecord::session_passes(SessionId, Record->Filter))
            Record->send_update(SessionId);
    }
    g_pClientInterface->SendServerGFCompleteAmbientScriptList(SessionId, RoomId);
}

// ============================================================================
// FHandlePool
// ============================================================================

// FUN_06d15d80.
void FHandlePool::set(unsigned int Position, bool bValue)
{
    if (Position > 0xfff)
        ThrowBitsetPosition();
    if (bValue)
        Bits[Position >> 5] |= 1u << (Position & 0x1f);
    else
        Bits[Position >> 5] &= ~(1u << (Position & 0x1f));
}

// FUN_06d15720: returns the handle (bit + 1), or -1 when the pool is full. The range
// throw is unreachable after a wrap.
int FHandlePool::allocate()
{
    bool bSearching = true;
    unsigned int Position = Last + 1;
    if (Position >= 0x1000)
        Position = 0;
    for (;;)
    {
        if (Position >= 0x1000)
            ThrowBitsetPosition();
        if (!test(Position))
        {
            if (!bSearching)
                return -1;
            set(Position, true);
            Last = Position;
            return (int)Position + 1;
        }
        if (!bSearching)
            return -1;
        ++Position;
        if (Position >= 0x1000)
            Position = 0;
        else if (Position == Last)
            bSearching = false;
    }
}

// The copy inlined in FUN_06d122f0 and three siblings: identical scan, but the handle is
// the bit index itself and stays 0xffffffff when the scan gives up. The caller creates
// its record either way.
unsigned int FHandlePool::allocate_index()
{
    bool bSearching = true;
    unsigned int Position = Last + 1;
    if (Position >= 0x1000)
        Position = 0;
    for (;;)
    {
        if (Position >= 0x1000)
            ThrowBitsetPosition();
        if (!test(Position))
        {
            if (!bSearching)
                return 0xffffffff;
            set(Position, true);
            Last = Position;
            return Position;
        }
        if (!bSearching)
            return 0xffffffff;
        ++Position;
        if (Position >= 0x1000)
            Position = 0;
        else if (Position == Last)
            bSearching = false;
    }
}

// ============================================================================
// FRoomEntry
// ============================================================================

// FUN_06d11b70.
FRoomEntry::FRoomEntry()
{
    init();
}

// FUN_06d11c20.
FRoomEntry::~FRoomEntry()
{
    Characters.clear();
    Behaviors.clear();
    AmbientScripts.clear();
    Sessions.free();
    init();
}

// FUN_06d11ec0. Retail then calls an empty member of the session list (an ICF fold,
// 06ce1090); nothing is emitted for it.
void FRoomEntry::init()
{
    BaseId = 0;
    RoomId = 0;
    Data = nullptr;
    Characters.Sessions = &Sessions;
    Behaviors.Sessions = &Sessions;
    AmbientScripts.Sessions = &Sessions;
}

// FUN_06d11ee0 (thiscall): the player's own character record for this room.
void FRoomEntry::enter(unsigned int SessionId)
{
    int const Handle = GCharacterHandles.allocate();
    CharacterRecord* Record = new CharacterRecord();     // new(0x70)
    Record->Props.Key = (unsigned int)Handle;
    Record->Props.SessionId = SessionId;
    Record->Props.RoomId = RoomId;
    Record->Props.NameIds = 0;
    Record->Props.FactionId = 0xffffffff;
    PlayerData& Slot = Players[SessionId];
    malloc_wstrcpy(&Record->Props.NameWide, Slot.get_character_name());

    // A local copy of the 0x34-byte Costume at slot+0x2e4, then the four part ids, the
    // accessory count and the first `count` accessories -- not the whole struct.
    Costume const Source = Slot.costume;
    Record->Props.Cost.PartIds[0] = Source.PartIds[0];
    Record->Props.Cost.PartIds[1] = Source.PartIds[1];
    Record->Props.Cost.PartIds[2] = Source.PartIds[2];
    Record->Props.Cost.PartIds[3] = Source.PartIds[3];
    Record->Props.Cost.AccessoryCount = Source.AccessoryCount;
    for (int Index = 0; Index < (int)Record->Props.Cost.AccessoryCount; ++Index)
        Record->Props.Cost.AccessoryIds[Index] = Source.AccessoryIds[Index];

    malloc_strcpy(&Record->Props.Script, "scripts\\extras\\player_fidget.thn");
    Record->Props.BehaviorId = 0xffffffff;
    Record->Props.VoiceId = CreateID(Slot.voice);
    Record->Props.NameAnsi = nullptr;
    Record->Props.CharType = 1;
    Record->Props.Posture = 0;
    Record->Filter = 0;
    Record->bDirty = true;

    Characters.Records.append(Record);

    // Every session of the room that the filter (FUN_06d11b50, inlined) admits.
    MetaNode<unsigned int>* Session = nullptr;
    while (Characters.Sessions->traverse(Session, true))
    {
        unsigned int const Sid = *Session->Data;
        if (BroadcastRecord::session_passes(Sid, Record->Filter))
            Record->send_update(Sid);
    }
    Record->bDirty = false;
}

// FUN_06d120b0 (thiscall).
int FRoomEntry::create_character(CharacterProperties const& Props, unsigned int RecordFilter)
{
    int const Handle = GCharacterHandles.allocate();
    CharacterRecord* Record = new CharacterRecord();        // new(0x70)
    Record->Props = Props;
    Record->Props.Key = (unsigned int)Handle;

    // The placement the description names (Props+0x18) is looked up by name in the room's
    // character placements; its posture (+0x38) goes to Props+0x20. A miss leaves the
    // index at -1 and retail reads placements[-1] regardless; no name at all keeps the
    // posture at -1.
    unsigned int Posture = 0xffffffff;
    char const* const PlacementName = Props.NameAnsi;
    if (PlacementName != nullptr)
    {
        int Found = -1;
        int Index = 0;
        for (;;)
        {
            // Re-read each pass.
            int const Count = (int)Data->CharacterPlacements.size();
            if (Index >= Count)
                break;
            char const* const Name = Data->CharacterPlacements.data()[Index]->Name;
            if (Name != nullptr && _stricmp(Name, PlacementName) == 0)
            {
                Found = Index;
                break;
            }
            ++Index;
        }
        CharPlaceInfo const* const Placement = Data->CharacterPlacements.data()[Found];
        char const* const FoundName = Placement->Name;
        Posture = (unsigned int)Placement->Posture;

        // A walk over the room's characters for one on the same placement that the filter
        // admits. Nothing reads the result.
        MetaNode<CharacterRecord>* Node = nullptr;
        while (Characters.Records.traverse(Node, true))
        {
            CharacterRecord const* const Other = Node->Data;
            if (Other->Props.NameAnsi == nullptr || FoundName == nullptr)
                continue;
            if (_stricmp(Other->Props.NameAnsi, FoundName) != 0)
                continue;
            if (RecordFilter == 0 || Other->Filter == 0 || Other->Filter == RecordFilter)
                break;
        }
    }

    Record->Props.CharType = 0;
    Record->Props.Posture = Posture;
    Record->Filter = RecordFilter;
    Record->bDirty = true;
    Characters.Records.append(Record);

    MetaNode<unsigned int>* Session = nullptr;
    while (Characters.Sessions->traverse(Session, true))
    {
        unsigned int const Sid = *Session->Data;
        if (BroadcastRecord::session_passes(Sid, Record->Filter))
            Record->send_update(Sid);
    }
    Record->bDirty = false;
    return Handle;
}

// FUN_06d122f0 (thiscall).
unsigned int FRoomEntry::create_behavior(ScriptBehavior const& Props, unsigned int RecordFilter)
{
    unsigned int const Handle = GBehaviorHandles.allocate_index();
    BehaviorRecord* Record = new BehaviorRecord();                 // new(0x26c)
    Record->Props = Props;
    Record->Props.BehaviorId = (int)Handle;
    Record->Filter = RecordFilter;
    Record->bDirty = true;
    Behaviors.Records.append(Record);

    MetaNode<unsigned int>* Session = nullptr;
    while (Behaviors.Sessions->traverse(Session, true))
    {
        unsigned int const Sid = *Session->Data;
        if (BroadcastRecord::session_passes(Sid, Record->Filter))
            Record->send_update(Sid);
    }
    Record->bDirty = false;
    return Handle;
}

// FUN_06d12520 (thiscall): FUN_06d122f0's twin on the ambient pool, a 0x20-byte
// AmbientScriptRecord and the list at room+0x40.
unsigned int FRoomEntry::create_ambient_script(AmbientScriptProperties const& Props, unsigned int RecordFilter)
{
    unsigned int const Handle = GAmbientScriptHandles.allocate_index();
    AmbientScriptRecord* Record = new AmbientScriptRecord();
    Record->Props = Props;
    Record->Props.Key = (int)Handle;
    Record->Filter = RecordFilter;
    Record->bDirty = true;
    AmbientScripts.Records.append(Record);

    MetaNode<unsigned int>* Session = nullptr;
    while (AmbientScripts.Sessions->traverse(Session, true))
    {
        unsigned int const Sid = *Session->Data;
        if (BroadcastRecord::session_passes(Sid, Record->Filter))
            Record->send_update(Sid);
    }
    Record->bDirty = false;
    return Handle;
}

// FUN_06d127d0 (thiscall): the name of placement `index` (read unchecked), then the
// first character on that placement whose filter admits `filter` -- its key, else 0.
unsigned int FRoomEntry::get_character_on_placement(unsigned int SessionFilter, int PlacementIndex) const
{
    char const* const PlacementName = Data->CharacterPlacements.data()[PlacementIndex]->Name;
    MetaNode<CharacterRecord>* Node = nullptr;
    while (Characters.Records.traverse(Node, true))
    {
        CharacterRecord const* const Record = Node->Data;
        if (Record->Props.NameAnsi == nullptr || PlacementName == nullptr)
            continue;
        if (_stricmp(Record->Props.NameAnsi, PlacementName) != 0)
            continue;
        if (SessionFilter == 0 || Record->Filter == 0 || Record->Filter == SessionFilter)
            return Record->Props.Key;
    }
    return 0;
}

// ============================================================================
// FBaseEntry
// ============================================================================

// FUN_06d12860.
FBaseEntry::FBaseEntry()
{
    init();
}

// FUN_06d12900.
FBaseEntry::~FBaseEntry()
{
    delete[] Rooms;
    MissionVendors.clear();
    NewsBroadcasts.clear();
    Sessions.free();
    init();
}

// FUN_06d12b10, with the same empty ICF-folded call FUN_06d11ec0 has.
void FBaseEntry::init()
{
    BaseId = 0;
    Data = nullptr;
    Rooms = nullptr;
    NumRooms = 0;
    MissionVendors.Sessions = &Sessions;
    NewsBroadcasts.Sessions = &Sessions;
}

// FUN_06d12b30 (thiscall). No test of the allocation.
void FBaseEntry::load(BaseData* Source)
{
    Data = Source;
    BaseId = Source->BaseId;
    NumRooms = (int)Source->Rooms.size();
    Rooms = new FRoomEntry[NumRooms];
    int Index = 0;
    for (std::list<RoomData*>::iterator It = Source->Rooms.begin(); It != Source->Rooms.end(); ++It, ++Index)
    {
        RoomData* const Room = *It;
        Rooms[Index].BaseId = Room->BaseId;
        Rooms[Index].RoomId = Room->RoomId;
        Rooms[Index].Data = Room;
    }
}

// FUN_06d12be0 (thiscall). No default start room FDUMPs and carries on. Room by room,
// the first behaviour record with a non-zero StartRoomLocationId overrides it. IClient
// +0x70.
void FBaseEntry::send_start_room(unsigned int SessionId)
{
    unsigned int const DefaultRoom = Data->StartRoomId;
    if (DefaultRoom == 0)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: No start room in base %u\n", GBaseDBFile, 0x454, Data->BaseId);
    }

    unsigned int StartRoom = DefaultRoom;
    for (int Index = 0; Index < NumRooms; ++Index)
    {
        MetaNode<BehaviorRecord>* Node = Rooms[Index].Behaviors.Records.get_head();
        while (Node != nullptr && Node->Data->Props.StartRoomLocationId == 0)
            Node = Node->Next;
        if (Node != nullptr)
        {
            StartRoom = Node->Data->Props.StartRoomLocationId;
            break;
        }
    }

    g_pClientInterface->SendServerSetStartRoom(SessionId, BaseId, StartRoom);
}

// FUN_06d13070 (thiscall): FUN_06d122f0's twin on the mission-vendor pool, a 0x6c-byte
// MissionComputerRecord and the list at base+0x14.
unsigned int FBaseEntry::create_mission_vendor(MissionVendorProperties const& Props, unsigned int RecordFilter)
{
    unsigned int const Handle = GMissionVendorHandles.allocate_index();
    MissionComputerRecord* Record = new MissionComputerRecord();
    Record->Props = Props;
    Record->Props.Key = (int)Handle;
    Record->Filter = RecordFilter;
    Record->bDirty = true;
    MissionVendors.Records.append(Record);

    MetaNode<unsigned int>* Session = nullptr;
    while (MissionVendors.Sessions->traverse(Session, true))
    {
        unsigned int const Sid = *Session->Data;
        if (BroadcastRecord::session_passes(Sid, Record->Filter))
            Record->send_update(Sid);
    }
    Record->bDirty = false;
    return Handle;
}

// FUN_06d13270 (thiscall): IClient +0xa4.
void FBaseEntry::mission_vendor_acceptance(unsigned long Handle, bool bAccepted, FmtStr const& Reason,
                                           unsigned int SessionId)
{
    MissionVendorAcceptance Acceptance;
    Acceptance.Key = (int)Handle;
    Acceptance.TargetId = BaseId;
    Acceptance.bAccepted = bAccepted;
    Acceptance.Reason = Reason;
    MarshalBuffer Buf;
    Buf.use_default();
    Acceptance.marshal(&Buf);
    g_pClientInterface->SendServerGFMissionVendorAcceptance(SessionId, Buf.get_buffer(), (unsigned int)Buf.get_num_written());
}

// FUN_06d13350: the news twin (0x6c-byte NewsBroadcastRecord, list at base+0x2c).
unsigned int FBaseEntry::create_news_broadcast(NewsBroadcastProperties const& Props, unsigned int RecordFilter)
{
    unsigned int const Handle = GNewsHandles.allocate_index();
    NewsBroadcastRecord* Record = new NewsBroadcastRecord();
    Record->Props = Props;
    Record->Props.Key = (int)Handle;
    Record->Filter = RecordFilter;
    Record->bDirty = true;
    NewsBroadcasts.Records.append(Record);

    MetaNode<unsigned int>* Session = nullptr;
    while (NewsBroadcasts.Sessions->traverse(Session, true))
    {
        unsigned int const Sid = *Session->Data;
        if (BroadcastRecord::session_passes(Sid, Record->Filter))
            Record->send_update(Sid);
    }
    Record->bDirty = false;
    return Handle;
}

// ============================================================================
// BaseDB
// ============================================================================

namespace
{
    // DAT_06d8da70 (__xc initializer FUN_06d15620, atexit thunk FUN_06d15640).
    BaseDB GBaseDB;
}

// FUN_06d15650.
BaseDB* GetBaseDB()
{
    return &GBaseDB;
}

// FUN_06d18840.
void Server_SubsystemInit()
{
    GetBaseDB()->init();
}

// FUN_06d18850.
void Server_SubsystemShutdown()
{
    GetBaseDB()->shutdown();
}

// FUN_06d18860.
void Server_SubsystemUpdate(float DeltaTime)
{
    GetBaseDB()->update(DeltaTime);
}

// FUN_06d13550.
BaseDB::BaseDB()
    : Bases(nullptr)
    , NumBases(0)
{
}

// FUN_06d13960.
BaseDB::~BaseDB()
{
    BaseMap.clear();
    LocationMap.clear();
    CharacterMap.clear();
    BehaviorMap.clear();
    MissionVendorMap.clear();
    NewsMap.clear();
    AmbientScriptMap.clear();
    delete[] Bases;
    Bases = nullptr;
    NumBases = 0;
}

// FUN_06d13cc0. No latch, no null test of the list, no clearing beforehand.
void BaseDB::init()
{
    std::list<BaseData*>* const List = BaseDataList_get()->get_base_data_list();
    NumBases = (int)List->size();
    Bases = new FBaseEntry[NumBases];
    int Index = 0;
    for (std::list<BaseData*>::iterator It = List->begin(); It != List->end(); ++It, ++Index)
        Bases[Index].load(*It);

    for (int BaseIndex = 0; BaseIndex < NumBases; ++BaseIndex)
    {
        FBaseEntry* const Base = &Bases[BaseIndex];
        BaseMap.insert(std::map<unsigned int, FBaseEntry*>::value_type(Base->BaseId, Base));
        for (int RoomIndex = 0; RoomIndex < Base->NumRooms; ++RoomIndex)
        {
            FRoomEntry* const Room = &Base->Rooms[RoomIndex];
            LocationMap.insert(std::map<unsigned int, FRoomEntry*>::value_type(Room->RoomId, Room));
        }
    }
}

// FUN_06d13bf0.
void BaseDB::shutdown()
{
    BaseMap.clear();
    LocationMap.clear();
    CharacterMap.clear();
    BehaviorMap.clear();
    MissionVendorMap.clear();
    NewsMap.clear();
    AmbientScriptMap.clear();
    delete[] Bases;
    Bases = nullptr;
    NumBases = 0;
}

// FUN_06d13ef0 (thiscall). The float is never read. For every base with a subscriber,
// every room with a subscriber re-broadcasts its dirty records (FUN_06d17120); then, when
// the base still has a subscriber (re-tested), its dirty mission-vendor and news records.
void BaseDB::update(float DeltaTime)
{
    (void)DeltaTime;
    for (int BaseIndex = 0; BaseIndex < NumBases; ++BaseIndex)
    {
        FBaseEntry* const Base = &Bases[BaseIndex];
        if (Base->Sessions.empty())
            continue;

        for (int RoomIndex = 0; RoomIndex < Base->NumRooms; ++RoomIndex)
        {
            FRoomEntry* const Room = &Base->Rooms[RoomIndex];
            if (Room->Sessions.empty())
                continue;

            MetaNode<CharacterRecord>* Character = nullptr;
            while (Room->Characters.Records.traverse(Character, true))
            {
                CharacterRecord* const Record = Character->Data;
                if (!Record->bDirty)
                    continue;
                Room->Characters.broadcast(Record);
                Record->bDirty = false;
            }

            MetaNode<BehaviorRecord>* Behavior = nullptr;
            while (Room->Behaviors.Records.traverse(Behavior, true))
            {
                BehaviorRecord* const Record = Behavior->Data;
                if (Record->bDirty)
                {
                    Room->Behaviors.broadcast(Record);
                    Record->bDirty = false;
                }
            }

            MetaNode<AmbientScriptRecord>* Ambient = nullptr;
            while (Room->AmbientScripts.Records.traverse(Ambient, true))
            {
                AmbientScriptRecord* const Record = Ambient->Data;
                if (Record->bDirty)
                {
                    Room->AmbientScripts.broadcast(Record);
                    Record->bDirty = false;
                }
            }
        }

        if (Base->Sessions.empty())
            continue;

        MetaNode<MissionComputerRecord>* Offer = nullptr;
        while (Base->MissionVendors.Records.traverse(Offer, true))
        {
            MissionComputerRecord* const Record = Offer->Data;
            if (!Record->bDirty)
                continue;
            Base->MissionVendors.broadcast(Record);
            Record->bDirty = false;
        }

        MetaNode<NewsBroadcastRecord>* News = nullptr;
        while (Base->NewsBroadcasts.Records.traverse(News, true))
        {
            NewsBroadcastRecord* const Record = News->Data;
            if (!Record->bDirty)
                continue;
            Base->NewsBroadcasts.broadcast(Record);
            Record->bDirty = false;
        }
    }
}

// FUN_06d140f0.
FBaseEntry* BaseDB::find_base(unsigned int Id)
{
    std::map<unsigned int, FBaseEntry*>::iterator It = BaseMap.find(Id);
    if (It != BaseMap.end())
        return It->second;
    FDUMP(kFdumpError, "%s(%d) : *** ERROR: Base %u is invalid\n", GBaseDBFile, 0x659, Id);
    return nullptr;
}

// FUN_06d14170.
FRoomEntry* BaseDB::find_location(unsigned int Id)
{
    std::map<unsigned int, FRoomEntry*>::iterator It = LocationMap.find(Id);
    if (It != LocationMap.end())
        return It->second;
    FDUMP(kFdumpError, "%s(%d) : *** ERROR: Location %u is invalid\n", GBaseDBFile, 0x66d, Id);
    return nullptr;
}

// FUN_06d14230 (from IServerImpl::BaseEnter): RoomData::load on every room. SessionId is
// pushed but never read.
void BaseDB::load_base(unsigned int SessionId, unsigned int Id)
{
    (void)SessionId;
    std::map<unsigned int, FBaseEntry*>::iterator It = BaseMap.find(Id);
    if (It == BaseMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Base %u is invalid\n", GBaseDBFile, 0x659, Id);
        return;
    }
    FBaseEntry* const Base = It->second;
    if (Base == nullptr)
        return;
    for (int Index = 0; Index < Base->NumRooms; ++Index)
    {
        // Retail tests the element address.
        FRoomEntry* const Room = &Base->Rooms[Index];
        if (Room != nullptr)
            Room->Data->load();
    }
}

// FUN_06d142c0: FUN_06d14230 with RoomData::unload. SessionId unused.
void BaseDB::unload_base(unsigned int SessionId, unsigned int Id)
{
    (void)SessionId;
    std::map<unsigned int, FBaseEntry*>::iterator It = BaseMap.find(Id);
    if (It == BaseMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Base %u is invalid\n", GBaseDBFile, 0x659, Id);
        return;
    }
    FBaseEntry* const Base = It->second;
    if (Base == nullptr)
        return;
    for (int Index = 0; Index < Base->NumRooms; ++Index)
    {
        // Retail tests the element address.
        FRoomEntry* const Room = &Base->Rooms[Index];
        if (Room != nullptr)
            Room->Data->unload();
    }
}

// FUN_06d14350.
void BaseDB::enter_location(unsigned int SessionId, unsigned int Id)
{
    std::map<unsigned int, FRoomEntry*>::iterator It = LocationMap.find(Id);
    if (It == LocationMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Location %u is invalid\n", GBaseDBFile, 0x66d, Id);
        return;
    }
    if (It->second != nullptr)
        It->second->enter(SessionId);
}

// FUN_06d143e0: removes the session's own character record from the room, then clears
// pool bit (handle - 1) whether or not the record was found. CharacterMap is not touched.
void BaseDB::exit_location(unsigned int SessionId, unsigned int Id)
{
    std::map<unsigned int, FRoomEntry*>::iterator It = LocationMap.find(Id);
    if (It == LocationMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Location %u is invalid\n", GBaseDBFile, 0x66d, Id);
        return;
    }
    FRoomEntry* const Room = It->second;
    if (Room == nullptr)
        return;

    unsigned int Handle = 0;
    MetaNode<CharacterRecord>* Node = nullptr;
    while (Room->Characters.Records.traverse(Node, true))
    {
        if (Node->Data->Props.SessionId == SessionId)
        {
            Handle = Node->Data->Props.Key;
            break;
        }
    }
    if ((Handle & 0x3fffffff) == 0)
        return;

    Node = nullptr;
    while (Room->Characters.Records.traverse(Node, true))
    {
        CharacterRecord* const Record = Node->Data;
        if (!Record->matches(Handle))
            continue;
        MetaNode<unsigned int>* Session = nullptr;
        while (Room->Characters.Sessions->traverse(Session, true))
        {
            unsigned int const Sid = *Session->Data;
            if (BroadcastRecord::session_passes(Sid, Record->Filter))
                Record->send_destroy(Sid);
        }
        delete Record;
        Room->Characters.Records.remove(Node);
        break;
    }
    GCharacterHandles.set(Handle - 1, false);
}

// FUN_06d14570: !bFull unsubscribes; a new subscriber gets the start room, the offers
// (IClient +0xa0 ends the list) and the news (IClient +0xb4).
void BaseDB::base_info(unsigned int SessionId, unsigned int Id, bool bFull)
{
    std::map<unsigned int, FBaseEntry*>::iterator It = BaseMap.find(Id);
    if (It == BaseMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Base %u is invalid\n", GBaseDBFile, 0x659, Id);
        return;
    }
    FBaseEntry* const Base = It->second;
    if (Base == nullptr)
        return;
    if (!bFull)
    {
        Base->Sessions.remove(SessionId);
        return;
    }
    if (Base->Sessions.contains(SessionId))
        return;
    Base->Sessions.add(SessionId);
    Base->send_start_room(SessionId);

    unsigned int const BaseId = Base->BaseId;
    MetaNode<MissionComputerRecord>* Offer = nullptr;
    while (Base->MissionVendors.Records.traverse(Offer, true))
    {
        if (BroadcastRecord::session_passes(SessionId, Offer->Data->Filter))
            Offer->Data->send_update(SessionId);
    }
    g_pClientInterface->SendServerGFCompleteMissionComputerList(SessionId, BaseId);

    MetaNode<NewsBroadcastRecord>* News = nullptr;
    while (Base->NewsBroadcasts.Records.traverse(News, true))
    {
        if (BroadcastRecord::session_passes(SessionId, News->Data->Filter))
            News->Data->send_update(SessionId);
    }
    g_pClientInterface->SendServerGFCompleteNewsBroadcastList(SessionId, BaseId);
}

// FUN_06d146e0: the room twin of base_info -- characters (IClient +0x7c), behaviours
// (+0x88), ambient scripts (FUN_06d167a0, +0x94).
void BaseDB::location_info(unsigned int SessionId, unsigned int Id, bool bFull)
{
    std::map<unsigned int, FRoomEntry*>::iterator It = LocationMap.find(Id);
    if (It == LocationMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Location %u is invalid\n", GBaseDBFile, 0x66d, Id);
        return;
    }
    FRoomEntry* const Room = It->second;
    if (Room == nullptr)
        return;
    if (!bFull)
    {
        Room->Sessions.remove(SessionId);
        return;
    }
    if (Room->Sessions.contains(SessionId))
        return;
    Room->Sessions.add(SessionId);

    MetaNode<CharacterRecord>* Character = nullptr;
    while (Room->Characters.Records.traverse(Character, true))
    {
        if (BroadcastRecord::session_passes(SessionId, Character->Data->Filter))
            Character->Data->send_update(SessionId);
    }
    g_pClientInterface->SendServerGFCompleteCharList(SessionId, Room->RoomId);

    MetaNode<BehaviorRecord>* Behavior = nullptr;
    while (Room->Behaviors.Records.traverse(Behavior, true))
    {
        if (BroadcastRecord::session_passes(SessionId, Behavior->Data->Filter))
            Behavior->Data->send_update(SessionId);
    }
    g_pClientInterface->SendServerGFCompleteScriptBehaviorList(SessionId, Room->RoomId);

    Room->AmbientScripts.send_all(SessionId, Room->RoomId);
}

// FUN_06d14870: a missing room FDUMPs and goes on with a null room, which retail then
// calls through.
int BaseDB::create_character(CharacterProperties const& Props, unsigned int RecordFilter)
{
    FRoomEntry* Room = nullptr;
    std::map<unsigned int, FRoomEntry*>::iterator It = LocationMap.find(Props.RoomId);
    if (It != LocationMap.end())
        Room = It->second;
    else
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Location %u is invalid\n", GBaseDBFile, 0x66d, Props.RoomId);
    int const Handle = Room->create_character(Props, RecordFilter);
    CharacterMap.insert(std::map<unsigned int, FRoomEntry*>::value_type((unsigned int)Handle, Room));
    return Handle;
}

// FUN_06d149c0: a miss FDUMPs and goes on; then the map is searched again for the erase
// (a second FDUMP on a miss).
void BaseDB::destroy_character(unsigned int Handle)
{
    std::map<unsigned int, FRoomEntry*>::iterator It = CharacterMap.find(Handle);
    if (It == CharacterMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find npc character %u\n", GBaseDBFile, 0x720, Handle);
    }
    else if (It->second != nullptr)
    {
        It->second->Characters.remove(Handle);
        GCharacterHandles.set(Handle - 1, false);
    }
    It = CharacterMap.find(Handle);
    if (It == CharacterMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find npc character %u\n", GBaseDBFile, 0x739, Handle);
        return;
    }
    CharacterMap.erase(It);
}

// FUN_06d14ac0: the record lookup may yield null; retail writes through it regardless.
void BaseDB::set_character_behavior(unsigned int Handle, unsigned long Behavior)
{
    std::map<unsigned int, FRoomEntry*>::iterator It = CharacterMap.find(Handle);
    if (It == CharacterMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find npc character %u\n", GBaseDBFile, 0x720, Handle);
        return;
    }
    FRoomEntry* const Room = It->second;
    if (Room == nullptr)
        return;
    CharacterRecord* Record = nullptr;
    MetaNode<CharacterRecord>* Node = nullptr;
    while (Room->Characters.Records.traverse(Node, true))
    {
        if (Node->Data->matches(Handle))
        {
            Record = Node->Data;
            break;
        }
    }
    Record->Props.BehaviorId = (unsigned int)Behavior;
    Record->bDirty = true;
}

// FUN_06d14b70: FUN_06d14870 for behaviours.
unsigned int BaseDB::create_behavior(ScriptBehavior const& Props, unsigned int RecordFilter)
{
    FRoomEntry* Room = nullptr;
    std::map<unsigned int, FRoomEntry*>::iterator It = LocationMap.find(Props.LocationId);
    if (It != LocationMap.end())
        Room = It->second;
    else
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Location %u is invalid\n", GBaseDBFile, 0x66d, Props.LocationId);
    unsigned int const Handle = Room->create_behavior(Props, RecordFilter);
    BehaviorMap.insert(std::map<unsigned int, FRoomEntry*>::value_type(Handle, Room));
    return Handle;
}

// FUN_06d14cc0: FUN_06d149c0 for behaviours; the pool bit is the handle itself.
void BaseDB::destroy_behavior(unsigned long Handle)
{
    unsigned int const Key = (unsigned int)Handle;
    std::map<unsigned int, FRoomEntry*>::iterator It = BehaviorMap.find(Key);
    if (It == BehaviorMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find behavior %u\n", GBaseDBFile, 0x774, Key);
    }
    else if (It->second != nullptr)
    {
        It->second->Behaviors.remove(Key);
        GBehaviorHandles.set(Key, false);
    }
    It = BehaviorMap.find(Key);
    if (It == BehaviorMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find behavior %u\n", GBaseDBFile, 0x78d, Key);
        return;
    }
    BehaviorMap.erase(It);
}

// FUN_06d14dc0: a missing base is passed on as null.
unsigned int BaseDB::create_mission_vendor(MissionVendorProperties const& Props, unsigned int RecordFilter)
{
    FBaseEntry* Base = nullptr;
    std::map<unsigned int, FBaseEntry*>::iterator It = BaseMap.find(Props.BaseId);
    if (It != BaseMap.end())
        Base = It->second;
    else
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Base %u is invalid\n", GBaseDBFile, 0x659, Props.BaseId);
    unsigned int const Handle = Base->create_mission_vendor(Props, RecordFilter);
    MissionVendorMap.insert(std::map<unsigned int, FBaseEntry*>::value_type(Handle, Base));
    return Handle;
}

// FUN_06d14f10: as destroy_behavior.
void BaseDB::destroy_mission_vendor(unsigned long Handle)
{
    unsigned int const Key = (unsigned int)Handle;
    std::map<unsigned int, FBaseEntry*>::iterator It = MissionVendorMap.find(Key);
    if (It == MissionVendorMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find mission vendor offer %u\n", GBaseDBFile, 0x7b2, Key);
    }
    else if (It->second != nullptr)
    {
        It->second->MissionVendors.remove(Key);
        GMissionVendorHandles.set(Key, false);
    }
    It = MissionVendorMap.find(Key);
    if (It == MissionVendorMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find mission vendor offer %u\n", GBaseDBFile, 0x7cb, Key);
        return;
    }
    MissionVendorMap.erase(It);
}

// FUN_06d15010: a missing base FDUMPs and goes on with a null base, whose list retail
// then reads.
int BaseDB::count_mission_vendors(unsigned int Id, unsigned int SessionFilter)
{
    FBaseEntry* Base = nullptr;
    std::map<unsigned int, FBaseEntry*>::iterator It = BaseMap.find(Id);
    if (It != BaseMap.end())
        Base = It->second;
    else
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Base %u is invalid\n", GBaseDBFile, 0x659, Id);
    int Count = 0;
    MetaNode<MissionComputerRecord>* Node = nullptr;
    while (Base->MissionVendors.Records.traverse(Node, true))
    {
        unsigned int const RecordFilter = Node->Data->Filter;
        if (SessionFilter == 0 || RecordFilter == 0 || RecordFilter == SessionFilter)
            ++Count;
    }
    return Count;
}

// FUN_06d150a0.
void BaseDB::mission_vendor_acceptance(unsigned long Handle, bool bAccepted, FmtStr const& Reason,
                                       unsigned int SessionId)
{
    std::map<unsigned int, FBaseEntry*>::iterator It = MissionVendorMap.find((unsigned int)Handle);
    if (It == MissionVendorMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find mission vendor offer %u\n", GBaseDBFile, 0x7b2,
              (unsigned int)Handle);
        return;
    }
    if (It->second != nullptr)
        It->second->mission_vendor_acceptance(Handle, bAccepted, Reason, SessionId);
}

// FUN_06d15140: as create_mission_vendor.
unsigned int BaseDB::create_news_broadcast(NewsBroadcastProperties const& Props, unsigned int RecordFilter)
{
    FBaseEntry* Base = nullptr;
    std::map<unsigned int, FBaseEntry*>::iterator It = BaseMap.find(Props.BaseId);
    if (It != BaseMap.end())
        Base = It->second;
    else
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Base %u is invalid\n", GBaseDBFile, 0x659, Props.BaseId);
    unsigned int const Handle = Base->create_news_broadcast(Props, RecordFilter);
    NewsMap.insert(std::map<unsigned int, FBaseEntry*>::value_type(Handle, Base));
    return Handle;
}

// FUN_06d15290: as destroy_behavior.
void BaseDB::destroy_news_broadcast(unsigned long Handle)
{
    unsigned int const Key = (unsigned int)Handle;
    std::map<unsigned int, FBaseEntry*>::iterator It = NewsMap.find(Key);
    if (It == NewsMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find news broadcast %u\n", GBaseDBFile, 0x809, Key);
    }
    else if (It->second != nullptr)
    {
        It->second->NewsBroadcasts.remove(Key);
        GNewsHandles.set(Key, false);
    }
    It = NewsMap.find(Key);
    if (It == NewsMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find news broadcast %u\n", GBaseDBFile, 0x822, Key);
        return;
    }
    NewsMap.erase(It);
}

// FUN_06d15390: unlike its siblings, a missing or null room returns 0xffffffff.
unsigned int BaseDB::create_ambient_script(AmbientScriptProperties const& Props, unsigned int RecordFilter)
{
    std::map<unsigned int, FRoomEntry*>::iterator It = LocationMap.find(Props.RoomId);
    if (It == LocationMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Location %u is invalid\n", GBaseDBFile, 0x66d, Props.RoomId);
        return 0xffffffff;
    }
    FRoomEntry* const Room = It->second;
    if (Room == nullptr)
        return 0xffffffff;
    unsigned int const Handle = Room->create_ambient_script(Props, RecordFilter);
    AmbientScriptMap.insert(std::map<unsigned int, FRoomEntry*>::value_type(Handle, Room));
    return Handle;
}

// FUN_06d15510: as destroy_behavior.
void BaseDB::destroy_ambient_script(unsigned long Handle)
{
    unsigned int const Key = (unsigned int)Handle;
    std::map<unsigned int, FRoomEntry*>::iterator It = AmbientScriptMap.find(Key);
    if (It == AmbientScriptMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find ambient script %u\n", GBaseDBFile, 0x84a, Key);
    }
    else if (It->second != nullptr)
    {
        It->second->AmbientScripts.remove(Key);
        GAmbientScriptHandles.set(Key, false);
    }
    It = AmbientScriptMap.find(Key);
    if (It == AmbientScriptMap.end())
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: Could not find ambient script %u\n", GBaseDBFile, 0x863, Key);
        return;
    }
    AmbientScriptMap.erase(It);
}
