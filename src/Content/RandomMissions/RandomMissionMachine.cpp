// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TU61 -- the random-mission runtime machine and the runtime slots of the offer components.
// Declarations in RandomMissionMachine.h, RandomMissionOffer.h and RandomMissionRuntime.h.

#include "Core/ContentStaticIds.h"       // TU61's 16 static-init thunks
#include "Core/ContentDefaultId.h"       // TU61 LAB_06f84260: CreateID("default") -> DAT_06fd3bc4

#include "RandomMissions/RandomMissionMachine.h"

#include "Core/ContentInternal.h"        // SubtractVectors, SpacePopRemoveExclusionSphere
#include "Core/ContentMessageRouter.h"   // GMessageRouter (DAT_06fcf254)
#include "Core/ContentPlayerState.h"     // FContentPlayerState
#include "NPCs/DockQueues.h"             // PlaceFormationBlocks
#include "Encounters/Encounters.h"             // FindFormation
#include "Comms/Gcs.h"                    // MakeVoiceLineName
#include "Guides/Guides.h"                 // FGuideDesc
#include "Missions/MissionObject.h"          // CreateMissionObjective, FNNObjectiveType7
#include "Missions/MissionObjList.h"         // CreateObjListDirective, FObjListStayInRange, FObjListGotoVec
#include "NPCs/NpcBase.h"                // FContentSpaceObj::GetPosition
#include "NPCs/NpcNames.h"               // FNpcNameRequest, BuildNpcName, Create*Reputation
#include "NPCs/NpcShip.h"                // FContentNpcShip, ApplyNpcShipArchById, ApplyNpcShipFaction
#include "Core/RefHandle.h"              // TRefHandle
#include "Encounters/RandomEncounters.h"       // FRmKillableSolar, FRmCriticalLoot
#include "RandomMissions/RandomMissionObject.h"    // FRandomMissionObject
#include "Encounters/SpacePaths.h"             // BuildNnPath
#include "Universe/SystemInfo.h"             // FSystemInfo, FindSystemInfo
#include "common_csys.h"            // Transform (pub::SpaceObj::GetEmptyPos)
#include "common_misc.h"            // SUBOBJ_ID_NONE
#include "common_newchar.h"         // pub::StateGraph::get_state_graph
#include "common_pub.h"             // SetZoneBehaviorParams, SetPersonalityParams
#include "common_server.h"
#include "server_cobject.h"         // SolarInfo, LootInfo, CreateSolar, CreateLoot

#include <cstdlib>
#include <cstring>

// =======================================================================================
// FRmObjective (vtable 06fbc6d8)
// =======================================================================================

// FUN_06f85440: operator new(0x48); a failed new returns null.
FRmObjective* FRmObjective::Clone() const
{
    return new FRmObjective(*this);
}

// LAB_06ee9e10 (ICF).
int FRmObjective::GetType() const
{
    return 1;
}

// LAB_06f85590. The factory's out-parameter is the argument slot of InMission (overwritten;
// type 7 always builds).
FMissionObjective* FRmObjective::CreateMissionObjective(FMissionObject* InMission,
                                                        FRmRuntimeMachine* Machine)
{
    FMissionObjective* Created = nullptr;
    ::CreateMissionObjective(7, InMission, &Created);
    Created->Objective.Type = static_cast<unsigned int>(ObjectiveType);
    IRmTextParamSource* Source = nullptr;
    if (Machine != nullptr)
        Source = Machine;
    static_cast<FNNObjectiveType7*>(Created)->Text = *Build(Source);
    Created->bHidden = 0;
    return Created;
}

// LAB_06ea1470 (ICF).
bool FRmObjective::GetPosition(Vector* /*Out*/)
{
    return false;
}

// LAB_06f85580.
void FRmObjective::SetPosition(Vector /*InPosition*/)
{
}

// LAB_06ea1470 (ICF).
bool FRmObjective::GetSystemId(unsigned int* /*Out*/)
{
    return false;
}

// =======================================================================================
// FRmObjectiveType2 (vtable 06fbc710)
// =======================================================================================

// FUN_06f855f0; the VC6 vector operator= is inlined.
FRmObjectiveType2::FRmObjectiveType2(FRmParamText const* Source)
{
    StrId = Source->StrId;
    if (&Params != &Source->Params)
        Params = Source->Params;
}

// LAB_06f860a0.
FmtStr* FRmObjectiveType2::Build(IRmTextParamSource* Source)
{
    if (Text.MadLibId == 0)
    {
        Text.begin_mad_lib(static_cast<unsigned int>(StrId));
        for (std::vector<int>::iterator Param = Params.begin(); Param != Params.end(); ++Param)
            Source->GetTextParam(*Param, &Text);
        Text.end_mad_lib();
        FmtStr::NavMarker Marker;
        std::memcpy(&Marker.Data[0], &MarkerSystemId, sizeof(unsigned int));
        std::memcpy(&Marker.Data[1], &MarkerValue, sizeof(unsigned int));
        std::memcpy(&Marker.Data[2], &MarkerPosition.x, sizeof(float));
        std::memcpy(&Marker.Data[3], &MarkerPosition.y, sizeof(float));
        std::memcpy(&Marker.Data[4], &MarkerPosition.z, sizeof(float));
        Text.append_nav_marker(Marker);
    }
    return &Text;
}

// FUN_06f85790: operator new(0x5c); null when new fails.
FRmObjective* FRmObjectiveType2::Clone() const
{
    return new FRmObjectiveType2(*this);
}

// LAB_06ee9660 (ICF).
int FRmObjectiveType2::GetType() const
{
    return 2;
}

// LAB_06f86060.
bool FRmObjectiveType2::GetPosition(Vector* Out)
{
    *Out = MarkerPosition;
    return true;
}

// LAB_06f86080.
void FRmObjectiveType2::SetPosition(Vector InPosition)
{
    MarkerPosition = InPosition;
}

// LAB_06f858b0.
bool FRmObjectiveType2::GetSystemId(unsigned int* Out)
{
    *Out = MarkerSystemId;
    return true;
}

// LAB_06f86030.
void FRmObjectiveType2::SetMarker(unsigned int InSystemId, int InValue, Vector InPosition)
{
    MarkerSystemId = InSystemId;
    MarkerValue = InValue;
    MarkerPosition = InPosition;
}

// =======================================================================================
// FRmObjectiveType3 (vtable 06fbc73c)
// =======================================================================================

// FUN_06f858c0; the base part is inline.
FRmObjectiveType3::FRmObjectiveType3(FRmParamText const* Source)
{
    StrId = Source->StrId;
    if (&Params != &Source->Params)
        Params = Source->Params;
}

// FUN_06f85f60. Players is not null-tested.
FRmObjectiveType3::~FRmObjectiveType3()
{
    for (std::vector<unsigned int>::iterator Id = Ids.begin(); Id != Ids.end(); ++Id)
    {
        for (std::vector<FContentPlayerState*>::iterator Player = Players->begin();
             Player != Players->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, *Id, 0);
    }
}

// FUN_06f85aa0: operator new(0x70).
FRmObjective* FRmObjectiveType3::Clone() const
{
    return new FRmObjectiveType3(*this);
}

// LAB_06f5e540 (ICF).
int FRmObjectiveType3::GetType() const
{
    return 3;
}

// LAB_06f85d90. The text is reset so the next Build rebuilds it.
void FRmObjectiveType3::SetTargets(std::vector<unsigned int> const* InIds,
                                   std::vector<FContentPlayerState*>* InPlayers,
                                   unsigned int InSystemId, Vector InPosition)
{
    MarkerPosition = InPosition;
    MarkerSystemId = InSystemId;
    MarkerValue = 0;
    Players = InPlayers;
    Text.destroy();
    Text = FmtStr();
    for (std::vector<unsigned int>::iterator Id = Ids.begin(); Id != Ids.end(); ++Id)
    {
        for (std::vector<FContentPlayerState*>::iterator Player = InPlayers->begin();
             Player != InPlayers->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, *Id, 0);
    }
    if (&Ids != InIds)
        Ids = *InIds;
    for (std::vector<unsigned int>::const_iterator Id = InIds->begin(); Id != InIds->end(); ++Id)
    {
        for (std::vector<FContentPlayerState*>::iterator Player = InPlayers->begin();
             Player != InPlayers->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, *Id, 1);
    }
}

// LAB_06f85c40.
void FRmObjectiveType3::SetTargetRecords(FRmRecordList* Records,
                                         std::vector<FContentPlayerState*>* InPlayers,
                                         unsigned int Mask, unsigned int InSystemId,
                                         Vector InPosition)
{
    std::vector<unsigned int> Found;
    Found.reserve(Records->Records.size());
    for (std::list<FRmSpawnRecord*>::iterator It = Records->Records.begin();
         It != Records->Records.end(); ++It)
    {
        FRmSpawnRecord* const Record = *It;
        if ((Record->Flags & Mask) != 0 && Record->State == 2)
            Found.push_back(Record->ObjectId);
    }
    SetTargets(&Found, InPlayers, InSystemId, InPosition);
}

// =======================================================================================
// FRmObjectiveType4 (vtable 06fbc770)
// =======================================================================================

namespace
{
    // The "away from the origin" test, inlined three times: per coordinate, a square greater
    // than 1.0 (DAT_06fb5810) or unordered is away.
    bool RmIsAwayFromOrigin(Vector const& At)
    {
        double const X = At.x;
        double const Y = At.y;
        double const Z = At.z;
        return !(X * X <= 1.0) || !(Y * Y <= 1.0) || !(Z * Z <= 1.0);
    }
}

// LAB_06f86230.
FmtStr* FRmObjectiveType4::Build(IRmTextParamSource* Source)
{
    if (Text.MadLibId == 0)
    {
        Text.begin_mad_lib(static_cast<unsigned int>(StrId));
        for (std::vector<int>::iterator Param = Params.begin(); Param != Params.end(); ++Param)
            Source->GetTextParam(*Param, &Text);
        Text.end_mad_lib();
        if (RmIsAwayFromOrigin(MarkerPosition))
        {
            FmtStr::NavMarker Marker;
            std::memcpy(&Marker.Data[0], &MarkerSystemId, sizeof(unsigned int));
            std::memcpy(&Marker.Data[1], &MarkerValue, sizeof(unsigned int));
            std::memcpy(&Marker.Data[2], &MarkerPosition.x, sizeof(float));
            std::memcpy(&Marker.Data[3], &MarkerPosition.y, sizeof(float));
            std::memcpy(&Marker.Data[4], &MarkerPosition.z, sizeof(float));
            Text.append_nav_marker(Marker);
        }
        Text.append_int(static_cast<unsigned int>(Seconds));
    }
    return &Text;
}

// LAB_06f25080 (ICF).
int FRmObjectiveType4::GetType() const
{
    return 4;
}

// LAB_06f861c0.
bool FRmObjectiveType4::GetPosition(Vector* Out)
{
    if (!RmIsAwayFromOrigin(MarkerPosition))
        return false;
    *Out = MarkerPosition;
    return true;
}

// LAB_06f86120.
void FRmObjectiveType4::SetCountdown(int InSeconds, unsigned int InSystemId, int InValue,
                                     Vector InPosition)
{
    Text.destroy();
    Text = FmtStr();
    if (RmIsAwayFromOrigin(InPosition))
    {
        MarkerSystemId = InSystemId;
        MarkerValue = InValue;
        MarkerPosition = InPosition;
    }
    Seconds = InSeconds;
}

// =======================================================================================
// FRmRuntimeMachine helpers
// =======================================================================================

// FUN_06f8a570: mission slot 29.
std::vector<FContentPlayerState*>* FRmRuntimeMachine::GetMissionPlayers()
{
    return Mission->GetPlayerStates();
}

// FUN_06f8a580: mission slot 30.
std::map<unsigned int, FContentPlayerState*>* FRmRuntimeMachine::GetMissionPlayerMap()
{
    return Mission->GetPlayerStateMap();
}

// =======================================================================================
// FRmConversationMessage (vtable 06fbc6d0)
// =======================================================================================

// Inlined in FUN_06f84f10.
FRmConversationMessage::FRmConversationMessage(FRmConversationBase* Source)
    : StateMachineMessage(GRmMsgConversation)
    , Conversation(Source->Clone())
{
}

// FUN_06f87dc0.
FRmConversationMessage::~FRmConversationMessage()
{
    if (Conversation != nullptr)
        delete Conversation;
}

// FUN_06f87e10: StateMachineMessage::operator new(0x20). No null test: a failed new reads
// +0x1c of null.
StateMachineMessage* FRmConversationMessage::clone() const
{
    FRmConversationMessage* const Copy = new FRmConversationMessage(*this);
    Copy->Conversation = Copy->Conversation->Clone();
    return Copy;
}

// =======================================================================================
// FRmOfferComponent slots 5-7 (vtable 06fbc6ac) and the slot 7 overrides
// =======================================================================================

// FUN_06f84f10. The message carries a clone of the conversation; its dtor deletes it.
void FRmOfferComponent::SendConversations(int Trigger, FRmRuntimeMachine* Machine,
                                          FContentPlayerState* Player)
{
    for (std::list<FRmConversation>::iterator It = Conversations.begin();
         It != Conversations.end(); ++It)
    {
        FRmConversation* Conversation = &*It;
        if (Conversation->GetTrigger() != Trigger)
            continue;
        PrepareConversation(Conversation, Machine, Player);
        FRmConversationMessage Message(Conversation);
        unsigned int const Target = static_cast<MessageHandler*>(Machine)->get_id();
        send_delayed_message(&Message, Conversation->GetDelay(), Target);
    }
}

// LAB_06f85020. Retail resolves with bSpeaker 1 first, then 0.
void FRmOfferComponent::PrepareConversation(FRmConversation* Conversation,
                                            FRmRuntimeMachine* Machine,
                                            FContentPlayerState* Player)
{
    Conversation->MarkReady();
    for (std::vector<FGcsLine*>::iterator It = Conversation->Lines.begin();
         It != Conversation->Lines.end(); ++It)
    {
        FRmLine* Line = static_cast<FRmLine*>(*It);
        Line->TextIds.clear();
        Line->PlayerIds.clear();
        ResolveLine(true, Line, Machine, Player);
        ResolveLine(false, Line, Machine, Player);
        if (Line->VoiceId == 0)
            pub::SpaceObj::GetVoiceID(Line->Speaker, Line->VoiceId);
        TString<0x40> const Name = MakeVoiceLineName(Line->Text.Data, "-", Line->VoiceId);
        unsigned int const TextId = pub::GetNicknameId(Name.Data);
        Line->TextIds.push_back(TextId);
        Line->PermutationIds = IDS_COMM_ORDER_1;
        Line->bPrepared = 1;
    }
}

// LAB_06f852b0.
bool FRmOfferComponent::ResolveLine(bool bSpeaker, FRmLine* Line, FRmRuntimeMachine* Machine,
                                    FContentPlayerState* Player)
{
    return Machine->ResolveLine(bSpeaker, Line, Player);
}

namespace
{
    // The ship and solar slot 7 bodies (LAB_06f88df0 / LAB_06f88af0) differ in two places:
    // the kind 8 range test (ship <, solar <=) and the bSpeaker argument passed to the machine
    // without Player (ship 1, solar 0).
    bool RmResolveRangeLine(bool bSpeaker, FRmLine* Line, FRmRuntimeMachine* Machine,
                            FContentPlayerState* Player, bool bSolar)
    {
        if (bSpeaker)
            return Machine->ResolveLine(true, Line, Player);
        int const Kind = Line->TargetKind;
        if (Kind != 8 && Kind != 9)
            return Machine->ResolveLine(false, Line, Player);
        if (Player == nullptr)
            return Machine->ResolveLine(!bSolar, Line, nullptr);

        std::vector<FContentPlayerState*>* Players = Machine->GetMissionPlayers();
        Line->PlayerIds.reserve(Players->size());
        Vector Center;
        Player->ShipObject->GetPosition(&Center);
        for (std::vector<FContentPlayerState*>::iterator It = Players->begin();
             It != Players->end(); ++It)
        {
            FContentPlayerState* const Other = *It;
            if (Other->ShipObject == nullptr)
                continue;
            Vector Position;
            Other->ShipObject->GetPosition(&Position);
            if (Kind == 9)
            {
                // Float differences (FUN_06eb7810), summed unrounded against 1e8
                // (DAT_06fbc2ac); the player is kept when farther.
                float const Dx = Position.x - Center.x;
                float const Dy = Position.y - Center.y;
                float const Dz = Position.z - Center.z;
                double const DistanceSq = static_cast<double>(Dz) * Dz +
                                          static_cast<double>(Dy) * Dy +
                                          static_cast<double>(Dx) * Dx;
                if (DistanceSq > 100000000.0)
                    Line->PlayerIds.push_back(Other->PlayerId);
            }
            else
            {
                // SubtractVectors is FUN_06eeaee0.
                Vector const Delta = SubtractVectors(Position, Center);
                double const DistanceSq = static_cast<double>(Delta.z) * Delta.z +
                                          static_cast<double>(Delta.y) * Delta.y +
                                          static_cast<double>(Delta.x) * Delta.x;
                bool const bInRange = bSolar ? DistanceSq <= 100000000.0
                                             : DistanceSq < 100000000.0;
                if (bInRange)
                    Line->PlayerIds.push_back(Other->PlayerId);
            }
        }
        return true;
    }
}

// LAB_06f88df0.
bool FRmShipComponent::ResolveLine(bool bSpeaker, FRmLine* Line, FRmRuntimeMachine* Machine,
                                   FContentPlayerState* Player)
{
    return RmResolveRangeLine(bSpeaker, Line, Machine, Player, false);
}

// LAB_06f88af0.
bool FRmSolarComponent::ResolveLine(bool bSpeaker, FRmLine* Line, FRmRuntimeMachine* Machine,
                                    FContentPlayerState* Player)
{
    return RmResolveRangeLine(bSpeaker, Line, Machine, Player, true);
}

// =======================================================================================
// Sub-machine messages
// =======================================================================================

// Inlined in LAB_06f86a00.
FRmSolarMessage::FRmSolarMessage(unsigned int Type, unsigned int InObjectId)
    : StateMachineMessage(Type)
    , ObjectId(InObjectId)
{
}

// FUN_06f87cb0.
StateMachineMessage* FRmSolarMessage::clone() const
{
    return new FRmSolarMessage(*this);
}

// FUN_06f86c80.
FRmLootMessage::FRmLootMessage(unsigned int Type, unsigned int InObjectId, unsigned int InValue)
    : StateMachineMessage(Type)
    , ObjectId(InObjectId)
    , Value(InValue)
{
}

// FUN_06f86cb0.
StateMachineMessage* FRmLootMessage::clone() const
{
    return new FRmLootMessage(*this);
}

// Inlined in LAB_06f86fb0.
FRmLootPlayerMessage::FRmLootPlayerMessage(unsigned int Type, unsigned int InObjectId,
                                           unsigned int InValue0, unsigned int InValue1)
    : StateMachineMessage(Type)
    , ObjectId(InObjectId)
    , Value0(InValue0)
    , Value1(InValue1)
{
}

// FUN_06f86d20.
StateMachineMessage* FRmLootPlayerMessage::clone() const
{
    return new FRmLootPlayerMessage(*this);
}

// Inlined in LAB_06f87e90.
FRmShipMessage::FRmShipMessage(unsigned int Type, unsigned int InGroup, unsigned int InObjectId)
    : StateMachineMessage(Type)
    , Group(InGroup)
    , ObjectId(InObjectId)
{
}

// FUN_06f87c40.
StateMachineMessage* FRmShipMessage::clone() const
{
    return new FRmShipMessage(*this);
}

// FUN_06f87d10.
FRmCookieMessage::FRmCookieMessage(unsigned int InCookie)
    : StateMachineMessage(GRmMsgSpawnGroup)
    , Cookie(InCookie)
{
}

// FUN_06f87d40.
StateMachineMessage* FRmCookieMessage::clone() const
{
    return new FRmCookieMessage(*this);
}

// =======================================================================================
// Records
// =======================================================================================

// FUN_06f866a0.
FRmSpawnRecord::FRmSpawnRecord()
{
    Name.Length = 0;
    Name.Data[0] = '\0';
}

// FUN_06f86700 / FUN_06f87bf0.
FRmSpawnRecord::~FRmSpawnRecord()
{
}

// FUN_06f86820.
void FRmRecordList::DeleteAll()
{
    for (std::list<FRmSpawnRecord*>::iterator It = Records.begin(); It != Records.end(); ++It)
    {
        if (*It != nullptr)
            delete *It;
    }
}

// FUN_06f86850.
FRmSpawnRecord* FRmRecordList::FindById(unsigned int Id)
{
    if (Id == 0)
        return nullptr;
    for (std::list<FRmSpawnRecord*>::iterator It = Records.begin(); It != Records.end(); ++It)
    {
        if (Id == (*It)->ObjectId)
            return *It;
    }
    return nullptr;
}

// FUN_06f86880 (strcmp inlined).
FRmSpawnRecord* FRmRecordList::FindByName(TString<0x30> InName)
{
    for (std::list<FRmSpawnRecord*>::iterator It = Records.begin(); It != Records.end(); ++It)
    {
        if (std::strcmp(InName.Data, (*It)->Name.Data) == 0)
            return *It;
    }
    return nullptr;
}

// =======================================================================================
// Sub-machines
// =======================================================================================

// Inlined in the runtime machine ctor.
FRmSubMachine::FRmSubMachine(unsigned int InOwnerId)
    : StateMachineMessageHandler(GMessageRouter)
    , OwnerId(InOwnerId)
{
}

void FRmSubMachine::SendToOwner(StateMachineMessage* Message)
{
    send_immediate_message(Message, OwnerId);
}

FRmSolarMachine::FRmSolarMachine(unsigned int InOwnerId)
    : FRmSubMachine(InOwnerId)
{
}

// FUN_06f86920.
FRmSolarMachine::~FRmSolarMachine()
{
    Records.DeleteAll();
}

// LAB_06f86a00. True for every message 0x39 in state 0.
bool FRmSolarMachine::OnStateEvent(SMEventData const* Event, int const& State)
{
    if (State < 0 || State != 0)
        return false;
    SMControllerEvent const* Controller = static_cast<SMControllerEvent const*>(Event);
    // The type 4 event's int +0x08 is the controller message id (FRandomMissionObject::
    // HandleMissionMessage), cast back after the type test.
    if (Event->get_event_type() != 4 ||
        static_cast<pub::Controller::EMessage>(Controller->ControllerId) !=
            pub::Controller::EMessage::ObjectDestroyed)
        return false;
    unsigned int const Id = *static_cast<unsigned int const*>(Controller->DataPtr);
    FRmSpawnRecord* Record = Records.FindById(Id);
    if (Record != nullptr)
    {
        unsigned int const ObjectId = Record->ObjectId;
        Record->State = 3;
        FRmSolarMessage Message(GRmMsgSolarDestroyed, ObjectId);
        send_immediate_message(&Message, OwnerId);
    }
    return true;
}

// FUN_06f86ae0: a local list, returned as a copy.
FRmRecordList FRmSolarMachine::Query(int Kind, int InState, unsigned int Mask)
{
    FRmRecordList Result;
    for (std::list<FRmSpawnRecord*>::iterator It = Records.Records.begin();
         It != Records.Records.end(); ++It)
    {
        FRmSolarRecord* const Record = static_cast<FRmSolarRecord*>(*It);
        if (Kind != -1 && Record->SolarType != Kind)
            continue;
        if (InState != 0 && Record->State != InState)
            continue;
        if (Mask != 0 && (Record->Flags & Mask) == 0)
            continue;
        Result.Records.push_back(Record);
    }
    return Result;
}

FRmShipMachine::FRmShipMachine(unsigned int InOwnerId)
    : FRmSubMachine(InOwnerId)
{
}

// FUN_06f87990.
FRmShipMachine::~FRmShipMachine()
{
    Records.DeleteAll();
}

// LAB_06f87e90. DAT_06fd3ba4: every member of the group destroyed; DAT_06fd3ba0: the last
// alive flagged (Flags bit 1) member died. True for every message 0x39 in state 0.
bool FRmShipMachine::OnStateEvent(SMEventData const* Event, int const& State)
{
    if (State < 0 || State != 0)
        return false;
    SMControllerEvent const* Controller = static_cast<SMControllerEvent const*>(Event);
    // The type 4 event's int +0x08 is the controller message id, cast back after the type test.
    if (Event->get_event_type() != 4 ||
        static_cast<pub::Controller::EMessage>(Controller->ControllerId) !=
            pub::Controller::EMessage::ObjectDestroyed)
        return false;
    unsigned int const Id = *static_cast<unsigned int const*>(Controller->DataPtr);
    FRmShipRecord* Record = static_cast<FRmShipRecord*>(Records.FindById(Id));
    if (Record == nullptr)
        return true;

    unsigned int const Group = Record->Group;
    unsigned int const ObjectId = Record->ObjectId;
    Record->State = 3;
    // FuncInfo 06fc627c: the destroyed message lives to the exit; the group list and the two
    // later messages nest inside it.
    FRmShipMessage Destroyed(GRmMsgShipDestroyed, Group, ObjectId);
    send_immediate_message(&Destroyed, OwnerId);

    FRmRecordList Members = QueryGroup(Record->Group);
    bool const bFlagged = ((Record->Flags >> 1) & 1) != 0;
    bool bNoFlaggedAlive = true;
    unsigned int DeadCount = 0;
    for (std::list<FRmSpawnRecord*>::iterator It = Members.Records.begin();
         It != Members.Records.end(); ++It)
    {
        int const MemberState = (*It)->State;
        if (MemberState == 3)
            ++DeadCount;
        if (MemberState == 2 && ((*It)->Flags & 2) != 0)
            bNoFlaggedAlive = false;
    }
    if (DeadCount == Members.Records.size())
    {
        FRmShipMessage Message(GRmMsgGroupDestroyed, Record->Group, Record->ObjectId);
        send_immediate_message(&Message, OwnerId);
    }
    if (bFlagged && bNoFlaggedAlive)
    {
        FRmShipMessage Message(GRmMsgGroupTargetsDestroyed, Record->Group, Record->ObjectId);
        send_immediate_message(&Message, OwnerId);
    }
    return true;
}

// FUN_06f87a50.
FRmRecordList FRmShipMachine::Query(int Kind, int InState, unsigned int Mask)
{
    FRmRecordList Result;
    for (std::list<FRmSpawnRecord*>::iterator It = Records.Records.begin();
         It != Records.Records.end(); ++It)
    {
        FRmShipRecord* const Record = static_cast<FRmShipRecord*>(*It);
        if (Kind != 0 && Record->Kind != Kind)
            continue;
        if (InState != 0 && Record->State != InState)
            continue;
        if (Mask != 0 && (Record->Flags & Mask) == 0)
            continue;
        Result.Records.push_back(Record);
    }
    return Result;
}

// FUN_06f880c0.
FRmRecordList FRmShipMachine::QueryGroup(unsigned int Group)
{
    FRmRecordList Result;
    if (Group != 0xffffffffu)
    {
        for (std::list<FRmSpawnRecord*>::iterator It = Records.Records.begin();
             It != Records.Records.end(); ++It)
        {
            FRmShipRecord* const Record = static_cast<FRmShipRecord*>(*It);
            if (Group == Record->Group)
                Result.Records.push_back(Record);
        }
    }
    return Result;
}

FRmLootMachine::FRmLootMachine(unsigned int InOwnerId)
    : FRmSubMachine(InOwnerId)
{
}

// FUN_06f86ef0.
FRmLootMachine::~FRmLootMachine()
{
    Records.DeleteAll();
}

// LAB_06f86fb0. State 0 only; every handled message answers true.
bool FRmLootMachine::OnStateEvent(SMEventData const* Event, int const& State)
{
    if (State < 0 || State != 0)
        return false;

    // Message DAT_06fd3b6c {+0x1c loot id, +0x20 ship id}: the loot is taken from its holder
    // and created again next to the ship. Record and player are not null-tested.
    if (Event->get_event_type() == 3)
    {
        StateMachineMessage const* Message = static_cast<SMMessage const*>(Event)->Message;
        if ((Message->DeliveryState == -1 || Message->DeliveryState == get_state()) &&
            Message->MessageType == GRmMsgDropLoot)
        {
            FRmLootMessage const* Drop = static_cast<FRmLootMessage const*>(Message);
            unsigned int const* ShipId = &Drop->Value;
            FContentPlayerState* Player = FindContentPlayerStateByShip(ShipId);
            FRmLootRecord* Record = static_cast<FRmLootRecord*>(Records.FindById(Drop->ObjectId));
            pub::Player::RemoveCargo(Record->HolderId, Record->CargoSlot, 1);

            pub::SpaceObj::LootInfo Info;
            Info.SystemID = 0;
            Info.EquipArchID = 0;
            Info.NameId = 0;
            Info.OwnerObjId = 0;
            Info.Velocity = Vector{0.0f, 0.0f, 0.0f};
            Info.Position = Vector{0.0f, 0.0f, 0.0f};
            Info.AngularVelocity = Vector{0.0f, 0.0f, 0.0f};
            std::memset(&Info.Orientation, 0, sizeof(Info.Orientation));
            Info.Orientation.E[0][0] = 1.0f;
            Info.Orientation.E[1][1] = 1.0f;
            Info.Orientation.E[2][2] = 1.0f;
            Info.Units = 0;
            Info.HitPtsScale = 0.0f;
            Info.bCanAiTractor = 1;
            Info.bMission = 0;
            Info.MarkForPlayer = 0;

            Info.SystemID = Player->GetSystemRecord()->SystemId;
            pub::SpaceObj::GetArchetypeID(Record->GoodId, Info.EquipArchID);
            unsigned int const PlayerShip = Player->GetShipId();
            Info.OwnerObjId = PlayerShip;
            Vector Linear;
            Vector Angular;
            pub::SpaceObj::GetMotion(PlayerShip, Linear, Angular);
            Vector ShipPosition;
            Player->ShipObject->GetPosition(&ShipPosition);
            // 10.0 is DAT_06fb4438.
            Info.Position = Vector{ShipPosition.x, ShipPosition.y + 10.0f, ShipPosition.z};
            Info.Velocity = Vector{Linear.x, Linear.y + 10.0f, Linear.z};
            Info.AngularVelocity = Angular;
            Info.Units = 1;
            // 2^-15 is DAT_06fb4448, the double 0.5 DAT_06fb74e0.
            int const Roll = std::rand();
            float const Scaled = static_cast<float>(Roll) * 3.0517578125e-05f;
            Info.HitPtsScale = static_cast<float>(static_cast<double>(Scaled) * 0.5 + 0.5);
            Info.bCanAiTractor = 0;
            Info.bMission = 1;
            pub::SpaceObj::CreateLoot(Record->ObjectId, Info);

            unsigned int const LootId = Record->ObjectId;
            Record->State = 2;
            Record->Units = static_cast<int>(Info.Units);
            Record->HolderId = 0;
            Record->CargoSlot = SUBOBJ_ID_NONE;
            FRmLootMessage Reply(GRmMsgLootDropped, LootId, *ShipId);
            send_immediate_message(&Reply, OwnerId);
            return true;
        }
    }

    using pub::Controller::EMessage;
    SMControllerEvent const* Controller = static_cast<SMControllerEvent const*>(Event);
    // A type 4 event's int +0x08 is the controller message id, cast back after the type test.

    // Message 0x39 {id, ..., +0x10 cause}: the loot is gone.
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Controller->ControllerId) == EMessage::ObjectDestroyed)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        FRmLootRecord* Record = static_cast<FRmLootRecord*>(Records.FindById(Parms[0]));
        if (Record == nullptr)
            return true;
        int const Cause = static_cast<int>(Parms[4]);
        if (Cause == 1 || Cause == 0)
            Record->State = 3;
        else
            Record->State = 4;
        if (Cause == 1)
        {
            FRmLootMessage Message(GRmMsgLootDestroyed, Record->ObjectId, 0);
            send_immediate_message(&Message, OwnerId);
        }
        else if (Cause == 0)
        {
            FRmLootMessage Message(GRmMsgLootLost, Record->ObjectId, 0);
            send_immediate_message(&Message, OwnerId);
        }
        if (Record->Kind == 2)
        {
            bool bNoneLeft = false;
            {
                FRmRecordList Alive = Query(0, 2, 0, 2, 0);
                bNoneLeft = Alive.Records.empty();
            }
            if (bNoneLeft)
            {
                FRmLootMessage Message(GRmMsgCriticalLootGone, Record->ObjectId, Parms[1]);
                send_immediate_message(&Message, OwnerId);
            }
        }
        return true;
    }

    // Message 0x18 {id, ship, word slot, good, units}: picked up.
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Controller->ControllerId) == EMessage::LootAcquired)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        FRmLootRecord* Record = static_cast<FRmLootRecord*>(Records.FindById(Parms[0]));
        if (Record == nullptr)
            return true;
        Record->Units -= static_cast<int>(Parms[4]);
        Record->GoodId = Parms[3];
        Record->CargoSlot = *reinterpret_cast<unsigned short const*>(&Parms[2]);
        unsigned int const* ShipId = &Parms[1];
        Record->HolderId = FindContentPlayerStateByShip(ShipId)->PlayerId;
        if (Record->Units == 0)
        {
            // FuncInfo 06fc6100: the picked-up message outlives the critical-loot test, the
            // query and the 3b8c message.
            FRmLootMessage PickedUp(GRmMsgLootPickedUp, Record->ObjectId, *ShipId);
            send_immediate_message(&PickedUp, OwnerId);
            if (Record->Kind == 2)
            {
                bool bNoneLeft = false;
                {
                    FRmRecordList Alive = Query(0, 2, 0, 2, 0);
                    bNoneLeft = Alive.Records.empty();
                }
                if (bNoneLeft)
                {
                    FRmLootMessage Message(GRmMsgCriticalLootGone, Record->ObjectId, *ShipId);
                    send_immediate_message(&Message, OwnerId);
                }
            }
        }
        return true;
    }

    // Message 0x27 {ship, word 1}: the critical loot the player holds.
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Controller->ControllerId) == EMessage::Msg0x27)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        FContentPlayerState* Player = FindContentPlayerStateByShip(Parms);
        if (Player == nullptr)
            return true;
        FRmRecordList Held = Query(0, 2, 0, 4, Player->PlayerId);
        if (!Held.Records.empty())
        {
            for (std::list<FRmSpawnRecord*>::iterator It = Held.Records.begin();
                 It != Held.Records.end(); ++It)
            {
                FRmLootPlayerMessage Message(GRmMsgLootHolderDocked, (*It)->ObjectId, Parms[0],
                                             Parms[1]);
                send_immediate_message(&Message, OwnerId);
            }
        }
        return true;
    }

    // Message 0x3d {system}: the first alive critical loot, if in that system, is destroyed.
    if (Event->get_event_type() == 4 &&
        static_cast<EMessage>(Controller->ControllerId) == EMessage::SystemUnloaded)
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        FRmRecordList Alive = Query(0, 2, 0, 2, 0);
        if (!Alive.Records.empty())
        {
            FRmSpawnRecord* const First = Alive.Records.front();
            unsigned int System = 0;
            pub::SpaceObj::GetSystem(First->ObjectId, System);
            if (Parms[0] == System)
                pub::SpaceObj::Destroy(First->ObjectId, static_cast<DestroyType>(0));
        }
        return true;
    }
    return false;
}

// =======================================================================================
// FRmRuntimeMachine
// =======================================================================================

float GRmMultiplayerDelay = 30.0f;

namespace
{
    // The x87 squared distance of FUN_06f89e90 / FUN_06f8bb80: the three differences kept in
    // extended precision, summed (x*x + z*z) + y*y.
    double RmDistanceSq(Vector const& A, Vector const& B)
    {
        double const Dx = static_cast<double>(A.x) - static_cast<double>(B.x);
        double const Dy = static_cast<double>(A.y) - static_cast<double>(B.y);
        double const Dz = static_cast<double>(A.z) - static_cast<double>(B.z);
        return (Dx * Dx + Dz * Dz) + Dy * Dy;
    }
}

// FUN_06f89a00. The three sub-machines are added with the StateMachineGroup import (not
// FRmMachineGroup::AddMachine).
FRmRuntimeMachine::FRmRuntimeMachine(FRmOffer* InOffer)
    : FRmMachineGroup(GMessageRouter)
    , Mission(InOffer->Mission)
    , Offer(InOffer)
{
    if (!pub::SinglePlayer())
        GRmMultiplayerDelay = 45.0f;
    Solars = new FRmSolarMachine(get_id());
    StateMachineGroup::add_state_machine(Solars);
    Ships = new FRmShipMachine(get_id());
    StateMachineGroup::add_state_machine(Ships);
    Loot = new FRmLootMachine(get_id());
    StateMachineGroup::add_state_machine(Loot);
}

// FUN_06f89cc0: the member destructors, then ~FRmMachineGroup.
FRmRuntimeMachine::~FRmRuntimeMachine()
{
}

// LAB_06f8a5c0.
bool FRmRuntimeMachine::OnStateEvent(SMEventData const* Event, int const& State)
{
    if (State < 0)
        return false;
    if (State == 0)
        OnState0(Event, State);
    else if (State == 1)
        OnState1(Event, State);
    else if (State == 2)
        OnState2(Event, State);
    return false;
}

// LAB_06ebf720.
void FRmRuntimeMachine::Shutdown()
{
}

// FUN_06f89dc0.
unsigned int FRmRuntimeMachine::GetSystemId()
{
    if (Objectives.Objectives.empty())
        return Offer->System->SystemId;
    unsigned int SystemId = 0;
    std::vector<FRmObjective*>::iterator It = ActiveObjectives.begin();
    for (; It != ActiveObjectives.end(); ++It)
    {
        if ((*It)->GetSystemId(&SystemId))
            break;
    }
    if (It != ActiveObjectives.end())
        return SystemId;
    return Offer->System->SystemId;
}

// FUN_06f89e30.
FRmSpawnRecord* FRmRuntimeMachine::FindSolarByName(TString<0x30> InName)
{
    return Solars->Records.FindByName(InName);
}

// FUN_06f89e60.
FRmSpawnRecord* FRmRuntimeMachine::FindShipByName(TString<0x30> InName)
{
    return Ships->Records.FindByName(InName);
}

// LAB_06f89e90 (jump tables 06f8a53c / 06f8a550). Speaker kinds: 1 BASE, 2 BIG_SOLAR,
// 3 BIG_SHIP, 4 DEFENSIVE_SHIP, 5 FRIENDLY_SHIP. Listener kinds: 6 PLAYER, 7 ALL_PLAYERS,
// 8 / 9 PLAYERS_IN_RANGE / _OUT_OF_RANGE, 10 / 11 PLAYERS_IN_SYSTEM / _OUT_OF_SYSTEM.
bool FRmRuntimeMachine::ResolveLine(bool bSpeaker, FRmLine* Line, FContentPlayerState* Player)
{
    if (bSpeaker)
    {
        switch (Line->SpeakerKind)
        {
        case 1:
            Line->Speaker = 0;
            Line->CostumeId = Offer->McCostume;
            Line->VoiceId = Offer->VoiceId;
            Line->CaptionIds = 0x32d7;
            return true;
        case 2:
        {
            FRmRecordList Found = Solars->Query(0, 2, 0);
            if (Found.Records.empty())
                return false;
            Line->Speaker = Found.Records.front()->ObjectId;
            return true;
        }
        case 3:
            for (std::list<FRmSpawnRecord*>::iterator It = Ships->Records.Records.begin();
                 It != Ships->Records.Records.end(); ++It)
            {
                if (static_cast<FRmShipRecord*>(*It)->Kind == 2)
                {
                    Line->Speaker = (*It)->ObjectId;
                    return true;
                }
            }
            return false;
        case 4:
            for (std::list<FRmSpawnRecord*>::iterator It = Ships->Records.Records.begin();
                 It != Ships->Records.Records.end(); ++It)
            {
                if (static_cast<FRmShipRecord*>(*It)->Kind == 1 && (*It)->State == 2)
                {
                    Line->Speaker = (*It)->ObjectId;
                    return true;
                }
            }
            return false;
        case 5:
            // 0.0 is DAT_06fbc29c; unordered skips.
            for (std::list<FRmSpawnRecord*>::iterator It = Ships->Records.Records.begin();
                 It != Ships->Records.Records.end(); ++It)
            {
                if ((*It)->Attitude > 0.0f && (*It)->State == 2)
                {
                    Line->Speaker = (*It)->ObjectId;
                    return true;
                }
            }
            return false;
        default:
            return false;
        }
    }

    switch (Line->TargetKind)
    {
    case 6:
        Line->PlayerIds.push_back(Player->PlayerId);
        return true;
    case 7:
    {
        std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
        for (std::vector<FContentPlayerState*>::iterator It = Players->begin();
             It != Players->end(); ++It)
            Line->PlayerIds.push_back((*It)->PlayerId);
        return true;
    }
    case 8:
    case 9:
    {
        std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
        Vector Center;
        if (Player != nullptr)
            Player->ShipObject->GetPosition(&Center);
        else
            Center = Position;
        for (std::vector<FContentPlayerState*>::iterator It = Players->begin();
             It != Players->end(); ++It)
        {
            if ((*It)->ShipObject == nullptr)
                continue;
            Vector Other;
            (*It)->ShipObject->GetPosition(&Other);
            double const DistanceSq = RmDistanceSq(Other, Center);
            // 1e8 is DAT_06fbc2ac; unordered is skipped by both kinds.
            bool const bKeep = Line->TargetKind == 8 ? DistanceSq <= 100000000.0
                                                     : DistanceSq > 100000000.0;
            if (bKeep)
                Line->PlayerIds.push_back((*It)->PlayerId);
        }
        return true;
    }
    case 10:
    case 11:
    {
        // Retail: the player index advances only past a player WITHOUT a ship (06f8a191 /
        // 06f8a129), so the loop never ends once a mission player has a ship. Shipped
        // vignetteparams.ini never names PLAYERS_IN_SYSTEM / _OUT_OF_SYSTEM, so it is not hit.
        std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
        std::vector<FContentPlayerState*>::iterator It = Players->begin();
        while (It != Players->end())
        {
            if ((*It)->ShipObject == nullptr)
            {
                ++It;
                continue;
            }
            unsigned int const PlayerSystem = (*It)->GetSystemRecord()->SystemId;
            bool const bSame = PlayerSystem == GetSystemId();
            if (bSame == (Line->TargetKind == 10))
                Line->PlayerIds.push_back((*It)->PlayerId);
        }
        return true;
    }
    default:
        return false;
    }
}

// LAB_06f8a590.
bool FRmRuntimeMachine::CanSave()
{
    if (get_state() == RmStateSucceeded || get_state() == RmStateFailed ||
        get_state() == RmStateDone)
        return false;
    return true;
}

// LAB_06f71100 (ICF with FRmTrigger::OnIdle).
bool FRmRuntimeMachine::OnState0(SMEventData const* Event, int const& State)
{
    if (State == RmStateIdle && Event->get_event_type() < 4 && Event->get_event_type() == 1)
    {
        set_state(RmStateWaitStart);
        return true;
    }
    return false;
}

// LAB_06f8a610.
bool FRmRuntimeMachine::OnState1(SMEventData const* Event, int const& State)
{
    if (State != RmStateWaitStart || Event->get_event_type() != 3)
        return false;
    StateMachineMessage const* Message = static_cast<SMMessage const*>(Event)->Message;
    if (Message->DeliveryState != -1 && get_state() != Message->DeliveryState)
        return false;
    if (Message->MessageType != GRmMsgStartMachine)
        return false;
    set_state(RmStateStarted);
    return true;
}

// LAB_06f8a670.
bool FRmRuntimeMachine::OnState2(SMEventData const* Event, int const& State)
{
    if (State == RmStateStarted && Event->get_event_type() < 4 && Event->get_event_type() == 1)
    {
        set_state(RmStateStart);
        return true;
    }
    return false;
}

// FUN_06f8b160: over a copy of the mission's players.
void FRmRuntimeMachine::RefreshObjectives()
{
    std::vector<FContentPlayerState*> Players = *GetMissionPlayers();
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end();
         ++It)
    {
        unsigned int const MachineSystem = GetSystemId();
        if ((*It)->GetSystemId() == MachineSystem)
            UpdatePlayerObjectives((*It)->PlayerId);
    }
    bObjectivesChanged = false;
}

// FUN_06f8b250: over a copy of the mission's players.
void FRmRuntimeMachine::SendPlaceholderObjectives()
{
    std::vector<FContentPlayerState*> Players = *GetMissionPlayers();
    for (std::vector<FContentPlayerState*>::iterator It = Players.begin(); It != Players.end();
         ++It)
        Mission->GetOrCreatePlayer((*It)->PlayerId)->SendPlaceholderObjective();
}

// FUN_06f8b330.
void FRmRuntimeMachine::UpdatePlayerObjectives(unsigned int InPlayerId)
{
    FMissionPlayer* Player = Mission->GetOrCreatePlayer(InPlayerId);
    if (Player == nullptr)
        return;
    std::list<FMissionObjective*>& List = Player->Objectives.Objectives;

    // The machine's run starts after the first objective of a player without a ship and
    // after the type 5 path objectives; it ends at a type 8 or 5 objective.
    std::list<FMissionObjective*>::iterator First = List.begin();
    bool bReplaced = false;
    // Deviation (CONTENT-OBJECTIVE-END-ITERATOR, docs/re/deviations.md): retail 06f8b376 has
    // no end check and steps through the VC6 ring node (on an empty list, onto itself);
    // std::list forbids ++end(), so the port stays at end().
    if (Player->PlayerState->ShipObject == nullptr && First != List.end())
        ++First;
    while (First != List.end() && (*First)->GetType() == 5)
        ++First;
    std::list<FMissionObjective*>::iterator Last = First;
    while (Last != List.end() && (*Last)->GetType() != 8 && (*Last)->GetType() != 5)
        ++Last;

    // The run is replaced by clones of the machine's objectives unless it already matches
    // them one by one (IsSame, slot 6).
    bool bSame = true;
    {
        std::list<FMissionObjective*>::iterator Mine = Objectives.Objectives.begin();
        std::list<FMissionObjective*>::iterator Theirs = First;
        for (; Theirs != Last; ++Theirs, ++Mine)
        {
            if (Mine == Objectives.Objectives.end() || !(*Theirs)->IsSame(*Mine))
            {
                bSame = false;
                break;
            }
        }
        if (bSame && Mine != Objectives.Objectives.end())
            bSame = false;
    }
    if (!bSame)
    {
        Player->Objectives.EraseRange(First, Last);
        Player->Objectives.InsertClones(Last, &Objectives, Mission);
        bReplaced = true;
    }

    // Only a player in this machine's system gets anything sent.
    FContentPlayerState* State = Player->PlayerState;
    if (State->GetSystemRecord()->SystemId != GetSystemId())
        return;

    bool bPathChanged = false;
    if (State->ShipObject != nullptr && State->ShipObject->IsInTradeLane())
    {
        // Once per launch, a reached first path objective is dropped (1e6 is DAT_06fb8570;
        // unordered skips).
        if (Player->bTradelanePathChecked == 0)
        {
            Player->bTradelanePathChecked = 1;
            if (!List.empty())
            {
                FMissionObjective* Front = Player->Objectives.GetFront();
                if (Front->GetType() == 5)
                {
                    Vector Now;
                    State->GetPosition(&Now);
                    FNNObjectiveType5 const* Leg = static_cast<FNNObjectiveType5 const*>(Front);
                    Vector LegPoint;
                    std::memcpy(&LegPoint.x, &Leg->Marker.Data[2], sizeof(float));
                    std::memcpy(&LegPoint.y, &Leg->Marker.Data[3], sizeof(float));
                    std::memcpy(&LegPoint.z, &Leg->Marker.Data[4], sizeof(float));
                    Vector Delta;
                    Vec3Assign(&Delta, Now.x - LegPoint.x, Now.y - LegPoint.y,
                               Now.z - LegPoint.z);
                    double const DistanceSq = static_cast<double>(Delta.z) * Delta.z +
                                              static_cast<double>(Delta.y) * Delta.y +
                                              static_cast<double>(Delta.x) * Delta.x;
                    if (DistanceSq < 1000000.0)
                    {
                        Player->Objectives.PopFront();
                        bPathChanged = true;
                    }
                }
            }
        }
    }
    else
    {
        // A single path objective makes the objective after it carry flags 0x50.
        Player->bTradelanePathChecked = 0;
        UpdatePathObjectives(Player, &bPathChanged);
        if (bPathChanged)
        {
            std::list<FMissionObjective*>::iterator It = List.begin();
            int PathCount = 0;
            while (It != List.end() && (*It)->GetType() == 5)
            {
                ++It;
                ++PathCount;
            }
            if (PathCount == 1 && It != List.end())
                (*It)->Objective.Type |= 0x50;
        }
    }

    bool bCountChanged = false;
    if (!pub::SinglePlayer())
        Mission->UpdatePlayerCountObjective(Player, &bCountChanged);
    if (bReplaced || bPathChanged || bCountChanged)
        Player->SendObjectives();
}

// FUN_06f8b620.
bool FRmRuntimeMachine::UpdatePathObjectives(FMissionPlayer* Player, bool* bChanged)
{
    FMissionObjectiveList* List = &Player->Objectives;
    Vector Target;
    if (!ActiveObjectives.empty() && ActiveObjectives[0]->GetPosition(&Target))
    {
        FRmObjective* const First = ActiveObjectives[0];
        std::list<FNnPathLeg> Legs;
        unsigned int SystemId = GetSystemId();
        FSystemInfo* System = FindSystemInfo(&SystemId);
        FContentPlayerState* State = Player->PlayerState;
        Vector PlayerPosition;
        State->GetPosition(&PlayerPosition);
        // A player at the origin (every coordinate squared <= 1.0) gets no path; unordered
        // builds.
        bool const bPlaced =
            !(static_cast<double>(PlayerPosition.x) * PlayerPosition.x <= 1.0) ||
            !(static_cast<double>(PlayerPosition.y) * PlayerPosition.y <= 1.0) ||
            !(static_cast<double>(PlayerPosition.z) * PlayerPosition.z <= 1.0);
        if (bPlaced)
        {
            float const Range = 0.1f;
            Vector From;
            State->GetPosition(&From);
            BuildNnPath(System, &From, &Target, &Legs, 0x19, &Range, 1);

            int Ids = 0;
            int const MachineType = Offer->MachineType;
            if (MachineType == 1)
                Ids = ((std::rand() * 3) >> 15) + 0x3307;
            else if (MachineType == 2)
                Ids = ((std::rand() * 3) >> 15) + 0x3304;
            else if (MachineType == 3)
                Ids = ((std::rand() * 3) >> 15) + 0x330a;

            bool bWhere = false;
            if (State->ShipObject == nullptr && !List->Objectives.empty())
                bWhere = true;
            std::list<FMissionObjective*>::iterator Where = List->Objectives.begin();

            // RmObjReturnLoot: the first object found at the target. Retail reads the
            // buffer's first word even when nothing was found (stack garbage); the port's
            // buffer is 0.
            unsigned int TargetObjectId = 0;
            if (First->Key == RmObjReturnLoot)
            {
                unsigned int ScanSystem = System->SystemId;
                unsigned int Buffer[0x20] = {};
                unsigned int Count = 0;
                pub::System::ScanObjects(ScanSystem, Buffer, 0x20, Target, 0.0f, 0x600de0, Count);
                TargetObjectId = Buffer[0];
            }
            int const FirstType = State->ShipObject != nullptr ? 0x5a : 0x0a;
            *bChanged = Mission->BuildPathObjectives(System, &Legs, List, Ids, 1, 1,
                                                     TargetObjectId, FirstType, 9, 0xa,
                                                     bWhere ? &Where : nullptr);
        }
        return *bChanged;
    }

    // No target: the path objectives at the front are dropped.
    if (!List->Objectives.empty())
    {
        if (List->GetFrontType() == 5)
            *bChanged = true;
        while (!List->Objectives.empty() && List->GetFrontType() == 5)
            List->PopFront();
    }
    return *bChanged;
}

// FUN_06f8bac0. The vector index stays valid across
// RebuildObjective (erase + insert at the same place, no reallocation).
void FRmRuntimeMachine::OnPlayerJoined(unsigned int InPlayerId)
{
    SetPlayerAttitudes(FindContentPlayerState(InPlayerId));
    for (size_t Index = 0; Index < ActiveObjectives.size(); ++Index)
    {
        FRmObjective* const Objective = ActiveObjectives[Index];
        if (Objective->GetType() != 3)
            continue;
        FRmObjectiveType3* const Targets = static_cast<FRmObjectiveType3*>(Objective);
        unsigned int SystemId = 0;
        Targets->GetSystemId(&SystemId);
        Vector Where;
        Targets->GetPosition(&Where);
        Targets->SetTargets(&Targets->Ids, GetMissionPlayers(), SystemId, Where);
        RebuildObjective(Targets->Key);
    }
    RefreshObjectives();
}

// FUN_06f8b930. An attitude of -2.0 (DAT_06fbc2e8) means none.
void FRmRuntimeMachine::SetPlayerAttitudes(FContentPlayerState* Player)
{
    FRmRecordList ShipList = Ships->Query(0, 2, 0);
    for (std::list<FRmSpawnRecord*>::iterator It = ShipList.Records.begin();
         It != ShipList.Records.end(); ++It)
    {
        FRmSpawnRecord* const Record = *It;
        if (Record->Attitude == -2.0f)
            continue;
        int const PlayerRep = Player->GetReputation();
        int const ShipRep = Record->NpcShip->GetReputation();
        pub::Reputation::SetAttitude(ShipRep, PlayerRep, Record->Attitude);
    }
    FRmRecordList SolarList = Solars->Query(-1, 2, 0);
    for (std::list<FRmSpawnRecord*>::iterator It = SolarList.Records.begin();
         It != SolarList.Records.end(); ++It)
    {
        FRmSolarRecord* const Record = static_cast<FRmSolarRecord*>(*It);
        if (Record->Attitude == -2.0f)
            continue;
        int const PlayerRep = Player->GetReputation();
        pub::Reputation::SetAttitude(Record->Reputation, PlayerRep, Record->Attitude);
    }
}

// FUN_06f92680.
void FRmRuntimeMachine::SpawnSolars(std::list<FRmSolarSpawn>* InSolars)
{
    for (std::list<FRmSolarSpawn>::iterator It = InSolars->begin(); It != InSolars->end(); ++It)
        SpawnSolar(&*It);
}

// FUN_06f926b0.
bool FRmRuntimeMachine::SpawnSolar(FRmSolarSpawn* Solar)
{
    if (Offer->System == nullptr)
        return false;
    bool bCreated = false;

    pub::SpaceObj::SolarInfo Info;
    std::memset(&Info, 0, sizeof(Info));
    Info.Orientation.E[0][0] = 1.0f;
    Info.Orientation.E[1][1] = 1.0f;
    Info.Orientation.E[2][2] = 1.0f;
    Info.VibeGroupId = -1;
    Info.VisitValue = 1;
    Info.HitPts = -1;
    Costume SolarCostume;
    FSystemInfo* System = FindSystemInfo(&Solar->DestSys);
    std::strcpy(Info.Nickname, Solar->Nickname.Data);
    Info.Field_0x00 = Mission->ControllerId;
    Info.SolarArchID = Solar->ArchetypeId;
    Info.SystemID = System != nullptr ? System->SystemId : 0;
    Info.Position = Solar->Position;
    Info.HitPts = Solar->Hitpoints;
    std::memcpy(&Info.Orientation, &Solar->Orientation, sizeof(FMatrix));
    Info.LoadoutID = Solar->LoadoutId;
    Info.VoiceId = Solar->VoiceId;
    Info.DockingBaseId = Solar->BaseId;
    if (Solar->CostumeId != 0)
    {
        pub::GF::GetCostumeByID(static_cast<int>(Solar->CostumeId), SolarCostume);
        Info.CostumeHead = SolarCostume.PartIds[0];
        Info.CostumeBody = SolarCostume.PartIds[1];
        Info.CostumeLeftHand = SolarCostume.PartIds[2];
        Info.CostumeRightHand = SolarCostume.PartIds[3];
        for (int Index = 0; Index < 8; ++Index)
            Info.CostumeAccessory[Index] = SolarCostume.AccessoryIds[Index];
        Info.CostumeAccessoryCount = static_cast<int>(SolarCostume.AccessoryCount);
    }
    Info.VisitValue = 0;

    // The reputation is named by the killable solar's string (+0x507d0).
    FmtStr Name;
    Name.begin_mad_lib(static_cast<unsigned int>(Solar->Arch->StringId + 0x507d0));
    Name.end_mad_lib();
    {
        FmtStr Empty;
        pub::Reputation::Alloc(Info.VibeGroupId, Name, Empty);
    }

    FRmSolarRecord Record;
    Record.Name = Solar->Nickname.Data;
    Record.State = 2;
    Record.Loot = Solar->Loot;
    Record.Flags = static_cast<unsigned int>(Solar->ModeFlag);
    Record.Attitude = Solar->Attitude;
    Record.SolarType = Solar->SolarType;
    Record.Reputation = Info.VibeGroupId;
    Record.bHostile = true;
    pub::Reputation::SetAffiliation(Info.VibeGroupId, Solar->Faction);

    if (pub::SpaceObj::CreateSolar(Record.ObjectId, Info) == 0)
    {
        pub::AI::SetPersonalityParams Op;
        Op.StateGraphId = pub::StateGraph::get_state_graph("NOTHING", pub::StateGraph::LEADER);
        Op.DirectiveCallbackObject = nullptr;
        Op.ContentCallbackObject = nullptr;
        if (Solar->bPilot != 0)
        {
            Op.bSetPersonality = 1;
            Op.Value = Solar->Personality;
        }
        pub::AI::SubmitState(Record.ObjectId, &Op);
        if ((Solar->ModeFlag & 1) != 0)
        {
            pub::SpaceObj::SetInvincible(Record.ObjectId, true, Solar->bPlayerCanDamage != 0,
                                         Solar->MaxHpLoss);
        }
        if (!(Record.Attitude == -2.0f))
        {
            std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
            for (std::vector<FContentPlayerState*>::iterator It = Players->begin();
                 It != Players->end(); ++It)
            {
                int const PlayerRep = (*It)->GetReputation();
                pub::Reputation::SetAttitude(Info.VibeGroupId, PlayerRep, Record.Attitude);
            }
        }
        Solars->Records.Records.push_back(new FRmSolarRecord(Record));
        System->AddMissionObject(Record.ObjectId);
        bCreated = true;
    }
    return bCreated;
}

namespace
{
    // FUN_06efc7a0.
    void RmSetIdentity(Matrix* Out)
    {
        std::memset(Out, 0, sizeof(Matrix));
        Out->E[0][0] = 1.0f;
        Out->E[1][1] = 1.0f;
        Out->E[2][2] = 1.0f;
    }

    // The spawn point search of FUN_06f915a0: on success a second search from a point raised
    // by Lift (FUN_06f0bb80, float adds) with SecondClearance. The second result is not
    // tested.
    void RmFindSpawnPoint(FRmShipGroup const* Group, float StartRadius, float Clearance,
                          float SecondClearance, float Lift,
                          pub::SpaceObj::PosSelectionType Selection, Vector* OutPosition)
    {
        Transform Base;
        RmSetIdentity(&Base.Rotation);
        Base.Translation = Group->Position;
        unsigned int SystemId = Group->System->SystemId;
        if (pub::SpaceObj::GetEmptyPos(SystemId, Base, StartRadius, Clearance, Selection,
                                       *OutPosition) == 0)
            return;
        Clearance = SecondClearance;
        Vector Raised;
        Raised.x = Group->Position.x + 0.0f;
        Raised.y = Group->Position.y + Lift;
        Raised.z = Group->Position.z + 0.0f;
        RmSetIdentity(&Base.Rotation);
        Base.Translation = Raised;
        SystemId = Group->System->SystemId;
        pub::SpaceObj::GetEmptyPos(SystemId, Base, StartRadius, Clearance, Selection,
                                   *OutPosition);
    }

    // The stores into the copied JobStruct (personality +0x2a0) inlined in FUN_06f915a0, in
    // retail order; AttackPreferences gets four rows and the terminator class 12.
    void RmTuneShipJob(pub::AI::Personality::JobStruct* Job)
    {
        float const Range = 2000.0f + 2000.0f;          // DAT_06fbc2cc doubled
        int const Kinds[4] = {8, 0xa, 0xb, 0xc};
        int const Flags = 0xf;
        pub::AI::Personality::JobStruct::FAttackPreference* const Prefs = Job->AttackPreferences;
        Job->FleeSceneThreatStyle = 4;                  // +0x3c4
        Job->SceneToughnessThreshold = 4;               // +0x2a4
        Job->Field_00 = 0;                              // +0x2a0
        Prefs[0].ShipClass = 0;                         // +0x2b0
        Prefs[0].Range = Range;                         // +0x2b4
        Prefs[1].Range = Range;                         // +0x2c0
        Prefs[2].Range = Range;                         // +0x2cc
        Prefs[3].Range = Range;                         // +0x2d8
        std::memset(&Job->FleeWhenHullDamagedPercent, 0, sizeof(float));   // +0x3c8 dword 0
        Job->bFleeWhenLeaderFleesStyle = 0;             // +0x3cc
        Job->bFleeNoWeaponsStyle = 0;                   // +0x3cd
        Prefs[0].WeaponFlags = 1;                       // +0x2b8
        Prefs[1].ShipClass = Kinds[0];                  // +0x2bc
        Prefs[1].WeaponFlags = Flags;                   // +0x2c4
        Prefs[2].ShipClass = Kinds[1];                  // +0x2c8
        Prefs[2].WeaponFlags = Flags;                   // +0x2d0
        Prefs[3].ShipClass = Kinds[2];                  // +0x2d4
        Prefs[3].WeaponFlags = Flags;                   // +0x2dc
        Prefs[4].ShipClass = Kinds[3];                  // +0x2e0
    }
}

// FUN_06f915a0. Locals in retail's construction order. Ships launched from a dock
// (DockTarget != 0) get no record here and the result is false.
bool FRmRuntimeMachine::SpawnShipGroup(FRmShipGroup* Group, bool bNotFirst)
{
    bool bSpawned = false;
    std::list<FContentNpcShipParams> Blocks;
    std::vector<FRmShipRecord> Records;
    Group->bSpawned = 1;
    if (!Group->Ships.empty())
        Records.reserve(Group->Ships.size());

    // Retail does not initialise the point, and need not: GetEmptyPos (Server 06cf9bd0) fails
    // only on a negative radius / clearance or a selection type above 3, and every other exit
    // writes the point.
    Vector SpawnPoint;
    if (!bNotFirst)
        RmFindSpawnPoint(Group, 350.0f, 50.0f, 50.0f, 250.0f,
                         pub::SpaceObj::POS_SEL_3D_VOLUME, &SpawnPoint);
    else
        RmFindSpawnPoint(Group, 2625.0f, 1000.0f, 250.0f, 150.0f,
                         pub::SpaceObj::POS_SEL_YZ_PLANE, &SpawnPoint);

    for (std::vector<FRmShipSpawn>::iterator Ship = Group->Ships.begin();
         Ship != Group->Ships.end(); ++Ship)
    {
        Blocks.push_back(FContentNpcShipParams());
        Records.push_back(FRmShipRecord());
        FContentNpcShipParams* Block = &Blocks.back();
        FRmShipRecord* Record = &Records.back();

        // The name: the offer's target name on the target ship, a random name, or the [NPC]
        // block's.
        FNpcNameParams Name;
        if (Ship->TargetName != nullptr)
        {
            Name.First = Ship->TargetName->First;
            Name.Second = Ship->TargetName->Second;
            Name.Affiliation = Ship->TargetName->Affiliation;
            Name.Gender = Ship->TargetName->Gender;
        }
        if (Ship->TargetName != nullptr || Ship->bRandomName != 0)
        {
            if (Ship->TargetName == nullptr)
            {
                PickNpcNameGender(Ship->Npc.Affiliation, &Name);             // FUN_06f319b0
                FNpcNameRequest Request;                                    // FUN_06f31650
                BuildNpcName(Ship->Npc.Affiliation, &Request, &Name, true); // FUN_06f319d0
            }
            if (Block->ReputationHandle != nullptr)
            {
                Block->ReputationHandle->Release();
                Block->ReputationHandle = nullptr;
            }
            CreateNpcNameReputation(&Name, &Block->ReputationHandle);       // FUN_06f31910
        }
        else
        {
            if (Block->ReputationHandle != nullptr)
            {
                Block->ReputationHandle->Release();
                Block->ReputationHandle = nullptr;
            }
            CreateMsnNpcReputation(&Ship->Npc, &Block->ReputationHandle);   // FUN_06f31d40
        }

        // The npc_ship_arch result is not tested.
        Block->Nickname = Ship->Nickname.Data;                              // FUN_06ed0160
        ApplyNpcShipArchById(Block, &Ship->Npc.NpcShipArch.Id, nullptr, nullptr);
        ApplyNpcShipFaction(Ship->Npc.Affiliation, Name.Gender, Block);
        Block->CreationType = 1;
        std::memcpy(&Block->Orientation, &Group->Orientation, sizeof(FMatrix));
        Block->ControllerId = Mission->ControllerId;
        Block->LabelIds = Ship->LabelIds;
        Block->DockIndex = Group->ArrivalDockIndex;
        Block->Cargo.clear();
        for (std::vector<FMissionShipCargo>::iterator Cargo = Ship->Cargo.begin();
             Cargo != Ship->Cargo.end(); ++Cargo)
        {
            pub::SpaceObj::CargoDesc Desc;
            Desc.GoodId = Cargo->GoodId;
            Desc.Count = static_cast<unsigned int>(Cargo->Count);
            Block->Cargo.push_back(Desc);
        }
        Block->Priority = Ship->Priority;
        Block->HitPoints = Ship->HitPoints;

        Record->Name = Ship->Nickname.Data;
        Record->State = 2;
        Record->Loot = Ship->Loot;
        Record->Group = Group->Serial;
        Record->Flags = static_cast<unsigned int>(Ship->TargetFlags);
        Record->Attitude = Group->Attitude;
        Record->Kind = Ship->ShipKind;
        Record->bHostile = Ship->Npc.Affiliation == Offer->HostileGroup;

        // 2000 is DAT_06fbc2cc.
        TRefHandle<FObjListDirective> StayInRange;
        CreateObjListDirective(OBJLIST_STAY_IN_RANGE, &StayInRange.Pointer);
        static_cast<FObjListStayInRange*>(StayInRange.Pointer)->SetSphere(&Group->Position, 2000.0f);
        {
            FObjListDirective* const Added = StayInRange.Pointer;
            Block->Directives.Items.push_back(Added);
            Added->AddRef();
        }
        if (Ship == Group->Ships.begin())
        {
            TRefHandle<FObjListDirective> GotoGroup;
            CreateObjListDirective(OBJLIST_GOTO_VEC, &GotoGroup.Pointer);
            FObjListGotoVec* const Goto = static_cast<FObjListGotoVec*>(GotoGroup.Pointer);
            Goto->SetDestination(&Group->Position, 500.0f, -1.0f, 0, false, false);
            Goto->Op.Priority = 1;
            FObjListDirective* const Added = GotoGroup.Pointer;
            Block->Directives.Items.push_back(Added);
            Added->AddRef();
        }
    }

    // Retail does not test FindFormation's (FUN_06efd1a0) result here.
    if (!Group->Formation.IsEmpty())
    {
        FTransform Placement;
        std::memset(&Placement.Rotation, 0, sizeof(Placement.Rotation));
        Placement.Rotation.D[0][0] = 1.0f;
        Placement.Rotation.D[1][1] = 1.0f;
        Placement.Rotation.D[2][2] = 1.0f;
        Placement.Translation.X = SpawnPoint.x;
        Placement.Translation.Y = SpawnPoint.y;
        Placement.Translation.Z = SpawnPoint.z;
        PlaceFormationBlocks(&Blocks, FindFormation(&Group->Formation.Id), &Placement);
    }

    // The dock target: the solar of the group's name (not null-tested), else ArrivalObjId.
    // CreateNpcShips is TU58 FUN_06f73810.
    unsigned int DockTarget = static_cast<unsigned int>(Group->ArrivalObjId);
    if (Group->Name.Length != 0)
        DockTarget = Solars->Records.FindByName(Group->Name)->ObjectId;
    std::list<FContentNpcShip*> Created;
    Group->System->CreateNpcShips(&Blocks, DockTarget, &Created);
    if (DockTarget != 0)
        return bSpawned;

    // Block i's ship goes with record i (retail walks the created list without an end test).
    // The record copy is FUN_06f92440.
    std::list<FContentNpcShip*>::iterator Spawned = Created.begin();
    for (std::vector<FRmShipRecord>::iterator Record = Records.begin(); Record != Records.end();
         ++Record, ++Spawned)
    {
        FContentNpcShip* const Npc = *Spawned;
        Record->ObjectId = Npc->SpaceObjId;
        Record->NpcShip = Npc;
        pub::AI::Personality Tuned = *Npc->GetPersonality();
        RmTuneShipJob(&Tuned.JobParams);
        pub::AI::SetPersonalityParams Op;
        Op.bSetPersonality = 1;
        Op.Value = Tuned;
        Op.StateGraphId = pub::AI::get_state_graph_id(Npc->SpaceObjId);
        pub::AI::SubmitState(Npc->SpaceObjId, &Op);
        Ships->Records.Records.push_back(new FRmShipRecord(*Record));
    }

    // -0.9 is DAT_06fbc2b8, set both ways with every other alive ship of a different
    // attitude.
    std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
    for (std::vector<FRmShipRecord>::iterator Record = Records.begin(); Record != Records.end();
         ++Record)
    {
        if (Record->Attitude == -2.0f)
            continue;
        for (std::vector<FContentPlayerState*>::iterator Player = Players->begin();
             Player != Players->end(); ++Player)
        {
            int const PlayerRep = (*Player)->GetReputation();
            int const ShipRep = Record->NpcShip->GetReputation();
            pub::Reputation::SetAttitude(ShipRep, PlayerRep, Record->Attitude);
        }
        for (std::list<FRmSpawnRecord*>::iterator It = Ships->Records.Records.begin();
             It != Ships->Records.Records.end(); ++It)
        {
            FRmSpawnRecord* const Other = *It;
            if (Other->State != 2)
                continue;
            if (Other->ObjectId == Record->ObjectId)
                continue;
            if (Other->Attitude == Record->Attitude)
                continue;
            {
                int const OtherRep = Other->NpcShip->GetReputation();
                int const ShipRep = Record->NpcShip->GetReputation();
                pub::Reputation::SetAttitude(ShipRep, OtherRep, -0.9f);
            }
            {
                int const ShipRep = Record->NpcShip->GetReputation();
                int const OtherRep = Other->NpcShip->GetReputation();
                pub::Reputation::SetAttitude(OtherRep, ShipRep, -0.9f);
            }
        }
    }
    bSpawned = true;
    return bSpawned;
}

// FUN_06f90f60. Only a critical loot (Kind 2) gets a record and a
// message; every loot is created (a relative one only when its parent object exists).
bool FRmRuntimeMachine::SpawnLoot(FRmLootSpawn* InLoot)
{
    FSystemInfo* System = Offer->System;
    if (System == nullptr)
        return false;
    pub::SpaceObj::LootInfo* Info = &InLoot->Info;
    Info->SystemID = System->SystemId;
    if (InLoot->Kind == 2)
        Info->bMission = 1;
    if (InLoot->CriticalLoot != nullptr)
        Info->NameId = static_cast<unsigned int>(InLoot->CriticalLoot->Name + 0x50bb8);
    if (InLoot->bRelPos != 0)
    {
        unsigned int const Parent = Info->OwnerObjId;
        if ((Parent & 0x3fffffff) != 0)
        {
            Vector ParentPosition;
            Matrix ParentOrientation;
            pub::SpaceObj::GetLocation(Parent, ParentPosition, ParentOrientation);
            Vector Linear;
            Vector Angular;
            pub::SpaceObj::GetMotion(Parent, Linear, Angular);
            Info->Position.x = ParentPosition.x + InLoot->RelPosOffset.x;
            Info->Position.y = ParentPosition.y + InLoot->RelPosOffset.y;
            Info->Position.z = ParentPosition.z + InLoot->RelPosOffset.z;
            Info->Velocity = Linear;
            Info->AngularVelocity = Angular;
            std::memset(&Info->Orientation, 0, sizeof(Info->Orientation));
            Info->Orientation.E[0][0] = 1.0f;
            Info->Orientation.E[1][1] = 1.0f;
            Info->Orientation.E[2][2] = 1.0f;
            pub::SpaceObj::CreateLoot(InLoot->SpaceObjId, *Info);
        }
    }
    else
    {
        pub::SpaceObj::CreateLoot(InLoot->SpaceObjId, *Info);
    }

    FRmLootRecord Record;
    Record.ObjectId = InLoot->SpaceObjId;
    Record.ParentId = Info->OwnerObjId;
    Record.Units = static_cast<int>(Info->Units);
    Record.CriticalLootMode = InLoot->CriticalLootMode;
    Record.Kind = InLoot->Kind;
    Record.HolderId = 0;
    Record.CargoSlot = SUBOBJ_ID_NONE;
    Record.GoodId = 0;
    if (InLoot->Kind == 2)
    {
        Record.Flags = 2;
        if (InLoot->CriticalLootMode != 1)
            pub::SpaceObj::SetInvincible2(Record.ObjectId, false, true, 1.0f);
        Loot->Records.Records.push_back(new FRmLootRecord(Record));
        FRmLootMessage Message(GRmMsgCriticalLootSpawned, Record.ObjectId, 0);
        Loot->SendToOwner(&Message);
    }
    return true;
}

// FUN_06f90f20.
void FRmRuntimeMachine::SpawnRecordLoot(FRmSpawnRecord* Record)
{
    for (std::vector<FRmLootSpawn>::iterator It = Record->Loot.begin(); It != Record->Loot.end();
         ++It)
    {
        It->Info.OwnerObjId = Record->ObjectId;
        SpawnLoot(&*It);
    }
}

// FUN_06f8ade0.
void FRmRuntimeMachine::AddObjective(int Key, FRmObjective* Objective)
{
    Objective->Key = Key;
    FMissionObjective* Built = Objective->CreateMissionObjective(Mission, this);
    Built->BuildDescription();
    ActiveObjectives.push_back(Objective);
    Objectives.Objectives.push_back(Built);
    bObjectivesChanged = true;
    RefreshObjectives();
}

// The Objectives node at the index of Where, one further when a type 5 objective is anywhere
// in the list.
std::list<FMissionObjective*>::iterator FRmRuntimeMachine::ObjectiveListPosition(
    std::vector<FRmObjective*>::iterator Where)
{
    int Offset = 0;
    for (std::list<FMissionObjective*>::iterator It = Objectives.Objectives.begin();
         It != Objectives.Objectives.end(); ++It)
    {
        if ((*It)->GetType() == 5)
        {
            Offset = 1;
            break;
        }
    }
    int Steps = static_cast<int>(Where - ActiveObjectives.begin()) + Offset;
    std::list<FMissionObjective*>::iterator Node = Objectives.Objectives.begin();
    while (Steps > 0)
    {
        --Steps;
        ++Node;
    }
    return Node;
}

// FUN_06f8ae70. The list position is taken before the objective
// is built (the type 5 scan comes first in retail).
void FRmRuntimeMachine::InsertObjective(int Key, FRmObjective* Objective,
                                        std::vector<FRmObjective*>::iterator Where)
{
    Objective->Key = Key;
    std::list<FMissionObjective*>::iterator Node = ObjectiveListPosition(Where);
    FMissionObjective* Built = Objective->CreateMissionObjective(Mission, this);
    Built->BuildDescription();
    ActiveObjectives.insert(Where, Objective);
    Objectives.Objectives.insert(Node, Built);
    bObjectivesChanged = true;
    RefreshObjectives();
}

// FUN_06f8af40.
bool FRmRuntimeMachine::RebuildObjective(int Key)
{
    std::vector<FRmObjective*>::iterator It = FindObjective(Key);
    if (It == ActiveObjectives.end())
        return false;
    std::list<FMissionObjective*>::iterator Node = ObjectiveListPosition(It);
    FRmObjective* const Objective = *It;
    size_t const Index = static_cast<size_t>(It - ActiveObjectives.begin());
    ActiveObjectives.erase(It);
    std::list<FMissionObjective*>::iterator Next = Node;
    ++Next;
    Objectives.EraseRange(Node, Next);
    InsertObjective(Key, Objective, ActiveObjectives.begin() + Index);
    return true;
}

// FUN_06f8b030.
bool FRmRuntimeMachine::RemoveObjective(int Key)
{
    std::vector<FRmObjective*>::iterator It = FindObjective(Key);
    if (It == ActiveObjectives.end())
        return false;
    std::list<FMissionObjective*>::iterator Node = ObjectiveListPosition(It);
    ActiveObjectives.erase(It);
    std::list<FMissionObjective*>::iterator Next = Node;
    ++Next;
    Objectives.EraseRange(Node, Next);
    bObjectivesChanged = true;
    RefreshObjectives();
    return true;
}

// FUN_06f8b110.
std::vector<FRmObjective*>::iterator FRmRuntimeMachine::FindObjective(int Key)
{
    std::vector<FRmObjective*>::iterator It = ActiveObjectives.begin();
    for (; It != ActiveObjectives.end(); ++It)
    {
        if ((*It)->Key == Key)
            break;
    }
    return It;
}

// =======================================================================================
// FRmVignetteMachine
// =======================================================================================

FRmVignetteMachine::FRmVignetteMachine(FRmVignetteOffer* InOffer)
    : FRmRuntimeMachine(InOffer)
    , VignetteOffer(InOffer)
{
}

// FUN_06f8a6d0.
FRmVignetteMachine::~FRmVignetteMachine()
{
}

// FUN_06f8b140 (on the +0x54 sub-object).
bool FRmVignetteMachine::GetTextParam(int Param, FmtStr* Out)
{
    return VignetteOffer->GetTextParam(Param, Out);
}

// LAB_06f8a820.
void FRmVignetteMachine::Shutdown()
{
    // The population exclusion spheres (10000.0, DAT_06fbc2ec).
    if (VignetteOffer->TargetShips.bActive)
    {
        Vector const& Center = VignetteOffer->TargetShips.Position;
        SpacePopRemoveExclusionSphere(Center.x, Center.y, Center.z, 10000.0f);
    }
    {
        Vector const& Center = VignetteOffer->Solars.bActive ? VignetteOffer->Solars.Position
                                                              : VignetteOffer->Ships.Position;
        SpacePopRemoveExclusionSphere(Center.x, Center.y, Center.z, 10000.0f);
    }

    // A ship that is not a formation wingman also gets a type 4 guide.
    FRmRecordList ShipList = Ships->Query(0, 2, 0);
    pub::AI::SetZoneBehaviorParams ZoneParams;
    ZoneParams.InvertMode = 2;
    for (std::list<FRmSpawnRecord*>::iterator It = ShipList.Records.begin();
         It != ShipList.Records.end(); ++It)
    {
        FContentNpcShip* const Ship = (*It)->NpcShip;
        Ship->Longevity = 24.0f;
        pub::AI::SubmitState(Ship->SpaceObjId, &ZoneParams);
        if (Ship->FormationSlot == 0 || Ship->FormationId == 0xffffffffu)
        {
            FGuideDesc Desc(4, Ship);
            Ship->SetGuideFromDesc(&Desc, true);
        }
    }

    FRmRecordList SolarList = Solars->Query(-1, 2, 0);
    for (std::list<FRmSpawnRecord*>::iterator It = SolarList.Records.begin();
         It != SolarList.Records.end(); ++It)
        VignetteOffer->System->SetObjectFlag((*It)->ObjectId, true);

    FRmRecordList LootList = Loot->Query(0, 0, 0, 2, 0);
    for (std::list<FRmSpawnRecord*>::iterator It = LootList.Records.begin();
         It != LootList.Records.end(); ++It)
    {
        FRmLootRecord* const Record = static_cast<FRmLootRecord*>(*It);
        if (Record->Kind == 2)
            pub::SpaceObj::Destroy(Record->ObjectId, static_cast<DestroyType>(0));
        else
            pub::SpaceObj::SetInvincible(Record->ObjectId, false, true, 0.0f);
    }

    UnmarkObjects(GetMissionPlayers());
}

// LAB_06f8bb80. No empty test on the players.
void FRmVignetteMachine::OnSucceeded()
{
    FmtStr* Text = VignetteOffer->RewardText.Build(this);
    std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
    for (std::vector<FContentPlayerState*>::iterator It = Players->begin(); It != Players->end();
         ++It)
    {
        FContentPlayerState* const Player = *It;
        // Only strictly closer players; unordered is far.
        bool bNear = false;
        if (Player->ShipObject != nullptr)
        {
            Vector Where;
            Player->GetPosition(&Where);
            double const Limit = 10000.0 * 10000.0;
            bNear = RmDistanceSq(Where, Position) < Limit;
        }
        if (bNear)
        {
            Player->SetFlags(PLAYER_FLAG_MISSION_AREA);
            Player->SetCountdown(120.0f);
        }
        else
        {
            Player->SetFlags(0);
        }
    }
    unsigned int const FirstPlayer = (*Players->begin())->PlayerId;
    pub::Player::DisplayMissionMessage(FirstPlayer, *Text, static_cast<MissionMessageType>(3),
                                       true);
}

// FUN_06f8ab10.
void FRmVignetteMachine::UnmarkObjects(std::vector<FContentPlayerState*> const* Players)
{
    FRmRecordList ShipList = Ships->Query(0, 2, 0);
    for (std::list<FRmSpawnRecord*>::iterator It = ShipList.Records.begin();
         It != ShipList.Records.end(); ++It)
    {
        FContentNpcShip* const Ship = (*It)->NpcShip;
        for (std::vector<FContentPlayerState*>::const_iterator Player = Players->begin();
             Player != Players->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, Ship->SpaceObjId, 0);
    }
    FRmRecordList SolarList = Solars->Query(-1, 2, 0);
    for (std::list<FRmSpawnRecord*>::iterator It = SolarList.Records.begin();
         It != SolarList.Records.end(); ++It)
    {
        for (std::vector<FContentPlayerState*>::const_iterator Player = Players->begin();
             Player != Players->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, (*It)->ObjectId, 0);
    }
    FRmRecordList LootList = Loot->Query(0, 0, 0, 2, 0);
    for (std::list<FRmSpawnRecord*>::iterator It = LootList.Records.begin();
         It != LootList.Records.end(); ++It)
    {
        for (std::vector<FContentPlayerState*>::const_iterator Player = Players->begin();
             Player != Players->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, (*It)->ObjectId, 0);
    }
    // The held critical loot (State 4) leaves its holder's cargo. Retail assigns the query
    // into the loot list above (FuncInfo 06fc6698 has no fourth list).
    LootList.Records = Loot->Query(0, 0, 0, 4, 0).Records;
    for (std::list<FRmSpawnRecord*>::iterator It = LootList.Records.begin();
         It != LootList.Records.end(); ++It)
    {
        FRmLootRecord* const Record = static_cast<FRmLootRecord*>(*It);
        pub::Player::RemoveCargo(Record->HolderId, Record->CargoSlot, 1);
    }
}

// FUN_06f8c460 (the vignette machine's slot 2) and its helpers: RandomMissionVignette.cpp.

// FUN_06f890f0.
bool __cdecl CreateRmRuntimeMachine(FRmOffer* Offer, FRmRuntimeMachine** Out)
{
    FRmRuntimeMachine* Machine = nullptr;
    if (Offer->MachineType > 0 && Offer->MachineType <= 3)
        Machine = new FRmVignetteMachine(static_cast<FRmVignetteOffer*>(Offer));
    *Out = Machine;
    return Machine != nullptr;
}

// FUN_06f877c0.
FRmRecordList FRmLootMachine::Query(int InCriticalLootMode, int Kind, unsigned int InParentId,
                                    int InState, unsigned int InHolderId)
{
    FRmRecordList Result;
    for (std::list<FRmSpawnRecord*>::iterator It = Records.Records.begin();
         It != Records.Records.end(); ++It)
    {
        FRmLootRecord* const Record = static_cast<FRmLootRecord*>(*It);
        if (InParentId != 0 && Record->ParentId != InParentId)
            continue;
        if (InHolderId != 0 && Record->HolderId != InHolderId)
            continue;
        if (InCriticalLootMode != 0 && Record->CriticalLootMode != InCriticalLootMode)
            continue;
        if (Kind != 0 && Record->Kind != Kind)
            continue;
        if (InState != 0 && Record->State != InState)
            continue;
        Result.Records.push_back(Record);
    }
    return Result;
}
