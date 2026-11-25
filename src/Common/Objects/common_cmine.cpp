// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CMine and CCounterMeasure.
#include "common_cobject.h"
#include "Objects/common_cobject_internal.h" // CmnCobject::RotateWorldToObjectSpace, GetObjectDbSystemId

#include "common_physys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using CmnCobject::GetObjectDbSystemId;
using CmnCobject::RotateWorldToObjectSpace;

// ============================================================================
// CMine implementation
// ============================================================================

CMine::CMine(Class ObjectClass)
    : CProjectile(ObjectClass)
{
}

CMine::~CMine()
{
}

void CMine::init_physics(Vector const& Velocity, Vector const& AngularVelocity)
{
    // FUN_062aca30. Name is _itoa(OwnerId, radix 24). No null test on the archetype and no
    // zero test on the mass.
    PhySys::CreateParms Phy;
    memset(&Phy, 0, sizeof(Phy));
    Phy.AngularDamping.x = PhySys::DEFAULT_ANGULAR_DAMPING.x;
    Phy.AngularDamping.y = PhySys::DEFAULT_ANGULAR_DAMPING.y;
    Phy.AngularDamping.z = PhySys::DEFAULT_ANGULAR_DAMPING.z;
    Phy.Type = PhySys::ANIM_TECHNIQUE_MERGED;
    Phy.bEnableCollisions = 1;
    _itoa((int)OwnerId, Phy.Name, 0x18);

    Archetype::Mine const* const Arch = minearch();
    Phy.Mass = Arch->Mass;
    Phy.LinearDamping = Arch->LinearDrag / Arch->Mass;
    Phy.SystemId = GetObjectDbSystemId(ObjectDb);
    PhySys::BuildIVP_Real(this, Phy);
    PhySys::AddToVelocity(this, Velocity);
    // World-space AngularVelocity rotated into object space.
    PhySys::AddToAngularVelocityOS(this, RotateWorldToObjectSpace(Orientation, AngularVelocity));
}

int CMine::update(float Dt, unsigned Flags)
{
    // FUN_062aca00
    return CProjectile::update(Dt, Flags);
}

void CMine::expire_safe_time()
{
    // FUN_062aca10
    PhySys::SetCollisionGroup(this, "");
}

Archetype::Mine const* CMine::minearch()
{
    // FUN_06262d50 (ICF fold)
    return (Archetype::Mine const*)Archetype;
}

// ============================================================================
// CCounterMeasure implementation
// ============================================================================

CCounterMeasure::CCounterMeasure(Class ObjectClass)
    : CProjectile(ObjectClass)
{
}

CCounterMeasure::~CCounterMeasure()
{
}

void CCounterMeasure::init_physics(Vector const& Velocity, Vector const& AngularVelocity)
{
    // FUN_062a85a0. The same body as CMine::init_physics.
    PhySys::CreateParms Phy;
    memset(&Phy, 0, sizeof(Phy));
    Phy.AngularDamping.x = PhySys::DEFAULT_ANGULAR_DAMPING.x;
    Phy.AngularDamping.y = PhySys::DEFAULT_ANGULAR_DAMPING.y;
    Phy.AngularDamping.z = PhySys::DEFAULT_ANGULAR_DAMPING.z;
    Phy.Type = PhySys::ANIM_TECHNIQUE_MERGED;
    Phy.bEnableCollisions = 1;
    _itoa((int)OwnerId, Phy.Name, 0x18);

    Archetype::CounterMeasure const* const Arch = countermeasure_arch();
    Phy.Mass = Arch->Mass;
    Phy.LinearDamping = Arch->LinearDrag / Arch->Mass;
    Phy.SystemId = GetObjectDbSystemId(ObjectDb);
    PhySys::BuildIVP_Real(this, Phy);
    PhySys::AddToVelocity(this, Velocity);
    PhySys::AddToAngularVelocityOS(this, RotateWorldToObjectSpace(Orientation, AngularVelocity));
}

int CCounterMeasure::update(float Dt, unsigned Flags)
{
    return CProjectile::update(Dt, Flags);
}

void CCounterMeasure::expire_safe_time()
{
    // FUN_062aca10 (ICF-folded with CMine)
    PhySys::SetCollisionGroup(this, "");
}

Archetype::CounterMeasure const* CCounterMeasure::countermeasure_arch()
{
    // FUN_06262d50 (ICF fold)
    return (Archetype::CounterMeasure const*)Archetype;
}
