// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#define RPCLOCAL_EXPORTS
#include <windows.h>
#include "../include/rpc_iserver.h"
#include "../include/common_server.h"
#include "../include/rpc_iclient.h"
#include "../include/common_goods.h"        // CostumeHint (0x4d LOADHINT), EquipDescVector
#include "../include/server_createinfo.h"   // CShipCreateInfo / CSolarCreateInfo / loot / guided
#include "../include/server_characterset.h" // CCharacterSet::CharacterInfo (0x03)
#include "../include/common_misc.h"
#include "../include/common_archetype.h"
#include "../include/rpc_packet.h"
#include "../include/rpc_iclient_args.h"
#include "../include/server_sysdb.h" // FMineExplosion (0x31)
#include "rpc_server_channel_receive_private.h"
#include "../include/rpc_server_channel_codec.h"

#include <list>
#include <new>
#include <cstring>
#include <cwchar>

struct FRpcLocalCostumeHint final : public CostumeHint
{
};

// ----- 0x03 CHARACTERINFO receiver -----

// RS 06bdfdae (+0x274; +0x278 likewise): the new-expression test only skips the zero stores
// (a null block is written through, reproduced on purpose); the copy is bounded by the
// DESTINATION's count.
static void RpcServerCharacterIdBlockCopy(FRpcServerCharacterIdBlock** Out,
    const FRpcServerCharacterIdBlock* Source) {
    if (Source == nullptr) return;
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
    *Out = Block;
    Block->Body = Source->Body;
    Block->Head = Source->Head;
    Block->RightHand = Source->RightHand;
    Block->LeftHand = Source->LeftHand;
    Block->Count = Source->Count;
    if (static_cast<unsigned int>(Block->Count) != 0) {
        Block->Ids = static_cast<unsigned int*>(
            ::operator new(static_cast<unsigned int>(Block->Count) << 2, std::nothrow));
        for (unsigned int Index = 0; Index < static_cast<unsigned int>(Block->Count); ++Index) {
            Block->Ids[Index] = Source->Ids[Index];
        }
    }
}

// RS 06bdfb70 (packet method, = rpclocal 06d94460). The new-expression test only skips the
// ctor; the copy runs on the result either way (reproduced on purpose).
static void RpcServerCharacterRecordListCopy(std::list<FRpcServerCharacterRecord*>& Dest,
    const std::list<FRpcServerCharacterRecord*>& Source) {
    for (std::list<FRpcServerCharacterRecord*>::const_iterator It = Source.begin();
         It != Source.end(); ++It) {
        const FRpcServerCharacterRecord* From = *It;
        if (From == nullptr) continue;
        FRpcServerCharacterRecord* To = new (std::nothrow) FRpcServerCharacterRecord();
        To->Name = From->Name;
        To->bIsRestart = From->bIsRestart;
        To->bIsAutoSave = From->bIsAutoSave;
        To->TextA = From->TextA;
        To->TextB = From->TextB;
        To->DescripStrId = From->DescripStrId;
        To->TimestampLow = From->TimestampLow;
        To->TimestampHigh = From->TimestampHigh;
        To->ShipArchetype = From->ShipArchetype;
        To->Money = From->Money;
        To->SystemId = From->SystemId;
        To->BaseId = From->BaseId;
        To->LocationId = From->LocationId;
        To->VoiceId = From->VoiceId;
        To->Rank = From->Rank;
        To->Field_0x24c = From->Field_0x24c;
        To->HullStatusBits = From->HullStatusBits;
        To->NumKills = From->NumKills;
        To->NumMissionFailures = From->NumMissionFailures;
        To->NumMissionSuccesses = From->NumMissionSuccesses;
        RpcServerCharacterIdBlockCopy(&To->BlockA, From->BlockA);
        RpcServerCharacterIdBlockCopy(&To->BlockB, From->BlockB);
        To->Equipment.AppendCopy(From->Equipment);
        // +0x260 = low byte of the copied list's size, or 0.
        To->EquipmentCount = (To->Equipment.Records != nullptr)
            ? static_cast<unsigned char>(To->Equipment.Records->size() & 0xffu) : 0u;
        To->CollisionGroups = From->CollisionGroups;
        Dest.push_back(To);
    }
}

// HandleCharacterInfo 06bd48e7: one CCharacterSet::CharacterInfo from one wire record (the
// inverse of the sender's permutation).
static void RpcServerCharacterInfoFromRecord(CCharacterSet::CharacterInfo* Dest,
    const FRpcServerCharacterRecord* Source) {
    Dest->Id = Source->Name;
    Dest->bIsRestart = (Source->bIsRestart != 0) ? 1u : 0u;
    Dest->bIsAutoSave = (Source->bIsAutoSave != 0) ? 1u : 0u;
    Dest->Name = Source->TextA;
    Dest->Description = Source->TextB;
    Dest->DescripStrId = Source->DescripStrId;
    Dest->TimestampLow = Source->TimestampLow;
    Dest->TimestampHigh = Source->TimestampHigh;
    Dest->ShipArchetype = Source->ShipArchetype;
    Dest->Money = static_cast<int>(Source->Money);
    Dest->VoiceId = Source->VoiceId;                                    // -> +0x26c
    Dest->Rank = Source->Rank;                                       // -> +0x270
    Dest->BaseId = Source->BaseId;                                     // -> +0x264
    Dest->LocationId = Source->LocationId;                                 // -> +0x268
    Dest->SystemId = Source->SystemId;                                   // -> +0x260
    std::memcpy(&Dest->HullStatus, &Source->HullStatusBits, 4u);               // -> +0x244
    Dest->NumKills = static_cast<int>(Source->NumKills);                 // -> +0x238
    Dest->NumMissionFailures = static_cast<int>(Source->NumMissionFailures);       // -> +0x240
    Dest->NumMissionSuccesses = static_cast<int>(Source->NumMissionSuccesses);      // -> +0x23c
    // A null block leaves the Costume as constructed; otherwise [1],[0],[3],[2], the count,
    // and an unsigned copy NOT bounded by the eight slots (reproduced on purpose), the bound
    // re-read from the block every round.
    const FRpcServerCharacterIdBlock* Blocks[2] = { Source->BlockA, Source->BlockB };
    Costume* Costumes[2] = { &Dest->ComCostume, &Dest->BaseCostume };
    for (int Which = 0; Which < 2; ++Which) {
        const FRpcServerCharacterIdBlock* Block = Blocks[Which];
        if (Block == nullptr) continue;
        Costume& Out = *Costumes[Which];
        Out.PartIds[1] = Block->Body;
        Out.PartIds[0] = Block->Head;
        Out.PartIds[3] = Block->RightHand;
        Out.PartIds[2] = Block->LeftHand;
        Out.AccessoryCount = static_cast<unsigned int>(Block->Count);
        unsigned int* OutIds = &Out.AccessoryIds[0];
        for (unsigned int Index = 0; Index < static_cast<unsigned int>(Block->Count); ++Index) {
            OutIds[Index] = Block->Ids[Index];
        }
    }
    // Only the handle's list header is tested. Each element is a default EquipDesc appended to
    // +0x248, then the setters on the appended node.
    if (Source->Equipment.Records != nullptr) {
        for (std::list<FRpcServerEquipRecord*>::const_iterator It =
                 Source->Equipment.Records->begin();
             It != Source->Equipment.Records->end(); ++It) {
            const FRpcServerEquipRecord* E = *It;
            Dest->Equipment.Items.push_back(EquipDesc());
            EquipDesc& Item = Dest->Equipment.Items.back();
            Item.set_arch_id(E->ArchId);
            Item.set_count(E->Count);
            Item.set_id(E->Id);
            Item.set_equipped(E->bEquipped);                                // raw byte
            Item.set_status(E->Health);
            // The wire byte stored raw into EquipDesc +0x18.
            std::memcpy(&Item.bMission, &E->bMission, 1u);
            CacheString Hardpoint;
            Hardpoint.Value = StringAlloc(E->Hardpoint, false);
            Item.set_hardpoint(Hardpoint);
        }
    }
    // Onto +0x254.
    Dest->CollisionGroups = Source->CollisionGroups;
}

// RS 06bd4750 (exported; identical to rpclocal 06d84490). The packet is dereferenced
// untested. The packet's list is deep-copied into a local list; per copy a new(0x2dc) record
// whose null result skips the conversion but is still appended to the set, and the copy is then
// freed on the spot while its node keeps the dangling pointer. Retail destroys the Set before
// the local list's nodes; the reverse order here only changes the order of frees. `this` is
// never read, so case 03 and the exported member share this body.
static bool RpcServerHandleCharacterInfo(const FPacketBase* Packet, IClient* Client,
    unsigned int Arg) {
    const FRpcServerCharacterInfoPacket* Typed =
        static_cast<const FRpcServerCharacterInfoPacket*>(Packet);
    CCharacterSet Set;
    std::list<FRpcServerCharacterRecord*> Local;
    RpcServerCharacterRecordListCopy(Local, Typed->Records);
    for (std::list<FRpcServerCharacterRecord*>::iterator It = Local.begin(); It != Local.end();
         ++It) {
        const FRpcServerCharacterRecord* Source = *It;                      // untested
        CCharacterSet::CharacterInfo* Info = new (std::nothrow) CCharacterSet::CharacterInfo();
        if (Info != nullptr) {
            RpcServerCharacterInfoFromRecord(Info, Source);
        }
        Set.Characters.append(Info);
        if (*It != nullptr) {
            delete *It;
        }
    }
    Set.SaveFailureCount = Typed->SaveFailureCount;
    Client->SendServerCharacterInfo(Arg, Set);
    return true;
}

bool IServerImpl::HandleCharacterInfo(CDAPCharacterInfo* Packet, IClient* Client, unsigned int Arg) {
    return RpcServerHandleCharacterInfo(reinterpret_cast<const FPacketBase*>(Packet), Client, Arg);
}
// ----- end 0x03 CHARACTERINFO receiver -----

// RS 06bd5030 (exported; identical to rpclocal 06d84c60; no caller inside remoteserver).
// `this` is unused. The DamageList assignment is the packet's 06bdb260.
bool IServerImpl::HandleDamageObject(CDAPDamageSpaceObj* Packet, IClient* Client, unsigned int Arg) {
    if (Packet == nullptr) {
        return false;
    }
    const FRpcServerDamageObjectPacket* Typed =
        reinterpret_cast<const FRpcServerDamageObjectPacket*>(Packet);
    DamageList Local;
    Local = Typed->Damage;
    Client->SendServerDamageObject(Arg, Typed->ObjectId, Local);
    return true;
}

// ----- 0x29 / 0x2b relation array (inline in cases 06bd159c and 06bd1706) -----
// new(list size * 8) with no null test; every non-null relation is copied at a 16-bit index
// and then freed. The list keeps its (now dangling) values; only its nodes are freed later.
static ::Reputation::Relation* RpcServerRelationArrayFromList(
    std::list< ::Reputation::Relation*>& Local) {
    ::Reputation::Relation* Array = static_cast< ::Reputation::Relation*>(::operator new(
        static_cast<unsigned int>(Local.size()) * sizeof(::Reputation::Relation), std::nothrow));
    unsigned short At = 0;
    for (std::list< ::Reputation::Relation*>::iterator It = Local.begin(); It != Local.end(); ++It) {
        ::Reputation::Relation* Relation = *It;
        if (Relation == nullptr) continue;
        Array[At].GroupId = Relation->GroupId;
        std::memcpy(&Array[At].Feeling, &Relation->Feeling, 4u);
        ++At;
        ::operator delete(Relation);
    }
    return Array;
}
// ----- end relation array -----

void RpcReceiveServerChannel(unsigned int ClientId, FPacketBase* Packet, IClient* Client)
{
    unsigned int Opcode = Packet->GetOpcode();

        switch (Opcode)
        {
            case FlPacketOpcode(FLPACKET_SERVER_CONNECTRESPONSE): // RS case 06bd17c8
            {
                // The only SERVER call without a client id.
                Client->OnConnect(static_cast<const FRpcConnectResponsePacket*>(Packet)->Value);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_LOGINRESPONSE): // RS case 06bd17dc
            {
                // The byte goes through module global 06c138b0.
                static unsigned char GLoginResponseStatic = 0;
                GLoginResponseStatic = static_cast<const FRpcLoginResponsePacket*>(Packet)->Value;
                Client->SendServerLoginResponse(1u, GLoginResponseStatic);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_CHARACTERINFO): // RS case 06bd17fc
            {
                // HandleCharacterInfo(packet, GClient, 1); `this` is unused by the member, so
                // the shared body is called directly.
                RpcServerHandleCharacterInfo(Packet, Client, 1u);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_CREATESHIP): // RS case 06bd20b5
            {
                const FRpcServerCreateShipPacket* Typed =
                    static_cast<const FRpcServerCreateShipPacket*>(Packet);
                // .data:06c138f0, built on first use (guard bit 2 of 06c138b8) and destroyed by
                // the atexit 06bd4390.
                static CShipCreateInfo GCreateShipInfo;
                RpcServerCreateObjectRead(&Typed->Object, &GCreateShipInfo);
                GCreateShipInfo.bUsingTradelane = Typed->bUsingTradelane;
                GCreateShipInfo.OwnerPlayer = Typed->OwnerPlayer;
                GCreateShipInfo.GroupId = static_cast<unsigned int>(Typed->GroupId);
                GCreateShipInfo.GroupNameId = static_cast<unsigned int>(Typed->GroupNameId);
                GCreateShipInfo.ReputationRank = static_cast<unsigned int>(Typed->ReputationRank);
                std::memcpy(&GCreateShipInfo.Throttle, &Typed->Throttle, 4u);
                // x by integer move, y and z through the x87 stack (retail loads z then y and
                // stores y then z).
                std::memcpy(&GCreateShipInfo.AngularVelocity.x, &Typed->AngularVelocity.x, 4u);
                RpcServerX87CopyFloat(&GCreateShipInfo.AngularVelocity.y, &Typed->AngularVelocity.y);
                RpcServerX87CopyFloat(&GCreateShipInfo.AngularVelocity.z, &Typed->AngularVelocity.z);
                std::memcpy(&GCreateShipInfo.Velocity.x, &Typed->Velocity.x, 4u);
                RpcServerX87CopyFloat(&GCreateShipInfo.Velocity.y, &Typed->Velocity.y);
                RpcServerX87CopyFloat(&GCreateShipInfo.Velocity.z, &Typed->Velocity.z);
                GCreateShipInfo.bMarkedBy = Typed->bMarkedBy;
                if (!SinglePlayer()) {
                    // The vibe is the PACKET's +0x2c.
                    FmtStr NameA;
                    FmtStr NameB;
                    NameA.unflatten(Typed->NameA, Typed->NameASize);
                    NameB.unflatten(Typed->NameB, Typed->NameBSize);
                    const int& Group = Typed->Object.VibeGroupId;
                    Reputation::Vibe::EnsureExists(Group);
                    Reputation::Vibe::SetName(Group, NameA, NameB, Typed->DisplayName);
                    Reputation::Vibe::SetAffiliation(Group, Typed->Affiliation, false);
                    Reputation::Vibe::SetAttitudeTowardsPlayer(Group, Typed->Attitude);
                }
                Client->SendServerCreateShip(1u, GCreateShipInfo);
                // CShipCreateInfo::clear() inlined in retail.
                GCreateShipInfo.clear();
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_DAMAGEOBJECT): // RS case 06bd1fc4
            {
                // HandleDamageObject's body inlined, client id literal 1.
                const FRpcServerDamageObjectPacket* Typed =
                    static_cast<const FRpcServerDamageObjectPacket*>(Packet);
                DamageList Local;
                Local = Typed->Damage;
                Client->SendServerDamageObject(1u, Typed->ObjectId, Local);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_DESTROYOBJECT): // RS case 06bd205a
            {
                // Function-local static (guard bit 1 of 06c138b8). The byte is zero-extended
                // into the dword.
                const FRpcServerDestroyObjectPacket* Typed =
                    static_cast<const FRpcServerDestroyObjectPacket*>(Packet);
                static FServerDestroyObjectInfo GDestroyObjectStatic;
                GDestroyObjectStatic.ObjectId = Typed->ObjectId;
                const unsigned int Widened = Typed->DestroyType;
                std::memcpy(&GDestroyObjectStatic.DestroyType, &Widened, sizeof(Widened));
                Client->SendServerDestroyObject(1u, GDestroyObjectStatic);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_LAUNCH): // RS case 06bd1a41
            {
                // Stack struct, every dword copied by integer moves.
                const FRpcServerLaunchPacket* Typed = static_cast<const FRpcServerLaunchPacket*>(Packet);
                FServerLaunchInfo Info;
                Info.ShipId = Typed->ShipId;
                Info.LaunchObjId = Typed->LaunchObjId;
                Info.LaunchPosIndex = Typed->LaunchPosIndex;
                std::memcpy(&Info.Orientation, &Typed->Orientation, sizeof(Info.Orientation));
                std::memcpy(&Info.Position, &Typed->Position, sizeof(Info.Position));
                Client->SendServerLaunch(1u, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_CHARSELECTVERIFIED): // RS case 06bd1918
            {
                // Stores +0x00, +0x08, +0x0c only; +0x04 stays unwritten as in retail -- the
                // game reader (005ab380) never reads it.
                // The double moves as two integer dwords.
                const FRpcServerCharSelectVerifiedPacket* Typed =
                    static_cast<const FRpcServerCharSelectVerifiedPacket*>(Packet);
                FServerCharSelectVerifiedInfo Info;
                Info.VibeHandle = static_cast<int>(Typed->RepInstance);
                std::memcpy(&Info.ServerTime, &Typed->Time, sizeof(Info.ServerTime));
                Client->SendServerCharSelectVerified(1u, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_ACTIVATEOBJECT): // RS case 06bd249b
            {
                // Module globals 06c1389c/06c138a0/06c138a4; only the byte of +0x04 is
                // written, its three pad bytes stay zero.
                const FRpcServerActivateObjectPacket* Typed =
                    static_cast<const FRpcServerActivateObjectPacket*>(Packet);
                static FPayloadServerActivateObject GActivateObjectStatic;
                GActivateObjectStatic.ObjectId = Typed->ObjectId;
                GActivateObjectStatic.bActivate = Typed->bActivate;
                GActivateObjectStatic.ActivateParam = Typed->ActivateParam;
                Client->SendServerActivateObject(1u, GActivateObjectStatic);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_LAND): // RS case 06bd246c
            {
                const FRpcServerLandPacket* Typed = static_cast<const FRpcServerLandPacket*>(Packet);
                FServerLandInfo Info;
                Info.ShipId = Typed->Field_0x00;
                Info.DockId = Typed->Field_0x04;
                Info.BaseId = Typed->Field_0x08;
                Client->SendServerLand(1u, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETSTARTROOM): // RS case 06bd1970
            {
                const FRpcServerSetStartRoomPacket* Typed =
                    static_cast<const FRpcServerSetStartRoomPacket*>(Packet);
                Client->SendServerSetStartRoom(1u, Typed->Field_0x00, Typed->Field_0x04);
                break;
            }
            // GF list cases 0x0e..0x1f: every IClient argument is read straight off the
            // packet, client id literal 1; the packet stays owned by the caller (its
            // destructor frees a blob's Data).
            case FlPacketOpcode(FLPACKET_SERVER_GFCOMPLETENEWSBROADCASTLIST): // RS case 06bd19d6
                Client->SendServerGFCompleteNewsBroadcastList(1u,
                    static_cast<FRpcServerGfCompleteNewsBroadcastListPacket*>(Packet)->Value);
                break;
            case FlPacketOpcode(FLPACKET_SERVER_GFCOMPLETECHARLIST): // RS case 06bd195a
                Client->SendServerGFCompleteCharList(1u,
                    static_cast<FRpcServerGfCompleteCharListPacket*>(Packet)->Value);
                break;
            case FlPacketOpcode(FLPACKET_SERVER_GFCOMPLETEMISSIONCOMPUTERLIST): // RS case 06bd19a4
                Client->SendServerGFCompleteMissionComputerList(1u,
                    static_cast<FRpcServerGfCompleteMissionComputerListPacket*>(Packet)->Value);
                break;
            case FlPacketOpcode(FLPACKET_SERVER_GFCOMPLETESCRIPTBEHAVIORLIST): // RS case 06bd19ef
                Client->SendServerGFCompleteScriptBehaviorList(1u,
                    static_cast<FRpcServerGfCompleteScriptBehaviorListPacket*>(Packet)->Value);
                break;
            case FlPacketOpcode(FLPACKET_SERVER_GFCOMPLETEAMBIENTSCRIPTLIST): // RS case 06bd19bd
                Client->SendServerGFCompleteAmbientScriptList(1u,
                    static_cast<FRpcServerGfCompleteAmbientScriptListPacket*>(Packet)->Value);
                break;
            case FlPacketOpcode(FLPACKET_SERVER_GFDESTROYNEWSBROADCAST): // RS case 06bd29e9
            {
                const FRpcServerGfDestroyNewsBroadcastPacket* Typed =
                    static_cast<const FRpcServerGfDestroyNewsBroadcastPacket*>(Packet);
                Client->SendServerGFDestroyNewsBroadcast(1u, Typed->Field_0x00, Typed->Field_0x04);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_GFDESTROYCHARACTER): // RS case 06bd198a
            {
                const FRpcServerGfDestroyCharacterPacket* Typed =
                    static_cast<const FRpcServerGfDestroyCharacterPacket*>(Packet);
                Client->SendServerGFDestroyCharacter(1u, Typed->Field_0x00, Typed->Field_0x04);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_GFDESTROYMISSIONCOMPUTER): // RS case 06bd2ab0
            {
                const FRpcServerGfDestroyMissionComputerPacket* Typed =
                    static_cast<const FRpcServerGfDestroyMissionComputerPacket*>(Packet);
                Client->SendServerGFDestroyMissionComputer(1u, Typed->Field_0x00, Typed->Field_0x04);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_GFDESTROYSCRIPTBEHAVIOR): // RS case 06bd2a76
            {
                const FRpcServerGfDestroyScriptBehaviorPacket* Typed =
                    static_cast<const FRpcServerGfDestroyScriptBehaviorPacket*>(Packet);
                Client->SendServerGFDestroyScriptBehavior(1u, Typed->Field_0x00, Typed->Field_0x04);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_GFDESTROYAMBIENTSCRIPT): // RS case 06bd2a23
            {
                const FRpcServerGfDestroyAmbientScriptPacket* Typed =
                    static_cast<const FRpcServerGfDestroyAmbientScriptPacket*>(Packet);
                Client->SendServerGFDestroyAmbientScript(1u, Typed->Field_0x00, Typed->Field_0x04);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_GFSCRIPTBEHAVIOR): // RS case 06bd2a59: (1, Data +0x24, Size +0x20)
            {
                const FRpcServerGfScriptBehaviorPacket* Typed =
                    static_cast<const FRpcServerGfScriptBehaviorPacket*>(Packet);
                Client->SendServerGFScriptBehavior(1u, Typed->Data, Typed->Size);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_GFUPDATEMISSIONCOMPUTER): // RS case 06bd2a93: (1, Data +0x24, Size +0x20)
            {
                const FRpcServerGfUpdateMissionComputerPacket* Typed =
                    static_cast<const FRpcServerGfUpdateMissionComputerPacket*>(Packet);
                Client->SendServerGFUpdateMissionComputer(1u, Typed->Data, Typed->Size);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_GFUPDATENEWSBROADCAST): // RS case 06bd29cc: (1, Data +0x24, Size +0x20)
            {
                const FRpcServerGfUpdateNewsBroadcastPacket* Typed =
                    static_cast<const FRpcServerGfUpdateNewsBroadcastPacket*>(Packet);
                Client->SendServerGFUpdateNewsBroadcast(1u, Typed->Data, Typed->Size);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_GFUPDATEAMBIENTSCRIPT): // RS case 06bd2a06: (1, Data +0x20, Size +0x24)
            {
                const FRpcServerGfUpdateAmbientScriptPacket* Typed =
                    static_cast<const FRpcServerGfUpdateAmbientScriptPacket*>(Packet);
                Client->SendServerGFUpdateAmbientScript(1u, Typed->Data, Typed->Size);
                break;
            }
            // ===== SERVER 0x20..0x3f =====
            case FlPacketOpcode(FLPACKET_SERVER_GFMISSIONVENDORACCEPTANCE): // RS case 06bd2acd: (1, Data +0x24, Size +0x20)
            {
                const FRpcServerGfMissionVendorAcceptancePacket* Typed =
                    static_cast<const FRpcServerGfMissionVendorAcceptancePacket*>(Packet);
                Client->SendServerGFMissionVendorAcceptance(1u, Typed->Data, Typed->Size);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SYSTEM_SWITCH_OUT): // RS 06bd2441
            {
                // Retail copies both dwords into a module global (06c137d4 / 06c137d8)
                // and passes its address; a static FServerSystemSwitchOutInfo (two dwords)
                // has the same storage.
                static FServerSystemSwitchOutInfo GSystemSwitchOut;
                const FRpcServerSystemSwitchOutPacket* Typed =
                    static_cast<const FRpcServerSystemSwitchOutPacket*>(Packet);
                GSystemSwitchOut.ShipId = Typed->Field_0x00;
                GSystemSwitchOut.GateId = Typed->Field_0x04;
                Client->SendServerSystemSwitchOut(1u, GSystemSwitchOut);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SYSTEM_SWITCH_IN): // RS 06bd23b6
            {
                // Function-local static 06c138c8 (guard bit 4 of 06c138b8): nine dwords copied
                // by integer moves.
                const FRpcServerSystemSwitchInPacket* Typed =
                    static_cast<const FRpcServerSystemSwitchInPacket*>(Packet);
                static FPayloadServerSystemSwitchIn GSystemSwitchInStatic;
                std::memcpy(&GSystemSwitchInStatic.Position, &Typed->Position,
                    sizeof(GSystemSwitchInStatic.Position));
                std::memcpy(&GSystemSwitchInStatic.Orientation, &Typed->Orientation,
                    sizeof(GSystemSwitchInStatic.Orientation));
                GSystemSwitchInStatic.ShipId = Typed->ShipId;
                GSystemSwitchInStatic.JumpObjTypeBits = Typed->JumpObjTypeBits;
                Client->SendServerSystemSwitchIn(1u, GSystemSwitchInStatic);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETSHIPARCH): // RS 06bd1944
            {
                Client->SendServerSetShipArch(1u, static_cast<const FRpcSetShipArchPacket*>(Packet)->Value);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETEQUIPMENT): // RS 06bd18ae
            {
                // The vector is built first, the handle second (destroyed first, 06bdc790).
                EquipDescVector Vector;
                FRpcServerEquipHandle Handle;
                static_cast<const FRpcServerSetEquipmentPacket*>(Packet)->ExtractEquipment(&Handle);
                RpcServerEquipVectorFromHandle(&Handle, &Vector);
                Client->SendServerSetEquipment(1u, Vector);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETCARGO): // RS 06bd180f
            {
                EquipDescVector Vector;
                FRpcServerEquipHandle Handle;
                static_cast<const FRpcServerSetCargoPacket*>(Packet)->ExtractEquipment(&Handle);
                RpcServerEquipVectorFromHandle(&Handle, &Vector);
                Client->SendServerSetCargo(1u, Vector);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_GFUPDATECHAR): // RS case 06bd29b2: (1, Data +0x24, Size +0x20)
            {
                const FRpcServerGfUpdateCharPacket* Typed =
                    static_cast<const FRpcServerGfUpdateCharPacket*>(Packet);
                Client->SendServerGFUpdateChar(1u, Typed->Data, Typed->Size);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_REQUESTCREATESHIPRESP): // RS 06bd1aa1
            {
                const FRpcServerRequestCreateShipRespPacket* Typed =
                    static_cast<const FRpcServerRequestCreateShipRespPacket*>(Packet);
                // The wire byte as the bool: the callee keeps one byte (rpclocal 06d8a158),
                // so only values above 1 would differ.
                Client->SendServerRequestCreateShipResp(1u, Typed->bSuccess != 0, Typed->ShipId);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_CREATELOOT): // RS 06bd12b8
            {
                // An empty list sends nothing. Count = the low byte of the list size; the
                // new(Count * 0x48) array is not null-tested (a failed allocation is written
                // through, reproduced on purpose), its elements get the inlined ctor 06bd4200,
                // and the records are copied by integer moves at a byte index. Deviation:
                // retail leaves the request's pad bytes and each element's +0x47
                // uninitialised; the port zeroes them.
                const FRpcServerCreateLootPacket* Typed =
                    static_cast<const FRpcServerCreateLootPacket*>(Packet);
                const std::list<FRpcServerLootRecord>& Loot = Typed->Loot;
                if (Loot.size() == 0) break;
                const unsigned char Count = static_cast<unsigned char>(Loot.size());
                FServerCreateLootRequest Request;
                Request.Count = Count;
                Request.bMarkedBy = 0;
                Request.bTemporary = 0;
                Request.Pad0a[0] = 0;
                Request.Pad0a[1] = 0;
                Request.Items = nullptr;
                Request.OwnerId = Typed->OwnerId;
                Request.Kind = Typed->Kind;
                Request.Pad11[0] = 0;
                Request.Pad11[1] = 0;
                Request.Pad11[2] = 0;
                FServerLootDesc* Items = static_cast<FServerLootDesc*>(
                    ::operator new(static_cast<unsigned int>(Count) * 0x48u, std::nothrow));
                if (Items != nullptr) {
                    for (unsigned int Index = 0; Index < Count; ++Index) {
                        FServerLootDesc& Desc = Items[Index];
                        Desc.LootId = 0;
                        Desc.ContentsNickname = 0;
                        std::memset(&Desc.ContentsHitPts, 0, 4u);
                        Desc.Units = 0;
                        Desc.ContainerNickname = 0;
                        std::memset(&Desc.HitPts, 0, 4u);
                        Desc.NameOverride = 0;
                        Desc.Field_0x44 = 1;
                        Desc.bTemporary = 0;
                        Desc.bMarkedBy = 0;
                        Desc.Pad47 = 0;
                    }
                }
                Request.Items = Items;
                unsigned char At = 0;
                for (std::list<FRpcServerLootRecord>::const_iterator It = Loot.begin();
                    It != Loot.end(); ++It, ++At) {
                    const FRpcServerLootRecord& Record = *It;
                    FServerLootDesc& Desc = Items[At];
                    Desc.LootId = Record.LootId;
                    Desc.ContentsNickname = Record.ContentsNickname;
                    std::memcpy(&Desc.ContentsHitPts, &Record.ContentsHitPts, 4u);
                    Desc.Units = Record.Units;
                    Desc.ContainerNickname = Record.ContainerNickname;
                    std::memcpy(&Desc.HitPts, &Record.HitPts, 4u);
                    Desc.NameOverride = Record.NameOverride;
                    std::memcpy(&Desc.Position, &Record.Position, sizeof(Desc.Position));
                    std::memcpy(&Desc.Velocity, &Record.Velocity, sizeof(Desc.Velocity));
                    std::memcpy(&Desc.Orientation, &Record.Orientation, sizeof(Desc.Orientation));
                    Desc.bMarkedBy = Record.bMarkedBy;
                    Desc.bTemporary = Record.bTemporary;
                }
                Client->SendServerCreateLoot(1u, Request);
                ::operator delete(Items);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETREPUTATION): // RS 06bd14bf
            {
                // VibeHandle = +0x58, Affiliation = +0x5c, Attitude = +0x30 (integer moves);
                // the packet's relations are deep-copied into a stack list (06bddef0) and
                // flattened into an array freed after the send. Deviation: retail leaves +0x0c,
                // the pad bytes and the rest of DisplayName uninitialised; the port zeroes them.
                const FRpcServerSetReputationPacket* Typed =
                    static_cast<const FRpcServerSetReputationPacket*>(Packet);
                FServerSetReputationInfo Info;
                Info.Relations = nullptr;
                Info.RelationCount = 0;
                Info.Rank = 0;
                Info.Pad_0x15[0] = 0;
                Info.Pad_0x15[1] = 0;
                Info.Pad_0x15[2] = 0;
                for (unsigned int Index = 0; Index < 24u; ++Index) {
                    Info.DisplayName[Index] = L'\0';
                }
                std::list< ::Reputation::Relation*> Local;
                Info.VibeHandle = static_cast<int>(Typed->Target);
                Info.Affiliation = Typed->Affiliation;
                std::memcpy(&Info.Attitude, &Typed->Attitude, 4u);
                Info.NameA.unflatten(Typed->NameA.Data, Typed->NameA.Size);
                Info.NameB.unflatten(Typed->NameB.Data, Typed->NameB.Size);
                if (Typed->DisplayName != nullptr) {
                    std::wcscpy(Info.DisplayName, Typed->DisplayName);
                } else {
                    Info.DisplayName[0] = L'\0';
                }
                RpcServerRelationListAppendCopy(Local, Typed->Relations);
                Info.RelationCount = static_cast<unsigned char>(Local.size());
                if (Local.size() > 0) {
                    Info.Relations = RpcServerRelationArrayFromList(Local);
                } else {
                    Info.Relations = nullptr;
                }
                Client->SendServerSetReputation(1u, Info);
                if (Info.RelationCount > 0) {
                    ::operator delete(Info.Relations);
                }
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_ADJUSTATTITUDE): // RS 06bd1407
            {
                // The float moves through x87.
                const FRpcServerAdjustAttitudePacket* Typed =
                    static_cast<const FRpcServerAdjustAttitudePacket*>(Packet);
                FPayloadServerAdjustAttitude Local;
                Local.VibeHandle = Typed->VibeHandle;
                RpcServerX87CopyFloat(&Local.Attitude, &Typed->Attitude);
                Client->SendServerAdjustAttitude(1u, Local);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETGROUPFEELINGS): // RS 06bd16ab
            {
                // strncpy of the name with no bound to the 0x10-byte field (reproduced on
                // purpose); relations deep-copied (06bdd430) and flattened as in 0x29
                // (06bd1706). The array is never freed after the call (a retail leak,
                // reproduced on purpose). Deviation: retail leaves Relations uninitialised for
                // an empty list and the pad bytes always; the port zeroes them.
                const FRpcServerSetGroupFeelingsPacket* Typed =
                    static_cast<const FRpcServerSetGroupFeelingsPacket*>(Packet);
                FServerSetGroupFeelingsInfo Info;
                std::list< ::Reputation::Relation*> Local;
                std::memset(Info.Name, 0, sizeof(Info.Name)); // bytes past the copy: retail garbage
                std::strncpy(Info.Name, Typed->Name, Typed->NameSize);
                RpcServerRelationListAppendCopy(Local, Typed->Relations);
                Info.RelationCount = static_cast<unsigned char>(Local.size());
                Info.Relations = nullptr;
                Info.Pad_0x15[0] = 0;
                Info.Pad_0x15[1] = 0;
                Info.Pad_0x15[2] = 0;
                if (Local.size() > 0) {
                    Info.Relations = RpcServerRelationArrayFromList(Local);
                }
                Client->SendServerSetGroupFeelings(1u, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_CREATEMINE): // RS 06bd1bb1
            {
                // Integer moves: info +0x00..+0x08 = +0x20..+0x28, word +0x0c = +0x2c,
                // Position = +0x40, Orientation = +0x30, Velocity = +0x4c, +0x38 = +0x58,
                // +0x44 = +0x64. Deviation: retail leaves Pad0e uninitialised; the port zeroes
                // it. The 1 goes BY VALUE.
                const FRpcServerCreateMinePacket* Typed =
                    static_cast<const FRpcServerCreateMinePacket*>(Packet);
                FServerCreateProjectileInfo Info;
                Info.ObjId = Typed->ObjId;
                Info.ArchetypeNickname = Typed->ArchetypeNickname;
                Info.OwnerId = Typed->OwnerId;
                Info.SubObjId = Typed->SubObjId;
                Info.Pad0e = 0;
                std::memcpy(&Info.Position, &Typed->Position, sizeof(Info.Position));
                std::memcpy(&Info.Orientation, &Typed->Orientation, sizeof(Info.Orientation));
                std::memcpy(&Info.Velocity, &Typed->Velocity, sizeof(Info.Velocity));
                std::memcpy(&Info.AngularVelocity, &Typed->AngularVelocity, sizeof(Info.AngularVelocity));
                Info.OwnerPlayer = Typed->OwnerPlayer;
                Client->SendServerCreateMine(1u, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_CREATECOUNTER): // RS 06bd1dd9
            {
                // Integer moves. Pad0e (+0x0e..+0x0f) stays unwritten as in retail; the game
                // reader (0053cd70) never reads it. Client id 1 goes BY REFERENCE.
                const FRpcServerCreateCounterPacket* Typed =
                    static_cast<const FRpcServerCreateCounterPacket*>(Packet);
                FServerCreateProjectileInfo Info;
                Info.ObjId = Typed->ObjId;
                Info.ArchetypeNickname = Typed->ArchetypeNickname;
                Info.OwnerId = Typed->OwnerId;
                Info.SubObjId = Typed->SubObjId;
                std::memcpy(&Info.Position, &Typed->Position, sizeof(Info.Position));
                std::memcpy(&Info.Orientation, &Typed->Orientation, sizeof(Info.Orientation));
                std::memcpy(&Info.Velocity, &Typed->Velocity, sizeof(Info.Velocity));
                std::memcpy(&Info.AngularVelocity, &Typed->AngularVelocity, sizeof(Info.AngularVelocity));
                Info.OwnerPlayer = Typed->OwnerPlayer;
                unsigned int One = 1u;
                Client->SendServerCreateCounter(One, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETADDITEM): // RS 06bd1add
            {
                // Setters in native order. The Mission byte is a raw move into EquipDesc
                // +0x18, not a bool test.
                const FRpcServerSetAddItemPacket* Typed =
                    static_cast<const FRpcServerSetAddItemPacket*>(Packet);
                EquipDesc Desc;
                Desc.set_arch_id(Typed->ArchId);
                Desc.set_id(static_cast<unsigned short>(Typed->Id));
                Desc.set_count(Typed->Count);
                Desc.set_status(Typed->Health);
                Desc.set_equipped(Typed->bEquipped);
                std::memcpy(&Desc.bMission, &Typed->bMission, 1u);
                if (Typed->HardpointSize != 0) {
                    CacheString Interned;
                    Interned.Value = StringAlloc(Typed->Hardpoint, false);
                    Desc.set_hardpoint(Interned);
                } else {
                    Desc.make_internal();
                }
                Client->SendServerSetAddItem(1u, static_cast<int>(Typed->AddResult), Desc);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETREMOVEITEM): // RS 06bd1b90
            {
                const FRpcServerSetRemoveItemPacket* Typed =
                    static_cast<const FRpcServerSetRemoveItemPacket*>(Packet);
                Client->SendServerSetRemoveItem(1u, static_cast<int>(Typed->Field_0x08),
                    static_cast<unsigned short>(Typed->Field_0x00), static_cast<int>(Typed->Field_0x04));
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETCASH): // RS 06bd1f9a
            {
                // The client id goes BY REFERENCE.
                unsigned int One = 1u;
                Client->SendServerSetCash(One,
                    static_cast<int>(static_cast<const FRpcServerSetCashPacket*>(Packet)->Value));
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_EXPLODEASTEROIDMINE): // RS 06bd1c5d
            {
                // Integer moves.
                const FRpcServerExplodeAsteroidMinePacket* Typed =
                    static_cast<const FRpcServerExplodeAsteroidMinePacket*>(Packet);
                FMineExplosion Info;
                Info.MineId = Typed->MineId;
                std::memcpy(&Info.Offset.x, &Typed->Position.x, 4u);
                std::memcpy(&Info.Offset.y, &Typed->Position.y, 4u);
                std::memcpy(&Info.Offset.z, &Typed->Position.z, 4u);
                Client->SendServerExplodeAsteroidMine(1u, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_REQUESTSPACESCRIPT): // RS 06bd1d6d
            {
                // +0x000 = [+0x20], +0x108..+0x110 = [+0x28..+0x30], +0x114 = ([+0x34] == 0) + 1.
                // The rest of the name buffer, and all of it when Name is null, stays unwritten
                // as in retail. On this receiver Name is always null (the RS decoder 06bf48f0
                // never allocates it), so the game's SendServerRequestSpaceScript (0053d9d0)
                // hands the uninitialised buffer to the space-script camera: nondeterministic,
                // reproduced on purpose.
                const FRpcServerRequestSpaceScriptPacket* Typed =
                    static_cast<const FRpcServerRequestSpaceScriptPacket*>(Packet);
                FServerRequestSpaceScriptInfo Info;
                Info.ObjectId = Typed->ObjectId;
                Info.PosXBits = Typed->Position[0];
                Info.PosYBits = Typed->Position[1];
                Info.PosZBits = Typed->Position[2];
                Info.CameraMode = (Typed->Mode == 0) ? kRequestSpaceScriptCameraIdentity : kRequestSpaceScriptCameraKeepTransform;
                if (Typed->Name != nullptr) {
                    unsigned int i = 0;
                    do {
                        Info.Name[i] = Typed->Name[i];
                    } while (Typed->Name[i++] != '\0');
                }
                Client->SendServerRequestSpaceScript(1u, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETMISSIONOBJECTIVESTATE): // RS 06bd24cf
            {
                // {dword +0x20, byte of +0x24, word +0x28 zero-extended}. Deviation: retail
                // leaves bytes +5..+7 uninitialised; the port zeroes them. Client id 1 BY
                // REFERENCE.
                const FRpcServerSetMissionObjectiveStatePacket* Typed =
                    static_cast<const FRpcServerSetMissionObjectiveStatePacket*>(Packet);
                FServerMissionObjectiveStateInfo Info;
                Info.MissionId = Typed->MissionId;
                Info.ObjectiveIndex = static_cast<unsigned char>(Typed->ObjectiveIndex);
                Info.Pad_0x05[0] = 0;
                Info.Pad_0x05[1] = 0;
                Info.Pad_0x05[2] = 0;
                Info.State = Typed->State;
                unsigned int One = 1u;
                Client->SendServerSetMissionObjectiveState(One, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_REPLACEMISSIONOBJECTIVE): // RS 06bd2584
            {
                // +0x00 = [+0x20], FmtStr at +0x04, byte +0x20 = [+0x24]; client id 1 BY
                // REFERENCE. Deviation: retail leaves +0x1c and the pad bytes uninitialised;
                // the port zeroes them.
                const FRpcServerReplaceMissionObjectivePacket* Typed =
                    static_cast<const FRpcServerReplaceMissionObjectivePacket*>(Packet);
                FServerReplaceMissionObjectiveInfo Info;
                Info.MissionId = Typed->MissionId;
                Info.ObjectiveType = 0;
                Info.ObjectiveIndex = static_cast<unsigned char>(Typed->ObjectiveIndex);
                Info.Pad_0x21[0] = 0;
                Info.Pad_0x21[1] = 0;
                Info.Pad_0x21[2] = 0;
                Info.Text.unflatten(Typed->TextData, Typed->TextSize);
                unsigned int One = 1u;
                Client->SendServerReplaceMissionObjective(One, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETMISSIONOBJECTIVES): // RS 06bd27b9
            {
                // Count = [+0x38], byte +0x3c = [+0x24]. Entries are 0x1c bytes; the packet's
                // i-th record (06bdac30) is used with no null test. Deviation: retail leaves
                // Entries uninitialised when Count is 0; the port uses null.
                const FRpcServerSetMissionObjectivesPacket* Typed =
                    static_cast<const FRpcServerSetMissionObjectivesPacket*>(Packet);
                FServerSetMissionObjectivesInfo Info;
                Info.MissionId = Typed->MissionId;
                Info.Entries = nullptr;
                Info.TextA.unflatten(Typed->TextA.Data, Typed->TextA.Size);
                Info.TextB.unflatten(Typed->TextB.Data, Typed->TextB.Size);
                Info.MissionType = static_cast<unsigned char>(Typed->MissionType);
                Info.Pad_0x3d[0] = 0;
                Info.Pad_0x3d[1] = 0;
                Info.Pad_0x3d[2] = 0;
                Info.Count = Typed->Count;
                if (Info.Count != 0) {
                    Info.Entries = new (std::nothrow) FServerMissionObjectiveEntry[Info.Count];
                    for (unsigned int Index = 0; Index < Info.Count; ++Index) {
                        const FRpcServerMissionObjectiveRecord* Record = Typed->ObjectiveAt(Index);
                        Info.Entries[Index].State = Record->State;
                        Info.Entries[Index].Text.unflatten(Record->Data, Record->Size);
                    }
                }
                unsigned int One = 1u;
                Client->SendServerSetMissionObjectives(One, Info);
                if (Info.Count != 0 && Info.Entries != nullptr) {
                    delete[] Info.Entries;
                }
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_CREATEGUIDED): // RS 06bd1e96
            {
                // Integer moves. Pad26/Pad36 stay unwritten as in retail; the game reader
                // (0053d080) never reads them. OwnerPlayer comes from packet +0x20, which never
                // crosses the wire (the decoder zeroes it): always 0, reproduced on purpose.
                const FRpcServerCreateGuidedPacket* Typed =
                    static_cast<const FRpcServerCreateGuidedPacket*>(Packet);
                FServerCreateGuidedInfo Info;
                Info.ObjId = Typed->ObjId;
                std::memcpy(&Info.Position, &Typed->Position, sizeof(Info.Position));
                std::memcpy(&Info.Orientation, &Typed->Orientation, sizeof(Info.Orientation));
                Info.TargetId = Typed->TargetId;
                Info.TargetSubObjId = Typed->TargetSubObjId;
                Info.ArchetypeNickname = Typed->ArchetypeNickname;
                Info.OwnerPlayer = Typed->OwnerPlayer;
                Info.OwnerId = Typed->OwnerId;
                Info.SubObjId = Typed->SubObjId;
                unsigned int One = 1u; // passed by reference
                Client->SendServerCreateGuided(One, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_ITEMTRACTORED): // RS 06bd1f2d
            {
                const FRpcServerItemTractoredPacket* Typed =
                    static_cast<const FRpcServerItemTractoredPacket*>(Packet);
                FPayloadServerItemTractored Info;
                Info.LootId = Typed->LootId;
                Info.ShipId = Typed->ShipId;
                Info.TractorEquipId = Typed->TractorEquipId;
                Info.UnitsTaken = Typed->UnitsTaken;
                Client->SendServerItemTractored(1u, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SCANNOTIFY): // RS 06bd1ac0
            {
                const FRpcServerScanNotifyPacket* Typed = static_cast<const FRpcServerScanNotifyPacket*>(Packet);
                Client->SendServerScanNotify(1u, Typed->Field_0x00, Typed->Field_0x04);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_REPAIROBJECT): // RS 06bd2c45
            {
                // {dword +0x20, word of +0x24, dword +0x28}. Deviation: retail leaves the pad
                // word at +0x06 uninitialised; the port zeroes it.
                const FRpcServerRepairObjectPacket* Typed =
                    static_cast<const FRpcServerRepairObjectPacket*>(Packet);
                FServerRepairObjectInfo Info;
                Info.ObjectId = Typed->ObjectId;
                Info.EquipId = static_cast<unsigned short>(Typed->EquipId);
                Info.Pad_0x06 = 0;
                std::memcpy(&Info.Points, &Typed->Points, 4u);
                Client->SendServerRepairObject(1u, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_REMOTEOBJECTCARGOUPDATE): // RS 06bd2b04
            {
                // 06bdaa80 extracts, 06bd5130 converts; client id 1 BY REFERENCE, ObjectId
                // +0x28, Flag byte +0x24.
                const FRpcServerRemoteObjectCargoUpdatePacket* Typed =
                    static_cast<const FRpcServerRemoteObjectCargoUpdatePacket*>(Packet);
                EquipDescVector Vector;
                FRpcServerEquipHandle Handle;
                Typed->ExtractEquipment(&Handle);
                RpcServerEquipVectorFromHandle(&Handle, &Vector);
                unsigned int One = 1u;
                Client->SendServerRemoteObjectCargoUpdate(One, Typed->ObjectId, Vector, Typed->Flag);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETNUMKILLS): // RS 06bd2c76
            {
                unsigned int One = 1u; // passed by reference
                Client->SendServerSetNumKills(One,
                    static_cast<int>(static_cast<const FRpcServerSetNumKillsPacket*>(Packet)->Value));
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETMISSIONSUCCESSES): // RS 06bd2ca0
            {
                unsigned int One = 1u; // passed by reference
                Client->SendServerSetMissionSuccesses(One,
                    static_cast<int>(static_cast<const FRpcServerSetMissionSuccessesPacket*>(Packet)->Value));
                break;
            }
            // ===== end SERVER 0x20..0x3f =====
            // ===== SERVER 0x40..0x5b =====
            // Cases of HandlePacket 06bd1260 (switch 06bd12b1). Where retail passes &one, the 1
            // is a stack local written right before the call.
            case FlPacketOpcode(FLPACKET_SERVER_SETMISSIONFAILURES): // caseD_40 06bd2cca
            {
                unsigned int One = 1u;
                Client->SendServerSetMissionFailures(One,
                    static_cast<int>(static_cast<FRpcServerSetMissionFailuresPacket*>(Packet)->Value));
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_BURNFUSE): // caseD_41 06bd2cf4: integer copies into a stack record
            {
                const FRpcServerBurnFusePacket* Typed = static_cast<FRpcServerBurnFusePacket*>(Packet);
                FServerBurnFuseInfo Info = {};
                Info.SourceId = Typed->SourceId;
                Info.ObjectId = Typed->ObjectId;
                Info.FuseId = Typed->FuseId;
                Info.SubObjId = Typed->SubObjId;
                std::memcpy(&Info.AtT, &Typed->AtT, 4u);
                std::memcpy(&Info.Lifetime, &Typed->Lifetime, 4u);
                Info.bLit = Typed->bLit;
                unsigned int One = 1u;
                Client->SendServerBurnFuse(One, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_CREATESOLAR): // caseD_42 06bd2d52
            {
                // A function-local static CSolarCreateInfo (guard bit 8 of 06c138b8, atexit
                // destructor 06bd4370) filled by the block reader 06bd52a0. Outside single
                // player the vibe is created, named, affiliated and given its attitude (the
                // float pushed as is). Then the inlined CEqObjCreateInfo::clear() (06bd3050).
                FRpcServerCreateSolarPacket* Typed = static_cast<FRpcServerCreateSolarPacket*>(Packet);
                static CSolarCreateInfo Static;
                RpcServerCreateObjectRead(&Typed->Fields, &Static);
                Static.bDestructible = Typed->bDestructible;
                Static.bMarkedBy = Typed->bMarkedBy;
                if (!SinglePlayer()) {
                    FmtStr NameA;
                    FmtStr NameB;
                    NameA.unflatten(Typed->NameA, Typed->NameASize);
                    NameB.unflatten(Typed->NameB, Typed->NameBSize);
                    Reputation::Vibe::EnsureExists(Typed->Fields.VibeGroupId);
                    Reputation::Vibe::SetName(Typed->Fields.VibeGroupId, NameA, NameB, nullptr);
                    Reputation::Vibe::SetAffiliation(Typed->Fields.VibeGroupId, Typed->Affiliation, false);
                    Reputation::Vibe::SetAttitudeTowardsPlayer(Typed->Fields.VibeGroupId, Typed->Attitude);
                }
                Client->SendServerCreateSolar(1u, Static);
                Static.CEqObjCreateInfo::clear();
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SET_STORY_CUE): // caseD_43 06bd2631: the cue id by address
            {
                unsigned int CueId = static_cast<FRpcServerSetStoryCuePacket*>(Packet)->Value;
                unsigned int One = 1u;
                Client->SendServerSetStoryCue(One, CueId);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_REQUEST_RETURNED): // caseD_44 06bd1a08: +0x24 zero-, +0x25/+0x26 sign-extended
            {
                const FRpcServerRequestReturnedPacket* Typed =
                    static_cast<FRpcServerRequestReturnedPacket*>(Packet);
                unsigned int One = 1u;
                Client->SendServerRequestReturned(One, Typed->ObjectId,
                    static_cast<int>(Typed->RequestType),
                    static_cast<int>(static_cast<signed char>(Typed->Response)),
                    static_cast<int>(static_cast<signed char>(Typed->DockPort)));
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SET_MISSION_MESSAGE): // caseD_45 06bd2713
            {
                FRpcServerSetMissionMessagePacket* Typed = static_cast<FRpcServerSetMissionMessagePacket*>(Packet);
                CSetMissionMessage Info;
                Info.Text.unflatten(Typed->TextData, Typed->TextSize);
                Info.Type = Typed->Type;
                unsigned int One = 1u;
                Client->SendServerSetMissionMessage(One, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_MARKOBJ): // caseD_46 06bd2510: the mark byte zero-extended
            {
                const FRpcServerMarkObjPacket* Typed = static_cast<FRpcServerMarkObjPacket*>(Packet);
                unsigned int One = 1u;
                Client->SendServerMarkObj(One, Typed->ObjectId, static_cast<int>(Typed->MarkType));
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_CFGINTERFACENOTIFICATION): // caseD_47 06bd253f: the byte normalized to 0/1
            {
                const FRpcServerCfgInterfaceNotificationPacket* Typed =
                    static_cast<FRpcServerCfgInterfaceNotificationPacket*>(Packet);
                FPayloadServerCfgInterfaceNotification Info = {};
                Info.NotifType = Typed->NotifType;
                Info.Param = Typed->Param;
                Info.bEnable = static_cast<unsigned char>(Typed->bEnable != 0 ? 1 : 0);
                unsigned int One = 1u;
                Client->SendServerCfgInterfaceNotification(One, Info);
                break;
            }
                        case FlPacketOpcode(FLPACKET_SERVER_SETCOLLISIONGROUPS):
            {
                // SETCOLLISIONGROUPS, caseD_48 06bd3102: the packet's own list (+0x20), no copy.
                FRpcServerSetCollisionGroupsPacket* Typed =
                    static_cast<FRpcServerSetCollisionGroupsPacket*>(Packet);
                Client->SendServerSetCollisionGroups(1u, Typed->Groups);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETHULLSTATUS): // the float's dword pushed as is
            {
                // Bit copy, not an x87 load: retail pushes the packet's dword unchanged.
                float HullStatus;
                std::memcpy(&HullStatus, &static_cast<FRpcSetHullStatusPacket*>(Packet)->Value, 4u);
                Client->SendServerSetHullStatus(1u, HullStatus);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETGUIDEDTARGET): // caseD_4a 06bd313e: the word zero-extended
            {
                const FRpcServerSetGuidedTargetPacket* Typed =
                    static_cast<FRpcServerSetGuidedTargetPacket*>(Packet);
                Client->SendServerSetGuidedTarget(1u, Typed->GuidedId, Typed->TargetId,
                    Typed->TargetSubObjId);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SET_CAMERA): // caseD_4b 06bd2663: the quaternion through x87
            {
                const FRpcServerSetCameraPacket* Typed = static_cast<FRpcServerSetCameraPacket*>(Packet);
                FPayloadServerSetCamera Info;
                std::memcpy(&Info.Position.x, &Typed->Position.x, 4u);
                std::memcpy(&Info.Position.y, &Typed->Position.y, 4u);
                std::memcpy(&Info.Position.z, &Typed->Position.z, 4u);
                RpcServerX87CopyFloat(&Info.Orient.w, &Typed->Orientation.w);
                RpcServerX87CopyFloat(&Info.Orient.x, &Typed->Orientation.x);
                RpcServerX87CopyFloat(&Info.Orient.y, &Typed->Orientation.y);
                RpcServerX87CopyFloat(&Info.Orient.z, &Typed->Orientation.z);
                std::memcpy(&Info.Fov, &Typed->Fov, 4u);
                std::memcpy(&Info.Aspect, &Typed->Aspect, 4u);
                unsigned int One = 1u;
                Client->SendServerSetCamera(One, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_REVERT_CAMERA): // caseD_4c 06bd26f3
            {
                unsigned int One = 1u;
                Client->SendServerRevertCamera(One);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_LOADHINT): // caseD_4d 06bd1c93
            {
                // Only Kind 1 builds a hint: new(0x3c), COMMON's CostumeHint ctor and this
                // module's vftable 06c0b790 (FRpcLocalCostumeHint), then UnSerialize with no
                // null test (reproduced on purpose).
                FRpcServerLoadHintPacket* Typed = static_cast<FRpcServerLoadHintPacket*>(Packet);
                CostumeHint* Hint = nullptr;
                if (Typed->Kind == kLoadHintKindCostume) {
                    Hint = new (std::nothrow) FRpcLocalCostumeHint;
                    Hint->UnSerialize(Typed->Data, Typed->Size);
                }
                // The hint pointer goes out even when it is null (any Kind but 1). No
                // implementer null-tests it; the game's LAB_0053de70 reads Kind through it first.
                unsigned int One = 1u;
                Client->SendServerLoadHint(One, Hint);
                if (Hint != nullptr) {
                    delete Hint;
                }
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SETDIRECTIVE): // caseD_4e 06bd1d2b
            {
                // Retail seeds the local (06bd4230) and then wholly overwrites it with 0x25
                // dwords from packet +0x20, padding included; the seeding is not observable and
                // is not repeated here.
                FRpcServerSetDirectivePacket* Typed = static_cast<FRpcServerSetDirectivePacket*>(Packet);
                FPayloadServerSetDirective Info;
                std::memcpy(&Info, &Typed->Directive, 0x94u);
                unsigned int One = 1u;
                Client->SendServerSetDirective(One, Info);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SENDCOMM): // caseD_4f 06bd315c
            {
                // The costume goes through a stack copy (accessor 06bd4300); the other values
                // come from accessors 06bd42d0..06bd4350 (the count sign-extended); the float
                // goes through x87 (06bd4360); bReceiverIsOwnShip is the literal 0.
                FRpcServerSendCommPacket* Typed = static_cast<FRpcServerSendCommPacket*>(Packet);
                Costume Local;
                std::memcpy(&Local, &Typed->Body, sizeof(Costume));
                float Level;
                RpcServerX87CopyFloat(&Level, &Typed->Duration);
                Client->SendServerSendComm(1u, Typed->SenderId, Typed->ReceiverId, Typed->VoiceId,
                    Local, Typed->CaptionIds, Typed->TextIds,
                    static_cast<int>(Typed->TextCount), Typed->PermutationIds, Level, false);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_USE_ITEM): // caseD_51 06bd1f67: a stack FPayloadServerUseItem
            {
                const FRpcServerUseItemPacket* Typed = static_cast<FRpcServerUseItemPacket*>(Packet);
                FPayloadServerUseItem Info;
                Info.ObjectId = Typed->ObjectId;
                Info.EquipId = Typed->EquipId;
                Info.Count = Typed->Count;
                Client->SendServerUseItem(1u, Info);
                break;
            }
                        case FlPacketOpcode(FLPACKET_SERVER_PLAYERLIST):
            {
                // PLAYERLIST, caseD_52 06bd31f2: any other Kind does nothing.
                const FRpcServerPlayerListPacket* Typed =
                    static_cast<FRpcServerPlayerListPacket*>(Packet);
                if (Typed->Kind == kPlayerListKindClear) {
                    Client->SendServerPlayerListClear(1u);
                    break;
                }
                if (Typed->Kind == kPlayerListKindEntry) {
                    Client->SendServerPlayerListAdd(1u, Typed->Name, Typed->PlayerId,
                        Typed->bAlreadyPresent != 0);
                    break;
                }
                if (Typed->Kind == kPlayerListKindDrop) {
                    Client->SendServerPlayerListRemove(1u, Typed->PlayerId);
                    break;
                }
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_FORMATION_UPDATE): // caseD_53 06bd142f: Offset through x87
            {
                const FRpcServerFormationUpdatePacket* Typed =
                    static_cast<FRpcServerFormationUpdatePacket*>(Packet);
                Vector Local;
                RpcServerX87CopyFloat(&Local.x, &Typed->Offset.x);
                RpcServerX87CopyFloat(&Local.y, &Typed->Offset.y);
                RpcServerX87CopyFloat(&Local.z, &Typed->Offset.z);
                Client->SendServerFormationUpdate(1u, Typed->LeaderId, Local);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_MISCOBJUPDATE): // caseD_54 06bd325d
            {
                // Flags is re-read from the packet for every test except the 0x01 / 0x04 pair,
                // which tests one snapshot. The text and the vector go by address into the
                // packet; the word +0x2c zero-extended.
                FRpcServerMiscObjUpdatePacket* Typed = static_cast<FRpcServerMiscObjUpdatePacket*>(Packet);
                if ((Typed->Flags & kMiscObjUpdateFlagAffiliation) != 0) {
                    Client->SendServerMiscObjUpdateAffiliation(1u, Typed->ObjectId, Typed->Affiliation);
                }
                const unsigned short Snapshot = Typed->Flags;
                if ((Snapshot & kMiscObjUpdateFlagGroupId) != 0) {
                    if ((Snapshot & kMiscObjUpdateFlagPlayerId) != 0) {
                        Client->SendServerMiscObjUpdatePlayerGroup(1u, Typed->PlayerId, Typed->GroupId);
                    } else {
                        Client->SendServerMiscObjUpdateObjectGroup(1u, Typed->ObjectId, Typed->GroupId);
                    }
                }
                if ((Typed->Flags & kMiscObjUpdateFlagMission) != 0) {
                    Client->SendServerMiscObjUpdateMission(1u, Typed->MissionId, Typed->MissionSetBy);
                }
                if ((Typed->Flags & kMiscObjUpdateFlagText) != 0) {
                    Client->SendServerMiscObjUpdateMotd(1u, Typed->Text);
                }
                if ((Typed->Flags & kMiscObjUpdateFlagRank) != 0) {
                    Client->SendServerMiscObjUpdateRank(1u, Typed->PlayerId, static_cast<int>(Typed->Rank));
                }
                if ((Typed->Flags & kMiscObjUpdateFlagSystemId) != 0) {
                    Client->SendServerMiscObjUpdateSystem(1u, Typed->PlayerId, Typed->SystemId);
                }
                if ((Typed->Flags & kMiscObjUpdateFlagIds) != 0) {
                    Client->SendServerMiscObjUpdateFormation(1u, Typed->Ids);
                }
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_OBJECTCARGOUPDATE): // caseD_55 06bd2bad: Common EquipDesc setters
            {
                const FRpcServerObjectCargoUpdatePacket* Typed =
                    static_cast<FRpcServerObjectCargoUpdatePacket*>(Packet);
                EquipDesc Desc;
                Desc.set_id(Typed->EquipId);
                Desc.set_arch_id(Typed->ArchId);
                Desc.set_count(static_cast<int>(Typed->Count));
                Desc.set_status(Typed->Status);
                Desc.make_internal();
                Desc.set_equipped(0);
                Desc.bMission = (Typed->bMission != 0);
                unsigned int One = 1u;
                Client->SendServerObjectCargoUpdate(One, Typed->ObjectId, Desc);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SENDNNMESSAGE):
            {
                Client->SendServerSendNNMessage(1u, static_cast<FRpcServerSendNNMessagePacket*>(Packet)->Value);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_SET_MUSIC): // caseD_57 06bd1462
            {
                const FRpcServerSetMusicPacket* Typed = static_cast<FRpcServerSetMusicPacket*>(Packet);
                // Bit copy, not an x87 load: retail pushes the packet's dword unchanged.
                float Fade;
                std::memcpy(&Fade, &Typed->Fade, 4u);
                // The wire byte as the bool: the callee keeps one byte (rpclocal 06d8c6ad),
                // so only values above 1 would differ.
                Client->SendServerSetMusic(1u, Typed->SpaceMusic, Typed->DangerMusic, Typed->BattleMusic,
                    Typed->OverrideMusic, Typed->bNoLoop != 0, Fade);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_CANCEL_MUSIC):
            {
                Client->SendServerCancelMusic(1u);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_PLAY_SOUND_EFFECT):
            {
                Client->SendServerPlaySoundEffect(1u, static_cast<FRpcServerPlaySoundEffectPacket*>(Packet)->Value);
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_GFMISSIONVENDORWHYEMPTY): // caseD_5a 06bd2aea: the byte zero-extended
            {
                Client->SendServerGFMissionVendorWhyEmpty(1u, static_cast<unsigned int>(
                    static_cast<FRpcServerGfMissionVendorWhyEmptyPacket*>(Packet)->Value));
                break;
            }
            case FlPacketOpcode(FLPACKET_SERVER_MISSIONSAVEA): // caseD_5b 06bd2a40
            {
                Client->SendServerMissionSaveA(1u, static_cast<FRpcServerMissionSaveAPacket*>(Packet)->Value);
                break;
            }
            // ===== end SERVER 0x40..0x5b =====
            // Opcodes outside 1..0x5b and the jump-table holes go to caseD_9 06bd3c0f, which only
            // releases the packet; here the release is the caller's (IServerImpl::HandlePacket).
            default:
                break;
        }

}
