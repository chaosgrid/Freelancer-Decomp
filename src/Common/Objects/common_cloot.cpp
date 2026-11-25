// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CLoot (retail CLoot.cpp).
#include "common_cobject.h"
#include "Objects/common_cobject_internal.h" // CmnCobject::RotateWorldToObjectSpace, GetObjectDbSystemId

#include "common_physys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using CmnCobject::GetObjectDbSystemId;
using CmnCobject::RotateWorldToObjectSpace;

// ============================================================================
// CLoot implementation
// ============================================================================

CLoot::CLoot(Class ObjectClass)
    : CSimple(ObjectClass)
{
    // FUN_062ac400. The alignment bytes +0xfe/+0xff are not written by retail.
    OwnerId = 0;
    NameId = 0;
    ContentsArch = nullptr;
    Units = 0;
    ContentsHitPts = 0.0f;
    OwnerSafeTime = 0.0f;
    bTemporary = false;
    Reserved100 = 0;
    bCanAiTractor = true;
    TypeBits = 0x10000000;
}

// FUN_0629d530, with CSimple's copy ctor inlined. The alignment bytes +0xfe/+0xff are not
// written by retail.
CLoot::CLoot(CLoot const& Other)
    : CSimple(Other)
    , OwnerId(Other.OwnerId)
    , NameId(Other.NameId)
    , ContentsArch(Other.ContentsArch)
    , Units(Other.Units)
    , ContentsHitPts(Other.ContentsHitPts)
    , OwnerSafeTime(Other.OwnerSafeTime)
    , bCanAiTractor(Other.bCanAiTractor)
    , bTemporary(Other.bTemporary)
    , Reserved100(Other.Reserved100)
{
}

CLoot::~CLoot()
{
}

void CLoot::init(CreateParms const& Parms)
{
    // FUN_062ac460
    OwnerId = Parms.OwnerId;
    if (OwnerId != 0)
        OwnerSafeTime = LOOT_OWNER_SAFE_TIME;
    NameId = Parms.NameId;
    ContentsArch = Parms.ContentsArch;
    Units = Parms.Units;
    ContentsHitPts = Parms.ContentsHitPts;
    bCanAiTractor = Parms.bCanAiTractor;
    bTemporary = Parms.bTemporary;
    CSimple::init(Parms);
}

void CLoot::init_physics(Vector const& Velocity, Vector const& AngularVelocity)
{
    // FUN_062ac4c0. No null test on the container archetype. Name is _itoa(OwnerId, else Id,
    // radix 24). The bytes retail leaves unwritten are zero here.
    PhySys::CreateParms Phy;
    memset(&Phy, 0, sizeof(Phy));
    Phy.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
    Phy.AngularDamping.x = PhySys::DEFAULT_ANGULAR_DAMPING.x;
    Phy.AngularDamping.y = PhySys::DEFAULT_ANGULAR_DAMPING.y;
    Phy.AngularDamping.z = PhySys::DEFAULT_ANGULAR_DAMPING.z;
    Phy.Type = PhySys::ANIM_TECHNIQUE_MERGED;
    Phy.bEnableCollisions = 1;
    Phy.Mass = Archetype->Mass;
    Phy.SystemId = GetObjectDbSystemId(ObjectDb);
    _itoa((int)((OwnerId != 0) ? OwnerId : Id), Phy.Name, 0x18);
    PhySys::BuildIVP_Real(this, Phy);

    // No body (+0x54): report and leave the velocity alone. The contents name is fetched first.
    if (BodyPtr == nullptr)
    {
        char const* const ContentsName = ContentsArch->Nickname.get_string();
        char const* const ContainerName = container_arch()->Nickname.get_string();
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: CLoot::init_physics() failed for %s (containing %s). Verify 'DA_archeytpe' data! NOT ignorable.",
              "E:\\FL\\Scratch\\Source\\Common\\CLoot.cpp", 0x53, ContainerName, ContentsName);
        return;
    }

    PhySys::AddToVelocity(this, Velocity);
    // World-space AngularVelocity rotated into object space.
    PhySys::AddToAngularVelocityOS(this, RotateWorldToObjectSpace(Orientation, AngularVelocity));
}

int CLoot::update(float Dt, unsigned Flags)
{
    // FUN_062ac680. When the owner-safe time lapses, the collision group becomes the loot's own
    // id, so the owner can collide with it.
    update_zones(Dt, Flags);
    if (OwnerSafeTime > 0.0f)
    {
        OwnerSafeTime -= Dt;
        if (OwnerSafeTime <= 0.0f)
        {
            char Name[8];
            _itoa((int)Id, Name, 0x18);
            PhySys::SetCollisionGroup(this, Name);
        }
    }
    return CObject::update(Dt, Flags);
}

void CLoot::set_hit_pts(float NewHitPts)
{
    // FUN_062ac700. When the loot is its own cargo, ContentsHitPts mirrors HitPts. No null tests.
    if (contents_arch()->get_id() == container_arch()->get_id())
        ContentsHitPts = NewHitPts;
    CSimple::set_hit_pts(NewHitPts);
}

unsigned CLoot::get_name() const
{
    // FUN_062ac740. No null test on ContentsArch.
    if (NameId != 0)
        return (unsigned)NameId;
    return (unsigned)ContentsArch->IdsName;
}

unsigned CLoot::get_owner() const
{
    // FUN_0629d470
    return OwnerId;
}

unsigned CLoot::get_units() const
{
    return Units;
}

float CLoot::get_volume() const
{
    // FUN_0629d4e0. Units is widened as unsigned; no null test on the contents archetype.
    return (float)((double)Units * contents_arch()->Volume);
}

float CLoot::get_contents_hit_pts() const
{
    return ContentsHitPts;
}

void CLoot::set_units(unsigned Count)
{
    Units = Count;
}

void CLoot::set_contents_hit_pts(float Pts)
{
    ContentsHitPts = Pts;
}

bool CLoot::is_loot_temporary() const
{
    return bTemporary;
}

bool CLoot::can_ai_tractor() const
{
    // FUN_0629d510
    return bCanAiTractor;
}

Archetype::Equipment* CLoot::contents_arch() const
{
    // FUN_0629d490
    return const_cast<Archetype::Equipment*>(ContentsArch);
}

Archetype::Equipment* CLoot::container_arch() const
{
    // FUN_0629d480, a JMP to the ICF-folded CAsteroid::asteroidarch.
    return (Archetype::Equipment*)Archetype;
}
