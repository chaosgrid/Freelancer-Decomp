// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_cobject.h"
#include "Objects/common_cobject_internal.h" // CmnCobject helpers defined below
#include "x86math.h"

#include "common_cobjdb.h"   // IObjDB::GetZoneDb
#include "common_goods.h"
#include "common_physys.h"
#include "common_iface.h"
#include "common_universe.h" // Universe::ISystem (CmnCobject::GetObjectDbCollisionGroup)
#include "common_misc.h"     // Heap (CObject::operator new/delete pools)

#include "DALib.h"
#include "dacom.h"

#include <cstdint>
#include <list>
#include <new>
#include <stdlib.h>

namespace
{
    const char kCObjectSource[] = "E:\\FL\\Scratch\\Source\\Common\\CObject.cpp";

    // Per-class instance lists (FUN_062ae690 / FUN_062af9b0).
    // Not the Vibe map DAT_064018c8 (CleanUpDatabases / FreeDynamic).
    enum EClassList
    {
        ListObject = 0,
        ListMine,
        ListCounterMeasure,
        ListBeam,
        ListGuided,
        ListSolar,
        ListAsteroid,
        ListShip,
        ListLoot,
        ListEquipment,
        ListDynamicAsteroid,
        ListCount
    };

    std::list<CObject*> G_ClassLists[ListCount];
    std::list<CObject*>* G_FindList = nullptr;
    std::list<CObject*>::iterator G_FindNextIt;
    bool G_bFindActive = false;

    // The class -> list switch inlined at the head of Count / Find / FindFirst: exactly the
    // eleven concrete classes own a list; any other value (0, ClassSimple, ClassEqObj, ...) has
    // none.
    std::list<CObject*>* ListForClass(CObject::Class ObjectClass)
    {
        switch (ObjectClass)
        {
        case CObject::ClassObject:          return &G_ClassLists[ListObject];          // 063fc32c
        case CObject::ClassMine:            return &G_ClassLists[ListMine];            // 063fc374
        case CObject::ClassCounterMeasure:  return &G_ClassLists[ListCounterMeasure];  // 063fc368
        case CObject::ClassBeam:            return &G_ClassLists[ListBeam];            // 063fc398
        case CObject::ClassGuided:          return &G_ClassLists[ListGuided];          // 063fc38c
        case CObject::ClassSolar:           return &G_ClassLists[ListSolar];           // 063fc35c
        case CObject::ClassShip:            return &G_ClassLists[ListShip];            // 063fc3a4
        case CObject::ClassLoot:            return &G_ClassLists[ListLoot];            // 063fc380
        case CObject::ClassAsteroid:        return &G_ClassLists[ListAsteroid];        // 063fc350
        case CObject::ClassEquipmentObj:    return &G_ClassLists[ListEquipment];       // 063fc338
        case CObject::ClassDynamicAsteroid: return &G_ClassLists[ListDynamicAsteroid]; // 063fc344
        default:                            return nullptr;
        }
    }

    bool ClassHasSimpleId(CObject::Class ObjectClass)
    {
        return ObjectClass != CObject::ClassObject
            && ObjectClass != CObject::ClassEquipmentObj
            && ObjectClass != CObject::ClassDynamicAsteroid;
    }

    void RemoveFromClassList(CObject* Object)
    {
        if (Object == nullptr)
            return;
        std::list<CObject*>* List = ListForClass(Object->ObjectClass);
        if (List == nullptr)
            return;
        std::list<CObject*>::iterator It = List->begin();
        for (; It != List->end(); ++It)
        {
            if (*It == Object)
            {
                if (G_bFindActive && G_FindList == List && G_FindNextIt == It)
                    ++G_FindNextIt;
                List->erase(It);
                return;
            }
        }
    }
}

// Helpers shared by several CObject-family TUs; declared in common_cobject_internal.h.
namespace CmnCobject
{

    // Rotates a world-space vector into object space: Orientation transposed into a local,
    // then the cached 3DMathEngine's TransformMatrix (slot +0x2c). Inlined in the
    // init_physics of CGuided, CMine, CCounterMeasure, CLoot and CShip before
    // PhySys::AddToAngularVelocityOS, which takes an object-space delta.
    Vector RotateWorldToObjectSpace(Matrix const& Orient, Vector const& World)
    {
        Matrix Transposed;
        Transposed.E[0][0] = Orient.E[0][0];
        Transposed.E[0][1] = Orient.E[1][0];
        Transposed.E[0][2] = Orient.E[2][0];
        Transposed.E[1][0] = Orient.E[0][1];
        Transposed.E[1][1] = Orient.E[1][1];
        Transposed.E[1][2] = Orient.E[2][1];
        Transposed.E[2][0] = Orient.E[0][2];
        Transposed.E[2][1] = Orient.E[1][2];
        Transposed.E[2][2] = Orient.E[2][2];
        Vector Out;
        Acquire3DMathEngine()->TransformMatrix((FVector&)Out, (FMatrix const&)Transposed,
                                               (FVector const&)World);
        return Out;
    }

    // The system id: IObjDB slot 2 (GetZoneDb) then ISystem slot 0 (SYSTEM 062969c0, +0x4c),
    // both __thiscall with no arguments. Inlined in the init_physics of CGuided, CMine,
    // CCounterMeasure, CLoot, CShip and CSolar; none tests the database or the system for null.
    unsigned GetObjectDbCollisionGroup(IObjDB* Db)
    {
        Universe::ISystem const* const System = Db->GetZoneDb();
        return System->GetId();
    }

    void CopyMatrix9(Matrix& Dst, Matrix const& Src)
    {
        unsigned Row = 0;
        for (; Row < 3; ++Row)
        {
            unsigned Col = 0;
            for (; Col < 3; ++Col)
                Dst.E[Row][Col] = Src.E[Row][Col];
        }
    }
} // namespace CmnCobject

void DACOM_API CObject::destroy_instance(long Inst)
{
    // FUN_062af610 (stdcall). The deleting dtor is vtable +0x38.
    (void)Inst;
    EngineInstanceId = -1;
    if (RefCount == 1)
    {
        RefCount = 0;
        delete this;
    }
}

CObject::CObject(Class ObjectClass)
    : EngineObject()
{
    // FUN_062af2e0. The node-pool refcount (DAT_063fc438) around the containers is VC6
    // allocator internals.
    this->ObjectClass = ObjectClass;
    PhysicsSystemId = 0;
    BodyPtr = nullptr;
    PhyPart = nullptr;
    SurfaceManager = nullptr;
    GolemController = nullptr;
    PhysicsControlMode = 0;
    CollisionStateMgr = nullptr;
    Archetype = nullptr;
    RefCount = 0;
}

// No standalone CObject copy ctor exists in retail; this is the CObject half inlined in
// CSimple::CSimple(CSimple const&) (FUN_062811b0), in its copy order.
CObject::CObject(CObject const& Other)
    : EngineObject(Other)
    , ObjectClass(Other.ObjectClass)
    , PhysicsSystemId(Other.PhysicsSystemId)
    , BodyPtr(Other.BodyPtr)
    , PhyPart(Other.PhyPart)
    , SurfaceManager(Other.SurfaceManager)
    , GolemController(Other.GolemController)
    , PartMap(Other.PartMap)
    , PhysicsControlMode(Other.PhysicsControlMode)
    , Phantoms(Other.Phantoms)
    , CollisionStateMgr(Other.CollisionStateMgr)
    , Archetype(Other.Archetype)
    , Advisors(Other.Advisors)
    , RefCount(Other.RefCount)
{
    // The containers are deep-copied; the archetype is not AddRef'd and RefCount (+0x98) is the
    // source's live count.
}

CObject::~CObject()
{
    // FUN_062af440
    RemoveFromClassList(this);
    // The field is not reset.
    if (EngineInstanceId != -1)
        DALib::Engine->DestroyInstance((void*)EngineInstanceId);
    // CBase::notify_of_destruction is the ICF body at 062ed730.
    for (std::list<CBase*>::iterator It = Advisors.begin(); It != Advisors.end(); ++It)
        (*It)->notify_of_destruction(this);
    // DetachPhantom unlinks the node itself.
    while (!Phantoms.empty())
        PhySys::DetachPhantom(Phantoms.front());
    PhySys::DeactivatePhysics(this);
    if (Archetype != nullptr)
    {
        Archetype->Release();
        Archetype = nullptr;
    }
}

// The six per-size pools behind CObject::operator new/delete, built by static initialisers
// (062adaa0..062adb92) as Heap(Size, -1) at .data 0x063fc400 (0x9c), 0x063fc3f0 (0x120),
// 0x063fc3e0 (0x104), 0x063fc3d0 (0xf0), 0x063fc3c0 (0x1ec), 0x063fc3b0 (0x2dc).
static Heap GObjectHeap9c(0x9c, -1);
static Heap GObjectHeap120(0x120, -1);
static Heap GObjectHeap104(0x104, -1);
static Heap GObjectHeapF0(0xf0, -1);
static Heap GObjectHeap1ec(0x1ec, -1);
static Heap GObjectHeap2dc(0x2dc, -1);

void* CObject::operator new(unsigned int Size) noexcept
{
    // FUN_062adbc0. Any other size goes to the CRT malloc.
    switch (Size)
    {
    case 0x9c:
        return GObjectHeap9c.Malloc();
    case 0xf0:
        return GObjectHeapF0.Malloc();
    case 0x104:
        return GObjectHeap104.Malloc();
    case 0x120:
        return GObjectHeap120.Malloc();
    case 0x1ec:
        return GObjectHeap1ec.Malloc();
    case 0x2dc:
        return GObjectHeap2dc.Malloc();
    default:
        return malloc(Size);
    }
}

void CObject::operator delete(void* Ptr, unsigned int Size)
{
    // FUN_062adc40
    switch (Size)
    {
    case 0x9c:
        GObjectHeap9c.Free(Ptr);
        return;
    case 0xf0:
        GObjectHeapF0.Free(Ptr);
        return;
    case 0x104:
        GObjectHeap104.Free(Ptr);
        return;
    case 0x120:
        GObjectHeap120.Free(Ptr);
        return;
    case 0x1ec:
        GObjectHeap1ec.Free(Ptr);
        return;
    case 0x2dc:
        GObjectHeap2dc.Free(Ptr);
        return;
    default:
        free(Ptr);
        return;
    }
}

CObject* __cdecl CObject::Alloc(Class ObjectClass)
{
    // FUN_062aee50. `new X()` resolves to CObject::operator new(sizeof(X)), the same pool retail
    // uses. AddRef has no null test, so a failed allocation crashes, as in retail. The class
    // lists are appended through FUN_062af9b0 / FUN_062af7d0. Any other class returns 0.
    CObject* Result = nullptr;
    switch (ObjectClass)
    {
    case ClassObject:                                   // Heap 063fc400 (0x9c), list 063fc32c
        Result = new CObject(ClassObject);
        Result->AddRef();
        G_ClassLists[ListObject].push_back(Result);
        break;
    case ClassBeam:                                     // Heap 063fc3f0 (0x120), list 063fc398
        Result = new CBeam(ClassBeam);
        Result->AddRef();
        G_ClassLists[ListBeam].push_back(Result);
        break;
    case ClassGuided:                                   // Heap 063fc3e0 (0x104), list 063fc38c
        Result = new CGuided(ClassGuided);
        Result->AddRef();
        G_ClassLists[ListGuided].push_back(Result);
        break;
    case ClassMine:                                     // Heap 063fc3d0 (0xf0), list 063fc374
        Result = new CMine(ClassMine);
        Result->AddRef();
        G_ClassLists[ListMine].push_back(Result);
        break;
    case ClassCounterMeasure:                           // Heap 063fc3d0 (0xf0), list 063fc368
        Result = new CCounterMeasure(ClassCounterMeasure);
        Result->AddRef();
        G_ClassLists[ListCounterMeasure].push_back(Result);
        break;
    case ClassSolar:                                    // Heap 063fc3c0 (0x1ec), list 063fc35c
        Result = new CSolar(ClassSolar);
        Result->AddRef();
        G_ClassLists[ListSolar].push_back(Result);
        break;
    case ClassShip:                                     // Heap 063fc3b0 (0x2dc), list 063fc3a4
        Result = new CShip();
        Result->AddRef();
        G_ClassLists[ListShip].push_back(Result);
        break;
    case ClassLoot:                                     // operator new(0x104), list 063fc380
        Result = new CLoot(ClassLoot);
        Result->AddRef();
        G_ClassLists[ListLoot].push_back(Result);
        break;
    case ClassAsteroid:                                 // operator new(0xfc), list 063fc350
        Result = new CAsteroid();
        Result->AddRef();
        G_ClassLists[ListAsteroid].push_back(Result);
        break;
    case ClassEquipmentObj:                             // operator new(0x9c), list 063fc338
        Result = new CEquipmentObj(ClassEquipmentObj);
        Result->AddRef();
        G_ClassLists[ListEquipment].push_back(Result);
        break;
    case ClassDynamicAsteroid:                          // operator new(0x9c), list 063fc344
        Result = new CDynamicAsteroid();
        Result->AddRef();
        G_ClassLists[ListDynamicAsteroid].push_back(Result);
        break;
    default:
        break;
    }
    return Result;
}

int __cdecl CObject::Count(Class ObjectClass)
{
    // FUN_062ae7e0. -1 for a class without a list.
    std::list<CObject*>* const List = ListForClass(ObjectClass);
    if (List == nullptr)
        return -1;
    return (int)List->size();
}

CObject* __cdecl CObject::Find(long EngineInstanceId, Class ObjectClass)
{
    // FUN_062ae960. The match is AddRef'd.
    if (EngineInstanceId == -1)
        return nullptr;
    std::list<CObject*>* const List = ListForClass(ObjectClass);
    if (List == nullptr)
        return nullptr;
    for (std::list<CObject*>::iterator It = List->begin(); It != List->end(); ++It)
    {
        CObject* const Obj = *It;
        if (Obj->EngineInstanceId == EngineInstanceId)
        {
            Obj->AddRef();
            return Obj;
        }
    }
    return nullptr;
}

CObject* __cdecl CObject::Find(unsigned const& Id, Class ObjectClass)
{
    // FUN_062aeb00. Matches CSimple::Id (+0xb0). Deviation: retail reads +0xb0 even in the
    // ClassObject / EquipmentObj / DynamicAsteroid lists, past the end of their 0x9c-byte
    // objects; the port skips those entries.
    std::list<CObject*>* List = ListForClass(ObjectClass);
    if (List == nullptr)
        return nullptr;
    for (std::list<CObject*>::iterator It = List->begin(); It != List->end(); ++It)
    {
        CObject* const Obj = *It;
        if (!ClassHasSimpleId(Obj->ObjectClass))
            continue;
        CSimple* const Simple = (CSimple*)Obj;
        if (Simple->Id == Id)
        {
            Obj->AddRef();
            return Obj;
        }
    }
    return nullptr;
}

CObject* __cdecl CObject::FindFirst(Class ObjectClass)
{
    // FUN_062aec90. The list is remembered in DAT_063fc430 and the cursor in DAT_063fc328.
    // G_bFindActive mirrors "list non-null" for RemoveFromClassList.
    G_FindList = ListForClass(ObjectClass);
    if (G_FindList == nullptr)
    {
        G_bFindActive = false;
        return nullptr;
    }
    G_FindNextIt = G_FindList->begin();
    G_bFindActive = true;
    if (G_FindNextIt == G_FindList->end())
        return nullptr;
    CObject* Obj = *G_FindNextIt;
    ++G_FindNextIt;
    return Obj;
}

CObject* __cdecl CObject::FindNext()
{
    // FUN_062aee20
    if (!G_bFindActive || G_FindList == nullptr)
        return nullptr;
    if (G_FindNextIt == G_FindList->end())
        return nullptr;
    CObject* Obj = *G_FindNextIt;
    ++G_FindNextIt;
    return Obj;
}

void __cdecl CObject::ReleaseAll()
{
    // FUN_062af260. Despite the name, no live object is released: retail frees its cached STL
    // list nodes (DAT_063fc420/DAT_063fc42c, no counterpart with std::list) and shrinks the six
    // size pools, in this order.
    GObjectHeap9c.Shrink();
    GObjectHeap120.Shrink();
    GObjectHeap104.Shrink();
    GObjectHeap1ec.Shrink();
    GObjectHeap2dc.Shrink();
    GObjectHeapF0.Shrink();
}

void CObject::open(Archetype::Root* Arch)
{
    // FUN_062af400. No null test.
    Arch->load();
    Archetype = Arch;
}

int CObject::update(float Dt, unsigned Flags)
{
    // FUN_062af420. CollisionStateMgr is +0x84.
    (void)Flags;
    if (BodyPtr != nullptr)
    {
        PhySys::PhyCollisionStateManager* Mgr = CollisionStateMgr;
        if (Mgr != nullptr)
            Mgr->update(Dt);
    }
    return 0;
}

float CObject::get_physical_radius_r(Vector& OutCenter) const
{
    // FUN_062ad980
    if (BodyPtr != nullptr)
        return PhySys::GetRadiusR(this, false, OutCenter);

    // Otherwise the engine extent (IEngine +0x88), centre transformed to world by the math
    // singleton (DAT_063fc41c) slot +0x28. No null tests.
    float Radius = 0.0f;
    DALib::Engine->GetInstanceExtent((void*)EngineInstanceId, 0, &Radius, (float*)&OutCenter);
    I3DMathEngine* const Math = Acquire3DMathEngine();
    Vector World;
    // this+0x08: Orientation followed by WorldPosition, one Transform.
    Math->TransformXform((FVector&)World, *(FTransform const*)&Orientation,
                         (FVector const&)OutCenter);
    OutCenter = World;
    return Radius;
}

float CObject::hierarchy_radius() const
{
    // FUN_0627cdd0. vtable slot +0x64.
    Vector Discard;
    return get_physical_radius_r(Discard);
}

namespace
{
    // FUN_062ad6f0 (__cdecl). A child whose user data is Parent is descended with the same
    // Parent; any other user data is inserted into Set and descended from its own engine
    // instance. No null test on the user data.
    void CollectHierarchyObjects(CObject* Parent, long Inst, CheapSet<CObject*, std::less<CObject*>>& Set)
    {
        // IEngine passes instance handles as pointer-sized values.
        void* const InstHandle = (void*)Inst;
        long Child = (long)DALib::Engine->GetRelatedInstance(InstHandle, 1, (void*)(intptr_t)-1);
        while (Child != -1)
        {
            CObject* const Obj = (CObject*)DALib::Engine->GetInstanceUserData((void*)Child);
            if (Obj == Parent)
            {
                CollectHierarchyObjects(Parent, Child, Set);
            }
            else
            {
                Set.Insert(Obj);
                CollectHierarchyObjects(Obj, Obj->EngineInstanceId, Set);
            }
            Child = (long)DALib::Engine->GetRelatedInstance(InstHandle, 1, (void*)Child);
        }
    }
}

void CObject::get_intruder_set_r(CheapSet<CObject*, std::less<CObject*>>& Out)
{
    // FUN_062ad7c0, with FUN_062ad6f0's walk inlined. Every member whose collision state is on
    // adds its phantom intruders' roots.
    CheapSet<CObject*, std::less<CObject*>> Hierarchy;
    Hierarchy.Insert(this);
    CollectHierarchyObjects(this, EngineInstanceId, Hierarchy);
    for (std::vector<CObject*>::iterator It = Hierarchy.Items.begin(); It != Hierarchy.Items.end(); ++It)
    {
        CObject* const Obj = *It;
        if (PhySys::GetCollisionState(Obj))
            Obj->CollisionStateMgr->get_intruding_cobjs(true, Out);
    }
}

float CObject::hierarchy_radius(Vector& OutCenter) const
{
    // FUN_0627cdf0. vtable slot +0x64.
    return get_physical_radius_r(OutCenter);
}

Vector CObject::get_velocity() const
{
    // FUN_0627d120
    if (BodyPtr != nullptr)
    {
        return PhySys::GetVelocity(this);
    }
    Vector Out;
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    return Out;
}

float CObject::get_mass() const
{
    // FUN_062ada60. No null test on the archetype.
    if (BodyPtr != nullptr)
        return PhySys::GetMass(this);
    return Archetype->Mass;
}

Vector CObject::get_angular_velocity() const
{
    // FUN_0627d150. The object-space angular velocity rotated to world (3DMathEngine slot +0x2c).
    if (BodyPtr != nullptr)
    {
        Vector const ObjSpace = PhySys::GetAngularVelocityOS(this);
        Vector World;
        Acquire3DMathEngine()->TransformMatrix((FVector&)World, (FMatrix const&)Orientation,
                                               (FVector const&)ObjSpace);
        return World;
    }
    return Vector{0.0f, 0.0f, 0.0f};
}

void CObject::beam_object(Vector const& Pos, Matrix const& Orient, bool bAttach)
{
    // FUN_062ada80
    PhySys::BeamR(this, Pos, Orient, bAttach);
}

void CObject::disable_controllers()
{
    // FUN_062a8920, a bare RET shared with enable_controllers. vtable +0x5c.
}

void CObject::enable_controllers()
{
    // FUN_062a8920 (shared). vtable +0x60.
}

void CObject::unmake_physical()
{
    // FUN_062ad520
    disable_controllers();
    PhySys::UnMakePhysicalR(this);
}

void CObject::remake_physical(PhySys::CreateParms const& Parms, float Radius)
{
    // FUN_062ad4f0
    PhySys::ReMakePhysicalR(this, Parms, Radius);
    enable_controllers();
}

long CObject::part_to_inst(unsigned PartId) const
{
    // FUN_0627d1e0. Without a SurfaceManager (+0x5c) the object's own instance.
    if (SurfaceManager == nullptr)
        return EngineInstanceId;
    for (std::vector<FPartMapEntry>::const_iterator It = PartMap.begin(); It != PartMap.end(); ++It)
    {
        if (It->PartId == PartId)
            return It->EngineInstanceId;
    }
    return -1;
}

Vector CObject::get_moment_of_inertia() const
{
    // FUN_0627d0b0
    if (BodyPtr != nullptr)
    {
        return PhySys::GetMomentOfInertia(this);
    }
    Vector Out;
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    return Out;
}

void CObject::add_impulse(Vector const& Impulse)
{
    // FUN_0627d0e0
    if (BodyPtr != nullptr)
    {
        PhySys::LinearImpulse(this, Impulse, 1.0f);
    }
}

void CObject::add_impulse(Vector const& Impulse, Vector const& Point)
{
    // FUN_0627d100
    if (BodyPtr != nullptr)
    {
        PhySys::AddImpulseAtPoint(this, Impulse, Point);
    }
}

unsigned CObject::inst_to_part(long EngineInst) const
{
    // FUN_0627d220. No SurfaceManager test, unlike part_to_inst.
    for (std::vector<FPartMapEntry>::const_iterator It = PartMap.begin(); It != PartMap.end(); ++It)
    {
        if (It->EngineInstanceId == EngineInst)
            return It->PartId;
    }
    return 0;
}

bool CObject::is_shield_part(unsigned PartId) const
{
    // FUN_0627d250
    for (std::vector<FPartMapEntry>::const_iterator It = PartMap.begin(); It != PartMap.end(); ++It)
    {
        if (It->PartId == PartId)
            return It->bShieldPart != 0;
    }
    return false;
}

bool CObject::flag_part_as_shield(unsigned PartId)
{
    // FUN_062ad4c0
    for (std::vector<FPartMapEntry>::iterator It = PartMap.begin(); It != PartMap.end(); ++It)
    {
        if (It->PartId == PartId)
        {
            It->bShieldPart = 1;
            return true;
        }
    }
    return false;
}

bool CObject::get_surface_extents(Vector& OutMin, Vector& OutMax) const
{
    // FUN_062ad470. PhyPart +0x58, its extents at +0x08 / +0x14.
    if (PhyPart == nullptr)
        return false;
    OutMin = PhyPart->ExtentsMin;
    OutMax = PhyPart->ExtentsMax;
    return true;
}

Archetype::Root* CObject::get_archetype() const
{
    return Archetype;
}

unsigned CObject::AddRef()
{
    RefCount += 1;
    return RefCount;
}

unsigned CObject::Release()
{
    unsigned Next = RefCount - 1;
    RefCount = Next;
    if (Next == 0)
        delete this;
    return Next;
}

void CObject::advise(CBase* Advisor, bool bOn)
{
    // FUN_062af660. Duplicates are allowed; removal erases every copy.
    if (bOn)
    {
        Advisors.push_back(Advisor);
        return;
    }
    Advisors.remove(Advisor);
}

void CObject::enable_collisions_r(bool bEnable)
{
    // FUN_062ad540, with FUN_062ad6f0's walk inlined. No null tests.
    CheapSet<CObject*, std::less<CObject*>> Hierarchy;
    Hierarchy.Insert(this);
    CollectHierarchyObjects(this, EngineInstanceId, Hierarchy);
    for (std::vector<CObject*>::iterator It = Hierarchy.Items.begin(); It != Hierarchy.Items.end(); ++It)
    {
        CObject* const Obj = *It;
        if (Obj->BodyPtr != nullptr)
            PhySys::SetCollisionState(Obj, bEnable);
    }
}

COMMON_API IBehaviorManager* __cdecl GetBehaviorManager(IObjRW* Obj)
{
    // FUN_062d4ba0. Creates the manager on demand for a CEqObj.
    if (Obj == nullptr)
        return nullptr;
    CEqObj* const EqObj = (CEqObj*)const_cast<CObject*>(Obj->cobject());
    if (EqObj == nullptr || (EqObj->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
        return nullptr;
    IBehaviorManager* const Manager = EqObj->get_behavior_interface();
    if (Manager != nullptr)
        return Manager;
    return EqObj->create_behavior_interface(Obj, 0);
}

// CObject update-flag constants, exported from .rdata 0x0639e524..0x0639e53c. UPDATE_DUMB is
// NO_DYNAMICS|NO_SCANNER|NO_BEHAVIOR|NO_ACTIONS|NO_COUNTERMEASURE_AI.
unsigned int const CObject::UPDATE_DEFAULT = 0x00u;
unsigned int const CObject::NO_DYNAMICS = 0x01u;
unsigned int const CObject::NO_SCANNER = 0x04u;
unsigned int const CObject::NO_BEHAVIOR = 0x08u;
unsigned int const CObject::NO_ACTIONS = 0x10u;
unsigned int const CObject::NO_COUNTERMEASURE_AI = 0x20u;
unsigned int const CObject::UPDATE_DUMB = 0x3Du;

Vector CObject::get_center_of_mass() const
{
    // FUN_062ada20. Without a body, WorldPosition as is (no transform, unlike
    // get_physical_radius_r's fallback).
    if (BodyPtr != nullptr)
        return PhySys::GetCenterOfMass(this);
    return WorldPosition;
}

// FUN_062a8920 (?ToggleIceCap@@YAX_N@Z), a bare RET in retail.
void __cdecl ToggleIceCap(bool bOn)
{
    (void)bOn;
}
