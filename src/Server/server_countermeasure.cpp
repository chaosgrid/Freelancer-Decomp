// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Server.dll CounterMeasure wrapper (vtable .rdata:06d64fcc), declared in
// server_gameobject.h. No fields of its own beyond GameObject's 0x60 bytes.

#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"   // StarSystem::explode / find_object
#include "../include/common_misc.h"    // SUBOBJ_ID_NONE, DamageList
#include "../include/common_id.h"      // ID_String::IsEmpty
#include "../include/common_physys.h"  // PhySys::FindSphereCollisions
#include "../include/DALib.h"          // DALib::Engine (CounterMeasure::create, IEngine +0x6c)

#include <cstdlib>   // rand

// DAT_06d8d588 -- the file-static hit buffer divert_missiles hands to FindSphereCollisions.
static CObject* GCounterMeasureHits[0x1e];

// FUN_06ce6170. No field of its own is written.
CounterMeasure::CounterMeasure()
{
}

// LAB_06ce61f0.
unsigned int CounterMeasure::get_type_bits() const
{
    return 0x8000000u;
}

CCounterMeasure* CounterMeasure::ccountermeasure() const
{
    CObject* const Obj = Object;
    if (Obj == nullptr || (static_cast<unsigned int>(Obj->ObjectClass) & CObject::ClassCounterMeasure) != CObject::ClassCounterMeasure)
        return nullptr;
    return static_cast<CCounterMeasure*>(Obj);
}

// LAB_06ce6200 -- vtable +0x164.
bool CounterMeasure::create(Archetype::Root* Arch)
{
    Object = CObject::Alloc(CObject::ClassCounterMeasure);

    Arch->create_instance(static_cast<IEngineInstance*>(Object));

    DALib::Engine->UpdateInstance(Object->EngineInstanceId, 0, nullptr);

    // The (& 3) narrowing is not null-tested.
    CObject* Narrowed = Object;
    if (Narrowed == nullptr || (static_cast<unsigned int>(Narrowed->ObjectClass) & CObject::ClassSimple) != CObject::ClassSimple)
        Narrowed = nullptr;
    Narrowed->open(Arch);

    advise(true);

    return get_index() != -1;
}

// .text:06d02cb0 -- vtable +0x168: always false.
bool CounterMeasure::find(unsigned int const& Id)
{
    (void)Id;
    return false;
}

// FUN_06ce6280 -- vtable +0x16c.
bool CounterMeasure::update(float Dt)
{
    if (!GameObject::update(Dt))
        return false;

    // Not null-tested.
    CCounterMeasure* const Cm = ccountermeasure();
    Cm->update(Dt, CObject::UPDATE_DEFAULT);

    // A spent lifetime (below zero, or unordered on x87) destroys it quietly.
    if (!(Cm->Lifetime >= 0.0f))
        destroy(0, 0);

    return !is_destroyed();
}

// FUN_06ce6300.
void CounterMeasure::divert_missiles()
{
    // The archetype's Range, read through the raw CBase::Object.
    Archetype::CounterMeasure const* const Arch =
        static_cast<Archetype::CounterMeasure const*>(Object->get_archetype());
    int const Count = PhySys::FindSphereCollisions(OwnerSystem->SystemInfo->NicknameId,
                                                   get_position(), Arch->Range,
                                                   GCounterMeasureHits, 0x1e);

    for (int Index = 0; Index < Count; ++Index)
    {
        // Missiles only ((& 0xa7), a CGuided).
        CObject* const Hit = GCounterMeasureHits[Index];
        if (Hit == nullptr || (static_cast<unsigned int>(Hit->ObjectClass) & CObject::ClassGuided) != CObject::ClassGuided)
            continue;

        // Not a missile of this countermeasure's own owner (this side's narrowing is not
        // null-tested).
        CCounterMeasure* const Cm = ccountermeasure();
        if (static_cast<CProjectile*>(Hit)->OwnerId == Cm->OwnerId)
            continue;

        GameObject* const Missile = OwnerSystem->find_object(static_cast<CSimple*>(Hit)->Id);
        if (Missile == nullptr)
            continue;

        // 0..99, with MSVC's signed-division rounding.
        int const Roll = (std::rand() * 100) / 0x8000;

        // A roll above DiversionPctg keeps the target (the archetype fetched again).
        Archetype::CounterMeasure const* const RollArch =
            static_cast<Archetype::CounterMeasure const*>(Object->get_archetype());
        if (Roll > RollArch->DiversionPctg)
            continue;

        static_cast<IObjAffect*>(Missile)->set_target(static_cast<IObjRW*>(this),
                                                      SUBOBJ_ID_NONE, 0);
    }
}

// LAB_06ce63f0 -- vtable +0x158.
bool CounterMeasure::destroy(int Kind, unsigned int ObjId)
{
    // The base result is not used.
    GameObject::destroy(Kind, ObjId);

    // Only a real kill blows up, and only with an explosion that has a radius and some
    // effect above 0.0001 (DAT_06d65190).
    if (Kind == 1)
    {
        if (!Object->get_archetype()->ExplosionArch.IsEmpty())
        {
            Archetype::Explosion* const Explosion =
                Archetype::GetExplosion(Object->get_archetype()->ExplosionArch);
            double const Epsilon = 0.0001;
            if (Explosion != nullptr &&
                static_cast<double>(Explosion->Radius) > Epsilon &&
                (static_cast<double>(Explosion->Impulse) > Epsilon ||
                 static_cast<double>(Explosion->HullDamage) > Epsilon ||
                 static_cast<double>(Explosion->EnergyDamage) > Epsilon))
            {
                // The owner read off the raw object with no narrowing; cause 4.
                ExplosionDamageEvent Blast;
                Blast.ExplodingObjId = get_id();
                Blast.InflictorId = static_cast<CProjectile*>(Object)->OwnerId;
                Blast.Cause = static_cast<DamageCause>(4);
                Blast.Position = get_position();
                Blast.Explosion = Explosion;
                Blast.SkipCollisionGroup = nullptr;
                OwnerSystem->explode(Blast);
            }
        }
    }

    return true;
}

// LAB_06cf4230 -- vtable +0x17c AND +0x180: any hit destroys it.
void CounterMeasure::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    Damage.bDestroyed = true;
}

void CounterMeasure::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    (void)Hit;
    Damage.bDestroyed = true;
}
