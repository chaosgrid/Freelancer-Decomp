// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CPhysControllerEquip, CSteering, CNudgeEngine, CStrafeEngine (retail CStrafeEngine.cpp).
// Declared in common_cequip.h; shared helpers are in common_cequip_internal.h (namespace
// CmnCequip).
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"  // AnimDB::Add/Rem (common_animdb.cpp)

#include "common_cobject.h"
#include "common_csys.h"     // Acquire3DMathEngine (MatrixToQuaternionViaMathEngine)
#include "common_fuses.h"    // WeaponModDB_GetShieldMod (CEShield::GetWeaponModifier)
#include "common_iface.h"    // EquipStatus, EquipStatusFlag* (CEquip::GetStatus)
#include "common_ini.h"
#include "common_misc.h"     // ComputeExplosiveImpulses (CAttachedEquip::Disconnect)
#include "common_physys.h"
#include "common_spatial.h"  // ISpatialPartition (ScanList::add_in_range bucket query)
#include "common_timing.h"   // Timing::GetGlobalTime (CEShieldGenerator / CEShield)
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
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

// ============================================================
// CPhysControllerEquip — base equipment physics controller
// ============================================================

// .text:06286e40: Controller(), then [this+0x10] = 0. Nothing past +0x10 -- the base ends there.
CPhysControllerEquip::CPhysControllerEquip()
    : bControllerEnabled(false)
{
}

// .text:06286e70: Controller's three dwords (+4/+8/+0xc), then the +0x10 byte.
CPhysControllerEquip::CPhysControllerEquip(CPhysControllerEquip const& Other)
    : PhySys::Controller(Other)
    , bControllerEnabled(Other.bControllerEnabled)
{
}

// .text:06287870 (one body for the CPhysControllerEquip, CSteering, CNudgeEngine and
// CStrafeEngine destructors): a direct DisableController call when the controller is
// registered, then ~Controller.
CPhysControllerEquip::~CPhysControllerEquip()
{
    if (bControllerEnabled)
    {
        CPhysControllerEquip::DisableController();
    }
}

CPhysControllerEquip& CPhysControllerEquip::operator=(CPhysControllerEquip const& Other)
{
    // 0x06280ad0 (ICF-folded with PhyCollisionStateManager::operator=); no self test.
    PhySys::Controller::operator=(Other);
    bControllerEnabled = Other.bControllerEnabled;
    return *this;
}

bool CPhysControllerEquip::ControlObject(CObject* Object)
{
    // Ghidra @ 0x3b090
    if (!bControllerEnabled)
    {
        bControllerEnabled = true;
        PhySys::Controller::Register(this, Object);
        wakeup();
    }
    return bControllerEnabled;
}

bool CPhysControllerEquip::DisableController()
{
    // Ghidra @ 0x3b0c0
    if (bControllerEnabled)
    {
        bControllerEnabled = false;
        PhySys::Controller::UnRegister(this);
    }
    return bControllerEnabled;
}

bool CPhysControllerEquip::IsControllerEnabled() const
{
    // 0x26e60 (ICF-shared with CEquip::IsActive).
    return bControllerEnabled;
}

// ============================================================
// CSteering — ship steering controller
// ============================================================

float const CSteering::AXIS_CHANGE_THRESHOLD = 1e-6f; // retail DAT_0639ecc8 = 0.000001, not 1e-12

// .text:062bb5e0: base (+0x10 = 0), then +0x14 OwnerShip, +0x18 bActive, +0x1c..+0x24 zeroed.
CSteering::CSteering()
    : OwnerShip(nullptr)
    , bActive(false)
{
    AxisThrottle.x = 0.0f;
    AxisThrottle.y = 0.0f;
    AxisThrottle.z = 0.0f;
}

// .text:06287810: base copy (+4..+0x10), then +0x14 dword, +0x18 byte, +0x1c Vector.
CSteering::CSteering(CSteering const& Other)
    : CPhysControllerEquip(Other)
    , OwnerShip(Other.OwnerShip)
    , bActive(Other.bActive)
    , AxisThrottle(Other.AxisThrottle)
{
}

CSteering::~CSteering()
{
}

CSteering& CSteering::operator=(CSteering const& Other)
{
    // .text:06287950 (ICF-folded with CNudgeEngine::operator=); no self test.
    CPhysControllerEquip::operator=(Other);
    OwnerShip = Other.OwnerShip;
    bActive = Other.bActive;
    AxisThrottle = Other.AxisThrottle;
    return *this;
}

bool CSteering::EnableController()
{
    // 0x4d330, one body shared by CNudgeEngine/CSteering/CStrafeEngine::EnableController
    // (CPhysControllerEquip's own slot is _purecall): an unconditional virtual
    // ControlObject(OwnerShip) (+0x28), with no guard; ControlObject is the idempotency check.
    return ControlObject(OwnerShip);
}

bool CSteering::Activate(bool bOn)
{
    // Ghidra @ 0x3bd0
    bActive = bOn;
    return bActive;
}

bool CSteering::IsActive() const
{
    // .text:062877f0 (one body for CSteering/CNudgeEngine::IsActive): the virtual
    // IsControllerEnabled (vtable +0x24), then bActive.
    return IsControllerEnabled() && bActive;
}

double CSteering::GetAxisSteeringTorque(unsigned Axis, float Factor) const
{
    // .text:062bb610. OwnerShip and its archetype (+0x88) are dereferenced without a test; any
    // other axis returns the local 0.0f times Factor. The product stays unrounded in ST0.
    float const Zero = 0.0f;
    if (!IsControllerEnabled() || !bActive)
        return Zero;
    Archetype::Ship const* ShipArch = OwnerShip->shiparch();
    if (Axis == 0)
        return (double)ShipArch->SteeringTorqueX * Factor;
    if (Axis == 1)
        return (double)ShipArch->SteeringTorqueY * Factor;
    if (Axis == 2)
        return (double)ShipArch->SteeringTorqueZ * Factor;
    return (double)Zero * Factor;
}

Vector const& CSteering::GetAxisThrottle() const
{
    return AxisThrottle;
}

void CSteering::SetAxisThrottle(Vector const& Throttle)
{
    // .text:062bb6a0. Each axis (z, y, x) is clamped to [-1, 1]; a NaN passes through.
    float const ClampedZ = Throttle.z < -1.0f ? -1.0f : (Throttle.z > 1.0f ? 1.0f : Throttle.z);
    float const ClampedY = Throttle.y < -1.0f ? -1.0f : (Throttle.y > 1.0f ? 1.0f : Throttle.y);
    float const ClampedX = Throttle.x < -1.0f ? -1.0f : (Throttle.x > 1.0f ? 1.0f : Throttle.x);

    // The threshold is squared and stored as a float.
    float const ThresholdSq = AXIS_CHANGE_THRESHOLD * AXIS_CHANGE_THRESHOLD;

    // Squared differences stay on the FPU; greater or unordered on any axis updates.
    double const Dx = (double)AxisThrottle.x - ClampedX;
    double const Dy = (double)AxisThrottle.y - ClampedY;
    double const Dz = (double)AxisThrottle.z - ClampedZ;
    if (Dx * Dx <= ThresholdSq && Dy * Dy <= ThresholdSq && Dz * Dz <= ThresholdSq)
        return;

    AxisThrottle.x = ClampedX;
    AxisThrottle.y = ClampedY;
    AxisThrottle.z = ClampedZ;
    wakeup();
}

void CSteering::SetOwner(CShip* Ship)
{
    OwnerShip = Ship;
}

void CSteering::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // .text:062bb7c0, RET 8. The math engine is acquired lazily (DAT_063fc490); OwnerShip+0x08
    // is the transform; SteeringTorque X/Y/Z are Archetype::Ship +0x114/+0x118/+0x11c.
    (void)CoreList;
    if (!IsControllerEnabled())
        return;
    if (!bActive)
        return;
    // Unrounded; a NaN does not return.
    double const LenSq = ((double)AxisThrottle.x * AxisThrottle.x + (double)AxisThrottle.y * AxisThrottle.y) +
                         (double)AxisThrottle.z * AxisThrottle.z;
    if (LenSq == 0.0)
        return;

    FTransform const& ShipXform = (FTransform const&)(OwnerShip->get_transform());
    I3DMathEngine* Math = Acquire3DMathEngine();
    Vector Local;
    Math->InverseRotate((FVector&)(Local), ShipXform,
                        (FVector const&)(AxisThrottle)); // +0x34

    Archetype::Ship const* ShipArch = OwnerShip->shiparch();
    Vector Scaled;
    Scaled.x = Local.x * ShipArch->SteeringTorqueX;
    Scaled.y = Local.y * ShipArch->SteeringTorqueY;
    Scaled.z = Local.z * ShipArch->SteeringTorqueZ;

    Math = Acquire3DMathEngine();
    Vector Torque;
    Math->Rotate((FVector&)(Torque), ShipXform,
                 (FVector const&)(Scaled)); // +0x30
    rotate(Torque, EventSim->delta_time);
}

// ============================================================
// CNudgeEngine — lightweight controller that applies a nudge vector
// ============================================================

float const CNudgeEngine::NUDGE_CHANGE_THRESHOLD = 9.9999997e-05f; // retail DAT_0639e518 = ~1e-4, not 1e-8

// .text:062ad220: same shape as CSteering's ctor -- OwnerShip (+0x14) is this class's own.
CNudgeEngine::CNudgeEngine()
    : OwnerShip(nullptr)
    , bActive(false)
{
    CurrentNudge.x = 0.0f;
    CurrentNudge.y = 0.0f;
    CurrentNudge.z = 0.0f;
}

// .text:062878f0: base copy, then +0x14 OwnerShip, +0x18 bActive, +0x1c CurrentNudge.
CNudgeEngine::CNudgeEngine(CNudgeEngine const& Other)
    : CPhysControllerEquip(Other)
    , OwnerShip(Other.OwnerShip)
    , bActive(Other.bActive)
    , CurrentNudge(Other.CurrentNudge)
{
}

CNudgeEngine::~CNudgeEngine()
{
}

CNudgeEngine& CNudgeEngine::operator=(CNudgeEngine const& Other)
{
    // .text:06287950 (ICF-folded with CSteering::operator=); no self test.
    CPhysControllerEquip::operator=(Other);
    OwnerShip = Other.OwnerShip;
    bActive = Other.bActive;
    CurrentNudge = Other.CurrentNudge;
    return *this;
}

bool CNudgeEngine::EnableController()
{
    // 0x4d330, shared; see CSteering::EnableController.
    return ControlObject(OwnerShip);
}

bool CNudgeEngine::Activate(bool bOn)
{
    bActive = bOn;
    return bActive;
}

bool CNudgeEngine::IsActive() const
{
    // .text:062877f0: the virtual IsControllerEnabled (vtable +0x24), then bActive.
    return IsControllerEnabled() && bActive;
}

Vector CNudgeEngine::GetNudgeVec() const
{
    return CurrentNudge;
}

void CNudgeEngine::SetNudgeVec(Vector const& Vec)
{
    // .text:062ad250. Same change test as CSteering::SetAxisThrottle.
    float const ThresholdSq = NUDGE_CHANGE_THRESHOLD * NUDGE_CHANGE_THRESHOLD;
    double const DiffX = (double)CurrentNudge.x - Vec.x;
    double const DiffY = (double)CurrentNudge.y - Vec.y;
    double const DiffZ = (double)CurrentNudge.z - Vec.z;
    if (DiffX * DiffX <= ThresholdSq && DiffY * DiffY <= ThresholdSq && DiffZ * DiffZ <= ThresholdSq)
        return;

    // wakeup() comes before CurrentNudge is written, as in retail.
    wakeup();

    // Only an ordered length > 1 normalises, by the unrounded reciprocal.
    double const Len = sqrt(((double)Vec.z * Vec.z + (double)Vec.y * Vec.y) + (double)Vec.x * Vec.x);
    CurrentNudge = Vec;
    if (Len > 1.0)
    {
        double const Inv = 1.0 / Len;
        CurrentNudge.x = (float)(Inv * CurrentNudge.x);
        CurrentNudge.y = (float)(Inv * CurrentNudge.y);
        CurrentNudge.z = (float)(Inv * CurrentNudge.z);
    }
}

void CNudgeEngine::SetOwner(CShip* Ship)
{
    OwnerShip = Ship;
}

void CNudgeEngine::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // .text:062ad340, RET 8. The math engine is acquired lazily (DAT_063fc324); NudgeForce is
    // Archetype::Ship +0x120.
    (void)CoreList;
    if (!IsControllerEnabled())
        return;
    if (!bActive)
        return;
    // The gate is exactly 0.0, not NUDGE_CHANGE_THRESHOLD; a NaN does not return.
    double const LenSq = ((double)CurrentNudge.x * CurrentNudge.x + (double)CurrentNudge.y * CurrentNudge.y) +
                         (double)CurrentNudge.z * CurrentNudge.z;
    if (LenSq == 0.0)
        return;

    I3DMathEngine* Math = Acquire3DMathEngine();
    Vector World;
    Math->Rotate((FVector&)(World),
                 (FTransform const&)(OwnerShip->get_transform()),
                 (FVector const&)(CurrentNudge)); // +0x30

    float const Scale = OwnerShip->shiparch()->NudgeForce;
    Vector Force;
    Force.x = World.x * Scale;
    Force.y = World.y * Scale;
    Force.z = World.z * Scale;
    push(Force, EventSim->delta_time);
}

// ============================================================
// CStrafeEngine — strafe thruster controller
// ============================================================

// .text:062bb920: base (+0x10 = 0), then +0x14 OwnerShip and +0x18 CurrentStrafe zeroed.
CStrafeEngine::CStrafeEngine()
    : OwnerShip(nullptr)
    , CurrentStrafe(StrafeNone)
{
}

// .text:06287a00: base copy, then +0x14 OwnerShip and +0x18 CurrentStrafe.
CStrafeEngine::CStrafeEngine(CStrafeEngine const& Other)
    : CPhysControllerEquip(Other)
    , OwnerShip(Other.OwnerShip)
    , CurrentStrafe(Other.CurrentStrafe)
{
}

CStrafeEngine::~CStrafeEngine()
{
}

CStrafeEngine& CStrafeEngine::operator=(CStrafeEngine const& Other)
{
    // .text:06287a50; no self test.
    CPhysControllerEquip::operator=(Other);
    OwnerShip = Other.OwnerShip;
    CurrentStrafe = Other.CurrentStrafe;
    return *this;
}

bool CStrafeEngine::EnableController()
{
    // 0x4d330, shared; see CSteering::EnableController.
    return ControlObject(OwnerShip);
}

StrafeDir CStrafeEngine::GetStrafe() const
{
    return CurrentStrafe;
}

void CStrafeEngine::SetStrafe(StrafeDir Dir)
{
    // Ghidra @ 0x5b940
    if (Dir != CurrentStrafe)
    {
        CurrentStrafe = Dir;
        if (Dir != StrafeNone)
        {
            wakeup();
        }
    }
}

void CStrafeEngine::SetOwner(CShip* Ship)
{
    OwnerShip = Ship;
}

void CStrafeEngine::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // .text:062bb960, RET 8. StrafePowerUsage is Archetype::Ship +0x128, StrafeForce +0x124;
    // jump table at 062bbae0. An unknown direction returns after the power has been drained.
    (void)CoreList;
    if (CurrentStrafe == StrafeNone)
        return;

    // Cost stays unrounded; a cost above the pool (or unordered) cancels the strafe.
    double const Cost = (double)OwnerShip->shiparch()->StrafePowerUsage * EventSim->delta_time;
    double const Pool = OwnerShip->ThrustPower;
    if (!(Cost <= Pool))
    {
        CurrentStrafe = StrafeNone;
        return;
    }
    OwnerShip->ThrustPower = (float)(Pool - Cost);

    Matrix const& Ori = OwnerShip->Orientation;
    Vector Dir;
    switch (CurrentStrafe)
    {
    case StrafeLeft:
        Dir.x = -Ori.E[0][0];
        Dir.y = -Ori.E[1][0];
        Dir.z = -Ori.E[2][0];
        break;
    case StrafeRight:
        Dir.x = Ori.E[0][0];
        Dir.y = Ori.E[1][0];
        Dir.z = Ori.E[2][0];
        break;
    case StrafeUp:
        Dir.x = Ori.E[0][1];
        Dir.y = Ori.E[1][1];
        Dir.z = Ori.E[2][1];
        break;
    case StrafeDown:
        Dir.x = -Ori.E[0][1];
        Dir.y = -Ori.E[1][1];
        Dir.z = -Ori.E[2][1];
        break;
    default:
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
              "E:\\FL\\Scratch\\Source\\Common\\CStrafeEngine.cpp", 0x4d,
              "No strafe direction!");
        return;
    }

    float const StrafeForce = OwnerShip->shiparch()->StrafeForce;
    Vector Force;
    Force.x = Dir.x * StrafeForce;
    Force.y = Dir.y * StrafeForce;
    Force.z = Dir.z * StrafeForce;
    push(Force, EventSim->delta_time);
}
