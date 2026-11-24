// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Physics/common_physys_internal.h"

using namespace CmnPhySys;

// ============================================================================
// PhySys::PhySurfaceManager / PhySys::RaySolver_MaskedSurface
// ============================================================================
//
// PhySurfaceManager (vtable .rdata:0639c258, operator new(0xc)) is the surface manager of
// every polygon body PhySys builds: IVP_SurfaceManager_Polygon over the part's compact surface
// plus the owning CObject. Its constructor is inlined at both allocation sites
// (BuildPartIVP_Real and the DetachInstance builder FUN_0628a0c0). It overrides slots 4, 5, 6
// and the destructor.
//
// The three overrides hide the ledges of parts that are no longer attached: a ledge's client
// data is its part id, and a ledge survives only while Owner->part_to_inst(part id) != -1.
namespace PhySys
{
    class PhySurfaceManager : public IVP_SurfaceManager_Polygon
    {
    public:
        PhySurfaceManager(const IVP_Compact_Surface* Surface, CObject* InOwner)
            : IVP_SurfaceManager_Polygon(Surface)
            , Owner(InOwner)
        {
        }

        // FUN_06289340
        ~PhySurfaceManager() override {}

        // Slot 4, FUN_06285f10
        void get_all_ledges_within_radius(const float* Center3, float Radius, void* Param3,
                                          void* Param4, void* Param5,
                                          IVP_SurfaceManager_OutList* OutList) override;
        // Slot 5, FUN_06285fa0
        void get_all_terminal_ledges(IVP_SurfaceManager_OutList* OutList) override;
        // Slot 6, FUN_06285e80: Param1 = IVP_Ray_Solver*, Param2 = IVP_Real_Object*.
        void insert_all_ledges_hitting_ray(void* Param1, void* Param2) override;

        CObject* Owner; // +0x08
    };
    static_assert(sizeof(PhySurfaceManager) == 0x0c, "PhySys::PhySurfaceManager is operator new(0xc)");

    // vtable .rdata:0639baec, one slot. Built on the stack by insert_all_ledges_hitting_ray.
    class RaySolver_MaskedSurface : public IVP_Ray_Solver_Os
    {
    public:
        RaySolver_MaskedSurface(IVP_Ray_Solver* RaySolver, IVP_Real_Object* Object, PhySurfaceManager* InManager)
            : IVP_Ray_Solver_Os(RaySolver, Object)
            , Manager(InManager)
        {
        }

        // Slot 0, FUN_06285ec0
        int check_ray_against_compact_ledge_os(const IVP_Compact_Ledge* Ledge) override;

        PhySurfaceManager* Manager; // +0x50
    };
    static_assert(sizeof(RaySolver_MaskedSurface) == 0x54, "PhySys::RaySolver_MaskedSurface size");
}

namespace
{
    // The inlined part test: Owner->part_to_inst(Ledge's part id) != -1.
    bool PhySys_LedgeIsAttached(CObject const* Owner, IVP_Compact_Ledge const* Ledge)
    {
        return Owner->part_to_inst((unsigned)Ledge->ClientData) != -1;
    }

    // The removal loop of slots 4 and 5: walk the list from the back and close up every
    // detached ledge in place.
    void PhySys_DropDetachedLedges(CObject const* Owner, IVP_SurfaceManager_OutList* OutList)
    {
        for (int32_t Index = OutList->Count - 1; Index >= 0; --Index)
        {
            IVP_Compact_Ledge const* const Ledge = (IVP_Compact_Ledge const*)OutList->Elems[Index];
            if (PhySys_LedgeIsAttached(Owner, Ledge))
                continue;
            for (int32_t Move = Index; Move < OutList->Count - 1; ++Move)
                OutList->Elems[Move] = OutList->Elems[Move + 1];
            --OutList->Count;
        }
    }
}

// The base query, then the filter.
void PhySys::PhySurfaceManager::get_all_ledges_within_radius(const float* Center3, float Radius, void* Param3,
                                                             void* Param4, void* Param5,
                                                             IVP_SurfaceManager_OutList* OutList)
{
    IVP_SurfaceManager_Polygon::get_all_ledges_within_radius(Center3, Radius, Param3, Param4, Param5, OutList);
    PhySys_DropDetachedLedges(Owner, OutList);
}

// The base query, then the filter.
void PhySys::PhySurfaceManager::get_all_terminal_ledges(IVP_SurfaceManager_OutList* OutList)
{
    IVP_SurfaceManager_Polygon::get_all_terminal_ledges(OutList);
    PhySys_DropDetachedLedges(Owner, OutList);
}

// Instead of the base's plain IVP_Ray_Solver_Os, a masked one over this manager's surface.
void PhySys::PhySurfaceManager::insert_all_ledges_hitting_ray(void* Param1, void* Param2)
{
    // The IVP base declares the slot with void* parameters (ivp_surman_polygon.h).
    RaySolver_MaskedSurface Solver((IVP_Ray_Solver*)Param1, (IVP_Real_Object*)Param2, this);
    Solver.check_ray_against_compact_surface(compact_surface);
}

// A ledge of a detached part reports no hit (0); any other goes to IVP_Ray_Solver_Os's test.
int PhySys::RaySolver_MaskedSurface::check_ray_against_compact_ledge_os(const IVP_Compact_Ledge* Ledge)
{
    if (!PhySys_LedgeIsAttached(Manager->Owner, Ledge))
        return 0;
    return IVP_Ray_Solver_Os::check_ray_against_compact_ledge_os(Ledge);
}

// ?GetRoot@@YAJJ@Z (FUN_0630e6a0), defined in common_hierarchy.cpp next to the CObject*
// overload; no header declares it.
COMMON_API long __cdecl GetRoot(long Inst);

namespace
{
    // The bHasInertia block of the three template builders: an explicit inertia box replaces
    // the automatic one. Retail also stores an unwritten stack float at +0x30; the port keeps
    // the constructor's value there.
    void PhySys_FillTemplateInertia(IVP_RealObjectConstructionTemplate& Template, Vector const& Inertia)
    {
        Template.AutoCheckRotInertia = 0;          // +0x20
        Template.InertiaBoxVector.x = Inertia.x;   // +0x24
        Template.InertiaBoxVector.y = Inertia.y;   // +0x28
        Template.InertiaBoxVector.z = Inertia.z;   // +0x2c
        Template.RotInertiaClampFactor = 0.0f;     // +0x34
    }

    // The CreateParms inputs BuildPartIVP_Real and FUN_0628a510 copy. Both also store an
    // unwritten stack float at +0x48; the port keeps the constructor's 0.
    void PhySys_FillTemplateDamping(IVP_RealObjectConstructionTemplate& Template, PhySys::CreateParms const& Parms)
    {
        Template.MassInput = Parms.Mass;                        // +0x1c <- +0x00
        Template.SpeedDampInput = Parms.LinearDamping;          // +0x38 <- +0x04
        Template.RotSpeedDampInputX = Parms.AngularDamping[0];  // +0x3c <- +0x08
        Template.RotSpeedDampInputY = Parms.AngularDamping[1];  // +0x40 <- +0x0c
        Template.RotSpeedDampInputZ = Parms.AngularDamping[2];  // +0x44 <- +0x10
    }

    // FUN_06289100, retail's BuildPartIVP_Real (cdecl).
    void PhySys_BuildPartIVP_Real(CObject* Object, PhyArch::Part const* Part, PhySys::CreateParms const& Parms)
    {
        if (Part->Surface == nullptr)
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kPhySysSource, 0x215,
                  "BuildPartIVP_Real(): No surface input for part_arch");
            return;
        }

        Object->PhyPart = Part;
        Object->SurfaceManager = new PhySys::PhySurfaceManager(Part->Surface, Object);
        // The collision state manager, unconditionally.
        Object->CollisionStateMgr = new PhySys::PhyCollisionStateManager(Object);

        IVP_RealObjectConstructionTemplate Template;
        Template.set_short_name(Parms.Name);
        PhySys_FillTemplateDamping(Template, Parms);
        Template.FlagsHi = Parms.bPhysicalUnmoveable;           // +0x0c <- +0x1c

        // Outside single player the mass centre is pinned to the object origin: an identity
        // matrix becomes the template's override.
        IVP_U_Matrix ComOverride;
        if (!SinglePlayer())
        {
            memset(&ComOverride, 0, sizeof(ComOverride));
            ComOverride.rows[0].k[0] = 1.0f;
            ComOverride.rows[1].k[1] = 1.0f;
            ComOverride.rows[2].k[2] = 1.0f;
            Template.OptionalComOverride = &ComOverride;
        }

        if (Parms.bHasInertia != 0)
            PhySys_FillTemplateInertia(Template, Parms.Inertia);

        // A miss only reports.
        std::map<unsigned, PhysicalSystem>::iterator SystemIt = G_PhysicalSystems.find(Parms.SystemId);
        if (SystemIt == G_PhysicalSystems.end())
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kPhySysSource, 0x211,
                  "Tried to create physics before Ipion was initialized.");
            return;
        }
        SystemIt->second.CreatePolygonBody(Object, &Template);
    }

    // The part-map entry {part id, instance, not a shield part} BuildIVP_Real and its walk
    // append.
    void PhySys_AppendPart(CObject* Object, unsigned PartId, long Instance)
    {
        FPartMapEntry Entry;
        Entry.PartId = PartId;
        Entry.EngineInstanceId = Instance;
        Entry.bShieldPart = 0;
        Object->PartMap.push_back(Entry);
    }

    // FUN_06288b30 (cdecl): one engine instance of BuildIVP_Real's walk. A free ('!fxd') part
    // becomes an object of its own; any other instance stays with Owner. Then the instance's
    // children, owned by whichever of the two it went to.
    void PhySys_BuildInstanceIVP_Real(CObject* Owner, long Instance, PhySys::CreateParms const& Parms)
    {
        IEngine* const Engine = DALib::Engine;

        // The part id is the CRC half of the instance name.
        FEngineName Name;
        Engine->GetInstanceName(&Name, PhySys_InstanceHandle(Instance));
        unsigned const PartId = Name.Crc;

        PhyArch::Part const* const Part = Owner->Archetype->get_phy_part(PartId);
        CObject* RecursionOwner = Owner;
        if (Part != nullptr && Part->bNotFixed == 0)
        {
            CObject* const New = CObject::Alloc(CObject::ClassObject);   // Alloc(1)
            New->open(Owner->Archetype);                                 // vtbl +0x4c
            Engine->SetInstanceTransform(PhySys_InstanceHandle(Instance), New); // IEngine +0x70
            PhySys_SetInstanceObject(Instance, New);                     // IEngine +0x7c
            PhySys_AppendPart(New, PartId, Instance);                    // FUN_0628f700
            PhySys_BuildPartIVP_Real(New, Part, Parms);                  // FUN_06289100
            PhySys::AttachGolemController(New, Parms);                   // FUN_06288e60
            RecursionOwner = New;
        }
        else
        {
            PhySys_SetInstanceObject(Instance, Owner);
            PhySys_AppendPart(Owner, PartId, Instance);
        }

        for (long Child = PhySys_RelatedInstance(Instance, 1, -1); Child != -1;
             Child = PhySys_RelatedInstance(Instance, 1, Child))
        {
            PhySys_BuildInstanceIVP_Real(RecursionOwner, Child, Parms);
        }
    }
}

// ============================================================================
// PhySys::BuildIVP_Real
// ============================================================================

// FUN_06288970
void __cdecl PhySys::BuildIVP_Real(CObject* Object, CreateParms const& Parms)
{
    long const Instance = Object->EngineInstanceId;

    PhySys_SetInstanceObject(Instance, Object);

    // The root part id, registered in the object's part map.
    FEngineName Name;
    DALib::Engine->GetInstanceName(&Name, PhySys_InstanceHandle(Instance));
    PhySys_AppendPart(Object, Name.Crc, Instance);

    PhyArch::Part const* Part = Object->Archetype->get_phy_part(Name.Crc);
    if (Part == nullptr)
        Part = PhyArch::GetDefaultPart();
    PhySys_BuildPartIVP_Real(Object, Part, Parms);

    // No body, no children.
    if (Object->BodyPtr == nullptr)
    {
        char const* const PartName = (Name.Text != nullptr) ? Name.Text : "";
        FDUMP(0x100001, "%s(%d) : *** ERROR: BuildIVP_Real(): failed to create ivp_real for part (%s) in file (%s).",
              kPhySysSource, 0x27a, PartName, Object->Archetype->DaArchetype.Value);
        return;
    }

    // The child parts get a copy without bPhysicalUnmoveable, with the golem-child (golem) or
    // default (merged) mass and damping.
    CreateParms ChildParms = Parms;
    ChildParms.bPhysicalUnmoveable = 0;
    if (Parms.Type == PhySys::ANIM_TECHNIQUE_GOLEM)
    {
        ChildParms.Mass = GOLEM_CHILD_MASS;
        ChildParms.LinearDamping = GOLEM_CHILD_LINEAR_DAMP;
        ChildParms.AngularDamping[0] = GOLEM_CHILD_ANGULAR_DAMP.x;
        ChildParms.AngularDamping[1] = GOLEM_CHILD_ANGULAR_DAMP.y;
        ChildParms.AngularDamping[2] = GOLEM_CHILD_ANGULAR_DAMP.z;
    }
    else if (Parms.Type == PhySys::ANIM_TECHNIQUE_MERGED)
    {
        ChildParms.Mass = 1.0f;
        ChildParms.LinearDamping = DEFAULT_LINEAR_DAMPING;
        ChildParms.AngularDamping[0] = DEFAULT_ANGULAR_DAMPING.x;
        ChildParms.AngularDamping[1] = DEFAULT_ANGULAR_DAMPING.y;
        ChildParms.AngularDamping[2] = DEFAULT_ANGULAR_DAMPING.z;
    }

    for (long Child = PhySys_RelatedInstance(Instance, 1, -1); Child != -1;
         Child = PhySys_RelatedInstance(Instance, 1, Child))
    {
        PhySys_BuildInstanceIVP_Real(Object, Child, ChildParms);
    }

    if (Parms.bEnableCollisions != 0)
        Object->enable_collisions_r(true);
}

// ============================================================================
// PhySys::BuildIVP_Sphere
// ============================================================================

// FUN_06289370
void __cdecl PhySys::BuildIVP_Sphere(CObject* Object, float Radius, CreateParms const& Parms)
{
    IVP_RealObjectConstructionTemplate Template;
    Template.set_short_name(Parms.Name);

    Template.MassInput = Parms.Mass;                        // +0x1c <- +0x00
    Template.FlagsHi = Parms.bPhysicalUnmoveable;           // +0x0c <- +0x1c
    Template.SpeedDampInput = Parms.LinearDamping;          // +0x38 <- +0x04
    Template.RotSpeedDampInputX = Parms.AngularDamping[0];  // +0x3c <- +0x08
    Template.RotSpeedDampInputY = Parms.AngularDamping[1];  // +0x40 <- +0x0c
    Template.RotSpeedDampInputZ = Parms.AngularDamping[2];  // +0x44 <- +0x10
    // Retail stores an unwritten stack float at +0x48; the port keeps the constructor's zero.

    // No miss test: a miss builds into the map head node's unconstructed storage (retail
    // quirk, see PhySys_FindSystem).
    PhysicalSystem& System = PhySys_FindSystem(Parms.SystemId);

    if (Template.Material == nullptr)
        Template.Material = G_PhySysMaterial;

    IVP_Ball* const Body = System.CreateBallBody(Object->WorldPosition, Object->Orientation, Radius, &Template);
    Object->BodyPtr = Body;
    Object->PhysicsSystemId = System.SystemId;
    Body->client_data = Object;

    // With bEnableCollisions and a body, a collision state manager, enabled immediately.
    if (Parms.bEnableCollisions != 0 && Object->BodyPtr != nullptr)
    {
        PhyCollisionStateManager* const Manager = new PhyCollisionStateManager(Object);
        Object->CollisionStateMgr = Manager;
        Manager->enable_collisions(true, true);
    }

    PhySys_SetInstanceObject(Object->EngineInstanceId, Object);
}

// ============================================================================
// PhySys::Attach / Detach
// ============================================================================
//
// Ghidra's decompilation of this cluster is corrupted (the recursive helpers' stack arguments
// are misread); everything below follows the listing. The two IVP calls are
//   FUN_06343700(ParentBody, ChildBody, -1.0f) -- IVP_Merge_Object_Into_Core (Attach / weld);
//   FUN_06343940(Body, &Template)              -- IVP_Split_Object_From_Core.

namespace
{
    // The {object, desired collision state} records Attach / Detach save while they pull a
    // subtree apart.
    using FCollisionSaveList = std::vector<std::pair<CObject*, bool>>;

    // FUN_06284a90 (cdecl). Also inlined with the same shape in FUN_06284ae0, FUN_06284bc0 and
    // PhySys::Attach.
    void PhySys_LinkBodies(CObject* Parent, CObject* Child)
    {
        IVP_Real_Object* const ParentBody = Parent->BodyPtr;
        if (ParentBody == nullptr)
            return;
        IVP_Real_Object* const ChildBody = Child->BodyPtr;
        if (ChildBody == nullptr)
            return;

        strncpy(ChildBody->name, ParentBody->name, 8);

        IVP_Merge_Object_Into_Core(ParentBody, ChildBody, -1.0f);

        Child->PhysicsControlMode = PhySys::ANIM_TECHNIQUE_MERGED;
    }

    // FUN_062847e0 (cdecl): detach Object's body from the core it shares, with DetachParms as
    // the new core's mass / damping / inertia.
    void PhySys_DetachBody(CObject* Object, PhySys::DetachParms const& Parms)
    {
        IVP_Real_Object* const Body = Object->BodyPtr;
        if (Body != nullptr)
        {
            // Named only for a non-empty name.
            IVP_RealObjectConstructionTemplate Template;
            if (Parms.Name[0] != 0)
                Template.set_short_name(Parms.Name);

            // Retail stores an unwritten stack float at +0x30 and +0x48; the port keeps the
            // constructor's values.
            Template.MassInput = Parms.Mass;                            // +0x1c <- +0x18
            if (Parms.bHasInertia != 0)
                PhySys_FillTemplateInertia(Template, Parms.Inertia);    // +0x20..+0x34

            Template.SpeedDampInput = Parms.LinearDamping;              // +0x38 <- +0x00
            Template.RotSpeedDampInputX = Parms.AngularDamping.x;       // +0x3c <- +0x04
            Template.RotSpeedDampInputY = Parms.AngularDamping.y;       // +0x40 <- +0x08
            Template.RotSpeedDampInputZ = Parms.AngularDamping.z;       // +0x44 <- +0x0c
            Template.Material = G_PhySysMaterial;                       // +0x18 <- DAT_063fc0bc
            Template.FlagsHi = 0;                                       // +0x0c

            // A ball (object_type 3) keeps its radius.
            if (Body->object_type == 3)
                Template.ExtraRadius = Body->extra_radius;

            IVP_Split_Object_From_Core(Body, &Template);
        }

        Object->PhysicsControlMode = PhySys::ANIM_TECHNIQUE_NONE;
    }

    // FUN_06284ae0 (cdecl): walk ObjId's children; an instance of another object (not null,
    // not PrevRef) is linked onto NewParent and becomes PrevRef for its own subtree, any other
    // instance is descended into with PrevRef unchanged.
    void PhySys_ReparentSubtree(long ObjId, CObject* NewParent, CObject* PrevRef)
    {
        if (ObjId == -1)
            return;

        for (long ChildId = PhySys_RelatedInstance(ObjId, 1, -1); ChildId != -1;
             ChildId = PhySys_RelatedInstance(ObjId, 1, ChildId))
        {
            CObject* const ChildObject = PhySys_InstanceObject(ChildId);
            if (ChildObject != nullptr && ChildObject != PrevRef)
            {
                PhySys_LinkBodies(NewParent, ChildObject);
                PhySys_ReparentSubtree(ChildId, NewParent, ChildObject);
            }
            else
            {
                PhySys_ReparentSubtree(ChildId, NewParent, PrevRef);
            }
        }
    }

    // FUN_06284bc0 (cdecl): the direct children of Container's instance (the id is read once),
    // each linked onto NewParent unless it is Container's own (or nobody's), then
    // PhySys_ReparentSubtree below it. Caller: PhySys_SplitInstance with NewParent ==
    // Container.
    void PhySys_LinkAllDirectChildren(CObject* NewParent, CObject* Container)
    {
        long const ObjId = Container->EngineInstanceId;
        if (ObjId == -1)
            return;

        for (long ChildId = PhySys_RelatedInstance(ObjId, 1, -1); ChildId != -1;
             ChildId = PhySys_RelatedInstance(ObjId, 1, ChildId))
        {
            CObject* const ChildObject = PhySys_InstanceObject(ChildId);
            if (ChildObject != nullptr && ChildObject != Container)
            {
                PhySys_LinkBodies(NewParent, ChildObject);
                PhySys_ReparentSubtree(ChildId, NewParent, ChildObject);
            }
            else
            {
                PhySys_ReparentSubtree(ChildId, NewParent, Container);
            }
        }
    }

    // FUN_06284690 (cdecl): walk ObjId's children. Every instance of another object (not null,
    // not Ref) has its desired collision state saved, its collisions switched off and its body
    // detached with default parms, and becomes Ref for its own subtree; any other instance is
    // descended into with Ref unchanged.
    void PhySys_DisableCollisionsAndDetachSubtree(long ObjId, CObject* Ref, FCollisionSaveList& SaveList)
    {
        if (ObjId == -1)
            return;

        for (long ChildId = PhySys_RelatedInstance(ObjId, 1, -1); ChildId != -1;
             ChildId = PhySys_RelatedInstance(ObjId, 1, ChildId))
        {
            CObject* const ChildObject = PhySys_InstanceObject(ChildId);
            if (ChildObject != nullptr && ChildObject != Ref)
            {
                // False without a body. The collision manager itself is not tested.
                bool const bDesired = (ChildObject->BodyPtr != nullptr)
                    ? ChildObject->CollisionStateMgr->get_desired_collision_state()
                    : false;
                SaveList.push_back(std::make_pair(ChildObject, bDesired));

                if (ChildObject->BodyPtr != nullptr)
                    ChildObject->CollisionStateMgr->enable_collisions(false, true);

                // Retail leaves the unread bytes of the defaults unwritten; the port zeroes
                // them.
                PhySys::DetachParms Defaults = {};
                Defaults.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
                Defaults.AngularDamping = PhySys::DEFAULT_ANGULAR_DAMPING;
                Defaults.Name[0] = 0;
                Defaults.Mass = 1.0f;
                Defaults.bHasInertia = 0;
                Defaults.Inertia.x = 0.0f;
                Defaults.Inertia.y = 0.0f;
                Defaults.Inertia.z = 0.0f;
                PhySys_DetachBody(ChildObject, Defaults);

                PhySys_DisableCollisionsAndDetachSubtree(ChildId, ChildObject, SaveList);
            }
            else
            {
                PhySys_DisableCollisionsAndDetachSubtree(ChildId, Ref, SaveList);
            }
        }
    }

    // The restore loop Attach and Detach inline: every saved object that has a body gets its
    // saved state back, immediately.
    void PhySys_RestoreCollisionStates(FCollisionSaveList const& SaveList)
    {
        for (size_t Index = 0; Index < SaveList.size(); ++Index)
        {
            CObject* const Saved = SaveList[Index].first;
            if (Saved->BodyPtr != nullptr)
                Saved->CollisionStateMgr->enable_collisions(SaveList[Index].second, true);
        }
    }
}

// FUN_06284ca0. Child's subtree is pulled apart (collision states saved), Child is linked onto
// Parent, then every other object hanging off Child's instance, and the saved collision states
// come back.
void __cdecl PhySys::Attach(CObject* Parent, CObject* Child)
{
    FCollisionSaveList SaveList;
    PhySys_DisableCollisionsAndDetachSubtree(Child->EngineInstanceId, Child, SaveList);

    PhySys_LinkBodies(Parent, Child);

    long const ChildId = Child->EngineInstanceId;
    if (ChildId != -1)
    {
        for (long BelowId = PhySys_RelatedInstance(ChildId, 1, -1); BelowId != -1;
             BelowId = PhySys_RelatedInstance(ChildId, 1, BelowId))
        {
            CObject* const BelowObject = PhySys_InstanceObject(BelowId);
            if (BelowObject != nullptr && BelowObject != Child)
            {
                PhySys_LinkBodies(Parent, BelowObject);
                PhySys_ReparentSubtree(BelowId, Parent, BelowObject);
            }
            else
            {
                PhySys_ReparentSubtree(BelowId, Parent, Child);
            }
        }
    }

    PhySys_RestoreCollisionStates(SaveList);
}

// FUN_06284910. Mode 2 (welded): pull the subtree apart, detach the body with Parms, re-link
// everything below Object onto Object, restore the collision states. Mode 1 (golem): delete
// the controller. Every mode ends at 0.
void __cdecl PhySys::Detach(CObject* Object, DetachParms const& Parms)
{
    if (Object->PhysicsControlMode == PhySys::ANIM_TECHNIQUE_MERGED)
    {
        FCollisionSaveList SaveList;
        PhySys_DisableCollisionsAndDetachSubtree(Object->EngineInstanceId, Object, SaveList);

        PhySys_DetachBody(Object, Parms);

        long const ObjId = Object->EngineInstanceId;
        if (ObjId != -1)
        {
            for (long BelowId = PhySys_RelatedInstance(ObjId, 1, -1); BelowId != -1;
                 BelowId = PhySys_RelatedInstance(ObjId, 1, BelowId))
            {
                CObject* const BelowObject = PhySys_InstanceObject(BelowId);
                if (BelowObject != nullptr && BelowObject != Object)
                {
                    PhySys_LinkBodies(Object, BelowObject);
                    PhySys_ReparentSubtree(BelowId, Object, BelowObject);
                }
                else
                {
                    PhySys_ReparentSubtree(BelowId, Object, Object);
                }
            }
        }

        PhySys_RestoreCollisionStates(SaveList);
        Object->PhysicsControlMode = PhySys::ANIM_TECHNIQUE_NONE;
        return;
    }

    if (Object->PhysicsControlMode == PhySys::ANIM_TECHNIQUE_GOLEM)
    {
        IVP_Controller* const Controller = Object->GolemController;
        if (Controller != nullptr)
            delete Controller;
        Object->GolemController = nullptr;
    }

    Object->PhysicsControlMode = PhySys::ANIM_TECHNIQUE_NONE;
}

// FUN_06345450: the body's virtual scalar deleting destructor, null-tested. Retail calls it
// with the object's old body pointer, after the field is cleared. Shared by DeactivatePhysics
// and UnMakePhysicalR (FUN_0628a7d0), which releases only the body (no controller or collision
// state manager).
void CmnPhySys::PhySys_DeleteBody(IVP_Real_Object* Body)
{
    // ~IVP_Object is virtual, so this dispatches to IVP_Polygon / IVP_Ball as the vtable call
    // does.
    if (Body != nullptr)
    {
        delete Body;
    }
}

static void PhySys_ReleaseBody(CObject* Object)
{
    IVP_Real_Object* const Body = Object->BodyPtr;
    Object->BodyPtr = nullptr;
    Object->PhysicsSystemId = 0;
    PhySys_DeleteBody(Body);
}

void __cdecl PhySys::DeactivatePhysics(CObject* Object)
{
    // FUN_06285350. No null test on Object.

    // The golem / remote controller; cleared either way.
    IVP_Controller* const Controller = Object->GolemController;
    if (Controller != nullptr)
        delete Controller;
    Object->GolemController = nullptr;

    PhyCollisionStateManager* const Manager = Object->CollisionStateMgr;
    if (Manager != nullptr)
        delete Manager;
    Object->CollisionStateMgr = nullptr;

    // Only with a body are BodyPtr / PhysicsSystemId cleared.
    IVP_Real_Object* const Body = Object->BodyPtr;
    if (Body != nullptr)
    {
        Object->BodyPtr = nullptr;
        Object->PhysicsSystemId = 0;
        PhySys_DeleteBody(Body);
    }

    IVP_SurfaceManager* const SurfaceManager = Object->SurfaceManager;
    if (SurfaceManager != nullptr)
        delete SurfaceManager;
    Object->SurfaceManager = nullptr;
}

// FUN_06285490 (cdecl Object, Instance), the companion DeactivatePhysicsR calls when a child
// instance belongs to Object itself. Ghidra's decompilation of it is corrupted; this follows
// the listing. A child object other than Object gets the inlined DeactivatePhysics body and is
// recursed into with its own instance; a child that is Object itself is recursed into with the
// raw child instance. Neither the engine nor the child object is tested.
static void PhySys_DeactivatePhysicsSelfLoop(CObject* Object, long Instance)
{
    for (long Child = PhySys_RelatedInstance(Instance, 1, -1); Child != -1;
         Child = PhySys_RelatedInstance(Instance, 1, Child))
    {
        CObject* const ChildObject = PhySys_InstanceObject(Child);
        if (ChildObject != Object)
        {
            PhySys::DeactivatePhysics(ChildObject);
            PhySys_DeactivatePhysicsSelfLoop(ChildObject, ChildObject->EngineInstanceId);
        }
        else
        {
            PhySys_DeactivatePhysicsSelfLoop(ChildObject, Child);
        }
    }
}

// FUN_062853c0. The inlined DeactivatePhysics body on Object, then every child instance: a
// child object other than Object goes through DeactivatePhysicsR, Object itself through
// FUN_06285490 with the child instance. Nothing is tested.
void __cdecl PhySys::DeactivatePhysicsR(CObject* Object)
{
    DeactivatePhysics(Object);

    long const Instance = Object->EngineInstanceId;
    for (long Child = PhySys_RelatedInstance(Instance, 1, -1); Child != -1;
         Child = PhySys_RelatedInstance(Instance, 1, Child))
    {
        CObject* const ChildObject = PhySys_InstanceObject(Child);
        if (ChildObject == Object)
            PhySys_DeactivatePhysicsSelfLoop(ChildObject, Child);
        else
            DeactivatePhysicsR(ChildObject);
    }
}

// ============================================================================
// ReMakePhysicalR / UnMakePhysicalR
// ============================================================================

namespace
{
    void PhySys_ReMakeInstance(CObject* Owner, long Instance, PhySys::CreateParms const& Parms);

    // FUN_0628a510 (cdecl): a new body for an object that keeps its surface manager (or a
    // ball of Radius without one), in Parms' system, then the same for every other object
    // hanging off its instance tree.
    void PhySys_ReMakeBody(CObject* Object, PhySys::CreateParms const& Parms, float Radius)
    {
        IVP_RealObjectConstructionTemplate Template;
        Template.set_short_name(Parms.Name);
        // FlagsHi is 0 here (the build paths copy bPhysicalUnmoveable instead).
        PhySys_FillTemplateDamping(Template, Parms);
        Template.FlagsHi = 0;
        if (Parms.bHasInertia != 0)
            PhySys_FillTemplateInertia(Template, Parms.Inertia);

        // A miss is not tested (retail quirk, see PhySys_FindSystem).
        PhysicalSystem& System = PhySys_FindSystem(Parms.SystemId);

        if (Object->SurfaceManager != nullptr)
        {
            System.CreatePolygonBody(Object, &Template);
        }
        else
        {
            if (Template.Material == nullptr)
                Template.Material = G_PhySysMaterial;
            IVP_Ball* const Body = System.CreateBallBody(Object->WorldPosition, Object->Orientation, Radius, &Template);
            Object->BodyPtr = Body;
            Object->PhysicsSystemId = System.SystemId;
            Body->client_data = Object;
        }

        long const Instance = Object->EngineInstanceId;
        for (long Child = PhySys_RelatedInstance(Instance, 1, -1); Child != -1;
             Child = PhySys_RelatedInstance(Instance, 1, Child))
        {
            PhySys_ReMakeInstance(Object, Child, Parms);
        }
    }

    // FUN_0628a700 (cdecl): an instance that belongs to another object rebuilds that object
    // (as a ball of radius 0 if it has no surface manager) and re-attaches it to Owner's root;
    // an instance of Owner (or of nobody) is descended into.
    void PhySys_ReMakeInstance(CObject* Owner, long Instance, PhySys::CreateParms const& Parms)
    {
        CObject* const Child = PhySys_InstanceObject(Instance);
        if (Child != nullptr && Child != Owner)
        {
            PhySys_ReMakeBody(Child, Parms, 0.0f);
            CObject* const Root = PhySys_InstanceObject(GetRoot(Owner->EngineInstanceId));
            PhySys::Attach(Root, Child);
            // No test on the manager.
            if (Parms.bEnableCollisions != 0)
                Child->CollisionStateMgr->enable_collisions(true, true);
            return;
        }

        for (long Below = PhySys_RelatedInstance(Instance, 1, -1); Below != -1;
             Below = PhySys_RelatedInstance(Instance, 1, Below))
        {
            PhySys_ReMakeInstance(Owner, Below, Parms);
        }
    }
}

// FUN_0628a4e0
void __cdecl PhySys::ReMakePhysicalR(CObject* Object, CreateParms const& Parms, float Radius)
{
    PhySys_ReMakeBody(Object, Parms, Radius);
    if (Parms.bEnableCollisions != 0)
        Object->enable_collisions_r(true);
}

// FUN_0628a8b0 (cdecl Owner, Instance): walks the instances below Instance that still resolve
// to Owner (or to no CObject). An instance that resolves to a different CObject has that
// object's body released, and the walk continues rooted at it. Nothing is tested but the user
// data.
static void PhySys_UnMakePhysicalWalk(CObject* Owner, long Instance)
{
    CObject* const Owning = PhySys_InstanceObject(Instance);
    if (Owning == nullptr || Owning == Owner)
    {
        for (long Child = PhySys_RelatedInstance(Instance, 1, -1); Child != -1;
             Child = PhySys_RelatedInstance(Instance, 1, Child))
        {
            PhySys_UnMakePhysicalWalk(Owner, Child);
        }
        return;
    }

    PhySys_ReleaseBody(Owning);

    // Its own instance, re-read every step.
    if (Owning->EngineInstanceId == -1)
        return;
    for (long Child = PhySys_RelatedInstance(Owning->EngineInstanceId, 1, -1); Child != -1;
         Child = PhySys_RelatedInstance(Owning->EngineInstanceId, 1, Child))
    {
        PhySys_UnMakePhysicalWalk(Owning, Child);
    }
}

// FUN_0628a7d0. FUN_0628a990 is an instruction-identical copy, called for a child owned by
// another object; the port recurses into this function instead. Object is not tested.
void __cdecl PhySys::UnMakePhysicalR(CObject* Object)
{
    PhySys_ReleaseBody(Object);

    // The instance is re-read every step.
    if (Object->EngineInstanceId == -1)
        return;
    for (long Child = PhySys_RelatedInstance(Object->EngineInstanceId, 1, -1); Child != -1;
         Child = PhySys_RelatedInstance(Object->EngineInstanceId, 1, Child))
    {
        CObject* const Owning = PhySys_InstanceObject(Child);
        if (Owning != nullptr && Owning != Object)
        {
            PhySys::UnMakePhysicalR(Owning);
            continue;
        }

        // Otherwise each of the child's own children through FUN_0628a8b0.
        for (long Grandchild = PhySys_RelatedInstance(Child, 1, -1); Grandchild != -1;
             Grandchild = PhySys_RelatedInstance(Child, 1, Grandchild))
        {
            PhySys_UnMakePhysicalWalk(Object, Grandchild);
        }
    }
}

// FUN_0628a0c0 (cdecl): DetachInstance's sub-part branch. Instance becomes a new object of
// Owner's archetype with a polygon body of its own part, takes along every part-map entry and
// instance below it, and inherits Owner's velocity.
static CObject* PhySys_SplitInstance(CObject* Owner, long Instance, PhySys::DetachParms const& Parms)
{
    IEngine* const Engine = DALib::Engine;

    CObject* const New = CObject::Alloc(CObject::ClassObject);
    Engine->SetInstanceTransform(PhySys_InstanceHandle(Instance), New);
    New->open(Owner->Archetype);

    // The part of Instance, or the default part.
    FEngineName Name;
    Engine->GetInstanceName(&Name, PhySys_InstanceHandle(Instance));
    PhyArch::Part const* Part = Owner->Archetype->get_phy_part(Name.Crc);
    if (Part == nullptr)
        Part = PhyArch::GetDefaultPart();

    // No surface test here (BuildPartIVP_Real has one).
    New->PhyPart = Part;
    New->SurfaceManager = new PhySys::PhySurfaceManager(Part->Surface, New);
    New->CollisionStateMgr = new PhySys::PhyCollisionStateManager(New);

    // Retail stores an unwritten stack float at +0x48 (and +0x30 with an inertia); the port
    // keeps the constructor's values.
    IVP_RealObjectConstructionTemplate Template;
    Template.FlagsHi = 0;                                          // +0x0c
    Template.MassInput = Parms.Mass;                               // +0x1c <- +0x18
    Template.SpeedDampInput = Parms.LinearDamping;                 // +0x38 <- +0x00
    Template.RotSpeedDampInputX = Parms.AngularDamping.x;          // +0x3c <- +0x04
    Template.RotSpeedDampInputY = Parms.AngularDamping.y;          // +0x40 <- +0x08
    Template.RotSpeedDampInputZ = Parms.AngularDamping.z;          // +0x44 <- +0x0c
    Template.set_short_name(Parms.Name);                           // +0x10
    if (Parms.bHasInertia != 0)
        PhySys_FillTemplateInertia(Template, Parms.Inertia);       // +0x20..+0x34 <- +0x20..+0x28

    // Owner's system; a miss is not tested (retail quirk, see PhySys_FindSystem).
    PhySys_FindSystem(Owner->PhysicsSystemId).CreatePolygonBody(New, &Template);

    PhySys_SetInstanceObject(Instance, New);

    // Every instance below Instance (relation 0) that Owner owned now belongs to New.
    for (long Below = PhySys_RelatedInstance(Instance, 0, -1); Below != -1;
         Below = PhySys_RelatedInstance(Instance, 0, Below))
    {
        if (PhySys_InstanceObject(Below) == Owner)
            PhySys_SetInstanceObject(Below, New);
    }

    // The part-map entries under Instance move to New, in order.
    std::vector<FPartMapEntry>::iterator Entry = Owner->PartMap.begin();
    while (Entry != Owner->PartMap.end())
    {
        if (IsDescendant(Instance, Entry->EngineInstanceId))
        {
            New->PartMap.push_back(*Entry);
            Entry = Owner->PartMap.erase(Entry);
        }
        else
        {
            ++Entry;
        }
    }

    PhySys_LinkAllDirectChildren(New, New);

    if (PhySys::GetCollisionState(Owner) && New->BodyPtr != nullptr)
        New->CollisionStateMgr->enable_collisions(true, true);

    PhySys::AddToVelocity(New, PhySys::GetVelocity(Owner));
    return New;
}

// ---- Instance detach helpers ----
// Callers: CNonPhysAttachment::Disconnect, CPhysAttachment::Disconnect, CArchGroup::Destroy.
// Retail has no null guards in either function.

// FUN_0628a410
void __cdecl DetachInstanceInPlace(long InstanceId)
{
    long const ParentId = PhySys_InstanceParent(InstanceId);
    DALib::Engine->DestroyJoint(PhySys_InstanceHandle(ParentId), PhySys_InstanceHandle(InstanceId));
    CObject* const Owner = PhySys_InstanceObject(InstanceId);

    if (Owner->EngineInstanceId == InstanceId)
    {
        Owner->Release();
        return;
    }

    // A sub-part: erase every PartMap entry whose instance descends from it, then destroy the
    // instance.
    std::vector<FPartMapEntry>::iterator Entry = Owner->PartMap.begin();
    while (Entry != Owner->PartMap.end())
    {
        if (IsDescendant(InstanceId, Entry->EngineInstanceId))
            Entry = Owner->PartMap.erase(Entry);
        else
            ++Entry;
    }
    DALib::Engine->DestroyInstance(PhySys_InstanceHandle(InstanceId));
}

// FUN_0628a050
CObject* __cdecl DetachInstance(long InstanceId, PhySys::DetachParms const& Parms)
{
    long const ParentId = PhySys_InstanceParent(InstanceId);
    DALib::Engine->DestroyJoint(PhySys_InstanceHandle(ParentId), PhySys_InstanceHandle(InstanceId));
    CObject* const Owner = PhySys_InstanceObject(InstanceId);
    if (Owner->EngineInstanceId == InstanceId)
    {
        PhySys::Detach(Owner, Parms);
        return Owner;
    }
    // A sub-part is split into a new object.
    return PhySys_SplitInstance(Owner, InstanceId, Parms);
}
