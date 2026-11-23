// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_fleebehavior.h"
#include "common_dockbehavior.h"     // DockBehavior::CopyDockOp (FUN_062c3590)
#include "common_aisteer.h"          // HasReadyWeaponSlot (FUN_062eb910)
#include "common_systemmanager.h"    // SystemManager
#include "dacom.h"                   // FDUMP

#include <cstdio>   // std::sprintf -- the dead diagnostics retail formats into stack buffers

// FleeBehavior; see common_fleebehavior.h.

// FUN_062ecaf0 is FGunConvergenceStateBlock::GetCurrentLeader: the follow / formation leader
// watched at SystemManager+0x1364, or 0.
// CopyGotoOp (common_gotobehavior.cpp): GotoBehavior's own DirectiveGotoOp copy-out.
extern "C" bool __fastcall CopyGotoOp(unsigned char* This, void* OutBuffer);

namespace
{
// DAT_0639f7a4: 00 40 1c 46 == 10000.0f. Ghidra merges its last byte into the string that
// follows ("FFleeBehavior::deactivate()..."); the string itself starts at 0639f7a8.
float const GFleeDestinationOffsetX = 10000.0f;

// The format strings of retail's dead diagnostics (formatted into a stack buffer and never
// read): 0639f6b8 and 0639f7a8.
char const* const GPerformUninitialized =
    "FleeBehavior::Perform() running uninitialized flee_type. Quitting behavior. Ship id: %d (decimal)";
char const* const GDeactivateUninitialized =
    "FleeBehavior::deactivate() running uninitialized flee_type. Quitting behavior. Ship id: %d (decimal)";

// OnEvaluateReadiness's hostile-power-ratio thresholds, indexed by
// JobParams.FleeSceneThreatStyle (0..4), default 0.5f: DAT_0639e0e0, DAT_0639e0dc,
// DAT_0639e0d8, DAT_0639e0d4, DAT_0639a1d0, DAT_0639c13c.
float FleeHostileRatioThreshold(int Level)
{
    switch (Level)
    {
    case 0: return 0.15f;
    case 1: return 0.35f;
    case 2: return 0.65f;
    case 3: return 0.85f;
    case 4: return 1.0f;
    default: return 0.5f;
    }
}
}

// FUN_062c7020: FUN_062d2020(TypeId), DirectiveGotoOp ctor on +0x4c, DirectiveDockOp ctor on
// +0xbc, +0x2c/+0x38/+0x34 = 0, +0x30 = 4. Retail leaves +0x3c..+0x48 unwritten (slots
// 3/4/5/22 write the delegate pointers before anything reads them); the port zeroes them.
FleeBehavior::FleeBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , OwnerContextPtr14(nullptr)
    , ObjectDb(nullptr)
    , DeltaTime1c(0.0f)
    , BehaviorMgrPtr20(nullptr)
    , NavHelperPtr24(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , bFleeActive(0)
    , Pad2d{}
    , FleeReason(4)
    , FleeDestType(0)
    , bFollowFleeingTarget(0)
    , Pad39{}
    , GotoDelegate(nullptr)
    , DockDelegate(nullptr)
    , FormationDelegate(nullptr)
    , bHasSavedDirective(0)
    , Pad49{}
    , GotoOp()
    , DockOp()
{
    ConstructBase(InTypeId);   // FUN_062d2020
}

// Slot 0 is FUN_062c7090, the scalar-deleting dtor ICF-shared by seven behavior vtables.
FleeBehavior::~FleeBehavior()
{
}

// Slot 1, LAB_062cf3e0: `JMP FUN_062d2090`.
bool FleeBehavior::SetContext(void* Owner, void* Manager, void* Sys)
{
    return BindContext(Owner, Manager, Sys);
}

// The three loads slots 3/4/5/22 each open with (inlined in retail).
void FleeBehavior::RefreshDelegates()
{
    GotoDelegate = BehaviorMgrPtr20->BehaviorDispatchTable[2];    // +0x17c
    DockDelegate = BehaviorMgrPtr20->BehaviorDispatchTable[7];    // +0x190
    FormationDelegate = BehaviorMgrPtr20->BehaviorDispatchTable[10];   // +0x19c
}

// ============================================================================
// Slot 5, LAB_062c70b0.
// ============================================================================
int FleeBehavior::OnEvaluateReadiness(float Unused)
{
    (void)Unused;
    SystemManager* const Sys = NavHelperPtr24;
    pub::AI::Personality::JobStruct const& Job = Sys->PersonalityData.JobParams;

    // 2 is the "do not flee" answer.
    bFollowFleeingTarget = 0;
    int Result = 2;
    if (Job.bFleeWhenLeaderFleesStyle != 0)                                  // Sys+0x1950
    {
        IObjRW* const Watched = Sys->GunConvergenceStateBytes.GetCurrentLeader();   // FUN_062ecaf0
        if (Watched != nullptr)
        {
            // GetBehaviorManager(obj)->CurrentBehaviorIndex == 4: the target is fleeing.
            IBehaviorManager* const TargetMgr = GetBehaviorManager(Watched);
            if (TargetMgr->CurrentBehaviorIndex == 4)
            {
                bFollowFleeingTarget = 1;
                bFleeActive = 1;
                FleeReason = 1;
            }
        }
    }

    // A flee already running answers 3 (with +0x29) or 0.
    if (bFleeActive != 0)
    {
        if (Flag29 != 0)
        {
            ModeSelector0xc = 3;
            return 3;
        }
        ModeSelector0xc = 0;
        return 0;
    }

    RefreshDelegates();

    // The own ship's shield (SystemManager's watcher at +0x08); retail calls through the
    // receiver even when it is null.
    float ShieldCur = 0.0f;
    float ShieldMax = 0.0f;
    bool bShieldActive = false;
    {
        IObjRW* const Object = ObjRwFromWatchable(const_cast<Watchable*>(Sys->Watcher.target()));
        (void)ObjInspectOf(Object)->get_shield_status(ShieldCur, ShieldMax, bShieldActive);
    }

    // Retail calls get_status twice; the ratio reads HitPts from the first call and MaxHitPts
    // from the second.
    float HitPtsA = 0.0f;
    float MaxHitPtsA = 0.0f;
    float HitPtsB = 0.0f;
    float MaxHitPtsB = 0.0f;
    IObjInspect const* const External = Sys->SelfTarget.Iface;
    (void)External->get_status(HitPtsA, MaxHitPtsA);
    (void)External->get_status(HitPtsB, MaxHitPtsB);

    // Strictly below FleeWhenHullDamagedPercent (Sys+0x194c); unordered does not flee.
    bool bFlee = false;
    {
        double const Ratio = (static_cast<double>(HitPtsA) + static_cast<double>(ShieldCur)) /
                             (static_cast<double>(MaxHitPtsB) + static_cast<double>(ShieldMax));
        if (Ratio < static_cast<double>(Job.FleeWhenHullDamagedPercent))
        {
            bFlee = true;
            FleeReason = 2;
        }
    }

    // Targeting.AggressionRatio (Sys+0x1438) strictly above the per-level threshold.
    if (static_cast<double>(Sys->Targeting.AggressionRatio) >
        static_cast<double>(FleeHostileRatioThreshold(Job.FleeSceneThreatStyle)))   // Sys+0x1948
    {
        bFlee = true;
        FleeReason = 0;
    }

    // With bFleeNoWeaponsStyle (Sys+0x1951), no ready weapon (FUN_062eb910) forces the flee.
    if (Job.bFleeNoWeaponsStyle != 0 && !Sys->GunGroups.HasReadyWeaponSlot())
    {
        FleeReason = 3;
        Result = 0;
    }
    else if (bFlee)
    {
        Result = 0;
    }

    if (Flag29 != 0)
    {
        ModeSelector0xc = 3;
        return 3;
    }
    ModeSelector0xc = Result;
    return Result;
}

// ============================================================================
// Slot 7, FleeBehavior::Perform, LAB_062c72c0. NavHelper, ExternalIface and the delegates are
// not null-tested.
// ============================================================================
int FleeBehavior::Perform()
{
    SystemManager* const Sys = NavHelperPtr24;

    if (bFleeActive == 0)
    {
        bFleeActive = 1;

        // Retail seeds only Radius (500.0f); the other out-params are uninitialised stack.
        pub::AI::ContentCallback::FleeDestinationType Type =
            static_cast<pub::AI::ContentCallback::FleeDestinationType>(0);
        Vector Destination = { 0.0f, 0.0f, 0.0f };
        uint DestinationId = 0;
        float Radius = 500.0f;
        uint const OwnerId = Sys->SelfTarget.Iface->get_id();
        if (!BehaviorMgrPtr20->RequestFleeDestination(
                OwnerId, static_cast<pub::AI::ContentCallback::FleeReason>(FleeReason),
                Type, Destination, DestinationId, Radius))
        {
            // Fallback: type 3, the cached physical centre pushed 10000 along X only.
            Type = static_cast<pub::AI::ContentCallback::FleeDestinationType>(3);
            if (Sys->SelfTarget.bPosValid == 0)
            {
                Sys->SelfTarget.bPosValid = 1;
                (void)Sys->SelfTarget.Iface->get_physical_radius(
                    Sys->SelfTarget.CachedRadius, Sys->SelfTarget.CachedPos);
            }
            Destination.x = Sys->SelfTarget.CachedPos.x + GFleeDestinationOffsetX;
            Destination.y = Sys->SelfTarget.CachedPos.y;
            Destination.z = Sys->SelfTarget.CachedPos.z;
            Radius = 500.0f;
        }

        FleeDestType = static_cast<int>(Type);
        if (FleeDestType == 2)
        {
            // Save the dock delegate's current op when it is the active directive (type 7).
            pub::AI::DirectiveDockOp Op;
            Op.DockObjectId = DestinationId;
            bHasSavedDirective = 0;
            IDirectiveInfo const* const Current = BehaviorMgrPtr20->get_current_directive();
            if (Current != nullptr &&
                *reinterpret_cast<int const*>(reinterpret_cast<char const*>(Current) + 0x8) == 7)
            {
                if (static_cast<DockBehavior*>(DockDelegate)->CopyDockOp(&DockOp))
                    bHasSavedDirective = 1;
            }
            DockDelegate->OnApplyDirective(&Op);
            DockDelegate->OnEngage();
        }
        else if (FleeDestType == 1 || FleeDestType == 3)
        {
            pub::AI::DirectiveGotoOp Op;
            if (FleeDestType == 3)
            {
                Op.Field_0C = 1;
                Op.Destination = Destination;
            }
            else
            {
                Op.Field_0C = 0;
                Op.Field_1C = static_cast<int32_t>(DestinationId);
            }
            Op.Field_50 = Radius;
            bHasSavedDirective = 0;
            IDirectiveInfo const* const Current = BehaviorMgrPtr20->get_current_directive();
            if (Current != nullptr &&
                *reinterpret_cast<int const*>(reinterpret_cast<char const*>(Current) + 0x8) == 2)
            {
                if (CopyGotoOp(reinterpret_cast<unsigned char*>(GotoDelegate), &GotoOp))
                    bHasSavedDirective = 1;
            }
            GotoDelegate->OnApplyDirective(&Op);
            GotoDelegate->OnEngage();
        }
        else
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
                  "E:\\FL\\Scratch\\Source\\Common\\Ai\\BehaviorClasses.cpp", 0x453,
                  "FleeBehavior::perform() has uninitialized flee type from callback. Come get Joel W");
        }

        if (bFleeActive == 0)
        {
            UpdateResult10 = 1;
            return 1;
        }
    }

    if (bFollowFleeingTarget != 0)
    {
        FormationDelegate->OnEngage();
        FormationDelegate->SetTickDelta(GetTickDelta());
        (void)FormationDelegate->OnUpdate();
        UpdateResult10 = 1;
        return 1;
    }

    // Types 1/3 drive GotoDelegate, type 2 DockDelegate, unguarded.
    if (FleeDestType == 1 || FleeDestType == 2 || FleeDestType == 3)
    {
        Behavior* const Delegate = (FleeDestType == 2) ? DockDelegate : GotoDelegate;
        Delegate->SetTickDelta(GetTickDelta());
        int const ChildResult = Delegate->OnUpdate();
        if (ChildResult != 2 && ChildResult != 3)
        {
            UpdateResult10 = 1;
            return 1;
        }
    }
    else
    {
        // Retail formats into a stack buffer that is never read, then finishes.
        char Buffer[0xf4];
        std::sprintf(Buffer, GPerformUninitialized, Sys->SelfTarget.Iface->get_id());
    }

    bFleeActive = 0;
    BehaviorMgrPtr20->SetDirectiveResult(4, 0);                              // FUN_062d29a0
    UpdateResult10 = 2;
    return 2;
}

// ============================================================================
// Slot 3, LAB_062c7570.
// ============================================================================
void FleeBehavior::OnEngage()
{
    RefreshDelegates();
    if (bFleeActive == 0)
        return;
    if (bFollowFleeingTarget != 0)
    {
        FormationDelegate->OnEngage();
        return;
    }
    if (FleeDestType == 1 || FleeDestType == 3)
    {
        GotoDelegate->OnEngage();
        return;
    }
    if (FleeDestType == 2)
    {
        DockDelegate->OnEngage();
        return;
    }
    // Retail formats the "deactivate()" message into a stack buffer that is never read.
    SystemManager* const Sys = NavHelperPtr24;
    char Buffer[0x100];
    std::sprintf(Buffer, GDeactivateUninitialized, Sys->SelfTarget.Iface->get_id());
}

// ============================================================================
// Slot 4, LAB_062c7610. Restores the delegate's saved op when it is still the active directive.
// ============================================================================
void FleeBehavior::OnDisengage()
{
    RefreshDelegates();
    if (bFollowFleeingTarget != 0)
    {
        FormationDelegate->OnDisengage();
        return;
    }
    if (FleeDestType == 1 || FleeDestType == 3)
    {
        if (GotoDelegate == nullptr)
            return;
        GotoDelegate->OnDisengage();
        IDirectiveInfo const* const Current = BehaviorMgrPtr20->get_current_directive();
        if (Current != nullptr &&
            *reinterpret_cast<int const*>(reinterpret_cast<char const*>(Current) + 0x8) == 2 &&
            bHasSavedDirective != 0)
        {
            GotoDelegate->OnApplyDirective(&GotoOp);
        }
        return;
    }
    if (FleeDestType == 2)
    {
        if (DockDelegate == nullptr)
            return;
        DockDelegate->OnDisengage();
        Behavior_ReleaseDirectiveTarget(DockDelegate);                            // FUN_062d21c0
        IDirectiveInfo const* const Current = BehaviorMgrPtr20->get_current_directive();
        if (Current != nullptr &&
            *reinterpret_cast<int const*>(reinterpret_cast<char const*>(Current) + 0x8) == 7 &&
            bHasSavedDirective != 0)
        {
            DockDelegate->OnApplyDirective(&DockOp);
        }
        return;
    }
    // Formatted into a never-read buffer, as retail.
    SystemManager* const Sys = NavHelperPtr24;
    char Buffer[0x100];
    std::sprintf(Buffer, GDeactivateUninitialized, Sys->SelfTarget.Iface->get_id());
}

// ============================================================================
// Slot 22, LAB_062c7710.
// ============================================================================
void FleeBehavior::OnEvent(void* Event)
{
    RefreshDelegates();
    if (bFollowFleeingTarget != 0)
    {
        if (FormationDelegate != nullptr)
            FormationDelegate->OnEvent(Event);
        return;
    }
    if (FleeDestType == 1 || FleeDestType == 3)
    {
        if (GotoDelegate != nullptr)
            GotoDelegate->OnEvent(Event);
        return;
    }
    if (FleeDestType == 2)
    {
        if (DockDelegate != nullptr)
            DockDelegate->OnEvent(Event);
        return;
    }
    FDUMP(0x100001, "%s(%d) : *** ERROR: %s",
          "E:\\FL\\Scratch\\Source\\Common\\Ai\\BehaviorClasses.cpp", 0x504,
          "FleeBehavior::event_handler() has failed because of uninitialized values. Come get Joel W");
}
