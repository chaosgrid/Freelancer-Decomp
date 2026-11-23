// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Content.dll TU53 (SpaceSupport.cpp in retail): the docking queues of ships (NPC and player)
// and the formation helpers they use.
//
// Globals (current std containers, no retail ABI):
//   DAT_06fd1bb8  list<FDockQueueEntry>  ships waiting for a free docking terminal
//   DAT_06fd1bc8  list<FDockQueueEntry>  ships granted a terminal and flying in
//   DAT_06fd1c88  vector<FDockTerminal>  terminals marked occupied / free
//   DAT_06fd1b84  map<dock target, list<NPC ship params>>  ships queued to launch from a
//                 dock target (consumed by FUN_06f607e0)
//   DAT_06fd1c84  float, -0.6: the attitude at or below which a dock target refuses a ship
//   DAT_06fd1ccc..06fd1cd8  ship class ids of the four terminal sizes
//
// Terminal types (pub::SpaceObj::TerminalInfo::Value, the docking sphere type): 1, 2, 6 berth,
// 3 small moor, 4 medium moor, 5 large moor, 7 and 8 open to every ship (FUN_06f6c580's
// switch). The response codes are DOCK_HOST_RESPONSE values (common_server.h): 1 access
// denied, 2 no terminal fits the ship, 3 every fitting terminal is busy / the target moves,
// 4 proceed, 5 the target has no terminal (or, from FUN_06f6d820, the ship is unknown).

#include "Comms/Gcs.h"               // FGcsObjectEvent

#include <list>
#include <map>
#include <vector>

class FContentShipBase;
class FContentNpcShip;
struct I3DMathEngine;
struct FSpaceObjPlacement;
struct FTransform;
class Matrix;
struct FFormation;
class Vector;
struct FContentNpcShipParams;
struct FSystemInfo;

// The 0x28-byte value of both dock lists (node + 8).
struct FDockQueueEntry
{
    // FUN_06f6bbf0: Time = the game time (FUN_06efec10).
    FDockQueueEntry();

    unsigned int SystemId;           // +0x00
    unsigned int ShipId;             // +0x04
    unsigned int DockTargetId;       // +0x08
    int DockIndex;                   // +0x0c terminal index, -1 = any
    // +0x10: in the waiting list a docking ring's terminal choice; in the docking list set
    // once the ship is inside -- it is then finished by FUN_06f6e9d0 instead of timing out.
    // TODO: its writer (SpaceSupport).
    bool bField_0x10;
    double Time;                     // +0x18 game time the ship entered the docking list
    // +0x20: set, the docking entry never times out. It is the dock request's +0x0c byte;
    // waiting entries keep 0, so a grant out of the wait queue passes 0.
    bool bField_0x20;
};

// The GCS events of the dock queues: the dock target speaks (Speaker +0x08) to the ship
// (Listener +0x0c).
// Type 2 (0x20, vtable 06fbbd70): a waiting ship was granted a terminal (sent by FinishDocking;
// the defaults are its stores before the fields are set).
class FGcsDockGrantedEvent : public FGcsObjectEvent
{
public:
    // LAB_06ee9660.
    int GetType() const override;

    int DockIndex = -1;                  // +0x10
    // +0x14: the terminal's TerminalInfo::Value, or with index -1 GetDockTypeForClasses.
    unsigned int DockType = 0;
    int Response = 5;                    // +0x18 DOCK_HOST_RESPONSE, 4 when sent
    // +0x1c: 1 when the grant comes out of the wait queue; TU25 then only says
    // "gcs_dockrequest_nowcleared" / JumpGateOpenAfterDelay.
    bool bWaitedInQueue = false;
};

// Type 3 (0x18, vtable 06fbbc18): a ship leaves a terminal (LaunchQueuedShips, SpaceSupport
// msg 0x36). Retail sets every field.
class FGcsLaunchEvent : public FGcsObjectEvent
{
public:
    // LAB_06f5e540.
    int GetType() const override;

    int DockIndex = 0;                   // +0x10
    unsigned int TerminalType = 0;       // +0x14 TerminalInfo::Value
};

// DAT_06fd1c88 element (0xc).
struct FDockTerminal
{
    unsigned int DockTargetId;       // +0x00
    int DockIndex;                   // +0x04
    bool bOccupied;                  // +0x08
};

extern std::list<FDockQueueEntry> GDockWaitingShips;                                  // DAT_06fd1bb8
extern std::list<FDockQueueEntry> GDockingShips;                                      // DAT_06fd1bc8
extern std::vector<FDockTerminal> GDockTerminals;                                     // DAT_06fd1c88
// DAT_06fd1c98: the terminal states last handed to pub::SpaceObj::Activate. FUN_06f6bc20
// moves GDockTerminals' changes into it.
extern std::vector<FDockTerminal> GDockTerminalsApplied;
extern std::map<unsigned int, std::list<FContentNpcShipParams>> GDockLaunchQueues;    // DAT_06fd1b84
extern float GDockHostileAttitude;                                                     // DAT_06fd1c84
// DAT_06fd1cd8 / DAT_06fd1cd4 / DAT_06fd1cd0 / DAT_06fd1ccc (can_use_berths / small / medium /
// large moors) are the last four of TU54's 16 static ids: DockQueues.cpp's private GIdCanUse*
// copies from ContentStaticIds.h.

// DAT_06fd1c70: the TU's one I3DMathEngine, filled on first use. Every retail site reads it
// and, when null, calls FUN_06fa8fa0 (ContentAcquire3DMathEngine) and stores the result;
// GetSpaceMathEngine is that inline sequence.
extern I3DMathEngine* GSpaceMathEngine;
I3DMathEngine* GetSpaceMathEngine();

// FUN_06f601f0: the first entry of List whose ShipId is ShipId, or end().
std::list<FDockQueueEntry>::iterator __cdecl FindDockQueueEntry(std::list<FDockQueueEntry>* List,
                                                                unsigned int ShipId);

// FUN_06f66220 (45 callers): appends to Out every ship of formation FormationId -- per system
// the space objects, then every player ship -- and sorts all of Out by the formation slot.
// FormationId -1 does nothing.
void __cdecl CollectFormationMembers(std::vector<FContentShipBase*>* Out, unsigned int FormationId);

// FUN_06f67660: the leader (slot 0, or formation -1) of formation FormationId; null when none.
FContentShipBase* __cdecl FindFormationLeader(unsigned int FormationId);

// FUN_06f6ee40: when the leader of FormationId is in the docking list at a type 7 / 8
// terminal, cancel Ship's docking.
void __cdecl CancelDockingBehindLeader(FContentShipBase* Ship, unsigned int FormationId);

// FUN_06f671a0: resubmit Ship's state graph (escort behind its leader, else leader) and its
// formation state.
void __cdecl ResetFormationState(FContentShipBase* Ship);

// FUN_06f67090: take Ship out of its formation; a leader dissolves the whole formation.
void __cdecl RemoveFromFormation(FContentShipBase* Ship);

// FUN_06efeb00 (TU33, lives in GameSupport.cpp): ++DAT_06fcf42c, the new formation id.
unsigned int __cdecl AllocateFormationId();

// FUN_06f66dc0: every other member of Ship's formation leaves it; the ships of the players
// among them are appended to Ships first.
void __cdecl ReleaseFormationMembers(FContentShipBase* Ship, std::list<FContentShipBase*>* Ships);

// FUN_06f66ec0: the ships of Ships become a new formation laid out by Formation -- NPC ships
// take the non-player slots in list order, player ships the first player slot (every player
// the same one).
void __cdecl FormFormation(std::list<FContentShipBase*>* Ships, FFormation* Formation,
                           bool bReleaseLeaderFollowers);

// FUN_06f6eca0: the first docking entry of *ShipId; OutTerminalType's GetTerminalInfo result is
// not tested. False when the ship is not docking.
bool __cdecl GetShipDockingTarget(unsigned int const* ShipId, unsigned int* DockTargetId,
                                  int* DockIndex, unsigned int* OutTerminalType);

// FUN_06f6dff0: ActivateEquipment type 3 on ShipId, or with bFormation on every member of its
// formation (the ship alone when it has none).
void __cdecl SetDockEquipment(unsigned int ShipId, bool bOn, bool bFormation, FSystemInfo* System);

// FUN_06f6e810: drop ShipId from both dock lists (its docking entry answered with response 5);
// with bFormation, the rest of its formation too. PassThrough is only handed to the recursive
// calls.
void __cdecl CancelShipDocking(unsigned int ShipId, unsigned int PassThrough, bool bFormation);

// FUN_06f67740: into no system the ship's docking is cancelled first; then the dock queues are
// updated (tail jump to FUN_06f6c250).
void __cdecl RelocateShip(unsigned int const* ShipId, unsigned int const* SystemId,
                          Vector const* Position, Matrix const* Orientation);

// FUN_06f682c0 (only caller the TU21 mission vibe setter FUN_06ec9b30): the attitude is Lo
// (Lo == Hi) or Lo + rand() / 32768 * (Hi - Lo); -2.0f when either GetRep fails.
float __cdecl SetObjectAttitude(unsigned int ObjA, unsigned int ObjB, float Lo, float Hi);

// FUN_06f6c250: the dock queue tick.
void __cdecl UpdateDockQueues();

// FUN_06f6e9d0: erase *It from the docking list (freeing its terminal when nothing waits to
// launch there) and grant waiting ships their terminals.
void __cdecl FinishDocking(std::list<FDockQueueEntry>::iterator* It);

// FUN_06f6d820: ask for a terminal; on 4 the ship enters the docking list.
int __cdecl TryStartDocking(unsigned int SystemId, unsigned int const* ShipId,
                            unsigned int const* DockTargetId, int* DockIndex, bool bField_0x20);

// FUN_06f6c920: gate locks, the player's dock permission and the target's attitude, then
// ChooseDockTerminal; with 4 a moving target answers 3.
int __cdecl CheckDockPermission(std::vector<unsigned int> const* ClassIds, int const* ShipRep,
                                unsigned int const* ShipId, Vector const* Position,
                                unsigned int const* DockTargetId, int* DockIndex);

// FUN_06f6c580: pick a free terminal of DockTargetId that fits ClassIds (the one nearest
// Position, else a random one).
int __cdecl ChooseDockTerminal(std::vector<unsigned int> const* ClassIds,
                               unsigned int const* DockTargetId, Vector const* Position,
                               int* DockIndex);

// FUN_06f6bdd0.
bool __cdecl IsShipAtDockPoint(unsigned int SystemId, unsigned int const* ShipId,
                               unsigned int const* DockTargetId, int const* DockIndex);

// FUN_06f6e420: 7 jump gate / hole, 8 airlock, docking ring 6 or 0, else 1 / 3 / 4 / 5 or 0.
int __cdecl GetDockTypeForClasses(unsigned int const* DockTargetId,
                                  std::vector<unsigned int> const* ClassIds);

// FUN_06f6d920 (FinishDocking passes (0, 1), SpaceSupport msg 0x11 (1, 0)): a formation whose
// leader docks at a station breaks up -- the NPC members leave it, take the leader's directives
// and a clone of its guide; with either flag the players' ships leave it too, and with
// bRedirectPlayers each is sent to dock where the leader's first (Dock) directive or its own
// docking entry says.
void __cdecl BreakFormationAtDock(unsigned int const* SystemId, unsigned int const* ShipId,
                                  bool bRedirectPlayers, bool bReleasePlayers);

// FUN_06f6dd80: the other members of the ship's formation enter the docking list behind it
// and are told to dock (response 4).
void __cdecl DockFormationFollowers(unsigned int const* SystemId, unsigned int const* ShipId,
                                    unsigned int const* DockTargetId, int const* DockIndex);

// FUN_06f6e530 (also inlined in FUN_06f6c250 / FUN_06f6e9d0): the entry takes bOccupied unless
// its flag is already set; an unlisted pair is appended.
void __cdecl MarkDockTerminal(unsigned int DockTargetId, int DockIndex, bool bOccupied);

// FUN_06f6ed10 (after a refused formation request): a wingman whose formation leader is in the
// docking list at a terminal of type 7 or 8 follows it in.
void __cdecl FollowLeaderIntoDock(FContentShipBase* Ship);

// FUN_06f66690 (SpaceSupport msg 0x19): *ShipId asks to join *LeaderId's formation. 0 =
// joined; -4 no leader ship, -3 no requesting ship, -1 already together, -8 formation records
// missing / the formation's first member is a wingman / players not grouped, -9 the formation
// docks at a gate locked for the player (or a 0x120-type target), -5 no free slot.
int __cdecl HandleFormationRequest(unsigned int const* ShipId, unsigned int const* LeaderId,
                                   FSystemInfo* System);

// FUN_06f6df00: true when ShipId is a player's ship or any member of its formation is.
bool __cdecl IsPlayerInShipFormation(FSystemInfo* System, unsigned int ShipId);

// The 0x10-byte parameter block of SpaceSupport message 0x12 (a ship asks to dock).
struct FDockRequest
{
    unsigned int ShipId;          // +0x00
    unsigned int DockTargetId;    // +0x04
    unsigned int SystemId;        // +0x08
    bool bField_0x0c;             // +0x0c handed to TryStartDocking (its bField_0x20 argument)
};

// FUN_06f6e110: SpaceSupport message 0x12. A __thiscall on the support object in retail, but
// the body never reads this, so the port keeps it a free function. Returns the
// DOCK_HOST_RESPONSE.
int HandleDockRequest(FDockRequest const* Request);

// FUN_06f6d040: members of the ship's formation that are players for whom DockTargetId is a
// locked gate leave the formation.
void __cdecl DropPlayersLockedOutOfGate(unsigned int const* ShipId, unsigned int const* DockTargetId);

// FUN_06f66c80: the blocks become one new formation laid out by Formation around Placement --
// block i takes slot i unless that is a player slot (then it keeps its own values; i still
// advances), then every block is placed at its slot.
void __cdecl PlaceFormationBlocks(std::list<FContentNpcShipParams>* Blocks, FFormation* Formation,
                                  FTransform const* Placement);

// FUN_06f67440: the NPC ships of System (those with +0x5a8 clear) within *Radius of the
// placement, or -- when *ConeRange > 0 -- within *ConeRange and inside the 35 degree cone
// around the placement's -z axis; with ClassId only ships of that class count.
int __cdecl CountNpcShipsNear(FSystemInfo* System, FSpaceObjPlacement const* Placement,
                              float const* Radius, float const* ConeRange,
                              unsigned int const* ClassId);

// FUN_06f6cb20: *ObjectId's docking target is a base whose [BaseFaction] list has *Group.
// Neither GetDockingTarget's result nor the base record is tested.
bool __cdecl IsGroupAtDockBase(unsigned int const* Group, unsigned int const* ObjectId);

// FUN_06f6cb90: whether a ship of these classes could dock at *DockTargetId at all -- not a
// gate locked for *ShipId, an attitude above -0.6 (the target's towards *ShipRep, or with
// ShipRep -1 the group feeling of the target's affiliation towards *Group, 1.0 when either is
// -1), and some terminal that fits the classes (7 and 8 fit every ship).
bool __cdecl CanUseDockTarget(std::vector<unsigned int> const* ClassIds, int const* ShipRep,
                              unsigned int const* Group, unsigned int const* ShipId,
                              unsigned int const* DockTargetId);

// FUN_06f6bc20 (SpaceSupport HandleMessage tail, every message but 0x1007): each changed
// terminal state goes to pub::SpaceObj::Activate; on success it is recorded, and targets that
// became occupied get message 0x1007 afterwards. GDockTerminals is emptied.
void __cdecl ApplyDockTerminalStates();

// FUN_06f60340 (caller FSystemInfo::CreateNpcShip): a copy of *Params joins
// GDockLaunchQueues[DockTarget], then the launch pump runs. Returns whether that queue is
// still not empty.
bool __cdecl QueueDockLaunch(FContentNpcShipParams const* Params, unsigned int DockTarget,
                             FContentNpcShip** OutShip);

// FUN_06f607e0 (also the SpaceSupport heartbeat): creates the NPC ships waiting in
// GDockLaunchQueues whose dock target lets them out -- jump gates / holes / airlocks (type &
// 0xc40) jump them in, bases and docking rings (type & 0x600de0) take a terminal first and
// launch them from it. Returns the last created ship whose params +0x104 is not negative.
FContentNpcShip* __cdecl LaunchQueuedShips();
