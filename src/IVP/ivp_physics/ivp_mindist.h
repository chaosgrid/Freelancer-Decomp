// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "ivp_core.h"
#include "ivp_real_object.h"
#include "ivp_controller.h" // IVP_ListManager is an IVP_Controller_Dependent; IVP_Event_Sim
#include "../ivp_compact_builder/ivp_compact_ledge.h"
#include "../ivp_collision/ivp_collision.h" // IVP_Collision / IVP_Collision_Delegator (mindist bases)
#include "ivp_listener.h" // IVP_Listener_Hull (IVP_Synapse_Real's base)
#include <cstdint>
#include <cstring>

class IVP_Environment;
struct IVP_Friction_Info_For_Core; // declared in full next to IVP_ListManager below
struct IVP_Contact_Point;
struct IVP_Cache_Ledge_Point; // ivp_mindist_minimize.h

// Synapse status (IVP_Synapse_Real::status, +0x1a): which feature of the
// object the synapse currently tracks. Polygon synapses start at 0 and ball
// synapses at 3 (IVP_Ledge_Mindist::init_mindist, 0634def0 via 0634e090);
// the exact-simplex search (0635bdd0 family) moves polygon synapses between
// 0/1/2 as it narrows the closest features.
enum EIVP_Synapse_Type : int16_t
{
    IVP_SYNAPSE_POINT = 0,
    IVP_SYNAPSE_EDGE  = 1,
    IVP_SYNAPSE_POLY  = 2,
    IVP_SYNAPSE_BALL  = 3
};

class IVP_Ledge_Mindist;

// ============================================================================
// IVP_Synapse_Real (library name). One side of a mindist: IVP_Ledge_Mindist
// holds two by value at +0x18 / +0x34 (stride 0x1c), built by the eh vector
// constructor iterator in the mindist base ctor 0634db70 (ctor LAB_0634dd00,
// dtor LAB_0634dc50). It is an IVP_Listener_Hull: 06350050 files the synapse
// as the payload of its object's hull-manager pool, and the hull manager
// dispatches through its vtable 063a55cc:
//   slot 0 06325390 get_type (returns 0)
//   slot 1 0634f9e0 hull_limit_exceeded_event   -> mindist 0634fa40
//   slot 2 0634fa00 hull_manager_is_going_to_be_deleted_event -> delete mindist
//   slot 3 0634f9c0 hull_manager_is_reset       -> mindist 0634fa20
//   slot 4 0634dc60 scalar deleting dtor
// ============================================================================
class IVP_Synapse_Real : public IVP_Listener_Hull
{
public:
    IVP_Synapse_Real* next;         // +0x08: next synapse on l_obj's synapse_list_head / invalid_synapse_list_head
    IVP_Synapse_Real* prev;         // +0x0c
    IVP_Real_Object*  l_obj;        // +0x10
    IVP_Compact_Edge* edge;         // +0x14: tagged, masked `& 0xfffffff0` before use
    int16_t           mindist_offset; // +0x18: (char*)mindist - (char*)this
    int16_t           status;       // +0x1a: EIVP_Synapse_Type

    // LAB_0634dd00: only the vptr store; the fields are filled by
    // init_synapse_real / set_edge (0634def0's callees).
    IVP_Synapse_Real() {}
    // LAB_0634dc50 / LAB_0634dc60 (slot 4).
    virtual ~IVP_Synapse_Real() {}

    IVP_Ledge_Mindist* get_mindist() const
    {
        return reinterpret_cast<IVP_Ledge_Mindist*>(
            reinterpret_cast<char*>(const_cast<IVP_Synapse_Real*>(this)) + mindist_offset);
    }

    // FUN_0634e070
    void init_synapse_real(IVP_Ledge_Mindist* Mindist, IVP_Real_Object* Object)
    {
        mindist_offset = static_cast<int16_t>(reinterpret_cast<char*>(Mindist) - reinterpret_cast<char*>(this));
        l_obj = Object;
    }

    // FUN_0634e090
    void set_edge(IVP_Compact_Edge* Edge, int16_t Status)
    {
        edge = Edge;
        status = Status;
    }

    // FUN_0634ded0: the ledge the (masked) edge's triangle belongs to --
    // tri = edge & ~0xf; tri - ((*(uint32*)tri & 0xfff) + 1) * 16.
    // Defined in ivp_mindist_limiter.cpp.
    const IVP_Compact_Ledge* get_ledge() const;

    // ---- IVP_Listener_Hull (vtable 063a55cc), defined in ivp_mindist_limiter.cpp ----
    // slot 0, 06325390 (shared `XOR EAX,EAX; RET`).
    int32_t get_type() override { return 0; }
    // slot 1, LAB_0634f9e0: get_mindist()->hull_limit_exceeded_event(HullIntrusionValue).
    void hull_limit_exceeded_event(IVP_Hull_Manager* HullManager, float HullIntrusionValue) override;
    // slot 2, LAB_0634fa00: deletes the mindist (slot 4) when its address is non-null.
    void hull_manager_is_going_to_be_deleted_event(IVP_Hull_Manager* HullManager) override;
    // slot 3, LAB_0634f9c0: get_mindist()->hull_manager_is_reset(DeltaTime, CenterDeltaTime).
    void hull_manager_is_reset(float DeltaTime, float CenterDeltaTime) override;
};
static_assert(offsetof(IVP_Synapse_Real, HullPoolSlot) == 0x04, "IVP_Synapse_Real+0x04 -- 06350050 :063500b4");
static_assert(offsetof(IVP_Synapse_Real, next) == 0x08, "IVP_Synapse_Real+0x08 -- 0634fec0 :0634ff06");
static_assert(offsetof(IVP_Synapse_Real, prev) == 0x0C, "IVP_Synapse_Real+0x0c");
static_assert(offsetof(IVP_Synapse_Real, l_obj) == 0x10, "IVP_Synapse_Real+0x10 -- 0634e070 :0634e07e");
static_assert(offsetof(IVP_Synapse_Real, edge) == 0x14, "IVP_Synapse_Real+0x14 -- 0634e090");
static_assert(offsetof(IVP_Synapse_Real, mindist_offset) == 0x18, "IVP_Synapse_Real+0x18 -- 0634e070 :0634e07a");
static_assert(offsetof(IVP_Synapse_Real, status) == 0x1A, "IVP_Synapse_Real+0x1a -- 0634e090");
static_assert(sizeof(IVP_Synapse_Real) == 0x1C, "IVP_Synapse_Real stride -- mindist holds two at +0x18/+0x34");

// A Vector padded to retail's 16-byte per-element slot (IVP_Core uses the same
// stride), still converting to `Vector*` for call sites that take &CoordOS[N].
struct IVP_Mindist_PadVec : public Vector
{
    float _Pad; // unused 4th float lane
};
static_assert(sizeof(IVP_Mindist_PadVec) == 0x10, "IVP_Mindist_PadVec must be a padded-to-16-byte Vector slot");

// ============================================================================
// IVP_Impact_Solver_Long_Term (Ipion name; the IVP_Contact_Point's +0x40
// Owner). Allocated as 0xD8 bytes by FUN_0635a1e0 (FUN_06347380). The
// friction solver (ivp_friction_solver.cpp) uses the same object as its
// per-contact record and overlays some slots with other types, hence the
// unions at +0x58 and +0x60.
// ============================================================================
class alignas(16) IVP_Impact_Solver_Long_Term
{
public:
    Vector             ContactNormalWS;    // +0x00: Normalized contact normal (World Space)
    float              CollDist;           // +0x0C: Minimum distance between objects
    // +0x10: the library's `speed` (relative speed of the contact). The impact
    // solver writes it through IVP_Impact_Solver::FrictionVectorWS (+0x10C);
    // do_impact saves and restores it around the impact.
    Vector             Speed;              // +0x10
    float              ContactRadius;      // +0x1C: Margin / radius
    // +0x20: the shared world-space contact point both CoordOS are derived
    // from. FUN_063599f0 stores it; FUN_0635a1e0 moves it along the normal.
    Vector             SolverContactPointWS; // +0x20..0x2B
    uint8_t            _pad2c[0x04];       // +0x2C..0x2F: unconfirmed
    IVP_Real_Object*   Objects[2];         // +0x30: copied from the contact point's Object1/Object2 (FUN_06358c50)
    // +0x38..0x47: filled by FUN_06358c50 / FUN_06358bb0 (IVP_Contact_Point::PopulateOwner,
    // ivp_mindist_limiter.cpp).
    IVP_Compact_Edge*  Feature0;           // +0x38: tagged IVP_Compact_Edge* copy of Candidate->Feature1; mask
                                            // `&0xfffffff0` before use, as for IVP_Synapse_Real::edge.
    IVP_Compact_Edge*  Feature1;           // +0x3c: mirror of Feature0, Candidate->Feature2.
    IVP_Material*      Materials[2];       // +0x40, +0x44: per-side FUN_06358bb0 output (the object's material,
                                            // or the manager's get_material_by_index for a tagged feature).
    // +0x48 / +0x4C: per-core kinetic energy deltas across the impact system
    // (after - before), written by do_impact (FUN_06358680). No reader found.
    float              EnergyDelta0;       // +0x48
    float              EnergyDelta1;       // +0x4C
    // +0x50: the contact's index in the friction matrix (FUN_06368970), or 0xffff.
    int16_t            IndexInFs;          // +0x50
    int16_t            Status;             // +0x52: impact iteration count, incremented by FUN_063591a0 and passed as DoImpactSystem's ImpactCounter
    uint8_t            StateProcessed;     // +0x54: 0/1 "already processed this pass" gate (FUN_063591a0, set by FUN_063595c0)
    // +0x55: bits 0..1 = (dword +0x54 >> 8) & 3. Apart from whole-dword
    // initialisation, every writer stores 1 (degenerate contact frame,
    // 063598d9 / 06359cfa / 0635a0e2 / 0635a109); every reader tests
    // (bits & 3) == 1 and then removes the contact from its friction system
    // (06368da8, 06356598, 06355c47, 06358f17). Bit 1 is never set.
    uint8_t            FrictionIsBroken;   // +0x55
    uint8_t            _pad56[0x02];       // +0x56..0x57: unconfirmed
    union
    {
        struct
        {
            float      LastImpactTimeStep; // +0x58: DoImpact's TimeStep argument (FUN_063591a0); written by FUN_063595c0 from FUN_06359560
            float      WinnerPriority;     // +0x5C: candidate-selection key (FUN_063591a0); FUN_063595c0 resets it to 0x60ad78ec
        };
        IVP_Friction_Info_For_Core* FrictionInfos[2]; // +0x58/+0x5C: friction use (FUN_06368970)
    };
    union
    {
        // +0x60: the pair's combined elasticity, from
        // material_manager->get_elasticity (FUN_06358c50); fed to FUN_06358c00.
        float          ContactElasticity;
        int32_t        HasNegativePullSince; // +0x60: friction use, copy of IVP_Contact_Point::HasNegativePullSince
    };
    float              GapDistance;        // +0x64: friction use, copy of IVP_Contact_Point::GapDistance
    // +0x68/+0x6C: the contact's scalar effective-mass pair, built by
    // FUN_0635a1e0: Denom = sum over dynamic sides of
    // InvRotInertia . CoordOSCrossNormal^2 + InvMass; Recip = 1.0f / Denom.
    float              EffectiveMassDenomRecip; // +0x68
    float              EffectiveMassDenom;      // +0x6C
    // +0x70/+0x74: each side's Objects[side]->core, or null when that core is
    // kinematic (Flags & 0xc), set by FUN_0635a1e0.
    IVP_Core*          ContactCores[2];    // +0x70, +0x74
    // +0x78 / +0x88: the friction basis, orthonormal with ContactNormalWS.
    // TangentAxis0 = normalize(cross(N, R)) with R = (1,0,0) when N.x*N.x < 0.9f
    // (DAT_063a53fc), else (0,0,1) (FUN_063599f0); TangentAxis1 =
    // cross(N, TangentAxis0) (FUN_0635a1e0). +0x84 / +0x94 are slot padding.
    Vector             TangentAxis0;       // +0x78..0x83
    uint8_t            _pad84[0x04];       // +0x84..0x87
    Vector             TangentAxis1;       // +0x88..0x93
    uint8_t            _pad94[0x04];       // +0x94..0x97
    IVP_Mindist_PadVec CoordOS[2];         // +0x98 / +0xA8: contact point in each object's space (16-byte stride)
    // +0xB8 / +0xC8: per side, cross(CoordOS[side], vimult(core, ContactNormalWS))
    // in that side's object space (FUN_0635a1e0); zeroed for a kinematic side.
    // Ends exactly at the 0xD8 allocation size.
    IVP_Mindist_PadVec CoordOSCrossNormal[2]; // +0xB8 / +0xC8

    // FUN_06358d60 (thiscall on the long-term contact data): Ipion
    // IVP_Impact_Solver_Long_Term::do_impact_long_term. Callers 0635886c, 063592ce.
    void DoImpactLongTerm(IVP_Core* PushedCores[2], float RescueSpeedVal, IVP_Contact_Point* Cp);
};
static_assert(sizeof(IVP_Impact_Solver_Long_Term) >= 0xD8, "IVP_Impact_Solver_Long_Term covers retail's 0xd8-byte allocation (FUN_0635a1e0)");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, SolverContactPointWS) == 0x20, "IVP_Impact_Solver_Long_Term+0x20");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, Feature0) == 0x38, "IVP_Impact_Solver_Long_Term+0x38");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, Feature1) == 0x3C, "IVP_Impact_Solver_Long_Term+0x3C");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, Materials) == 0x40, "IVP_Impact_Solver_Long_Term+0x40");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, LastImpactTimeStep) == 0x58, "IVP_Impact_Solver_Long_Term+0x58");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, TangentAxis0) == 0x78, "IVP_Impact_Solver_Long_Term+0x78");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, TangentAxis1) == 0x88, "IVP_Impact_Solver_Long_Term+0x88");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, CoordOS) == 0x98, "IVP_Impact_Solver_Long_Term+0x98");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, EffectiveMassDenomRecip) == 0x68, "IVP_Impact_Solver_Long_Term+0x68");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, EffectiveMassDenom) == 0x6C, "IVP_Impact_Solver_Long_Term+0x6C");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, ContactCores) == 0x70, "IVP_Impact_Solver_Long_Term+0x70");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, CoordOSCrossNormal) == 0xB8, "IVP_Impact_Solver_Long_Term+0xB8");
// Upper bound: retail allocates exactly 0xD8 bytes (rounded up here to the
// class's 16-byte alignment).
static_assert(sizeof(IVP_Impact_Solver_Long_Term) <= 0xE0, "IVP_Impact_Solver_Long_Term must fit retail's 0xD8-byte allocation (rounded up to this class's own 16-byte alignment)");

// ============================================================================
// IVP_Impact_Solver: the 0x110-byte stack object of do_impact_long_term
// (FUN_06358d60). Working velocities use 16-byte slots (_padNN), as retail
// addresses RotSpeed{i}/Speed{i} via this + i*0x10.
// ============================================================================
class alignas(16) IVP_Impact_Solver
{
public:
    // +0x00: a speed: float((min_vertical_speed_at_collision + RescueSpeedAddon)
    // * 1.2f), written by DoImpactSystem; read by PushContactPoint and
    // FinalizeImpact.
    float             RescueSpeed;            // +0x00
    // +0x04/+0x08: per-core effective masses along the normal (FUN_06358560:
    // core 1 first, a kinematic core takes the other's value * 100000.0f).
    // FUN_06358630 combines them as the doubled reduced mass.
    float             EffectiveMass0;         // +0x04: per-core effective mass, Cores[0]/ContactCoordsOS[0]
    float             EffectiveMass1;         // +0x08: per-core effective mass, Cores[1]/ContactCoordsOS[1]
    // +0x0C: DoImpactSystem's raw second argument, cleared after ten impacts;
    // FinalizeImpact commits both cores immediately when it is zero.
    int32_t           bAllowDeferredCommit;   // +0x0C
    // +0x10..+0x1B: no reader in retail; DoImpactSystem only zeroes +0x18.
    int32_t           Unread_0x10[3];         // +0x10..+0x1B: dead slot, [2] zeroed by DoImpactSystem
    const IVP_U_Matrix* MWorldFCore[2];       // +0x1C, +0x20: Core orientation matrices
    Vector            RotSpeed0;              // +0x24: Core 0 angular velocity
    uint8_t           _pad30[0x04];           // +0x30..0x33: 16-byte slot padding
    Vector            RotSpeed1;              // +0x34: Core 1 angular velocity
    uint8_t           _pad40[0x04];           // +0x40..0x43
    Vector            Speed0;                 // +0x44: Core 0 linear velocity
    uint8_t           _pad50[0x04];           // +0x50..0x53
    Vector            Speed1;                 // +0x54: Core 1 linear velocity
    uint8_t           _pad60[0x04];           // +0x60..0x63
    // +0x64 / +0x74 / +0x84 / +0x94: the deltas StepImpulse2 last applied,
    // read back by UndoLastStepImpulse (FUN_06357340).
    Vector            RotSpeedDelta0;         // +0x64: Cores[0] angular delta
    uint8_t           _pad70[0x04];           // +0x70..+0x73: unconfirmed
    Vector            RotSpeedDelta1;         // +0x74: Cores[1] angular delta
    uint8_t           _pad80[0x04];           // +0x80..0x83
    Vector            SpeedDelta0;            // +0x84: Cores[0] linear delta
    uint8_t           _pad90[0x04];           // +0x90..0x93
    Vector            SpeedDelta1;            // +0x94: Cores[1] linear delta
    uint8_t           _padA0[0x04];           // +0xA0..0xA3
    Vector            RelativePointSpeed;     // +0xA4: contact point speed of core 1 - core 0 (UpdateRelativeSpeed)
    uint8_t           _padB0[0x04];           // +0xB0..0xB3
    Vector            PushDirection;          // +0xB4: Normalized impulse push direction
    uint8_t           _padC0[0x04];           // +0xC0..0xC3
    Vector            FrictionImpulse;        // +0xC4: Accumulated friction impulse
    // +0xD4..+0xE7: read by FUN_06356fb0 / FUN_06357120; no writer found.
    uint8_t           _padD0[0x04];           // +0xD0..+0xD3: unconfirmed
    int32_t           UseFrictionConeBlend;   // +0xD4: nonzero -> UpdateFrictionPushDirection calls ClampPushDirectionToFrictionCone instead of its plain Gram-Schmidt blend
    float             ConeBlendScale;         // +0xD8: FUN_06357120's elliptical-cone secondary-axis scale
    Vector            FrictionReferenceAxis;  // +0xDC: dotted against the tangent in FUN_06357120; name tentative
    uint8_t           _padE8[0x04];           // +0xE8..+0xEB: unconfirmed
    IVP_Core*         Cores[2];               // +0xEC, +0xF0: Participating cores
    Vector*           ContactCoordsOS[2];     // +0xF4, +0xF8: Local contact coordinates
    float             Restitution;            // +0xFC: Coefficient of restitution (0.0 to 1.0)
    // +0x100/+0x104: friction-cone blend coefficients from FUN_06358c00
    // (PushDirection = Tangent*TangentCoeff - Normal*NormalCoeff).
    float             NormalCoeff;            // +0x100
    float             TangentCoeff;           // +0x104
    Vector*           NormalVectorWS;         // +0x108: Pointer to World Space normal
    Vector*           FrictionVectorWS;       // +0x10C: Pointer to World Space friction axis

    void PushContactPoint(const Vector* PushVector, int32_t bNormalDirection);

    // FUN_063578a0: the impact system's entry point, called once per impact
    // from do_impact_long_term (FUN_06358d60), which passes bAllowDeferred = 1.
    void DoImpactSystem(IVP_Core* CoresOut[2], int32_t bAllowDeferred, int32_t ImpactCounter, float RescueSpeedAddon);

    // FUN_06356f30: RelativePointSpeed = relative point velocity from the working
    // velocities.
    void UpdateRelativeSpeed();

    // FUN_06358560: EffectiveMass0/1 along the given normal.
    void PopulateEffectiveMasses(const Vector* NormalWS);

    // FUN_06358630: the reduced-mass impulse needed to stop the normal
    // speed. Returns double: retail leaves it unrounded in ST0.
    double CalcImpulseNeeded2();

    // FUN_06358350: applies ImpulseMagnitude along PushDirection to each
    // dynamic core (full lever arm and rotation), recording the deltas.
    void StepImpulse2(float ImpulseMagnitude);

    // FUN_06357340: undoes the last StepImpulse2.
    void UndoLastStepImpulse();

    // FUN_06356fb0: recomputes the friction-tangent PushDirection.
    void UpdateFrictionPushDirection();

    // FUN_06357120: clamps PushDirection to the elliptical friction cone.
    // Dot = NormalVectorWS . normalize(RelativePointSpeed).
    void ClampPushDirectionToFrictionCone(float Dot);

    // FUN_063582f0: zeroes both cores' RotSpeedChange and SpeedChange.
    void ClearPendingCoreChanges();

    // FUN_063572b0: writes the working velocities into the core and applies
    // the anomaly manager's freeze check (MovementState bits 8..15).
    void CommitCoreVelocityImmediate(int32_t CoreIndex);

    // FUN_06358270: sets SpeedChange/RotSpeedChange to working - current
    // (overwrites, does not accumulate), leaving Speed/RotSpeed alone.
    void CommitCoreVelocityDeferred(int32_t CoreIndex);

    // FUN_06358070: commits both cores immediately, or defers the winning
    // (larger effective-mass) core's update to the main simulation step.
    void FinalizeImpact(IVP_Core* CoresOut[2]);

    // FUN_06359380 (thiscall on the IVP_Impact_Solver, RET 4): the second
    // friction axis of an anisotropic material -- FrictionReferenceAxis (+0xdc),
    // ConeBlendScale (+0xd8) and UseFrictionConeBlend (+0xd4).
    void CalcSecondFriction(IVP_Contact_Point* Cp);
};
static_assert(sizeof(IVP_Impact_Solver) == 0x110, "IVP_Impact_Solver size must be 0x110 (272 bytes), per the class's own top-of-file comment");
static_assert(offsetof(IVP_Impact_Solver, EffectiveMass0) == 0x04, "IVP_Impact_Solver+0x04");
static_assert(offsetof(IVP_Impact_Solver, EffectiveMass1) == 0x08, "IVP_Impact_Solver+0x08");
static_assert(offsetof(IVP_Impact_Solver, bAllowDeferredCommit) == 0x0C, "IVP_Impact_Solver+0x0C");
static_assert(offsetof(IVP_Impact_Solver, Unread_0x10) == 0x10, "IVP_Impact_Solver+0x10");
static_assert(offsetof(IVP_Impact_Solver, MWorldFCore) == 0x1C, "IVP_Impact_Solver+0x1C");
static_assert(offsetof(IVP_Impact_Solver, RotSpeed0) == 0x24, "IVP_Impact_Solver+0x24");
static_assert(offsetof(IVP_Impact_Solver, RotSpeed1) == 0x34, "IVP_Impact_Solver+0x34");
static_assert(offsetof(IVP_Impact_Solver, Speed0) == 0x44, "IVP_Impact_Solver+0x44");
static_assert(offsetof(IVP_Impact_Solver, Speed1) == 0x54, "IVP_Impact_Solver+0x54");
static_assert(offsetof(IVP_Impact_Solver, RotSpeedDelta0) == 0x64, "IVP_Impact_Solver+0x64");
static_assert(offsetof(IVP_Impact_Solver, RotSpeedDelta1) == 0x74, "IVP_Impact_Solver+0x74");
static_assert(offsetof(IVP_Impact_Solver, SpeedDelta0) == 0x84, "IVP_Impact_Solver+0x84");
static_assert(offsetof(IVP_Impact_Solver, SpeedDelta1) == 0x94, "IVP_Impact_Solver+0x94");
static_assert(offsetof(IVP_Impact_Solver, RelativePointSpeed) == 0xA4, "IVP_Impact_Solver+0xA4");
static_assert(offsetof(IVP_Impact_Solver, PushDirection) == 0xB4, "IVP_Impact_Solver+0xB4");
static_assert(offsetof(IVP_Impact_Solver, FrictionImpulse) == 0xC4, "IVP_Impact_Solver+0xC4");
static_assert(offsetof(IVP_Impact_Solver, UseFrictionConeBlend) == 0xD4, "IVP_Impact_Solver+0xD4");
static_assert(offsetof(IVP_Impact_Solver, ConeBlendScale) == 0xD8, "IVP_Impact_Solver+0xD8");
static_assert(offsetof(IVP_Impact_Solver, FrictionReferenceAxis) == 0xDC, "IVP_Impact_Solver+0xDC");
static_assert(offsetof(IVP_Impact_Solver, Cores) == 0xEC, "IVP_Impact_Solver+0xEC");
static_assert(offsetof(IVP_Impact_Solver, ContactCoordsOS) == 0xF4, "IVP_Impact_Solver+0xF4");
static_assert(offsetof(IVP_Impact_Solver, Restitution) == 0xFC, "IVP_Impact_Solver+0xFC");
static_assert(offsetof(IVP_Impact_Solver, NormalCoeff) == 0x100, "IVP_Impact_Solver+0x100");
static_assert(offsetof(IVP_Impact_Solver, TangentCoeff) == 0x104, "IVP_Impact_Solver+0x104");
static_assert(offsetof(IVP_Impact_Solver, NormalVectorWS) == 0x108, "IVP_Impact_Solver+0x108");
static_assert(offsetof(IVP_Impact_Solver, FrictionVectorWS) == 0x10C, "IVP_Impact_Solver+0x10C");

// ---------------------------------------------------------------------------
// Mindists
// ---------------------------------------------------------------------------
// IVP_Ledge_Mindist (the library's IVP_Mindist, 0x80 bytes) tracks one
// candidate ledge/feature pair through its two synapses. IVP_Complex_Mindist
// (IVP_Mindist_Recursive, 0x94 bytes) is also an IVP_Collision_Delegator and
// spawns child mindists for non-convex objects built from several ledges;
// every spawn is capped by PhySys::MAX_SPAWNED_MINDIST_COUNT (DAT_0658ab3c)
// through the delegator chain (IVP_Collision::delegator, +0x08) so one cap
// covers the whole recursive tree. FUN_0634e0b0 (create_exact_mindists) is
// IVP_Create_Exact_Mindists in ivp_collision/ivp_mindist_create.h.
//
// FUN_06356bc0 (IVP_Ledge_Mindist::try_to_generate_managed_friction) reads the mindist only
// until it calls the find-or-create FUN_0635a950; from there on it works on
// the returned contact point (IVP_Contact_Point below), not on the mindist.

// ---------------------------------------------------------------------------
// IVP_Contact_Point (Ipion name): the per-(Object1, Object2) contact record.
// ---------------------------------------------------------------------------
// operator_new(0x74) + ctor FUN_063539d0, found-or-created by FUN_0635a950
// (lookup FUN_0635a900 walks Object1->+0x28's list). Two embedded list nodes,
// one per side, let it be found from either object's list; each node's
// self-relative Delta recovers the record base. Fields whose purpose is not
// established keep `_fieldNN` names.
class IVP_ListManager;

#pragma pack(push, 4)
struct IVP_Contact_Point
{
    // +0x00/+0x04: links on IVP_ListManager::CtxListHead (InsertCtx / RemoveCtx,
    // FUN_0634b9a0 / FUN_0634b8d0); a separate list from Next1/Prev1.
    IVP_Contact_Point* ListManagerListNext; // +0x00
    IVP_Contact_Point* ListManagerListPrev; // +0x04

    // Node #1, on Object1->+0x28's list. The links are NODE pointers (record+0x08
    // or record+0x1C), mapped back to a record via the node's Delta; see
    // IVP_FindExistingPairRecord / the ctor in ivp_mindist_limiter.cpp.
    void* Next1; // +0x08
    void* Prev1; // +0x0C
    IVP_Real_Object* Object1;       // +0x10: one of the mindist's two objects, picked by StateFlags bit 0x100
    int16_t Delta1;                 // +0x14: self-relative link, base(this+0x8) to record base (always -0x8)
    // +0x16: low byte = side 1's synapse status (shape type) at construction,
    // read as a signed byte; the high byte is kept.
    uint16_t Status1;                // +0x16
    IVP_Compact_Edge* Feature1;      // +0x18: tagged IVP_Compact_Edge* copy (side 1's synapse edge)

    // Node #2: mirror of node #1 for Object2.
    void* Next2; // +0x1C
    void* Prev2; // +0x20
    IVP_Real_Object* Object2;       // +0x24
    int16_t Delta2;                  // +0x28: self-relative link (always -0x1c)
    uint16_t Status2;                 // +0x2A: mirror of Status1
    IVP_Compact_Edge* Feature2;       // +0x2C: mirror of Feature1, other side

    // +0x30: 1 / reduced effective mass of the two cores (FUN_063562d0, via
    // IVP_Core::calc_conservative_effective_mass).
    float MassFactor; // +0x30
    // +0x34: a byte. The ctor clears it, FUN_063562d0 sets it to 1; 1 selects
    // the one-direction reaction FUN_063548e0, anything else the 2x2 one
    // FUN_06354120.
    uint8_t SolverMode;          // +0x34
    uint8_t _pad35[0x03];        // +0x35..0x37: never read or written as such
    // +0x38/+0x3C: Ipion span_friction_s[2]. Zeroed at construction;
    // FUN_0635a1e0 integrates the tangential slip into them each pass
    // (-= dot(relative point velocity, TangentAxisN) * dt), and the friction
    // kernels clamp them into the friction cone.
    float SpanFrictionS[2];      // +0x38 / +0x3C
    IVP_Impact_Solver_Long_Term* Owner;           // +0x40: FUN_063595c0 writes its +0x54/+0x58/+0x5c through it
    // +0x44: the contact's friction coefficient, from
    // material_manager->get_friction_factor (FUN_06358c50). Not initialized by
    // the ctor, in retail either. Cone radius = MassFactor * NowFrictionPressure
    // * this * dt^2.
    float FrictionCoefficient;   // +0x44
    // +0x48: zeroed at construction; the friction-cone clamp accumulates
    // (len - cone) * pressure * coefficient into it.
    float FrictionCorrection;    // +0x48
    // +0x4C: 1 / |unnormalised face normal| of side 2, set at construction only
    // when side 2 is a face (status 2); otherwise left uninitialized, as in
    // retail. Scales the point-face hesse normal (FUN_06359850).
    float InvFaceNormalLength;   // +0x4C
    // +0x50: zeroed at construction; FUN_06354120's running tangential work.
    float TangentWork;           // +0x50
    // +0x54: zeroed at construction; the solved friction impulse times
    // i_delta_time (Ipion now_friction_pressure).
    float NowFrictionPressure;   // +0x54
    // +0x58: the surface gap, max(0, centre distance - both objects' +0x94
    // radii), rewritten every narrow-phase pass (FUN_063599f0 / FUN_0635a1e0).
    // Seeded from friction_dist (DAT_0658ab1c) at construction.
    float GapDistance;           // +0x58
    int16_t _const5c;             // +0x5C: always 0x14 (20) at construction, no other writer found
    uint8_t _pad5e[0x02];         // +0x5E..0x5F: unconfirmed
    // +0x60: low byte 1 = test the contact against its feature on the next
    // contact-frame pass (and flag the owner's friction broken when it lies
    // outside); the pass clears the byte. Set to 1 by the ctor, the friction-cone
    // clamp and FUN_0635a1e0's shape-type-3 case. The upper bytes are never used.
    uint32_t RecheckFeatureFlags;  // +0x60
    // +0x64: zeroed at construction. The friction solver's push counter:
    // FUN_063687b0 sets it to -1 / decrements it while the solved impulse is
    // positive, zeroes it on zero, and counts it up (wrapping past 9 to 0)
    // while negative; FUN_06368e60 sorts by it, FUN_06368540 counts the leading
    // negative entries.
    int32_t HasNegativePullSince;  // +0x64
    // +0x68/+0x6C: when this record last ran a contact-frame pass, a copy of
    // the environment's split current_time. Seeded from ObjectA's environment
    // at construction; FUN_0635a1e0 takes dt from it and restamps it.
    float LastRecalcTimeBase;     // +0x68
    float LastRecalcTimeOffset;   // +0x6C
    // +0x70: the IVP_ListManager this record is threaded onto (InsertCtx);
    // zeroed at construction.
    IVP_ListManager* ListManagerOwner; // +0x70

    // ---- friction-impulse methods (ivp_contact_point.cpp) ------------------

    // FUN_06354d70: project ImpulseChange onto Owner's two tangent axes and
    // add the results into SpanFrictionS.
    void AccumulateFrameTangent(const Vector& ImpulseChange);

    // FUN_06353f10: normal-impulse solve for one PSI. EventSim is read only
    // through its i_delta_time (+4).
    void SolveNormalImpulse(const IVP_Event_Sim* EventSim, float Depth, float Gain);

    // FUN_06353e80: world-space friction direction from SpanFrictionS,
    // normalized; returns its length, or ClampTangentDirection's value when
    // SolverMode == 1.
    float RecomputeTangentDirection(Vector* OutDir);

    // FUN_06353b50
    float ClampTangentDirection(Vector* InOutDir);

    // FUN_063539d0: the constructor, called by IVP_FindOrCreatePairRecord
    // (FUN_0635a950) on a fresh operator_new(0x74) block.
    explicit IVP_Contact_Point(IVP_Ledge_Mindist* Mindist);

    // FUN_0635a160: subtract a split time delta from the +0x68 / +0x6c
    // timestamp and renormalize the offset into [0, 1]. Caller:
    // IVP_ListManager::reset_time.
    void ShiftTimeStamp(float DeltaBase, float DeltaOffset);

    // FUN_06355200. IVP_Contact_Point's destructor body: releases both ledges
    // through the surface managers, fires the friction-deleted events and unlinks
    // the record from both objects' mindist_list.
    void UnlinkFromObjects();

    // FUN_0635a1e0: the contact-frame dispatcher of one dist (ivp_mindist_limiter.cpp).
    void UpdateContactFrame();

    // FUN_063562d0. Record->MassFactor = reciprocal two-body reduced mass of
    // the two cores (only the other side's effective mass when one core is
    // kinematic); SolverMode = 1 when either material has
    // SecondFrictionXEnabled (+0x08). Implemented in ivp_mindist_limiter.cpp.
    void ComputeMassFactor();
    // FUN_06358bb0 (RET 4): per side, the object's material or, for a tagged
    // feature, the material manager's (ivp_mindist_limiter.cpp).
    void ResolveSideMaterials(IVP_Material** OutArray);

    // FUN_06358c50 (RET 4): read_materials_for_contact_situation -- fills Owner's
    // objects/features/materials/elasticity and this contact point's friction
    // factor (ivp_mindist_limiter.cpp).
    void PopulateOwner(IVP_Impact_Solver_Long_Term* Owner);

    // FUN_06359560 (thiscall on the contact point, RET 4): Ipion
    // IVP_Contact_Point::get_rescue_speed_impact. Returned on the x87 stack
    // (double here); both callers store it as a float at once.
    double GetRescueSpeedImpact(IVP_Environment* Env);

    // FUN_063595c0 (thiscall on the contact point, no arguments): marks the
    // long-term data processed and recomputes its impact priority (+0x5c).
    void CalcImpactPriority();
    void CalcFrictionEdgeEdge(const IVP_Compact_Edge* K, const IVP_Compact_Edge* L,
                              const IVP_Cache_Ledge_Point* CacheK,
                              const IVP_Cache_Ledge_Point* CacheL,
                              IVP_Impact_Solver_Long_Term* LongTerm, Vector* OutScratch);
};
#pragma pack(pop)
static_assert(sizeof(IVP_Contact_Point) == 0x74, "retail operator_new(0x74), FUN_0635a950/FUN_063539d0");
static_assert(offsetof(IVP_Contact_Point, Next1) == 0x08, "IVP_Contact_Point+0x08");
static_assert(offsetof(IVP_Contact_Point, Prev1) == 0x0C, "IVP_Contact_Point+0x0C");
static_assert(offsetof(IVP_Contact_Point, MassFactor) == 0x30, "IVP_Contact_Point+0x30");
static_assert(offsetof(IVP_Contact_Point, Object1) == 0x10, "IVP_Contact_Point+0x10");
static_assert(offsetof(IVP_Contact_Point, Delta1) == 0x14, "IVP_Contact_Point+0x14");
static_assert(offsetof(IVP_Contact_Point, Feature1) == 0x18, "IVP_Contact_Point+0x18");
static_assert(offsetof(IVP_Contact_Point, Next2) == 0x1C, "IVP_Contact_Point+0x1C");
static_assert(offsetof(IVP_Contact_Point, Prev2) == 0x20, "IVP_Contact_Point+0x20");
static_assert(offsetof(IVP_Contact_Point, Object2) == 0x24, "IVP_Contact_Point+0x24");
static_assert(offsetof(IVP_Contact_Point, Delta2) == 0x28, "IVP_Contact_Point+0x28");
static_assert(offsetof(IVP_Contact_Point, Feature2) == 0x2C, "IVP_Contact_Point+0x2C");
static_assert(offsetof(IVP_Contact_Point, Owner) == 0x40, "IVP_Contact_Point+0x40 -- the resolved Owner field");
static_assert(offsetof(IVP_Contact_Point, ListManagerListNext) == 0x00, "IVP_Contact_Point+0x00");
static_assert(offsetof(IVP_Contact_Point, SolverMode) == 0x34, "IVP_Contact_Point+0x34");
static_assert(offsetof(IVP_Contact_Point, SpanFrictionS) == 0x38, "IVP_Contact_Point+0x38");
static_assert(offsetof(IVP_Contact_Point, FrictionCoefficient) == 0x44, "IVP_Contact_Point+0x44");
static_assert(offsetof(IVP_Contact_Point, FrictionCorrection) == 0x48, "IVP_Contact_Point+0x48");
static_assert(offsetof(IVP_Contact_Point, TangentWork) == 0x50, "IVP_Contact_Point+0x50");
static_assert(offsetof(IVP_Contact_Point, NowFrictionPressure) == 0x54, "IVP_Contact_Point+0x54");
static_assert(offsetof(IVP_Contact_Point, InvFaceNormalLength) == 0x4C, "IVP_Contact_Point+0x4C");
static_assert(offsetof(IVP_Contact_Point, GapDistance) == 0x58, "IVP_Contact_Point+0x58");
static_assert(offsetof(IVP_Contact_Point, _const5c) == 0x5C, "IVP_Contact_Point+0x5C");
static_assert(offsetof(IVP_Contact_Point, RecheckFeatureFlags) == 0x60, "IVP_Contact_Point+0x60");
static_assert(offsetof(IVP_Contact_Point, HasNegativePullSince) == 0x64, "IVP_Contact_Point+0x64");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, IndexInFs) == 0x50, "IVP_Impact_Solver_Long_Term+0x50");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, FrictionIsBroken) == 0x55, "IVP_Impact_Solver_Long_Term+0x55");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, FrictionInfos) == 0x58, "IVP_Impact_Solver_Long_Term+0x58");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, HasNegativePullSince) == 0x60, "IVP_Impact_Solver_Long_Term+0x60");
static_assert(offsetof(IVP_Impact_Solver_Long_Term, GapDistance) == 0x64, "IVP_Impact_Solver_Long_Term+0x64");
static_assert(offsetof(IVP_Contact_Point, LastRecalcTimeBase) == 0x68, "IVP_Contact_Point+0x68");
static_assert(offsetof(IVP_Contact_Point, LastRecalcTimeOffset) == 0x6C, "IVP_Contact_Point+0x6C");
static_assert(offsetof(IVP_Contact_Point, ListManagerOwner) == 0x70, "IVP_Contact_Point+0x70");

// ---------------------------------------------------------------------------
// IVP_ListManager (Ipion's IVP_Friction_System): the friction system, 0x4c
// bytes (operator_new at 06356e27 in FUN_06356bc0, ctor FUN_0634b600).
// ---------------------------------------------------------------------------
// Two different pair records live here:
//   - IVP_Contact_Point (above, keyed by the two IVP_Real_Object*), threaded
//     onto CtxListHead.
//   - IVP_ListManager_CorePairEntry (below, 0x30 bytes, keyed by the two
//     IVP_Core*), found-or-created by FUN_0634b900 for every attached contact
//     point; each holds the contact points attached under its core pair.
//
// Ctor layout notes:
//   +0x00/+0x08/+0x10: three vtables (the system 063a54c8, the static handle
//     063a5500, the energy handle 063a54e4); the destructor FUN_0634b710
//     stamps IVP_Controller's 0639b64c back into all three. The system is an
//     IVP_Controller_Dependent; each handle an IVP_Controller_Independent
//     holding a back pointer at its own +4 (+0x0c / +0x14 here). RegisterCore
//     registers all three on every dynamic core, UnregisterCore removes them.
//   +0x44 low byte: union_find_necessary. The upper 3 bytes and
//     +0x18..+0x1f / +0x40..+0x43 / +0x48..+0x4b are not touched by the ctor.

// A core's per-friction-system record, returned by
// IVP_Core::find_object_hash_entry(Key = system) (06347c40 compares +0x8
// against the key). operator_new(0xc) at 063558fd and in FUN_06356bc0.
// Released by IVP_Core::release_object_hash_entry with `delete`.
struct IVP_Friction_Info_For_Core
{
    IVP_U_Vector<IVP_Contact_Point> FrDists; // +0x00
    IVP_ListManager* FrictionSystem;              // +0x08
};
static_assert(sizeof(IVP_Friction_Info_For_Core) == 0xc, "IVP_Friction_Info_For_Core is 0xc bytes");

#pragma pack(push, 4)
struct IVP_ListManager_CorePairEntry
{
    // +0x00: the contact points attached under this core pair, zeroed by the
    // ctor FUN_06356240, filled by FUN_0634b900.
    IVP_U_Vector<IVP_Contact_Point> AttachedCtx; // +0x00

    uint8_t _pad08[0x10]; // +0x08..0x17: not touched by FUN_06356240; unconfirmed

    // +0x18: the redundant-dist merge countdown: FUN_063551c0 decrements it
    // once per pass and at zero runs FUN_06355050 on this pair and resets it
    // to 5.
    int32_t MergeCacheCountdown = 1; // +0x18
    // +0x1c: the library's last_impact_time; IVP_Mindist_Do_Impact reads and
    // restamps it.
    IVP_Time LastImpactTime = { -1000.0f, 0.0f }; // +0x1c / +0x20
    // +0x24: the per-pair energy budget (a float): scaled by
    // Environment->integrated_energy_damp per tick (IVP_ListManager_CorePairEntry::TickBudget),
    // grown by RecalcAllContactPoints.
    float IntegratedAntiEnergy = 0.0f;

    // +0x28/+0x2c: written by FindOrAttachCorePairEntry right after
    // construction, not by the ctor.
    IVP_Core* CoreA; // +0x28
    IVP_Core* CoreB; // +0x2c

    // FUN_06356240: the initialisers above and nothing else.
    IVP_ListManager_CorePairEntry() {}

    // FUN_06355c10, thiscall on Entry with Manager on the stack (the caller
    // passes its own [EDI+0x20]). For each attached record: FUN_0635a1e0, then
    // IVP_Contact_Point::PopulateOwner; a record whose Owner's packed state is
    // 0x100 under mask 0x300 is removed (FUN_06355490) and not counted. Returns
    // the live count. Implemented in ivp_mindist_limiter.cpp.
    int RefreshAttachedCtxAndCountLive(IVP_ListManager* Manager);

    // FUN_06355cc0. Scales Entry->IntegratedAntiEnergy by Environment->integrated_energy_damp; while the
    // result is non-negative and neither core has an AxisLock (+0x10), spends
    // some through IVP_ListManager_CorePairEntry::ComputeAndApplyContactAxisImpulse, adds the amount spent
    // to the float at IVP_Environment+0x68 and subtracts it from the budget.
    void TickBudget();

    // FUN_063561c0, thiscall on Entry (reads Entry->CoreA/CoreB, +0x28/+0x2c).
    // Builds the scratch, clamps Budget to EnergySum * 0.1f (DAT_063a5464) and,
    // unless EnergySum < 1e-10f (DAT_063a53d8), applies Budget/EnergySum.
    // Returns the amount spent.
    float ComputeAndApplyContactAxisImpulse(float Budget);
};
#pragma pack(pop)
static_assert(sizeof(IVP_ListManager_CorePairEntry) == 0x30, "retail operator_new(0x30), FUN_0634b900/FUN_06356240");
static_assert(offsetof(IVP_ListManager_CorePairEntry, MergeCacheCountdown) == 0x18, "IVP_ListManager_CorePairEntry+0x18");
static_assert(offsetof(IVP_ListManager_CorePairEntry, LastImpactTime) == 0x1C, "IVP_ListManager_CorePairEntry+0x1C");
static_assert(offsetof(IVP_ListManager_CorePairEntry, IntegratedAntiEnergy) == 0x24, "IVP_ListManager_CorePairEntry+0x24");
static_assert(offsetof(IVP_ListManager_CorePairEntry, CoreA) == 0x28, "IVP_ListManager_CorePairEntry+0x28");
static_assert(offsetof(IVP_ListManager_CorePairEntry, CoreB) == 0x2C, "IVP_ListManager_CorePairEntry+0x2C");

// ---------------------------------------------------------------------------
// The friction system's two embedded controller handles (IVP SDK names).
// Both destruct through IVP_Controller's destructor only (slot 6 0634b6b0).
// ---------------------------------------------------------------------------
class IVP_ListManager;

// IVP_ListManager+0x08. vtable .rdata:063a5500:
//   +0x00 063568a0  core_is_going_to_be_deleted_event      (this class)
//   +0x04 0634b6d0  `FLD [063a53d0]=1.0f; RET`              (this class's copy)
//   +0x08 06285a60  IVP_Controller_Independent's shared vector
//   +0x0c 0628f050  IVP_Controller's `RET 0x8`
//   +0x10 063565c0  do_simulation_controller               (this class)
//   +0x14 06325390  `XOR EAX,EAX; RET` -> priority 0        (this class)
//   +0x18 0634b6b0  deleting destructor
class IVP_Friction_Sys_Static : public IVP_Controller_Independent
{
public:
    IVP_ListManager* FrictionSystem; // +0x04 (= IVP_ListManager+0x0c)

    explicit IVP_Friction_Sys_Static(IVP_ListManager* FrictionSystemIn)
        : FrictionSystem(FrictionSystemIn)
    {
    }
    virtual ~IVP_Friction_Sys_Static() {}

    // 063568a0
    virtual void core_is_going_to_be_deleted_event(IVP_Core* Core) override;
    // 0634b6d0
    virtual float get_minimum_simulation_frequency() override { return 1.0f; }
    // 063565c0
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList) override;
    // 06325390
    virtual IVP_CONTROLLER_PRIORITY get_controller_priority() override { return IVP_CP_STATIC_FRICTION; }

    // 06356560: the one-dist case of the static pass -- normal impulse on the
    // system's only dist, then drop it when it is out of range.
    void DoSimulationSingleFriction(IVP_Event_Sim* EventSim);
};
static_assert(sizeof(IVP_Friction_Sys_Static) == 0x8, "static friction handle: vptr + back pointer");

// IVP_ListManager+0x10. vtable .rdata:063a54e4:
//   +0x00 062ed730  IVP_Controller's `RET 4`
//   +0x04 0627cd20  IVP_Controller's 1.0f default
//   +0x08 06285a60  IVP_Controller_Independent's shared vector
//   +0x0c 0628f050  IVP_Controller's `RET 0x8`
//   +0x10 06356510  do_simulation_controller               (this class)
//   +0x14 0634b690  `MOV EAX,0x7d0; RET` -> priority 2000  (this class)
//   +0x18 0634b6b0  deleting destructor
class IVP_Friction_Sys_Energy : public IVP_Controller_Independent
{
public:
    IVP_ListManager* FrictionSystem; // +0x04 (= IVP_ListManager+0x14)

    explicit IVP_Friction_Sys_Energy(IVP_ListManager* FrictionSystemIn)
        : FrictionSystem(FrictionSystemIn)
    {
    }
    virtual ~IVP_Friction_Sys_Energy() {}

    // 06356510
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList) override;
    // 0634b690
    virtual IVP_CONTROLLER_PRIORITY get_controller_priority() override { return IVP_CP_ENERGY_FRICTION; }
};
static_assert(sizeof(IVP_Friction_Sys_Energy) == 0x8, "energy friction handle: vptr + back pointer");

// IVP_ListManager itself (Ipion's IVP_Friction_System). vtable .rdata:063a54c8:
//   +0x00 062ed730  IVP_Controller's `RET 4` (core_is_going_to_be_deleted_event default)
//   +0x04 0634b6d0  `FLD 1.0f; RET`                         (this class's copy)
//   +0x08 06356500  `LEA EAX,[ECX+0x2c]; RET` -> &RegisteredDynamicCores
//                   (ICF-labelled EngineObject::get_position)
//   +0x0c 06355bb0  reset_time(IVP_Time)                     (this class)
//   +0x10 06356660  do_simulation_controller                 (this class)
//   +0x14 0634b6e0  `MOV EAX,0x258; RET` -> priority 600     (this class)
//   +0x18 0634b6f0  deleting destructor (FUN_0634b710 + operator delete)
#pragma pack(push, 4)
class IVP_ListManager : public IVP_Controller_Dependent
{
public:
    // +0x04: the ctor's argument. The friction solver reads its arena (+0xa0)
    // and gravity_length through it.
    IVP_Environment* Environment;     // +0x04
    IVP_Friction_Sys_Static StaticFsHandle; // +0x08 vptr / +0x0c back pointer
    IVP_Friction_Sys_Energy EnergyFsHandle; // +0x10 vptr / +0x14 back pointer
    uint8_t _pad18[0x08];             // +0x18..0x1f: not touched by the ctor; unconfirmed.

    IVP_Contact_Point* CtxListHead; // +0x20: head of the intrusive list through ListManagerListNext/Prev
    IVP_U_Vector<IVP_Core> RegisteredCores;        // +0x24: every core RegisterCore() has seen
    IVP_U_Vector<IVP_Core> RegisteredDynamicCores; // +0x2c: those that were not kinematic at registration
    IVP_U_Vector<IVP_ListManager_CorePairEntry> CorePairEntries; // +0x34: per-(CoreA,CoreB) entries
    uint16_t RegisterCoreCallCount;   // +0x3c: incremented by every RegisterCore() call, both branches
    uint16_t CtxListCount;            // +0x3e: live count of the CtxListHead list
    // +0x40: int16, not touched by the ctor. The friction pass reads it
    // sign-extended as the count of leading CtxListHead entries left out of
    // the friction matrix; FUN_06368d80 writes it (always 0). Ipion's
    // complex_not_necessary_number.
    int16_t ComplexNotNecessaryNumber; // +0x40
    uint8_t _pad42[0x02];             // +0x42..0x43: unconfirmed.
    // +0x44: Ipion's union_find_necessary, an 8-bit bit-field in a dword. The
    // ctor clears it, RemoveDistFromSystem and the core teardown (063464e8)
    // set it to 1, and the friction pass tests and clears it before the
    // union-find split. The upper 24 bits are never written, as in retail.
    uint32_t union_find_necessary : 8; // +0x44
    uint32_t _Unused44 : 24;           // +0x45..0x47: never touched.
    uint8_t _pad48[0x04];             // +0x48..0x4b: not touched by the ctor; unconfirmed.

    // FUN_0634b600
    explicit IVP_ListManager(IVP_Environment* EnvironmentIn);

    // FUN_0634b710 / FUN_0634b6f0 (deleting, slot 6). Tears down the three
    // vectors in reverse order and restamps IVP_Controller's vtable -- the
    // member destruction order of this class. Does not touch the dist list
    // or the core-pair entries.
    virtual ~IVP_ListManager() {}

    // ---- IVP_Controller overrides (vtable .rdata:063a54c8) ----------------
    // 0634b6d0
    virtual float get_minimum_simulation_frequency() override { return 1.0f; }
    // 06356500
    virtual IVP_U_Vector<IVP_Core>* get_associated_controlled_cores() override { return &RegisteredDynamicCores; }
    // 06355bb0: rebases every dist's IVP_Time stamp by Delta.
    virtual void reset_time(IVP_Time Delta) override;
    // 06356660: the friction-impulse pass (ivp_contact_point.cpp).
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList) override;
    // 0634b6e0
    virtual IVP_CONTROLLER_PRIORITY get_controller_priority() override { return IVP_CP_DYNAMIC_FRICTION; }

    // FUN_0634b7d0: re-runs the contact frame of every dist (FUN_0635a1e0) and
    // adds the pressure-weighted gap closure of each core pair, when positive,
    // into that pair's IntegratedAntiEnergy.
    void RecalcAllContactPoints();

    // FUN_0634b9a0: insert at the head of CtxListHead; stamps
    // Node->ListManagerOwner = this.
    void InsertCtx(IVP_Contact_Point* Node);

    // FUN_0634b8d0
    void RemoveCtx(IVP_Contact_Point* Node);

    // FUN_06355770: linear scan for the entry matching {CoreA,CoreB} in either
    // order.
    IVP_ListManager_CorePairEntry* FindCorePairEntry(IVP_Core* CoreA, IVP_Core* CoreB);

    // FUN_06356280: as above, scanning from the last element down; 0 when
    // none. Called by do_impact (06358680).
    IVP_ListManager_CorePairEntry* FindCorePairEntryExact(IVP_Core* KeyA, IVP_Core* KeyB);

    // FUN_0634b900: finds or creates (operator_new(0x30) + FUN_06356240) the
    // entry for Ctx's two cores and attaches Ctx to it.
    void FindOrAttachCorePairEntry(IVP_Contact_Point* Ctx);

    // FUN_0634b9d0: adds Core to RegisteredCores; a dynamic core
    // ((Flags & 0xc) == 0) also goes into RegisteredDynamicCores and gets the
    // three controllers registered (FUN_06349180) in the order static handle,
    // system, energy handle. Always bumps RegisterCoreCallCount.
    void RegisterCore(IVP_Core* Core);

    // FUN_0634ba50: mirror of RegisterCore; the controllers are removed
    // (FUN_06349110) in reverse order.
    void UnregisterCore(IVP_Core* Core);

    // FUN_063555b0: moves Other's contact points and registered cores into
    // `this` (merging the cores' per-system hash entries,
    // IVP_MergeRegisteredCores), then deletes Other.
    void MergeFrom(IVP_ListManager* Other);

    // ------------------------------------------------------------------
    // Friction pass. Defined in ivp_friction_solver.cpp, except
    // RetirePairRecordBookkeeping (ivp_mindist_limiter.cpp).
    // ------------------------------------------------------------------

    // FUN_0634b850: detaches Dist from its (CoreA,CoreB) entry and destroys the
    // entry when it becomes empty. Returns 1 if the entry was destroyed.
    int RetirePairRecordBookkeeping(IVP_Contact_Point* Dist);

    // FUN_06355490: removes Dist from the system and deletes it.
    void RemoveDistFromSystem(IVP_Contact_Point* Dist);

    // FUN_06368540: number of leading CtxListHead entries whose
    // HasNegativePullSince is negative.
    int GetNumSupposedActiveFrDists() const;

    // FUN_06368e20: moves Second in front of First (neighbours, First first).
    void ExchangeFrDists(IVP_Contact_Point* First, IVP_Contact_Point* Second);

    // FUN_06368e60: one insertion-sort pass of CtxListHead by
    // HasNegativePullSince.
    void ReorderMindistsForComplex();

    // FUN_06368d80: drops dists that are out of friction range, moves
    // far-but-shared ones to the list head, then zeroes
    // ComplexNotNecessaryNumber.
    void RecheckFrDistsBeforeSolve();

    // FUN_06368ea0: 1 unless Core sits in more than one CorePairEntries pair
    // whose two cores are both dynamic.
    int CoreIsInAtMostOneDynamicPair(IVP_Core* Core) const;

    // FUN_06368f00: the oversized-matrix fallback.
    void StaticFrOversizedMatrixPanic();

    // FUN_06368f60: the friction pass entry (called by FUN_063565c0 when more
    // than one dist is attached).
    void DoPushesDistanceKeepers(const IVP_Event_Sim* EventSim);

    // FUN_063690b0: kinetic energy of RegisteredCores with their pending
    // speed changes applied.
    double KineticEnergyOfSystem() const;

    // FUN_06369170: commit_speed_changes on every RegisteredDynamicCores entry.
    void ConfirmComplexPushes();

    // FUN_06369190: clear_speed_change on every RegisteredDynamicCores entry.
    void UndoComplexPushes();

    // FUN_063691b0: sum of Mass * 9.81f * 0.1f over the RegisteredCores whose
    // Flags & 0xc is clear.
    double MaxEnergyGain() const;

    // FUN_063557e0. Union-find connectivity check over Manager->RegisteredCores
    // via Manager->CorePairEntries. Returns a core whose master differs from the
    // reference master, or nullptr if all registered cores are one component.
    IVP_Core* FindDisconnectedCore();

    // FUN_06355890. Moves every core whose master is TargetMaster (and any
    // further disconnected group left behind) out of Manager into new
    // IVP_ListManagers (ctor FUN_0634b600); an underfull manager is deleted.
    void SplitOffCoreGroup(IVP_Core* TargetMaster);

    // FUN_06355c80. IVP_ListManager_CorePairEntry::TickBudget on every entry.
    void TickAllCorePairBudgets();

    // FUN_06355ca0. Clears CorePairEntry+0x24 (IntegratedAntiEnergy, the budget) on every entry.
    void ClearAllCorePairBudgets();

    // FUN_06354c10: per-PSI Coulomb friction-cone clamp over every core pair of
    // the system, then the per-dist reaction kernel.
    void ClampFrictionCone(const IVP_Event_Sim* EventSim);

    // FUN_063551c0: age each pair's merge countdown by one PSI; at zero, re-merge
    // its redundant dists (FUN_06355050) and reset the countdown to 5.
    void AgeAndRemergeContactSets();

    // FUN_06355590: the two steps above,
    // run back to back once per PSI.
    void StepFrictionSolver(const IVP_Event_Sim* EventSim);
};
#pragma pack(pop)
static_assert(sizeof(IVP_ListManager) == 0x4c, "retail operator new(0x4c), FUN_06356bc0 (ctor FUN_0634b600)");
static_assert(offsetof(IVP_ListManager, Environment) == 0x04, "IVP_ListManager+0x04");
static_assert(offsetof(IVP_ListManager, ComplexNotNecessaryNumber) == 0x40, "IVP_ListManager+0x40");
static_assert(offsetof(IVP_ListManager, StaticFsHandle) == 0x08, "IVP_ListManager+0x08 static handle (vtable 063a5500)");
static_assert(offsetof(IVP_ListManager, EnergyFsHandle) == 0x10, "IVP_ListManager+0x10 energy handle (vtable 063a54e4)");
static_assert(offsetof(IVP_ListManager, CtxListHead) == 0x20, "IVP_ListManager+0x20");
static_assert(offsetof(IVP_ListManager, RegisteredCores) == 0x24, "IVP_ListManager+0x24");
static_assert(offsetof(IVP_ListManager, RegisteredDynamicCores) == 0x2C, "IVP_ListManager+0x2C");
static_assert(offsetof(IVP_ListManager, CorePairEntries) == 0x34, "IVP_ListManager+0x34");
static_assert(offsetof(IVP_ListManager, RegisterCoreCallCount) == 0x3C, "IVP_ListManager+0x3C");
static_assert(offsetof(IVP_ListManager, CtxListCount) == 0x3E, "IVP_ListManager+0x3E");
static_assert(offsetof(IVP_ListManager, _pad48) == 0x48, "IVP_ListManager+0x44 bit-field dword ends at +0x48");

class IVP_Complex_Mindist;
class IVP_Mindist_Manager; // src/IVP/ivp_physics/ivp_ov_element.h

// ============================================================================
// IVP_Mindist_Base (library name, from Common.dll's exported PhySys
// signatures), vtable 063a55a4: the layer between IVP_Collision and
// IVP_Ledge_Mindist.
//   slot 0 0634dc00 (IVP_Collision's CORE trap) / 1 0634de70 get_objects /
//   2 0634de90 get_ledges / 3 0634dc10 (IVP_Collision) / 4 0634dc80 deleting
//   dtor over 0634dca0.
// IVP_Collision owns +0x04 EventQueueHandle (0xFFFF = not queued), +0x08
// delegator, +0x0c/+0x10 the two IVP_OV_Element back-indices.
// Ctor FUN_0634db70 (arg = delegator), see ivp_mindist_limiter.cpp.
// Dtor FUN_0634dca0: the synapse member destructors and the base's.
// ============================================================================
#pragma pack(push, 4)
class IVP_Mindist_Base : public IVP_Collision
{
public:
    uint32_t StateFlags;                 // +0x14: packed bitfield (kind bits 0xc000/0xc00, winner-recalc bits 0x100000/0x1000)
    // +0x18 / +0x34: the two synapses. Index i is the physical slot:
    // synapse[0] at +0x18 (object +0x28, edge +0x2c, status +0x32), synapse[1]
    // at +0x34 (object +0x44, edge +0x48, status +0x4e). StateFlags bits 8-9
    // pick the sorted order (get_sorted_synapse).
    IVP_Synapse_Real synapse[2];           // +0x18 / +0x34

    // FUN_06352e20: this + 0x18 + (((flags >> 8) & 3) ^ index) * 0x1c. Other
    // callers inline the same arithmetic, the other side with flags ^ 0x100.
    IVP_Synapse_Real* get_sorted_synapse(int32_t Index)
    {
        return &synapse[((StateFlags >> 8) & 3u) ^ static_cast<uint32_t>(Index)];
    }

    float CombinedRadius;                 // +0x50: sum of both objects' contact radii (FUN_0634def0)
    // +0x54..+0x6b: written by the minimize solver's leaves
    // (ivp_mindist_minimize.cpp; simplest: minimize_BB FUN_063536d0). Names
    // describe the math.
    float SeparationGap;                   // +0x54: |Delta| - CombinedRadius; on the near-zero-length path
                                            // (|Delta| <= DAT_063a53d8) retail skips the reciprocal and stores
                                            // |Delta|^2 - CombinedRadius instead.
    float SecondaryNormalDot;              // +0x58: dot(SlotA.Secondary - SlotB.Secondary, SeparatingNormal)
    // +0x5c: how far the two objects may still travel before this mindist
    // stops being valid: the sum over both sides of the hull manager's spread
    // (OuterGradient - InnerGradient) * dt + (OuterValueAtLastEdit -
    // InnerValueAtLastEdit) at the current time (FUN_06350050). Port name.
    float CombinedHullSpread;              // +0x5c..0x5f
    Vector SeparatingNormal;               // +0x60: normalized (SlotA.Position - SlotB.Position), or the raw
                                            // difference on the degenerate path (see SeparationGap).
    uint8_t _pad6c[0x04];                  // +0x6c..0x6f: unconfirmed

    // FUN_0634db70. Defined in ivp_mindist_limiter.cpp.
    explicit IVP_Mindist_Base(IVP_Collision_Delegator* Delegator);

    // FUN_0634dca0 (+ deleting FUN_0634dc80, slot 4).
    ~IVP_Mindist_Base() override {}

    // Slot 1, LAB_0634de70
    void get_objects(IVP_Real_Object* Out[2]) override { Out[0] = synapse[0].l_obj; Out[1] = synapse[1].l_obj; }
    // Slot 2, LAB_0634de90: see GetLedges.
    void get_ledges(const IVP_Compact_Ledge* Out[2]) override { GetLedges(Out); }

    // LAB_0634de90: each synapse's ledge from its tagged edge. Defined in
    // ivp_mindist_limiter.cpp.
    void GetLedges(const IVP_Compact_Ledge* Out[2]) const;
};
#pragma pack(pop)
static_assert(offsetof(IVP_Mindist_Base, StateFlags) == 0x14, "IVP_Mindist_Base+0x14 (0634db70 :0634dbdb)");
static_assert(offsetof(IVP_Mindist_Base, synapse) == 0x18, "IVP_Mindist_Base::synapse[0] at +0x18 (0634db70 :0634dbb1)");
static_assert(offsetof(IVP_Mindist_Base, CombinedRadius) == 0x50, "IVP_Mindist_Base+0x50");
static_assert(offsetof(IVP_Mindist_Base, CombinedHullSpread) == 0x5c, "IVP_Mindist_Base+0x5c (0634db70 :0634dbde)");
static_assert(sizeof(IVP_Mindist_Base) == 0x70, "IVP_Ledge_Mindist's own fields start at +0x70 (0634dd10 :0634dd1f)");

// ============================================================================
// IVP_Ledge_Mindist: the library's IVP_Mindist (vtable 063a55e0), 0x80 bytes.
// IVP_Collision declares slots 0..4, so the C++ slot indices of the virtuals
// below equal retail's (5..8: 06352b10 / 06325390 / 0634e9d0 / 06358cb0).
// ============================================================================
#pragma pack(push, 4)
class IVP_Ledge_Mindist : public IVP_Mindist_Base
{
public:
    // +0x70: per-step stamp: FUN_06352cf0 and FUN_06352b90 return 4 early when
    // it equals ObjectA->environment->sim_step_count (+0x110), else store it.
    // Zeroed by the ctor; the dtor does not touch it.
    uint32_t LastDispatchTick;             // +0x70
    // +0x74/+0x78: this mindist's node on IVP_Mindist_Manager::ActiveMindistHead
    // (+0x08). Only a node with a null +0x78 is the head (unlink at 0634fee4),
    // so +0x78 is prev and +0x74 next.
    IVP_Ledge_Mindist* ManagerNext;        // +0x74
    IVP_Ledge_Mindist* ManagerPrev;        // +0x78
    const IVP_Compact_Edge* LastPretensionEdge; // +0x7c: zeroed by FUN_0634dd10; guards duplicate pretension in recalc_all_exact_wheel_mindist.

    // FUN_0634dd10: IVP_Mindist_Base's ctor FUN_0634db70 first. Defined in
    // ivp_mindist_limiter.cpp.
    IVP_Ledge_Mindist(IVP_Environment* Environment, IVP_Collision_Delegator* Delegator);

    // FUN_0634dd60. Defined in ivp_mindist_limiter.cpp.
    ~IVP_Ledge_Mindist() override;

    // ---- IVP_Collision slots (retail vtable 063a55e0) ----
    // Slot 0, FUN_0634fe60: the argument is never read.
    void simulate_time_event(IVP_Environment* /*Environment*/) override { UpdatePerTick(); }
    // Slots 1 / 2 (get_objects / get_ledges) are IVP_Mindist_Base's, unchanged.

    // FUN_0634fa40, reached through synapse slot 1 (0634f9e0). Defined in
    // ivp_mindist_limiter.cpp.
    void hull_limit_exceeded_event(float HullIntrusionValue);
    // FUN_0634fa20, reached through synapse slot 3 (0634f9c0). The difference
    // stays on the x87 stack, so it is held as double here.
    void hull_manager_is_reset(float DeltaTime, float CenterDeltaTime)
    {
        CombinedHullSpread = (float)(((double)DeltaTime - CenterDeltaTime) + CombinedHullSpread);
    }
    // FUN_0634def0: files the two objects and their start edges into the
    // synapses, sums the objects' extra radii into CombinedRadius and hands
    // the mindist to the environment's mindist manager. Defined in
    // ivp_mindist_limiter.cpp.
    void init_mindist(IVP_Real_Object* Object0, IVP_Real_Object* Object1,
                      IVP_Compact_Edge* Edge0, IVP_Compact_Edge* Edge1);

    // FUN_06352cf0: per-step stamp (+0x70; returns 4 if already done this
    // step), runs IVP_Mindist_Minimize_Solver (P_Finish_Counter 0x14) through
    // mms_function_table, retries after proove_polypoly at most twice, and
    // ends in mindist_rescue_push unless the 0x1000 / 0x100000 kinds are set.
    // Returns the IVP_MRC_TYPE of the last handler. Defined in
    // ivp_mindist_limiter.cpp.
    int32_t DispatchNarrowPhaseSolve();

    // FUN_06352b90: the library's IVP_Mindist::recalc_mindist. As
    // DispatchNarrowPhaseSolve with P_Finish_Counter 0 and without the retry:
    // result 3 runs proove_polypoly once and stops. Callers: 0634eb0a
    // (insert_lazy_mindist), 06362d96, 06345478.
    int32_t recalc_mindist();

    // Slot 5, LAB_06352b10: the library's mindist_rescue_push --
    // obj0->environment->anomaly_manager->inter_penetration(this, obj0, obj1).
    // Called by 06352b90 and 06352cf0. Defined in ivp_mindist_limiter.cpp.
    virtual void mindist_rescue_push();

    // Slot 6 (06325390 `XOR EAX,EAX; RET` here, 063624b0 `MOV EAX,1; RET` in
    // IVP_Complex_Mindist): the library's is_recursive. Called by 06350460 and
    // 0634eb00.
    virtual int32_t is_recursive() { return 0; }

    // Slot 7, LAB_0634e9d0 (IVP_Complex_Mindist's is LAB_06362920). The
    // argument is the mindist manager, `this` of both calls:
    // Manager->DetachMindist(this) (FUN_0634fec0), then
    // Manager->insert_invalid_mindist(this) (FUN_0634e960). Called by
    // IVP_Mindist_Manager::insert_exact_mindist.
    virtual void exact_mindist_went_invalid(IVP_Mindist_Manager* Manager);

    // Slot 8, FUN_06358cb0: the library's IVP_Mindist::do_impact, dispatched
    // by UpdatePerTick; IVP_Complex_Mindist's slot 8 (LAB_06362650) calls this
    // body directly. Defined in ivp_mindist_limiter.cpp.
    virtual void do_impact();

    // FUN_0634fe60 (slot 0): runs DispatchNarrowPhaseSolve(), then (unless
    // the kind bits 0xc000 are set) either defers via FUN_0634f320
    // (IVP_Ledge_Mindist::RescheduleCollisionEvent) when StateFlags' low nibble is set, or
    // compares SeparationGap against a per-shape-pair threshold
    // (DAT_0658aa18[idx] + DAT_0658ab34) to choose between do_impact and the
    // same deferral with another event mode. See ivp_mindist_limiter.cpp.
    void UpdatePerTick();

    // FUN_06356bc0 (thiscall on the dist, RET 0x10): Ipion's
    // IVP_Mindist::try_to_generate_managed_friction. Finds or creates the dist's
    // contact point; a new one is announced to the listeners, put into a friction
    // system (found on either core, merged, or created) and the two cores' sim
    // units are fused. *OutFrictionSystem gets the system, *OutHavingNew 1 for a
    // new contact point. SimUnitNotDestroy is the sim unit that survives the
    // fusion; bCallRecalcSvals re-runs the contact frame and the materials.
    IVP_Contact_Point* try_to_generate_managed_friction(IVP_ListManager** OutFrictionSystem,
                                                        int32_t* OutHavingNew,
                                                        IVP_Simulation_Unit* SimUnitNotDestroy,
                                                        int32_t bCallRecalcSvals);

    // Defined in ivp_mindist_limiter.cpp.
    void RescheduleCollisionEvent(int32_t RearmWhenFar, int32_t EventMode);
};
#pragma pack(pop)
static_assert(offsetof(IVP_Ledge_Mindist, LastDispatchTick) == 0x70, "IVP_Ledge_Mindist+0x70 (0634dd10 :0634dd1f)");
static_assert(sizeof(IVP_Ledge_Mindist) == 0x80,
              "IVP_Ledge_Mindist must be retail's 0x80 bytes: IVP_Complex_Mindist's delegator vptr sits at +0x80");

// The library's IVP_Mindist_Recursive (0x94 bytes, vtable 063a5878). Also an
// IVP_Collision_Delegator (second vptr at +0x80, vtable 063a5868: 06362c00 /
// 06362ee0 / 06362ea0 / 06362ec0) and the delegator of every child mindist
// it spawns. Base order matters: IVP_Ledge_Mindist is exactly 0x80 bytes, so
// the delegator vptr lands at +0x80 and WinnerFlag at +0x84 as in retail.
#pragma pack(push, 4)
class IVP_Complex_Mindist : public IVP_Ledge_Mindist, public IVP_Collision_Delegator
{
public:
    int32_t WinnerFlag;                   // +0x84: 0/1, which of FeatureA/FeatureB "wins" (FUN_06362840/LAB_06362650/LAB_06362920); -1 from the ctor
    // +0x88: the spawned child collisions (memsize +0x88, n_elems +0x8a, elems
    // +0x8c); the inline-buffer test compares elems against this+0x90, which
    // is simply the next field.
    IVP_U_Vector<IVP_Collision> Children;
    // +0x90: spawned_mindist_count (FUN_06362ea0 adds to it, FUN_06362ec0
    // returns it).
    int32_t SpawnedMindistCount;

    // FUN_06362ab0
    IVP_Complex_Mindist(IVP_Environment* Environment, IVP_Collision_Delegator* Delegator);
    ~IVP_Complex_Mindist() override;                                  // FUN_06362b50 (+ FUN_06362b30 wrapper)

    // ---- IVP_Collision_Delegator slots (sub-object vtable 063a5868) ----
    // Slot 0, LAB_06362c00: O(1) removal of a child from Children.
    void collision_is_going_to_be_deleted_event(IVP_Collision* Collision) override;
    // Slot 2, LAB_06362ea0.
    void change_spawned_mindist_count(int32_t Change) override;
    // Slot 3, FUN_06362ec0.
    int32_t get_spawned_mindist_count() override;

    // FUN_06362840: the tagged-pointer (IVP_Compact_Ledge) winner recalc. The
    // argument (the mindist manager) is never read.
    void RecalcWinnerLedgePair(IVP_Mindist_Manager* Manager);
    // LAB_06362650, slot 8: do_impact override, a 2-level switch on the two
    // synapse status words; falls back to IVP_Ledge_Mindist::do_impact.
    // Per-case notes at the definition in ivp_mindist_limiter.cpp.
    void do_impact() override;
    // Slot 5, 062a8920 (`RET`, ICF-shared): the recursive mindist does not push.
    void mindist_rescue_push() override {}

    // Slot 6, 063624b0
    int32_t is_recursive() override { return 1; }

    // LAB_06362920, slot 7. Defined in ivp_mindist_limiter.cpp.
    void exact_mindist_went_invalid(IVP_Mindist_Manager* Manager) override;

    void SpawnOrDeferChild(float Radius); // FUN_06362c70: the capped spawn step; Radius = R0 + R1 of the range manager
    void DeactivateToSimple();         // FUN_06362d60: drop back out of complex tracking
    void RecalcAndMaybeDeactivate();   // FUN_06362d90: recompute + conditionally call DeactivateToSimple

    void ClearChildren();              // FUN_06362a30: teardown helper (also used by the real dtor)
};
#pragma pack(pop)

// FUN_06360c80 (the object-level mindist factory) is
// IVP_Collision_Delegator_Root_Mindist::delegate_collisions_for_object, slot 5
// of vtable 063a5760 (ivp_collision.h / ivp_ov_element.cpp).
