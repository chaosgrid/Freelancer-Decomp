// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <cstddef>
#include <cstdint>

class IVP_Compact_Ledge; // ivp_compact_ledge.h
class IVP_Ray_Solver;     // ivp_ray_solver.h
class IVP_Real_Object;    // ivp_real_object.h

// ============================================================================
// IVP_SurfaceManager: the interface through which an object presents its
// ledge geometry to collision detection (IVP_Real_Object::surface_manager,
// +0x80), plus this module's one concrete default implementation and its
// single static instance.
//
// ----------------------------------------------------------------------------
// VTABLES (extents taken from the .rdata labels, both exactly 11 slots):
//   Abstract base   .rdata:063a56ac .. 063a56d4
//   Default derived .rdata:063a5830 .. 063a5858 (next: IVP_Ball's table)
//
//   slot  base (063a56ac)              derived (063a5830)
//   ----  --------------------------   --------------------------------------
//    0    LAB_06391fb0 (_purecall)     062d1ad0 "TractorArm::GetMode" (ICF)
//    1    LAB_06391fb0 (_purecall)     063623d0 "CAsteroid::get_velocity" (ICF)
//    2    LAB_06391fb0 (_purecall)     LAB_063623f0
//    3    LAB_06391fb0 (_purecall)     LAB_0634dc00
//    4    LAB_06391fb0 (_purecall)     LAB_06362400
//    5    LAB_06391fb0 (_purecall)     FUN_06362460
//    6    LAB_06391fb0 (_purecall)     LAB_063624a0
//    7    062ed730                     062ed730   <-- same address in both
//    8    062ed730                     062ed730   <-- same address in both
//    9    LAB_06391fb0 (_purecall)     FUN_063624c0 (scalar-deleting dtor)
//   10    LAB_06391fb0 (_purecall)     063624b0 "Archetype::EqObj::get_class_type" (ICF)
//
// Slots 7 and 8 (a bare `RET 4`) are a base default the derived class
// inherits. The names on slots 0, 1 and 10 come from /OPT:ICF folding and say
// nothing about the receiver; the table belongs to this class because
// FUN_06362370 stores it into DAT_0658afe0.
//
// Four derived slots start with or consist of `MOV dword ptr [0],0`, a
// deliberate crash marker (063623f0 slot 2, 0634dc00 slot 3, 06362465 slot 5
// before real work, 063624a0 slot 6); the port uses IVP_CoreTrap.
// ============================================================================

#pragma pack(push, 4)

// Growable pointer array used by slots 4 and 5. Not IVP_U_Vector: 32-bit
// fields (capacity +0x00, count +0x04, elems +0x08), grown by FUN_0635d3e0 to
// 2*old+1. Every list holds compact ledges.
struct IVP_SurfaceManager_OutList
{
    int32_t Capacity; // +0x00
    int32_t Count;    // +0x04
    IVP_Compact_Ledge** Elems; // +0x08

    // 0x0635d3e0: grow to 2*capacity+1,
    // copying the live elements; the old buffer is freed unless it is the inline
    // buffer right after the header (or null). Callers: the compact ledge
    // solver, the polygon surface manager, IVP_Create_Exact_Mindists'
    // ledge lists.
    void grow();

    // 0x06350590: grow only when the vector is
    // full (count >= capacity). Retail's sole caller (0634e223,
    // IVP_Create_Exact_Mindists) is ported with this test inlined
    // (ivp_mindist_create.cpp, FLedgeList::Add), so nothing calls it here.
    void ensure_room();
};
static_assert(offsetof(IVP_SurfaceManager_OutList, Capacity) == 0x00, "IVP_SurfaceManager_OutList+0x00");
static_assert(offsetof(IVP_SurfaceManager_OutList, Count) == 0x04, "IVP_SurfaceManager_OutList+0x04");
static_assert(offsetof(IVP_SurfaceManager_OutList, Elems) == 0x08, "IVP_SurfaceManager_OutList+0x08");

// Abstract base, vptr only (its destructor FUN_0635d440 writes nothing else).
class IVP_SurfaceManager
{
public:
    // Slot names: the Ipion IVP_SurfaceManager API, matched slot-for-slot against the bodies
    // of IVP_SurfaceManager_Polygon (vtable 0639c2dc, RTTI-proven; see ivp_surman_polygon.h).
    // Slot 0 -- the single convex ledge.
    virtual IVP_Compact_Ledge* get_single_convex() = 0;

    // Slot 1 -- writes a 3-float vector through OutVec3 and returns it.
    virtual float* get_mass_center(float* OutVec3) = 0;

    // Slot 2 -- three arguments. The default traps; IVP_SurfaceManager_Polygon
    // implements it as (const IVP_U_Point* In, float* OutA, float* OutB), see
    // IVP_Real_Object::init_core_rest_state_and_damping. Parameter types stay generic.
    virtual void get_radius_and_radius_dev_to_given_center(void* Param1, float* Param2, float* Param3) = 0;

    // Slot 3 -- one argument. As slot 2: the polygon manager writes an
    // IVP_U_Point through it.
    virtual void get_rotation_inertia(void* Param1) = 0;

    // Slot 4 -- six arguments; the default is a sphere-overlap collect.
    virtual void get_all_ledges_within_radius(const float* Center3, float Radius, void* Param3,
                                              void* Param4, void* Param5,
                                              IVP_SurfaceManager_OutList* OutList) = 0;

    // Slot 5
    virtual void get_all_terminal_ledges(IVP_SurfaceManager_OutList* OutList) = 0;

    // Slot 6
    virtual void insert_all_ledges_hitting_ray(IVP_Ray_Solver* Param1, IVP_Real_Object* Param2) = 0;

    // Slots 7 and 8 -- the shared `RET 4` default (not pure).
    virtual void add_reference_to_ledge(IVP_Compact_Ledge* Param1);
    virtual void remove_reference_to_ledge(IVP_Compact_Ledge* Param1);

    // Slot 9 -- pure in the base yet with a body (FUN_0635d440), which is what
    // MSVC emits for a pure virtual destructor with a definition.
    virtual ~IVP_SurfaceManager() = 0;

    // Slot 10 -- the default returns 1.
    virtual int32_t get_type() = 0;
};

// The module's one concrete implementation, vtable .rdata:063a5830. Ctor
// FUN_06362370, dtor FUN_063624e0, scalar-deleting wrapper FUN_063624c0.
// "_Default" is a port name: it backs the module's single static instance,
// which IVP_Ball installs. No retail symbol survives.
class IVP_SurfaceManager_Default : public IVP_SurfaceManager
{
public:
    IVP_SurfaceManager_Default();
    ~IVP_SurfaceManager_Default() override;

    IVP_Compact_Ledge*   get_single_convex() override;
    float*  get_mass_center(float* OutVec3) override;
    void    get_radius_and_radius_dev_to_given_center(void* Param1, float* Param2, float* Param3) override;
    void    get_rotation_inertia(void* Param1) override;
    void    get_all_ledges_within_radius(const float* Center3, float Radius, void* Param3,
                                         void* Param4, void* Param5,
                                         IVP_SurfaceManager_OutList* OutList) override;
    void    get_all_terminal_ledges(IVP_SurfaceManager_OutList* OutList) override;
    void    insert_all_ledges_hitting_ray(IVP_Ray_Solver* Param1, IVP_Real_Object* Param2) override;
    int32_t get_type() override;

    // +0x04: a zero-filled 0x20-byte block, 16-byte aligned, from the module's
    // aligned allocator FUN_06343fc0 (which stores magic 0x65981234 at the raw
    // base and the raw pointer at [aligned-4]); freed with FUN_06343ff0. Never
    // written again in this module: slot 0 returns it and slots 4/5 append it
    // to the caller's list. Contents opaque.
    IVP_Compact_Ledge* GeometryBlock;
};
static_assert(sizeof(IVP_SurfaceManager) == 0x04, "IVP_SurfaceManager size mismatch (vptr only)");
static_assert(sizeof(IVP_SurfaceManager_Default) == 0x08, "IVP_SurfaceManager_Default size mismatch");

// GeometryBlock's size and alignment (`PUSH 0x10; PUSH 0x20` at 0636238e).
constexpr std::size_t kIVPSurfaceManagerGeometryBlockSize  = 0x20;
constexpr std::size_t kIVPSurfaceManagerGeometryBlockAlign = 0x10;

// DAT_0658afe0: the module's single static IVP_SurfaceManager_Default.
// FUN_06362370 is its constructor (not a getter), reached from the CRT static
// initializer LAB_06362350 (table entry 063ea610) via the thunk FUN_06362360;
// FUN_06362550 registers LAB_06362560 (the destructor) with atexit. Retail has
// no getter: IVP_Ball's ctor takes the address directly (063625c4).
extern IVP_SurfaceManager_Default g_IVP_DefaultSurfaceManager;

#pragma pack(pop)
