// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Physics/common_physys_internal.h"
#include "ivp_utility/ivp_core_trap.h"      // IVP_CoreTrap -- retail's `MOV [0],0`

using namespace CmnPhySys;

// ============================================================================
// Mass / Inertia
// ============================================================================

// FUN_062841c0. No pointer is tested. change_mass scales the inertia by NewMass / Mass, then
// the saved pre-scale inertia is written back: only the mass changes.
void __cdecl PhySys::SetMass(CObject* Object, float Mass)
{
    IVP_Core* const Core = Object->BodyPtr->core;
    Vector const SavedInertia = Core->RotInertia;
    Object->BodyPtr->change_mass(Mass);
    Object->BodyPtr->core->set_rotation_inertia(&SavedInertia);
}

// FUN_06284220. Nothing tested.
float __cdecl PhySys::GetMass(CObject const* Object)
{
    return Object->BodyPtr->core->Mass;
}

// FUN_06284240. Nothing tested.
void __cdecl PhySys::SetMomentOfInertia(CObject* Object, Vector const& Inertia)
{
    Vector const Copy = Inertia;
    Object->BodyPtr->core->set_rotation_inertia(&Copy);
}

// FUN_06284290. Nothing tested.
Vector __cdecl PhySys::GetMomentOfInertia(CObject const* Object)
{
    IVP_Core const* const Core = Object->BodyPtr->core;
    Vector Result;
    Result.x = Core->RotInertia.x;
    Result.y = Core->RotInertia.y;
    Result.z = Core->RotInertia.z;
    return Result;
}

// ============================================================================
// Damping
// ============================================================================

// FUN_06285570. Nothing tested.
void __cdecl PhySys::GetDamping(CObject const* Object, float& OutLinear, Vector& OutAngular)
{
    IVP_Core const* const Core = Object->BodyPtr->core;
    OutLinear = Core->SpeedDamp;
    OutAngular.x = Core->RotSpeedDamp.x;
    OutAngular.y = Core->RotSpeedDamp.y;
    OutAngular.z = Core->RotSpeedDamp.z;
}

// FUN_062855c0. The angular triple is copied as four dwords; retail's fourth (+0x30) is an
// unwritten stack slot, the port stores 0.
void __cdecl PhySys::SetDamping(CObject* Object, float Linear, Vector const& Angular)
{
    IVP_Core* const Core = Object->BodyPtr->core;
    Core->SpeedDamp = Linear;
    Core->RotSpeedDamp.x = Angular.x;
    Core->RotSpeedDamp.y = Angular.y;
    Core->RotSpeedDamp.z = Angular.z;
    Core->CenterOfMassX = 0.0f;               // garbage in retail
}

// ============================================================================
// Velocity / Angular velocity
// ============================================================================

Vector __cdecl PhySys::GetVelocity(CObject const* Object)
{
    // FUN_062857f0. Neither Object nor the body's core is tested.
    Vector Result;
    Result.x = 0.0f;
    Result.y = 0.0f;
    Result.z = 0.0f;

    if (!SinglePlayer())
    {
        // A CRemotePhysicsController (dynamic_cast, as retail's __RTDynamicCast) answers with
        // its own velocity; any other controller, or none, falls through to the core.
        IVP_Controller* const Rc = Object->GolemController;
        if (Rc != nullptr)
        {
            CRemotePhysicsController* const Remote = dynamic_cast<CRemotePhysicsController*>(Rc);
            if (Remote != nullptr)
                return Remote->m_Velocity;                                 // this+0x348
        }
    }

    // A null body gives zero.
    IVP_Real_Object const* const Body = Object->BodyPtr;
    if (Body != nullptr)
    {
        Result.x = Body->core->Speed.x;
        Result.y = Body->core->Speed.y;
        Result.z = Body->core->Speed.z;
    }
    return Result;
}

void __cdecl PhySys::AddToVelocity(CObject* Object, Vector const& Delta)
{
    // FUN_06285740: async_add_speed_object_ws (fourth dword unwritten). Nothing is tested.
    IVP_U_Point SpeedWs;
    SpeedWs.set(Delta.x, Delta.y, Delta.z);
    Object->BodyPtr->async_add_speed_object_ws(&SpeedWs);
}

// FUN_062857a0. Nothing tested.
Vector __cdecl PhySys::GetAngularVelocityOS(CObject const* Object)
{
    IVP_Core const* const Core = Object->BodyPtr->core;
    Vector Result;
    Result.x = Core->RotSpeed.x;
    Result.y = Core->RotSpeed.y;
    Result.z = Core->RotSpeed.z;
    return Result;
}

void __cdecl PhySys::AddToAngularVelocityOS(CObject* Object, Vector const& Delta)
{
    // FUN_06285770: the same shape as AddToVelocity, into async_add_rot_speed_object_cs.
    // Nothing is tested.
    IVP_U_Point RotSpeedCs;
    RotSpeedCs.set(Delta.x, Delta.y, Delta.z);
    Object->BodyPtr->async_add_rot_speed_object_cs(&RotSpeedCs);
}

// ============================================================================
// Impulses
// ============================================================================

void __cdecl PhySys::AddImpulseAtPoint(CObject* Object, Vector const& Impulse, Vector const& Point)
{
    // FUN_06285630: the fourth dword of each copy is left unwritten. Nothing is tested.
    IVP_U_Point ImpulseWs;
    ImpulseWs.set(Impulse.x, Impulse.y, Impulse.z);
    IVP_U_Point PointWs;
    PointWs.set(Point.x, Point.y, Point.z);
    Object->BodyPtr->async_push_object_ws(&PointWs, &ImpulseWs);
}

// FUN_06285680. Nothing tested.
void __cdecl PhySys::LinearImpulse(CObject* Object, Vector const& Direction, float Magnitude)
{
    Vector const Copy = Direction;
    Object->BodyPtr->core->push_core_linear(&Copy, Magnitude);
}

// FUN_062856c0
void __cdecl PhySys::AngularImpulse(CObject* Object, Vector const& Axis, float Magnitude)
{
    Vector const Copy = Axis;
    Object->BodyPtr->core->push_core_angular_ws(&Copy, Magnitude);
}

// FUN_06285700
void __cdecl PhySys::AngularImpulseOS(CObject* Object, Vector const& Axis, float Magnitude)
{
    Vector const Copy = Axis;
    Object->BodyPtr->core->push_core_angular_cs(&Copy, Magnitude);
}

// ============================================================================
// Activation
// ============================================================================

void __cdecl PhySys::Wakeup(CObject* Object)
{
    // FUN_06285620. Neither pointer is tested.
    Object->BodyPtr->EnsureInSimulation();
}

// ============================================================================
// Collision state
// ============================================================================

// FUN_062858c0: the manager is not tested.
bool __cdecl PhySys::GetCollisionState(CObject const* Object)
{
    if (Object->BodyPtr == nullptr)
        return false;
    return Object->CollisionStateMgr->get_desired_collision_state();
}

// FUN_062858e0
void __cdecl PhySys::SetCollisionState(CObject* Object, bool bEnable)
{
    if (Object->BodyPtr == nullptr)
        return;
    Object->CollisionStateMgr->enable_collisions(bEnable, true);
}

// FUN_06284e10. Nothing tested.
char const* __cdecl PhySys::GetCollisionGroup(CObject const* Object)
{
    return Object->BodyPtr->name;
}

// FUN_06284e20: then the body rechecks its collision filter. Nothing tested.
void __cdecl PhySys::SetCollisionGroup(CObject* Object, char const* const Group)
{
    IVP_Real_Object* const Body = Object->BodyPtr;
    safe_strcpy(Body->name, 8, Group);
    Object->BodyPtr->recheck_collision_filter();
}

// ============================================================================
// Center of mass / Radius
// ============================================================================

// FUN_06284570. No null tests.
Vector __cdecl PhySys::GetCenterOfMass(CObject const* Object)
{
    IVP_U_Matrix World;
    Object->BodyPtr->get_m_world_f_object_AT(&World);

    Vector Result;
    IVP_SurfaceManager* const Manager = Object->SurfaceManager;
    if (Manager == nullptr)
    {
        // The body origin.
        Result.x = World.vv.k[0];
        Result.y = World.vv.k[1];
        Result.z = World.vv.k[2];
        return Result;
    }

    // The surface's mass centre in object space.
    float Center[3];
    Manager->get_mass_center(Center);

    // x87: X0, Y0 and Z1 stay on the stack; Z0, X1 and Y1 are stored as floats; each result
    // is rounded once.
    IVP_U_Point const* const Rows = World.rows;
    double const X0 = (double)(Rows[0].k[0]) * Center[0];
    double const Y0 = (double)(Rows[1].k[0]) * Center[0];
    float const Z0 = (float)((double)(Rows[2].k[0]) * Center[0]);
    float const X1 = (float)((double)(Rows[0].k[1]) * Center[1] + X0);
    float const Y1 = (float)((double)(Rows[1].k[1]) * Center[1] + Y0);
    double const Z1 = (double)(Rows[2].k[1]) * Center[1] + Z0;
    Result.x = (float)((double)(Rows[0].k[2]) * Center[2] + X1 + World.vv.k[0]);
    Result.y = (float)((double)(Rows[1].k[2]) * Center[2] + Y1 + World.vv.k[1]);
    Result.z = (float)((double)(Rows[2].k[2]) * Center[2] + Z1 + World.vv.k[2]);
    return Result;
}

namespace
{
    // FUN_062842e0 (cdecl): the radius of Object's own body. False without a body. A body with
    // a phantom controller answers true and leaves *OutRadius alone. Otherwise *OutRadius =
    // extra_radius + the shape radius: object_type 2 asks the surface manager for the radius around
    // its mass centre, object_type 3 takes |shift_core_f_object|.
    bool PhySys_GetBodyRadius(CObject const* Object, float* OutRadius)
    {
        IVP_Real_Object* const Body = Object->BodyPtr;
        if (Body == nullptr)
            return false;
        if (Body->controller_phantom != nullptr)
            return true;

        float ShapeRadius = 0.0f;
        if (Body->object_type == 2)
        {
            float MassCenter[3];
            float RadiusDeviation;
            Object->SurfaceManager->get_mass_center(MassCenter);
            Object->SurfaceManager->get_radius_and_radius_dev_to_given_center(MassCenter, &ShapeRadius,
                                                                              &RadiusDeviation);
        }
        else if (Body->object_type == 3)
        {
            // x87: the length stays unrounded until stored.
            IVP_U_Point const& Shift = Body->shift_core_f_object;
            double const X = Shift.k[0];
            double const Y = Shift.k[1];
            double const Z = Shift.k[2];
            ShapeRadius = (float)(sqrt((X * X + Y * Y) + Z * Z));
        }
        else
        {
            // Retail's `MOV dword ptr [0],0`, a trap in every build.
            IVP_CoreTrap();
        }

        *OutRadius = (float)((double)(Body->extra_radius) + ShapeRadius);
        return true;
    }

    float PhySys_GetChildrenRadius(Vector const& Center, CObject const* Owner, long Instance,
                                   bool bIncludeChildren, uint32_t UseHullSlot);

    // FUN_06284390 (cdecl).
    // Retail quirk kept: the radius lives in the UseHull argument's own stack slot and the hull
    // flag in the low byte of the Object argument's slot. A path that writes neither
    // (GetBodyRadius without a body or for a phantom body, an unknown mode) keeps the argument
    // bits: the radius is UseHullSlot read as a float (1.4e-45f from GetRadiusR), the flag the
    // Object pointer's low byte. On the recursive calls those bits are an object address, so
    // the values differ from retail's.
    float PhySys_GetRadius(CObject const* Object, bool bIncludeChildren, uint32_t UseHullSlot, Vector& OutCenter)
    {
        float Radius;                                       // the UseHull slot
        memcpy(&Radius, &UseHullSlot, sizeof(Radius));
        uint32_t ObjectSlot = (uint32_t)(uintptr_t)Object; // its low byte is the flag
        switch (Object->PhysicsControlMode)
        {
        case PhySys::ANIM_TECHNIQUE_NONE:
            // With phantoms attached the body radius, else the core's.
            if (!Object->Phantoms.empty())
            {
                PhySys_GetBodyRadius(Object, &Radius);
                ObjectSlot = (ObjectSlot & ~0xffu) | 1u;
            }
            else
            {
                Radius = Object->BodyPtr->get_upper_limit_radius();
                ObjectSlot &= ~0xffu;
            }
            break;
        case PhySys::ANIM_TECHNIQUE_GOLEM:
            if (bIncludeChildren)
                Radius = Object->BodyPtr->get_upper_limit_radius();
            else
                Radius = 0.0f;
            ObjectSlot &= ~0xffu;
            break;
        case PhySys::ANIM_TECHNIQUE_MERGED:
            if ((uint8_t)UseHullSlot != 0)
            {
                PhySys_GetBodyRadius(Object, &Radius);
                ObjectSlot = (ObjectSlot & ~0xffu) | 1u;
            }
            else
            {
                Radius = 0.0f;
                ObjectSlot &= ~0xffu;
            }
            break;
        default:
            // Neither slot is written.
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kPhySysSource, 0xc8,
                  "PhySys::GetRadiusR(): Unknown anim_technique!");
            break;
        }

        OutCenter = PhySys::GetCenterOfMass(Object);

        // With children or the flag byte, the farthest child sphere wins when strictly larger;
        // the whole Object slot dword is passed as the callee's UseHull argument.
        if (bIncludeChildren || (uint8_t)ObjectSlot != 0)
        {
            float const ChildRadius = PhySys_GetChildrenRadius(OutCenter, Object, Object->EngineInstanceId,
                                                               bIncludeChildren, ObjectSlot);
            if (ChildRadius > Radius)
                return ChildRadius;
        }
        return Radius;
    }

    // FUN_06284480 (cdecl): the largest `child radius + distance(child centre, Center)` over
    // Instance's children; instances of Owner itself are descended into.
    float PhySys_GetChildrenRadius(Vector const& Center, CObject const* Owner, long Instance,
                                   bool bIncludeChildren, uint32_t UseHullSlot)
    {
        float Max = 0.0f;
        for (long Child = PhySys_RelatedInstance(Instance, 1, -1); Child != -1;
             Child = PhySys_RelatedInstance(Instance, 1, Child))
        {
            CObject const* const ChildObject = PhySys_InstanceObject(Child);
            double Candidate;
            if (ChildObject == Owner)
            {
                Candidate = PhySys_GetChildrenRadius(Center, Owner, Child, bIncludeChildren, UseHullSlot);
            }
            else
            {
                // x87: the squares summed z, x, then y, all unrounded.
                Vector ChildCenter;
                float const ChildRadius = PhySys_GetRadius(ChildObject, bIncludeChildren, UseHullSlot, ChildCenter);
                double const Dx = (double)(ChildCenter.x) - Center.x;
                double const Dy = (double)(ChildCenter.y) - Center.y;
                double const Dz = (double)(ChildCenter.z) - Center.z;
                Candidate = ChildRadius + sqrt((Dz * Dz + Dx * Dx) + Dy * Dy);
            }

            // Only a strictly greater candidate is taken.
            if (Candidate > Max)
                Max = (float)(Candidate);
        }
        return Max;
    }
}

// FUN_06284370
float __cdecl PhySys::GetRadiusR(CObject const* Object, bool bIncludeChildren, Vector& OutCenter)
{
    return PhySys_GetRadius(Object, bIncludeChildren, 1u, OutCenter);
}

// ============================================================================
// BeamR
// ============================================================================

namespace
{
    // The object's Orientation (+0x08) and WorldPosition (+0x2c) as one 12-float transform,
    // as retail copies it.
    Transform& PhySys_ObjectTransform(CObject* Object)
    {
        static_assert(offsetof(CObject, WorldPosition) == offsetof(CObject, Orientation) + sizeof(Matrix),
                      "CObject: Orientation and WorldPosition are one Transform");
        return *(Transform*)&Object->Orientation;
    }

    // FUN_06284f70, called with the nine elements pushed in column order: the transpose.
    Matrix PhySys_Transpose(Matrix const& M)
    {
        Matrix Out;
        for (int Row = 0; Row < 3; ++Row)
        {
            for (int Column = 0; Column < 3; ++Column)
                Out.E[Row][Column] = M.E[Column][Row];
        }
        return Out;
    }

    // FUN_06284fc0 (cdecl): beams Object's body to NewXform, carries every attached phantom
    // along (its offset kept in the object's old frame), then the object takes NewXform and its
    // jointed children are re-posed.
    void PhySys_BeamObject(CObject* Object, Transform const* NewXform, bool bOptimize)
    {
        I3DMathEngine* const Math = Acquire3DMathEngine();

        IVP_U_Point Position;
        Position.set(NewXform->Translation.x, NewXform->Translation.y, NewXform->Translation.z);

        // The engine's (W, X, Y, Z) stored as IVP's (x, y, z, w).
        Quaternion Quat;
        Math->MatrixToQuaternion(Quat, NewXform->Rotation);
        IVP_U_Quat Rotation;
        Rotation.x = Quat.x;
        Rotation.y = Quat.y;
        Rotation.z = Quat.z;
        Rotation.w = Quat.w;

        // The body is not tested.
        Object->BodyPtr->beam_object_to_new_position(&Rotation, &Position, bOptimize ? 1 : 0);

        // Each attached phantom keeps its offset: Local = transpose(old Orientation) *
        // (PhantomPos - old WorldPosition), NewPos = NewXform * Local.
        if (!Object->Phantoms.empty())
        {
            Matrix const InvRotation = PhySys_Transpose(Object->Orientation);
            for (std::list<void*>::iterator It = Object->Phantoms.begin(); It != Object->Phantoms.end(); ++It)
            {
                Vector PhantomPosition;
                PhySys_GetPhantomPosition(*It, &PhantomPosition);
                Vector Offset;
                Offset.x = (float)((double)(PhantomPosition.x) - Object->WorldPosition.x);
                Offset.y = (float)((double)(PhantomPosition.y) - Object->WorldPosition.y);
                Offset.z = (float)((double)(PhantomPosition.z) - Object->WorldPosition.z);

                Vector Local;
                Math->TransformMatrix(Local, InvRotation, Offset);

                Vector NewPosition;
                Math->TransformXform(NewPosition, *NewXform, Local);
                Vector Target;
                Target.x = NewPosition.x;
                Target.y = NewPosition.y;
                Target.z = NewPosition.z;
                PhySys_SetPhantomPosition(*It, &Target);
            }
        }

        PhySys_ObjectTransform(Object) = *NewXform;
        PhySys_UpdateJointedChildren(Object->EngineInstanceId, Object);
    }

    // FUN_06285190 (cdecl; PhySys::_BeamR_Children in PhyObj.cpp): a child whose user data is
    // Object itself is only recursed into. Any other child object gets ChildNew = NewXform *
    // (InvXform * ChildOld), then by its control mode: 1 -> the body is beamed, 2 -> the
    // transform is stored and the jointed children re-posed, else an error; then it is
    // recursed into. The child object is not tested for null.
    void PhySys_BeamR_Children(CObject* Object, long Instance, Transform const* NewXform,
                               Transform const* InvXform, bool bOptimize)
    {
        for (long ChildInstance = PhySys_RelatedInstance(Instance, 1, -1); ChildInstance != -1;
             ChildInstance = PhySys_RelatedInstance(Instance, 1, ChildInstance))
        {
            CObject* const ChildObject = PhySys_InstanceObject(ChildInstance);
            if (ChildObject == Object)
            {
                PhySys_BeamR_Children(Object, ChildInstance, NewXform, InvXform, bOptimize);
                continue;
            }

            I3DMathEngine* const Math = Acquire3DMathEngine();
            Transform const ChildOld = PhySys_ObjectTransform(ChildObject);
            Transform Relative;
            Math->MulTransform(Relative, *InvXform, ChildOld);
            Transform ChildNew;
            Math->MulTransform(ChildNew, *NewXform, Relative);

            if (ChildObject->PhysicsControlMode == PhySys::ANIM_TECHNIQUE_GOLEM)
            {
                PhySys_BeamObject(ChildObject, &ChildNew, bOptimize);
            }
            else if (ChildObject->PhysicsControlMode == PhySys::ANIM_TECHNIQUE_MERGED)
            {
                PhySys_ObjectTransform(ChildObject) = ChildNew;
                PhySys_UpdateJointedChildren(ChildInstance, ChildObject);
            }
            else
            {
                FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s", kPhyObjSource, 0x21d,
                      "Unknown anim_technique in PhySys::_BeamR_Children()");
            }

            PhySys_BeamR_Children(ChildObject, ChildInstance, NewXform, InvXform, bOptimize);
        }
    }
}

// FUN_06284e50
void __cdecl PhySys::BeamR(CObject* Object, Vector const& Pos, Matrix const& Orient, bool bFlag)
{
    Transform NewXform;
    NewXform.Rotation = Orient;
    NewXform.Translation.x = Pos.x;
    NewXform.Translation.y = Pos.y;
    NewXform.Translation.z = Pos.z;

    // The inverse of Object's current transform. Object is not tested.
    Vector Rotated;
    Acquire3DMathEngine()->TransposeTransform(Rotated, Object->Orientation, Object->WorldPosition);
    Transform InvXform;
    InvXform.Rotation = PhySys_Transpose(Object->Orientation);
    InvXform.Translation.x = -Rotated.x;
    InvXform.Translation.y = -Rotated.y;
    InvXform.Translation.z = -Rotated.z;

    PhySys_BeamObject(Object, &NewXform, bFlag);
    PhySys_BeamR_Children(Object, Object->EngineInstanceId, &NewXform, &InvXform, bFlag);
}

namespace
{
    // FUN_06289c90 (cdecl): Template's ray against Object's body only; nearest hit first, at
    // most MaxHits, client-less hits skipped. Unlike PhysicalSystem::FindRayCollisions, x stays
    // on the x87 stack and no normal is written.
    int PhySys_FindRayIntersectObject(CObject* Object, IVP_Ray_Solver_Template const* Template,
                                      PhySys::RayHit* Hits, int MaxHits)
    {
        IVP_Ray_Solver_Min_Hash Solver(Template);
        Solver.check_ray_against_object(Object->BodyPtr);

        const float StartX = Template->ray_start_point.x;
        const float StartY = Template->ray_start_point.y;
        const float StartZ = Template->ray_start_point.z;
        const float DirX = Template->ray_normized_direction.x;
        const float DirY = Template->ray_normized_direction.y;
        const float DirZ = Template->ray_normized_direction.z;

        int Count = 0;
        PhySys::RayHit* Out = Hits;
        for (;;)
        {
            IVP_Ray_Hit* const Hit = (IVP_Ray_Hit*)Solver.min_hash.find_min_elem();
            if (Hit == nullptr || Count >= MaxHits)
                break;

            CObject* const Client = (CObject*)Hit->hit_real_object->client_data;
            if (Client != nullptr)
            {
                Out->Object = Client;

                // x87: x * distance stays unrounded; the y / z products are stored first.
                const double Distance = Hit->hit_distance;
                const double ScaledX = DirX * Distance;
                const float ScaledY = (float)(DirY * Distance);
                const float ScaledZ = (float)(DirZ * Distance);
                Out->Point.x = (float)(ScaledX + StartX);
                Out->Point.y = (float)((double)(ScaledY) + StartY);
                Out->Point.z = (float)((double)(ScaledZ) + StartZ);

                Out->PartId = (Hit->hit_compact_ledge != nullptr)
                                  ? (unsigned)Hit->hit_compact_ledge->ClientData
                                  : 0;
                ++Count;
                ++Out;
            }

            Solver.min_hash.remove(Hit);
        }
        return Count;
    }
}

int __cdecl PhySys::FindRayCollisions(unsigned SystemId, Vector const& From, Vector const& To,
                                       RayHit* Hits, int MaxHits)
{
    // FUN_06289c20: a miss returns 0.
    std::map<unsigned, PhysicalSystem>::iterator SystemIt = G_PhysicalSystems.find(SystemId);
    if (SystemIt == G_PhysicalSystems.end())
        return 0;

    return SystemIt->second.FindRayCollisions(From, To, Hits, MaxHits);
}

// FUN_06289ef0
int __cdecl PhySys::FindRayIntersect(CObject* Object, Vector const& From, Vector const& To,
                                      RayHit* Hits, int MaxHits)
{
    // x87: z is stored but its unrounded value is used for its own square, and InvLength * dz
    // stays on the stack until stored.
    const float Dx = (float)((double)(To.x) - From.x);
    const float Dy = (float)((double)(To.y) - From.y);
    const double DzExact = (double)(To.z) - From.z;
    const float Dz = (float)(DzExact);

    const double Length = sqrt((DzExact * Dz + (double)(Dx) * Dx) + (double)(Dy) * Dy);
    const double InvLength = 1.0 / Length;
    const float DirX = (float)(Dx * InvLength);
    const float DirY = (float)(Dy * InvLength);
    const float DirZ = (float)(InvLength * Dz);

    // Retail leaves both fourth floats as stack garbage; the port stores 0.
    IVP_Ray_Solver_Template Template;
    Template.ray_start_point.set(From.x, From.y, From.z);
    Template.ray_start_point.hesse = 0.0f;
    Template.ray_normized_direction.set(DirX, DirY, DirZ);
    Template.ray_normized_direction.hesse = 0.0f;
    Template.ray_length = (float)(Length);
    Template.ray_flags = 0;

    return PhySys_FindRayIntersectObject(Object, &Template, Hits, MaxHits);
}

// FUN_06289ff0 (cdecl). No miss test, unlike PhySys::FindRayCollisions: a miss queries the
// head node's unconstructed storage (retail quirk, see PhySys_FindSystem).
int __cdecl PhySys::FindSphereCollisions(unsigned SystemId, Vector const& Center, float Radius,
                                          CObject** OutObjects, int MaxObjects)
{
    return PhySys_FindSystem(SystemId).FindSphereCollisions(Center, Radius, OutObjects, MaxObjects);
}

void __cdecl PhySys::GenerateCollisions(CBeam* Beam)
{
    // FUN_06289b20. x87: z is stored but stays unrounded on the stack.
    Vector const& Source = (Beam->bMoved != 0) ? Beam->LastPos : Beam->WorldPosition;
    Vector Delta;
    Delta.x = (float)((double)(Beam->WorldPosition.x) - Source.x);
    Delta.y = (float)((double)(Beam->WorldPosition.y) - Source.y);
    double const DeltaZExact = (double)(Beam->WorldPosition.z) - Source.z;
    Delta.z = (float)(DeltaZExact);

    // The length is compared unrounded; the stored float is what is used afterwards. Returns
    // unless strictly greater than 0.0001 (NaN returns).
    double const LengthX87 = sqrt((DeltaZExact * Delta.z + (double)(Delta.x) * Delta.x) +
                                  (double)(Delta.y) * Delta.y);
    float const Length = (float)(LengthX87);
    if (!(LengthX87 > 0.0001))
        return;

    // The system id is the beam's object database zone.
    Universe::ISystem const* const System = Beam->ObjectDb->GetZoneDb();
    unsigned const SystemId = System->GetId();

    // A miss is not tested (retail quirk, see PhySys_FindSystem).
    PhysicalSystem& PhysSystem = PhySys_FindSystem(SystemId);

    double const InvLength = 1.0 / (double)(Length);
    Delta.x = (float)(Delta.x * InvLength);
    Delta.y = (float)(Delta.y * InvLength);
    Delta.z = (float)(Delta.z * InvLength);

    PhysSystem.GenerateCollisions(Beam, Delta, Length);
}

void __cdecl PhySys::GetCollisions(unsigned SystemId, CollisionEvent const*& Begin,
                                    CollisionEvent const*& End)
{
    // FUN_06289a20. A miss is not tested (retail quirk, see PhySys_FindSystem).
    std::vector<CollisionEvent> const& Collisions = PhySys_FindSystem(SystemId).Collisions;
    Begin = Collisions.data();
    End = Collisions.data() + Collisions.size();
}

// ============================================================================
// Misc
// ============================================================================

bool __cdecl PhySys::AnyActive()
{
    // FUN_06288960: the world map's size.
    return !G_PhysicalSystems.empty();
}

unsigned __cdecl PhySys::MovingObjectCount(unsigned SystemId)
{
    // FUN_06289ad0. A miss is not tested (retail quirk, see PhySys_FindSystem).
    return (unsigned)PhySys_FindSystem(SystemId).MovingObjects.size();
}

IVP_Time __cdecl PhySys::GetCurrentTime(unsigned SystemId)
{
    // FUN_06289a70: the environment's split-precision time stamp, copied verbatim as two raw
    // dwords. Neither the lookup (retail quirk, see PhySys_FindSystem) nor the environment
    // pointer is tested.
    IVP_Environment* const Environment = PhySys_FindSystem(SystemId).Environment;
    IVP_Time Result;
    Result.time_base = Environment->current_time.time_base;
    Result.time_offset = Environment->current_time.time_offset;
    return Result;
}

