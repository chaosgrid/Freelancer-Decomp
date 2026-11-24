// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU43: the NPC ship -- every ship Content creates for a mission, an encounter
// or the population. It is the TU50 ship base plus a pub::AI::ContentCallback at +0x174 (the
// AI reports tether / lead / flee / combat events there), an optional TU37 guide at +0x178
// and its own copy of the pilot personality.
//
// Also here: TU53's NPC ship event list DAT_06fd1bd8. It lives in SpaceSupport's range, but
// the NPC ship is its only element type; SpaceSupport routes its messages into it.
//
// Retail class sizes: parameters 0x504 (ctor FUN_06f33500), ship 0x5b0. No RTTI in Content;
// the class names are the port's.

#include "NPCs/BehaviorCmds.h"
#include "Guides/Guides.h"             // FGuideDescHandle (the +0xfc member)
#include "common_id.h"          // ID_String
#include "common_pub.h"         // pub::AI::ContentCallback, pub::AI::Personality
#include "common_reputation.h"  // Costume
#include "server_cobject.h"     // pub::SpaceObj::CargoDesc

#include <list>
#include <vector>

class FContentGuide;            // Guides.h (TU37 guide, vtable base 06fb8240)
struct FGuideDesc;              // Guides.h (TU37 guide description, vtable 06fb8224)
class FGuideDescBase;           // Guides.h (head of FGuideDesc and of the [MetaBehavior] objects)
struct FNpcShipArch;            // NpcShips.h (TU42)
struct FContentPlayerState;     // ContentPlayerState.h
class FContentNpcShip;

// Parameter block of an NPC ship (0x504). The default ctor FUN_06f33500 builds the TU50
// block (see FContentShipParams), then the members below with their ctor values.
struct FContentNpcShipParams : public FContentShipParams
{
    unsigned char bExcludeFromShipCount = 0;             // +0x78 -> ship +0x5a8
    unsigned int Loadout = 0;                            // +0x7c ShipInfo::LoadoutID; arch +8
    std::vector<pub::SpaceObj::CargoDesc> Cargo;         // +0x80 copied into ShipInfo::Cargo
    Matrix Orientation;                                  // +0x90 identity
    Vector Position;                                     // +0xb4 0
    int StateGraph = -1;                                 // +0xc0 arch +0x10; -1 -> "NOTHING"
    // +0xc4: the ctor zeroes the four parts and the count only; the accessories are copied
    // up to AccessoryCount. Filled by FUN_06f34a80 (FactionProps).
    Costume CommCostume;
    unsigned int Voice = 0;                              // +0xf8 ShipInfo::VoiceId
    // +0xfc: one reference held (AddRef FUN_06f21fd0, Release FUN_06f28e10). Any guide
    // description: an FGuideDesc from the encounter / random mission code, or a [MetaBehavior]
    // object from the mission ship spawn (TU38); the ship ctor hands it to CreateGuide.
    FGuideDescHandle GuideDesc;
    ID_String Pilot;                                     // +0x100 cleared
    float Longevity = -1.0f;                             // +0x104 DAT_06fb956c -> ship +0x1bc
    float InitialLifeTime = 24.0f;                       // +0x108 -> ship +0x1c8
    float Field_0x10c = 500.0f;                          // +0x10c -> ship +0x1c0
    int DockIndex = -1;                                // +0x110 TODO: no reader in TU43
    // +0x114: an object id; when set, the guide starts only if the object's type has a bit
    // of 0xc40.
    unsigned int GuideTargetId = 0;
    std::list<unsigned int> LabelIds;                 // +0x118 -> ship +0x17c
    pub::AI::Personality Personality;                    // +0x124
    unsigned int CreationType = 2;                       // +0x4fc -> ship +0x1b4
    int Level = 0;                                       // +0x500 arch +0x24; ShipInfo +0xbc

    // FUN_06f33500.
    FContentNpcShipParams();
    // Copy, inlined in TU58 FUN_06f732e0: the TU50 block (FUN_06f73680), then every member.
    FContentNpcShipParams(FContentNpcShipParams const& Other);
    FContentNpcShipParams& operator=(FContentNpcShipParams const&) = delete;
    // FUN_06ef4bb0; the TU50 block dtor (FUN_06ef4ab0 body) is inlined.
    ~FContentNpcShipParams();
};

// FUN_06f348f0 (cdecl): fill Params from an npcships.ini arch (null: nothing). Pilot
// overrides the arch pilot and Job selects a JobBlock (both optional).
void __cdecl ApplyNpcShipArch(FContentNpcShipParams* Params, FNpcShipArch const* Arch,
                              ID_String* Pilot, ID_String const* Job);
// FUN_06f34a40 (cdecl): the arch *ArchId of GNpcShipArchs through ApplyNpcShipArch; true
// when the arch exists.
bool __cdecl ApplyNpcShipArchById(FContentNpcShipParams* Params, unsigned int const* ArchId,
                                  ID_String* Pilot, ID_String const* Job);
// FUN_06f34a80 (cdecl): costume and voice of FactionId's faction properties (TU48).
// Gender goes to the costume pick as is and to the voice pick as (Gender != 1) + 1.
// TODO: confirm the meaning of Gender (TU48 FUN_06f4f430 / FUN_06f4f620).
bool __cdecl ApplyNpcShipFaction(unsigned int FactionId, int Gender, FContentNpcShipParams* Params);

// Retail vtables: primary 06fb95a0 {sdd 06f33d00, 06ea1460, 06f33cf0, 06f57c30, 06f57c40,
// 06f33e70, 06f341f0}; DirectiveCallback at +0x118 06fb959c {06f33f90}; ContentCallback at
// +0x174 06fb9574 {06f34240, 06f34250, 06f34400, 06f34000, 06f34ce0, 06fa8f2a (Common's
// information_request), 06f34ad0, 06f34090, 06f34050, 06f34cb0}.
class FContentNpcShip : public FContentShipBase, public pub::AI::ContentCallback
{
public:
    // FUN_06f336a0.
    explicit FContentNpcShip(FContentNpcShipParams const* Params);
    // FUN_06f34680 (called by the deleting dtor 06f33d00).
    ~FContentNpcShip() override;

    // Slot 1, 06ea1460.
    bool IsNpcShip() override;
    // Slot 5, 06f33e70.
    virtual pub::AI::Personality* GetPersonality();
    // Slot 6, 06f341f0: up to MaxCount scan_for_cargo archetypes of the faction properties
    // into Out; returns the count.
    virtual int GetScanForCargo(unsigned int* Out, int MaxCount);

    // DirectiveCallback slot 0, 06f33f90.
    void OnDirectiveComplete(int CompletedBehavior, int DirectiveResult) override;

    // ContentCallback. follow_event (06f34240) and information_request (the Common import
    // 06fa8f2a) keep the inline bodies of common_pub.h.
    void tether_object_event(uint ObjectId1, uint ObjectId2, TetherStatusType Status,
                             Vector const& Position) override;                 // 06f34250
    void lead_object_event(uint ObjectId1, uint ObjectId2, LeadProgressType Progress,
                           float Value, Vector const& Position) override;      // 06f34400
    void ship_in_sights_event(uint ObjectId) override;                         // 06f34000
    bool request_flee_destination(uint ObjectId, FleeReason Reason, FleeDestinationType& OutType,
                                  Vector& OutPosition, uint& OutParam,
                                  float& OutFloat) override;                   // 06f34ce0
    void scan_state_change(bool bScanning) override;                           // 06f34ad0
    void combat_state_notification(CSNType Type, uint Param1, uint Param2) override; // 06f34090
    void combat_drift_event() override;                                        // 06f34050
    CreationType get_creation_type() override;                                 // 06f34cb0

    // FUN_06f33e80: owned by the SpacePop controller (ControllerId == DAT_06fd0e64).
    bool IsSpacePopShip() const;
    // FUN_06f33ea0: flee to a position.
    void SetFleePosition(Vector const* Position, float const* Distance);
    // FUN_06f33ee0: flee from / to an object.
    void SetFleeObject(unsigned int const* ObjectId, float const* Distance, bool bType3);
    // FUN_06f33f20; only caller the GCS player hail conversation.
    void SetLastHailPlayer(FContentPlayerState* Player);
    // FUN_06f33f40: no flee destination.
    void ClearFlee();
    // FUN_06f33fd0: when set, the event list stops this ship's timer (FUN_06f68600). While
    // set the tether / lead callbacks do nothing.
    void SetCommMuted(bool Value);
    // FUN_06f33ff0.
    bool IsCommMuted() const;
    // FUN_06f34100: a comm line of this ship ended (SpaceSupport msg 0xb via the event list).
    void OnCommLineDone();
    // FUN_06f34800: move the ship to System (null: out of every system) at Position /
    // Orientation (either may be null).
    void Relocate(FSystemInfo* NewSystem, Vector const* Position, Matrix const* Orientation);
    // FUN_06f34af0: replace the guide by one built from Desc.
    void SetGuideFromDesc(FGuideDescBase* Desc, bool bStart);
    // FUN_06f34b70: replace the guide (null: cancel and drop it).
    void SetGuide(FContentGuide* NewGuide, bool bStart, bool bReset);
    // FUN_06f34c30: forward an event to the guide's HandleEvent (slot 7); drop the guide when
    // it is done.
    void ForwardToGuide(pub::Controller::EMessage MsgId, void const* Parms);
    // FUN_06f34c80: the guide's type, 0 without a guide.
    int GetGuideType() const;
    // FUN_06f34ca0.
    FContentGuide* GetGuide() const;

    // +0x178: ref-holding; the dtor's unwind map (FuncInfo 06fc3b8c) releases it after
    // LabelIds and before the TU50 base.
    FContentGuideHandle Guide;
    std::list<unsigned int> LabelIds;           // +0x17c params +0x118. TODO: meaning
    // +0x188 flee destination kind read by request_flee_destination: 0 ask the guide,
    // 1 position (+0x190), 2 / 3 object (+0x19c).
    int FleeState = 0;
    float FleeDistance = 0.0f;                     // +0x18c returned as OutFloat
    Vector FleePosition;                           // +0x190
    unsigned int FleeObjectId = 0;                 // +0x19c
    // +0x1a0: the player that hailed this ship last (GCS type 5 compares it with the hailing
    // player state); +0x1a8 the game time of that hail.
    FContentPlayerState* LastHailPlayer = nullptr;
    double LastHailTime = -1.0;
    bool bCommMuted = false;                      // +0x1b1 see SetCommMuted
    // +0x1b4 params +0x4fc, mapped by get_creation_type (the name CreationType is taken by
    // ContentCallback's enum).
    unsigned int CreationKind;
    // +0x1b8: 5 from the ctor and from the event list's timer; each finished comm line
    // counts it down, at 0 the owning controller gets message 0x1006.
    int CommLinesLeft = 5;
    // +0x1bc params +0x104: the life LifeTime is refilled to while a player is near or the
    // ship flies in a formation; -1 = never expires (FSystemInfo::UpdateNpcShipLives).
    float Longevity;
    float Field_0x1c0;                             // +0x1c0 params +0x10c. TODO: no reader
    unsigned int Loadout;                          // +0x1c4 params +0x7c
    // +0x1c8 params +0x108: the remaining life; run down by UpdateNpcShipLives at
    // GetNpcShipLifeRate, the ship is destroyed when it drops below 0.
    float LifeTime;
    ID_String PilotId;                             // +0x1cc cleared. TODO: writer
    pub::AI::Personality Personality;              // +0x1d0
    // +0x5a8 params +0x78: when set, CountNpcShipsNear (FUN_06f67440, the encounter density
    // count) skips the ship. Only the params ctor writes it in the port (0).
    unsigned char bExcludeFromShipCount;
    // +0x5ac cleared by the ctor; the only writer is FEncounterParameters::Spawn: the
    // [EncounterParameters] nickname id of the encounter that spawned the ship. Content has
    // no reader.
    ID_String EncounterParamsId;
};

// ---------------------------------------------------------------------------------------
// TU53 NPC ship event list (DAT_06fd1bd8; ctor FUN_06f5ca60 from the _initterm thunk
// LAB_06f5ca20, dtor FUN_06f15350 from the atexit thunk FUN_06f5ca40). One record per NPC
// ship with a pending timer / lead / tether report; the timers are SpaceSupport's
// (DAT_06fd1c74), so SpaceSupport hands its timer and comm messages to HandleMessage.
// ---------------------------------------------------------------------------------------

// List value (0x30; node 0x38).
struct FNpcShipEvent
{
    FContentNpcShip* Ship;        // +0x00 matched by space object id (+4)
    unsigned int CommLineId;      // +0x04 SetCommLine; matched by msg 0xb
    unsigned int TimerId;         // +0x08 0 = no timer pending
    unsigned int ObjectId;        // +0x0c lead / tether object
    unsigned int LeadProgress;    // +0x10
    unsigned int FollowStatus;    // +0x14 kind 2 only; no writer in Content
    unsigned int TetherStatus;    // +0x18
    float Value;                  // +0x1c lead value (kind 2: follow distance)
    Vector Position;              // +0x20
    // +0x2c what the timer reports: 0 clear the ship's bCommMuted, 1 lead_object_event,
    // 2 follow_event, 3 tether_object_event.
    int Kind;
};

class FNpcShipEventList
{
public:
    // FUN_06f683e0: SpaceSupport messages 0x3e (timer), 0x39 (object destroyed), 0xb (comm
    // line finished).
    void HandleMessage(pub::Controller::EMessage MsgId, void const* Parms);
    // FUN_06f68570.
    void SetCommLine(FContentNpcShip* Ship, unsigned int CommLineId);
    // FUN_06f68600.
    void ClearTimer(FContentNpcShip* Ship);
    // FUN_06f68630: mute the ship (SetCommMuted(true)) for Delay seconds.
    void StartTimer(FContentNpcShip* Ship, float Delay);
    // FUN_06f68710: report the lead event again in 30 s.
    void QueueLeadEvent(FContentNpcShip* Ship, unsigned int ObjectId, unsigned int Progress,
                        float Value, Vector const* Position);
    // FUN_06f68840: report the tether event again in 30 s.
    void QueueTetherEvent(FContentNpcShip* Ship, unsigned int ObjectId, unsigned int Status,
                          Vector const* Position);

    std::list<FNpcShipEvent> Events;       // +0x00
    unsigned int LastLeadProgress = 0;     // +0x0c DAT_06fd1be4
    unsigned int LastFollowStatus = 0;     // +0x10 DAT_06fd1be8
    unsigned int LastTetherStatus = 0;     // +0x14 DAT_06fd1bec
};

extern FNpcShipEventList GNpcShipEvents;   // DAT_06fd1bd8
