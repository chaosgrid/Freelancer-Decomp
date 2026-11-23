// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- Behavior (the AI behavior base class) and the Behavior_* helpers; also the two
// shared follower helpers FollowerExternalIfaceIsPlayer / FollowerPlayerShipOrNull, declared for
// the follower/formation TUs in common_behavior_internal.h.

#include "common_behavior.h"
#include "AI/common_behavior_internal.h" // CmnBehavior:: -- the shared follower helpers defined here
#include "common_aisteer.h" // GetIfacePositionOrZero, IsWithinInterceptRangeOfIface
#include "common_systemmanager.h" // SystemManager -- BaseFollowerBehavior::NavHelper's real type
#include "common_misc.h" // FastInvSqrt
#include "common_csys.h"  // Matrix, Transform, MatrixSetIdentity, Acquire3DMathEngine
#include "common_iface.h" // IObjInspect / IObjRW / ObjInspectOf / ObjRwFromWatchable
#include "x86math.h"      // I3DMathEngine::TransformMatrix (+0x2c), FVector, FMatrix
#include "common_pathfind.h" // PathfindManager -- Behavior::OnDisengage / Behavior::SetCruiseLock

#include <cmath>
#include <cstdint>
#include <cstring>

// FUN_062e5b70. Null-guarded IObjInspect slot 0xd4 (get_projected_throttle) trampoline. See
// common_behavior.h.
float Behavior_InvokeManagerSlotD4(IObjInspect const* Obj, float Value, ThrustEquipType Type)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408

    return Obj->get_projected_throttle(Value, Type);
}

// FUN_062d21c0. See common_behavior.h for the receiver. No null test, as retail: every call
// site has already called through the same pointer.
void Behavior_ReleaseDirectiveTarget(void* Target)
{
    unsigned char* const Flags28 = reinterpret_cast<unsigned char*>(Target) + 0x28;
    *Flags28 = static_cast<unsigned char>((*Flags28 & 0xfd) | 0x01);
}

// FUN_062e5af0. Same shape as Behavior_InvokeManagerSlotD4, slot 0xd8 (get_speed).
float Behavior_InvokeManagerSlotD8(IObjInspect const* Obj, float Value, ThrustEquipType Type)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408

    return Obj->get_speed(Value, Type);
}

// FUN_062e5950.
float Behavior_GetAngularDistanceTravelled(IObjInspect const* Obj, float From, float To, float Dt,
                                           float MaxSpeed)
{
    if (Obj == nullptr)
        return 0.0f;                                   // DAT_06399408
    // Written as `< || ==` so NaN goes on to the call, as retail's flag test does.
    if (MaxSpeed < 0.0f || MaxSpeed == 0.0f)
        return 0.0f;
    if (static_cast<double>(Dt) < 0.001)
        Dt = 0.0f;
    return Obj->get_angular_distance_travelled(From, To, Dt, MaxSpeed);   // slot 0xec
}

// FUN_062e6010. No null test of its own, as retail: every caller pre-validates Iface.
float Behavior_GetIfaceMaxScalarAttribute(IObjInspect const* Iface)
{
    // Slot 0x2c is get_physical_radius(float& Radius, Vector& Center).
    float Radius = 1.0f;
    Vector Center{ 0.0f, 0.0f, 0.0f };
    Iface->get_physical_radius(Radius, Center);

    float AtmosphereRange = 0.0f;
    Iface->get_atmosphere_range(AtmosphereRange);

    return (AtmosphereRange > Radius) ? AtmosphereRange : Radius;
}

// FUN_062e5850. See common_behavior.h.
float Behavior_QueryRadiusEffect(IObjInspect const* Obj, float Unused2, float Radius, float Falloff, ThrustEquipType Type)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408

    // Only a strict less-than clamps (NaN is kept).
    if (Radius < 0.0f)
        Radius = 0.0f;
    if (Falloff < 0.0f)
        Falloff = 0.0f;

    // No vtable call when both are exactly 0.0f.
    if (Radius == 0.0f && Unused2 == 0.0f)
        return 0.0f;

    // Double 0.001 at DAT_0639c9f0.
    if ((double)Radius < 0.001)
        Radius = 0.0f;

    return Obj->get_time_to_accelerate(Unused2, Radius, Falloff, Type);
}

// FUN_062e58f0. See common_behavior.h.
float Behavior_QueryFalloffEffect(IObjInspect const* Obj, float Unused2, float Radius, float Falloff, ThrustEquipType Type)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408

    // No vtable call when Falloff is strictly negative (NaN continues).
    if (Falloff < 0.0f)
        return 0.0f;

    // Double 0.001 at DAT_0639c9f0.
    if ((double)Radius < 0.001)
        Radius = 0.0f;

    return Obj->get_distance_travelled(Unused2, Radius, Falloff, Type);
}

// FUN_062e59b0. Retail inlines the slot calls of Behavior_InvokeManagerSlotD8 / QueryRadiusEffect / QueryFalloffEffect
// (their null tests and the 0/0 early-out drop away because the arguments are known);
// the clamps that remain are reproduced in retail's order.
float Behavior_QueryChainedRadiusEffect(void* Obj, float Radius, int Selector)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408

    IObjInspect const* const Inspect = ObjInspectOf(Obj);
    ThrustEquipType const Type = (ThrustEquipType)Selector;

    // Value stays on the FPU.
    double Value = (double)Inspect->get_speed(1.0f, Type) - 1.0;           // slot 0xd8
    if (Value < 0.0)
        Value = 0.0;
    if (Value < Radius)
        return 0.0f;

    // The parameter itself snaps here.
    if ((double)Radius < 0.001)
        Radius = 0.0f;

    // QueryRadiusEffect's clamps on the inlined arguments.
    float RadiusCopy = Radius;
    float Falloff = (float)Value;
    if (RadiusCopy < 0.0f)
        RadiusCopy = 0.0f;
    if (Value < 0.0)
        Falloff = 0.0f;
    if ((double)RadiusCopy < 0.001)
        RadiusCopy = 0.0f;

    float const RadiusResult = Inspect->get_time_to_accelerate(1.0f, RadiusCopy, Falloff, Type);
    return Inspect->get_distance_travelled(1.0f, Radius, RadiusResult, Type);
}

// FUN_062e5ab0. See common_behavior.h.
float Behavior_QueryFixedFalloffEffect(void* Obj, float Radius)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408
    if (Radius < 0.001f) // DAT_0639e6c8
        return 0.0f;

    // 1000.0f at DAT_063a0760; selector fixed to literal 1.
    return Behavior_QueryFalloffEffect(ObjInspectOf(Obj), 0.0f, Radius, 1000.0f, ThrustEquipEngine);
}

// FUN_062e5bc0. Slot 0xdc (get_initial_speed_to_coast_distance). See common_behavior.h.
float Behavior_InvokeManagerSlotDC(IObjInspect const* Obj, float Value)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408
    if (Value == 0.0f)
        return 0.0f;

    return Obj->get_initial_speed_to_coast_distance(Value, true);
}

// FUN_062e5b20. See common_behavior.h.
float Behavior_InvokeManagerSlotF0(IObjInspect const* Obj, float Speed, float MaxSpeed)
{
    if (Obj == nullptr)
        return 0.0f; // DAT_06399408
    return Obj->get_angular_speed_XY(Speed, MaxSpeed);
}

// FUN_062e5b50, cdecl: IObjInspect::get_angular_speed_Z (slot 0xf4) or 0.0f for a null object.
float Behavior_InvokeManagerSlotF4(IObjInspect const* Obj, float Speed)
{
    if (Obj == nullptr)
        return 0.0f;
    return Obj->get_angular_speed_Z(Speed);
}

// FUN_062e5ba0, cdecl: IObjInspect::get_projected_axis_throttle_XY (slot 0xf8) or 0.0f.
float Behavior_InvokeManagerSlotF8(IObjInspect const* Obj, float Value)
{
    if (Obj == nullptr)
        return 0.0f;
    return Obj->get_projected_axis_throttle_XY(Value);
}

// FUN_062e61f0: a `Node->vtbl[0xc](Node)` tail-thunk, no null test. Slot 0xc is
// get_transform(); slots 3 and 4 are COMDAT-folded onto one address, so the pointer this hands
// back is equally the object's orientation Matrix (common_iface.h).
Transform const* Behavior_InvokeNodeSlotC(void* Node)
{
    return &ObjInspectOf(Node)->get_transform();
}

// ============================================================================
// Behavior -- the base class's own +0x1c per-tick delta.
//
// An accessor pair over a raw offset, not a data member: nineteen classes re-declare
// Behavior's whole +0x04..+0x29 block on themselves, and their offsetof static_asserts depend
// on sizeof(Behavior) staying 4 (see common_behavior.h). Moving the block into the base is an
// open TODO that has to touch all of them at once.
//
// The offset is retail's: FUN_062d2020 zeroes +0x1c, and IBehaviorManager::UpdateBehaviorTick
// stores a float there through a bare Behavior* just before dispatching OnUpdate.
// ============================================================================

float Behavior::GetTickDelta() const
{
    return *reinterpret_cast<float const*>(reinterpret_cast<char const*>(this) + 0x1c);
}

void Behavior::SetTickDelta(float Delta)
{
    *reinterpret_cast<float*>(reinterpret_cast<char*>(this) + 0x1c) = Delta;
}

// Vtable slot 2 (+0x08), 0x0629afd0. Same raw-offset device as SetTickDelta.
void Behavior::SetObjectDb(IObjDB* ObjectDb)
{
    *reinterpret_cast<IObjDB**>(reinterpret_cast<char*>(this) + 0x18) = ObjectDb;
}

// Vtable slot 23 (+0x5c), LAB_062c2f50, shared by all 21 Behavior vtables. Called from
// IBehaviorManager::set_current_directive.
void Behavior::cancel_notify()
{
    unsigned char* const Flags28 = reinterpret_cast<unsigned char*>(this) + 0x28;
    *Flags28 = static_cast<unsigned char>(*Flags28 & 0xfd);
}

// Same raw-offset device for the type id FUN_062d2020 stores at +0x04. Read-only: the storage
// is written by whichever derived class declares it.
int Behavior::GetTypeId() const
{
    return *reinterpret_cast<int const*>(reinterpret_cast<char const*>(this) + 0x04);
}

// Same raw-offset device for the NavHelper pointer at +0x24, written by FUN_062d2090.
void* Behavior::GetNavHelper() const
{
    return *reinterpret_cast<void* const*>(reinterpret_cast<char const*>(this) + 0x24);
}

// Vtable slot 3 (+0x0c), FUN_062d20d0.
void Behavior::OnEngage()
{
    char* const Self = reinterpret_cast<char*>(this);
    *reinterpret_cast<int*>(Self + 0x0c) = 1;
    *reinterpret_cast<int*>(Self + 0x10) = 1;
    unsigned char* const Flags28 = reinterpret_cast<unsigned char*>(Self + 0x28);
    *Flags28 = static_cast<unsigned char>(*Flags28 & 0xfe);
    *reinterpret_cast<float*>(Self + 0x1c) = 0.0f;
}

// Vtable slot 4 (+0x10), FUN_062d20f0. The manager pointer itself is not null-tested.
void Behavior::OnDisengage()
{
    char* const Self = reinterpret_cast<char*>(this);
    IBehaviorManager* const Manager = *reinterpret_cast<IBehaviorManager* const*>(Self + 0x20);
    PathfindManager* const Pathfind = Manager->Pathfind;
    if (Pathfind != nullptr)
        Pathfind->Field_064 = 0;
    *reinterpret_cast<unsigned char*>(Self + 0x29) = 0;
}

// FUN_062d2020, the vptr store aside; stores in retail order.
void Behavior::ConstructBase(int TypeId)
{
    char* const Self = reinterpret_cast<char*>(this);
    *reinterpret_cast<int*>(Self + 0x04) = TypeId;
    *reinterpret_cast<unsigned int*>(Self + 0x08) = 0;
    *reinterpret_cast<void**>(Self + 0x14) = nullptr;
    *reinterpret_cast<void**>(Self + 0x24) = nullptr;
    *reinterpret_cast<void**>(Self + 0x18) = nullptr;
    *reinterpret_cast<unsigned int*>(Self + 0x1c) = 0;
    *reinterpret_cast<unsigned char*>(Self + 0x29) = 0;
    unsigned char* const Flags28 = reinterpret_cast<unsigned char*>(Self + 0x28);
    *Flags28 = static_cast<unsigned char>(*Flags28 & 0xfc);
    *reinterpret_cast<int*>(Self + 0x0c) = 2;
    *reinterpret_cast<int*>(Self + 0x10) = 1;
}

// FUN_062d2090, RET 0xc. get_id is IObjInspect slot 0x20.
bool Behavior::BindContext(void* Owner, void* Manager, void* Sys)
{
    if (Owner == nullptr || Sys == nullptr)
        return false;
    char* const Self = reinterpret_cast<char*>(this);
    *reinterpret_cast<void**>(Self + 0x14) = Owner;
    unsigned int const Id = ObjInspectOf(Owner)->get_id();
    *reinterpret_cast<void**>(Self + 0x24) = Sys;
    *reinterpret_cast<unsigned int*>(Self + 0x08) = Id;
    *reinterpret_cast<void**>(Self + 0x20) = Manager;
    return true;
}

// Vtable slot 9 (+0x24).
int Behavior::GetStatus()
{
    return *reinterpret_cast<int const*>(reinterpret_cast<char const*>(this) + 0x10);
}

// FUN_062d21d0: tail-jump to FUN_062dd3d0 (PathfindManager::SetCruiseLock); no null tests.
void Behavior::SetCruiseLock(int Value, int Mask)
{
    IBehaviorManager* const Manager =
        *reinterpret_cast<IBehaviorManager* const*>(reinterpret_cast<char const*>(this) + 0x20);
    Manager->Pathfind->SetCruiseLock(Value, static_cast<unsigned int>(Mask));
}

// ----------------------------------------------------------------------------
// Behavior vtable slot 16 (+0x40) -- FUN_062d2110.
//
// Queries is_cruise_active (slot 0x5c) on the object behind NavHelper+0x8 (the Watchable
// list head, -8 adjusted); does nothing while cruising outside a trade lane. Otherwise toggles
// ThrustManager (NavHelper+0x18) thrusters and sets throttle 1.0f, mode 2 in a trade lane,
// mode 0 otherwise.
//
// Retail dereferences [this+0x24] with no null test, and with a null list head it calls
// through a null receiver; both are reproduced as written.
//
// NAME CAVEAT: `set_afterburner` is this slot's inherited positional name and was not
// re-derived. The body is a cruise/throttle-gate poke, not an afterburner write, so the name
// is worth re-checking against the pub::AI interface; renaming slot 16 touches
// IBehaviorManager and every subclass.
void Behavior::set_afterburner(bool bOn)
{
    void* const NavHelper = GetNavHelper();

    void* const Head = *reinterpret_cast<void* const*>(static_cast<char*>(NavHelper) + 0x8);
    bool bCruise = false;
    bool bTradeLane = false;
    (void)ObjInspectOf(ObjRwFromWatchable(Head))->is_cruise_active(bCruise, bTradeLane);

    if (bCruise && !bTradeLane)
        return;

    ThrustManager* const Thrust = &((SystemManager*)NavHelper)->Thrust;

    if (!bOn)
    {
        Thrust->SetThrustersActive(false);
        return;
    }

    Thrust->SetThrustersActive(true);

    Thrust->SetThrottle(1.0f, bTradeLane ? 2 : 0);
}

// ----------------------------------------------------------------------------
// Behavior vtable slot 17 (+0x44) -- FUN_062d21b0: a tail JMP to FUN_062e9030,
// ThrustManager::SetStrafeDir, on NavHelper+0x18. The argument is one dword.
void Behavior::slide_strafe_burst(StrafeDir Dir)
{
    ((SystemManager*)GetNavHelper())->Thrust.SetStrafeDir(Dir);
}

// ============================================================================
// Shared follower helpers (common_behavior_internal.h, namespace CmnBehavior). Neither is a
// retail function: both are inlined instruction sequences, on the SystemManager and on a
// CObject.
// ============================================================================

// Sys+0x1580 -> IObjInspect::is_player (slot 0xbc). Retail tests neither pointer
// (e.g. .text:062ecb23, 062cb76a).
bool CmnBehavior::FollowerExternalIfaceIsPlayer(SystemManager* Sys)
{
    return Sys->SelfTarget.Iface->is_player();
}

// The "[obj+0x4c] & mask == mask" object-class test (common_cobject.h documents the 0x103
// variant). Used at .text:062cb7bd and 062cb7e2 to reduce a cobject() result to "a
// player-flavoured ship, else null".
CObject const* CmnBehavior::FollowerPlayerShipOrNull(CObject const* Object)
{
    if (Object == nullptr)
        return nullptr;
    unsigned int const Class = *reinterpret_cast<unsigned int const*>(
        reinterpret_cast<char const*>(Object) + 0x4c);
    return ((Class & 0x503u) == 0x503u) ? Object : nullptr;
}
