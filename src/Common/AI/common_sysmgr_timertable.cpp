// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- SystemManagerTimerTable (common_systemmanager.h), the AI target list / threat
// timer table at SystemManager+0x34 (retail run 0x062e6af0-0x062e8970).

#include "common_systemmanager.h"
#include "common_behavior.h" // SystemManagerTimerTable::MissileWithinReaction00/04/08 declarations
#include "common_iface.h"   // IObjRW, IObjInspect::get_id
#include "common_cobjdb.h"  // IObjDB::FindById
#include "common_aisteer.h" // SustainedThresholdTimer
#include "common_misc.h"
#include "common_cobject.h"
#include "dacom.h"

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <new>

// The six SustainedThresholdTimer Active bytes (Timers at +0x28, stride 0x10).

// FUN_062e8810 ([ECX+0x28])
bool SystemManagerTimerTable::IsTimer0Active() const
{
    return Timers[0].Active != 0;
}

// FUN_062e8820 ([ECX+0x38])
bool SystemManagerTimerTable::IsTimer1Active() const
{
    return Timers[1].Active != 0;
}

// FUN_062e8860 ([ECX+0x48])
bool SystemManagerTimerTable::IsTimer2Active() const
{
    return Timers[2].Active != 0;
}

// FUN_062e8830 ([ECX+0x58])
bool SystemManagerTimerTable::IsTimer3Active() const
{
    return Timers[3].Active != 0;
}

// FUN_062e8850 ([ECX+0x68])
bool SystemManagerTimerTable::IsTimer4Active() const
{
    return Timers[4].Active != 0;
}

// FUN_062e8840 ([ECX+0x78])
bool SystemManagerTimerTable::IsTimer5Active() const
{
    return Timers[5].Active != 0;
}

// FUN_062e74a0. Guard order as retail. 18 callers in the behaviours.
IObjRW* SystemManagerTimerTable::FindVerifiedObject(unsigned const* Id) const
{
    if (*Id == 0)
        return nullptr;

    IObjDB* const Db = ObjectDb;
    if (Db == nullptr)
        return nullptr;

    IObjRW* const Found = Db->FindById(Id);
    if (Found == nullptr)
        return nullptr;

    if (Found->get_id() != *Id)
        return nullptr;

    return Found;
}


// FUN_062e8910 / 062e8940 / 062e8970 -- see common_behavior.h. Each needs Best > 0, then is
// true on less or equal (false on greater or unordered).
bool SystemManagerTimerTable::MissileWithinReaction00() const
{
    float const Best = BestValueSoFar;
    if (!(Best > 0.0f))
        return false;
    return Best <= Owner->PersonalityData.MissileReaction.EvadeBreakMissileReactionTime;   // +0x17b4
}

bool SystemManagerTimerTable::MissileWithinReaction04() const
{
    float const Best = BestValueSoFar;
    if (!(Best > 0.0f))
        return false;
    return Best <= Owner->PersonalityData.MissileReaction.EvadeSlideMissileReactionTime;   // +0x17b8
}

bool SystemManagerTimerTable::MissileWithinReaction08() const
{
    float const Best = BestValueSoFar;
    if (!(Best > 0.0f))
        return false;
    return Best <= Owner->PersonalityData.MissileReaction.EvadeAfterburnMissileReactionTime;   // +0x17bc
}

namespace
{
    // Doubly-linked circular list node: {next, prev, embedded BaseWatcher}, 0x10 bytes (the
    // size of both retail operator new calls, sentinel and node).
    struct SystemManagerTimerListNode
    {
        SystemManagerTimerListNode* Next;  // +0x00
        SystemManagerTimerListNode* Prev;  // +0x04
        unsigned char Watcher[8];          // +0x08, BaseWatcher-sized payload
    };
}

// ============================================================================
// SystemManagerTimerTable -- FUN_062e6af0.
// ============================================================================
void SystemManagerTimerTable::Construct()
{
    std::memset(this, 0, sizeof(*this));

    SustainedThresholdTimer* const Timers = this->Timers;
    for (int i = 0; i < 11; ++i)
    {
        Timers[i].Active = 0;
        Timers[i].TriggerThreshold = 100.0f;
        Timers[i].Duration = 1.0f;
        Timers[i].Accumulator = 0.0f;
    }

    DecayScale = 0.5f;

    // An empty, self-referential circular-list sentinel head (operator new(0x10)).
    SystemManagerTimerListNode* const Node =
        static_cast<SystemManagerTimerListNode*>(::operator new(0x10));
    Node->Next = Node;
    Node->Prev = Node;
    ListHeadNode = Node;
    ListCount = 0;

    BestValueSoFar = 3.402823466e38f; // FLT_MAX
}

namespace
{
    // FUN_062d4c90, applied by IBehaviorManager's dtor to Timers+0xDC: a std::list whose node
    // is {Next, Prev, BaseWatcher}.
    void DestroySystemManagerTimerList(SystemManagerTimerTable* Self)
    {
        SystemManagerTimerListNode* const Head =
            static_cast<SystemManagerTimerListNode*>(Self->ListHeadNode);
        SystemManagerTimerListNode* Node = Head->Next;
        while (Node != Head)
        {
            SystemManagerTimerListNode* const Next = Node->Next;
            SystemManagerTimerListNode* const Prev = Node->Prev;
            Prev->Next = Next;
            Next->Prev = Prev;
            reinterpret_cast<BaseWatcher*>(Node->Watcher)->~BaseWatcher();
            ::operator delete(Node);
            --Self->ListCount;
            Node = Next;
        }
        ::operator delete(Head);
        Self->ListHeadNode = nullptr;
        Self->ListCount = 0;
    }
}

// FUN_062d2700
SystemManagerTimerTable::~SystemManagerTimerTable()
{
    reinterpret_cast<BaseWatcher*>(BestCandidateWatcherBytes)->set(nullptr);
    DestroySystemManagerTimerList(this);
    ::operator delete(Targets.Begin);
    Targets.Begin = nullptr;
    Targets.End = nullptr;
    Targets.Cap = nullptr;
}

// FUN_062e8720, thiscall (this = SystemManager+0x34), RET 4. Sole caller: IBehaviorManager::
// Slot00 case 6, with the event's Data -- the {OwnerId, missile id} block Server's
// StarSystem::create_guided sends. Data is not an object: the missile is looked up by id.
// Retail's SEH frame only tears down the scratch watcher on an exception; RAII does the same.
void SystemManagerTimerTable::AppendTimerListEntry(void* Source)
{
    if (Source == nullptr)
        return;

    // The ObjectDb set by SystemManager::SetSharedListHead (FUN_062e68e0); not null-tested, as
    // in retail.
    unsigned const* const IdBlock = static_cast<unsigned const*>(Source);
    IObjRW* const Candidate = ObjectDb->FindById(IdBlock + 1);

    // Retail tests both Candidate and Candidate+8.
    BaseWatcher Scratch;
    if (Candidate != nullptr)
    {
        Watchable* const Target =
            reinterpret_cast<Watchable*>(reinterpret_cast<unsigned char*>(Candidate) + 8);
        if (Target != nullptr)
            Scratch.set(Target);
    }

    // std::list _Buynode(Head, Head->Prev): a null Prev means the new node itself.
    SystemManagerTimerListNode* const Head = static_cast<SystemManagerTimerListNode*>(ListHeadNode);
    SystemManagerTimerListNode* Prev = Head->Prev;

    SystemManagerTimerListNode* const NewNode =
        static_cast<SystemManagerTimerListNode*>(::operator new(sizeof(SystemManagerTimerListNode)));
    NewNode->Next = Head;
    if (Prev == nullptr)
        Prev = NewNode;
    NewNode->Prev = Prev;
    Head->Prev = NewNode;
    NewNode->Prev->Next = NewNode;

    // FUN_062ed070, the watcher copy-ctor overload reading Scratch's watched pointer.
    Watchable* ScratchTarget = const_cast<Watchable*>(Scratch.target());
    new (NewNode->Watcher) BaseWatcher(&ScratchTarget);

    ++ListCount;

    Scratch.set(nullptr);
}

// FUN_062e86d0. Candidate is an unidentified per-object record accessed via raw offsets.
void SystemManagerTimerTable::AccumulateTimerLevel(void* Candidate)
{
    // Only the param is tested; Owner and its SelfTarget.Iface are dereferenced blind.
    if (Candidate == nullptr)
        return;

    int const CurrentId = static_cast<int>(Owner->SelfTarget.Iface->get_id());

    int CandidateId;
    std::memcpy(&CandidateId, static_cast<unsigned char*>(Candidate) + 0x08, sizeof(int));
    int CandidateState;
    std::memcpy(&CandidateState, static_cast<unsigned char*>(Candidate) + 0x14, sizeof(int));

    if (CandidateId == CurrentId && 1 < CandidateState && CandidateState < 8)
    {
        float Value;
        std::memcpy(&Value, static_cast<unsigned char*>(Candidate) + 0x18, sizeof(float));
        Level += Value;
    }
}

// FUN_062e8870. Retail re-tests Best > 0 before each of the three compares; the answer cannot
// change, so one test suffices. Each compare is true on less or equal. Owner is read without
// a check.
bool SystemManagerTimerTable::HasMissileReactionTarget() const
{
    float const Best = BestValueSoFar;
    if (!(Best > 0.0f))
        return false;

    pub::AI::Personality::MissileReactionStruct const& Reaction = Owner->PersonalityData.MissileReaction;
    return Best <= Reaction.EvadeBreakMissileReactionTime || Best <= Reaction.EvadeSlideMissileReactionTime ||
           Best <= Reaction.EvadeAfterburnMissileReactionTime;
}

// FUN_062e6c60, RET 4; this = &Sys->Timers. DecayRate is MaxShield + MaxHitPts.
bool SystemManagerTimerTable::Arm(SystemManager* Owner)
{
    if (Owner == nullptr)
        return false;

    Watchable const* const WatcherTarget = Owner->Watcher.target();
    if (WatcherTarget == nullptr || reinterpret_cast<std::uintptr_t>(WatcherTarget) == 8)
        return false;

    this->Owner = Owner;

    // Retail re-reads the watcher here.
    IObjRW* const Object = ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()));
    // The scanner equipment id the AI target list rebuild is gated on.
    if (static_cast<IObjInspect*>(Object)->find_equipment(&ScannerEquipId, 1, EquipTypeScanner) != 0)
        return false;

    float HitPts = 0.0f;
    float MaxHitPts = 0.0f;
    (void)static_cast<IObjInspect*>(Object)->get_status(HitPts, MaxHitPts);

    float MaxShield = 0.0f;
    bool bShieldActive = false;
    (void)static_cast<IObjInspect*>(Object)->get_shield_status(HitPts, MaxShield, bShieldActive);

    bBuilt = 0;
    bKeepCache = 0;
    DecayRate = MaxShield + MaxHitPts;

    return true;
}

// ============================================================================
// FUN_062e72d0 -- rebuild the timer thresholds from Personality. `this` is
// &SystemManager::Timers, not the ThrustManager (see common_systemmanager.h).
// Each slot gets TriggerThreshold = <percent> * DecayRate and Duration = <time>
// (1.0f for slots 0-2); the slot index follows from the write offset.
// ============================================================================
bool SystemManagerTimerTable::RefreshTimerThresholdsFromPersonality()
{
    SystemManager* const Sys = Owner;
    if (Sys == nullptr)
        return true;

    pub::AI::Personality const& P = Sys->PersonalityData;
    pub::AI::Personality::DamageReactionStruct const& D = P.DamageReaction;
    pub::AI::Personality::FormationUseStruct const& F = P.FormationParams;

    float const Scale = DecayRate;

    SustainedThresholdTimer* const Timers = this->Timers;

    // Slot 0 (Personality +0x1770)
    Timers[0].TriggerThreshold = D.EvadeBreakDamageTriggerPercent * Scale;
    Timers[0].Duration = 1.0f;

    // Slot 1 (+0x1774)
    Timers[1].TriggerThreshold = D.EvadeDodgeMoreDamageTriggerPercent * Scale;
    Timers[1].Duration = 1.0f;

    // Slot 2 (+0x1798)
    Timers[2].TriggerThreshold = D.BrakeReverseDamageTriggerPercent * Scale;
    Timers[2].Duration = 1.0f;

    // Slot 3 (+0x1780 / +0x1784)
    Timers[3].TriggerThreshold = D.EngineKillFaceDamageTriggerPercent * Scale;
    Timers[3].Duration = D.EngineKillFaceDamageTriggerTime;

    // Slot 4 (+0x1790 / +0x1794)
    Timers[4].TriggerThreshold = D.AfterburnerDamageTriggerPercent * Scale;
    Timers[4].Duration = D.AfterburnerDamageTriggerTime;

    // Slot 5 (+0x1788 / +0x178c)
    Timers[5].TriggerThreshold = D.RollDamageTriggerPercent * Scale;
    Timers[5].Duration = D.RollDamageTriggerTime;

    // Slot 6 (+0x17a4 / +0x17a8)
    Timers[6].TriggerThreshold = D.FireGunsDamageTriggerPercent * Scale;
    Timers[6].Duration = D.FireGunsDamageTriggerTime;

    // Slot 7 (+0x1778 / +0x177c)
    Timers[7].TriggerThreshold = D.DropMinesDamageTriggerPercent * Scale;
    Timers[7].Duration = D.DropMinesDamageTriggerTime;

    // Slot 8 (+0x179c / +0x17a0)
    Timers[8].TriggerThreshold = D.FireMissilesDamageTriggerPercent * Scale;
    Timers[8].Duration = D.FireMissilesDamageTriggerTime;

    // Slot 9 (+0x17d4 / +0x17d8)
    Timers[9].TriggerThreshold = F.BreakFormationDamageTriggerPercent * Scale;
    Timers[9].Duration = F.BreakFormationDamageTriggerTime;

    // Slot 10 (+0x17dc / +0x17e0)
    Timers[10].TriggerThreshold = F.BreakApartFormationDamageTriggerPercent * Scale;
    Timers[10].Duration = F.BreakApartFormationDamageTriggerTime;

    return true;
}

// ============================================================================
// FUN_062e6d40 -- the missile-reaction candidate scan; `this` is &Timers.
// The nearest closing candidate within EvadeMissileDistance wins; the tail then
// computes a time-to-impact estimate:
//     BestDistance / (|CandidateVel| - |OwnerVel| * dot(norm OwnerVel, norm CandidateVel))
// kept unrounded, and commits it to BestValueSoFar only when strictly less than
// the previous value and strictly greater than zero (unordered fails both).
// SystemManagerTimerTable::HasMissileReactionTarget compares that against three
// MissileReaction time thresholds. Retail calls get_velocity on the owner twice
// in the tail (no CSE); the values are identical.
// ============================================================================
void SystemManagerTimerTable::UpdateCandidateScan(float /*UnusedArg*/)
{
    BaseWatcher* const BestWatcher =
        reinterpret_cast<BaseWatcher*>(BestCandidateWatcherBytes);

    BestWatcher->set(nullptr);

    float const kNoCandidate = 3.402823466e38f;      // FLT_MAX

    // Only ListCount is tested; Owner is dereferenced unguarded inside the loop.
    if (ListCount == 0)
    {
        BestValueSoFar = kNoCandidate;
        return;
    }

    SystemManagerTimerListNode* const Head =
        static_cast<SystemManagerTimerListNode*>(ListHeadNode);
    SystemManagerTimerListNode* Cur = Head->Next;
    if (Cur == Head)
    {
        BestValueSoFar = kNoCandidate;
        return;
    }

    bool bFound = false;
    float BestDistance = kNoCandidate;
    Vector BestCandidateVelocity = { 0.0f, 0.0f, 0.0f };

    while (true)
    {
        BaseWatcher* const NodeWatcher = reinterpret_cast<BaseWatcher*>(Cur->Watcher);
        Watchable const* const Watched = NodeWatcher->target();

        // A null (or literal 8) target is gone: unlink, destroy, free and keep walking --
        // the list's garbage collector.
        if (Watched == nullptr || reinterpret_cast<std::uintptr_t>(Watched) == 8)
        {
            SystemManagerTimerListNode* const Next = Cur->Next;
            SystemManagerTimerListNode* const Prev = Cur->Prev;
            Prev->Next = Next;
            Next->Prev = Prev;
            NodeWatcher->~BaseWatcher();             // FUN_062b57e0 with flag 0
            ::operator delete(Cur);
            --ListCount;
            Cur = Next;
        }
        else
        {
            IObjRW* const Candidate =
                ObjRwFromWatchable(const_cast<Watchable*>(Watched));

            SystemManager* const Owner = this->Owner;

            // Lazily cached centre/radius; Iface is not null-tested.
            if (Owner->SelfTarget.bPosValid == 0)
            {
                Owner->SelfTarget.bPosValid = 1;
                (void)Owner->SelfTarget.Iface->get_physical_radius(
                    Owner->SelfTarget.CachedRadius, Owner->SelfTarget.CachedPos);
            }

            Vector const& CandidatePos = ObjInspectOf(Candidate)->get_position();
            Vector Delta;
            Vec3Assign(&Delta,
                Owner->SelfTarget.CachedPos.x - CandidatePos.x,
                Owner->SelfTarget.CachedPos.y - CandidatePos.y,
                Owner->SelfTarget.CachedPos.z - CandidatePos.z);

            // Keep only candidates closing on the owner (strictly positive, unrounded dot).
            Vector const CandidateVelocity = ObjInspectOf(Candidate)->get_velocity();
            double const Along = ((double)Delta.z * CandidateVelocity.z +
                                  (double)Delta.y * CandidateVelocity.y) +
                                 (double)Delta.x * CandidateVelocity.x;

            if (Along > 0.0)
            {
                // Unrounded for both strict compares.
                double const Distance = std::sqrt(
                    ((double)Delta.z * Delta.z + (double)Delta.y * Delta.y) +
                    (double)Delta.x * Delta.x);

                // Personality+0x23C.
                float const MaxRange = Owner->PersonalityData.MissileReaction.EvadeMissileDistance;

                if (Distance < MaxRange && Distance < BestDistance)
                {
                    BestDistance = (float)Distance;
                    BestCandidateVelocity = ObjInspectOf(Candidate)->get_velocity();
                    BestWatcher->set(Watched);
                    bFound = true;
                }
            }

            Cur = Cur->Next;
        }

        if (ListCount == 0 || Cur == Head)
            break;
    }

    // Owner and its Iface are dereferenced blind below.
    if (!bFound)
    {
        BestValueSoFar = kNoCandidate;
        return;
    }

    // ---- the x87 tail (see the header comment) ----
    Vector OwnerVelocity = this->Owner->SelfTarget.Iface->get_velocity();

    Vector OwnerDirection;
    Vector CandidateDirection;
    (void)VecNormalize3(&OwnerVelocity, &OwnerDirection);
    (void)VecNormalize3(&BestCandidateVelocity, &CandidateDirection);
    float const Dot = ClampedDot3(OwnerDirection, CandidateDirection);

    // Retail calls get_velocity() a second time into the same slot.
    OwnerVelocity = this->Owner->SelfTarget.Iface->get_velocity();

    // Everything below stays on the x87 stack until the final store.
    double const CandidateSpeed = std::sqrt(
        ((double)BestCandidateVelocity.y * BestCandidateVelocity.y +
         (double)BestCandidateVelocity.z * BestCandidateVelocity.z) +
        (double)BestCandidateVelocity.x * BestCandidateVelocity.x);
    double const OwnerSpeed = std::sqrt(
        ((double)OwnerVelocity.z * OwnerVelocity.z +
         (double)OwnerVelocity.y * OwnerVelocity.y) +
        (double)OwnerVelocity.x * OwnerVelocity.x);

    double const ClosingRate = CandidateSpeed - OwnerSpeed * Dot;
    double const Estimate = BestDistance / ClosingRate;

    if (Estimate < BestValueSoFar && Estimate > 0.0)
        BestValueSoFar = (float)Estimate;
}

// ============================================================================
// FUN_062e7010; `this` is &Timers. DeltaTime is passed through to the candidate
// scan, which does not read it.
// ============================================================================
void SystemManagerTimerTable::Update(float DeltaTime)
{
    UpdateCandidateScan(DeltaTime);

    // FUN_062e6a20 eleven times: slots 0-7 inlined, 8-10 as real calls; identical bodies.
    for (int Slot = 0; Slot < 11; ++Slot)
        (void)Timers[Slot].Update(DeltaTime, Level);

    // Decay only while the old Level is positive. The unrounded difference is tested, so
    // only a strictly negative result clamps to 0.
    if (Level > 0.0f)
    {
        double const NewLevel = Level -
            (double)DecayScale * DecayRate * DeltaTime;
        Level = (float)NewLevel;
        if (NewLevel < 0.0)
            Level = 0.0f;
    }
}
