// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Physics/common_physys_internal.h"
#include "ivp_utility/ivp_core_trap.h"      // IVP_CoreTrap -- retail's `MOV [0],0`

using namespace CmnPhySys;

// ============================================================================
// Phantoms -- PhySys::Phantom
// ============================================================================
//
// The handle every exported phantom function takes as void*. RTTI ".?AUPhantom@PhySys@@":
// struct Phantom : IVP_Listener_Set_Active<IVP_Real_Object>, vtable .rdata:0639c30c with only
// the three listener slots (no destructor slot). Retail does not export the type. All three
// CreatePhantom bodies allocate it with operator new(0x1c) and zero every field; DestroyPhantom
// inlines its non-virtual destructor.
#include "ivp_utility/ivp_malloc.h" // ivp_free_aligned (FUN_06343ff0)

namespace PhySys
{
    struct Phantom : public IVP_Listener_Set_Active<IVP_Real_Object>
    {
        Phantom()
            : Body(nullptr)
            , Surface(nullptr)
            , SurfaceManager(nullptr)
            , Listener(nullptr)
            , Owner(nullptr)
            , SystemId(0)
        {
        }

        // Inlined into DestroyPhantom.
        ~Phantom();

        // Slot 0, FUN_0628aa70
        void element_added(IVP_U_Set_Active<IVP_Real_Object>* Set, IVP_Real_Object* Elem) override;
        // Slot 1, FUN_0628aa90
        void element_removed(IVP_U_Set_Active<IVP_Real_Object>* Set, IVP_Real_Object* Elem) override;
        // Slot 2, FUN_0628aab0
        void pset_is_going_to_be_deleted(IVP_U_Set_Active<IVP_Real_Object>* Set) override;

        IVP_Real_Object* Body;                      // +0x04  the phantom's IVP body
        IVP_Compact_Surface* Surface;               // +0x08  built and owned by the box phantom only
        IVP_SurfaceManager_Polygon* SurfaceManager; // +0x0c  box and part phantoms
        CObjectCB* Listener;                        // +0x10  PhantomParms+0x40
        CObject* Owner;                             // +0x14  AttachPhantom
        unsigned SystemId;                          // +0x18  PhantomParms+0x00
    };
    static_assert(sizeof(Phantom) == 0x1c, "PhySys::Phantom is operator new(0x1c)");
}

// Each owned piece is freed only when set.
PhySys::Phantom::~Phantom()
{
    if (Body != nullptr)
        PhySys_DeleteBody(Body);
    if (Surface != nullptr)
        ivp_free_aligned(Surface); // FUN_06343ff0
    if (SurfaceManager != nullptr)
        delete SurfaceManager;
}

// An object entered the phantom: with a listener and a client object (IVP_Real_Object+0xa4),
// the listener's slot 0 gets the client.
void PhySys::Phantom::element_added(IVP_U_Set_Active<IVP_Real_Object>* /*Set*/, IVP_Real_Object* Elem)
{
    if (Listener == nullptr)
        return;
    CObject* const Client = (CObject*)Elem->client_data;
    if (Client == nullptr)
        return;
    Listener->added(Client);
}

// The same, slot 1.
void PhySys::Phantom::element_removed(IVP_U_Set_Active<IVP_Real_Object>* /*Set*/, IVP_Real_Object* Elem)
{
    if (Listener == nullptr)
        return;
    CObject* const Client = (CObject*)Elem->client_data;
    if (Client == nullptr)
        return;
    Listener->deleted(Client);
}

// The set goes away: only the listener pointer is dropped.
void PhySys::Phantom::pset_is_going_to_be_deleted(IVP_U_Set_Active<IVP_Real_Object>* /*Set*/)
{
    Listener = nullptr;
}

namespace
{
    // The tail all three CreatePhantom bodies share (inlined in each): the body becomes a
    // phantom with manage_intruding_objects = 1 and client_data = PhantomParms+0x05; with a
    // listener the phantom stores it and joins the listeners of the controller's object set.
    void PhySys_ConvertToPhantom(PhySys::Phantom* NewPhantom, PhySys::PhantomParms const& Parms)
    {
        IVP_Template_Phantom PhantomTemplate;
        PhantomTemplate.manage_intruding_objects = 1;
        PhantomTemplate.client_data = (void*)(uintptr_t)Parms.Flag05;
        NewPhantom->Body->convert_to_phantom(&PhantomTemplate);

        PhySys::CObjectCB* const Listener = Parms.Listener;
        if (Listener != nullptr)
        {
            NewPhantom->Listener = Listener;
            ((IVP_Controller_Phantom*)NewPhantom->Body->controller_phantom)->set_of_objects->listeners.Add(NewPhantom);
        }
    }
}

// The two pose helpers below are declared in common_physys_internal.h; their caller,
// PhySys_BeamObject, is in common_physys_query.cpp.
namespace CmnPhySys
{
    // FUN_0628b740: null handle -> 1; else the body's position (the quaternion is dropped), 0.
    int PhySys_GetPhantomPosition(void* Handle, Vector* OutPosition)
    {
        if (Handle == nullptr)
            return 1;
        IVP_U_Quat Rotation;
        IVP_U_Point Position;
        ((PhySys::Phantom*)Handle)->Body->calc_at_time_AT(&Rotation, &Position);
        OutPosition->x = Position.x;
        OutPosition->y = Position.y;
        OutPosition->z = Position.z;
        return 0;
    }

    // FUN_0628b6e0: null handle -> 1; else beam the body to the target keeping its current
    // orientation, 0. Retail never writes the target's fourth float; the beam reads x/y/z only.
    int PhySys_SetPhantomPosition(void* Handle, Vector const* NewPosition)
    {
        if (Handle == nullptr)
            return 1;
        IVP_Real_Object* const Body = ((PhySys::Phantom*)Handle)->Body;
        IVP_U_Point Target;
        Target.set(NewPosition->x, NewPosition->y, NewPosition->z);
        IVP_U_Quat Rotation;
        IVP_U_Point Position;
        Body->calc_at_time_AT(&Rotation, &Position);
        Body->beam_object_to_new_position(&Rotation, &Target, 0);
        return 0;
    }
}

// FUN_0628aac0. A box phantom; HalfExtents are the box's half sizes.
int __cdecl PhySys::CreatePhantom(PhantomParms const& Parms, Vector const& HalfExtents, void*& OutHandle)
{
    IVP_RealObjectConstructionTemplate Template;
    Template.set_short_name(Parms.Name);
    Template.FlagsHi = Parms.bPhysicalUnmoveable;

    // The lookup is not tested: a miss builds into the map head node's unconstructed storage
    // (retail quirk, see PhySys_FindSystem).
    PhysicalSystem& System = PhySys_FindSystem(Parms.SystemId);

    Phantom* const NewPhantom = new Phantom();
    NewPhantom->SystemId = Parms.SystemId;

    {
        IVP_SurfaceBuilder_Ledge_Soup Soup;
        IVP_U_Vector<IVP_U_Float_Point> Points;

        // The eight corners, appended in retail's order. Retail leaves each corner's fourth
        // float as stack garbage; the port stores 0.
        const float Ex = HalfExtents.x;
        const float Ey = HalfExtents.y;
        const float Ez = HalfExtents.z;
        IVP_U_Float_Point Corners[8] = {
            {  Ex,  Ey,  Ez, 0.0f },
            { -Ex, -Ey, -Ez, 0.0f },
            { -Ex,  Ey,  Ez, 0.0f },
            {  Ex, -Ey, -Ez, 0.0f },
            {  Ex, -Ey,  Ez, 0.0f },
            { -Ex,  Ey, -Ez, 0.0f },
            {  Ex,  Ey, -Ez, 0.0f },
            { -Ex, -Ey,  Ez, 0.0f },
        };
        for (int Index = 0; Index < 8; ++Index)
            Points.Add(&Corners[Index]);

        // The hull goes into the soup, compiled with options {0, 1, 0, 1}.
        IVP_Compact_Ledge* const Hull = IVP_SurfaceBuilder_Pointsoup::convert_pointsoup_to_compact_ledge(&Points);
        Soup.InsertLedge(Hull);
        const int32_t CompileOptions[4] = { 0, 1, 0, 1 };
        NewPhantom->Surface = (IVP_Compact_Surface*)Soup.Compile(CompileOptions);
    }

    // The plain IVP_SurfaceManager_Polygon over the new surface.
    NewPhantom->SurfaceManager = new IVP_SurfaceManager_Polygon(NewPhantom->Surface);

    IVP_U_Point Position;
    Position.set(Parms.Position.x, Parms.Position.y, Parms.Position.z);

    // FUN_0628b070, then the (W, X, Y, Z) -> (x, y, z, w) copy.
    IVP_U_Quat Rotation;
    PhySys_OrientationToIvpQuat(Parms.Orientation, Rotation);

    if (Template.Material == nullptr)
        Template.Material = G_PhySysMaterial;

    NewPhantom->Body = System.Environment->create_polygon(NewPhantom->SurfaceManager, &Template,
                                                          &Rotation, &Position);

    PhySys_ConvertToPhantom(NewPhantom, Parms);

    OutHandle = NewPhantom;
    return 0;
}

// FUN_0628b0a0. A ball phantom.
int __cdecl PhySys::CreatePhantom(PhantomParms const& Parms, float Radius, void*& OutHandle)
{
    IVP_RealObjectConstructionTemplate Template;
    Template.set_short_name(Parms.Name);
    Template.FlagsHi = Parms.bPhysicalUnmoveable;

    // A miss is not tested (retail quirk, see PhySys_FindSystem).
    PhysicalSystem& System = PhySys_FindSystem(Parms.SystemId);

    Phantom* const NewPhantom = new Phantom();
    NewPhantom->SystemId = Parms.SystemId;

    NewPhantom->Body = System.CreateBallBody(Parms.Position, Parms.Orientation, Radius, &Template);

    PhySys_ConvertToPhantom(NewPhantom, Parms);

    OutHandle = NewPhantom;
    return 0;
}

// FUN_0628b200. A phantom over a part's compact surface.
int __cdecl PhySys::CreatePhantom(PhantomParms const& Parms, PhyArch::Part const& Part,
                                   void*& OutHandle)
{
    // The phantom comes first here.
    Phantom* const NewPhantom = new Phantom();
    NewPhantom->SystemId = Parms.SystemId;

    // A part without a surface is a warning and result 1 with a null handle. Retail leaks the
    // phantom on this path; so does the port.
    if (Part.Surface == nullptr)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kPhySysSource, 0x5d0, "Failed to create Phantom surface.");
        OutHandle = nullptr;
        return 1;
    }

    IVP_RealObjectConstructionTemplate Template;
    Template.set_short_name(Parms.Name);
    Template.FlagsHi = Parms.bPhysicalUnmoveable;

    // A miss is not tested (retail quirk, see PhySys_FindSystem).
    PhysicalSystem& System = PhySys_FindSystem(Parms.SystemId);

    // Over the part's own surface; phantom+0x08 stays 0, the part keeps owning it.
    NewPhantom->SurfaceManager = new IVP_SurfaceManager_Polygon(Part.Surface);

    IVP_U_Point Position;
    Position.set(Parms.Position.x, Parms.Position.y, Parms.Position.z);

    IVP_U_Quat Rotation;
    PhySys_OrientationToIvpQuat(Parms.Orientation, Rotation);

    if (Template.Material == nullptr)
        Template.Material = G_PhySysMaterial;

    NewPhantom->Body = System.Environment->create_polygon(NewPhantom->SurfaceManager, &Template,
                                                          &Rotation, &Position);

    PhySys_ConvertToPhantom(NewPhantom, Parms);

    OutHandle = NewPhantom;
    return 0;
}

// FUN_0628b440
void __cdecl PhySys::DestroyPhantom(void* Handle)
{
    if (Handle == nullptr)
        return;
    Phantom* const OldPhantom = (Phantom*)Handle;

    // The phantom leaves the listeners of its controller's object set (Body not tested).
    // Retail quirk kept (inside RemoveElement): a phantom created without a listener is not in
    // the vector, so the search gives -1 and the vector is corrupted. Retail's callers always
    // pass a listener.
    ((IVP_Controller_Phantom*)OldPhantom->Body->controller_phantom)->set_of_objects->listeners.RemoveElement(OldPhantom);

    // The owner's phantom list loses the node directly, not through DetachPhantom (no IVP
    // split). Unlike DetachPhantom this path checks for "not found".
    CObject* const Owner = OldPhantom->Owner;
    if (Owner != nullptr)
    {
        std::list<void*>::iterator It = Owner->Phantoms.begin();
        while (It != Owner->Phantoms.end() && *It != Handle)
            ++It;
        if (It != Owner->Phantoms.end())
            Owner->Phantoms.erase(It);
    }

    delete OldPhantom;
}

int __cdecl PhySys::AttachPhantom(CObject* Object, void* Handle)
{
    // FUN_0628b560. Object is not tested.
    if (Handle == nullptr)
        return 1;
    Phantom* const AttachedPhantom = (Phantom*)Handle;

    IVP_U_Quat PhantomRotation;
    IVP_U_Point PhantomPosition;
    AttachedPhantom->Body->calc_at_time_AT(&PhantomRotation, &PhantomPosition);

    // x87: sqrt((dz*dz + dy*dy) + dx*dx), rounded once.
    const double Dx = (double)(Object->WorldPosition.x) - PhantomPosition.x;
    const double Dy = (double)(Object->WorldPosition.y) - PhantomPosition.y;
    const double Dz = (double)(Object->WorldPosition.z) - PhantomPosition.z;
    const float Distance = (float)(sqrt((Dz * Dz + Dy * Dy) + Dx * Dx));

    IVP_Merge_Object_Into_Core(Object->BodyPtr, AttachedPhantom->Body, Distance);   // FUN_06343700

    Object->Phantoms.push_back(Handle);
    AttachedPhantom->Owner = Object;
    return 0;
}

int __cdecl PhySys::DetachPhantom(void* Handle)
{
    // FUN_0628b610. The owner is not tested.
    if (Handle == nullptr)
        return 1;
    Phantom* const DetachedPhantom = (Phantom*)Handle;

    IVP_RealObjectConstructionTemplate Template;
    Template.set_short_name(DetachedPhantom->Owner->BodyPtr->name);
    Template.FlagsHi = 1;
    Template.Material = G_PhySysMaterial;

    // Retail quirk kept: the erase does not test for "not found", so when Handle is not in the
    // list retail unlinks and frees the list head itself. Owner is also left set.
    CObject* const Owner = DetachedPhantom->Owner;
    std::list<void*>::iterator It = Owner->Phantoms.begin();
    while (It != Owner->Phantoms.end() && *It != Handle)
        ++It;
    Owner->Phantoms.erase(It);

    IVP_Split_Object_From_Core(DetachedPhantom->Body, &Template);   // FUN_06343940
    return 0;
}

bool __cdecl PhySys::GetCollisionStatePhantom(void* const Handle)
{
    // FUN_0628b510: the collision-detection bits (Body->flags & 0x300).
    if (Handle == nullptr)
        return false;
    IVP_Real_Object* const Body = ((Phantom*)Handle)->Body;
    if (Body == nullptr)
        return false;
    return (Body->flags & 0x300) != 0;
}

void __cdecl PhySys::SetCollisionStatePhantom(void* Handle, bool bEnable)
{
    // FUN_0628b540. The body is not tested.
    if (Handle == nullptr)
        return;
    ((Phantom*)Handle)->Body->enable_collision_detection(bEnable);
}

// ============================================================================
// PhySys::CalcClosestPointOnSynapse -- FUN_0628b7a0
//
// File-local in retail (no export, one caller: GetClosestPhantomIntruderPos); the name comes
// from its own FDUMP string. Given a world-space query point and one IVP_Synapse_Real, it
// returns the world-space point on that synapse's feature (vertex, edge, face or ball surface)
// closest to the query point.
//
// Retail primitives used:
// - The narrow-phase cache slot is resolved and released around every return path
//   (ResolveNarrowPhaseCacheSlot / ReleaseNarrowPhaseCacheSlot; retail inlines the resolve).
// - FUN_0634d8d0 -> IVP_NarrowPhaseCacheSlot::transform_slot_to_world_space.
// - FUN_0634ded0 -> the feature pointer masked to its triangle and ledge (GetCompactLedge).
//   It allocates nothing.
// - FUN_06345ea0 -> get_upper_limit_radius.
// - FUN_06347ef0 -> reproduced as PhySys_Vec3Normize for its exact threshold.
// ============================================================================

// Retail leaf vector helpers, reproduced locally.

// FUN_0628bdf0 / FUN_0628bf60 (two identical bodies): Dst = Src.
static void PhySys_Vec3Copy(Vector& Dst, Vector const& Src)
{
    Dst.x = Src.x;
    Dst.y = Src.y;
    Dst.z = Src.z;
}

// FUN_0628be10(this=Dst; Src, Scale): Dst = Src * Scale.
static void PhySys_Vec3Scale(Vector& Dst, Vector const& Src, float Scale)
{
    Dst.x = Src.x * Scale;
    Dst.y = Src.y * Scale;
    Dst.z = Src.z * Scale;
}

// FUN_0628be40(this=A; B): dot(A, B) as (z*z' + y*y') + x*x', returned in ST0 (double). Each
// caller decides whether it is stored as float.
static double PhySys_Vec3Dot(Vector const& A, Vector const& B)
{
    return ((double)(B.z) * A.z + (double)(B.y) * A.y) +
           (double)(B.x) * A.x;
}

// FUN_0628be60(this=Dst; A, B, Scale): Dst = A + B * Scale. Retail passes Scale == -1.0f for a
// plain subtraction. Declared in common_physys_internal.h (BeamBodyToWorldPose uses it too).
void CmnPhySys::PhySys_Vec3AddMul(Vector& Dst, Vector const& A, Vector const& B, float Scale)
{
    // x87: one rounding per component.
    float const Rx = (float)((double)(Scale) * B.x + A.x);
    float const Ry = (float)((double)(Scale) * B.y + A.y);
    float const Rz = (float)((double)(Scale) * B.z + A.z);
    Dst.x = Rx;
    Dst.y = Ry;
    Dst.z = Rz;
}

// FUN_0628bea0(this=Dst; A, B): Dst = A - B.
static void PhySys_Vec3Sub(Vector& Dst, Vector const& A, Vector const& B)
{
    float const Rx = A.x - B.x;
    float const Ry = A.y - B.y;
    float const Rz = A.z - B.z;
    Dst.x = Rx;
    Dst.y = Ry;
    Dst.z = Rz;
}

// FUN_0628bed0(this=Dst; A, B): Dst = A x B, the standard right-handed cross product.
// x87: products and differences stay double; every operand is read before the first store.
static void PhySys_Vec3Cross(Vector& Dst, Vector const& A, Vector const& B)
{
    double const Rz = (double)(A.x) * B.y - (double)(A.y) * B.x;
    double const Ry = (double)(B.x) * A.z - (double)(A.x) * B.z;
    double const Rx = (double)(A.y) * B.z - (double)(A.z) * B.y;
    Dst.x = (float)(Rx);
    Dst.y = (float)(Ry);
    Dst.z = (float)(Rz);
}

// FUN_06347ef0(this=V): normalize in place unless |V|^2 is below 1e-10f (DAT_063a53d8); returns
// whether it normalized (every caller here discards it). Retail takes the normalizing path on
// qlen >= 1e-10f, while ivp_math.h's normize() uses >, so this stays a local copy. x87: the
// reciprocal stays double.
static bool PhySys_Vec3Normize(Vector& V)
{
    double const QuadLen = ((double)(V.x) * V.x + (double)(V.y) * V.y) +
                           (double)(V.z) * V.z;
    if (!(QuadLen >= 1e-10f))   // NaN bails too.
        return false;
    double const Inv = 1.0 / sqrt(QuadLen);
    V.x = (float)(Inv * V.x);
    V.y = (float)(Inv * V.y);
    V.z = (float)(Inv * V.z);
    return true;
}

// FUN_0628bf10(this=Dst; A, B): cross, then normalize. Not a duplicate of FUN_0628bed0; the
// trailing normalize is the difference.
static void PhySys_Vec3CrossNormalize(Vector& Dst, Vector const& A, Vector const& B)
{
    PhySys_Vec3Cross(Dst, A, B);
    PhySys_Vec3Normize(Dst);
}

// FUN_062c4780: |V|^2 = (x*x + y*y) + z*z, returned in ST0 (double).
static double PhySys_Vec3QuadLength(Vector const& V)
{
    return ((double)(V.x) * V.x + (double)(V.y) * V.y) +
           (double)(V.z) * V.z;
}

// FUN_0628bf80(P, A, B, Out): closest point on the segment A..B to P:
// t = -dot(A - P, B - A) / |B - A|^2 clamped to [0,1], Out = A + (B - A) * t.
// x87: Dy, Ex, Ey are stored as floats; Dz and Dx stay on the stack; Ez is stored but its
// unrounded value multiplies Dz.
static void PhySys_ClosestPointOnSegment(Vector const& P, Vector const& A, Vector const& B, Vector& Out)
{
    float const DyF = (float)((double)(A.y) - P.y);
    double const Dz = (double)(A.z) - P.z;
    double const Dx = (double)(A.x) - P.x;
    float const ExF = (float)((double)(B.x) - A.x);
    float const EyF = (float)((double)(B.y) - A.y);
    double const Ez = (double)(B.z) - A.z;
    float const EzF = (float)(Ez);

    double const S = (Dz * Ez + (double)(EyF) * DyF) + (double)(ExF) * Dx;
    double const L = ((double)(EzF) * EzF + (double)(EyF) * EyF) +
                     (double)(ExF) * ExF;
    double T = -(S / L);
    // A NaN T takes neither branch and passes through unclamped.
    if (T < 0.0f)
        T = 0.0f;
    else if (T > 1.0f)
        T = 1.0f;

    // Retail's store order: y, z, x.
    float const OutY = (float)((double)(EyF) * T + A.y);
    float const OutZ = (float)((double)(EzF) * T + A.z);
    float const OutX = (float)((double)(ExF) * T + A.x);
    Out.y = OutY;
    Out.z = OutZ;
    Out.x = OutX;
}

// The synapse is IVP_Synapse_Real (ivp_mindist.h): l_obj +0x10, edge +0x14 (masked with ~0xf
// for faces, unused for balls), status +0x1a (the switch key). The caller picks one of a
// mindist's two synapses at +0x18 / +0x34.

// Feature-type switch keys. The compare is unsigned, so anything outside 0..3 (negatives
// included) falls into the FDUMP branch.
enum EPhySysSynapseFeature
{
    kPhySysSynapseVertex = 0,
    kPhySysSynapseEdge   = 1,
    kPhySysSynapseFace   = 2,
    kPhySysSynapseBall   = 3
};

// FUN_0634d8d0: one compact-mesh vertex from the cached slot's local space to world space.
// Only the first three floats of either side are touched.
static void PhySys_SlotPointToWorld(IVP_NarrowPhaseCacheSlot* Slot,
                                     IVP_U_Float_Point const* LocalPoint, Vector& OutWorld)
{
    Slot->transform_slot_to_world_space((IVP_U_Point const*)LocalPoint, (IVP_U_Point*)&OutWorld);
}

static void PhySys_CalcClosestPointOnSynapse(Vector const& QueryPos, IVP_Synapse_Real const* Syn, Vector& Out)
{
    IVP_Real_Object* const Object = Syn->l_obj;

    // Every return path below drops this reference again.
    IVP_NarrowPhaseCacheSlot* const Slot = Object->ResolveNarrowPhaseCacheSlot();

    switch ((int)Syn->status)
    {
    case kPhySysSynapseVertex:
    {
        // The edge's start point, in world space.
        IVP_Compact_Edge const* const Edge =
            Syn->edge;
        IVP_Compact_Ledge const* const Ledge = Edge->GetCompactLedge();
        PhySys_SlotPointToWorld(Slot, Ledge->GetPoint(Edge), Out);
        break;
    }

    case kPhySysSynapseEdge:
    {
        // A = this edge's start point, B = the opposite edge's start point.
        IVP_Compact_Edge const* const Edge =
            Syn->edge;
        IVP_Compact_Ledge const* const Ledge = Edge->GetCompactLedge();

        Vector A;
        Vector B;
        PhySys_SlotPointToWorld(Slot, Ledge->GetPoint(Edge), A);
        PhySys_SlotPointToWorld(Slot, Ledge->GetPoint(Edge->GetOppositeEdge()), B);

        Vector AMinusQuery;
        PhySys_Vec3Sub(AMinusQuery, A, QueryPos);
        Vector Dir;
        PhySys_Vec3Sub(Dir, B, A);

        // x87: the numerator is stored as a float, the denominator stays on the stack, and
        // the first compare sees the unrounded T.
        float const NumF = (float)(PhySys_Vec3Dot(AMinusQuery, Dir));
        double const T = -((double)(NumF) / PhySys_Vec3Dot(Dir, Dir));
        float TF = (float)(T);
        // Clamp when T < 0 or TF > 1 (a NaN passes unclamped). The clamp path re-tests the
        // old TF: strictly below 0 keeps 0, anything else stores 1.
        if (T < 0.0 || TF > 1.0f)
        {
            float const OldTF = TF;
            TF = 0.0f;
            if (!(OldTF < 0.0f))
                TF = 1.0f;
        }

        PhySys_Vec3AddMul(Out, A, Dir, TF);
        break;
    }

    case kPhySysSynapseFace:
    {
        // The feature pointer masked down to its IVP_Compact_Triangle; V0/V1/V2 come from its
        // three edges.
        IVP_Compact_Edge const* const Edge =
            Syn->edge;
        IVP_Compact_Ledge const* const Ledge = Edge->GetCompactLedge();
        IVP_Compact_Triangle const* const Tri = Edge->GetCompactTriangle();

        Vector V0;
        Vector V1;
        Vector V2;
        PhySys_SlotPointToWorld(Slot, Ledge->GetPoint(Tri->GetEdge(0)), V0);
        PhySys_SlotPointToWorld(Slot, Ledge->GetPoint(Tri->GetEdge(1)), V1);
        PhySys_SlotPointToWorld(Slot, Ledge->GetPoint(Tri->GetEdge(2)), V2);

        Vector Edge0;  // V1 - V0
        Vector Edge1;  // V2 - V0
        Vector Edge2;  // V2 - V1  -- retail computes it and never reads it.
        PhySys_Vec3AddMul(Edge0, V1, V0, -1.0f);
        PhySys_Vec3AddMul(Edge1, V2, V0, -1.0f);
        PhySys_Vec3AddMul(Edge2, V2, V1, -1.0f);
        (void)Edge2;

        // Note the operand order: cross(Edge1, Edge0).
        Vector Normal;
        PhySys_Vec3CrossNormalize(Normal, Edge1, Edge0);
        Vector Tangent;
        PhySys_Vec3Cross(Tangent, Normal, Edge1);

        // Bails only when -1e-5 < Denom < +1e-5 with both compares ordered (NaN proceeds).
        // On the bail path Out is left untouched. x87: Denom stays double.
        double const Denom = PhySys_Vec3Dot(Edge0, Tangent);
        if (Denom > -1e-5f && Denom < 1e-5f)
            break;

        float const InvDenom = (float)(1.0 / Denom);

        Vector QueryMinusV0;
        PhySys_Vec3AddMul(QueryMinusV0, QueryPos, V0, -1.0f);

        // The two barycentric weights, each rounded to float.
        float const B1 = (float)(PhySys_Vec3Dot(QueryMinusV0, Tangent) * InvDenom);
        Vector CrossQE0;
        PhySys_Vec3Cross(CrossQE0, QueryMinusV0, Edge0);
        float const B2 = (float)(PhySys_Vec3Dot(Normal, CrossQE0) * InvDenom);

        // The query point projected onto the triangle's plane.
        PhySys_Vec3Scale(Out, V0, (float)((1.0 - B1) - B2));
        PhySys_Vec3AddMul(Out, Out, V1, B1);
        PhySys_Vec3AddMul(Out, Out, V2, B2);

        // Written as the outside test because every retail compare is strictly ordered: NaN
        // weights land on the inside path and keep the planar projection.
        bool const bOutside = (B1 < 0.0f) || (B1 > 1.0f) ||
                              (B2 < 0.0f) || (B2 > 1.0f) ||
                              (((double)(B2) + B1) > 1.0f); // x87: double sum
        if (!bOutside)
            break;

        // Outside: the closest point on each edge to the planar projection (not to the
        // original query point), keeping the nearest.
        Vector CandV0V1;
        Vector CandV0V2;
        Vector CandV1V2;
        PhySys_ClosestPointOnSegment(Out, V0, V1, CandV0V1);
        PhySys_ClosestPointOnSegment(Out, V0, V2, CandV0V2);
        PhySys_ClosestPointOnSegment(Out, V1, V2, CandV1V2);

        Vector Delta;
        PhySys_Vec3AddMul(Delta, Out, CandV0V1, -1.0f);
        // x87: D1 and D2 are stored as floats; D3 stays double.
        float const D1 = (float)(PhySys_Vec3QuadLength(Delta));
        PhySys_Vec3AddMul(Delta, Out, CandV0V2, -1.0f);
        float const D2 = (float)(PhySys_Vec3QuadLength(Delta));
        PhySys_Vec3AddMul(Delta, Out, CandV1V2, -1.0f);
        double const D3 = PhySys_Vec3QuadLength(Delta);

        // Ordered <= tests (NaN fails them). The final else is IVP's core trap (retail's
        // `MOV [0],0`), reachable only with a NaN distance.
        if (D1 <= D2 && D1 <= D3)
            PhySys_Vec3Copy(Out, CandV0V1);
        else if (D2 <= D1 && D2 <= D3)
            PhySys_Vec3Copy(Out, CandV0V2);
        else if (D3 <= D1)
            PhySys_Vec3Copy(Out, CandV1V2);
        else
            IVP_CoreTrap();
        break;
    }

    case kPhySysSynapseBall:
    {
        // Out = Center - normalize(Center - P) * Radius: the point on the ball's surface
        // facing the query point. Center is MWorldFCore.vv; Radius is get_upper_limit_radius.
        IVP_Core* const Core = Object->core;
        Vector Center;
        Center.x = Core->MWorldFCore.vv.k[0];
        Center.y = Core->MWorldFCore.vv.k[1];
        Center.z = Core->MWorldFCore.vv.k[2];

        PhySys_Vec3Copy(Out, Center);

        Vector Dir;
        PhySys_Vec3Sub(Dir, Out, QueryPos);
        PhySys_Vec3Normize(Dir);

        float const Radius = Object->get_upper_limit_radius();
        PhySys_Vec3AddMul(Out, Out, Dir, -Radius);
        break;
    }

    default:
        // Retail's own error text and line number. The zero-fill is an ICF-folded helper
        // (AmbientScriptInfo::clear).
        FDUMP(kFdumpError,
              "%s(%d) : *** ERROR: PhySys::CalcClosestPointOnSynapse(): Unknown IVP_Synapse type (%d).",
              kPhySysSource, 0x6d6, (int)Syn->status);
        Out.x = 0.0f;
        Out.y = 0.0f;
        Out.z = 0.0f;
        break;
    }

    Object->ReleaseNarrowPhaseCacheSlot(Slot);
}

bool __cdecl PhySys::GetClosestPhantomIntruderPos(void* Handle, unsigned FilterA, unsigned FilterB,
                                                    CSimple*& OutObject, Vector& OutPos)
{
    // FUN_0628c080. A null handle, or a phantom body that is not a ball (object_type != 3),
    // -> false. The out parameters are written only when a closer intruder is found.
    if (Handle == nullptr)
        return false;
    IVP_Real_Object* const Body = ((Phantom*)Handle)->Body;
    if (Body->object_type != IVP_BALL)
        return false;

    // The core's world translation.
    IVP_U_Point QueryPoint;
    Body->get_core_position_ws(&QueryPoint);
    Vector QueryPos;
    QueryPos.x = QueryPoint.x;
    QueryPos.y = QueryPoint.y;
    QueryPos.z = QueryPoint.z;

    // The phantom controller's mindist set, walked from slot SizeMm down to 0.
    struct FVHashSlot
    {
        unsigned HashIndex;
        IVP_Mindist_Base* Elem;
    };
    IVP_Controller_Phantom* const Controller = (IVP_Controller_Phantom*)Body->controller_phantom;
    float BestDistSq = FLT_MAX;
    for (int Slot = Controller->mindist_set.SizeMm; Slot >= 0; --Slot)
    {
        IVP_Mindist_Base* const Mindist = ((FVHashSlot*)Controller->mindist_set.Elems)[Slot].Elem;
        if (Mindist == nullptr)
            continue;

        // The intruder's synapse is whichever one is not the phantom body's.
        IVP_Synapse_Real const* Synapse = &Mindist->synapse[0];
        if (Synapse->l_obj == Body)
            Synapse = &Mindist->synapse[1];

        // An intruder that is itself a phantom is skipped.
        IVP_Real_Object* const Intruder = Synapse->l_obj;
        if (Intruder->controller_phantom != nullptr)
            continue;

        // The root of its client must exist, have class bits 3 (a CSimple), share a type bit
        // with FilterA and not carry the id FilterB.
        CObject* const Root = GetRoot((CObject*)Intruder->client_data);
        if (Root == nullptr)
            continue;
        if ((uint8_t)((unsigned)Root->ObjectClass & CObject::ClassSimple) != CObject::ClassSimple)
            continue;
        CSimple* const Simple = (CSimple*)Root;
        if ((Simple->TypeBits & FilterA) == 0)
            continue;
        if (Simple->Id == FilterB)
            continue;

        Vector Closest;
        PhySys_CalcClosestPointOnSynapse(QueryPos, Synapse, Closest);

        // x87: (dy*dy + dz*dz) + dx*dx, unrounded for the compare.
        const double Dx = (double)(Closest.x) - QueryPos.x;
        const double Dy = (double)(Closest.y) - QueryPos.y;
        const double Dz = (double)(Closest.z) - QueryPos.z;
        const double DistSq = (Dy * Dy + Dz * Dz) + Dx * Dx;

        // Only a strictly smaller distance is taken.
        if (DistSq < BestDistSq)
        {
            BestDistSq = (float)(DistSq);
            OutPos = Closest;
            OutObject = Simple;
        }
    }

    return BestDistSq < FLT_MAX;
}

