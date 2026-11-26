// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CSimple (retail CSimple.cpp) and CEquipmentObj.
#include "common_cobject.h"
#include "Objects/common_cobject_internal.h" // CmnCobject::CopyMatrix9
#include "x86math.h"

#include "common_cobjdb.h"
#include "common_physys.h"
#include "common_net.h"     // IsMPServer
#include "common_server.h"  // CRemotePhysicsController
#include "common_universe.h"

#include "DALib.h"

using CmnCobject::CopyMatrix9;

namespace
{
    const char kCSimpleSource[] = "E:\\FL\\Scratch\\Source\\Common\\CSimple.cpp";
    const float kHitPtsUseMax = -1.0f;
    const float kHitPtsNegativeFallback = 0.5f;
}

CEquipmentObj::CEquipmentObj(Class ObjectClass)
    : CObject(ObjectClass)
{
    // FUN_062940a0
}

CEquipmentObj::CEquipmentObj(CEquipmentObj const& Other)
    : CObject(Other)
{
    // FUN_06292d40, with the CObject copy inlined; no fields of its own.
}

CEquipmentObj::~CEquipmentObj()
{
}

CSimple::CSimple(Class ObjectClass)
    : CObject(ObjectClass)
{
    // FUN_062b5850. AttachedDamageObjs (+0xbc) and SelfWatchable (+0x9c) default-construct.
    FlagsA0 = 1;
    CurrentZone = nullptr;
    ZoneImmersion = 0.0f;
    ObjectDb = nullptr;
    Id = 0;
    OwnerPlayer = 0;
    HitPts = 0.0f;
    CachedCenterOfMass = Vector{0.0f, 0.0f, 0.0f};
    // -1.0f (+0xdc) is the "not yet cached" sentinel; 0 is a valid radius.
    CachedRadius = -1.0f;
    TypeBits = 0;
}

// FUN_062811b0, in retail's copy order; the CObject half is CObject's copy ctor.
CSimple::CSimple(CSimple const& Other)
    : CObject(Other)
    // Retail's XOR/AND/XOR copies only bit 0 (targetable) into a byte assumed to be zero; the
    // port copies bit 0 directly.
    , FlagsA0((unsigned char)(Other.FlagsA0 & 0x1))
    , CurrentZone(Other.CurrentZone)
    , ZoneImmersion(Other.ZoneImmersion)
    , ObjectDb(Other.ObjectDb)
    , Id(Other.Id)
    , OwnerPlayer(Other.OwnerPlayer)
    , HitPts(Other.HitPts)
    , AttachedDamageObjs(Other.AttachedDamageObjs)
    , CachedCenterOfMass(Other.CachedCenterOfMass)
    , CachedRadius(Other.CachedRadius)
    , TypeBits(Other.TypeBits)
{
    // Retail quirk kept: SelfWatchable (+0x9c) copies only the raw Head, sharing the source's
    // watcher chain.
    SelfWatchable.Head = Other.SelfWatchable.Head;
}

CSimple::~CSimple()
{
    // FUN_062b5980. The set at +0xbc and SelfWatchable are torn down by their member
    // destructors.
    PhySys::SetCollisionState(this, false);
}

void CSimple::connect(IObjDB* ObjDb)
{
    // FUN_062b5f80
    ObjectDb = ObjDb;
}

void CSimple::disconnect(IObjDB* ObjDb)
{
    if (ObjectDb == ObjDb)
        ObjectDb = nullptr;
}

void CSimple::open(Archetype::Root* Arch)
{
    // FUN_062b5cd0. Does not create the engine instance: every creator calls
    // Archetype::Root::create_instance itself (CPhysAttachment::Connect,
    // CArchGroup::AttachDamageObjs, CAsteroidCube::init, the Server creators, freelancer.exe
    // FUN_00539bc0). TypeBits (+0xe0) is seeded from the archetype (no null test).
    CObject::open(Arch);
    if (TypeBits == 0)
        TypeBits = Archetype->ObjectType;
}

void CSimple::cache_physical_props()
{
    // FUN_062b5ed0. CachedCenterOfMass +0xd0, CachedRadius +0xdc. Without a body: the engine
    // extent (IEngine +0x88, no null test) transformed to world (math slot +0x28).
    if (BodyPtr != nullptr)
    {
        CachedRadius = PhySys::GetRadiusR(this, false, CachedCenterOfMass);
        return;
    }

    DALib::Engine->GetInstanceExtent(EngineInstanceId, 0, &CachedRadius,
                                     &CachedCenterOfMass);
    Vector World;
    // this+0x08: Orientation followed by WorldPosition, one Transform.
    Acquire3DMathEngine()->TransformXform(World, get_transform(), CachedCenterOfMass);
    CachedCenterOfMass = World;
}

float CSimple::get_max_hit_pts() const
{
    // FUN_062b5fb0. No null test.
    return Archetype->HitPts;
}

float CSimple::get_hit_pts() const
{
    // FUN_062b5fc0
    return HitPts;
}

void CSimple::set_hit_pts(float NewHitPts)
{
    // FUN_062b5fd0. No clamping.
    HitPts = NewHitPts;
}

double CSimple::get_relative_health() const
{
    // FUN_062b5fe0. Unrounded in ST0; no null or zero test on the archetype.
    return (double)HitPts / Archetype->HitPts;
}

void CSimple::unmake_physical()
{
    // FUN_062b5f60, a JMP to CObject::unmake_physical. The virtual disable_controllers must
    // run, or the controllers stay "enabled" on the dead core and are never registered on the
    // new one (ship frozen after a system switch).
    CObject::unmake_physical();
}

float CSimple::get_scanner_interference() const
{
    // FUN_062b5e00. The zone's own interference (zone slot +0x60, ZONE 0633ad90) scales the
    // immersion cached by update_zones; the result is clamped to 1.0 (NaN gives 1.0).
    float Result = 0.0f;
    if (CurrentZone != nullptr)
    {
        float Modifier = CurrentZone->GetInterference();
        if (Modifier > 0.0f)
            Result = Modifier * ZoneImmersion;
    }
    if (!(Result < 1.0f))
        return 1.0f;
    return Result;
}

unsigned int const& CSimple::get_id() const
{
    return Id;
}

unsigned int CSimple::get_type() const
{
    return TypeBits;
}

IObjDB* CSimple::get_object_database() const
{
    return ObjectDb;
}

unsigned int CSimple::GetOwnerPlayer() const
{
    return OwnerPlayer;
}

void CSimple::SetOwnerPlayer(unsigned int PlayerId)
{
    // FUN_062d8920
    OwnerPlayer = PlayerId;
}

unsigned CSimple::get_name() const
{
    // FUN_062b5f70 (ICF-shared with CShip). The first get_name slot; CObject has none. No null
    // test.
    return (unsigned)Archetype->IdsName;
}

bool CSimple::is_targetable() const
{
    // FUN_06281190 (+0xa0 bit 0)
    return (FlagsA0 & 1) != 0;
}

void CSimple::init(CreateParms const& Parms)
{
    // FUN_062b5b60. No null tests on Archetype or DALib::Engine. connect is vtable +0x90.
    Id = Parms.Id;
    connect(Parms.ObjDb);
    OwnerPlayer = Parms.OwnerPlayer;

    // -1.0f (exact bit compare) means "archetype max".
    if (Parms.HitPts == kHitPtsUseMax)
    {
        HitPts = Archetype->HitPts;
    }
    // NaN goes on to the max check, and is kept.
    else if (!(Parms.HitPts < 0.0f))
    {
        float const ArchMax = Archetype->HitPts;
        if (!(ArchMax < Parms.HitPts))
        {
            HitPts = Parms.HitPts;
        }
        else
        {
            FDUMP(kFdumpError,
                  "%s(%d) : *** ERROR: ARCH(%s) ID(0x%X) CSimple::init(): "
                  "init_hit_pts (%f) greater than max (%f)",
                  kCSimpleSource, 0x45, Archetype->Nickname.get_string(), Id,
                  (double)Parms.HitPts, (double)ArchMax);
            HitPts = ArchMax;
        }
    }
    else
    {
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: ARCH(%s) ID(0x%X) CSimple::init(): "
              "Negative init_hit_pts (%f)",
              kCSimpleSource, 0x3b, Archetype->Nickname.get_string(), Id,
              (double)Parms.HitPts);
        HitPts = kHitPtsNegativeFallback;
    }

    WorldPosition = Parms.Pos;
    CopyMatrix9(Orientation, Parms.Orient);

    // The engine instance was made by whoever created this object (see CSimple::open).
    DALib::Engine->UpdateInstance(EngineInstanceId, 0, nullptr);

    init_physics(Parms.Velocity, Parms.AngularVelocity);   // vtable +0x9c
}

void CSimple::update_zones(float Dt, unsigned int Flags)
{
    // FUN_062b5d00. Neither argument is read. ZoneImmersion is +0xa8, CurrentZone +0xa4.
    (void)Dt;
    (void)Flags;

    if (ObjectDb == nullptr)
    {
        ZoneImmersion = 0.0f;
        CurrentZone = nullptr;
        return;
    }

    // A multiplayer server samples a player-owned object at the latest position its client
    // reported (the CRemotePhysicsController at +0x60, via __RTDynamicCast); otherwise, a
    // failed cast included, the local position.
    Vector Pos;
    if (IsMPServer() && OwnerPlayer != 0)
    {
        CRemotePhysicsController* const Remote =
            dynamic_cast<CRemotePhysicsController*>(GolemController);
        if (Remote != nullptr)
        {
            // No client sample yet: only the zone is cleared; the interference is kept.
            if (Remote->GetSampleCount() == 0)
            {
                CurrentZone = nullptr;
                return;
            }
            // Controller +0xe0 (GetLatestUpdate, inlined).
            Pos = Remote->GetLatestUpdate();
        }
        else
        {
            Pos = WorldPosition;
        }
    }
    else
    {
        Pos = WorldPosition;
    }

    // ISystem slot 11 (SYSTEM 0633b4b0); the system is used without a null check.
    Universe::ISystem const* const ZoneDb = ObjectDb->GetZoneDb();
    Universe::IZone const* const Zone = ZoneDb->GetSmallestPhysicalZoneAt(&Pos);
    CurrentZone = const_cast<Universe::IZone*>(Zone);
    if (Zone == nullptr)
    {
        ZoneImmersion = 0.0f;
        return;
    }
    ZoneImmersion = Zone->GetImmersion(Pos);   // zone slot 0
}

float CSimple::get_physical_radius_r(Vector& OutCenter) const
{
    // FUN_062b5e60. Recomputes the cache (vtable +0x84, cache_physical_props) while the radius
    // is still the -1.0f sentinel.
    if (CachedRadius == -1.0f)
        const_cast<CSimple*>(this)->cache_physical_props();
    OutCenter = CachedCenterOfMass;
    return CachedRadius;
}

void CSimple::beam_object(Vector const& Pos, Matrix const& Orient, bool bAttach)
{
    // FUN_062b5ea0. vtable +0x84 (slot 0639b908) is cache_physical_props.
    CObject::beam_object(Pos, Orient, bAttach);
    cache_physical_props();
}
