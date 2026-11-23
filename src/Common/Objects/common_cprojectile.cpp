// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CProjectile, CMunition, CGuided (retail CGuided.cpp) and CBeam (retail CBeam.cpp).
#include "common_cobject.h"
#include "Objects/common_cobject_internal.h" // CmnCobject::RotateWorldToObjectSpace, GetObjectDbCollisionGroup

#include "common_physys.h"
#include "common_net.h"
#include "common_iface.h"

#include "DALib.h"

#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using CmnCobject::GetObjectDbCollisionGroup;
using CmnCobject::RotateWorldToObjectSpace;

namespace
{
    const char kCBeamSource[] = "E:\\FL\\Scratch\\Source\\Common\\CBeam.cpp";
    const char kCGuidedSource[] = "E:\\FL\\Scratch\\Source\\Common\\CGuided.cpp";

    // IEngine +0x7c, as CBeam::init_physics calls it: no test on the instance or the engine.
    void SetInstanceUserData(long Instance, CBeam* Beam)
    {
        // IEngine takes the instance handle as a pointer-sized value.
        DALib::Engine->SetInstanceUserData((void*)Instance, Beam);
    }
}

CProjectile::CProjectile(Class ObjectClass)
    : CSimple(ObjectClass)
{
    // FUN_062afa90. Lifetime is left for init.
    OwnerId = 0;
    OwnerSafeTime = 0.0f;
}

CProjectile::~CProjectile()
{
}

void CProjectile::init(CreateParms const& Parms)
{
    // FUN_062afac0. OwnerId +0xe4 is set before CSimple::init; Lifetime / OwnerSafeTime
    // (+0xe8 / +0xec) come from the archetype with no null test.
    OwnerId = Parms.OwnerId;
    CSimple::init(Parms);
    Archetype::Projectile const* const Proj = projarch();
    Lifetime = Proj->Lifetime;
    OwnerSafeTime = Proj->OwnerSafeTime;
}

int CProjectile::update(float Dt, unsigned Flags)
{
    CObject::update(Dt, Flags);
    Lifetime -= Dt;
    if (OwnerSafeTime > 0.0f)
    {
        OwnerSafeTime -= Dt;
        if (OwnerSafeTime <= 0.0f)
        {
            OwnerSafeTime = 0.0f;
            expire_safe_time();
        }
    }
    return 0;
}

Archetype::Projectile const* CProjectile::projarch() const
{
    // FUN_06262d50 (ICF fold)
    return (Archetype::Projectile const*)Archetype;
}

void CProjectile::set_dead()
{
    Lifetime = -1.0f;
}

unsigned int const& CProjectile::get_owner() const
{
    return OwnerId;
}

bool CProjectile::is_alive() const
{
    // FUN_06286070
    return Lifetime >= 0.0f;
}

bool CProjectile::is_owner_safe() const
{
    // FUN_062afb70. Exactly 0.0 and NaN give false; negative counts as safe.
    return OwnerSafeTime < 0.0f || OwnerSafeTime > 0.0f;
}

CMunition::CMunition(Class ObjectClass)
    : CProjectile(ObjectClass)
{
    // FUN_06286380 (copy ctor FUN_062863a0); no fields of its own.
}

CMunition::~CMunition()
{
    // FUN_06286c80, a JMP to ~CSimple ICF-folded with the CBeam, CCounterMeasure, CLoot, CMine
    // and CProjectile destructors.
}

Archetype::Munition const* CBeam::munitionarch() const
{
    // FUN_06262d50 (ICF fold)
    return (Archetype::Munition const*)Archetype;
}

Archetype::Munition const* CMunition::munitionarch() const
{
    // FUN_06262d50, folded with the other arch accessors (Ghidra labels it
    // CAsteroid::asteroidarch). No null check.
    return (Archetype::Munition const*)Archetype;
}

CGuided::CGuided(Class ObjectClass)
    : CMunition(ObjectClass)
{
    // FUN_062acbd0. TargetWatch is +0xf0/+0xf4. The alignment word +0xfa and SeekerElapsed
    // (+0x100) are not written here (init() zeroes SeekerElapsed).
    SubTarget = SUBOBJ_ID_NONE;
    MotorArch = nullptr;
}

CGuided::~CGuided()
{
    // FUN_062acc40. Retail unlinks the watcher explicitly; TargetWatch's destructor does the same.
}

void CGuided::init(CreateParms const& Parms)
{
    // FUN_062accb0. Parms.Target converts IObjRW -> Watchable (+8, null stays null).
    CProjectile::init(Parms);
    Watchable const* NewTarget = Parms.Target;
    TargetWatch.set(NewTarget);
    SubTarget = Parms.SubTarget;
    SeekerElapsed = 0.0f;
    // No null test on the archetype.
    Archetype::Munition const* const Mun = munitionarch();
    if (Mun->Motor.IsEmpty())
        MotorArch = nullptr;
    else
        MotorArch = Archetype::GetMotor(Mun->Motor.Id);
}

int CGuided::update(float Dt, unsigned Flags)
{
    SeekerElapsed += Dt;
    return CProjectile::update(Dt, Flags);
}

bool __cdecl CGuided::seeker_can_see(Vector const& From, Vector const& Target, Vector const& Forward,
                                     Archetype::Munition const* Arch)
{
    // FUN_062acd60. D's components are rounded to float; every later term stays on the x87
    // stack. Equal and unordered fail both tests.
    float const Dx = Target.x - From.x;
    float const Dy = Target.y - From.y;
    float const Dz = Target.z - From.z;
    double const DistSq = ((double)Dy * Dy + (double)Dz * Dz) + (double)Dx * Dx;
    double const Range = Arch->SeekerRange;
    if (!(Range * Range > DistSq))
        return false;
    double const InvDist = 1.0 / sqrt(DistSq);
    double const Dot = (Dz * InvDist * Forward.z + Dy * InvDist * Forward.y) + Dx * InvDist * Forward.x;
    return Dot >= Arch->SeekerFovCos;   // +0x9c
}

bool CGuided::seeker_can_see(Vector const& Pos) const
{
    // FUN_062ace20. Forward is Orientation's column 2 negated. Sum order and rounding differ
    // from the static overload: the first square multiplies the unrounded dz (FST) by the
    // rounded one.
    float const ForwardX = -Orientation.E[0][2];
    float const ForwardY = -Orientation.E[1][2];
    float const ForwardZ = -Orientation.E[2][2];
    float const Dx = Pos.x - WorldPosition.x;
    float const Dy = Pos.y - WorldPosition.y;
    double const DzWide = (double)Pos.z - WorldPosition.z;
    float const Dz = (float)DzWide;
    double const DistSq = (DzWide * Dz + (double)Dy * Dy) + (double)Dx * Dx;

    // No null test on the archetype.
    Archetype::Munition const* const Mun = munitionarch();
    double const Range = Mun->SeekerRange;
    if (!(DistSq < Range * Range))
        return false;

    double const InvDist = 1.0 / sqrt(DistSq);
    double const Dot = (Dz * InvDist * ForwardZ + Dy * InvDist * ForwardY) + Dx * InvDist * ForwardX;
    return Dot >= Mun->SeekerFovCos;
}

void CGuided::init_physics(Vector const& Velocity, Vector const& AngularVelocity)
{
    // FUN_062acf00. Mass from the archetype (no null test), both dampings 0, Name =
    // _itoa(OwnerId, radix 24). The bytes retail leaves unwritten are zero here.
    PhySys::CreateParms Phy;
    memset(&Phy, 0, sizeof(Phy));
    Phy.Type = 2;
    Phy.bEnableCollisions = 1;
    _itoa((int)OwnerId, Phy.Name, 0x18);
    Phy.Mass = Archetype->Mass;
    Phy.CollisionGroupId = GetObjectDbCollisionGroup(ObjectDb);
    PhySys::BuildIVP_Real(this, Phy);
    PhySys::AddToVelocity(this, Velocity);
    PhySys::AddToAngularVelocityOS(this, RotateWorldToObjectSpace(Orientation, AngularVelocity));
}

void CGuided::expire_safe_time()
{
    // FUN_062aca10 (ICF-folded with CMine)
    PhySys::SetCollisionGroup(this, "");
}

IObjRW* CGuided::get_target() const
{
    // FUN_06286890. The Watchable sits at IObjRW+8; null stays null.
    Watchable const* Watched = TargetWatch.target();
    if (Watched == nullptr)
        return nullptr;
    return static_cast<IObjRW*>(const_cast<Watchable*>(Watched));
}

void CGuided::set_target(IObjRW* Target)
{
    // FUN_062868c0. Only for a guided munition (Seeker +0x90; no null test on the archetype).
    if (!munitionarch()->is_guided())
        return;
    Watchable const* NewTarget = Target;   // IObjRW -> its Watchable base (+8), null stays null
    TargetWatch.set(NewTarget);
}

unsigned short CGuided::get_sub_target() const
{
    return SubTarget;
}

void CGuided::set_sub_target(unsigned short SubTargetId)
{
    // FUN_06286900. As set_target; SubTarget is +0xf8.
    if (munitionarch()->is_guided())
        SubTarget = SubTargetId;
}

bool CGuided::motor_on() const
{
    // FUN_06286920. On between Delay and Delay + Lifetime (sum unrounded); unordered is off.
    if (MotorArch == nullptr)
        return false;
    if (!(SeekerElapsed >= MotorArch->Delay))
        return false;
    if (!((double)MotorArch->Lifetime + MotorArch->Delay >= SeekerElapsed))
        return false;
    return true;
}

Archetype::MotorData const* CGuided::get_motor_arch() const
{
    return MotorArch;
}

CBeam::CBeam(Class ObjectClass)
    : CMunition(ObjectClass)
{
    bMoved = 0;
}

CBeam::~CBeam()
{
}

void CBeam::init_physics(Vector const& Velocity, Vector const& AngularVelocity)
{
    // FUN_062ad190. A beam has no physics body.
    (void)AngularVelocity;
    SetInstanceUserData(EngineInstanceId, this);
    _itoa((int)OwnerId, CollisionGroup, 0x18);
    this->Velocity = Velocity;
}

void CBeam::expire_safe_time()
{
    CollisionGroup[0] = '\0';
}

Vector CBeam::get_velocity() const
{
    return Velocity;
}

float CBeam::get_mass() const
{
    return 0.0f;
}

void CBeam::move(float Dt)
{
    // FUN_062ad090. A negative or unordered Lifetime returns.
    if (!(Lifetime >= 0.0f))
        return;
    if (bMoved == 0)
    {
        OriginPos = WorldPosition;
        LastPos = WorldPosition;
        bMoved = 1;
    }
    LastPos = WorldPosition;
    // x87: the x and y steps are added unrounded; the z step is rounded to float first.
    float const StepZ = Dt * Velocity.z;
    WorldPosition.x = (float)((double)Dt * Velocity.x + WorldPosition.x);
    WorldPosition.y = (float)((double)Dt * Velocity.y + WorldPosition.y);
    WorldPosition.z = StepZ + WorldPosition.z;
}

void CBeam::set_velocity(Vector const& Vel)
{
    Velocity = Vel;
}

Vector const& CBeam::get_last_pos() const
{
    if (bMoved != 0)
        return LastPos;
    return WorldPosition;
}

char const* CBeam::get_collision_group() const
{
    return CollisionGroup;
}
