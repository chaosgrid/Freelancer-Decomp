// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Missions/MissionScript.h"
#include "Core/ContentStaticIds.h"   // TU21's 16 static-init thunks 06eb7360-06eb7540
#include "Core/ContentDefaultId.h"   // TU21's "default" thunk 06eb7560
#include "Bases/BaseDB.h"
#include "Core/ContentInternal.h"
#include "Core/ContentPlayerState.h"
#include "Encounters/Encounters.h"
#include "Missions/MissionAct.h"
#include "Missions/MissionCnd.h"
#include "Missions/MissionDialogs.h"
#include "Missions/MissionObjList.h"     // CreateObjListDirective, FObjListAvoidance (FUN_06eb8d10)
#include "Core/RefHandle.h"          // TRefHandle
#include "NPCs/BehaviorCmds.h"       // FContentShipBase::SetDirectives
#include "NPCs/DockQueues.h"
#include "NPCs/NpcShips.h"
#include "NPCs/PilotPersonalities.h" // GetPilotPersonality (FUN_06f3e020)
#include "Encounters/Populators.h"
#include "RandomMissions/RandomMissionDB.h"
#include "Bases/RtcScripts.h"
#include "Universe/SystemInfo.h"
#include "common_goods.h"
#include "common_id.h"
#include "common_iface.h"
#include "common_ini.h"
#include "common_server.h"
#include "common_server_controller.h"
#include "server_gameobject.h"       // FLootAcquiredEvent (controller message 0x18)

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string.h>
#include <windows.h>

// DAT_06fd1c79 (see MissionScript.h).
unsigned char GMissionScanClamp = 0;

// DAT_06fcf018: stored 0 by the TU21 ctor; retail never reads it.
unsigned int GMissionScriptDat06fcf018 = 0;

namespace
{
    int const MissionStateFail = G_MissionStateFail;
    int const MissionStateSucceed = G_MissionStateSucceed;
    int const MissionStateNoState = G_MissionStateNoState;
}

// =======================================================================================
// Retail trigger engine (TU21). See MissionScript.h.
// =======================================================================================

namespace
{
    template <class T>
    Cnd* NewTriggerCondition(FMissionScript* Mission)
    {
        return new T(Mission);
    }

    template <class T>
    Act* NewTriggerAction(FMissionScript* Mission)
    {
        return new T(Mission);
    }

    // One Cnd_* key of FUN_06ebb850: the class and the message ids pushed into trigger +0x18
    // after Parse.
    using pub::Controller::EMessage;

    struct FCndFactoryEntry
    {
        char const* Name;
        Cnd* (*Create)(FMissionScript* Mission);
        int EventCount;
        EMessage EventIds[3];   // past EventCount: unused (value-initialized)
    };

    // In the order of the is_value chain.
    FCndFactoryEntry const GCndFactory[] = {
        {"Cnd_BaseEnter", &NewTriggerCondition<CndBaseEnter>, 1, {EMessage::BaseEnter}},
        {"Cnd_BaseExit", &NewTriggerCondition<CndBaseExit>, 1, {EMessage::BaseExit}},
        {"Cnd_CargoScanned", &NewTriggerCondition<CndCargoScanned>, 1, {EMessage::CargoScanned}},
        {"Cnd_CharSelect", &NewTriggerCondition<CndCharSelect>, 1, {EMessage::GFObjSelect}},
        {"Cnd_CommComplete", &NewTriggerCondition<CndCommComplete>, 1, {EMessage::CommComplete}},
        {"Cnd_CmpToPlane", &NewTriggerCondition<CndCmpToPlane>, 1, {EMessage::Heartbeat}},
        {"Cnd_Destroyed", &NewTriggerCondition<CndDestroyed>, 1, {EMessage::ObjectDestroyed}},
        {"Cnd_DistCircle", &NewTriggerCondition<CndDistCircle>, 1, {EMessage::Heartbeat}},
        {"Cnd_DistShip", &NewTriggerCondition<CndDistShip>, 1, {EMessage::Heartbeat}},
        {"Cnd_DistVec", &NewTriggerCondition<CndDistVec>, 1, {EMessage::Heartbeat}},
        {"Cnd_DistVecLbl", &NewTriggerCondition<CndDistVecLbl>, 1, {EMessage::Heartbeat}},
        {"Cnd_EncLaunched", &NewTriggerCondition<CndEncLaunched>, 1, {EMessage::EncounterLaunched}},
        {"Cnd_HasMsn", &NewTriggerCondition<CndHasMsn>, 1, {EMessage::Heartbeat}},
        {"Cnd_HealthDec", &NewTriggerCondition<CndHealthDec>, 1, {EMessage::ObjectDamaged}},
        {"Cnd_InSpace", &NewTriggerCondition<CndInSpace>, 1, {EMessage::Heartbeat}},
        {"Cnd_InTradelane", &NewTriggerCondition<CndInTradelane>, 1, {EMessage::Heartbeat}},
        {"Cnd_InZone", &NewTriggerCondition<CndInZone>, 1, {EMessage::Heartbeat}},
        {"Cnd_JumpgateAct", &NewTriggerCondition<CndJumpgateAct>, 1, {EMessage::JumpgateAct}},
        {"Cnd_JumpInComplete", &NewTriggerCondition<CndJumpInComplete>, 1, {EMessage::JumpInComplete}},
        {"Cnd_LaunchComplete", &NewTriggerCondition<CndLaunchComplete>, 1, {EMessage::LaunchComplete}},
        {"Cnd_LocEnter", &NewTriggerCondition<CndLocEnter>, 1, {EMessage::LocationEnter}},
        {"Cnd_LocExit", &NewTriggerCondition<CndLocExit>, 1, {EMessage::LocationExit}},
        {"Cnd_LootAcquired", &NewTriggerCondition<CndLootAcquired>, 1, {EMessage::LootAcquired}},
        {"Cnd_MsnResponse", &NewTriggerCondition<CndMsnResponse>, 1, {EMessage::MissionResponse}},
        {"Cnd_NPCSystemEnter", &NewTriggerCondition<CndNpcSystemEnter>, 3,
         {EMessage::NpcSystemEnter, EMessage::Heartbeat, EMessage::NpcShipCreated}},
        {"Cnd_NPCSystemExit", &NewTriggerCondition<CndNpcSystemExit>, 1, {EMessage::NpcSystemExit}},
        {"Cnd_PlayerLaunch", &NewTriggerCondition<CndPlayerLaunch>, 1, {EMessage::PlayerLaunch}},
        {"Cnd_PlayerManeuver", &NewTriggerCondition<CndPlayerManeuver>, 1, {EMessage::PlayerManeuver}},
        {"Cnd_PopUpDialog", &NewTriggerCondition<CndPopUpDialog>, 1, {EMessage::PopUpDialog}},
        {"Cnd_ProjHit", &NewTriggerCondition<CndProjHit>, 1, {EMessage::ObjectDamaged}},
        {"Cnd_ProjHitShipToLbl", &NewTriggerCondition<CndProjHitShipToLbl>, 1, {EMessage::ObjectDamaged}},
        {"Cnd_RTCDone", &NewTriggerCondition<CndRtcDone>, 1, {EMessage::RtcDone}},
        {"Cnd_RumorHeard", &NewTriggerCondition<CndRumorHeard>, 1, {EMessage::RumorHeard}},
        {"Cnd_SpaceEnter", &NewTriggerCondition<CndSpaceEnter>, 1, {EMessage::SpaceEnter}},
        {"Cnd_SpaceExit", &NewTriggerCondition<CndSpaceExit>, 1, {EMessage::SpaceExit}},
        {"Cnd_SystemEnter", &NewTriggerCondition<CndSystemEnter>, 1, {EMessage::SystemEnter}},
        {"Cnd_SystemExit", &NewTriggerCondition<CndSystemExit>, 1, {EMessage::SystemExit}},
        {"Cnd_TetherBroke", &NewTriggerCondition<CndTetherBroke>, 1, {EMessage::TetherObject}},
        {"Cnd_Timer", &NewTriggerCondition<CndTimer>, 1, {EMessage::Heartbeat}},
        {"Cnd_TLEntered", &NewTriggerCondition<CndTlEntered>, 1, {EMessage::TradelaneEnter}},
        {"Cnd_TLExited", &NewTriggerCondition<CndTlExited>, 1, {EMessage::TradelaneExit}},
        {"Cnd_True", &NewTriggerCondition<CndTrue>, 1, {EMessage::Heartbeat}},
        {"Cnd_WatchTrigger", &NewTriggerCondition<CndWatchTrigger>, 1, {EMessage::Heartbeat}},
        {"Cnd_WatchVibe", &NewTriggerCondition<CndWatchVibe>, 1, {EMessage::Heartbeat}},
    };

    // One Act_* key of FUN_06ebb850 (new, ctor, Parse, push_back into trigger +0x08 with
    // FUN_06ecfc30, FUN_06ebf700 with trigger +0x2c).
    struct FActFactoryEntry
    {
        char const* Name;
        Act* (*Create)(FMissionScript* Mission);
    };

    // In the order of the is_value chain.
    FActFactoryEntry const GActFactory[] = {
        {"Act_ActTrig", &NewTriggerAction<ActActTrig>},
        {"Act_AddAmbient", &NewTriggerAction<ActAddAmbient>},
        {"Act_AddRTC", &NewTriggerAction<ActAddRtc>},
        {"Act_AdjAcct", &NewTriggerAction<ActAdjAcct>},
        {"Act_AdjHealth", &NewTriggerAction<ActAdjHealth>},
        {"Act_CallThorn", &NewTriggerAction<ActCallThorn>},
        {"Act_ChangeState", &NewTriggerAction<ActChangeState>},
        {"Act_Cloak", &NewTriggerAction<ActCloak>},
        {"Act_DeactTrig", &NewTriggerAction<ActDeactTrig>},
        {"Act_DebugMsg", &NewTriggerAction<ActDebugMsg>},
        {"Act_Destroy", &NewTriggerAction<ActDestroy>},
        {"Act_DisableEnc", &NewTriggerAction<ActDisableEnc>},
        {"Act_DisableFriendlyFire", &NewTriggerAction<ActDisableFriendlyFire>},
        {"Act_DisableTradelane", &NewTriggerAction<ActDisableTradelane>},
        {"Act_DockRequest", &NewTriggerAction<ActDockRequest>},
        {"Act_EnableEnc", &NewTriggerAction<ActEnableEnc>},
        {"Act_EnableManeuver", &NewTriggerAction<ActEnableManeuver>},
        {"Act_EtherComm", &NewTriggerAction<ActEtherComm>},
        {"Act_ForceLand", &NewTriggerAction<ActForceLand>},
        {"Act_GCSClamp", &NewTriggerAction<ActGcsClamp>},
        {"Act_GiveMB", &NewTriggerAction<ActGiveMb>},
        {"Act_GiveNNObjs", &NewTriggerAction<ActGiveNnObjs>},
        {"Act_GiveObjList", &NewTriggerAction<ActGiveObjList>},
        {"Act_HostileClamp", &NewTriggerAction<ActHostileClamp>},
        {"Act_Invulnerable", &NewTriggerAction<ActInvulnerable>},
        {"Act_Jumper", &NewTriggerAction<ActJumper>},
        {"Act_LightFuse", &NewTriggerAction<ActLightFuse>},
        {"Act_LockDock", &NewTriggerAction<ActLockDock>},
        {"Act_LockManeuvers", &NewTriggerAction<ActLockManeuvers>},
        {"Act_MarkObj", &NewTriggerAction<ActMarkObj>},
        {"Act_MovePlayer", &NewTriggerAction<ActMovePlayer>},
        {"Act_NagClamp", &NewTriggerAction<ActNagClamp>},
        {"Act_NagDistLeaving", &NewTriggerAction<ActNagDistLeaving>},
        {"Act_NagDistTowards", &NewTriggerAction<ActNagDistTowards>},
        {"Act_NagGreet", &NewTriggerAction<ActNagGreet>},
        {"Act_NagOff", &NewTriggerAction<ActNagOff>},
        {"Act_NNPath", &NewTriggerAction<ActNnPath>},
        {"Act_NNIds", &NewTriggerAction<ActNnIds>},
        {"Act_PlayerCanDock", &NewTriggerAction<ActPlayerCanDock>},
        {"Act_PlayerCanTradelane", &NewTriggerAction<ActPlayerCanTradelane>},
        {"Act_PlayerEnemyClamp", &NewTriggerAction<ActPlayerEnemyClamp>},
        {"Act_PlayerForm", &NewTriggerAction<ActPlayerForm>},
        {"Act_PlayMusic", &NewTriggerAction<ActPlayMusic>},
        {"Act_PlayNN", &NewTriggerAction<ActPlayNn>},
        {"Act_PlaySoundEffect", &NewTriggerAction<ActPlaySoundEffect>},
        {"Act_PilotParams", &NewTriggerAction<ActPilotParams>},
        {"Act_PobjIdle", &NewTriggerAction<ActPobjIdle>},
        {"Act_PopUpDialog", &NewTriggerAction<ActPopUpDialog>},
        {"Act_SetPriority", &NewTriggerAction<ActSetPriority>},
        {"Act_RandomPop", &NewTriggerAction<ActRandomPop>},
        {"Act_RandomPopSphere", &NewTriggerAction<ActRandomPopSphere>},
        {"Act_RelocateForm", &NewTriggerAction<ActRelocateForm>},
        {"Act_RelocateShip", &NewTriggerAction<ActRelocateShip>},
        {"Act_RemoveAmbient", &NewTriggerAction<ActRemoveAmbient>},
        {"Act_RemoveCargo", &NewTriggerAction<ActRemoveCargo>},
        {"Act_RemoveRTC", &NewTriggerAction<ActRemoveRtc>},
        {"Act_RepChangeRequest", &NewTriggerAction<ActRepChangeRequest>},
        {"Act_RevertCam", &NewTriggerAction<ActRevertCam>},
        {"Act_RpopAttClamp", &NewTriggerAction<ActRpopAttClamp>},
        {"Act_RpopTLAttacksEnabled", &NewTriggerAction<ActRpopTlAttacksEnabled>},
        {"Act_Save", &NewTriggerAction<ActSave>},
        {"Act_SendComm", &NewTriggerAction<ActSendComm>},
        {"Act_SetFlee", &NewTriggerAction<ActSetFlee>},
        {"Act_SetInitialPlayerPos", &NewTriggerAction<ActSetInitialPlayerPos>},
        {"Act_SetLifeTime", &NewTriggerAction<ActSetLifeTime>},
        {"Act_SetNNHidden", &NewTriggerAction<ActSetNnHidden>},
        {"Act_SetNNObj", &NewTriggerAction<ActSetNnObj>},
        {"Act_SetNNState", &NewTriggerAction<ActSetNnState>},
        {"Act_SetOffer", &NewTriggerAction<ActSetOffer>},
        {"Act_SetOrient", &NewTriggerAction<ActSetOrient>},
        {"Act_SetRep", &NewTriggerAction<ActSetRep>},
        {"Act_SetShipAndLoadout", &NewTriggerAction<ActSetShipAndLoadout>},
        {"Act_SetTitle", &NewTriggerAction<ActSetTitle>},
        {"Act_SetVibe", &NewTriggerAction<ActSetVibe>},
        {"Act_SetVibeLbl", &NewTriggerAction<ActSetVibeLbl>},
        {"Act_SetVibeLblToShip", &NewTriggerAction<ActSetVibeLblToShip>},
        {"Act_SetVibeShipToLbl", &NewTriggerAction<ActSetVibeShipToLbl>},
        {"Act_SetVibeOfferBaseHack", &NewTriggerAction<ActSetVibeOfferBaseHack>},
        {"Act_SpawnFormation", &NewTriggerAction<ActSpawnFormation>},
        {"Act_SpawnLoot", &NewTriggerAction<ActSpawnLoot>},
        {"Act_SpawnShip", &NewTriggerAction<ActSpawnShip>},
        {"Act_SpawnShipRel", &NewTriggerAction<ActSpawnShipRel>},
        {"Act_SpawnSolar", &NewTriggerAction<ActSpawnSolar>},
        {"Act_StartDialog", &NewTriggerAction<ActStartDialog>},
        {"Act_StaticCam", &NewTriggerAction<ActStaticCam>},
    };
}

// FUN_06ebb850. A second Cnd_* key replaces the condition without deleting the first (a leak,
// as retail). An unknown InitState and "repeatable = false" leave the field.
void FTrigger::Load(INI_Reader* Reader)
{
    while (Reader->read_value())
    {
        bool bMatched = false;
        for (int Index = 0; Index < static_cast<int>(sizeof(GCndFactory) / sizeof(GCndFactory[0]));
             ++Index)
        {
            FCndFactoryEntry const& Entry = GCndFactory[Index];
            if (!Reader->is_value(Entry.Name))
                continue;
            Cnd* const Condition = Entry.Create(Mission);
            this->Condition = Condition;
            Condition->Parse(*Reader);
            for (int EventIndex = 0; EventIndex < Entry.EventCount; ++EventIndex)
                EventIds.push_back(Entry.EventIds[EventIndex]);
            this->Condition->SetTriggerId(Id);
            bMatched = true;
            break;
        }
        if (bMatched)
            continue;
        for (int Index = 0; Index < static_cast<int>(sizeof(GActFactory) / sizeof(GActFactory[0]));
             ++Index)
        {
            FActFactoryEntry const& Entry = GActFactory[Index];
            if (!Reader->is_value(Entry.Name))
                continue;
            Act* const Action = Entry.Create(Mission);
            Action->Parse(*Reader);
            Actions.push_back(Action);
            Action->SetTriggerId(Id);
            bMatched = true;
            break;
        }
        if (bMatched)
            continue;
        if (Reader->is_value("InitState"))
        {
            char const* const StateName = Reader->get_value_string(0);
            if (_stricmp("ACTIVE", StateName) == 0)
                State = G_TriggerStateActive;
            else if (_stricmp("INACTIVE", StateName) == 0)
                State = G_TriggerStateInactive;
            else if (_stricmp("COMPLETE", StateName) == 0)
                State = G_TriggerStateComplete;
            continue;
        }
        if (Reader->is_value("repeatable"))
        {
            if (Reader->get_value_bool(0))
                bRepeatable = 1;
            continue;
        }
        // Retail tests "system" and ignores the result.
        Reader->is_value("system");
    }
}

// FUN_06eb84c0 [Trigger] branch, after is_header("Trigger").
void FMissionScript::LoadTriggerSection(INI_Reader* Reader)
{
    unsigned int TriggerId = 0;
    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            TriggerId = CreateID(Reader->get_value_string(0));
            // A known nickname is skipped.
            if (TriggerMap.find(TriggerId) != TriggerMap.end())
                continue;
            FTrigger Trigger;
            Trigger.State = G_TriggerStateInactive;
            Trigger.Condition = nullptr;
            Trigger.Mission = this;
            Trigger.bRepeatable = 0;
            Trigger.Id = TriggerId;
            Trigger.Load(Reader);
            // The copies share the condition and action pointers.
            TriggerMap.insert(FTriggerMap::value_type(TriggerId, Trigger));
            // An InitState ACTIVE trigger also goes into +0x1e4 as {id, id}.
            if (Trigger.State == G_TriggerStateActive)
                InitiallyActive.insert(
                    std::map<unsigned int, unsigned int>::value_type(TriggerId, TriggerId));
            continue;
        }
        // "State" goes through vtbl +0x68 and the result is dropped.
        if (Reader->is_value("State"))
            static_cast<void>(MissionStateFromName(Reader->get_value_string(0)));
    }
}

// FUN_06ebb830.
void FTrigger::Rearm()
{
    State = G_TriggerStateActive;
    if (Condition != nullptr)
        Condition->OnActivate();
}

// FUN_06ebb7f0.
void FTrigger::DeleteContents()
{
    if (Condition != nullptr)
        delete Condition;
    std::list<Act*>::iterator It = Actions.begin();
    for (; It != Actions.end(); ++It)
    {
        if (*It != nullptr)
            delete *It;
    }
}

// FUN_06eb82c0.
void FMissionScript::ActivateTrigger(unsigned int const* TriggerId)
{
    FTriggerMap::iterator Found = TriggerMap.find(*TriggerId);
    if (Found == TriggerMap.end())
        return;

    // The whole active list is walked (no early exit).
    bool bAlreadyActive = false;
    std::list<FTriggerMap::iterator>::iterator ActiveIt = ActiveTriggers.begin();
    for (; ActiveIt != ActiveTriggers.end(); ++ActiveIt)
    {
        if ((*ActiveIt)->first == *TriggerId)
            bAlreadyActive = true;
    }
    if (bAlreadyActive)
        return;

    FTrigger& Trigger = Found->second;
    Trigger.State = G_TriggerStateActive;
    if (Trigger.Condition != nullptr)
        Trigger.Condition->OnActivate();

    // State is re-read; the condition is called without a null check. -1 is no message id:
    // the activation probe.
    if (Trigger.State == G_TriggerStateActive &&
        Trigger.Condition->Matches(static_cast<EMessage>(-1), nullptr))
    {
        Trigger.State = G_TriggerStateComplete;
        std::list<Act*>::iterator ActIt = Trigger.Actions.begin();
        for (; ActIt != Trigger.Actions.end(); ++ActIt)
            (*ActIt)->Execute();
        // FTrigger::Rearm inlined.
        if (Trigger.bRepeatable != 0)
        {
            Trigger.State = G_TriggerStateActive;
            if (Trigger.Condition != nullptr)
                Trigger.Condition->OnActivate();
        }
    }

    if (Trigger.State != G_TriggerStateComplete)
        ActiveTriggers.push_back(Found);
}

// FUN_06eb83e0.
void FMissionScript::DeactivateTrigger(unsigned int const* TriggerId)
{
    // Only the first active node with this key whose trigger is not COMPLETE.
    std::list<FTriggerMap::iterator>::iterator ActiveIt = ActiveTriggers.begin();
    for (; ActiveIt != ActiveTriggers.end(); ++ActiveIt)
    {
        FTrigger& Trigger = (*ActiveIt)->second;
        if ((*ActiveIt)->first == *TriggerId && Trigger.State != G_TriggerStateComplete)
        {
            Trigger.State = G_TriggerStateInactive;
            ActiveTriggers.erase(ActiveIt);
            break;
        }
    }

    std::map<unsigned int, unsigned int>::iterator PendingIt = PendingActivations.begin();
    while (PendingIt != PendingActivations.end())
    {
        if (PendingIt->first == *TriggerId)
            PendingIt = PendingActivations.erase(PendingIt);
        else
            ++PendingIt;
    }
}

// FUN_06eb9af0. *EventId is the message id the caller (FUN_06eb8f50) stored in its local.
void FMissionScript::DispatchTriggerEvent(EMessage const* EventId, void const* EventParms)
{
    std::list<FTriggerMap::iterator>::iterator ActiveIt = ActiveTriggers.begin();
    while (ActiveIt != ActiveTriggers.end())
    {
        FTrigger& Trigger = (*ActiveIt)->second;

        bool bListens = false;
        std::vector<EMessage>::iterator EventIt = Trigger.EventIds.begin();
        for (; EventIt != Trigger.EventIds.end(); ++EventIt)
        {
            if (*EventIt == *EventId)
            {
                bListens = true;
                break;
            }
        }

        // Only ACTIVE triggers are evaluated; others are skipped, never erased.
        if (bListens && Trigger.State == G_TriggerStateActive)
        {
            if (Trigger.Condition->Matches(*EventId, EventParms))
            {
                Trigger.State = G_TriggerStateComplete;
                std::list<Act*>::iterator ActIt = Trigger.Actions.begin();
                for (; ActIt != Trigger.Actions.end(); ++ActIt)
                    (*ActIt)->Execute();
                if (Trigger.bRepeatable != 0)
                    Trigger.Rearm();
            }
            if (Trigger.State == G_TriggerStateComplete)
            {
                ActiveIt = ActiveTriggers.erase(ActiveIt);
                continue;
            }
        }
        ++ActiveIt;
    }

    // Drain the pending map front to back. Retail also looks the source id up in the trigger
    // map and discards the result; that lookup has no side effect and is not reproduced.
    while (!PendingActivations.empty())
    {
        std::map<unsigned int, unsigned int>::iterator First = PendingActivations.begin();
        unsigned int TargetId = First->first;
        PendingActivations.erase(First);
        ActivateTrigger(&TargetId);
    }
    PendingActivations.clear();
}

// FUN_06eb9c60.
void FMissionScript::TickTimers(float DeltaSeconds)
{
    std::list<FMissionTimer>::iterator It = Timers.begin();
    while (It != Timers.end())
    {
        It->Remaining = It->Remaining - DeltaSeconds;
        // DAT_06fb444c; unordered does not run.
        if (It->Remaining <= 0.0f)
        {
            if (It->Action != nullptr)
                It->Action->Execute();
            It = Timers.erase(It);
            continue;
        }
        ++It;
    }
}

// LAB_06eb8260 (names at DAT_06fb47c0).
int FMissionScript::MissionStateFromName(char const* Name)
{
    if (_stricmp(Name, "FAIL") == 0)
        return MissionStateFail;
    if (_stricmp(Name, "SUCCEED") == 0)
        return MissionStateSucceed;
    if (_stricmp(Name, "NO_STATE") == 0)
        return MissionStateNoState;
    return -1;
}

// LAB_06eb9730.
void FMissionScript::SetDesiredMissionState(int State, unsigned int MadLibId)
{
    if (Field_0x2b0 == 1 && State == MissionStateFail)
        return;
    MissionState = State;
    StateMessageId = MadLibId;
}

// FUN_06eb8f50 case 0x1000.
void ActivateInitialMissionTriggers(FMissionScript* Script)
{
    std::map<unsigned int, unsigned int>::iterator It = Script->InitiallyActive.begin();
    for (; It != Script->InitiallyActive.end(); ++It)
        Script->ActivateTrigger(&It->first);
}

namespace
{
    // The script's +0x10 (Roles._First) passed as FindCharacter's role: the first role's
    // TString at +0x00; retail passes the raw pointer, null for an empty vector.
    TString<32> const* GetFirstRtcRole(FCharacterEncounter const* Script)
    {
        if (Script->Roles.empty())
            return nullptr;
        return &Script->Roles.front().Role;
    }
}

// FUN_06eba430.
void FMissionScript::RemoveRtc(TString<0x80> const* Path)
{
    std::list<FMissionRtc>::iterator It = RtcScripts.begin();
    for (; It != RtcScripts.end(); ++It)
    {
        if (strcmp(It->Path, Path->Data) != 0)
            continue;
        if (FindContentPlayerState(PlayerId) == nullptr)
            continue;
        // Any miss below still marks the script finished.
        FContentBase* const Base = PlayerState->GetBaseRecord();
        if (Base != nullptr)
        {
            FBaseRoomState* const Room = Base->FindRoomState(&CurrentLocationId);
            if (Room != nullptr && It->Encounter != nullptr &&
                FindContentPlayerState(PlayerId) != nullptr)
            {
                FBarCharacterList::iterator Character;
                if (Room->FindCharacter(GetFirstRtcRole(It->Encounter), PlayerId,
                                        &Character))
                {
                    Character->SetBehavior(static_cast<unsigned long>(-1));
                }
            }
        }
        It->bFinished = 1;
    }
}

// FUN_06ebb540.
void FMissionScript::MovePlayer(Vector const* Position, float RadiusSq)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    FContentPlayerShip* const Ship = PlayerState->ShipObject;
    if (Ship == nullptr)
        return;
    // Each delta is stored as a float.
    Vector ShipPosition;
    Ship->GetPosition(&ShipPosition);
    float const DeltaX = ShipPosition.x - Position->x;
    float const DeltaY = ShipPosition.y - Position->y;
    float const DeltaZ = ShipPosition.z - Position->z;
    // (y*y + z*z) + x*x on the x87 stack; only a distance strictly beyond the radius (and not
    // unordered) moves the ship.
    double const DistanceSq = static_cast<double>(DeltaY) * DeltaY
                              + static_cast<double>(DeltaZ) * DeltaZ
                              + static_cast<double>(DeltaX) * DeltaX;
    if (!(DistanceSq > static_cast<double>(RadiusSq)))
        return;
    // The player's system record is not null-checked.
    Vector CurrentPosition;
    Matrix CurrentOrientation;
    pub::SpaceObj::GetLocation(Ship->SpaceObjId, CurrentPosition, CurrentOrientation);
    unsigned int const SystemId = PlayerState->GetSystemRecord()->SystemId;
    unsigned int const ShipId = PlayerState->ShipObject->SpaceObjId;
    RelocateShip(&ShipId, &SystemId, Position, &CurrentOrientation);
}

// =======================================================================================
// FMissionScript (TU21 script mission, vtable 06fb4710)
// =======================================================================================

// FUN_06eb75a0. Slots 18 and 2 are called directly, not through the vtable.
// +0x2b4/+0x2b8/+0x2c0/+0x2c4 and +0x308 are left to the mission ctors.
FMissionScript::FMissionScript(unsigned int const* InControllerId,
                               FStoryMissionCreateParms const* Parms)
    : FMissionObject(InControllerId, Parms)
    , StoryEvents(nullptr)
    , PreviousMissionState(G_MissionStateNoState)
    , MissionState(G_MissionStateNoState)
    , Field_0x2b0(0)
    , CampaignSlotCurrent(0)
    , CampaignSlotSucceed(0)
    , StoryCue(0)
    , Field_0x2c0(0)
    , AcceptedObjectiveIds(0)
    , bCutsceneActive(0)
    , bRepPopUpShown(0)
    , bRepPopUpArmed(0)
    , RepPopUpSystemId(0)
    , StateMessageId(1)
    , bNeedsObjectiveRenumber(0)
{
    GMissionScriptDat06fcf018 = 0;
    FMissionObject::SetPlayer(Parms->PlayerId);
    StoryEvents = Parms->StoryEvents;
    FMissionObject::GetOrCreatePlayer(PlayerId);
    ElapsedTime = 0.0f;
}

// FUN_06eb7d30; the remaining members, then the TU38 base (FUN_06f0ed00), die afterwards.
FMissionScript::~FMissionScript()
{
    ActiveTriggers.clear();
    FTriggerMap::iterator TriggerIt = TriggerMap.begin();
    while (TriggerIt != TriggerMap.end())
    {
        TriggerIt->second.DeleteContents();
        TriggerIt = TriggerMap.erase(TriggerIt);
    }
    TriggerMap.clear();
    ReleaseRtcScripts();
    std::map<unsigned int, FMissionNag*>::iterator NagIt = Nags.begin();
    while (NagIt != Nags.end())
    {
        if (NagIt->second != nullptr)
            delete NagIt->second;
        NagIt = Nags.erase(NagIt);
    }
    FriendlyFireOffLabels.clear();
    std::list<FMissionAmbientScene>::iterator SceneIt = AmbientScenes.begin();
    for (; SceneIt != AmbientScenes.end(); ++SceneIt)
    {
        if (SceneIt->Handle != -1)
        {
            unsigned long Handle = static_cast<unsigned long>(SceneIt->Handle);
            pub::GF::AmbientScriptDestroy(&Handle);
            SceneIt->Handle = -1;
        }
    }
    AmbientScenes.clear();
}

// FUN_06eb8b70.
void FMissionScript::ReleaseRtcScripts()
{
    FContentBase* Base = nullptr;
    if (FindContentPlayerState(PlayerId) != nullptr)
        Base = PlayerState->GetBaseRecord();
    std::list<FMissionRtc>::iterator It = RtcScripts.begin();
    for (; It != RtcScripts.end(); ++It)
    {
        if (It->Encounter == nullptr)
            continue;
        if (Base != nullptr && It->BarSceneHandle != -1)
        {
            // RemoveBarScene = FUN_06ea9fc0.
            Base->RemoveBarScene(It->BarSceneHandle, nullptr);
            It->BarSceneHandle = -1;
        }
        // FUN_06eb8c40 is the script's member-wise teardown.
        delete It->Encounter;
        It->Encounter = nullptr;
    }
    RtcScripts.clear();
}

// FUN_06eb8a20.
void FMissionScript::StartRtcScript(FMissionRtc* Rtc)
{
    // operator new(0x54) + FUN_06ed4420.
    FCharacterEncounter* Script = new FCharacterEncounter();
    if (FindContentPlayerState(PlayerId) != nullptr)
    {
        FContentBase* Base = PlayerState->GetBaseRecord();
        // Load = FUN_06ed44a0; the script's base must be the player's.
        if (Base != nullptr && Script->Load(Rtc->Path) && Base->BaseId == Script->BaseId)
        {
            // A mission text without a live mission takes the loaded text id.
            if (Script->MissionTextId != 0 && (PlayerState->GetLiveMission() & GIdValueMask) == 0)
                Script->MissionTextId = Script->LoadedMissionTextId;
            // AllocateFormationId = FUN_06efeb00, CreateBarScene = FUN_06ea9640.
            Script->SequenceId = AllocateFormationId();
            Rtc->BarSceneHandle = Base->CreateBarScene(Script, PlayerId, ControllerId);
            Rtc->Encounter = Script;
            return;
        }
    }
    delete Script;
}

// FUN_06eb8d10.
void FMissionScript::RevertStaticCam()
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    // The ship id is not masked here.
    bCutsceneActive = 0;
    pub::SpaceObj::SetInvincible(PlayerState->GetShipId(), false, true, 0.0f);
    NagClamp = 0;
    std::map<unsigned int, FContentNpcShip*>::iterator NpcIt = NpcObjects.begin();
    for (; NpcIt != NpcObjects.end(); ++NpcIt)
        NpcIt->second->SetCommMuted(false);
    // The player ship gets one new Avoidance(true) directive.
    FContentPlayerShip* const Ship = PlayerState->ShipObject;
    if (Ship != nullptr)
    {
        FMissionDirectiveList Directives;
        TRefHandle<FObjListDirective> Directive;
        CreateObjListDirective(OBJLIST_AVOIDANCE, &Directive.Pointer);
        static_cast<FObjListAvoidance*>(Directive.Pointer)->SetAvoidance(true);
        Directives.Items.push_back(Directive.Pointer);
        Directive.Pointer->AddRef();
        Ship->SetDirectives(&Directives.Items, 0);
    }
    SpacePopSetGcsClampEnabled(0);
}

void FMissionScript::SetChapter(int SlotCurrent, int SlotSucceed, unsigned int Value2c0,
                                unsigned int Value2c4)
{
    CampaignSlotCurrent = SlotCurrent;
    CampaignSlotSucceed = SlotSucceed;
    Field_0x2c0 = Value2c0;
    AcceptedObjectiveIds = Value2c4;
}

void FMissionScript::LoadMissionIni(char const* IniPath)
{
    INI_Reader Reader;
    OpenContentDataIni(IniPath, &Reader);
    LoadMission(&Reader);
    Reader.close();
}

// LAB_06eba010 (NnPath.bActive is +0x290).
void FMissionScript::UpdateObjectiveTimer(EMessage MessageId, void const* MessageParms)
{
    if (NnPath.bActive != 0)
        return;
    FMissionObject::UpdateObjectiveTimer(MessageId, MessageParms);
}

// FUN_06ebab90.
void FMissionScript::OnNpcSystemEnter(void const* Parms)
{
    EMessage EventId = EMessage::NpcSystemEnter;
    DispatchTriggerEvent(&EventId, Parms);
}

// FUN_06eb9aa0. OnMissionFail / OnMissionSucceed are vtbl +0x60 / +0x5c; OnMissionEvent is
// StoryEvents vtbl 0.
void FMissionScript::ApplyDesiredMissionState()
{
    int Desired = MissionState;
    if (Desired == PreviousMissionState)
        return;
    PreviousMissionState = Desired;
    if (Desired == G_MissionStateFail)
    {
        OnMissionFail();
        return;
    }
    if (Desired != G_MissionStateSucceed)
        return;
    OnMissionSucceed();
    if (StoryEvents != nullptr)
        StoryEvents->OnMissionEvent(STORY_MISSION_EVENT_SUCCEEDED, static_cast<unsigned int>(CampaignSlotSucceed), ControllerId);
}

// FUN_06eb9760: the default mad lib 1 becomes 0x4cf.
void FMissionScript::OnMissionSucceed()
{
    if (StateMessageId != 0 && FindContentPlayerState(PlayerId) != nullptr)
    {
        if (StateMessageId == 1)
            StateMessageId = 0x4cf;
        FmtStr MissionMsg;
        MissionMsg.begin_mad_lib(StateMessageId);
        MissionMsg.end_mad_lib();
        pub::Player::DisplayMissionMessage(PlayerId, MissionMsg, MISSION_MSG_SUCCESS, true);
        // FUN_06f49090.
        PlayerState->CountRandomMissionCompleted(7);
        DestroyMissionObjects();
    }
    FMissionObject::OnMissionSucceed();
}

// LAB_06eb9870: the default mad lib 1 becomes 0x412.
void FMissionScript::OnMissionFail()
{
    if (StateMessageId == 1)
        StateMessageId = 0x412;
    FmtStr MissionMsg;
    MissionMsg.begin_mad_lib(StateMessageId);
    MissionMsg.end_mad_lib();
    pub::Player::DisplayMissionMessage(PlayerId, MissionMsg, MISSION_MSG_FAILURE, true);
    std::map<unsigned int, FContentNpcShip*>::iterator It = NpcObjects.begin();
    for (; It != NpcObjects.end(); ++It)
        It->second->SetCommMuted(true);
    DestroyMissionObjects();
    FMissionObject::OnMissionFail();
}

// LAB_06eb8070.
void FMissionScript::EndMission()
{
    std::map<unsigned int, FMissionNag*>::iterator NagIt = Nags.begin();
    while (NagIt != Nags.end())
    {
        if (NagIt->second != nullptr)
            delete NagIt->second;
        NagIt = Nags.erase(NagIt);
    }
    // The timer actions stay owned by their triggers.
    Timers.clear();
    // Retail quirk: only a player without a ship gets lock_maneuvers(false), with that (unset)
    // ship id. The player state is not null-tested.
    if ((PlayerState->GetShipId() & GIdValueMask) == 0)
        pub::AI::lock_maneuvers(PlayerState->GetShipId(), false);
    std::list<FTriggerMap::iterator>::iterator ActiveIt = ActiveTriggers.begin();
    for (; ActiveIt != ActiveTriggers.end(); ++ActiveIt)
        (*ActiveIt)->second.Condition->OnMissionEnd();
    ReleaseRtcScripts();
    FriendlyFireOffLabels.clear();
    RandEncs.clear();
    // The global dialog map DAT_06fcf260 (TU28).
    GMissionDialogs.erase(GMissionDialogs.begin(), GMissionDialogs.end());
    // The SpacePop clamps back to their defaults.
    SpacePopSetAttackClamp(0);
    GMissionScanClamp = 0;
    SpacePopSetEncounterEnabled(1);
    SpacePopSetPopulationEnabled(1);
    SpacePopClearExclusionSpheres();
    SpacePopSetHostileClamp(1);
    if (FindContentPlayerState(PlayerId) != nullptr)
    {
        PlayerState->SetDockAccess(1, nullptr);
        PlayerState->SetTradelaneAccess(1, nullptr);
        pub::Audio::CancelMusic(PlayerId);
    }
    Invulnerables.clear();
    DecloakedNames.clear();
    MarkObjNames.clear();
    FMissionObject::EndMission();
}

// Inlined by FUN_06eb84c0 and the Act_* comm senders (insert FUN_06ed1d30).
void FMissionScript::AddDialogCostume(Costume const* SpeakerCostume)
{
    std::vector<Costume>::iterator It = DialogCostumes.begin();
    for (; It != DialogCostumes.end(); ++It)
    {
        if (*It == *SpeakerCostume) // Costume::operator==, FUN_06eb8ef0
            return;
    }
    DialogCostumes.push_back(*SpeakerCostume);
}

// FUN_06eb84c0.
void FMissionScript::LoadMission(INI_Reader* Reader)
{
    FMissionObject::LoadMission(Reader);
    Reader->reset();
    while (Reader->read_header())
    {
        if (Reader->is_header("Trigger"))
        {
            LoadTriggerSection(Reader);
            continue;
        }
        // DAT_06fb43a0 = "NPC".
        if (Reader->is_header("NPC"))
        {
            // LoadMsnNpc = FUN_06f31470; a duplicate id keeps the first block.
            FMsnNpc Npc;
            ID_String Nickname;
            Nickname.clear();
            LoadMsnNpc(&Nickname, &Npc, Reader);
            MissionNpcs.insert(std::map<unsigned int, FMsnNpc>::value_type(Nickname.Id, Npc));
            continue;
        }
        if (!Reader->is_header("Dialog"))
            continue;

        // Every "line" spoken to "Player": the speaker's costume goes into +0x31c.
        while (Reader->read_value())
        {
            if (!Reader->is_value("line"))
                continue;
            if (_stricmp(Reader->get_value_string(1), "Player") != 0)
                continue;
            TString<0x30> Speaker;
            Speaker.Length = 0;
            Speaker.Data[0] = '\0';
            Speaker = Reader->get_value_string(0);
            Costume* const SpeakerCostume = GetMissionCostume(&Speaker);
            if (SpeakerCostume == nullptr)
                continue;
            AddDialogCostume(SpeakerCostume);
        }
    }
}

// FUN_06eb8f50. Every case ends either with the TU38 body (FUN_06f0f150) and the tail, or the
// tail only; the tail runs the NN path and the nag objects unless the mission is ending, then
// always vtbl +0x78.
void FMissionScript::HandleMissionMessage(EMessage MessageId, void const* MessageParms)
{
    // Nothing at all without a live player state.
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;

    unsigned int const* Parms = static_cast<unsigned int const*>(MessageParms);
    EMessage EventId = MessageId;
    bool bBaseBody = true;

    switch (MessageId)
    {
    case EMessage::BaseEnter:
        // Parms {base, player}.
        if (Parms[1] == PlayerId)
            OnBaseEnter(Parms);
        break;
    case EMessage::BaseExit:
        if (Parms[1] == PlayerId)
            OnBaseExit(Parms);
        break;
    case EMessage::CargoScanned:
    case EMessage::CommComplete:
    case EMessage::ObjectDamaged:
    case EMessage::TradelaneEnter:
    case EMessage::TradelaneExit:
    case EMessage::RtcDone:
    case EMessage::JumpInComplete:
    case EMessage::LaunchComplete:
    case EMessage::PlayerManeuver:
    case EMessage::JumpgateAct:
    case EMessage::NpcSystemExit:
    case EMessage::EncounterLaunched:
    case EMessage::RumorHeard:
    case EMessage::TetherObject:
        DispatchTriggerEvent(&EventId, MessageParms);
        break;
    case EMessage::PlayerSave:
    {
        // Parms {player, FILE*}; the TU38 body runs inside, only when the gate passes.
        bBaseBody = false;
        if (Parms[0] != PlayerId || !pub::SinglePlayer())
            break;
        FILE* File = reinterpret_cast<FILE*>(static_cast<uintptr_t>(Parms[1]));
        fprintf(File, "\n[MissionState]\n");
        SaveMissionState(File);
        FMissionObject::HandleMissionMessage(MessageId, MessageParms);
        fprintf(File, "\n[BStoryMissionDone]\n");
        bTriggerSavePending = 0;
        break;
    }
    case EMessage::PlayerLoad:
    {
        // Parms {player, INI_Reader*}.
        PlayerState = FindContentPlayerState(PlayerId);
        if (PlayerState == nullptr)
        {
            bPendingDestroy = 1;
            SetPlayer(0);
        }
        else if (Parms[0] == PlayerId)
        {
            INI_Reader* Reader = reinterpret_cast<INI_Reader*>(static_cast<uintptr_t>(Parms[1]));
            INI_Reader::State SavedState;
            Reader->get_state(SavedState);
            Reader->reset();
            LoadMissionState(Reader);
            Reader->set_state(SavedState);
        }
        // The TU38 body only while a player state is live.
        if (FindContentPlayerState(PlayerId) == nullptr)
            bBaseBody = false;
        break;
    }
    case EMessage::PlayerLaunch:
        if (Parms[0] == PlayerId)
            DispatchTriggerEvent(&EventId, MessageParms);
        break;
    case EMessage::PlayerLogin:
    case EMessage::SystemLoaded:
    case EMessage::LoadGame:
        bBaseBody = false;
        break;
    case EMessage::PlayerLogout:
    {
        // Drop every trigger.
        ActiveTriggers.clear();
        FTriggerMap::iterator TriggerIt = TriggerMap.begin();
        for (; TriggerIt != TriggerMap.end(); ++TriggerIt)
            TriggerIt->second.DeleteContents();
        TriggerMap.clear();
        PlayerState = FindContentPlayerState(PlayerId);
        if (PlayerState == nullptr)
        {
            bPendingDestroy = 1;
            SetPlayer(0);
            bBaseBody = false;
        }
        break;
    }
    case EMessage::LootAcquired:
    {
        // The trigger event, then word parms+8 into the loot record's +0xf8.
        DispatchTriggerEvent(&EventId, MessageParms);
        FMissionLoot* Found = FindLootById(Parms);
        if (Found != nullptr)
            Found->SubObjId = static_cast<FLootAcquiredEvent const*>(MessageParms)->SubObjId;
        break;
    }
    case EMessage::GFObjSelect:
        if (PlayerId == Parms[1])
            OnCharacterSelect(Parms);
        break;
    case EMessage::Heartbeat:
    {
        // Event 0x20 gets a local copy of dt, event 0x1011 no parms, the TU38 body the real
        // parms.
        float DeltaSeconds = *static_cast<float const*>(MessageParms);
        ElapsedTime = DeltaSeconds + ElapsedTime;
        TickTimers(DeltaSeconds);
        DispatchTriggerEvent(&EventId, &DeltaSeconds);
        EventId = EMessage::TetherObject;
        DispatchTriggerEvent(&EventId, nullptr);
        break;
    }
    case EMessage::AbortMission:
        // The TU38 body first.
        FMissionObject::HandleMissionMessage(MessageId, MessageParms);
        OnAbortMission(Parms);
        bBaseBody = false;
        break;
    case EMessage::LocationEnter:
        // The location enter on the player's base is remembered.
        if (PlayerId == Parms[1] && FindContentPlayerState(PlayerId) != nullptr)
        {
            if (PlayerState->GetBaseRecord() != nullptr)
                CurrentLocationId = Parms[0];
            DispatchTriggerEvent(&EventId, MessageParms);
        }
        break;
    case EMessage::LocationExit:
        if (PlayerId == Parms[1])
            OnLocationExit(Parms);
        break;
    case EMessage::MissionResponse:
        if (Parms[3] == PlayerId && Parms[0] == ControllerId)
            OnOfferResponse(MessageParms);
        break;
    case EMessage::PopUpDialog:
        // The answer to the reputation pop-up.
        if (Parms[0] == PlayerId)
        {
            if (bRepPopUpShown != 0)
            {
                if ((Parms[1] & 1) != 0)
                {
                    int PlayerRep = -1;
                    pub::Player::GetRep(PlayerId, PlayerRep);
                    std::vector<unsigned int>::iterator GroupIt = RepPopUpGroups.begin();
                    for (; GroupIt != RepPopUpGroups.end(); ++GroupIt)
                        pub::Reputation::SetReputation(PlayerRep, *GroupIt, 0.6f);
                    bRepPopUpArmed = 0;
                }
                bRepPopUpShown = 0;
            }
            DispatchTriggerEvent(&EventId, MessageParms);
        }
        break;
    case EMessage::SpaceEnter:
        // The TU38 body first.
        FMissionObject::HandleMissionMessage(MessageId, MessageParms);
        if (Parms[1] == PlayerId)
            OnSpaceEnter(Parms);
        bBaseBody = false;
        break;
    case EMessage::SpaceExit:
        // The TU38 body first.
        FMissionObject::HandleMissionMessage(MessageId, MessageParms);
        if (Parms[1] == PlayerId)
            DispatchTriggerEvent(&EventId, MessageParms);
        bBaseBody = false;
        break;
    case EMessage::ObjectDestroyed:
        OnObjectDestroyed(Parms);
        break;
    case EMessage::SystemExit:
        if (Parms[2] == PlayerId)
            DispatchTriggerEvent(&EventId, MessageParms);
        break;
    case EMessage::SystemEnter:
        // The TU38 body first.
        FMissionObject::HandleMissionMessage(MessageId, MessageParms);
        OnSystemEnter(Parms);
        OnSystemEnterPopUp(Parms);
        bBaseBody = false;
        break;
    case EMessage::MissionStart:
        ActivateInitialMissionTriggers(this);
        bBaseBody = false;
        break;
    case EMessage::DamageReputation:
        if (FindContentPlayerState(PlayerId) != nullptr
            && PlayerState->GetLiveMission() == ControllerId)
            CheckFriendlyFire(Parms);
        bBaseBody = false;
        break;
    case EMessage::Msg0x1006:
        if (Parms[1] == 0)
        {
            if (Parms[2] == 1)
                StateMessageId = 0x5462;
            else
                StateMessageId = Parms[2];
        }
        break;
    case EMessage::PlayerMissionChanged:
        OnPlayerMissionChanged(Parms);
        break;
    case EMessage::NpcShipCreated:
    {
        FContentNpcShip* const* NpcParms = static_cast<FContentNpcShip* const*>(MessageParms);
        FContentNpcShip* Npc = *NpcParms;
        if (Npc->ControllerId == ControllerId)
        {
            RegisterNpc(NpcParms);
            SubmitNpcPersonality(Npc, 0);
            DispatchTriggerEvent(&EventId, MessageParms);
        }
        break;
    }
    default:
        // Every other message id, inside or outside the two jump tables.
        break;
    }

    if (bBaseBody)
        FMissionObject::HandleMissionMessage(MessageId, MessageParms);

    // The tail.
    if (bPendingDestroy == 0)
    {
        if (bNeedsObjectiveRenumber == 0 && NnPath.bActive != 0
            && (PlayerState->GetLiveMission() == ControllerId
                || (PlayerState->GetLiveMission() & GIdValueMask) == 0))
            NnPath.HandleMessage(MessageId, MessageParms);
        DispatchNagMessage(MessageId, MessageParms);
    }
    ApplyDesiredMissionState();
}

// FUN_06eb9dc0.
void FMissionScript::OnBaseEnter(unsigned int const* Parms)
{
    // Every RTC script not yet started is started.
    std::list<FMissionRtc>::iterator ScriptIt = RtcScripts.begin();
    for (; ScriptIt != RtcScripts.end(); ++ScriptIt)
    {
        if (ScriptIt->BarSceneHandle == -1)
            StartRtcScript(&*ScriptIt);
    }
    // Every ambient scene of this base is created.
    std::list<FMissionAmbientScene>::iterator SceneIt = AmbientScenes.begin();
    for (; SceneIt != AmbientScenes.end(); ++SceneIt)
    {
        if (SceneIt->BaseId != Parms[0])
            continue;
        AmbientScriptDescription Desc;
        Desc.RoomId = SceneIt->RoomId;
        Desc.ScriptId = 1;
        Desc.Flags = 0;
        Desc.Name = SceneIt->Path.Data;
        Desc.Filter = PlayerId;
        SceneIt->Handle = static_cast<int>(pub::GF::AmbientScriptCreate(Desc));
    }
    if (bNeedsObjectiveRenumber != 0)
    {
        RenumberAllObjectives();
        bNeedsObjectiveRenumber = 0;
    }
    else if (NnPath.bActive != 0)
    {
        NnPath.Rebuild();
        NnPath.SendObjectives(false, false);
    }
    EMessage EventId = EMessage::BaseEnter;
    DispatchTriggerEvent(&EventId, Parms);
}

// FUN_06eb9ef0.
void FMissionScript::OnBaseExit(unsigned int const* Parms)
{
    // The scripts are only walked with a base record (FUN_06eac0c0).
    FContentBase* Base = FindContentBaseInfo(Parms);
    if (Base != nullptr)
    {
        std::list<FMissionRtc>::iterator It = RtcScripts.begin();
        while (It != RtcScripts.end())
        {
            if (It->Encounter != nullptr)
            {
                // The handle is not tested for -1 here.
                Base->RemoveBarScene(It->BarSceneHandle, nullptr);
                It->BarSceneHandle = -1;
                delete It->Encounter;
                It->Encounter = nullptr;
            }
            if (It->bFinished != 0)
                It = RtcScripts.erase(It);
            else
                ++It;
        }
    }
    std::list<FMissionAmbientScene>::iterator SceneIt = AmbientScenes.begin();
    for (; SceneIt != AmbientScenes.end(); ++SceneIt)
    {
        if (SceneIt->Handle != -1)
        {
            unsigned long Handle = static_cast<unsigned long>(SceneIt->Handle);
            pub::GF::AmbientScriptDestroy(&Handle);
            SceneIt->Handle = -1;
        }
    }
    EMessage EventId = EMessage::BaseExit;
    DispatchTriggerEvent(&EventId, Parms);
}

// FUN_06eba350.
void FMissionScript::OnCharacterSelect(unsigned int const* Parms)
{
    std::list<FMissionRtc>::iterator It = RtcScripts.begin();
    for (; It != RtcScripts.end(); ++It)
    {
        if (It->bFinished != 0 || It->bRepeatable != 0)
            continue;
        if (FindContentPlayerState(PlayerId) == nullptr)
            continue;
        if (It->Encounter == nullptr)
            continue;
        // The selected character (*parms) of the script's role finishes the script.
        FContentBase* const Base = PlayerState->GetBaseRecord();
        if (Base == nullptr)
            continue;
        FBaseRoomState* const Room = Base->FindRoomState(&CurrentLocationId);
        if (Room == nullptr)
            continue;
        FBarCharacterList::iterator Character;
        if (!Room->FindCharacter(GetFirstRtcRole(It->Encounter), PlayerId, &Character))
            continue;
        if (Character->CharacterId != *Parms)
            continue;
        Character->SetBehavior(static_cast<unsigned long>(-1));
        It->bFinished = 1;
    }
    EMessage EventId = EMessage::GFObjSelect;
    DispatchTriggerEvent(&EventId, Parms);
}

// FUN_06eba530.
void FMissionScript::OnLocationExit(unsigned int const* Parms)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    FContentBase* Base = PlayerState->GetBaseRecord();
    if (Base != nullptr)
    {
        // The roles of the unfinished scripts of this location are kept.
        unsigned int const LocationId = *Parms;
        std::vector<FRtcScriptRole> KeepRoles;
        std::list<FMissionRtc>::iterator It = RtcScripts.begin();
        for (; It != RtcScripts.end(); ++It)
        {
            if (It->bFinished != 0 || It->Encounter == nullptr)
                continue;
            if (It->Encounter->LocationId != LocationId)
                continue;
            std::vector<FRtcScriptRole>::const_iterator Role = It->Encounter->Roles.begin();
            for (; Role != It->Encounter->Roles.end(); ++Role)
                KeepRoles.push_back(*Role);
        }
        // Every finished script with an object is released, sparing the kept roles.
        It = RtcScripts.begin();
        while (It != RtcScripts.end())
        {
            if (It->bFinished != 1 || It->Encounter == nullptr)
            {
                ++It;
                continue;
            }
            Base->RemoveBarScene(It->BarSceneHandle, &KeepRoles);
            It->BarSceneHandle = -1;
            delete It->Encounter;
            It->Encounter = nullptr;
            It = RtcScripts.erase(It);
        }
    }
    EMessage EventId = EMessage::LocationExit;
    DispatchTriggerEvent(&EventId, Parms);
}

// FUN_06eba030.
void FMissionScript::OnOfferResponse(void const* MessageParms)
{
    EMessage EventId = EMessage::MissionResponse;
    unsigned char const* ParmsBytes = static_cast<unsigned char const*>(MessageParms);
    if (ParmsBytes[8] == 0 || FindContentPlayerState(PlayerId) == nullptr)
    {
        DispatchTriggerEvent(&EventId, MessageParms);
        return;
    }
    // An empty FmtStr shown with type 10, broadcast.
    {
        FmtStr EmptyMsg;
        pub::Player::DisplayMissionMessage(PlayerId, EmptyMsg,
                                           MISSION_MSG_PRUNE_HISTORY, true);
    }
    DispatchTriggerEvent(&EventId, MessageParms);
    if (StoryEvents != nullptr)
        StoryEvents->OnMissionEvent(STORY_MISSION_EVENT_JOINED, static_cast<unsigned int>(CampaignSlotSucceed), ControllerId);
    // Another live mission of the player is destroyed and left.
    unsigned int const LiveMission = PlayerState->GetLiveMission();
    if ((LiveMission & GIdValueMask) != 0 && LiveMission != ControllerId)
    {
        pub::Controller::Destroy(LiveMission);
        PlayerState->EndLiveMission(LiveMission, VNPC_MISSION_ABORTED, true);
    }
    bRepPopUpShown = 0;
    bRepPopUpArmed = 0;
    RepPopUpSystemId = 0;
    RepPopUpGroups.clear();
    SpacePopSetAttackClamp(1);
    SpacePopSetEncounterEnabled(0);
    GMissionScanClamp = 1;
    // The group joins this mission (FUN_06f488b0).
    ID_String NoVnpc;
    NoVnpc.clear();
    PlayerState->JoinMissionAsGroup(ControllerId, 0, &NoVnpc, true, AcceptedObjectiveIds);
}

// FUN_06eba720.
void FMissionScript::OnSpaceEnter(unsigned int const* Parms)
{
    if (Parms[1] != PlayerId)
        return;
    // A CostumeHint per dialog costume (Costume's inline operator= copies `count` accessories).
    std::vector<Costume>::iterator It = DialogCostumes.begin();
    for (; It != DialogCostumes.end(); ++It)
    {
        CostumeHint Hint;
        Hint.Cost.PartIds[0] = It->PartIds[0];
        Hint.Cost.PartIds[1] = It->PartIds[1];
        Hint.Cost.PartIds[2] = It->PartIds[2];
        Hint.Cost.PartIds[3] = It->PartIds[3];
        Hint.Cost.AccessoryCount = It->AccessoryCount;
        for (int Index = 0; Index < static_cast<int>(It->AccessoryCount); ++Index)
            Hint.Cost.AccessoryIds[Index] = It->AccessoryIds[Index];
        pub::Player::LoadHint(PlayerId, &Hint);
    }
    EMessage EventId = EMessage::SpaceEnter;
    DispatchTriggerEvent(&EventId, Parms);
    if (NnPath.bActive == 0)
    {
        RenumberAllObjectives();
    }
    else
    {
        NnPath.Rebuild();
        HideAllObjectives();
        NnPath.SendObjectives(false, false);
    }
    bNeedsObjectiveRenumber = 0;
}

// FUN_06ebaae0.
void FMissionScript::OnObjectDestroyed(unsigned int const* Parms)
{
    EMessage EventId = EMessage::ObjectDestroyed;
    DispatchTriggerEvent(&EventId, Parms);
    // The first friendly-fire label naming the destroyed object is dropped.
    std::list<TString<0x30>>::iterator It = FriendlyFireOffLabels.begin();
    for (; It != FriendlyFireOffLabels.end(); ++It)
    {
        if (GetLabelObjectId(&*It) == Parms[0])
        {
            FriendlyFireOffLabels.erase(It);
            break;
        }
    }
    if (!IsPlayerDead(PlayerId) && FindContentPlayerState(PlayerId) != nullptr
        && PlayerState->ShipObject == nullptr)
        bPendingDestroy = 1;
}

// FUN_06eba860.
void FMissionScript::OnSystemEnter(unsigned int const* Parms)
{
    if (Parms[2] != PlayerId || FindContentPlayerState(PlayerId) == nullptr)
        return;
    EMessage EventId = EMessage::SystemEnter;
    DispatchTriggerEvent(&EventId, Parms);
    FContentPlayerShip* const Ship = PlayerState->ShipObject;
    if (Ship == nullptr)
        return;
    // Event 0x100f with {ship id, parms[0], parms[3] or 0}.
    unsigned int NpcEnterParms[3];
    NpcEnterParms[0] = Ship->SpaceObjId;
    NpcEnterParms[1] = Parms[0];
    NpcEnterParms[2] = (Parms[1] & GIdValueMask) != 0 ? Parms[3] : 0;
    EventId = EMessage::NpcSystemEnter;
    DispatchTriggerEvent(&EventId, NpcEnterParms);
}

// FUN_06eba1f0.
void FMissionScript::OnSystemEnterPopUp(unsigned int const* Parms)
{
    if (bRepPopUpArmed == 0 || Parms[0] != RepPopUpSystemId || Parms[2] != PlayerId)
        return;
    FmtStr Title;
    Title.begin_mad_lib(0x54c4);
    Title.end_mad_lib();
    FmtStr Text;
    Text.begin_mad_lib(0x54c5);
    Text.end_mad_lib();
    pub::Player::PopUpDialog(PlayerId, Title, Text, 5);
    bRepPopUpShown = 1;
}

// FUN_06eb9cd0.
void FMissionScript::OnAbortMission(unsigned int const* Parms)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    if (Parms[0] != PlayerId)
        return;
    if (PlayerState->GetLiveMission() != ControllerId)
        return;
    FmtStr MissionMsg;
    MissionMsg.begin_mad_lib(StateMessageId);
    MissionMsg.end_mad_lib();
    pub::Player::DisplayMissionMessage(PlayerId, MissionMsg, MISSION_MSG_FAILURE, true);
    bPendingDestroy = 1;
}

// FUN_06eb99a0: parms {kind, ship, victim}.
void FMissionScript::CheckFriendlyFire(unsigned int const* Parms)
{
    if (FindContentPlayerState(PlayerId) == nullptr)
        return;
    FContentPlayerShip* const Ship = PlayerState->ShipObject;
    if (Ship == nullptr)
        return;
    if (Parms[1] != Ship->SpaceObjId || Parms[0] != 1)
        return;
    // A victim named by a friendly-fire label is excused.
    std::list<TString<0x30>>::iterator It = FriendlyFireOffLabels.begin();
    for (; It != FriendlyFireOffLabels.end(); ++It)
    {
        if (GetLabelObjectId(&*It) == Parms[2])
            return;
    }
    int VictimRep = 0;
    int ShipRep;
    float Attitude = 0.0f;
    pub::SpaceObj::GetRep(Parms[2], VictimRep);
    pub::SpaceObj::GetRep(Parms[1], ShipRep);
    pub::Reputation::GetAttitude(VictimRep, ShipRep, Attitude);
    // DAT_06fb4668 (0.2f).
    if (Attitude > 0.2f)
    {
        SetDesiredMissionState(G_MissionStateFail, 0x5460);
        return;
    }
    // DAT_06fb4660 (-0.2f); unordered does not fail.
    if (Attitude >= -0.2f)
        SetDesiredMissionState(G_MissionStateFail, 0x5463);
}

// FUN_06eba8f0.
void FMissionScript::OnPlayerMissionChanged(unsigned int const* Parms)
{
    if (FindContentPlayerState(PlayerId) != nullptr && PlayerState->ShipObject != nullptr
        && Parms[0] != ControllerId)
    {
        FMissionRtc Restart;
        // The first unfinished script with a mission text (+0x2c) is restarted, while the
        // player has a base.
        std::list<FMissionRtc>::iterator It = RtcScripts.begin();
        for (; It != RtcScripts.end(); ++It)
        {
            if (It->Encounter == nullptr || It->Encounter->MissionTextId == 0 ||
                It->bFinished != 0)
            {
                continue;
            }
            FContentBase* const Base = PlayerState->GetBaseRecord();
            if (Base == nullptr)
                continue;
            size_t PathLength = strlen(It->Path);
            if (PathLength >= 0x80)
                PathLength = 0x7f;
            memcpy(Restart.Path, It->Path, PathLength);
            Restart.Path[PathLength] = '\0';
            Restart.PathLength = static_cast<unsigned int>(PathLength);
            Restart.bRepeatable = It->bRepeatable;
            Base->RemoveBarScene(It->BarSceneHandle, nullptr);
            It->BarSceneHandle = -1;
            delete It->Encounter;
            It->Encounter = nullptr;
            RtcScripts.erase(It);
            break;
        }
        if (Restart.PathLength != 0)
        {
            StartRtcScript(&Restart);
            RtcScripts.push_back(Restart);
        }
    }
    // With *parms == 0 the player's first objective is resent with its type or-ed with 0x30.
    // The first objective is not null-tested.
    if (Parms[0] != 0)
        return;
    FMissionPlayer* Player = FindPlayer(PlayerId);
    if (Player == nullptr)
        return;
    FMissionObjective* First = Player->Objectives.GetFront();
    unsigned int const SavedType = First->Objective.Type;
    First->Objective.Type = SavedType | 0x30;
    Player->SetForceResendObjectives(true);
    Player->SendObjectives();
    Player->SetForceResendObjectives(false);
    First->Objective.Type = SavedType;
}

// FUN_06ebabb0. The player state is not null-tested.
void FMissionScript::SaveMissionState(FILE* File)
{
    fprintf(File, "mission_accepted = %d\n", PlayerState->GetLiveMission() == ControllerId ? 1 : 0);
    // DAT_06fd1c78.
    fprintf(File, GSpacePopAttackClamp == 1 ? "att_clamp = true\n" : "att_clamp = false\n");
    fprintf(File, SpacePopGetEncounterEnabled() == 1 ? "tradelane_attacks = true\n"
                                                      : "tradelane_attacks = false\n");
    fprintf(File, GMissionScanClamp == 1 ? "scan_clamp = true\n" : "scan_clamp = false\n");
    fprintf(File, ContentIsGcsClampEnabled() ? "gcs_clamp = true\n" : "gcs_clamp = false\n");
    fprintf(File, SpacePopGetHostileClamp() == 1 ? "hostile_clamp = true\n"
                                                  : "hostile_clamp = false \n");
    fprintf(File, SpacePopGetPopulationEnabled() != 0 ? "random_pop = true\n"
                                                       : "random_pop = false\n");
    fprintf(File, "msn_offer = %d\n", MissionOffer.MadLibId);
    fprintf(File, "msn_title = %d\n", MissionTitle.MadLibId);
    if (NnPath.bActive != 0)
        NnPath.Save(File);
    if (bTriggerSavePending != 0)
    {
        fprintf(File, "\n[TriggerSave]\n");
        fprintf(File, "trigger = %u\n", SaveTriggerId);
        return;
    }
    std::list<FMissionRtc>::iterator ScriptIt = RtcScripts.begin();
    for (; ScriptIt != RtcScripts.end(); ++ScriptIt)
    {
        if (ScriptIt->bFinished != 0)
            continue;
        fprintf(File, "rtc = %s, %s\n", ScriptIt->Path, ScriptIt->bRepeatable != 0 ? "true" : "false");
    }
    std::list<FMissionAmbientScene>::iterator SceneIt = AmbientScenes.begin();
    for (; SceneIt != AmbientScenes.end(); ++SceneIt)
        fprintf(File, "ambi_scene = %s, %u, %u\n", SceneIt->Path.Data, SceneIt->RoomId,
                SceneIt->BaseId);
    std::list<TString<0x30>>::iterator LabelIt = FriendlyFireOffLabels.begin();
    for (; LabelIt != FriendlyFireOffLabels.end(); ++LabelIt)
        fprintf(File, "friendly_fire_excused = %s\n", LabelIt->Data);
    fprintf(File, "story_cue = %d\n", StoryCue);
    // A [TriggerSave] per active trigger with its condition's Save (slot 5, +0x14).
    std::list<FTriggerMap::iterator>::iterator ActiveIt = ActiveTriggers.begin();
    for (; ActiveIt != ActiveTriggers.end(); ++ActiveIt)
    {
        fprintf(File, "\n[TriggerSave]\n");
        fprintf(File, "trigger = %u\n", (*ActiveIt)->first);
        (*ActiveIt)->second.Condition->Save(File);
    }
}

// FUN_06ebae10. Four local flags: [MissionState] seen (the [NNObjective]
// gate), the RTC list released, the active triggers dropped, the objectives cleared. The
// header loop ends at [BStoryMissionDone]; any other header is skipped.
void FMissionScript::LoadMissionState(INI_Reader* Reader)
{
    bool bMissionStateSeen = false;
    bool bRtcReleased = false;
    bool bTriggersDropped = false;
    bool bObjectivesCleared = false;
    while (Reader->read_header())
    {
        if (Reader->is_header("MissionState"))
        {
            bNeedsObjectiveRenumber = 1;
            bMissionStateSeen = true;
            while (Reader->read_value())
            {
                if (Reader->is_value("rtc"))
                {
                    // The first "rtc" releases the started scripts.
                    if (!bRtcReleased)
                        ReleaseRtcScripts();
                    FMissionRtc Script;
                    TString<0x80> Path;
                    Path = Reader->get_value_string(0);
                    Script.PathLength = Path.Length;
                    memcpy(Script.Path, Path.Data, Path.Length + 1);
                    Script.bRepeatable = Reader->get_value_bool(1) ? 1 : 0;
                    RtcScripts.push_back(Script);
                    bRtcReleased = true;
                    continue;
                }
                if (Reader->is_value("ambi_scene"))
                {
                    // Retail also fills an AmbientScriptDescription on the stack that is never
                    // passed anywhere.
                    FMissionAmbientScene Scene;
                    Scene.Path.Length = 0;
                    Scene.Path.Data[0] = '\0';
                    Scene.Handle = -1;
                    Scene.RoomId = 0;
                    Scene.BaseId = 0;
                    Scene.Path = Reader->get_value_string(0);
                    Scene.RoomId = static_cast<unsigned int>(Reader->get_value_int(1));
                    Scene.BaseId = static_cast<unsigned int>(Reader->get_value_int(2));
                    AmbientScenes.push_back(Scene);
                    continue;
                }
                if (Reader->is_value("friendly_fire_excused"))
                {
                    TString<0x30> Label;
                    Label = Reader->get_value_string(0);
                    FriendlyFireOffLabels.push_back(Label);
                    continue;
                }
                if (Reader->is_value("mission_accepted"))
                {
                    if (Reader->get_value_bool(0) != true)
                        continue;
                    if (FindContentPlayerState(PlayerId) == nullptr)
                        continue;
                    ID_String ObjectiveIds;
                    ObjectiveIds.clear();
                    PlayerState->JoinMissionAsGroup(ControllerId, 0, &ObjectiveIds, false, 0);
                    if (StoryEvents != nullptr)
                        StoryEvents->OnMissionEvent(STORY_MISSION_EVENT_JOINED, static_cast<unsigned int>(CampaignSlotSucceed),
                                                    ControllerId);
                    continue;
                }
                if (Reader->is_value("att_clamp"))
                {
                    // DAT_06fd1c78 in place.
                    GSpacePopAttackClamp = Reader->get_value_bool(0) ? 1 : 0;
                    continue;
                }
                if (Reader->is_value("tradelane_attacks"))
                {
                    SpacePopSetEncounterEnabled(Reader->get_value_bool(0) ? 1 : 0); // FUN_06f5a820
                    continue;
                }
                if (Reader->is_value("scan_clamp"))
                {
                    GMissionScanClamp = Reader->get_value_bool(0) ? 1 : 0;
                    continue;
                }
                if (Reader->is_value("gcs_clamp"))
                {
                    SpacePopSetGcsClampEnabled(Reader->get_value_bool(0) ? 1 : 0); // FUN_06f5cf60
                    continue;
                }
                if (Reader->is_value("random_pop"))
                {
                    SpacePopSetPopulationEnabled(Reader->get_value_bool(0) ? 1 : 0); // FUN_06f5a800
                    continue;
                }
                if (Reader->is_value("hostile_clamp"))
                {
                    SpacePopSetHostileClamp(Reader->get_value_bool(0) ? 1 : 0); // FUN_06f5a7b0
                    continue;
                }
                // A non-zero id rebuilds the offer / title mad lib (i0 is read again).
                FmtStr* MadLib = nullptr;
                if (Reader->is_value("msn_offer"))
                {
                    if (Reader->get_value_int(0) != 0)
                        MadLib = &MissionOffer;
                }
                else if (Reader->is_value("msn_title"))
                {
                    if (Reader->get_value_int(0) != 0)
                        MadLib = &MissionTitle;
                }
                else if (Reader->is_value("nnpath"))
                {
                    HideAllObjectives();
                    NnPath.Load(Reader, this);
                    continue;
                }
                if (MadLib == nullptr)
                    continue;

                MadLib->destroy();
                MadLib->Head = nullptr;
                MadLib->MadLibId = 0;
                for (int Index = 0; Index < FmtStr::KindCount; ++Index)
                    MadLib->ValueIndex[Index] = 0;
                MadLib->begin_mad_lib(static_cast<unsigned int>(Reader->get_value_int(0)));
                MadLib->end_mad_lib();
            }
            continue;
        }
        if (Reader->is_header("TriggerSave"))
        {
            while (Reader->read_value())
            {
                if (!Reader->is_value("trigger"))
                    continue;
                // The first "trigger" drops the active list (the triggers' states untouched),
                // +0x1e4 and +0x1d0.
                if (!bTriggersDropped)
                {
                    ActiveTriggers.clear();
                    InitiallyActive.clear();
                    PendingActivations.clear();
                }
                // {i0, i0} unique into +0x1e4 (activated by message 0x1000). The key is the
                // second read, the value the first.
                unsigned int const FirstRead = static_cast<unsigned int>(Reader->get_value_int(0));
                unsigned int const SecondRead = static_cast<unsigned int>(Reader->get_value_int(0));
                InitiallyActive.insert(
                    std::map<unsigned int, unsigned int>::value_type(SecondRead, FirstRead));
                // Every trigger with id i0 (read a third time) loads its condition (slot 6).
                unsigned int const TriggerId = static_cast<unsigned int>(Reader->get_value_int(0));
                FTriggerMap::iterator It = TriggerMap.begin();
                for (; It != TriggerMap.end(); ++It)
                {
                    if (It->first == TriggerId)
                        It->second.Condition->Load(Reader);
                }
                bTriggersDropped = true;
            }
            continue;
        }
        if (Reader->is_header("NNObjective") && bMissionStateSeen)
        {
            // The first one clears every objective. GetFirstPlayer() is not null-tested.
            if (!bObjectivesCleared)
            {
                ClearObjectives();
                bObjectivesCleared = true;
            }
            LoadNNObjective(Reader, this, &GetFirstPlayer()->Objectives);
            continue;
        }
        if (Reader->is_header("BStoryMissionDone"))
            return;
    }
}

// FUN_06ebb620 (stdcall, no this); case 0x1010 passes (*parms, 0).
void FMissionScript::SubmitNpcPersonality(FContentNpcShip* Npc, int Mode)
{
    if (Npc == nullptr)
        return;
    unsigned int const Flid = Npc->SpaceObjId;
    pub::AI::Personality LocalPersonality;
    if (!pub::AI::get_personality(Flid, LocalPersonality))
        return;
    if (Mode == 0)
    {
        LocalPersonality.JobParams.RangeThreshold = 0;
        LocalPersonality.JobParams.SceneToughnessThreshold = 4;
        LocalPersonality.JobParams.LootPreference = 0;
        LocalPersonality.JobParams.FleeSceneThreatStyle = 4;
        LocalPersonality.JobParams.FleeWhenHullDamagedPercent = 0;
        LocalPersonality.JobParams.bFleeWhenLeaderFleesStyle = 0;
        LocalPersonality.JobParams.bFleeNoWeaponsStyle = 0;
    }
    else if (Mode == 1)
    {
        // The job fields come from the ship's own pilot or, when that is empty, from the
        // pilot of its npcships entry (FUN_06f324d0).
        pub::AI::Personality PilotPersonality;
        ID_String PilotId = Npc->PilotId;
        bool bLoaded = false;
        if (PilotId.IsEmpty())
        {
            FNpcShipArch* const Arch = FindNpcShipArchByShip(Npc->ShipArchId, &GNpcShipArchs);
            if (Arch != nullptr)
                bLoaded = GetPilotPersonality(&Arch->Pilot.Id, &PilotPersonality);
        }
        else
        {
            bLoaded = GetPilotPersonality(&PilotId.Id, &PilotPersonality);
        }
        if (bLoaded)
        {
            LocalPersonality.JobParams.FleeSceneThreatStyle = PilotPersonality.JobParams.FleeSceneThreatStyle;
            LocalPersonality.JobParams.FleeWhenHullDamagedPercent = PilotPersonality.JobParams.FleeWhenHullDamagedPercent;
            LocalPersonality.JobParams.bFleeWhenLeaderFleesStyle = PilotPersonality.JobParams.bFleeWhenLeaderFleesStyle;
            LocalPersonality.JobParams.bFleeNoWeaponsStyle = PilotPersonality.JobParams.bFleeNoWeaponsStyle;
            LocalPersonality.JobParams.SceneToughnessThreshold = PilotPersonality.JobParams.SceneToughnessThreshold;
            LocalPersonality.JobParams.RangeThreshold = PilotPersonality.JobParams.RangeThreshold;
            LocalPersonality.JobParams.LootPreference = PilotPersonality.JobParams.LootPreference;
        }
    }
    pub::AI::SetPersonalityParams Params;
    Params.bSetPersonality = 1;
    Params.Value = LocalPersonality;
    Params.StateGraphId = pub::AI::get_state_graph_id(Flid);
    // npc +0x174 (ContentCallback) and +0x118 (DirectiveCallback).
    Params.ContentCallbackObject = static_cast<pub::AI::ContentCallback*>(Npc);
    Params.DirectiveCallbackObject = static_cast<pub::AI::DirectiveCallback*>(Npc);
    pub::AI::SubmitState(Flid, &Params);
}
