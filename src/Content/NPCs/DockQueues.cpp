// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU53: the dock queues and the formation helpers (see DockQueues.h).

// TU54's own 16 static ids; the dock-type tests read the last four (GIdCanUseBerths ..
// GIdCanUseLargeMoors).
#include "Core/ContentStaticIds.h"

#include "NPCs/DockQueues.h"

#include "Bases/BaseDB.h"              // FindContentBaseInfo, FContentBase::FindFaction
#include "Core/ContentInternal.h"
#include "Core/ContentPlayerState.h"
#include "Core/ContentSort.h"         // ContentSort (the VC6 std::sort of CollectFormationMembers)
#include "Encounters/Encounters.h"          // FFormation::GetSlotPosition
#include "Guides/Guides.h"              // FContentGuide::Clone / Release (BreakFormationAtDock)
#include "Missions/MissionObjList.h"      // CreateObjListDirective, FObjListDelay, FObjListLaunch
#include "Missions/MissionObject.h"       // FMissionDirectiveList (the ref-owning list of BreakFormationAtDock)
#include "NPCs/NpcShip.h"             // FContentNpcShipParams (GDockLaunchQueues), FContentNpcShip
#include "Universe/SystemInfo.h"
#include "common_id.h"           // CreateID
#include "common_newchar.h"      // pub::StateGraph::get_state_graph
#include "common_server.h"
#include "common_server_controller.h"   // pub::Controller::_SendMessage
#include "server_cobject.h"      // EQInfo, TerminalInfo, ActivateEquipment, GetTerminalInfo

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

std::list<FDockQueueEntry> GDockWaitingShips;
std::list<FDockQueueEntry> GDockingShips;
std::vector<FDockTerminal> GDockTerminals;
std::vector<FDockTerminal> GDockTerminalsApplied;
std::map<unsigned int, std::list<FContentNpcShipParams>> GDockLaunchQueues;

// LAB_06f6eec0: copied from DAT_06fbbcf4.
float GDockHostileAttitude = -0.6f;

namespace
{

    bool ContainsClass(std::vector<unsigned int> const* ClassIds, unsigned int ClassId)
    {
        // FUN_06eb5f30.
        return std::find(ClassIds->begin(), ClassIds->end(), ClassId) != ClassIds->end();
    }

    // The terminal-type switch inlined twice in FUN_06f6c580.
    bool DoesTerminalFitClasses(unsigned int TerminalType, std::vector<unsigned int> const* ClassIds)
    {
        switch (TerminalType)
        {
        case 1:
        case 2:
        case 6:
            return ContainsClass(ClassIds, GIdCanUseBerths);
        case 3:
            return ContainsClass(ClassIds, GIdCanUseSmallMoors);
        case 4:
            return ContainsClass(ClassIds, GIdCanUseMediumMoors);
        case 5:
            return ContainsClass(ClassIds, GIdCanUseLargeMoors);
        case 7:
        case 8:
            return true;
        default:
            return false;
        }
    }

    // The docking-list scan inlined twice in FUN_06f6c580.
    bool IsDockTerminalInUse(unsigned int DockTargetId, int DockIndex)
    {
        for (std::list<FDockQueueEntry>::iterator It = GDockingShips.begin();
             It != GDockingShips.end(); ++It)
        {
            if (It->DockIndex == DockIndex && It->DockTargetId == DockTargetId)
                return true;
        }
        return false;
    }

    // FUN_06f6c250's local vector (0x14-byte elements).
    struct FDockKick
    {
        FContentShipBase* Ship;      // +0x00 null: cancel the docking by id
        unsigned int ShipId;         // +0x04 only written (and read) for a null Ship
        unsigned int DockTargetId;   // +0x08
        int DockIndex;               // +0x0c
        bool bRedock;                // +0x10
    };

    // FUN_06eeaee0 / FUN_06eb7810: each component of the difference is stored as a float;
    // the squares are summed on the x87 stack (kept here in double).
    double DistanceSquared(Vector const& A, Vector const& B)
    {
        float X = A.x - B.x;
        float Y = A.y - B.y;
        float Z = A.z - B.z;
        return static_cast<double>(Z) * Z + static_cast<double>(Y) * Y + static_cast<double>(X) * X;
    }

    // The 0x5f3759df inverse-square-root idiom inlined twice in FUN_06f6bdd0 (one Newton step,
    // times Square).
    double ApproximateSqrt(float StoredSquare, double Square)
    {
        int Bits;
        std::memcpy(&Bits, &StoredSquare, sizeof(Bits));
        Bits = 0x5f3759df - (Bits >> 1);
        float Y;
        std::memcpy(&Y, &Bits, sizeof(Y));
        double Step = 1.5 - static_cast<double>(Y) * Y * Square * 0.5;
        return Step * Y * Square;
    }
}

// FUN_06f6e530, also inlined in FUN_06f6c250 and FUN_06f6e9d0. A set flag is never cleared
// here.
void __cdecl MarkDockTerminal(unsigned int DockTargetId, int DockIndex, bool bOccupied)
{
    for (std::vector<FDockTerminal>::iterator It = GDockTerminals.begin();
         It != GDockTerminals.end(); ++It)
    {
        if (It->DockTargetId == DockTargetId && It->DockIndex == DockIndex)
        {
            if (!It->bOccupied)
                It->bOccupied = bOccupied;
            return;
        }
    }
    FDockTerminal Terminal;
    Terminal.DockTargetId = DockTargetId;
    Terminal.DockIndex = DockIndex;
    Terminal.bOccupied = bOccupied;
    GDockTerminals.push_back(Terminal);
}

// FUN_06f6bbf0.
FDockQueueEntry::FDockQueueEntry()
    : SystemId(0)
    , ShipId(0)
    , DockTargetId(0)
    , DockIndex(-1)
    , bFinishOnClear(false)
    , Time(GetContentGameTime())
    , bNoTimeout(false)
{
}

// FUN_06f601f0.
std::list<FDockQueueEntry>::iterator __cdecl FindDockQueueEntry(std::list<FDockQueueEntry>* List,
                                                                unsigned int ShipId)
{
    for (std::list<FDockQueueEntry>::iterator It = List->begin(); It != List->end(); ++It)
    {
        if (It->ShipId == ShipId)
            return It;
    }
    return List->end();
}

// FUN_06f66220. The whole vector is sorted by the signed slot with retail's inlined VC6
// std::sort (ContentSort), so ties keep retail's order.
void __cdecl CollectFormationMembers(std::vector<FContentShipBase*>* Out, unsigned int FormationId)
{
    if (FormationId == 0xffffffffu)
        return;

    std::map<unsigned int, FSystemInfo>* Systems = GetSystemInfos();
    for (std::map<unsigned int, FSystemInfo>::iterator SystemIt = Systems->begin();
         SystemIt != Systems->end(); ++SystemIt)
    {
        std::list<FContentSpaceObj*>& Objects = SystemIt->second.SpaceObjs;
        for (std::list<FContentSpaceObj*>::iterator ObjIt = Objects.begin(); ObjIt != Objects.end();
             ++ObjIt)
        {
            FContentShipBase* Ship = static_cast<FContentShipBase*>(*ObjIt);
            if (Ship->FormationId == FormationId)
                Out->push_back(Ship);
        }
    }

    std::map<unsigned int, std::unique_ptr<FContentPlayerState>>& Players = GetContentPlayerStates();
    for (std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator PlayerIt =
             Players.begin();
         PlayerIt != Players.end(); ++PlayerIt)
    {
        FContentShipBase* Ship = PlayerIt->second->ShipObject;
        if (Ship != nullptr && Ship->FormationId == FormationId)
            Out->push_back(Ship);
    }

    ContentSort(Out->data(), Out->data() + Out->size(),
                [](FContentShipBase* A, FContentShipBase* B)
                {
                    return static_cast<int>(A->FormationSlot) < static_cast<int>(B->FormationSlot);
                });
}

// FUN_06f67660. The player walk sits inside the system loop (retail quirk: the players are
// rescanned per system).
FContentShipBase* __cdecl FindFormationLeader(unsigned int FormationId)
{
    std::map<unsigned int, FSystemInfo>* Systems = GetSystemInfos();
    for (std::map<unsigned int, FSystemInfo>::iterator SystemIt = Systems->begin();
         SystemIt != Systems->end(); ++SystemIt)
    {
        std::list<FContentSpaceObj*>& Objects = SystemIt->second.SpaceObjs;
        for (std::list<FContentSpaceObj*>::iterator ObjIt = Objects.begin(); ObjIt != Objects.end();
             ++ObjIt)
        {
            FContentShipBase* Ship = static_cast<FContentShipBase*>(*ObjIt);
            if (Ship->FormationId == FormationId &&
                (Ship->FormationSlot == 0 || Ship->FormationId == 0xffffffffu))
                return Ship;
        }

        std::map<unsigned int, std::unique_ptr<FContentPlayerState>>& Players = GetContentPlayerStates();
        for (std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator PlayerIt =
                 Players.begin();
             PlayerIt != Players.end(); ++PlayerIt)
        {
            FContentShipBase* Ship = PlayerIt->second->ShipObject;
            if (Ship != nullptr && Ship->FormationId == FormationId &&
                (Ship->FormationSlot == 0 || Ship->FormationId == 0xffffffffu))
                return Ship;
        }
    }
    return nullptr;
}

// FUN_06f6ee40. GetTerminalInfo's result is not tested.
void __cdecl CancelDockingBehindLeader(FContentShipBase* Ship, unsigned int FormationId)
{
    FContentShipBase* Leader = FindFormationLeader(FormationId);
    if (Leader == nullptr)
        return;

    std::list<FDockQueueEntry>::iterator It = GDockingShips.begin();
    for (; It != GDockingShips.end(); ++It)
    {
        if (It->ShipId == Leader->SpaceObjId)
            break;
    }
    if (It == GDockingShips.end())
        return;

    unsigned int DockTargetId = It->DockTargetId;
    pub::SpaceObj::TerminalInfo Info;
    pub::SpaceObj::GetTerminalInfo(It->DockTargetId, It->DockIndex, Info);
    if (Info.Value == 7 || Info.Value == 8)
        CancelShipDocking(Ship->SpaceObjId, DockTargetId, false);
}

// FUN_06f671a0 (slot position FUN_06efcb20). The SetPersonalityParams carries only the state
// graph. A follower without a leader gets nothing.
void __cdecl ResetFormationState(FContentShipBase* Ship)
{
    unsigned int FormationId = Ship->FormationId;
    Vector Offset;
    Offset.x = 0.0f;
    Offset.y = 0.0f;
    Offset.z = 0.0f;
    if (Ship->Formation != nullptr)
    {
        FVector SlotPosition = Ship->Formation->GetSlotPosition(Ship->FormationSlot);
        Offset.x = SlotPosition.X;
        Offset.y = SlotPosition.Y;
        Offset.z = SlotPosition.Z;
    }

    pub::AI::SetPersonalityParams Params;
    if (FormationId != 0xffffffffu && Ship->FormationSlot != 0 && Ship->FormationId != 0xffffffffu)
    {
        FContentShipBase* Leader = FindFormationLeader(FormationId);
        if (Leader == nullptr)
            return;
        Params.StateGraphId = pub::StateGraph::get_state_graph(
            pub::AI::get_state_graph_id(Ship->SpaceObjId), pub::StateGraph::ESCORT);
        pub::AI::SubmitState(Ship->SpaceObjId, &Params);
        pub::AI::update_formation_state(Ship->SpaceObjId, Leader->SpaceObjId, Offset);
        return;
    }

    Params.StateGraphId = pub::StateGraph::get_state_graph(
        pub::AI::get_state_graph_id(Ship->SpaceObjId), pub::StateGraph::LEADER);
    pub::AI::SubmitState(Ship->SpaceObjId, &Params);
    unsigned int LeaderId = FormationId == 0xffffffffu ? 0 : Ship->SpaceObjId;
    pub::AI::update_formation_state(Ship->SpaceObjId, LeaderId, Offset);
}

// FUN_06f67090. A leader's members are looked up again in their own system; a dissolved member
// gets slot 0, a single leaver slot -1.
void __cdecl RemoveFromFormation(FContentShipBase* Ship)
{
    unsigned int FormationId = Ship->FormationId;
    if (FindFormationLeader(FormationId) == Ship)
    {
        std::vector<FContentShipBase*> Members;
        CollectFormationMembers(&Members, FormationId);
        for (int Index = 0; Index < static_cast<int>(Members.size()); ++Index)
        {
            FContentShipBase* Member = Members[Index];
            unsigned int MemberId = Member->SpaceObjId;
            FContentShipBase* Live =
                static_cast<FContentShipBase*>(Member->GetSystem()->FindShip(MemberId));
            if (Live != nullptr && Live->FormationId != 0xffffffffu)
            {
                CancelDockingBehindLeader(Live, Live->FormationId);
                Live->SetFormation(0xffffffffu, 0, nullptr);
            }
            ResetFormationState(Member);
        }
        return;
    }

    if (FormationId == 0xffffffffu)
        return;
    Ship->SetFormation(0xffffffffu, 0xffffffffu, nullptr);
    ResetFormationState(Ship);
}

// FUN_06f66dc0.
void __cdecl ReleaseFormationMembers(FContentShipBase* Ship, std::list<FContentShipBase*>* Ships)
{
    std::vector<FContentShipBase*> Members;
    CollectFormationMembers(&Members, Ship->FormationId);
    for (int Index = 0; Index < static_cast<int>(Members.size()); ++Index)
    {
        FContentShipBase* Member = Members[Index];
        unsigned int MemberId = Member->SpaceObjId;
        if (MemberId == Ship->SpaceObjId)
            continue;
        FContentPlayerState* Player = FindContentPlayerStateByShip(&MemberId);
        if (Player != nullptr && Player->ShipObject != nullptr)
            Ships->push_back(Player->ShipObject);
        RemoveFromFormation(Member);
    }
}

// FUN_06f66ec0 (IsPlayerSlot = FUN_06efccd0). Retail quirk: in pass 1 an NPC ship that meets
// a player slot is skipped without advancing Slot, so it gets no formation; in pass 2 Slot is
// not advanced, so every player shares one slot.
void __cdecl FormFormation(std::list<FContentShipBase*>* Ships, FFormation* Formation,
                           bool bReleaseLeaderFollowers)
{
    if (Ships->empty())
        return;

    FContentShipBase* First = Ships->front();
    if (First->FormationId != 0xffffffffu && First->IsNpcShip())
        ReleaseFormationMembers(First, Ships);

    unsigned int FormationId = AllocateFormationId();
    int Slot = 0;
    for (std::list<FContentShipBase*>::iterator It = Ships->begin(); It != Ships->end(); ++It)
    {
        FContentShipBase* Ship = *It;
        unsigned int ShipId = Ship->SpaceObjId;
        if (FindContentPlayerStateByShip(&ShipId) != nullptr)
            continue;
        if (Formation->IsPlayerSlot(Slot))
            continue;
        if (bReleaseLeaderFollowers && Ship->FormationSlot == 0)
            ReleaseFormationMembers(Ship, Ships);
        Ship->SetFormation(FormationId, static_cast<unsigned int>(Slot), Formation);
        ResetFormationState(Ship);
        ++Slot;
    }

    for (std::list<FContentShipBase*>::iterator It = Ships->begin(); It != Ships->end(); ++It)
    {
        FContentShipBase* Ship = *It;
        unsigned int ShipId = Ship->SpaceObjId;
        if (FindContentPlayerStateByShip(&ShipId) == nullptr)
            continue;
        if (!Formation->IsPlayerSlot(Slot))
        {
            while (Slot < static_cast<int>(Formation->Slots.size()))
            {
                ++Slot;
                if (Formation->IsPlayerSlot(Slot))
                    break;
            }
        }
        if (Slot < static_cast<int>(Formation->Slots.size()))
        {
            Ship->SetFormation(FormationId, static_cast<unsigned int>(Slot), Formation);
            ResetFormationState(Ship);
        }
    }
}

// FUN_06f6eca0.
bool __cdecl GetShipDockingTarget(unsigned int const* ShipId, unsigned int* DockTargetId,
                                  int* DockIndex, unsigned int* OutTerminalType)
{
    for (std::list<FDockQueueEntry>::iterator It = GDockingShips.begin(); It != GDockingShips.end();
         ++It)
    {
        if (It->ShipId != *ShipId)
            continue;
        *DockIndex = It->DockIndex;
        *DockTargetId = It->DockTargetId;
        if (OutTerminalType != nullptr)
        {
            pub::SpaceObj::TerminalInfo Info;
            pub::SpaceObj::GetTerminalInfo(It->DockTargetId, *DockIndex, Info);
            *OutTerminalType = Info.Value;
        }
        return true;
    }
    return false;
}

// FUN_06f6dff0. Deviation: retail writes only Type and the flag byte of EQInfo; the port
// zero-fills the rest.
void __cdecl SetDockEquipment(unsigned int ShipId, bool bOn, bool bFormation, FSystemInfo* System)
{
    pub::SpaceObj::EQInfo Info;
    std::memset(&Info, 0, sizeof(Info));
    Info.Type = 3;
    Info.EquipId = bOn ? 1 : 0;

    if (!bFormation)
    {
        pub::SpaceObj::ActivateEquipment(ShipId, &Info);
        return;
    }

    FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(ShipId));
    if (Ship == nullptr)
        return;

    std::vector<FContentShipBase*> Members;
    CollectFormationMembers(&Members, Ship->FormationId);
    if (Members.empty())
        Members.push_back(Ship);
    for (int Index = 0; Index < static_cast<int>(Members.size()); ++Index)
        pub::SpaceObj::ActivateEquipment(Members[Index]->SpaceObjId, &Info);
}

// FUN_06f6e810. The system is looked up at the first matching entry of either list and
// reused. With bFormation the ship is not null-tested.
void __cdecl CancelShipDocking(unsigned int ShipId, unsigned int PassThrough, bool bFormation)
{
    FSystemInfo* System = nullptr;

    std::list<FDockQueueEntry>::iterator It = FindDockQueueEntry(&GDockWaitingShips, ShipId);
    while (It != GDockWaitingShips.end())
    {
        if (It->ShipId != ShipId)
        {
            ++It;
            continue;
        }
        if (System == nullptr)
            System = FindSystemInfo(&It->SystemId);
        SetDockEquipment(It->ShipId, false, true, System);
        It = GDockWaitingShips.erase(It);
    }

    It = FindDockQueueEntry(&GDockingShips, ShipId);
    if (It != GDockingShips.end())
    {
        pub::SpaceObj::Dock(ShipId, It->DockTargetId, -1, DOCK_HOST_RESPONSE_5);
        while (It != GDockingShips.end())
        {
            if (It->ShipId != ShipId)
            {
                ++It;
                continue;
            }
            if (System == nullptr)
                System = FindSystemInfo(&It->SystemId);
            FinishDocking(&It);
        }
    }

    if (System == nullptr || !bFormation)
        return;

    FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(ShipId));
    std::vector<FContentShipBase*> Members;
    CollectFormationMembers(&Members, Ship->FormationId);
    for (int Index = 0; Index < static_cast<int>(Members.size()); ++Index)
    {
        unsigned int MemberId = Members[Index]->SpaceObjId;
        if (MemberId != ShipId)
            CancelShipDocking(MemberId, PassThrough, false);
    }
}

// FUN_06f67740.
void __cdecl RelocateShip(unsigned int const* ShipId, unsigned int const* SystemId,
                          Vector const* Position, Matrix const* Orientation)
{
    pub::SpaceObj::Relocate(*ShipId, *SystemId, *Position, *Orientation);
    if (*SystemId == 0)
        CancelShipDocking(*ShipId, 0, false);
    UpdateDockQueues();
}

// FUN_06f682c0 (1/32768 = DAT_06fb4448). x87: unordered takes the random pick.
float __cdecl SetObjectAttitude(unsigned int ObjA, unsigned int ObjB, float Lo, float Hi)
{
    float Result = -2.0f;
    int RepA;
    int const ResultA = pub::SpaceObj::GetRep(ObjA, RepA);
    int RepB;
    int const ResultB = pub::SpaceObj::GetRep(ObjB, RepB);
    if (ResultA != 0 || ResultB != 0)
        return Result;
    Result = Lo;
    if (!(Lo == Hi))
    {
        double const Random = static_cast<double>(rand()) * (1.0 / 32768.0);
        Result = static_cast<float>(Random * (static_cast<double>(Hi) - Lo) + Lo);
    }
    pub::Reputation::SetAttitude(RepA, RepB, Result);
    return Result;
}

// FUN_06f6c250. A docking entry times out after 90 s (DAT_06fbbd20): a gone ship is cancelled
// by id, a leader or formation-less ship cancels its directive and docks again, a follower
// gets nothing. The kicks run after the list walk.
void __cdecl UpdateDockQueues()
{
    std::vector<FDockKick> Kicks;

    std::list<FDockQueueEntry>::iterator It = GDockingShips.begin();
    while (It != GDockingShips.end())
    {
        FDockQueueEntry& Entry = *It;
        if (!Entry.bNoTimeout && !Entry.bFinishOnClear)
        {
            double Now = GetContentGameTime();
            // x87: unordered does not time out.
            if (GetContentTimeDistance(&Now, &Entry.Time) > 90.0)
            {
                FSystemInfo* System = FindSystemInfo(&Entry.SystemId);
                FContentShipBase* Ship =
                    static_cast<FContentShipBase*>(System->FindShip(Entry.ShipId));
                if (Ship == nullptr)
                {
                    FDockKick Kick;
                    Kick.Ship = nullptr;
                    Kick.ShipId = Entry.ShipId;
                    Kick.DockTargetId = Entry.DockTargetId;
                    Kick.DockIndex = Entry.DockIndex;
                    Kick.bRedock = false;
                    Kicks.push_back(Kick);
                }
                else if (Ship->FormationSlot == 0 || Ship->FormationId == 0xffffffffu)
                {
                    FDockKick Kick;
                    Kick.Ship = Ship;
                    Kick.ShipId = 0;   // +0x04 left unwritten by retail
                    Kick.DockTargetId = Entry.DockTargetId;
                    Kick.DockIndex = Entry.DockIndex;
                    Kick.bRedock = true;
                    Kicks.push_back(Kick);
                }
                ++It;
                continue;
            }
        }

        if (IsShipAtDockPoint(Entry.SystemId, &Entry.ShipId, &Entry.DockTargetId, &Entry.DockIndex))
        {
            MarkDockTerminal(Entry.DockTargetId, Entry.DockIndex, true);
            ++It;
            continue;
        }
        if (Entry.bFinishOnClear)
        {
            FinishDocking(&It);
            continue;
        }
        MarkDockTerminal(Entry.DockTargetId, Entry.DockIndex, false);
        ++It;
    }

    for (int Index = 0; Index < static_cast<int>(Kicks.size()); ++Index)
    {
        FDockKick& Kick = Kicks[Index];
        if (Kick.Ship == nullptr)
        {
            CancelShipDocking(Kick.ShipId, Kick.DockTargetId, false);
            continue;
        }
        pub::AI::DirectiveCancelOp Cancel;
        pub::AI::SubmitDirective(Kick.Ship->SpaceObjId, &Cancel);
        if (Kick.bRedock)
        {
            pub::AI::DirectiveDockOp Dock;
            Dock.DockObjectId = Kick.DockTargetId;
            pub::AI::SubmitDirective(Kick.Ship->SpaceObjId, &Dock);
        }
    }
}

// FUN_06f6e9d0. The launch-queue lookup is map operator[] (it inserts an empty queue). A
// granted gate / airlock / jump hole (type & 0xc40) takes the followers along, any other
// target breaks the formation up; response 3 keeps the waiting entry.
void __cdecl FinishDocking(std::list<FDockQueueEntry>::iterator* It)
{
    FDockQueueEntry& Finished = **It;
    if (GDockLaunchQueues[Finished.DockTargetId].empty())
        MarkDockTerminal(Finished.DockTargetId, Finished.DockIndex, false);
    *It = GDockingShips.erase(*It);

    std::list<FDockQueueEntry>::iterator Wait = GDockWaitingShips.begin();
    while (Wait != GDockWaitingShips.end())
    {
        int DockIndex = -1;
        int Response = DOCK_HOST_RESPONSE_5;
        unsigned int TargetType = 0;
        if (pub::SpaceObj::GetType(Wait->DockTargetId, TargetType) == 0)
        {
            if ((TargetType & 0x20) != 0)
                DockIndex = Wait->bFinishOnClear ? 1 : 0;
            Response = TryStartDocking(Wait->SystemId, &Wait->ShipId, &Wait->DockTargetId,
                                       &DockIndex, Wait->bNoTimeout);
            if (Response == DOCK_HOST_RESPONSE_4)
            {
                // The radio chatter of a granted dock (DAT_06fd1c08, FUN_06ee8d70; its float
                // result is dropped). System is not null-tested before FindShip.
                FGcsDockGrantedEvent Event;
                Event.System = FindSystemInfo(&Wait->SystemId);
                if (DockIndex != -1)
                {
                    pub::SpaceObj::TerminalInfo Info;
                    pub::SpaceObj::GetTerminalInfo(Wait->DockTargetId, DockIndex, Info);
                    Event.DockType = Info.Value;
                }
                else
                {
                    FContentShipBase* Ship =
                        static_cast<FContentShipBase*>(Event.System->FindShip(Wait->ShipId));
                    Event.DockType = static_cast<unsigned int>(
                        GetDockTypeForClasses(&Wait->DockTargetId, &Ship->ClassIds));
                }
                Event.Speaker = Wait->DockTargetId;
                Event.Listener = Wait->ShipId;
                Event.DockIndex = DockIndex;
                Event.Response = DOCK_HOST_RESPONSE_4;
                Event.bWaitedInQueue = true;
                GGcsConversations.QueueConversation(&Event);
                unsigned int DockTargetType = 0;
                pub::SpaceObj::GetType(Wait->DockTargetId, DockTargetType);
                pub::SpaceObj::Dock(Wait->ShipId, Wait->DockTargetId, DockIndex, DOCK_HOST_RESPONSE_4);
                if ((DockTargetType & 0xc40) == 0)
                    BreakFormationAtDock(&Wait->SystemId, &Wait->ShipId, false, true);
                else
                {
                    DockFormationFollowers(&Wait->SystemId, &Wait->ShipId, &Wait->DockTargetId,
                                           &DockIndex);
                }
                Wait = GDockWaitingShips.erase(Wait);
                continue;
            }
        }

        switch (Response)
        {
        case 0:
        case 1:
        case 2:
        case 5:
            pub::SpaceObj::Dock(Wait->ShipId, Wait->DockTargetId, -1,
                                static_cast<DOCK_HOST_RESPONSE>(Response));
            Wait = GDockWaitingShips.erase(Wait);
            break;
        case 3:
            ++Wait;
            break;
        default:
            // Retail does not advance here (unreachable: TryStartDocking returns 1..5).
            break;
        }
    }
}

// FUN_06f6d820. FindSystemInfo is not null-tested.
int __cdecl TryStartDocking(unsigned int SystemId, unsigned int const* ShipId,
                            unsigned int const* DockTargetId, int* DockIndex, bool bNoTimeout)
{
    FSystemInfo* System = FindSystemInfo(&SystemId);
    FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(*ShipId));
    if (Ship == nullptr)
        return DOCK_HOST_RESPONSE_5;

    Vector Position;
    Ship->GetPosition(&Position);
    int ShipRep = Ship->ReputationHandle->GetReputation();
    int Response = CheckDockPermission(&Ship->ClassIds, &ShipRep, ShipId, &Position,
                                       DockTargetId, DockIndex);
    if (Response != DOCK_HOST_RESPONSE_4)
        return Response;

    FDockQueueEntry Entry;
    Entry.Time = GetContentGameTime();
    Entry.SystemId = SystemId;
    Entry.ShipId = Ship->SpaceObjId;
    Entry.DockTargetId = *DockTargetId;
    Entry.DockIndex = *DockIndex;
    Entry.bFinishOnClear = false;
    Entry.bNoTimeout = bNoTimeout;
    GDockingShips.push_back(Entry);
    return Response;
}

// FUN_06f6c920 (CanDockAt = FUN_06f47d10). x87: an unordered attitude refuses. The chosen
// terminal's TerminalInfo is fetched but unused; the motion threshold 0.01 is DAT_06fb7a88.
int __cdecl CheckDockPermission(std::vector<unsigned int> const* ClassIds, int const* ShipRep,
                                unsigned int const* ShipId, Vector const* Position,
                                unsigned int const* DockTargetId, int* DockIndex)
{
    FContentPlayerState* Player = FindContentPlayerStateByShip(ShipId);
    bool bPlayerMayDock = true;
    if (Player != nullptr)
        bPlayerMayDock = Player->CanDockAt(*DockTargetId);

    if (IsGateLockedForShip(*ShipId, *DockTargetId) || (Player != nullptr && !bPlayerMayDock))
    {
        if (Player != nullptr)
        {
            unsigned int MessageId;
            if (Player->GetLiveMission() != 0 && !bPlayerMayDock)
                MessageId = pub::GetNicknameId("dock_disallowed");
            else
                MessageId = pub::GetNicknameId("info_access_denied");
            pub::Player::SendNNMessage(Player->PlayerId, MessageId);
        }
        *DockIndex = -1;
        return DOCK_HOST_RESPONSE_1;
    }

    unsigned int TargetType = 0;
    pub::SpaceObj::GetType(*DockTargetId, TargetType);
    if ((TargetType & 0xc40) == 0)
    {
        float Attitude = 1.0f;
        if (*ShipRep != -1)
        {
            int TargetRep = 0;
            pub::SpaceObj::GetRep(*DockTargetId, TargetRep);
            pub::Reputation::GetAttitude(TargetRep, *ShipRep, Attitude);
        }
        if (!(Attitude > GDockHostileAttitude))
        {
            *DockIndex = -1;
            if (Player != nullptr)
                pub::Player::SendNNMessage(Player->PlayerId,
                                           pub::GetNicknameId("nnv_dock_denied_destination_hostile"));
            return DOCK_HOST_RESPONSE_0;
        }
    }

    DropPlayersLockedOutOfGate(ShipId, DockTargetId);
    int Response = ChooseDockTerminal(ClassIds, DockTargetId, Position, DockIndex);
    if (Response != DOCK_HOST_RESPONSE_4)
        return Response;

    pub::SpaceObj::TerminalInfo Info;
    pub::SpaceObj::GetTerminalInfo(*DockTargetId, *DockIndex, Info);
    Vector Linear;
    Vector Angular;
    if (pub::SpaceObj::GetMotion(*DockTargetId, Linear, Angular) != 0)
        return DOCK_HOST_RESPONSE_4;
    double LinearSpeed = std::sqrt(static_cast<double>(Linear.y) * Linear.y +
                                   static_cast<double>(Linear.z) * Linear.z +
                                   static_cast<double>(Linear.x) * Linear.x);
    if (LinearSpeed > 0.01)
        return DOCK_HOST_RESPONSE_3;
    double AngularSpeed = std::sqrt(static_cast<double>(Angular.y) * Angular.y +
                                    static_cast<double>(Angular.z) * Angular.z +
                                    static_cast<double>(Angular.x) * Angular.x);
    if (AngularSpeed > 0.01)
        return DOCK_HOST_RESPONSE_3;
    return DOCK_HOST_RESPONSE_4;
}

// FUN_06f6c580. With index -1 the terminals are walked until GetTerminalInfo fails (failing at
// 0 -> 5); none fitting -> 2, none free -> 3. The nearest pick needs a strictly smaller squared
// distance (GetHardpoint results not tested); without a Position a random candidate
// (rand() * n / 32768, no rand() for one). Every refusal also stores -1.
int __cdecl ChooseDockTerminal(std::vector<unsigned int> const* ClassIds,
                               unsigned int const* DockTargetId, Vector const* Position,
                               int* DockIndex)
{
    std::list<int> Candidates;
    pub::SpaceObj::TerminalInfo Info;

    if (*DockIndex == -1)
    {
        int Index = 0;
        if (pub::SpaceObj::GetTerminalInfo(*DockTargetId, 0, Info) != 0)
        {
            *DockIndex = -1;
            return DOCK_HOST_RESPONSE_5;
        }
        bool bAnyFits = false;
        do
        {
            if (DoesTerminalFitClasses(Info.Value, ClassIds))
            {
                bAnyFits = true;
                if (!IsDockTerminalInUse(*DockTargetId, Index))
                    Candidates.push_back(Index);
            }
            ++Index;
        } while (pub::SpaceObj::GetTerminalInfo(*DockTargetId, Index, Info) == 0);
        if (!bAnyFits)
        {
            *DockIndex = -1;
            return DOCK_HOST_RESPONSE_2;
        }
    }
    else
    {
        if (pub::SpaceObj::GetTerminalInfo(*DockTargetId, *DockIndex, Info) != 0)
        {
            *DockIndex = -1;
            return DOCK_HOST_RESPONSE_5;
        }
        if (!DoesTerminalFitClasses(Info.Value, ClassIds))
        {
            *DockIndex = -1;
            return DOCK_HOST_RESPONSE_2;
        }
        if (!IsDockTerminalInUse(*DockTargetId, *DockIndex))
            Candidates.push_back(*DockIndex);
    }

    if (Candidates.empty())
    {
        *DockIndex = -1;
        return DOCK_HOST_RESPONSE_3;
    }

    if (*DockIndex == -1 && Position != nullptr)
    {
        int BestIndex = -1;
        float BestDistance = FLT_MAX;
        // Retail keeps one local across the loop and overwrites it with the difference; a
        // failed GetHardpoint (Common 0630ef30 writes nothing) leaves the previous difference
        // in it, as here. Deviation: before the first call the retail slot is stack residue;
        // the port starts it at zero.
        Vector Work;
        Work.x = 0.0f;
        Work.y = 0.0f;
        Work.z = 0.0f;
        for (std::list<int>::iterator It = Candidates.begin(); It != Candidates.end(); ++It)
        {
            pub::SpaceObj::GetTerminalInfo(*DockTargetId, *It, Info);
            pub::SpaceObj::GetHardpoint(*DockTargetId, Info.Name, &Work, nullptr);
            Work.x = Work.x - Position->x;
            Work.y = Work.y - Position->y;
            Work.z = Work.z - Position->z;
            // x87 sum order (x*x + z*z) + y*y.
            double Distance = static_cast<double>(Work.x) * Work.x +
                              static_cast<double>(Work.z) * Work.z +
                              static_cast<double>(Work.y) * Work.y;
            if (Distance < BestDistance)
            {
                BestIndex = *It;
                BestDistance = static_cast<float>(Distance);
            }
        }
        *DockIndex = BestIndex;
        return DOCK_HOST_RESPONSE_4;
    }

    int Count = static_cast<int>(Candidates.size());
    int Pick = 0;
    if (Count - 1 != 0)
        Pick = rand() * Count / 32768;
    std::list<int>::iterator Chosen = Candidates.begin();
    for (; Pick != 0; --Pick)
        ++Chosen;
    *DockIndex = *Chosen;
    return DOCK_HOST_RESPONSE_4;
}

// FUN_06f6bdd0. Constants: 40000 DAT_06fbbcfc, 6250000 DAT_06fbbd00, 250000 DAT_06fbbd04, 10
// DAT_06fbbd18, 25 / 5000 DAT_06fbbd10 / DAT_06fbbd08, 1.85 DAT_06fbbcf8. A berth (6) also
// accepts a ship on the segment from the hardpoint to 5000 behind it. x87: every unordered
// compare is false.
bool __cdecl IsShipAtDockPoint(unsigned int SystemId, unsigned int const* ShipId,
                               unsigned int const* DockTargetId, int const* DockIndex)
{
    pub::SpaceObj::TerminalInfo Info;
    if (pub::SpaceObj::GetTerminalInfo(*DockTargetId, *DockIndex, Info) != 0)
        return false;
    Vector Hardpoint;
    Matrix HardpointOrientation;
    if (pub::SpaceObj::GetHardpoint(*DockTargetId, Info.Name, &Hardpoint, &HardpointOrientation) != 0)
        return false;
    FSystemInfo* System = FindSystemInfo(&SystemId);
    if (System->FindShip(*ShipId) == nullptr)
        return false;
    Vector ShipPosition;
    Matrix ShipOrientation;
    if (pub::SpaceObj::GetLocation(*ShipId, ShipPosition, ShipOrientation) != 0)
        return false;

    switch (Info.Value)
    {
    case 1:
    case 3:
    case 4:
    case 5:
    {
        // The one call of FUN_06eeaee0 here; x87 sum order (z*z + y*y) + x*x.
        Vector const Delta = SubtractVectors(ShipPosition, Hardpoint);
        return (static_cast<double>(Delta.z) * Delta.z + static_cast<double>(Delta.y) * Delta.y) +
                   static_cast<double>(Delta.x) * Delta.x < 40000.0f;
    }
    case 7:
        return DistanceSquared(ShipPosition, Hardpoint) < 6250000.0f;
    case 8:
        return DistanceSquared(ShipPosition, Hardpoint) < 250000.0f;
    case 6:
        break;
    default:
        return false;
    }

    if (std::fabs(static_cast<double>(Hardpoint.x) - ShipPosition.x) < 10.0 &&
        std::fabs(static_cast<double>(Hardpoint.z) - ShipPosition.z) < 10.0)
        return true;

    FTransform Transform;
    std::memcpy(&Transform.Rotation, &HardpointOrientation, sizeof(Transform.Rotation));
    Transform.Translation.X = Hardpoint.x;
    Transform.Translation.Y = Hardpoint.y;
    Transform.Translation.Z = Hardpoint.z;
    FVector Back;
    Back.X = 0.0f;
    Back.Y = 0.0f;
    Back.Z = -5000.0f;
    FVector FarPoint;
    ContentMathEngine()->TransformXform(FarPoint, Transform, Back);
    Vector Far;
    Far.x = FarPoint.X;
    Far.y = FarPoint.Y;
    Far.z = FarPoint.Z;

    double ToFarSquare = DistanceSquared(ShipPosition, Far);
    float ToFarStored = static_cast<float>(ToFarSquare);
    double ToHardpointSquare = DistanceSquared(ShipPosition, Hardpoint);
    double ToHardpoint = ApproximateSqrt(static_cast<float>(ToHardpointSquare), ToHardpointSquare);
    double ToFar = ApproximateSqrt(ToFarStored, ToFarStored);
    if (std::fabs(5000.0f - (ToFar + ToHardpoint)) < 25.0)
        return true;

    float Radius = 0.0f;
    Vector Center;
    pub::SpaceObj::GetRadius(*DockTargetId, Radius, Center);
    // Unlike FUN_06eb7810, the three differences are not rounded to float here.
    double const DeltaX = static_cast<double>(ShipPosition.x) - Hardpoint.x;
    double const DeltaY = static_cast<double>(ShipPosition.y) - Hardpoint.y;
    double const DeltaZ = static_cast<double>(ShipPosition.z) - Hardpoint.z;
    double DistanceSquare = DeltaZ * DeltaZ + DeltaY * DeltaY + DeltaX * DeltaX;
    double Distance = ApproximateSqrt(static_cast<float>(DistanceSquare), DistanceSquare);
    return static_cast<double>(Radius) * 1.85f > Distance;
}

// FUN_06f6e420. GetType's result is not tested; Server's GetType zeroes the type first, so a
// failure reads 0.
int __cdecl GetDockTypeForClasses(unsigned int const* DockTargetId,
                                  std::vector<unsigned int> const* ClassIds)
{
    unsigned int TargetType = 0;
    pub::SpaceObj::GetType(*DockTargetId, TargetType);
    if ((TargetType & 0x840) != 0)
        return 7;
    if ((TargetType & 0x400) != 0)
        return 8;
    if ((TargetType & 0x20) != 0)
        return ContainsClass(ClassIds, GIdCanUseBerths) ? 6 : 0;
    if (ContainsClass(ClassIds, GIdCanUseBerths))
        return 1;
    if (ContainsClass(ClassIds, GIdCanUseSmallMoors))
        return 3;
    if (ContainsClass(ClassIds, GIdCanUseMediumMoors))
        return 4;
    return ContainsClass(ClassIds, GIdCanUseLargeMoors) ? 5 : 0;
}

// FUN_06f6dd80. FindSystemInfo / FindShip are not null-tested. Element 0 (the sorted leader)
// is skipped.
void __cdecl DockFormationFollowers(unsigned int const* SystemId, unsigned int const* ShipId,
                                    unsigned int const* DockTargetId, int const* DockIndex)
{
    FSystemInfo* System = FindSystemInfo(SystemId);
    FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(*ShipId));
    std::vector<FContentShipBase*> Members;
    CollectFormationMembers(&Members, Ship->FormationId);
    for (int Index = 1; Index < static_cast<int>(Members.size()); ++Index)
    {
        FDockQueueEntry Entry;
        Entry.Time = GetContentGameTime();
        Entry.SystemId = *SystemId;
        Entry.ShipId = Members[Index]->SpaceObjId;
        Entry.DockTargetId = *DockTargetId;
        Entry.DockIndex = *DockIndex;
        Entry.bFinishOnClear = false;
        Entry.bNoTimeout = false;
        GDockingShips.push_back(Entry);
        unsigned int MemberId = Members[Index]->SpaceObjId;
        pub::SpaceObj::Dock(MemberId, *DockTargetId, *DockIndex, DOCK_HOST_RESPONSE_4);
    }
}

// FUN_06f6df00. The ship is not null-tested.
bool __cdecl IsPlayerInShipFormation(FSystemInfo* System, unsigned int ShipId)
{
    if (FindContentPlayerStateByShip(&ShipId) != nullptr)
        return true;
    std::vector<FContentShipBase*> Members;
    FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(ShipId));
    CollectFormationMembers(&Members, Ship->FormationId);
    for (std::vector<FContentShipBase*>::iterator It = Members.begin(); It != Members.end(); ++It)
    {
        unsigned int MemberId = (*It)->SpaceObjId;
        if (FindContentPlayerStateByShip(&MemberId) != nullptr)
            return true;
    }
    return false;
}

// FUN_06f6ed10. The leader is not null-tested, nor GetTerminalInfo's status.
void __cdecl FollowLeaderIntoDock(FContentShipBase* Ship)
{
    if (Ship->FormationSlot == 0 || Ship->FormationId == 0xffffffffu)
        return;
    FContentShipBase* Leader = FindFormationLeader(Ship->FormationId);
    unsigned int const LeaderId = Leader->SpaceObjId;
    std::list<FDockQueueEntry>::iterator It = GDockingShips.begin();
    while (It != GDockingShips.end() && It->ShipId != LeaderId)
        ++It;
    if (It == GDockingShips.end())
        return;

    unsigned int DockTargetId = It->DockTargetId;
    int const DockIndex = It->DockIndex;
    pub::SpaceObj::TerminalInfo Info;
    pub::SpaceObj::GetTerminalInfo(It->DockTargetId, DockIndex, Info);
    if (Info.Value != 7 && Info.Value != 8)
        return;

    FDockQueueEntry Entry;
    Entry.Time = GetContentGameTime();
    Entry.SystemId = Ship->GetSystem()->SystemId;
    Entry.ShipId = Ship->SpaceObjId;
    Entry.DockTargetId = DockTargetId;
    Entry.DockIndex = DockIndex;
    Entry.bFinishOnClear = false;
    Entry.bNoTimeout = false;
    GDockingShips.push_back(Entry);
    unsigned int ShipId = Ship->SpaceObjId;
    pub::SpaceObj::Dock(ShipId, DockTargetId, DockIndex, DOCK_HOST_RESPONSE_4);
}

// FUN_06f66690 (FindFormation = FUN_06efd1a0). Two paths: single player or NPC-only uses
// freighter_basic and a player / non-player slot match; multiplayer with a player uses
// multiplayer_freighter and moves the ship's whole formation when it leads one.
// Retail quirks kept: the leader formation's element 0 is read without an empty test; in the
// multiplayer path the members list is not refreshed per joiner, so several joiners can get
// the same slot (a full formation -> -5 at that joiner).
int __cdecl HandleFormationRequest(unsigned int const* ShipId, unsigned int const* LeaderId,
                                   FSystemInfo* System)
{
    FContentShipBase* Leader = static_cast<FContentShipBase*>(System->FindShip(*LeaderId));
    if (Leader == nullptr)
        return -4;
    FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(*ShipId));
    if (Ship == nullptr)
        return -3;

    unsigned int LeaderFormation = Leader->FormationId;
    unsigned int const ShipFormation = Ship->FormationId;
    FContentShipBase* const ShipLeader = Ship->GetFormationLeader();
    FContentShipBase* const LeaderLeader = Leader->GetFormationLeader();
    FContentPlayerState* const ShipPlayer = FindContentPlayerStateByShip(ShipId);
    FContentPlayerState* const LeaderPlayer = FindContentPlayerStateByShip(LeaderId);
    bool const bPlayerRules =
        !pub::SinglePlayer() && (ShipPlayer != nullptr || LeaderPlayer != nullptr);

    unsigned int FormationName = CreateID("multiplayer_freighter");
    FFormation* const MultiplayerFreighter = FindFormation(&FormationName);
    FormationName = CreateID("freighter_basic");
    FFormation* const FreighterBasic = FindFormation(&FormationName);
    if (MultiplayerFreighter == nullptr || FreighterBasic == nullptr)
        return -8;

    if (!bPlayerRules)
    {
        if (LeaderFormation == 0xffffffffu)
        {
            LeaderFormation = AllocateFormationId();
            Leader->SetFormation(LeaderFormation, 0, FreighterBasic);
            ResetFormationState(Leader);
        }
        if (ShipFormation == LeaderFormation)
            return -1;

        std::vector<FContentShipBase*> Members;
        CollectFormationMembers(&Members, LeaderFormation);
        FContentShipBase* const First = *Members.data();     // no empty test
        if (First->FormationSlot != 0 && First->FormationId != 0xffffffffu)
            return -8;

        FContentPlayerState* const Player = FindContentPlayerStateByShip(ShipId);
        unsigned int DockTargetId = 0;
        unsigned int FirstId = First->SpaceObjId;
        int DockIndex = 0;
        GetShipDockingTarget(&FirstId, &DockTargetId, &DockIndex, nullptr);
        if ((DockTargetId & 0x3fffffff) != 0)
        {
            unsigned int TargetType = 0;
            pub::SpaceObj::GetType(DockTargetId, TargetType);
            if (Player != nullptr)
            {
                unsigned int PlayerId = Player->PlayerId;
                if (IsContentGateLocked(&PlayerId, &DockTargetId) || (TargetType & 0x120) != 0)
                    return -9;
            }
        }

        FFormation* const Formation = First->Formation;
        int const SlotCount = static_cast<int>(Formation->Slots.size());
        for (int Slot = 0; Slot < SlotCount; ++Slot)
        {
            bool bTaken = false;
            for (std::vector<FContentShipBase*>::iterator It = Members.begin(); It != Members.end();
                 ++It)
            {
                if ((*It)->FormationSlot == static_cast<unsigned int>(Slot))
                {
                    bTaken = true;
                    break;
                }
            }
            if (bTaken)
                continue;
            bool const bPlayerShip = FindContentPlayerStateByShip(ShipId) != nullptr;
            bool const bPlayerSlot = Formation->IsPlayerSlot(Slot);
            if (bPlayerShip != bPlayerSlot)
                continue;
            Ship->SetFormation(First->FormationId, static_cast<unsigned int>(Slot), Formation);
            return 0;
        }
        return -5;
    }

    if (ShipPlayer != nullptr && LeaderPlayer != nullptr &&
        !ShipPlayer->IsGroupMember(LeaderPlayer->PlayerId))
        return -8;
    if (ShipLeader != nullptr && ShipLeader == LeaderLeader)
        return -1;

    FormationName = CreateID("multiplayer_freighter");
    FFormation* const Formation = FindFormation(&FormationName);
    if (LeaderFormation == 0xffffffffu)
    {
        LeaderFormation = AllocateFormationId();
        Leader->SetFormation(LeaderFormation, 0, Formation);
        ResetFormationState(Leader);
    }

    std::vector<FContentShipBase*> Joining;
    if (ShipLeader == Ship && ShipFormation != 0xffffffffu)
    {
        CollectFormationMembers(&Joining, ShipFormation);
        for (std::vector<FContentShipBase*>::iterator It = Joining.begin(); It != Joining.end(); ++It)
        {
            unsigned int const MemberId = (*It)->SpaceObjId;
            FContentShipBase* Member =
                static_cast<FContentShipBase*>((*It)->GetSystem()->FindShip(MemberId));
            if (Member == nullptr || Member->FormationId == 0xffffffffu)
                continue;
            CancelDockingBehindLeader(Member, Member->FormationId);
            Member->SetFormation(0xffffffffu, 0, nullptr);
        }
    }
    else
    {
        Joining.push_back(Ship);
    }

    std::vector<FContentShipBase*> Members;
    CollectFormationMembers(&Members, LeaderFormation);
    int const SlotCount = static_cast<int>(Formation->Slots.size());
    for (std::vector<FContentShipBase*>::iterator Join = Joining.begin(); Join != Joining.end();
         ++Join)
    {
        int Slot = 0;
        if (SlotCount <= 0)
            return -5;
        for (;;)
        {
            bool bTaken = false;
            for (std::vector<FContentShipBase*>::iterator It = Members.begin(); It != Members.end();
                 ++It)
            {
                if ((*It)->FormationSlot == static_cast<unsigned int>(Slot))
                {
                    bTaken = true;
                    break;
                }
            }
            if (!bTaken)
                break;
            ++Slot;
            if (Slot >= SlotCount)
                return -5;
        }
        (*Join)->SetFormation(LeaderFormation, static_cast<unsigned int>(Slot), Formation);
        ResetFormationState(*Join);
    }
    return 0;
}

// FUN_06f6e110. GetType's status is not tested; with index -1 FindShip is not null-tested.
// On 3 a player's ship, a ship with a player in its formation, or a space object of creation
// kind 0 / 1 (+0x1b4 read whatever the object is) queues behind the players already waiting;
// any other ship goes to the end.
int HandleDockRequest(FDockRequest const* Request)
{
    unsigned int TargetType = 0;
    pub::SpaceObj::GetType(Request->DockTargetId, TargetType);
    int DockIndex = (TargetType & 0x20) != 0 ? 0 : -1;
    int const Response = TryStartDocking(Request->SystemId, &Request->ShipId,
                                         &Request->DockTargetId, &DockIndex,
                                         Request->bNoTimeout);
    FSystemInfo* System = FindSystemInfo(&Request->SystemId);

    if (Response != DOCK_HOST_RESPONSE_5)
    {
        FGcsDockGrantedEvent Event;
        Event.System = System;
        if (DockIndex != -1)
        {
            pub::SpaceObj::TerminalInfo Info;
            pub::SpaceObj::GetTerminalInfo(Request->DockTargetId, DockIndex, Info);
            Event.DockType = Info.Value;
        }
        else
        {
            FContentShipBase* Ship =
                static_cast<FContentShipBase*>(System->FindShip(Request->ShipId));
            Event.DockType = static_cast<unsigned int>(
                GetDockTypeForClasses(&Request->DockTargetId, &Ship->ClassIds));
        }
        Event.Speaker = Request->DockTargetId;
        Event.Listener = Request->ShipId;
        Event.DockIndex = DockIndex;
        Event.Response = Response;
        GGcsConversations.QueueConversation(&Event);
    }

    if (Response == DOCK_HOST_RESPONSE_4)
    {
        unsigned int DockTargetType = 0;
        pub::SpaceObj::GetType(Request->DockTargetId, DockTargetType);
        pub::SpaceObj::Dock(Request->ShipId, Request->DockTargetId, DockIndex,
                            DOCK_HOST_RESPONSE_4);
        if ((DockTargetType & 0xc40) != 0)
        {
            DockFormationFollowers(&Request->SystemId, &Request->ShipId, &Request->DockTargetId,
                                   &DockIndex);
        }
        else
        {
            bool const bReleasePlayers = (DockTargetType & 0x120) != 0;
            BreakFormationAtDock(&Request->SystemId, &Request->ShipId, false,
                                 bReleasePlayers);
        }
        return Response;
    }

    if (Response == DOCK_HOST_RESPONSE_3)
    {
        FDockQueueEntry Entry;
        Entry.Time = GetContentGameTime();
        Entry.SystemId = Request->SystemId;
        Entry.ShipId = Request->ShipId;
        Entry.DockTargetId = Request->DockTargetId;
        Entry.DockIndex = DockIndex;
        Entry.bFinishOnClear = false;
        Entry.bNoTimeout = false;

        bool bBehindPlayers = FindContentPlayerStateByShip(&Entry.ShipId) != nullptr ||
                              IsPlayerInShipFormation(System, Entry.ShipId);
        if (!bBehindPlayers)
        {
            FContentNpcShip* Object =
                static_cast<FContentNpcShip*>(System->FindSpaceObj(Entry.ShipId));
            bBehindPlayers = Object != nullptr &&
                             (Object->CreationKind == 0 || Object->CreationKind == 1);
        }
        std::list<FDockQueueEntry>::iterator Position = GDockWaitingShips.end();
        if (bBehindPlayers)
        {
            Position = GDockWaitingShips.begin();
            while (Position != GDockWaitingShips.end() &&
                   FindContentPlayerStateByShip(&Position->ShipId) != nullptr)
                ++Position;
        }
        GDockWaitingShips.insert(Position, Entry);
        pub::SpaceObj::Dock(Request->ShipId, Request->DockTargetId, -1, DOCK_HOST_RESPONSE_3);
        return Response;
    }

    if (Response == DOCK_HOST_RESPONSE_2 || Response == DOCK_HOST_RESPONSE_1 ||
        Response == DOCK_HOST_RESPONSE_0 || Response == DOCK_HOST_RESPONSE_5)
    {
        pub::SpaceObj::Dock(Request->ShipId, Request->DockTargetId, DockIndex,
                            static_cast<DOCK_HOST_RESPONSE>(Response));
    }
    return Response;
}

// FUN_06f6d920. The system is not null-tested, nor the player state of a non-NPC member.
// Deviations: retail reads the leader's guide at +0x178 whatever the ship is (for a player's
// ship the low dword of CalmSince); the port reads a guide from NPC ships only. Type 0x13 also
// names InstantTradelane directives (GetType ICF-shared): retail copies their bytes (and the
// bytes past their end) as a DockOp; the port copies Dock directives only.
void __cdecl BreakFormationAtDock(unsigned int const* SystemId, unsigned int const* ShipId,
                                  bool bRedirectPlayers, bool bReleasePlayers)
{
    std::vector<FContentShipBase*> Members;
    FSystemInfo* System = FindSystemInfo(SystemId);
    FContentShipBase* Leader = static_cast<FContentShipBase*>(System->FindShip(*ShipId));
    if (Leader == nullptr)
        return;
    if (Leader->FormationSlot != 0 && Leader->FormationId != 0xffffffffu)
        return;
    CollectFormationMembers(&Members, Leader->FormationId);
    if (Members.size() <= 1)
        return;

    std::vector<FContentShipBase*> NpcMembers;
    std::vector<FContentShipBase*> PlayerMembers;
    for (int Index = 1; Index < static_cast<int>(Members.size()); ++Index)
    {
        FContentSpaceObj* Object = System->FindSpaceObj(Members[Index]->SpaceObjId);
        if (Object != nullptr)
        {
            NpcMembers.push_back(static_cast<FContentShipBase*>(Object));
            continue;
        }
        unsigned int MemberId = Members[Index]->SpaceObjId;
        PlayerMembers.push_back(FindContentPlayerStateByShip(&MemberId)->ShipObject);
    }

    for (int Index = 0; Index < static_cast<int>(NpcMembers.size()); ++Index)
    {
        FContentNpcShip* Member = static_cast<FContentNpcShip*>(NpcMembers[Index]);
        RemoveFromFormation(Member);
        Member->FollowDirectivesOf(Leader);
        FContentGuide* LeaderGuide =
            Leader->IsNpcShip() ? static_cast<FContentNpcShip*>(Leader)->GetGuide() : nullptr;
        if (LeaderGuide == nullptr)
            continue;
        Member->SetGuide(LeaderGuide->Clone(), true, false);
        Member->GetGuide()->Release();
    }

    if (!bReleasePlayers && !bRedirectPlayers)
        return;
    for (int Index = 0; Index < static_cast<int>(PlayerMembers.size()); ++Index)
        RemoveFromFormation(PlayerMembers[Index]);
    if (!bRedirectPlayers || PlayerMembers.empty())
        return;

    for (int Index = 0; Index < static_cast<int>(PlayerMembers.size()); ++Index)
    {
        FContentShipBase* Player = PlayerMembers[Index];
        // The copy lives in a list that owns its references (retail unwind FUN_06ecfa90).
        FMissionDirectiveList Directives;
        Directives.Items = Leader->CopyDirectives();
        std::list<FDockQueueEntry>::iterator Entry =
            FindDockQueueEntry(&GDockingShips, Player->SpaceObjId);
        if (!Directives.Items.empty())
        {
            FObjListDock* Dock = Directives.Items.front()->GetType() == 0x13
                                     ? dynamic_cast<FObjListDock*>(Directives.Items.front())
                                     : nullptr;
            if (Dock != nullptr)
            {
                pub::AI::DirectiveDockOp Op(Dock->Op);
                if (Entry != GDockingShips.end())
                {
                    Op.DockObjectId = Entry->DockTargetId;
                    Op.bHasDockPortIndex = 1;
                    Op.DockPortIndex = Entry->DockIndex;
                }
                pub::AI::SubmitDirective(Player->SpaceObjId, &Op);
            }
        }
        else if (Entry != GDockingShips.end())
        {
            pub::AI::DirectiveDockOp Op;
            Op.Priority = 1;
            Op.DockObjectId = Entry->DockTargetId;
            Op.bHasDockPortIndex = 1;
            Op.DockPortIndex = Entry->DockIndex;
            pub::AI::SubmitDirective(Player->SpaceObjId, &Op);
        }
    }
}

// FUN_06f6d040 (the IsContentGateLocked test inlined).
void __cdecl DropPlayersLockedOutOfGate(unsigned int const* ShipId, unsigned int const* DockTargetId)
{
    if ((*ShipId & 0x3fffffffu) == 0)
        return;
    unsigned int SystemId = 0;
    pub::SpaceObj::GetSystem(*ShipId, SystemId);
    FSystemInfo* System = FindSystemInfo(&SystemId);
    if (System == nullptr)
        return;
    FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(*ShipId));
    if (Ship == nullptr || Ship->FormationId == 0xffffffffu)
        return;

    std::vector<FContentShipBase*> Members;
    CollectFormationMembers(&Members, Ship->FormationId);
    for (int Index = 0; Index < static_cast<int>(Members.size()); ++Index)
    {
        unsigned int MemberId = Members[Index]->SpaceObjId;
        FContentPlayerState* Player = FindContentPlayerStateByShip(&MemberId);
        if (Player == nullptr)
            continue;
        if (IsContentGateLocked(&Player->PlayerId, DockTargetId))
            RemoveFromFormation(Members[Index]);
    }
}

// FUN_06f67440. Every object of SpaceObjs is taken as an NPC ship. The radius test keeps the
// differences unrounded; the cone test rounds V, |V|^2, |F|^2, V.F and |V| to float (fast
// rsqrt FUN_06f5aeb0; cos 35 degrees from double DAT_06fbbcc8). x87: unordered compares fail.
int __cdecl CountNpcShipsNear(FSystemInfo* System, FSpaceObjPlacement const* Placement,
                              float const* Radius, float const* ConeRange,
                              unsigned int const* ClassId)
{
    float RadiusSquare = static_cast<float>(static_cast<double>(*Radius) * *Radius);
    int Count = 0;
    for (std::list<FContentSpaceObj*>::iterator It = System->SpaceObjs.begin();
         It != System->SpaceObjs.end(); ++It)
    {
        FContentNpcShip* Ship = static_cast<FContentNpcShip*>(*It);
        if (Ship->bExcludeFromShipCount != 0)
            continue;
        Vector Position;
        Ship->GetPosition(&Position);
        double DX = static_cast<double>(Placement->Position.x) - Position.x;
        double DY = static_cast<double>(Placement->Position.y) - Position.y;
        double DZ = static_cast<double>(Placement->Position.z) - Position.z;
        double DistanceSquare = DZ * DZ + DY * DY + DX * DX;
        if (!(DistanceSquare <= RadiusSquare))
        {
            if (!(*ConeRange > 0.0f))
                continue;
            Vector const V = SubtractVectors(Position, Placement->Position);
            float VX = V.x;
            float VY = V.y;
            float VZ = V.z;
            float VSquare = static_cast<float>(static_cast<double>(VZ) * VZ +
                                               static_cast<double>(VY) * VY +
                                               static_cast<double>(VX) * VX);
            if (!(FastReciprocalSqrt(VSquare) * VSquare <= *ConeRange))
                continue;
            float FX = -Placement->Orientation.E[0][2];
            float FY = -Placement->Orientation.E[1][2];
            float FZ = -Placement->Orientation.E[2][2];
            float FSquare = static_cast<float>(static_cast<double>(FZ) * FZ +
                                               static_cast<double>(FY) * FY +
                                               static_cast<double>(FX) * FX);
            float Dot = static_cast<float>(static_cast<double>(VZ) * FZ +
                                           static_cast<double>(VY) * FY +
                                           static_cast<double>(FX) * VX);
            float VLength = static_cast<float>(FastReciprocalSqrt(VSquare) * VSquare);
            double FLength = FastReciprocalSqrt(FSquare) * FSquare;
            double Cosine = Dot / (FLength * VLength);
            if (!(std::cos(0.6108652334660292) <= Cosine))
                continue;
        }
        if (ClassId != nullptr && !Ship->HasClassId(ClassId))
            continue;
        ++Count;
    }
    return Count;
}

// FUN_06f6cb20. Server's GetDockingTarget zeroes the out value first, so a failed call reads 0.
bool __cdecl IsGroupAtDockBase(unsigned int const* Group, unsigned int const* ObjectId)
{
    unsigned int BaseId = 0;
    pub::SpaceObj::GetDockingTarget(*ObjectId, BaseId);
    if (BaseId == 0)
        return false;
    FContentBase* Base = FindContentBaseInfo(&BaseId);
    return Base->FindFaction(*Group) != nullptr;
}

// FUN_06f6cb90: retail has two copies of one body that differ only in the attitude source.
// x87: an unordered attitude refuses.
bool __cdecl CanUseDockTarget(std::vector<unsigned int> const* ClassIds, int const* ShipRep,
                              unsigned int const* Group, unsigned int const* ShipId,
                              unsigned int const* DockTargetId)
{
    if (IsGateLockedForShip(*ShipId, *DockTargetId))
        return false;

    // Retail keeps the out-values in argument slots it no longer needs, and GetRep's result is
    // not checked: a failed GetRep (it writes only on success) leaves the argument pointer's own
    // value in the slot, and the next call reads it as the rep. The build is Win32 only, so the
    // seeds below carry exactly retail's 32-bit value. GetAffiliation always writes its
    // out-value (-1 on failure).
    float Attitude = 1.0f;
    if (*ShipRep != -1)
    {
        // The target rep lands in the ShipRep argument's slot.
        int TargetRep = static_cast<int>(reinterpret_cast<uintptr_t>(ShipRep));
        pub::SpaceObj::GetRep(*DockTargetId, TargetRep);
        pub::Reputation::GetAttitude(TargetRep, *ShipRep, Attitude);
    }
    else if (*Group != 0xffffffffu)
    {
        // The target rep lands in the Group argument's slot, the affiliation in ShipId's.
        int TargetRep = static_cast<int>(reinterpret_cast<uintptr_t>(Group));
        pub::SpaceObj::GetRep(*DockTargetId, TargetRep);
        unsigned int Affiliation = static_cast<unsigned int>(reinterpret_cast<uintptr_t>(ShipId));
        pub::Reputation::GetAffiliation(TargetRep, Affiliation);
        if (Affiliation != 0xffffffffu)
            pub::Reputation::GetReputation(Affiliation, *Group, Attitude);
    }
    if (!(Attitude > GDockHostileAttitude))
        return false;

    pub::SpaceObj::TerminalInfo Info;
    int Index = 0;
    if (pub::SpaceObj::GetTerminalInfo(*DockTargetId, Index, Info) != 0)
        return false;
    for (;;)
    {
        switch (Info.Value)
        {
        case 7:
        case 8:
            return true;
        default:
            if (DoesTerminalFitClasses(Info.Value, ClassIds))
                return true;
            break;
        }
        ++Index;
        if (pub::SpaceObj::GetTerminalInfo(*DockTargetId, Index, Info) != 0)
            return false;
    }
}

// FUN_06f6bc20.
void __cdecl ApplyDockTerminalStates()
{
    std::vector<unsigned int> OccupiedTargets;
    for (std::vector<FDockTerminal>::iterator Pending = GDockTerminals.begin();
         Pending != GDockTerminals.end(); ++Pending)
    {
        std::vector<FDockTerminal>::iterator Applied = GDockTerminalsApplied.begin();
        for (; Applied != GDockTerminalsApplied.end(); ++Applied)
        {
            if (Applied->DockTargetId == Pending->DockTargetId &&
                Applied->DockIndex == Pending->DockIndex)
                break;
        }

        if (Applied != GDockTerminalsApplied.end())
        {
            if (Applied->bOccupied == Pending->bOccupied)
                continue;
            if (pub::SpaceObj::Activate(Pending->DockTargetId, Pending->bOccupied,
                                        Pending->DockIndex) != 0)
                continue;
            Applied->bOccupied = Pending->bOccupied;
        }
        else
        {
            if (pub::SpaceObj::Activate(Pending->DockTargetId, Pending->bOccupied,
                                        Pending->DockIndex) != 0)
                continue;
            GDockTerminalsApplied.push_back(*Pending);
        }
        if (Pending->bOccupied)
            OccupiedTargets.push_back(Pending->DockTargetId);
    }
    GDockTerminals.clear();

    for (int Index = 0; Index < static_cast<int>(OccupiedTargets.size()); ++Index)
    {
        unsigned int const Controller = 0;
        unsigned int TargetId = OccupiedTargets[Index];
        pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::JumpgateAct, &TargetId);
    }
}

// LAB_06ee9660 (ICF-shared).
int FGcsDockGrantedEvent::GetType() const
{
    return 2;
}

// LAB_06f5e540.
int FGcsLaunchEvent::GetType() const
{
    return 3;
}

// ---------------------------------------------------------------------------------------
// The dock launch queue (DAT_06fd1b84): FUN_06f60340, FUN_06f603d0, FUN_06f607e0.
// ---------------------------------------------------------------------------------------

I3DMathEngine* GetSpaceMathEngine()
{
    return ContentMathEngine();
}

namespace
{
    FVector ToFVector(Vector const& Source)
    {
        FVector Out;
        Out.X = Source.x;
        Out.Y = Source.y;
        Out.Z = Source.z;
        return Out;
    }

    FMatrix ToFMatrix(Matrix const& Source)
    {
        FMatrix Out;
        std::memcpy(&Out, &Source, sizeof(Out));
        return Out;
    }

    // .rdata 06fbbb70: the ship id CheckDockPermission gets for a ship that does not exist
    // yet (0).
    unsigned int const GLaunchNoShipId = 0;

    // Inlined twice in FUN_06f607e0 (no null test). The creating reference belongs to the
    // caller's holder, which releases it.
    void AddLeadingDirective(FContentNpcShipParams* Params, FObjListDirective* Directive)
    {
        Params->Directives.Items.push_front(Directive);
        Directive->AddRef();
    }

    // FUN_06f603d0: where a ship launched from a terminal starts. A docking ring only offsets
    // the position (constants DAT_06fb4448, DAT_06fbbc38, DAT_06fbbc30); otherwise a non-moor
    // terminal turns the orientation half a circle about its own column 1 (pi/2 is double
    // DAT_06fb74e8). Every intermediate sum is stored as float.
    // Retail quirks kept: FindHardpoint's result is not tested (the port's outputs start at
    // zero, retail's keep earlier values); the mount-orientation product is computed into a
    // local that is never read. |column 1| is x87 extended in retail; the port uses double.
    void PlaceLaunchedShip(FContentNpcShipParams* Params, unsigned int const* TargetType,
                           unsigned int TerminalType, Vector const* HardpointPosition,
                           Matrix const* HardpointOrientation)
    {
        if ((*TargetType & 0x20) != 0)
        {
            int Random = rand();
            Vector Offset;
            Offset.x = 0.0f;
            Offset.y = static_cast<float>(static_cast<double>(Random) *
                                              static_cast<double>(3.0517578125e-05f) * 10000.0 +
                                          static_cast<double>(10000.0f));
            Offset.z = 0.0f;
            Params->Position.x = HardpointPosition->x + Offset.x;
            Params->Position.y = HardpointPosition->y + Offset.y;
            Params->Position.z = HardpointPosition->z + Offset.z;
            return;
        }

        Params->Orientation = *HardpointOrientation;
        if (TerminalType != 3 && TerminalType != 4 && TerminalType != 5)
        {
            Vector Axis;
            Axis.x = Params->Orientation.E[0][1];
            Axis.y = Params->Orientation.E[1][1];
            Axis.z = Params->Orientation.E[2][1];
            float SinHalf = static_cast<float>(std::sin(1.5707963267948966));
            double Length = std::sqrt(static_cast<double>(Axis.x) * Axis.x +
                                      static_cast<double>(Axis.y) * Axis.y +
                                      static_cast<double>(Axis.z) * Axis.z);
            float Scale = static_cast<float>(SinHalf / Length);
            FQuaternion Quat;
            Quat.W = static_cast<float>(std::cos(1.5707963267948966));
            Quat.X = Scale * Axis.x;
            Quat.Y = Scale * Axis.y;
            Quat.Z = Scale * Axis.z;
            FMatrix Rotation;
            GetSpaceMathEngine()->QuaternionToMatrix(Rotation, Quat);
            FMatrix Current = ToFMatrix(Params->Orientation);
            FMatrix Product;
            GetSpaceMathEngine()->MulMatrix(Product, Rotation, Current);
            std::memcpy(&Params->Orientation, &Product, sizeof(Product));
        }

        Vector MountPosition;
        MountPosition.x = 0.0f;
        MountPosition.y = 0.0f;
        MountPosition.z = 0.0f;
        Matrix MountOrientation;
        std::memset(&MountOrientation, 0, sizeof(MountOrientation));
        pub::FindHardpoint("HpMount", Params->ShipArchId, MountPosition, MountOrientation);

        FMatrix Current = ToFMatrix(Params->Orientation);
        FVector Rotated;
        GetSpaceMathEngine()->TransformMatrix(Rotated, Current, ToFVector(MountPosition));
        Vector MountPoint;
        MountPoint.x = Rotated.X + HardpointPosition->x;
        MountPoint.y = Rotated.Y + HardpointPosition->y;
        MountPoint.z = Rotated.Z + HardpointPosition->z;

        FMatrix MountProduct;
        GetSpaceMathEngine()->MulMatrix(MountProduct, Current, ToFMatrix(MountOrientation));

        Vector Offset;
        Offset.x = HardpointPosition->x - MountPoint.x;
        Offset.y = HardpointPosition->y - MountPoint.y;
        Offset.z = HardpointPosition->z - MountPoint.z;

        Matrix Transposed(Params->Orientation.E[0][0], Params->Orientation.E[1][0],
                          Params->Orientation.E[2][0], Params->Orientation.E[0][1],
                          Params->Orientation.E[1][1], Params->Orientation.E[2][1],
                          Params->Orientation.E[0][2], Params->Orientation.E[1][2],
                          Params->Orientation.E[2][2]);
        FVector Local;
        GetSpaceMathEngine()->TransformMatrix(Local, ToFMatrix(Transposed), ToFVector(Offset));
        FVector Back;
        GetSpaceMathEngine()->TransformMatrix(Back, Current, Local);
        Params->Position.x = Back.X + HardpointPosition->x;
        Params->Position.y = Back.Y + HardpointPosition->y;
        Params->Position.z = Back.Z + HardpointPosition->z;
    }

    // The whole-queue erase of FUN_06f607e0: element by element from the front.
    void EraseLaunchQueue(std::list<FContentNpcShipParams>* Queue)
    {
        while (!Queue->empty())
            Queue->erase(Queue->begin());
    }
}

// FUN_06f60340 (the block's copy ctor is FUN_06ef7bd0).
bool __cdecl QueueDockLaunch(FContentNpcShipParams const* Params, unsigned int DockTarget,
                             FContentNpcShip** OutShip)
{
    GDockLaunchQueues[DockTarget].push_back(*Params);
    if (OutShip != nullptr)
        *OutShip = LaunchQueuedShips();
    else
        LaunchQueuedShips();
    return !GDockLaunchQueues[DockTarget].empty();
}

// FUN_06f607e0. A gate-type target gives a non-follower a leading 2 s Delay directive; a
// base / ring target asks CheckDockPermission (0, 2 erase the block, 5 the whole queue, 3 keeps
// the block while Longevity < 0 else erases the queue) and gives a Launch directive unless
// Longevity is exactly -1.0f (an integer compare) and the terminal is 2..5.
// Retail quirks kept: on responses 1 and above 5 the terminal / hardpoint locals still hold an
// earlier block's values (the port's start at zero); after a created ship the loop increment
// runs on the iterator erase returned, so the next block is skipped this pass, and erasing the
// last one wraps to the queue's first block (the VC6 list head links back to it).
FContentNpcShip* __cdecl LaunchQueuedShips()
{
    FContentNpcShip* Result = nullptr;

    unsigned int TargetType = 0;
    int DockIndex = -1;
    pub::SpaceObj::TerminalInfo Info;
    std::memset(&Info, 0, sizeof(Info));
    Vector HardpointPosition;
    HardpointPosition.x = 0.0f;
    HardpointPosition.y = 0.0f;
    HardpointPosition.z = 0.0f;
    Matrix HardpointOrientation;
    std::memset(&HardpointOrientation, 0, sizeof(HardpointOrientation));

    std::map<unsigned int, std::list<FContentNpcShipParams>>::iterator QueueIt =
        GDockLaunchQueues.begin();
    while (QueueIt != GDockLaunchQueues.end())
    {
        unsigned int const* DockTargetId = &QueueIt->first;
        std::list<FContentNpcShipParams>& Queue = QueueIt->second;

        std::list<FContentNpcShipParams>::iterator It = Queue.begin();
        while (It != Queue.end())
        {
            if (pub::SpaceObj::GetType(*DockTargetId, TargetType) != 0)
            {
                EraseLaunchQueue(&Queue);
                break;
            }

            FContentNpcShipParams& Params = *It;
            DockIndex = Params.DockIndex;

            if ((TargetType & 0xc40) != 0)
            {
                if (Params.FormationSlot == 0 || Params.FormationId == 0xffffffffu)
                {
                    TRefHandle<FObjListDirective> Directive;
                    CreateObjListDirective(OBJLIST_DELAY, &Directive.Pointer);
                    static_cast<FObjListDelay*>(Directive.Pointer)->SetDelay(2.0f);
                    AddLeadingDirective(&Params, Directive);
                }
            }
            else if ((TargetType & 0x600de0) != 0)
            {
                if ((TargetType & 0x20) != 0)
                    DockIndex = 1;
                int ShipRep = Params.ReputationHandle->GetReputation();
                int Response = CheckDockPermission(&Params.ClassIds, &ShipRep, &GLaunchNoShipId,
                                                   nullptr, DockTargetId, &DockIndex);
                if (Response == 0 || Response == 2)
                {
                    It = Queue.erase(It);
                    continue;
                }
                if (Response == 5)
                {
                    EraseLaunchQueue(&Queue);
                    break;
                }
                if (Response == 3)
                {
                    if (Params.Longevity >= 0.0f)
                    {
                        EraseLaunchQueue(&Queue);
                        break;
                    }
                    ++It;
                    continue;
                }
                if (Response == 4)
                {
                    pub::SpaceObj::GetTerminalInfo(*DockTargetId, DockIndex, Info);
                    pub::SpaceObj::GetHardpoint(*DockTargetId, Info.Name, &HardpointPosition,
                                                &HardpointOrientation);
                }

                PlaceLaunchedShip(&Params, &TargetType, Info.Value, &HardpointPosition,
                                  &HardpointOrientation);
                bool bOpenTerminal = !(Info.Value == 2 || Info.Value == 3 || Info.Value == 4 ||
                                       Info.Value == 5);
                if (Params.Longevity != -1.0f || bOpenTerminal)
                {
                    TRefHandle<FObjListDirective> Directive;
                    CreateObjListDirective(OBJLIST_LAUNCH, &Directive.Pointer);
                    static_cast<FObjListLaunch*>(Directive.Pointer)->SetLaunch(
                        DockTargetId, DockIndex, static_cast<int>(Info.Value));
                    AddLeadingDirective(&Params, Directive);
                }
            }

            Params.DockIndex = DockIndex;
            Params.GuideTargetId = *DockTargetId;
            FContentNpcShip* Ship = new FContentNpcShip(&Params);
            if (Params.Longevity >= 0.0f)
                Result = Ship;
            Params.System->AddSpaceObj(Ship);
            unsigned int const Controller = 0;
            FContentNpcShip* MsgShip = Ship;
            pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::NpcShipCreated, &MsgShip);

            if ((TargetType & 0xc40) != 0)
            {
                unsigned int ShipId = Ship->SpaceObjId;
                pub::SpaceObj::JumpIn(ShipId, *DockTargetId);
            }
            else if ((TargetType & 0x600de0) != 0)
            {
                FDockQueueEntry Entry;
                Entry.SystemId = Params.System->SystemId;
                Entry.DockIndex = DockIndex;
                Entry.DockTargetId = *DockTargetId;
                Entry.ShipId = Ship->SpaceObjId;
                Entry.bFinishOnClear = true;
                GDockingShips.push_back(Entry);

                if ((TargetType & 0x200100) != 0)
                {
                    // The dock hail handler keeps quiet about this ship for 30 s (FUN_06ee9b70;
                    // phantom DAT_06fbbb98 = 0, i.e. any).
                    int const HandlerType = 0;
                    void* const AnyPhantom = nullptr;
                    unsigned int ShipId = Ship->SpaceObjId;
                    static_cast<FGcsDockHailHandler*>(GetGcsHandler(&HandlerType))
                        ->SuppressHail(&ShipId, &AnyPhantom, 0x1e);
                }

                // The info goes into the same local as the permission path's, so a later
                // block's case 1 sees it.
                FGcsLaunchEvent Event;
                Event.System = Params.System;
                pub::SpaceObj::GetTerminalInfo(Entry.DockTargetId, Entry.DockIndex, Info);
                Event.TerminalType = Info.Value;
                Event.Speaker = Entry.DockTargetId;
                Event.DockIndex = Entry.DockIndex;
                Event.Listener = Ship->SpaceObjId;
                GGcsConversations.QueueConversation(&Event);

                if ((TargetType & 0x100) == 0)
                {
                    unsigned int ShipId = Ship->SpaceObjId;
                    pub::SpaceObj::Launch(ShipId, *DockTargetId, 0);
                }
            }

            It = Queue.erase(It);
            if (It == Queue.end())
                It = Queue.begin();
            else
                ++It;
        }

        if (Queue.empty())
            QueueIt = GDockLaunchQueues.erase(QueueIt);
        else
            ++QueueIt;
    }
    return Result;
}

// FUN_06f66c80 (GetSlotTransform = FUN_06efcbe0, result not tested).
void __cdecl PlaceFormationBlocks(std::list<FContentNpcShipParams>* Blocks, FFormation* Formation,
                                  FTransform const* Placement)
{
    unsigned int FormationId = AllocateFormationId();
    int Slot = 0;
    for (std::list<FContentNpcShipParams>::iterator It = Blocks->begin(); It != Blocks->end();
         ++It)
    {
        if (!Formation->IsPlayerSlot(Slot))
        {
            It->FormationSlot = static_cast<unsigned int>(Slot);
            It->Formation = Formation;
            It->FormationId = FormationId;
        }
        ++Slot;
    }

    // Retail quirk: only GetSlotTransform writes these locals, so a block whose slot it rejects
    // (a player slot left at -1 by pass 1, or an index past the slots) gets the previous
    // block's transform. Declared outside the loop for the same carry-over; retail's first
    // values are indeterminate, the port's are zero.
    FVector SlotPosition;
    SlotPosition.X = 0.0f;
    SlotPosition.Y = 0.0f;
    SlotPosition.Z = 0.0f;
    FMatrix SlotOrientation;
    std::memset(&SlotOrientation, 0, sizeof(SlotOrientation));
    for (std::list<FContentNpcShipParams>::iterator It = Blocks->begin(); It != Blocks->end();
         ++It)
    {
        Formation->GetSlotTransform(It->FormationSlot, &SlotPosition, &SlotOrientation);

        FVector World;
        GetSpaceMathEngine()->TransformXform(World, *Placement, SlotPosition);
        It->Position.x = World.X;
        It->Position.y = World.Y;
        It->Position.z = World.Z;

        FMatrix Orientation;
        GetSpaceMathEngine()->MulMatrix(Orientation, Placement->Rotation, SlotOrientation);
        std::memcpy(&It->Orientation, &Orientation, sizeof(Orientation));
    }
}
