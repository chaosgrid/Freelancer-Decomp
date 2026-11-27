// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include <windows.h>
#include "ClientImpl.h"
#include "rpc_packet.h"
#include "rpc_packet_codec.h"
#include "dacom.h"
#include "common_server_types.h"
#include "common_server.h"
#include "common_goods.h"

static SLoginInfo GStaticLoginInfo = {};
static SCreateCharacterInfo GStaticCreateCharacterInfo = {};

IServer* RemoteClientGetServer();
IChatServer* RemoteClientGetChatServer();

static void DispatchClientPacket(unsigned int ClientId, FPacketBase* Packet)
{
    IServer* Server = RemoteClientGetServer();

    unsigned int Channel = Packet->GetChannel();
    unsigned int Opcode = Packet->GetOpcode();

    if (Channel == kPacketChannelCommon) {
        switch (Opcode) {
        case FlPacketOpcode(FLPACKET_COMMON_UPDATEOBJECT):
        {
            const FRpcUpdateObjectPacket* Wire = static_cast<const FRpcUpdateObjectPacket*>(Packet);
            SSPObjUpdateInfo Info;
            Info.ObjectId = Wire->ObjectId;
            Info.Orientation = Wire->Orientation;
            Info.Position = Wire->Position;
            Info.State = Wire->State;
            const float* TimeSource = &Wire->UpdateTime;
            const float* FieldSource = &Wire->Throttle;
            double* TimeDestination = &Info.Timestamp;
            unsigned int* FieldDestination = &Info.ThrottleBits;
            float StoredTime;
            float StoredField;
            // Preserve both FST float intermediates and the native x87 order.
            __asm {
                mov eax, TimeSource
                fld dword ptr [eax]
                fst dword ptr StoredTime
                mov eax, FieldSource
                fld dword ptr [eax]
                fst dword ptr StoredField
                fxch st(1)
                mov eax, TimeDestination
                fstp qword ptr [eax]
                mov eax, FieldDestination
                fstp dword ptr [eax]
            }
            Server->SPObjUpdate(Info, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_FIREWEAPON):
        {
            const FRpcFireWeaponPacket* Wire = static_cast<const FRpcFireWeaponPacket*>(Packet);
            XFireWeaponInfo Info;
            Info.ObjectId = Wire->ObjectId;
            Info.TargetPos = Wire->TargetPos;
            Info.Hardpoints = Wire->Hardpoints;
            Server->FireWeapon(ClientId, Info);
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_SETTARGET):
        {
            const FRpcSetTargetPacket* Wire = static_cast<const FRpcSetTargetPacket*>(Packet);
            XSetTarget Info;
            Info.ObjectId = Wire->ObjectId;
            Info.SubObjId = Wire->SubObjId;
            Info.TargetObjectId = Wire->TargetId;
            Info.TargetSubObjId = Wire->TargetSubObjId;
            Server->SetTarget(ClientId, Info);
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_CHATMSG):
        {
            const FRpcChatMsgPacket* Wire = static_cast<const FRpcChatMsgPacket*>(Packet);
            RemoteClientGetChatServer()->SubmitChat(
                CHAT_ID(static_cast<unsigned short>(ClientId)), Wire->DataSize,
                Wire->Data, CHAT_ID(Wire->TargetId), -1);
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_ACTIVATEEQUIP):
        {
            const FRpcActivateEquipPacket* Wire = static_cast<const FRpcActivateEquipPacket*>(Packet);
            XActivateEquip Info;
            Info.ObjectId = Wire->ObjectId;
            Info.EquipId = Wire->EquipId;
            Info.bActivate = (Wire->bActive != 0);
            Server->ActivateEquip(ClientId, Info);
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_ACTIVATECRUISE):
        {
            const FRpcActivateCruisePacket* Wire = static_cast<const FRpcActivateCruisePacket*>(Packet);
            XActivateCruise Info;
            Info.ObjectId = Wire->ObjectId;
            Info.bActivate = (Wire->bActive != 0);
            Info.bDisruptCruise = (Wire->bDisruptCruise != 0) ? 1u : 0u;
            Server->ActivateCruise(ClientId, Info);
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_GOTRADELANE):
        {
            const FRpcGoTradeLanePacket* Wire = static_cast<const FRpcGoTradeLanePacket*>(Packet);
            XGoTradelane Info;
            Info.ObjectId = Wire->ObjectId;
            Info.TradelaneRing1 = Wire->TradelaneRing1;
            Info.TradelaneRing2 = Wire->TradelaneRing2;
            Info.bMidLane = (Wire->bMidLane != 0);
            Server->GoTradelane(ClientId, Info);
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_STOPTRADELANE):
        {
            const FRpcStopTradeLanePacket* Wire = static_cast<const FRpcStopTradeLanePacket*>(Packet);
            Server->StopTradelane(ClientId, Wire->ShipId, Wire->TradeLaneRing1, Wire->TradeLaneRing2);
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_SET_WEAPON_GROUP):
        {
            const FRpcSetWeaponGroupPacket* Wire = static_cast<const FRpcSetWeaponGroupPacket*>(Packet);
            Server->SetWeaponGroup(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE):
        {
            const FRpcPlayerTradePacket* Wire = static_cast<const FRpcPlayerTradePacket*>(Packet);
            switch (Wire->Action)
            {
            case kPlayerTradeInitiateTrade:
                Server->InitiateTrade(ClientId, Wire->TargetPlayer);
                break;
            case kPlayerTradeTerminateTrade:
                Server->TerminateTrade(ClientId, Wire->Reason);
                break;
            case kPlayerTradeSetTradeMoney:
                Server->SetTradeMoney(ClientId, static_cast<unsigned long>(Wire->Money));
                break;
            case kPlayerTradeAddTradeEquip:
            {
                EquipDesc Equip;
                Equip.set_arch_id(Wire->EquipArchId);
                Equip.set_count(Wire->EquipCount);
                Equip.set_status(Wire->EquipHealth);
                Equip.set_id(Wire->EquipId);
                Server->AddTradeEquip(ClientId, Equip);
                break;
            }
            case kPlayerTradeDelTradeEquip:
            {
                EquipDesc Equip;
                Equip.set_arch_id(Wire->EquipArchId);
                Equip.set_count(Wire->EquipCount);
                Equip.set_id(Wire->EquipId);
                Server->DelTradeEquip(ClientId, Equip);
                break;
            }
            case kPlayerTradeRequestTrade:
                Server->RequestTrade(ClientId, Wire->TargetPlayer);
                break;
            case kPlayerTradeAcceptTrade:
                // The native decoder already produces a canonical 0/1 byte.
                Server->AcceptTrade(ClientId, Wire->bAccepted != 0);
                break;
            case kPlayerTradeStopTradeRequest:
                Server->StopTradeRequest(Wire->TargetPlayer);
                break;
            default:
                break;
            }
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_SET_VISITED_STATE):
        {
            const FRpcSetVisitedStatePacket* Wire = static_cast<const FRpcSetVisitedStatePacket*>(Packet);
            Server->SetVisitedState(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_JETTISONCARGO):
        {
            const FRpcJettisonCargoPacket* Wire = static_cast<const FRpcJettisonCargoPacket*>(Packet);
            XJettisonCargo Info;
            Info.ObjectId = Wire->ObjectId;
            Info.ItemId = Wire->ItemId;
            Info.Count = Wire->Count;
            Server->JettisonCargo(ClientId, Info);
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_ACTIVATETHRUSTERS):
        {
            const FRpcActivateThrustersPacket* Wire = static_cast<const FRpcActivateThrustersPacket*>(Packet);
            XActivateThrusters Info;
            Info.ObjectId = Wire->ObjectId;
            Info.bActivate = (Wire->bActive != 0);
            Server->ActivateThrusters(ClientId, Info);
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_REQUEST_BEST_PATH):
        {
            const FRpcRequestBestPathPacket* Wire = static_cast<const FRpcRequestBestPathPacket*>(Packet);
            Server->RequestBestPath(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_REQUEST_GROUP_POSITIONS):
        {
            const FRpcRequestGroupPositionsPacket* Wire = static_cast<const FRpcRequestGroupPositionsPacket*>(Packet);
            Server->RequestGroupPositions(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_REQUEST_PLAYER_STATS):
        {
            const FRpcRequestPlayerStatsPacket* Wire = static_cast<const FRpcRequestPlayerStatsPacket*>(Packet);
            Server->RequestPlayerStats(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_SET_MISSION_LOG):
        {
            const FRpcSetMissionLogPacket* Wire = static_cast<const FRpcSetMissionLogPacket*>(Packet);
            Server->SetMissionLog(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_REQUEST_RANK_LEVEL):
        {
            const FRpcRequestRankLevelPacket* Wire = static_cast<const FRpcRequestRankLevelPacket*>(Packet);
            Server->RequestRankLevel(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_POP_UP_DIALOG):
        {
            const FRpcPopUpDialogPacket* Wire = static_cast<const FRpcPopUpDialogPacket*>(Packet);
            Server->PopUpDialog(ClientId, Wire->Buttons);
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_SET_INTERFACE_STATE):
        {
            const FRpcSetInterfaceStatePacket* Wire = static_cast<const FRpcSetInterfaceStatePacket*>(Packet);
            Server->SetInterfaceState(ClientId, Wire->Data, static_cast<int>(Wire->DataSize));
            break;
        }
        case FlPacketOpcode(FLPACKET_COMMON_TRACTOROBJECTS):
        {
            const FRpcTractorObjectsPacket* Wire = static_cast<const FRpcTractorObjectsPacket*>(Packet);
            XTractorObjects Info;
            Info.ObjectId = Wire->ObjectId;
            Info.TractorEquipId = Wire->TractorEquipId;
            Info.TargetIds = Wire->TargetIds;
            Server->TractorObjects(ClientId, Info);
            break;
        }
        default:
            break;
        }
    } else if (Channel == kPacketChannelClient) {
        switch (Opcode) {
        case FlPacketOpcode(FLPACKET_CLIENT_LOGIN):
            // Slot 18 - Login
            {
                const FRpcClientLoginPacket* Wire = static_cast<const FRpcClientLoginPacket*>(Packet);
                wchar_t AccountName[256];
                wchar_t SecondString[256];
                memcpy(AccountName, Wire->AccountName, sizeof(AccountName));
                memcpy(SecondString, Wire->Password, sizeof(SecondString));
                const unsigned char CreateFlag = Wire->CreateFlag;
                wcsncpy(GStaticLoginInfo.AccountName, AccountName, 0x28);
                wcsncpy(GStaticLoginInfo.Password, SecondString, 0x10);
                *reinterpret_cast<unsigned char*>(&GStaticLoginInfo.CreateFlag) = CreateFlag;
                GStaticLoginInfo.AccountName[39] = L'\0';
                GStaticLoginInfo.Password[15] = L'\0';
                Server->Login(GStaticLoginInfo, ClientId);
            }
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_MUNCOLLISION):
            // Slot 53 - SPMunitionCollision
            {
                const FRpcMunitionCollisionPacket* Wire = static_cast<const FRpcMunitionCollisionPacket*>(Packet);
                SSPMunitionCollisionInfo Info;
                Info.ProjectileArchId = Wire->ProjectileArchId;
                Info.AttackerId = Wire->AttackerId;
                Info.ObjectId = Wire->ObjectId;
                Info.SubObjId = Wire->SubObjId;
                std::memcpy(&Info.HitPosX, &Wire->HitPosition.x, 4u);
                std::memcpy(&Info.HitPosY, &Wire->HitPosition.y, 4u);
                std::memcpy(&Info.HitPosZ, &Wire->HitPosition.z, 4u);
                Server->SPMunitionCollision(Info, ClientId);
            }
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_REQUESTLAUNCH):
            // Slot 48 - PlayerLaunch
        {
            const FRpcRequestLaunchPacket* Wire = static_cast<const FRpcRequestLaunchPacket*>(Packet);
            Server->PlayerLaunch(Wire->Value, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQUESTCHARINFO):
            // Slot 19 - CharacterInfoReq
            Server->CharacterInfoReq(ClientId, false);
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_SELECTCHARACTER):
            // Slot 20 - CharacterSelect
        {
            const FRpcSelectCharacterPacket* Wire = static_cast<const FRpcSelectCharacterPacket*>(Packet);
            Server->CharacterSelect(Wire->CharacterId, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_ENTERBASE):
            // Slot 35 - BaseEnter
        {
            const FRpcEnterBasePacket* Wire = static_cast<const FRpcEnterBasePacket*>(Packet);
            Server->BaseEnter(Wire->Value, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQUESTBASEINFO):
            // Slot 39 - BaseInfoRequest
        {
            const FRpcRequestBaseInfoPacket* Wire = static_cast<const FRpcRequestBaseInfoPacket*>(Packet);
            // The Server/BaseDB endpoint observes only zero versus nonzero.
            Server->BaseInfoRequest(Wire->Id, ClientId, Wire->bFull != 0);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQUESTLOCATIONINFO):
            // Slot 40 - LocationInfoRequest
        {
            const FRpcRequestLocationInfoPacket* Wire = static_cast<const FRpcRequestLocationInfoPacket*>(Packet);
            Server->LocationInfoRequest(Wire->Id, ClientId, Wire->bFull != 0);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_GFREQUESTSHIPINFO):
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE):
            // Slot 47 - SystemSwitchOutComplete
        {
            const FRpcSystemSwitchOutCompletePacket* Wire = static_cast<const FRpcSystemSwitchOutCompletePacket*>(Packet);
            Server->SystemSwitchOutComplete(Wire->Value, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_OBJCOLLISION):
            // Slot 55 - SPObjCollision
            {
                const FRpcObjectCollisionPacket* Wire = static_cast<const FRpcObjectCollisionPacket*>(Packet);
                SSPObjCollisionInfo Info;
                Info.TargetObjectId = Wire->TargetObjectId;
                Info.TargetSubObjId = Wire->TargetSubObjId;
                Info.Pad_0x06 = 0;
                Info.ObjectId = Wire->ObjectId;
                Info.SubObjId = Wire->SubObjId;
                std::memcpy(&Info.DamageBits, &Wire->Damage, 4u);
                Server->SPObjCollision(Info, ClientId);
            }
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_EXITBASE):
            // Slot 36 - BaseExit
        {
            const FRpcExitBasePacket* Wire = static_cast<const FRpcExitBasePacket*>(Packet);
            Server->BaseExit(Wire->Value, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_ENTERLOCATION):
            // Slot 37 - LocationEnter
        {
            const FRpcEnterLocationPacket* Wire = static_cast<const FRpcEnterLocationPacket*>(Packet);
            Server->LocationEnter(Wire->Value, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_EXITLOCATION):
            // Slot 38 - LocationExit
        {
            const FRpcExitLocationPacket* Wire = static_cast<const FRpcExitLocationPacket*>(Packet);
            Server->LocationExit(Wire->Value, ClientId);
            break;
        }
        
        case FlPacketOpcode(FLPACKET_CLIENT_REQUESTCREATESHIP):
            Server->RequestCreateShip(ClientId);
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_GFGOODSELL):
        {
            const FRpcGfGoodSellPacket* Wire = static_cast<const FRpcGfGoodSellPacket*>(Packet);
            SGFGoodSellInfo Info;
            Info.BaseId = Wire->Field_0x00;
            Info.GoodId = Wire->Field_0x04;
            Info.Count = Wire->Field_0x08;
            Server->GFGoodSell(Info, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_GFGOODBUY):
        {
            const FRpcGfGoodBuyPacket* Wire = static_cast<const FRpcGfGoodBuyPacket*>(Packet);
            SGFGoodBuyInfo Info;
            Info.BaseId = Wire->Field_0x00;
            Info.Null04 = Wire->Field_0x04;
            Info.GoodId = Wire->Field_0x08;
            Info.Count = Wire->Field_0x0c;
            Server->GFGoodBuy(Info, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_GFSELECTOBJECT):
        {
            const FRpcGfSelectObjectPacket* Wire = static_cast<const FRpcGfSelectObjectPacket*>(Packet);
            Server->GFObjSelect(Wire->Value, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_MISSIONRESPONSE):
        {
            const FRpcMissionResponsePacket* Wire = static_cast<const FRpcMissionResponsePacket*>(Packet);
            Server->MissionResponse(Wire->OfferId, Wire->OfferKind, Wire->bAccepted, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQSHIPARCH):
        {
            const FRpcRequestShipArchPacket* Wire = static_cast<const FRpcRequestShipArchPacket*>(Packet);
            Server->ReqShipArch(Wire->Value, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQEQUIPMENT):
        {
            EquipDescList List;
            FRpcServerEquipHandle Snapshot;
            const FRpcRequestEquipmentPacket* Wire = static_cast<const FRpcRequestEquipmentPacket*>(Packet);
            Wire->ExtractEquipment(&Snapshot);
            if (Snapshot.Records != nullptr)
            {
                for (std::list<FRpcServerEquipRecord*>::const_iterator It = Snapshot.Records->begin();
                     It != Snapshot.Records->end(); ++It)
                {
                    const FRpcServerEquipRecord* Record = *It;
                    const EquipDesc Default;
                    List.Items.push_back(Default);
                    EquipDesc& Desc = List.Items.back();
                    Desc.set_arch_id(Record->ArchId);
                    Desc.set_count(Record->Count);
                    Desc.set_status(Record->Health);
                    Desc.set_id(Record->Id);
                    Desc.set_equipped(Record->bEquipped);
                    // Native ignores the record's bMission byte here.
                    if (Record->HardpointSize != 0)
                    {
                        CacheString Hardpoint;
                        Hardpoint.Value = StringAlloc(Record->Hardpoint, false);
                        Desc.set_hardpoint(Hardpoint);
                    }
                    else
                    {
                        Desc.make_internal();
                    }
                }
            }
            Server->ReqEquipment(List, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQCARGO):
        {
            EquipDescList List;
            FRpcServerEquipHandle Snapshot;
            const FRpcRequestCargoPacket* Wire = static_cast<const FRpcRequestCargoPacket*>(Packet);
            Wire->ExtractEquipment(&Snapshot);
            if (Snapshot.Records != nullptr)
            {
                for (std::list<FRpcServerEquipRecord*>::const_iterator It = Snapshot.Records->begin();
                     It != Snapshot.Records->end(); ++It)
                {
                    const FRpcServerEquipRecord* Record = *It;
                    const EquipDesc Default;
                    List.Items.push_back(Default);
                    EquipDesc& Desc = List.Items.back();
                    Desc.set_arch_id(Record->ArchId);
                    Desc.set_count(Record->Count);
                    Desc.set_status(Record->Health);
                    Desc.set_id(Record->Id);
                    Desc.set_equipped(Record->bEquipped);
                    // Native ignores the record's bMission byte here.
                    if (Record->HardpointSize != 0)
                    {
                        CacheString Hardpoint;
                        Hardpoint.Value = StringAlloc(Record->Hardpoint, false);
                        Desc.set_hardpoint(Hardpoint);
                    }
                    else
                    {
                        Desc.make_internal();
                    }
                }
            }
            Server->ReqCargo(List, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQADDITEM):
        {
            const FRpcRequestAddItemPacket* Wire = static_cast<const FRpcRequestAddItemPacket*>(Packet);
            Server->ReqAddItem(Wire->ArchId, Wire->Hardpoint, Wire->Count, Wire->Health,
                               Wire->bMounted, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQREMOVEITEM):
        {
            const FRpcRequestRemoveItemPacket* Wire = static_cast<const FRpcRequestRemoveItemPacket*>(Packet);
            Server->ReqRemoveItem(static_cast<unsigned short>(Wire->EquipId), Wire->Count, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQMODIFYITEM):
        {
            const FRpcRequestModifyItemPacket* Wire = static_cast<const FRpcRequestModifyItemPacket*>(Packet);
            Server->ReqModifyItem(static_cast<unsigned short>(Wire->EquipId), Wire->Hardpoint, Wire->Count,
                                  Wire->Health, Wire->bMounted, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQSETCASH):
        {
            const FRpcRequestSetCashPacket* Wire = static_cast<const FRpcRequestSetCashPacket*>(Packet);
            Server->ReqSetCash(Wire->Value, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQCHANGECASH):
        {
            const FRpcRequestChangeCashPacket* Wire = static_cast<const FRpcRequestChangeCashPacket*>(Packet);
            Server->ReqChangeCash(Wire->Value, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_SAVEGAME):
        {
            const FRpcSaveGamePacket* Wire = static_cast<const FRpcSaveGamePacket*>(Packet);
            Server->SaveGame(Wire->CharacterId, Wire->Description, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_MINEASTEROID):
        {
            const FRpcMineAsteroidPacket* Wire = static_cast<const FRpcMineAsteroidPacket*>(Packet);
            Vector Position;
            std::memcpy(&Position.x, &Wire->Position.x, 4u);
            std::memcpy(&Position.y, &Wire->Position.y, 4u);
            std::memcpy(&Position.z, &Wire->Position.z, 4u);
            Server->MineAsteroid(Wire->SystemId, Position, Wire->LootId, Wire->SecondaryLoot,
                                 Wire->Quantity, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_DBGCREATESHIP):
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Network\\FLServerPacketHandler.cpp", 0x26a,
                  "FLPACKET_CLIENT_DBGCREATESHIP encountered in release mode");
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_DBGLOADSYSTEM):
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Network\\FLServerPacketHandler.cpp", 0x217,
                  "FLPACKET_CLIENT_DBGLOADSYSTEM encountered in release mode");
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_DOCK):
        {
            const FRpcDockPacket* Wire = static_cast<const FRpcDockPacket*>(Packet);
            Server->Dock(Wire->Value, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_DBGDESTROYOBJECT):
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Network\\FLServerPacketHandler.cpp", 0x1e4,
                  "FLPACKET_CLIENT_DBGDESTROYOBJECT encountered in Final Release mode");
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_TRADERESPONSE):
        {
            const FRpcTradeResponsePacket* Wire = static_cast<const FRpcTradeResponsePacket*>(Packet);
            Server->TradeResponse(Wire->Data, static_cast<int>(Wire->DataSize), ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_CARGOSCAN):
        {
            const FRpcCargoScanPacket* Wire = static_cast<const FRpcCargoScanPacket*>(Packet);
            Server->SPScanCargo(Wire->Field_0x00, Wire->Field_0x04, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_DBGCONSOLE):
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Network\\FLServerPacketHandler.cpp", 0x204,
                  "FLPACKET_CLIENT_DBGCONSOLE encountered in release mode");
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_DBGFREESYSTEM):
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Network\\FLServerPacketHandler.cpp", 0x227,
                  "FLPACKET_CLIENT_DBGFREESYSTEM encountered in release mode");
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_SETMANEUVER):
        {
            const FRpcSetManeuverPacket* Wire = static_cast<const FRpcSetManeuverPacket*>(Packet);
            XSetManeuver Info;
            Info.ShipFrom = Wire->ShipFrom;
            Info.ObjectId = Wire->ObjectId;
            Info.Flag = Wire->Flag;
            Server->SetManeuver(ClientId, Info);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_DBGRELOCATE_SHIP):
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Network\\FLServerPacketHandler.cpp", 0x1f5,
                  "FLPACKET_CLIENT_DBGRELOCATE_SHIP encountered in Final Release mode");
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_REQUEST_EVENT):
        {
            const FRpcRequestEventPacket* Wire = static_cast<const FRpcRequestEventPacket*>(Packet);
            const int Tail = static_cast<signed char>(Wire->Field_0x10);
            Server->RequestEvent(static_cast<int>(Wire->EventType), Wire->ShipId, Wire->TargetId, Wire->SecondId,
                                 static_cast<unsigned long>(Tail), ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQUEST_CANCEL):
        {
            const FRpcRequestCancelPacket* Wire = static_cast<const FRpcRequestCancelPacket*>(Packet);
            const int Tail = static_cast<signed char>(Wire->Field_0x0c);
            Server->RequestCancel(static_cast<int>(Wire->EventType), Wire->ShipId, Wire->TargetId,
                                  static_cast<unsigned long>(Tail), ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_INTERFACEITEMUSED):
        {
            const FRpcInterfaceItemUsedPacket* Wire = static_cast<const FRpcInterfaceItemUsedPacket*>(Packet);
            Server->InterfaceItemUsed(ClientId, Wire->Value);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQCOLLISIONGROUPS):
        {
            const FRpcRequestCollisionGroupsPacket* Wire = static_cast<const FRpcRequestCollisionGroupsPacket*>(Packet);
            Server->ReqCollisionGroups(Wire->Groups, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_COMMCOMPLETE):
        {
            const FRpcCommCompletePacket* Wire = static_cast<const FRpcCommCompletePacket*>(Packet);
            Server->CommComplete(Wire->Field_0x00, Wire->Field_0x04, Wire->Field_0x08,
                                 static_cast<CommResult>(Wire->Field_0x0c));
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQUESTNEWCHARINFO):
            Server->NewCharacterInfoReq(ClientId);
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_CREATENEWCHAR):
        {
            const FRpcCreateNewCharacterPacket* Wire = static_cast<const FRpcCreateNewCharacterPacket*>(Packet);
            wcscpy(GStaticCreateCharacterInfo.Name, Wire->Name);
            GStaticCreateCharacterInfo.FactionId = Wire->FactionId;
            GStaticCreateCharacterInfo.BaseId = Wire->BaseId;
            GStaticCreateCharacterInfo.PackageId = Wire->PackageId;
            GStaticCreateCharacterInfo.PilotId = Wire->PilotId;
            Server->CreateNewCharacter(GStaticCreateCharacterInfo, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_DESTROYCHAR):
        {
            const FRpcDestroyCharacterPacket* Wire = static_cast<const FRpcDestroyCharacterPacket*>(Packet);
            Server->DestroyCharacter(Wire->CharacterId, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQHULLSTATUS):
        {
            const FRpcRequestHullStatusPacket* Wire = static_cast<const FRpcRequestHullStatusPacket*>(Packet);
            Server->ReqHullStatus(Wire->Value, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_GFGOODVAPORIZED):
        {
            const FRpcGfGoodVaporizedPacket* Wire = static_cast<const FRpcGfGoodVaporizedPacket*>(Packet);
            SGFGoodVaporizedInfo Info;
            Info.Field_0x00 = Wire->Field_0x00;
            Info.Field_0x04 = Wire->Field_0x04;
            Info.Field_0x08 = Wire->Field_0x08;
            Server->GFGoodVaporized(Info, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_BADLANDSOBJCOLLISION):
        {
            const FRpcBadlandsObjectCollisionPacket* Wire = static_cast<const FRpcBadlandsObjectCollisionPacket*>(Packet);
            SSPBadLandsObjCollisionInfo Info;
            Info.BadLandsObjId = Wire->BadLandsObjId;
            Info.ObjectId = Wire->ObjectId;
            Info.SubObjId = Wire->SubObjId;
            Server->SPBadLandsObjCollision(Info, ClientId);
        }
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_LAUNCHCOMPLETE):
        {
            const FRpcLaunchCompletePacket* Wire = static_cast<const FRpcLaunchCompletePacket*>(Packet);
            Server->LaunchComplete(Wire->Field_0x00, Wire->Field_0x04);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_HAIL):
        {
            const FRpcHailPacket* Wire = static_cast<const FRpcHailPacket*>(Packet);
            Server->Hail(Wire->Field_0x00, Wire->Field_0x04, Wire->Field_0x08);
            break;
        }
        
        case FlPacketOpcode(FLPACKET_CLIENT_REQUEST_USE_ITEM):
        {
            const FRpcRequestUseItemPacket* Wire = static_cast<const FRpcRequestUseItemPacket*>(Packet);
            SSPUseItem Info;
            Info.ObjectId = Wire->ObjectId;
            Info.ItemId = Wire->ItemId;
            Info.Count = Wire->Count;
            Server->SPRequestUseItem(Info, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_ABORT_MISSION):
        {
            const FRpcAbortMissionPacket* Wire = static_cast<const FRpcAbortMissionPacket*>(Packet);
            Server->AbortMission(ClientId, Wire->Value);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_SKIP_AUTOSAVE):
            Server->CharacterSkipAutosave(ClientId);
            break;
        case FlPacketOpcode(FLPACKET_CLIENT_JUMPINCOMPLETE):
        {
            const FRpcJumpInCompletePacket* Wire = static_cast<const FRpcJumpInCompletePacket*>(Packet);
            Server->JumpInComplete(Wire->Field_0x00, Wire->Field_0x04);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQINVINCIBILITY):
        {
            const FRpcRequestInvincibilityPacket* Wire = static_cast<const FRpcRequestInvincibilityPacket*>(Packet);
            Server->SPRequestInvincibility(Wire->ShipId, Wire->bInvincible,
                                           static_cast<InvincibilityReason>(Wire->Reason), ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_MISSIONSAVEB):
        {
            const FRpcMissionSaveBPacket* Wire = static_cast<const FRpcMissionSaveBPacket*>(Packet);
            Server->MissionSaveB(ClientId, Wire->Value);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_REQDIFFICULTYSCALE):
        {
            const FRpcRequestDifficultyScalePacket* Wire = static_cast<const FRpcRequestDifficultyScalePacket*>(Packet);
            Server->ReqDifficultyScale(Wire->Value, ClientId);
            break;
        }
        case FlPacketOpcode(FLPACKET_CLIENT_RTCDONE):
        {
            const FRpcRtcDonePacket* Wire = static_cast<const FRpcRtcDonePacket*>(Packet);
            Server->RTCDone(ClientId, Wire->Value);
            break;
        }
        default:
            break;
        }
    }
}

void HandleClientPacket(unsigned int ClientId, FPacketBase* Packet)
{
    DispatchClientPacket(ClientId, Packet);
    Packet->Release();
}
