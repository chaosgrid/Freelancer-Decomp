// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU50 (06f56f50-06f57cb0): the ship base class (see BehaviorCmds.h).

#include "NPCs/BehaviorCmds.h"
#include "NPCs/DockQueues.h"        // GetShipDockingTarget (FollowDirectivesOf)
#include "Missions/MissionObjList.h"    // FObjListDock (FollowDirectivesOf)
#include "Missions/ObjListDirective.h"
#include "Core/RefHandle.h"          // TRefHandle (AdvanceDirective's Next)
#include "Core/ContentPlayerState.h"
#include "RandomMissions/RandomMissionDB.h"   // FindShipClass (TU51 FUN_06f58080)
#include "Core/ContentStaticIds.h"  // TU50's 16 static-init thunks 06f56f50-06f57130
#include "Core/ContentInternal.h"     // GIdValueMask
#include "common_server.h"

// FUN_06f571a0. Retail does not write +0x164 (see BehaviorCmds.h).
FContentShipBase::FContentShipBase(FContentShipParams const* Params)
    : FContentSpaceObj(Params)
    , ClassIds(Params->ClassIds)
    , ShipArchId(Params->ShipArchId)
    , FormationSlot(Params->FormationSlot)
    , Formation(Params->Formation)
    , FormationId(Params->FormationId)
    , Priority(Params->Priority)
    , ControllerId(Params->ControllerId)
{
    CurrentDirective = Directives.Items.end();
}

// FUN_06f33d20; the members (queued directives released without a null check), then the TU20
// dtor FUN_06eb7230, follow.
FContentShipBase::~FContentShipBase()
{
    if (ActiveDirective != nullptr)
    {
        ActiveDirective->Release();
        ActiveDirective = nullptr;
    }
}

// 06ea1480.
bool FContentShipBase::IsNpcShip()
{
    return false;
}

// 06f33cf0.
bool FContentShipBase::IsInCombat()
{
    return bInCombat != 0;
}

// 06f57c30.
FObjListDirective** FContentShipBase::GetActiveDirectiveSlot()
{
    return &ActiveDirective;
}

// 06f57c40.
void FContentShipBase::ReleaseActiveDirective()
{
    if (ActiveDirective != nullptr)
    {
        ActiveDirective->Release();
        ActiveDirective = nullptr;
    }
}

// FUN_06f57820; both arguments unused.
void FContentShipBase::OnDirectiveComplete(int CompletedBehavior, int DirectiveResult)
{
    (void)CompletedBehavior;
    (void)DirectiveResult;
    if (!Directives.Items.empty())
        AdvanceDirective();
}

// FUN_06f33e40. The handle is not null-tested; a failed GetRank leaves Rank uninitialised.
int FContentShipBase::GetReputationRank() const
{
    int Rep = ReputationHandle->GetReputation();
    float Rank;
    pub::Reputation::GetRank(Rep, Rank);
    return static_cast<int>(Rank);
}

// FUN_06f57150. Retail's local is uninitialised; the Server export writes it.
unsigned int FContentShipBase::GetCargoHoldSize() const
{
    unsigned int Size = 0;
    pub::GetCargoHoldSize(ShipArchId, Size);
    return Size;
}

// FUN_06f57170.
unsigned int FContentShipBase::GetCargoSpaceOccupied() const
{
    unsigned int Occupied = 0;
    unsigned int Id = SpaceObjId;
    pub::SpaceObj::GetCargoSpaceOccupied(Id, Occupied);
    return Occupied;
}

// FUN_06f57310.
void FContentShipBase::AddDamage(unsigned int const* AttackerId, float Amount)
{
    unsigned int Id = *AttackerId;
    if ((Id & GIdValueMask) == 0)
        return;
    for (std::vector<std::pair<unsigned int, float>>::iterator It = Damage.begin(); It != Damage.end(); ++It)
    {
        if (It->first == Id)
        {
            It->second = Amount + It->second;
            return;
        }
    }
    Damage.push_back(std::pair<unsigned int, float>(Id, Amount));
}

// FUN_06f57500. The last entry with damage >= the current maximum wins (x87: unordered skips).
void FContentShipBase::GetTopDamagers(unsigned int* OutTopPlayer, unsigned int* OutTopAny) const
{
    unsigned int TopAny = 0;
    unsigned int TopPlayer = 0;
    float MaxAny = 0.0f;
    float MaxPlayer = 0.0f;
    for (std::vector<std::pair<unsigned int, float>>::const_iterator It = Damage.begin(); It != Damage.end(); ++It)
    {
        if (FindContentPlayerStateByShip(&It->first) != nullptr && It->second >= MaxPlayer)
        {
            MaxPlayer = It->second;
            TopPlayer = It->first;
        }
        if (It->second >= MaxAny)
        {
            MaxAny = It->second;
            TopAny = It->first;
        }
    }
    *OutTopPlayer = TopPlayer;
    *OutTopAny = TopAny;
}

// FUN_06f575a0.
bool FContentShipBase::HasClassId(unsigned int const* ClassId) const
{
    for (std::vector<unsigned int>::const_iterator It = ClassIds.begin(); It != ClassIds.end(); ++It)
    {
        if (*It == *ClassId)
            return true;
    }
    return false;
}

// FUN_06f575d0 (FindShipClass = FUN_06f58080; the FUN_06f575a0 loop is inlined).
bool FContentShipBase::IsInShipClass(unsigned int const* ShipClassId) const
{
    std::vector<unsigned int>* Members = FindShipClass(ShipClassId);
    if (Members == nullptr)
        return false;
    for (std::vector<unsigned int>::const_iterator It = Members->begin(); It != Members->end(); ++It)
    {
        if (HasClassId(&*It))
            return true;
    }
    return false;
}

// FUN_06f57640 (used by the GCS combat conversation). The first member is read without a size
// test.
FContentShipBase* FContentShipBase::GetFormationLeader()
{
    if (FormationSlot == 0 || FormationId == GNoFormationId)
        return this;
    std::vector<FContentShipBase*> Members;
    CollectFormationMembers(&Members, FormationId);
    return Members.front();
}

// FUN_06f576c0.
void FContentShipBase::SetFormation(unsigned int InFormationId, unsigned int InFormationSlot,
                                    FFormation* InFormation)
{
    FormationId = InFormationId;
    FormationSlot = InFormationSlot;
    Formation = InFormation;
}

// FUN_06f576f0.
void FContentShipBase::SetTradeLaneRings(unsigned int const* Value120, unsigned int const* Value124)
{
    TradeLaneEntryRing = *Value120;
    TradeLaneEndRing = *Value124;
}

// FUN_06f57710.
bool FContentShipBase::IsInTradeLane() const
{
    return TradeLaneEntryRing != 0;
}

// FUN_06f57720.
void FContentShipBase::GetTradeLaneRings(unsigned int* Out120, unsigned int* Out124) const
{
    *Out120 = TradeLaneEntryRing;
    *Out124 = TradeLaneEndRing;
}

// FUN_06f57740. No null check on the released directives.
void FContentShipBase::CancelDirectives()
{
    pub::AI::DirectiveCancelOp Op;
    pub::AI::SubmitDirective(SpaceObjId, &Op);
    std::list<FObjListDirective*>& Items = Directives.Items;
    for (std::list<FObjListDirective*>::iterator It = Items.begin(); It != Items.end(); ++It)
        (*It)->Release();
    std::list<FObjListDirective*>::iterator Node = Items.begin();
    while (Node != Items.end())
        Node = Items.erase(Node);
    CurrentDirective = Items.end();
}

// FUN_06f577d0 (no null check).
void FContentShipBase::AddDirective(FObjListDirective* Directive)
{
    Directives.Items.push_back(Directive);
    Directive->AddRef();
}

// FUN_06f57840. No set_directive_priority here. The second release + clear pass over the now
// empty list is retail's, kept.
void FContentShipBase::SetDirectives(std::list<FObjListDirective*> const* NewList, int InPriority)
{
    Priority = InPriority;
    pub::AI::DirectiveCancelOp Op;
    pub::AI::SubmitDirective(SpaceObjId, &Op);

    std::list<FObjListDirective*>& Items = Directives.Items;
    for (std::list<FObjListDirective*>::iterator It = Items.begin(); It != Items.end(); ++It)
        (*It)->Release();
    Items.clear();
    CurrentDirective = Items.end();
    for (std::list<FObjListDirective*>::iterator It = Items.begin(); It != Items.end(); ++It)
        (*It)->Release();
    Items.clear();

    if (NewList != &Items)
        Items = *NewList;

    for (std::list<FObjListDirective*>::iterator It = Items.begin(); It != Items.end(); ++It)
        (*It)->AddRef();
    CurrentDirective = Items.end();
    AdvanceDirective();
}

// FUN_06f57b00. Next is a ref-holding local, released on every exit.
void FContentShipBase::AdvanceDirective()
{
    TRefHandle<FObjListDirective> Next;
    std::list<FObjListDirective*>& Items = Directives.Items;
    if (CurrentDirective != Items.end())
    {
        if (*CurrentDirective != nullptr)
            (*CurrentDirective)->Release();
        CurrentDirective = Items.erase(CurrentDirective);
        if (CurrentDirective == Items.end() || *CurrentDirective == nullptr)
            return;
        Next = *CurrentDirective;
        Next->AddRef();
    }
    else
    {
        if (Items.empty())
            return;
        CurrentDirective = Items.begin();
        if (*CurrentDirective == nullptr)
            return;
        Next = *CurrentDirective;
        Next->AddRef();
    }

    if (ActiveDirective != nullptr)
    {
        ActiveDirective->Release();
        ActiveDirective = nullptr;
    }
    ActiveDirective = Next;
    Next->AddRef();
    if (!Next->Execute(this))
        AdvanceDirective();
}

// FUN_06f57c00.
void FContentShipBase::RestartDirectives()
{
    CurrentDirective = Directives.Items.end();
    AdvanceDirective();
}

// FUN_06f579c0. Same double release + clear pass as SetDirectives. A leading type 0x13
// directive gets this ship's docking target written into it, although the directive object is
// shared with the leader.
// Type 0x13 is answered by two classes (LAB_06f250a0 is in both the Dock vtable 06fb8ff0 and the
// InstantTradelane vtable 06fb8fd4), and retail writes raw offsets +0x118/+0x120/+0x124. For an
// InstantTradelane +0x124 lies past the 0x124-byte object. Deviation: the port does not
// reproduce that heap write.
void FContentShipBase::FollowDirectivesOf(FContentShipBase* Leader)
{
    pub::AI::DirectiveCancelOp Op;
    pub::AI::SubmitDirective(SpaceObjId, &Op);

    std::list<FObjListDirective*>& Items = Directives.Items;
    for (std::list<FObjListDirective*>::iterator It = Items.begin(); It != Items.end(); ++It)
        (*It)->Release();
    Items.clear();
    CurrentDirective = Items.end();
    for (std::list<FObjListDirective*>::iterator It = Items.begin(); It != Items.end(); ++It)
        (*It)->Release();
    Items.clear();

    Items = Leader->Directives.Items;
    for (std::list<FObjListDirective*>::iterator It = Items.begin(); It != Items.end(); ++It)
        (*It)->AddRef();

    if (!Items.empty() && Items.front()->GetType() == OBJLIST_DOCK)
    {
        unsigned int OwnId = SpaceObjId;
        int DockIndex = 0;
        FObjListDock* Dock = dynamic_cast<FObjListDock*>(Items.front());
        if (Dock != nullptr)
        {
            if (GetShipDockingTarget(&OwnId, &Dock->Op.DockObjectId, &DockIndex, nullptr))
            {
                Dock->Op.bHasDockPortIndex = 1;
                Dock->Op.DockPortIndex = DockIndex;
            }
        }
        else
        {
            FObjListInstantTradelane* Lane =
                static_cast<FObjListInstantTradelane*>(Items.front());
            if (GetShipDockingTarget(&OwnId, &Lane->Op.TradeLaneId, &DockIndex, nullptr))
                reinterpret_cast<unsigned char*>(&Lane->Op.LaneSpeed)[0] = 1;
        }
    }

    CurrentDirective = Items.end();
    AdvanceDirective();
}

// FUN_06f14400.
std::list<FObjListDirective*> FContentShipBase::CopyDirectives() const
{
    std::list<FObjListDirective*> Copy = Directives.Items;
    for (std::list<FObjListDirective*>::iterator It = Copy.begin(); It != Copy.end(); ++It)
        (*It)->AddRef();
    return Copy;
}

// FUN_06f57c20.
bool FContentShipBase::HasNoDirectives() const
{
    return Directives.Items.empty();
}

// FUN_06f57c60.
bool FContentShipBase::SubmitDirective(pub::AI::BaseOp* Op)
{
    return pub::AI::SubmitDirective(SpaceObjId, Op) == 1;
}

// FUN_06f57c80.
int FContentShipBase::GetPriority() const
{
    return Priority;
}

// FUN_06f57c90.
void FContentShipBase::SetPriority(int const* InPriority)
{
    Priority = *InPriority;
    pub::AI::set_directive_priority(SpaceObjId, static_cast<pub::AI::DirectivePriority>(*InPriority));
}
