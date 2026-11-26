// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU53 (06f5c760-06f6b840): the SpaceSupport controller and the
// locked-gate table. CreateInstance("SpaceSupport") -> new 4 -> FUN_06f5cd10;
// vtable 06fbbc08 (slot 0 FUN_06f5d560 HandleMessage, slot 1 FUN_06f5cd80
// deleting dtor = IDLL::~IDLL only).

#include "Core/SpaceSupport.h"

#include "NPCs/CombatReputation.h"   // TU56: OnDamageReputation / ApplyKillReputation / OnHeartbeatReputation
#include "Core/ContentInternal.h"
#include "Core/ContentMessageRouter.h"
#include "Core/ContentPlayerState.h"
#include "Core/ContentSort.h"        // ContentSort (the VC6 std::sort FUN_06f6ab00)
#include "NPCs/DockQueues.h"
#include "NPCs/FactionProps.h"       // HandleFactionNameTimer (FUN_06f513c0)
#include "Comms/Gcs.h"                // GGcsHandlers, GGcsConversations, the category setters
#include "Guides/Guides.h"             // FContentGuide::SetShip (slot +0x20)
#include "Bases/LootProps.h"          // FindLootProps & co., CollectPhantomLoot (TU35)
#include "Missions/MissionObject.h"      // FNnPathLeg
#include "NPCs/NpcShip.h"
#include "Encounters/SpacePaths.h"         // BuildNnPath (FUN_06f5ea40)
#include "Encounters/Populators.h"         // FSpacePopExclusionSphere
#include "RandomMissions/RandomMissionDB.h"    // GetShipMissionProperties (TU51 FUN_06f587d0)
#include "Universe/SystemInfo.h"
#include "Universe/TradelaneRequests.h"  // HandleTradelaneRequest / Release (TU55, msgs 0x14 / 0x15)
#include "common_goods.h"       // EquipDesc
#include "common_iface.h"       // BEHAVIOR_FORMATION
#include "common_ini.h"
#include "common_server.h"
#include "common_server_controller.h"
#include "server_cobject.h"     // pub::SpaceObj::Destroy, DestroyType
#include "server_gameobject.h"  // FObjectDestroyedEvent, FSubObjDamageEvent, FEquipLootEvent

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <vector>

// Retail DAT_06fd1c74: the SpaceSupport controller id (the object itself is
// only a vtable). Declared in ContentInternal.h; the message router sets its
// timers on it.
unsigned int GSpaceSupportControllerId = 0;

// DAT_06fd1b9c (LAB_06f5cc60 / FUN_06f5cca0). See SpaceSupport.h.
std::list<FContentNpcShip*> GSpaceSupportGuidedShips;

namespace
{
    // Retail DAT_06fce770 (.data, only read): heartbeat period.
    float GSpaceSupportHeartbeat = 1.0f;

    // Retail DAT_06fd1c2c, static init LAB_06f5c960: the next module-wide unique id
    // (DAT_06fce76c post-increment). Only read by the timer case: that timer cancels the
    // player's music. GContentNextUniqueId is constant-initialised, so it is
    // ready before this dynamic initialiser runs.
    unsigned int GSpaceSupportMusicTimerId = GContentNextUniqueId++;

    struct FGateLock
    {
        unsigned int GateId;
        std::vector<unsigned int> PlayerIds;
    };

    // Retail DAT_06fd1ca8-06fd1cb4: an ordered vector of {gate, players} (0x14 stride).
    std::vector<FGateLock> GContentGateLocks;

    bool ContainsId(std::vector<unsigned int> const& Values, unsigned int Value)
    {
        for (int Index = 0; Index < static_cast<int>(Values.size()); ++Index)
        {
            if (Values[Index] == Value)
                return true;
        }
        return false;
    }

    struct FSpaceSupport : IDLL
    {
        explicit FSpaceSupport(unsigned int InControllerId);
        void HandleMessage(pub::Controller::EMessage MessageId, void const* Parms) override;

        // FUN_06f5cda0.
        void InitGcs();
        // Case 0x39 of HandleMessage.
        void OnObjectDestroyed(FObjectDestroyedEvent* Event);
        // FUN_06f5cfa0 (RET 0x10; this is not read): the loot of ShipId in Items --
        // duplicates merged, counts scaled by the worth of the players near the ship, a
        // random health, and with bAddPhantomLoot the [PhantomLoot] of its rank.
        void PrepareLoot(FSystemInfo* System, unsigned int ShipId, std::vector<EquipDesc>* Items,
                         bool bAddPhantomLoot);
        // FUN_06f68960 (RET 4; this is not read; caller msg 0x36 in multiplayer):
        // every solar of the system takes the launching player's feeling towards its
        // affiliation as its attitude towards him when the two differ by more than 0.01.
        void SyncSolarAttitudes(unsigned int const* Msg);
        // FUN_06f5e680 (RET 8; this and System are not read; caller OnObjectDestroyed for a
        // destroyed NPC ship): a destroyed formation leader hands its
        // formation, directives and guide to a new leader (true), or with no NPC wingman
        // left frees the player wingmen (false). False for a wingman or no ship.
        bool PromoteFormationLeader(FSystemInfo* System, FContentNpcShip* Ship);
        // FUN_06f5e550 (RET 4; this is not read): the new leader among Ships -- NPC ships
        // only (vtable slot 1), a LARGE_SHIPS member over any other, then the higher
        // reputation rank (the first wins a tie); it is moved to the front of Ships.
        FContentShipBase* PickFormationLeader(std::list<FContentShipBase*>* Ships);
    };

    // FUN_06f68960 (06f68960-06f68ab9). Msg = {system, player, ...} of message 0x36.
    void FSpaceSupport::SyncSolarAttitudes(unsigned int const* Msg)
    {
        unsigned int ShipId = 0;
        pub::Player::GetShip(Msg[1], ShipId);
        if (ShipId == 0)
            return;

        // Up to 1000 objects of the system, any distance from the origin (radius FLT_MAX,
        // type mask 0xfdff).
        unsigned int Objects[0x3e8];
        unsigned int Count = 0;
        Vector Origin;
        Origin.x = 0.0f;
        Origin.y = 0.0f;
        Origin.z = 0.0f;
        pub::System::ScanObjects(Msg[0], Objects, 0x3e8, Origin, 3.40282347e+38f,
                                 OBJ_SOLAR_NO_WAYPOINT_MASK, Count);

        int PlayerRep = -1;
        if (pub::SpaceObj::GetRep(ShipId, PlayerRep) != 0)
            return;
        if (PlayerRep == -1 || Count == 0)
            return;

        for (unsigned int Index = 0; Index < Count; ++Index)
        {
            int SolarRep = -1;
            unsigned int Affiliation = kNoAffiliation;
            if (pub::SpaceObj::GetSolarRep(Objects[Index], SolarRep) != 0)
                continue;
            if (pub::Reputation::GetAffiliation(SolarRep, Affiliation) != 0)
                continue;

            // Both results ignored.
            float Attitude = 0.0f;
            float Feeling = 0.0f;
            pub::Reputation::GetAttitude(SolarRep, PlayerRep, Attitude);
            pub::Reputation::GetReputation(PlayerRep, Affiliation, Feeling);

            // 0.01 = DAT_06fbbb88 (double); unordered skips.
            if (std::fabs(static_cast<double>(Attitude) - static_cast<double>(Feeling)) > 0.01)
                pub::Reputation::SetAttitude(SolarRep, PlayerRep, Feeling);
        }
    }

    // Retail DAT_06fd1b98, static init LAB_06f5cf70: the radius of the players whose worth
    // scales a loot drop (FUN_06f5cfa0).
    float GSpaceSupportLootRadius = 5000.0f;

    // FUN_06f5e550 (06f5e550-06f5e676).
    FContentShipBase* FSpaceSupport::PickFormationLeader(std::list<FContentShipBase*>* Ships)
    {
        unsigned int const LargeShips = CreateID("LARGE_SHIPS");
        FContentShipBase* Best = nullptr;
        std::list<FContentShipBase*>::iterator BestIt = Ships->end();
        for (std::list<FContentShipBase*>::iterator It = Ships->begin(); It != Ships->end();
             ++It)
        {
            FContentShipBase* Candidate = (*It)->IsNpcShip() ? *It : nullptr;
            if (Best == nullptr)
            {
                if (Candidate != nullptr)
                {
                    Best = Candidate;
                    BestIt = It;
                }
                continue;
            }
            if (Candidate == nullptr)
                continue;

            // A large ship beats a non-large one; otherwise (both large or neither) the
            // candidate needs a strictly higher rank.
            bool bCompareRanks = false;
            if (Candidate->IsInShipClass(&LargeShips))                      // FUN_06f575d0
            {
                if (!Best->IsInShipClass(&LargeShips))
                {
                    Best = Candidate;
                    BestIt = It;
                    continue;
                }
                bCompareRanks = true;
            }
            else if (!Best->IsInShipClass(&LargeShips))
            {
                bCompareRanks = true;
            }
            if (bCompareRanks)
            {
                int const CandidateRank = Candidate->GetReputationRank();   // FUN_06f33e40
                if (Best->GetReputationRank() < CandidateRank)
                {
                    Best = Candidate;
                    BestIt = It;
                }
            }
        }

        // The leader moves to the front. With no NPC ship retail would unlink the list head
        // itself -- the only caller always has one.
        Ships->erase(BestIt);
        Ships->push_front(Best);
        return Best;
    }

    // FUN_06f5e680 (06f5e680-06f5e8ff).
    bool FSpaceSupport::PromoteFormationLeader(FSystemInfo* /*System*/, FContentNpcShip* Ship)
    {
        // No ship, or a wingman -> false.
        if (Ship == nullptr)
            return false;
        if (Ship->FormationSlot != 0 && Ship->FormationId != GNoFormationId)
            return false;

        std::vector<FContentShipBase*> Members;
        std::list<FContentShipBase*> NpcWingmen;
        std::list<FContentShipBase*> PlayerWingmen;
        CollectFormationMembers(&Members, Ship->FormationId);               // FUN_06f66220

        // The wingmen, split by whether a player flies them.
        for (std::vector<FContentShipBase*>::iterator It = Members.begin(); It != Members.end();
             ++It)
        {
            FContentShipBase* Member = *It;
            if (Member->FormationSlot == 0 || Member->FormationId == GNoFormationId)
                continue;
            unsigned int MemberId = Member->SpaceObjId;
            if (FindContentPlayerStateByShip(&MemberId) == nullptr)
                NpcWingmen.push_back(Member);
            else
                PlayerWingmen.push_back(Member);
        }

        if (NpcWingmen.empty())
        {
            // No NPC wingman: every player wingman (looked up again in its system) still in
            // a formation stops docking behind the leader and leaves the formation.
            for (std::list<FContentShipBase*>::iterator It = PlayerWingmen.begin();
                 It != PlayerWingmen.end(); ++It)
            {
                FContentShipBase* Wingman =
                    static_cast<FContentShipBase*>((*It)->GetSystem()->FindShip((*It)->SpaceObjId));
                if (Wingman == nullptr || Wingman->FormationId == GNoFormationId)
                    continue;
                CancelDockingBehindLeader(Wingman, Wingman->FormationId);  // FUN_06f6ee40
                Wingman->SetFormation(0xffffffffu, 0, nullptr);              // FUN_06f576c0
            }
            return false;
        }

        // The player wingmen go after the NPC ones.
        for (std::list<FContentShipBase*>::iterator It = PlayerWingmen.begin();
             It != PlayerWingmen.end(); ++It)
            NpcWingmen.push_back(*It);

        // The new leader, a new formation of the same layout (FUN_06f66ec0), the old
        // leader's directives (FUN_06f579c0) and its guide (+0x178, FUN_06f34b70).
        FContentShipBase* NewLeader = PickFormationLeader(&NpcWingmen);
        FormFormation(&NpcWingmen, Ship->Formation, false);
        NewLeader->FollowDirectivesOf(Ship);
        static_cast<FContentNpcShip*>(NewLeader)->SetGuide(Ship->GetGuide(), true, false);

        // To controller 0 (DAT_06fbbb78).
        struct FLeaderChangedMsg
        {
            FContentShipBase* NewLeader;
            FContentShipBase* OldLeader;
        };
        FLeaderChangedMsg Msg;
        Msg.NewLeader = NewLeader;
        Msg.OldLeader = Ship;
        unsigned int const ControllerId = 0;
        pub::Controller::_SendMessage(ControllerId, pub::Controller::EMessage::FormationLeaderChanged,
                                      &Msg);
        return true;
    }

    // FUN_06f5cd10 (06f5cd10-06f5cd71).
    FSpaceSupport::FSpaceSupport(unsigned int InControllerId)
    {
        GSpaceSupportControllerId = InControllerId;
        pub::Controller::SetHeartbeatInterval(GSpaceSupportControllerId, GSpaceSupportHeartbeat);
        InitGcs();
    }

    // FUN_06f5cda0 (06f5cda0-06f5cf44): the GCS handlers, category intervals (FUN_06eeba50;
    // 12.0f DAT_06fbbbb0, 30.0f DAT_06fbbbb4, 3.0f DAT_06fbbbb8, 6.0 computed as 3.0 + 3.0),
    // the default interval (FUN_06eeba30, 15.0f DAT_06fbbbbc widened), the chances
    // (FUN_06eebb00) and the speaker / listener categories (FUN_06eebbb0 / FUN_06eebc60).
    void FSpaceSupport::InitGcs()
    {
        int Type = kGcsHandlerDockHail;
        GGcsHandlers.AddHandler(&Type);
        Type = kGcsHandlerCombat;
        GGcsHandlers.AddHandler(&Type);
        Type = kGcsHandlerFidget;
        GGcsHandlers.AddHandler(&Type);

        SetGcsCategoryInterval(GCS_EVENT_DOCK_GRANTED, 12.0f);
        SetGcsCategoryInterval(GCS_EVENT_FIDGET, 30.0f);
        SetGcsDefaultInterval(static_cast<double>(15.0f));
        SetGcsCategoryChance(GCS_EVENT_KILLED, 0.4f);
        SetGcsCategoryChance(GCS_EVENT_DAMAGED, 0.05f);
        SetGcsCategoryInterval(GCS_EVENT_DAMAGED, 3.0f + 3.0f);
        SetGcsCategoryInterval(GCS_EVENT_KILLED, 0.0f);
        SetGcsCategoryInterval(GCS_EVENT_IN_SIGHTS, 3.0f + 3.0f);
        SetGcsCategoryInterval(GCS_EVENT_ENGAGE, 3.0f);
        SetGcsCategoryInterval(GCS_EVENT_COMBAT_OVER, 3.0f);
        SetGcsCategoryInterval(GCS_EVENT_NO_ENGAGE, 3.0f);
        SetGcsCategoryInterval(GCS_EVENT_DRIFT, 3.0f);
        SetGcsCategoryInterval(GCS_EVENT_FLEE, 3.0f);
        SetGcsCategoryInterval(GCS_EVENT_FRIENDLY_FIRE, 3.0f);
        SetGcsCategoryInterval(GCS_EVENT_LEADER_CHANGED, 3.0f);

        int const Categories[] = {
            GCS_EVENT_DAMAGED, GCS_EVENT_KILLED, GCS_EVENT_IN_SIGHTS, GCS_EVENT_COMBAT_OVER,
            GCS_EVENT_NO_ENGAGE, GCS_EVENT_DRIFT, GCS_EVENT_FLEE, GCS_EVENT_FRIENDLY_FIRE,
            GCS_EVENT_LEADER_CHANGED};
        for (int Index = 0; Index < static_cast<int>(sizeof(Categories) / sizeof(Categories[0]));
             ++Index)
        {
            EnableGcsSpeakerCategory(Categories[Index]);
            EnableGcsListenerCategory(Categories[Index]);
        }
    }

    // FUN_06f5cfa0 (06f5cfa0-06f5d550). Callers: case 0x13 (one item, no phantom loot) and
    // OnObjectDestroyed (the dropped equipment, phantom loot on).
    void FSpaceSupport::PrepareLoot(FSystemInfo* System, unsigned int ShipId,
                                    std::vector<EquipDesc>* Items, bool bAddPhantomLoot)
    {
        // FUN_06f6ab00 = the VC6 std::sort with LAB_06f5cf80 (unsigned ArchId compare).
        ContentSort(Items->data(), Items->data() + Items->size(),
                    [](EquipDesc const& A, EquipDesc const& B) { return A.ArchId < B.ArchId; });

        // Runs of one arch id fold into their first item.
        std::vector<EquipDesc>::iterator Current = Items->begin();
        while (Current != Items->end())
        {
            std::vector<EquipDesc>::iterator Next = Current + 1;
            while (Next != Items->end() && Next->ArchId == Current->ArchId)
            {
                Current->set_count(Current->Count + Next->Count);
                Next = Items->erase(Next);
            }
            Current = Next;
        }

        // Neither caller tests the FindSystemInfo result.
        FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(ShipId));
        if (Ship == nullptr)
            return;

        // The players within DAT_06fd1b98 of the ship.
        std::vector<FContentPlayerState*> Players;
        Vector Position;
        Ship->GetPosition(&Position);
        System->FindPlayersNear(&Position, &GSpaceSupportLootRadius, &Players);

        // Their worth summed in a float (rounded per player), kept in the reused Items
        // argument slot.
        float Worth = 0.0f;
        for (std::vector<FContentPlayerState*>::iterator It = Players.begin();
             It != Players.end(); ++It)
            Worth = static_cast<float>((*It)->GetWorth() + static_cast<double>(Worth));

        if (!Items->empty())
        {
            int const WorthValue = static_cast<int>(Worth);

            // The two out-params live in reused argument slots and keep their value when the
            // pub:: call does not write it: GoodId starts as ShipId, the commodity flag as the
            // low byte of the float sum.
            unsigned int GoodId = ShipId;
            unsigned int WorthBits = 0;
            std::memcpy(&WorthBits, &Worth, sizeof(WorthBits));
            bool bCommodity = (WorthBits & 0xff) != 0;

            for (std::vector<EquipDesc>::iterator Item = Items->begin(); Item != Items->end();
                 ++Item)
            {
                // The loot properties of the item's good, else the default commodity /
                // equipment record (not null-tested).
                unsigned int const ArchId = Item->ArchId;
                pub::TranslateArchToGood(ArchId, GoodId);
                pub::IsCommodity(GoodId, bCommodity);
                FLootProps* Props = FindLootProps(&GoodId);                 // FUN_06f05060
                if (Props == nullptr)
                {
                    if (bCommodity)
                        Props = FindDefaultCommodityLootProps();            // FUN_06f05000
                    else
                        Props = FindDefaultEquipmentLootProps();            // FUN_06f05030
                }

                int const Count = Item->Count;
                int NewCount = 0;
                // Worth below drop_properties[1] (signed) -> 0.
                if (WorthValue >= Props->MinWorth)
                {
                    // The excess worth, per drop_properties[2], capped at the count, rounded
                    // (0.5 = DAT_06fb74e0); a divisor 0 keeps the count; no excess gives 0.
                    int const Excess = WorthValue - Props->MinWorth;
                    int Scaled = 0;
                    if (Excess > 0)
                    {
                        Scaled = Count;
                        if (Props->WorthPerUnit != 0)
                        {
                            int Capped = Excess / Props->WorthPerUnit;
                            if (Capped >= Count)
                                Capped = Count;
                            Scaled = static_cast<int>(floor(static_cast<double>(Capped) + 0.5));
                        }
                    }

                    // + drop_properties[3], at most drop_properties[4], then at most the count.
                    int Bounded = Props->MaxCandidates;
                    if (Props->BonusCount + Scaled < Props->MaxCandidates)
                        Bounded = Scaled + Props->BonusCount;
                    if (Bounded >= Count)
                        Bounded = Count;

                    // x = (float)(n * drop chance); floor(x) items, and one more when
                    // rand()/32768 < the float fraction (unordered skips).
                    float const Expected = static_cast<float>(
                        static_cast<double>(Bounded) * static_cast<double>(Props->DropChance));
                    NewCount = static_cast<int>(floor(static_cast<double>(Expected)));
                    float const Fraction = static_cast<float>(
                        static_cast<double>(Expected) - floor(static_cast<double>(Expected)));
                    if (static_cast<double>(rand()) * static_cast<double>(1.0f / 32768.0f) <
                        static_cast<double>(Fraction))
                        ++NewCount;

                    // At most drop_properties[5].
                    if (NewCount >= Props->MaxDrops)
                        NewCount = Props->MaxDrops;
                }

                // Health 0.5 + rand()/32768 * 0.3 (DAT_06fbbc10 is the double of 0.3f,
                // DAT_06fb74e0 0.5).
                float const Status = static_cast<float>(
                    static_cast<double>(rand()) * static_cast<double>(1.0f / 32768.0f) *
                        static_cast<double>(0.3f) +
                    0.5);
                Item->set_status(Status);
                Item->set_count(NewCount);
            }
        }

        // Phantom loot only when asked for, for an NPC ship whose affiliation (FUN_06eb7070)
        // is not 0x62e8.
        if (bAddPhantomLoot && Ship->IsNpcShip() && Ship->GetAffiliation() != 0x62e8)
        {
            std::vector<FPhantomLoot const*> Phantoms;
            int const Rank = Ship->GetReputationRank();
            CollectPhantomLoot(&Rank, &Phantoms);
            for (std::vector<FPhantomLoot const*>::iterator It = Phantoms.begin();
                 It != Phantoms.end(); ++It)
            {
                // Unordered skips.
                float const Chance = (*It)->PercentChance;
                if (!(static_cast<double>(rand()) * static_cast<double>(1.0f / 32768.0f) <
                      static_cast<double>(Chance)))
                    continue;

                // Signed division.
                EquipDesc Desc;
                Desc.set_arch_id((*It)->Nickname.Id);
                int const Range = (*It)->NumToDropMax - (*It)->NumToDropMin + 1;
                Desc.set_count(rand() * Range / 32768 + (*It)->NumToDropMin);

                Items->push_back(Desc);
            }
        }
    }

    // The dword at Msg[Index] read as a float (retail FLD / PUSH of the parms slot).
    float MsgFloat(unsigned int const* Msg, int Index)
    {
        float Value;
        std::memcpy(&Value, &Msg[Index], sizeof(Value));
        return Value;
    }

    // The inline search of GDockingShips by ship id in cases 0x11, 0x12, 0x27, 0x28, 0x3a.
    std::list<FDockQueueEntry>::iterator FindDockingShip(unsigned int ShipId)
    {
        return FindDockQueueEntry(&GDockingShips, ShipId);
    }

    // The inline map erase of cases 0x37 / 0x3a (FUN_06f694b0 over begin..end).
    void EraseAllDockLaunchQueues()
    {
        GDockLaunchQueues.erase(GDockLaunchQueues.begin(), GDockLaunchQueues.end());
    }

    // The inline vector erase of case 0x39: the first terminal state of this target.
    void EraseFirstTerminalOf(std::vector<FDockTerminal>* Terminals, unsigned int DockTargetId)
    {
        for (std::vector<FDockTerminal>::iterator It = Terminals->begin(); It != Terminals->end();
             ++It)
        {
            if (It->DockTargetId == DockTargetId)
            {
                Terminals->erase(It);
                return;
            }
        }
    }

    // FUN_06f5d560 (06f5d560-06f5e44b, RET 8). Parms is the message's dword array (Msg).
    // Order: some ids first go to the GCS set DAT_06fd1bf0 (FUN_06ee9900) and to the NPC ship
    // event list (FUN_06f683e0); then the main switch; then a tail run for every id: 0x3c /
    // 0x3d to the GCS set, ApplyDockTerminalStates (FUN_06f6bc20) unless 0x1007, and every
    // non-null ship of GSpaceSupportGuidedShips forwards (id, parms) to its guide
    // (FUN_06f34c30) before the null entries are erased.
    void FSpaceSupport::HandleMessage(pub::Controller::EMessage MessageId, void const* Parms)
    {
        using pub::Controller::EMessage;
        unsigned int const* Msg = static_cast<unsigned int const*>(Parms);

        switch (MessageId)
        {
        case EMessage::ObjectDamaged:
        case EMessage::Hail:
        case EMessage::Heartbeat:
        case EMessage::PhantomEnter:
        case EMessage::PhantomExit:
        case EMessage::DeathFuseLit:
        case EMessage::ObjectDestroyed:
        case EMessage::TimerExpired:
        case EMessage::CombatState:
        case EMessage::CombatDrift:
        case EMessage::NpcTurnedHostile:
        case EMessage::FleeRequest:
        case EMessage::DamageReputation:
        case EMessage::FormationLeaderChanged:
        case EMessage::ShipInSights:
            GGcsHandlers.HandleMessage(MessageId, Parms);                     // FUN_06ee9900
            break;
        default:
            break;
        }

        if (MessageId == EMessage::CommComplete || MessageId == EMessage::ObjectDestroyed ||
            MessageId == EMessage::TimerExpired)
            GNpcShipEvents.HandleMessage(MessageId, Parms);                   // FUN_06f683e0

        switch (MessageId)
        {
        case EMessage::CommComplete:
        {
            // A comm line ended {sender?, system, Arg1, Key}.
            FSystemInfo* System = FindSystemInfo(&Msg[1]);
            if (System != nullptr)
                System->SendPendingComms(Msg[2], Msg[0], Msg[3]);             // FUN_06f72980
            GGcsConversations.OnMessage(Parms);                               // FUN_06ee8bd0
            break;
        }
        case EMessage::ObjectDamaged:
        {
            FSubObjDamageEvent const* Event = static_cast<FSubObjDamageEvent const*>(Parms);
            OnDamageReputation(Event);                                        // FUN_06f70490
            FSystemInfo* System = FindSystemInfo(&Event->SystemId);           // not null-tested
            FContentShipBase* Ship =
                static_cast<FContentShipBase*>(System->FindShip(Event->ObjId));
            if (Ship != nullptr)
                Ship->AddDamage(&Event->InflictorId, Event->Damage);          // FUN_06f57310
            break;
        }
        case EMessage::DockCancel:
            // {ship, pass-through}.
            CancelShipDocking(Msg[0], Msg[1], true);                          // FUN_06f6e810
            break;
        case EMessage::DockComplete:
        {
            // {ship, ?, system}.
            FSystemInfo* System = FindSystemInfo(&Msg[2]);
            if (System == nullptr)
                break;
            unsigned int SystemId = System->SystemId;
            BreakFormationAtDock(&SystemId, &Msg[0], true, false);            // FUN_06f6d920
            SetDockEquipment(Msg[0], false, false, nullptr);                  // FUN_06f6dff0
            std::list<FDockQueueEntry>::iterator It = FindDockingShip(Msg[0]);
            if (It == GDockingShips.end())
                break;
            FinishDocking(&It);                                               // FUN_06f6e9d0
            UpdateDockQueues();                                               // FUN_06f6c250
            break;
        }
        case EMessage::DockRequest:
        {
            // {ship, ...}.
            std::list<FDockQueueEntry>::iterator It = FindDockingShip(Msg[0]);
            if (It != GDockingShips.end())
                FinishDocking(&It);
            // FUN_06f6e110; its DOCK_HOST_RESPONSE is not used here.
            HandleDockRequest(static_cast<FDockRequest const*>(Parms));
            break;
        }
        case EMessage::EquipDropLoot:
        {
            // Equipment about to drop as loot (Server copies Desc back and loots that). Not
            // inflicted by a player's ship -> the count becomes 0.
            FEquipLootEvent* Event =
                static_cast<FEquipLootEvent*>(const_cast<void*>(Parms));
            if (FindContentPlayerStateByShip(&Event->InflictorId) == nullptr)
            {
                Event->Desc.set_count(0);
                break;
            }
            // A one-element copy goes through PrepareLoot and is copied back.
            std::vector<EquipDesc> Items;
            Items.insert(Items.begin(), 1, Event->Desc);
            PrepareLoot(FindSystemInfo(&Event->SystemId), Event->ObjId, &Items, false);
            Event->Desc = Items[0];
            break;
        }
        case EMessage::TradelaneRequest:
            // FUN_06f6fcb0 (this not read).
            HandleTradelaneRequest(static_cast<FTradelaneRequest const*>(Parms));
            break;
        case EMessage::TradelaneRelease:
            // FUN_06f6fc90.
            HandleTradelaneRelease(static_cast<FTradelaneRequest const*>(Parms));
            break;
        case EMessage::FormationRequest:
        {
            // {ship, leader, system}.
            FSystemInfo* System = FindSystemInfo(&Msg[2]);                    // FUN_06f75f30
            int const Result = HandleFormationRequest(&Msg[0], &Msg[1], System); // FUN_06f66690
            if (Result == 0)
            {
                // The ship (not null-tested) leaves its old formation state, is told 0, and
                // follows a docking leader.
                FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(Msg[0]));
                ResetFormationState(Ship);                                    // FUN_06f671a0
                pub::SpaceObj::FormationResponse(Msg[0], static_cast<FORMATION_RTYPE>(0));
                FollowLeaderIntoDock(Ship);                                   // FUN_06f6ed10
            }
            else
            {
                pub::SpaceObj::FormationResponse(Msg[0], static_cast<FORMATION_RTYPE>(Result));
            }
            break;
        }
        case EMessage::FormationLeave:
        {
            // {ship, ?, system}.
            FSystemInfo* System = FindSystemInfo(&Msg[2]);                    // not null-tested
            FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(Msg[0]));
            if (Ship != nullptr && Ship->FormationId != GNoFormationId)
            {
                CancelDockingBehindLeader(Ship, Ship->FormationId);           // FUN_06f6ee40
                Ship->SetFormation(0xffffffffu, 0, nullptr);                   // FUN_06f576c0
            }
            ResetFormationState(static_cast<FContentShipBase*>(System->FindShip(Msg[0])));
            if (!pub::SinglePlayer())
                break;
            FContentPlayerState* Player = FindContentPlayerStateByShip(&Msg[0]);
            if (Player != nullptr && Player->ManeuverBehaviorId == BEHAVIOR_FORMATION)                   // FUN_06f48c10
                Player->SetManeuverBehaviorId(0);                                        // FUN_06f48c20
            break;
        }
        case EMessage::Heartbeat:
        {
            // Every system runs its NPC ship lives, then every system drops its flagged
            // objects (both with the heartbeat period DAT_06fce770, not the elapsed time).
            std::map<unsigned int, FSystemInfo>* Systems = GetSystemInfos();  // FUN_06f75f20
            for (std::map<unsigned int, FSystemInfo>::iterator It = Systems->begin();
                 It != Systems->end(); ++It)
            {
                It->second.UpdateNpcShipLives(GSpaceSupportHeartbeat);        // FUN_06f73fa0
            }
            for (std::map<unsigned int, FSystemInfo>::iterator It = Systems->begin();
                 It != Systems->end(); ++It)
            {
                It->second.DestroyFlaggedObjectsAwayFromPlayers(GSpaceSupportHeartbeat); // FUN_06f73dd0
            }
            UpdateDockQueues();                                               // FUN_06f6c250
            LaunchQueuedShips();                                              // FUN_06f607e0
            OnHeartbeatReputation(GSpaceSupportHeartbeat);                    // FUN_06f70480
            float DeltaTime = MsgFloat(Msg, 0);
            std::map<unsigned int, std::unique_ptr<FContentPlayerState>>& Players =
                GetContentPlayerStates();
            for (std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator It =
                     Players.begin();
                 It != Players.end(); ++It)
            {
                FContentPlayerShip* Ship = It->second->ShipObject;
                if (Ship != nullptr)
                    Ship->UpdateHostileContacts(DeltaTime);                   // FUN_06f47590
            }
            GGcsConversations.Update(DeltaTime);                              // FUN_06ee8c50
            break;
        }
        case EMessage::ShipLeftSystem:
        {
            // {object, ?, system}: an object of the system's list is destroyed.
            FSystemInfo* System = FindSystemInfo(&Msg[2]);
            if (System == nullptr || System->FindSpaceObj(Msg[0]) == nullptr) // FUN_06f73990
                break;
            pub::SpaceObj::Destroy(Msg[0], DESTROY_VANISH);
            break;
        }
        case EMessage::Msg0x27:
        {
            // {ship, ?, ?, byte cancel}.
            if (static_cast<unsigned char>(Msg[3]) != 0)
            {
                CancelShipDocking(Msg[0], 0, false);
                break;
            }
            std::list<FDockQueueEntry>::iterator It = FindDockingShip(Msg[0]);
            if (It == GDockingShips.end())
                break;
            It->bFinishOnClear = true;
            MarkDockTerminal(It->DockTargetId, It->DockIndex, false);
            UpdateDockQueues();
            break;
        }
        case EMessage::LaunchComplete:
        {
            // {?, ship}.
            std::list<FDockQueueEntry>::iterator It = FindDockingShip(Msg[1]);
            if (It != GDockingShips.end())
                FinishDocking(&It);
            break;
        }
        case EMessage::PlayerManeuver:
        {
            // {player, E8 value, E4 value}.
            FContentPlayerState* Player = FindContentPlayerState(Msg[0]);     // FUN_06f49b00
            if (Player == nullptr)
                break;
            Player->SetManeuverBehaviorId(Msg[2]);
            Player->SetManeuverTargetId(Msg[1]);
            break;
        }
        case EMessage::FreeTerminalRequest:
        {
            // {player, dock target}. The player's ship arch, its mission properties (TU51 FUN_06f587d0), a terminal for them (index -1 in
            // multiplayer, 0 in single player; no position) -> ReportFreeTerminal on 4.
            unsigned int ShipArchId;
            pub::Player::GetShipID(Msg[0], ShipArchId);
            std::vector<unsigned int> ClassIds;
            if (GetShipMissionProperties(ShipArchId, &ClassIds))
            {
                int DockIndex = pub::SinglePlayer() ? 0 : -1;
                if (ChooseDockTerminal(&ClassIds, &Msg[1], nullptr, &DockIndex) == DOCK_HOST_RESPONSE_PROCEED)
                    pub::ReportFreeTerminal(Msg[1], DockIndex);
            }
            break;
        }
        case EMessage::SpaceEnter:
        {
            // Player launch {system, player, dock target, terminal}.
            if (!pub::SinglePlayer())
                SyncSolarAttitudes(Msg);
            unsigned int TargetType;
            if (pub::SpaceObj::GetType(Msg[2], TargetType) != 0)
                break;
            if ((TargetType & (OBJ_DOCKING_RING | OBJ_JUMP_GATE | OBJ_TRADELANE_RING |
                               OBJ_STATION | OBJ_AIRLOCK_GATE | OBJ_JUMP_HOLE | OBJ_CAPITAL |
                               OBJ_MINING)) == 0)   // 0x600de0
                break;
            FDockQueueEntry Entry;                                            // FUN_06f6bbf0
            Entry.SystemId = Msg[0];
            Entry.DockTargetId = Msg[2];
            Entry.DockIndex = static_cast<int>(Msg[3]);
            pub::Player::GetShip(Msg[1], Entry.ShipId);
            Entry.bFinishOnClear = true;
            GDockingShips.push_back(Entry);
            if ((TargetType & (OBJ_STATION | OBJ_CAPITAL)) != 0)
            {
                // The dock hail handler keeps quiet about the ship for 30 s.
                int const HandlerType = kGcsHandlerDockHail;
                void* const AnyPhantom = nullptr;                              // DAT_06fbbb98
                static_cast<FGcsDockHailHandler*>(GetGcsHandler(&HandlerType))
                    ->SuppressHail(&Entry.ShipId, &AnyPhantom, 0x1e);           // FUN_06ee9b70
            }
            // The type 3 launch event (the player state lookup's result is dropped).
            FGcsLaunchEvent Event;
            Event.System = FindSystemInfo(&Msg[0]);
            FindContentPlayerState(Msg[1]);                                   // FUN_06f49b00
            pub::SpaceObj::TerminalInfo Info;
            pub::SpaceObj::GetTerminalInfo(Entry.DockTargetId, Entry.DockIndex, Info);
            Event.Speaker = Entry.DockTargetId;
            Event.TerminalType = Info.Value;
            Event.Listener = Entry.ShipId;
            Event.DockIndex = Entry.DockIndex;
            GGcsConversations.QueueConversation(&Event);                      // FUN_06ee8d70
            break;
        }
        case EMessage::SpaceExit:
        {
            // {system, player}: no system or an empty one drops every launch queue.
            if ((Msg[0] & GIdValueMask) == 0 || !IsContentSystemOccupied(Msg[0]))
                EraseAllDockLaunchQueues();                                   // FUN_06f694b0
            FSystemInfo* System = FindSystemInfo(&Msg[0]);
            if (System != nullptr)
                System->OnPlayerChanged(FindContentPlayerState(Msg[1]));      // FUN_06f72c90
            break;
        }
        case EMessage::ObjectDestroyed:
            OnObjectDestroyed(static_cast<FObjectDestroyedEvent*>(const_cast<void*>(Parms)));
            break;
        case EMessage::SystemExit:
        {
            // {system, dock target, player}.
            if ((Msg[0] & GIdValueMask) == 0 || !IsContentSystemOccupied(Msg[0]))
                EraseAllDockLaunchQueues();
            FSystemInfo* System = FindSystemInfo(&Msg[0]);
            if (System == nullptr)
                break;
            FContentPlayerState* Player = FindContentPlayerState(Msg[2]);     // not null-tested
            if (Player->ShipObject != nullptr)
            {
                // The docking entry of the ship is finished unless its SYSTEM (entry +0, not
                // the dock target) is Msg[1].
                std::list<FDockQueueEntry>::iterator It =
                    FindDockingShip(Player->ShipObject->SpaceObjId);
                if (It != GDockingShips.end() && It->SystemId != Msg[1])
                    FinishDocking(&It);
            }
            System->RemovePlayer(Player);                                     // FUN_06f72c30
            break;
        }
        case EMessage::SystemEnter:
        {
            // {system, ?, player, arrival object}.
            FSystemInfo* System = FindSystemInfo(&Msg[0]);
            if (System != nullptr)
            {
                FContentPlayerState* Player = FindContentPlayerState(Msg[2]);
                System->AddPlayer(Player);                                    // FUN_06f72a20
                if (Player->ShipObject != nullptr)
                {
                    Player->ShipObject->bScanTarget = 0;
                    Player->ShipObject->ResetHostileState();                  // FUN_06f47850
                }
            }
            // Arriving through an airlock (type & 0x400): the player's ship enters the docking
            // list as launched.
            if (Msg[3] == 0)
                break;
            unsigned int ArrivalType;
            if (pub::SpaceObj::GetType(Msg[3], ArrivalType) != 0 || (ArrivalType & OBJ_AIRLOCK_GATE) == 0)
                break;
            FDockQueueEntry Entry;
            Entry.SystemId = Msg[0];
            Entry.DockTargetId = Msg[3];
            Entry.DockIndex = 0;
            pub::Player::GetShip(Msg[2], Entry.ShipId);
            Entry.bFinishOnClear = true;
            GDockingShips.push_back(Entry);                                   // FUN_06f690a0
            break;
        }
        case EMessage::SystemLoaded:
            // Not null-tested.
            FindSystemInfo(&Msg[0])->LoadFull();                              // FUN_06f75940
            break;
        case EMessage::SystemUnloaded:
            // Every applied terminal state is forgotten.
            FindSystemInfo(&Msg[0])->UnloadFull();                            // FUN_06f75bc0
            GDockTerminalsApplied.clear();
            break;
        case EMessage::TimerExpired:
        {
            // The music timer cancels the player's music; any other timer goes to the faction
            // name queues (FUN_06f513c0) and the message router.
            pub::Controller::TimerExpired const* Timer =
                static_cast<pub::Controller::TimerExpired const*>(Parms);
            if (Timer->Param1 == GSpaceSupportMusicTimerId)
            {
                pub::Audio::CancelMusic(Timer->Param2);
                break;
            }
            HandleFactionNameTimer(Timer);
            GMessageRouter->OnTimerExpired(Timer);
            break;
        }
        case EMessage::DamageReputation:
        {
            // {kind, ship, other, system}: kind 1 and a player's ship sharing its
            // formation group with the other ship -> the ship leaves the formation.
            FSystemInfo* System = FindSystemInfo(&Msg[3]);
            if (System == nullptr || Msg[0] != 1)
                break;
            FContentShipBase* Ship = static_cast<FContentShipBase*>(System->FindShip(Msg[1]));
            FContentShipBase* Other = static_cast<FContentShipBase*>(System->FindShip(Msg[2]));
            if (Ship == nullptr || Other == nullptr)
                break;
            if (FindContentPlayerStateByShip(&Msg[1]) == nullptr)
                break;
            if (Ship->FormationId == Other->FormationId)
                RemoveFromFormation(Ship);                                    // FUN_06f67090
            break;
        }
        case EMessage::NpcShipCreated:
            // Parms = &ship.
            ResetFormationState(*static_cast<FContentNpcShip* const*>(Parms)); // FUN_06f671a0
            break;
        default:
            break;
        }

        // The tail, for every id.
        if (MessageId == EMessage::SystemLoaded || MessageId == EMessage::SystemUnloaded)
            GGcsHandlers.HandleMessage(MessageId, Parms);                     // FUN_06ee9900
        if (MessageId != EMessage::JumpgateAct)
            ApplyDockTerminalStates();                                        // FUN_06f6bc20

        for (std::list<FContentNpcShip*>::iterator It = GSpaceSupportGuidedShips.begin();
             It != GSpaceSupportGuidedShips.end(); ++It)
        {
            if (*It != nullptr)
                (*It)->ForwardToGuide(MessageId, Parms);                      // FUN_06f34c30
        }
        std::list<FContentNpcShip*>::iterator It = GSpaceSupportGuidedShips.begin();
        while (It != GSpaceSupportGuidedShips.end())
        {
            if (*It != nullptr)
                ++It;
            else
                It = GSpaceSupportGuidedShips.erase(It);
        }
    }

    // Case 0x39 of FUN_06f5d560: an object was destroyed.
    void FSpaceSupport::OnObjectDestroyed(FObjectDestroyedEvent* Event)
    {
        unsigned int ObjectId = Event->ObjId;

        // Players whose +0xe8 holds the object clear it.
        std::map<unsigned int, std::unique_ptr<FContentPlayerState>>& Players =
            GetContentPlayerStates();
        for (std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator It =
                 Players.begin();
             It != Players.end(); ++It)
        {
            if (It->second->ManeuverTargetId == ObjectId)                              // FUN_06f48c30
                It->second->SetManeuverTargetId(0);                                    // FUN_06f48c40
        }

        FSystemInfo* System = FindSystemInfo(&Event->SystemId);
        FContentShipBase* Ship = nullptr;
        if (System != nullptr)
            Ship = static_cast<FContentShipBase*>(System->FindShip(ObjectId));
        if (Ship != nullptr)
        {
            if (Ship->IsInTradeLane())                                          // FUN_06f57710
            {
                unsigned int Id120 = 0;
                unsigned int Id124 = 0;
                Ship->GetTradeLaneRings(&Id120, &Id124);                            // FUN_06f57720
                System->RemoveTradeLaneShip(&ObjectId, &Id120);               // FUN_06f78dd0
            }

            FContentNpcShip* NpcShip = nullptr;
            bool bGuideKept = false;
            if (Ship->IsNpcShip())
                NpcShip = static_cast<FContentNpcShip*>(Ship);

            if (Event->Kind == DESTROY_FUSE)
            {
                unsigned int TopPlayer = 0;
                unsigned int TopAny = 0;
                Ship->GetTopDamagers(&TopPlayer, &TopAny);                    // FUN_06f57500
                if (TopPlayer != 0 && TopPlayer == TopAny)
                {
                    PrepareLoot(System, Ship->SpaceObjId, &Event->DroppedEquip, true);
                }
                else
                {
                    // Only a kill a player earned drops loot.
                    Event->DroppedEquip.clear();
                }
                // FUN_06f704a0: the TU56 reputation hit.
                ApplyKillReputation(Ship, TopPlayer != 0 ? &TopPlayer : &Event->KillerId);
                if (NpcShip != nullptr)
                    bGuideKept = PromoteFormationLeader(System, NpcShip);
                FContentPlayerState* Killer = FindContentPlayerStateByShip(&Event->KillerId);
                if (Killer != nullptr)
                    Killer->CountShipTypeKilled(Ship->ShipArchId);            // FUN_06f48fb0
            }

            // An NPC ship's guide loses its ship unless kept.
            if (NpcShip != nullptr && !bGuideKept && NpcShip->GetGuide() != nullptr)
                NpcShip->Guide->SetShip(nullptr);                             // FUN_06f34ca0
        }

        // A killed object owned by a player -> "music_death" for him.
        if (Event->OwnerPlayer != 0 && Event->Kind == DESTROY_FUSE)
        {
            pub::Audio::Tryptich Track;
            Track.SpaceMusic = 0;
            Track.DangerMusic = 0;
            Track.BattleMusic = 0;
            Track.OverrideMusic = pub::GetNicknameId("music_death");
            Track.Field10 = 1;
            float Fade = -1.0f;
            std::memcpy(&Track.FadeBits, &Fade, sizeof(Fade));
            pub::Audio::SetMusic(Event->OwnerPlayer, Track);
        }

        GDockLaunchQueues.erase(ObjectId);
        RemoveContentNpcGateLock(&ObjectId);                                  // FUN_06f6d220
        unsigned int const AllPlayers = 0;                                    // DAT_06fbbb74
        UnregisterContentGateLock(&AllPlayers, &ObjectId);                    // FUN_06f6d5d0
        if (System != nullptr)
        {
            System->RemoveSpaceObj(ObjectId, true);                           // FUN_06f74310
            System->RemoveMissionObject(ObjectId);                            // FUN_06f74620
        }
        CancelShipDocking(ObjectId, 0, false);                                // FUN_06f6e810
        EraseFirstTerminalOf(&GDockTerminals, ObjectId);
        EraseFirstTerminalOf(&GDockTerminalsApplied, ObjectId);
    }
}

// FUN_06f616e0 (06f616e0-06f617f2). Per sphere: the offset and its squared length stay on the
// x87 stack (z*z + y*y + x*x; double here); the length is the square times the 0x5f3759df
// estimate refined once (0.5f DAT_06fb605c, 1.5f DAT_06fb6058) from the square stored as
// float. Strictly below the radius is inside; equal, greater or unordered is outside.
bool __cdecl TestExclusionSpheres(Vector const* Position,
                                  std::vector<FSpacePopExclusionSphere> const* Spheres,
                                  bool const* bAll, bool const* bInside)
{
    if (Spheres == nullptr)
        return true;

    int Inside = 0;
    int Outside = 0;
    for (std::vector<FSpacePopExclusionSphere>::const_iterator It = Spheres->begin();
         It != Spheres->end(); ++It)
    {
        double DX = static_cast<double>(Position->x) - It->X;
        double DY = static_cast<double>(Position->y) - It->Y;
        double DZ = static_cast<double>(Position->z) - It->Z;
        double Square = DZ * DZ + DY * DY + DX * DX;
        float StoredSquare = static_cast<float>(Square);
        int Bits;
        std::memcpy(&Bits, &StoredSquare, sizeof(Bits));
        Bits = 0x5f3759df - (Bits >> 1);
        float Guess;
        std::memcpy(&Guess, &Bits, sizeof(Guess));
        double Distance =
            (1.5f - static_cast<double>(Guess) * Guess * Square * 0.5f) * Guess * Square;
        if (Distance < It->Radius)
            ++Inside;
        else
            ++Outside;
    }

    int Count = static_cast<int>(Spheres->size());
    if (*bAll)
        return *bInside ? Inside == Count : Outside == Count;
    return *bInside ? Inside > 0 : Outside > 0;
}

// FUN_06f6d400 (06f6d400-06f6d5ca).
void RegisterContentGateLock(unsigned int PlayerId, unsigned int GateId)
{
    for (int GateIndex = 0; GateIndex < static_cast<int>(GContentGateLocks.size()); ++GateIndex)
    {
        FGateLock& Gate = GContentGateLocks[GateIndex];
        if (Gate.GateId != GateId)
            continue;

        if (!ContainsId(Gate.PlayerIds, PlayerId))
            Gate.PlayerIds.push_back(PlayerId);
        return;
    }

    std::vector<unsigned int> Players;
    Players.push_back(PlayerId);
    FGateLock Gate = { GateId, Players };
    GContentGateLocks.push_back(Gate);
}

// FUN_06f6d2b0 (06f6d2b0-06f6d3f5).
void CollectContentGateLocks(unsigned int PlayerId, std::vector<unsigned int>& OutGates)
{
    for (int GateIndex = 0; GateIndex < static_cast<int>(GContentGateLocks.size()); ++GateIndex)
    {
        FGateLock const& Gate = GContentGateLocks[GateIndex];
        if (ContainsId(Gate.PlayerIds, PlayerId) || ContainsId(Gate.PlayerIds, 0))
        {
            if (!ContainsId(OutGates, Gate.GateId))
                OutGates.push_back(Gate.GateId);
        }
    }
}

// FUN_06f6ce90 (06f6ce90-06f6cf12, cdecl): only the first lock of GateId counts. Callers TU27,
// TU41 (player 0), TU53.
bool IsContentGateLocked(unsigned int const* PlayerId, unsigned int const* GateId)
{
    for (int GateIndex = 0; GateIndex < static_cast<int>(GContentGateLocks.size()); ++GateIndex)
    {
        FGateLock const& Gate = GContentGateLocks[GateIndex];
        if (Gate.GateId != *GateId)
            continue;
        if (ContainsId(Gate.PlayerIds, 0))
            return true;
        return ContainsId(Gate.PlayerIds, *PlayerId);
    }
    return false;
}

// DAT_06fd1cbc: gates locked for NPC traffic, unique ids in insertion order.
namespace
{
    std::vector<unsigned int> GContentNpcGateLocks;
}

// FUN_06f6d1c0 (06f6d1c0-06f6d212).
void RegisterContentNpcGateLock(unsigned int const* GateId)
{
    if (!ContainsId(GContentNpcGateLocks, *GateId))
        GContentNpcGateLocks.insert(GContentNpcGateLocks.end(), 1, *GateId);
}

// FUN_06f6d280 (06f6d280-06f6d2ab).
bool IsContentNpcGateLocked(unsigned int const* GateId)
{
    return ContainsId(GContentNpcGateLocks, *GateId);
}

// FUN_06f6d220 (06f6d220-06f6d26f).
void RemoveContentNpcGateLock(unsigned int const* GateId)
{
    for (std::vector<unsigned int>::iterator It = GContentNpcGateLocks.begin();
         It != GContentNpcGateLocks.end(); ++It)
    {
        if (*It == *GateId)
        {
            GContentNpcGateLocks.erase(It);
            return;
        }
    }
}

namespace
{
    void EraseFirstId(std::vector<unsigned int>* Ids, unsigned int Value)
    {
        for (std::vector<unsigned int>::iterator It = Ids->begin(); It != Ids->end(); ++It)
        {
            if (*It == Value)
            {
                Ids->erase(It);
                return;
            }
        }
    }

    // FUN_06f6d5d0's per-lock step (inlined twice): in single player a listed player 0 is
    // erased too. Returns whether the lock has no players left.
    bool DropPlayerFromLock(FGateLock* Lock, unsigned int PlayerId)
    {
        EraseFirstId(&Lock->PlayerIds, PlayerId);
        if (pub::SinglePlayer() && ContainsId(Lock->PlayerIds, 0))
            EraseFirstId(&Lock->PlayerIds, 0);
        return Lock->PlayerIds.empty();
    }
}

// FUN_06f6d5d0 (06f6d5d0-06f6d813).
void UnregisterContentGateLock(unsigned int const* PlayerId, unsigned int const* GateId)
{
    if (*PlayerId == 0)
    {
        for (std::vector<FGateLock>::iterator It = GContentGateLocks.begin();
             It != GContentGateLocks.end(); ++It)
        {
            if (It->GateId == *GateId)
            {
                It->PlayerIds.clear();
                GContentGateLocks.erase(It);
                return;
            }
        }
        return;
    }

    if (*GateId == 0)
    {
        std::vector<FGateLock>::iterator It = GContentGateLocks.begin();
        while (It != GContentGateLocks.end())
        {
            if (DropPlayerFromLock(&*It, *PlayerId))
                It = GContentGateLocks.erase(It);
            else
                ++It;
        }
        return;
    }

    for (std::vector<FGateLock>::iterator It = GContentGateLocks.begin();
         It != GContentGateLocks.end(); ++It)
    {
        if (It->GateId != *GateId)
            continue;
        if (DropPlayerFromLock(&*It, *PlayerId))
            GContentGateLocks.erase(It);
        return;
    }
}

// FUN_06f6cf20 (06f6cf20-06f6d032). IsContentGateLocked is inlined in retail; the GetSystem
// result is ignored; IsSpacePopShip is FUN_06f33e80.
bool IsGateLockedForShip(unsigned int ShipId, unsigned int GateId)
{
    if (ShipId == 0)
        return false;

    FContentPlayerState* Player = FindContentPlayerStateByShip(&ShipId);
    if (Player != nullptr)
        return IsContentGateLocked(&Player->PlayerId, &GateId);

    unsigned int SystemId = 0;
    pub::SpaceObj::GetSystem(GateId, SystemId);
    if ((SystemId & GIdValueMask) == 0)
        return false;
    FSystemInfo* System = FindSystemInfo(&SystemId);
    if (System == nullptr)
        return false;
    FContentSpaceObj* Object = System->FindShip(ShipId);
    if (Object != nullptr && !static_cast<FContentNpcShip*>(Object)->IsSpacePopShip())
        return false;
    return ContainsId(GContentNpcGateLocks, GateId);
}

// FUN_06f67cd0 (06f67cd0-06f67d7e): [locked_gates] of a save; locked_gate is registered
// for player 0 (.rdata 06fbbb74 = 0), npc_locked_gate for NPCs.
void LoadContentGateLocks(INI_Reader* Reader)
{
    while (Reader->read_header())
    {
        if (!Reader->is_header("locked_gates"))
            continue;
        while (Reader->read_value())
        {
            if (Reader->is_value("locked_gate"))
            {
                unsigned int GateId = static_cast<unsigned int>(Reader->get_value_int(0));
                RegisterContentGateLock(0, GateId);                            // FUN_06f6d400
            }
            else if (Reader->is_value("npc_locked_gate"))
            {
                unsigned int GateId = static_cast<unsigned int>(Reader->get_value_int(0));
                RegisterContentNpcGateLock(&GateId);                            // FUN_06f6d1c0
            }
        }
    }
}

// FUN_06f67bc0 (06f67bc0-06f67ccd).
void SaveContentGateLocks(FILE* File)
{
    std::vector<unsigned int> Gates;
    CollectContentGateLocks(0, Gates);                                         // FUN_06f6d2b0
    if (!GContentNpcGateLocks.empty() || !Gates.empty())
        fprintf(File, "[%s]\n", "locked_gates");
    for (std::vector<unsigned int>::iterator It = GContentNpcGateLocks.begin();
         It != GContentNpcGateLocks.end(); ++It)
    {
        fprintf(File, "%s = %u\n", "npc_locked_gate", *It);
    }
    for (std::vector<unsigned int>::iterator It = Gates.begin(); It != Gates.end(); ++It)
        fprintf(File, "%s = %u\n", "locked_gate", *It);
}

// FUN_06f5e910 (06f5e910-06f5ea1b).
bool __cdecl BuildBestPathLegs(unsigned int /*Unused*/, FSystemInfo* System, Vector const* From,
                               Vector const* To, std::list<FNnPathLeg>* Legs, int Flags,
                               float const* Range, int Unused7)
{
    bool const bPath = BuildNnPath(System, From, To, Legs, Flags, Range, Unused7);
    if (bPath)
        return bPath;

    Legs->clear();

    FNnPathLeg Leg;
    Leg.Kind = kNnPathLegFlight;
    Leg.PointA = *From;
    Leg.PointB = *To;
    Leg.ObjectA = 0;
    Leg.ObjectB = 0;
    Legs->push_back(Leg);
    return bPath;
}

IDLL* CreateSpaceSupport(char const* /*Name*/, unsigned int ControllerId, void const* /*Parms*/)
{
    return new (std::nothrow) FSpaceSupport(ControllerId);
}
