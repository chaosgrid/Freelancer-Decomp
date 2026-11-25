// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Common-internal declarations shared by the translation units retail's physys.cpp /
// PhyObj.cpp code is split into. Not a public header. Shared helpers live in namespace
// CmnPhySys; the two types keep their global names (PhysicalSystem from RTTI,
// FLControllerGolemParms because other headers forward-declare it). It also carries the one
// include set those translation units share.


#include "common_physys.h"

#include "common_archetype.h"
#include "common_cobject.h"
#include "common_cobjdb.h"
#include "common_id.h"
#include "common_universe.h"
#include "common_net.h"
#include "common_phyarch.h"
#include "common_misc.h"
#include "common_server.h"
#include "common_csys.h"  // Acquire3DMathEngine

#include "dacom.h"
#include "DALib.h"    // DALib::Engine (the instance-id helpers below)
#include "engine/engbase.h"
#include "x86math.h"  // Quaternion, Matrix
#include "ivp_collision/ivp_collision.h"
#include "ivp_physics/ivp_core.h"
#include "ivp_physics/ivp_real_object.h"
#include "ivp_physics/ivp_environment.h"
#include "ivp_physics/ivp_controller.h"
#include "ivp_physics/ivp_material.h"
#include "ivp_physics/ivp_listener.h"
#include "ivp_physics/ivp_mindist_settings.h"
#include "ivp_physics/ivp_mindist.h"
#include "ivp_compact_builder/ivp_compact_ledge.h"
#include "ivp_compact_builder/ivp_surface_builder_pointsoup.h"
#include "ivp_compact_builder/ivp_surface_builder_ledge_soup.h"
#include "ivp_physics/ivp_surman_polygon.h"
#include "ivp_collision/ivp_ray_solver.h"
#include "ivp_collision/ivp_sphere_query.h"

#include <assert.h>
#include <cstdint>
#include <float.h>
#include <math.h>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include <mmsystem.h>  // timeGetTime; winmm is linked by common_playeraccount.cpp

// Win32 macro conflict: PhySys::GetCurrentTime vs windows.h GetCurrentTime()
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif


class IVP_Material;

// ============================================================================
// PhysicalSystem -- one per star system; the mapped value of the world map DAT_063fc098.
// RTTI: .?AUPhysicalSystem@@ with bases IVP_Listener_Object (+0x00), IVP_Listener_Collision
// (+0x04) and IVP_Collision_Filter_Coll_Group_Ident (+0x0c).
// ============================================================================
struct PhysicalSystem : public IVP_Listener_Object,
                        public IVP_Listener_Collision,
                        public IVP_Collision_Filter_Coll_Group_Ident
{
    // FUN_0628c1f0
    explicit PhysicalSystem(unsigned InSystemId);
    // FUN_06289790 (deleting FUN_0628c2e0). Member teardown only; it does not delete the
    // environment.
    virtual ~PhysicalSystem() {}

    PhysicalSystem(PhysicalSystem const&) = delete;
    PhysicalSystem& operator=(PhysicalSystem const&) = delete;

    // FUN_0628c900: first-use environment creation + refcount.
    void AddRef();
    // FUN_0628ca80: one PhySys::Update tick.
    void Update(float Dt);
    // FUN_0628ccf0: the beam ray cast of PhySys::GenerateCollisions.
    void GenerateCollisions(CBeam* Beam, Vector const& Direction, float Length);
    // FUN_0628d1a0
    int FindRayCollisions(Vector const& From, Vector const& To, PhySys::RayHit* Hits, int MaxHits);
    // FUN_0628d450
    int FindSphereCollisions(Vector const& Center, float Radius, CObject** OutObjects, int MaxObjects);
    // FUN_0628c7b0: build Object's polygon body in this system.
    void CreatePolygonBody(CObject* Object, IVP_RealObjectConstructionTemplate* Template);
    // FUN_0628c860: build a ball body in this system.
    IVP_Ball* CreateBallBody(Vector const& Position, Matrix const& Orientation, float Radius,
                             IVP_RealObjectConstructionTemplate* Template);

    // IVP_Listener_Object
    virtual void event_object_deleted(IVP_Environment_Object_Notify_Event* Event) override; // LAB_0628c300
    // ICF-shared no-op.
    virtual void event_object_created(IVP_Environment_Object_Notify_Event* /*Event*/) override {}
    virtual void event_object_revived(IVP_Environment_Object_Notify_Event* Event) override; // LAB_0628c5b0
    virtual void event_object_frozen(IVP_Environment_Object_Notify_Event* Event) override;  // LAB_0628c660

    // IVP_Listener_Collision (the other slots keep the base's no-op bodies)
    virtual void event_pre_collision(IVP_Event_Collision* Event) override; // LAB_0628c410

    // IVP_Collision_Filter
    virtual int32_t check_objects_for_collision_detection(IVP_Real_Object* A, IVP_Real_Object* B) override; // FUN_0628c770
    virtual void environment_will_be_deleted(IVP_Environment* Env) override; // LAB_0628c7a0

    unsigned SystemId;                                // +0x14
    IVP_Environment* Environment;                     // +0x18
    std::vector<PhySys::CollisionEvent> Collisions;   // +0x1c
    std::set<CObject*> MovingObjects;                 // +0x2c
    int RefCount;                                     // +0x40
};

// The stack-built tuning struct FUN_06288e60 hands to the golem constructor (PhySys::RCBegin
// builds one the same way with its RC_* values). Defaults from FUN_06342c40 + FUN_06343020.
// +0x00..+0x23 is IVP_Controller_Motion_Parms.
struct FLControllerGolemParms : public IVP_Controller_Motion_Parms
{
    float MaxDeltaPosition;        // +0x24  default 10.0f
    float DeltaOrientation;        // +0x28  default PI/2
    float Field_0x2c;              // +0x2c  default 10.0f
    float Field_0x30;              // +0x30  default 1e20f
    float Field_0x34;              // +0x34  default 5.0f
};

static_assert(sizeof(FLControllerGolemParms) == 0x38, "FLControllerGolemParms 0x38");
static_assert(offsetof(FLControllerGolemParms, MaxTorque) == 0x20, "parms+0x20 MaxTorque");
static_assert(offsetof(FLControllerGolemParms, MaxDeltaPosition) == 0x24, "parms+0x24");
static_assert(offsetof(FLControllerGolemParms, Field_0x34) == 0x34, "parms+0x34");

namespace CmnPhySys
{
    // .rdata source-file names retail passes to FDUMP.
    const char kPhySysSource[] = "E:\\FL\\Scratch\\Source\\Common\\physys.cpp";
    const char kPhyObjSource[] = "E:\\FL\\Scratch\\Source\\Common\\PhyObj.cpp";

    // DAT_063fc0bc, the game's single IVP material (common_physys.cpp).
    extern IVP_Material* G_PhySysMaterial;
    // DAT_063fc098, the world map (common_physys.cpp).
    extern std::map<unsigned, PhysicalSystem> G_PhysicalSystems;

    // The system a SystemId names, looked up as retail's world-map readers do. Retail quirk
    // kept on purpose: the lookup is not tested for end(), so a miss uses the tree's head node,
    // whose value storage was never constructed. The port returns its own head node's storage
    // likewise (same address behaviour; the content is uninitialised in both). Reading the node
    // pointer never dereferences the iterator, so no checked-iterator assert fires.
    // Callers: PhySys::Update, GetCollisions, GetCurrentTime, MovingObjectCount,
    // GenerateCollisions, FindSphereCollisions, BuildIVP_Sphere, SplitInstance, ReMakeBody and
    // the three CreatePhantom. The callers that do test (Startup, Shutdown, BuildPartIVP_Real,
    // FindRayCollisions) use find().
    inline PhysicalSystem& PhySys_FindSystem(unsigned SystemId)
    {
        return G_PhysicalSystems.find(SystemId)._Ptr->_Myval.second;
    }

    // Each helper below is exactly one IEngine slot call on an engine instance id (-1 for
    // "none"), nothing more.
    // IEngine +0xd8 GetRelatedInstance(From, Flags, Cursor): Cursor -1 starts a walk, -1 ends it.
    inline long PhySys_RelatedInstance(long From, unsigned long Flags, long Cursor)
    {
        return (DALib::Engine->GetRelatedInstance(From, Flags, Cursor));
    }

    // IEngine +0x80 GetInstanceUserData: the CObject PhySys stored on the instance, or null.
    inline CObject* PhySys_InstanceObject(long Instance)
    {
        return (CObject*)DALib::Engine->GetInstanceUserData(Instance);
    }

    // IEngine +0x7c SetInstanceUserData.
    inline void PhySys_SetInstanceObject(long Instance, CObject* Object)
    {
        DALib::Engine->SetInstanceUserData(Instance, Object);
    }

    // IEngine +0xd4 GetInstanceParent.
    inline long PhySys_InstanceParent(long Instance)
    {
        return (DALib::Engine->GetInstanceParent(Instance));
    }

    // FUN_062883c0: re-pose Object's jointed child instances (common_physys.cpp).
    void PhySys_UpdateJointedChildren(long Instance, CObject* Object);
    // The MatrixToQuaternion pose conversion of the body builders and CreatePhantom
    // (common_physicalsystem.cpp).
    void PhySys_OrientationToIvpQuat(Matrix const& Orientation, IVP_U_Quat& Out);
    // FUN_06345450 on an old body pointer (common_physys_build.cpp).
    void PhySys_DeleteBody(IVP_Real_Object* Body);
    // FUN_06342c40 + FUN_06343020, the tuning struct's defaults (common_controllergolem.cpp).
    void FLControllerGolemParms_Init(FLControllerGolemParms& Parms);
    // FUN_0628be60: Dst = A + B * Scale (common_phantom.cpp).
    void PhySys_Vec3AddMul(Vector& Dst, Vector const& A, Vector const& B, float Scale);
    // FUN_0628b740 / FUN_0628b6e0: a phantom handle's position (common_phantom.cpp).
    int PhySys_GetPhantomPosition(void* Handle, Vector* OutPosition);
    int PhySys_SetPhantomPosition(void* Handle, Vector const* NewPosition);
}
