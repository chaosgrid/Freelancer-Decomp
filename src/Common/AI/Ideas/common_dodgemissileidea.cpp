// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll DodgeMissileIdea (ctor 0x062d94b0). See src/include/common_idea.h.

#include "common_idea.h"
#include "AI/Ideas/common_idea_internal.h"
#include "common_aisteer.h"   // BearingAngleToPoint, GetIfacePositionOrZero
#include "common_behavior.h"
#include "common_cequip.h"    // ThrustEquipType (IObjInspect::get_speed's 2nd argument)
#include "common_csys.h"      // Matrix / Transform
#include "common_iface.h"     // IObjInspect / IObjRW / IBehaviorManager / ObjRwFromWatchable
#include "common_pathfind.h"  // PathfindManager (IBehaviorManager::Pathfind)
#include "common_systemmanager.h" // SystemManager / ThrustManager (BaseIdea::Handle)
#include "x86math.h"          // I3DMathEngine::TransformMatrix

#include <cmath>
#include <cstring>

using namespace CmnIdea;

namespace
{
    // DodgeMissileIdea::Update's two inline cross products (062d975d, 062d98cf):
    // Dir x Axis in double, one float rounding per component.
    Vector DodgeCross(Vector const& Dir, Vector const& Axis)
    {
        Vector Result;
        Result.x = static_cast<float>(static_cast<double>(Dir.y) * Axis.z - static_cast<double>(Axis.y) * Dir.z);
        Result.y = static_cast<float>(static_cast<double>(Dir.z) * Axis.x - static_cast<double>(Axis.z) * Dir.x);
        Result.z = static_cast<float>(static_cast<double>(Axis.y) * Dir.x - static_cast<double>(Dir.y) * Axis.x);
        return Result;
    }
}

// ----------------------------------------------------------------------------
// DodgeMissileIdea -- see common_idea.h for the RTTI walk and size proof.
// ----------------------------------------------------------------------------
// DAT_063a0540 is the qword 0x3FE6666660000000: float 0.7f widened to double, not
// double 0.7 (MSVC's `fabs(<float expr>) > 0.7f`). The two gates against it point
// opposite ways: 062d9728 continues only on strictly-greater, 062d9881's JP arm
// (|dot| >= 0.7f or unordered) picks the UP column and the strictly-less
// fall-through picks FORWARD.
// ----------------------------------------------------------------------------

// Ctor FUN_062d94b0. Retail writes only +0x04, +0x08, +0x10 (byte), +0x14, +0x18
// and the vtable; Context (+0x0c) and Mode1c (+0x1c) are zeroed here as a
// port-only safety init (see the header).
DodgeMissileIdea::DodgeMissileIdea()
    : BaseIdea()
    , MissileWatcher()
    , Mode1c(0)           // not written by retail's ctor
{
}

// Own vtable slot 1 -- LAB_062d94f0.
int DodgeMissileIdea::Update(float /*DeltaTime*/)
{
    // DeltaTime is unused by retail (RET 4, slot never read).
    if (Object == nullptr)
    {
        return 3;
    }

    // Retail's second downcast test can only fire on the raw value 8.
    IObjRW* const Missile = WatchedObjectOf(MissileWatcher);
    if (Missile == nullptr)
    {
        return 1;
    }

    // Context is the IBehaviorManager; Context+0xc, Handle, Handle+0x14fc, its +0x84
    // owner and Handle+0x18 are all used by retail without a test.
    IBehaviorManager* const Manager = static_cast<IBehaviorManager*>(Context);
    FAiTarget* const Nav = &Handle->SelfTarget;
    FAiTarget* const NavTarget = Nav;
    IObjInspect const* const Owner = Nav->Iface;
    ThrustManager* const Thrust = &Handle->Thrust;

    // DAT_0639f534 == 0x3e490fdb == pi/16, also the spread angle.
    float const SpreadAngle = 0.19634954f;
    // DAT_063a0540: float 0.7f widened to double.
    double const HeadOnCosine = static_cast<double>(0.7f);

    // Volume probe: a zero return means "avoid, go this way instead" and takes
    // over the whole tick; same shape as BreakAwayIdea::Update's probe arm.
    Vector ProbeDir;
    if (Manager->Pathfind->GetUserZoneApproachDirection(&ProbeDir) == 0)
    {
        Matrix Frame;
        BuildOrientationBasis(Frame, Owner->get_transform().Rotation, ProbeDir, nullptr);

        Matrix Spread;
        BuildRandomSpreadFrame(Spread, Frame, SpreadAngle, 0.0f);

        float TurnAngle = 0.0f;
        Vector Throttle = TurnHelper::get_angular_throttle(Owner->get_transform().Rotation,
                                                           Spread, &TurnAngle);

        Vector Command{ 0.0f, 0.0f, 0.0f };
        if (TurnAngle > SpreadAngle)
        {
            Vector const Up = Manager->get_ship_up_direction();
            ComputeBankedSteeringFrame(Nav, Up, Spread, Throttle, 1);
            Command = Throttle;
        }
        // else: retail passes a separate zero-filled slot, so the small-turn arm
        // commands a zero vector, not the unsteered throttle.

        // Unlike the mode ladder's tail, no SetThrustersActive here.
        Thrust->SetAxisThrottle(Command);
        Thrust->SetThrottle(1.0f, 0);
        return 0;
    }

    // Mode ladder; values other than 0/1/2 return 0.
    Vector Command{ 0.0f, 0.0f, 0.0f };

    if (Mode1c == 2)
    {
        // Nudge sideways; never touches the throttle and returns on its own.

        // Retail inlines FAiTarget::RefreshPos (FUN_062c47c0) here; mode 1 calls it
        // out of line.
        NavTarget->RefreshPos();

        Vector const& MissilePos = ObjInspectOf(Missile)->get_position();
        Vector Delta{ MissilePos.x - NavTarget->CachedPos.x,
                      MissilePos.y - NavTarget->CachedPos.y,
                      MissilePos.z - NavTarget->CachedPos.z };

        Vector Dir{ 0.0f, 0.0f, 0.0f };
        VecNormalize3(&Delta, &Dir);

        // Retail accumulates z, y, x; kept in that order (x87 addition is not
        // associative).
        Matrix const& Rot = Owner->get_transform().Rotation;
        Vector const Fwd = MatrixColumn2(Rot);
        double const HeadOnDot = static_cast<double>(Dir.z) * Fwd.z
                               + static_cast<double>(Dir.y) * Fwd.y
                               + static_cast<double>(Dir.x) * Fwd.x;
        if (!(std::fabs(HeadOnDot) > HeadOnCosine))
        {
            return 0;
        }

        // Up is column 1 of a re-fetched get_transform (retail does not reuse the
        // matrix it just read).
        Matrix const& Rot2 = Owner->get_transform().Rotation;
        Vector const Up{ Rot2.E[0][1], Rot2.E[1][1], Rot2.E[2][1] };
        Vector const Cross = DodgeCross(Dir, Up);

        Vector Sideways;
        VecNormalize3(&Cross, &Sideways);

        Thrust->SetNudgeDirection(Sideways);
        return 0;
    }

    if (Mode1c == 1)
    {
        // The real dodge: fly a frame along the perpendicular escape direction.
        NavTarget->RefreshPos();

        Vector const& MissilePos = ObjInspectOf(Missile)->get_position();
        Vector Delta{ MissilePos.x - NavTarget->CachedPos.x,
                      MissilePos.y - NavTarget->CachedPos.y,
                      MissilePos.z - NavTarget->CachedPos.z };

        Vector Dir{ 0.0f, 0.0f, 0.0f };
        VecNormalize3(&Delta, &Dir);

        // Same dot as mode 2, gated the other way (see the section comment).
        Matrix const& Rot = Owner->get_transform().Rotation;
        Vector const Fwd = MatrixColumn2(Rot);
        double const HeadOnDot = static_cast<double>(Dir.z) * Fwd.z
                               + static_cast<double>(Dir.y) * Fwd.y
                               + static_cast<double>(Dir.x) * Fwd.x;

        // |dot| >= 0.7f or unordered: UP column; strictly-less: FORWARD column.
        // Both re-fetch get_transform.
        Matrix const& Rot2 = Owner->get_transform().Rotation;
        Vector Axis{ 0.0f, 0.0f, 0.0f };
        if (!(std::fabs(HeadOnDot) < HeadOnCosine))
        {
            Axis.x = Rot2.E[0][1];
            Axis.y = Rot2.E[1][1];
            Axis.z = Rot2.E[2][1];
        }
        else
        {
            Axis.x = Rot2.E[0][2];
            Axis.y = Rot2.E[1][2];
            Axis.z = Rot2.E[2][2];
        }

        Vector const Cross = DodgeCross(Dir, Axis);

        Vector Escape;
        VecNormalize3(&Cross, &Escape);

        // UpHint and the reference matrix are third and fourth get_transform fetches.
        Matrix const& Rot3 = Owner->get_transform().Rotation;
        Vector const UpHint{ Rot3.E[0][1], Rot3.E[1][1], Rot3.E[2][1] };
        Matrix Basis;
        BuildOrientationBasis(Basis, Owner->get_transform().Rotation, Escape, &UpHint);

        // Null OutAngle and no angle gate, unlike the probe arm: banked steering
        // always runs.
        Vector Throttle = TurnHelper::get_angular_throttle(Owner->get_transform().Rotation,
                                                           Basis, nullptr);

        Vector const Up = Manager->get_ship_up_direction();
        ComputeBankedSteeringFrame(Nav, Up, Basis, Throttle, 1);
        Command = Throttle;
    }
    else if (Mode1c != 0)
    {
        return 0;
    }
    // else Mode1c == 0: zero command into the shared tail.

    // Shared by modes 0 and 1, so SetThrustersActive(true) fires on both.
    Thrust->SetAxisThrottle(Command);
    Thrust->SetThrottle(1.0f, 0);
    Thrust->SetThrustersActive(true);
    return 0;
}

// FUN_062d9ae0 (RET 4): IObjRW -> Watchable +8 with a null arm, then
// BaseWatcher::set on MissileWatcher (+0x14).
void DodgeMissileIdea::SetWatchObject(void const* Object)
{
    Watchable const* const Target =
        (Object != nullptr)
            ? reinterpret_cast<Watchable const*>(static_cast<unsigned char const*>(Object) + 8)
            : nullptr;
    MissileWatcher.set(Target);
}

// Own vtable slot 4 -- LAB_062d9a70. Stand down: zero nudge and axis throttle,
// thrusters off. Retail re-zeroes the stack scratch before each call; Handle +
// 0x18 is recomputed per call and never tested.
void DodgeMissileIdea::Commit()
{
    Vector ZeroVector{ 0.0f, 0.0f, 0.0f };
    Handle->Thrust.SetNudgeDirection(ZeroVector);
    ZeroVector.x = 0.0f;
    ZeroVector.y = 0.0f;
    ZeroVector.z = 0.0f;
    Handle->Thrust.SetAxisThrottle(ZeroVector);
    Handle->Thrust.SetThrustersActive(false);
}
