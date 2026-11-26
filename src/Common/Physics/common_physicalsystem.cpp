// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Physics/common_physys_internal.h"
#include "Physics/physics_tick_profile.h" // Port-only owner-requested diagnostics

using namespace CmnPhySys;

// ============================================================================
// PhySys::PhyPerfCounter -- the IVP performance counter every PhysicalSystem hands to its
// environment. RTTI: .?AVPhyPerfCounter@PhySys@@ : IVP_PerformanceCounter, vtable
// .rdata:0639baac, 0xc bytes. Every slot is a no-op (ICF-shared RETs), the deleting call or
// the destructor.
// ============================================================================
namespace PhySys
{
    class PhyPerfCounter : public IVP_PerformanceCounter
    {
    public:
        // FUN_06285df0
        PhyPerfCounter()
            : Unread04(0)
            , Unread08(0)
        {
        }

        virtual void start_pcount() override {}
        virtual void pcount(int32_t /*Arg*/) override {}
        virtual void stop_pcount() override {}

        // LAB_06285e30: the deleting destructor.
        virtual void environment_is_going_to_be_deleted(IVP_Environment* /*Env*/) override
        {
            delete this;
        }

        virtual void reset_and_print_performance_counters(float /*Arg0*/, float /*Arg1*/) override {}

        // FUN_063602c0 (ICF-folded).
        virtual ~PhyPerfCounter() {}

        // Zeroed by the ctor, never read.
        int32_t Unread04;   // +0x04
        uint8_t Unread08;   // +0x08
    };
}

namespace
{
    // DAT_063fc0c8: the multiplayer time accumulator Update keeps (one double for all
    // systems).
    double G_PhySysSimAccumulator = 0.0;

    // The IVP_Compact_Edge part id: the fourth dword of the edge's start point.
    unsigned PhySys_PartIdOfEdge(IVP_Compact_Edge const* Edge)
    {
        IVP_U_Float_Point const* Point = Edge->GetStartPoint();
        unsigned PartId;
        memcpy(&PartId, &Point->HesseVal, sizeof(PartId));
        return PartId;
    }
}

PhysicalSystem::PhysicalSystem(unsigned InSystemId)
    : IVP_Listener_Collision(IVP_LISTENER_COLLISION_CALLBACK_PRE_COLLISION)
    , IVP_Collision_Filter_Coll_Group_Ident(0)
    , SystemId(InSystemId)
    , Environment(nullptr)
    , RefCount(0)
{
}

// LAB_0628c300. The object's client data is the CObject; a null one is skipped.
void PhysicalSystem::event_object_deleted(IVP_Environment_Object_Notify_Event* Event)
{
    CObject* const Object = (CObject*)Event->Object->client_data;
    if (Object == nullptr)
        return;
    MovingObjects.erase(Object);
}

// LAB_0628c5b0: a null client is skipped, otherwise a unique insert.
void PhysicalSystem::event_object_revived(IVP_Environment_Object_Notify_Event* Event)
{
    CObject* const Object = (CObject*)Event->Object->client_data;
    if (Object == nullptr)
        return;
    MovingObjects.insert(Object);
}

// LAB_0628c660: the same erase without the null test, so a null client erases a null key
// (normally absent).
void PhysicalSystem::event_object_frozen(IVP_Environment_Object_Notify_Event* Event)
{
    CObject* const Object = (CObject*)Event->Object->client_data;
    MovingObjects.erase(Object);
}

// LAB_0628c410, on the IVP_Listener_Collision sub-object.
void PhysicalSystem::event_pre_collision(IVP_Event_Collision* Event)
{
    // Proceeds only when strictly greater (NaN returns).
    if (!((double)(Event->d_time_since_last_collision) > PhySys::MIN_TIME_BETWEEN_COLLISIONS))
        return;

    IVP_Contact_Situation* const Situation = Event->contact_situation;
    CObject* const ObjectA = (CObject*)Situation->objects[0]->client_data;
    CObject* const ObjectB = (CObject*)Situation->objects[1]->client_data;

    if (ObjectA != nullptr)
    {
        if (ObjectB != nullptr)
        {
            PhySys::CollisionEvent Collision;
            Collision.ObjectA = ObjectA;
            Collision.ObjectB = ObjectB;
            Collision.PartIdA = PhySys_PartIdOfEdge(Situation->compact_edges[0]);
            Collision.PartIdB = PhySys_PartIdOfEdge(Situation->compact_edges[1]);
            Collision.Point.x = Situation->contact_point_ws[0];
            Collision.Point.y = Situation->contact_point_ws[1];
            Collision.Point.z = Situation->contact_point_ws[2];
            Collision.SurfaceNormal.x = Situation->surf_normal[0];
            Collision.SurfaceNormal.y = Situation->surf_normal[1];
            Collision.SurfaceNormal.z = Situation->surf_normal[2];
            Collision.RelativeVelocity.x = Situation->speed[0];
            Collision.RelativeVelocity.y = Situation->speed[1];
            Collision.RelativeVelocity.z = Situation->speed[2];
            Collisions.push_back(Collision);
        }

        // A class with the three low bits set (the projectile classes) is taken out of
        // collision after its first contact. The body is not tested.
        if (((unsigned)ObjectA->ObjectClass & 7) == 7)
        {
            ObjectA->BodyPtr->disable_simulation();
            ObjectA->enable_collisions_r(false);
        }
    }

    if (ObjectB != nullptr && ((unsigned)ObjectB->ObjectClass & 7) == 7)
    {
        ObjectB->BodyPtr->disable_simulation();
        ObjectB->enable_collisions_r(false);
    }
}

// FUN_0628c770, on the filter sub-object. When the base filter allows the pair (== 1) and a
// game filter is installed, the game filter's answer is returned instead.
int32_t PhysicalSystem::check_objects_for_collision_detection(IVP_Real_Object* A, IVP_Real_Object* B)
{
    int32_t const Result = IVP_Collision_Filter_Coll_Group_Ident::check_objects_for_collision_detection(A, B);
    if (PhySys::m_pCollisionFilter != nullptr && Result == 1)
        return (int32_t)PhySys::m_pCollisionFilter(A, B);
    return Result;
}

// LAB_0628c7a0: only Environment is cleared; the base's self-delete is not called.
void PhysicalSystem::environment_will_be_deleted(IVP_Environment* /*Env*/)
{
    Environment = nullptr;
}

// FUN_0628cbe0: copy an IVP body's pose into its CObject.
static void PhySys_SyncObjectFromBody(CObject* Object)
{
    IVP_U_Quat BodyQuat;
    IVP_U_Point BodyPos;
    Object->BodyPtr->calc_at_time_AT(&BodyQuat, &BodyPos);

    // The quaternion goes from IVP's (x, y, z, w) to the math engine's (w, x, y, z).
    Vector Position;
    Position.x = BodyPos.x;
    Position.y = BodyPos.y;
    Position.z = BodyPos.z;
    Quaternion Quat;
    Quat.w = BodyQuat.w;
    Quat.x = BodyQuat.x;
    Quat.y = BodyQuat.y;
    Quat.z = BodyQuat.z;

    Matrix Orientation;
    Acquire3DMathEngine()->QuaternionToMatrix(Orientation, Quat);

    static_assert(sizeof(Matrix) == sizeof(Matrix), "EngineObject orientation is 9 floats");
    Object->WorldPosition = Position;
    memcpy(&Object->Orientation, &Orientation, sizeof(Matrix));

    // With an engine instance, read the instance extent (flags 1) and write the same values
    // back, then update the jointed children. Retail reuses the position copy as the vector
    // buffer for both calls.
    if (Object->EngineInstanceId != -1)
    {
        float Extent;
        DALib::Engine->GetInstanceExtent(Object->EngineInstanceId, 1, &Extent, &Position);
        DALib::Engine->SetInstanceExtent(Object->EngineInstanceId, 1, Extent, &Position);
        PhySys_UpdateJointedChildren(Object->EngineInstanceId, Object);   // FUN_062883c0
    }

    // CSimple-derived classes re-cache their physical properties.
    if (((unsigned)Object->ObjectClass & 3) == 3)
        ((CSimple*)Object)->cache_physical_props();
}

// FUN_0628c900
void PhysicalSystem::AddRef()
{
    // The environment is built only on 0 -> 1.
    int const PreviousRefCount = RefCount;
    RefCount = PreviousRefCount + 1;
    if (PreviousRefCount != 0)
        return;

    IVP_Environment_Create_Parms Parms;
    Parms.CollisionFilter = this;                              // implicit upcast: this + 0x0c
    Parms.PerformanceCounter = new PhySys::PhyPerfCounter();

    Environment = IVP_Environment_Manager::get_environment_manager()->create_environment(
        &Parms, "digitalanvil", 0x11c2de34);

    // IVP's MAX_SPAWNED_MINDIST_COUNT is written between the division and the call, as in
    // retail.
    float const PsiTime = 1.0f / PhySys::PHYSICAL_SIM_RATE;
    G_IVP_MindistSettings.max_spawned_mindist_count = PhySys::MAX_SPAWNED_MINDIST_COUNT;
    IVP_Environment_SetDeltaPsiTime(Environment, PsiTime);

    IVP_Range_Manager* const RangeManager = Environment->range_manager;
    RangeManager->look_ahead_time_world = PhySys::RMGR_LOOK_AHEAD_TIME_WORLD;                 // +0x20
    RangeManager->look_ahead_max_radius_world = PhySys::RMGR_LOOK_AHEAD_MAX_RADIUS_WORLD;     // +0x24
    RangeManager->look_ahead_min_distance_world = PhySys::RMGR_LOOK_AHEAD_MIN_DISTANCE_WORLD; // +0x28
    RangeManager->look_ahead_max_distance_world = PhySys::RMGR_LOOK_AHEAD_MAX_DISTANCE_WORLD; // +0x2c
    RangeManager->look_ahead_min_seconds_world = PhySys::RMGR_LOOK_AHEAD_MIN_SECONDS_WORLD;   // +0x30
    RangeManager->look_ahead_time_intra = PhySys::RMGR_LOOK_AHEAD_TIME_INTRA;                 // +0x0c
    RangeManager->look_ahead_max_radius_intra = PhySys::RMGR_LOOK_AHEAD_MAX_RADIUS_INTRA;     // +0x10
    RangeManager->look_ahead_min_distance_intra = PhySys::RMGR_LOOK_AHEAD_MIN_DISTANCE_INTRA; // +0x14
    RangeManager->look_ahead_max_distance_intra = PhySys::RMGR_LOOK_AHEAD_MAX_DISTANCE_INTRA; // +0x18
    RangeManager->look_ahead_min_seconds_intra = PhySys::RMGR_LOOK_AHEAD_MIN_SECONDS_INTRA;   // +0x1c

    // Zero gravity (the fourth float is not written).
    IVP_U_Point Gravity;
    Gravity.set(0.0f, 0.0f, 0.0f);
    IVP_Environment_SetGravity(Environment, &Gravity);

    // The IVP helpers take void*, so the base sub-objects are formed first (implicit upcasts:
    // IVP_Listener_Object at +0x00, IVP_Listener_Collision at +0x04).
    IVP_Listener_Object* const ObjectListener = this;
    IVP_Listener_Collision* const CollisionListener = this;
    IVP_Environment_Append_Object_Listener(Environment, ObjectListener);
    IVP_Environment_Add_Listener_Collision(Environment, CollisionListener);

    Environment->anomaly_limits->MaxVelocity = PhySys::ANOM_LIMITS_MAX_VELOCITY;
    Environment->anomaly_limits->MaxAngularVelocityPerPsi = PhySys::ANOM_LIMITS_MAX_ANGULAR_VELOCITY_PER_PSI;
}

// FUN_0628ca80
void PhysicalSystem::Update(float Dt)
{
    // The result is never used.
    timeGetTime();

    Collisions.clear();

    if (SinglePlayer())
    {
        // Clamped only when strictly greater (NaN passes).
        if ((double)(Dt) > 0.0999)
            Dt = (float)(0.0999);
        IVP_Environment_SimulateDTime(Environment, Dt);
    }
    else
    {
        // Multiplayer: accumulate, and step only once above 0.0051.
        G_PhySysSimAccumulator += (double)(Dt);
        if (G_PhySysSimAccumulator > 0.0051)
        {
            // Whole 0.0999 steps, each followed by a sync of the moving objects.
            while (G_PhySysSimAccumulator > 0.0999)
            {
                IVP_Environment_SimulateDTime(Environment, (float)(0.0999));
                G_PhySysSimAccumulator -= 0.0999;
                FPhysicsTickScope SyncProfile(EPhysicsTickSection::SyncObjects);
                for (std::set<CObject*>::iterator It = MovingObjects.begin(); It != MovingObjects.end(); ++It)
                    PhySys_SyncObjectFromBody(*It);
            }
            // The remainder, when above 1e-5f (DAT_0639c3f0).
            if (G_PhySysSimAccumulator > (double)(1.0e-5f))
            {
                IVP_Environment_SimulateDTime(Environment, (float)(G_PhySysSimAccumulator));
            }
            G_PhySysSimAccumulator = 0.0;
        }
    }

    FPhysicsTickScope SyncProfile(EPhysicsTickSection::SyncObjects);
    for (std::set<CObject*>::iterator It = MovingObjects.begin(); It != MovingObjects.end(); ++It)
        PhySys_SyncObjectFromBody(*It);
}

// FUN_0628ccf0. The beam's ray through this system's environment: the nearest hit whose object
// is not in the beam's collision group and has a client object moves the beam to the hit point
// and becomes one CollisionEvent.
void PhysicalSystem::GenerateCollisions(CBeam* Beam, Vector const& Direction, float Length)
{
    // The start is LastPos when the beam moved, else its WorldPosition.
    Vector const& Source = (Beam->bMoved != 0) ? Beam->LastPos : Beam->WorldPosition;
    Vector Start = Source;

    // When BULLET_LENGTH_FUDGE * 0.5f < Length (NaN skips): Length += fudge, and the start is
    // pulled back by (Direction * fudge) * 0.5f.
    const float Fudge = PhySys::BULLET_LENGTH_FUDGE;
    if ((double)(Fudge) * 0.5f < Length)
    {
        Length = (float)((double)(Fudge) + Length);
        Vector Scaled;
        Scaled.x = (float)((double)(Fudge) * Direction.x);
        Scaled.y = (float)((double)(Fudge) * Direction.y);
        Scaled.z = (float)((double)(Fudge) * Direction.z);
        Vector Half;
        Half.x = (float)(0.5 * Scaled.x);
        Half.y = (float)(0.5 * Scaled.y);
        Half.z = (float)(0.5 * Scaled.z);
        Start.x = (float)((double)(Start.x) - Half.x);
        Start.y = (float)((double)(Start.y) - Half.y);
        Start.z = (float)((double)(Start.z) - Half.z);
    }

    // The direction is used as given (not normalised here). Retail leaves both fourth floats
    // as stack garbage; the port stores 0.
    IVP_Ray_Solver_Template Template;
    Template.ray_start_point.set(Start.x, Start.y, Start.z);
    Template.ray_start_point.hesse = 0.0f;
    Template.ray_normized_direction.set(Direction.x, Direction.y, Direction.z);
    Template.ray_normized_direction.hesse = 0.0f;
    Template.ray_length = Length;
    Template.ray_flags = 0;

    // Against everything in the simulation.
    IVP_Ray_Solver_Min_Hash Solver(&Template);
    Solver.check_ray_against_all_objects_in_sim(Environment);

    if (Solver.min_hash.counter <= 0)
        return;

    for (;;)
    {
        IVP_Ray_Hit* const Hit = (IVP_Ray_Hit*)Solver.min_hash.find_min_elem();
        if (Hit == nullptr)
            return;
        IVP_Real_Object* const HitObject = Hit->hit_real_object;

        // A hit in the beam's own collision group (both names non-empty and equal), or one
        // without a client object, is dropped and the next one taken.
        bool bDrop = false;
        if (HitObject->name[0] != 0 && Beam->CollisionGroup[0] != 0 &&
            strcmp(HitObject->name, Beam->CollisionGroup) == 0)
            bDrop = true;
        else if (HitObject->client_data == nullptr)
            bDrop = true;
        if (bDrop)
        {
            Solver.min_hash.remove_min();
            continue;
        }

        // The distance is the min hash element's key.
        CObject* const Client = (CObject*)HitObject->client_data;
        const float Distance = Solver.min_hash.stadium[1]->value;
        PhySys::CollisionEvent Event;
        Event.ObjectA = Client;
        Event.ObjectB = Beam;
        Event.PartIdA = (Hit->hit_compact_ledge != nullptr) ? (unsigned)Hit->hit_compact_ledge->ClientData
                                                            : 0;
        Event.PartIdB = 0;

        // The hit point Start + Direction * Distance becomes the beam's WorldPosition.
        Vector Step;
        Step.x = (float)((double)(Distance) * Template.ray_normized_direction.x);
        Step.y = (float)((double)(Distance) * Template.ray_normized_direction.y);
        Step.z = (float)((double)(Distance) * Template.ray_normized_direction.z);
        Vector HitPoint;
        HitPoint.x = (float)((double)(Start.x) + Step.x);
        HitPoint.y = (float)((double)(Start.y) + Step.y);
        HitPoint.z = (float)((double)(Start.z) + Step.z);
        Beam->WorldPosition = HitPoint;
        Event.Point = HitPoint;

        // The object-space hit normal rotated to world space through the cache slot.
        IVP_NarrowPhaseCacheSlot* const Slot = HitObject->ResolveNarrowPhaseCacheSlot();
        IVP_U_Point NormalWs;
        IVP_RotateSlotToWorldSpace(Slot, &Hit->hit_surface_direction_os, &NormalWs);
        HitObject->ReleaseNarrowPhaseCacheSlot(Slot);
        Event.SurfaceNormal.x = NormalWs.x;
        Event.SurfaceNormal.y = NormalWs.y;
        Event.SurfaceNormal.z = NormalWs.z;

        // The client body's surface speed at the hit point. Retail never writes the point's
        // fourth float; the port stores 0.
        IVP_U_Point HitPointWs;
        HitPointWs.set(HitPoint.x, HitPoint.y, HitPoint.z);
        HitPointWs.hesse = 0.0f;
        IVP_U_Point SurfaceSpeed;
        Client->BodyPtr->core->get_surface_speed(&HitPointWs, &SurfaceSpeed);

        const Vector BeamVelocity = Beam->get_velocity();
        Event.RelativeVelocity.x = (float)((double)(BeamVelocity.x) - SurfaceSpeed.x);
        Event.RelativeVelocity.y = (float)((double)(BeamVelocity.y) - SurfaceSpeed.y);
        Event.RelativeVelocity.z = (float)((double)(BeamVelocity.z) - SurfaceSpeed.z);

        // Only the first accepted hit is used.
        Collisions.push_back(Event);
        return;
    }
}

namespace CmnPhySys
{
    // The pose conversion CreatePolygonBody / CreateBallBody inline: MatrixToQuaternion, then
    // the (W, X, Y, Z) result stored as IVP's (x, y, z, w).
    void PhySys_OrientationToIvpQuat(Matrix const& Orientation, IVP_U_Quat& Out)
    {
        static_assert(sizeof(Matrix) == sizeof(Matrix), "EngineObject orientation is 9 floats");
        Quaternion Quat;
        Acquire3DMathEngine()->MatrixToQuaternion(Quat, Orientation);
        Out.x = Quat.x;
        Out.y = Quat.y;
        Out.z = Quat.z;
        Out.w = Quat.w;
    }
}

// FUN_0628c7b0
void PhysicalSystem::CreatePolygonBody(CObject* Object, IVP_RealObjectConstructionTemplate* Template)
{
    // The fourth float of the IVP_U_Point is not written.
    IVP_U_Point Position;
    Position.set(Object->WorldPosition.x, Object->WorldPosition.y, Object->WorldPosition.z);

    IVP_U_Quat Rotation;
    PhySys_OrientationToIvpQuat(Object->Orientation, Rotation);

    if (Template->Material == nullptr)
        Template->Material = G_PhySysMaterial;

    // Object->SurfaceManager is the one BuildPartIVP_Real stores.
    IVP_Polygon* const Body = IVP_Environment_CreatePolygon(Environment, Object->SurfaceManager, Template,
                                                            &Rotation, &Position);

    Object->BodyPtr = Body;
    Object->PhysicsSystemId = SystemId;
    Body->client_data = Object;
}

// FUN_0628c860
IVP_Ball* PhysicalSystem::CreateBallBody(Vector const& Position, Matrix const& Orientation, float Radius,
                                         IVP_RealObjectConstructionTemplate* Template)
{
    IVP_U_Point BallPosition;
    BallPosition.set(Position.x, Position.y, Position.z);

    IVP_U_Quat Rotation;
    PhySys_OrientationToIvpQuat(Orientation, Rotation);

    if (Template->Material == nullptr)
        Template->Material = G_PhySysMaterial;

    // The radius is passed by address (FUN_0634cdf0).
    float BallRadius = Radius;
    return IVP_Environment_CreateBall(Environment, &BallRadius, Template, &Rotation, &BallPosition);
}

// ============================================================================
// PhySys::Startup / Shutdown
// ============================================================================

void __cdecl PhySys::Startup(unsigned SystemId)
{
    // FUN_06289510: the material singleton once (both constants doubles rounded to float),
    // then the system by id, created on a miss, and its reference count bumped.
    if (G_PhySysMaterial == nullptr)
    {
        G_PhySysMaterial = new IVP_Material_Simple((float)(MATERIAL_FRICTION),
                                                   (float)(MATERIAL_ELASTICITY));
    }

    std::map<unsigned, PhysicalSystem>::iterator It = G_PhysicalSystems.find(SystemId);
    if (It == G_PhysicalSystems.end())
    {
        // Retail builds the system on the stack and copies it into the map. The port builds it
        // in place, since AddRef registers it with IVP by address.
        It = G_PhysicalSystems.emplace(std::piecewise_construct, std::forward_as_tuple(SystemId),
                                       std::forward_as_tuple(SystemId)).first;
    }

    It->second.AddRef();
}

void __cdecl PhySys::Shutdown(unsigned SystemId)
{
    // FUN_06289910
    std::map<unsigned, PhysicalSystem>::iterator It = G_PhysicalSystems.find(SystemId);
    if (It != G_PhysicalSystems.end())
    {
        PhysicalSystem& System = It->second;
        if (--System.RefCount == 0)
        {
            IVP_Environment* const Env = System.Environment;
            if (Env != nullptr)
            {
                IVP_Environment_Destructor(Env);
                ::operator delete(Env);
            }
            System.Environment = nullptr;
        }
        if (System.RefCount == 0)
            G_PhysicalSystems.erase(It);
    }

    // The material goes when the map is empty, whether or not SystemId was found; then IVP's
    // cached point-soup surface is freed (FUN_0636c7e0).
    if (G_PhysicalSystems.empty())
    {
        if (G_PhySysMaterial != nullptr)
            delete G_PhySysMaterial;
        G_PhySysMaterial = nullptr;
        IVP_SurfaceBuilder_Pointsoup::Cleanup();
    }
}

// ============================================================================
// PhySys::Update
// ============================================================================

void __cdecl PhySys::Update(unsigned SystemId, float Dt)
{
    // FUN_062899c0. A missing SystemId updates the map head node's unconstructed storage
    // (retail quirk, see PhySys_FindSystem).
    PhySys_FindSystem(SystemId).Update(Dt);
}

// ============================================================================
// Queries: ray / sphere queries against the system's IVP environment
// ============================================================================

// FUN_0628d1a0. The ray From -> To against every object of this system's environment, nearest
// hit first, at most MaxHits; hits without a client object are skipped. Returns the number
// written.
int PhysicalSystem::FindRayCollisions(Vector const& From, Vector const& To, PhySys::RayHit* Hits, int MaxHits)
{
    const float Dx = (float)((double)(To.x) - From.x);
    const float Dy = (float)((double)(To.y) - From.y);
    const float Dz = (float)((double)(To.z) - From.z);

    // x87: the length stays on the stack; each direction component is rounded.
    const double Length = sqrt(((double)(Dy) * Dy + (double)(Dz) * Dz) +
                               (double)(Dx) * Dx);
    const double InvLength = 1.0 / Length;
    const float DirX = (float)(Dx * InvLength);
    const float DirY = (float)(Dy * InvLength);
    const float DirZ = (float)(Dz * InvLength);

    // Retail leaves both fourth floats as stack garbage; the port stores 0.
    IVP_Ray_Solver_Template Template;
    Template.ray_start_point.set(From.x, From.y, From.z);
    Template.ray_start_point.hesse = 0.0f;
    Template.ray_normized_direction.set(DirX, DirY, DirZ);
    Template.ray_normized_direction.hesse = 0.0f;
    Template.ray_length = (float)(Length);
    Template.ray_flags = 0;

    IVP_Ray_Solver_Min_Hash Solver(&Template);
    Solver.check_ray_against_all_objects_in_sim(Environment);

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

            const double Distance = Hit->hit_distance;
            const float Vx = (float)(DirX * Distance);
            const float Vy = (float)(DirY * Distance);
            const float Vz = (float)(DirZ * Distance);
            Out->Point.x = (float)((double)(From.x) + Vx);
            Out->Point.y = (float)((double)(From.y) + Vy);
            Out->Point.z = (float)((double)(From.z) + Vz);

            // The object-space hit normal through the client's orientation.
            Vector NormalOs;
            NormalOs.x = Hit->hit_surface_direction_os.x;
            NormalOs.y = Hit->hit_surface_direction_os.y;
            NormalOs.z = Hit->hit_surface_direction_os.z;
            Vector NormalWs;
            Acquire3DMathEngine()->TransformMatrix(NormalWs, Client->Orientation, NormalOs);
            Out->Normal.x = NormalWs.x;
            Out->Normal.y = NormalWs.y;
            Out->Normal.z = NormalWs.z;

            // The hit ledge's client data (its part id), or 0.
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

// FUN_0628d450. Every object whose client is non-null goes to OutObjects, at most MaxObjects.
// Returns Count. Nothing else is tested.
int PhysicalSystem::FindSphereCollisions(Vector const& Center, float Radius, CObject** OutObjects, int MaxObjects)
{
    FIVP_Sphere_Query_Record Record;
    Record.Center.x = Center.x;
    Record.Center.y = Center.y;
    Record.Center.z = Center.z;
    Record.Unwritten_0x0c = 0.0f;  // uninitialised in retail; never read
    Record.Radius = Radius;
    Record.MaxLedgeTreeDepth = 4;

    IVP_Sphere_Object_Query Query;
    Query.collect_objects(&Record, Environment);

    int Count = 0;
    for (int i = 0; i < (int)Query.Found.n_elems; ++i)
    {
        if (Count >= MaxObjects)
        {
            break;
        }
        CObject* const Client = (CObject*)Query.Found.elems[i]->client_data;
        if (Client != nullptr)
        {
            OutObjects[Count] = Client;
            ++Count;
        }
    }
    return Count;
}

