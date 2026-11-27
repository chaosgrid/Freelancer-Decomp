// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// LaunchAction, JumpInAction, LaunchActionVars. Declared in common_cequip.h; shared helpers are
// in common_cequip_internal.h (namespace CmnCequip).
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"

#include "common_cobject.h"
#include "common_csys.h"
#include "common_fuses.h"
#include "common_iface.h"
#include "common_ini.h"
#include "common_misc.h"     // SinglePlayer
#include "common_physys.h"
#include "common_spatial.h"
#include "common_timing.h"
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "x87_trig.h"        // retail's inline FSIN / FCOS (LaunchAction::update)
#include "ivp_physics/ivp_real_object.h"
#include "ivp_physics/ivp_controller.h"  // IVP_Event_Sim (do_simulation_controller)

#include <cstdarg>
#include <cstdint>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Equipment/common_cequip_internal.h"

using namespace CmnCequip;

// Retail exports 3235/3236 (.rdata:0639e568 / 0639e564), see common_cequip.h.
namespace LaunchActionVars
{
    COMMON_API float const launch_coast_time = 0.5f;
    COMMON_API float const launch_decel_time = 2.5f;
}

namespace
{
    // The `Obj && (ObjectClass & 0x103) == 0x103 ? Obj : 0` receiver retail builds before
    // every CEqObj::set_control_exclusion call in this file; the call itself is made
    // through the possibly-null result with no test.
    CEqObj* EqObjOrNull(CObject* Obj)
    {
        return (Obj != nullptr && (Obj->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj) ? (CEqObj*)Obj : nullptr;
    }
}

// FUN_062afda0. DockObjWatcher +0x1c, BaseObj +0x24, DockPortIndex +0x2c. LaunchFlags is
// BaseObj's TypeBits (no null test); bit 0x20 forces DockPortIndex 1. The pad bytes +0x4e/+0x4f
// are not written.
LaunchAction::LaunchAction(IObjRW* DockObj, CEqObj* InBaseObj, int InDockPortIndex)
    : BaseObj(InBaseObj)
    , LaunchFlags(0)
    , DockPortIndex(InDockPortIndex)
    , CoastElapsed(0.0f)
    , bNoDockingCamera(false)
    , bUpdateComplete(false)
    , LaunchOrient(1.0f, 0.0f, 0.0f,
                   0.0f, 1.0f, 0.0f,
                   0.0f, 0.0f, 1.0f)
    , CoastTimer(0.0f)
{
    DockObjWatcher.set(DockObj);   // IObjRW -> its Watchable base (+8); null stays null
    LaunchPos.x = 0.0f;
    LaunchPos.y = 0.0f;
    LaunchPos.z = 0.0f;
    LaunchDir.x = 0.0f;
    LaunchDir.y = 0.0f;
    LaunchDir.z = 0.0f;

    LaunchFlags = BaseObj->TypeBits;
    if ((LaunchFlags & OBJ_DOCKING_RING) != 0)
    {
        DockPortIndex = 1;
    }
}

LaunchAction::~LaunchAction()
{
    // FUN_062afec0. An AI-directive launch registered `this` as a directive callback (Start);
    // it is unregistered here so the BehaviorManager keeps no dangling pointer. The watcher
    // member destructor runs after this body.
    IObjRW* Dock = ObjRwFromWatchable(WatchedTarget(DockObjWatcher));

    if (Dock == nullptr)
        return;

    IBehaviorManager* BM = GetBehaviorManager(Dock);
    if (BM != nullptr)
    {
        // Same +0x18 DirectiveCallback adjustment as set_directive_callback in Start.
        BM->clear_directive_callback(this);
    }
}

// FUN_062b01f0, LaunchAction's DirectiveCallback slot 0 (vtable 0639e57c). Retail addresses the
// fields relative to the DirectiveCallback sub-object at +0x18; the compiler's thunk does that
// here.
void LaunchAction::OnDirectiveComplete(int CompletedBehavior, int DirectiveResult)
{
    // RET 0x8 fixes two arguments, but retail never reads them.
    (void)CompletedBehavior;
    (void)DirectiveResult;

    IObjRW* Dock = ObjRwFromWatchable(WatchedTarget(DockObjWatcher));

    if (Dock == nullptr)
    {
        return;
    }

    CObject* Docked = (CObject*)Dock->cobject();   // slot 84 (+0x150)

    // Called through the possibly-null narrowing, as in retail.
    EqObjOrNull(Docked)->set_control_exclusion(0);

    {
        ObjAffectOf(Dock)->launch_complete(BaseObj->Id);
    }

    bUpdateComplete = true;
}

void LaunchAction::Start()
{
    // FUN_062aff80. Retail quirk kept: Dock is dereferenced with no null test (a null DockObj
    // crashes).
    IObjRW* Dock = ObjRwFromWatchable(WatchedTarget(DockObjWatcher));

    CObject* Docked = (CObject*)Dock->cobject();   // slot 84 (+0x150)

    if ((LaunchFlags & OBJ_DOCKING_RING) == 0)
    {
        // An AI directive launch: the ship flies itself out.
        if ((LaunchFlags & (OBJ_STATION | OBJ_CAPITAL)) != 0)
        {
            pub::AI::DirectiveLaunchOp Op;
            Op.LaunchObjectId = BaseObj->Id; // CSimple::Id, +0xb0
            Op.DockPortIndex = DockPortIndex;
            Op.TerminalType = TERM_BERTH;

            IBehaviorManager* BM = GetBehaviorManager(Dock);
            if (BM != nullptr)
            {
                BM->set_directive_callback(this);

                EqObjOrNull(Docked)->set_control_exclusion(6);

                // IDirectiveInfo {1, Op+0x08, 8}; the middle dword is BaseOp::Priority, zeroed
                // by the DirectiveLaunchOp ctor.
                unsigned const DirWords[3] = { 1, Op.Priority, 8 };
                IDirectiveInfo Dir;
                memcpy(&Dir, DirWords, sizeof(Dir));

                BM->set_current_directive(Dir, &Op);
            }
        }
        return;
    }

    // Immediate physical launch.
    Docked->enable_collisions_r(false);

    EqObjOrNull(Docked)->set_control_exclusion(7);

    // IObjAffect slot 0 (IObjRW+4).
    ObjAffectOf(Dock)->set_throttle(0.0f);

    BaseObj->launch_pos(LaunchPos, LaunchOrient, DockPortIndex);

    // Push the launch point 150 along the forward axis, then correct by the docked ship's
    // origin-to-"hpmount" offset (ported from the listing).
    Vector Forward{-LaunchOrient.E[0][2], -LaunchOrient.E[1][2], -LaunchOrient.E[2][2]};
    LaunchPos.x += Forward.x * 150.0f;
    LaunchPos.y += Forward.y * 150.0f;
    LaunchPos.z += Forward.z * 150.0f;

    Vector HpOut;
    HardpointPosition(Docked->EngineInstanceId, "hpmount", &HpOut, nullptr);

    LaunchPos.x += Docked->WorldPosition.x - HpOut.x;
    LaunchPos.y += Docked->WorldPosition.y - HpOut.y;
    LaunchPos.z += Docked->WorldPosition.z - HpOut.z;

    LaunchDir = Vector{LaunchOrient.E[0][2], LaunchOrient.E[1][2], LaunchOrient.E[2][2]};

    // 80.0f is an unnamed .rdata constant.
    CoastTimer = LaunchActionVars::launch_coast_time * 80.0f;

    PhySys::Controller::Register(this, Docked);

    Archetype::EqObj* Arch = (Archetype::EqObj*)BaseObj->Archetype;
    bNoDockingCamera = !Arch->bDockingCamera;
}

// LAB_062b0270, LaunchAction's IAction slot 1 (vtable 0639e5a4), from the listing. Dt is never
// read.
bool LaunchAction::update(float /*Dt*/)
{
    IObjRW* Dock = ObjRwFromWatchable(WatchedTarget(DockObjWatcher));

    if (Dock == nullptr)
    {
        bUpdateComplete = true;
        return !bUpdateComplete;
    }

    if ((LaunchFlags & OBJ_DOCKING_RING) == 0)
    {
        // AI-directive launch: done once OnDirectiveComplete fires.
        return !bUpdateComplete;
    }

    if (!bNoDockingCamera)
    {
        // Complete once CoastElapsed (+0x30, advanced by do_simulation_controller) reaches
        // launch_coast_time + launch_decel_time; unordered is not complete.
        bUpdateComplete = ((double)CoastElapsed >=
                           (double)LaunchActionVars::launch_coast_time +
                           LaunchActionVars::launch_decel_time);

        if (bUpdateComplete)
        {
            // Throttle first, then a -80.0f (DAT_0639e560, negated) push along the docked
            // object's current forward axis (not LaunchOrient); no null test on cobject().
            ObjAffectOf(Dock)->set_throttle(1.0f);

            CObject* Docked0 = (CObject*)Dock->cobject();   // slot 84 (+0x150)

            float const Scale = -80.0f;
            Vector Delta{ Docked0->Orientation.E[0][2] * Scale,
                          Docked0->Orientation.E[1][2] * Scale,
                          Docked0->Orientation.E[2][2] * Scale };
            PhySys::AddToVelocity(Docked0, Delta);
        }
    }

    if (!bUpdateComplete)
    {
        // Not complete: keep the controller awake. With bNoDockingCamera the coast check above
        // is skipped; do_simulation_controller sets the flag instead.
        PhySys::Controller::wakeup();
        return !bUpdateComplete;
    }

    // Retail re-resolves through the watcher rather than reusing Docked0.
    CObject* Docked = (CObject*)Dock->cobject();   // slot 84 (+0x150)

    if (!SinglePlayer())
    {
        // IObjInspect slot 47 (+0xbc), is_player.
        bool const bIsServer = ObjInspectOf(Dock)->is_player();

        if (bIsServer)
        {
            // A multiplayer player leaves the dock with a random heading: two random angles in
            // [-pi/8, pi/8) (pi/4 double DAT_0639e5c8, pi/8 DAT_0639e5c0) turn the x/z, then y/z
            // columns, and the object is beamed to its centre of mass (slot 5). cos is rounded
            // to float, sin and every product are not.
            Matrix Orient = ObjInspectOf(Dock)->get_transform().Rotation;

            double Angle = (double)((float)rand() * 3.0517578125e-05f) *
                           0.78539818525314331 - 0.39269909262657166;
            float Cos = X87CosF(Angle);
            double Sin = X87Sin(Angle);
            unsigned Row = 0;
            for (; Row < 3; ++Row)
            {
                float X = Orient.E[Row][0];
                float Z = Orient.E[Row][2];
                Orient.E[Row][0] = (float)((double)Cos * X - Sin * Z);
                Orient.E[Row][2] = (float)((double)Cos * Z + X * Sin);
            }

            Angle = (double)((float)rand() * 3.0517578125e-05f) *
                    0.78539818525314331 - 0.39269909262657166;
            Cos = X87CosF(Angle);
            Sin = X87Sin(Angle);
            for (Row = 0; Row < 3; ++Row)
            {
                float Y = Orient.E[Row][1];
                float Z = Orient.E[Row][2];
                Orient.E[Row][1] = (float)(Sin * Z + (double)Cos * Y);
                Orient.E[Row][2] = (float)((double)Cos * Z - Y * Sin);
            }

            Vector Center = ObjInspectOf(Dock)->get_center_of_mass();
            Docked->beam_object(Center, Orient, false);
        }
    }

    // No null test on Docked.
    Docked->enable_collisions_r(true);

    EqObjOrNull(Docked)->set_control_exclusion(0);

    {
        // IObjAffect slot 28 (+0x70).
        ObjAffectOf(Dock)->launch_complete(BaseObj->Id);
    }

    {
        // Retail sets the throttle a second time on this path.
        ObjAffectOf(Dock)->set_throttle(1.0f);
    }

    IBehaviorManager* BM = GetBehaviorManager(Dock);
    if (BM != nullptr)
    {
        BM->update_current_behavior_throttle(1.0f);
    }

    return !bUpdateComplete;
}

// LAB_062b0560, LaunchAction's PhySys::Controller slot 4 (from the listing). Advances
// CoastElapsed (only when LaunchFlags == 0x20 exactly, not a bit test) and beams the docked
// object along LaunchDir: an ease-out (150.0f DAT_0639e56c, 80.0f DAT_0639e560) during
// launch_decel_time, then a linear countdown over launch_coast_time. With bNoDockingCamera it
// beams straight to the launch pose and completes. No null test on cobject().
void LaunchAction::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    (void)CoreList;
    float const Dt = EventSim->delta_time;
    if (LaunchFlags == OBJ_DOCKING_RING)
    {
        CoastElapsed += Dt;
    }

    IObjRW* Watched = ObjRwFromWatchable(WatchedTarget(DockObjWatcher));
    if (Watched == nullptr)
    {
        return;
    }

    CObject* Dock = (CObject*)Watched->cobject();   // slot 84 (+0x150)

    if (bNoDockingCamera)
    {
        Dock->beam_object(LaunchPos, LaunchOrient, false);
        bUpdateComplete = true;
        return;
    }

    // Unordered takes the countdown branch; all unrounded.
    double X;
    float const Decel = LaunchActionVars::launch_decel_time;
    float const Coast = LaunchActionVars::launch_coast_time;
    if (CoastElapsed < Decel)
    {
        double const Remaining = (double)Decel - CoastElapsed;
        double const Frac = Remaining / Decel;
        X = (150.0 * Frac * Frac + 80.0 * Remaining) + CoastTimer;
    }
    else
    {
        X = (((double)Coast + Decel - CoastElapsed) / Coast) * CoastTimer;
    }

    // x87: the x product is added unrounded; the y and z products are rounded to float first.
    float const StepY = (float)(X * LaunchDir.y);
    float const StepZ = (float)(X * LaunchDir.z);
    Vector InterpPos;
    InterpPos.x = (float)(X * LaunchDir.x + LaunchPos.x);
    InterpPos.y = StepY + LaunchPos.y;
    InterpPos.z = StepZ + LaunchPos.z;

    Dock->beam_object(InterpPos, LaunchOrient, false);
}

// Inlined in CShip::jump_in (FUN_062b2d40). JumpObjWatcher is +0x18, bJumpComplete +0x64; the
// pad bytes +0x65..+0x67 are not written.
JumpInAction::JumpInAction(IObjRW* JumpObj, Vector const& Pos, Matrix const& Orient, float Duration)
    : JumpElapsed(0.0f)
    , JumpDuration(0.0f)
    , JumpApproachBudget(0.0f)
    , JumpOrient(1.0f, 0.0f, 0.0f,
                 0.0f, 1.0f, 0.0f,
                 0.0f, 0.0f, 1.0f)
    , Field5c(0.0f)
    , Field60(0.0f)
    , bJumpComplete(false)
{
    JumpPos.x = 0.0f;
    JumpPos.y = 0.0f;
    JumpPos.z = 0.0f;

    JumpObjWatcher.set(JumpObj);

    // No null test on the watched object (a null JumpObj faults, as in retail).
    IObjRW* const Watched = ObjRwFromWatchable(WatchedTarget(JumpObjWatcher));
    CObject* Target = (CObject*)Watched->cobject();                     // slot 84 (+0x150)
    if (Target == nullptr || (Target->ObjectClass & CObject::ClassShip) != CObject::ClassShip)
    {
        Target = nullptr;
    }

    JumpPos = Pos;
    JumpOrient = Orient;
    JumpDuration = Duration;
    JumpApproachBudget = Duration * 2000.0f;

    // Written but never read by JumpInAction.
    Field5c = 1500.0f;
    Field60 = 10.0f;

    // Retail quirk kept: Target is dereferenced with no null test (a non-ship crashes).
    CShip* TargetShip = (CShip*)Target;
    TargetShip->enable_collisions_r(false);
    TargetShip->Throttle = 0.0f;

    CEquip* EngineEquip = const_cast<CEquip*>(TargetShip->EquipManager.FindFirst(EquipTypeEngine));
    if (EngineEquip != nullptr)
    {
        if (!EngineEquip->IsActive())
        {
            EngineEquip->Activate(true);
        }
    }

    TargetShip->beam_object(Pos, Orient, false);
    PhySys::Controller::Register(this, TargetShip);
}

JumpInAction::~JumpInAction()
{
    // FUN_062b2f30. Undoes the ctor's enable_collisions_r(false) on the re-resolved target.
    IObjRW* const Watched = ObjRwFromWatchable(WatchedTarget(JumpObjWatcher));
    if (Watched != nullptr)
    {
        CObject* Target = (CObject*)Watched->cobject();   // slot 84 (+0x150)
        if (Target != nullptr)
        {
            Target->enable_collisions_r(true);
        }
    }

    PhySys::Controller::UnRegister(this);
}

bool JumpInAction::update(float /*Dt*/)
{
    // FUN_062afd80 (IAction slot 1). bJumpComplete is set by do_simulation_controller.
    if (!bJumpComplete)
    {
        PhySys::Controller::wakeup();
    }
    return !bJumpComplete;
}

// LAB_062afbe0, JumpInAction's PhySys::Controller slot 4 (from the listing). Until JumpDuration
// seconds have elapsed the ship eases toward JumpPos along JumpOrient's forward column by
// ((JumpDuration - t) / JumpDuration)^2 * JumpApproachBudget; then it is placed
// exactly, its engine re-activated and the arrival system id reported (IObjAffect
// jump_in_complete).
// Retail quirk kept: no null test on the resolved ship (a non-ship faults).
void JumpInAction::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    (void)CoreList;
    IObjRW* Watched = ObjRwFromWatchable(WatchedTarget(JumpObjWatcher));

    if (Watched == nullptr)
    {
        bJumpComplete = true;
        return;
    }

    CObject* Target = (CObject*)Watched->cobject();   // slot 84 (+0x150)
    CShip* TargetShip = (Target != nullptr && (Target->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
        ? (CShip*)Target : nullptr;

    // FST: the float is stored, the unrounded sum feeds the compare and the ease.
    double const Elapsed = (double)JumpElapsed + EventSim->delta_time;
    JumpElapsed = (float)Elapsed;

    // Unordered eases.
    if (!(Elapsed >= JumpDuration))
    {
        // Clamped to [0, 1] (NaN passes), all unrounded.
        double Blend = ((double)JumpDuration - Elapsed) / JumpDuration;
        Blend = Blend * Blend;
        if (Blend > 1.0)
            Blend = 1.0;
        else if (Blend < 0.0)
            Blend = 0.0;
        Blend = Blend * JumpApproachBudget;

        // x87: x and z are rounded before the add, y is not.
        float const OffsetX = (float)(JumpOrient.E[0][2] * Blend);
        float const OffsetZ = (float)(JumpOrient.E[2][2] * Blend);
        Vector InterpPos;
        InterpPos.x = OffsetX + JumpPos.x;
        InterpPos.y = (float)(JumpOrient.E[1][2] * Blend + JumpPos.y);
        InterpPos.z = OffsetZ + JumpPos.z;
        TargetShip->beam_object(InterpPos, JumpOrient, false);   // vt+0x7c
        return;
    }

    bJumpComplete = true;
    TargetShip->beam_object(JumpPos, JumpOrient, false);
    TargetShip->Throttle = 0.0f;
    CEquip* EngineEquip = const_cast<CEquip*>(TargetShip->EquipManager.FindFirst(EquipTypeEngine));
    if (EngineEquip != nullptr && !EngineEquip->IsActive())
        EngineEquip->Activate(true);

    // IObjAffect jump_in_complete (+0x74; ship gameobjects send JumpInComplete with it) gets
    // the id of the ship's system.
    IObjDB* ObjDb = TargetShip->ObjectDb;   // +0xac
    if (ObjDb == nullptr)
        return;
    Universe::ISystem const* System = ObjDb->GetZoneDb();
    if (System == nullptr)
        return;
    unsigned const SystemId = System->GetId();
    ObjAffectOf(Watched)->jump_in_complete(SystemId);
}
