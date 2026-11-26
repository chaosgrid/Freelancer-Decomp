// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include <windows.h>
#include "ClientImpl.h"
#include "../include/common_server_types.h"
#include "common_text.h"
#include "common_goods.h"
#include "rpc_iclient_args.h"
#include <windows.h>

#include "../include/rpc_packet.h"
#include "dalib_net.h"
#include "rpc_packet_codec.h"

CClientImpl Client;

CClientImpl::CClientImpl()
    : Serializer(nullptr), Host(nullptr), ClientProxies{}, NumClients(0),
      Field_0x214(0), bStarted(false) {}
CClientImpl::~CClientImpl() {}

IClient::IClient() {}
IClient::IClient(const IClient& Other) {}
IClient& IClient::operator=(const IClient& Other) { return *this; }

void CClientImpl::SendCommonFireWeapon(unsigned int ClientId, const XFireWeaponInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_FIREWEAPON), &Packet) == 0)
    {
        FRpcFireWeaponPacket* TypedPacket = static_cast<FRpcFireWeaponPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->TargetPos = Info.TargetPos;
        TypedPacket->Hardpoints = Info.Hardpoints;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonActivateEquip(unsigned int ClientId, const XActivateEquip& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_ACTIVATEEQUIP), &Packet) == 0)
    {
        FRpcActivateEquipPacket* TypedPacket = static_cast<FRpcActivateEquipPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->EquipId = Info.EquipId;
        TypedPacket->bActive = Info.bActivate;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonActivateCruise(unsigned int ClientId, const XActivateCruise& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_ACTIVATECRUISE), &Packet) == 0)
    {
        FRpcActivateCruisePacket* TypedPacket = static_cast<FRpcActivateCruisePacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->bActive = Info.bActivate;
        TypedPacket->bDisruptCruise = Info.bDisruptCruise;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonActivateThrusters(unsigned int ClientId, const XActivateThrusters& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_ACTIVATETHRUSTERS), &Packet) == 0)
    {
        FRpcActivateThrustersPacket* TypedPacket = static_cast<FRpcActivateThrustersPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->bActive = Info.bActivate;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonSetTarget(unsigned int ClientId, const XSetTarget& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SETTARGET), &Packet) == 0)
    {
        FRpcSetTargetPacket* TypedPacket = static_cast<FRpcSetTargetPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->TargetId = Info.TargetObjectId;
        TypedPacket->SubObjId = Info.SubObjId;
        TypedPacket->TargetSubObjId = Info.TargetSubObjId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonTractorObjects(unsigned int ClientId, const XTractorObjects& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_TRACTOROBJECTS), &Packet) == 0)
    {
        FRpcTractorObjectsPacket* TypedPacket = static_cast<FRpcTractorObjectsPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->TractorEquipId = Info.TractorEquipId;
        TypedPacket->TargetIds = Info.TargetIds;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonGoTradeLane(unsigned int ClientId, const XGoTradelane& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_GOTRADELANE), &Packet) == 0)
    {
        FRpcGoTradeLanePacket* TypedPacket = static_cast<FRpcGoTradeLanePacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->TradelaneRing1 = Info.TradelaneRing1;
        TypedPacket->TradelaneRing2 = Info.TradelaneRing2;
        TypedPacket->bMidLane = Info.bMidLane;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonStopTradeLane(unsigned int ClientId, unsigned int ShipId,
                                          unsigned int TradelaneRing1, unsigned int TradelaneRing2) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_STOPTRADELANE), &Packet) == 0)
    {
        // Retail ignores the last two arguments; the constructor zeroes both rings.
        static_cast<FRpcStopTradeLanePacket*>(Packet)->ShipId = ShipId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonJettisonCargo(unsigned int ClientId, const XJettisonCargo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_JETTISONCARGO), &Packet) == 0)
    {
        FRpcJettisonCargoPacket* TypedPacket = static_cast<FRpcJettisonCargoPacket*>(Packet);
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->ItemId = Info.ItemId;
        TypedPacket->Count = Info.Count;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::OnConnect(unsigned int ClientId) {
}

void CClientImpl::SendCommonUpdateObject(unsigned int ClientId, const FCommonUpdateObjectInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_UPDATEOBJECT), &Packet) == 0)
    {
        FRpcUpdateObjectPacket* TypedPacket = static_cast<FRpcUpdateObjectPacket*>(Packet);
        TypedPacket->State = Info.State;
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->Position = Info.Position;
        TypedPacket->Orientation = Info.Orientation;
        memcpy(&TypedPacket->UpdateTime, &Info.Timestamp, sizeof(float));
        memcpy(&TypedPacket->Throttle, &Info.Throttle, sizeof(float));
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonSetWeaponGroup(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SET_WEAPON_GROUP), &Packet) == 0)
    {
        FRpcSetWeaponGroupPacket* TypedPacket = static_cast<FRpcSetWeaponGroupPacket*>(Packet);
        // BorrowData keeps the caller's block with the ownership byte 0 (never writes or
        // frees it); its parameter is non-const only because the packet field is.
        TypedPacket->BorrowData(const_cast<unsigned char*>(Data), Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonSetVisitedState(unsigned int ClientId, unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SET_VISITED_STATE), &Packet) == 0)
    {
        FRpcSetVisitedStatePacket* TypedPacket = static_cast<FRpcSetVisitedStatePacket*>(Packet);
        TypedPacket->AdoptData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonRequestBestPath(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_REQUEST_BEST_PATH), &Packet) == 0)
    {
        FRpcRequestBestPathPacket* TypedPacket = static_cast<FRpcRequestBestPathPacket*>(Packet);
        TypedPacket->CopyData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonRequestPlayerStats(unsigned int ClientId, const unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_REQUEST_PLAYER_STATS), &Packet) == 0)
    {
        FRpcRequestPlayerStatsPacket* TypedPacket = static_cast<FRpcRequestPlayerStatsPacket*>(Packet);
        TypedPacket->CopyData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPopUpDialog(unsigned int ClientId, const FmtStr& Title, const FmtStr& Text,
                                        unsigned int Buttons) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_POP_UP_DIALOG), &Packet) == 0)
    {
        FRpcPopUpDialogPacket* TypedPacket = static_cast<FRpcPopUpDialogPacket*>(Packet);
        TypedPacket->SizeA = RpcGetFlattenedFmtStrSize(Title);
        TypedPacket->DataA = static_cast<unsigned char*>(::operator new(TypedPacket->SizeA, std::nothrow));
        Title.flatten(TypedPacket->DataA, TypedPacket->SizeA);
        TypedPacket->SizeB = RpcGetFlattenedFmtStrSize(Text);
        TypedPacket->DataB = static_cast<unsigned char*>(::operator new(TypedPacket->SizeB, std::nothrow));
        Text.flatten(TypedPacket->DataB, TypedPacket->SizeB);
        TypedPacket->Buttons = Buttons;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonRequestGroupPositions(unsigned int ClientId, unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_REQUEST_GROUP_POSITIONS), &Packet) == 0)
    {
        FRpcRequestGroupPositionsPacket* TypedPacket = static_cast<FRpcRequestGroupPositionsPacket*>(Packet);
        TypedPacket->AdoptData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonSetMissionLog(unsigned int ClientId, unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SET_MISSION_LOG), &Packet) == 0)
    {
        FRpcSetMissionLogPacket* TypedPacket = static_cast<FRpcSetMissionLogPacket*>(Packet);
        TypedPacket->AdoptData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonSetInterfaceState(unsigned int ClientId, unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SET_INTERFACE_STATE), &Packet) == 0)
    {
        FRpcSetInterfaceStatePacket* TypedPacket = static_cast<FRpcSetInterfaceStatePacket*>(Packet);
        TypedPacket->AdoptData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonRequestRankLevel(unsigned int ClientId, unsigned char* Data, unsigned int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_REQUEST_RANK_LEVEL), &Packet) == 0)
    {
        FRpcRequestRankLevelPacket* TypedPacket = static_cast<FRpcRequestRankLevelPacket*>(Packet);
        TypedPacket->AdoptData(Data, Size);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeInitiateTrade(unsigned int ClientId, unsigned int ShipId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = kPlayerTradeInitiateTrade;
        TypedPacket->TargetPlayer = ShipId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeTerminateTrade(unsigned int ClientId, int Reason) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = kPlayerTradeTerminateTrade;
        TypedPacket->Reason = Reason;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeAcceptTrade(unsigned int ClientId, unsigned int ShipId, bool bAccepted) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = kPlayerTradeAcceptTrade;
        TypedPacket->TargetPlayer = ShipId;
        TypedPacket->bAccepted = static_cast<unsigned char>(bAccepted);
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeSetTradeMoney(unsigned int ClientId, unsigned int ShipId, unsigned long Money) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = kPlayerTradeSetTradeMoney;
        TypedPacket->Money = static_cast<int>(Money);
        TypedPacket->TargetPlayer = ShipId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeAddTradeEquip(unsigned int ClientId, unsigned int ShipId, const EquipDesc& Item) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = kPlayerTradeAddTradeEquip;
        TypedPacket->EquipArchId = Item.ArchId;
        TypedPacket->EquipCount = Item.Count;
        const float* SourceHealth = &Item.Health;
        float* DestinationHealth = &TypedPacket->EquipHealth;
        __asm {
            mov eax, SourceHealth
            mov edx, DestinationHealth
            fld dword ptr [eax]
            fstp dword ptr [edx]
        }
        TypedPacket->TargetPlayer = ShipId;
        TypedPacket->EquipId = Item.Id;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeDelTradeEquip(unsigned int ClientId, unsigned int ShipId, const EquipDesc& Item) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = kPlayerTradeDelTradeEquip;
        TypedPacket->EquipArchId = Item.ArchId;
        TypedPacket->EquipCount = Item.Count;
        TypedPacket->TargetPlayer = ShipId;
        TypedPacket->EquipId = Item.Id;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeRequestTrade(unsigned int ClientId, unsigned int ShipId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = kPlayerTradeRequestTrade;
        TypedPacket->TargetPlayer = ShipId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::SendCommonPlayerTradeStopTradeRequest(unsigned int ClientId, unsigned int ShipId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) == 0)
    {
        FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
        TypedPacket->Action = kPlayerTradeStopTradeRequest;
        TypedPacket->TargetPlayer = ShipId;
        PushToClient(ClientId, Packet->AsRaw());
    }
}

void CClientImpl::DumpPacketStats(const char* Name) {
    RpcGetRemotePacketSerializer()->DumpStats(Name);
}

void CClientImpl::SendCommonChatMsg(CHAT_ID TargetId, CHAT_ID SenderId, unsigned long Size, const void* Data) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_CHATMSG), &Packet) == 0)
    {
        FRpcChatMsgPacket* TypedPacket = static_cast<FRpcChatMsgPacket*>(Packet);
        if (Data != nullptr && Size != 0)
        {
            TypedPacket->DataSize = Size;
            TypedPacket->Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
            memcpy(TypedPacket->Data, Data, Size);
        }
        else
        {
            TypedPacket->DataSize = 0;
            TypedPacket->Data = nullptr;
        }
        TypedPacket->TargetId = TargetId.Id;
        TypedPacket->SenderId = SenderId.Id;
        // The client id is the CHAT_ID's low word (.text:06d8c4ed).
        PushToClient(static_cast<unsigned short>(TargetId.Id), Packet->AsRaw());
    }
}

