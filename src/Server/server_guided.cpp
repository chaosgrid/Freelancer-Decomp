// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Server.dll Guided (missile) and Bullet (gun round) wrappers -- retail .text:06cf3ce0-06cf4ecf.
// The out-of-line Vector helpers (FUN_06cf4960 .. FUN_06cf4a40) are inlined here; the two
// damage bodies Guided shares with Mine (.text:06cf4230 / 06cf4240) are in server_mine.cpp.
//
// Guided layout: Munition/Projectile/GameObject +0x00..+0x5f, PhySys::Controller +0x60
// (vtable .rdata:06d66700), PhySys::CObjectCB +0x70 (vtable .rdata:06d664a4), then
// +0x74..+0x7f; size 0x80 from the spawner (StarSystem FUN_06d09640).
// Bullet has no field of its own (operator new(0x60) in FUN_06d099e0).

#include "../include/server_gameobject.h"
#include "../include/server_sysdb.h"       // StarSystem::explode / munition_collision / find_object
#include "../include/server_createinfo.h"  // Server_Acquire3DMathEngine (FUN_06d5ff70)
#include "../include/x86math.h"            // I3DMathEngine::TransformMatrix
#include "../include/common_misc.h"        // SUBOBJ_ID_ROOT, DamageList
#include "../include/common_id.h"          // ID_String::IsEmpty
#include "../include/common_csys.h"        // Matrix, Transform (get_transform)
#include "../include/DALib.h"              // DALib::Engine (create, IEngine +0x6c)
#include "../include/common_ivp_controller.h" // IVP_Event_Sim::delta_time

#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// FUN_06cf4a90: Matrix * Vector through I3DMathEngine::TransformMatrix (vt+0x2c). The
// engine is fetched once (FUN_06d5ff70) and never null-tested.
// ---------------------------------------------------------------------------
static Vector Guided_TransformMatrix(Matrix const& M, Vector const& V)
{
    Vector Tmp;
    Server_MathEngine()->TransformMatrix(Tmp, M, V);
    Vector Out;
    Out.x = Tmp.x;
    Out.y = Tmp.y;
    Out.z = Tmp.z;
    return Out;
}

// ===========================================================================
// Guided
// ===========================================================================

// ---------------------------------------------------------------------------
// FUN_06cf3ce0. Fields: +0x74 Phantom, byte +0x78 bTriggered, +0x7c TriggerObjId.
// ---------------------------------------------------------------------------
Guided::Guided()
    : Phantom(nullptr)
    , bTriggered(false)
    , TriggerObjId(0)
{
}

// ---------------------------------------------------------------------------
// FUN_06cf3db0, entered on the CBase sub-object (+0xc).
// ---------------------------------------------------------------------------
Guided::~Guided()
{
    // Retail calls the +0x15c slot of Guided's own table (.rdata:06d66698), which from the
    // destructor resolves to GameObject's destroy_notify (FUN_06cee670).
    GameObject::destroy_notify();

    // No null test on the phantom.
    PhySys::DestroyPhantom(Phantom);
    Phantom = nullptr;
}

// LAB_06cf3d70.
unsigned int Guided::get_type_bits() const
{
    return 0x1000000u;
}

CGuided* Guided::cguided() const
{
    CObject* const Obj = Object;
    if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassGuided) != CObject::ClassGuided)
        return nullptr;
    return static_cast<CGuided*>(Obj);
}

// ---------------------------------------------------------------------------
// LAB_06cf3e50 -- vtable +0x164.
// ---------------------------------------------------------------------------
bool Guided::create(Archetype::Root* Arch)
{
    Object = CObject::Alloc(CObject::ClassGuided);

    // create_instance runs before open.
    Arch->create_instance(static_cast<IEngineInstance*>(Object));

    DALib::Engine->UpdateInstance(Object->EngineInstanceId, 0, nullptr);

    // The CGuided narrowing is not null-tested before open (vt+0x4c).
    CObject* Narrowed = Object;
    if (Narrowed == nullptr || (static_cast<unsigned>(Narrowed->ObjectClass) & CObject::ClassGuided) != CObject::ClassGuided)
        Narrowed = nullptr;
    Narrowed->open(Arch);

    advise(true);

    return get_index() != -1;
}

// ---------------------------------------------------------------------------
// LAB_06cf3ed0 -- vtable +0x168.
// ---------------------------------------------------------------------------
bool Guided::find(unsigned int const& Id)
{
    Object = CObject::Find(Id, CObject::ClassGuided);
    advise(true);
    return Object != nullptr;
}

// ---------------------------------------------------------------------------
// LAB_06cf4310 -- vtable +0x1bc.
// ---------------------------------------------------------------------------
void Guided::arm()
{
    PhySys::Controller::Register(static_cast<PhySys::Controller*>(this), Object);

    // A proximity phantom only for a strictly positive detonation distance (Munition+0xa4,
    // squared).
    Archetype::Munition const* const Arch =
        static_cast<Archetype::Munition const*>(Object->get_archetype());
    if (!(Arch->DetonationDistSq > 0.0f))
        return;

    PhySys::PhantomParms Parms;
    Parms.SystemId = 0;
    Parms.bPhysicalUnmoveable = 0;
    Parms.Flag05 = 0;
    Parms.Name[0] = '\0';
    Parms.Position.x = 0.0f;
    Parms.Position.y = 0.0f;
    Parms.Position.z = 0.0f;
    Parms.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                               0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 1.0f);
    Parms.Listener = nullptr;

    // FUN_06d06cd0, the system's nickname id.
    Parms.SystemId = OwnerSystem->SystemInfo->NicknameId;

    // The group name is passed as the format string, as retail does.
    std::sprintf(Parms.Name, PhySys::GetCollisionGroup(Object));

    // The CObjectCB sub-object (+0x70) is the listener.
    Parms.Position = get_position();
    Parms.Listener = static_cast<PhySys::CObjectCB*>(this);
    Parms.Flag05 = 1;

    float const Radius = static_cast<float>(std::sqrt(static_cast<double>(Arch->DetonationDistSq)));
    PhySys::CreatePhantom(Parms, Radius, Phantom);
    PhySys::AttachPhantom(Object, Phantom);
}

// ---------------------------------------------------------------------------
// LAB_06cf3f10 -- vtable +0x16c.
// ---------------------------------------------------------------------------
bool Guided::update(float Dt)
{
    if (!GameObject::update(Dt))
        return false;

    // The narrowing is not null-tested.
    CGuided* const Cg = cguided();
    Cg->update(Dt, CObject::UPDATE_DEFAULT);

    // A spent lifetime (CProjectile+0xe8 below zero, or unordered) kills.
    if (!(Cg->Lifetime >= 0.0f))
    {
        destroy(1, 0);
        return !is_destroyed();
    }

    // The phantom's collisions go on first; once they are on, a touch latched by added()
    // detonates.
    if (!PhySys::GetCollisionStatePhantom(Phantom))
        PhySys::SetCollisionStatePhantom(Phantom, true);
    else if (bTriggered)
        destroy(1, TriggerObjId);

    wakeup();

    // Only a lock-on seeker (Munition+0x90 == 2) steers; retail tests zero, then two.
    Archetype::Munition const* const Arch =
        static_cast<Archetype::Munition const*>(Object->get_archetype());
    if (Arch->Seeker == 0)
        return !is_destroyed();
    if (Arch->Seeker != 2)
        return !is_destroyed();

    // ... and only with a target (CGuided+0xf0, the watched IObjRW).
    if (cguided()->get_target() == nullptr)
        return !is_destroyed();

    // Aim point: a sub-targeted CEqObj gives its sub-object's centre of mass (CEqObj
    // vt+0xe0); otherwise, or when that fails, the target's centre of mass. Retail leaves
    // the slot unwritten if the re-read finds no target (ruled out above); the port zeroes it.
    Vector TargetPos;
    TargetPos.x = 0.0f;
    TargetPos.y = 0.0f;
    TargetPos.z = 0.0f;
    IObjRW* const PosTarget = cguided()->get_target();
    if (PosTarget != nullptr)
    {
        bool bHavePos = false;
        CObject const* const TargetObj = PosTarget->cobject();                     // vt+0x150
        if (TargetObj != nullptr &&
            (static_cast<unsigned>(TargetObj->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
        {
            unsigned short const SubTarget = cguided()->SubTarget;
            if (SUBOBJ_ID_ROOT != SubTarget)
            {
                bHavePos = static_cast<CEqObj const*>(TargetObj)->get_sub_obj_center_of_mass(
                    cguided()->SubTarget, TargetPos);
            }
        }
        if (!bHavePos)
            TargetPos = PosTarget->get_center_of_mass();
    }

    if (cguided()->seeker_can_see(TargetPos))
    {
        // Same shape for velocity: sub-object velocity (CEqObj vt+0xdc), else the target's.
        Vector TargetVel;
        TargetVel.x = 0.0f;
        TargetVel.y = 0.0f;
        TargetVel.z = 0.0f;
        IObjRW* const VelTarget = cguided()->get_target();
        if (VelTarget != nullptr)
        {
            bool bHaveVel = false;
            CObject const* const TargetObj = VelTarget->cobject();
            if (TargetObj != nullptr &&
                (static_cast<unsigned>(TargetObj->ObjectClass) & CObject::ClassEqObj) == CObject::ClassEqObj)
            {
                unsigned short const SubTarget = cguided()->SubTarget;
                if (SUBOBJ_ID_ROOT != SubTarget)
                {
                    bHaveVel = static_cast<CEqObj const*>(TargetObj)->get_sub_obj_velocity(
                        cguided()->SubTarget, TargetVel);
                }
            }
            if (!bHaveVel)
                TargetVel = VelTarget->get_velocity();
        }

        seek(TargetPos, TargetVel);
    }
    else
    {
        // Lost sight: this Guided's own set_target (IObjAffect vt+0x48).
        set_target(nullptr, SUBOBJ_ID_ROOT, 0);
    }

    return !is_destroyed();
}

// ---------------------------------------------------------------------------
// FUN_06cf45a0. Every out-of-line Vector helper (FUN_06cf49e0 subtract, FUN_06cf4a10 scale,
// FUN_06cf4960 length, FUN_06cf4990 negate, FUN_06cf49b0 add, FUN_06ce7c10 construct)
// stores floats, so each intermediate is rounded to float where retail stores it.
// ---------------------------------------------------------------------------
void Guided::seek(Vector const& TargetPos, Vector const& TargetVel)
{
    Vector const& MyPos = get_position();
    Vector RelPos;
    RelPos.x = static_cast<float>(static_cast<double>(TargetPos.x) - static_cast<double>(MyPos.x));
    RelPos.y = static_cast<float>(static_cast<double>(TargetPos.y) - static_cast<double>(MyPos.y));
    RelPos.z = static_cast<float>(static_cast<double>(TargetPos.z) - static_cast<double>(MyPos.z));

    Vector const MyVel = get_velocity();
    Vector RelVel;
    RelVel.x = static_cast<float>(static_cast<double>(TargetVel.x) - static_cast<double>(MyVel.x));
    RelVel.y = static_cast<float>(static_cast<double>(TargetVel.y) - static_cast<double>(MyVel.y));
    RelVel.z = static_cast<float>(static_cast<double>(TargetVel.z) - static_cast<double>(MyVel.z));

    // Turn only while the target closes in (strictly negative; unordered does not turn).
    // The summation order (z, y, then x) is retail's.
    double const Closing =
        (static_cast<double>(RelVel.z) * static_cast<double>(RelPos.z) +
         static_cast<double>(RelVel.y) * static_cast<double>(RelPos.y)) +
        static_cast<double>(RelVel.x) * static_cast<double>(RelPos.x);
    if (Closing < 0.0)
    {
        // RelPos x RelVel, computed z, then y, then x.
        Vector Cross;
        Cross.z = static_cast<float>(static_cast<double>(RelVel.y) * static_cast<double>(RelPos.x) -
                                     static_cast<double>(RelPos.y) * static_cast<double>(RelVel.x));
        Cross.y = static_cast<float>(static_cast<double>(RelPos.z) * static_cast<double>(RelVel.x) -
                                     static_cast<double>(RelVel.z) * static_cast<double>(RelPos.x));
        Cross.x = static_cast<float>(static_cast<double>(RelVel.z) * static_cast<double>(RelPos.y) -
                                     static_cast<double>(RelPos.z) * static_cast<double>(RelVel.y));

        // No turn below 0.0001f (.rdata:06d66968) or unordered.
        double const DistSq =
            (static_cast<double>(RelPos.z) * static_cast<double>(RelPos.z) +
             static_cast<double>(RelPos.y) * static_cast<double>(RelPos.y)) +
            static_cast<double>(RelPos.x) * static_cast<double>(RelPos.x);
        if (DistSq >= static_cast<double>(0.0001f))
        {
            // Line-of-sight rate; the reciprocal is rounded to float first.
            float const InvDistSq = static_cast<float>(1.0 / DistSq);
            Vector Omega;
            Omega.x = static_cast<float>(static_cast<double>(InvDistSq) * static_cast<double>(Cross.x));
            Omega.y = static_cast<float>(static_cast<double>(InvDistSq) * static_cast<double>(Cross.y));
            Omega.z = static_cast<float>(static_cast<double>(InvDistSq) * static_cast<double>(Cross.z));

            // Navigation gain 3.0f (.rdata:06d6649c).
            Vector Desired;
            Desired.x = static_cast<float>(3.0 * static_cast<double>(Omega.x));
            Desired.y = static_cast<float>(3.0 * static_cast<double>(Omega.y));
            Desired.z = static_cast<float>(3.0 * static_cast<double>(Omega.z));

            // Summed y, z, then x here (unlike the normalise below).
            float const Magnitude = static_cast<float>(std::sqrt(
                (static_cast<double>(Desired.y) * static_cast<double>(Desired.y) +
                 static_cast<double>(Desired.z) * static_cast<double>(Desired.z)) +
                static_cast<double>(Desired.x) * static_cast<double>(Desired.x)));

            // Only a strictly larger rate is clamped to MaxAngularVelocity (Munition+0xa0).
            Archetype::Munition const* const Arch =
                static_cast<Archetype::Munition const*>(Object->get_archetype());
            if (Magnitude > Arch->MaxAngularVelocity)
            {
                // Normalise (FUN_06cf4960); the reciprocal stays on the FPU unrounded.
                double const Length = std::sqrt(
                    (static_cast<double>(Desired.x) * static_cast<double>(Desired.x) +
                     static_cast<double>(Desired.y) * static_cast<double>(Desired.y)) +
                    static_cast<double>(Desired.z) * static_cast<double>(Desired.z));
                double const Inverse = 1.0 / Length;
                Desired.x = static_cast<float>(static_cast<double>(Desired.x) * Inverse);
                Desired.y = static_cast<float>(static_cast<double>(Desired.y) * Inverse);
                Desired.z = static_cast<float>(static_cast<double>(Desired.z) * Inverse);

                double const MaxRate = static_cast<double>(Arch->MaxAngularVelocity);
                Desired.x = static_cast<float>(MaxRate * static_cast<double>(Desired.x));
                Desired.y = static_cast<float>(MaxRate * static_cast<double>(Desired.y));
                Desired.z = static_cast<float>(MaxRate * static_cast<double>(Desired.z));
            }

            Vector const AngularVelocity = PhySys::GetAngularVelocityOS(Object);
            Vector NegAngular;
            NegAngular.x = -AngularVelocity.x;
            NegAngular.y = -AngularVelocity.y;
            NegAngular.z = -AngularVelocity.z;

            // Transpose of CObject+0x08 (FUN_06cf4a40).
            Matrix const& Orientation = Object->Orientation;
            Matrix const Transposed(Orientation.E[0][0], Orientation.E[1][0], Orientation.E[2][0],
                                    Orientation.E[0][1], Orientation.E[1][1], Orientation.E[2][1],
                                    Orientation.E[0][2], Orientation.E[1][2], Orientation.E[2][2]);

            // The wanted rate in object space.
            Vector const LocalRate = Guided_TransformMatrix(Transposed, Desired);

            // Adding (wanted - current) sets the object-space angular velocity to the wanted one.
            Vector Change;
            Change.x = static_cast<float>(static_cast<double>(NegAngular.x) + static_cast<double>(LocalRate.x));
            Change.y = static_cast<float>(static_cast<double>(NegAngular.y) + static_cast<double>(LocalRate.y));
            Change.z = static_cast<float>(static_cast<double>(NegAngular.z) + static_cast<double>(LocalRate.z));
            PhySys::AddToAngularVelocityOS(Object, Change);
        }
    }

    // The nose is minus the third rotation column.
    Vector const Velocity = get_velocity();
    Transform const& Xform = get_transform();
    Vector Nose;
    Nose.z = -Xform.Rotation.E[2][2];
    Nose.y = -Xform.Rotation.E[1][2];
    Nose.x = -Xform.Rotation.E[0][2];

    // Nothing unless flying nose first (strictly positive; double 0.0 at .rdata:06d65538).
    double const Along =
        (static_cast<double>(Nose.z) * static_cast<double>(Velocity.z) +
         static_cast<double>(Nose.y) * static_cast<double>(Velocity.y)) +
        static_cast<double>(Velocity.x) * static_cast<double>(Nose.x);
    if (!(Along > 0.0))
        return;

    float const Speed = static_cast<float>(std::sqrt(
        (static_cast<double>(Velocity.z) * static_cast<double>(Velocity.z) +
         static_cast<double>(Velocity.y) * static_cast<double>(Velocity.y)) +
        static_cast<double>(Velocity.x) * static_cast<double>(Velocity.x)));

    // Retail fetches the transform again.
    Transform const& Xform2 = get_transform();
    Vector Forward;
    Forward.x = -Xform2.Rotation.E[0][2];
    Forward.y = -Xform2.Rotation.E[1][2];
    Forward.z = -Xform2.Rotation.E[2][2];

    Vector Wanted;
    Wanted.z = static_cast<float>(static_cast<double>(Forward.z) * static_cast<double>(Speed));
    Wanted.y = static_cast<float>(static_cast<double>(Forward.y) * static_cast<double>(Speed));
    Wanted.x = static_cast<float>(static_cast<double>(Forward.x) * static_cast<double>(Speed));

    Vector NegVelocity;
    NegVelocity.z = -Velocity.z;
    NegVelocity.y = -Velocity.y;
    NegVelocity.x = -Velocity.x;

    // The whole speed is put back on the nose.
    Vector Change;
    Change.z = static_cast<float>(static_cast<double>(Wanted.z) + static_cast<double>(NegVelocity.z));
    Change.y = static_cast<float>(static_cast<double>(Wanted.y) + static_cast<double>(NegVelocity.y));
    Change.x = static_cast<float>(static_cast<double>(Wanted.x) + static_cast<double>(NegVelocity.x));
    PhySys::AddToVelocity(Object, Change);
}

// ---------------------------------------------------------------------------
// LAB_06cf4450 -- vtable +0x158.
// ---------------------------------------------------------------------------
bool Guided::destroy(int Kind, unsigned int ObjId)
{
    // GameObject's destroy (FUN_06cee630); its result is not used.
    GameObject::destroy(Kind, ObjId);

    // Explode only on a real kill, and not one by a countermeasure (type mask 0x8000000).
    if (Kind != 1)
        return true;
    if (OwnerSystem->find_object(ObjId, 0x8000000u) != nullptr)
        return true;

    // Needs an explosion with a radius and some effect (double 0.0001 at .rdata:06d65190).
    Archetype::Root* const Arch = Object->get_archetype();
    if (Arch->ExplosionArch.IsEmpty())
        return true;
    Archetype::Explosion* const Explosion = Archetype::GetExplosion(Arch->ExplosionArch);
    if (Explosion == nullptr)
        return true;
    double const Epsilon = 0.0001;
    if (!(static_cast<double>(Explosion->Radius) > Epsilon))
        return true;
    if (!(static_cast<double>(Explosion->Impulse) > Epsilon) &&
        !(static_cast<double>(Explosion->HullDamage) > Epsilon) &&
        !(static_cast<double>(Explosion->EnergyDamage) > Epsilon))
        return true;

    // Owner is CGuided+0xe4 (narrowing not null-tested). Cause 6 for a cruise disruptor
    // (Munition+0xac), else 5; retail first stores 4, then overwrites it.
    ExplosionDamageEvent Blast;
    Blast.ExplodingObjId = get_id();
    Blast.InflictorId = cguided()->OwnerId;
    Archetype::Munition const* const MunitionArch =
        static_cast<Archetype::Munition const*>(Object->get_archetype());
    Blast.Cause = MunitionArch->bCruiseDisruptor ? DamageCause::CruiseDisrupter : DamageCause::MissileTorpedo;
    Blast.Position = get_position();
    Blast.Explosion = Explosion;
    Blast.SkipCollisionGroup = nullptr;
    OwnerSystem->explode(Blast);

    return true;
}

// .text:06cf4230 -- Guided's +0x17c, the body Mine::damage_sub_obj ports.
void Guided::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    Damage.bDestroyed = true;
}

// .text:06cf4240 -- Guided's +0x180, the body Mine::munition_hit ports.
void Guided::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    if (Hit.Munition->Seeker != 0)
    {
        Damage.bDestroyed = true;
        return;
    }
    GameObject::munition_hit(Hit, Damage);
}

// ---------------------------------------------------------------------------
// LAB_06cf4270 -- vtable +0x1a4.
// ---------------------------------------------------------------------------
void Guided::on_collision(FCollisionEvent const& Hit)
{
    if (Hit.Other->get_id() == 0)
        return;

    // Not null-tested.
    CGuided* const Cg = cguided();

    // A player-owned object (CSimple+0xb4; narrowing not null-tested) handles the hit on
    // its own client.
    if (Hit.Other->csimple()->OwnerPlayer != 0)
        return;

    // StarSystem::munition_collision (FUN_06d0d500). Retail never writes the record's
    // +0x10..+0x1b (hit position); the port zeroes it.
    SSPMunitionCollisionInfo Info = {};
    Info.ProjectileArchId = Object->get_archetype()->Nickname.Id;
    Info.AttackerId = Cg->OwnerId;
    Info.ObjectId = Hit.Other->get_id();
    Info.SubObjId = Hit.OtherSubObjId;
    OwnerSystem->munition_collision(Info);
}

// ---------------------------------------------------------------------------
// LAB_06cf4ae0 -- CObjectCB slot 0 on the +0x70 sub-object: something entered the
// proximity phantom.
// ---------------------------------------------------------------------------
void Guided::added(CObject* Obj)
{
    CObject* const Root = GetRoot(Obj);
    unsigned int Id = 0;
    if (Root != nullptr && (static_cast<unsigned>(Root->ObjectClass) & 3u) == 3u)
        Id = static_cast<CSimple*>(Root)->Id;

    // The owner is exempt while its owner-safe time runs (narrowing not null-tested).
    CGuided* const Cg = cguided();
    if (Id != 0 && Id == Cg->OwnerId && Cg->is_owner_safe())
        return;

    bTriggered = true;
    TriggerObjId = Id;
}

// .text:06ce1050 -- CObjectCB slot 1 of Guided's table: nothing.
void Guided::deleted(CObject* Obj)
{
    (void)Obj;
}

// ---------------------------------------------------------------------------
// LAB_06cf4b50 -- PhySys::Controller slot 4 on +0x60.
// ---------------------------------------------------------------------------
void Guided::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    (void)CoreList;

    // Only while the motor burns: MotorData (CGuided+0xfc) set and SeekerElapsed (+0x100)
    // within [Delay (+8), Delay + Lifetime (+0x10)]; an unordered compare also stops it.
    CGuided* const Cg = cguided();
    Archetype::MotorData const* const Motor = Cg->MotorArch;
    if (Motor == nullptr)
        return;
    if (!(Cg->SeekerElapsed >= Motor->Delay))
        return;
    double const BurnEnd = static_cast<double>(Motor->Lifetime) + static_cast<double>(Motor->Delay);
    if (!(BurnEnd >= static_cast<double>(Cg->SeekerElapsed)))
        return;

    // Retail re-narrows to re-read the motor.
    Archetype::MotorData const* const Motor2 = cguided()->MotorArch;
    Transform const& Xform = get_transform();
    Vector Axis;
    Axis.x = Xform.Rotation.E[0][2];
    Axis.y = Xform.Rotation.E[1][2];
    Axis.z = Xform.Rotation.E[2][2];

    // Accel is MotorData+0xc.
    float const Mass = get_mass();
    float const Amount = static_cast<float>(
        -((static_cast<double>(Mass) * static_cast<double>(Motor2->Accel)) *
          static_cast<double>(EventSim->delta_time)));
    push(Axis, Amount);
}

// ---------------------------------------------------------------------------
// LAB_06cf4c10 -- IObjAffect +0x48 on the +4 sub-object.
// ---------------------------------------------------------------------------
int Guided::set_target(IObjRW* Target, unsigned short SubTarget, int SourceClient)
{
    if (SourceClient == -1)
        SourceClient = 0;

    // A non-seeking munition refuses (narrowing not null-tested).
    if (cguided()->munitionarch()->Seeker == 0)
        return -3;

    // In single player a change from a client is only relayed.
    bool bChanged = false;
    if (!SinglePlayer() || SourceClient == 0)
    {
        // CGuided's set_target / set_sub_target are inlined in retail.
        CGuided* const Cg = cguided();
        if (Cg->get_target() != Target || Cg->SubTarget != SubTarget)
        {
            bChanged = true;
            Cg->set_target(Target);
            Cg->set_sub_target(SubTarget);
        }

        if (SourceClient == 0 && !bChanged)
            return 0;
    }

    // StarSystem FUN_06d0e670 -- IClient XSetTarget.
    unsigned int const ObjId = get_id();
    OwnerSystem->notify_set_target(ObjId, SUBOBJ_ID_ROOT, Target, SubTarget, SourceClient);

    return 0;
}

// ===========================================================================
// Bullet
// ===========================================================================

// .text:06d09b90.
unsigned int Bullet::get_type_bits() const
{
    return 0x2000000u;
}

CBeam* Bullet::cbeam() const
{
    CObject* const Obj = Object;
    if (Obj == nullptr || (static_cast<unsigned>(Obj->ObjectClass) & CObject::ClassBeam) != CObject::ClassBeam)
        return nullptr;
    return static_cast<CBeam*>(Obj);
}

// ---------------------------------------------------------------------------
// FUN_06cf4d40 -- vtable +0x164. Same shape as Guided::create.
// ---------------------------------------------------------------------------
bool Bullet::create(Archetype::Root* Arch)
{
    Object = CObject::Alloc(CObject::ClassBeam);

    Arch->create_instance(static_cast<IEngineInstance*>(Object));

    DALib::Engine->UpdateInstance(Object->EngineInstanceId, 0, nullptr);

    // The CBeam narrowing is not null-tested before open.
    CObject* Narrowed = Object;
    if (Narrowed == nullptr || (static_cast<unsigned>(Narrowed->ObjectClass) & CObject::ClassBeam) != CObject::ClassBeam)
        Narrowed = nullptr;
    Narrowed->open(Arch);

    advise(true);

    return get_index() != -1;
}

// .text:06d02cb0 -- the Bullet vtable's +0x168.
bool Bullet::find(unsigned int const& Id)
{
    (void)Id;
    return false;
}

// ---------------------------------------------------------------------------
// FUN_06cf4dc0 -- vtable +0x16c.
// ---------------------------------------------------------------------------
bool Bullet::update(float Dt)
{
    if (!GameObject::update(Dt))
        return false;

    // Not null-tested.
    CBeam* const Cb = cbeam();
    Cb->update(Dt, CObject::UPDATE_DEFAULT);

    // A spent lifetime (CProjectile+0xe8 below zero, or unordered) removes it quietly.
    if (!(Cb->Lifetime >= 0.0f))
        destroy(0, 0);

    return !is_destroyed();
}

// .text:06d28880 -- Bullet's +0x17c: nothing.
void Bullet::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// .text:06d28880 -- Bullet's +0x180: nothing.
void Bullet::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// .text:06d28880 -- Bullet's +0x184: nothing.
void Bullet::explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage)
{
    (void)Blast;
    (void)Damage;
}

// .text:06d28880 -- Bullet's own +0x1bc: nothing.
void Bullet::Slot1bc(unsigned int Arg1, unsigned int Arg2)
{
    (void)Arg1;
    (void)Arg2;
}

// ---------------------------------------------------------------------------
// LAB_06cf4e40 -- vtable +0x1a4.
// ---------------------------------------------------------------------------
void Bullet::on_collision(FCollisionEvent const& Hit)
{
    // Owner is CBeam+0xe4 (narrowing not null-tested). Retail never writes the record's
    // +0x10..+0x1b (hit position); the port zeroes it.
    CBeam* const Cb = cbeam();
    SSPMunitionCollisionInfo Info = {};
    Info.ProjectileArchId = Object->get_archetype()->Nickname.Id;
    Info.AttackerId = Cb->OwnerId;
    Info.ObjectId = Hit.Other->get_id();
    Info.SubObjId = Hit.OtherSubObjId;
    OwnerSystem->munition_collision(Info);

    // The round is spent.
    destroy(1, Hit.Other->get_id());
}
