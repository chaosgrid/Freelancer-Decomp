// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// TU61 -- the vignette runtime machine's state machine (FRmVignetteMachine::OnStateEvent,
// FUN_06f8c460) and its helpers FUN_06f8bc70..FUN_06f8c3c0 / FUN_06f90b40 / FUN_06f90b70, plus
// the two TU61 trigger classes it builds (vtables 06fbca40 / 06fbca84). Declarations in
// RandomMissionMachine.h.
//
// FUN_06f8c460 is one function in retail; the port splits it by state (Handle*,
// ERmMachineState). The objective keys are the generator's FRmObjectiveMap keys
// (ERmObjectiveKey, RandomMissionRuntime.h).

#include "RandomMissions/RandomMissionMachine.h"

#include "Bases/BaseDB.h"                 // FContentBase, FindContentBaseInfo
#include "NPCs/BehaviorCmds.h"           // FContentShipBase directives
#include "Core/ContentInternal.h"        // GetContentGameTime, GetContentTimeDistance, SpacePop*
#include "Core/ContentPlayerState.h"     // FContentPlayerState, FindContentPlayerState*
#include "NPCs/DockQueues.h"             // RemoveFromFormation
#include "Comms/Gcs.h"                    // GGcsConversationManager
#include "Missions/MissionObjList.h"         // CreateObjListDirective, FObjListStayInRange, FObjListGotoVec
#include "NPCs/NpcShip.h"                // FContentNpcShip
#include "RandomMissions/RandomMissionObject.h"    // FRandomMissionObject
#include "Core/RefHandle.h"              // TRefHandle
#include "Universe/SystemInfo.h"             // FSystemInfo::SystemId
#include "common_misc.h"            // SMM_TIMEOUT
#include "common_server.h"
#include "server_cobject.h"         // pub::SpaceObj::GetHealth
#include "server_gameobject.h"      // FSubObjDamageEvent (controller message 0xd)

#include <cstring>

// LAB_06f84280 (TU61 static init): 3500.0 (DAT_06fbc2c8) + 6000.0 (DAT_06fbc2b4).
float GRmVignetteOuterRadius = 3500.0f + 6000.0f;

namespace
{
    // DAT_06fd3c0c (.bss, 0): the cookie counter of FUN_06f8c460's function-local statics
    // (guard bytes DAT_06fd3b68 / DAT_06fd3b44); every cookie is the counter's value the first
    // time its code is reached, post-incremented.
    unsigned int GRmVignetteNextCookie = 0;

    unsigned int RmNextVignetteCookie()
    {
        return GRmVignetteNextCookie++;
    }

    // The three statics initialised on every entry of FUN_06f8c460, in this order (guard bits
    // 1, 2, 4 of DAT_06fd3b68).
    struct FRmVignetteEntryCookies
    {
        unsigned int LeaveArea;     // DAT_06fd3b64: FRmProximityLeaveTrigger of the combat area
        unsigned int ReenterArea;   // DAT_06fd3b60: FRmProximityTrigger back into the area
        unsigned int Countdown;     // DAT_06fd3b5c: FRmTimerTrigger of the countdown
    };

    FRmVignetteEntryCookies const& RmVignetteEntryCookies()
    {
        static FRmVignetteEntryCookies const Cookies = {RmNextVignetteCookie(),
                                                        RmNextVignetteCookie(),
                                                        RmNextVignetteCookie()};
        return Cookies;
    }

    // The message test FUN_06f8c460 repeats for every message.
    StateMachineMessage const* RmMessageOf(StateMachine const* Machine, SMEventData const* Event,
                                           unsigned int Type)
    {
        if (Event->get_event_type() != SMET_Message)
            return nullptr;
        StateMachineMessage const* Message = static_cast<SMMessage const*>(Event)->Message;
        if (Message->DeliveryState != -1 && Machine->get_state() != Message->DeliveryState)
            return nullptr;
        if (Message->MessageType != Type)
            return nullptr;
        return Message;
    }

    // A trigger-fired message (DAT_06fd3a18) whose +0x1c is Cookie.
    FRmTriggerFiredMessage const* RmFiredTriggerOf(StateMachine const* Machine,
                                                   SMEventData const* Event, unsigned int Cookie)
    {
        StateMachineMessage const* Message = RmMessageOf(Machine, Event, GRmMsgTriggerFired);
        if (Message == nullptr)
            return nullptr;
        FRmTriggerFiredMessage const* Fired = static_cast<FRmTriggerFiredMessage const*>(Message);
        if (Fired->Cookie != Cookie)
            return nullptr;
        return Fired;
    }

    using pub::Controller::EMessage;

    // A controller event (type 4) of message MessageId. The event's int +0x08 (named
    // ControllerId in common_net.h) is the controller message id, cast back after the type test.
    SMControllerEvent const* RmControllerOf(SMEventData const* Event, EMessage MessageId)
    {
        if (Event->get_event_type() != SMET_Controller)
            return nullptr;
        SMControllerEvent const* Controller = static_cast<SMControllerEvent const*>(Event);
        if (static_cast<EMessage>(Controller->ControllerId) != MessageId)
            return nullptr;
        return Controller;
    }

    // The enter event test inlined in every state.
    bool RmIsEnterEvent(SMEventData const* Event)
    {
        return Event->get_event_type() < SMET_Controller && Event->get_event_type() == SMET_Enter;
    }

    // FUN_06f90b40 (and its inline copies).
    bool RmHasUnspawnedGroup(std::vector<FRmShipGroup> const* Groups)
    {
        for (std::vector<FRmShipGroup>::const_iterator It = Groups->begin(); It != Groups->end();
             ++It)
        {
            if (It->bRequired != 0 && It->bSpawned == 0)
                return true;
        }
        return false;
    }

    // The inline choice of the main battle centre: the solars' when that component is active,
    // else the ships'.
    Vector const* RmMainBattleCenter(FRmVignetteOffer const* Offer)
    {
        if (Offer->Solars.bActive)
            return &Offer->Solars.Position;
        return &Offer->Ships.Position;
    }

    // FUN_06f931c0 / FUN_06f930a0: the objective of Key in a component's map (insert of
    // {Key, null} when missing, as operator[]).
    FRmParamText* RmComponentObjective(FRmOfferComponent* Component, int Key)
    {
        return Component->Objectives.Objectives[Key];
    }
}

// =======================================================================================
// TU61 triggers
// =======================================================================================

FRmShipCountTrigger::FRmShipCountTrigger(unsigned int InCookie, unsigned int InOwnerId,
                                         FRmRecordList* InRecords, int InThreshold)
    : FRmTrigger(InCookie, InOwnerId, true, 1.0f)
    , Records(InRecords)
    , Threshold(InThreshold)
{
}

// LAB_06f92dd0.
bool FRmShipCountTrigger::OnArmed(SMEventData const* Event, int const& State)
{
    if (State != RmTriggerArmed)
        return false;
    if (RmIsEnterEvent(Event))
    {
        if (CountAlive() <= Threshold)
            set_state(RmTriggerFired);
        return true;
    }
    if (RmControllerOf(Event, EMessage::ObjectDestroyed) != nullptr)
    {
        if (CountAlive() <= Threshold)
            set_state(RmTriggerFired);
        return true;
    }
    return false;
}

// LAB_06f92e50.
int FRmShipCountTrigger::CountAlive()
{
    int Count = 0;
    for (std::list<FRmSpawnRecord*>::iterator It = Records->Records.begin();
         It != Records->Records.end(); ++It)
    {
        if ((*It)->bHostile && (*It)->State == RmRecordAlive)
            ++Count;
    }
    return Count;
}

FRmShipHealthTrigger::FRmShipHealthTrigger(unsigned int InCookie, unsigned int InOwnerId,
                                           FRmRecordList* InRecords, int InThreshold, int InKind)
    : FRmTrigger(InCookie, InOwnerId, true, 1.0f)
    , Records(InRecords)
    , Threshold(InThreshold)
    , Kind(InKind)
{
}

// FUN_06f92ed0.
bool FRmShipHealthTrigger::OnArmed(SMEventData const* Event, int const& State)
{
    if (State != RmTriggerArmed)
        return false;
    if (RmIsEnterEvent(Event))
    {
        int const Lowest = GetLowestHealth();
        if (Lowest != -1 && Lowest <= Threshold)
            set_state(RmTriggerFired);
        return true;
    }
    SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::ObjectDamaged);
    if (Controller == nullptr)
        return false;

    // The record of the damaged object, of this trigger's kind.
    FSubObjDamageEvent const* Damage = static_cast<FSubObjDamageEvent const*>(Controller->DataPtr);
    FRmSpawnRecord* Record = nullptr;
    for (std::list<FRmSpawnRecord*>::iterator It = Records->Records.begin();
         It != Records->Records.end(); ++It)
    {
        if ((*It)->ObjectId == Damage->ObjId)
        {
            Record = *It;
            break;
        }
    }
    if (Record == nullptr)
        return true;
    if (static_cast<FRmShipRecord*>(Record)->Kind != Kind)
        return true;

    // -100.0 is DAT_06fbcab8; RelativeDamage is hit points lost / max (Server 06ceb5d8).
    // Retail keeps both products on the x87 stack and truncates them with _ftol; the doubles
    // below are x87 registers as well (/arch:IA32), so they round the same way.
    unsigned int const ObjectId = Record->ObjectId;
    float Current = 0.0f;
    float Max = 0.0f;
    pub::SpaceObj::GetHealth(ObjectId, Current, Max);
    int const Health = static_cast<int>(static_cast<double>(Current) /
                                        static_cast<double>(Max) * -100.0);
    int const DamagePercent =
        static_cast<int>(static_cast<double>(Damage->RelativeDamage) * -100.0);
    if (DamagePercent - Health < Threshold)
    {
        SetActivator(Record->ObjectId);
        set_state(2);
    }
    return true;
}

// LAB_06f92ff0. 100.0 is DAT_06fb8574; x87 as in OnArmed above.
int FRmShipHealthTrigger::GetLowestHealth()
{
    int Lowest = -1;
    for (std::list<FRmSpawnRecord*>::iterator It = Records->Records.begin();
         It != Records->Records.end(); ++It)
    {
        FRmSpawnRecord* const Record = *It;
        if (!Record->bHostile || Record->State != RmRecordAlive)
            continue;
        if (static_cast<FRmShipRecord*>(Record)->Kind != Kind)
            continue;
        unsigned int const ObjectId = Record->ObjectId;
        float Current = 0.0f;
        float Max = 0.0f;
        pub::SpaceObj::GetHealth(ObjectId, Current, Max);
        int const Health = static_cast<int>(static_cast<double>(Current) /
                                            static_cast<double>(Max) * 100.0);
        if (Lowest == -1 || Health < Lowest)
            Lowest = Health;
    }
    return Lowest;
}

// =======================================================================================
// FRmVignetteMachine helpers
// =======================================================================================

// FUN_06f8bc70. The alive flagged ships are looked up again by id (not null-tested) before
// their flags are cleared.
void FRmVignetteMachine::ClearShipTargets()
{
    RemoveObjective(RmObjKillShips);
    RemoveObjective(RmObjKillBigShips);
    FRmRecordList Flagged = Ships->Query(0, RmRecordAlive, RmRecordFlagTarget);
    for (std::list<FRmSpawnRecord*>::iterator It = Flagged.Records.begin();
         It != Flagged.Records.end(); ++It)
    {
        FRmSpawnRecord* const Record = Ships->Records.FindById((*It)->ObjectId);
        Record->Flags = 0;
        std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
        for (std::vector<FContentPlayerState*>::iterator Player = Players->begin();
             Player != Players->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, Record->ObjectId, 0);
    }
}

// FUN_06f8bd90.
void FRmVignetteMachine::ClearSolarTargets()
{
    RemoveObjective(RmObjKillBigSolar);
    RemoveObjective(RmObjKillOtherSolars);
    FRmRecordList Flagged = Solars->Query(-1, RmRecordAlive, RmRecordFlagTarget);
    for (std::list<FRmSpawnRecord*>::iterator It = Flagged.Records.begin();
         It != Flagged.Records.end(); ++It)
    {
        FRmSpawnRecord* const Record = Solars->Records.FindById((*It)->ObjectId);
        Record->Flags = 0;
        std::vector<FContentPlayerState*>* Players = GetMissionPlayers();
        for (std::vector<FContentPlayerState*>::iterator Player = Players->begin();
             Player != Players->end(); ++Player)
            pub::Player::MarkObj((*Player)->PlayerId, Record->ObjectId, 0);
    }
}

// FUN_06f8beb0. Retail has no end test after the group search; every caller passes the
// serial of one of Component's groups.
void FRmVignetteMachine::SpawnGroupWithTriggers(unsigned int Serial, FRmShipComponent* Component,
                                                unsigned int Cookie)
{
    std::vector<FRmShipGroup>::iterator Group = Component->Groups.begin();
    while (Group != Component->Groups.end() && Group->Serial != Serial)
        ++Group;
    if (Group->bSpawned != 0)
        return;
    if (Group->bClearPriorTargets != 0)
        ClearShipTargets();

    SpawnShipGroup(&*Group, Group != Component->Groups.begin());
    FRmRecordList Members = Ships->QueryGroup(Group->Serial);
    MarkGroupTargets(&Members, &Component->Objectives);

    // The first-spawn comm sequences. A positive attitude is a friendly group; unordered goes
    // to the hostile side.
    if (Group->Attitude > 0.0f)
    {
        if (!bFriendlySpawnSent)
        {
            VignetteOffer->General.SendConversations(RmCommFirstFriendlySpawn, this, nullptr);
            bFriendlySpawnSent = 1;
        }
    }
    else if (!bBigShipSpawnSent || !bDefensiveShipSpawnSent)
    {
        bool bDefensive = false;
        bool bBig = false;
        for (std::list<FRmSpawnRecord*>::iterator It = Members.Records.begin();
             It != Members.Records.end(); ++It)
        {
            int const Kind = static_cast<FRmShipRecord*>(*It)->Kind;
            if (Kind == RmShipKindDefensive)
                bDefensive = true;
            else if (Kind == RmShipKindBig)
                bBig = true;
        }
        if (!bBigShipSpawnSent && bBig)
        {
            VignetteOffer->General.SendConversations(RmCommFirstBigShipSpawn, this, nullptr);
            bBigShipSpawnSent = 1;
        }
        else if (!bDefensiveShipSpawnSent && bDefensive)
        {
            VignetteOffer->General.SendConversations(RmCommFirstDefensiveShipSpawn, this,
                                                     nullptr);
            bDefensiveShipSpawnSent = 1;
        }
    }

    // The next group not spawned yet gets a trigger for "all ships dead" and, by its
    // SpawnTriggerMode, a delayed cookie (2) or a "SpawnTriggerParam ships left" trigger (3).
    std::vector<FRmShipGroup>::iterator Next = Group + 1;
    while (Next != Component->Groups.end() && Next->bSpawned != 0)
        ++Next;
    if (Next == Component->Groups.end())
        return;

    FRmShipCountTrigger* AllDead =
        new FRmShipCountTrigger(Cookie, get_id(), &Ships->Records, 0);
    AllDead->SetActivator(Next->Serial);
    AddStateMachine(AllDead);
    if (Next->SpawnTriggerMode == RmSpawnWaveDelay)
    {
        FRmCookieMessage Delayed(Next->Serial);
        send_delayed_message_to_me(&Delayed, static_cast<float>(Next->SpawnTriggerParam), SMMS_Self, 0);
    }
    else if (Next->SpawnTriggerMode == RmSpawnShipsLeft)
    {
        FRmShipCountTrigger* FewLeft =
            new FRmShipCountTrigger(Cookie, get_id(), &Ships->Records, Next->SpawnTriggerParam);
        FewLeft->SetActivator(Next->Serial);
        AddStateMachine(FewLeft);
    }
}

// FUN_06f8c200. No null test on the record.
void FRmVignetteMachine::OnFlaggedShipsLost(unsigned int ObjectId)
{
    FRmShipRecord* Record = static_cast<FRmShipRecord*>(Ships->Records.FindById(ObjectId));
    int const Kind = Record->Kind;
    if (Kind == RmShipKindBig)
    {
        FRmRecordList Left = Ships->Query(RmShipKindBig, RmRecordAlive, RmRecordFlagTarget);
        if (Left.Records.size() == 0)
            RemoveObjective(RmObjKillBigShips);
    }
    else if (Kind == RmShipKindDefensive)
    {
        FRmRecordList Left = Ships->Query(RmShipKindDefensive, RmRecordAlive, RmRecordFlagTarget);
        if (Left.Records.size() == 0)
            RemoveObjective(RmObjKillShips);
    }
}

// FUN_06f8c330.
bool FRmVignetteMachine::HasTargetObjectives()
{
    if (FindObjective(RmObjKillBigShips) != ActiveObjectives.end())
        return true;
    if (FindObjective(RmObjKillBigSolar) != ActiveObjectives.end())
        return true;
    if (FindObjective(RmObjKillShips) != ActiveObjectives.end())
        return true;
    if (FindObjective(RmObjKillOtherSolars) != ActiveObjectives.end())
        return true;
    return false;
}

// FUN_06f8c3c0.
void FRmVignetteMachine::SendSystemConversations(unsigned int InPlayerId, unsigned int SystemId)
{
    std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
    std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerMap->find(InPlayerId);
    if (It == PlayerMap->end())
        return;
    FContentPlayerState* const Player = It->second;
    if (Player->ShipObject == nullptr)
        return;
    if (SystemId == GetSystemId())
        VignetteOffer->General.SendConversations(RmCommInSpaceInSystem, this, Player);
    else
        VignetteOffer->General.SendConversations(RmCommInSpaceNotInSystem, this, Player);
}

// FUN_06f90b70. Retail reads Members' first record without an empty test; the spawn always
// gives the group a record.
void FRmVignetteMachine::MarkGroupTargets(FRmRecordList* Members, FRmObjectiveMap* Map)
{
    bool bMarked = false;

    // The first member flagged as a target (Flags bit 1).
    FRmShipRecord* Flagged = nullptr;
    for (std::list<FRmSpawnRecord*>::iterator It = Members->Records.begin();
         It != Members->Records.end(); ++It)
    {
        if (((*It)->Flags & RmRecordFlagTarget) != 0)
        {
            Flagged = static_cast<FRmShipRecord*>(*It);
            break;
        }
    }
    if (Flagged != nullptr)
    {
        // Defensive ships 6, any other kind 7.
        int const Key = (Flagged->Kind != RmShipKindDefensive ? 1 : 0) + RmObjKillShips;
        std::vector<FRmObjective*>::iterator Active = FindObjective(Key);
        unsigned int const SystemId = Offer->System->SystemId;
        if (Active == ActiveObjectives.end())
        {
            // The component's objective marks the members; AddObjective is inlined in retail.
            static_cast<FRmObjectiveType3*>(Map->Objectives[Key])
                ->SetTargetRecords(Members, GetMissionPlayers(), RmRecordFlagTarget, SystemId, Position);
            AddObjective(Key, static_cast<FRmObjective*>(Map->Objectives[Key]));
        }
        else
        {
            // The active objective marks every alive flagged ship of the kind.
            FRmRecordList SameKind = Ships->Query(Flagged->Kind, RmRecordAlive, RmRecordFlagTarget);
            static_cast<FRmObjectiveType3*>(*Active)
                ->SetTargetRecords(&SameKind, GetMissionPlayers(), RmRecordFlagTarget, SystemId, Position);
            RebuildObjective((*Active)->Key);
        }
        RemoveObjective(RmObjDontKillShips);
        bMarked = true;
    }

    // A hostile group (unordered attitude is not) gets the component's objective 8 when
    // nothing was marked above and none of 8, 6, 7 is active.
    FRmSpawnRecord* const First = Members->Records.front();
    if (!(First->Attitude < 0.0f))
        return;
    if (Map->Objectives.find(RmObjDontKillShips) == Map->Objectives.end())
        return;
    if (bMarked)
        return;
    if (FindObjective(RmObjDontKillShips) != ActiveObjectives.end())
        return;
    if (FindObjective(RmObjKillShips) != ActiveObjectives.end())
        return;
    if (FindObjective(RmObjKillBigShips) != ActiveObjectives.end())
        return;
    AddObjective(RmObjDontKillShips,
                 static_cast<FRmObjective*>(Map->Objectives[RmObjDontKillShips]));
}

// =======================================================================================
// FRmVignetteMachine::OnStateEvent (FUN_06f8c460)
// =======================================================================================

// FUN_06f8c460.
bool FRmVignetteMachine::OnStateEvent(SMEventData const* Event, int const& State)
{
    RmVignetteEntryCookies();
    if (State < 0)
        return HandleAnyState(Event);

    // States 0..2 are the base's slots 13..15, their result dropped.
    if (State == RmStateIdle)
    {
        OnState0(Event, State);
        return false;
    }
    if (State == RmStateWaitStart)
    {
        OnState1(Event, State);
        return false;
    }
    if (State == RmStateStarted)
    {
        OnState2(Event, State);
        return false;
    }
    if (State == RmStateStart)
        return HandleStart(Event);
    if (State == RmStatePreBattle)
        return HandlePreBattle(Event);
    if (State == RmStateTargetBattle)
        return HandleTargetBattle(Event);
    if (State == RmStateApproachMain)
        return HandleApproachMain(Event);
    if (State == RmStateMainBattle)
        return HandleMainBattle(Event);
    if (State == RmStateCriticalLoot)
        return HandleCriticalLoot(Event);
    if (State == RmStateSucceeded)
        return HandleSucceeded(Event);
    if (State == RmStateFailed)
        return HandleFailed(Event);
    if (State == RmStateDone)
        return HandleDone(Event);
    return false;
}

// The messages of every state.
bool FRmVignetteMachine::HandleAnyState(SMEventData const* Event)
{
    FRmVignetteEntryCookies const& Cookies = RmVignetteEntryCookies();

    // Controller message 0x20.
    if (RmControllerOf(Event, EMessage::Heartbeat) != nullptr)
    {
        RefreshObjectives();
        return true;
    }

    // A comm sequence conversation (DAT_06fd3b74) goes to the GCS manager (DAT_06fcf1b0)
    // when valid.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgConversation))
    {
        FRmConversationBase* const Conversation =
            static_cast<FRmConversationMessage const*>(Message)->Conversation;
        if (Conversation->IsValid())
            GGcsConversationManager->Add(Conversation);
        return true;
    }

    // Controller messages 0x36 {system, player} and 0x3b {system, .., player}.
    if (SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::SpaceEnter))
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        SendSystemConversations(Parms[1], Parms[0]);
        return true;
    }
    if (SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::SystemEnter))
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        SendSystemConversations(Parms[2], Parms[0]);
        return true;
    }

    // Controller message 0xa {player} of a mapped player.
    if (SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::GroupMemberJoined))
    {
        unsigned int const* PlayerId = static_cast<unsigned int const*>(Controller->DataPtr);
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        if (PlayerMap->find(*PlayerId) == PlayerMap->end())
            return true;
        OnPlayerJoined(*PlayerId);
        return true;
    }

    // A player left (DAT_06fd3b70).
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgPlayerLeft))
    {
        unsigned int const PlayerId = static_cast<FRmPlayerLeftMessage const*>(Message)->PlayerId;
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerMap->find(PlayerId);
        if (It == PlayerMap->end())
            return true;
        std::vector<FContentPlayerState*> Leaving;
        Leaving.insert(Leaving.begin(), It->second);
        UnmarkObjects(&Leaving);
        return true;
    }

    // Controller message 5 {player} of a mapped player.
    if (SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::PlayerLoad))
    {
        unsigned int const* PlayerId = static_cast<unsigned int const*>(Controller->DataPtr);
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerMap->find(*PlayerId);
        if (It == PlayerMap->end())
            return true;
        SetPlayerAttitudes(It->second);
        return true;
    }

    // Controller message 0x27 {ship, base} (docked): in single player, once the area was
    // reached, a mapped player's dock fails the mission (13).
    if (SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::Msg0x27))
    {
        if (!pub::SinglePlayer())
            return true;
        if (!bAreaReached)
            return true;
        FContentPlayerState* Player =
            FindContentPlayerStateByShip(static_cast<unsigned int const*>(Controller->DataPtr));
        if (Player == nullptr)
            return true;
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        if (PlayerMap->find(Player->PlayerId) == PlayerMap->end())
            return true;
        FailReason = 13;
        set_state(RmStateFailed);
        return true;
    }

    // The critical loot was spawned (DAT_06fd3b98): the loot's objective (0xa or 0xb by its
    // mode) marks it -- the active one rebuilt, else the offer's added. Record not null-tested.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgCriticalLootSpawned))
    {
        unsigned int const LootId = static_cast<FRmLootMessage const*>(Message)->ObjectId;
        FRmLootRecord* Record = static_cast<FRmLootRecord*>(Loot->Records.FindById(LootId));
        if (Record->Kind != RmLootCritical)
            return true;
        ClearShipTargets();
        ClearSolarTargets();
        set_state(RmStateCriticalLoot);
        int Key = 0;
        if (Record->CriticalLootMode == RmCriticalLootDestroy)
            Key = RmObjDestroyLoot;
        else if (Record->CriticalLootMode == RmCriticalLootTractor ||
                 Record->CriticalLootMode == RmCriticalLootReturn)
            Key = RmObjTractorLoot;
        else
            return true;

        Vector Where;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(Record->ObjectId, Where, Orientation);
        unsigned int SystemId = 0;
        pub::SpaceObj::GetSystem(Record->ObjectId, SystemId);
        std::vector<FRmObjective*>::iterator Active = FindObjective(Key);
        if (Active != ActiveObjectives.end())
        {
            FRmRecordList Critical = Loot->Query(Record->CriticalLootMode, RmLootCritical, 0, RmRecordAlive, 0);
            static_cast<FRmObjectiveType3*>(*Active)
                ->SetTargetRecords(&Critical, Mission->GetPlayerStates(), RmRecordFlagTarget, SystemId, Where);
            RebuildObjective((*Active)->Key);
        }
        else
        {
            FRmRecordList Critical;
            Critical.Records.push_back(Record);
            static_cast<FRmObjectiveType3*>(RmComponentObjective(&VignetteOffer->General, Key))
                ->SetTargetRecords(&Critical, Mission->GetPlayerStates(), RmRecordFlagTarget, SystemId, Where);
            AddObjective(Key, static_cast<FRmObjective*>(
                                  RmComponentObjective(&VignetteOffer->General, Key)));
        }
        return true;
    }

    // Controller message 0x39 {.., player +0xc, position +0x14}: a mapped player's ship
    // destroyed inside 1e8 squared of the area (DAT_06fbc2f4) counts the players down; at 0
    // the mission fails (reason 2 single player, 3 multiplayer).
    if (SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::ObjectDestroyed))
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerMap->find(Parms[3]);
        if (It == PlayerMap->end())
            return true;
        Vector const* Killed = reinterpret_cast<Vector const*>(&Parms[5]);
        Vector const Delta = SubtractVectors(*Killed, Position);
        // x87 sum in retail's order.
        double const DistanceSq = (static_cast<double>(Delta.x) * Delta.x +
                                   static_cast<double>(Delta.z) * Delta.z) +
                                  static_cast<double>(Delta.y) * Delta.y;
        if (!(DistanceSq < 100000000.0))
            return true;
        if (Mission->DecrementPlayerCount() > 0)
            return true;
        VignetteOffer->General.SendConversations(RmCommGlobalFailOther, this, It->second);
        FailReason = pub::SinglePlayer() ? 2 : 3;
        set_state(RmStateFailed);
        return true;
    }

    // The players left the combat area: the countdown objective 9, a trigger back into the
    // area, the countdown timer and the 5 s tick DAT_06fd3b7c.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, Cookies.LeaveArea))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        LeftAreaTime = static_cast<float>(GetContentGameTime());
        VignetteOffer->General.SendConversations(
            RmCommLeftCombatArea, this, FindContentPlayerStateByShip(&Fired->Activator));
        FRmObjectiveType4* Countdown = new FRmObjectiveType4();
        Countdown->ObjectiveType = 0xda;
        Countdown->StrId =
            RmComponentObjective(&VignetteOffer->General, RmObjReturnToBattle)->StrId;
        unsigned int const SystemId = VignetteOffer->System->SystemId;
        Countdown->SetCountdown(static_cast<int>(GRmMultiplayerDelay), SystemId, 0x44d,
                                Position);
        InsertObjective(RmObjReturnToBattle, Countdown, ActiveObjectives.begin());
        AddMachine(new FRmProximityTrigger(Cookies.ReenterArea, get_id(),
                                           VignetteOffer->System->SystemId, &Position,
                                           AreaRadius - 250.0f, GetMissionPlayers()));
        AddMachine(new FRmTimerTrigger(Cookies.Countdown, get_id(), GRmMultiplayerDelay));
        StateMachineMessage Tick(GRmMsgCountdownTick);
        send_delayed_message_to_me(&Tick, 5.0f, SMMS_Broadcast, 0);
        return true;
    }

    // Back in the area: the countdown objective removed and deleted, a new leave trigger.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, Cookies.ReenterArea))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        VignetteOffer->General.SendConversations(
            RmCommReenteredCombatArea, this, FindContentPlayerStateByShip(&Fired->Activator));
        SendPlaceholderObjectives();
        std::vector<FRmObjective*>::iterator Active = FindObjective(RmObjReturnToBattle);
        if (Active != ActiveObjectives.end())
        {
            FRmObjective* const Countdown = *Active;
            RemoveObjective(RmObjReturnToBattle);
            if (Countdown != nullptr)
                delete Countdown;
        }
        AddStateMachine(new FRmProximityLeaveTrigger(Cookies.LeaveArea, get_id(),
                                                     VignetteOffer->System->SystemId, &Position,
                                                     AreaRadius + 250.0f, GetMissionPlayers()));
        LeftAreaTime = -1.0f;
        return true;
    }

    // The countdown ran out: fails when (GRmMultiplayerDelay - 1.0) <= the time since the
    // players left; unordered does not fail.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, Cookies.Countdown))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        if (LeftAreaTime == -1.0f)
            return true;
        double const Now = GetContentGameTime();
        double const Left = LeftAreaTime;
        double const Elapsed = GetContentTimeDistance(&Left, &Now);
        double const Limit = static_cast<double>(GRmMultiplayerDelay) - 1.0;
        if (Limit > Elapsed || Limit != Limit || Elapsed != Elapsed)
            return true;
        VignetteOffer->General.SendConversations(
            RmCommGlobalFailCombatRange, this, FindContentPlayerStateByShip(&Fired->Activator));
        FailReason = pub::SinglePlayer() ? 0xb : 0xc;
        set_state(RmStateFailed);
        LeftAreaTime = -1.0f;
        return true;
    }

    // The 5 s tick (DAT_06fd3b7c): the countdown objective is taken out and, while time is
    // left (unordered stops), put back at the front with the remaining seconds. Retail reads
    // the objective without an end test: the tick only runs while it is active.
    if (RmMessageOf(this, Event, GRmMsgCountdownTick) != nullptr)
    {
        if (LeftAreaTime == -1.0f)
            return true;
        double const Now = GetContentGameTime();
        double const Left = LeftAreaTime;
        int const Elapsed = static_cast<int>(GetContentTimeDistance(&Left, &Now));
        FRmObjective* const Countdown = *FindObjective(RmObjReturnToBattle);
        RemoveObjective(RmObjReturnToBattle);
        double const Remaining = static_cast<double>(GRmMultiplayerDelay) -
                                 static_cast<double>(Elapsed);
        if (!(Remaining > 0.0))
            return true;
        unsigned int const SystemId = VignetteOffer->System->SystemId;
        static_cast<FRmObjectiveType4*>(Countdown)
            ->SetCountdown(static_cast<int>(Remaining), SystemId, 0x44d, Position);
        InsertObjective(RmObjReturnToBattle, Countdown, ActiveObjectives.begin());
        StateMachineMessage Tick(GRmMsgCountdownTick);
        send_delayed_message_to_me(&Tick, 5.0f, SMMS_Broadcast, 0);
        return true;
    }

    // A ship destroyed (DAT_06fd3ba8): its loot.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgShipDestroyed))
    {
        unsigned int const ObjectId = static_cast<FRmShipMessage const*>(Message)->ObjectId;
        SpawnRecordLoot(Ships->Records.FindById(ObjectId));
        return true;
    }
    return false;
}

// State 3: on enter the mission hears the machine started; the target ships' marker
// (state 8) or the main battle's (state 10).
bool FRmVignetteMachine::HandleStart(SMEventData const* Event)
{
    if (!RmIsEnterEvent(Event))
        return false;
    StateMachineMessage Started(GRmMsgMachineStarted);
    send_immediate_message(&Started, static_cast<MessageHandler*>(Mission)->get_id());

    FRmVignetteOffer* const Vignette = VignetteOffer.Get();
    if (Vignette->TargetShips.bActive)
    {
        unsigned int const SystemId = Vignette->System->SystemId;
        FRmParamText* const Guide = RmComponentObjective(&Vignette->General, RmObjGuideToPreBattle);
        static_cast<FRmObjectiveType2*>(Guide)->SetMarker(SystemId, 0x44d,
                                                          Vignette->TargetShips.Position);
        AddObjective(RmObjGuideToPreBattle, static_cast<FRmObjective*>(Guide));
        set_state(RmStatePreBattle);
    }
    else
    {
        Vector const* Center = RmMainBattleCenter(Vignette);
        unsigned int const SystemId = Vignette->System->SystemId;
        FRmParamText* const Guide =
            RmComponentObjective(&Vignette->General, RmObjGuideToMainBattle);
        static_cast<FRmObjectiveType2*>(Guide)->SetMarker(SystemId, 0x44d, *Center);
        AddObjective(RmObjGuideToMainBattle, static_cast<FRmObjective*>(Guide));
        set_state(RmStateApproachMain);
    }
    AreaRadius = 3500.0f;
    std::vector<FContentPlayerState*>* Players = Mission->GetPlayerStates();
    for (std::vector<FContentPlayerState*>::iterator It = Players->begin(); It != Players->end();
         ++It)
    {
        if ((*It)->ShipObject == nullptr)
            continue;
        SendSystemConversations((*It)->PlayerId, (*It)->GetSystemRecord()->SystemId);
    }
    return true;
}

// State 8: the target ships' pre-battle.
bool FRmVignetteMachine::HandlePreBattle(SMEventData const* Event)
{
    static unsigned int const CookieTargetFar = RmNextVignetteCookie();     // DAT_06fd3b58
    static unsigned int const CookieTargetNear = RmNextVignetteCookie();    // DAT_06fd3b54

    // 2500 is DAT_06fbc2c4, 10000 DAT_06fbc2ec.
    if (RmIsEnterEvent(Event))
    {
        Position = VignetteOffer->TargetShips.Position;
        AddStateMachine(new FRmProximityTrigger(CookieTargetNear, get_id(),
                                                VignetteOffer->System->SystemId,
                                                &VignetteOffer->TargetShips.Position, 2500.0f,
                                                GetMissionPlayers()));
        AddStateMachine(new FRmProximityTrigger(CookieTargetFar, get_id(),
                                                VignetteOffer->System->SystemId,
                                                &VignetteOffer->TargetShips.Position, 10000.0f,
                                                GetMissionPlayers()));
        return true;
    }

    // A player within 10000: the population is kept out of the target's 10000 sphere.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieTargetFar))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        Vector const& Target = VignetteOffer->TargetShips.Position;
        SpacePopAddExclusionSphere(Target.x, Target.y, Target.z, 10000.0f);
        bAreaReached = 1;
        return true;
    }

    // A player within 2500.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieTargetNear))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        VignetteOffer->General.SendConversations(
            RmCommPreBattleSpawnRange, this, FindContentPlayerStateByShip(&Fired->Activator));
        RemoveObjective(RmObjGuideToPreBattle);
        set_state(RmStateTargetBattle);
        return true;
    }
    return false;
}

// State 9: the fight with the target ships.
bool FRmVignetteMachine::HandleTargetBattle(SMEventData const* Event)
{
    static unsigned int const CookieNextGroup = RmNextVignetteCookie();     // DAT_06fd3b50
    static unsigned int const CookieTargetDone = RmNextVignetteCookie();    // DAT_06fd3b4c
    FRmVignetteEntryCookies const& Cookies = RmVignetteEntryCookies();
    FRmVignetteOffer* const Vignette = VignetteOffer.Get();

    // The first target group spawns, the delayed ones (mode 1) get their cookie after
    // SpawnTriggerParam seconds; the end condition by EndTriggerMode.
    if (RmIsEnterEvent(Event))
    {
        AddStateMachine(new FRmProximityLeaveTrigger(Cookies.LeaveArea, get_id(),
                                                     Vignette->System->SystemId,
                                                     &Vignette->TargetShips.Position,
                                                     AreaRadius + 250.0f, GetMissionPlayers()));
        std::vector<FRmShipGroup>& Groups = Vignette->TargetShips.Groups;
        if (!Groups.empty())
        {
            SpawnGroupWithTriggers(Groups.front().Serial, &Vignette->TargetShips, CookieNextGroup);
            for (std::vector<FRmShipGroup>::iterator It = VignetteOffer->TargetShips.Groups.begin() + 1;
                 It != VignetteOffer->TargetShips.Groups.end(); ++It)
            {
                if (It->SpawnTriggerMode != RmSpawnPhaseDelay)
                    continue;
                FRmCookieMessage Delayed(It->Serial);
                send_delayed_message_to_me(&Delayed, static_cast<float>(It->SpawnTriggerParam),
                                           SMMS_Self, 0);
            }
        }
        int const Mode = VignetteOffer->TargetShips.EndTriggerMode;
        if (Mode == RmEndTimer)
        {
            AddStateMachine(new FRmTimerTrigger(
                CookieTargetDone, get_id(),
                static_cast<float>(VignetteOffer->TargetShips.EndTriggerParam)));
        }
        else if (Mode == RmEndShipCount)
        {
            AddStateMachine(new FRmShipCountTrigger(CookieTargetDone, get_id(), &Ships->Records,
                                                    VignetteOffer->TargetShips.EndTriggerParam));
        }
        else if (Mode == RmEndShipHealth)
        {
            AddStateMachine(new FRmShipHealthTrigger(CookieTargetDone, get_id(),
                                                     &Ships->Records,
                                                     VignetteOffer->TargetShips.EndTriggerParam, 2));
        }
        return true;
    }

    // The target fight is over once every target group spawned; the target component's
    // objective 3 marks the main battle (as key 2, at the front).
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieTargetDone))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        if (RmHasUnspawnedGroup(&VignetteOffer->TargetShips.Groups))
            return true;
        // Built and kept to the end unread, as retail.
        FRmRecordList Flagged = Ships->Query(0, RmRecordAlive, RmRecordFlagTarget);
        bool bNoneAlive = false;
        {
            FRmRecordList Alive = Ships->Query(0, RmRecordAlive, 0);
            bNoneAlive = Alive.Records.size() == 0;
        }
        if (bNoneAlive)
        {
            if (!RmHasUnspawnedGroup(&VignetteOffer->Ships.Groups))
            {
                set_state(RmStateSucceeded);
                return true;
            }
            VignetteOffer->General.SendConversations(RmCommKilledAllGoingToMain, this, nullptr);
        }
        else
        {
            VignetteOffer->General.SendConversations(RmCommNotKilledAllGoingToMain, this,
                                                     nullptr);
        }
        Vector const* Center = RmMainBattleCenter(VignetteOffer.Get());
        unsigned int const SystemId = VignetteOffer->System->SystemId;
        FRmParamText* const Guide =
            RmComponentObjective(&VignetteOffer->TargetShips, RmObjGuideToMainFromPre);
        static_cast<FRmObjectiveType2*>(Guide)->SetMarker(SystemId, 0x44d, *Center);
        InsertObjective(RmObjGuideToMainBattle, static_cast<FRmObjective*>(Guide),
                        ActiveObjectives.begin());
        set_state(RmStateApproachMain);
        return true;
    }

    // The next target group, unless every group spawned and no target objective is left.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieNextGroup))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        if (!RmHasUnspawnedGroup(&VignetteOffer->TargetShips.Groups))
        {
            if (FindObjective(RmObjKillBigShips) == ActiveObjectives.end() &&
                FindObjective(RmObjKillBigSolar) == ActiveObjectives.end() &&
                FindObjective(RmObjKillShips) == ActiveObjectives.end() &&
                FindObjective(RmObjKillOtherSolars) == ActiveObjectives.end())
                return true;
        }
        SpawnGroupWithTriggers(Fired->Activator, &VignetteOffer->TargetShips, CookieNextGroup);
        return true;
    }

    // A delayed target group (DAT_06fd3b78).
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgSpawnGroup))
    {
        SpawnGroupWithTriggers(static_cast<FRmCookieMessage const*>(Message)->Cookie,
                               &VignetteOffer->TargetShips, CookieNextGroup);
        return true;
    }

    // A whole group destroyed (DAT_06fd3ba4) is taken.
    if (RmMessageOf(this, Event, GRmMsgGroupDestroyed) != nullptr)
        return true;

    // The flagged ships of a group died (DAT_06fd3ba0): once every target group spawned and
    // at most the countdown is active, the main battle starts (or state 5 with no main ships).
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgGroupTargetsDestroyed))
    {
        OnFlaggedShipsLost(static_cast<FRmShipMessage const*>(Message)->ObjectId);
        if (RmHasUnspawnedGroup(&VignetteOffer->TargetShips.Groups))
            return true;
        if (ActiveObjectives.size() != 0)
        {
            if (ActiveObjectives.size() != 1)
                return true;
            if (FindObjective(RmObjReturnToBattle) == ActiveObjectives.end())
                return true;
        }
        if (ActiveObjectives.size() == 1)
        {
            RemoveObjective(RmObjReturnToBattle);
            LeftAreaTime = -1.0f;
        }
        if (!RmHasUnspawnedGroup(&VignetteOffer->Ships.Groups))
        {
            set_state(RmStateSucceeded);
            return true;
        }
        VignetteOffer->General.SendConversations(RmCommKilledAllGoingToMain, this, nullptr);
        AddObjective(RmObjGuideToMainBattle,
                     static_cast<FRmObjective*>(RmComponentObjective(&VignetteOffer->TargetShips,
                                                                     RmObjGuideToMainFromPre)));
        set_state(RmStateApproachMain);
        return true;
    }

    // The exit to state 10: every alive ship leaves its formation for the main battle
    // (2000 is DAT_06fbc2cc).
    if (Event->get_event_type() < SMET_Controller && Event->get_event_type() == SMET_Exit)
    {
        if (PendingState != RmStateApproachMain)
            return true;
        AreaRadius = GRmVignetteOuterRadius;
        TRefHandle<FObjListDirective> StayInRange;
        CreateObjListDirective(OBJLIST_STAY_IN_RANGE, &StayInRange.Pointer);
        static_cast<FObjListStayInRange*>(StayInRange.Pointer)
            ->SetSphere(RmMainBattleCenter(VignetteOffer.Get()), 2000.0f);
        TRefHandle<FObjListDirective> GotoCenter;
        CreateObjListDirective(OBJLIST_GOTO_VEC, &GotoCenter.Pointer);
        static_cast<FObjListGotoVec*>(GotoCenter.Pointer)
            ->SetDestination(RmMainBattleCenter(VignetteOffer.Get()), 750.0f, -1.0f, 0, false, true);
        FRmRecordList Alive = Ships->Query(0, 2, 0);
        for (std::list<FRmSpawnRecord*>::iterator It = Alive.Records.begin();
             It != Alive.Records.end(); ++It)
        {
            FContentNpcShip* const Ship = (*It)->NpcShip;
            RemoveFromFormation(Ship);
            Ship->CancelDirectives();
            Ship->AddDirective(StayInRange.Pointer);
            Ship->AddDirective(GotoCenter.Pointer);
            Ship->RestartDirectives();
        }
        return true;
    }
    return false;
}

// State 10: on the way to the main battle.
bool FRmVignetteMachine::HandleApproachMain(SMEventData const* Event)
{
    static unsigned int const CookieMainShips = RmNextVignetteCookie();     // DAT_06fd3b48
    static unsigned int const CookieMainFar = RmNextVignetteCookie();       // DAT_06fd3b40
    static unsigned int const CookieMainNear = RmNextVignetteCookie();      // DAT_06fd3b3c
    FRmVignetteEntryCookies const& Cookies = RmVignetteEntryCookies();

    // The objectives still at the old centre move to the main battle's (unordered skips);
    // 7500 is DAT_06fbc2b0.
    if (RmIsEnterEvent(Event))
    {
        Vector const* CenterPtr = RmMainBattleCenter(VignetteOffer.Get());
        Vector const Center = *CenterPtr;
        Vector const Old = Position;
        for (std::vector<FRmObjective*>::iterator It = ActiveObjectives.begin();
             It != ActiveObjectives.end(); ++It)
        {
            Vector At;
            if (!(*It)->GetPosition(&At))
                continue;
            double const Dx = static_cast<double>(At.x) - static_cast<double>(Old.x);
            if (!(Dx * Dx <= 1.0))
                continue;
            double const Dy = static_cast<double>(At.y) - static_cast<double>(Old.y);
            if (!(Dy * Dy <= 1.0))
                continue;
            double const Dz = static_cast<double>(At.z) - static_cast<double>(Old.z);
            if (!(Dz * Dz <= 1.0))
                continue;
            (*It)->SetPosition(Center);
        }
        Position = Center;
        if (VignetteOffer->TargetShips.bActive)
        {
            AddStateMachine(new FRmProximityLeaveTrigger(
                Cookies.LeaveArea, get_id(), VignetteOffer->System->SystemId,
                RmMainBattleCenter(VignetteOffer.Get()), AreaRadius + 250.0f, GetMissionPlayers()));
        }
        AddStateMachine(new FRmProximityTrigger(CookieMainShips, get_id(),
                                                VignetteOffer->System->SystemId,
                                                RmMainBattleCenter(VignetteOffer.Get()), 7500.0f,
                                                GetMissionPlayers()));
        AddStateMachine(new FRmProximityTrigger(CookieMainFar, get_id(),
                                                VignetteOffer->System->SystemId,
                                                RmMainBattleCenter(VignetteOffer.Get()), 10000.0f,
                                                GetMissionPlayers()));
        return true;
    }

    // The flagged ships of a group died (DAT_06fd3ba0): with every main group spawned and no
    // target, loot or dock objective left, state 5.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgGroupTargetsDestroyed))
    {
        OnFlaggedShipsLost(static_cast<FRmShipMessage const*>(Message)->ObjectId);
        if (RmHasUnspawnedGroup(&VignetteOffer->Ships.Groups))
            return true;
        if (FindObjective(RmObjKillBigShips) != ActiveObjectives.end() ||
            FindObjective(RmObjKillBigSolar) != ActiveObjectives.end() ||
            FindObjective(RmObjKillShips) != ActiveObjectives.end() ||
            FindObjective(RmObjKillOtherSolars) != ActiveObjectives.end())
            return true;
        if (FindObjective(RmObjDestroyLoot) != ActiveObjectives.end() ||
            FindObjective(RmObjTractorLoot) != ActiveObjectives.end() ||
            FindObjective(RmObjReturnLoot) != ActiveObjectives.end() ||
            FindObjective(RmObjJettisonLoot) != ActiveObjectives.end())
            return true;
        set_state(RmStateSucceeded);
        return true;
    }

    // A solar destroyed (DAT_06fd3b9c): the last flagged solar of its type takes objective 4
    // (type 0) or 5 (type 1); with no flagged solar left the lone objective 8 goes; once
    // nothing else is left, state 5. Record not null-tested.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgSolarDestroyed))
    {
        unsigned int const SolarId = static_cast<FRmSolarMessage const*>(Message)->ObjectId;
        FRmSolarRecord* Record = static_cast<FRmSolarRecord*>(Solars->Records.FindById(SolarId));
        size_t TypeLeft = 0;
        {
            FRmRecordList Left = Solars->Query(Record->SolarType, RmRecordAlive, RmRecordFlagTarget);
            TypeLeft = Left.Records.size();
        }
        if (TypeLeft != 0)
            return true;
        if (Record->SolarType == kRmSolarBig)
            RemoveObjective(RmObjKillBigSolar);
        else if (Record->SolarType == kRmSolarDefensive)
            RemoveObjective(RmObjKillOtherSolars);
        size_t AnyLeft = 0;
        {
            FRmRecordList Left = Solars->Query(-1, 2, 2);
            AnyLeft = Left.Records.size();
        }
        if (AnyLeft != 0)
            return true;
        if (ActiveObjectives.size() == 1 &&
            FindObjective(RmObjDontKillShips) != ActiveObjectives.end())
            RemoveObjective(RmObjDontKillShips);
        if (RmHasUnspawnedGroup(&VignetteOffer->Ships.Groups))
            return true;
        if (FindObjective(RmObjDestroyLoot) != ActiveObjectives.end() ||
            FindObjective(RmObjTractorLoot) != ActiveObjectives.end() ||
            FindObjective(RmObjReturnLoot) != ActiveObjectives.end() ||
            FindObjective(RmObjJettisonLoot) != ActiveObjectives.end())
            return true;
        // Retail searches key 7 through the FUN_06f90a40 getter here.
        std::vector<FRmObjective*>::iterator Big = ActiveObjectives.begin();
        while (Big != ActiveObjectives.end() && (*Big)->Key != RmObjKillBigShips)
            ++Big;
        if (Big != ActiveObjectives.end())
            return true;
        if (FindObjective(RmObjKillBigSolar) != ActiveObjectives.end() ||
            FindObjective(RmObjKillShips) != ActiveObjectives.end() ||
            FindObjective(RmObjKillOtherSolars) != ActiveObjectives.end())
            return true;
        if (FindObjective(RmObjReturnToBattle) != ActiveObjectives.end())
        {
            RemoveObjective(RmObjReturnToBattle);
            LeftAreaTime = -1.0f;
        }
        set_state(RmStateSucceeded);
        return true;
    }

    // A player within 7500: the solars spawn, a trigger at 2500 for the ships.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieMainShips))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        VignetteOffer->General.SendConversations(RmCommMainBattleSolarSpawnRange, this,
                                                 FindContentPlayerStateByShip(&Fired->Activator));
        SpawnSolars(&VignetteOffer->Solars.Solars);
        AddStateMachine(new FRmProximityTrigger(CookieMainNear, get_id(),
                                                VignetteOffer->System->SystemId,
                                                RmMainBattleCenter(VignetteOffer.Get()), 2500.0f,
                                                GetMissionPlayers()));
        return true;
    }

    // A player within 10000.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieMainFar))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        Vector const* Center = RmMainBattleCenter(VignetteOffer.Get());
        SpacePopAddExclusionSphere(Center->x, Center->y, Center->z, 10000.0f);
        bAreaReached = 1;
        return true;
    }

    // A player within 2500.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieMainNear))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        VignetteOffer->General.SendConversations(RmCommMainBattleShipSpawnRange, this,
                                                 FindContentPlayerStateByShip(&Fired->Activator));
        set_state(RmStateMainBattle);
        return true;
    }

    // The exit drops the main battle marker (key 2).
    if (Event->is_sm_event() && Event->get_event_type() == SMET_Exit)
    {
        RemoveObjective(RmObjGuideToMainBattle);
        return true;
    }
    return false;
}

// State 11: the main battle.
bool FRmVignetteMachine::HandleMainBattle(SMEventData const* Event)
{
    static unsigned int const CookieNextGroup = RmNextVignetteCookie();     // DAT_06fd3b38
    FRmVignetteEntryCookies const& Cookies = RmVignetteEntryCookies();

    // The flagged solars are marked; the first main group spawns unless delayed, every
    // delayed group gets its cookie.
    if (Event->is_sm_event() && Event->get_event_type() == SMET_Enter)
    {
        AreaRadius = 3500.0f;
        FRmRecordList Others = Solars->Query(1, 2, 2);
        if (Others.Records.size() != 0)
        {
            FRmParamText* const Kill =
                RmComponentObjective(&VignetteOffer->Solars, RmObjKillOtherSolars);
            static_cast<FRmObjectiveType3*>(Kill)->SetTargetRecords(
                &Others, GetMissionPlayers(), 2, VignetteOffer->System->SystemId, Position);
            AddObjective(RmObjKillOtherSolars, static_cast<FRmObjective*>(Kill));
        }
        FRmRecordList Big = Solars->Query(0, 2, 2);
        if (Big.Records.size() != 0)
        {
            FRmParamText* const Kill =
                RmComponentObjective(&VignetteOffer->Solars, RmObjKillBigSolar);
            static_cast<FRmObjectiveType3*>(Kill)->SetTargetRecords(
                &Big, GetMissionPlayers(), 2, VignetteOffer->System->SystemId, Position);
            AddObjective(RmObjKillBigSolar, static_cast<FRmObjective*>(Kill));
        }
        if (!VignetteOffer->Ships.Groups.empty())
        {
            std::vector<FRmShipGroup>::iterator First = VignetteOffer->Ships.Groups.begin();
            if (First->SpawnTriggerMode != RmSpawnPhaseDelay)
                SpawnGroupWithTriggers(First->Serial, &VignetteOffer->Ships, CookieNextGroup);
            for (std::vector<FRmShipGroup>::iterator It = First;
                 It != VignetteOffer->Ships.Groups.end(); ++It)
            {
                if (It->SpawnTriggerMode != RmSpawnPhaseDelay)
                    continue;
                FRmCookieMessage Delayed(It->Serial);
                send_delayed_message_to_me(&Delayed, static_cast<float>(It->SpawnTriggerParam),
                                           SMMS_Self, 0);
            }
        }
        AddStateMachine(new FRmProximityLeaveTrigger(
            Cookies.LeaveArea, get_id(), VignetteOffer->System->SystemId,
            RmMainBattleCenter(VignetteOffer.Get()), AreaRadius + 250.0f, GetMissionPlayers()));
        return true;
    }

    // The next main group.
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, CookieNextGroup))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        if (!RmHasUnspawnedGroup(&VignetteOffer->Ships.Groups) && !HasTargetObjectives())
            return true;
        SpawnGroupWithTriggers(Fired->Activator, &VignetteOffer->Ships, CookieNextGroup);
        return true;
    }

    // A delayed main group (DAT_06fd3b78).
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgSpawnGroup))
    {
        SpawnGroupWithTriggers(static_cast<FRmCookieMessage const*>(Message)->Cookie,
                               &VignetteOffer->Ships, CookieNextGroup);
        return true;
    }

    // The common tail of the two handlers below: with no objective or only the countdown,
    // state 5 (the countdown removed).
    struct FTail
    {
        static bool Run(FRmVignetteMachine* Machine)
        {
            if (!Machine->ActiveObjectives.empty())
            {
                if (Machine->ActiveObjectives.size() != 1)
                    return true;
                if (Machine->FindObjective(RmObjReturnToBattle) == Machine->ActiveObjectives.end())
                    return true;
            }
            if (Machine->ActiveObjectives.size() == 1)
            {
                Machine->RemoveObjective(RmObjReturnToBattle);
                Machine->LeftAreaTime = -1.0f;
            }
            Machine->set_state(RmStateSucceeded);
            return true;
        }
    };

    // The flagged ships of a group died (DAT_06fd3ba0).
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgGroupTargetsDestroyed))
    {
        OnFlaggedShipsLost(static_cast<FRmShipMessage const*>(Message)->ObjectId);
        if (RmHasUnspawnedGroup(&VignetteOffer->Ships.Groups))
            return true;
        return FTail::Run(this);
    }

    // A solar destroyed (DAT_06fd3b9c).
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgSolarDestroyed))
    {
        unsigned int const SolarId = static_cast<FRmSolarMessage const*>(Message)->ObjectId;
        FRmSolarRecord* Record = static_cast<FRmSolarRecord*>(Solars->Records.FindById(SolarId));
        bool bTypeGone = false;
        {
            FRmRecordList Left = Solars->Query(Record->SolarType, RmRecordAlive, RmRecordFlagTarget);
            bTypeGone = Left.Records.empty();
        }
        if (!bTypeGone)
            return true;
        if (Record->SolarType == kRmSolarBig)
            RemoveObjective(RmObjKillBigSolar);
        else if (Record->SolarType == kRmSolarDefensive)
            RemoveObjective(RmObjKillOtherSolars);
        if (ActiveObjectives.size() == 1 &&
            FindObjective(RmObjDontKillShips) != ActiveObjectives.end())
            RemoveObjective(RmObjDontKillShips);
        if (RmHasUnspawnedGroup(&VignetteOffer->Ships.Groups))
            return true;
        return FTail::Run(this);
    }
    return false;
}

// State 12: the critical loot.
bool FRmVignetteMachine::HandleCriticalLoot(SMEventData const* Event)
{
    // Controller message 0x27 is taken (the "any state" dock failure does not apply here).
    if (Event->is_sm_event() && Event->get_event_type() == SMET_Enter)
        return true;
    if (RmControllerOf(Event, EMessage::Msg0x27) != nullptr)
        return true;

    // A mapped player with a ship left (DAT_06fd3b70): the critical loot he holds is dropped
    // next to his ship. False: the "any state" handler runs as well. The first
    // FindContentPlayerState call's result is unused, as retail.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgPlayerLeft))
    {
        unsigned int const PlayerId = static_cast<FRmPlayerLeftMessage const*>(Message)->PlayerId;
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        std::map<unsigned int, FContentPlayerState*>::iterator It = PlayerMap->find(PlayerId);
        if (It == PlayerMap->end())
            return false;
        if (!It->second->HasShip())
            return false;
        FRmRecordList Held = Loot->Query(0, 2, 0, 4, PlayerId);
        if (!Held.Records.empty())
        {
            FRmLootRecord* const Record = static_cast<FRmLootRecord*>(Held.Records.front());
            FindContentPlayerState(Record->HolderId);
            unsigned int const ShipId = FindContentPlayerState(Record->HolderId)->GetShipId();
            FRmLootMessage Drop(GRmMsgDropLoot, Record->ObjectId, ShipId);
            send_immediate_message(&Drop, static_cast<MessageHandler*>(Loot)->get_id());
        }
        return false;
    }

    // The DAT_06fd3b6c drop of the loot record's holder (inlined several times).
    struct FDrop
    {
        static void Send(FRmVignetteMachine* Machine, FRmLootRecord* Record)
        {
            unsigned int const ShipId = FindContentPlayerState(Record->HolderId)->GetShipId();
            FRmLootMessage Drop(GRmMsgDropLoot, Record->ObjectId, ShipId);
            Machine->send_immediate_message(&Drop,
                                            static_cast<MessageHandler*>(Machine->Loot)->get_id());
        }
    };

    // The loot picked up (DAT_06fd3b90 {loot, ship}). Mode 1: dropped again.
    // Mode 2: a mission player has it -> objective 0xb goes, state 5. Mode 3: a mission player
    // has it -> objective 0xb goes, the dock of the offer base becomes objective 0xc. A
    // non-mission picker gets it dropped again.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgLootPickedUp))
    {
        FRmLootMessage const* Picked = static_cast<FRmLootMessage const*>(Message);
        FRmLootRecord* Record = static_cast<FRmLootRecord*>(Loot->Records.FindById(Picked->ObjectId));
        int const Mode = Record->CriticalLootMode;
        if (Mode == RmCriticalLootDestroy)
        {
            FDrop::Send(this, Record);
            return true;
        }
        if (Mode != RmCriticalLootTractor && Mode != RmCriticalLootReturn)
            return true;
        FContentPlayerState* Picker = FindContentPlayerStateByShip(&Picked->Value);
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        if (PlayerMap->find(Picker->PlayerId) == PlayerMap->end())
        {
            FDrop::Send(this, Record);
            return true;
        }
        RemoveObjective(RmObjTractorLoot);
        if (Mode == RmCriticalLootTractor)
        {
            set_state(RmStateSucceeded);
            return true;
        }

        // The dock object of the offer base (FUN_06eac4d0) nearest the holder's ship.
        FContentPlayerState* Holder = FindContentPlayerState(Record->HolderId);
        FContentBase* Base = FindContentBaseInfo(&VignetteOffer->OfferBase);
        Vector ShipPosition;
        Holder->ShipObject->GetPosition(&ShipPosition);
        unsigned int const DockId = Base->FindDockObject(&ShipPosition);
        Vector Where;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(DockId, Where, Orientation);
        std::vector<unsigned int> Dock;
        Dock.push_back(DockId);
        FRmParamText* const Return = RmComponentObjective(&VignetteOffer->General, RmObjReturnLoot);
        static_cast<FRmObjectiveType3*>(Return)->SetTargets(
            &Dock, GetMissionPlayers(), Base->GetSystemInfo()->SystemId, Where);
        AddObjective(RmObjReturnLoot, static_cast<FRmObjective*>(Return));
        return true;
    }

    // The loot created again (DAT_06fd3b84): mode 3 -> the offer's objective 0xb marks the
    // loot; modes 1 / 2 -> the active objective 0xa / 0xb marks every alive critical loot of
    // the mode (no end test on the find, as retail).
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgLootDropped))
    {
        unsigned int const LootId = static_cast<FRmLootMessage const*>(Message)->ObjectId;
        FRmLootRecord* Record = static_cast<FRmLootRecord*>(Loot->Records.FindById(LootId));
        int const Mode = Record->CriticalLootMode;
        if (Mode == RmCriticalLootReturn)
        {
            RemoveObjective(RmObjReturnLoot);
            RemoveObjective(RmObjTractorLoot);
            Vector Where;
            Matrix Orientation;
            pub::SpaceObj::GetLocation(Record->ObjectId, Where, Orientation);
            unsigned int SystemId = 0;
            pub::SpaceObj::GetSystem(Record->ObjectId, SystemId);
            FRmRecordList Single;
            Single.Records.push_back(Record);
            FRmParamText* const Tractor =
                RmComponentObjective(&VignetteOffer->General, RmObjTractorLoot);
            static_cast<FRmObjectiveType3*>(Tractor)->SetTargetRecords(
                &Single, GetMissionPlayers(), 2, SystemId, Where);
            AddObjective(RmObjTractorLoot, static_cast<FRmObjective*>(Tractor));
            return true;
        }
        if (Mode != RmCriticalLootDestroy && Mode != RmCriticalLootTractor)
            return true;
        Vector Where;
        Matrix Orientation;
        pub::SpaceObj::GetLocation(Record->ObjectId, Where, Orientation);
        unsigned int SystemId = 0;
        pub::SpaceObj::GetSystem(Record->ObjectId, SystemId);
        std::vector<FRmObjective*>::iterator Active =
            FindObjective(Mode == RmCriticalLootDestroy ? RmObjDestroyLoot : RmObjTractorLoot);
        FRmRecordList Critical = Loot->Query(Record->CriticalLootMode, 2, 0, 2, 0);
        FRmObjective* const Objective = *Active;
        static_cast<FRmObjectiveType3*>(Objective)
            ->SetTargetRecords(&Critical, GetMissionPlayers(), 2, SystemId, Where);
        RebuildObjective(Objective->Key);
        return true;
    }

    // The loot destroyed by a player (DAT_06fd3b94 {loot, ship}): mode 1 succeeds, modes 2 / 3
    // fail.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgLootDestroyed))
    {
        FRmLootMessage const* Destroyed = static_cast<FRmLootMessage const*>(Message);
        FRmLootRecord* Record =
            static_cast<FRmLootRecord*>(Loot->Records.FindById(Destroyed->ObjectId));
        int const Mode = Record->CriticalLootMode;
        if (Mode == RmCriticalLootDestroy)
        {
            RemoveObjective(RmObjDestroyLoot);
            set_state(RmStateSucceeded);
            return true;
        }
        if (Mode != RmCriticalLootTractor && Mode != RmCriticalLootReturn)
            return true;
        RemoveObjective(RmObjTractorLoot);
        VignetteOffer->General.SendConversations(RmCommGlobalFailDestroyedCriticalLoot, this,
                                                 FindContentPlayerStateByShip(&Destroyed->Value));
        FailReason = Mode == RmCriticalLootTractor ? 4 : 5;
        set_state(RmStateFailed);
        return true;
    }

    // The loot destroyed otherwise (DAT_06fd3b80); the reason is unchanged for another mode.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgLootLost))
    {
        unsigned int const LootId = static_cast<FRmLootMessage const*>(Message)->ObjectId;
        FRmLootRecord* Record = static_cast<FRmLootRecord*>(Loot->Records.FindById(LootId));
        int const Mode = Record->CriticalLootMode;
        if (Mode == RmCriticalLootDestroy)
            FailReason = 6;
        else if (Mode == RmCriticalLootTractor)
            FailReason = 7;
        else if (Mode == RmCriticalLootReturn)
            FailReason = 8;
        set_state(RmStateFailed);
        return true;
    }

    // Controller message 0x39 of a mapped player (parms +0xc) holding the first critical loot.
    // False either way. Retail reads the first record without an empty test: in state 12 a
    // critical loot record exists.
    if (SMControllerEvent const* Controller = RmControllerOf(Event, EMessage::ObjectDestroyed))
    {
        unsigned int const* Parms = static_cast<unsigned int const*>(Controller->DataPtr);
        std::map<unsigned int, FContentPlayerState*>* PlayerMap = GetMissionPlayerMap();
        if (PlayerMap->find(Parms[3]) == PlayerMap->end())
            return false;
        FRmRecordList Critical = Loot->Query(0, 2, 0, 0, 0);
        FRmLootRecord* const First = static_cast<FRmLootRecord*>(Critical.Records.front());
        if (First->State == RmRecordDestroyedOther && First->HolderId == Parms[3])
        {
            FailReason = 9;
            set_state(RmStateFailed);
        }
        return false;
    }

    // The holder docked (DAT_06fd3b88 {loot, ship, base}): mode 3 at the offer base succeeds,
    // elsewhere fails.
    if (StateMachineMessage const* Message = RmMessageOf(this, Event, GRmMsgLootHolderDocked))
    {
        FRmLootPlayerMessage const* Docked = static_cast<FRmLootPlayerMessage const*>(Message);
        FRmLootRecord* Record =
            static_cast<FRmLootRecord*>(Loot->Records.FindById(Docked->ObjectId));
        if (Record->CriticalLootMode != RmCriticalLootReturn)
            return true;
        if (Docked->Value1 == VignetteOffer->OfferBase)
        {
            RemoveObjective(RmObjReturnLoot);
            set_state(RmStateSucceeded);
            return true;
        }
        FailReason = 10;
        set_state(RmStateFailed);
        return true;
    }
    return false;
}

// State 5: succeeded.
bool FRmVignetteMachine::HandleSucceeded(SMEventData const* Event)
{
    FRmVignetteEntryCookies const& Cookies = RmVignetteEntryCookies();
    if (Event->is_sm_event() && Event->get_event_type() == SMET_Enter)
    {
        VignetteOffer->General.SendConversations(RmCommGlobalSuccess, this, nullptr);
        StateMachineMessage Timeout(SMM_TIMEOUT);
        send_delayed_message_to_me(&Timeout, 5.0f, SMMS_Self, 0);
        return true;
    }
    if (RmMessageOf(this, Event, SMM_TIMEOUT) != nullptr)
    {
        OnSucceeded();
        StateMachineMessage Done(GRmMsgMachineSucceeded);
        send_delayed_message(&Done, 0.001f, static_cast<MessageHandler*>(Mission)->get_id());
        set_state(RmStateDone);
        return true;
    }
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, Cookies.ReenterArea))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        return true;
    }
    return false;
}

// State 6: failed.
bool FRmVignetteMachine::HandleFailed(SMEventData const* Event)
{
    FRmVignetteEntryCookies const& Cookies = RmVignetteEntryCookies();
    if (Event->is_sm_event() && Event->get_event_type() == SMET_Enter)
    {
        StateMachineMessage Timeout(SMM_TIMEOUT);
        send_delayed_message_to_me(&Timeout, 5.0f, SMMS_Self, 0);
        return true;
    }
    if (RmMessageOf(this, Event, SMM_TIMEOUT) != nullptr)
    {
        FRmMachineFailedMessage Failed(FailReason);
        send_delayed_message(&Failed, 0.001f, static_cast<MessageHandler*>(Mission)->get_id());
        set_state(RmStateDone);
        return true;
    }
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, Cookies.ReenterArea))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        return true;
    }
    return false;
}

// State 7: done.
bool FRmVignetteMachine::HandleDone(SMEventData const* Event)
{
    FRmVignetteEntryCookies const& Cookies = RmVignetteEntryCookies();
    if (Event->is_sm_event() && Event->get_event_type() == SMET_Enter)
    {
        Shutdown();
        return true;
    }
    if (FRmTriggerFiredMessage const* Fired = RmFiredTriggerOf(this, Event, Cookies.ReenterArea))
    {
        if (Fired->bFinal)
            RemoveMachine(Fired->Sender);
        return true;
    }
    return false;
}
