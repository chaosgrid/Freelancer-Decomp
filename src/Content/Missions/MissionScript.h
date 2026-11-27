// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "content.h"
#include "Missions/MissionObject.h"
#include "common_reputation.h"  // Costume
#include "x86math.h"

#include <cstdio>
#include <list>
#include <map>
#include <vector>

class Cnd;
class Act;
class FMissionNag;
class INI_Reader;
struct FMissionScript;

// ---------------------------------------------------------------------------------------
// TU21 (06eb7360-06ed3d90): the script mission (ctor FUN_06eb75a0, vtable 06fb4710, 31 slots)
// over the TU38 mission object, and its trigger engine. GenericMission and Mission_01a..13
// (StoryMissions.cpp) derive from it.
// ---------------------------------------------------------------------------------------

// DAT_06fd1c79 ("scan_clamp"): cleared by slot 22, set by the offer acceptance, loaded by
// FUN_06ebae10, saved by FUN_06ebabb0, read by TU37.
extern unsigned char GMissionScanClamp;

int const G_TriggerStateActive = 0;   // "ACTIVE" (FUN_06ebb850)
int const G_TriggerStateInactive = 1; // "INACTIVE"; the default
int const G_TriggerStateComplete = 2; // "COMPLETE"

// Mission states (+0x2a8 / +0x2ac, LAB_06eb8260 names).
int const G_MissionStateFail = 0;
int const G_MissionStateSucceed = 1;
int const G_MissionStateNoState = 2;

// One [Trigger] section: the value of the mission+0x1b0 map (node +0x10), built on the stack
// in FUN_06eb84c0, filled by FUN_06ebb850 and copied into the map
// (FUN_06ed0030). The copy shares Condition/Actions pointers; FUN_06ebb7f0 deletes them.
struct FTrigger
{
    int State;                 // +0x00, default INACTIVE
    Cnd* Condition;            // +0x04
    std::list<Act*> Actions;   // +0x08
    FMissionScript* Mission;   // +0x14
    std::vector<pub::Controller::EMessage> EventIds; // +0x18: message ids the condition listens to
    unsigned char bRepeatable; // +0x28 "repeatable"
    unsigned int Id;           // +0x2c CreateID(nickname)

    // FUN_06ebb850 (thiscall on the trigger): reads the rest of the [Trigger] section
    // into Trigger (Cnd_* / Act_* objects, InitState, repeatable).
    void Load(INI_Reader* Reader);

    // FUN_06ebb830: State = ACTIVE, then Condition->OnActivate() when set.
    void Rearm();

    // FUN_06ebb7f0: deletes Condition and every action (scalar deleting dtor, flag 1).
    void DeleteContents();
};

typedef std::map<unsigned int, FTrigger> FTriggerMap;

// mission+0x1f8 list node value (+0x08 remaining seconds, +0x0c action), pushed by delayed
// actions (e.g. Act_StartDialog) and run by FUN_06eb9c60.
struct FMissionTimer
{
    float Remaining;
    Act* Action;
};

// The 0x54-byte object FUN_06eb8a20 news for a started RTC script (TU23, RtcScripts.h).
struct FCharacterEncounter;

// One RTC script of the mission's +0x2f0 list (0x90 bytes at node+8, node 0x98, FUN_06ed0f20;
// the +0x310 list shares the node allocator): +0 path length, +4 char[0x80] path, +0x84 bar
// scene handle (-1), +0x88 the started script, +0x8c/+0x8d flags. Built on the stack by
// Mission_01b and copied in (0x24 dwords). The offset comments below are node offsets
// (value offset + 8).
struct FMissionRtc
{
    unsigned int PathLength;
    char Path[0x80];
    int BarSceneHandle;                // +0x8c bar scene of the RTC location slot (-1 none)
    FCharacterEncounter* Encounter;    // +0x90 the started script
    unsigned char bFinished;           // +0x94
    unsigned char bRepeatable;         // +0x95 "rtc = %s, true/false" (FUN_06ebabb0)

    FMissionRtc()
        : PathLength(0)
        , BarSceneHandle(-1)
        , Encounter(nullptr)
        , bFinished(0)
        , bRepeatable(0)
    {
        Path[0] = '\0';
    }
};

// The value of the mission's +0x310 list (0x90 bytes; its node allocator is ICF-folded with
// the +0x2f0 list's, the fields are not): +0 path length, +4 char[0x80] ambient script path,
// +0x84 pub::GF ambient script handle (-1 none), +0x88 room id, +0x8c base id.
// Created by Act_AddAmbient, started on base enter (FUN_06eb9dc0), stopped on base exit
// (FUN_06eb9ef0), saved as "ambi_scene = %s, %u, %u" (FUN_06ebabb0).
struct FMissionAmbientScene
{
    TString<0x80> Path;                 // +0x00
    int Handle;                         // +0x84
    unsigned int RoomId;                // +0x88
    unsigned int BaseId;                // +0x8c
};

// The TU21 script mission (see the file comment). Offsets are retail object offsets.
struct FMissionScript : FMissionObject
{
    // FUN_06eb75a0.
    FMissionScript(unsigned int const* InControllerId, FStoryMissionCreateParms const* Parms);
    // Slot 1, FUN_06eb79d0 -> FUN_06eb7d30 (member teardown) -> operator delete.
    ~FMissionScript() override;

    // Slot 19, LAB_06eba010: the TU38 objective timer only while the NN path is idle.
    void UpdateObjectiveTimer(pub::Controller::EMessage MessageId, void const* MessageParms) override;
    // Slot 21, FUN_06eb8f50: the TU21 message dispatcher (calls the TU38 body directly).
    void HandleMissionMessage(pub::Controller::EMessage MessageId, void const* MessageParms) override;
    // Slot 22, LAB_06eb8070: tear the script down, then the TU38 body FUN_06f0fc40.
    void EndMission() override;
    // Slot 23, FUN_06eb9760: SUCCEED display; Mission_13 overrides it.
    void OnMissionSucceed() override;
    // Slot 24, LAB_06eb9870: FAIL display.
    void OnMissionFail() override;
    // Slot 25, FUN_06eb84c0: FUN_06f172f0 (the TU38 body), then [Trigger] / [NPC] /
    // [Dialog].
    void LoadMission(INI_Reader* Reader) override;
    // Slot 26, LAB_06eb8260: "FAIL" -> 0, "SUCCEED" -> 1, "NO_STATE" -> 2, else -1 (stricmp).
    int MissionStateFromName(char const* Name) override;
    // Slot 28, FUN_06ebab90: message 0x100f with the argument as the parms.
    void OnNpcSystemEnter(void const* Parms) override;
    // Slot 29 (vtbl+0x74), LAB_06eb9730: unless (+0x2b0 == 1 && State == FAIL):
    // +0x2ac = State, +0x2e8 = MadLibId.
    virtual void SetDesiredMissionState(int State, unsigned int MadLibId);
    // Slot 30 (vtbl+0x78), FUN_06eb9aa0: apply +0x2ac when it differs from +0x2a8.
    virtual void ApplyDesiredMissionState();

    // The per-mission ctor stores (+0x2b4, +0x2b8, +0x2c0, +0x2c4).
    void SetChapter(int SlotCurrent, int SlotSucceed, unsigned int Value2c0, unsigned int Value2c4);
    // The tail of every mission ctor: FUN_06f24280 (open the DATA-relative ini; the result is
    // not tested), then LoadMission.
    void LoadMissionIni(char const* IniPath);

    // FUN_06eb8f50 case 1 -> FUN_06eb9dc0: base enter.
    void OnBaseEnter(unsigned int const* Parms);
    // FUN_06eb8f50 case 2 -> FUN_06eb9ef0: base exit.
    void OnBaseExit(unsigned int const* Parms);
    // FUN_06eb8f50 case 0x1b -> FUN_06eba350: character selected in a bar.
    void OnCharacterSelect(unsigned int const* Parms);
    // FUN_06eb8f50 case 0x2a -> FUN_06eba530: location exit.
    void OnLocationExit(unsigned int const* Parms);
    // FUN_06eb8f50 case 0x2c -> FUN_06eba030: the player's answer to the offer.
    void OnOfferResponse(void const* MessageParms);
    // FUN_06eb8f50 case 0x36 -> FUN_06eba720: space enter (dialog costume hints).
    void OnSpaceEnter(unsigned int const* Parms);
    // FUN_06eb8f50 case 0x39 -> FUN_06ebaae0: object destroyed.
    void OnObjectDestroyed(unsigned int const* Parms);
    // FUN_06eb8f50 case 0x3b -> FUN_06eba860: system enter.
    void OnSystemEnter(unsigned int const* Parms);
    // FUN_06eb8f50 case 0x3b -> FUN_06eba1f0: the reputation offer pop-up.
    void OnSystemEnterPopUp(unsigned int const* Parms);
    // FUN_06eb8f50 case 0x24 -> FUN_06eb9cd0: the player aborts the live mission (state
    // message shown, bPendingDestroy set).
    void OnAbortMission(unsigned int const* Parms);
    // FUN_06eb8f50 case 0x1005 -> FUN_06eb99a0: the player's ship provoked a victim; the mission
    // fails unless the victim was hostile or carries a friendly-fire-off label.
    void CheckFriendlyFire(unsigned int const* Parms);
    // FUN_06eb8f50 case 0x100b -> FUN_06eba8f0: restart an unfinished RTC script, resend the
    // first objective.
    void OnPlayerMissionChanged(unsigned int const* Parms);
    // FUN_06ebabb0: the [MissionState] body of the save (message 4).
    void SaveMissionState(FILE* File);
    // FUN_06ebae10: the save load (message 5).
    void LoadMissionState(INI_Reader* Reader);
    // FUN_06ebb620 (stdcall, no this): the NPC's job personality (Mode 0 / 1).
    static void SubmitNpcPersonality(FContentNpcShip* Npc, int Mode);
    // FUN_06eb8b70: stop and free every started RTC script, then empty +0x2f0 (dtor, slot 22).
    void ReleaseRtcScripts();
    // FUN_06eb8a20: load Rtc->Path as a TU23 script; when the player is docked at the script's
    // base, start its bar scene (Rtc->BarSceneHandle) and keep it in Rtc->Encounter, otherwise
    // free it. Act_AddRTC, Mission_01b, OnBaseEnter, OnPlayerMissionChanged.
    void StartRtcScript(FMissionRtc* Rtc);
    // FUN_06eb8d10: undo Act_StaticCam -- bCutsceneActive = 0, the player ship vulnerable
    // again, NagClamp = 0, every NPC's +0x1b1 cleared, the ship handed one Avoidance(true)
    // directive, the GCS clamp off. Act_RevertCam.
    void RevertStaticCam();
    // FUN_06ec9b30: the attitude of ObjA towards ObjB (FUN_06f682c0, TU53
    // SetObjectAttitude) and of the rest of ObjA's formation when ObjA is an NPC leading it or
    // flying alone; the reputation of each non-mission solar involved is kept once in +0x174.
    // Returns ObjA's attitude. Act_SetVibe / SetVibeLbl / SetVibeLblToShip / SetVibeShipToLbl.
    float SetObjectVibe(unsigned int ObjA, unsigned int ObjB, float Lo, float Hi);

    // Trigger engine members.
    FTriggerMap TriggerMap;                                   // +0x1b0 all triggers by id
    std::list<FTriggerMap::iterator> ActiveTriggers;          // +0x1c4 active (map nodes)
    std::map<unsigned int, unsigned int> PendingActivations;  // +0x1d0 target -> source id
    std::map<unsigned int, unsigned int> InitiallyActive;     // +0x1e4 InitState ACTIVE ids
    std::list<FMissionTimer> Timers;                          // +0x1f8 delayed actions
    // +0x204: map<id, nag object> (ctor FUN_06f1f4f0, unique keys; Act_NagGreet /
    // Act_NagDistTowards / Act_NagDistLeaving insert, Act_NagOff erases, FUN_06ecef10 gets the
    // messages, slot 22 / the dtor delete every value). MissionAct.h.
    std::map<unsigned int, FMissionNag*> Nags;
    unsigned char NagClamp = 0;                               // +0x218 (ctor 0)
    FMissionNnPath NnPath;                                    // +0x21c (0x88 bytes)
    IStoryMissionEvents* StoryEvents;                         // +0x2a4 = CreateParms +8
    int PreviousMissionState;                                 // +0x2a8 (ctor 2)
    int MissionState;                                         // +0x2ac desired (ctor 2)
    // +0x2b0: when 1, SetDesiredMissionState drops FAIL requests (06eb9730). The ctor's 0
    // (06eb7793) is the only write in content.dll.
    unsigned char Field_0x2b0;
    int CampaignSlotCurrent;                                  // +0x2b4 (mission ctor)
    int CampaignSlotSucceed;                                  // +0x2b8 (mission ctor)
    int StoryCue;                                             // +0x2bc "story_cue" (ctor 0)
    // +0x2c0: 0x5528 + 0xc8 per story mission, written by each mission ctor (SetChapter,
    // 06ea18ec ..); no reader in content.dll.
    unsigned int Field_0x2c0;
    unsigned int AcceptedObjectiveIds;                                 // +0x2c4 objective ids of the accepted offer
    // +0x2c8 (ctor 0): set with the cutscene clamps by Act_CallThorn / Act_StaticCam, cleared
    // by RevertStaticCam. No reader in TU21.
    unsigned char bCutsceneActive;
    // +0x2cc: the reputation pop-up is up (FUN_06eba1f0 sets it, message 0x30 answers it).
    unsigned char bRepPopUpShown;
    // +0x2cd: the reputation pop-up is armed (cleared by the answer and the acceptance).
    unsigned char bRepPopUpArmed;
    unsigned int RepPopUpSystemId;                            // +0x2d0 system the pop-up waits for
    std::vector<unsigned int> RepPopUpGroups;                 // +0x2d4 groups set to 0.6 on "yes"
    // +0x2e4 ID_String: trigger id of the last Act_Save; written by the trigger save as
    // "trigger = %u".
    unsigned int SaveTriggerId = 0;
    // +0x2e8 (ctor 1): mad-lib id shown with the state change (Act_ChangeState +0x10, message
    // 0x1006, FUN_06eb99a0).
    unsigned int StateMessageId;
    // +0x2ec (ctor 0): objectives need a renumber (base enter / space enter clear it).
    unsigned char bNeedsObjectiveRenumber;
    // +0x2f0 (head +0x2f4, size +0x2f8): started RTC scripts. Pushed by Mission_01b and
    // FUN_06eb8a20's callers; walked by FUN_06eb8b70, FUN_06eb9ef0 and Cnd_RTCDone.
    std::list<FMissionRtc> RtcScripts;
    // +0x2fc (ctor FUN_06ed0440, node 0x3c; size +0x304): labels named by Act_DisableFriendlyFire
    // (LAB_06ec8270 appends each one not yet listed).
    std::list<TString<0x30>> FriendlyFireOffLabels;
    // +0x308: location id of the player's last location enter on a base (FUN_06eb8f50 case
    // 0x29). Retail's ctor does not write it. Read by Cnd_CharSelect / Cnd_RumorHeard.
    unsigned int CurrentLocationId = 0;
    // +0x30c: mission time in seconds (ctor 0; FUN_06eb8f50 case 0x20 adds the frame time).
    // Read by Cnd_NPCSystemEnter and Cnd_TetherBroke.
    float ElapsedTime = 0.0f;
    // +0x310 (head +0x314, size +0x318): ambient scenes.
    std::list<FMissionAmbientScene> AmbientScenes;
    // +0x31c: costumes of the [Dialog] "line = ..., Player" speakers (FUN_06eb84c0, unique by
    // FUN_06eb8ef0), sent as CostumeHint on space enter (FUN_06eba720).
    std::vector<Costume> DialogCostumes;

    // The unique push_back of a speaker costume into mission +0x31c (FUN_06eb8ef0 test,
    // FUN_06ed1d30 insert), inlined by FUN_06eb84c0 [Dialog] and the comm Acts.
    void AddDialogCostume(Costume const* SpeakerCostume);

    // FUN_06eb84c0 [Trigger] branch: the values of one [Trigger] section
    // ("nickname" builds and inserts a trigger through FTrigger::Load, "State" is read and dropped).
    void LoadTriggerSection(INI_Reader* Reader);

    // FUN_06eb82c0: activate the trigger with *TriggerId (no-op when unknown or already listed).
    void ActivateTrigger(unsigned int const* TriggerId);

    // FUN_06eb83e0: drop *TriggerId from the active list (State = INACTIVE) and from the
    // pending-activation map.
    void DeactivateTrigger(unsigned int const* TriggerId);

    // FUN_06eb9af0: run message *EventId through the active triggers, then drain pending
    // activations queued by Act_ActTrig.
    void DispatchTriggerEvent(pub::Controller::EMessage const* EventId, void const* EventParms);

    // FUN_06eba430 (thiscall): every script of RtcScripts whose path equals Path->Data
    // is marked finished (+0x94 of the node); when the player stands in the mission's location
    // (+0x308) of their base, the bar character named by the script's role gets its behavior
    // cleared (-1). Act_RemoveRTC, Mission_01b.
    void RemoveRtc(TString<0x80> const* Path);

    // FUN_06ebb540 (thiscall): when the player's ship is farther than sqrt(RadiusSq) from
    // *Position, relocate it there (FUN_06f67740). Act_MovePlayer.
    void MovePlayer(Vector const* Position, float RadiusSq);

    // FUN_06eb9c60: count the delayed-action list down by DeltaSeconds and run expired ones.
    void TickTimers(float DeltaSeconds);

    // FUN_06ecef10 (thiscall on the mission's +0x204 map, RET 8): message 0x3b moves the nags of
    // the entering player to its system; unless NagClamp (+0x218) is set, every nag listening to
    // MessageId gets it, and a done one is unlinked (its object is not freed).
    void DispatchNagMessage(pub::Controller::EMessage MessageId, void const* MessageParms);
};

// FUN_06eb8ef0 (thiscall on A): the four parts, the accessory count and the first
// count accessories are equal.

// FUN_06eb8f50 case 0x1000: activate every InitState ACTIVE trigger.
void ActivateInitialMissionTriggers(FMissionScript* Script);
