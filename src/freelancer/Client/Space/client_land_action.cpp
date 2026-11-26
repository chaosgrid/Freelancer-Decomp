// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/client_land_action.h"

#include "Space/client_gameobj.h"
#include "common_archetype.h"   // Archetype::EqObj::bDockingCamera
#include "common_cobject.h"     // CEqObj, CShip
#include "Space/Effects/jump_effect.h"        // FJumpShipEffect (the Ship's +0x188)
#include "Space/space_state.h"        // SetPendingLandBase (FUN_00548ef0)
#include "Space/player_behavior.h"    // GetPlayerObjRW (FUN_0054baf0)

#include <cmath>
#include <cstring>

static_assert(sizeof(Action_Land) == 0x50, "Action_Land is IAction::operator new(0x50)");
static_assert(sizeof(Action_JumpOut) == 0x64, "Action_JumpOut is IAction::operator new(0x64)");

namespace
{
    // DAT_00678b68 -- ship.cpp's 3DMathEngine, acquired on first use at every site.
    I3DMathEngine* G_ShipMath = nullptr;

    I3DMathEngine* ShipMath()
    {
        if (G_ShipMath == nullptr)
            G_ShipMath = Acquire3DMathEngine();
        return G_ShipMath;
    }

    // DAT_005c75dc: the eased fraction's cap.
    constexpr float kJumpOutMaxFraction = 1.0f;

    // DAT_005dfb34: the landing run's length in seconds (the Ship's land request hands the same
    // constant to the docking camera).
    constexpr float kLandSeconds = 3.0f;
    // DAT_005d440c: the speed gained per second of the run.
    constexpr float kLandAcceleration = 50.0f;
    // DAT_005ca230: the speed cap.
    constexpr float kLandMaxSpeed = 100.0f;
}

I3DMathEngine* GetShipMathEngine()
{
    return ShipMath();
}

Action_Land::Action_Land(FClientShipGameobj* InShip, CEqObj* Dock)
{
    // FUN_00542b70. With no Ship nothing else is written.
    Elapsed = 0.0f;
    Gameobj = InShip;
    if (Gameobj == nullptr)
        return;

    // None of the calls below tests the narrowed CShip.
    CObject* const Object = Gameobj->Object;
    CShip* Ship = nullptr;
    if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
        Ship = static_cast<CShip*>(Object);

    // Control exclusion 7 -- with a set_control_exclusion(0) first when 7 is already excluded.
    PhySys::Controller::Register(this, Ship);
    Ship->enable_collisions_r(false);
    if (Ship->is_control_excluded(7))
        Ship->set_control_exclusion(0);
    Ship->set_control_exclusion(7);

    // Launch point 0: the position is dropped, the orientation kept.
    BaseId = Dock->DockingPointBaseId;
    Vector LaunchPos;
    Dock->launch_pos(LaunchPos, Orient, 0);

    // Columns 0 and 2 negated -- the launch orientation turned half way round its Y axis,
    // facing into the dock.
    Orient.E[0][0] = -Orient.E[0][0];
    Orient.E[1][0] = -Orient.E[1][0];
    Orient.E[2][0] = -Orient.E[2][0];
    Orient.E[0][2] = -Orient.E[0][2];
    Orient.E[1][2] = -Orient.E[1][2];
    Orient.E[2][2] = -Orient.E[2][2];

    Vector const Velocity = Gameobj->get_velocity();
    Speed = std::sqrt(Velocity.x * Velocity.x + Velocity.y * Velocity.y + Velocity.z * Velocity.z);

    bNoDockingCamera = !Dock->eqobjarch()->bDockingCamera;

    Ship->set_throttle(0.0f);
}

Action_Land::~Action_Land()
{
    // FUN_00542d40: the compiler's own sequence for this class.
}

char const* Action_Land::get_id() const
{
    // LAB_00542d10
    return "Land";
}

bool Action_Land::update(float Dt)
{
    // LAB_00542da0. The clock advances even with no Ship.
    Elapsed += Dt;
    if (Gameobj == nullptr)
        return true;

    // Without a docking camera the landing ends at once; with one it waits for the run (below
    // 3 or unordered keeps waiting).
    if (!bNoDockingCamera && !(Elapsed >= kLandSeconds))
        return true;

    // The player's Ship asks the space frame for the base (FUN_00548ef0); any other Ship is
    // removed. The action stays on the list either way.
    if (static_cast<void*>(static_cast<IObjRW*>(Gameobj)) == GetPlayerObjRW())
        SetPendingLandBase(&BaseId);
    else
        Gameobj->MarkForRemoval(true);
    return true;
}

void Action_Land::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // LAB_00542e10; CoreList is never read.
    (void)CoreList;
    if (Gameobj == nullptr)
        return;

    // Fetched before the tests.
    CObject* const Object = const_cast<CObject*>(Gameobj->cobject());

    // Only a docking-camera landing, and only during the run (unordered stops).
    if (bNoDockingCamera)
        return;
    if (!(Elapsed < kLandSeconds))
        return;

    float RunSpeed = Elapsed * kLandAcceleration + Speed;
    if (RunSpeed > kLandMaxSpeed)
        RunSpeed = kLandMaxSpeed;

    // Along the negated column 2 of the dock-facing orientation.
    float const Dt = EventSim->delta_time;
    float const StepX = (-Orient.E[0][2] * RunSpeed) * Dt;
    float const StepY = (-Orient.E[1][2] * RunSpeed) * Dt;
    float const StepZ = (RunSpeed * -Orient.E[2][2]) * Dt;

    // Beamed with its own orientation, not attached.
    Vector const& Position = Gameobj->get_position();
    Vector NewPosition;
    NewPosition.z = StepZ + Position.z;
    NewPosition.y = StepY + Position.y;
    NewPosition.x = StepX + Position.x;
    Object->beam_object(NewPosition, Object->Orientation, false);
}

Action_JumpOut::Action_JumpOut(FClientShipGameobj* InShip, float InDuration, unsigned InGateType)
{
    // FUN_00542f10
    Ship = InShip;
    bDone = false;
    Elapsed = 0.0f;
    Duration = InDuration;
    GateType = InGateType;

    // Only a ship whose object has all of class 0x103; otherwise the rest of the object stays
    // unwritten, as retail leaves it.
    if (Ship == nullptr)
        return;
    CObject* const Object = Ship->GetObject();
    if (Object == nullptr || (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassEqObj) != CObject::ClassEqObj)
        return;

    Object->enable_collisions_r(false);
    static_cast<IObjAffect*>(Ship)->set_throttle(1.0f);
    Vector ZeroAxis;
    ZeroAxis.x = 0.0f;
    ZeroAxis.y = 0.0f;
    ZeroAxis.z = 0.0f;
    static_cast<IObjAffect*>(Ship)->set_axis_throttle(ZeroAxis);

    // get_orientation is ICF-folded with get_transform: the pointer is the whole Transform.
    // With a jump effect the end frame is its gate hardpoint frame (+0xa8) instead.
    Transform StartXform;
    Transform EndXform;
    Transform const& ShipXform =
        reinterpret_cast<Transform const&>(Ship->get_orientation());
    std::memcpy(&StartXform, &ShipXform, sizeof(Transform));
    std::memcpy(&EndXform, &ShipXform, sizeof(Transform));
    FJumpShipEffect* const Effect = static_cast<FJumpShipEffect*>(Ship->JumpEffect);
    if (Effect != nullptr)
        std::memcpy(&EndXform, &Effect->GateHardpointFrame, sizeof(Transform));

    // Retail converts the start frame a second time into a local nothing reads.
    ShipMath()->MatrixToQuaternion(StartRotation,
                                   StartXform.Rotation);
    ShipMath()->MatrixToQuaternion(EndRotation,
                                   EndXform.Rotation);
    Quaternion Unused;
    ShipMath()->MatrixToQuaternion(Unused, StartXform.Rotation);

    StartPosition = StartXform.Translation;
    EndPosition = EndXform.Translation;

    PhySys::Controller::Register(this, Object);
}

Action_JumpOut::~Action_JumpOut()
{
    // FUN_00543130: the compiler's own sequence for this class.
}

char const* Action_JumpOut::get_id() const
{
    // LAB_00543100
    return "JumpOut";
}

bool Action_JumpOut::update(float Dt)
{
    // FUN_00543190: the action stays until the controller has marked it done (re-read after
    // the wakeup).
    (void)Dt;
    if (!bDone)
        PhySys::Controller::wakeup();
    return !bDone;
}

void Action_JumpOut::do_simulation_controller(IVP_Event_Sim* EventSim,
                                              IVP_U_Vector<IVP_Core>* CoreList)
{
    // LAB_005431c0; CoreList is never read.
    (void)CoreList;

    // Past the duration (compared unrounded) the action is done and nothing moves.
    double const Clock = static_cast<double>(EventSim->delta_time) + static_cast<double>(Elapsed);
    Elapsed = static_cast<float>(Clock);
    if (Clock > static_cast<double>(Duration))
    {
        bDone = true;
        return;
    }

    // Eased to t * t, stored as a float.
    double const Fraction = Clock / static_cast<double>(Duration);
    float Eased = static_cast<float>(Fraction * Fraction);
    if (Eased > kJumpOutMaxFraction)
        Eased = kJumpOutMaxFraction;

    // A gate type with bit 0x800 keeps the start orientation.
    Quaternion Rotation;
    if ((GateType & OBJ_JUMP_HOLE) != 0)
    {
        Rotation = StartRotation;
    }
    else
    {
        Quaternion Slerped;
        ShipMath()->QuatSlerp(Slerped, StartRotation, EndRotation, Eased);
        Rotation = Slerped;
    }

    Vector Position;
    Position.x = (EndPosition.x - StartPosition.x) * Eased + StartPosition.x;
    Position.y = (EndPosition.y - StartPosition.y) * Eased + StartPosition.y;
    Position.z = (EndPosition.z - StartPosition.z) * Eased + StartPosition.z;

    // Only a CShip is beamed, not attached.
    if (Ship == nullptr)
        return;
    CObject* const Object = Ship->GetObject();
    if (Object == nullptr || (static_cast<unsigned>(Object->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return;
    Matrix Orientation;
    ShipMath()->QuaternionToMatrix(Orientation, Rotation);
    Object->beam_object(Position, Orientation, false);
}
