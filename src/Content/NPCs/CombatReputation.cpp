// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU56 (06f6fd90-06f70dd0): reputation reactions to combat. See
// CombatReputation.h.

#include "NPCs/CombatReputation.h"
#include "Core/ContentStaticIds.h"     // TU56's 16 static-init thunks 06f6fd90-06f6ff70
#include "NPCs/BehaviorCmds.h"         // FContentShipBase (FormationId, HasField120, Slot08)
#include "Core/ContentInternal.h"      // GSpaceSupportControllerId (DAT_06fd1c74)
#include "Core/ContentPlayerState.h"   // FindContentPlayerStateByShip (FUN_06f49bc0), ...
#include "NPCs/FactionProps.h"         // ApplyFactionReputationChange (FUN_06f50c50), GetFactionEventRate
#include "NPCs/NpcBase.h"              // FContentSpaceObj, FSpaceObjAttacker (TU20)
#include "NPCs/NpcShip.h"              // FContentNpcShip::CreationKind (+0x1b4)
#include "Encounters/Populators.h"           // GSpacePopAttackClamp (DAT_06fd1c78)
#include "Encounters/SpacePaths.h"           // FastLength (the 0x5f3759df length)
#include "Universe/SystemInfo.h"           // FSystemInfo (TU58), GetSystemInfos, FindSystemInfo
#include "common_ini.h"           // Vector
#include "common_server.h"
#include "common_server_controller.h"
#include "server_gameobject.h"    // FSubObjDamageEvent

#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <vector>

namespace
{
    // One entry of the heartbeat's scan table (0xe4 bytes in retail): one object found
    // around a player. +0x20..+0x5f of the retail entry are never referenced and are not kept.
    struct FNpcReputationScanEntry
    {
        unsigned char bValid;          // +0x00 (ctor 1)
        unsigned int SpaceObjId;       // +0x04 (ctor 0)
        Vector Position;               // +0x08 pub::SpaceObj::GetLocation (not initialised)
        unsigned int Type;             // +0x14 (ctor 0) pub::SpaceObj::GetType
        unsigned int Affiliation;      // +0x18 (ctor -1)
        int Reputation;                // +0x1c (ctor -1)
        unsigned char bPlayer;         // +0x60 (ctor 0) the ship of a player of the system
        float Attitudes[32];           // +0x64 (ctor 0) towards the entry of the same index

        // FUN_06f70530 (a _initterm entry).
        FNpcReputationScanEntry()
            : bValid(1)
            , SpaceObjId(0)
            , Type(0)
            , Affiliation(0xffffffffu)
            , Reputation(-1)
            , bPlayer(0)
        {
            for (int Index = 0; Index < 32; ++Index)
                Attitudes[Index] = 0.0f;
        }
    };

    // DAT_06fd1d58: 32 entries, filled per player by FUN_06f70580.
    FNpcReputationScanEntry GNpcReputationScan[32];

    // DAT_06fd1d50: the attitude FUN_06f70580 sets, copied from DAT_06fbbd7c by the _initterm
    // entry LAB_06f70570.
    float GNpcAllyHostileAttitude = -0.6f;

    // Message 0x1005 sent to controller 0 by FUN_06f6ff90.
    struct FDamageReputationMsg
    {
        unsigned int bProvoked;        // +0x00 1 at hostility 50 / damage causes 5 and 6, 0 at 25
        unsigned int AttackerId;       // +0x04 the event's InflictorId
        unsigned int VictimId;         // +0x08 the event's ObjId (or its solar parent)
        unsigned int SystemId;         // +0x0c the system's +0x48
    };

    // Message 0x1003 sent to the SpaceSupport controller by FUN_06f70580.
    struct FNpcHostilityMsg
    {
        unsigned int ObjectId;         // +0x00 the object whose attitude changed
        unsigned int OtherId;          // +0x04
        unsigned int SystemId;         // +0x08
    };

    void SendDamageReputation(unsigned int bProvoked, FSubObjDamageEvent const& Event,
                              FSystemInfo const* System)
    {
        FDamageReputationMsg Msg;
        Msg.bProvoked = bProvoked;
        Msg.AttackerId = Event.InflictorId;
        Msg.VictimId = Event.ObjId;
        Msg.SystemId = System->SystemId;
        unsigned int const Controller = 0;
        pub::Controller::_SendMessage(Controller, pub::Controller::EMessage::DamageReputation, &Msg);
    }

    // Inlined twice in FUN_06f70580; x87 sum order (dz*dz + dy*dy) + dx*dx.
    double ScanDistance(Vector const& From, Vector const& To)
    {
        double const Dx = static_cast<double>(From.x) - To.x;
        double const Dy = static_cast<double>(From.y) - To.y;
        double const Dz = static_cast<double>(From.z) - To.z;
        return FastLength((Dz * Dz + Dy * Dy) + Dx * Dx);
    }
}

// FUN_06f6ff90.
void __cdecl HandleDamageReputation(FSubObjDamageEvent const* InEvent)
{
    FSubObjDamageEvent Event = *InEvent;

    // Causes 2..7 only.
    int const Cause = static_cast<int>(Event.Cause);
    bool bCause = false;
    if (Cause == 2)
        bCause = true;
    if (Cause == 3)
        bCause = true;
    if (Cause == 4)
        bCause = true;
    if (Cause == 5)
        bCause = true;
    if (Cause == 6)
        bCause = true;
    if (Cause != 7 && !bCause)
        return;

    // The attacker is a player's ship and not the victim itself.
    if (FindContentPlayerStateByShip(&Event.InflictorId) == nullptr)
        return;
    if (Event.InflictorId == Event.ObjId)
        return;

    // A solar part stands for its parent.
    unsigned int Parent = 0;
    if (pub::SpaceObj::GetSolarParent(Event.ObjId, Parent) == 0)
        Event.ObjId = Parent;
    unsigned int VictimType = 0;
    if (Event.ObjId != 0)
    {
        pub::SpaceObj::GetType(Event.ObjId, VictimType);
        if ((VictimType & (OBJ_SHIP_MASK | OBJ_MISSION_SATELLITE | OBJ_DESTROYABLE_DEPOT |
                           OBJ_WEAPONS_PLATFORM | OBJ_STATION | OBJ_SATELLITE)) == 0)   // 0x7fb110
            return;
    }

    int AttackerRep = -1;
    pub::SpaceObj::GetRep(Event.InflictorId, AttackerRep);
    unsigned int AttackerAffiliation = 0xffffffffu;
    pub::Reputation::GetAffiliation(AttackerRep, AttackerAffiliation);
    int VictimRep = -1;
    pub::SpaceObj::GetRep(Event.ObjId, VictimRep);
    unsigned int VictimAffiliation = 0xffffffffu;
    pub::Reputation::GetAffiliation(VictimRep, VictimAffiliation);

    // Type 0x2000 at no health hit on equipment type 0x800: -0.1 with empathy (x87: unordered
    // skips).
    if (VictimType == OBJ_DESTROYABLE_DEPOT && Event.RelativeHealth <= 0.0f && Event.EquipType == 0x800)
        ApplyFactionReputationChange(AttackerRep, -0.1f, VictimAffiliation, true);

    if (AttackerRep == -1)
        return;
    if (VictimRep == -1)
        return;
    if (AttackerAffiliation == 0xffffffffu && VictimAffiliation == 0xffffffffu)
        return;

    // Both attitudes above -0.6, the victim's below 1.0 (x87: unordered goes on at the 1.0
    // test).
    float AttackerAttitude = 0.0f;
    pub::Reputation::GetAttitude(AttackerRep, VictimRep, AttackerAttitude);
    float VictimAttitude = 0.0f;
    pub::Reputation::GetAttitude(VictimRep, AttackerRep, VictimAttitude);
    if (AttackerAttitude <= -0.6f)
        return;
    if (VictimAttitude <= -0.6f)
        return;
    if (VictimAttitude >= 1.0f)
        return;

    // The system is not null-tested (as retail).
    double Now = 0.0;
    pub::GetRwTime(Now);
    FSystemInfo* System = FindSystemInfo(&Event.SystemId);
    FContentSpaceObj* AttackerShip = System->FindShipOrSolar(Event.InflictorId);
    if (AttackerShip == nullptr)
        return;

    // In single player nothing while the player runs a mission and its ship is in combat.
    if (pub::SinglePlayer())
    {
        FContentPlayerState* First = GetFirstContentPlayerState();
        if (First != nullptr && First->GetLiveMission() != 0 && First->ShipObject != nullptr
            && First->ShipObject->IsInCombat())
        {
            return;
        }
    }

    // The attacker ship's slot for this victim.
    FSpaceObjAttacker* Slot = nullptr;
    if (!AttackerShip->FindOrAllocAttacker(Event.ObjId, &Slot))
        return;

    // Causes 5 and 6 provoke at once.
    if (Cause == 5 || Cause == 6)
    {
        SendDamageReputation(1, Event, System);
        pub::Reputation::SetAttitude(VictimRep, AttackerRep, -1.0f);
        return;
    }

    // A new victim restarts the slot.
    double Elapsed = Now - Slot->LastTime;
    Slot->LastTime = static_cast<float>(Now);
    if (Slot->VictimId != Event.ObjId)
    {
        Slot->VictimId = Event.ObjId;
        Slot->FirstTime = static_cast<float>(Now);
        Slot->HostilityTime = static_cast<float>(Now);
        Elapsed = 0.0;
    }
    if (Elapsed > 1.0)
        Slot->HostilityTime = static_cast<float>(Now);

    // +10 at most once a second, plus 50 times the relative damage.
    if (Now - Slot->HostilityTime > 1.0)
    {
        Slot->HostilityTime = static_cast<float>(Now);
        Slot->Hostility += 10.0f;
    }
    Slot->Hostility =
        static_cast<float>(static_cast<double>(Event.RelativeDamage) * 50.0 + Slot->Hostility);

    // 50 provokes the victim and its formation (x87: unordered goes to the 25 test).
    if (Slot->Hostility >= 50.0f && Slot->bProvoked == 0)
    {
        Slot->bProvoked = 1;
        SendDamageReputation(1, Event, System);
        pub::Reputation::SetAttitude(VictimRep, AttackerRep, -1.0f);
        FContentSpaceObj* Victim = System->FindSpaceObj(Event.ObjId);
        if (Victim == nullptr)
            return;
        unsigned int const Group = static_cast<FContentShipBase*>(Victim)->FormationId;
        if (Group == 0xffffffffu)
            return;
        for (std::list<FContentSpaceObj*>::iterator It = System->SpaceObjs.begin();
             It != System->SpaceObjs.end(); ++It)
        {
            if (static_cast<FContentShipBase*>(*It)->FormationId != Group)
                continue;
            int MemberRep = -1;
            pub::SpaceObj::GetRep((*It)->SpaceObjId, MemberRep);
            pub::Reputation::SetAttitude(MemberRep, AttackerRep, -1.0f);
        }
        return;
    }

    // Above 25 warns once.
    if (!(Slot->Hostility > 25.0f))
        return;
    if (Slot->bWarned != 0)
        return;
    Slot->bWarned = 1;
    SendDamageReputation(0, Event, System);
}

// FUN_06f70490.
void __cdecl OnDamageReputation(FSubObjDamageEvent const* Event)
{
    HandleDamageReputation(Event);
}

// FUN_06f704a0.
void __cdecl ApplyKillReputation(FContentSpaceObj const* Ship, unsigned int const* KillerId)
{
    // The random-population attack clamp.
    if (GSpacePopAttackClamp == 1)
        return;
    FContentPlayerState* Killer = FindContentPlayerStateByShip(KillerId);
    if (Killer == nullptr)
        return;
    // Retail passes uninitialised slots; both calls always write them.
    int ShipRep;
    pub::SpaceObj::GetRep(Ship->SpaceObjId, ShipRep);
    unsigned int Affiliation;
    pub::Reputation::GetAffiliation(ShipRep, Affiliation);
    // Event 0. x87: unordered does not return.
    float const Rate = GetFactionEventRate(Affiliation, 0);
    if (Rate == 0.0f)
        return;
    // With empathy.
    ApplyFactionReputationChange(Killer->GetReputation(), Rate, Affiliation, true);
}

// FUN_06f70580.
void __cdecl UpdateNpcReputations(float /*Unused*/)
{
    bool const bMultiplayer = !pub::SinglePlayer();
    unsigned int Found[32];
    std::map<unsigned int, FSystemInfo>* Systems = GetSystemInfos();
    for (std::map<unsigned int, FSystemInfo>::iterator SystemIt = Systems->begin();
         SystemIt != Systems->end(); ++SystemIt)
    {
        FSystemInfo* System = &SystemIt->second;
        std::vector<FContentPlayerState*>* Players = System->GetPlayers();
        if (Players->empty())
            continue;
        // Once per system.
        unsigned int Count = 0;
        for (std::vector<FContentPlayerState*>::iterator PlayerIt = Players->begin();
             PlayerIt != Players->end(); ++PlayerIt)
        {
            FContentPlayerShip* PlayerShip = (*PlayerIt)->ShipObject;
            if (PlayerShip == nullptr)
                continue;

            // Up to 32 objects (type mask 0x7f31e0) within 5000.
            Vector Position;
            PlayerShip->GetPosition(&Position);
            unsigned int const SystemId = System->SystemId;
            pub::System::ScanObjects(SystemId, Found, 0x20, Position, 5000.0f,
                                     OBJ_SHIP_MASK | OBJ_DESTROYABLE_DEPOT | OBJ_WEAPONS_PLATFORM | OBJ_STATION |
                                     OBJ_TRADELANE_RING | OBJ_JUMP_GATE | OBJ_DOCKING_RING, Count);   // 0x7f31e0
            std::vector<unsigned int>* MissionObjects = System->GetMissionObjects();

            // Fill the table.
            for (unsigned int Index = 0; Index < Count; ++Index)
            {
                FNpcReputationScanEntry& Entry = GNpcReputationScan[Index];
                Entry.Type = 0;
                Entry.Affiliation = 0xffffffffu;
                Entry.Reputation = -1;
                Entry.bValid = 1;
                Entry.bPlayer = 0;
                for (int Slot = 0; Slot < 32; ++Slot)
                    Entry.Attitudes[Slot] = 0.0f;
                Entry.SpaceObjId = Found[Index];
                for (std::vector<FContentPlayerState*>::iterator It = Players->begin();
                     It != Players->end(); ++It)
                {
                    FContentPlayerShip* Ship = (*It)->ShipObject;
                    if (Ship != nullptr && Ship->SpaceObjId == Entry.SpaceObjId)
                        Entry.bPlayer = 1;
                }
                if (Entry.SpaceObjId == 0)
                    continue;

                // NPC ships in creation kinds 0 / 1 (single player) or in a trade lane are left
                // out.
                bool bExcluded = false;
                FContentSpaceObj* Object = System->FindSpaceObj(Entry.SpaceObjId);
                if (Object != nullptr)
                {
                    FContentNpcShip* Npc = static_cast<FContentNpcShip*>(Object);
                    if (!bMultiplayer && (Npc->CreationKind == 0 || Npc->CreationKind == 1))
                        bExcluded = true;
                    if (Npc->IsInTradeLane())
                        bExcluded = true;
                }
                // In single player the mission objects too.
                if (!bMultiplayer
                    && std::find(MissionObjects->begin(), MissionObjects->end(), Found[Index])
                           != MissionObjects->end())
                {
                    Entry.bValid = 0;
                    continue;
                }
                if (bExcluded)
                {
                    Entry.bValid = 0;
                    continue;
                }

                // Place, type, reputation (a solar's first), affiliation.
                Matrix Orientation;
                int const LocationResult =
                    pub::SpaceObj::GetLocation(Entry.SpaceObjId, Entry.Position, Orientation);
                Entry.Type = 0;
                pub::SpaceObj::GetType(Entry.SpaceObjId, Entry.Type);
                int RepResult = 0;
                if ((Entry.Type & 0xfdff) != 0)
                {
                    RepResult = pub::SpaceObj::GetSolarRep(Entry.SpaceObjId, Entry.Reputation);
                    if (RepResult != 0)
                        RepResult = pub::SpaceObj::GetRep(Entry.SpaceObjId, Entry.Reputation);
                }
                else
                {
                    RepResult = pub::SpaceObj::GetRep(Entry.SpaceObjId, Entry.Reputation);
                }
                int const AffiliationResult =
                    pub::Reputation::GetAffiliation(Entry.Reputation, Entry.Affiliation);
                if (LocationResult != 0 || RepResult != 0 || AffiliationResult != 0)
                {
                    Entry.SpaceObjId = 0;
                    Entry.bValid = 0;
                }
            }

            // Every valid entry's attitude towards every other one.
            for (unsigned int A = 0; A < Count; ++A)
            {
                FNpcReputationScanEntry& EntryA = GNpcReputationScan[A];
                if (EntryA.bValid == 0 || EntryA.SpaceObjId == 0)
                    continue;
                for (unsigned int B = 0; B < Count; ++B)
                {
                    FNpcReputationScanEntry& EntryB = GNpcReputationScan[B];
                    if (EntryB.bValid == 0 || EntryB.SpaceObjId == 0)
                        continue;
                    if (A == B)
                        EntryA.Attitudes[B] = 0.0f;
                    else
                        pub::Reputation::GetAttitude(EntryA.Reputation, EntryB.Reputation,
                                                     EntryA.Attitudes[B]);
                }
            }

            // A, liking B within 5000, turns hostile when B (or C) is hostile to C (or B) within
            // 5000 of B. The attitude tests below keep x87 unordered behaviour.
            for (unsigned int A = 0; A < Count; ++A)
            {
                FNpcReputationScanEntry& EntryA = GNpcReputationScan[A];
                if (EntryA.bValid == 0 || EntryA.SpaceObjId == 0
                    || EntryA.Affiliation == 0xffffffffu)
                {
                    continue;
                }
                for (unsigned int B = 0; B < Count; ++B)
                {
                    if (A == B)
                        continue;
                    FNpcReputationScanEntry& EntryB = GNpcReputationScan[B];
                    if (EntryB.bValid == 0 || EntryB.SpaceObjId == 0)
                        continue;
                    if (ScanDistance(EntryA.Position, EntryB.Position) > 5000.0)
                        continue;
                    float const AttitudeAB = EntryA.Attitudes[B];
                    if (AttitudeAB < 0.6f)
                        continue;
                    for (unsigned int C = 0; C < Count; ++C)
                    {
                        if (B == C)
                            continue;
                        FNpcReputationScanEntry& EntryC = GNpcReputationScan[C];
                        if (EntryC.bValid == 0 || EntryC.SpaceObjId == 0)
                            continue;
                        if (EntryB.bPlayer != 0 && EntryC.bPlayer != 0)
                            continue;
                        if (EntryB.Affiliation == 0xffffffffu && EntryC.Affiliation == 0xffffffffu)
                            continue;
                        if ((EntryA.Type & OBJ_TRADELANE_RING) != 0)
                        {
                            if ((EntryB.Type & OBJ_TRADELANE_RING) != 0)
                                continue;
                            if ((EntryC.Type & OBJ_TRADELANE_RING) != 0)
                                continue;
                        }
                        if (ScanDistance(EntryB.Position, EntryC.Position) > 5000.0)
                            continue;
                        float const AttitudeAC = EntryA.Attitudes[C];
                        if (AttitudeAC < -0.6f)
                            continue;
                        // C hostile to B (B to C when C has no affiliation).
                        float const AttitudeBC = EntryB.Attitudes[C];
                        float const AttitudeCB = EntryC.Attitudes[B];
                        bool const bHostileCB = AttitudeCB <= -0.6f;
                        if (EntryC.Affiliation == 0xffffffffu)
                        {
                            if (!(AttitudeBC <= -0.6f))
                                continue;
                        }
                        else if (!bHostileCB)
                        {
                            continue;
                        }
                        bool const bLikesB = AttitudeAB >= 0.6f;
                        bool const bLikesC = AttitudeAC >= 0.6f;

                        FNpcHostilityMsg Msg;
                        if (EntryB.Affiliation == 0xffffffffu)
                        {
                            if (!bLikesC)
                                continue;
                            float BReputation = 0.0f;
                            pub::Reputation::GetReputation(EntryB.Reputation, EntryC.Affiliation,
                                                           BReputation);
                            // Retail compares against 0.01 doubled (x87).
                            double const Gap =
                                std::fabs(static_cast<double>(BReputation) - AttitudeCB);
                            bool const bMatches = 0.01 + 0.01 > Gap;
                            if (bMatches)
                            {
                                if (AttitudeAC >= 1.0f)
                                    continue;
                                pub::Reputation::SetAttitude(EntryA.Reputation, EntryC.Reputation,
                                                             GNpcAllyHostileAttitude);
                                Msg.ObjectId = EntryA.SpaceObjId;
                                Msg.OtherId = EntryB.SpaceObjId;
                            }
                            else
                            {
                                if (AttitudeAB >= 1.0f)
                                    continue;
                                pub::Reputation::SetAttitude(EntryA.Reputation, EntryB.Reputation,
                                                             GNpcAllyHostileAttitude);
                                Msg.ObjectId = EntryA.SpaceObjId;
                                Msg.OtherId = EntryC.SpaceObjId;
                            }
                        }
                        else if (bLikesB)
                        {
                            if (bLikesC)
                                continue;
                            if (AttitudeAC >= 1.0f)
                                continue;
                            if (!(AttitudeAC <= -0.6f))
                                continue;
                            pub::Reputation::SetAttitude(EntryA.Reputation, EntryC.Reputation,
                                                         GNpcAllyHostileAttitude);
                            Msg.ObjectId = EntryA.SpaceObjId;
                            Msg.OtherId = EntryB.SpaceObjId;
                        }
                        else
                        {
                            if (AttitudeAB >= 1.0f)
                                continue;
                            if (!(AttitudeAB <= -0.6f))
                                continue;
                            pub::Reputation::SetAttitude(EntryA.Reputation, EntryB.Reputation,
                                                         GNpcAllyHostileAttitude);
                            Msg.ObjectId = EntryA.SpaceObjId;
                            Msg.OtherId = EntryB.SpaceObjId;
                        }
                        Msg.SystemId = System->SystemId;
                        pub::Controller::_SendMessage(GSpaceSupportControllerId,
                                                      pub::Controller::EMessage::NpcTurnedHostile, &Msg);
                    }
                }
            }
        }
    }
}

// FUN_06f70480.
void __cdecl OnHeartbeatReputation(float HeartbeatPeriod)
{
    UpdateNpcReputations(HeartbeatPeriod);
}
