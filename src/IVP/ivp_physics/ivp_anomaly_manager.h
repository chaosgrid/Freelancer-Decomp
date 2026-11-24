// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_ini.h" // For Vector
#include <cstddef>
#include <cstdint>

class IVP_Core;
class IVP_Environment;
class IVP_Real_Object;
class IVP_Ledge_Mindist;

// ============================================================================
// IVP_Anomaly_Manager / IVP_Anomaly_Limits: the environment's limit-violation
// policy and its limits. The IVP_Environment ctor (FUN_0634bf50) creates both
// with ctor(1) and stores them at Environment+0x20 (manager) / +0x24
// (limits). The ctor argument lands at +0x04 and is a "delete with the
// environment" flag tested by EnvironmentWillBeDeleted.
// ============================================================================

// ----------------------------------------------------------------------------
// IVP_Anomaly_Limits -- ctor FUN_06360e30, vtable 063a577c (2 slots).
// Field roles come from their consumers:
//   +0x08 max velocity: manager slot 0 rescales to 0.99f * it; DoImpactSystem
//         compares |speed|^2 against its square (06357d81).
//   +0x0c max collisions per PSI (an int, 70000): compared against
//         IVP_Core::PendingImpactCount by CommitCoreVelocityImmediate (0635730a).
//   +0x10 max angular velocity per PSI: times inv_delta_time (+0xB0) in manager
//         slot 1 and in DoImpactSystem's tail (06357d34).
// slot 0 LAB_06360e90: delete this when the flag is set (the Environment*
// argument is ignored); slot 1 FUN_06360e60: scalar deleting dtor.
// ----------------------------------------------------------------------------
class IVP_Anomaly_Limits
{
public:
    explicit IVP_Anomaly_Limits(int32_t bDeleteWithEnvironment);

    // vtable slot 0 (LAB_06360e90).
    virtual void EnvironmentWillBeDeleted(IVP_Environment* Env);
    // vtable slot 1 (FUN_06360e60 / FUN_06360e80).
    virtual ~IVP_Anomaly_Limits();

    int32_t bDeleteOnEnvironmentDelete; // +0x04: ctor argument, 1 for the Environment-owned singleton
    float   MaxVelocity;                // +0x08: 2000.0f
    int32_t MaxCollisionsPerPsi;        // +0x0C: 70000
    float   MaxAngularVelocityPerPsi;   // +0x10: 1.5707964f (pi/2)
};

static_assert(sizeof(IVP_Anomaly_Limits) == 0x14, "IVP_Anomaly_Limits size must be 0x14 (operator_new(0x14) in FUN_0634bf50, 0634c142)");
static_assert(offsetof(IVP_Anomaly_Limits, bDeleteOnEnvironmentDelete) == 0x04, "IVP_Anomaly_Limits+0x04");
static_assert(offsetof(IVP_Anomaly_Limits, MaxVelocity) == 0x08, "IVP_Anomaly_Limits+0x08");
static_assert(offsetof(IVP_Anomaly_Limits, MaxCollisionsPerPsi) == 0x0C, "IVP_Anomaly_Limits+0x0C");
static_assert(offsetof(IVP_Anomaly_Limits, MaxAngularVelocityPerPsi) == 0x10, "IVP_Anomaly_Limits+0x10");

// ----------------------------------------------------------------------------
// IVP_Anomaly_Manager -- ctor FUN_06360eb0, vtable 063a5784 (7 slots).
// Every caller reaches it as Core->Environment->+0x20 with the limits at +0x24.
// ----------------------------------------------------------------------------
class IVP_Anomaly_Manager
{
public:
    explicit IVP_Anomaly_Manager(int32_t bDeleteWithEnvironment);

    // slot 0, LAB_06360f20 (RET 0xc): rescales *Speed to Limits->MaxVelocity *
    // 0.99f (DAT_063a57a0). `this` and `Core` are unused by the base body.
    virtual void MaxVelocityExceeded(IVP_Anomaly_Limits* Limits, IVP_Core* Core, Vector* Speed);

    // slot 1, LAB_06360f70 (RET 0xc): the same with the bound
    // Core->Environment->inv_delta_time * Limits->MaxAngularVelocityPerPsi *
    // 0.9f (DAT_063a53fc).
    virtual void MaxAngularVelocityExceeded(IVP_Anomaly_Limits* Limits, IVP_Core* Core, Vector* RotSpeed);

    // slot 2, LAB_06361130 (RET 0xc): the library's
    // inter_penetration(mindist, object0, object1). Its caller is
    // IVP_Ledge_Mindist::mindist_rescue_push (06352b10), which passes (this
    // mindist, synapse[0] object, synapse[1] object). When neither core is fixed
    // (core flags & 0xc), or the fixed side's synapse is not a vertex / edge / face,
    // both objects are pushed apart (solve_inter_penetration_simple); otherwise the
    // free object is pushed out along the face normal of the fixed object's ledge
    // that is farthest in front of it. Body in ivp_environment.cpp.
    virtual void inter_penetration(IVP_Ledge_Mindist* Mindist, IVP_Real_Object* Object0, IVP_Real_Object* Object1);

    // FUN_06360fd0 (thiscall, RET 8): pushes the two
    // objects apart along the line between their cores. Non-virtual; its only
    // callers are inter_penetration's two early exits (06361184 / 063611b5).
    // Library name solve_inter_penetration_simple.
    void solve_inter_penetration_simple(IVP_Real_Object* Object0, IVP_Real_Object* Object1);

    // slot 3, LAB_06361490 (RET 8): returns 1 (always allows the freeze).
    // CommitCoreVelocityImmediate calls it when a core's PendingImpactCount
    // exceeds Limits->MaxCollisionsPerPsi (0635731d).
    virtual int32_t MaxCollisionsExceededCheckFreezing(IVP_Anomaly_Limits* Limits, IVP_Core* Core);

    // slot 4, LAB_06360f00 (RET 4): as IVP_Anomaly_Limits' slot 0, through
    // this class's destructor slot. Called by ~IVP_Environment.
    virtual void EnvironmentWillBeDeleted(IVP_Environment* Env);

    // slot 5, LAB_06361460 (RET 8), the library's get_push_speed_penetration:
    // 2 * |gravity| * delta_time of Object0's environment (+0xb4 / +0xac).
    // Returned as double: 06360fd0 rounds it to float, 06361130 multiplies the
    // unrounded value.
    virtual double get_push_speed_penetration(IVP_Real_Object* Object0, IVP_Real_Object* Object1);

    // slot 6, FUN_06360ed0 / FUN_06360ef0.
    virtual ~IVP_Anomaly_Manager();

    int32_t bDeleteOnEnvironmentDelete; // +0x04: ctor argument, 1 for the Environment-owned singleton
};

static_assert(sizeof(IVP_Anomaly_Manager) == 0x08, "IVP_Anomaly_Manager size must be 0x8 (operator_new(0x8) in FUN_0634bf50, 0634c106)");
static_assert(offsetof(IVP_Anomaly_Manager, bDeleteOnEnvironmentDelete) == 0x04, "IVP_Anomaly_Manager+0x04");
