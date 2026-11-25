// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- TargetManager (common_iface.h): the AI target arbiter embedded in
// SystemManager at +0x142C (retail run 0x062ed570-0x062eec50, vftable .rdata:063a0888).

#include "common_iface.h"         // TargetManager, IBehaviorManager, IObjRW, ObjInspectOf
#include "common_systemmanager.h" // SystemManager
#include "common_aisteer.h"       // FAiTarget, ClampedDot3, GetCachedRange, GetIfacePositionOrZero
#include "AI/common_aisteer_internal.h" // CmnAiSteer::InlineFastLength (retail's inlined x87 fast length)
#include "common_iustategraph.h"  // BuildAiTargetList / GetAiTargetList / ResetAiTargetList
#include "common_pathfind.h"      // PathfindManager user-zone probes (FUN_062ddcd0 / FUN_062db830)
#include "common_cobject.h"       // CShip::get_targeted_enemy_count / get_formation_followers
#include "common_cobjdb.h"        // IObjDB -- TargetManager::GetScanResponse's FindById call
#include "common_misc.h"          // FastInvSqrt, SUBOBJ_ID_NONE
#include "common_universe.h"      // Universe::ISystem::Slot08, Geometry::Sphere

#include <cstdlib>
#include <cstring>

// ============================================================================
// FUN_062ed780 (UpdateAttackTargetOverride) cluster -- sibling callees
// ============================================================================
//
// The receiver of this cluster is SystemManager+0x142C, i.e. SystemManager::Targeting: callers
// pass `*(IBehaviorManager+0x08) + 0x142C`. Its +4 is the SystemManager, whose +4 is the owning
// IBehaviorManager (two hops reach IBehaviorManager::get_current_directive and ContentCbPtr at
// +0x110). There is no separate "IUStateGraph" class: every field this cluster touches is a
// SystemManager field (+0x34 AI target list, +0x1364 FormationLeaders, +0x14FC the
// owner's FAiTarget, +0x1580 ExternalIface, +0x1584 PersonalityData).

// FUN_062ee240 (fastcall). Severity vector -> the first tier of .rdata DAT_063a0818
// ({0,0,0}, {1,0,0}, {2,1,1}) that dominates it component-wise, or 3. Tier meanings are not
// recovered. Only caller: TargetManager::UpdateAttackTargetOverride.
static int ClassifyZoneLevelTier(int const* severity)
{
    static int const kTiers[3][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 2, 1, 1 } };

    for (int tier = 0; tier < 3; ++tier)
    {
        bool dominates = true;
        for (int i = 0; i < 3; ++i)
        {
            if (kTiers[tier][i] < severity[i])
            {
                dominates = false;
            }
        }
        if (dominates)
        {
            return tier;
        }
    }
    return 3;
}

// FUN_062ed670, thiscall (probeSrc, out3). Zone properties of a 20.0f sphere around probeSrc
// (the same query PathfindManager::Update makes). Bit 0x40000 fails (returns 0, out3
// untouched); otherwise out3 = {level from bits 0x1/0x2/0x4, level from 0x8/0x10/0x20,
// bit 0x8000 (Ghidra's `(char)(x>>8) < 0`)} and 1.
int TargetManager::QueryZonePropertyLevels(IObjInspect const* probeSrc, int* out3)
{
    Vector const& pos = probeSrc->get_position();

    Universe::ISystem const* const System = Owner->ObjectDb->GetZoneDb();

    Geometry::Sphere probe;
    probe.Center = pos;
    probe.Radius = 20.0f;

    unsigned const mask = System->GetSphereProperties(&probe);

    if ((mask & 0x40000u) != 0)
    {
        return 0;
    }

    out3[0] = 0;
    out3[1] = 0;
    out3[2] = 0;
    if (mask & 1u) out3[0] = 1;
    if (mask & 2u) out3[0] = 2;
    if (mask & 4u) out3[0] = 3;
    if (mask & 8u) out3[1] = 1;
    if (mask & 0x10u) out3[1] = 2;
    if (mask & 0x20u) out3[1] = 3;
    if (mask & 0x8000u) out3[2] = 1;
    return 1;
}

// FUN_062ee280, thiscall, RET 4 (retail name not exported). Callers: ScanAutoTurretTargets
// (0 or 2) and four sites in UpdateAttackTargetOverride. No-op when AttackState (+0x08)
// already equals NewState; otherwise stores it and, when the owning IBehaviorManager has a
// content callback (+0x110), reports combat_state_notification: state 0 -> (0, owner id,
// owner's target id or 0), 1 -> (2, owner id, 0), 2 -> (4, owner id, 0); other states only
// store. State 0 also refreshes AnchorPosition (+0x3C), callback or not. The owner object is
// resolved with no null guard, as in retail.
void TargetManager::SetAttackState(int NewState)
{
    if (AttackState == NewState)
    {
        return;
    }

    SystemManager* const Sys = Owner;
    AttackState = NewState;
    pub::AI::ContentCallback* const Listener = Sys->Owner->ContentCbPtr;

    switch (NewState)
    {
    case 0:
    {
        if (Listener != nullptr)
        {
            unsigned int TargetId = 0;
            IObjRW* Target = nullptr;
            ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()))->get_target(Target);
            if (Target != nullptr)
            {
                TargetId = Target->get_id();
            }

            // The owner is re-resolved here.
            unsigned int const OwnerId = ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()))->get_id();
            Listener->combat_state_notification(pub::AI::ContentCallback::CSNType0, OwnerId, TargetId);
        }

        // Slot 4 (get_orientation) is the /OPT:ICF fold of get_transform; retail reads the
        // Translation at +0x24 of its result.
        Transform const& Xform = (Transform const&)Sys->SelfTarget.Iface->get_orientation();
        AnchorPosition = Xform.Translation;
        break;
    }
    case 1:
        if (Listener != nullptr)
        {
            unsigned int const OwnerId = ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()))->get_id();
            Listener->combat_state_notification(pub::AI::ContentCallback::CSNType2, OwnerId, 0);
        }
        break;
    case 2:
        if (Listener != nullptr)
        {
            unsigned int const OwnerId = ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()))->get_id();
            Listener->combat_state_notification(pub::AI::ContentCallback::CSNType4, OwnerId, 0);
        }
        break;
    default:
        break;
    }
}

// FUN_062e77e0 is ResetAiTargetList (common_iustategraph.cpp); FUN_062ee390 is
// TargetManager::GetHostilePowerRatio (below).

// TargetManager's two enemy-count clamp bounds, .data dwords initialised to 2. Written only by
// set_player_enemy_clamp; read only by FUN_062ee600 below.
namespace
{
    int GTargetManagerEnemyClampMin = 2; // DAT_063ec754
    int GTargetManagerEnemyClampMax = 2; // DAT_063ec758
}

void TargetManager::set_player_enemy_clamp(int MinClamp, int MaxClamp)
{
    // FUN_062ee5e0; `this` is not touched.
    GTargetManagerEnemyClampMin = MinClamp;
    GTargetManagerEnemyClampMax = MaxClamp;
}

int TargetManager::submit_forced_target(unsigned int TargetId)
{
    // FUN_062eec10
    int const Count = NumForcedTargets;
    if (Count >= MaxForcedTargets)
        return 0;
    for (int Index = 0; Index < Count; ++Index)
    {
        if (ForcedTargets[Index] == TargetId)
            return 0;
    }
    ForcedTargets[Count] = TargetId;
    ++NumForcedTargets;
    return 1;
}

int TargetManager::remove_forced_target(unsigned int TargetId)
{
    // FUN_062eec50; the shift loop re-reads the count each pass.
    int const Count = NumForcedTargets;
    int Index = 0;
    for (; Index < Count; ++Index)
    {
        if (ForcedTargets[Index] == TargetId)
            break;
    }
    if (Index >= Count)
        return 0;
    for (int Move = Index; Move < NumForcedTargets - 1; ++Move)
        ForcedTargets[Move] = ForcedTargets[Move + 1];
    --NumForcedTargets;
    return 1;
}

TargetManager::TargetManager()
{
    // FUN_062ed570. Only these are written; Owner, AttackState, AggressionRatio,
    // SubtargetDistanceSq, NumForcedTargets, ForcedTargets, AnchorPosition, bCommitted,
    // CommitTimer and SelectionPriority are left as the allocation found them.
    NumCandidatesA = 0;
    NumCandidatesB = 0;
    NumSelected = 0;
    bSelectedIsPlayer = false;
    TargetedEnemyCount = 0;
    bPlayerOverClampKept = false;
    bPlayerOverClampDropped = false;
    SubtargetId = SUBOBJ_ID_NONE;
}

// The cache refresh retail inlines twice in enumerate_equip (the same block FaceBehavior and
// FUN_062e6d40 inline); the latch is raised before the call.
static void TargetManager_EnsurePhysicalCacheFresh(SystemManager* Sys)
{
    if (Sys->SelfTarget.bPosValid != 0)
        return;
    Sys->SelfTarget.bPosValid = 1;
    Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                         Sys->SelfTarget.CachedPos);
}

bool TargetManager::enumerate_equip(unsigned short Id, unsigned Type)
{
    // LAB_062ee9d0, vftable .rdata:063a0888 slot 0, RET 8. Only guns (Type 0x80) score: a
    // subpart facing the owner (dot < 0; unordered does not) scores the squared distance to the
    // target; anything else scores FLT_MAX. The value stays unrounded to the compare. Owner's
    // object and the target are not null-tested, as in retail. Never stops the walk.
    double Value = 3.4028235e+38f; // DAT_0639c39c
    if (Type == 0x80)
    {
        TargetManager_EnsurePhysicalCacheFresh(Owner);

        IObjRW* Target = nullptr;
        IObjRW* const Obj = ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()));
        Obj->get_target(Target);

        Vector SubCenter;
        SubCenter.x = 0.0f;
        SubCenter.y = 0.0f;
        SubCenter.z = 0.0f;
        Target->get_sub_obj_center_of_mass(Id, SubCenter);

        Vector TargetPos;
        GetIfacePositionOrZero(&TargetPos, Target);

        Vector ToTarget;
        ToTarget.x = TargetPos.x - Owner->SelfTarget.CachedPos.x;
        ToTarget.y = TargetPos.y - Owner->SelfTarget.CachedPos.y;
        ToTarget.z = TargetPos.z - Owner->SelfTarget.CachedPos.z;
        Vector SubOffset;
        SubOffset.x = SubCenter.x - TargetPos.x;
        SubOffset.y = SubCenter.y - TargetPos.y;
        SubOffset.z = SubCenter.z - TargetPos.z;

        if (ClampedDot3(SubOffset, ToTarget) < 0.0f)
        {
            TargetManager_EnsurePhysicalCacheFresh(Owner);
            double const Dx = (double)(TargetPos.x) - Owner->SelfTarget.CachedPos.x;
            double const Dy = (double)(TargetPos.y) - Owner->SelfTarget.CachedPos.y;
            double const Dz = (double)(TargetPos.z) - Owner->SelfTarget.CachedPos.z;
            Value = (Dz * Dz + Dy * Dy) + Dx * Dx;
        }
    }

    // Only strictly less (ordered) is kept.
    if (Value < (double)(SubtargetDistanceSq))
    {
        SubtargetDistanceSq = (float)(Value);
        SubtargetId = Id;
    }
    return false;
}

bool TargetManager::enumerate_subpart(unsigned short Id, unsigned CollisionGroup)
{
    // 0x0632c0c0 (ICF-folded), vftable slot 1.
    (void)Id;
    (void)CollisionGroup;
    return false;
}

bool TargetManager::Attach(SystemManager* NewOwner)
{
    // FUN_062ed620, RET 4. The target - 8 test cannot fail for a non-null target but is
    // retail's own.
    if (NewOwner == nullptr)
        return false;
    Watchable const* const Watched = NewOwner->Watcher.target();
    if (Watched == nullptr)
        return false;
    if (ObjRwFromWatchable(const_cast<Watchable*>(Watched)) == nullptr)
        return false;
    Owner = NewOwner;
    AttackState = 2;
    NumForcedTargets = 0;
    AggressionRatio = 0.5f;
    AnchorPosition.z = 0.0f;
    AnchorPosition.y = 0.0f;
    AnchorPosition.x = 0.0f;
    CommitTimer = 0.0f;
    bCommitted = false;
    return true;
}

void TargetManager::UpdateCommitTimer(float Dt)
{
    // FUN_062ed740, RET 4. The stored sum is rounded, the compare with 10.0f (DAT_063a0814)
    // reads the unrounded one; the commit lapses only on strictly greater.
    if (!bCommitted)
        return;
    double const Sum = (double)(Dt) + CommitTimer;
    CommitTimer = (float)(Sum);
    if (Sum > 10.0)
    {
        CommitTimer = 0.0f;
        AnchorPosition.z = 0.0f;
        AnchorPosition.y = 0.0f;
        AnchorPosition.x = 0.0f;
        bCommitted = false;
    }
}

IObjRW* TargetManager::GetTarget() const
{
    // FUN_062ed5a0. The owner object is not null-tested, as in retail.
    IObjRW* Target = nullptr;
    IObjRW* const Obj = ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()));
    Obj->get_target(Target);
    return Target;
}

unsigned short TargetManager::GetSubTarget() const
{
    // FUN_062ed5e0; resolved as in GetTarget.
    unsigned short SubTarget = SUBOBJ_ID_NONE;
    IObjRW* const Obj = ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()));
    Obj->get_subtarget(SubTarget);
    return SubTarget;
}

double TargetManager::GetHostilePowerRatio()
{
    // FUN_062ee390
    float Hostile = 0.0f;

    // The owner object is not null-tested, as in retail.
    IObjRW* const Self = ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()));
    float Own = (float)(Self->get_toughness());

    TargetManager_EnsurePhysicalCacheFresh(Owner);

    // FUN_062e7580; End is re-read on every pass.
    FAiTargetVector* const List = BuildAiTargetList(GetAiTargetList(Owner));
    for (FAiTarget* Elem = List->Begin; Elem != List->End; ++Elem)
    {
        // The lazily cached type; the guard is raised after the get_type call.
        if (Elem->bFlagsValid == 0)
        {
            Elem->Iface->get_type(Elem->CachedFlags);
            Elem->bFlagsValid = 1;
        }

        if ((Elem->CachedFlags & 0x7f8110) == 0)
            continue;

        // Three FUN_062e6a80 calls: reverse attitude, then attitude, <= -0.6f (DAT_063a0808,
        // ordered) add to the hostile sum; otherwise a reverse attitude >= 0.6f (DAT_063a080c,
        // ordered) adds to our own.
        float Attitude = 0.0f;
        float Reverse = 0.0f;
        GetCachedRange(Elem, &Attitude, &Reverse);
        bool bHostile = (Reverse <= -0.6f);
        if (!bHostile)
        {
            GetCachedRange(Elem, &Attitude, &Reverse);
            bHostile = (Attitude <= -0.6f);
        }

        if (bHostile)
        {
            Hostile = (float)((double)(Elem->Iface->get_toughness())
                                         + (double)(Hostile));
            continue;
        }

        GetCachedRange(Elem, &Attitude, &Reverse);
        if (Reverse >= 0.6f)
        {
            Own = (float)((double)(Elem->Iface->get_toughness())
                                     + (double)(Own));
        }
    }

    float const Total = Own + Hostile;

    // 0.01 is the double DAT_0639e6b0; unordered returns 0. The quotient is returned unrounded.
    if ((double)(Total) > 0.01)
        return (double)(Hostile) / Total;
    return 0.0;
}

pub::AI::ScanResponse TargetManager::GetScanResponse(uint Type)
{
    // FUN_062eeb70, RET 4. Retail looks Type up as an object id and drops the result.
    Owner->ObjectDb->FindById(&Type);

    // FUN_062e77e0: drop the cached AI target list.
    ResetAiTargetList(GetAiTargetList(Owner));

    double const Ratio = GetHostilePowerRatio(); // unrounded, compared below

    // JobParams.LootFleeThreshold (Personality+0x37C), a TOUGHNESS_TYPE. The range test is
    // unsigned, so anything outside 0..4 takes 0.5f. Constants: DAT_0639e0e0 / 0639e0dc /
    // 0639e0d8 / 0639e0d4 / 0639a1d0 / 0639c13c. Retail inlines this rather than calling
    // Personality::get_toughness_ratio, whose table is the same.
    float Threshold;
    switch (Owner->PersonalityData.JobParams.LootFleeThreshold)
    {
    case 0:
        Threshold = 0.15f;
        break;
    case 1:
        Threshold = 0.35f;
        break;
    case 2:
        Threshold = 0.65f;
        break;
    case 3:
        Threshold = 0.85f;
        break;
    case 4:
        Threshold = 1.0f;
        break;
    default:
        Threshold = 0.5f;
        break;
    }

    // 1 when Ratio is below the threshold or unordered with it.
    if (!(Ratio >= (double)(Threshold)))
        return (pub::AI::ScanResponse)(1);
    return (pub::AI::ScanResponse)(0);
}

// The job attack-order class-mask table, .rdata DAT_063a0848..DAT_063a087b. Indexed by an
// attack-order record's class id; class 12 is the terminator. The last dword (DAT_063a087c)
// looks like a pointer, not a mask -- copied verbatim since no observed record indexes that
// far. Read by MatchesAttackOrder (FUN_062ee530) and SelectAttackCandidate (FUN_062ee600).
static unsigned const GAttackOrderClassMasks[14] = {
    0x00010000u, 0x00020000u, 0x00100000u, 0x00040000u, 0x00080000u, 0x00200000u,
    0x00000080u, 0x00000040u, 0x00001000u, 0x00002000u, 0x0000cd3fu, 0xffffffffu,
    0x00000000u, 0x063a9df4u,
};

bool TargetManager::MatchesAttackOrder(FAiTarget* Candidate)
{
    // FUN_062ee530, RET 4. Counts the 12-byte AttackPreferences records (Sys+0x1834) up to
    // class 12 with no bound, and indexes the mask table with no bounds test, as in retail.
    // The type guard is raised after get_type.
    pub::AI::Personality::JobStruct::FAttackPreference const* const Records =
        Owner->PersonalityData.JobParams.AttackPreferences;
    int Count = 0;
    while (Records[Count].ShipClass != 0xc)
        ++Count;

    for (int Index = 0; Index < Count; ++Index)
    {
        unsigned const Mask = GAttackOrderClassMasks[
            Owner->PersonalityData.JobParams.AttackPreferences[Index].ShipClass];

        if (Candidate->bFlagsValid == 0)
        {
            Candidate->Iface->get_type(Candidate->CachedFlags);
            Candidate->bFlagsValid = 1;
        }

        if ((Mask & Candidate->CachedFlags) != 0)
            return true;
    }
    return false;
}

// FUN_062ee600: candidate/target arbitration. Walks the AttackPreferences tiers (Sys+0x1834,
// stride 0xc: class id, float range, weapon flags), keeps the first non-empty tier's matches
// within range, then prefers players whose targeted-enemy count is below the clamp minimum
// (dropping over-clamp players when others remain), before falling back to a random pick
// with a sticky-target tie-break. Ghidra's 65517-int `aiStackY_40040` array is a frame
// artifact (the frame is 0x70 bytes) and is omitted.
FAiTarget* TargetManager::SelectAttackCandidate(
    FAiTarget** Candidates, int CandidateCount, IObjRW const* StickyTarget, int* OutPriority)
{
    // DAT_063ec754 / DAT_063ec758, read live as retail does.
    int const kEnemyClampMin = GTargetManagerEnemyClampMin;
    int const kEnemyClampMax = GTargetManagerEnemyClampMax;

    *OutPriority = 0xf;

    // Sys+0x1834: the personality's JobParams.AttackPreferences, ended by ShipClass 12.
    pub::AI::Personality::JobStruct::FAttackPreference const* const Prefs =
        Owner->PersonalityData.JobParams.AttackPreferences;

    int tierCount = 0;
    while (Prefs[tierCount].ShipClass != 0xc)
    {
        ++tierCount;
    }

    FAiTarget* candidatesOut[16] = {};
    int matchCount = 0;

    for (int tier = 0; tier < tierCount; ++tier)
    {
        pub::AI::Personality::JobStruct::FAttackPreference const& Pref = Prefs[tier];
        // Retail quirk kept: no bounds test, a class id past the table reads what follows it.
        unsigned const classMask = GAttackOrderClassMasks[Pref.ShipClass];

        matchCount = 0;
        for (int i = 0; i < CandidateCount; ++i)
        {
            FAiTarget* const cand = Candidates[i];

            if (cand->bFlagsValid == 0)
            {
                cand->Iface->get_type(cand->CachedFlags);
                cand->bFlagsValid = 1;
            }

            unsigned typeFlags = classMask & cand->CachedFlags;

            if (cand->Iface->is_player() != 0)
            {
                typeFlags = classMask & 0x30000u;
            }

            if (typeFlags == 0)
            {
                continue;
            }

            // The owner object is not null-tested, as in retail.
            Vector const OwnerPos =
                ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()))->get_position();

            // FAiTarget::RefreshPos inlined (guard raised before the call).
            if (cand->bPosValid == 0)
            {
                cand->bPosValid = 1;
                cand->Iface->get_physical_radius(cand->CachedRadius, cand->CachedPos);
            }

            // Unrounded throughout; retail's inlined fast length.
            double const dx = (double)cand->CachedPos.x - OwnerPos.x;
            double const dy = (double)cand->CachedPos.y - OwnerPos.y;
            double const dz = (double)cand->CachedPos.z - OwnerPos.z;
            double const dist = CmnAiSteer::InlineFastLength((dz * dz + dy * dy) + dx * dx);

            if (dist <= (double)Pref.Range)
            {
                // Stored with no bound; cannot overflow, since the only caller passes at most 16
                // candidates and only one tier's matches survive.
                candidatesOut[matchCount] = cand;
                ++matchCount;
                // The record's +8 dword (the weapon-type mask) is what retail reports.
                *OutPriority = (int)Pref.WeaponFlags;
            }
        }

        if (matchCount > 0)
        {
            break;
        }
    }

    int bestIndex = -1;
    if (matchCount > 0)
    {
        for (int i = 0; i < matchCount; ++i)
        {
            IObjInspect const* const candIface = candidatesOut[i]->Iface;
            if (!candIface->is_player())
            {
                continue;
            }

            // Slot 0x150 is cobject(), declared as returning CObject const*. This site needs
            // the CShip it actually is -- the 0x503 class mask below is CObject::ObjectClass.
            CShip* ship = (CShip*)candIface->cobject();
            if (ship == nullptr || ((unsigned)ship->ObjectClass & 0x503u) != 0x503u)
            {
                continue;
            }

            int const enemyCount = ship->get_targeted_enemy_count();
            if (enemyCount > kEnemyClampMax)
            {
                if (matchCount < 2)
                {
                    bPlayerOverClampKept = true;
                }
                else
                {
                    // Retail: i is not stepped back, so the candidate shifted into slot i is
                    // skipped.
                    std::memmove(&candidatesOut[i], &candidatesOut[i + 1],
                                 (size_t)(matchCount - (i + 1)) * sizeof(FAiTarget*));
                    --matchCount;
                    bPlayerOverClampDropped = true;
                }
            }

            if (enemyCount < kEnemyClampMin)
            {
                bestIndex = i;
                break;
            }
        }

        if (matchCount > 0)
        {
            // rand() runs before the bestIndex test, so a clamp-selected pick still advances
            // the CRT generator.
            int const r = rand();
            int const scaledIndex = (int)((r * matchCount + ((r * matchCount) >> 0x1f & 0x7fff)) >> 0xf);
            if (bestIndex != -1)
            {
                return candidatesOut[bestIndex];
            }
            int pickIndex = scaledIndex;

            if (StickyTarget != nullptr)
            {
                bool found = false;
                int j = 0;
                for (; j < matchCount; ++j)
                {
                    // The candidate's +0x84 Iface compared with the sticky object (IObjInspect
                    // is IObjRW's base at offset 0, so the pointer values are the same).
                    if (candidatesOut[j]->Iface == StickyTarget)
                    {
                        found = true;
                        break;
                    }
                }
                pickIndex = found ? j : scaledIndex;
            }

            return candidatesOut[pickIndex];
        }
    }

    return nullptr;
}

// Shared tail FUN_062ed780 jumps into from two sites (LAB_062ed80f / LAB_062ed811): when the
// owner has a target, clear it through the owner's IObjAffect sub-object (+4):
// set_target(nullptr, SUBOBJ_ID_NONE, -1). Retail pushes a dword whose high word is stale
// (the Pathfind seed), but SubTarget is an unsigned short, so it never reaches the callee.
// Neither owner resolution has a null guard in retail.
static void NotifyOverrideCommitTail(SystemManager* Sys)
{
    IObjRW* OwnerTarget = nullptr;
    ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()))->get_target(OwnerTarget);
    if (OwnerTarget == nullptr)
    {
        return;
    }
    IObjRW* const Owner = ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()));
    static_cast<IObjAffect*>(Owner)->set_target(nullptr, SUBOBJ_ID_NONE, -1);
}

// FUN_062ed780 (no exported name): the per-tick wingman attack-coordination dispatcher on
// SystemManager::Targeting. It (a) resolves/commits a leader-driven target override via the
// Sys+0x1364 block, (b) handles the already-committed / drift case, (c) decodes the zone
// tier and the aggression-ratio gate (FUN_062ee390) that can short-circuit into
// SetAttackState, and (d) gathers nearby attack candidates in two neighbour scans and
// arbitrates them via FUN_062ee600.
void TargetManager::UpdateAttackTargetOverride()
{
    SystemManager* const Sys = Owner;
    IBehaviorManager* behaviorMgr = Sys->Owner;
    // Loaded at entry; the receiver of the two user-zone probes.
    PathfindManager* const probe = behaviorMgr->Pathfind;
    IObjRW* const owner0 = ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()));
    // Sys+0x1584, the personality; JobParams is at +0x2A0 (Sys+0x1824).
    pub::AI::Personality::JobStruct const& Job = Sys->PersonalityData.JobParams;

    NumCandidatesA = 0;
    NumCandidatesB = 0;
    NumSelected = 0;
    bSelectedIsPlayer = false;
    TargetedEnemyCount = 0;
    bPlayerOverClampKept = false;
    bPlayerOverClampDropped = false;

    // --- (a) leader override ---
    // FUN_062ecaf0 on the Sys+0x1364 block.
    IObjRW* const overrideTarget = Sys->FormationLeaders.GetCurrentLeader();

    bool skipDirectiveGate = false; // retail's LAB_062ed83d falls into LAB_062ed896 directly
    // Retail's `local_f4`, read again deep in the arbitration tail, so hoisted here.
    IObjRW* directiveProbeTarget = nullptr;
    if (overrideTarget != nullptr && overrideTarget != owner0)
    {
        unsigned char const committedFlag = Sys->FormationLeaders.bLeaderIdleFormation;
        if (committedFlag != 1)
        {
            // LAB_062ed83d: with JobParams.bWaitForLeaderTarget set and a leader without a
            // target, suppress this tick. Otherwise skip the directive gate and go on to the
            // committed check; the override is not applied on this path either way.
            overrideTarget->get_target(directiveProbeTarget);
            if (Job.bWaitForLeaderTarget == 1 && directiveProbeTarget == nullptr)
            {
                return;
            }
            skipDirectiveGate = true;
        }
        else
        {
            NotifyOverrideCommitTail(Sys);
            return;
        }
    }

    // --- LAB_062ed866: directive gate ---
    if (!skipDirectiveGate)
    {
        IDirectiveInfo const* directive = behaviorMgr->get_current_directive();
        unsigned char const* directiveBytes = reinterpret_cast<unsigned char const*>(directive);
        int const directiveKind = *reinterpret_cast<int const*>(directiveBytes);
        int const directiveArg = *reinterpret_cast<int const*>(directiveBytes + 4);
        int const directiveTag = *reinterpret_cast<int const*>(directiveBytes + 8);
        bool const isIdleDirective = (directiveKind == 1 && directiveArg == 0);
        if (directiveTag != 0x14 && directiveTag != 6 && isIdleDirective)
        {
            return;
        }
    }

    // --- (b) LAB_062ed896: already committed / drifted from the anchor ---
    if (bCommitted == 1)
    {
        return;
    }

    // The owner object is not null-tested, as in retail.
    Vector const selfPos =
        ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()))->get_position();

    bool committedThisTick = false;
    if (AttackState == 0)
    {
        // The differences are rounded to float; the length runs on the unrounded sum.
        float const dx = AnchorPosition.x - selfPos.x;
        float const dy = AnchorPosition.y - selfPos.y;
        float const dz = AnchorPosition.z - selfPos.z;
        double const dist = CmnAiSteer::InlineFastLength(
            ((double)dx * dx + (double)dz * dz) + (double)dy * dy);

        // JobParams.CombatDriftDistance (Sys+0x1830); below or unordered skips.
        if (dist >= (double)Job.CombatDriftDistance)
        {
            pub::AI::ContentCallback* const Listener = behaviorMgr->ContentCbPtr;
            if (Listener != nullptr)
            {
                Listener->combat_drift_event();
            }
            bCommitted = true;
            SetAttackState(2);
            committedThisTick = true;
        }
    }

    // Retail falls into the same shared tail (LAB_062ed811) as (a).
    if (committedThisTick)
    {
        NotifyOverrideCommitTail(Sys);
        return;
    }

    // --- (c) zone tier ---
    // AmbientScriptInfo::clear (exported) is a 3-dword zero-fill of this local buffer; its
    // class is not modelled.
    int ambientFlags[3] = { 0, 0, 0 };
    bool const decoded = QueryZonePropertyLevels(Sys->SelfTarget.Iface, ambientFlags) != 0;
    bool useAmbientTier = false;
    if (decoded)
    {
        int const ambientTier = ClassifyZoneLevelTier(ambientFlags);
        useAmbientTier = (ambientTier <= Job.FieldTargeting);   // Sys+0x18f8
    }
    // `useAmbientTier` (retail's frame byte +0x1e) gates one filter in the second neighbour
    // scan below.

    // --- (d) first neighbour scan (LAB_062eda00-LAB_062edb4e) ---
    // Walks the owner's AI target list: every hostile entry bumps the counters, and anything
    // maximally hostile and close tightens the hostility threshold the second scan uses.
    float hostileThreshold = -0.6f;              // .rdata:063a0808
    // Retail keeps the vector pointer in a frame slot and the second scan walks the same
    // object, so it is hoisted here.
    FAiTargetVector* savedTargets = nullptr;
    {
        FAiTargetList* const targetList = GetAiTargetList(Sys);
        FAiTargetVector* const targets = BuildAiTargetList(targetList);
        savedTargets = targets;

        // JobParams.RangeThreshold (Sys+0x1824) picks a fraction of 5000.0f (.rdata:063a0810): the
        // doubles 2/3 and 1/3 at .rdata:0639e0f0 / 0639e0e8 for 1 and 2, else all of it. The
        // compare below is done in double.
        int const proximityTierSel = Job.RangeThreshold;
        double proximityLimit = 5000.0;
        if (proximityTierSel == 1)
        {
            proximityLimit = 5000.0 * 0.66666666666666663;
        }
        else if (proximityTierSel == 2)
        {
            proximityLimit = 5000.0 * 0.33333333333333331;
        }

        for (FAiTarget* elem = targets->Begin; elem != targets->End; ++elem)
        {
            float attitude = 0.0f;
            float reverseAttitude = 0.0f;
            GetCachedRange(elem, &attitude, &reverseAttitude);

            if (!(attitude <= -0.6f))
            {
                continue;
            }
            ++NumCandidatesA;

            // Only maximally hostile entries (.rdata:063a0804) reach the proximity test.
            if (!(attitude <= -1.0f))
            {
                continue;
            }
            ++NumCandidatesB;

            // Slot +0x10 (get_orientation), ICF-folded with get_transform; retail reads the
            // Translation past the Matrix.
            Vector const& elemPos = ((Transform const&)elem->Iface->get_orientation()).Translation;

            // selfPos is retail's stack snapshot of the owner position. The differences are
            // rounded to float; the length runs unrounded.
            float const ndx = elemPos.x - selfPos.x;
            float const ndy = elemPos.y - selfPos.y;
            float const ndz = elemPos.z - selfPos.z;
            double const nDist = CmnAiSteer::InlineFastLength(
                ((double)ndx * ndx + (double)ndz * ndz) + (double)ndy * ndy);

            // Only strictly below passes.
            if (nDist < proximityLimit)
            {
                hostileThreshold = -1.0f;
            }
        }
    }

    // --- aggression-ratio early return ---
    // Retail's raw float bit patterns (0.15/0.35/0.65/0.85/1.0, default 0.5).
    static unsigned const kAggressionThresholdBits[5] = {
        0x3e19999au, 0x3eb33333u, 0x3f266666u, 0x3f59999au, 0x3f800000u,
    };
    static unsigned const kAggressionThresholdDefaultBits = 0x3f000000u;
    int const aggressionTierSel = Job.SceneToughnessThreshold;   // Sys+0x1828
    unsigned const aggressionThresholdBits =
        (aggressionTierSel >= 0 && aggressionTierSel <= 4)
            ? kAggressionThresholdBits[aggressionTierSel]
            : kAggressionThresholdDefaultBits;
    float aggressionThreshold;
    std::memcpy(&aggressionThreshold, &aggressionThresholdBits, sizeof(float));

    // The stored copy is rounded; both compares read the unrounded ratio.
    double const aggressionRatio = GetHostilePowerRatio();
    AggressionRatio = (float)aggressionRatio;
    if (aggressionRatio != 0.0 && (double)aggressionThreshold < aggressionRatio)
    {
        SetAttackState(1);
        return;
    }

    // --- second neighbour scan (.text:062edbdf-062edf33) ---
    // The probe receiver is the PathfindManager loaded at entry.

    // One up-front probe query whose direction is discarded; only "did it answer" is kept.
    Vector probeDirScratch = { 0.0f, 0.0f, 0.0f };
    bool const skipContainmentTest =
        (probe->GetUserZoneApproachDirection(&probeDirScratch) == 0); // FUN_062ddcd0

    // 16 entries; retail's own guard at both insertion points drops a candidate rather than
    // growing.
    FAiTarget* candidateSlots[16] = {};
    int matchCount = 0;

    for (FAiTarget* cand = savedTargets->Begin; cand != savedTargets->End; ++cand)
    {
        // A side effect, not a filter: refresh the targeted-enemy count from any player CShip
        // in the list.
        if (cand->Iface->is_player())
        {
            CShip* const candShip =
                (CShip*)cand->Iface->cobject();
            if (candShip != nullptr && ((unsigned)candShip->ObjectClass & 0x503u) == 0x503u)
            {
                TargetedEnemyCount = candShip->get_targeted_enemy_count();
            }
        }

        // Forced targets are admitted immediately -- and retail still runs the whole filter
        // chain on them, so one that also passes is inserted twice. Retail quirk kept; no
        // early break, count re-read each pass.
        for (int pinned = 0; pinned < NumForcedTargets; ++pinned)
        {
            if (cand->Iface->get_id() != ForcedTargets[pinned])
            {
                continue;
            }
            if (matchCount < 16)
            {
                candidateSlots[matchCount] = cand;
                ++matchCount;
            }
        }

        if (cand->bFlagsValid == 0)
        {
            cand->Iface->get_type(cand->CachedFlags);
            cand->bFlagsValid = 1;
        }

        // Anything whose flags survive the 0xFDFF mask must also be alive. The threshold is
        // the double 0.0 at DAT_0639c430 and only exact equality rejects: NaN passes.
        if ((cand->CachedFlags & 0xfdffu) != 0)
        {
            float health = 1.0f; // retail seeds the out slot with 1.0f
            cand->Iface->get_status(health);
            if ((double)health == 0.0)
            {
                continue;
            }
        }

        // JobParams.bAllowPlayerTargeting (Sys+0x1952).
        if (Job.bAllowPlayerTargeting == 0 && cand->Iface->is_player())
        {
            continue;
        }

        // The first scan's gate, against its possibly tightened threshold.
        float candAttitude = 0.0f;
        float candReverseAttitude = 0.0f;
        GetCachedRange(cand, &candAttitude, &candReverseAttitude);
        if (!(candAttitude <= hostileThreshold))
        {
            continue;
        }

        // Proximity to the owner against the same scaled 5000.0f limit, all unrounded with
        // retail's inlined fast length; only strictly closer passes. Slot +0x10 again.
        Vector const& candPos = ((Transform const&)cand->Iface->get_orientation()).Translation;
        double const cdx = (double)candPos.x - selfPos.x;
        double const cdy = (double)candPos.y - selfPos.y;
        double const cdz = (double)candPos.z - selfPos.z;
        double const candDist = CmnAiSteer::InlineFastLength((cdz * cdz + cdy * cdy) + cdx * cdx);

        int const proximityTierSel2 = Job.RangeThreshold;   // Sys+0x1824
        double proximityLimit2 = 5000.0; // .rdata:DAT_063a0810
        if (proximityTierSel2 == 1)
        {
            proximityLimit2 = 5000.0 * 0.66666666666666663; // DAT_0639e0f0
        }
        else if (proximityTierSel2 == 2)
        {
            proximityLimit2 = 5000.0 * 0.33333333333333331; // DAT_0639e0e8
        }
        if (!(candDist < proximityLimit2))
        {
            continue;
        }

        // With a leader override, the candidate must also be within
        // JobParams.MaximumLeaderTargetDistance (Sys+0x18f4) of the leader; strictly greater
        // rejects.
        if (overrideTarget != nullptr && overrideTarget != owner0)
        {
            Vector const& candPos2 = ((Transform const&)cand->Iface->get_orientation()).Translation;
            Vector const& ovrPos = overrideTarget->get_position();
            double const odx = (double)candPos2.x - ovrPos.x;
            double const ody = (double)candPos2.y - ovrPos.y;
            double const odz = (double)candPos2.z - ovrPos.z;
            double const ovrDist = CmnAiSteer::InlineFastLength((odz * odz + ody * ody) + odx * odx);
            if (ovrDist > (double)Job.MaximumLeaderTargetDistance)
            {
                continue;
            }
        }

        // The zone-tier gate, per candidate this time.
        if (useAmbientTier)
        {
            int candAmbientFlags[3] = { 0, 0, 0 };
            if (QueryZonePropertyLevels(cand->Iface, candAmbientFlags) == 0)
            {
                continue;
            }
            if (ClassifyZoneLevelTier(candAmbientFlags) > Job.FieldTargeting)   // Sys+0x18f8
            {
                continue;
            }
        }

        // The volume-containment test, skipped when the up-front probe query returned 0.
        if (!skipContainmentTest)
        {
            if (cand->bPosValid == 0)
            {
                cand->Iface->get_physical_radius(cand->CachedRadius, cand->CachedPos);
                cand->bPosValid = 1;
            }
            float radiusScratch = cand->CachedRadius; // the arg FUN_062db830 never reads
            // Retail repeats the bPosValid refresh here; dead, not reproduced.
            if (!probe->UserZoneContainsPoint(&cand->CachedPos)) // FUN_062db830
            {
                continue;
            }
            (void)radiusScratch;
        }

        if (matchCount < 16)
        {
            candidateSlots[matchCount] = cand;
            ++matchCount;
        }
    }

    // --- LAB_062edf39: arbitration ---
    NumSelected = matchCount;
    int selectionPriority = 0xf; // retail's own seed
    if (matchCount > 0)
    {
        // The current target is the sticky candidate; retail re-reads the owner here, with no
        // null guard.
        IObjRW* stickyTarget = nullptr;
        ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()))->get_target(stickyTarget);

        FAiTarget* const selected = SelectAttackCandidate(candidateSlots, matchCount,
                                                          stickyTarget, &selectionPriority);
        if (selected != nullptr)
        {
            IObjInspect* const selectedIface = selected->Iface;

            if (selectedIface->is_player())
            {
                bSelectedIsPlayer = true;
            }
            SelectionPriority = selectionPriority;

            // Commit the new target unless the owner is already on it (same pointer, or same
            // id when both are non-null). SUBOBJ_ID_NONE (.rdata:063a4aa4) is 0.
            unsigned const subObjNone = 0; // SUBOBJ_ID_NONE
            // The owner is re-resolved with no null guard, as in retail.
            IObjRW* currentTarget = nullptr;
            ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()))->get_target(currentTarget);

            bool bAlreadyOnTarget = ((void*)currentTarget == (void*)selectedIface);
            if (!bAlreadyOnTarget && currentTarget != nullptr && selectedIface != nullptr)
            {
                // The current target's get_id runs first.
                unsigned int const CurrentId = currentTarget->get_id();
                bAlreadyOnTarget = (CurrentId == selectedIface->get_id());
            }
            if (!bAlreadyOnTarget)
            {
                (void)static_cast<IObjAffect*>(ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target())))
                    ->set_target((IObjRW*)selectedIface, (unsigned short)subObjNone, -1);
            }

            SetAttackState(0);
            // JobParams.bForceAttackFormation (Sys+0x1958).
            if (Job.bForceAttackFormation == 0)
            {
                return;
            }

            if (skipDirectiveGate)
            {
                // Leader-override path: the owner is re-pointed at the leader's target
                // (directiveProbeTarget) unless already on it. No null guards on the owner.
                IObjRW* ownerTargetD = nullptr;
                ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()))->get_target(ownerTargetD);
                if (directiveProbeTarget == ownerTargetD)
                {
                    return;
                }
                IObjRW* ownerTargetE = nullptr;
                ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()))->get_target(ownerTargetE);
                if (ownerTargetE == directiveProbeTarget)
                {
                    return;
                }
                if (ownerTargetE != nullptr && directiveProbeTarget != nullptr)
                {
                    // The owner's target's get_id runs first.
                    unsigned int const OwnerTargetId = ownerTargetE->get_id();
                    if (OwnerTargetId == directiveProbeTarget->get_id())
                    {
                        return;
                    }
                }
                (void)static_cast<IObjAffect*>(ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target())))
                    ->set_target(directiveProbeTarget, (unsigned short)subObjNone, -1);
                return;
            }

            // No leader override: push the new target onto every non-player formation
            // follower of the owner's ship instead.
            CShip* ownerShip = (CShip*)
                ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()))->cobject();
            if (ownerShip == nullptr || ((unsigned)ownerShip->ObjectClass & 0x503u) != 0x503u)
            {
                ownerShip = nullptr;
            }
            IObjRW* followers[0x20] = { nullptr };
            // Retail calls get_formation_followers even on a null ship (it faults in the
            // callee); reproduced.
            unsigned const followerCount =
                (unsigned)ownerShip->get_formation_followers(followers, 0x20);
            // Unsigned count and index.
            for (unsigned f = 0; f < followerCount; ++f)
            {
                IObjRW* const follower = followers[f];
                if (follower == nullptr || ObjInspectOf(follower)->is_player())
                {
                    continue;
                }
                (void)ObjAffectOf(follower)->set_target((IObjRW*)selectedIface,
                                                   (unsigned short)subObjNone, -1);
            }
            return;
        }
        // A null pick falls through to LAB_062ee1ab below.
    }

    // --- LAB_062ee1ab: nothing selected ---
    // When the owner still holds a target, retail clears it. No null guards on the owner.
    {
        IObjRW* previousTarget = nullptr;
        ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()))->get_target(previousTarget);
        if (previousTarget != nullptr)
        {
            (void)static_cast<IObjAffect*>(ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target())))
                ->set_target(nullptr, SUBOBJ_ID_NONE, -1);
        }
    }
    SetAttackState(2);
}

// ---------------------------------------------------------------------------
// TargetManager::RetargetOwner -- FUN_062ee940, RET 8. Used by FaceBehavior::evaluate.
// ---------------------------------------------------------------------------
bool TargetManager::RetargetOwner(IObjRW* Target, unsigned short SubTarget)
{
    // No null guard on the ship, as in retail.
    IObjRW* Current = nullptr;
    IObjRW* const Ship = ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()));
    Ship->get_target(Current);

    if (Current == Target)
        return false;
    if (Current != nullptr && Target != nullptr &&
        Current->get_id() == Target->get_id())
        return true;

    // The ship is re-resolved, then its IObjAffect sub-object.
    IObjRW* const Again = ObjRwFromWatchable(const_cast<Watchable*>(Owner->Watcher.target()));
    static_cast<IObjAffect*>(Again)->set_target(Target, SubTarget, -1);
    return true;
}
