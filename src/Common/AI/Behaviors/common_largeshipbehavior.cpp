// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_largeshipbehavior.h"
#include "common_aisteer.h"        // FAiTarget, GetCachedRange, RaySphereIntersect
#include "common_iustategraph.h"   // BuildAiTargetList / GetAiTargetList (FUN_062e7580)
#include "common_systemmanager.h"  // SystemManager, FAiTargetVector
#include "common_ini.h"            // Vec3Assign (FUN_0628b030)
#include "AI/common_aisteer_internal.h" // CmnAiSteer::InlineFastLength

#include <cmath>  // sqrtf, for the two places retail uses a real FSQRT rather than the bit-hack

// FUN_062cfe20. Move is a MoveIdea (FUN_062d4e60), Face a FaceIdea (FUN_062d6750).
LargeShipBehavior::LargeShipBehavior(int InTypeId)
    : Behavior(InTypeId)
    , AverageGunRange(1500.0f)
    , Move()
    , Face()
    , AvoidanceScanCooldown(0.0f)
    , ThreatCooldown(0.0f)
    , AvoidanceGoalPos{}
{
}

// FUN_062cfec0 (scalar deleting FUN_062cfea0): Face's watcher (+0xbc) is cleared before
// Move's (+0x44); ~Behavior (FUN_062d2080) chains after.
LargeShipBehavior::~LargeShipBehavior()
{
    Face.Watcher.set(nullptr);
    Move.Watcher.set(nullptr);
}

// Slot 1, LAB_062cff40. Retail does not test the BindContext result before binding the ideas
// (they run with whatever +0x14/+0x24 hold); it only returns it.
bool LargeShipBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    bool const bBound = BindContext(Owner, Manager, Sys);

    Move.SetContext(OwnerContext, SysMgr, Manager);

    // FUN_062d5a10, called non-virtually like all MoveIdea setters.
    Move.SetEnabled(true);

    Face.SetContext(OwnerContext, SysMgr, Manager);

    return bBound;
}

// Slot 3, LAB_062cffa0.
void LargeShipBehavior::OnEngage()
{
    Behavior::OnEngage();

    // Retail calls the ICF fold named TextRenderContext::set_origin_h with pi (0x40490fdb),
    // restoring MoveIdea's ctor default.
    Move.SetAngle38(3.14159274f);

    SetAvoidanceAllowed(0, kAvoidanceObstacleNudge | kAvoidancePoint);                                            // FUN_062d21d0

    // FUN_062eb5a0 (ComputeGunRangeStats): the average, not the max.
    float OutAverage = 0.0f;
    float OutMax = 0.0f;
    if (SysMgr->GunGroups.ComputeGunRangeStats(&OutAverage, &OutMax))
        AverageGunRange = OutAverage;
}

// Slot 4, FUN_062d09e0. Face commits before Move.
void LargeShipBehavior::OnDisengage()
{
    Face.Commit();
    Move.Commit();

    Behavior::OnDisengage();
}

// Slot 5, LAB_062cfff0.
int LargeShipBehavior::OnEvaluateReadiness(float Value)
{
    (void)Value;   // argument unread
    int Mode = 2;

    // The mask is tested on the out-parameter; get_type's return value is discarded.
    unsigned ObjectType = 0;
    ObjInspectOf(OwnerContext)->get_type(ObjectType);
    if ((ObjectType & OBJ_CRUISER) != 0)
    {
        if (SysMgr->Targeting.GetTarget() != nullptr)      // FUN_062ed5a0
            Mode = 1;
    }

    if (Flag29 != 0)
    {
        Readiness = 3;
        return 3;
    }
    Readiness = Mode;
    return Mode;
}

// ============================================================================
// Update()'s FAiTarget helpers. Update()'s on-stack scratch is one ordinary FAiTarget
// (0x88 bytes): the two arguments are pushed before the `LEA ECX,[ESP+0x60]`, so it and the
// later `LEA ECX,[ESP+0x58]` are the same base. FAiTarget::Init (FUN_062d09b0) and
// RefreshPos (FUN_062c47c0) are in common_aitarget.cpp.
// ============================================================================

// The lazily-cached type-flags fetch retail inlines at six sites in Update(). Unlike
// RefreshPos, the guard is set AFTER the call.
static void RefreshAiTargetFlags(FAiTarget& Self)
{
    if (Self.bFlagsValid != 0)
    {
        return;
    }
    Self.Iface->get_type(Self.CachedFlags);   // no null test, as retail
    Self.bFlagsValid = 1;
}

// The owning ship's IObjInspect: SystemManager+0x08 watcher target, -8 Watchable adjust.
// Retail inlines it three times and re-reads it each iteration.
static IObjInspect const* OwnerObjInspectOf(SystemManager* Sys)
{
    Watchable const* const Watched = Sys->Watcher.target();
    if (Watched == nullptr)
    {
        return nullptr;
    }
    unsigned char* const Raw = reinterpret_cast<unsigned char*>(const_cast<Watchable*>(Watched)) - 8;
    return static_cast<IObjInspect const*>(reinterpret_cast<IObjRW*>(Raw));
}

// Retail inlines the 0x5f3759df fast length three times in Update(); the squared length and
// the Newton step stay unrounded (CmnAiSteer::InlineFastLength).
static double FastLengthFromSquared(double LengthSq)
{
    return CmnAiSteer::InlineFastLength(LengthSq);
}

// ----------------------------------------------------------------------------
// Slot 7, LAB_062d0050 (no Ghidra decompile; from the raw listing).
//
// The byte the ThreatCooldown clamp writes ([ESP+0x27], read at 062d0274 as [ESP+0x2b] after
// a PUSH) gates the second half of the avoidance scan: while ThreatCooldown has not run down
// this tick, retail averages the hostile-contact positions and stops, committing both ideas
// instead of steering (bThreatCooldownExpired below).
// ----------------------------------------------------------------------------
int LargeShipBehavior::Update()
{
    IObjRW* const Handle = SysMgr->Targeting.GetTarget();
    if (Handle == nullptr)
    {
        UpdateResult = kBehaviorUpdateFinished;
        return kBehaviorUpdateFinished;
    }

    // AttitudeRefIface is the behaviour's own threat source, Iface the current target.
    // Deviation: zero-initialised where retail leaves the unused slices indeterminate; every
    // field read below is set by Init or one of the lazy refreshes.
    FAiTarget Scratch = {};
    Scratch.Init(ObjInspectOf(OwnerContext), ObjInspectOf(Handle));

    // Clamped at zero (DAT_0639c430 = 0.0 double).
    bool bThreatCooldownExpired = false;
    ThreatCooldown -= DeltaTime;
    if (ThreatCooldown <= 0.0f)
    {
        ThreatCooldown = 0.0f;
        bThreatCooldownExpired = true;
    }

    RefreshAiTargetFlags(Scratch);
    if ((Scratch.CachedFlags & (OBJ_STATION | OBJ_CRUISER | OBJ_CAPITAL)) != 0)
    {
        // Steer straight at the threat and hold that decision for 30 seconds.
        Scratch.RefreshPos();
        Vector const ThreatPos = Scratch.CachedPos;

        Move.SetRange3c(500.0f);  // DAT_0639f55c
        Move.SetMode(kMoveModeEngine);
        Move.SetScale40(0.6f);    // DAT_0639f554
        Face.SetPosition(ThreatPos);
        Move.SetPosition(ThreatPos);

        ThreatCooldown = 30.0f;  // DAT_0639f550
        UpdateResult = kBehaviorUpdateRunning;
        return kBehaviorUpdateRunning;  // early return: this path skips the shared tail's tick calls
    }

    // A separate cooldown gates the avoidance scan (DAT_06399408 = 0.0f).
    AvoidanceScanCooldown -= DeltaTime;
    if (AvoidanceScanCooldown < 0.0f)
    {
        AvoidanceScanCooldown = 5.0f;  // DAT_0639f54c

        SystemManager* const Sys = SysMgr;

        // The owning ship's own world position; no null test, as retail.
        Vector const SelfPos = OwnerObjInspectOf(Sys)->get_position();

        // Rebuilt (or reused within one IBehaviorManager tick), see common_iustategraph.h.
        Vector HostileCentre = {0.0f, 0.0f, 0.0f};
        int HostileCount = 0;
        FAiTargetVector* const List = BuildAiTargetList(GetAiTargetList(Sys));

        // ---- Pass 1: sum the positions of every candidate whose cached attitude is
        // ---- hostile enough.
        for (FAiTarget* Elem = List->Begin; Elem != List->End; ++Elem)
        {
            float Attitude = 0.0f;
            float AttitudeHigh = 0.0f;
            GetCachedRange(Elem, &Attitude, &AttitudeHigh);

            // DAT_0639f540 = -0.6f here, not the -0.2f the other consumer uses. Iface is
            // dereferenced with no null test.
            if (Attitude <= -0.6f)
            {
                // Retail dispatches slot +0x10 (folded with get_transform) and reads
                // Transform+0x24.
                Vector const& Pos = Elem->Iface->get_transform().Translation;
                HostileCentre.x += Pos.x;
                HostileCentre.y += Pos.y;
                HostileCentre.z += Pos.z;
                ++HostileCount;
            }
        }

        bool bBlocked = false;
        if (HostileCount > 0)
        {
            // The unrounded reciprocal multiplies each sum, one rounding each.
            double const InvCount = 1.0 / static_cast<double>(HostileCount);
            HostileCentre.x = static_cast<float>(HostileCentre.x * InvCount);
            HostileCentre.y = static_cast<float>(HostileCentre.y * InvCount);
            HostileCentre.z = static_cast<float>(HostileCentre.z * InvCount);

            if (bThreatCooldownExpired)
            {
                // ---- Pass 2: average the positions of every nearby candidate of one of
                // ---- three exact types.
                Vector NearbyCentre = {0.0f, 0.0f, 0.0f};
                int NearbyCount = 0;
                for (FAiTarget* Elem = List->Begin; Elem != List->End; ++Elem)
                {
                    // Neither pointer is tested.
                    IObjInspect const* const Owner = OwnerObjInspectOf(Sys);
                    // Candidate id first.
                    unsigned const CandId = Elem->Iface->get_id();
                    unsigned const SelfId = Owner->get_id();
                    if (CandId == SelfId)
                    {
                        continue;
                    }

                    // Exact equality on the whole flags dword (CMP, not TEST), each test
                    // preceded by its own copy of the lazy refresh.
                    RefreshAiTargetFlags(*Elem);
                    if (Elem->CachedFlags != OBJ_CRUISER)
                    {
                        RefreshAiTargetFlags(*Elem);
                        if (Elem->CachedFlags != OBJ_TRANSPORT)
                        {
                            RefreshAiTargetFlags(*Elem);
                            if (Elem->CachedFlags != OBJ_CAPITAL)
                            {
                                continue;
                            }
                        }
                    }

                    Vector const& Pos = Elem->Iface->get_transform().Translation;
                    Vector Delta = {};
                    Vec3Assign(&Delta, Pos.x - SelfPos.x, Pos.y - SelfPos.y, Pos.z - SelfPos.z);

                    // Summed (x, z, y) in double; unordered skips.
                    double const DistSq = (static_cast<double>(Delta.x) * Delta.x
                                           + static_cast<double>(Delta.z) * Delta.z)
                                        + static_cast<double>(Delta.y) * Delta.y;
                    if (!(FastLengthFromSquared(DistSq) < 1500.0))  // DAT_0639ff94
                    {
                        continue;
                    }

                    ++NearbyCount;  // retail increments before the refresh
                    Elem->RefreshPos();
                    NearbyCentre.x += Elem->CachedPos.x;
                    NearbyCentre.y += Elem->CachedPos.y;
                    NearbyCentre.z += Elem->CachedPos.z;
                }

                if (NearbyCount > 1)
                {
                    // Push away from the crowd: aim 500 units along the reverse of its
                    // bearing. One float rounding per component.
                    double const InvNearby = 1.0 / static_cast<double>(NearbyCount);
                    Vector AwayDir = {};
                    Vec3Assign(&AwayDir,
                               static_cast<float>(NearbyCentre.x * InvNearby - SelfPos.x),
                               static_cast<float>(NearbyCentre.y * InvNearby - SelfPos.y),
                               static_cast<float>(NearbyCentre.z * InvNearby - SelfPos.z));

                    // A real FSQRT here, not the fast length. x*inv stays unrounded; y*inv and
                    // z*inv are stored to float before their own * 500.
                    double const InvLen = 1.0 / std::sqrt((static_cast<double>(AwayDir.x) * AwayDir.x
                                                           + static_cast<double>(AwayDir.z) * AwayDir.z)
                                                          + static_cast<double>(AwayDir.y) * AwayDir.y);
                    double const PushX = (AwayDir.x * InvLen) * 500.0;             // DAT_0639ff90
                    float const NormY = static_cast<float>(AwayDir.y * InvLen);
                    float const NormZ = static_cast<float>(AwayDir.z * InvLen);
                    double const PushY = static_cast<double>(NormY) * 500.0;
                    double const PushZ = static_cast<double>(NormZ) * 500.0;
                    Vector Goal = {};
                    Vec3Assign(&Goal,
                               static_cast<float>(HostileCentre.x - PushX),
                               static_cast<float>(HostileCentre.y - PushY),
                               static_cast<float>(HostileCentre.z - PushZ));
                    AvoidanceGoalPos = Goal;
                }
                else
                {
                    AvoidanceGoalPos = HostileCentre;
                }

                // Hold the goal within +/-50 units of our own altitude (DAT_0639e6d4); the
                // bounds stay unrounded for the compares.
                double const Ceiling = static_cast<double>(SelfPos.y) + 50.0;
                if (Ceiling < AvoidanceGoalPos.y)
                {
                    AvoidanceGoalPos.y = static_cast<float>(Ceiling);
                }
                double const Floor = static_cast<double>(SelfPos.y) - 50.0;
                if (Floor > AvoidanceGoalPos.y)
                {
                    AvoidanceGoalPos.y = static_cast<float>(Floor);
                }

                // Already close enough: skip straight to the shared tail's ticks.
                Vector ToGoal = {};
                Vec3Assign(&ToGoal,
                           AvoidanceGoalPos.x - SelfPos.x,
                           AvoidanceGoalPos.y - SelfPos.y,
                           AvoidanceGoalPos.z - SelfPos.z);
                double const ToGoalSq = (static_cast<double>(ToGoal.x) * ToGoal.x
                                         + static_cast<double>(ToGoal.z) * ToGoal.z)
                                      + static_cast<double>(ToGoal.y) * ToGoal.y;
                if (FastLengthFromSquared(ToGoalSq) < 100.0)  // DAT_0639fb58
                {
                    Face.Update(DeltaTime);
                    Move.Update(DeltaTime);
                    UpdateResult = kBehaviorUpdateRunning;
                    return kBehaviorUpdateRunning;
                }

                Vector RayDir = {};
                Vec3Assign(&RayDir,
                           AvoidanceGoalPos.x - SelfPos.x,
                           AvoidanceGoalPos.y - SelfPos.y,
                           AvoidanceGoalPos.z - SelfPos.z);
                float BestHit = 0.0f;

                // ---- Pass 3: sweep the ray to the goal against every candidate and keep
                // ---- the nearest hit.
                for (FAiTarget* Elem = List->Begin; Elem != List->End; ++Elem)
                {
                    // Neither pointer is tested.
                    IObjInspect const* const Owner = OwnerObjInspectOf(Sys);
                    // Own id first here, the reverse of pass 2's order.
                    unsigned const SelfId = Owner->get_id();
                    unsigned const CandId = Elem->Iface->get_id();
                    if (CandId == SelfId)
                    {
                        continue;
                    }

                    // A mask test this time, with an exact-equality fallback.
                    RefreshAiTargetFlags(*Elem);
                    if ((Elem->CachedFlags & OBJ_SOLAR_NO_WAYPOINT_MASK) == 0)
                    {
                        RefreshAiTargetFlags(*Elem);
                        if (Elem->CachedFlags != OBJ_CAPITAL)
                        {
                            continue;
                        }
                    }

                    // Retail renormalises the ray in place on every qualifying iteration
                    // (idempotent after the first); one rounding per component.
                    double const RayInv = 1.0 / std::sqrt((static_cast<double>(RayDir.x) * RayDir.x
                                                           + static_cast<double>(RayDir.y) * RayDir.y)
                                                          + static_cast<double>(RayDir.z) * RayDir.z);
                    RayDir.x = static_cast<float>(RayDir.x * RayInv);
                    RayDir.y = static_cast<float>(RayDir.y * RayInv);
                    RayDir.z = static_cast<float>(RayDir.z * RayInv);

                    Elem->RefreshPos();
                    float const Limit = Elem->CachedRadius + 500.0f;  // DAT_0639ff90
                    float Hit = 0.0f;
                    Elem->RefreshPos();  // retail's own second, redundant refresh

                    // Already inside the candidate's clearance envelope: latch "blocked" and
                    // stop steering this tick. The differences stay unrounded, summed (z, y, x).
                    double const DX = static_cast<double>(SelfPos.x) - Elem->CachedPos.x;
                    double const DY = static_cast<double>(SelfPos.y) - Elem->CachedPos.y;
                    double const DZ = static_cast<double>(SelfPos.z) - Elem->CachedPos.z;
                    if (FastLengthFromSquared((DZ * DZ + DY * DY) + DX * DX) < Limit)
                    {
                        bBlocked = true;
                        continue;
                    }

                    // FUN_062df830. Retail passes the radius by address (`float const&`); the
                    // port's plain `float` parameter computes the same thing.
                    Elem->RefreshPos();  // retail's own third refresh
                    if (!RaySphereIntersect(SelfPos, RayDir, Elem->CachedPos, Limit, &Hit))
                    {
                        continue;
                    }
                    // Take the hit outright the first time, then only when it is nearer.
                    if (BestHit == 0.0f || Hit < BestHit)
                    {
                        BestHit = Hit;
                    }
                }

                // Pull the goal back to the first thing the ray hits. The x and y products
                // stay unrounded until the add; z's is stored to float first.
                if (BestHit != 0.0f)
                {
                    AvoidanceGoalPos.x = static_cast<float>(static_cast<double>(RayDir.x) * BestHit + SelfPos.x);
                    AvoidanceGoalPos.y = static_cast<float>(static_cast<double>(RayDir.y) * BestHit + SelfPos.y);
                    float const ScaledZ = static_cast<float>(static_cast<double>(RayDir.z) * BestHit);
                    AvoidanceGoalPos.z = static_cast<float>(static_cast<double>(ScaledZ) + SelfPos.z);
                }

                // Retail re-tests bBlocked separately because LAB_062d0905 is also reached
                // directly when the candidate list is empty.
                if (!bBlocked)
                {
                    Move.SetRange3c(250.0f);  // DAT_0639f558
                    Move.SetMode(kMoveModeEngine);
                    Move.SetScale40(0.6f);    // DAT_0639f554
                    Face.SetPosition(AvoidanceGoalPos);
                    Move.SetPosition(AvoidanceGoalPos);
                    ThreatCooldown = 30.0f;               // DAT_0639f550
                }

                if (bBlocked)
                {
                    Move.Commit();
                    Face.Commit();
                }
            }
            else
            {
                Move.Commit();
                Face.Commit();
            }
        }
        else
        {
            Move.Commit();
            Face.Commit();
        }
    }

    // LAB_062d0972, the shared tail. Both results are discarded.
    Face.Update(DeltaTime);
    Move.Update(DeltaTime);

    UpdateResult = kBehaviorUpdateRunning;
    return kBehaviorUpdateRunning;
}
