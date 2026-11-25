// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU46 (06f46940-06f4a780): per-player content state.

// 06f46940-06f46b20: TU46's own 16 static-ID initializers (DAT_06fd0c1c..DAT_06fd0be0),
// before the TU's other dynamic initializers (the player-state map DAT_06fd0bbc).
#include "Core/ContentStaticIds.h"

#include "Core/ContentPlayerState.h"
#include "Bases/BaseDB.h"
#include "Core/ContentInternal.h"      // GetContentGameTime
#include "Universe/SystemInfo.h"
#include "common_id.h"
#include "common_newchar.h"       // pub::StateGraph::get_state_graph
#include "common_server.h"
#include "common_server_controller.h"

#include <algorithm>
#include <memory>

namespace
{
    // Retail DAT_06fd0bbc: map<PlayerId, player state*> (static init LAB_06f49a00). The
    // atexit dtor FUN_06f49a40 only frees the nodes: a state still in the map at unload is
    // never deleted. Logout (GameSupport msg 8) and FUN_06f002e0 are the only places that
    // delete a state.
    struct FContentPlayerStateMap : public std::map<unsigned int, std::unique_ptr<FContentPlayerState>>
    {
        ~FContentPlayerStateMap()
        {
            for (iterator It = begin(); It != end(); ++It)
                It->second.release();
        }
    };

    FContentPlayerStateMap GContentPlayerStates;

    // Retail DAT_06fd0c20: "FP7_system", re-read by FUN_06f478d0 on every call.
    unsigned int GFp7SystemId = 0;

    // Inline find-then-insert-at-end of FUN_06f478d0 / FUN_06f479b0.
    void AppendUnique(std::vector<unsigned int>& Values, unsigned int Value)
    {
        for (int Index = 0; Index < static_cast<int>(Values.size()); ++Index)
        {
            if (Values[Index] == Value)
                return;
        }
        Values.push_back(Value);
    }
}

// FUN_06eb6be0 (06eb6be0-06eb6bf6).
FContentPlayerStateBase::FContentPlayerStateBase()
    : Reputation(-1)
{
}

// FUN_06eb6bb0 (06eb6bb0-06eb6bd1). Retail does not initialise the out value; the Server
// export always writes it (0xffffffff on failure).
unsigned int FContentPlayerStateBase::GetAffiliation() const
{
    int Handle = Reputation;
    unsigned int Affiliation;
    pub::Reputation::GetAffiliation(Handle, Affiliation);
    return Affiliation;
}

// FUN_06f33f80.
int FContentPlayerStateBase::GetReputation() const
{
    return Reputation;
}

// LAB_06f479a0. Never deletes: the map owns the object.
unsigned int FContentPlayerState::Release()
{
    --ReferenceCount;
    return ReferenceCount;
}

// FUN_06f46b90 (06f46b90-06f46d2f). -1.0f = DAT_06fb7d70.
FContentPlayerState::FContentPlayerState(unsigned int InPlayerId)
    : PlayerId(InPlayerId)
    , bCanDock(1)
    , bCanTradelane(1)
    , EncounterFlags(0)
    , MissionAreaTimeout(-1.0f)
    , LiveMissionId(0)
    , MissionNumber(0)
    , NextVendorOfferId(1)
    , ManeuverBehaviorId(0)
    , ManeuverTargetId(0)
{
    pub::Player::GetRep(PlayerId, Reputation);
}

// FUN_06f471d0 (06f471d0-06f472b6).
FContentPlayerShip::FContentPlayerShip(FContentPlayerShipParams const* Params)
    : FContentShipBase(Params)
{
    SpaceObjId = Params->ShipId;

    // SetPersonalityParams with only the state graph set.
    pub::AI::SetPersonalityParams Op;
    Op.bSetPersonality = 0;
    Op.StateGraphId = pub::StateGraph::get_state_graph("PLAYER", pub::StateGraph::LEADER);
    Op.ContentCallbackObject = nullptr;
    Op.DirectiveCallbackObject = nullptr;
    pub::AI::SubmitState(SpaceObjId, &Op);
}

// FUN_06f472e0 (06f472e0-06f47425): member and base dtors in the port.
FContentPlayerShip::~FContentPlayerShip()
{
}

// FUN_06f47430 (06f47430-06f47580).
void FContentPlayerShip::OnTargetChanged(unsigned int const* NewTargetId)
{
    TargetId = *NewTargetId;
    unsigned int Type;
    pub::SpaceObj::GetType(*NewTargetId, Type);

    FContentPlayerState* Owner = FindContentPlayerStateByShip(&SpaceObjId);
    if (Owner == nullptr || (Type & 0x80000) == 0)
        return;

    // The hole and its jump target, each once, into holes_visited.
    std::vector<unsigned int>& Holes = Owner->Stats.HolesVisited;
    unsigned int Hole = *NewTargetId;
    if (std::find(Holes.begin(), Holes.end(), Hole) == Holes.end())
        Holes.insert(Holes.end(), Hole);
    unsigned int JumpSystem = 0;
    unsigned int JumpObject = 0;
    pub::SpaceObj::GetJumpTarget(*NewTargetId, JumpSystem, JumpObject);
    if (std::find(Holes.begin(), Holes.end(), JumpObject) == Holes.end())
        Holes.insert(Holes.end(), JumpObject);
}

// FUN_06f47880 (06f47880-06f478a3).
void FContentPlayerState::SetShipObject(FContentPlayerShip* NewShip)
{
    if (ShipObject != nullptr)
        delete ShipObject;
    ShipObject = NewShip;
}

// FUN_06f478c0.
FSystemInfo* FContentPlayerState::GetSystemRecord() const
{
    return SystemRecord;
}

// FUN_06f47980 (06f47980-06f47983).
FContentBase* FContentPlayerState::GetBaseRecord() const
{
    return BaseRecord;
}

// FUN_06f478b0 (06f478b0-06f478bb).
unsigned int FContentPlayerState::GetSystemId() const
{
    if (SystemRecord == nullptr)
        return 0;
    return SystemRecord->SystemId;
}

// FUN_06f481e0 (06f481e0-06f481ed).
unsigned int FContentPlayerState::GetShipId() const
{
    if (ShipObject == nullptr)
        return 0;
    return ShipObject->SpaceObjId;
}

// FUN_06f47af0 (06f47af0-06f47b85, RET 4). The GetBasePosition result is ignored.
Vector* FContentPlayerState::GetPosition(Vector* Out) const
{
    Vector Position;
    Position.x = 0.0f;
    Position.y = 0.0f;
    Position.z = 0.0f;
    if (ShipObject != nullptr)
    {
        ShipObject->GetPosition(&Position);
    }
    else
    {
        unsigned int const BaseId = BaseRecord != nullptr ? BaseRecord->BaseId : 0;
        unsigned int const SystemId = SystemRecord != nullptr ? SystemRecord->SystemId : 0;
        pub::GF::GetBasePosition(SystemId, BaseId, Position);
    }
    *Out = Position;
    return Out;
}

// FUN_06f478d0 (06f478d0-06f47971).
void FContentPlayerState::OnSystemChange(unsigned int SystemId)
{
    SystemRecord = FindSystemInfo(&SystemId);                                 // FUN_06f75f30
    if (ShipObject != nullptr)
    {
        ShipObject->SetSystem(SystemRecord);                                  // FUN_06eb7090
    }

    if ((SystemId & 0x3fffffffu) == 0)
        return;

    pub::GetSystemID(GFp7SystemId, "FP7_system");
    if (SystemId == GFp7SystemId)
        return;

    AppendUnique(Stats.SystemsVisited, SystemId);
}

// FUN_06f479b0 (06f479b0-06f47a2d).
void FContentPlayerState::OnBaseChange(unsigned int BaseId)
{
    BaseRecord = FindContentBaseInfo(&BaseId);
    if ((BaseId & 0x3fffffffu) == 0)
        return;

    AppendUnique(Stats.BasesVisited, BaseId);
}

// FUN_06f47b90.
void FContentPlayerState::SetLocation(unsigned int InLocationId)
{
    LocationId = InLocationId;
}

// FUN_06f49030 (06f49030-06f49082).
void FContentPlayerState::UpdateTime(float DeltaTime)
{
    Stats.TotalTimePlayed = DeltaTime + Stats.TotalTimePlayed;

    // -1.0f means idle; NaN goes on.
    if (MissionAreaTimeout == -1.0f)
        return;

    // Greater (or unordered) keeps counting; less or equal resets the countdown and bit 2.
    MissionAreaTimeout = MissionAreaTimeout - DeltaTime;
    if (MissionAreaTimeout <= 0.0f)
    {
        MissionAreaTimeout = -1.0f;
        EncounterFlags &= ~static_cast<unsigned int>(PLAYER_FLAG_MISSION_AREA);
    }
}

// FUN_06f481f0 (06f481f0-06f483cb).
void FContentPlayerState::Save(FILE* File)
{
    std::vector<unsigned int> Gates;
    CollectContentGateLocks(PlayerId, Gates);

    fprintf(File, "[%s]\n", "mPlayer");
    for (int Index = 0; Index < static_cast<int>(Gates.size()); ++Index)
        fprintf(File, "%s = %u\n", "locked_gate", Gates[Index]);

    for (int Index = 0; Index < static_cast<int>(Vnpcs.size()); ++Index)
    {
        FVnpcRecord const& Record = Vnpcs[Index];
        // Retail passes the uninitialized 0x40 stack buffer on a lookup miss too.
        char BaseNickname[0x40];
        pub::GetBaseNickname(BaseNickname, 0x40, Record.BaseId);
        ID_String VnpcId;
        VnpcId.Id = Record.NpcId;
        fprintf(File, "%s = %u, %u, %u, %u ; %s, %s\n", "vnpc", Record.BaseId,
                Record.NpcId, Record.TalkCount, Record.MissionState, BaseNickname,
                VnpcId.get_string());
    }

    for (int Index = 0; Index < static_cast<int>(Rumors.size()); ++Index)
        fprintf(File, "%s = %u, %u\n", "rumor", Rumors[Index].RumorId, Rumors[Index].Count);

    fprintf(File, "%s = %d\n", "can_dock", bCanDock != 0 ? 1 : 0);
    for (int Index = 0; Index < static_cast<int>(DockExceptions.size()); ++Index)
        fprintf(File, "%s = %u\n", "dock_exception", DockExceptions[Index]);

    fprintf(File, "%s = %d\n", "can_tl", bCanTradelane != 0 ? 1 : 0);
    for (int Index = 0; Index < static_cast<int>(TradelaneExceptions.size()); ++Index)
    {
        fprintf(File, "%s = %u, %u\n", "tlr_exception", TradelaneExceptions[Index].FirstRingId,
                TradelaneExceptions[Index].SecondRingId);
    }

    Stats.Save(File);
}

namespace
{
    // FUN_06f49210 / FUN_06f49270 / FUN_06f492d0 body: map::find, value or 0.
    int FindMissionCount(std::map<unsigned char, int> const& Counts, unsigned char MissionType)
    {
        std::map<unsigned char, int>::const_iterator Entry = Counts.find(MissionType);
        if (Entry == Counts.end())
            return 0;
        return Entry->second;
    }
}

// FUN_06f49210 (06f49210-06f49261): player +0x5c map.
int FContentPlayerState::GetRandomMissionsCompleted(unsigned char MissionType) const
{
    return FindMissionCount(Stats.RandomMissionsCompleted, MissionType);
}

// FUN_06f49270 (06f49270-06f492c1): player +0x70 map.
int FContentPlayerState::GetRandomMissionsAborted(unsigned char MissionType) const
{
    return FindMissionCount(Stats.RandomMissionsAborted, MissionType);
}

// FUN_06f492d0 (06f492d0-06f49327): player +0x84 map.
int FContentPlayerState::GetRandomMissionsFailed(unsigned char MissionType) const
{
    return FindMissionCount(Stats.RandomMissionsFailed, MissionType);
}

// FUN_06f49330: vector at +0xb0.
int FContentPlayerState::GetBasesVisitedCount() const
{
    return static_cast<int>(Stats.BasesVisited.size());
}

// FUN_06f49350: vector at +0xc0.
int FContentPlayerState::GetHolesVisitedCount() const
{
    return static_cast<int>(Stats.HolesVisited.size());
}

// FUN_06f49370: vector at +0xa0.
int FContentPlayerState::GetSystemsVisitedCount() const
{
    return static_cast<int>(Stats.SystemsVisited.size());
}

// FUN_06f49390: +0x98.
int FContentPlayerState::GetTotalCashEarned() const
{
    return static_cast<int>(Stats.TotalCashEarned);
}

// FUN_06f493a0: +0x9c.
int FContentPlayerState::GetTotalTimePlayed() const
{
    return static_cast<int>(Stats.TotalTimePlayed);
}

// FUN_06f493b0: map at +0x48.
unsigned int FContentPlayerState::GetShipTypesKilledCount() const
{
    return static_cast<unsigned int>(Stats.ShipTypesKilled.size());
}

// FUN_06f493c0 (06f493c0-06f49434).
void FContentPlayerState::CopyShipTypesKilled(unsigned int* Out) const
{
    for (std::map<unsigned int, int>::const_iterator It = Stats.ShipTypesKilled.begin();
         It != Stats.ShipTypesKilled.end(); ++It)
    {
        Out[0] = It->first;
        Out[1] = static_cast<unsigned int>(It->second);
        Out += 2;
    }
}

// FUN_06f48d80 (06f48d80-06f48da7).
unsigned int FContentPlayerState::FindRumor(unsigned int Id) const
{
    for (std::vector<FRumorRecord>::const_iterator It = Rumors.begin(); It != Rumors.end(); ++It)
    {
        if (It->RumorId == Id)
            return It->Count;
    }
    return 0;
}

// FUN_06f48db0 (06f48db0-06f48fa5, RET 4).
void FContentPlayerState::AddRumor(unsigned int Id)
{
    for (std::vector<FRumorRecord>::iterator It = Rumors.begin(); It != Rumors.end(); ++It)
    {
        if (It->RumorId == Id)
        {
            ++It->Count;
            return;
        }
    }
    FRumorRecord Record;
    Record.RumorId = Id;
    Record.Count = 1;
    Rumors.push_back(Record);
}

// FUN_06f48ca0 (06f48ca0-06f48d7c, RET 8). Both searches compare BaseId and NpcId
// (the first through FUN_06f50140), so they find the same record.
void FContentPlayerState::RecordVnpcTalk(unsigned int BaseId, unsigned int NpcId)
{
    if (NpcId == 0)
        return;
    std::vector<FVnpcRecord>::iterator It = Vnpcs.begin();
    for (; It != Vnpcs.end(); ++It)
    {
        if (It->BaseId == BaseId && It->NpcId == NpcId)
            break;
    }
    if (It == Vnpcs.end() || It->TalkCount == 0)
    {
        FVnpcRecord Record;
        Record.BaseId = BaseId;
        Record.NpcId = NpcId;
        Record.TalkCount = 1;
        Record.MissionState = VNPC_MISSION_NONE;
        Vnpcs.push_back(Record);
        return;
    }
    if (It->MissionState != VNPC_MISSION_ACTIVE)
        It->MissionState = VNPC_MISSION_NONE;
    ++It->TalkCount;
}

// FUN_06f48c50 (06f48c50-06f48c5f).
unsigned int FContentPlayerState::AllocateVendorOfferId()
{
    unsigned int const Id = NextVendorOfferId;
    NextVendorOfferId = Id + 1;
    return Id;
}

// FUN_06f48070 (06f48070-06f480bb). The cash local is not initialised in retail
// (pub::Player::InspectCash writes it first on every path); the asset value starts at 0.0.
// The sum stays on the FPU (double).
double FContentPlayerState::GetWorth() const
{
    int Cash = 0;
    pub::Player::InspectCash(PlayerId, Cash);
    float AssetValue = 0.0f;
    pub::Player::GetAssetValue(PlayerId, AssetValue);
    return static_cast<double>(Cash) + static_cast<double>(AssetValue);
}

// FUN_06f487d0 (06f487d0-06f488ab). A member without a state is skipped.
unsigned int FContentPlayerState::GetGroupLiveMission() const
{
    if (LiveMissionId != 0)
        return LiveMissionId;
    std::vector<unsigned int> Members;
    pub::Player::GetGroupMembers(PlayerId, Members);
    for (std::vector<unsigned int>::const_iterator It = Members.begin(); It != Members.end(); ++It)
    {
        FContentPlayerState* Member = FindContentPlayerState(*It);
        if (Member != nullptr && Member->LiveMissionId != 0)
            return Member->LiveMissionId;
    }
    return 0;
}

// FUN_06f495f0 (06f495f0-06f49777).
void FContentPlayerStats::Save(FILE* File) const
{
    for (std::map<unsigned int, int>::const_iterator It = ShipTypesKilled.begin();
         It != ShipTypesKilled.end(); ++It)
        fprintf(File, "ship_type_killed = %u, %d\n", It->first, It->second);

    for (std::map<unsigned char, int>::const_iterator It = RandomMissionsCompleted.begin();
         It != RandomMissionsCompleted.end(); ++It)
        fprintf(File, "rm_completed = %u, %d\n", static_cast<unsigned int>(It->first), It->second);

    for (std::map<unsigned char, int>::const_iterator It = RandomMissionsAborted.begin();
         It != RandomMissionsAborted.end(); ++It)
        fprintf(File, "rm_aborted = %u, %d\n", static_cast<unsigned int>(It->first), It->second);

    for (std::map<unsigned char, int>::const_iterator It = RandomMissionsFailed.begin();
         It != RandomMissionsFailed.end(); ++It)
        fprintf(File, "rm_failed = %u, %d\n", static_cast<unsigned int>(It->first), It->second);

    fprintf(File, "total_cash_earned = %f\n", static_cast<double>(TotalCashEarned));
    fprintf(File, "total_time_played = %f\n", static_cast<double>(TotalTimePlayed));

    for (int Index = 0; Index < static_cast<int>(SystemsVisited.size()); ++Index)
        fprintf(File, "sys_visited = %u\n", SystemsVisited[Index]);
    for (int Index = 0; Index < static_cast<int>(BasesVisited.size()); ++Index)
        fprintf(File, "base_visited = %u\n", BasesVisited[Index]);
    for (int Index = 0; Index < static_cast<int>(HolesVisited.size()); ++Index)
        fprintf(File, "holes_visited = %u\n", HolesVisited[Index]);
}

// FUN_06f49b00 (06f49b00-06f49b43).
FContentPlayerState* FindContentPlayerState(unsigned int PlayerId)
{
    std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator Entry =
        GContentPlayerStates.lower_bound(PlayerId);
    if (Entry == GContentPlayerStates.end() || PlayerId < Entry->first)
        return nullptr;
    return Entry->second.get();
}

// FUN_06f47ba0 (06f47ba0-06f47d06, RET 8).
void FContentPlayerState::SetDockAccess(unsigned char bCan, std::vector<unsigned int> const* Exceptions)
{
    bCanDock = bCan;
    DockExceptions.clear();
    if (Exceptions == nullptr || Exceptions->empty() || Exceptions == &DockExceptions)
        return;
    DockExceptions = *Exceptions;
}

// FUN_06f47d60 (06f47d60-06f47ed1, RET 8).
void FContentPlayerState::SetTradelaneAccess(unsigned char bCan,
                                             std::vector<FTlrException> const* Exceptions)
{
    bCanTradelane = bCan;
    TradelaneExceptions.clear();
    if (Exceptions == nullptr || Exceptions->empty() || Exceptions == &TradelaneExceptions)
        return;
    TradelaneExceptions = *Exceptions;
}

// FUN_06f47ee0 (06f47ee0-06f4800f, RET 8). The default is !can_tl. With a system record and
// a non-empty exception list, the lane of RingA is read (FUN_06f77bd0, result not tested).
// For every ring of that lane and every exception pair whose first ring is both that
// ring and RingA: RingB being either end of the pair, or a ring of the lane reached while the
// toggle (flipped at both pair ends, in lane order) is set, gives can_tl ? 1 : 0; a full scan
// without such a hit gives 2. The last pair processed decides.
int FContentPlayerState::CheckTradelaneRoute(unsigned int RingA, unsigned int RingB)
{
    int Result = bCanTradelane == 0 ? 1 : 0;
    if (SystemRecord == nullptr || TradelaneExceptions.empty())
        return Result;

    std::vector<FTradeLaneRing> Rings;
    SystemRecord->GetTradeLaneRingsByRing(&RingA, &Rings);
    for (std::vector<FTradeLaneRing>::const_iterator Lane = Rings.begin(); Lane != Rings.end(); ++Lane)
    {
        for (std::vector<FTlrException>::const_iterator Pair = TradelaneExceptions.begin();
             Pair != TradelaneExceptions.end(); ++Pair)
        {
            if (Lane->ObjectId != Pair->FirstRingId || RingA != Pair->FirstRingId)
                continue;
            if (RingB == Pair->FirstRingId || RingB == Pair->SecondRingId)
            {
                Result = bCanTradelane != 0 ? 1 : 0;
                continue;
            }
            bool bInside = false;
            bool bHit = false;
            for (std::vector<FTradeLaneRing>::const_iterator Ring = Rings.begin(); Ring != Rings.end(); ++Ring)
            {
                if (Ring->ObjectId == Pair->FirstRingId || Ring->ObjectId == Pair->SecondRingId)
                    bInside = !bInside;
                if (Ring->ObjectId == RingB && bInside)
                {
                    bHit = true;
                    break;
                }
            }
            Result = bHit ? (bCanTradelane != 0 ? 1 : 0) : 2;
        }
    }
    return Result;
}

// FUN_06f49c30 (06f49c30-06f49c42).
FContentPlayerState* GetFirstContentPlayerState()
{
    if (GContentPlayerStates.empty())
        return nullptr;
    return GContentPlayerStates.begin()->second.get();
}

// FUN_06f49bc0 (06f49bc0-06f49c25).
FContentPlayerState* FindContentPlayerStateByShip(unsigned int const* ShipId)
{
    for (std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator It =
             GContentPlayerStates.begin();
         It != GContentPlayerStates.end(); ++It)
    {
        FContentPlayerShip* Ship = It->second->ShipObject;
        if (Ship != nullptr && Ship->SpaceObjId == *ShipId)
            return It->second.get();
    }
    return nullptr;
}

// FUN_06f49b50 (06f49b50-06f49bb1). No null check on the value.
FContentPlayerState* FindContentPlayerStateByLiveMission(unsigned int const* ControllerId)
{
    for (std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator It =
             GContentPlayerStates.begin();
         It != GContentPlayerStates.end(); ++It)
    {
        if (It->second->LiveMissionId == *ControllerId)
            return It->second.get();
    }
    return nullptr;
}

std::map<unsigned int, std::unique_ptr<FContentPlayerState>>& GetContentPlayerStates()
{
    return GContentPlayerStates;
}

// FUN_06f47d10 (06f47d10-06f47d59, RET 4).
bool FContentPlayerState::CanDockAt(unsigned int DockTargetId) const
{
    bool bListed = false;
    for (int Index = 0; Index < static_cast<int>(DockExceptions.size()); ++Index)
    {
        if (DockExceptions[Index] == DockTargetId)
        {
            bListed = true;
            break;
        }
    }
    if (bCanDock != 0)
        return !bListed;
    return bListed;
}

// FUN_06f002e0 (06f002e0-06f003d6) lookup/erase/new/insert part.
FContentPlayerState* ReplaceContentPlayerState(unsigned int PlayerId)
{
    // On a hit the old state is deleted first, then the node is unlinked.
    std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator Entry =
        GContentPlayerStates.lower_bound(PlayerId);
    if (Entry != GContentPlayerStates.end() && !(PlayerId < Entry->first))
    {
        Entry->second.reset();
        GContentPlayerStates.erase(Entry);
    }
    std::unique_ptr<FContentPlayerState> State(new FContentPlayerState(PlayerId));
    std::pair<std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator, bool> Inserted =
        GContentPlayerStates.emplace(PlayerId, std::move(State));
    return Inserted.first->second.get();
}

// GameSupport message 8 (inlined there in retail).
void RemoveContentPlayerState(unsigned int PlayerId)
{
    std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator Entry =
        GContentPlayerStates.lower_bound(PlayerId);
    if (Entry == GContentPlayerStates.end() || PlayerId < Entry->first)
        return;
    // The state is deleted first, then the node is unlinked.
    Entry->second.reset();
    GContentPlayerStates.erase(Entry);
}

// FUN_06f48030 (06f48030-06f4804b, RET 4): the result of AdjustCash is dropped.
void FContentPlayerState::AdjustCash(int Amount)
{
    unsigned int const Id = PlayerId;
    pub::Player::AdjustCash(Id, Amount);
}

// FUN_06f48050 (06f48050-06f48062, RET 4).
void FContentPlayerState::RewardGroup(int Amount)
{
    pub::Player::RewardGroup(PlayerId, Amount);
}

// FUN_06f480c0 (06f480c0-06f480df): the size local starts at 1; GetGroupSize's result code is
// ignored.
unsigned int FContentPlayerState::GetGroupSize() const
{
    unsigned int Size = 1;
    pub::Player::GetGroupSize(PlayerId, Size);
    return Size;
}

// FUN_06f487c0.
unsigned int FContentPlayerState::GetLiveMission() const
{
    return LiveMissionId;
}

namespace
{
    // FUN_06f48a20's leave-mission step for one state (inlined twice: per group member and
    // for the player alone).
    void LeaveLiveMission(FContentPlayerState* State)
    {
        State->LiveMissionId = 0;
        unsigned int const Message[2] = {0, State->PlayerId};
        unsigned int const NoController = 0;
        pub::Controller::_SendMessage(NoController, pub::Controller::EMessage::PlayerMissionChanged, Message);
        pub::Player::SetMsnID(State->PlayerId, State->LiveMissionId, State->PlayerId, false, 0);
    }
}

// FUN_06f48a20 (06f48a20-06f48b93, RET 0xc). The first argument is never read.
void FContentPlayerState::EndLiveMission(unsigned int Unused, int VnpcState, bool bGroup)
{
    (void)Unused;
    for (int Index = 0; Index < static_cast<int>(Vnpcs.size()); ++Index)
    {
        if (Vnpcs[Index].MissionState == VNPC_MISSION_ACTIVE)
            Vnpcs[Index].MissionState = static_cast<unsigned int>(VnpcState);
    }
    if (!bGroup)
    {
        LeaveLiveMission(this);
        return;
    }
    // Every group member with a state, in member order.
    std::vector<unsigned int> Members;
    pub::Player::GetGroupMembers(PlayerId, Members);
    for (int Index = 0; Index < static_cast<int>(Members.size()); ++Index)
    {
        FContentPlayerState* Member = FindContentPlayerState(Members[Index]);
        if (Member == nullptr)
            continue;
        LeaveLiveMission(Member);
    }
}

// GameSupport message 0x20 (heartbeat).
void UpdateAllContentPlayerTimes(float DeltaTime)
{
    for (std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator It =
             GContentPlayerStates.begin();
         It != GContentPlayerStates.end(); ++It)
        It->second->UpdateTime(DeltaTime);
}

// ---- Random-mission support (TU47 mission object) ----

// FUN_06f471a0 (06f471a0-06f471a6).
unsigned int FContentPlayerState::GetFlags() const
{
    return EncounterFlags;
}

// FUN_06f471b0 (06f471b0-06f471ba, RET 4).
void FContentPlayerState::SetFlags(unsigned int Flags)
{
    EncounterFlags = Flags;
}

// FUN_06f471c0 (06f471c0-06f471ca, RET 4).
void FContentPlayerState::SetCountdown(float Countdown)
{
    MissionAreaTimeout = Countdown;
}

// FUN_06f480e0 (06f480e0-06f4818d, RET 4): the name goes into a local first (GetName's result
// code is ignored), then *Out is built from it.
std::wstring* FContentPlayerState::GetName(std::wstring* Out) const
{
    std::wstring Name;
    pub::Player::GetName(PlayerId, Name);
    *Out = Name;
    return Out;
}

// FUN_06f48190 (06f48190-06f481bc, RET 4): the local answer starts false; the call's result
// code is ignored.
bool FContentPlayerState::IsGroupMember(unsigned int OtherPlayerId) const
{
    bool bMember = false;
    if (PlayerId == OtherPlayerId)
        return true;
    pub::Player::IsGroupMember(PlayerId, OtherPlayerId, bMember);
    return bMember;
}

// FUN_06f481c0 (06f481c0-06f481d2, RET 4).
void FContentPlayerState::GetGroupMembers(std::vector<unsigned int>* Out) const
{
    pub::Player::GetGroupMembers(PlayerId, *Out);
}

// FUN_06f48ba0 (06f48ba0-06f48c0a, RET 0x10). The message goes to controller 0 (the zeroed
// ControllerId argument slot is reused as the const uint& target).
void FContentPlayerState::JoinMission(unsigned int const* ControllerId, unsigned int SubId,
                                      bool bNotify, unsigned int NotifyArg)
{
    LiveMissionId = *ControllerId;
    unsigned int const Message[2] = {*ControllerId, PlayerId};
    unsigned int const NoController = 0;
    pub::Controller::_SendMessage(NoController, pub::Controller::EMessage::PlayerMissionChanged, Message);
    if (SubId == 0)
        SubId = PlayerId;
    pub::Player::SetMsnID(PlayerId, LiveMissionId, SubId, bNotify, NotifyArg);
}

// FUN_06f49090 (06f49090-06f49100, RET 4).
void FContentPlayerState::CountRandomMissionCompleted(unsigned char MissionType)
{
    std::map<unsigned char, int>::iterator Entry = Stats.RandomMissionsCompleted.find(MissionType);
    if (Entry == Stats.RandomMissionsCompleted.end())
        Stats.RandomMissionsCompleted.insert(std::pair<unsigned char const, int>(MissionType, 1));
    else
        ++Entry->second;
}

// FUN_06f49110 (06f49110-06f49180, RET 4): the same on +0x70.
void FContentPlayerState::CountRandomMissionAborted(unsigned char MissionType)
{
    std::map<unsigned char, int>::iterator Entry = Stats.RandomMissionsAborted.find(MissionType);
    if (Entry == Stats.RandomMissionsAborted.end())
        Stats.RandomMissionsAborted.insert(std::pair<unsigned char const, int>(MissionType, 1));
    else
        ++Entry->second;
}

// FUN_06f49190 (06f49190-06f49206, RET 4): the same on +0x84.
void FContentPlayerState::CountRandomMissionFailed(unsigned char MissionType)
{
    std::map<unsigned char, int>::iterator Entry = Stats.RandomMissionsFailed.find(MissionType);
    if (Entry == Stats.RandomMissionsFailed.end())
        Stats.RandomMissionsFailed.insert(std::pair<unsigned char const, int>(MissionType, 1));
    else
        ++Entry->second;
}

// FUN_06f488b0 (06f488b0-06f48a12, RET 0x14). VnpcOwner is only the vnpc key.
// Deviation: retail writes the vnpc flag at the found record or, when none matches, at +0xc
// of end() (past the vector); the port writes only a found record.
void FContentPlayerState::JoinMissionAsGroup(unsigned int ControllerId, unsigned int VnpcOwner,
                                             ID_String const* Vnpc, bool bNotify,
                                             unsigned int NotifyArg)
{
    if (VnpcOwner != 0 && !Vnpc->IsEmpty())
    {
        for (std::vector<FVnpcRecord>::iterator It = Vnpcs.begin(); It != Vnpcs.end(); ++It)
        {
            if (It->BaseId == VnpcOwner && It->NpcId == Vnpc->Id)
            {
                It->MissionState = VNPC_MISSION_ACTIVE;
                break;
            }
        }
    }

    std::vector<unsigned int> Members;
    pub::Player::GetGroupMembers(PlayerId, Members);
    for (std::vector<unsigned int>::iterator It = Members.begin(); It != Members.end(); ++It)
    {
        std::map<unsigned int, std::unique_ptr<FContentPlayerState>>::iterator Entry =
            GContentPlayerStates.find(*It);
        if (Entry == GContentPlayerStates.end() || Entry->second == nullptr)
            continue;
        FContentPlayerState* Member = Entry->second.get();
        Member->LiveMissionId = ControllerId;
        unsigned int const Message[2] = {ControllerId, Member->PlayerId};
        unsigned int const NoController = 0;
        pub::Controller::_SendMessage(NoController, pub::Controller::EMessage::PlayerMissionChanged, Message);
        unsigned int SubId = PlayerId;
        if (SubId == 0)
            SubId = Member->PlayerId;
        pub::Player::SetMsnID(Member->PlayerId, Member->LiveMissionId, SubId, bNotify, NotifyArg);
    }
}

// FUN_06f48770 (06f48770-06f487b6, RET 0xc).
bool FContentPlayerState::FindVnpcMissionState(unsigned int BaseId, unsigned int const* NpcId,
                                         unsigned int* OutValue) const
{
    for (std::vector<FVnpcRecord>::const_iterator It = Vnpcs.begin(); It != Vnpcs.end(); ++It)
    {
        if (It->BaseId == BaseId && It->NpcId == *NpcId)
        {
            *OutValue = It->MissionState;
            return true;
        }
    }
    return false;
}

// FUN_06f48c60 (06f48c60-06f48c9d, RET 8).
unsigned int FContentPlayerState::GetVnpcTalkCount(unsigned int BaseId, unsigned int NpcId) const
{
    for (std::vector<FVnpcRecord>::const_iterator It = Vnpcs.begin(); It != Vnpcs.end(); ++It)
    {
        if (It->BaseId == BaseId && It->NpcId == NpcId)
            return It->TalkCount;
    }
    return 0;
}

// FUN_06f48c20 (06f48c20-06f48c2a, RET 4).
void FContentPlayerState::SetManeuverBehaviorId(unsigned int Value)
{
    ManeuverBehaviorId = Value;
}

// FUN_06f48c40 (06f48c40-06f48c4a, RET 4).
void FContentPlayerState::SetManeuverTargetId(unsigned int Value)
{
    ManeuverTargetId = Value;
}

// FUN_06f48c10 (06f48c10-06f48c16).
unsigned int FContentPlayerState::GetManeuverBehaviorId() const
{
    return ManeuverBehaviorId;
}

// FUN_06f48c30 (06f48c30-06f48c36).
unsigned int FContentPlayerState::GetManeuverTargetId() const
{
    return ManeuverTargetId;
}

// FUN_06f48fb0 (06f48fb0-06f49020, RET 4).
void FContentPlayerState::CountShipTypeKilled(unsigned int ShipArchId)
{
    std::map<unsigned int, int>::iterator Entry = Stats.ShipTypesKilled.find(ShipArchId);
    if (Entry == Stats.ShipTypesKilled.end())
        Stats.ShipTypesKilled.insert(std::pair<unsigned int const, int>(ShipArchId, 1));
    else
        ++Entry->second;
}

// FUN_06f49f90 (06f49f90-06f49fc6): the player vector is SystemInfo +0xb8 (FUN_06f78ee0).
bool IsContentSystemOccupied(unsigned int SystemId)
{
    FSystemInfo* System = FindSystemInfo(&SystemId);
    return !System->Players.empty();
}

// FUN_06f47850 (06f47850-06f4786a).
void FContentPlayerShip::ResetHostileState()
{
    bInCombat = 0;
    CalmSince = static_cast<double>(-1.0f);                                   // DAT_06fb7d70
    HostileContactCount = 0;
}

// FUN_06f47840 (06f47840-06f47846).
double FContentPlayerShip::GetCalmSince() const
{
    return CalmSince;
}

// FUN_06f47870 (06f47870-06f47876).
int FContentPlayerShip::GetHostileContactCount() const
{
    return HostileContactCount;
}

// FUN_06f47590 (06f47590-06f4782f, RET 4). The ship's system (+0xdc) is not null-tested.
// The squared distance y*y + z*z + x*x (x87; double here) is compared with 2500^2
// (DAT_06fbafd0); unordered skips. A listed contact is refreshed -- in single player only
// while the object's attitude towards the ship is not above -0.6 (DAT_06fbafc0); an unlisted
// object with attitude <= -0.6 is appended and counted. Contacts not seen this pass are
// erased. Leaving combat sets CalmSince to the game time and unmutes the system's NPC ships
// (FUN_06f73bb0(1)); entering it sets CalmSince = -1.0 and mutes them (FUN_06f73bb0(0)).
void FContentPlayerShip::UpdateHostileContacts(float DeltaTime)
{
    for (std::list<FHostileContact>::iterator It = HostileContacts.begin();
         It != HostileContacts.end(); ++It)
    {
        It->TimeSinceSeen = It->TimeSinceSeen + DeltaTime;
    }

    FSystemInfo* ShipSystem = GetSystem();                                    // FUN_06eea860
    Vector Position;
    GetPosition(&Position);                                                   // FUN_06eb70f0
    for (std::list<FContentSpaceObj*>::iterator ObjIt = ShipSystem->SpaceObjs.begin();
         ObjIt != ShipSystem->SpaceObjs.end(); ++ObjIt)
    {
        FContentSpaceObj* Object = *ObjIt;
        Vector ObjectPosition;
        Object->GetPosition(&ObjectPosition);
        Vector const Delta = SubtractVectors(ObjectPosition, Position);
        double DistanceSquare = static_cast<double>(Delta.y) * Delta.y +
                                static_cast<double>(Delta.z) * Delta.z +
                                static_cast<double>(Delta.x) * Delta.x;
        if (!(DistanceSquare <= 6250000.0f))
            continue;

        std::list<FHostileContact>::iterator Contact = HostileContacts.begin();
        for (; Contact != HostileContacts.end(); ++Contact)
        {
            if (Contact->Object == Object)
                break;
        }

        if (Contact != HostileContacts.end())
        {
            if (pub::SinglePlayer())
            {
                int ShipRep = ReputationHandle->GetReputation();
                int ObjectRep = Object->ReputationHandle->GetReputation();
                float Attitude = 0.0f;
                pub::Reputation::GetAttitude(ObjectRep, ShipRep, Attitude);
                if (Attitude > -0.6f)
                    continue;
            }
            Contact->TimeNear = DeltaTime + Contact->TimeNear;
            Contact->TimeSinceSeen = 0.0f;
            continue;
        }

        int ShipRep = ReputationHandle->GetReputation();
        int ObjectRep = Object->ReputationHandle->GetReputation();
        float Attitude = 0.0f;
        pub::Reputation::GetAttitude(ObjectRep, ShipRep, Attitude);
        if (!(Attitude <= -0.6f))
            continue;
        FHostileContact NewContact;
        NewContact.Object = Object;
        NewContact.TimeNear = DeltaTime;
        NewContact.TimeSinceSeen = 0.0f;
        HostileContacts.push_back(NewContact);                                // FUN_06eaed40
        ++HostileContactCount;
    }

    std::list<FHostileContact>::iterator It = HostileContacts.begin();
    while (It != HostileContacts.end())
    {
        if (It->TimeSinceSeen == 0.0f)
            ++It;
        else
            It = HostileContacts.erase(It);
    }

    if (HostileContacts.empty())
    {
        if (bInCombat == 0)
            return;
        bInCombat = 0;
        CalmSince = GetContentGameTime();                                     // FUN_06efec10
        HostileContactCount = 0;
        FSystemInfo* CalmSystem = GetSystem();
        if (CalmSystem != nullptr)
            CalmSystem->SetNpcShipsCommEnabled(true);                            // FUN_06f73bb0
        return;
    }

    if (bInCombat != 0)
        return;
    bInCombat = 1;
    CalmSince = static_cast<double>(-1.0f);
    FSystemInfo* HostileSystem = GetSystem();
    if (HostileSystem != nullptr)
        HostileSystem->SetNpcShipsCommEnabled(false);                            // FUN_06f73bb0
}
