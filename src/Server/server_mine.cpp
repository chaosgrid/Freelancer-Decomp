// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Server.dll Mine wrapper -- retail .text:06cf3330-06cf3c6f plus the two damage overrides
// at .text:06cf4230 / 06cf4240 that its vtable (.rdata:06d6606c) shares with Guided.
// The class is declared in src/include/server_gameobject.h.
//
// Layout: Projectile/GameObject +0x00..+0x5f, PhySys::Controller +0x60 (vtable
// .rdata:06d6622c), PhySys::CObjectCB +0x70 (vtable .rdata:06d65fd8), then the Mine's own
// fields +0x74..+0x9f; size 0xa0 from the spawner (StarSystem FUN_06d08ef0).

#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"   // StarSystem::explode
#include "../include/common_misc.h"    // SUBOBJ_ID_*, DamageList
#include "../include/common_id.h"      // ID_String::IsEmpty
#include "../include/DALib.h"          // DALib::Engine (Mine::create, IEngine +0x6c)
#include "../IVP/ivp_physics/ivp_core.h"        // IVP_Core::Speed (+0xa4)
#include "../include/common_ivp_real_object.h" // IVP_Real_Object::core (+0x98)
#include "../include/common_ivp_controller.h"  // IVP_Event_Sim::delta_time

#include <cfloat>
#include <cmath>

// ---------------------------------------------------------------------------
// FUN_06cf3330. Retail zeroes +0x74, +0x78, byte +0x80, +0x84..+0x8c, byte +0x94, +0x98
// and sets +0x90 = FLT_MAX; +0x7c and +0x9c are not touched.
// ---------------------------------------------------------------------------
Mine::Mine()
    : Phantom(nullptr)
    , IntruderCount(0)
    , bSeeking(false)
    , SeekTimer(FLT_MAX)
    , bTriggered(false)
    , TriggerObjId(0)
{
    Target.x = 0.0f;
    Target.y = 0.0f;
    Target.z = 0.0f;
}

// ---------------------------------------------------------------------------
// FUN_06cf3440, entered on the CBase sub-object (+0xc).
// ---------------------------------------------------------------------------
Mine::~Mine()
{
    // No null test on the phantom.
    PhySys::DestroyPhantom(Phantom);
    Phantom = nullptr;
}

// LAB_06cf3400.
unsigned int Mine::get_type_bits() const
{
    return 0x4000000u;
}

CMine* Mine::cmine() const
{
    CObject* const Obj = Object;
    if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & 0xfu) != 0xfu)
        return nullptr;
    return static_cast<CMine*>(Obj);
}

// ---------------------------------------------------------------------------
// LAB_06cf34e0 -- vtable +0x164.
// ---------------------------------------------------------------------------
bool Mine::create(Archetype::Root* Arch)
{
    Object = CObject::Alloc(CObject::ClassMine);

    // Before open, so the port's CSimple::open scaffold finds a valid instance and does
    // nothing.
    Arch->create_instance(static_cast<IEngineInstance*>(Object));

    DALib::Engine->UpdateInstance(reinterpret_cast<void*>(Object->EngineInstanceId), 0, nullptr);

    // open (vt+0x4c) goes through the CSimple narrowing, not null-tested.
    CObject* Narrowed = Object;
    if (Narrowed == nullptr || (static_cast<unsigned>(Narrowed->ObjectClass) & 3u) != 3u)
        Narrowed = nullptr;
    Narrowed->open(Arch);

    advise(true);

    return get_index() != -1;
}

// .text:06d02cb0 -- the Mine vtable's +0x168.
bool Mine::find(unsigned int const& Id)
{
    (void)Id;
    return false;
}

// ---------------------------------------------------------------------------
// FUN_06cf3560.
// ---------------------------------------------------------------------------
void Mine::arm()
{
    // A seeking mine (SeekDist +0x88 > 0) registers its controller and uses SeekDist as the
    // phantom radius. Retail quirk: a non-seeking mine with DetonationDist (+0x84) > 0 also
    // takes SeekDist as the radius (.text:06cf35c5).
    Archetype::Mine const* const Arch =
        static_cast<Archetype::Mine const*>(Object->get_archetype());
    float Radius;
    if (Arch->SeekDist > 0.0f)
    {
        Radius = Arch->SeekDist;
        PhySys::Controller::Register(static_cast<PhySys::Controller*>(this), Object);
    }
    else
    {
        if (!(Arch->DetonationDist > 0.0f))
            return;
        Radius = Arch->SeekDist;
    }

    // Only a radius of exactly zero (ordered) makes no phantom.
    if (Radius == 0.0f)
        return;

    // The listener is the CObjectCB sub-object (+0x70).
    PhySys::PhantomParms Parms;
    Parms.SystemId = OwnerSystem->SystemInfo->NicknameId;
    Parms.Flag04 = 0;
    Parms.Flag05 = 1;
    Parms.Name[0] = '\0';
    Parms.Position = get_position();
    Parms.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                               0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 1.0f);
    Parms.Listener = static_cast<PhySys::CObjectCB*>(this);

    PhySys::CreatePhantom(Parms, Radius, Phantom);
    PhySys::AttachPhantom(Object, Phantom);
}

// ---------------------------------------------------------------------------
// LAB_06cf36a0 -- vtable +0x16c.
// ---------------------------------------------------------------------------
bool Mine::update(float Dt)
{
    if (!GameObject::update(Dt))
        return false;

    // Not null-tested.
    CMine* const Cm = cmine();
    Cm->update(Dt, CObject::UPDATE_DEFAULT);

    // A spent lifetime (CProjectile+0xe8 below zero, or unordered) removes the mine quietly.
    if (!(Cm->Lifetime >= 0.0f))
    {
        destroy(0, 0);
    }
    else
    {
        if (!PhySys::GetCollisionStatePhantom(Phantom))
            PhySys::SetCollisionStatePhantom(Phantom, true);

        if (IntruderCount != 0)
        {
            // The owner is exempt only while the owner-safe time runs.
            unsigned int Exempt = 0;
            if (Cm->is_owner_safe())
                Exempt = Cm->OwnerId;

            // Closest intruder of type mask 0x7ffdf8; its position lands in Target (+0x84).
            CSimple* Closest = nullptr;
            PhySys::GetClosestPhantomIntruderPos(Phantom, 0x7ffdf8u, Exempt, Closest, Target);
            if (Closest != nullptr)
            {
                Archetype::Mine const* const Arch =
                    static_cast<Archetype::Mine const*>(Object->get_archetype());
                float const DetonationSq = static_cast<float>(
                    static_cast<double>(Arch->DetonationDist) *
                    static_cast<double>(Arch->DetonationDist));

                // Summed z, x, then y in double precision; strictly inside detonates.
                Vector const& Pos = get_position();
                double const Dx = static_cast<double>(Target.x) - static_cast<double>(Pos.x);
                double const Dy = static_cast<double>(Target.y) - static_cast<double>(Pos.y);
                double const Dz = static_cast<double>(Target.z) - static_cast<double>(Pos.z);
                double const DistSq = (Dz * Dz + Dx * Dx) + Dy * Dy;
                if (DistSq < static_cast<double>(DetonationSq))
                {
                    destroy(1, Closest->Id);
                }
                else
                {
                    // The compare reads the unrounded double timer sum.
                    wakeup();
                    bSeeking = true;
                    double const NewTimer =
                        static_cast<double>(Dt) + static_cast<double>(SeekTimer);
                    SeekTimer = static_cast<float>(NewTimer);
                    if (NewTimer > 1.0)
                        SeekTimer = 0.0f;
                }
            }
            else
            {
                bSeeking = false;
                Target.z = 0.0f;
                Target.y = 0.0f;
                Target.x = 0.0f;
            }
        }
        else
        {
            // Empty phantom: a touch latched by added() detonates now.
            bSeeking = false;
            Target.z = 0.0f;
            Target.y = 0.0f;
            Target.x = 0.0f;
            if (bTriggered)
                destroy(1, TriggerObjId);
        }
    }

    return !is_destroyed();
}

// ---------------------------------------------------------------------------
// LAB_06cf3870 -- vtable +0x158.
// ---------------------------------------------------------------------------
bool Mine::destroy(int Kind, unsigned int ObjId)
{
    // GameObject's destroy (FUN_06cee630); its result is not used.
    GameObject::destroy(Kind, ObjId);

    // Only a real kill blows up, and only with an explosion that has a radius and some
    // effect (double 0.0001 at .rdata:06d65190).
    if (Kind == 1)
    {
        Archetype::Root* const Arch = Object->get_archetype();
        if (!Arch->ExplosionArch.IsEmpty())
        {
            Archetype::Explosion* const Explosion = Archetype::GetExplosion(Arch->ExplosionArch);
            double const Epsilon = 0.0001;
            if (Explosion != nullptr &&
                static_cast<double>(Explosion->Radius) > Epsilon &&
                (static_cast<double>(Explosion->Impulse) > Epsilon ||
                 static_cast<double>(Explosion->HullDamage) > Epsilon ||
                 static_cast<double>(Explosion->EnergyDamage) > Epsilon))
            {
                // Owner is CMine+0xe4 (narrowing not null-tested). Cause 7; retail first
                // stores 4, then overwrites it.
                ExplosionDamageEvent Blast;
                Blast.ExplodingObjId = get_id();
                Blast.InflictorId = cmine()->OwnerId;
                Blast.Cause = static_cast<DamageCause>(7);
                Blast.Position = get_position();
                Blast.Explosion = Explosion;
                Blast.SkipCollisionGroup = nullptr;
                OwnerSystem->explode(Blast);
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// FUN_06cf3990 -- CObjectCB slot 0 on the +0x70 sub-object.
// ---------------------------------------------------------------------------
void Mine::added(CObject* Obj)
{
    Archetype::Mine const* const Arch =
        static_cast<Archetype::Mine const*>(Object->get_archetype());
    if (Arch->SeekDist > 0.0f)
    {
        // A seeking mine counts CSimple roots as intruders.
        CObject* const Root = GetRoot(Obj);
        if (Root != nullptr && (static_cast<unsigned>(Root->ObjectClass) & 3u) == 3u)
            IntruderCount = IntruderCount + 1;
        return;
    }

    // A non-seeking mine latches a CSimple root that touches it, unless it is the owner
    // (narrowing not null-tested) while the owner-safe time still runs.
    CObject* const Root = GetRoot(Obj);
    if (Root == nullptr || (static_cast<unsigned>(Root->ObjectClass) & 3u) != 3u)
        return;
    CSimple* const Toucher = static_cast<CSimple*>(Root);

    CMine* const Cm = cmine();
    if (Toucher->Id == Cm->OwnerId && Cm->is_owner_safe())
        return;

    bTriggered = true;
    TriggerObjId = Toucher->Id;
}

// ---------------------------------------------------------------------------
// FUN_06cf3a50 -- CObjectCB slot 1 on the +0x70 sub-object.
// ---------------------------------------------------------------------------
void Mine::deleted(CObject* Obj)
{
    // Only a seeking mine counts intruders.
    Archetype::Mine const* const Arch =
        static_cast<Archetype::Mine const*>(Object->get_archetype());
    if (!(Arch->SeekDist > 0.0f))
        return;
    CObject* const Root = GetRoot(Obj);
    if (Root != nullptr && (static_cast<unsigned>(Root->ObjectClass) & 3u) == 3u)
        IntruderCount = IntruderCount - 1;
}

// ---------------------------------------------------------------------------
// LAB_06cf3aa0 -- PhySys::Controller slot 4 on +0x60.
// ---------------------------------------------------------------------------
void Mine::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    (void)CoreList;

    // Nothing unless update found a target.
    if (!bSeeking)
        return;

    Vector const Velocity = get_velocity();
    Vector const& Pos = get_position();
    Vector Dir;
    Dir.z = static_cast<float>(static_cast<double>(Target.z) - static_cast<double>(Pos.z));
    Dir.y = static_cast<float>(static_cast<double>(Target.y) - static_cast<double>(Pos.y));
    Dir.x = static_cast<float>(static_cast<double>(Target.x) - static_cast<double>(Pos.x));

    // No zero-length test.
    double const LengthSq = (static_cast<double>(Dir.z) * static_cast<double>(Dir.z) +
                             static_cast<double>(Dir.y) * static_cast<double>(Dir.y)) +
                            static_cast<double>(Dir.x) * static_cast<double>(Dir.x);
    double const Inverse = 1.0 / std::sqrt(LengthSq);
    Dir.x = static_cast<float>(static_cast<double>(Dir.x) * Inverse);
    Dir.y = static_cast<float>(static_cast<double>(Dir.y) * Inverse);
    Dir.z = static_cast<float>(static_cast<double>(Dir.z) * Inverse);

    // The speed already along it.
    float const Speed = static_cast<float>(
        (static_cast<double>(Dir.z) * static_cast<double>(Velocity.z) +
         static_cast<double>(Velocity.y) * static_cast<double>(Dir.y)) +
        static_cast<double>(Velocity.x) * static_cast<double>(Dir.x));

    // What is missing to TopSpeed (+0x90); stored as a float but the unrounded difference
    // is tested.
    Archetype::Mine const* const Arch =
        static_cast<Archetype::Mine const*>(Object->get_archetype());
    double const Missing = static_cast<double>(Arch->TopSpeed) - static_cast<double>(Speed);
    float Delta = static_cast<float>(Missing);
    if (!(Missing > 0.0))
        return;

    if (!(Arch->TopSpeed > Delta))
        Delta = Arch->TopSpeed;

    // Also capped at Acceleration (+0x8c) * dt, unless Acceleration is exactly -1.0f
    // (.rdata:06d6572c).
    float const Dt = EventSim->delta_time;
    if (!(Arch->Acceleration == -1.0f))
    {
        double const Cap = static_cast<double>(Arch->Acceleration) * static_cast<double>(Dt);
        if (static_cast<double>(Delta) > Cap)
            Delta = static_cast<float>(Cap);
    }

    // Drag-compensated push; LinearDrag is Mine+0x80, Mass Root+0x20.
    double const DragDt = (static_cast<double>(Arch->LinearDrag) / static_cast<double>(Arch->Mass)) *
                          static_cast<double>(Dt);
    double const K = (static_cast<double>(Delta) + static_cast<double>(Speed)) * DragDt +
                     static_cast<double>(Delta);

    // Written straight into the physics core's linear velocity (CObject+0x54 body, +0x98
    // core, +0xa4 speed), no null tests.
    IVP_Real_Object* const Body = static_cast<IVP_Real_Object*>(Object->BodyPtr);
    Vector& CoreSpeed = Body->core->Speed;
    CoreSpeed.x = static_cast<float>(static_cast<double>(Dir.x) * K + static_cast<double>(CoreSpeed.x));
    CoreSpeed.y = static_cast<float>(static_cast<double>(Dir.y) * K + static_cast<double>(CoreSpeed.y));
    CoreSpeed.z = static_cast<float>(static_cast<double>(Dir.z) * K + static_cast<double>(CoreSpeed.z));
}

// ---------------------------------------------------------------------------
// LAB_06cf4230 -- vtable +0x17c (Mine and Guided).
// Any sub-object hit destroys the projectile (DamageList byte +0xc).
// ---------------------------------------------------------------------------
void Mine::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    Damage.bDestroyed = true;
}

// ---------------------------------------------------------------------------
// LAB_06cf4240 -- vtable +0x180 (Mine and Guided).
// ---------------------------------------------------------------------------
void Mine::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    // A seeker munition (Munition+0x90 != 0) always kills.
    if (Hit.Munition->Seeker != 0)
    {
        Damage.bDestroyed = true;
        return;
    }
    // Tail-jumps to GameObject's body (FUN_06ceeb40).
    GameObject::munition_hit(Hit, Damage);
}
