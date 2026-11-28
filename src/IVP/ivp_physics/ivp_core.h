// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_ini.h" // For Vector
#include <cstdint>
#include "../ivp_utility/ivp_math.h"
#include "../ivp_utility/ivp_u_vector.h"

// Forward declarations
class IVP_Environment;
class IVP_Real_Object;
class IVP_Controller;
struct IVP_Friction_Info_For_Core; // ivp_mindist.h
struct IVP_Old_Sync_Rot_Z;         // ivp_impact.h
class IVP_Simulation_Unit;

// Simulation timestamp: a split-precision pair, base seconds plus a fraction in [0,1).
// Both halves are floats (IVP loads them with FLD float, e.g. 06346205). A class, not a
// struct: retail's export ?GetCurrentTime@PhySys@@YA?AVIVP_Time@@I@Z mangles it as `class`.
class IVP_Time
{
public:
    float time_base;
    float time_offset;
};
static_assert(sizeof(IVP_Time) == 0x08, "IVP_Time size");

// The unidentified record IVP_Core_AxisLock::Owner points at. Only the two
// fields FUN_06351700 and FUN_06354120 read are declared; its size is unknown.
struct IVP_Core_AxisLock_Owner
{
    uint8_t           _pad00[0x74]; // +0x00..0x73: not read by the traced sites
    int32_t           AxisIndex;    // +0x74: the locked axis, 0/1/2 (index into RotSpeed and a LockFrame column)
    uint8_t           _pad78[0x14]; // +0x78..0x8b: not read by the traced sites
    IVP_Real_Object** ObjectSlot;   // +0x8c: *ObjectSlot is the opposing object (0635422d)
};
static_assert(offsetof(IVP_Core_AxisLock_Owner, AxisIndex) == 0x74, "IVP_Core_AxisLock_Owner+0x74");
static_assert(offsetof(IVP_Core_AxisLock_Owner, ObjectSlot) == 0x8c, "IVP_Core_AxisLock_Owner+0x8c");

// ============================================================================
// IVP_Core_AxisLock -- PORT-LOCAL NAME for the object IVP_Core holds at
// +0x10 (IVP's car_wheel, per calc_next_psi_solver). Retail's class is not
// identified. IVP_Core's ctor never writes +0x10. FUN_06354120 uses it as
// the base of a 3x4 matrix and writes a world position at +0x48;
// FUN_06351700 reads [[+0x10]+4]+0x74 as a 0/1/2 axis index into the core's
// own RotSpeed, so the core rotates about a single locked axis.
//
// Only the fields the two call sites touch are named; the object continues
// past +0x68, so no sizeof() is asserted.
struct IVP_Core_AxisLock
{
    void*        Field_0x00;      // +0x00: never read by either traced site.
    IVP_Core_AxisLock_Owner* Owner; // +0x04

    IVP_U_Matrix LockFrame;       // +0x08 (0x40 bytes); column [axis] is what both sites read
    IVP_U_Point  ContactPoint;    // +0x48: world-space contact position, written by FUN_06354120
    float        ContactImpulse;  // +0x58: written by FUN_06354120 (0 on the in-cone path)
    IVP_Time     LastUpdateTime;  // +0x5c: copied verbatim from Environment::current_time (+0xF8)
    void*        Field_0x64;      // +0x64: pointer/flag; non-null selects FUN_06354120's scaled branch
};
static_assert(offsetof(IVP_Core_AxisLock, Owner) == 0x04, "IVP_Core_AxisLock+0x04");
static_assert(offsetof(IVP_Core_AxisLock, LockFrame) == 0x08, "IVP_Core_AxisLock+0x08");
static_assert(offsetof(IVP_Core_AxisLock, ContactPoint) == 0x48, "IVP_Core_AxisLock+0x48");
static_assert(offsetof(IVP_Core_AxisLock, ContactImpulse) == 0x58, "IVP_Core_AxisLock+0x58");
static_assert(offsetof(IVP_Core_AxisLock, LastUpdateTime) == 0x5c, "IVP_Core_AxisLock+0x5c");
static_assert(offsetof(IVP_Core_AxisLock, Field_0x64) == 0x64, "IVP_Core_AxisLock+0x64");

// IVP_Core::Flags: Ipion's five 2-bit IVP_BOOL bitfields, in declaration order
// (fast_piling_allowed_flag, physical_unmoveable, is_in_wakeup_vec,
// rot_inertias_are_equal, pinned). The uses that fix the middle three:
// ctor step 5 and the static-core ObjectHashMap (0xC), add_revive_core's queue
// state (0x30), RecomputeInverseInertia's isotropy bit (0xC0). Impacts and
// friction treat a pinned core like an unmoveable one; calc_effective_mass_*
// gives it unit mass.
enum : uint32_t
{
    IVP_CORE_FAST_PILING_ALLOWED      = 0x003u, // ctor step 9 (FlagsLo)
    IVP_CORE_PHYSICAL_UNMOVEABLE      = 0x00Cu, // ctor step 5 (FlagsHi); nonzero: static / kinematic
    IVP_CORE_PHYSICAL_UNMOVEABLE_TRUE = 0x004u, // the field == 1: ObjectHashMap is a hash
    IVP_CORE_IS_IN_WAKEUP_VEC         = 0x030u,
    IVP_CORE_ROT_INERTIAS_ARE_EQUAL   = 0x0C0u,
    IVP_CORE_PINNED                   = 0x300u,
};

// The movement-state byte: the low byte of IVP_Core::MovementState, of IVP_Real_Object::flags and of
// IVP_Simulation_Unit::State (the signed byte < IVP_MT_NOT_SIM means "in the simulation"). Values are
// retail's, names IVP's. classify_motion_state (FUN_063478f0) returns MOVING / SLOW / CALM and a unit
// sleeps only when every core reports CALM; mark_active_and_sync_objects (FUN_06347170) sets MOVING;
// the constructor and the sleep path set NOT_SIM, which revive_sleeping_core undoes.
enum EIVP_MovementState
{
    IVP_MT_MOVING = 1,
    IVP_MT_SLOW = 2,
    IVP_MT_CALM = 3,
    IVP_MT_NOT_SIM = 8,
};

// Complete full retail IVP_Core structure (0x1BC = 444 bytes)
class IVP_Core
{
public:
    // +0x00
    unsigned int Flags;                       // +0x00: Bitfield status & movement flags
    // +0x04: IVP's upper_limit_radius, the radius of the sphere bounding this core.
    // The OV-tree insertion adds it to a collision range and subtracts it back out
    // (0634ec97 / 0634eceb); DAngularLimitFactor = 0.5f / it. Written by SetRadius()
    // (from IVP_Real_Object::init_core_rest_state_and_damping) and grown when objects join a core.
    float UpperLimitRadius;                   // +0x04
    // +0x08: IVP's max_surface_deviation, the largest distance from the centre to the
    // surface: MaxSurfaceRotSpeed = AbsOmega * it (FUN_06351a50).
    float MaxSurfaceDeviation;                // +0x08
    IVP_Environment* Environment;             // +0x0C: Simulation environment

    // +0x10: see IVP_Core_AxisLock. The owning object is Objects[0] (+0x58).
    IVP_Core_AxisLock* AxisLock;              // +0x10
    Vector RotInertia;                       // +0x14: Moment of inertia vector (Ix, Iy, Iz)
    float Mass;                              // +0x20: Mass

    // +0x24
    Vector RotSpeedDamp;                     // +0x24: Angular velocity damping (3 floats)
    float CenterOfMassX;                     // +0x30: Center of mass offset / padding
    Vector InvRotInertia;                    // +0x34: Inverse moment of inertia (1/Ix, 1/Iy, 1/Iz)
    float InvMass;                           // +0x40: Inverse mass (1/Mass)
    float SpeedDamp;                         // +0x44: Linear velocity damping

    // +0x48
    float DAngularLimitFactor;               // +0x48: Angular limit factor (0.5f / UpperLimitRadius)
    // +0x4C: IVP's spin_clipping, a per-axis bound on |RotSpeed| (owned; freed by the
    // destructor, FUN_063477e0). calc_next_PSI_matrix (FUN_06351310) clamps
    // RotSpeed into [-clip, clip] per axis when it is non-null.
    Vector* SpinClipping;

    // +0x50: one inline slot (the destructor's sentinel test compares elems with +0x58).
    IVP_U_Vector_Fixed<IVP_Real_Object, 1> Objects; // +0x50 (12 bytes: 8-byte header + 1 inline slot)
    // +0x5C: IVP_VHash_Store (ivp_vhash.cpp; ctor FUN_063525f0) created lazily
    // (operator_new(0x14) + FUN_063525f0(2)) only for cores with (Flags & 0xC) == 4,
    // and used through find/register/unregister_object_hash_entry (FUN_06347c40 /
    // c80 / d20). The same flag test gates the destructor, so this and the
    // destructor's controller-notify branch never meet. Key = the candidate's +0x8,
    // value = the candidate; the candidate class is not identified (in practice an
    // IVP_Friction_Info_For_Core keyed by its friction system).
    void* ObjectHashMap;                     // +0x5C
    uint32_t MovementState;                  // +0x60: Movement type / state enum
    // +0x64: a pending-impact refcount (signed word): FinalizeImpact (FUN_06358070)
    // adds 1 - TypeCode(Flags) per core, CommitCoreVelocityImmediate (FUN_063572b0)
    // decrements it.
    int16_t PendingImpactCount;              // +0x64
    int16_t Pad66;                           // +0x66: unidentified
    IVP_Time TimeOfLastPsi;                  // +0x68: Timestamp of last simulation step (8 bytes)
    float IDeltaTime;                        // +0x70: Inverse delta time (1.0f / dt)

    // +0x74
    Vector RotSpeedChange;                   // +0x74: Angular acceleration / impulse delta accumulator
    float Pad80;                             // +0x80: Padding
    Vector SpeedChange;                      // +0x84: Linear acceleration / impulse delta accumulator
    float Pad90;                             // +0x90: Padding

    // +0x94 (a "+0x98 pointer" read in the impact code is IVP_Real_Object::core,
    // not RotSpeed.y; see IVP_Impact_Solver_Long_Term in ivp_mindist.h)
    Vector RotSpeed;                         // +0x94: Angular velocity in object space (OS)
    float PadA0;                             // +0xA0: Padding
    Vector Speed;                            // +0xA4: Linear velocity in world space
    float PadB0;                             // +0xB0: Padding

    // +0xB4
    Vector PosWorldFCore;                    // +0xB4: Position in world coordinates
    float PadC0;                             // +0xC0: Padding
    Vector DeltaWorldFCore;                  // +0xC4: Position delta step in world space
    float PadD0;                             // +0xD0: Padding

    // +0xD4
    IVP_U_Quat QWorldFCore;                  // +0xD4: Current orientation quaternion
    IVP_U_Quat QWorldFCoreNext;              // +0xE4: Next orientation quaternion

    // +0xF4
    IVP_U_Matrix MWorldFCore;                // +0xF4: 4x4 Transformation matrix to world space (64 bytes)

    // +0x134
    // +0x134..+0x14C: IVP's rotation_axis_world_space, current_speed, abs_omega and
    // max_surface_rot_speed, written per PSI by IVP_Calc_Next_PSI_Solver.
    Vector RotAxisWS;                        // +0x134: rotation axis in world space ((1, 0, 0) when not rotating)
    float Pad140;                            // +0x140: Padding
    float CurrentSpeed;                      // +0x144: |Speed|
    float AbsOmega;                          // +0x148: absolute angular speed
    float MaxSurfaceRotSpeed;                // +0x14C: AbsOmega * MaxSurfaceDeviation

    // +0x150: one inline slot (the destructor's sentinel test compares elems with +0x158).
    IVP_U_Vector_Fixed<IVP_Controller, 1> Controllers; // +0x150 (12 bytes)
    // +0x15C: the owning IVP_Simulation_Unit; the destructor (FUN_063477e0) passes it to
    // IVP_Simulation_Unit::detach_core (FUN_06348cb0).
    IVP_Simulation_Unit* OwningSimUnit;      // +0x15C

    // +0x160
    IVP_Time TimeOfLastMotion;               // +0x160: Last motion timestamp (8 bytes)
    IVP_Time TimeOfLastLongMotion;           // +0x168: Last long motion timestamp (8 bytes)
    IVP_U_Quat QLastMotion;                  // +0x170: Last motion orientation snapshot (16 bytes)
    IVP_U_Quat QLastLongMotion;              // +0x180: Last long motion orientation snapshot (16 bytes)
    Vector PosLastMotion;                    // +0x190: Last motion position snapshot (12 bytes)
    float Pad19C;                            // +0x19C: Padding
    Vector PosLastLongMotion;                // +0x1A0: Last long motion position snapshot (12 bytes)
    float Pad1AC;                            // +0x1AC: Padding
    // +0x1B0: Union-find "next" pointer for merged cores. When two cores are welded
    // together (e.g. by a fixed constraint), one core's chain of these pointers is
    // relinked to point at the other, so walking this field to its null terminator
    // (see IVP_Core_UnionFindGetFather / get_master_core()) finds the current representative core
    // for the merged group. Null means this core is its own master.
    IVP_Core* MergedCoreWhichReplacedIt;

    // +0x1B4
    // +0x1B4: Ipion's old_sync_info, the IVP_Old_Sync_Rot_Z record (arena memory)
    // recalc_interpolated_frame() saves and restore_fast_sim_snapshot() reads back.
    IVP_Old_Sync_Rot_Z* OldSyncInfo;         // +0x1B4
    // +0x1B8: a stamp of Environment->mindist_event_stamp, the counter do_impact increments
    // (06358d1a). notify_objects_and_cache_env_field() (FUN_06347be0) stores it;
    // recheck_mindists_of_other_cores (FUN_06344f50) re-solves only mindists whose
    // two cores carry different stamps. An int held in a pointer-sized slot.
    uint32_t MindistEventStamp;              // +0x1B8

    // Not a retail function: the port's PhySys (common_physys.cpp,
    // BuildIVP_Real/BuildIVP_Sphere) builds cores with `new IVP_Core()` and fills
    // every field itself. Only the two vector headers are initialised.
    IVP_Core() = default;

    // FUN_06347660 (wrapper) + FUN_06347550: IVP_Core's real constructor. Owner is
    // the IVP_Real_Object being built for.
    //   1. All 0x6F dwords zeroed (memset here: the caller hands over raw storage).
    //   2. Objects re-initialised, Objects.Add(Owner), Environment =
    //      Owner->environment (+0x18).
    //   3. OwningSimUnit = new IVP_Simulation_Unit (0x24 bytes, FUN_06348e10), this
    //      core added to its Cores; the unit's State and MovementState's low byte = 8.
    //   4. register_controller (FUN_06349180) with the environment's standard
    //      gravity controller (IVP_Environment+0x00); then MovementState low byte = 8.
    //   5. Flags = (Flags & ~0xC) | ((FlagsHi & 3) << 2);
    //      IVP_Simulation_Unit_Push_Onto_Manager(Environment->time_manager,
    //      OwningSimUnit) (FUN_06349400).
    //   6. QWorldFCore = *InitialOrientation, normize() (FUN_06350fb0),
    //      QWorldFCoreNext = QWorldFCore.
    //   7. PosWorldFCore = *InitialPosition, DeltaWorldFCore = 0,
    //      QWorldFCore.set_matrix(&MWorldFCore) (rotation block only),
    //      MWorldFCore.vv = *InitialPosition.
    //   8. SetRadius(1000.0f, 1000.0f).
    //   9. Flags = (Flags & ~0x3) | (FlagsLo & 0x3).
    IVP_Core(IVP_Real_Object* Owner, const IVP_U_Quat* InitialOrientation,
             const IVP_U_Point* InitialPosition, uint32_t FlagsHi, uint32_t FlagsLo);

    // FUN_06346870: UpperLimitRadius (+4) and MaxSurfaceDeviation (+8). Callers: the
    // constructor (1000.0f, 1000.0f) and FUN_06345b20.
    void SetRadius(float NewUpperLimitRadius, float NewMaxSurfaceDeviation);

    // Core methods
    void ensure_in_simulation();
    void revive_sleeping_core();

    // FUN_06349180: Controllers.Add(Controller), then
    // IVP_Simulation_Unit::register_core_with_controller(OwningSimUnit, Controller,
    // this) -- the counterpart of detach_controller().
    void register_controller(IVP_Controller* Controller);

    // FUN_06347040: IVP_Core::freeze_simulation_core -- stop
    // movement (IVP_Core_StopMovementWithoutCollisionRecheck), then per object (backward): movement byte = 8, mindist
    // manager RecheckOvElement, bring the hull values to "now" with both gradients zeroed
    // (the hull's edit time is NOT updated), DrainAndShift, release the narrow-phase cache
    // slot when +0x40 is set (FUN_0634d510).
    void freeze_simulation_core();

    // FUN_0634b590: per object (backward) build {environment,
    // object} and fire it through the cluster manager's per-object listeners
    // (FUN_0634af70, slot 3) and the environment's object listeners (FUN_0634cf00, slot 3).
    void fire_event_object_frozen();

    // FUN_0634b5e0: freeze_simulation_core then
    // fire_event_object_frozen. Called per core when a simulation unit goes to sleep
    // (FUN_063495e0 :0634965b).
    void freeze_and_fire_object_frozen();

    // FUN_06346290 (RET 8), the SDK's get_surface_speed: SpeedWsOut = Speed +
    // (MWorldFCore * RotSpeed) x (PositionWs - MWorldFCore.vv). Callers 06343940
    // (core split) and PhySys 0628d0dc. Only x/y/z of SpeedWsOut are written.
    void get_surface_speed(const IVP_U_Point* PositionWs, IVP_U_Point* SpeedWsOut) const;

    // FUN_06349110: Controllers.RemoveElement(Controller), then
    // IVP_Simulation_Unit::detach_core_from_controller_bucket(OwningSimUnit,
    // Controller, this) with no null test of OwningSimUnit (as retail; the known
    // callers FUN_06348f60 / FUN_06349040 only pass cores that have one).
    // Defined in ivp_core.cpp to avoid a circular include with ivp_simulation_unit.h.
    void detach_controller(IVP_Controller* Controller);

    // FUN_063470e0: Sum = current_time.time_offset + time_base (float), split into an
    // ftol integer part and the remainder, written to TimeOfLastMotion and
    // TimeOfLastLongMotion. Retail computes the identical split twice (once per
    // target); the port computes it once.
    void update_time_of_last_motion();

    // FUN_06346e70: IDeltaTime (+0x70) = InvDeltaTime, QWorldFCoreNext = QWorldFCore,
    // Speed / DeltaWorldFCore / the three speed bounds zeroed, RotAxisWS = (1, 0, 0).
    // DeltaTime is never read (kept for the call sites). Callers pass the environment's {delta_time,
    // inv_delta_time} or revive_pass()'s {Alpha, 1/Alpha} (falling back to the
    // environment pair when Alpha is within 1e-10f of 0).
    void synchronize_simulation_time_snapshot(float DeltaTime, float InvDeltaTime);

    // FUN_06347170: MovementState low byte = 1; TimeOfLastPsi = current_time -
    // delta_time (int/frac split); update_time_of_last_motion(); then TimeOfLastPsi
    // = current_time (raw copy, overwriting the first value). Then per object
    // (backward): flags (+0x78) low byte = 1 and recheck_ov_element_as_moving()
    // (FUN_063454e0).
    void mark_active_and_sync_objects();

    // FUN_0634b480 (Ipion revive_for_simulation's per-core step). Returns
    // grow_friction_system()'s result; the caller
    // (IVP_Simulation_Unit::revive_if_sleeping, 06349490) restarts its core scan on 1.
    int revive_pass();

    // FUN_063563d0 (no arguments; Ipion IVP_Core::grow_friction_system): for every
    // exact mindist of every object of this core, try to put it into a friction
    // system; returns 1 when a new contact point was created.
    int grow_friction_system();

    // FUN_063461a0: world-space velocity of a point offset from this core's
    // origin by LeverArmOS (object space), given explicit linear/angular
    // velocity vectors: OutWorldSpeed = MWorldFCore.rows * (AngularVelocityOS x LeverArmOS)
    // + LinearVelocityWS. Generic over the velocity source -- of its 13 call
    // sites only one (FUN_06346be0) passes this core's own Speed/RotSpeed, so
    // both vectors stay explicit parameters rather than being read from `this`.
    void calc_point_speed_ws(const Vector* LeverArmOS, const Vector* LinearVelocityWS,
                              const Vector* AngularVelocityOS, Vector* OutWorldSpeed) const;

    // FUN_06346be0: convenience wrapper using this core's own current Speed/RotSpeed.
    void calc_speed_at_object_position(const Vector* LeverArmOS, Vector* OutWorldSpeed) const;

    // FUN_06346cf0 (RET 0x14). Called twice by IVP_Impact_Solver::PushContactPoint
    // (FUN_06357400), once per dynamic core. The same impulse in two frames:
    // PointImpulseOS (object space) is crossed with LeverArmOS and scaled by
    // InvRotInertia into OutAngularDelta; PointImpulseWS (world space) is scaled
    // by InvMass into OutLinearDelta. Callers pass PointImpulseOS =
    // MWorldFCore.vimult(PointImpulseWS).
    void calc_impulse_deltas(const Vector* LeverArmOS, const Vector* PointImpulseOS,
                              const Vector* PointImpulseWS,
                              Vector* OutLinearDelta, Vector* OutAngularDelta) const;

    // FUN_06346790: the per-core effective mass along a direction, used by
    // IVP_Impact_Solver's FUN_06358560 / FUN_06358630 for EffectiveMass0/1. Not
    // PushContactPoint's TotalEffectiveMassInv sum, though it shares the call
    // chain. Returned unrounded (ST0); the caller rounds.
    double calc_effective_mass_along_direction(const Vector* LeverArmOS,
                                                const Vector* ImpulseDirOS,
                                                const Vector* ImpulseDirWS) const;

    // FUN_063463f0: a conservative effective mass over a lever arm with no
    // direction -- the maximum of the three principal-axis rotational terms:
    //   TermX = (LeverArm.y^2 + LeverArm.z^2) * InvRotInertia.x
    //   TermY = (LeverArm.z^2 + LeverArm.x^2) * InvRotInertia.y
    //   TermZ = (LeverArm.x^2 + LeverArm.y^2) * InvRotInertia.z
    //   return 1.0f / (max(TermX,TermY,TermZ) + ((Flags & 0x300) ? 1.0f : InvMass))
    // Port-local name.
    double calc_conservative_effective_mass(const Vector* LeverArm) const; // returned in ST0 unrounded

    // FUN_06347240, Ipion synchronize_with_rot_z: saves RotSpeed/QWorldFCoreNext into an
    // IVP_Old_Sync_Rot_Z record from the frozen-core arena (OldSyncInfo),
    // moves QWorldFCoreNext/MWorldFCore to the current time and -- gated on
    // `(Flags & 0xC0) == 0` -- refreshes RotSpeed from the PRE-interpolation
    // delta quaternion via asin. Full trace in ivp_core.cpp. Callers:
    // IVP_Ledge_Mindist::do_impact (ivp_mindist_limiter.cpp) and
    // IVP_Impact_System::synchronize_core_if_simulated (ivp_impact.cpp).
    void recalc_interpolated_frame();

    // FUN_06347c40 / FUN_06347c80 / FUN_06347d20: find / register / unregister.
    // With (Flags & 0xC) == 4, ObjectHashMap is an IVP_VHash_Store keyed by each
    // candidate's +0x8; otherwise it holds at most one candidate pointer directly.
    IVP_Friction_Info_For_Core* find_object_hash_entry(unsigned Key) const;
    void register_object_hash_entry(IVP_Friction_Info_For_Core* Candidate);
    void unregister_object_hash_entry(IVP_Friction_Info_For_Core* Candidate);

    // FUN_06347d50: unregister_object_hash_entry(Candidate), then (non-null) clear
    // its IVP_U_Vector (FUN_063449d0) and operator delete it. Every caller
    // (06355490, 063555b0, 06355890) releases a friction system's per-core record,
    // IVP_Friction_Info_For_Core (operator_new(0xc)).
    void release_object_hash_entry(IVP_Friction_Info_For_Core* Candidate);

    // FUN_063474d0: the whole world transform (MWorldFCore / PosWorldFCore /
    // QWorldFCore / QWorldFCoreNext) from LocalCsys concatenated onto the existing
    // MWorldFCore (Old * LocalCsys, IVP_U_Matrix::mmult4). Sole caller
    // FUN_06345b20 (IVP_Real_Object::init_core_rest_state_and_damping, ivp_real_object.cpp).
    void init_world_transform_from_local_csys(const IVP_U_Matrix* LocalCsys);

    // FUN_06347410 (IVP's calc_calc; also ported, on raw offsets, as
    // PhySys_RecomputeInverseInertia in common_physys.cpp): InvRotInertia =
    // 1/RotInertia, InvMass = 1/Mass, Flags 0x40/0x80 from an "isotropic"
    // check of the reciprocals, DAngularLimitFactor = 0.5f/UpperLimitRadius.
    // Retail divides unconditionally (zero gives +/-INF). Called from
    // IVP_Real_Object::init_core_rest_state_and_damping right after RotInertia/Mass are set.
    void RecomputeInverseInertia();

    // ---- the core helpers PhySys calls ----
    // FUN_06347c10 (thiscall, RET 4): Mass = NewMass, RotInertia scaled by NewMass / old Mass,
    // then calc_calc (RecomputeInverseInertia). Reached through IVP_Real_Object 06344d30.
    void set_mass(float NewMass);
    // FUN_06346bb0 (thiscall, RET 4): RotInertia = *Inertia, then calc_calc.
    void set_rotation_inertia(const Vector* Inertia);
    // FUN_06346a00 (thiscall, RET 8): SpeedChange += Direction * (InvMass * Magnitude).
    // PhySys::LinearImpulse.
    void push_core_linear(const Vector* Direction, float Magnitude);
    // FUN_06346c80 (thiscall, RET 8): the world axis taken into core space through
    // MWorldFCore (06366010), scaled by InvRotInertia, RotSpeedChange += it * Magnitude.
    // PhySys::AngularImpulse.
    void push_core_angular_ws(const Vector* AxisWs, float Magnitude);
    // FUN_06346d80 (thiscall, RET 8): the same with a core-space axis.
    // PhySys::AngularImpulseOS.
    void push_core_angular_cs(const Vector* AxisCs, float Magnitude);

    // FUN_06346800: 0.5*(Mass*|LinearVelocity|^2 + dot(RotInertia,
    // AngularVelocityOS squared componentwise)), angular velocity in object space.
    // Sole caller 06369143 passes the predicted velocities (Speed+SpeedChange,
    // RotSpeed+RotSpeedChange).
    double calc_kinetic_energy(const Vector* LinearVelocity, const Vector* AngularVelocityOS) const;

    // FUN_06346890: writes this core's pose at an explicit absolute time
    // (CurrentTimeBase/CurrentTimeOffset, matching Environment::current_time's
    // own 2-dword shape) into a caller-owned *OutMatrix, WITHOUT touching
    // this core's own QWorldFCoreNext/MWorldFCore (unlike
    // recalc_interpolated_frame(), which advances them in place). Falls
    // back to a raw copy of the current MWorldFCore when the core is
    // static (signed MovementState low byte >= 8) or the elapsed-time delta
    // compares equal to 0 or unordered.
    void calc_interpolated_transform(float CurrentTimeBase, float CurrentTimeOffset,
                                      IVP_U_Matrix* OutMatrix) const;

    // FUN_06346a50: per-axis exponential (or linear-approximated, for small deltas)
    // velocity damping -- see ivp_core.cpp for the full exp()/linear
    // branch-selection formula. Scales RotSpeed componentwise by RotDampVec
    // and Speed uniformly by SpeedDampValue.
    void apply_velocity_damping(float DeltaTime, const Vector* RotDampVec, float SpeedDampValue);

    // FUN_06346980: dispatcher for apply_velocity_damping() above: when
    // MovementState low byte >= 2, fudges RotSpeedDamp/SpeedDamp by +0.1f
    // (DAT_063a5464) before damping; otherwise uses them raw.
    void damp_velocities(float DeltaTime);

    // FUN_06346c00, "apply an impulse at a point": calls
    // calc_impulse_deltas() and ACCUMULATES the result into
    // RotSpeedChange/SpeedChange (rather than returning it). See
    // commit_speed_changes() below for the matching flush step.
    void apply_impulse_at_point(const Vector* LeverArmOS, const Vector* PointImpulseOS,
                                 const Vector* PointImpulseWS);

    // FUN_06346dd0: flushes RotSpeedChange/SpeedChange into RotSpeed/Speed, then zeroes
    // both accumulators.
    void commit_speed_changes();

    // FUN_06346960: zeroes RotSpeedChange and
    // SpeedChange without applying them.
    void clear_speed_change();

    // FUN_063473b0: restores RotSpeed and QWorldFCoreNext from the IVP_Old_Sync_Rot_Z
    // record OldSyncInfo (+0x1B4) points at, then clears
    // OldSyncInfo to null WITHOUT freeing it (frozen-core arena memory,
    // written by recalc_interpolated_frame()).
    void restore_fast_sim_snapshot();

    // FUN_06347630: Objects.Clear().
    void clear_objects_vector();

    // FUN_063478f0, the per-PSI motion classifier: compares position/orientation deltas since
    // the last short-term and long-term checkpoints against 4 thresholds,
    // updates PosLastMotion/QLastMotion/TimeOfLastMotion and/or
    // PosLastLongMotion/QLastLongMotion/TimeOfLastLongMotion as needed, and
    // returns a new MovementState low-byte value (1/2/3). Sole caller
    // folds the return value into MovementState's low byte (formula in
    // ivp_core.cpp).
    int classify_motion_state(float CurrentTimeBase, float CurrentTimeOffset);

    // FUN_06347be0: MindistEventStamp = Environment->mindist_event_stamp, then FUN_06344f50
    // on every attached object.
    void notify_objects_and_cache_env_field();

    // FUN_06347d80: no-op if (Flags & 0xC) != 0; otherwise
    // revive_sleeping_core() when MovementState low byte == 8, else
    // ensure_in_simulation().
    void wake_or_ensure_simulation();

    // FUN_06347db0: calls IVP_Real_Object::EnsureInSimulation() on every attached object.
    void ensure_objects_in_simulation();

    // FUN_063464a0: the contact points of a core that has just become fixed (ivp_core.cpp).
    void recalc_contact_points_of_fixed_core();

    // FUN_063465a0: recomputes the core's contact points (ivp_core.cpp).
    void refresh_contact_points();

    // FUN_06346ef0: appends Object to Objects.
    void add_object(IVP_Real_Object* Object);

    // FUN_06346f20: removes Object from Objects; the last one out runs the
    // core's teardown (ivp_core.cpp).
    void remove_object(IVP_Real_Object* Object);
};

static_assert(sizeof(IVP_Core) == 0x1BC, "IVP_Core size must be 0x1BC (444 bytes)");
static_assert(offsetof(IVP_Core, Flags) == 0x00, "IVP_Core+0x00");
static_assert(offsetof(IVP_Core, RotInertia) == 0x14, "IVP_Core+0x14");
static_assert(offsetof(IVP_Core, Mass) == 0x20, "IVP_Core+0x20");
static_assert(offsetof(IVP_Core, RotSpeedDamp) == 0x24, "IVP_Core+0x24");
static_assert(offsetof(IVP_Core, InvRotInertia) == 0x34, "IVP_Core+0x34");
static_assert(offsetof(IVP_Core, InvMass) == 0x40, "IVP_Core+0x40");
static_assert(offsetof(IVP_Core, SpeedDamp) == 0x44, "IVP_Core+0x44");
static_assert(offsetof(IVP_Core, Objects) == 0x50, "IVP_Core+0x50");
static_assert(offsetof(IVP_Core, ObjectHashMap) == 0x5C, "IVP_Core+0x5C");
static_assert(offsetof(IVP_Core, MovementState) == 0x60, "IVP_Core+0x60");
static_assert(offsetof(IVP_Core, RotSpeed) == 0x94, "IVP_Core+0x94");
static_assert(offsetof(IVP_Core, Speed) == 0xA4, "IVP_Core+0xA4");
static_assert(offsetof(IVP_Core, PosWorldFCore) == 0xB4, "IVP_Core+0xB4");
static_assert(offsetof(IVP_Core, DeltaWorldFCore) == 0xC4, "IVP_Core+0xC4");
static_assert(offsetof(IVP_Core, QWorldFCore) == 0xD4, "IVP_Core+0xD4");
static_assert(offsetof(IVP_Core, QWorldFCoreNext) == 0xE4, "IVP_Core+0xE4");
static_assert(offsetof(IVP_Core, MWorldFCore) == 0xF4, "IVP_Core+0xF4");
static_assert(offsetof(IVP_Core, CurrentSpeed) == 0x144, "IVP_Core+0x144");
static_assert(offsetof(IVP_Core, MaxSurfaceRotSpeed) == 0x14C, "IVP_Core+0x14C");
static_assert(offsetof(IVP_Core, Controllers) == 0x150, "IVP_Core+0x150");
static_assert(offsetof(IVP_Core, OwningSimUnit) == 0x15C, "IVP_Core+0x15C");
static_assert(offsetof(IVP_Core, MindistEventStamp) == 0x1B8, "IVP_Core+0x1B8");

// IVP_Core_UnionFindGetFather: IVP_Core::get_master_core() (defined in
// ivp_core.cpp; also used by ivp_simulation_unit.cpp).
IVP_Core* __fastcall IVP_Core_UnionFindGetFather(IVP_Core* Core);

// FUN_0634d510 (cdecl): release an object's narrow-phase cache slot -- if +0x40 is set,
// clear the slot's owner (+0x08) and +0x40.
void IVP_Real_Object_Free_Cache_Object(IVP_Real_Object* Object);

// FUN_06351700 / FUN_06351a50 / FUN_06351b90 (and the rest of 06351310-0635212f)
// are IVP_Calc_Next_PSI_Solver methods: ivp_calc_next_psi_solver.h.
