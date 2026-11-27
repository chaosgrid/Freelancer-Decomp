// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#define RPCLOCAL_EXPORTS
#include <windows.h>
#include "../include/rpc_iserver.h"
#define GetChatClientInterface __NetworkGetChatClientInterface
#define GetChatServerInterface __NetworkGetChatServerInterface
#include "../include/network.h"
#undef GetChatClientInterface
#undef GetChatServerInterface
#include "../include/rpc_packet.h"
#include "../include/rpc_packet_codec.h"
#include "../include/rpc_server_channel_codec.h"
#include "../include/common_goods.h"
#include <string.h>
#include <cwchar>
#include <new>

namespace
{
    FPacketSerializer* GRemotePacketSerializer = nullptr;
}

// 06be12a0: access alone does not register any packet types or add a reference.
FPacketSerializer* RpcGetRemotePacketSerializer()
{
    if (GRemotePacketSerializer == nullptr) {
        GRemotePacketSerializer = new (std::nothrow) FPacketSerializer;
    }
    return GRemotePacketSerializer;
}

// Retail remoteserver.dll ??0IServerImpl@@QAE@XZ (.text:06bd11b0); Serializer (+0x8) starts null.
IServerImpl::IServerImpl() : Serializer(nullptr) {}
IServerImpl::IServerImpl(const IServerImpl& Other) { Serializer = Other.Serializer; }
IServerImpl& IServerImpl::operator=(const IServerImpl& Other) { Serializer = Other.Serializer; return *this; }
void IServerImpl::GetServerStats(ServerStats& Stats) {}
void IServerImpl::HandlePacket(CDAPacket* Packet) {
    extern void HandlePacket(unsigned int, FPacketBase*);
    FPacketBase* Wire = reinterpret_cast<FPacketBase*>(Packet);
    HandlePacket(1, Wire);
    Wire->Release();
}

FRpcRefObject::~FRpcRefObject() {}
unsigned int FRpcRefObject::Release() {
    if (UsageCount != 0 && --UsageCount == 0) {
        delete this;
        return 0;
    }
    return UsageCount;
}
FPacketSerializer::FPacketSerializer()
{
    // 06be1357: inside the construction EH region, as in retail.
    RpcServerBuildHardpointDictionary();
}
FPacketSerializer::~FPacketSerializer() {
    for (std::map<unsigned int, FPacketTypeInfo*>::iterator It = Types.begin(); It != Types.end(); ++It) {
        delete It->second;
    }
}

// 06bd9cd0 (no Ghidra function; LAB_06bd98a0 tail-jumps here). Retail: a packet that fails
// Allocate or Decode skips only its 2-byte id, and a failed Decode leaks the packet.
void RpcDispatchMsgs(IServerImpl* Server, const unsigned char* Data, unsigned int Size) {
    const unsigned char* Cursor = Data;
    unsigned int Remaining = Size;
    
    while (Remaining >= 2) {
        unsigned short PacketId = *(const unsigned short*)Cursor;
        unsigned int Channel = PacketId >> 8;
        unsigned int Opcode = PacketId & 0xff;
        
        Cursor += 2;
        Remaining -= 2;
        
        FPacketBase* Packet = nullptr;
        if (!Server->Serializer || Server->Serializer->Allocate(Channel, Opcode, &Packet) != 0)
            continue;
        unsigned int BytesRead = 0;
        if (Packet->Decode(Cursor, Remaining, &BytesRead) == 0) {
            Cursor += BytesRead;
            Remaining -= BytesRead;
            Server->HandlePacket(Packet->AsRaw());
        }
    }
}

// Retail remoteserver.dll FUN_06be1750, the same body as rpclocal's FUN_06d96300
// (src/rpclocal/rpc_transport.cpp). The base constructor leaves Serializer null; the
// concrete network Server's Startup creates it and installs the packet registries.
int FPacketSerializer::Allocate(unsigned int Channel, unsigned int Opcode, FPacketBase** OutPacket)
{
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

// 06bd8140. The packet owns its copy of all hardpoint IDs.
void IServerImpl::FireWeapon(unsigned int PlayerId, const XFireWeaponInfo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_FIREWEAPON), &Packet) != 0)
        return;
    FRpcFireWeaponPacket* TypedPacket = static_cast<FRpcFireWeaponPacket*>(Packet);
    TypedPacket->ObjectId = Info.ObjectId;
    TypedPacket->TargetPos = Info.TargetPos;
    TypedPacket->Hardpoints = Info.Hardpoints;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::ActivateEquip(unsigned int PlayerId, const XActivateEquip& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_ACTIVATEEQUIP), &Packet) != 0)
        return;
    FRpcActivateEquipPacket* TypedPacket = static_cast<FRpcActivateEquipPacket*>(Packet);
    TypedPacket->ObjectId = Info.ObjectId;
    TypedPacket->EquipId = Info.EquipId;
    TypedPacket->bActive = Info.bActivate;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::ActivateCruise(unsigned int PlayerId, const XActivateCruise& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_ACTIVATECRUISE), &Packet) != 0)
        return;
    FRpcActivateCruisePacket* TypedPacket = static_cast<FRpcActivateCruisePacket*>(Packet);
    TypedPacket->ObjectId = Info.ObjectId;
    TypedPacket->bActive = Info.bActivate;
    TypedPacket->bDisruptCruise = Info.bDisruptCruise;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::ActivateThrusters(unsigned int PlayerId, const XActivateThrusters& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_ACTIVATETHRUSTERS), &Packet) != 0)
        return;
    FRpcActivateThrustersPacket* TypedPacket = static_cast<FRpcActivateThrustersPacket*>(Packet);
    TypedPacket->ObjectId = Info.ObjectId;
    TypedPacket->bActive = Info.bActivate;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::SetTarget(unsigned int PlayerId, const XSetTarget& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SETTARGET), &Packet) != 0)
        return;
    FRpcSetTargetPacket* TypedPacket = static_cast<FRpcSetTargetPacket*>(Packet);
    TypedPacket->ObjectId = Info.ObjectId;
    TypedPacket->TargetId = Info.TargetObjectId;
    TypedPacket->SubObjId = Info.SubObjId;
    TypedPacket->TargetSubObjId = Info.TargetSubObjId;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::TractorObjects(unsigned int PlayerId, const XTractorObjects& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_TRACTOROBJECTS), &Packet) != 0)
        return;
    FRpcTractorObjectsPacket* TypedPacket = static_cast<FRpcTractorObjectsPacket*>(Packet);
    TypedPacket->ObjectId = Info.ObjectId;
    TypedPacket->TractorEquipId = Info.TractorEquipId;
    TypedPacket->TargetIds = Info.TargetIds;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::GoTradelane(unsigned int PlayerId, const XGoTradelane& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_GOTRADELANE), &Packet) != 0)
        return;
    FRpcGoTradeLanePacket* TypedPacket = static_cast<FRpcGoTradeLanePacket*>(Packet);
    TypedPacket->ObjectId = Info.ObjectId;
    TypedPacket->TradelaneRing1 = Info.TradelaneRing1;
    TypedPacket->TradelaneRing2 = Info.TradelaneRing2;
    TypedPacket->bMidLane = Info.bMidLane;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::StopTradelane(unsigned int SessionId, unsigned int ShipId, unsigned int TradelaneRing1, unsigned int TradelaneRing2) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_STOPTRADELANE), &Packet) != 0)
        return;
    FRpcStopTradeLanePacket* TypedPacket = static_cast<FRpcStopTradeLanePacket*>(Packet);
    TypedPacket->ShipId = ShipId;
    TypedPacket->TradeLaneRing1 = TradelaneRing1;
    TypedPacket->TradeLaneRing2 = TradelaneRing2;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::JettisonCargo(unsigned int PlayerId, const XJettisonCargo& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_JETTISONCARGO), &Packet) != 0)
        return;
    FRpcJettisonCargoPacket* TypedPacket = static_cast<FRpcJettisonCargoPacket*>(Packet);
    TypedPacket->ObjectId = Info.ObjectId;
    TypedPacket->ItemId = Info.ItemId;
    TypedPacket->Count = Info.Count;
    this->PushToServer(Packet->AsRaw());
}

bool IServerImpl::Startup(const SStartupInfo& Info) {
    return false;
}

void IServerImpl::Shutdown() {
}

int IServerImpl::Update() {
    return 1;
}

void IServerImpl::ElapseTime(float Dt) {
}

void IServerImpl::SetActiveConnection(EFLConnection Conn) {
}

// 06bd9df0: the exported base answers true without reading either connection.
bool IServerImpl::SwapConnections(EFLConnection ConnA, EFLConnection ConnB) {
    return true;
}

void IServerImpl::Connect(const char* ServerName, unsigned short* Port) {
}

void IServerImpl::DisConnect(unsigned int SessionId, EFLConnection Type) {
}

void IServerImpl::OnConnect(unsigned int SessionId) {
}

void IServerImpl::Login(const SLoginInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_LOGIN), &Packet) != 0)
        return;
    FRpcClientLoginPacket* TypedPacket = static_cast<FRpcClientLoginPacket*>(Packet);
    std::wcscpy(TypedPacket->AccountName, Info.AccountName);
    std::wcscpy(TypedPacket->Password, Info.Password);
    TypedPacket->CreateFlag = static_cast<unsigned char>(Info.CreateFlag);
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::CharacterInfoReq(unsigned int SessionId, bool bSendEmpty) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQUESTCHARINFO), &Packet) == 0) {
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::CharacterSelect(const CHARACTER_ID& CharId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_SELECTCHARACTER), &Packet) == 0) {
        FRpcSelectCharacterPacket* TypedPacket = static_cast<FRpcSelectCharacterPacket*>(Packet);
        TypedPacket->CharacterId = CharId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::NewCharacterInfoReq(unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQUESTNEWCHARINFO), &Packet) == 0) {
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::CreateNewCharacter(const SCreateCharacterInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_CREATENEWCHAR), &Packet) != 0)
        return;
    wchar_t Name[64];
    std::wcsncpy(Name, Info.Name, 64);
    const unsigned int FactionId = Info.FactionId;
    const unsigned int BaseId = Info.BaseId;
    const unsigned int PackageId = Info.PackageId;
    const unsigned int PilotId = Info.PilotId;
    Name[63] = L'\0';
    FRpcCreateNewCharacterPacket* TypedPacket = static_cast<FRpcCreateNewCharacterPacket*>(Packet);
    memcpy(TypedPacket->Name, Name, sizeof(Name));
    TypedPacket->FactionId = FactionId;
    TypedPacket->BaseId = BaseId;
    TypedPacket->PackageId = PackageId;
    TypedPacket->PilotId = PilotId;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::DestroyCharacter(const CHARACTER_ID& CharId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_DESTROYCHAR), &Packet) == 0) {
        FRpcDestroyCharacterPacket* TypedPacket = static_cast<FRpcDestroyCharacterPacket*>(Packet);
        TypedPacket->CharacterId = CharId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::CharacterSkipAutosave(unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_SKIP_AUTOSAVE), &Packet) == 0) {
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqShipArch(unsigned int ArchId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQSHIPARCH), &Packet) == 0) {
        FRpcRequestShipArchPacket* TypedPacket = static_cast<FRpcRequestShipArchPacket*>(Packet);
        TypedPacket->Value = ArchId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqHullStatus(float HullStatus, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQHULLSTATUS), &Packet) != 0)
        return;
    FRpcRequestHullStatusPacket* TypedPacket = static_cast<FRpcRequestHullStatusPacket*>(Packet);
    float* Destination = &TypedPacket->Value;
    __asm {
        fld HullStatus
        mov eax, Destination
        fstp dword ptr [eax]
    }
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::ReqCollisionGroups(const std::list<CollisionGroupDesc>& Groups, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQCOLLISIONGROUPS), &Packet) == 0) {
        FRpcRequestCollisionGroupsPacket* TypedPacket = static_cast<FRpcRequestCollisionGroupsPacket*>(Packet);
        TypedPacket->Groups = Groups;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqEquipment(const EquipDescList& Equipment, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQEQUIPMENT), &Packet) == 0) {
        FRpcServerEquipHandle Temporary;
        const std::list<EquipDesc>::const_iterator End = Equipment.Items.end();
        for (std::list<EquipDesc>::const_iterator It = Equipment.Items.begin(); It != End; ++It) {
            const EquipDesc& Desc = *It;
            FRpcServerEquipRecord* Record = static_cast<FRpcServerEquipRecord*>(
                ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
            if (Record != nullptr) {
                Record->ArchId = 0;
                Record->Count = 0;
                std::memset(&Record->Health, 0, 4u);
                Record->Id = 0;
                Record->bEquipped = 0;
                Record->bMission = 0;
                Record->HardpointSize = 0;
                Record->Hardpoint = nullptr;
            }
            Record->ArchId = Desc.ArchId;
            Record->Count = Desc.Count;
            std::memcpy(&Record->Health, &Desc.Health, 4u);
            Record->Id = Desc.Id;
            Record->bEquipped = Desc.bEquipped;
            if (!Desc.is_internal()) {
                const unsigned int Length = static_cast<unsigned int>(std::strlen(Desc.Hardpoint)) + 1u;
                Record->HardpointSize = static_cast<unsigned short>(Length);
                Record->Hardpoint = static_cast<char*>(
                    ::operator new(Record->HardpointSize, std::nothrow));
                std::strcpy(Record->Hardpoint, Desc.Hardpoint);
            }
            Temporary.PushBack(Record);
        }
        FRpcRequestEquipmentPacket* TypedPacket = static_cast<FRpcRequestEquipmentPacket*>(Packet);
        TypedPacket->AssignEquipment(Temporary);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqCargo(const EquipDescList& Cargo, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQCARGO), &Packet) == 0) {
        FRpcServerEquipHandle Temporary;
        const std::list<EquipDesc>::const_iterator End = Cargo.Items.end();
        for (std::list<EquipDesc>::const_iterator It = Cargo.Items.begin(); It != End; ++It) {
            const EquipDesc& Desc = *It;
            FRpcServerEquipRecord* Record = static_cast<FRpcServerEquipRecord*>(
                ::operator new(sizeof(FRpcServerEquipRecord), std::nothrow));
            if (Record != nullptr) {
                Record->ArchId = 0;
                Record->Count = 0;
                std::memset(&Record->Health, 0, 4u);
                Record->Id = 0;
                Record->bEquipped = 0;
                Record->bMission = 0;
                Record->HardpointSize = 0;
                Record->Hardpoint = nullptr;
            }
            Record->ArchId = Desc.ArchId;
            Record->Count = Desc.Count;
            std::memcpy(&Record->Health, &Desc.Health, 4u);
            Record->Id = Desc.Id;
            Record->bEquipped = Desc.bEquipped;
            if (!Desc.is_internal()) {
                const unsigned int Length = static_cast<unsigned int>(std::strlen(Desc.Hardpoint)) + 1u;
                Record->HardpointSize = static_cast<unsigned short>(Length);
                Record->Hardpoint = static_cast<char*>(
                    ::operator new(Record->HardpointSize, std::nothrow));
                std::strcpy(Record->Hardpoint, Desc.Hardpoint);
            }
            Temporary.PushBack(Record);
        }
        FRpcRequestCargoPacket* TypedPacket = static_cast<FRpcRequestCargoPacket*>(Packet);
        TypedPacket->AssignEquipment(Temporary);
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqAddItem(unsigned int ArchId, const char* Hardpoint, int Count, float Health, unsigned char bMounted, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQADDITEM), &Packet) != 0)
        return;
    FRpcRequestAddItemPacket* TypedPacket = static_cast<FRpcRequestAddItemPacket*>(Packet);
    unsigned int* ArchDestination = &TypedPacket->ArchId;
    int* CountDestination = &TypedPacket->Count;
    float* HealthDestination = &TypedPacket->Health;
    unsigned char Mounted;
    // 06bd75bd: the float load precedes both integer stores; the float store follows them.
    __asm {
        fld Health
        mov eax, ArchId
        mov edx, ArchDestination
        mov dword ptr [edx], eax
        mov eax, Count
        mov edx, CountDestination
        mov dword ptr [edx], eax
        mov al, byte ptr bMounted
        mov Mounted, al
        mov edx, HealthDestination
        fstp dword ptr [edx]
    }
    TypedPacket->bMounted = Mounted;
    TypedPacket->CopyHardpoint(Hardpoint, static_cast<unsigned int>(strlen(Hardpoint) + 1u));
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::ReqRemoveItem(unsigned short EquipId, int Count, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQREMOVEITEM), &Packet) != 0)
        return;
    FRpcRequestRemoveItemPacket* TypedPacket = static_cast<FRpcRequestRemoveItemPacket*>(Packet);
    TypedPacket->EquipId = EquipId;
    TypedPacket->Count = Count;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::ReqModifyItem(unsigned short EquipId, const char* Hardpoint, int Count, float Health, unsigned char bMounted, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQMODIFYITEM), &Packet) != 0)
        return;
    FRpcRequestModifyItemPacket* TypedPacket = static_cast<FRpcRequestModifyItemPacket*>(Packet);
    unsigned int* EquipDestination = &TypedPacket->EquipId;
    int* CountDestination = &TypedPacket->Count;
    float* HealthDestination = &TypedPacket->Health;
    unsigned char Mounted;
    // 06bd768d: as in ReqAddItem, the integer stores sit between the float load and store.
    __asm {
        fld Health
        movzx eax, EquipId
        mov edx, EquipDestination
        mov dword ptr [edx], eax
        mov eax, Count
        mov edx, CountDestination
        mov dword ptr [edx], eax
        mov al, byte ptr bMounted
        mov Mounted, al
        mov edx, HealthDestination
        fstp dword ptr [edx]
    }
    TypedPacket->bMounted = Mounted;
    TypedPacket->CopyHardpoint(Hardpoint, static_cast<unsigned int>(strlen(Hardpoint) + 1u));
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::ReqSetCash(int Cash, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQSETCASH), &Packet) == 0) {
        FRpcRequestSetCashPacket* TypedPacket = static_cast<FRpcRequestSetCashPacket*>(Packet);
        TypedPacket->Value = Cash;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::ReqChangeCash(int Delta, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQCHANGECASH), &Packet) == 0) {
        FRpcRequestChangeCashPacket* TypedPacket = static_cast<FRpcRequestChangeCashPacket*>(Packet);
        TypedPacket->Value = Delta;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::BaseEnter(unsigned int BaseId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_ENTERBASE), &Packet) == 0) {
        FRpcEnterBasePacket* TypedPacket = static_cast<FRpcEnterBasePacket*>(Packet);
        TypedPacket->Value = BaseId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::BaseExit(unsigned int BaseId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_EXITBASE), &Packet) == 0) {
        FRpcExitBasePacket* TypedPacket = static_cast<FRpcExitBasePacket*>(Packet);
        TypedPacket->Value = BaseId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::LocationEnter(unsigned int LocationId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_ENTERLOCATION), &Packet) == 0) {
        FRpcEnterLocationPacket* TypedPacket = static_cast<FRpcEnterLocationPacket*>(Packet);
        TypedPacket->Value = LocationId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::LocationExit(unsigned int LocationId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_EXITLOCATION), &Packet) == 0) {
        FRpcExitLocationPacket* TypedPacket = static_cast<FRpcExitLocationPacket*>(Packet);
        TypedPacket->Value = LocationId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::BaseInfoRequest(unsigned int BaseId, unsigned int SessionId, bool bFull) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQUESTBASEINFO), &Packet) != 0)
        return;
    FRpcRequestBaseInfoPacket* TypedPacket = static_cast<FRpcRequestBaseInfoPacket*>(Packet);
    TypedPacket->Id = BaseId;
    memcpy(&TypedPacket->bFull, &bFull, 1u);
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::LocationInfoRequest(unsigned int LocationId, unsigned int SessionId, bool bFull) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQUESTLOCATIONINFO), &Packet) != 0)
        return;
    FRpcRequestLocationInfoPacket* TypedPacket = static_cast<FRpcRequestLocationInfoPacket*>(Packet);
    TypedPacket->Id = LocationId;
    memcpy(&TypedPacket->bFull, &bFull, 1u);
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::GFObjSelect(unsigned int ObjectId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_GFSELECTOBJECT), &Packet) == 0) {
        FRpcGfSelectObjectPacket* TypedPacket = static_cast<FRpcGfSelectObjectPacket*>(Packet);
        TypedPacket->Value = ObjectId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::GFGoodVaporized(const SGFGoodVaporizedInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_GFGOODVAPORIZED), &Packet) != 0)
        return;
    FRpcGfGoodVaporizedPacket* TypedPacket = static_cast<FRpcGfGoodVaporizedPacket*>(Packet);
    TypedPacket->Field_0x00 = Info.Field_0x00;
    TypedPacket->Field_0x04 = Info.Field_0x04;
    TypedPacket->Field_0x08 = Info.Field_0x08;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::MissionResponse(unsigned int OfferId, unsigned long OfferKind, unsigned char bAccepted, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_MISSIONRESPONSE), &Packet) != 0)
        return;
    FRpcMissionResponsePacket* TypedPacket = static_cast<FRpcMissionResponsePacket*>(Packet);
    TypedPacket->OfferId = OfferId;
    TypedPacket->OfferKind = OfferKind;
    TypedPacket->bAccepted = bAccepted;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::TradeResponse(const unsigned char* Data, int Size, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_TRADERESPONSE), &Packet) == 0) {
        FRpcTradeResponsePacket* TypedPacket = static_cast<FRpcTradeResponsePacket*>(Packet);
        TypedPacket->CopyData(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::GFGoodBuy(const SGFGoodBuyInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_GFGOODBUY), &Packet) != 0)
        return;
    FRpcGfGoodBuyPacket* TypedPacket = static_cast<FRpcGfGoodBuyPacket*>(Packet);
    TypedPacket->Field_0x00 = Info.BaseId;
    TypedPacket->Field_0x04 = Info.Null04;
    TypedPacket->Field_0x08 = Info.GoodId;
    TypedPacket->Field_0x0c = Info.Count;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::GFGoodSell(const SGFGoodSellInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_GFGOODSELL), &Packet) != 0)
        return;
    FRpcGfGoodSellPacket* TypedPacket = static_cast<FRpcGfGoodSellPacket*>(Packet);
    TypedPacket->Field_0x00 = Info.BaseId;
    TypedPacket->Field_0x04 = Info.GoodId;
    TypedPacket->Field_0x08 = Info.Count;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::SystemSwitchOutComplete(unsigned int ShipObjectId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_SYSTEM_SWITCH_OUT_COMPLETE), &Packet) == 0) {
        FRpcSystemSwitchOutCompletePacket* TypedPacket = static_cast<FRpcSystemSwitchOutCompletePacket*>(Packet);
        TypedPacket->Value = ShipObjectId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::PlayerLaunch(unsigned int ShipId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQUESTLAUNCH), &Packet) == 0) {
        FRpcRequestLaunchPacket* TypedPacket = static_cast<FRpcRequestLaunchPacket*>(Packet);
        TypedPacket->Value = ShipId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::LaunchComplete(unsigned int BaseObjId, unsigned int ObjectId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_LAUNCHCOMPLETE), &Packet) != 0)
        return;
    FRpcLaunchCompletePacket* TypedPacket = static_cast<FRpcLaunchCompletePacket*>(Packet);
    TypedPacket->Field_0x00 = BaseObjId;
    TypedPacket->Field_0x04 = ObjectId;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::JumpInComplete(unsigned int SystemId, unsigned int ObjectId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_JUMPINCOMPLETE), &Packet) != 0)
        return;
    FRpcJumpInCompletePacket* TypedPacket = static_cast<FRpcJumpInCompletePacket*>(Packet);
    TypedPacket->Field_0x00 = SystemId;
    TypedPacket->Field_0x04 = ObjectId;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::Hail(unsigned int ObjectId, unsigned int Arg1, unsigned int Arg2) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_HAIL), &Packet) != 0)
        return;
    FRpcHailPacket* TypedPacket = static_cast<FRpcHailPacket*>(Packet);
    TypedPacket->Field_0x00 = ObjectId;
    TypedPacket->Field_0x04 = Arg1;
    TypedPacket->Field_0x08 = Arg2;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::SPObjUpdate(const SSPObjUpdateInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_UPDATEOBJECT), &Packet) == 0) {
        const double* TimeSource = &Info.Timestamp;
        float NarrowedTime;
        // 06bd7fc6: the timestamp is narrowed before any packet payload store.
        __asm {
            mov eax, TimeSource
            fld qword ptr [eax]
            fstp dword ptr NarrowedTime
        }
        FRpcUpdateObjectPacket* TypedPacket = static_cast<FRpcUpdateObjectPacket*>(Packet);
        TypedPacket->State = Info.State;
        TypedPacket->ObjectId = Info.ObjectId;
        TypedPacket->Position = Info.Position;
        TypedPacket->Orientation = Info.Orientation;
        std::memcpy(&TypedPacket->UpdateTime, &NarrowedTime, sizeof(NarrowedTime));
        std::memcpy(&TypedPacket->Throttle, &Info.ThrottleBits, sizeof(Info.ThrottleBits));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SPMunitionCollision(const SSPMunitionCollisionInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_MUNCOLLISION), &Packet) != 0)
        return;
    FRpcMunitionCollisionPacket* TypedPacket = static_cast<FRpcMunitionCollisionPacket*>(Packet);
    TypedPacket->ProjectileArchId = Info.ProjectileArchId;
    TypedPacket->AttackerId = Info.AttackerId;
    TypedPacket->ObjectId = Info.ObjectId;
    TypedPacket->SubObjId = Info.SubObjId;
    std::memcpy(&TypedPacket->HitPosition.x, &Info.HitPosX, 4u);
    std::memcpy(&TypedPacket->HitPosition.y, &Info.HitPosY, 4u);
    std::memcpy(&TypedPacket->HitPosition.z, &Info.HitPosZ, 4u);
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::SPBadLandsObjCollision(const SSPBadLandsObjCollisionInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_BADLANDSOBJCOLLISION), &Packet) != 0)
        return;
    FRpcBadlandsObjectCollisionPacket* TypedPacket = static_cast<FRpcBadlandsObjectCollisionPacket*>(Packet);
    TypedPacket->BadLandsObjId = Info.BadLandsObjId;
    TypedPacket->ObjectId = Info.ObjectId;
    TypedPacket->SubObjId = Info.SubObjId;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::SPObjCollision(const SSPObjCollisionInfo& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_OBJCOLLISION), &Packet) != 0)
        return;
    FRpcObjectCollisionPacket* TypedPacket = static_cast<FRpcObjectCollisionPacket*>(Packet);
    TypedPacket->TargetObjectId = Info.TargetObjectId;
    TypedPacket->TargetSubObjId = Info.TargetSubObjId;
    TypedPacket->ObjectId = Info.ObjectId;
    TypedPacket->SubObjId = Info.SubObjId;
    std::memcpy(&TypedPacket->Damage, &Info.DamageBits, 4u);
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::SPRequestUseItem(const SSPUseItem& Info, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQUEST_USE_ITEM), &Packet) != 0)
        return;
    FRpcRequestUseItemPacket* TypedPacket = static_cast<FRpcRequestUseItemPacket*>(Packet);
    TypedPacket->ObjectId = Info.ObjectId;
    TypedPacket->ItemId = Info.ItemId;
    TypedPacket->Count = Info.Count;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::SPRequestInvincibility(unsigned int ShipId, unsigned char bInvincible, InvincibilityReason Reason, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQINVINCIBILITY), &Packet) != 0)
        return;
    FRpcRequestInvincibilityPacket* TypedPacket = static_cast<FRpcRequestInvincibilityPacket*>(Packet);
    TypedPacket->ShipId = ShipId;
    TypedPacket->Reason = static_cast<unsigned char>(Reason);
    TypedPacket->bInvincible = bInvincible;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::SaveGame(const CHARACTER_ID& CharId, const wchar_t* Description, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_SAVEGAME), &Packet) != 0)
        return;
    FRpcSaveGamePacket* TypedPacket = static_cast<FRpcSaveGamePacket*>(Packet);
    TypedPacket->CharacterId = CharId;
    const unsigned int DescriptionCount = static_cast<unsigned int>(std::wcslen(Description)) + 1u;
    TypedPacket->CopyDescription(Description, DescriptionCount);
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::MissionSaveB(unsigned int SessionId, unsigned long MissionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_MISSIONSAVEB), &Packet) == 0) {
        FRpcMissionSaveBPacket* TypedPacket = static_cast<FRpcMissionSaveBPacket*>(Packet);
        TypedPacket->Value = MissionId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RequestEvent(int EventType, unsigned int ObjectId, unsigned int TargetId, unsigned int SecondId, unsigned long bFlag, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQUEST_EVENT), &Packet) != 0)
        return;
    FRpcRequestEventPacket* TypedPacket = static_cast<FRpcRequestEventPacket*>(Packet);
    TypedPacket->EventType = static_cast<unsigned char>(EventType);
    TypedPacket->ShipId = ObjectId;
    TypedPacket->TargetId = TargetId;
    TypedPacket->SecondId = SecondId;
    TypedPacket->Field_0x10 = static_cast<unsigned char>(bFlag);
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::RequestCancel(int EventType, unsigned int ObjectId, unsigned int TargetId, unsigned long Arg3, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQUEST_CANCEL), &Packet) != 0)
        return;
    FRpcRequestCancelPacket* TypedPacket = static_cast<FRpcRequestCancelPacket*>(Packet);
    TypedPacket->EventType = static_cast<unsigned char>(EventType);
    TypedPacket->ShipId = ObjectId;
    TypedPacket->TargetId = TargetId;
    TypedPacket->Field_0x0c = static_cast<unsigned char>(Arg3);
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::MineAsteroid(unsigned int SystemId, const Vector& Position, unsigned int LootId, unsigned int SecondaryLoot, unsigned int Quantity, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_MINEASTEROID), &Packet) != 0)
        return;
    FRpcMineAsteroidPacket* TypedPacket = static_cast<FRpcMineAsteroidPacket*>(Packet);
    TypedPacket->SystemId = SystemId;
    std::memcpy(&TypedPacket->Position.x, &Position.x, 4u);
    std::memcpy(&TypedPacket->Position.y, &Position.y, 4u);
    std::memcpy(&TypedPacket->Position.z, &Position.z, 4u);
    TypedPacket->LootId = LootId;
    TypedPacket->SecondaryLoot = SecondaryLoot;
    TypedPacket->Quantity = Quantity;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::CommComplete(unsigned int Arg0, unsigned int Arg1, unsigned int Arg2, CommResult Result) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_COMMCOMPLETE), &Packet) != 0)
        return;
    FRpcCommCompletePacket* TypedPacket = static_cast<FRpcCommCompletePacket*>(Packet);
    TypedPacket->Field_0x00 = Arg0;
    TypedPacket->Field_0x04 = Arg1;
    TypedPacket->Field_0x08 = Arg2;
    TypedPacket->Field_0x0c = static_cast<unsigned int>(Result);
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::RequestCreateShip(unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQUESTCREATESHIP), &Packet) == 0) {
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SPScanCargo(const unsigned int& ScannerId, const unsigned int& TargetId, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_CARGOSCAN), &Packet) != 0)
        return;
    FRpcCargoScanPacket* TypedPacket = static_cast<FRpcCargoScanPacket*>(Packet);
    TypedPacket->Field_0x00 = ScannerId;
    TypedPacket->Field_0x04 = TargetId;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::SetManeuver(unsigned int SessionId, const XSetManeuver& Info) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_SETMANEUVER), &Packet) != 0)
        return;
    FRpcSetManeuverPacket* TypedPacket = static_cast<FRpcSetManeuverPacket*>(Packet);
    TypedPacket->ShipFrom = Info.ShipFrom;
    TypedPacket->ObjectId = Info.ObjectId;
    TypedPacket->Flag = Info.Flag;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::InterfaceItemUsed(unsigned int SessionId, unsigned int ItemId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_INTERFACEITEMUSED), &Packet) == 0) {
        FRpcInterfaceItemUsedPacket* TypedPacket = static_cast<FRpcInterfaceItemUsedPacket*>(Packet);
        TypedPacket->Value = ItemId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::AbortMission(unsigned int SessionId, unsigned int MissionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_ABORT_MISSION), &Packet) == 0) {
        FRpcAbortMissionPacket* TypedPacket = static_cast<FRpcAbortMissionPacket*>(Packet);
        TypedPacket->Value = MissionId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RTCDone(unsigned int SessionId, unsigned int RtcId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_RTCDONE), &Packet) == 0) {
        FRpcRtcDonePacket* TypedPacket = static_cast<FRpcRtcDonePacket*>(Packet);
        TypedPacket->Value = RtcId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SetWeaponGroup(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SET_WEAPON_GROUP), &Packet) == 0) {
        FRpcSetWeaponGroupPacket* TypedPacket = static_cast<FRpcSetWeaponGroupPacket*>(Packet);
        TypedPacket->BorrowData(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SetVisitedState(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SET_VISITED_STATE), &Packet) == 0) {
        FRpcSetVisitedStatePacket* TypedPacket = static_cast<FRpcSetVisitedStatePacket*>(Packet);
        TypedPacket->CopyDataIfNull(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RequestBestPath(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_REQUEST_BEST_PATH), &Packet) == 0) {
        FRpcRequestBestPathPacket* TypedPacket = static_cast<FRpcRequestBestPathPacket*>(Packet);
        TypedPacket->CopyDataIfNull(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RequestPlayerStats(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_REQUEST_PLAYER_STATS), &Packet) == 0) {
        FRpcRequestPlayerStatsPacket* TypedPacket = static_cast<FRpcRequestPlayerStatsPacket*>(Packet);
        TypedPacket->CopyDataIfNull(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::PopUpDialog(unsigned int SessionId, unsigned int Buttons) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_POP_UP_DIALOG), &Packet) == 0) {
        FRpcPopUpDialogPacket* TypedPacket = static_cast<FRpcPopUpDialogPacket*>(Packet);
        TypedPacket->Buttons = Buttons;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RequestGroupPositions(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_REQUEST_GROUP_POSITIONS), &Packet) == 0) {
        FRpcRequestGroupPositionsPacket* TypedPacket = static_cast<FRpcRequestGroupPositionsPacket*>(Packet);
        TypedPacket->CopyDataIfNull(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SetMissionLog(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SET_MISSION_LOG), &Packet) == 0) {
        FRpcSetMissionLogPacket* TypedPacket = static_cast<FRpcSetMissionLogPacket*>(Packet);
        TypedPacket->CopyDataIfNull(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::SetInterfaceState(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_SET_INTERFACE_STATE), &Packet) == 0) {
        FRpcSetInterfaceStatePacket* TypedPacket = static_cast<FRpcSetInterfaceStatePacket*>(Packet);
        TypedPacket->CopyDataIfNull(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::RequestRankLevel(unsigned int SessionId, unsigned char* Data, int Size) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_REQUEST_RANK_LEVEL), &Packet) == 0) {
        FRpcRequestRankLevelPacket* TypedPacket = static_cast<FRpcRequestRankLevelPacket*>(Packet);
        TypedPacket->CopyDataIfNull(Data, static_cast<unsigned int>(Size));
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::InitiateTrade(unsigned int SessionId, unsigned int TargetSessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) != 0)
        return;
    FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
    TypedPacket->Action = kPlayerTradeInitiateTrade;
    TypedPacket->TargetPlayer = TargetSessionId;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::TerminateTrade(unsigned int SessionId, int Reason) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) != 0)
        return;
    FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
    TypedPacket->Action = kPlayerTradeTerminateTrade;
    TypedPacket->Reason = Reason;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::AcceptTrade(unsigned int SessionId, bool bAccepted) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) != 0)
        return;
    FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
    TypedPacket->Action = kPlayerTradeAcceptTrade;
    TypedPacket->bAccepted = static_cast<unsigned char>(bAccepted);
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::SetTradeMoney(unsigned int SessionId, unsigned long Money) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) != 0)
        return;
    FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
    TypedPacket->Action = kPlayerTradeSetTradeMoney;
    TypedPacket->Money = static_cast<int>(Money);
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::AddTradeEquip(unsigned int SessionId, const EquipDesc& Equip) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) != 0)
        return;
    FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
    TypedPacket->Action = kPlayerTradeAddTradeEquip;
    TypedPacket->EquipArchId = Equip.get_arch_id();
    TypedPacket->EquipCount = Equip.get_count();
    const float* HealthSource = &Equip.Health;
    float* HealthDestination = &TypedPacket->EquipHealth;
    __asm {
        mov eax, HealthSource
        mov edx, HealthDestination
        fld dword ptr [eax]
        fstp dword ptr [edx]
    }
    TypedPacket->EquipId = Equip.get_id();
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::DelTradeEquip(unsigned int SessionId, const EquipDesc& Equip) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) != 0)
        return;
    FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
    TypedPacket->Action = kPlayerTradeDelTradeEquip;
    TypedPacket->EquipArchId = Equip.get_arch_id();
    TypedPacket->EquipCount = Equip.get_count();
    TypedPacket->EquipId = Equip.get_id();
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::RequestTrade(unsigned int SessionId, unsigned int TargetSessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) != 0)
        return;
    FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
    TypedPacket->Action = kPlayerTradeRequestTrade;
    TypedPacket->TargetPlayer = TargetSessionId;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::StopTradeRequest(unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_PLAYER_TRADE), &Packet) != 0)
        return;
    FRpcPlayerTradePacket* TypedPacket = static_cast<FRpcPlayerTradePacket*>(Packet);
    TypedPacket->Action = kPlayerTradeStopTradeRequest;
    TypedPacket->TargetPlayer = SessionId;
    this->PushToServer(Packet->AsRaw());
}

void IServerImpl::ReqDifficultyScale(float Scale, unsigned int SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_REQDIFFICULTYSCALE), &Packet) != 0)
        return;
    FRpcRequestDifficultyScalePacket* TypedPacket = static_cast<FRpcRequestDifficultyScalePacket*>(Packet);
    float* Destination = &TypedPacket->Value;
    __asm {
        fld Scale
        mov eax, Destination
        fstp dword ptr [eax]
    }
    this->PushToServer(Packet->AsRaw());
}

unsigned int IServerImpl::GetServerID() {
    return 0;
}

const char* IServerImpl::GetServerSig() {
    return "";
}

void IServerImpl::DumpPacketStats(const char* Name) {
    RpcGetRemotePacketSerializer()->DumpStats(Name);
}

void IServerImpl::Dock(const unsigned int& BaseId, const unsigned int& SessionId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelClient, FlPacketOpcode(FLPACKET_CLIENT_DOCK), &Packet) == 0) {
        FRpcDockPacket* TypedPacket = static_cast<FRpcDockPacket*>(Packet);
        TypedPacket->Value = BaseId;
        this->PushToServer(Packet->AsRaw());
    }
}

void IServerImpl::PushToServer(CDAPacket* Packet) {
    return;
}

// 06bd9040. Retail: SenderId is never stored, only TargetId (+0x20).
void IServerImpl::SubmitChat(CHAT_ID SenderId, unsigned long Size, const void* MsgData, CHAT_ID TargetId, int SystemId) {
    FPacketBase* Packet = nullptr;
    if (Serializer->Allocate(kPacketChannelCommon, FlPacketOpcode(FLPACKET_COMMON_CHATMSG), &Packet) != 0)
        return;
    FRpcChatMsgPacket* TypedPacket = static_cast<FRpcChatMsgPacket*>(Packet);
    if (MsgData == nullptr || Size == 0) {
        TypedPacket->DataSize = 0;
        TypedPacket->Data = nullptr;
    } else {
        TypedPacket->DataSize = Size;
        TypedPacket->Data = static_cast<unsigned char*>(::operator new(Size, std::nothrow));
        std::memcpy(TypedPacket->Data, MsgData, Size);
    }
    TypedPacket->TargetId = TargetId.Id;
    this->PushToServer(Packet->AsRaw());
}

