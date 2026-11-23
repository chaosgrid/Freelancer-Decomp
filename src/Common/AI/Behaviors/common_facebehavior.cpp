// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_facebehavior.h"
#include "common_pathfind.h" // PathfindManager (IBehaviorManager+0x0C)

#include "common_systemmanager.h"  // SystemManager, ThrustManager::CheckSteeringGate (FUN_062e92e0)
#include "common_aisteer.h"        // BearingAngleToIface, GetInterceptClearanceOfIface,
                                   // BuildPerpendicularFrame, BuildOrientationBasis
#include "common_iface.h"          // IObjInspect, ObjInspectOf
#include "common_pub.h"            // pub::AI::Personality::EngineKillUseStruct
#include "common_iustategraph.h"   // FindClosestHostileTarget

#include <cstdint>

// FUN_062e74a0 is SystemManagerTimerTable::FindVerifiedObject.

// FaceBehavior, the engine-kill manoeuvre; layout notes are in common_facebehavior.h.

// FUN_062c88e0. FUN_062d2020 (ConstructBase), then the MoveIdea (+0x68, FUN_062d4e60),
// FaceIdea (+0xe0, FUN_062d6750) and AfterburnerIdea (+0x1a0, FUN_062da500) members, then the
// trailing own fields. Retail stores Flag1b4 (+0x1b4) itself, so the AfterburnerIdea's 0x14
// bytes end there.
FaceBehavior::FaceBehavior(int InTypeId)
    : Behavior()
    , TypeId(InTypeId)
    , CachedHandle08(0)
    , ModeSelector0xc(0)
    , UpdateResult10(0)
    , ThreatSourcePtr14(nullptr)
    , ObjectDb(nullptr)
    , DeltaTime1c(0.0f)
    , BehaviorMgrPtr20(nullptr)
    , NavHelperPtr24(nullptr)
    , Flags28(0)
    , Flag29(0)
    , Pad2a{}
    , TargetKind2c(0)
    , Watcher30()
    , OrientMatrix38()
    , TargetPosition5c{}
    , SubObjectA()
    , SubObjectB()
    , SubObjectC()
    , Flag1b4(0)
    , Pad1b5{}
    , FaceAxisX(0.0f)
    , FaceAxisY(0.0f)
    , FaceAxisZ(0.0f)
    , Flag1c4(1)
    , Flag1c5(0)
    , Pad1c6{}
    , EngineKillStateElapsed(0.0f)
    , Field1cc(0.0f)
    , Field1d0(0.9f)
    , Flag1d4(0)
    , Pad1d5{}
    , AfterburnerElapsed(0.0f)
{
    ConstructBase(InTypeId);   // FUN_062d2020
    // Retail's `CMP [this+0x30],0` right after the vptr store is dead (Watcher30 was just
    // zeroed) and is not reproduced.
}

// FUN_062c8a30 (FUN_062c8a10 is the scalar deleting destructor).
FaceBehavior::~FaceBehavior()
{
    // Retail's order: +0xf4 (SubObjectB), +0x7c (SubObjectA), +0x30.
    SubObjectB.Watcher.set(nullptr);
    SubObjectA.Watcher.set(nullptr);
    Watcher30.set(nullptr);

    // FUN_062d2080 (~Behavior) runs as the base destructor.
}

// Slot 1, LAB_062c8ac0.
bool FaceBehavior::SetContext(void* Param1, void* Param2, void* Param3)
{
    if (!BindContext(Param1, Param2, Param3))   // FUN_062d2090
        return false;

    // Unlike DockBehavior, retail does not call SubObjectA's slot 0 here.
    SubObjectB.SetContext(Param1, Param3, Param2);

    SubObjectC.SetContext(Param1, Param3, Param2);

    // FUN_062e5b20.
    Field1cc = Behavior_InvokeManagerSlotF0(ObjInspectOf(ThreatSourcePtr14), 0.0f, 1.0f);

    return true;
}

// Slot 3, FUN_062c9320.
void FaceBehavior::OnEngage()
{
    Behavior::OnEngage();                                              // FUN_062d20d0

    Flag1c5 = 0;
    EngineKillStateElapsed = 0.0f;

    SetCruiseLock(0, 3);                                            // FUN_062d21d0

    // FUN_062e91d0.
    NavHelperPtr24->Thrust.SetEngineActive(Flag1c4 == 0);

    Flag1b4 = 0;
    FaceAxisX = 0.0f;
    FaceAxisY = 0.0f;
    FaceAxisZ = 0.0f;
    AfterburnerElapsed = 0.0f;
}

// Slot 4, FUN_062c9380.
void FaceBehavior::OnDisengage()
{
    if ((Flags28 & 0x2) == 0 && Watcher30.target() != nullptr)
    {
        Watcher30.set(nullptr);
    }

    SubObjectB.Commit();

    // AfterburnerIdea::Commit: afterburner off.
    SubObjectC.Commit();

    Behavior::OnDisengage();                                              // FUN_062d20f0
}

// Slot 5, LAB_062c8c10.
int FaceBehavior::OnEvaluateReadiness(float Unused)
{
    (void)Unused;
    if ((Flags28 & 0x2) != 0 && TargetKind2c == 1 && GetWatchedRawBlock() != nullptr)
    {
        int const Mode = Flag29 ? 3 : 1;
        ModeSelector0xc = Mode;
        return Mode;
    }

    int Result = 2;
    SystemManager* const Sys = NavHelperPtr24;
    if (!Sys->GunConvergenceStateBytes.FollowerDirectiveGateA())     // FUN_062ecb20
    {
        // A cached pathfind obstacle skips the scan. Retail also copies CachedObstacleId into
        // a local that nothing reads.
        PathfindManager* const Pathfind = BehaviorMgrPtr20->Pathfind;
        if (!(Pathfind->CachedObstacleId != 0 && Pathfind->bHasCachedObstacle))
        {
            IObjInspect* const Closest = FindClosestHostileTarget(
                &Sys->Timers, Sys->PersonalityData.EngineKillParams.EngineKillMaxTargetDistance);
            Watcher30.set(static_cast<Watchable const*>(static_cast<IObjRW*>(Closest)));

            IObjRW* const Watched = GetWatchedRawBlock();
            if (Watched != nullptr)
            {
                // Ship-class objects only (0x30000).
                uint Type;
                Watched->get_type(Type);
                if ((Type & 0x30000) != 0)
                {
                    // Own speed squared on the x87 stack, strictly above 2500.0f (DAT_0639f9c8).
                    Vector const V = Sys->SelfTarget.Iface->get_velocity();
                    double const SpeedSq = (static_cast<double>(V.z) * V.z + static_cast<double>(V.y) * V.y) +
                                           static_cast<double>(V.x) * V.x;
                    if (SpeedSq > 2500.0)
                    {
                        Result = Sys->Timers.IsTimer3Active() ? 0 : 1;   // FUN_062e8830
                    }
                }
            }
        }
    }

    if (Flag29)
        Result = 3;
    ModeSelector0xc = Result;
    return Result;
}

// Slot 6, LAB_062c8d80.
EvaluateResult FaceBehavior::evaluate(unsigned int Context)
{
    (void)Context;
    if (!PlayerTrailAvailable)
        return EvalSkip;

    // The owner's own target goes to the ship's TargetManager.
    IObjRW* Target = nullptr;
    static_cast<IObjRW*>(ThreatSourcePtr14)->get_target(Target);
    NavHelperPtr24->Targeting.RetargetOwner(Target, SUBOBJ_ID_NONE);
    return EvalBlocked;
}

// ----------------------------------------------------------------------------
// Inlined twice in FaceBehavior::Update (byte-identical); the same once-per-tick refresh
// FUN_062e6d40 performs on SystemManager's cached physical extent (latch +0x157a, set before
// the call).
static void EnsurePhysicalCacheFresh(SystemManager* Sys)
{
    if (Sys->SelfTarget.bPosValid != 0)
        return;
    Sys->SelfTarget.bPosValid = 1;
    Sys->SelfTarget.Iface->get_physical_radius(Sys->SelfTarget.CachedRadius,
                                                         Sys->SelfTarget.CachedPos);
}

// ----------------------------------------------------------------------------
// FaceBehavior::Update, slot 7, LAB_062c8de0 (with a four-entry jump table at 062c9308).
//
// The engine-kill manoeuvre: cut the engines, coast, and rotate to face a target. The five
// floats read at SystemManager+0x16d4..+0x16e4 are pub::AI::Personality::EngineKillUseStruct
// (Personality+0x150).
//
// .rdata constants:
//   DAT_0639e2d8  double 0x3FF921FB60000000: single-precision pi/2 widened to double, not the
//                 double nearest pi/2 (overshoots by ~4.4e-08). Same trap as DAT_0639e2d0 in
//                 LaunchBehavior::Update.
//   DAT_0639f9d0  double 0x3FC921FB60000000: the same widened constant / 8 (pi/16).
//   0x3f7fbe77    an inline float immediate, 0.998969435691833496 (cos 2.603 degrees).
int FaceBehavior::Update()
{
    // Abort request, consumed here.
    if (Flag1d4 != 0)
    {
        Flag1d4 = 0;
        UpdateResult10 = 2;
        return UpdateResult10;
    }

    SystemManager* const Sys = NavHelperPtr24;
    pub::AI::Personality::EngineKillUseStruct const& EngineKill =
        Sys->PersonalityData.EngineKillParams;

    double const kWideBearingGate = 1.5707963705062866;    // DAT_0639e2d8, float pi/2 widened
    double const kFaceLockGate    = 0.19634954631328583;   // DAT_0639f9d0, float pi/16 widened

    // The pending result lives in one stack slot preloaded with 3; EBX stays the literal 3
    // and is reused by two later sites.
    int Result = 3;

    // A player-flown ship skips the re-validation block and goes straight to the switch.
    if (!Sys->SelfTarget.Iface->is_player())
    {
        // Four gates in retail's evaluation order; any failure goes to the re-validation
        // block at LAB_062c8f0a.
        void* const WatchedBlock = GetWatchedRawBlock();

        bool bStillTurning = false;
        if (Watcher30.target() != nullptr && WatchedBlock != nullptr)
        {
            // TEST AH,0x44 / JNP equality idiom: continue only when non-zero.
            if (EngineKill.EngineKillUseAfterburner != 0.0f)
            {
                // Strictly less (unordered fails).
                if (AfterburnerElapsed < EngineKill.EngineKillAfterburnerTime)
                {
                    // The argument order is the opposite of the second BearingAngleToIface
                    // call below: Self is the owner, Target the watched block.
                    double const Bearing = BearingAngleToIface(ThreatSourcePtr14, WatchedBlock); // unrounded ST0

                    // Strictly greater: the ship has not finished rotating yet.
                    bStillTurning = (Bearing > kWideBearingGate);
                }
            }
        }

        if (bStillTurning)
        {
            // Keep coasting and steering; returns 0 without reaching the switch.
            AfterburnerElapsed += DeltaTime1c;
            // AfterburnerIdea::Update holds the afterburner on.
            SubObjectC.Update(DeltaTime1c);

            // FaceIdea tag 5 ("face an axis") with a zero axis.
            Vector ZeroAxis;
            Vec3Assign(&ZeroAxis, 0.0f, 0.0f, 0.0f);
            SubObjectB.SetAxis(ZeroAxis);
            SubObjectB.Update(DeltaTime1c);

            UpdateResult10 = 0;
            return 0;
        }

        // LAB_062c8f0a: re-validate the watch. AfterburnerIdea::Commit, afterburner off.
        SubObjectC.Commit();

        if (Flag1c5 != 0)
        {
            // Strictly less keeps the accumulator running.
            if (EngineKillStateElapsed < EngineKill.EngineKillFaceTime)
            {
                Result = 0;
                EngineKillStateElapsed += DeltaTime1c;
            }
            else
            {
                // Give up on this watch entirely.
                EngineKillStateElapsed = 0.0f;
                if (Watcher30.target() != nullptr)
                    Watcher30.set(nullptr);
                Result = 2;
            }
        }
        else if (Watcher30.target() != nullptr && GetWatchedRawBlock() != nullptr)
        {
            // Retail re-reads the watcher here, after the Commit() above, instead of reusing
            // the earlier value.
            void* const FreshBlock = GetWatchedRawBlock();
            TargetKind2c = 1;

            // Retail expands the watcher-to-block idiom three more times and branches on
            // each; all of those branches are dead after the guards above (one would
            // reinterpret the integer Result as a float), and only the call survives.
            // Operands are reversed relative to the first call: Self is the watched block.
            double const Angle = BearingAngleToIface(FreshBlock, ThreatSourcePtr14); // unrounded ST0

            // An angle within pi/16 is "locked on".
            if (Angle > kFaceLockGate)
            {
                // Anything but strictly-less (unordered included) ends the attempt.
                if (!(EngineKillStateElapsed < EngineKill.EngineKillSearchTime))
                {
                    Result = 3;
                }
                else
                {
                    Result = 0;
                    EngineKillStateElapsed += DeltaTime1c;
                }
            }
            else
            {
                // LAB_062c8fec: locked on.
                Flag1c5 = 1;
                EngineKillStateElapsed = 0.0f;
                Result = 1;

                // Sys+0x14fc is the ship's own FAiTarget record. The compare uses the
                // unrounded ST0 return, hence double.
                double const Clearance = GetInterceptClearanceOfIface(
                    &Sys->SelfTarget, GetWatchedRawBlock());

                // Strictly greater (unordered skips).
                if (Clearance > EngineKill.EngineKillMaxTargetDistance)
                    Result = 2;
            }
        }
        // Otherwise straight to the switch with Result still 3.
    }

    // LAB_062c9038: unsigned switch on TargetKind2c - 1; outside 1..4 goes to the shared tail.
    switch (TargetKind2c)
    {
    case 1:
    {
        if (Flag1b4 != 0)
        {
            // Retail calls get_transform() and discards the result; kept so the call count
            // matches.
            (void)Sys->SelfTarget.Iface->get_transform();

            // FaceIdea tag 5 on FaceAxisX/Y/Z (+0x1b8).
            Vector Axis;
            Vec3Assign(&Axis, FaceAxisX, FaceAxisY, FaceAxisZ);
            SubObjectB.SetAxis(Axis);
        }
        else
        {
            // FaceIdea tag 1, "watch an object".
            SubObjectB.SetWatchObject(GetWatchedRawBlock());

            // The only case arm with its own RET, skipping the shared tail.
            if (GetWatchedRawBlock() == nullptr)
            {
                UpdateResult10 = 3;
                return 3;
            }
        }
        break;
    }
    case 2:
    {
        if (Flag1b4 != 0)
        {
            Vector const Col2 = MatrixColumn2(
                Sys->SelfTarget.Iface->get_transform().Rotation);
            Vector NegCol2;
            VecNegate3(&Col2, &NegCol2);

            // |FaceAxis|^2, not a dot product against the negated column (the x87 operands
            // all come from the same vector). Kept on the x87 stack, hence double.
            Vector Axis;
            Vec3Assign(&Axis, FaceAxisX, FaceAxisY, FaceAxisZ);
            double const MagnitudeSquared = (static_cast<double>(Axis.x) * Axis.x
                                             + static_cast<double>(Axis.y) * Axis.y)
                                          + static_cast<double>(Axis.z) * Axis.z;

            // Strictly > 0 uses the explicit axis (tag 5).
            if (MagnitudeSquared > 0.0)
                SubObjectB.SetAxis(Axis);
            else
                SubObjectB.SetDirection(NegCol2);   // FaceIdea tag 3
        }
        else
        {
            bool bFrameBuilt = false;
            if (!Sys->Thrust.CheckSteeringGate())   // FUN_062e92e0
            {
                EnsurePhysicalCacheFresh(Sys);

                Vector Diff;
                Vec3Assign(&Diff,
                           TargetPosition5c.x - Sys->SelfTarget.CachedPos.x,
                           TargetPosition5c.y - Sys->SelfTarget.CachedPos.y,
                           TargetPosition5c.z - Sys->SelfTarget.CachedPos.z);

                bFrameBuilt = BuildPerpendicularFrame(Diff, &OrientMatrix38, 0.999f /* 0x3f7fbe77 */, nullptr);   // FUN_062e3460
            }

            if (!bFrameBuilt)
            {
                // Retail recomputes the cache check and the difference here.
                EnsurePhysicalCacheFresh(Sys);

                Vector Diff;
                Vec3Assign(&Diff,
                           TargetPosition5c.x - Sys->SelfTarget.CachedPos.x,
                           TargetPosition5c.y - Sys->SelfTarget.CachedPos.y,
                           TargetPosition5c.z - Sys->SelfTarget.CachedPos.z);

                // FUN_062e1f40; retail copies its returned out-buffer into OrientMatrix38.
                Matrix Frame;
                BuildOrientationBasis(Frame,
                                      Sys->SelfTarget.Iface->get_transform().Rotation,
                                      Diff, nullptr);
                OrientMatrix38 = Frame;
            }

            // FaceIdea tag 4, "face along a basis".
            SubObjectB.SetBasis(OrientMatrix38);
        }
        break;
    }
    case 3:
        SubObjectB.SetBasis(OrientMatrix38);
        break;
    case 4:
    {
        // The jump-table caseD_4 at 062c90c3, not the shared tail Ghidra gives the same name.
        Vector const Col2 = MatrixColumn2(
            Sys->SelfTarget.Iface->get_transform().Rotation);
        Vector NegCol2;
        VecNegate3(&Col2, &NegCol2);
        SubObjectB.SetDirection(NegCol2);   // FaceIdea tag 3
        break;
    }
    default:
        // TargetKind2c outside 1..4, 0 included (unsigned compare on Kind - 1).
        break;
    }

    // Shared tail, LAB_062c92ae.
    Sys->Thrust.SetEngineActive(Flag1c4 == 0);

    SubObjectB.Update(DeltaTime1c);

    // is_player() a second time; a player-flown ship always returns 1.
    if (Sys->SelfTarget.Iface->is_player())
        Result = 1;

    UpdateResult10 = Result;
    return Result;
}

// Slot 10, LAB_062c8b30. Every failure path is the shared tail at 062c8bf2.
bool FaceBehavior::OnApplyDirective(void* Message)
{
    if (Message == nullptr)
    {
        Flags28 = static_cast<unsigned char>(Flags28 & ~0x2);
        return false;
    }

    char* M = static_cast<char*>(Message);
    Flag1c4 = static_cast<unsigned char>(M[0x44]);
    int const Kind = *reinterpret_cast<int const*>(M + 0xc);
    TargetKind2c = Kind;

    switch (Kind)
    {
    case 1:
    {
        // FUN_062e74a0; then the watch must be live.
        void* const T = NavHelperPtr24->Timers.FindVerifiedObject((unsigned const*)(M + 0x10));
        Watcher30.set(T != nullptr ? reinterpret_cast<Watchable const*>(static_cast<char*>(T) + 8)
                                   : nullptr);
        if (GetWatchedRawBlock() == nullptr)
        {
            Flags28 = static_cast<unsigned char>(Flags28 & ~0x2);
            return false;
        }
        Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
        return true;
    }
    case 2:
    {
        Vector const& Src = *reinterpret_cast<Vector const*>(M + 0x14);
        TargetPosition5c = Src;
        Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
        return true;
    }
    case 3:
    {
        OrientMatrix38 = *reinterpret_cast<Matrix const*>(M + 0x20);
        Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
        return true;
    }
    case 4:
        Flags28 = static_cast<unsigned char>(Flags28 | 0x2);
        TargetKind2c = 4;
        return true;
    default:
        Flags28 = static_cast<unsigned char>(Flags28 & ~0x2);
        return false;
    }
}

// Slot 12, FUN_062c93c0. Constants: DAT_06399408 0.0f, DAT_06399404 -1.0f, DAT_0639a1d0 +1.0f,
// DAT_0639c430 0.0 (double).
void FaceBehavior::set_direction(Vector const& Offset)
{
    // TargetKind2c 2 always sets; 1 sets only for a non-zero offset; anything else clears.
    if (TargetKind2c != 2)
    {
        float const MagnitudeSquared = Offset.x * Offset.x + Offset.y * Offset.y + Offset.z * Offset.z;
        if (TargetKind2c != 1 || !(MagnitudeSquared > 0.0f))
        {
            Flag1b4 = 0;
            return;
        }
    }

    Flag1b4 = 1;
    FaceAxisX = -Offset.y;
    FaceAxisY = -Offset.x;
    FaceAxisZ = Offset.z;

    // FUN_062d4b20. Rescale only for a strictly positive factor; the reciprocal stays on the
    // x87 stack, unrounded, for both products.
    float const TurnSensitivity = BehaviorMgrPtr20->get_turn_sensitivity_factor();
    if (TurnSensitivity > 0.0f)
    {
        double const Rescale = 1.0 / static_cast<double>(TurnSensitivity);
        FaceAxisX = static_cast<float>(Rescale * FaceAxisX);
        FaceAxisY = static_cast<float>(Rescale * FaceAxisY);
    }

    // Clamp to [-1, 1]. Retail's third FCOMP per axis re-tests the lower bound with no
    // observable effect. Unordered takes -1.0f.
    if (!(FaceAxisX > -1.0f))
        FaceAxisX = -1.0f;
    else if (!(FaceAxisX < 1.0f))
        FaceAxisX = 1.0f;

    if (!(FaceAxisY > -1.0f))
        FaceAxisY = -1.0f;
    else if (!(FaceAxisY < 1.0f))
        FaceAxisY = 1.0f;
}

// Slot 13, LAB_062c9530: any throttle request aborts the manoeuvre on the next Update.
void FaceBehavior::set_throttle(float Throttle)
{
    (void)Throttle;
    Flag1d4 = 1;
}

// Slot 21, LAB_062c89c0: drop the watch when the watched object is the one leaving.
void FaceBehavior::OnObjectLeftSystem(void* Arg)
{
    IObjRW* const Watched = GetWatchedRawBlock();
    if (Watched == nullptr)
        return;
    if (Watched->get_id() != static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Arg)))
        return;
    Watcher30.set(nullptr);
}

// Slot 24, LAB_062c8a00.
IObjRW const* FaceBehavior::get_target()
{
    return GetWatchedRawBlock();
}

IObjRW* FaceBehavior::GetWatchedRawBlock() const
{
    return ObjRwFromWatchable(const_cast<Watchable*>(Watcher30.target()));
}
