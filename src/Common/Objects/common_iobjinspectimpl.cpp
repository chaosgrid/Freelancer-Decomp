// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll -- IObjInspectImpl (the CObject-backed IObjInspect implementation), its file-local
// class-mask helpers and ResolveLaneStart, the exported std::vector<IObjInspect*> instantiation,
// and InitEquipStatusBits.

#include "common_iface.h"
#include "common_behavior.h"
#include "common_aisteer.h"
#include "common_cobjdb.h"
#include "common_csys.h"
#include "x86math.h"
#include "common_goods.h"
#include "common_iustategraph.h"
#include "common_misc.h"
#include "common_newchar.h"
#include "common_pub.h"
#include "common_pathfind.h"
#include "common_universe.h" // Universe::IZone (CSimple::CurrentZone, get_zone_props)
#include "dacom.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

// ObjectClass bitmask checks from retail code:
//   & 3 == 3         → CSimple-derived (CSimple, CEqObj, CShip, etc.)
//   & 0x103 == 0x103 → CEqObj-derived (CEqObj, CShip)
//   & 0x503 == 0x503 → CShip
//   & 0x303 == 0x303 → CSolar
//   == 0xa7          → CGuided

// ============================================================================
// IObjInspectImpl — ctor / dtor / operator=
// ============================================================================

IObjInspectImpl::IObjInspectImpl()
{
    // FUN_062befa0. The base constructors do the vtable stores and field zeroing.
}

IObjInspectImpl::IObjInspectImpl(IObjInspectImpl const& Other)
{
    // FUN_062befd0 (from the listing). Copies the Watchable head (+0x08) and Object (+0x10)
    // raw. Retail's base adjustments would read through null for a null &Other; not reproduced.
    Head = Other.Head;
    Object = Other.Object;
}

IObjInspectImpl::~IObjInspectImpl()
{
    // FUN_062bf070: only the base destructors (~CBase, then ~Watchable, which unlinks every
    // watcher of this instance).
}

IObjInspectImpl& IObjInspectImpl::operator=(IObjInspectImpl const& Other)
{
    // FUN_062bf030. As the copy ctor; no self-assignment test.
    Head = Other.Head;
    Object = Other.Object;
    return *this;
}

// ============================================================================
// IObjInspectImpl — helper accessors
// ============================================================================

// File-local, not IObjInspectImpl members: retail inlines these class-mask tests at each call
// site and exports no GetSimple/GetShip/GetEqObj on IObjInspectImpl.
namespace
{
    CSimple* InspectSimple(CObject const* Object)
    {
        if (Object != nullptr && ((unsigned)Object->ObjectClass & CObject::ClassSimple) == CObject::ClassSimple) {
            return static_cast<CSimple*>(const_cast<CObject*>(Object));
        }
        return nullptr;
    }

    CShip* InspectShip(CObject const* Object)
    {
        if (Object != nullptr && (Object->ObjectClass & CObject::ClassShip) == CObject::ClassShip) {
            return static_cast<CShip*>(const_cast<CObject*>(Object));
        }
        return nullptr;
    }

    CEqObj* InspectEqObj(CObject const* Object)
    {
        if (Object != nullptr && (Object->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj) {
            return static_cast<CEqObj*>(const_cast<CObject*>(Object));
        }
        return nullptr;
    }
}

// ============================================================================
// IObjInspectImpl — core identity
// ============================================================================

int IObjInspectImpl::get_toughness() const
{
    // FUN_062bf160
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        unsigned Rank = 0;
        Reputation::Vibe::GetRank(Eq->VibeId, Rank);
        return static_cast<int>(Rank);
    }
    return 0;
}

Vector const& IObjInspectImpl::get_position() const
{
    // FUN_062bf1a0. Object (+0x10) is not null-tested, here or in the accessors below.
    return Object->WorldPosition;
}

Vector IObjInspectImpl::get_velocity() const
{
    // FUN_062bf1b0 (virtual +0x54)
    return Object->get_velocity();
}

Vector IObjInspectImpl::get_angular_velocity() const
{
    // FUN_062bf1d0 (virtual +0x58)
    return Object->get_angular_velocity();
}

Transform const& IObjInspectImpl::get_transform() const
{
    // FUN_062bf1f0, ICF-folded with get_orientation: Orientation (+0x08) followed by
    // WorldPosition (+0x2c) is one Transform.
    return Object->get_transform();
}

Matrix const& IObjInspectImpl::get_orientation() const
{
    // FUN_062bf1f0 (shared with get_transform)
    return Object->Orientation;
}

long IObjInspectImpl::get_index() const
{
    // FUN_062bf200
    return Object->EngineInstanceId;
}

unsigned int const IObjInspectImpl::get_id() const
{
    // FUN_062bf210. Retail reads Id (+0xb0) through the CSimple narrowing with no null test
    // (Server.dll's copy at 06ce61d0 is identical).
    return InspectSimple(Object)->Id;
}

int IObjInspectImpl::get_good_id(unsigned& OutId) const
{
    // FUN_062bf230. The loot's contents archetype (+0xec, no null test), not the crate's.
    if (Object != nullptr && (Object->ObjectClass & CObject::ClassLoot) == CObject::ClassLoot)
    {
        OutId = Arch2Good(((CLoot const*)Object)->ContentsArch->Nickname.Id);
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_archetype_extents(Vector& Min, Vector& Max) const
{
    // FUN_062bf270 (virtual +0x70)
    if (Object->get_surface_extents(Min, Max))
    {
        return 0;
    }
    memset(&Min, 0, sizeof(Min));
    memset(&Max, 0, sizeof(Max));
    return -5;
}

float IObjInspectImpl::get_mass() const
{
    // FUN_062bf2d0 (virtual +0x6c)
    return Object->get_mass();
}

int IObjInspectImpl::get_physical_radius(float& Radius, Vector& Center) const
{
    // FUN_062bf2b0 (virtual +0x64)
    Radius = Object->get_physical_radius_r(Center);
    return 0;
}

// ============================================================================
// IObjInspectImpl — status / health
// ============================================================================

int IObjInspectImpl::get_status(float& Health) const
{
    // FUN_062bf2e0 (from the listing). TypeBits (+0xe0) and EquipManager (+0xe4) are read
    // through the narrowed pointers with no null test. For TypeBits 0x80 the health is 0 as
    // soon as one shield (mask 0x100, IsFunctioning = vtable +0xc) is down, else 1; -3 only
    // when there is none at all. Otherwise the relative health, negative clamped to 0.
    CSimple* S = InspectSimple(Object);
    if (S->TypeBits == OBJ_TRADELANE_RING) {
        CEqObj* Eq = InspectEqObj(Object);
        CEquipTraverser Walk(EquipTypeShield);
        CEquip* E = Eq->EquipManager.Traverse(Walk);
        if (E == nullptr) {
            return -3;
        }
        do {
            if (!E->IsFunctioning()) {
                Health = 0.0f;
                return 0;
            }
            Health = 1.0f;
            E = Eq->EquipManager.Traverse(Walk);
        } while (E != nullptr);
        return 0;
    }
    // The unrounded value is tested; NaN is stored as is.
    double const Relative = S->get_relative_health();
    Health = (float)Relative;
    if (Relative < 0.0) {
        Health = 0.0f;
    }
    return 0;
}

int IObjInspectImpl::get_status(float& HitPts, float& MaxHitPts) const
{
    // FUN_062bf3d0
    CSimple* S = InspectSimple(Object);
    if (S == nullptr) {
        return -3;
    }
    HitPts = S->get_hit_pts();
    MaxHitPts = S->get_max_hit_pts();
    return 0;
}

int IObjInspectImpl::get_shield_status(float& Health, bool& bActive) const
{
    // FUN_062bf410. The 3-argument overload is vtable +0x44.
    float CurHealth = 0.0f;
    float MaxHealth = 0.0f;
    int Ret = get_shield_status(CurHealth, MaxHealth, bActive);
    if (Ret != 0) {
        return Ret;
    }
    if (MaxHealth > 0.0f) {
        Health = CurHealth / MaxHealth;
    } else {
        Health = 0.0f;
    }
    return 0;
}

int IObjInspectImpl::get_shield_status(float& CurHealth, float& MaxHealth, bool& bActive) const
{
    // FUN_062bf480 (from the listing). Sums GetHitPoints (vtable +0x3c) and GetMaxHitPoints
    // (+0x38) over the shields; bActive is overwritten per shield, so it ends as the last one's
    // IsFunctioning (+0xc).
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq == nullptr) {
        return -3;
    }
    CurHealth = 0.0f;
    MaxHealth = 0.0f;
    bActive = false;
    CEquipTraverser Walk(EquipTypeShield);
    CEquip* E = Eq->EquipManager.Traverse(Walk);
    while (E != nullptr) {
        CurHealth += E->GetHitPoints();
        MaxHealth += E->GetMaxHitPoints();
        bActive = E->IsFunctioning();
        E = Eq->EquipManager.Traverse(Walk);
    }
    return 0;
}

int IObjInspectImpl::get_data(void const*& OutData) const
{
    // FUN_062bf540
    OutData = nullptr;
    return -3;
}

// ============================================================================
// IObjInspectImpl — movement / physics queries
// ============================================================================

float IObjInspectImpl::get_time_to_accelerate(float From, float To, float Dt,
                                               ThrustEquipType Type) const
{
    // FUN_062bf560. The CShip forwarders below return -1.0f (DAT_06399404) for a non-ship.
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_time_to_accelerate(From, To, Dt, Type);
    }
    return -1.0f;
}

float IObjInspectImpl::get_distance_travelled(float Speed, float Dt, float Accel,
                                               ThrustEquipType Type) const
{
    // FUN_062bf5a0
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_distance_travelled(Speed, Dt, Accel, Type);
    }
    return -1.0f;
}

float IObjInspectImpl::get_initial_speed_to_coast_distance(float Speed, bool bStop) const
{
    // FUN_062bf5e0
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_initial_speed_to_coast_distance(Speed, bStop);
    }
    return -1.0f;
}

float IObjInspectImpl::get_speed(float Throttle, ThrustEquipType Type) const
{
    // FUN_062bf650
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_speed(Throttle, Type);
    }
    return -1.0f;
}

float IObjInspectImpl::get_projected_throttle(float Speed, ThrustEquipType Type) const
{
    // FUN_062bf620
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_projected_throttle(Speed, Type);
    }
    return -1.0f;
}

float IObjInspectImpl::get_time_to_accelerate_angularly(float From, float To, float Dt) const
{
    // FUN_062bf720
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_time_to_accelerate_angularly(From, To, Dt);
    }
    return -1.0f;
}

float IObjInspectImpl::get_time_to_accelerate_angularly(float From, float To, float Dt,
                                                         float MaxSpeed) const
{
    // FUN_062bf680
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_time_to_accelerate_angularly(From, To, Dt, MaxSpeed);
    }
    return -1.0f;
}

float IObjInspectImpl::get_angular_distance_travelled(float From, float To, float Dt) const
{
    // FUN_062bf760
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_angular_distance_travelled(From, To, Dt);
    }
    return -1.0f;
}

float IObjInspectImpl::get_angular_distance_travelled(float From, float To, float Dt,
                                                       float MaxSpeed) const
{
    // FUN_062bf6d0
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_angular_distance_travelled(From, To, Dt, MaxSpeed);
    }
    return -1.0f;
}

float IObjInspectImpl::get_angular_speed_XY(float Speed, float MaxSpeed) const
{
    // FUN_062bf800
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_angular_speed_XY(Speed, MaxSpeed);
    }
    return -1.0f;
}

float IObjInspectImpl::get_projected_axis_throttle_XY(float Speed) const
{
    // FUN_062bf7a0
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_projected_axis_throttle_XY(Speed);
    }
    return -1.0f;
}

float IObjInspectImpl::get_projected_axis_throttle_Z(float Speed) const
{
    // FUN_062bf7d0
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_projected_axis_throttle_Z(Speed);
    }
    return -1.0f;
}

float IObjInspectImpl::get_angular_speed_Z(float Speed) const
{
    // FUN_062bf840
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_angular_speed_Z(Speed);
    }
    return -1.0f;
}

float IObjInspectImpl::get_max_bank_angle() const
{
    // FUN_062bf870
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_max_bank_angle();
    }
    return 0.0f;
}

int IObjInspectImpl::get_throttle(float& OutThrottle) const
{
    // FUN_062bf8a0 (CShip +0x29c). The accessors below leave the output untouched on -3.
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutThrottle = Sh->Throttle;
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_axis_throttle(Vector& OutThrottle) const
{
    // FUN_062bf8d0 (+0x24c)
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutThrottle = Sh->get_axis_throttle();
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_nudge_vec(Vector& OutVec) const
{
    // FUN_062bf910 (+0x274)
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutVec = Sh->get_nudge_vec();
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_strafe_dir(StrafeDir& OutDir) const
{
    // FUN_062bf960 (+0x298)
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutDir = static_cast<StrafeDir>(Sh->get_strafe_dir());
        return 0;
    }
    return -3;
}

// ============================================================================
// IObjInspectImpl — combat / target
// ============================================================================

int IObjInspectImpl::are_thrusters_active(bool& OutActive) const
{
    // FUN_062bfa80
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutActive = Sh->any_thrusters_on();
        return 0;
    }
    return -3;
}

int IObjInspectImpl::is_cruise_active(bool& OutActive) const
{
    // FUN_062bf9a0
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutActive = Sh->is_cruise_active();
        return 0;
    }
    return -3;
}

int IObjInspectImpl::is_cruise_active(bool& OutCruise, bool& OutCharging) const
{
    // FUN_062bf9e0 (from the listing). OutCharging (the SDK calls it a tradelane flag): cruise
    // still charging, CruiseChargeTime > elapsed on the first engine. Retail reads the charge time
    // through CEArmor::ArmorArch on the CEEngine (same Arch field, Archetype::Engine+0x80); the
    // port calls CEEngine::GetCruiseChargeTime.
    CShip* Sh = InspectShip(Object);
    if (Sh == nullptr) {
        return -3;
    }
    CEquip* Equip = const_cast<CEquip*>(Sh->EquipManager.FindFirst(EquipTypeEngine));
    CEEngine* Engine = CEEngine::cast(Equip);
    if (Engine == nullptr) {
        return -3;
    }
    OutCruise = Engine->IsCruiseEngaged();
    if (!OutCruise) {
        OutCharging = false;
        return 0;
    }
    OutCharging = (Engine->GetCruiseChargeTime() > Engine->GetCruiseChargeTimeElapsed());
    return 0;
}

int IObjInspectImpl::get_target(IObjRW*& OutTarget) const
{
    // FUN_062bfc00. No null test on Object; the class is compared exactly, not masked.
    if (Object->ObjectClass == CObject::ClassGuided) {
        CGuided const* G = (CGuided const*)Object;
        OutTarget = G->get_target();
        return 0;
    }
    if (Object->ObjectClass == CObject::ClassShip) {
        CShip const* Sh = (CShip const*)Object;
        OutTarget = Sh->get_target();
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_rank(uint& OutRank) const
{
    // FUN_062bfc50. OutRank is written only on success.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        int VibeId = static_cast<int>(Eq->VibeId);
        unsigned Rank;
        if (Reputation::Vibe::GetRank(VibeId, Rank) == 0) {
            OutRank = Rank;
            return 0;
        }
        return -2;
    }
    return -3;
}

int IObjInspectImpl::get_affiliation(uint& OutAffil) const
{
    // FUN_062bfcb0, as get_rank.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        int VibeId = static_cast<int>(Eq->VibeId);
        unsigned Affiliation;
        if (Reputation::Vibe::GetAffiliation(VibeId, Affiliation, false) == 0) {
            OutAffil = Affiliation;
            return 0;
        }
        return -2;
    }
    return -3;
}

int IObjInspectImpl::get_type(uint& OutType) const
{
    // FUN_062bfd10. OutType is zeroed even on the -3 path.
    OutType = 0;
    CSimple* S = InspectSimple(Object);
    if (S != nullptr) {
        OutType = S->TypeBits;
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_base(uint& OutBase) const
{
    // FUN_062bfd50. Solars only (+0x160).
    if (Object != nullptr && (Object->ObjectClass & CObject::ClassSolar) == CObject::ClassSolar) {
        OutBase = static_cast<CEqObj const*>(Object)->DockingBaseId;
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_dock_target(uint& OutDock) const
{
    // FUN_062bfd80. Solars only, as get_base (+0x164).
    if (Object != nullptr && (Object->ObjectClass & CObject::ClassSolar) == CObject::ClassSolar) {
        CEqObj* Eq = static_cast<CEqObj*>(const_cast<CObject*>(Object));
        OutDock = Eq->DockingPointBaseId;
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_zone_props(unsigned long& OutProps) const
{
    // FUN_062bfdb0
    CSimple* S = InspectSimple(Object);
    if (S != nullptr)
    {
        if (S->CurrentZone != nullptr)
        {
            // Zone slot +0x1c (062a6690).
            OutProps = S->CurrentZone->GetProperties();
            return 0;
        }
        OutProps = 0;
        return -2;
    }
    return -3;
}

float IObjInspectImpl::get_scanner_interference() const
{
    // FUN_062bfe00. The zone-adjusted accessor, not the raw +0xa8 falloff.
    CSimple const* const Simple = InspectSimple(Object);
    if (Simple != nullptr)
        return Simple->get_scanner_interference();
    return 0.0f;
}

int IObjInspectImpl::get_power(float& OutPower) const
{
    // FUN_062bfe20 (+0x198)
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        OutPower = Eq->Power;
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_reputation(float& OutRep, uint const& GroupId) const
{
    // FUN_062bfbc0. Any Vibe error becomes -4.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        int VibeId = static_cast<int>(Eq->VibeId);
        int Result = Reputation::Vibe::GetGroupFeelingsTowards(VibeId, GroupId, OutRep);
        if (Result != 0)
            return -4;
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_attitude_towards(float& OutAtt, IObjInspect const* Other) const
{
    // FUN_062bfac0
    if (Other == nullptr) {
        return -4;
    }
    CObject const* OtherObj = Other->cobject();
    if (OtherObj == nullptr || (OtherObj->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj) {
        return -3;
    }
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        CEqObj const* OtherEq = static_cast<CEqObj const*>(OtherObj);
        OutAtt = Eq->attitude_towards(OtherEq);
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_attitude_towards_symmetrical(float& OutAtt, IObjInspect const* Other,
                                                       float& OutReverse) const
{
    // FUN_062bfb40
    if (Other == nullptr) {
        return -4;
    }
    CObject const* OtherObj = Other->cobject();
    if (OtherObj == nullptr || (OtherObj->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj) {
        return -3;
    }
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        CEqObj const* OtherEq = static_cast<CEqObj const*>(OtherObj);
        Eq->attitude_towards_symmetrical(OutAtt, OtherEq, OutReverse);
        return 0;
    }
    return -3;
}

bool IObjInspectImpl::is_player() const
{
    // FUN_062bfe60 (+0x2bd)
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->bIsPlayer != 0;
    }
    return false;
}

int IObjInspectImpl::get_hardpoint(char const* Name, Vector* Pos, Matrix* Orient) const
{
    // FUN_062bfe90. No null test on Object.
    bool bSuccess = HardpointPosition(Object->EngineInstanceId, Name, Pos, Orient);
    return bSuccess ? 0 : -2;
}

int IObjInspectImpl::find_equipment(CEquip const*& OutEquip, unsigned short const& EquipId) const
{
    // FUN_062bfed0. OutEquip is written even for a destroyed equip.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr)
    {
        CEquip* Found = Eq->EquipManager.FindByID(EquipId);
        OutEquip = Found;
        if (Found != nullptr && !Found->IsDestroyed())
        {
            return 0;
        }
        return -2;
    }
    return -3;
}

int IObjInspectImpl::find_equipment(unsigned short* const Buf, uint MaxCount,
                                    uint TypeMask) const
{
    // FUN_062bff30. Always 0 for a CEqObj (retail's SBB/AND tail cannot produce anything
    // else). No null test on Buf.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq == nullptr)
        return -3;

    uint Index = 0;
    CEquipTraverser Walk(static_cast<int>(TypeMask));
    for (CEquip* EqItem = Eq->EquipManager.Traverse(Walk); EqItem != nullptr;
         EqItem = Eq->EquipManager.Traverse(Walk))
    {
        if (Index < MaxCount)
        {
            Buf[Index] = EqItem->Id;
            ++Index;
            if (MaxCount == Index)
                return 0;
        }
    }
    if (Index < MaxCount)
    {
        // Retail reads the terminator from the .rdata SUBOBJ_ID_NONE word.
        Buf[Index] = 0;
    }
    return 0;
}

int IObjInspectImpl::get_equipment_status(EquipStatus& OutStatus,
                                           unsigned short const& Id) const
{
    // FUN_062bffe0
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr)
    {
        CEquip* Found = Eq->EquipManager.FindByID(Id);
        if (Found != nullptr && !Found->IsDestroyed())
        {
            // The virtual GetStatus (0629ae80) sets three flag bits in place; OutStatus is not
            // zeroed first.
            Found->GetStatus(OutStatus);
            return 0;
        }
        return -2;
    }
    return -3;
}

int IObjInspectImpl::get_equipment_val(EquipmentVal& OutVal, unsigned short const& Id,
                                        EquipmentValType Type, float Param) const
{
    // FUN_062c0460 (from the listing). Writes one dword (one byte for gun types 9 / 0xa) at
    // &OutVal, nothing else.
    unsigned char* const Out = reinterpret_cast<unsigned char*>(&OutVal);
    int const Kind = static_cast<int>(Type);

    if (Object == nullptr || (Object->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
        return -3;
    CEquip* const Found = static_cast<CEqObj*>(Object)->EquipManager.FindByID(Id);
    if (Found == nullptr || Found->IsDestroyed())
        return -2;

    float FloatVal;
    int IntVal;
    switch (Found->Type)
    {
    case EquipTypePower:                                                       // 0x40000
    {
        // Type 0 -> ChargeRate (+0x74), type 4 -> Capacity (+0x70); 0.0f once destroyed.
        Archetype::Power const* const Arch = static_cast<Archetype::Power const*>(Found->Arch);
        if (Kind == EquipValPowerDraw)
            FloatVal = Found->IsDestroyed() ? 0.0f : Arch->ChargeRate;
        else if (Kind == EquipValCapacity)
            FloatVal = Found->IsDestroyed() ? 0.0f : Arch->Capacity;
        else
            return -3;
        std::memcpy(Out, &FloatVal, sizeof(FloatVal));
        return 0;
    }
    case EquipTypeEngine:                                                      // CEEngine
    {
        if (Kind != EquipValPowerDraw)
            return -3;
        // An out-of-range Param gives -4; NaN passes the clamp but fails the equality, so -4.
        float Throttle;
        if (1.0f < Param)
            Throttle = 1.0f;
        else if (-1.0f > Param)
            Throttle = -1.0f;
        else
            Throttle = Param;
        if (!(Throttle == Param))
            return -4;
        FloatVal = static_cast<CEEngine*>(Found)->GetPowerDraw(Throttle);
        std::memcpy(Out, &FloatVal, sizeof(FloatVal));
        return 0;
    }
    case EquipTypeThruster:                                                    // CEThruster
        if (Kind != EquipValPowerDraw)
            return -3;
        FloatVal = static_cast<CEThruster*>(Found)->GetPowerDraw();
        std::memcpy(Out, &FloatVal, sizeof(FloatVal));
        return 0;
    case EquipTypeGun:                                                         // CEGun
    {
        CEGun* const Gun = static_cast<CEGun*>(Found);
        // FUN_062969c0 (+0x4c) is the munition archetype, FUN_0629fb90 (+0x0c) the gun's.
        Archetype::Munition const* const Ammo = static_cast<Archetype::Munition const*>(Gun->ProjectileArch());
        Archetype::Gun const* const GunArch = static_cast<Archetype::Gun const*>(Found->Arch);
        switch (Kind)
        {
        case EquipValPowerDraw:                                                // vtable +0x9c
            FloatVal = (float)Gun->GetPowerDrawPerFire();
            std::memcpy(Out, &FloatVal, sizeof(FloatVal));
            return 0;
        case EquipValHullDamagePerFire:
        {
            // Unsigned widening; HullDamage +0x80.
            unsigned const PerFire = Gun->GetProjectilesPerFire();
            FloatVal = static_cast<float>(static_cast<double>(PerFire) * Ammo->HullDamage);
            std::memcpy(Out, &FloatVal, sizeof(FloatVal));
            return 0;
        }
        case EquipValRefireDelay:
            std::memcpy(Out, &GunArch->RefireDelay, sizeof(float));             // +0x90
            return 0;
        case EquipValAmmoCount:                                                // vtable +0xa0
            IntVal = Gun->GetAmmoCount();
            std::memcpy(Out, &IntVal, sizeof(IntVal));
            return 0;
        case EquipValRange:
            FloatVal = Gun->GetMunitionRange();
            std::memcpy(Out, &FloatVal, sizeof(FloatVal));
            return 0;
        case EquipValMunitionKind:
            // +0xac byte == 1 -> 3; HpType (+0xa8) == 0xa -> 2; else Seeker (+0x90) != 0.
            if (*reinterpret_cast<unsigned char const*>(&Ammo->bCruiseDisruptor) == 1)
                IntVal = kMunitionKindCruiseDisruptor;
            else if (Ammo->HpType == HpTypeTorpedo)
                IntVal = kMunitionKindTorpedo;
            else
                IntVal = Ammo->Seeker != 0 ? kMunitionKindSeeker : kMunitionKindPlain;
            std::memcpy(Out, &IntVal, sizeof(IntVal));
            return 0;
        case EquipValMuzzleVelocity:
            std::memcpy(Out, &GunArch->MuzzleVelocity, sizeof(float));          // +0x94
            return 0;
        case EquipValAutoTurret:
            Out[0] = *reinterpret_cast<unsigned char const*>(&GunArch->bAutoTurret);   // +0xba byte
            return 0;
        case EquipValSeeker:
            Out[0] = Ammo->Seeker != 0 ? 1 : 0;                                // byte
            return 0;
        case EquipValTargetId:
        {
            // The gun target's Id, or 0.
            CSimple* Target = nullptr;
            unsigned short SubId = 0;
            Gun->GetTarget(Target, SubId);
            IntVal = Target != nullptr ? static_cast<int>(Target->Id) : 0;
            std::memcpy(Out, &IntVal, sizeof(IntVal));
            return 0;
        }
        default:                                                               // 4, 7, > 0xb
            return -3;
        }
    }
    case EquipTypeScanner:                                                     // CEScanner
        if (Kind == EquipValRange)
        {
            FloatVal = static_cast<CEScanner*>(Found)->GetRadarRange();
            std::memcpy(Out, &FloatVal, sizeof(FloatVal));
            return 0;
        }
        if (Kind == EquipValCargoScanRange)
        {
            std::memcpy(Out, &static_cast<Archetype::Scanner const*>(Found->Arch)->CargoScanRange, sizeof(float));
            return 0;
        }
        return -3;
    case EquipTypeTractor:                                                     // CETractor
        if (Kind != EquipValRange)
            return -3;
        FloatVal = static_cast<CETractor*>(Found)->GetRange();
        std::memcpy(Out, &FloatVal, sizeof(FloatVal));
        return 0;
    default:
        return -3;
    }
}

int IObjInspectImpl::can_point_at(bool& OutCanPoint, unsigned short const& EquipId,
                                    Vector const& Point, float Angle) const
{
    // FUN_062c0170
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr)
    {
        CEquip* Found = Eq->EquipManager.FindByID(EquipId);
        if (Found == nullptr || Found->IsDestroyed())
            return -2;
        if (Found->GetType() != EquipTypeGun)
            return -3;
        CEGun* Gun = static_cast<CEGun*>(Found);
        OutCanPoint = Gun->CanPointAt(Point, Angle);
        return 0;
    }
    return -3;
}

int IObjInspectImpl::is_pointing_at(bool& OutIsPointing, unsigned short const& EquipId,
                                     float Angle) const
{
    // FUN_062c00e0
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr)
    {
        CEquip* Found = Eq->EquipManager.FindByID(EquipId);
        if (Found == nullptr || Found->IsDestroyed())
            return -2;
        if (Found->GetType() != EquipTypeGun)
            return -3;
        CEGun* Gun = static_cast<CEGun*>(Found);
        OutIsPointing = Gun->CanSeeTargetObject(Angle);
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_tgt_lead_fire_pos(unsigned short const& EquipId, Vector& OutPos) const
{
    // FUN_062c0050
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr)
    {
        CEquip* Found = Eq->EquipManager.FindByID(EquipId);
        if (Found == nullptr || Found->IsDestroyed())
            return -2;
        if (Found->GetType() != EquipTypeGun)
            return -3;
        CEGun* Gun = static_cast<CEGun*>(Found);
        bool bComputed = Gun->ComputeTgtLeadPosition(OutPos);
        return bComputed ? 0 : -1;
    }
    return -3;
}

int IObjInspectImpl::get_subtarget(unsigned short& OutSub) const
{
    // FUN_062c0200. As get_target: no null test, exact class compare.
    if (Object->ObjectClass == CObject::ClassGuided)
    {
        CGuided const* G = (CGuided const*)Object;
        OutSub = G->SubTarget;
        return 0;
    }
    if (Object->ObjectClass == CObject::ClassShip)
    {
        CShip const* S = (CShip const*)Object;
        OutSub = S->get_sub_target();
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_subtarget_center_of_mass(Vector& OutCenter) const
{
    // FUN_062c0240
    CShip const* Ship = InspectShip(Object);
    if (Ship == nullptr)
        return -3;

    unsigned short SubId = 0;
    if (get_subtarget(SubId) != 0)   // own vtable +0x78
        return -3;
    if (SubId == SUBOBJ_ID_NONE)
        return -1;

    IObjRW* TargetRW = Ship->get_target();
    if (TargetRW == nullptr)
        return -5;

    // The target's own get_sub_obj_center_of_mass (vtable +0x18).
    return TargetRW->get_sub_obj_center_of_mass(SubId, OutCenter);
}

Vector IObjInspectImpl::get_center_of_mass() const
{
    // FUN_062c0360, a tail call to vtable +0x68 (CObject vtable 0639b66c). No null check.
    return Object->get_center_of_mass();
}

int IObjInspectImpl::get_sub_obj_center_of_mass(unsigned short SubId, Vector& OutCenter) const
{
    // FUN_062c0380. Forwards to the CEqObj virtual (vtable +0xe0).
    if (Object != nullptr && (Object->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
    {
        return static_cast<CEqObj*>(Object)->get_sub_obj_center_of_mass(SubId, OutCenter)
               ? 0 : -2;
    }
    return -3;
}

bool IObjInspectImpl::is_targetable() const
{
    // FUN_062c03c0. The virtual is_targetable (vtable +0x8c; CEqObj overrides it).
    CSimple* S = InspectSimple(Object);
    if (S == nullptr)
        return false;
    return S->is_targetable();
}

int IObjInspectImpl::get_scanlist(ScanList const*& OutList, uint Type, bool bFlag) const
{
    // FUN_062c03e0. No null test on the CEqObj narrowing, so there is no -3 path. Scans is
    // scanner +0x24.
    OutList = nullptr;
    CEqObj* Eq = InspectEqObj(Object);
    CEquip* Equip = const_cast<CEquip*>(Eq->EquipManager.FindFirst(EquipTypeScanner));
    CEScanner* Scanner = (CEScanner*)Equip;
    if (Scanner == nullptr)
    {
        return -2;
    }
    if (!bFlag)
    {
        Scanner->clear_cache();
    }
    Scanner->scan_for_types(Type);
    OutList = &Scanner->Scans;
    return 0;
}

int IObjInspectImpl::get_hold_left(float& OutHold) const
{
    // FUN_062c0330
    CShip* Ship = InspectShip(Object);
    if (Ship != nullptr)
    {
        OutHold = (float)Ship->get_cargo_hold_remaining();
        return 0;
    }
    return -3;
}

CObject const* IObjInspectImpl::cobject() const
{
    // FUN_06263a70 (ICF fold), +0x10
    return Object;
}

// ============================================================================
// IObjInspectImpl — dock / formation
// ============================================================================

bool IObjInspectImpl::has_dock_hardpoints() const
{
    // FUN_062c0870. No null test on the archetype. Dock types 1 (berth), 6 (ring/pad) and
    // 7 (jump) count.
    if (Object == nullptr || (Object->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
        return false;

    std::vector<Archetype::EqObj::DockingSphere> const& Spheres =
        static_cast<Archetype::EqObj const*>(Object->Archetype)->DockingSpheres;

    for (std::vector<Archetype::EqObj::DockingSphere>::const_iterator It = Spheres.begin();
         It != Spheres.end(); ++It)
    {
        unsigned const Type = It->Type;
        if (Type == TERM_BERTH || Type == TERM_RING || Type == TERM_JUMP)
            return true;
    }
    return false;
}

int IObjInspectImpl::get_dock_hardpoints(int Index, TERMINAL_TYPE* Type, Transform* XformA,
                                          Transform* XformB, Transform* XformC,
                                          float* Distance) const
{
    // FUN_062c08c0
    if (Object == nullptr || (Object->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
        return -5;

    std::vector<Archetype::EqObj::DockingSphere> const& Spheres =
        static_cast<Archetype::EqObj const*>(Object->Archetype)->DockingSpheres;
    if (Index == -1 || Spheres.data() == nullptr || static_cast<unsigned>(Index) >= Spheres.size())
        return -4;
    Archetype::EqObj::DockingSphere const& Sphere = Spheres[static_cast<unsigned>(Index)];

    if (Type != nullptr)
        *Type = static_cast<TERMINAL_TYPE>(Sphere.Type);
    if (Distance != nullptr)
        *Distance = Sphere.Radius;

    // XformA from the mount hardpoint (get_hardpoint, vtable +0xc0). No null test on XformA.
    char const* const MountName = Sphere.HardpointName.c_str();
    if (get_hardpoint(MountName, &XformA->Translation, &XformA->Rotation) != 0)
    {
        FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", "E:\\FL\\Scratch\\Source\\Common\\IObjImpl.cpp", 0x4f4,
              "station hardpoint in file must be misnamed");
        return -5;
    }

    if (std::strstr(MountName, "hpdockmount") == nullptr)
    {
        FDUMP(kFdumpWarning, "%s(%d) : WARNING:General:%s", "E:\\FL\\Scratch\\Source\\Common\\IObjImpl.cpp", 0x4fc,
              "station hardpoint in file must be misnamed");
        return -5;
    }

    // The mount's last character picks "hpdockpoint<c>01" / "<c>02"; a missing point copies
    // XformA and makes the result -2.
    char const Suffix = MountName[std::strlen(MountName) - 1];
    char PointName[0x40];
    int Result = 0;
    std::sprintf(PointName, "%s%c01", "hpdockpoint", Suffix);
    if (get_hardpoint(PointName, &XformB->Translation, &XformB->Rotation) != 0)
    {
        *XformB = *XformA;
        Result = -2;
    }
    std::sprintf(PointName, "%s%c02", "hpdockpoint", Suffix);
    if (get_hardpoint(PointName, &XformC->Translation, &XformC->Rotation) != 0)
    {
        Result = -2;
        *XformC = *XformA;
    }
    return Result;
}

int IObjInspectImpl::enumerate_subtargets(SubtargetEnumerator* Enum) const
{
    // FUN_062c0aa0. Two independent walks: equipment (mask 0x1fe0, callback slot +0x00), then
    // collision groups (slot +0x04) even if the first stopped early. Enum is not tested.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq == nullptr)
        return -3;

    CEquipTraverser Walk(EquipTypeMineDropper | EquipTypeCmDropper | EquipTypeGun |
                         EquipTypeShield | EquipTypeShieldGen | EquipTypeThruster |
                         EquipTypeCargoPod | EquipTypeCloak);
    CEquip* EqItem = Eq->EquipManager.Traverse(Walk);
    while (EqItem != nullptr)
    {
        if (!Enum->enumerate_equip(EqItem->Id, EqItem->Type))
            break;
        EqItem = Eq->EquipManager.Traverse(Walk);
    }

    CArchGrpTraverser GroupWalk;
    CArchGroup* Group = Eq->ArchGroups.Traverse(GroupWalk);
    while (Group != nullptr)
    {
        if (!Enum->enumerate_subpart(Group->GetID(), Group->GetCollisionGroupType()))
            break;
        Group = Eq->ArchGroups.Traverse(GroupWalk);
    }

    return 0;
}

int IObjInspectImpl::enumerate_cargo(CargoEnumerator* Enum) const
{
    // FUN_062c0b70. Enum is not tested; the 0x10000 traverser yields cargo only, so Count
    // (+0x28) is read without a cast check. The id is passed by reference to a copy.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq == nullptr)
        return -3;

    CEquipTraverser Walk(EquipTypeCargo);
    CEquip* E = Eq->EquipManager.Traverse(Walk);
    while (E != nullptr)
    {
        unsigned short const Id = E->Id;
        if (!Enum->enumerate(Id, E->Arch, static_cast<int>(static_cast<CECargo*>(E)->Count)))
            break;
        E = Eq->EquipManager.Traverse(Walk);
    }
    return 0;
}

int IObjInspectImpl::scan_cargo(IObjRW* Target, EquipDescVector& OutCargo) const
{
    // FUN_062c0c00. Target is the scanned object; once ScanCargo completes, OutCargo receives
    // a copy of this ship's scanner results (CEScanner +0x82c). No null test on Target.
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq == nullptr)
        return -3;

    CEquip* Equip = const_cast<CEquip*>(Eq->EquipManager.FindFirst(EquipTypeScanner));
    CEScanner* Sc = reinterpret_cast<CEScanner*>(Equip);

    CObject const* TargetObj = Target->cobject();
    CEqObj* TargetEq = InspectEqObj(TargetObj);

    if (Sc == nullptr || TargetEq == nullptr)
        return -3;

    CEScanner::ScanResult Res = Sc->ScanCargo(TargetEq);
    if (Res != CEScanner::ScanComplete)
        return -3;

    // A non-owning view of the scanner's buffer, so operator= does retail's inlined deep copy.
    // The view drops the pointers before ~EquipDescVector (which frees Begin) runs.
    EquipDescVector ScannerCargoView;
    ScannerCargoView.Begin = Sc->CargoVecBegin;
    ScannerCargoView.End = Sc->CargoVecEnd;
    ScannerCargoView.Cap = Sc->CargoVecCapacityEnd;
    OutCargo = ScannerCargoView;
    ScannerCargoView.Begin = nullptr;
    ScannerCargoView.End = nullptr;
    ScannerCargoView.Cap = nullptr;
    return 0;
}

int IObjInspectImpl::get_formation_offset(Vector& OutOffset) const
{
    // FUN_062c02b0 (+0x1c0)
    CShip* Sh = InspectShip(Object);
    if (Sh == nullptr)
        return -3;
    OutOffset = Sh->FormationOffset;
    return 0;
}

int IObjInspectImpl::get_formation_leader(IObjRW*& OutLeader) const
{
    // FUN_062c02f0 (+0x1b8 watcher)
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutLeader = Sh->FormationLeader.get();
        return 0;
    }
    return -3;
}

int IObjInspectImpl::get_follow_offset(Vector& OutOffset) const
{
    // FUN_062c10c0. Returns 0 on both paths; OutOffset is untouched without a ship.
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutOffset = Sh->get_follow_offset();
    }
    return 0;
}

int IObjInspectImpl::get_follow_leader(IObjRW*& OutLeader) const
{
    // FUN_062c1100. Returns 0 on both paths, as get_follow_offset.
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        OutLeader = Sh->get_follow_leader();
    }
    return 0;
}

bool IObjInspectImpl::generate_follow_offset(IObjInspect const* Leader, Transform& OutXform) const
{
    // FUN_062c1130
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->generate_follow_offset(Leader, OutXform);
    }
    return false;
}

int IObjInspectImpl::get_behavior_id() const
{
    // FUN_062c1150. The manager's CurrentBehaviorIndex (+0xc0) when there is one, else the
    // CEqObj's own BehaviorIndex (+0x1a0).
    CEqObj* Eq = InspectEqObj(Object);
    if (Eq != nullptr) {
        IBehaviorManager* Mgr = Eq->get_behavior_interface();
        if (Mgr != nullptr) {
            return Mgr->CurrentBehaviorIndex;
        }
        return Eq->BehaviorIndex;
    }
    return -1;
}

int IObjInspectImpl::get_formation_followers(IObjRW** const OutFollowers, uint MaxCount) const
{
    // FUN_062c1190
    CShip* Sh = InspectShip(Object);
    if (Sh != nullptr) {
        return Sh->get_formation_followers(OutFollowers, MaxCount);
    }
    return 0;
}

int IObjInspectImpl::get_atmosphere_range(float& OutRange) const
{
    // FUN_062c11b0 (CSolar +0x1ac)
    if (Object != nullptr && (Object->ObjectClass & CObject::ClassSolar) == CObject::ClassSolar) {
        OutRange = static_cast<CSolar const*>(Object)->AtmosphereRange;
        return 0;
    }
    return -3;
}

int IObjInspectImpl::is_using_tradelane(bool* OutUsing) const
{
    // FUN_062c1040. Always writes, with no null check, and returns 0.
    CShip* Sh = InspectShip(Object);
    *OutUsing = (Sh != nullptr) ? Sh->is_using_tradelane() : false;
    return 0;
}

namespace
{
    // FUN_062c2430 (cdecl), called only by get_lane_start. Self is the asking object, Ship the
    // lane-entering one; InOutLane, when set, is the ring already chosen.
    int ResolveLaneStart(IObjInspect const* Self, IObjInspect const* Ship, IObjInspect const*& InOutLane,
                         Vector& OutStart, Vector& OutDir, IObjDB* ObjectDb)
    {
        if (Self == nullptr || Ship == nullptr)
            return -4;

        // Retail calls get_orientation (+0x10) and drops the result.
        (void)Ship->get_orientation();

        // get_lane_direction is +0x128: from the given ring against the ship's frame, else from
        // the ship against Self's frame.
        bool bForward = false;
        int Status;
        if (InOutLane != nullptr)
            Status = InOutLane->get_lane_direction(Ship->get_transform(), &bForward);
        else
            Status = Ship->get_lane_direction(Self->get_transform(), &bForward);
        if (Status != 0)
            return -4;

        // traverse_rings (+0x130), ObjectDb FindById (+0x04); none -> the other way, once.
        unsigned RingId = 0;
        Ship->traverse_rings(RingId, bForward);
        InOutLane = ObjectDb->FindById(&RingId);
        if (InOutLane == nullptr)
        {
            bForward = !bForward;
            Ship->traverse_rings(RingId, bForward);
            InOutLane = ObjectDb->FindById(&RingId);
        }

        Vector HardpointPos;
        Matrix HardpointOrient;
        if (Ship->get_hardpoint(bForward ? "HpRightLane" : "HpLeftLane", &HardpointPos, &HardpointOrient) != 0)
            return -5;

        // OutDir is VecNormalize3 (FUN_062e1680) of the float differences.
        OutStart = HardpointPos;
        Vector const& ShipPos = Ship->get_position();
        Vector const& LanePos = InOutLane->get_position();
        Vector const Delta{ LanePos.x - ShipPos.x, LanePos.y - ShipPos.y, LanePos.z - ShipPos.z };
        Vector Dir;
        VecNormalize3(&Delta, &Dir);
        OutDir = Dir;
        return 0;
    }
}

int IObjInspectImpl::get_lane_start(IObjInspect const* Self, IObjInspect const*& OutLane,
                                     Vector& OutStart, Vector& OutEnd) const
{
    // FUN_062c1080. ObjectDb (+0xac) is read through the CSimple narrowing with no null test.
    CSimple const* Simple = nullptr;
    if (Object != nullptr && (static_cast<unsigned>(Object->ObjectClass) & 3) == 3)
        Simple = static_cast<CSimple const*>(Object);
    return ResolveLaneStart(this, Self, OutLane, OutStart, OutEnd, Simple->ObjectDb);
}

int IObjInspectImpl::get_lane_direction(Transform const& Xform, bool* OutDir) const
{
    // FUN_062c0e30. Lane rings only. The ring centre is the physical-radius centre (slot
    // +0x2c), or the position when that fails. Float differences, column 2 of the ring's
    // transform; NaN writes false. No null test on OutDir.
    if (Object == nullptr || (Object->ObjectClass & CObject::ClassSolar) != CObject::ClassSolar)
        return -4;
    if (!static_cast<CSolar const*>(Object)->is_lane_ring())
        return -4;

    float Radius;
    Vector Center;
    if (get_physical_radius(Radius, Center) != 0)
        Center = get_position();

    float const Dx = Center.x - Xform.Translation.x;
    float const Dy = Center.y - Xform.Translation.y;
    float const Dz = Center.z - Xform.Translation.z;
    Matrix const& Rot = get_transform().Rotation;
    double const Dot = (static_cast<double>(Dz) * Rot.E[2][2] + static_cast<double>(Rot.E[1][2]) * Dy) +
                       static_cast<double>(Rot.E[0][2]) * Dx;
    *OutDir = (Dot < 0.0);
    return 0;
}

int IObjInspectImpl::get_ring_side(Transform const& Xform, bool* OutSide) const
{
    // FUN_062c0f20. As get_lane_direction, but the differences stay unrounded and the sign
    // test is "greater than". No null test on OutSide.
    if (Object == nullptr || (Object->ObjectClass & CObject::ClassSolar) != CObject::ClassSolar)
        return -4;
    if (!static_cast<CSolar const*>(Object)->is_lane_ring())
        return -4;

    Matrix const& Rot = get_transform().Rotation;
    float const M02 = Rot.E[0][2];
    float const M12 = Rot.E[1][2];
    float const M22 = Rot.E[2][2];
    Vector const& Pos = get_position();
    double const Dx = static_cast<double>(Xform.Translation.x) - Pos.x;
    double const Dy = static_cast<double>(Xform.Translation.y) - Pos.y;
    double const Dz = static_cast<double>(Xform.Translation.z) - Pos.z;
    double const Dot = (M22 * Dz + Dy * M12) + Dx * M02;
    *OutSide = (Dot > 0.0);
    return 0;
}

int IObjInspectImpl::traverse_rings(uint& OutRing, bool bForward) const
{
    // FUN_062c0fe0. Lane rings only. The top two bits of the ring id are masked off before the
    // zero test.
    if (Object == nullptr || (Object->ObjectClass & CObject::ClassSolar) != CObject::ClassSolar)
        return -4;

    CSolar const* Solar = static_cast<CSolar const*>(Object);
    if (!Solar->is_lane_ring())
        return -4;

    OutRing = bForward ? Solar->NextRing : Solar->PrevRing;
    return ((OutRing & 0x3fffffff) != 0) ? 0 : -2;
}

bool IObjInspectImpl::is_dying() const
{
    // FUN_0629ba00 (ICF fold)
    return false;
}

// ===========================================================================
// Retail Common.dll exports all 47 members of its VC6 std::vector<IObjInspect*>; the port
// exports the modern instantiation. The importing declaration is in common_iface.h.
// ===========================================================================
template class COMMON_API std::vector<IObjInspect*>;

// ---------------------------------------------------------------------------
// InitEquipStatusBits -- FUN_062e8270, EquipStatus's out-of-line default constructor: clears
// the low 3 bits of the flag byte and the low 29 bits of the next dword. Inlined in
// HasReadyWeaponSlot and SetWatcherActive, where the port zero-initialises instead. No port
// caller: its one retail caller, FUN_062e7a30 (SystemManagerTimerTable::FindClosestObstacleToAvoid,
// common_obstacleavoidance.cpp), does not call it in the port. A free function, not a ctor, so
// those zero-init sites stay unchanged.
// ---------------------------------------------------------------------------
void InitEquipStatusBits(EquipStatus* Status)
{
    Status->Flags = Status->Flags & 0xf8;
    Status->EquipFlags &= 0xe0000000u;
}
