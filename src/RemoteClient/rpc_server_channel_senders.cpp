// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "ClientImpl.h"
#include "rpc_packet_codec.h"
#include "rpc_server_channel_codec.h"
#include "rpc_iclient_args.h"        // kPlayerListKind* (0x52 PLAYERLIST)
#include "common_goods.h"            // BaseHint (SendServerLoadHint), EquipDescVector
#include "server_characterset.h" // CCharacterSet (SendServerCharacterInfo)
#include "server_createinfo.h"   // CShipCreateInfo (SendServerCreateShip)
#include "server_sysdb.h"        // FMineExplosion (SendServerExplodeAsteroidMine)
#include "common_reputation.h"   // Reputation::Vibe (SendServerCreateShip)
#include "common_text.h"         // FmtStr (SendServerCreateShip)
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <list>
#include <new>

void CClientImpl::SendServerLoginResponse(unsigned int ClientId, const unsigned char& Status) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 2, &Packet) == 0)
    {
        static_cast<FRpcLoginResponsePacket*>(Packet)->Value = Status;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendServerSetShipArch(unsigned int ClientId, unsigned int ShipArchId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x23, &Packet) == 0)
    {
        static_cast<FRpcSetShipArchPacket*>(Packet)->Value = ShipArchId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// ===== SERVER 0x01..0x1f =====
// Every sender: Serializer->Allocate(2, op, &Packet) (06b3b480); on success fill the payload
// and PushToClient. An allocation failure sends nothing. The client id is never in the payload.
// "RC xxxx, IClient +0xNN" headers give the RemoteClient.dll body and its IClient vtable slot.

// CHARACTERINFO id block from an inline Costume (RC 06b355f8, used for record +0x274 <- src+0x274
// and +0x278 <- src+0x2a8). The new-expression test only skips the zero stores (a null block is
// written through, reproduced on purpose); part ids in the order [1],[0],[2],[3]; the accessory
// copy is not bounded by the eight slots.
static FRpcServerCharacterIdBlock* RpcServerCharacterIdBlockFromCostume(const Costume& Source) {
    FRpcServerCharacterIdBlock* Block = static_cast<FRpcServerCharacterIdBlock*>(
        ::operator new(sizeof(FRpcServerCharacterIdBlock), std::nothrow));
    if (Block != nullptr) {
        Block->Ids = nullptr;
        Block->Count = 0;
        Block->RightHand = 0;
        Block->LeftHand = 0;
        Block->Body = 0;
        Block->Head = 0;
    }
    Block->Body = Source.PartIds[1];
    Block->Head = Source.PartIds[0];
    Block->LeftHand = Source.PartIds[2];
    Block->RightHand = Source.PartIds[3];
    Block->Count = static_cast<int>(Source.AccessoryCount);
    if (static_cast<unsigned int>(Block->Count) != 0) {
        Block->Ids = static_cast<unsigned int*>(
            ::operator new(static_cast<unsigned int>(Block->Count) << 2, std::nothrow));
        const unsigned int* SourceIds = &Source.AccessoryIds[0];
        for (unsigned int Index = 0; Index < static_cast<unsigned int>(Block->Count); ++Index) {
            Block->Ids[Index] = SourceIds[Index];
        }
    }
    return Block;
}

// One wire record from one CCharacterSet::CharacterInfo (RC 06b353cf). Retail builds it twice:
// hop 1 here into a stack list, hop 2 (06b42690, = rpclocal 06d955c0) deep-copies every non-null
// value into the packet and derives +0x260 from the equipment list's size. Every hop
// re-allocates, so one hop plus that postscript yields the same record.
static void RpcServerCharacterRecordFromInfo(FRpcServerCharacterRecord* Record,
    const CCharacterSet::CharacterInfo* Source) {
    Record->Name = Source->Id;
    Record->bIsRestart = Source->bIsRestart;                     // raw byte
    Record->bIsAutoSave = Source->bIsAutoSave;                    // raw byte
    Record->TextA = Source->Name;
    Record->TextB = Source->Description;
    Record->DescripStrId = Source->DescripStrId;
    Record->TimestampLow = Source->TimestampLow;
    Record->TimestampHigh = Source->TimestampHigh;
    Record->ShipArchetype = Source->ShipArchetype;
    Record->Money = static_cast<unsigned int>(Source->Money);
    Record->SystemId = Source->SystemId;                       // <- +0x260
    Record->VoiceId = Source->VoiceId;                        // <- +0x26c
    Record->Rank = Source->Rank;                           // <- +0x270
    Record->BaseId = Source->BaseId;                         // <- +0x264
    Record->LocationId = Source->LocationId;                     // <- +0x268
    std::memcpy(&Record->HullStatusBits, &Source->HullStatus, 4u);   // integer move
    Record->NumKills = static_cast<unsigned int>(Source->NumKills);             // <- +0x238
    Record->NumMissionFailures = static_cast<unsigned int>(Source->NumMissionFailures);   // <- +0x240
    Record->NumMissionSuccesses = static_cast<unsigned int>(Source->NumMissionSuccesses);  // <- +0x23c
    Record->BlockA = RpcServerCharacterIdBlockFromCostume(Source->ComCostume);
    Record->BlockB = RpcServerCharacterIdBlockFromCostume(Source->BaseCostume);
    // One 0x18 record per EquipDesc, no filter.
    for (std::list<EquipDesc>::const_iterator It = Source->Equipment.Items.begin();
         It != Source->Equipment.Items.end(); ++It) {
        // The new-expression test only skips the zero stores; a null record is written
        // through, reproduced on purpose.
        FRpcServerEquipRecord* E = static_cast<FRpcServerEquipRecord*>(
            ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
        if (E != nullptr) {
            E->ArchId = 0;
            E->Count = 0;
            E->Health = 0.0f;
            E->Id = 0;
            E->HardpointSize = 0;
            E->Hardpoint = nullptr;
            E->bEquipped = 0;
            E->bMission = 0;
        }
        E->ArchId = It->ArchId;
        E->Count = It->Count;
        RpcServerX87CopyFloat(&E->Health, &It->Health);           // FLD/FSTP
        E->Id = It->Id;
        E->bEquipped = It->bEquipped;                             // raw byte
        // EquipDesc +0x18 (bMission) copied as a raw byte.
        std::memcpy(&E->bMission, &It->bMission, 1u);
        // Bare new(strlen + 1); the size is stored truncated to a word.
        const unsigned int Length = static_cast<unsigned int>(std::strlen(It->Hardpoint)) + 1u;
        E->Hardpoint = static_cast<char*>(::operator new(Length, std::nothrow));
        const char* Text = It->Hardpoint;
        char* Out = E->Hardpoint;
        char Value;
        do {
            Value = *Text++;
            *Out++ = Value;
        } while (Value != '\0');
        E->HardpointSize = static_cast<unsigned short>(Length);
        Record->Equipment.PushBack(E);
    }
    // Hop 2's postscript: +0x260 = low byte of the equipment list size, or 0.
    Record->EquipmentCount = (Record->Equipment.Records != nullptr)
        ? static_cast<unsigned char>(Record->Equipment.Records->size() & 0xffu) : 0u;
    // From src+0x254.
    Record->CollisionGroups = Source->CollisionGroups;
}

// RC 06b35350, IClient +0x34: CHARACTERINFO (= rpclocal 06d89890). Walks the NULL-terminated
// MetaList; every record is converted and appended (hops 1 and 2 folded). A null entry appends
// the PREVIOUS entry's copy again, so hop 2 ships that record twice; the port converts the
// previous source again (same bytes). Before the first record that copy is an uninitialised
// stack slot; the port reads it as null, which hop 2 skips: a nondeterministic retail read made
// deterministic. Then +0x20 = SaveFailureCount.
void CClientImpl::SendServerCharacterInfo(unsigned int ClientId, const CCharacterSet& Characters) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 3, &Packet) != 0) return;
    FRpcServerCharacterInfoPacket* Typed = static_cast<FRpcServerCharacterInfoPacket*>(Packet);
    const CCharacterSet* Source = &Characters;
    const CCharacterSet::CharacterInfo* Previous = nullptr;
    for (const MetaNode<CCharacterSet::CharacterInfo>* Node = Source->Characters.get_head();
         Node != nullptr; Node = Node->Next) {
        const CCharacterSet::CharacterInfo* Info = Node->Data;
        if (Info == nullptr) {
            Info = Previous;
            if (Info == nullptr) continue;
        }
        // new(0x27c); the null test only skips the ctor, the conversion runs on the result
        // either way: reproduced on purpose.
        FRpcServerCharacterRecord* Record = new (std::nothrow) FRpcServerCharacterRecord();
        RpcServerCharacterRecordFromInfo(Record, Info);
        Typed->Records.push_back(Record);
        Previous = Info;
    }
    Typed->SaveFailureCount = Source->SaveFailureCount;
    PushToClient(ClientId, Packet->AsRaw());
}

// RC 06b34660, IClient +0xfc: DAMAGEOBJECT (= rpclocal 06d88c10). +0x20 = ObjectId; the
// DamageList assignment is 06b3e690.
void CClientImpl::SendServerDamageObject(unsigned int ClientId, unsigned int ObjectId, const DamageList& Damage) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 5, &Packet) == 0) {
        FRpcServerDamageObjectPacket* Typed = static_cast<FRpcServerDamageObjectPacket*>(Packet);
        Typed->ObjectId = ObjectId;
        Typed->Damage = Damage;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35af0, IClient +0x38: CHARSELECTVERIFIED. The double is copied as two integer dwords,
// not through the FPU.
void CClientImpl::SendServerCharSelectVerified(unsigned int ClientId, const FServerCharSelectVerifiedInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 8, &Packet) == 0) {
        FRpcServerCharSelectVerifiedPacket* Typed =
            static_cast<FRpcServerCharSelectVerifiedPacket*>(Packet);
        Typed->RepInstance = static_cast<unsigned int>(Info.VibeHandle);
        std::memcpy(&Typed->Time, &Info.ServerTime, sizeof(Typed->Time));
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34120, IClient +0x70: SETSTARTROOM.
void CClientImpl::SendServerSetStartRoom(unsigned int ClientId, unsigned int BaseId, unsigned int RoomId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0xd, &Packet) == 0) {
        FRpcServerSetStartRoomPacket* Typed = static_cast<FRpcServerSetStartRoomPacket*>(Packet);
        Typed->Field_0x00 = BaseId;
        Typed->Field_0x04 = RoomId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34c00, IClient +0xdc: DESTROYOBJECT from {dword, byte}.
void CClientImpl::SendServerDestroyObject(unsigned int ClientId, const FServerDestroyObjectInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 6, &Packet) == 0) {
        FRpcServerDestroyObjectPacket* Typed = static_cast<FRpcServerDestroyObjectPacket*>(Packet);
        Typed->ObjectId = Info.ObjectId;
        Typed->DestroyType = Info.DestroyType;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35230, IClient +0xe0: ACTIVATEOBJECT; native store order +0x20, +0x28, then the raw
// byte +0x24.
void CClientImpl::SendServerActivateObject(unsigned int ClientId, const FPayloadServerActivateObject& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0xa, &Packet) == 0) {
        FRpcServerActivateObjectPacket* Typed = static_cast<FRpcServerActivateObjectPacket*>(Packet);
        Typed->ObjectId = Info.ObjectId;
        Typed->ActivateParam = Info.ActivateParam;
        Typed->bActivate = Info.bActivate;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36450, IClient +0xec: LAND; native store order [0], [8], [4].
void CClientImpl::SendServerLand(unsigned int ClientId, const FServerLandInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0xb, &Packet) == 0) {
        FRpcServerLandPacket* Typed = static_cast<FRpcServerLandPacket*>(Packet);
        Typed->Field_0x00 = Info.ShipId;
        Typed->Field_0x08 = Info.BaseId;
        Typed->Field_0x04 = Info.DockId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35b40, IClient +0xf0: LAUNCH. Argument struct {3 dwords, Orientation +0x0c,
// Position +0x1c}; both blocks move as integer dwords, Position first.
void CClientImpl::SendServerLaunch(unsigned int ClientId, const FServerLaunchInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 7, &Packet) == 0) {
        FRpcServerLaunchPacket* Typed = static_cast<FRpcServerLaunchPacket*>(Packet);
        Typed->ShipId = Info.ShipId;
        Typed->LaunchObjId = Info.LaunchObjId;
        Typed->LaunchPosIndex = Info.LaunchPosIndex;
        std::memcpy(&Typed->Position, &Info.Position, sizeof(Typed->Position));
        std::memcpy(&Typed->Orientation, &Info.Orientation, sizeof(Typed->Orientation));
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// GF list senders 0x0e..0x1f.

// RC 06b34170, IClient +0x74: GFDESTROYCHARACTER 0x15; +0x20 = RoomId, +0x24 = Key.
void CClientImpl::SendServerGFDestroyCharacter(unsigned int ClientId, unsigned int RoomId, unsigned int Key) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x15, &Packet) == 0) {
        FRpcServerGfDestroyCharacterPacket* Typed =
            static_cast<FRpcServerGfDestroyCharacterPacket*>(Packet);
        Typed->Field_0x00 = RoomId;
        Typed->Field_0x04 = Key;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34210, IClient +0x7c: GFCOMPLETECHARLIST 0x0f; +0x20 = RoomId.
void CClientImpl::SendServerGFCompleteCharList(unsigned int ClientId, unsigned int RoomId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0xf, &Packet) == 0) {
        static_cast<FRpcServerGfCompleteCharListPacket*>(Packet)->Value = RoomId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34250, IClient +0x80: GFSCRIPTBEHAVIOR 0x1a; set helper 06b3d130 (new(Size) + copy).
void CClientImpl::SendServerGFScriptBehavior(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x1a, &Packet) == 0) {
        static_cast<FRpcServerGfScriptBehaviorPacket*>(Packet)->SetData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b342a0, IClient +0x84: GFDESTROYSCRIPTBEHAVIOR 0x17; +0x20 = RoomId, +0x24 = BehaviorId.
void CClientImpl::SendServerGFDestroyScriptBehavior(unsigned int ClientId, unsigned int RoomId, unsigned int BehaviorId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x17, &Packet) == 0) {
        FRpcServerGfDestroyScriptBehaviorPacket* Typed =
            static_cast<FRpcServerGfDestroyScriptBehaviorPacket*>(Packet);
        Typed->Field_0x00 = RoomId;
        Typed->Field_0x04 = BehaviorId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b342f0, IClient +0x88: GFCOMPLETESCRIPTBEHAVIORLIST 0x11; +0x20 = RoomId.
void CClientImpl::SendServerGFCompleteScriptBehaviorList(unsigned int ClientId, unsigned int RoomId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x11, &Packet) == 0) {
        static_cast<FRpcServerGfCompleteScriptBehaviorListPacket*>(Packet)->Value = RoomId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34330, IClient +0x8c: GFUPDATEAMBIENTSCRIPT 0x1f; set helper 06b3d490 (Data +0x20,
// Size +0x24).
void CClientImpl::SendServerGFUpdateAmbientScript(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x1f, &Packet) == 0) {
        static_cast<FRpcServerGfUpdateAmbientScriptPacket*>(Packet)->SetData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34380, IClient +0x90: GFDESTROYAMBIENTSCRIPT 0x19; native store order +0x24 = Key,
// then +0x20 = RoomId.
void CClientImpl::SendServerGFDestroyAmbientScript(unsigned int ClientId, unsigned int RoomId, unsigned int Key) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x19, &Packet) == 0) {
        FRpcServerGfDestroyAmbientScriptPacket* Typed =
            static_cast<FRpcServerGfDestroyAmbientScriptPacket*>(Packet);
        Typed->Field_0x04 = Key;
        Typed->Field_0x00 = RoomId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b343d0, IClient +0x94: GFCOMPLETEAMBIENTSCRIPTLIST 0x13; +0x20 = RoomId.
void CClientImpl::SendServerGFCompleteAmbientScriptList(unsigned int ClientId, unsigned int RoomId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x13, &Packet) == 0) {
        static_cast<FRpcServerGfCompleteAmbientScriptListPacket*>(Packet)->Value = RoomId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34410, IClient +0x98: GFDESTROYMISSIONCOMPUTER 0x16; native store order +0x24 = Key,
// then +0x20 = BaseId.
void CClientImpl::SendServerGFDestroyMissionComputer(unsigned int ClientId, unsigned int BaseId, unsigned int Key) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x16, &Packet) == 0) {
        FRpcServerGfDestroyMissionComputerPacket* Typed =
            static_cast<FRpcServerGfDestroyMissionComputerPacket*>(Packet);
        Typed->Field_0x04 = Key;
        Typed->Field_0x00 = BaseId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34460, IClient +0x9c: GFUPDATEMISSIONCOMPUTER 0x1d; set helper 06b3d9b0.
void CClientImpl::SendServerGFUpdateMissionComputer(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x1d, &Packet) == 0) {
        static_cast<FRpcServerGfUpdateMissionComputerPacket*>(Packet)->SetData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b344b0, IClient +0xa0: GFCOMPLETEMISSIONCOMPUTERLIST 0x10; +0x20 = BaseId.
void CClientImpl::SendServerGFCompleteMissionComputerList(unsigned int ClientId, unsigned int BaseId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x10, &Packet) == 0) {
        static_cast<FRpcServerGfCompleteMissionComputerListPacket*>(Packet)->Value = BaseId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34580, IClient +0xac: GFDESTROYNEWSBROADCAST 0x14; +0x20 = BaseId, +0x24 = Key.
void CClientImpl::SendServerGFDestroyNewsBroadcast(unsigned int ClientId, unsigned int BaseId, unsigned int Key) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x14, &Packet) == 0) {
        FRpcServerGfDestroyNewsBroadcastPacket* Typed =
            static_cast<FRpcServerGfDestroyNewsBroadcastPacket*>(Packet);
        Typed->Field_0x00 = BaseId;
        Typed->Field_0x04 = Key;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b345d0, IClient +0xb0: GFUPDATENEWSBROADCAST 0x1e; set helper 06b3e200.
void CClientImpl::SendServerGFUpdateNewsBroadcast(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x1e, &Packet) == 0) {
        static_cast<FRpcServerGfUpdateNewsBroadcastPacket*>(Packet)->SetData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34620, IClient +0xb4: GFCOMPLETENEWSBROADCASTLIST 0x0e; +0x20 = BaseId.
void CClientImpl::SendServerGFCompleteNewsBroadcastList(unsigned int ClientId, unsigned int BaseId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0xe, &Packet) == 0) {
        static_cast<FRpcServerGfCompleteNewsBroadcastListPacket*>(Packet)->Value = BaseId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b3f970 (thiscall on the CREATESHIP packet): +0xd8 = length, +0xdc = new(2 * length + 2),
// untested.
void FRpcServerCreateShipPacket::SetDisplayName(const wchar_t* Name) {
    if (Name == nullptr) return;
    DisplayNameLength = static_cast<int>(std::wcslen(Name));
    DisplayName = static_cast<wchar_t*>(::operator new(
        static_cast<unsigned int>(DisplayNameLength) * 2u + 2u, std::nothrow));
    std::wcscpy(DisplayName, Name);
}

// RC 06b34ed0, IClient +0xbc: CREATESHIP. Every store is an integer move. In MP the vibe's
// display name ships when it is non-empty, else its two FmtStr names flattened (each sized like
// RC 06b347e0, operator new'd untested, flatten's return ignored); then the vibe's affiliation
// and its attitude towards Info's second group.
void CClientImpl::SendServerCreateShip(unsigned int ClientId, const CShipCreateInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 4, &Packet) != 0) return;
    FRpcServerCreateShipPacket* Typed = static_cast<FRpcServerCreateShipPacket*>(Packet);
    RpcServerCreateObjectFill(&Info, &Typed->Object);
    Typed->bUsingTradelane = Info.bUsingTradelane;
    Typed->OwnerPlayer = Info.OwnerPlayer;
    Typed->GroupNameId = static_cast<unsigned short>(Info.GroupNameId & 0xffffu);
    Typed->ReputationRank = static_cast<unsigned char>(Info.ReputationRank & 0xffu);
    std::memcpy(&Typed->Throttle, &Info.Throttle, 4u);
    std::memcpy(&Typed->Velocity, &Info.Velocity, 12u);
    std::memcpy(&Typed->AngularVelocity, &Info.AngularVelocity, 12u);
    Typed->bMarkedBy = Info.bMarkedBy;
    Typed->GroupId = static_cast<unsigned short>(Info.GroupId & 0xffffu);
    if (!SinglePlayer()) {
        FmtStr NameA;
        FmtStr NameB;
        const wchar_t* DisplayName = nullptr;
        Reputation::Vibe::GetName(Typed->Object.VibeGroupId, NameA, NameB, DisplayName);
        if (DisplayName != nullptr && std::wcslen(DisplayName) != 0) {
            Typed->SetDisplayName(DisplayName);
        } else {
            Typed->NameASize = RpcGetFlattenedFmtStrSize(NameA);
            Typed->NameA = static_cast<unsigned char*>(
                ::operator new(Typed->NameASize, std::nothrow));
            NameA.flatten(Typed->NameA, Typed->NameASize);
            Typed->NameBSize = RpcGetFlattenedFmtStrSize(NameB);
            Typed->NameB = static_cast<unsigned char*>(
                ::operator new(Typed->NameBSize, std::nothrow));
            NameB.flatten(Typed->NameB, Typed->NameBSize);
        }
        Reputation::Vibe::GetAffiliation(Typed->Object.VibeGroupId, Typed->Affiliation,
            false);
        Reputation::Vibe::AttitudeTowards(Typed->Object.VibeGroupId, Info.VibeOtherGroupId,
            Typed->Attitude);
    }
    PushToClient(ClientId, Packet->AsRaw());
}

// ===== end SERVER 0x01..0x1f =====

// ===== SERVER 0x20..0x3f =====
// Same sender shape as above (serializer at this+0x08). Where retail dereferences the first
// argument (the client id passed BY REFERENCE) the body says so.

// RC 06b33ef0, IClient +0x60: SETEQUIPMENT. The stack handle is constructed only after a
// successful allocation and destroyed by 06b3a450 on every exit.
void CClientImpl::SendServerSetEquipment(unsigned int ClientId, const EquipDescVector& Equipment) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x24, &Packet) == 0) {
        FRpcServerEquipHandle Handle;
        RpcServerEquipHandleFromVector(&Equipment, &Handle, true);
        static_cast<FRpcServerSetEquipmentPacket*>(Packet)->AssignEquipment(Handle);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b33f80, IClient +0x64: SETCARGO. Same body as SendServerSetEquipment, also with
// bAll = 1; only the packet setter differs (06b3c770).
void CClientImpl::SendServerSetCargo(unsigned int ClientId, const EquipDescVector& Cargo) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x25, &Packet) == 0) {
        FRpcServerEquipHandle Handle;
        RpcServerEquipHandleFromVector(&Cargo, &Handle, true);
        static_cast<FRpcServerSetCargoPacket*>(Packet)->AssignEquipment(Handle);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34010, IClient +0x68: SETADDITEM 0x2e. Offsets: +0x20 ArchId, +0x30 Id (zero-extended),
// +0x34 Health (x87), +0x24 Count, raw bytes +0x38 / +0x39, +0x2c AddResult; hardpoint setter
// 06b3cd50.
void CClientImpl::SendServerSetAddItem(unsigned int ClientId, int AddResult, const EquipDesc& Item) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x2e, &Packet) == 0) {
        FRpcServerSetAddItemPacket* Typed = static_cast<FRpcServerSetAddItemPacket*>(Packet);
        Typed->ArchId = Item.ArchId;
        Typed->Id = Item.Id;
        RpcServerX87CopyFloat(&Typed->Health, &Item.Health);
        Typed->Count = Item.Count;
        Typed->bEquipped = Item.bEquipped;
        std::memcpy(&Typed->bMission, &Item.bMission, 1u); // raw byte, not a bool test
        Typed->AddResult = static_cast<unsigned int>(AddResult);
        if (!Item.is_internal()) {
            Typed->SetHardpoint(Item.Hardpoint,
                static_cast<unsigned int>(std::strlen(Item.Hardpoint)));
        }
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36700, IClient +0x124: SETMISSIONOBJECTIVESTATE 0x33. +0x20 = [Info], +0x24 = byte
// [Info+4] zero-extended, word +0x28 = word [Info+8]. Client id by reference.
void CClientImpl::SendServerSetMissionObjectiveState(const unsigned int& ClientId,
                                                     const FServerMissionObjectiveStateInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x33, &Packet) == 0) {
        FRpcServerSetMissionObjectiveStatePacket* Typed =
            static_cast<FRpcServerSetMissionObjectiveStatePacket*>(Packet);
        Typed->MissionId = Info.MissionId;
        Typed->ObjectiveIndex = Info.ObjectiveIndex;
        Typed->State = static_cast<unsigned short>(Info.State);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36500, IClient +0xf8: REQUESTSPACESCRIPT 0x32. +0x20 = [Info], +0x28..+0x30 =
// [Info+0x108..+0x110]; any CameraMode other than 1/2 keeps the ctor's Mode 1. Name setter
// 06b44240.
void CClientImpl::SendServerRequestSpaceScript(unsigned int ClientId, const FServerRequestSpaceScriptInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x32, &Packet) == 0) {
        FRpcServerRequestSpaceScriptPacket* Typed =
            static_cast<FRpcServerRequestSpaceScriptPacket*>(Packet);
        Typed->ObjectId = Info.ObjectId;
        Typed->Position[0] = Info.PosXBits;
        Typed->Position[1] = Info.PosYBits;
        Typed->Position[2] = Info.PosZBits;
        if (Info.CameraMode == 1u) {
            Typed->Mode = 1;
        } else if (Info.CameraMode == 2u) {
            Typed->Mode = 0;
        }
        Typed->SetName(Info.Name, static_cast<unsigned int>(std::strlen(Info.Name)));
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36850, IClient +0x11c: REPLACEMISSIONOBJECTIVE 0x34. +0x20 = [Info], +0x24 = byte
// [Info+0x20] zero-extended; Text is the FmtStr at Info+4, allocated with no null test. Client
// id by reference.
void CClientImpl::SendServerReplaceMissionObjective(const unsigned int& ClientId,
                                                    const FServerReplaceMissionObjectiveInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x34, &Packet) == 0) {
        FRpcServerReplaceMissionObjectivePacket* Typed =
            static_cast<FRpcServerReplaceMissionObjectivePacket*>(Packet);
        Typed->MissionId = Info.MissionId;
        Typed->ObjectiveIndex = Info.ObjectiveIndex;
        Typed->TextSize = RpcGetFlattenedFmtStrSize(Info.Text);
        Typed->TextData = static_cast<unsigned char*>(::operator new(Typed->TextSize, std::nothrow));
        Info.Text.flatten(Typed->TextData, Typed->TextSize);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36ae0, IClient +0x120: SETMISSIONOBJECTIVES 0x35. +0x20 = [Info], +0x24 = byte
// [Info+0x3c] zero-extended; TextA / TextB from Info+0x0c / Info+0x24; one 0xc record per entry
// (count [Info+8], State = entry byte +0x18), the null test only skips the zeroing; 06b44e20
// appends. Client id by reference.
void CClientImpl::SendServerSetMissionObjectives(const unsigned int& ClientId,
                                                 const FServerSetMissionObjectivesInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x35, &Packet) == 0) {
        FRpcServerSetMissionObjectivesPacket* Typed =
            static_cast<FRpcServerSetMissionObjectivesPacket*>(Packet);
        Typed->MissionId = Info.MissionId;
        Typed->MissionType = Info.MissionType;
        Typed->TextA.Size = RpcGetFlattenedFmtStrSize(Info.TextA);
        Typed->TextA.Data = static_cast<unsigned char*>(::operator new(Typed->TextA.Size, std::nothrow));
        Info.TextA.flatten(Typed->TextA.Data, Typed->TextA.Size);
        Typed->TextB.Size = RpcGetFlattenedFmtStrSize(Info.TextB);
        Typed->TextB.Data = static_cast<unsigned char*>(::operator new(Typed->TextB.Size, std::nothrow));
        Info.TextB.flatten(Typed->TextB.Data, Typed->TextB.Size);
        for (unsigned int Index = 0; Index < Info.Count; ++Index) {
            FRpcServerMissionObjectiveRecord* Record = static_cast<FRpcServerMissionObjectiveRecord*>(
                ::operator new(sizeof(FRpcServerMissionObjectiveRecord), std::nothrow));
            if (Record != nullptr) {
                Record->Size = 0;
                Record->Data = nullptr;
            }
            const FServerMissionObjectiveEntry& Entry = Info.Entries[Index];
            Record->State = static_cast<unsigned char>(Entry.State);
            Record->Size = RpcGetFlattenedFmtStrSize(Entry.Text);
            Record->Data = static_cast<unsigned char*>(::operator new(Record->Size, std::nothrow));
            Entry.Text.flatten(Record->Data, Record->Size);
            Typed->PushObjective(Record);
        }
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b340c0, IClient +0x6c: SETREMOVEITEM. +0x28 = RemoveResult, +0x20 =
// ItemId (the low word zero-extended), +0x24 = Count.
void CClientImpl::SendServerSetRemoveItem(unsigned int ClientId, int RemoveResult, unsigned short ItemId, int Count) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x2f, &Packet) == 0) {
        FRpcServerSetRemoveItemPacket* Typed = static_cast<FRpcServerSetRemoveItemPacket*>(Packet);
        Typed->Field_0x08 = static_cast<unsigned int>(RemoveResult);
        Typed->Field_0x00 = ItemId;
        Typed->Field_0x04 = static_cast<unsigned int>(Count);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b341c0, IClient +0x78: GFUPDATECHAR 0x26; set helper 06b3cdd0. Same body as
// SendServerGFScriptBehavior except the opcode.
void CClientImpl::SendServerGFUpdateChar(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x26, &Packet) == 0) {
        static_cast<FRpcServerGfUpdateCharPacket*>(Packet)->SetData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b344f0, IClient +0xa4: GFMISSIONVENDORACCEPTANCE 0x20; set helper
// 06b3dcc0. Same body as SendServerGFScriptBehavior except the opcode.
void CClientImpl::SendServerGFMissionVendorAcceptance(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x20, &Packet) == 0) {
        static_cast<FRpcServerGfMissionVendorAcceptancePacket*>(Packet)->SetData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35e20, IClient +0xd0: EXPLODEASTEROIDMINE 0x31. +0x20..+0x2c = the four FMineExplosion
// dwords, integer moves.
void CClientImpl::SendServerExplodeAsteroidMine(unsigned int ClientId, const FMineExplosion& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x31, &Packet) == 0) {
        FRpcServerExplodeAsteroidMinePacket* Typed =
            static_cast<FRpcServerExplodeAsteroidMinePacket*>(Packet);
        Typed->MineId = Info.MineId;
        std::memcpy(&Typed->Position.x, &Info.Offset.x, 4u);
        std::memcpy(&Typed->Position.y, &Info.Offset.y, 4u);
        std::memcpy(&Typed->Position.z, &Info.Offset.z, 4u);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36370, IClient +0xe8: SYSTEM_SWITCH_IN 0x22. Retail stages Position/Orientation in
// stack temps first; every move is an integer one, so the direct copies are equivalent.
void CClientImpl::SendServerSystemSwitchIn(unsigned int ClientId, const FPayloadServerSystemSwitchIn& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x22, &Packet) == 0) {
        FRpcServerSystemSwitchInPacket* Typed = static_cast<FRpcServerSystemSwitchInPacket*>(Packet);
        Typed->ShipId = Info.ShipId;
        Typed->JumpObjTypeBits = Info.JumpObjTypeBits;
        std::memcpy(&Typed->Position, &Info.Position, sizeof(Typed->Position));
        std::memcpy(&Typed->Orientation, &Info.Orientation, sizeof(Typed->Orientation));
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b346b0, IClient +0x100: ITEMTRACTORED 0x38.
void CClientImpl::SendServerItemTractored(unsigned int ClientId, const FPayloadServerItemTractored& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x38, &Packet) == 0) {
        FRpcServerItemTractoredPacket* Typed = static_cast<FRpcServerItemTractoredPacket*>(Packet);
        Typed->LootId = Info.LootId;
        Typed->ShipId = Info.ShipId;
        Typed->TractorEquipId = Info.TractorEquipId;
        Typed->UnitsTaken = Info.UnitsTaken;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37aa0, IClient +0x150: REPAIROBJECT 0x3c; native store order +0x24 = word [Info+4],
// +0x20 = [Info], +0x28 = [Info+8].
void CClientImpl::SendServerRepairObject(unsigned int ClientId, const FServerRepairObjectInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x3c, &Packet) == 0) {
        FRpcServerRepairObjectPacket* Typed = static_cast<FRpcServerRepairObjectPacket*>(Packet);
        Typed->EquipId = Info.EquipId;
        Typed->ObjectId = Info.ObjectId;
        std::memcpy(&Typed->Points, &Info.Points, 4u);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36320, IClient +0xe4: SYSTEM_SWITCH_OUT.
void CClientImpl::SendServerSystemSwitchOut(unsigned int ClientId, const FServerSystemSwitchOutInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x21, &Packet) == 0) {
        FRpcServerSystemSwitchOutPacket* Typed = static_cast<FRpcServerSystemSwitchOutPacket*>(Packet);
        Typed->Field_0x00 = Info.ShipId;
        Typed->Field_0x04 = Info.GateId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35bf0, IClient +0xf4: REQUESTCREATESHIPRESP. +0x24 = ShipId, then the
// low byte of bSuccess into +0x20 (raw).
void CClientImpl::SendServerRequestCreateShipResp(unsigned int ClientId, bool bSuccess, unsigned int ShipId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x27, &Packet) == 0) {
        FRpcServerRequestCreateShipRespPacket* Typed =
            static_cast<FRpcServerRequestCreateShipRespPacket*>(Packet);
        Typed->ShipId = ShipId;
        Typed->bSuccess = static_cast<unsigned char>(bSuccess);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34a30, IClient +0x10c: ADJUSTATTITUDE; the float moves through the x87 stack.
void CClientImpl::SendServerAdjustAttitude(unsigned int ClientId, const FPayloadServerAdjustAttitude& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x2a, &Packet) == 0) {
        FRpcServerAdjustAttitudePacket* Typed = static_cast<FRpcServerAdjustAttitudePacket*>(Packet);
        Typed->VibeHandle = Info.VibeHandle;
        RpcServerX87CopyFloat(&Typed->Attitude, &Info.Attitude);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b361f0, IClient +0xc8: CREATEGUIDED 0x37. Integer copies (no x87), in retail order.
// Client id by reference.
void CClientImpl::SendServerCreateGuided(const unsigned int& ClientId, const FServerCreateGuidedInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x37, &Packet) == 0) {
        FRpcServerCreateGuidedPacket* Typed = static_cast<FRpcServerCreateGuidedPacket*>(Packet);
        Typed->OwnerPlayer = Info.OwnerPlayer;
        Typed->ArchetypeNickname = Info.ArchetypeNickname;
        Typed->OwnerId = Info.OwnerId;
        Typed->SubObjId = Info.SubObjId;
        Typed->TargetId = Info.TargetId;
        Typed->TargetSubObjId = Info.TargetSubObjId;
        Typed->ObjId = Info.ObjId;
        std::memcpy(&Typed->Position.x, &Info.Position.x, 4u);
        std::memcpy(&Typed->Position.y, &Info.Position.y, 4u);
        std::memcpy(&Typed->Position.z, &Info.Position.z, 4u);
        std::memcpy(&Typed->Orientation.w, &Info.Orientation.w, 4u);
        std::memcpy(&Typed->Orientation.x, &Info.Orientation.x, 4u);
        std::memcpy(&Typed->Orientation.y, &Info.Orientation.y, 4u);
        std::memcpy(&Typed->Orientation.z, &Info.Orientation.z, 4u);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35d30, IClient +0xcc: CREATECOUNTER 0x2d. Integer copies in retail order. Client id by
// reference.
void CClientImpl::SendServerCreateCounter(const unsigned int& ClientId, const FServerCreateProjectileInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x2d, &Packet) == 0) {
        FRpcServerCreateCounterPacket* Typed = static_cast<FRpcServerCreateCounterPacket*>(Packet);
        Typed->ArchetypeNickname = Info.ArchetypeNickname;
        Typed->ObjId = Info.ObjId;
        Typed->OwnerPlayer = Info.OwnerPlayer;
        Typed->OwnerId = Info.OwnerId;
        Typed->SubObjId = Info.SubObjId;
        std::memcpy(&Typed->Position.x, &Info.Position.x, 4u);
        std::memcpy(&Typed->Position.y, &Info.Position.y, 4u);
        std::memcpy(&Typed->Position.z, &Info.Position.z, 4u);
        std::memcpy(&Typed->Orientation.w, &Info.Orientation.w, 4u);
        std::memcpy(&Typed->Orientation.x, &Info.Orientation.x, 4u);
        std::memcpy(&Typed->Orientation.y, &Info.Orientation.y, 4u);
        std::memcpy(&Typed->Orientation.z, &Info.Orientation.z, 4u);
        std::memcpy(&Typed->Velocity.x, &Info.Velocity.x, 4u);
        std::memcpy(&Typed->Velocity.y, &Info.Velocity.y, 4u);
        std::memcpy(&Typed->Velocity.z, &Info.Velocity.z, 4u);
        std::memcpy(&Typed->AngularVelocity.x, &Info.AngularVelocity.x, 4u);
        std::memcpy(&Typed->AngularVelocity.y, &Info.AngularVelocity.y, 4u);
        std::memcpy(&Typed->AngularVelocity.z, &Info.AngularVelocity.z, 4u);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36cd0, IClient +0x134: SETCASH. Client id by reference.
void CClientImpl::SendServerSetCash(const unsigned int& ClientId, int Cash) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x30, &Packet) == 0) {
        static_cast<FRpcServerSetCashPacket*>(Packet)->Value = static_cast<unsigned int>(Cash);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36d10, IClient +0x138: SETNUMKILLS. Client id by reference.
void CClientImpl::SendServerSetNumKills(const unsigned int& ClientId, int NumKills) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x3e, &Packet) == 0) {
        static_cast<FRpcServerSetNumKillsPacket*>(Packet)->Value = static_cast<unsigned int>(NumKills);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36d50, IClient +0x13c: SETMISSIONSUCCESSES. Client id by reference.
void CClientImpl::SendServerSetMissionSuccesses(const unsigned int& ClientId, int NumSuccesses) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x3f, &Packet) == 0) {
        static_cast<FRpcServerSetMissionSuccessesPacket*>(Packet)->Value =
            static_cast<unsigned int>(NumSuccesses);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35ef0, IClient +0x1b4: SCANNOTIFY. +0x20 = ScannerId, +0x24 = GoodId.
void CClientImpl::SendServerScanNotify(unsigned int ClientId, unsigned int ScannerId, unsigned int GoodId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x39, &Packet) == 0) {
        FRpcServerScanNotifyPacket* Typed = static_cast<FRpcServerScanNotifyPacket*>(Packet);
        Typed->Field_0x00 = ScannerId;
        Typed->Field_0x04 = GoodId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b365a0, IClient +0xc0: CREATELOOT 0x28. +0x20 = [Info], +0x24 = byte [Info+4]; per item
// (byte count [Info+0x10], stride 0x48 from [Info+0xc]) a stack record filled by integer moves
// and appended (06b44b40). Retail quirk kept: the record's Orientation takes the item's w, y, x, z
// (x and y swapped, 06b3669c).
void CClientImpl::SendServerCreateLoot(unsigned int ClientId, const FServerCreateLootRequest& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x28, &Packet) == 0) {
        const FServerCreateLootRequest* Request = &Info;
        FRpcServerCreateLootPacket* Typed = static_cast<FRpcServerCreateLootPacket*>(Packet);
        Typed->OwnerId = Request->OwnerId;
        Typed->Kind = static_cast<unsigned char>(Request->Kind);
        const FServerLootDesc* Desc = Request->Items;
        FRpcServerLootRecord Record;
        std::memset(&Record, 0, sizeof(Record));
        Record.Orientation.w = 1.0f;
        for (unsigned int Left = Request->Count; Left != 0; --Left, ++Desc) {
            Record.LootId = Desc->LootId;
            Record.ContentsNickname = Desc->ContentsNickname;
            std::memcpy(&Record.ContentsHitPts, &Desc->ContentsHitPts, 4u);
            Record.Units = static_cast<unsigned short>(Desc->Units);
            Record.ContainerNickname = Desc->ContainerNickname;
            std::memcpy(&Record.HitPts, &Desc->HitPts, 4u);
            Record.NameOverride = Desc->NameOverride;
            std::memcpy(&Record.Position, &Desc->Position, sizeof(Record.Position));
            std::memcpy(&Record.Velocity, &Desc->Velocity, sizeof(Record.Velocity));
            std::memcpy(&Record.Orientation.w, &Desc->Orientation.w, 4u);
            std::memcpy(&Record.Orientation.x, &Desc->Orientation.y, 4u);
            std::memcpy(&Record.Orientation.y, &Desc->Orientation.x, 4u);
            std::memcpy(&Record.Orientation.z, &Desc->Orientation.z, 4u);
            Record.bMarkedBy = Desc->bMarkedBy;
            Record.bTemporary = Desc->bTemporary;
            Typed->AddLoot(Record);
        }
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35c40, IClient +0xc4: CREATEMINE 0x2c. Integer moves: +0x20..+0x28 = info +0x00..+0x08,
// word +0x2c = info +0x0c, Position = info +0x10, Orientation = info +0x1c, Velocity = info
// +0x2c, AngularVelocity = info +0x38, +0x64 = info +0x44.
void CClientImpl::SendServerCreateMine(unsigned int ClientId, const FServerCreateProjectileInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x2c, &Packet) == 0) {
        FRpcServerCreateMinePacket* Typed = static_cast<FRpcServerCreateMinePacket*>(Packet);
        Typed->ObjId = Info.ObjId;
        Typed->ArchetypeNickname = Info.ArchetypeNickname;
        Typed->OwnerId = Info.OwnerId;
        Typed->SubObjId = Info.SubObjId;
        std::memcpy(&Typed->Position, &Info.Position, sizeof(Typed->Position));
        std::memcpy(&Typed->Orientation, &Info.Orientation, sizeof(Typed->Orientation));
        std::memcpy(&Typed->Velocity, &Info.Velocity, sizeof(Typed->Velocity));
        std::memcpy(&Typed->AngularVelocity, &Info.AngularVelocity, sizeof(Typed->AngularVelocity));
        Typed->OwnerPlayer = Info.OwnerPlayer;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34830, IClient +0x108: SETREPUTATION 0x29. Target = [Info], Affiliation = [Info+8];
// NameA / NameB from Info+0x18 / Info+0x30 (allocated with no null test); display-name setter
// 06b3f670; the stack relation list (new(8) per relation, integer moves) is deep-copied into the
// packet (06b3f6c0) and freed after the send; Attitude = [Info+4] as an integer.
void CClientImpl::SendServerSetReputation(unsigned int ClientId, const FServerSetReputationInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x29, &Packet) == 0) {
        FRpcServerSetReputationPacket* Typed = static_cast<FRpcServerSetReputationPacket*>(Packet);
        Typed->Target = static_cast<unsigned int>(Info.VibeHandle);
        Typed->Affiliation = Info.Affiliation;
        Typed->NameA.Size = RpcGetFlattenedFmtStrSize(Info.NameA);
        Typed->NameA.Data = static_cast<unsigned char*>(::operator new(Typed->NameA.Size, std::nothrow));
        Info.NameA.flatten(Typed->NameA.Data, Typed->NameA.Size);
        Typed->NameB.Size = RpcGetFlattenedFmtStrSize(Info.NameB);
        Typed->NameB.Data = static_cast<unsigned char*>(::operator new(Typed->NameB.Size, std::nothrow));
        Info.NameB.flatten(Typed->NameB.Data, Typed->NameB.Size);
        Typed->SetDisplayName(Info.DisplayName,
            static_cast<unsigned int>(std::wcslen(Info.DisplayName)));
        std::list< ::Reputation::Relation*> Local;
        for (unsigned short Index = 0; Index < Info.RelationCount; ++Index) {
            ::Reputation::Relation* Copy = static_cast< ::Reputation::Relation*>(
                ::operator new(sizeof(::Reputation::Relation), std::nothrow));
            Copy->GroupId = Info.Relations[Index].GroupId;
            std::memcpy(&Copy->Feeling, &Info.Relations[Index].Feeling, 4u);
            Local.push_back(Copy);
        }
        RpcServerRelationListAppendCopy(Typed->Relations, Local);
        std::memcpy(&Typed->Attitude, &Info.Attitude, 4u);
        PushToClient(ClientId, Packet->AsRaw());
        for (std::list< ::Reputation::Relation*>::iterator It = Local.begin(); It != Local.end(); ++It) {
            if (*It != nullptr) ::operator delete(*It);
        }
    }
}

// RC 06b36080, IClient +0x148: SETGROUPFEELINGS 0x2b. Name setter 06b43920 (strlen + 1); the
// relation list is built as in SendServerSetReputation (count byte [Info+0x14], array
// [Info+0x10]) and deep-copied (06b43da0).
void CClientImpl::SendServerSetGroupFeelings(unsigned int ClientId, const FServerSetGroupFeelingsInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x2b, &Packet) == 0) {
        FRpcServerSetGroupFeelingsPacket* Typed = static_cast<FRpcServerSetGroupFeelingsPacket*>(Packet);
        Typed->SetName(Info.Name, static_cast<unsigned int>(std::strlen(Info.Name)) + 1u);
        std::list< ::Reputation::Relation*> Local;
        for (unsigned short Index = 0; Index < Info.RelationCount; ++Index) {
            ::Reputation::Relation* Copy = static_cast< ::Reputation::Relation*>(
                ::operator new(sizeof(::Reputation::Relation), std::nothrow));
            Copy->GroupId = Info.Relations[Index].GroupId;
            std::memcpy(&Copy->Feeling, &Info.Relations[Index].Feeling, 4u);
            Local.push_back(Copy);
        }
        RpcServerRelationListAppendCopy(Typed->Relations, Local);
        PushToClient(ClientId, Packet->AsRaw());
        for (std::list< ::Reputation::Relation*>::iterator It = Local.begin(); It != Local.end(); ++It) {
            if (*It != nullptr) ::operator delete(*It);
        }
    }
}

// RC 06b37b00, IClient +0x154: REMOTEOBJECTCARGOUPDATE 0x3d. The equipment handle is built with
// bAll = 0 (only unequipped items) and assigned by 06b45710. Client id by reference.
void CClientImpl::SendServerRemoteObjectCargoUpdate(const unsigned int& ClientId, unsigned int ObjectId,
                                                    const EquipDescVector& Cargo, unsigned char Flag) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x3d, &Packet) == 0) {
        FRpcServerRemoteObjectCargoUpdatePacket* Typed =
            static_cast<FRpcServerRemoteObjectCargoUpdatePacket*>(Packet);
        Typed->Flag = Flag;
        Typed->ObjectId = ObjectId;
        FRpcServerEquipHandle Handle;
        RpcServerEquipHandleFromVector(&Cargo, &Handle, false);
        Typed->AssignEquipment(Handle);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// ===== end SERVER 0x20..0x3f =====

// ===== SERVER 0x40..0x5b =====
// Same sender shape. BY REF: the client id is taken by reference; by value otherwise.

// RC 06b33e70, 0x49 SETHULLSTATUS, by value; the float moves through x87 into +0x20.
void CClientImpl::SendServerSetHullStatus(unsigned int ClientId, float HullStatus) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x49, &Packet) == 0) {
        RpcServerX87CopyFloat(&static_cast<FRpcSetHullStatusPacket*>(Packet)->Value, &HullStatus);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34540, 0x5a GFMISSIONVENDORWHYEMPTY, by value: the low byte of Reason.
void CClientImpl::SendServerGFMissionVendorWhyEmpty(unsigned int ClientId, unsigned int Reason) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x5a, &Packet) == 0) {
        static_cast<FRpcServerGfMissionVendorWhyEmptyPacket*>(Packet)->Value =
            static_cast<unsigned char>(Reason);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b362c0, 0x4a SETGUIDEDTARGET, by value: GuidedId, TargetId, low word of the third.
void CClientImpl::SendServerSetGuidedTarget(unsigned int ClientId, unsigned int GuidedId, unsigned int TargetId,
                                            unsigned short TargetSubObjId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x4a, &Packet) == 0) {
        FRpcServerSetGuidedTargetPacket* Typed = static_cast<FRpcServerSetGuidedTargetPacket*>(Packet);
        Typed->GuidedId = GuidedId;
        Typed->TargetId = TargetId;
        Typed->TargetSubObjId = TargetSubObjId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34710, 0x51 USE_ITEM, by value.
void CClientImpl::SendServerUseItem(unsigned int ClientId, const FPayloadServerUseItem& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x51, &Packet) == 0) {
        FRpcServerUseItemPacket* Typed = static_cast<FRpcServerUseItemPacket*>(Packet);
        Typed->ObjectId = Info.ObjectId;
        Typed->EquipId = Info.EquipId;
        Typed->Count = Info.Count;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34bc0, 0x56 SENDNNMESSAGE, by value.
void CClientImpl::SendServerSendNNMessage(unsigned int ClientId, unsigned int MessageId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x56, &Packet) == 0) {
        static_cast<FRpcServerSendNNMessagePacket*>(Packet)->Value = MessageId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36a20, 0x45 SET_MISSION_MESSAGE, BY REF. Size from the inlined 06b347e0 walk;
// allocation not null-tested.
void CClientImpl::SendServerSetMissionMessage(const unsigned int& ClientId, const CSetMissionMessage& Message) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x45, &Packet) == 0) {
        FRpcServerSetMissionMessagePacket* Typed = static_cast<FRpcServerSetMissionMessagePacket*>(Packet);
        Typed->TextSize = RpcGetFlattenedFmtStrSize(Message.Text);
        Typed->TextData = static_cast<unsigned char*>(::operator new(Typed->TextSize, std::nothrow));
        Message.Text.flatten(Typed->TextData, Typed->TextSize);
        Typed->Type = Message.Type;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36760, 0x5b MISSIONSAVEA, by value.
void CClientImpl::SendServerMissionSaveA(unsigned int ClientId, unsigned long MissionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x5b, &Packet) == 0) {
        static_cast<FRpcServerMissionSaveAPacket*>(Packet)->Value = MissionId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b367a0, 0x46 MARKOBJ, BY REF: ObjectId, low byte of MarkType.
void CClientImpl::SendServerMarkObj(const unsigned int& ClientId, unsigned int ObjectId, int MarkType) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x46, &Packet) == 0) {
        FRpcServerMarkObjPacket* Typed = static_cast<FRpcServerMarkObjPacket*>(Packet);
        Typed->ObjectId = ObjectId;
        Typed->MarkType = static_cast<unsigned char>(MarkType);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b367f0, 0x47 CFGINTERFACENOTIFICATION, BY REF: two dwords and the raw byte of Info.
void CClientImpl::SendServerCfgInterfaceNotification(const unsigned int& ClientId,
                                                     const FPayloadServerCfgInterfaceNotification& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x47, &Packet) == 0) {
        FRpcServerCfgInterfaceNotificationPacket* Typed =
            static_cast<FRpcServerCfgInterfaceNotificationPacket*>(Packet);
        Typed->NotifType = Info.NotifType;
        Typed->Param = Info.Param;
        Typed->bEnable = Info.bEnable;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36d90, 0x40 SETMISSIONFAILURES, BY REF.
void CClientImpl::SendServerSetMissionFailures(const unsigned int& ClientId, int NumFailures) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x40, &Packet) == 0) {
        static_cast<FRpcServerSetMissionFailuresPacket*>(Packet)->Value =
            static_cast<unsigned int>(NumFailures);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b364a0, 0x44 REQUEST_RETURNED, BY REF: ShipId and the low bytes of the last three.
void CClientImpl::SendServerRequestReturned(const unsigned int& ClientId, unsigned int ShipId, int RequestType,
                                            int Response, int Extra) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x44, &Packet) == 0) {
        FRpcServerRequestReturnedPacket* Typed = static_cast<FRpcServerRequestReturnedPacket*>(Packet);
        Typed->ObjectId = ShipId;
        Typed->RequestType = static_cast<unsigned char>(RequestType);
        Typed->Response = static_cast<unsigned char>(Response);
        Typed->DockPort = static_cast<unsigned char>(Extra);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37bb0, 0x55 OBJECTCARGOUPDATE, BY REF: ObjectId, then EquipDesc word +0x02, dword +0x04,
// word +0x14, float +0x10 (x87), raw byte +0x18.
void CClientImpl::SendServerObjectCargoUpdate(const unsigned int& ClientId, unsigned int ObjectId,
                                              const EquipDesc& Item) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x55, &Packet) == 0) {
        FRpcServerObjectCargoUpdatePacket* Typed = static_cast<FRpcServerObjectCargoUpdatePacket*>(Packet);
        Typed->ObjectId = ObjectId;
        Typed->EquipId = Item.Id;
        Typed->ArchId = Item.ArchId;
        Typed->Count = static_cast<unsigned short>(Item.Count);
        RpcServerX87CopyFloat(&Typed->Status, &Item.Health);
        Typed->bMission = *reinterpret_cast<const unsigned char*>(&Item.bMission);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37c30, 0x41 BURNFUSE, BY REF: every field an integer move (the two floats included).
void CClientImpl::SendServerBurnFuse(const unsigned int& ClientId, const FServerBurnFuseInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x41, &Packet) == 0) {
        FRpcServerBurnFusePacket* Typed = static_cast<FRpcServerBurnFusePacket*>(Packet);
        Typed->SourceId = Info.SourceId;
        Typed->ObjectId = Info.ObjectId;
        Typed->FuseId = Info.FuseId;
        Typed->SubObjId = Info.SubObjId;
        std::memcpy(&Typed->AtT, &Info.AtT, 4u);
        std::memcpy(&Typed->Lifetime, &Info.Lifetime, 4u);
        Typed->bLit = Info.bLit;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36950, 0x4b SET_CAMERA, BY REF: the nine dwords of Info by integer moves.
void CClientImpl::SendServerSetCamera(const unsigned int& ClientId, const FPayloadServerSetCamera& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x4b, &Packet) == 0) {
        FRpcServerSetCameraPacket* Typed = static_cast<FRpcServerSetCameraPacket*>(Packet);
        std::memcpy(&Typed->Position.x, &Info.Position.x, 4u);
        std::memcpy(&Typed->Position.y, &Info.Position.y, 4u);
        std::memcpy(&Typed->Position.z, &Info.Position.z, 4u);
        std::memcpy(&Typed->Orientation.w, &Info.Orient.w, 4u);
        std::memcpy(&Typed->Orientation.x, &Info.Orient.x, 4u);
        std::memcpy(&Typed->Orientation.y, &Info.Orient.y, 4u);
        std::memcpy(&Typed->Orientation.z, &Info.Orient.z, 4u);
        std::memcpy(&Typed->Fov, &Info.Fov, 4u);
        std::memcpy(&Typed->Aspect, &Info.Aspect, 4u);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b369e0, 0x4c REVERT_CAMERA, BY REF: header only.
void CClientImpl::SendServerRevertCamera(const unsigned int& ClientId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x4c, &Packet) == 0) {
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b36910, 0x43 SET_STORY_CUE, BY REF: the cue id is read through CueId too.
void CClientImpl::SendServerSetStoryCue(const unsigned int& ClientId, const unsigned int& CueId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x43, &Packet) == 0) {
        static_cast<FRpcServerSetStoryCuePacket*>(Packet)->Value = CueId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b380c0, 0x53 FORMATION_UPDATE, by value: LeaderId, then Offset (Vector) by integer moves.
void CClientImpl::SendServerFormationUpdate(unsigned int ClientId, unsigned int LeaderId, const Vector& Offset) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x53, &Packet) == 0) {
        FRpcServerFormationUpdatePacket* Typed = static_cast<FRpcServerFormationUpdatePacket*>(Packet);
        Typed->LeaderId = LeaderId;
        std::memcpy(&Typed->Offset.x, &Offset.x, 4u);
        std::memcpy(&Typed->Offset.y, &Offset.y, 4u);
        std::memcpy(&Typed->Offset.z, &Offset.z, 4u);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b38120, 0x57 SET_MUSIC, by value; Fade moves through x87.
void CClientImpl::SendServerSetMusic(unsigned int ClientId, unsigned int SpaceMusic, unsigned int DangerMusic,
                                     unsigned int BattleMusic, unsigned int OverrideMusic, bool bFlag,
                                     float Fade) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x57, &Packet) == 0) {
        FRpcServerSetMusicPacket* Typed = static_cast<FRpcServerSetMusicPacket*>(Packet);
        Typed->SpaceMusic = SpaceMusic;
        Typed->DangerMusic = DangerMusic;
        Typed->BattleMusic = BattleMusic;
        Typed->OverrideMusic = OverrideMusic;
        Typed->bNoLoop = static_cast<unsigned char>(bFlag);
        RpcServerX87CopyFloat(&Typed->Fade, &Fade);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b381a0, 0x58 CANCEL_MUSIC, by value: header only.
void CClientImpl::SendServerCancelMusic(unsigned int ClientId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x58, &Packet) == 0) {
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b381d0, 0x59 PLAY_SOUND_EFFECT, by value.
void CClientImpl::SendServerPlaySoundEffect(unsigned int ClientId, unsigned int SoundId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x59, &Packet) == 0) {
        static_cast<FRpcServerPlaySoundEffectPacket*>(Packet)->Value = SoundId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35e80, 0x4d LOADHINT: Hint is used with no null test. Serialize (vtable +4), data
// setter 06b42f40, +0x28 = BaseHint::Kind, CleanUp (vtable +0xc) before the send.
void CClientImpl::SendServerLoadHint(const unsigned int& ClientId, BaseHint* Hint) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x4d, &Packet) == 0) {
        FRpcServerLoadHintPacket* Typed = static_cast<FRpcServerLoadHintPacket*>(Packet);
        unsigned char* Serialized = nullptr;
        const unsigned int SerializedSize = Hint->Serialize(&Serialized);
        Typed->SetData(Serialized, SerializedSize);
        Typed->Kind = Hint->Kind;
        Hint->CleanUp(Serialized);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b35f40, 0x4e SETDIRECTIVE, BY REF: copied field by field (padding bytes not copied).
void CClientImpl::SendServerSetDirective(const unsigned int& ClientId, const FPayloadServerSetDirective& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x4e, &Packet) == 0) {
        static_cast<FRpcServerSetDirectivePacket*>(Packet)->SetDirective(Info);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37fc0, 0x52 PLAYERLIST Kind 1, by value; entry setter 06b466f0.
void CClientImpl::SendServerPlayerListAdd(unsigned int ClientId, const wchar_t* Name, unsigned int PlayerId,
                                          bool bAlreadyPresent) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x52, &Packet) == 0) {
        FRpcServerPlayerListPacket* Typed = static_cast<FRpcServerPlayerListPacket*>(Packet);
        Typed->Kind = kPlayerListKindEntry;
        Typed->bAlreadyPresent = static_cast<unsigned char>(bAlreadyPresent);
        Typed->SetEntry(Name, PlayerId);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b38020, 0x52 PLAYERLIST Kind 2, by value.
void CClientImpl::SendServerPlayerListRemove(unsigned int ClientId, unsigned int PlayerId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x52, &Packet) == 0) {
        FRpcServerPlayerListPacket* Typed = static_cast<FRpcServerPlayerListPacket*>(Packet);
        Typed->Kind = kPlayerListKindDrop;
        Typed->SetEntry(nullptr, PlayerId);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b38070, 0x52 PLAYERLIST Kind 0, by value.
void CClientImpl::SendServerPlayerListClear(unsigned int ClientId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x52, &Packet) == 0) {
        FRpcServerPlayerListPacket* Typed = static_cast<FRpcServerPlayerListPacket*>(Packet);
        Typed->Kind = kPlayerListKindClear;
        Typed->SetEntry(nullptr, 0u);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b33eb0, 0x48 SETCOLLISIONGROUPS, by value; list assignment 06b3a360.
void CClientImpl::SendServerSetCollisionGroups(unsigned int ClientId, const std::list<CollisionGroupDesc>& Groups) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x48, &Packet) == 0) {
        static_cast<FRpcServerSetCollisionGroupsPacket*>(Packet)->SetGroups(Groups);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34a80, 0x4f SENDCOMM, by value (23 argument dwords). Retail copies the by-value Costume
// (13 dwords) to a stack local and the accessory loop reads Ids[i] = local[4 + i] for i < the
// count (stored to +0x5c first and re-read each pass, signed), unbounded. Past the local lies
// retail's own frame -- the return address, then the 23 argument dwords -- so Frame reproduces
// that memory for counts up to 33: i = 8 rewrites the count with itself, i = 9 writes the
// return-address slot into +0x60 (overwritten by CaptionIds, so 0 here), i = 10 puts ClientId into
// +0x64, which is then handed to operator delete, as retail does. Counts above 33 read past
// Frame as retail reads past its caller's arguments (undefined; writes past the 0x78-byte packet
// already start at i = 15 in both). TextIds: new(TextCount * 4) with no null test, signed
// bound; Duration through x87.
void CClientImpl::SendServerSendComm(unsigned int ClientId, unsigned int SenderId, unsigned int ReceiverId,
                                     unsigned int VoiceId, Costume CommCostume, unsigned int CaptionIds,
                                     const unsigned int* TextIds, int TextCount, unsigned int PermutationIds,
                                     float Duration, bool bReceiverIsOwnShip) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x4f, &Packet) == 0) {
        FRpcServerSendCommPacket* Typed = static_cast<FRpcServerSendCommPacket*>(Packet);
        Typed->SenderId = SenderId;
        Typed->ReceiverId = ReceiverId;
        Typed->VoiceId = VoiceId;
        // The retail frame image as raw dwords: the costume local, the return-address slot,
        // then the 23 argument dwords (the costume a second time among them). The pointer,
        // float and bool arguments enter as their stack-slot bits; the bool's slot holds 0/1
        // here where retail's upper three bytes are whatever its caller pushed (Frame[36] is
        // read only at a count of 33).
        static_assert(sizeof(Costume) == 13 * sizeof(unsigned int), "Costume is 13 dwords");
        unsigned int Frame[37];
        std::memcpy(&Frame[0], &CommCostume, sizeof(Costume));
        Frame[13] = 0u;
        Frame[14] = ClientId;
        Frame[15] = SenderId;
        Frame[16] = ReceiverId;
        Frame[17] = VoiceId;
        std::memcpy(&Frame[18], &CommCostume, sizeof(Costume));
        Frame[31] = CaptionIds;
        Frame[32] = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(TextIds));
        Frame[33] = static_cast<unsigned int>(TextCount);
        Frame[34] = PermutationIds;
        std::memcpy(&Frame[35], &Duration, sizeof(float));
        Frame[36] = static_cast<unsigned int>(bReceiverIsOwnShip);
        Typed->Body.PartIds[0] = CommCostume.PartIds[0];
        Typed->Body.PartIds[1] = CommCostume.PartIds[1];
        Typed->Body.PartIds[2] = CommCostume.PartIds[2];
        Typed->Body.PartIds[3] = CommCostume.PartIds[3];
        Typed->Body.AccessoryCount = CommCostume.AccessoryCount;
        unsigned int* Ids = &Typed->Body.AccessoryIds[0];
        for (int Index = 0; Index < static_cast<int>(Typed->Body.AccessoryCount); ++Index) {
            Ids[Index] = Frame[4 + Index];
        }
        Typed->CaptionIds = CaptionIds;
        if (Typed->TextIds != nullptr) ::operator delete(Typed->TextIds);
        Typed->TextIds = static_cast<unsigned int*>(
            ::operator new(static_cast<unsigned int>(TextCount) * 4u, std::nothrow));
        Typed->TextCount = static_cast<signed char>(TextCount);
        for (int Index = 0; Index < TextCount; ++Index) {
            Typed->TextIds[Index] = TextIds[Index];
        }
        Typed->PermutationIds = PermutationIds;
        RpcServerX87CopyFloat(&Typed->Duration, &Duration);
        Typed->bReceiverIsOwnShip = static_cast<unsigned char>(bReceiverIsOwnShip);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// The eight MISCOBJUPDATE senders (0x54), all by value, through the setters RC 06b45830..06b45a60.
// RC 06b37cb0: +0x28 (0x08) = ObjectId, +0x34 (0x01) = GroupId.
void CClientImpl::SendServerMiscObjUpdateObjectGroup(unsigned int ClientId, unsigned int ObjectId, unsigned int GroupId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        FRpcServerMiscObjUpdatePacket* Typed = static_cast<FRpcServerMiscObjUpdatePacket*>(Packet);
        Typed->SetObjectId(ObjectId);
        Typed->SetGroupId(GroupId);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37d50: +0x38 = MissionId, +0x3c = MissionSetBy (0x02).
void CClientImpl::SendServerMiscObjUpdateMission(unsigned int ClientId, unsigned int MissionId, unsigned int MissionSetBy) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        static_cast<FRpcServerMiscObjUpdatePacket*>(Packet)->SetMission(MissionId, MissionSetBy);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37ef0: the text (0x10) assigned from Motd, no null test.
void CClientImpl::SendServerMiscObjUpdateMotd(unsigned int ClientId, const std::wstring& Motd) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        static_cast<FRpcServerMiscObjUpdatePacket*>(Packet)->SetText(Motd);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37d00: +0x28 (0x08) = ObjectId, +0x54 (0x20) = Affiliation.
void CClientImpl::SendServerMiscObjUpdateAffiliation(unsigned int ClientId, unsigned int ObjectId,
                                                     unsigned int Affiliation) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        FRpcServerMiscObjUpdatePacket* Typed = static_cast<FRpcServerMiscObjUpdatePacket*>(Packet);
        Typed->SetObjectId(ObjectId);
        Typed->SetAffiliation(Affiliation);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37da0: +0x24 (0x04) = PlayerId, the low word of Rank into +0x2c (0x40).
void CClientImpl::SendServerMiscObjUpdateRank(unsigned int ClientId, unsigned int PlayerId, int Rank) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        FRpcServerMiscObjUpdatePacket* Typed = static_cast<FRpcServerMiscObjUpdatePacket*>(Packet);
        Typed->SetPlayerId(PlayerId);
        Typed->SetRank(static_cast<unsigned short>(Rank));
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37e50: +0x24 (0x04) = PlayerId, +0x34 (0x01) = GroupId.
void CClientImpl::SendServerMiscObjUpdatePlayerGroup(unsigned int ClientId, unsigned int PlayerId, unsigned int GroupId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        FRpcServerMiscObjUpdatePacket* Typed = static_cast<FRpcServerMiscObjUpdatePacket*>(Packet);
        Typed->SetPlayerId(PlayerId);
        Typed->SetGroupId(GroupId);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37ea0: +0x24 (0x04) = PlayerId, +0x30 (0x80) = SystemId.
void CClientImpl::SendServerMiscObjUpdateSystem(unsigned int ClientId, unsigned int PlayerId, unsigned int SystemId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        FRpcServerMiscObjUpdatePacket* Typed = static_cast<FRpcServerMiscObjUpdatePacket*>(Packet);
        Typed->SetPlayerId(PlayerId);
        Typed->SetSystemId(SystemId);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b37df0: every element of MemberIds, first to last (the end re-read each pass), through
// the id setter 06b45a60; no null test.
void CClientImpl::SendServerMiscObjUpdateFormation(unsigned int ClientId, const std::vector<unsigned int>& MemberIds) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x54, &Packet) == 0) {
        FRpcServerMiscObjUpdatePacket* Typed = static_cast<FRpcServerMiscObjUpdatePacket*>(Packet);
        for (std::vector<unsigned int>::const_iterator It = MemberIds.begin(); It != MemberIds.end(); ++It) {
            Typed->AddId(*It);
        }
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// RC 06b34c50, 0x42 CREATESOLAR, by value. Block fill 06b382f0, +0x9c = Info+0xad, +0x9d =
// Info+0xa8 (raw bytes); outside single player, affiliation +0xb0, attitude +0xb4, then NameA
// +0xa0/+0xa4 and NameB +0xa8/+0xac (allocations not null-tested).
void CClientImpl::SendServerCreateSolar(unsigned int ClientId, const CSolarCreateInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(2, 0x42, &Packet) == 0) {
        FRpcServerCreateSolarPacket* Typed = static_cast<FRpcServerCreateSolarPacket*>(Packet);
        RpcServerCreateObjectFill(&Info, &Typed->Fields);
        Typed->bDestructible = Info.bDestructible;
        Typed->bMarkedBy = Info.bMarkedBy;
        if (!SinglePlayer()) {
            Reputation::Vibe::GetAffiliation(Typed->Fields.VibeGroupId, Typed->Affiliation, false);
            Reputation::Vibe::AttitudeTowards(Typed->Fields.VibeGroupId, Info.VibeOtherGroupId,
                Typed->Attitude);
            FmtStr NameA;
            FmtStr NameB;
            const wchar_t* DisplayName = nullptr;
            Reputation::Vibe::GetName(Typed->Fields.VibeGroupId, NameA, NameB, DisplayName);
            Typed->NameASize = RpcGetFlattenedFmtStrSize(NameA);
            Typed->NameA = static_cast<unsigned char*>(::operator new(Typed->NameASize, std::nothrow));
            NameA.flatten(Typed->NameA, Typed->NameASize);
            Typed->NameBSize = RpcGetFlattenedFmtStrSize(NameB);
            Typed->NameB = static_cast<unsigned char*>(::operator new(Typed->NameBSize, std::nothrow));
            NameB.flatten(Typed->NameB, Typed->NameBSize);
        }
        PushToClient(ClientId, Packet->AsRaw());
    }
}

// ===== end SERVER 0x40..0x5b =====
