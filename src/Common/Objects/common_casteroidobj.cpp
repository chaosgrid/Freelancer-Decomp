// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CAsteroid and CDynamicAsteroid (the asteroid objects; cubes are in common_casteroid.cpp,
// fields in common_casteroidfield.cpp).
#include "common_cobject.h"

#include "common_physys.h"

#include <string.h>

// ============================================================================
// CAsteroid implementation
// ============================================================================

CAsteroid::CAsteroid()
    : CSimple(ClassAsteroid)
{
    // FUN_062a2840. Deviation: retail leaves bMine/bInstantKill/OpaqueE4 for init(), which
    // always follows; the port zero-initialises them.
    memset(OpaqueE4, 0, sizeof(OpaqueE4));
    AsteroidId = 0;
    CubeId = 0;
    bMine = false;
    bInstantKill = false;
    CollisionGroupId = 0;
    OwnerField = nullptr;
}

CAsteroid::CAsteroid(CAsteroid const& Other)
    : CSimple(Other)
{
    // FUN_062a22f0
    memcpy(OpaqueE4, Other.OpaqueE4, sizeof(OpaqueE4));
    AsteroidId = Other.AsteroidId;
    CubeId = Other.CubeId;
    bMine = Other.bMine;
    bInstantKill = Other.bInstantKill;
    CollisionGroupId = Other.CollisionGroupId;
    OwnerField = Other.OwnerField;
}

CAsteroid::~CAsteroid()
{
    // FUN_062a2880. ~CSimple makes the same call again.
    PhySys::SetCollisionState(this, false);
}

void CAsteroid::init(CreateParms const& Parms)
{
    // FUN_062a28f0. Parms +0x5c/+0x60/+0x64/+0x65/+0x68 into +0xe8/+0xec/+0xf0/+0xf1/+0xf8.
    CSimple::init(Parms);
    AsteroidId = Parms.AsteroidId;
    CubeId = Parms.CubeId;
    bMine = Parms.bMine;
    bInstantKill = Parms.bInstantKill;
    OwnerField = Parms.OwnerField;
}

void CAsteroid::init_physics(Vector const& Velocity, Vector const& AngularVelocity)
{
    // FUN_062a2930. Retail's dead first pass of default dampings is skipped. Velocity and
    // AngularVelocity are never applied, as in retail.
    PhySys::CreateParms Phy;
    memset(&Phy, 0, sizeof(Phy));
    Phy.Mass = 1000.0f;
    Phy.LinearDamping = 1000000.0f;
    Phy.AngularDamping[0] = 1000000.0f;
    Phy.AngularDamping[1] = 1000000.0f;
    Phy.AngularDamping[2] = 1000000.0f;
    Phy.CollisionGroupId = CollisionGroupId;
    Phy.Type = 2;
    Phy.bFlag1c = 1;
    Phy.bEnableCollisions = 1;
    // Retail's sprintf of "Asteroid" into the 8-byte Name writes its NUL into bHasInertia
    // (+0x26), which is zeroed next anyway; 8 raw bytes give the same content without UB.
    memcpy(Phy.Name, "Asteroid", 8);
    Phy.bHasInertia = 0;
    Phy.Inertia.x = 0.0f;
    Phy.Inertia.y = 0.0f;
    Phy.Inertia.z = 0.0f;
    PhySys::BuildIVP_Real(this, Phy);
}

int CAsteroid::update(float Dt, unsigned Flags)
{
    // FUN_062a28e0, ICF-folded with CDynamicAsteroid::update.
    return CObject::update(Dt, Flags);
}

Vector CAsteroid::get_velocity() const
{
    // FUN_063623d0, ICF-folded with CSolar::get_velocity.
    Vector Zero;
    Zero.x = 0.0f;
    Zero.y = 0.0f;
    Zero.z = 0.0f;
    return Zero;
}

Archetype::Asteroid const* CAsteroid::asteroidarch() const
{
    // FUN_06262d50 (ICF-folded with the other per-class arch accessors), +0x88.
    return (Archetype::Asteroid const*)Archetype;
}

unsigned long CAsteroid::get_asteroid_id()
{
    // FUN_062a22b0
    return AsteroidId;
}

bool CAsteroid::is_mine()
{
    // FUN_062a22c0
    return bMine;
}

bool CAsteroid::is_instant_kill()
{
    // FUN_062a22d0
    return bInstantKill;
}

CmnAsteroid::CAsteroidField const* CAsteroid::get_owner_field()
{
    // FUN_062a22e0
    return OwnerField;
}

void CAsteroid::set_system(unsigned int SystemId)
{
    // FUN_0629d4d0 (+0xf4)
    CollisionGroupId = SystemId;
}

// ============================================================================
// CDynamicAsteroid implementation
// ============================================================================

CDynamicAsteroid::CDynamicAsteroid()
    : CObject(ClassDynamicAsteroid)
{
    // FUN_062a29f0
}

CDynamicAsteroid::CDynamicAsteroid(CDynamicAsteroid const& Other)
    : CObject(Other)
{
    // FUN_062a25f0
}

CDynamicAsteroid::~CDynamicAsteroid()
{
    // FUN_062a2a10. CObject::~CObject does not call SetCollisionState (unlike ~CSimple).
    PhySys::SetCollisionState(this, false);
}

void CDynamicAsteroid::init(CreateParms const& Parms)
{
    // FUN_062a2a70. Forwards straight to the virtual init_physics (vtable +0x80, the last
    // slot); CDynamicAsteroid is not a CSimple.
    init_physics(Parms.Velocity, Parms.AngularVelocity, Parms.CollisionGroupId);
}

void CDynamicAsteroid::init_physics(Vector const& Velocity, Vector const& AngularVelocity, unsigned CollisionGroupId)
{
    // FUN_062a2a90. As CAsteroid::init_physics: dead first pass skipped, velocities never applied.
    PhySys::CreateParms Phy;
    memset(&Phy, 0, sizeof(Phy));
    Phy.Mass = 1.0f;
    Phy.LinearDamping = 0.00001f;
    Phy.AngularDamping[0] = 0.0f;
    Phy.AngularDamping[1] = 0.0f;
    Phy.AngularDamping[2] = 0.0f;
    Phy.CollisionGroupId = CollisionGroupId;
    Phy.Type = 2;
    Phy.bFlag1c = 0;
    Phy.bEnableCollisions = 1;
    strcpy(Phy.Name, "DynAst");
    Phy.bHasInertia = 0;
    Phy.Inertia.x = 0.0f;
    Phy.Inertia.y = 0.0f;
    Phy.Inertia.z = 0.0f;
    PhySys::BuildIVP_Real(this, Phy);
}

int CDynamicAsteroid::update(float Dt, unsigned Flags)
{
    // vtable slot at 0639dc8c, ICF-folded with CAsteroid::update.
    return CObject::update(Dt, Flags);
}

Archetype::DynamicAsteroid const* CDynamicAsteroid::dynamicAsteroidArch() const
{
    // FUN_06262d50 (ICF fold)
    return (Archetype::DynamicAsteroid const*)Archetype;
}
