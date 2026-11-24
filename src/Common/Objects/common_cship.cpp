// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CShip (retail CShip.cpp), FAnimScriptRef and the CShip-only helpers.
#include "common_cobject.h"
#include "Objects/common_cobject_internal.h" // CmnCobject::RotateWorldToObjectSpace, GetObjectDbSystemId
#include "x86math.h"
#include "x87_trig.h"   // retail's inline FSIN / FCOS (CShip::generate_follow_offset)

#include "common_cobjdb.h"
#include "common_goods.h"
#include "common_physys.h"
#include "common_spatial.h" // ISpatialPartition, FSpatialQueryResult (CShip::update)
#include "common_net.h"
#include "common_animdb.h"  // AnimDB::Add/Rem (common_animdb.cpp)
#include "common_iface.h"
#include "common_universe.h"

#include "DALib.h"

#include <cmath>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using CmnCobject::GetObjectDbSystemId;
using CmnCobject::RotateWorldToObjectSpace;

namespace
{
    const char kCShipSource[] = "E:\\FL\\Scratch\\Source\\Common\\CShip.cpp";

    // 0x1ff0000: CInternalEquip's family (EquipTypeCargo..EquipTypeArmor).
    const int kInternalEquipMask = EquipTypeCargo | EquipTypeEngine | EquipTypePower |
                                   EquipTypeScanner | EquipTypeTractor | EquipTypeRepairDroid |
                                   EquipTypeInternalFX | EquipTypeTradeLane | EquipTypeArmor;
    static_assert(kInternalEquipMask == 0x1ff0000, "the 0x1ff0000 traverser mask");
}


CShip::CShip()
    : CEqObj(ClassShip)
    , PhySys::Controller()
{
    // FUN_062b0670. Controller is +0x1a4; FormationLeader (+0x1b8), FollowLeader (+0x1e0) and
    // Target (+0x220) default-construct to zero.
    GroupId = 0;
    FormationSpeed = 0.0f;
    bJumpedOut = 0;
    AtmosphereSolar = nullptr;
    // Deviation: retail leaves +0x21c unwritten.
    GroupNameId = 0;
    SubTargetIndex = SUBOBJ_ID_NONE;
    FormationOffset.x = 0.0f;
    FormationOffset.y = 0.0f;
    FormationOffset.z = 0.0f;
    FollowOffset.x = 0.0f;
    FollowOffset.y = 0.0f;
    FollowOffset.z = 0.0f;
    Steering.SetOwner(this);          // CNudgeEngine::SetOwner by ICF
    NudgeEngine.SetOwner(this);
    StrafeEngine.SetOwner(this);      // inlined in retail: +0x294 = this
    Throttle = 0.0f;                  // +0x29c..+0x2b8
    ThrustPower = 0.0f;
    MaxThrustPower = 0.0f;
    BayDoorCounter = 0;
    BayState = BayClosed;
    TradelaneSpeed = 0.0f;
    bUsingTradelane = 0;
    bIsPlayer = 0;
    AngularDragFactor = 1.0f;
    // GunStats (+0x2c8) is zeroed by its own member ctor.
    GunStatsDirty = 1;

    // Retail reserves inline for the two IObjRW-watcher vectors and through FUN_062b5100 for
    // the CSimple-watcher vector.
    FormationFollowers.reserve(16);
    FollowFollowers.reserve(16);
    Targeters.reserve(16);
}

FAnimScriptRef::~FAnimScriptRef()
{
    // FUN_0629abd0; inlined in ~CShip.
    Release();
}

void FAnimScriptRef::Release()
{
    if (Index != -1)
    {
        DALib::Anim->StopPlayback(Index);                          // slot 0x38
        DALib::Anim->DestroyScript((void*)Index);                  // slot 0x28
        Index = -1;
    }
}

// FUN_06287bb0. No callers in Common.dll, Server.dll or freelancer.exe (exported only because
// the class is). Deviation: retail inlines CSimple's, CEqObj's and PhySys::Controller's copy
// field by field; the port delegates to those copy ctors, then copies the CShip fields. The
// watcher vectors are copied exact-size with the TWatcher copy ctor, as std::vector does.
CShip::CShip(CShip const& Other)
    : CEqObj(Other)
    , PhySys::Controller(Other)
    // The CSteering/CNudgeEngine/CStrafeEngine copy ctors match retail, including not
    // retargeting OwnerShip to `this`. ActionDB's implicit copy is retail's two-dword copy.
    // FormationLeader re-watches the other ship's leader (null when W is 0 or 8).
    , FormationLeader(Other.FormationLeader)
    , FormationFollowers(Other.FormationFollowers)
    , FollowLeader(Other.FollowLeader)
    , FollowFollowers(Other.FollowFollowers)
    , Targeters(Other.Targeters)
    , Target(Other.Target)
    , BayAnim(Other.BayAnim)
    , Steering(Other.Steering)
    , NudgeEngine(Other.NudgeEngine)
    , StrafeEngine(Other.StrafeEngine)
    , Actions(Other.Actions)
{
    GroupId = Other.GroupId;
    FormationOffset = Other.FormationOffset;
    FormationSpeed = Other.FormationSpeed;
    FollowOffset = Other.FollowOffset;
    bJumpedOut = Other.bJumpedOut;
    AtmosphereSolar = Other.AtmosphereSolar;
    GroupNameId = Other.GroupNameId;
    SubTargetIndex = Other.SubTargetIndex;

    Throttle = Other.Throttle;
    ThrustPower = Other.ThrustPower;
    MaxThrustPower = Other.MaxThrustPower;
    BayDoorCounter = Other.BayDoorCounter;
    BayState = Other.BayState;
    TradelaneSpeed = Other.TradelaneSpeed;
    bUsingTradelane = Other.bUsingTradelane;
    bIsPlayer = Other.bIsPlayer;
    AngularDragFactor = Other.AngularDragFactor;
    GunStatsDirty = Other.GunStatsDirty;
    GunStats = Other.GunStats;
}

CShip::~CShip()
{
    // FUN_062b09b0. flush_animations is inlined (called by name, no virtual dispatch); the rest
    // is member destruction in reverse declaration order.
    Actions.free();
    CShip::flush_animations();
}

void CShip::init_physics(Vector const& Velocity, Vector const& AngularVelocity)
{
    // FUN_062b2850. Default dampings kept (unlike CGuided); bEnableCollisions is written 1 then
    // 0; Name = _itoa(Id, radix 24); no null test on the archetype. The bytes retail leaves
    // unwritten are zero here.
    PhySys::CreateParms Phy;
    memset(&Phy, 0, sizeof(Phy));
    Phy.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
    Phy.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
    Phy.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
    Phy.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
    Phy.Type = PhySys::ANIM_TECHNIQUE_MERGED;
    _itoa((int)Id, Phy.Name, 0x18);
    Phy.Mass = Archetype->Mass;
    Phy.SystemId = GetObjectDbSystemId(ObjectDb);
    Phy.bEnableCollisions = 0;
    if (Archetype->bHasRotationInertia)
    {
        Phy.bHasInertia = 1;
        Phy.Inertia.x = Archetype->RotationInertiaX;
        Phy.Inertia.y = Archetype->RotationInertiaY;
        Phy.Inertia.z = Archetype->RotationInertiaZ;
    }

    PhySys::BuildIVP_Real(this, Phy);

    if (BodyPtr == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: CShip::init_physics(): Failed to create ivp_real for %s. Expect a crash.",
              "E:\\FL\\Scratch\\Source\\Common\\CShip.cpp", 0x5c7, Archetype->DaArchetype.Value);
        return;
    }

    PhySys::AddToVelocity(this, Velocity);
    PhySys::AddToAngularVelocityOS(this, RotateWorldToObjectSpace(Orientation, AngularVelocity));
}

void CShip::init(CreateParms const& Parms)
{
    // FUN_062b2690
    CEqObj::init(Parms);

    GroupNameId = Parms.GroupNameId;                                  // Parms +0xec
    bUsingTradelane = Parms.bUsingTradelane;                          // Parms +0xf4

    // The base this ship sits in learns the current system (IBase slot 0x58).
    Universe::IBase* const Base = Universe::get_base(DockingBaseId);
    if (Base != nullptr)
    {
        Universe::ISystem const* const System = ObjectDb->GetZoneDb();
        Base->SetSystem(System->GetId());
    }

    enable_controllers();                                             // vtable +0x60

    // Bay door animation (+0xdc; only its first byte is tested). No null test on Anim.
    Archetype::Ship const* const ShipArch = shiparch();
    if (ShipArch->BayDoorAnim.Value[0] != '\0')
    {
        unsigned long const PlayId = DALib::Anim->PlayScript(AsOpaqueDword(Archetype->AnimIndex),
                                                             AsOpaqueDword(EngineInstanceId),
                                                             ShipArch->BayDoorAnim.Value,
                                                             nullptr, nullptr);
        BayAnim.Index = (int)PlayId;
    }

    // Guns that need no ammo start active. The walk ends at the first entry CEGun::cast
    // rejects; no null test on the projectile.
    CEquipTraverser GunWalk(EquipTypeGun);
    for (CEGun* Gun = CEGun::cast(EquipManager.Traverse(GunWalk)); Gun != nullptr;
         Gun = CEGun::cast(EquipManager.Traverse(GunWalk)))
    {
        Archetype::Projectile const* const Proj =
            (Archetype::Projectile const*)Gun->Projectile;
        if (!Proj->bRequiresAmmo)
            Gun->Activate(true);
    }

    // MaxThrustPower (+0x2a4) is not reset here; one float rounding per add.
    CEquipTraverser PowerWalk(EquipTypePower);
    for (CEquip* Power = EquipManager.Traverse(PowerWalk); Power != nullptr;
         Power = EquipManager.Traverse(PowerWalk))
    {
        float const Capacity = Power->IsDestroyed()
            ? 0.0f : ((Archetype::Power const*)Power->Arch)->ThrustCapacity;
        MaxThrustPower = (float)((double)Capacity + MaxThrustPower);
    }
    ThrustPower = MaxThrustPower;

    Steering.Activate(true);                                          // CNudgeEngine::Activate by ICF
    NudgeEngine.Activate(true);

    // +0x160 = Parms +0x5c.
    DockingBaseId = Parms.DockingBaseId;
    init_docking_points(Parms.DockingBaseId);

    Reputation::Vibe::SetRank(VibeId, Parms.Rank);                    // Parms +0xf0

    bIsPlayer = (OwnerPlayer != 0) ? 1 : 0;
}

CEquip* CShip::alloc_equip(unsigned short EquipId, Archetype::Equipment* Arch,
                           bool bMissionFlag)
{
    // FUN_062b0c40. Sizes: engine 0x58, thruster 0x64, tractor 0x60. No null test on Arch;
    // retail also falls back to CEqObj::alloc_equip on a null allocation.
    int const ClassType = (int)Arch->get_class_type();
    if (ClassType == Archetype::AClassEngine)
        return new CEEngine(this, EquipId, (Archetype::Engine const*)Arch, bMissionFlag);
    if (ClassType == Archetype::AClassThruster)
        return new CEThruster(this, EquipId, (Archetype::Thruster const*)Arch, bMissionFlag);
    if (ClassType == Archetype::AClassTractor)
        return new CETractor(this, EquipId, (Archetype::Tractor const*)Arch, bMissionFlag);
    return CEqObj::alloc_equip(EquipId, Arch, bMissionFlag);
}

bool CShip::is_cruise_active() const
{
    CEquip* Equip = const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeEngine));
    CEEngine* Engine = CEEngine::cast(Equip);
    if (Engine == nullptr)
        return false;
    return Engine->bCruiseActive != 0;
}

bool CShip::is_using_tradelane() const
{
    return bUsingTradelane != 0;
}

unsigned CShip::get_name() const
{
    // FUN_062b5f70 (ICF-shared with CSimple). No null test.
    return (unsigned)Archetype->IdsName;
}

bool CShip::any_thrusters_on() const
{
    // FUN_062b1660
    CEquipTraverser Walk(EquipTypeThruster);
    CEquip* Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
    while (Equip != nullptr)
    {
        if (Equip->IsActive())
            return true;
        Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
    }
    return false;
}

bool CShip::get_tgt_lead_fire_pos(Vector& OutPos) const
{
    // FUN_062b1480. Target is +0x220.
    IObjRW* const TargetObj = Target.get();
    if (TargetObj != nullptr)
    {
        IObjInspect const* const TargetInspect = TargetObj;
        return get_tgt_lead_fire_pos(TargetInspect, OutPos);
    }
    return get_tgt_lead_fire_pos((IObjInspect const*)nullptr, OutPos);
}

unsigned short CShip::get_sub_target() const
{
    // FUN_062b2300 (+0x228)
    return SubTargetIndex;
}

IObjRW* CShip::get_target() const
{
    // FUN_062b23c0
    return Target.get();
}

void CShip::set_target(IObjRW* NewTarget)
{
    // FUN_062b23e0. Old and new target ships are told about this targeter.
    if (Target.get() == NewTarget)
        return;

    IObjRW* const OldTarget = Target.get();
    if (OldTarget != nullptr)
    {
        CObject* const OldObj = const_cast<CObject*>(OldTarget->cobject()); // slot +0x150
        if (OldObj != nullptr && (OldObj->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
            ((CShip*)OldObj)->notify_set_target(this, false);
    }

    if (NewTarget != nullptr)
    {
        CObject* const NewObj = const_cast<CObject*>(NewTarget->cobject());
        if (NewObj != nullptr && (NewObj->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
            ((CShip*)NewObj)->notify_set_target(this, true);
    }

    Target.watch(NewTarget);
}

Vector CShip::get_nudge_vec() const
{
    // FUN_062b1700 (NudgeEngine +0x258, vector at +0x274)
    return NudgeEngine.GetNudgeVec();
}

StrafeDir CShip::get_strafe_dir() const
{
    // FUN_062b1740 (StrafeEngine +0x280, dword at +0x298)
    return (StrafeDir)StrafeEngine.GetStrafe();
}

ShipGunStats const& CShip::get_gun_stats() const
{
    // FUN_062b14f0. Rebuilds the cache when GunStatsDirty (+0x2c4) is set.
    if (GunStatsDirty != 0)
    {
        GunStatsDirty = 0;
        GunStats.ActiveGunCount = 0;
        GunStats.AvgMuzzleVelocity = 0.0f;
        GunStats.MaxMunitionRange = 0.0f;
        GunStats.ActiveMissileCount = 0;
        GunStats.MaxActiveMissileRange = 0.0f;

        bool const bPlayerShip = bIsPlayer != 0;
        CEquipTraverser Walk(EquipTypeGun);
        CEquip* Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
        while (Equip != nullptr)
        {
            // AI ships skip their auto-turret guns.
            if (Equip->IsFunctioning())
            {
                Archetype::Gun const* GunArch = (Archetype::Gun const*)Equip->EquipArch();
                if (bPlayerShip || !GunArch->bAutoTurret)
                {
                    CEGun* const Gun = (CEGun*)Equip;
                    // The munition's Seeker (+0x90) tells missiles from guns.
                    if (Gun->MunitionArch()->Seeker == 0)
                    {
                        ++GunStats.ActiveGunCount;
                        float const Range = Gun->GetMunitionRange();
                        if (GunStats.MaxMunitionRange < Range)
                            GunStats.MaxMunitionRange = Range;
                        GunStats.AvgMuzzleVelocity += GunArch->MuzzleVelocity;
                    }
                    else
                    {
                        ++GunStats.ActiveMissileCount;
                        float const Range = Gun->GetMunitionRange();
                        if (GunStats.MaxActiveMissileRange < Range)
                            GunStats.MaxActiveMissileRange = Range;
                    }
                }
            }
            Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
        }

        if (GunStats.ActiveGunCount != 0)
            GunStats.AvgMuzzleVelocity /= (float)(GunStats.ActiveGunCount);
        else
            GunStats.AvgMuzzleVelocity = 0.0f;
    }

    return GunStats;
}

bool CShip::get_farthest_active_gun_range(float& OutRange) const
{
    // FUN_062b14b0. Guns first, then missiles.
    ShipGunStats const& Stats = get_gun_stats();
    if (Stats.ActiveGunCount != 0)
    {
        OutRange = Stats.MaxMunitionRange;
        return true;
    }
    if (Stats.ActiveMissileCount != 0)
    {
        OutRange = Stats.MaxActiveMissileRange;
        return true;
    }
    OutRange = 0.0f;
    return false;
}

double CShip::get_cargo_hold_remaining() const
{
    // FUN_062b3040. Cargo counts are widened as unsigned; one float rounding
    // per add. HoldSize (+0x100, no null test); over-full or unordered gives 0, otherwise the
    // remainder unrounded in ST0.
    float Used = 0.0f;
    CEquipTraverser Walk(kInternalEquipMask);
    for (CEquip* Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk); Equip != nullptr;
         Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk))
    {
        if (Equip->Type == EquipTypeCargo)
            Used = (float)((double)(((CECargo const*)Equip)->Count) *
                                      Equip->Arch->Volume + Used);
        else
            Used = (float)((double)(Used) + Equip->Arch->Volume);
    }

    float const Hold = shiparch()->HoldSize;
    if (Used <= Hold)
        return (double)Hold - Used;
    return 0.0f;
}

int CShip::get_repair_kit_capacity_remaining() const
{
    // FUN_062b30f0. NanobotLimit +0x140 (no null test); -1 means unlimited. Only the first
    // repair-kit stack counts.
    Archetype::Ship const* const ShipArch = shiparch();
    int const Limit = ShipArch->NanobotLimit;
    if (Limit == -1)
        return Limit;

    CEquipTraverser Walk(EquipTypeCargo);
    CEquip* Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
    while (Equip != nullptr)
    {
        if (Equip->Arch->get_class_type() == Archetype::AClassRepairKit)
        {
            int const Count = (int)((CECargo*)Equip)->Count;
            if (Count > Limit)
                return 0;
            return Limit - Count;
        }
        Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
    }
    return Limit;
}

int CShip::get_shield_battery_capacity_remaining() const
{
    // FUN_062b3180. As get_repair_kit_capacity_remaining with ShieldBatteryLimit (+0x144).
    Archetype::Ship const* const ShipArch = shiparch();
    int const Limit = ShipArch->ShieldBatteryLimit;
    if (Limit == -1)
        return Limit;

    CEquipTraverser Walk(EquipTypeCargo);
    CEquip* Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
    while (Equip != nullptr)
    {
        if (Equip->Arch->get_class_type() == Archetype::AClassShieldBattery)
        {
            int const Count = (int)((CECargo*)Equip)->Count;
            if (Count > Limit)
                return 0;
            return Limit - Count;
        }
        Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk);
    }
    return Limit;
}

int CShip::get_ammo_capacity_remaining(Archetype::Equipment const* Arch) const
{
    // FUN_062b3210
    return get_ammo_capacity_remaining(Arch->get_id());
}

int CShip::get_ammo_capacity_remaining(unsigned EquipArchId) const
{
    // FUN_062b3220. The limit is MAX_PLAYER_AMMO (DAT_063ed1d4, read from the constants INI).
    // Only the first cargo stack with that id counts; a non-ammo class gives -1.
    int const Limit = MAX_PLAYER_AMMO;
    if (EquipArchId == 0)
        return -1;

    CEquipTraverser Walk(EquipTypeCargo);
    for (CEquip* Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk); Equip != nullptr;
         Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk))
    {
        if (Equip->Arch->Nickname.Id != EquipArchId)
            continue;

        Archetype::AClassType const ClassType = Equip->Arch->get_class_type();
        if (ClassType != Archetype::AClassMunition &&
            ClassType != Archetype::AClassMine &&
            ClassType != Archetype::AClassCounterMeasure)
            return -1;

        int const Count = (int)((CECargo const*)Equip)->Count;
        if (Count > Limit)
            return 0;
        return Limit - Count;
    }
    return Limit;
}

int CShip::get_space_for_cargo_type(Archetype::Equipment const* CargoArch) const
{
    // FUN_062b32e0. No null test on CargoArch or the archetype. A non-positive or unordered
    // Volume skips the hold step; the class limits still apply.
    int SpaceForType = 0x7fffffff;
    float const ItemVolume = CargoArch->Volume;
    if (ItemVolume > 0.0f)
    {
        // As get_cargo_hold_remaining, inlined.
        float Used = 0.0f;
        CEquipTraverser Walk(kInternalEquipMask);
        for (CEquip* Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk); Equip != nullptr;
             Equip = const_cast<CEquipManager&>(EquipManager).Traverse(Walk))
        {
            if (Equip->Type == EquipTypeCargo)
                Used = (float)((double)(((CECargo const*)Equip)->Count) *
                                          Equip->Arch->Volume + Used);
            else
                Used = (float)((double)(Used) + Equip->Arch->Volume);
        }

        double const Hold = shiparch()->HoldSize;
        double const Remaining = (Used <= Hold) ? (Hold - Used) : 0.0;
        SpaceForType = (int)std::floor(Remaining / ItemVolume);
    }

    // The smaller of the hold space and the class limit; retail recomputes the limit for the
    // return.
    switch (CargoArch->get_class_type())
    {
    case Archetype::AClassRepairKit:
    {
        int const KitRemaining = get_repair_kit_capacity_remaining();
        if (SpaceForType < KitRemaining)
            return SpaceForType;
        return get_repair_kit_capacity_remaining();
    }
    case Archetype::AClassShieldBattery:
    {
        int const BatteryRemaining = get_shield_battery_capacity_remaining();
        if (SpaceForType < BatteryRemaining)
            return SpaceForType;
        return get_shield_battery_capacity_remaining();
    }
    case Archetype::AClassMunition:
    case Archetype::AClassMine:
    case Archetype::AClassCounterMeasure:
        if (SpaceForType >= get_ammo_capacity_remaining(CargoArch->Nickname.Id))
            SpaceForType = get_ammo_capacity_remaining(CargoArch->Nickname.Id);
        return SpaceForType;
    default:
        return SpaceForType;
    }
}

wchar_t const* CShip::get_pilot_name() const
{
    // FUN_062b3630. GetName's result is ignored; null when the lookup fails.
    wchar_t const* WideName = nullptr;
    FmtStr Fmt1;
    FmtStr Fmt2;
    Reputation::Vibe::GetName(this->VibeId, Fmt1, Fmt2, WideName);
    return WideName;
}

bool CShip::is_launching() const
{
    // FUN_062b1a00
    return Actions.find("Launch") != nullptr;
}

bool CShip::is_jumping() const
{
    // FUN_062b1a20
    return Actions.find("JumpIn") != nullptr;
}

bool CShip::is_enemy(IObjInspect* OtherInspect)
{
    // OtherInspect is not null-tested (freelancer.exe 00544a70 filters nulls).
    CObject const* const Delegate = OtherInspect->cobject();
    if (Delegate == nullptr)
        return false;
    if ((Delegate->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
        return false;

    // Directional: the other's attitude towards this ship.
    int const ThisVibeId = VibeId;
    int const OtherVibeId = ((CEqObj const*)Delegate)->VibeId;
    float Attitude;
    int const Result = Reputation::Vibe::AttitudeTowards(OtherVibeId, ThisVibeId, Attitude);
    if (Result != 0)
        return false;

    // Unordered is not an enemy.
    return Attitude <= -0.6f;
}

unsigned CShip::get_group_name() const
{
    // FUN_062b2640
    if (GroupNameId != 0)
        return GroupNameId;
    unsigned Affiliation = 0;
    int Result = Reputation::Vibe::GetAffiliation(VibeId, Affiliation, false);
    if (Result == 0)
        return Reputation::get_name(Affiliation);
    return 0;
}

int CShip::get_targeted_enemy_count()
{
    // FUN_062b24a0
    int Count = 0;
    for (std::vector<FSimpleWatcher>::const_iterator It = Targeters.begin(); It != Targeters.end(); ++It)
    {
        if (It->get() != nullptr)
            ++Count;
    }
    return Count;
}

IObjRW* CShip::get_follow_leader()
{
    // FUN_062b4ee0
    return FollowLeader.get();
}

int CShip::get_formation_follower_count()
{
    // FUN_062b4a90
    int Count = 0;
    for (std::vector<FObjRWWatcher>::const_iterator It = FormationFollowers.begin();
         It != FormationFollowers.end(); ++It)
    {
        if (It->get() != nullptr)
            ++Count;
    }
    return Count;
}

int CShip::get_formation_followers(IObjRW** const OutFollowers, unsigned int MaxCount)
{
    // FUN_062b4ac0. Counts every live follower but stores only the first MaxCount. No null
    // test on OutFollowers.
    unsigned Count = 0;
    for (std::vector<FObjRWWatcher>::const_iterator It = FormationFollowers.begin();
         It != FormationFollowers.end(); ++It)
    {
        IObjRW* const Follower = It->get();
        if (Follower != nullptr)
        {
            if (Count < MaxCount)
                OutFollowers[Count] = Follower;
            ++Count;
        }
    }
    return (int)Count;
}

FORMATION_RTYPE CShip::add_formation_follower(IObjRW* Follower)
{
    // FUN_062b4810. Only a ship that is its own formation leader (+0x1b8 watcher, compared by
    // get_id, vtable +0x20) accepts followers.
    IObjRW* const Leader = FormationLeader.get();
    if (Leader == nullptr)
    {
        return FORMATION_RTYPE_NOT_LEADER;
    }
    if (Leader->get_id() != Id)
    {
        return FORMATION_RTYPE_NOT_LEADER;
    }

    // The first cleared slot takes the follower; a live slot with the same get_id() means it
    // is already there. A null Follower skips the walk and is appended as an empty watcher.
    bool bPlaced = false;
    if (Follower != nullptr)
    {
        for (std::vector<FObjRWWatcher>::iterator It = FormationFollowers.begin();
             It != FormationFollowers.end(); ++It)
        {
            IObjRW* const Existing = It->get();
            if (Existing == nullptr)
            {
                It->watch(Follower);
                bPlaced = true;
                break;
            }
            if (Existing->get_id() == Follower->get_id())
            {
                bPlaced = true;
                break;
            }
        }
    }

    if (!bPlaced)
    {
        // Vector insert FUN_062b5200.
        FObjRWWatcher const Temp(Follower);
        FormationFollowers.push_back(Temp);
    }

    // Inlined in retail.
    recalculate_formation_speed();
    return FORMATION_RTYPE_OK;
}

FORMATION_RTYPE CShip::add_follow_follower(IObjRW* Follower)
{
    // FUN_062b4d10. No null test on Follower. Same slot reuse as add_formation_follower.
    if (Follower->get_id() == Id)
        return FORMATION_RTYPE_SELF;

    for (std::vector<FObjRWWatcher>::iterator It = FollowFollowers.begin();
         It != FollowFollowers.end(); ++It)
    {
        IObjRW* const Existing = It->get();
        if (Existing == nullptr)
        {
            It->watch(Follower);
            return FORMATION_RTYPE_OK;
        }
        if (Existing->get_id() == Follower->get_id())
            return FORMATION_RTYPE_OK;
    }

    FObjRWWatcher const Temp(Follower);
    FollowFollowers.push_back(Temp);
    return FORMATION_RTYPE_OK;
}

bool CShip::generate_follow_offset(IObjInspect const* Inspect, Transform& OutOffset) const
{
    // FUN_062b4f10
    if (Inspect == nullptr)
        return false;

    // Retail calls get_id() once up front and drops the result.
    (void)Inspect->get_id();

    // Index counts every slot, cleared ones included, from 1.
    int Index = 1;
    std::vector<FObjRWWatcher>::const_iterator It = FollowFollowers.begin();
    for (; It != FollowFollowers.end(); ++It)
    {
        IObjRW* const Existing = It->get();
        if (Existing != nullptr && Existing->get_id() == Inspect->get_id())
            break;
        ++Index;
    }
    if (It == FollowFollowers.end())
        return false;

    // Signed C division.
    int const Slot = Index - 1;
    int const Ring = Slot / 4 + 1;
    int const Quad = Slot % 4;

    // Each product rounded once.
    Vector Offset;
    Offset.x = (float)((double)(50.0f) * Ring);  // DAT_0639e6d4 = 50.0f
    Offset.y = (float)((double)(0.0f) * Ring);   // DAT_06399408 = 0.0f
    Offset.z = (float)(Ring * (double)(20.0f));  // DAT_0639b21c = 20.0f

    // A = Quad * pi (float, DAT_0639d208) * 0.5 (double, DAT_0639df88). x87 quirk: FCOS runs on
    // the unrounded A, FSIN on the rounded one.
    double const Angle = (double)(Quad) * (double)(3.1415927f) * 0.5;
    float const AngleRounded = (float)(Angle);
    float const Cos = X87CosF(Angle);
    float const Sin = X87SinF((double)(AngleRounded));
    Matrix Spin;
    Spin.E[0][0] = Cos;
    Spin.E[0][1] = -Sin;
    Spin.E[0][2] = 0.0f;
    Spin.E[1][0] = Sin;
    Spin.E[1][1] = Cos;
    Spin.E[1][2] = 0.0f;
    Spin.E[2][0] = 0.0f;
    Spin.E[2][1] = 0.0f;
    Spin.E[2][2] = 1.0f;

    Vector Rotated;
    Acquire3DMathEngine()->TransformMatrix((FVector&)Rotated, (FMatrix const&)Spin, (FVector const&)Offset);
    OutOffset.Translation = Rotated;

    // vtable +0x64; only the centre is used.
    Vector Center;
    (void)get_physical_radius_r(Center);

    Matrix const Orient = Orientation;
    Acquire3DMathEngine()->TransformMatrix((FVector&)Rotated, (FMatrix const&)Orient,
                                           (FVector const&)OutOffset.Translation);

    OutOffset.Translation.x = (float)((double)(Rotated.x) + Center.x);
    OutOffset.Translation.y = (float)((double)(Rotated.y) + Center.y);
    OutOffset.Translation.z = (float)((double)(Rotated.z) + Center.z);
    OutOffset.Rotation = Orient;
    return true;
}

unsigned CShip::get_tradelane_entrance_ring()
{
    // FUN_062b1810, tail jump to FUN_062c2320. Retail also rejects IAction == 0x10 (a null
    // LaneAction after the -0x10 adjustment).
    IAction* const Action = Actions.find("LaneAction");
    if (Action == nullptr || Action == (IAction*)0x10)
        return 0;
    LaneAction const* const Lane = static_cast<LaneAction const*>(Action);
    if (Lane->EntranceRing == nullptr)
        return 0;
    return Lane->EntranceRing->get_id();
}

unsigned CShip::get_tradelane_exit_ring()
{
    // FUN_062b1840, tail jump to FUN_062c2330: LaneAction +0x28 (LegStartXform.Ring), the ring
    // the current leg starts at, not ExitRing (+0x24). Retail: once under way this is the last
    // ring passed; before the first hop it is the entrance ring.
    IAction* const Action = Actions.find("LaneAction");
    if (Action == nullptr || Action == (IAction*)0x10)
        return 0;
    LaneAction const* const Lane = static_cast<LaneAction const*>(Action);
    if (Lane->LegStartXform.Ring == nullptr)
        return 0;
    return Lane->LegStartXform.Ring->get_id();
}

float CShip::get_tradelane_speed()
{
    // FUN_062b17c0 (+0x2bc, +0x2b8)
    if (bUsingTradelane != 0)
        return TradelaneSpeed;
    return 0.0f;
}

int CShip::go_tradelane(IObjInspect const* EntranceRing, IObjInspect const* ExitRing,
                        IObjRW* TradelaneObj, bool bFastExit, float Speed)
{
    // FUN_062b1960. operator_new(0xfc), ctor 062c1220; the IAction base (+0x10) goes to
    // ActionDB::add with no null test.
    bUsingTradelane = 1;
    Actions.add(new LaneAction(EntranceRing, ExitRing, TradelaneObj, bFastExit, Speed));
    return 0;
}

void CShip::airlock_in(Vector const& Pos, Matrix const& Orient)
{
    // FUN_062b2ff0. The last call is vtable +0x7c, beam_object (PhySys::BeamR), which also
    // moves every attached child.
    Throttle = 1.0f;
    CEquip* EngineEquip = const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeEngine));
    if (EngineEquip != nullptr)
    {
        if (!EngineEquip->IsActive())
        {
            EngineEquip->Activate(true);
        }
    }
    beam_object(Pos, Orient, false);
}

float CShip::close_bay()
{
    // FUN_062b2c10. Bay states: 0 closed, 1 closing, 2 open, 3 opening.
    BayDoorCounter -= 1;
    if (BayDoorCounter != 0)
    {
        // Other open requests remain: the stored duration, unrounded. No Anim / handle test.
        return DALib::Anim->GetStoredDuration(BayAnim.Index);
    }

    if (BayState != BayOpening && BayState != BayOpen)
        return 0.0f;
    if (BayAnim.Index == -1)
    {
        BayState = BayClosed;
        return 0.0f;
    }

    BayState = BayClosing;
    DALib::Anim->EvalPlayback(BayAnim.Index, 1, -2.0f, 1.0f, nullptr, AsOpaqueDword(1.0f),
                              AsOpaqueDword(1.0f), nullptr);   // slot 0x2c
    AnimDB::Add(BayAnim.Index);
    float const Result = DALib::Anim->GetStoredDuration(BayAnim.Index);   // slot 0x40

    if (Notify != nullptr)
    {
        unsigned char bOpen = 0;
        notify(INotify::BayStateChanged, &bOpen);
    }

    // Shields come back on when the bay closes.
    CEquipTraverser Walk(EquipTypeShield);
    for (CEquip* Equip = EquipManager.Traverse(Walk); Equip != nullptr;
         Equip = EquipManager.Traverse(Walk))
    {
        Equip->Activate(true);
    }
    return Result;
}

void CShip::jump_in(IObjRW* JumpObj, Vector const& Pos, Matrix const& Orient, float Duration)
{
    // FUN_062b2d40. operator_new(0x68) with the JumpInAction ctor inlined; no null test.
    Actions.add(new JumpInAction(JumpObj, Pos, Orient, Duration));

}

void CShip::collect_force_components(float ThrottleVal, ThrustEquipType ThrustType,
                                     float& OutForceX, float& OutForceY) const
{
    // FUN_062b4540. Every accumulation is rounded to float.
    Archetype::Ship const* const ShipArch = shiparch();
    OutForceX = 0.0f;
    OutForceY = 0.0f;

    if (ThrustType == ThrustEquipEngine)
    {
        CEEngine* const Engine = CEEngine::cast(const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeEngine)));
        if (Engine != nullptr)
        {
            OutForceX = (float)((double)(Engine->GetThrust(ThrottleVal)) + OutForceX);
            OutForceY = (float)((double)(Engine->GetLinearDrag()) + OutForceY);
        }
    }
    else if (ThrustType == ThrustEquipThruster)
    {
        // The thruster walk stops at the first entry CEThruster::cast rejects.
        CEEngine* const Engine = CEEngine::cast(const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeEngine)));
        if (Engine != nullptr)
        {
            OutForceX = (float)((double)(Engine->GetThrust(ThrottleVal)) + OutForceX);
            OutForceY = (float)((double)(Engine->GetLinearDrag()) + OutForceY);
        }

        CEquipTraverser Trav(EquipTypeThruster);
        for (CEThruster* Thruster = CEThruster::cast(const_cast<CEquipManager&>(EquipManager).Traverse(Trav));
             Thruster != nullptr;
             Thruster = CEThruster::cast(const_cast<CEquipManager&>(EquipManager).Traverse(Trav)))
        {
            OutForceX = (float)((double)(Thruster->GetThrust()) + OutForceX);
        }
    }
    else if (ThrustType == ThrustEquipCruise)
    {
        CEEngine* const Engine = CEEngine::cast(const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeEngine)));
        if (Engine != nullptr)
        {
            OutForceX = (float)((double)(Engine->GetCruiseThrust(ThrottleVal)) + OutForceX);
            OutForceY = (float)((double)(Engine->GetCruiseDrag()) + OutForceY);
        }
    }
    else if (ThrustType == ThrustEquipTradelane)
    {
        // NudgeForce +0x120.
        OutForceX = (float)((double)(ThrottleVal) * ShipArch->NudgeForce + OutForceX);
        CEEngine* const Engine = CEEngine::cast(const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeEngine)));
        if (Engine != nullptr)
            OutForceY = (float)((double)(Engine->GetLinearDrag()) + OutForceY);
    }

    // LinearDrag +0x104; zone (+0xa4) slot +0x58 times ZoneImmersion (+0xa8).
    OutForceY = (float)((double)(ShipArch->LinearDrag) + OutForceY);
    float const BaseDrag = OutForceY;
    if (CurrentZone != nullptr)
        OutForceY = (float)((double)(CurrentZone->GetDragModifier()) * ZoneImmersion * BaseDrag +
                                       OutForceY);   // ZONE 0633ad70
}

void CShip::collect_angular_force_components(unsigned Axis, float ThrottleVal,
                                             float& OutTorqueX, float& OutTorqueY) const
{
    // FUN_062b4700. AngularDragFactor +0x2c0; no null test on the archetype. Any other axis
    // leaves OutTorqueY untouched.
    OutTorqueX = Steering.GetAxisSteeringTorque(Axis, ThrottleVal);
    Archetype::Ship const* const ShipArch = shiparch();
    float const DragX = AngularDragFactor * ShipArch->AngularDragX;
    float const DragY = AngularDragFactor * ShipArch->AngularDragY;
    float const DragZ = AngularDragFactor * ShipArch->AngularDragZ;
    if (Axis == 0)
        OutTorqueY = DragX;
    else if (Axis == 1)
        OutTorqueY = DragY;
    else if (Axis == 2)
        OutTorqueY = DragZ;
}

double CShip::get_linear_drag() const
{
    // FUN_062b3730. No null test on the archetype. Returns double: the zone-scaled drag is left
    // unrounded in ST0 and the three CEEngine callers keep it on the FPU.
    Archetype::Ship const* const ShipArch = shiparch();
    float TotalDrag = ShipArch->LinearDrag;

    CEEngine* const Engine = CEEngine::cast(const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeEngine)));
    if (Engine != nullptr && Engine->IsActive())
    {
        if (Engine->bCruiseActive != 0)
            TotalDrag = (float)((double)(Engine->GetCruiseDrag()) + TotalDrag);
        else
            TotalDrag = (float)((double)(Engine->GetLinearDrag()) + TotalDrag);
    }

    if (CurrentZone != nullptr)
        return (double)CurrentZone->GetDragModifier() * TotalDrag;   // ZONE 0633ad70

    return TotalDrag;
}

Vector CShip::get_angular_drag() const
{
    // FUN_062b37b0. AngularDrag +0x108/+0x10c/+0x110; no null test on the archetype.
    Vector OutDrag;
    Archetype::Ship const* const ShipArch = shiparch();
    OutDrag.x = AngularDragFactor * ShipArch->AngularDragX;
    OutDrag.y = AngularDragFactor * ShipArch->AngularDragY;
    OutDrag.z = AngularDragFactor * ShipArch->AngularDragZ;
    return OutDrag;
}

float CShip::get_time_to_accelerate(float ForceOverMass, float Mass, float Drag,
                                    float FromSpeed, float ToSpeed) const
{
    // FUN_062b42f0 (double stands for the unrounded x87 value). Only an ordered "less than"
    // returns 0; NaN goes on.
    if ((double)(FromSpeed) * ToSpeed < 0.0)
        return 0.0f;
    if (FromSpeed < 0.0f)
        return 0.0f;
    if (ToSpeed < 0.0f)
        return 0.0f;

    float const Force = ForceOverMass * Mass;

    // No zero test on Force.
    if (Drag == 0.0f)
        return (float)(((double)(ToSpeed) - FromSpeed) *
                                  ((double)(Mass) / Force));

    // ToSpeed is capped at 0.99 of terminal speed; with a non-positive terminal it is raised to
    // 0.01 (double 0639e6b0).
    double const Terminal = (double)(Force) / Drag;
    float const TerminalF = (float)(Terminal);
    if (Terminal > 0.0)
    {
        double const Cap = (double)(TerminalF) * 0.99f;
        if (ToSpeed > Cap)
            ToSpeed = (float)(Cap);
    }
    else if (TerminalF <= 0.0f)
    {
        if (ToSpeed < 0.01)
            ToSpeed = 0.01f;
    }

    // The gap is tested unrounded and used rounded.
    double const Gap = (double)(FromSpeed) - TerminalF;
    if (Gap == 0.0)
        return 0.0f;
    float const GapF = (float)(Gap);

    double const Ratio = ((double)(ToSpeed) - TerminalF) / GapF;
    if (!(Ratio > 0.0))
        return 0.0f;
    return (float)(-(std::log(Ratio) * Mass / Drag));
}

float CShip::get_distance_travelled(float ForceOverMass, float Mass, float Drag,
                                    float FromSpeed, float Time) const
{
    // FUN_062b4440. NaN passes every test. All on the x87 stack.
    if (Time < 0.0f)
        return 0.0f;
    if (Drag < 0.01f)
        return 0.0f;
    if (FromSpeed < 0.0f)
        FromSpeed = 0.0f;
    if (Time > 1000.0f)
        Time = 1000.0f;

    double const Terminal = (double)(Mass) / Drag * ForceOverMass;
    double const Decay = std::exp(-1.0 / Mass * Drag * Time);
    return (float)(Terminal * Time + ((1.0 - Decay) * ((FromSpeed - Terminal) * Mass)) / Drag);
}

float CShip::get_initial_speed_to_coast_distance(float A, float B, float C) const
{
    // FUN_062b44f0. NaN passes.
    if (A <= 0.0f)
        return 0.0f;
    if (C < 0.01f)
        return 0.0f;
    if (B < 0.0f)
        return 0.0f;
    return (float)((double)(A) / B * C);
}

void CShip::do_simulation_controller(IVP_Event_Sim* EventSim, IVP_U_Vector<IVP_Core>* CoreList)
{
    // FUN_0628f050, an empty body.
    (void)EventSim;
    (void)CoreList;
}

namespace
{
// Inlined at the head of every CShip angular query (CObject::get_moment_of_inertia).
Vector ShipMomentOfInertia(CShip const* Ship)
{
    if (Ship->BodyPtr != nullptr)
        return PhySys::GetMomentOfInertia(Ship);
    Vector Zero;
    Zero.x = 0.0f;
    Zero.y = 0.0f;
    Zero.z = 0.0f;
    return Zero;
}
}

float CShip::get_time_to_accelerate(float Throttle, float FromSpeed, float ToSpeed,
                                    ThrustEquipType ThrustType) const
{
    // FUN_062b37f0. NaN passes the clamps. get_mass is vtable +0x6c.
    if (FromSpeed < 0.0f)
        FromSpeed = 0.0f;
    if (ToSpeed < 0.0f)
        ToSpeed = 0.0f;
    if (Throttle > 1.0f)
        Throttle = 1.0f;
    else if (Throttle < -1.0f)
        Throttle = -1.0f;

    if (FromSpeed == 0.0f && Throttle == 0.0f)
        return 0.0f;

    float const Mass = get_mass();
    if (!(Mass > 0.01))
        return 0.0f;

    float Force = 0.0f;
    float Drag = 0.0f;
    collect_force_components(Throttle, ThrustType, Force, Drag);
    return get_time_to_accelerate(Force / Mass, Mass, Drag, FromSpeed, ToSpeed);
}

float CShip::get_time_to_accelerate_angularly(float PitchThrottle, float YawThrottle,
                                              float FromAngVel, float ToAngVel) const
{
    // FUN_062b3ce0. No archetype test, no zero-inertia guard. Pitch and yaw accelerations are
    // rounded to float; the combined sums and roots stay on the x87 stack until stored.
    Vector const Inertia = ShipMomentOfInertia(this);
    Archetype::Ship const* const ShipArch = shiparch();

    float const PitchAccel = Steering.GetAxisSteeringTorque(0, PitchThrottle) / Inertia.x;
    float const YawAccel = Steering.GetAxisSteeringTorque(1, YawThrottle) / Inertia.y;
    float const Drag = AngularDragFactor * ShipArch->AngularDragY;

    float const CombinedInertia = (float)(
        std::sqrt((double)(Inertia.y) * Inertia.y + (double)(Inertia.x) * Inertia.x));
    float const CombinedAccel = (float)(
        std::sqrt((double)(YawAccel) * YawAccel + (double)(PitchAccel) * PitchAccel));
    return get_time_to_accelerate(CombinedAccel, CombinedInertia, Drag, FromAngVel, ToAngVel);
}

float CShip::get_time_to_accelerate_angularly(float Throttle, float FromAngVel, float ToAngVel) const
{
    // FUN_062b3db0
    Vector const Inertia = ShipMomentOfInertia(this);
    Archetype::Ship const* const ShipArch = shiparch();
    float const RollAccel = Steering.GetAxisSteeringTorque(2, Throttle) / Inertia.z;
    float const Drag = AngularDragFactor * ShipArch->AngularDragZ;
    return get_time_to_accelerate(RollAccel, Inertia.z, Drag, FromAngVel, ToAngVel);
}

float CShip::get_distance_travelled(float Throttle, float FromSpeed, float Time,
                                    ThrustEquipType ThrustType) const
{
    // FUN_062b3900
    if (Time < 0.0f)
        return 0.0f;
    if (FromSpeed < 0.0f)
        FromSpeed = 0.0f;
    if (Throttle > 1.0f)
        Throttle = 1.0f;
    else if (Throttle < -1.0f)
        Throttle = -1.0f;

    float const Mass = get_mass();
    if (!(Mass > 0.01))
        return 0.0f;

    float Force = 0.0f;
    float Drag = 0.0f;
    collect_force_components(Throttle, ThrustType, Force, Drag);

    // The private FUN_062b4440 inlined, but the decay exponent's factor order and the final sum
    // order differ.
    float const Accel = Force / Mass;
    if (Time < 0.0f)
        return 0.0f;
    if (Drag < 0.01f)
        return 0.0f;
    float const StartSpeed = (FromSpeed < 0.0f) ? 0.0f : FromSpeed;
    double const T = (Time > 1000.0f) ? 1000.0 : (double)(Time);

    double const Terminal = (double)(Mass) / Drag * Accel;
    double const Decay = std::exp(-1.0 / Mass * T * Drag);
    return (float)(((1.0 - Decay) * ((StartSpeed - Terminal) * Mass)) / Drag + Terminal * T);
}

float CShip::get_initial_speed_to_coast_distance(float Speed, bool bStop) const
{
    // FUN_062b3aa0. Not the private FUN_062b44f0: here it is (drag / mass) * speed. NaN passes.
    if (Speed <= 0.0f)
        return 0.0f;

    float const Mass = get_mass();
    if (!(Mass > 0.01))
        return 0.0f;

    float Force = 0.0f;
    float Drag = 0.0f;
    collect_force_components(0.0f, bStop ? ThrustEquipEngine : ThrustEquipNormal, Force, Drag);

    if (Speed <= 0.0f)
        return 0.0f;
    if (Drag < 0.01f)
        return 0.0f;
    if (Mass < 0.0f)
        return 0.0f;
    return (float)((double)(Drag) / Mass * Speed);
}

float CShip::get_speed(float ThrottleVal, ThrustEquipType ThrustType) const
{
    // FUN_062b3c30
    if (ThrottleVal > 1.0f)
        ThrottleVal = 1.0f;
    else if (ThrottleVal < -1.0f)
        ThrottleVal = -1.0f;

    if (!(get_mass() > 0.01))
        return 0.0f;

    float Force = 0.0f;
    float Drag = 0.0f;
    collect_force_components(ThrottleVal, ThrustType, Force, Drag);
    if (!(Drag > 0.001f))
        return 0.0f;
    return Force / Drag;
}

float CShip::get_projected_throttle(float Speed, ThrustEquipType ThrustType) const
{
    // FUN_062b3b70. The 0.01f threshold is 0639e6c0; NaN returns 0 or r.
    float Direction = 1.0f;
    if (!(Speed > 0.0f))
        Direction = -1.0f;
    float Force = 0.0f;
    float Drag = 0.0f;
    collect_force_components(Direction, ThrustType, Force, Drag);

    if (!(Drag >= 0.0f))
        return 0.0f;
    if (!(std::fabs((double)(Force)) > (double)(0.01f)))
        return 0.0f;
    double const Result = std::fabs((double)(Speed)) * Drag / Force;
    if (!(Result > 1.0))
        return (float)(Result);
    return 1.0f;
}

float CShip::get_projected_axis_throttle_XY(float Speed) const
{
    // FUN_062b40e0. No archetype test, no zero-inertia guard. Pitch is rounded to float, yaw
    // is not.
    Vector const Inertia = ShipMomentOfInertia(this);
    Archetype::Ship const* const ShipArch = shiparch();
    float const PitchAccel = Steering.GetAxisSteeringTorque(0, 1.0f) / Inertia.x;
    double const YawAccel = (double)(Steering.GetAxisSteeringTorque(1, 1.0f)) / Inertia.y;
    float const Drag = AngularDragFactor * ShipArch->AngularDragY;
    double const Combined = std::sqrt(YawAccel * YawAccel + (double)(PitchAccel) * PitchAccel);
    if (!(std::fabs(Combined) > (double)(0.01f)))
        return 0.0f;
    return (float)((double)(Drag) * Speed / (Combined * Inertia.y));
}

float CShip::get_projected_axis_throttle_Z(float Speed) const
{
    // FUN_062b41b0. No archetype test, no zero-inertia guard; roll stays unrounded.
    Vector const Inertia = ShipMomentOfInertia(this);
    Archetype::Ship const* const ShipArch = shiparch();
    double const RollAccel = (double)(Steering.GetAxisSteeringTorque(2, 1.0f)) / Inertia.z;
    float const Drag = AngularDragFactor * ShipArch->AngularDragZ;
    if (!(std::fabs(RollAccel) > (double)(0.01f)))
        return 0.0f;
    return (float)((double)(Drag) * Speed / ((double)(Inertia.z) * RollAccel));
}

float CShip::get_angular_speed_XY(float MaxRollRate, float MaxPitchRate) const
{
    // FUN_062b4250. No archetype test. Pitch is rounded to float, yaw is not.
    Archetype::Ship const* const ShipArch = shiparch();
    float const PitchSpeed = (float)((double)(Steering.GetAxisSteeringTorque(0, MaxRollRate)) /
                                                ((double)(AngularDragFactor) * ShipArch->AngularDragX));
    double const YawSpeed = (double)(Steering.GetAxisSteeringTorque(1, MaxPitchRate)) /
                            ((double)(AngularDragFactor) * ShipArch->AngularDragY);
    return (float)(std::sqrt(YawSpeed * YawSpeed + (double)(PitchSpeed) * PitchSpeed));
}

float CShip::get_angular_speed_Z(float MaxYawRate) const
{
    // FUN_062b42c0. No archetype test.
    Archetype::Ship const* const ShipArch = shiparch();
    return (float)((double)(Steering.GetAxisSteeringTorque(2, MaxYawRate)) /
                              ((double)(AngularDragFactor) * ShipArch->AngularDragZ));
}

float CShip::get_angular_distance_travelled(float PitchThrottle, float YawThrottle,
                                            float FromAngVel, float Time) const
{
    // FUN_062b3e40. No archetype test, no zero-inertia guard. Pitch is rounded to float, yaw
    // is not; NaN passes the tests.
    Vector const Inertia = ShipMomentOfInertia(this);
    Archetype::Ship const* const ShipArch = shiparch();

    float const PitchAccel = Steering.GetAxisSteeringTorque(0, PitchThrottle) / Inertia.x;
    double const YawAccel = (double)(Steering.GetAxisSteeringTorque(1, YawThrottle)) / Inertia.y;
    float const Drag = AngularDragFactor * ShipArch->AngularDragY;
    float const CombinedAccel = (float)(
        std::sqrt(YawAccel * YawAccel + (double)(PitchAccel) * PitchAccel));
    float const CombinedInertia = (float)(
        std::sqrt((double)(Inertia.y) * Inertia.y + (double)(Inertia.x) * Inertia.x));

    if (Time < 0.0f)
        return 0.0f;
    if (Drag < 0.01f)
        return 0.0f;
    double const StartSpeed = (FromAngVel < 0.0f) ? 0.0 : (double)(FromAngVel);
    double const T = (Time > 1000.0f) ? 1000.0 : (double)(Time);

    double const Terminal = (double)(CombinedAccel) / Drag * CombinedInertia;
    double const Decay = std::exp(-1.0 / CombinedInertia * T * Drag);
    return (float)(Terminal * T + ((1.0 - Decay) * ((StartSpeed - Terminal) * CombinedInertia)) / Drag);
}

float CShip::get_angular_distance_travelled(float Throttle, float FromAngVel, float Time) const
{
    // FUN_062b3fb0. No archetype test, no zero-inertia guard.
    Vector const Inertia = ShipMomentOfInertia(this);
    Archetype::Ship const* const ShipArch = shiparch();
    float const RollAccel = Steering.GetAxisSteeringTorque(2, Throttle) / Inertia.z;
    float const Drag = AngularDragFactor * ShipArch->AngularDragZ;

    if (Time < 0.0f)
        return 0.0f;
    if (Drag < 0.01f)
        return 0.0f;
    double const StartSpeed = (FromAngVel < 0.0f) ? 0.0 : (double)(FromAngVel);
    double const T = (Time > 1000.0f) ? 1000.0 : (double)(Time);

    // The decay exponent's factor order differs from the 4-parameter overload.
    double const Terminal = (double)(RollAccel) / Drag * Inertia.z;
    double const Decay = std::exp(-1.0 / Inertia.z * Drag * T);
    return (float)(Terminal * T + ((1.0 - Decay) * ((StartSpeed - Terminal) * Inertia.z)) / Drag);
}

float CShip::get_throttle() const
{
    // FUN_06287b60 (+0x29c)
    return Throttle;
}

Vector const& CShip::get_axis_throttle() const
{
    // FUN_06287b70 (+0x24c, Steering+0x1c)
    return Steering.GetAxisThrottle();
}

float CShip::get_max_bank_angle() const
{
    // FUN_06287ba0. No null test on the archetype.
    return shiparch()->MaxBankAngle;
}

float CShip::get_thrust_power() const
{
    // FUN_06288240 (+0x2a0)
    return ThrustPower;
}

float CShip::get_max_thrust_power() const
{
    // FUN_06288260 (+0x2a4)
    return MaxThrustPower;
}

float CShip::get_thrust_power_ratio() const
{
    // FUN_06288270
    if (MaxThrustPower <= 0.0f)
        return 0.0f;
    return ThrustPower / MaxThrustPower;
}

enum BayState CShip::get_bay_state() const
{
    // FUN_06287b50 (+0x2ac). The member of the same name shadows the enum inside CShip, hence
    // the elaborated type specifier.
    return (enum BayState)BayState;
}

void CShip::VerifyTarget()
{
    // FUN_062b1190. Drops a sub-target that is no longer part of the target's hierarchy.
    IObjRW* const TargetObj = Target.get();
    if (TargetObj == nullptr)
    {
        SubTargetIndex = SUBOBJ_ID_NONE;
        return;
    }
    if (SubTargetIndex == SUBOBJ_ID_NONE)
        return;

    // sub_obj_id_to_inst is vtable +0xf8. Retail fetches cobject() twice.
    long SubInst = -1;
    CObject const* const Obj = TargetObj->cobject();
    if (Obj != nullptr && (Obj->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
    {
        CObject const* const EqObj = Target.get()->cobject();
        CEqObj const* const Eq = (EqObj != nullptr && (EqObj->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
            ? (CEqObj const*)EqObj : nullptr;
        SubInst = Eq->sub_obj_id_to_inst(SubTargetIndex);
    }

    // get_index is IObjInspect slot 0x1c.
    if (!IsDescendant(Target.get()->get_index(), SubInst))
        SubTargetIndex = SUBOBJ_ID_NONE;
}

bool CShip::get_tgt_lead_fire_pos(IObjInspect const* TargetInspect, Vector& OutPos) const
{
    // FUN_062b1290
    if (TargetInspect == nullptr)
        return false;

    bool bLead = false;
    ShipGunStats const& Stats = get_gun_stats();
    float RangeSq = 0.0f;

    if (Stats.ActiveGunCount > 0)
    {
        // Aim at the sub-target's centre of mass (slot 6) when that answers 0, else the
        // object's (slot 5).
        Vector const TargetVelocity = TargetInspect->get_velocity();
        Vector TargetPos;
        if (TargetInspect->get_sub_obj_center_of_mass(SubTargetIndex, TargetPos) != 0)
            TargetPos = TargetInspect->get_center_of_mass();

        Vector RelPos;
        RelPos.x = (float)((double)(TargetPos.x) - WorldPosition.x);
        RelPos.y = (float)((double)(TargetPos.y) - WorldPosition.y);
        RelPos.z = (float)((double)(TargetPos.z) - WorldPosition.z);
        Vector const ShipVelocity = get_velocity();
        Vector RelVel;
        RelVel.x = (float)((double)(TargetVelocity.x) - ShipVelocity.x);
        RelVel.y = (float)((double)(TargetVelocity.y) - ShipVelocity.y);
        RelVel.z = (float)((double)(TargetVelocity.z) - ShipVelocity.z);
        float Time = 0.0f;

        // x87: x and y round once, z rounds the product first.
        if (CEGun::ComputeTimeToTgt(RelPos, RelVel, Stats.AvgMuzzleVelocity, Time))
        {
            float const LeadZ = (float)((double)(RelVel.z) * Time);
            OutPos.x = (float)((double)(RelVel.x) * Time + TargetPos.x);
            OutPos.y = (float)((double)(RelVel.y) * Time + TargetPos.y);
            OutPos.z = (float)((double)(LeadZ) + TargetPos.z);
            bLead = true;
        }

        RangeSq = (float)((double)(Stats.MaxMunitionRange) * Stats.MaxMunitionRange);
    }
    else if (Stats.ActiveMissileCount > 0)
    {
        RangeSq = (float)((double)(Stats.MaxActiveMissileRange) * Stats.MaxActiveMissileRange);
    }

    if (!bLead)
        OutPos = TargetInspect->get_position();

    // In range only when strictly below (sum order dz, dy, dx).
    double const Dx = (double)(OutPos.x) - WorldPosition.x;
    double const Dy = (double)(OutPos.y) - WorldPosition.y;
    double const Dz = (double)(OutPos.z) - WorldPosition.z;
    double const DistSq = Dz * Dz + Dy * Dy + Dx * Dx;
    return DistSq < RangeSq;
}

bool CShip::check_formation_follower_status(float MaxDistance)
{
    // FUN_062b4bf0. False when any live follower (no class test) is farther than MaxDistance
    // from its formation slot.
    for (std::vector<FObjRWWatcher>::const_iterator It = FormationFollowers.begin();
         It != FormationFollowers.end(); ++It)
    {
        IObjRW* const Follower = It->get();
        if (Follower == nullptr)
            continue;

        Vector Offset;   // IObjInspect slot 0xac
        Offset.x = 0.0f;
        Offset.y = 0.0f;
        Offset.z = 0.0f;
        Follower->get_formation_offset(Offset);

        // I3DMathEngine slot 0x28, this+8 read as one Transform.
        Vector Slot;
        Acquire3DMathEngine()->TransformXform((FVector&)Slot, (FTransform const&)Orientation,
                                              (FVector const&)Offset);

        Vector const& FollowerPos = Follower->get_position();
        double const Dx = (double)(FollowerPos.x) - Slot.x;
        double const Dy = (double)(FollowerPos.y) - Slot.y;
        double const Dz = (double)(FollowerPos.z) - Slot.z;
        double const S = Dz * Dz + Dy * Dy + Dx * Dx;

        // Approximate distance: the 0x5f3759df inverse square root of S rounded to float, one
        // Newton step against the unrounded S (0.5 = DAT_0639c13c, 1.5 = DAT_0639e6d0), times S.
        float const SRounded = (float)(S);
        int Bits;
        memcpy(&Bits, &SRounded, sizeof(Bits));
        Bits = 0x5f3759df - (Bits >> 1);
        float Y;
        memcpy(&Y, &Bits, sizeof(Y));
        double Dist = (double)(Y) * Y;
        Dist *= S;
        Dist *= (double)(0.5f);
        Dist = (double)(1.5f) - Dist;
        Dist *= Y;
        Dist *= S;

        if (Dist > MaxDistance)
            return false;
    }
    return true;
}

void CShip::flush_animations()
{
    // FUN_062b0dd0. AnimDB::Rem runs even for a -1 handle.
    AnimDB::Rem(BayAnim.Index);
    if (BayAnim.Index != -1)
    {
        DALib::Anim->StopPlayback(BayAnim.Index);                         // slot 0x38
        DALib::Anim->DestroyScript((void*)BayAnim.Index);                 // slot 0x28
        BayAnim.Index = -1;
    }

    CEqObj::flush_animations();
}

void CShip::disable_controllers()
{
    // FUN_062b0d30
    CEqObj::disable_controllers();
    Steering.DisableController();
    NudgeEngine.DisableController();
    StrafeEngine.DisableController();
    PhySys::Controller::UnRegister(this);
}

void CShip::enable_controllers()
{
    // FUN_062b0d80
    CEqObj::enable_controllers();
    Steering.EnableController();
    NudgeEngine.EnableController();
    StrafeEngine.EnableController();
    PhySys::Controller::Register(this, this);
}

// ============================================================================
// CShip::update
// ============================================================================

int CShip::update(float Dt, unsigned Flags)
{
    // FUN_062b0e30. Always returns 0; CEqObj::update's result is dropped.
    if ((Flags & CObject::NO_ACTIONS) == 0)
        Actions.update(Dt);
    CSimple::update_zones(Dt, Flags);
    CEqObj::update(Dt, Flags);

    if ((Flags & CObject::NO_DYNAMICS) == 0)
    {
        PhySys::Wakeup(this);

        // No null test on the archetype.
        Archetype::Ship const* const ShipArch = shiparch();
        float Drag = ShipArch->LinearDrag;

        // GetCruiseDrag leaves its sum unrounded in ST0, so the engine drag is double.
        CEEngine* const Engine =
            CEEngine::cast(const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeEngine)));
        if (Engine != nullptr && Engine->IsActive())
        {
            double const EngineDrag = Engine->bCruiseActive != 0 ? Engine->GetCruiseDrag()
                                                                 : Engine->GetLinearDrag();
            Drag = (float)(EngineDrag + Drag);
        }

        // IZone slot +0x58.
        if (CurrentZone != nullptr)
            Drag = (float)((double)(CurrentZone->GetDragModifier()) * Drag);

        // No zero test on the mass.
        float const LinearDamping = (float)((double)(Drag) / get_mass());

        Vector AngularDrag;
        AngularDrag.x = (float)((double)(AngularDragFactor) * ShipArch->AngularDragX);
        AngularDrag.y = (float)((double)(AngularDragFactor) * ShipArch->AngularDragY);
        AngularDrag.z = (float)((double)(AngularDragFactor) * ShipArch->AngularDragZ);

        // No zero test on the inertia.
        Vector const MomentOfInertia = PhySys::GetMomentOfInertia(this);
        Vector AngularDamping;
        AngularDamping.x = (float)((double)(AngularDrag.x) / MomentOfInertia.x);
        AngularDamping.y = (float)((double)(AngularDrag.y) / MomentOfInertia.y);
        AngularDamping.z = (float)((double)(AngularDrag.z) / MomentOfInertia.z);
        PhySys::SetDamping(this, LinearDamping, AngularDamping);
    }

    // Bay door animation (slots 0x14 / 0x40): opening becomes open when the stored duration
    // reaches the script's (unordered keeps opening); closing becomes closed at <= 0. No null
    // test on Anim.
    if (BayState == BayOpening)
    {
        float const ScriptDuration =
            DALib::Anim->GetScriptDuration((void*)BayAnim.Index);
        float const StoredDuration = DALib::Anim->GetStoredDuration(BayAnim.Index);
        if (StoredDuration >= ScriptDuration)
            BayState = BayOpen;
    }
    else if (BayState == BayClosing)
    {
        float const StoredDuration = DALib::Anim->GetStoredDuration(BayAnim.Index);
        if (StoredDuration <= 0.0f)
            BayState = BayClosed;
    }

    // The atmosphere the ship is inside: IObjDB slot 0x0c bucket 0xfdff, QuerySphere (slot
    // 0x1c) with radius 14000.
    AtmosphereSolar = nullptr;

    if (ObjectDb != nullptr && PhySys::GetCollisionState(this))
    {
        ISpatialPartition* const Bucket = (ISpatialPartition*)ObjectDb->GetBucket(0xfdff);
        if (Bucket != nullptr)
        {
            Geometry::Sphere Query;
            Query.Center = WorldPosition;
            Query.Radius = 14000.0f;
            FSpatialQueryResult Found;
            Found.Capacity = 0x800;
            Found.Count = 0;
            if ((unsigned)Bucket->QuerySphere(&Query, &Found) != 0)
            {
                for (unsigned i = 0; i < Found.Count; ++i)
                {
                    // No null test on the narrowed solar: bucket 0xfdff holds solars only, and
                    // a non-solar entry faults as in retail.
                    IObjRW* const Candidate = (IObjRW*)Found.Items[i];
                    CObject const* const Obj = Candidate->cobject();
                    CSolar* const Solar = (Obj != nullptr && (Obj->ObjectClass & CObject::ClassSolar) == CObject::ClassSolar)
                        ? (CSolar*)Obj : nullptr;
                    if (Solar->get_atmosphere_immersion(WorldPosition) > 0.0f)
                    {
                        AtmosphereSolar = Solar;
                        break;
                    }
                }
            }
        }
    }

    // A target that is no longer targetable (IObjInspect slot 0x34) is dropped.
    if (Target.get() != nullptr && !Target.get()->is_targetable())
    {
        if (Target.get() != nullptr)
        {
            CObject* const TargetObj = const_cast<CObject*>(Target.get()->cobject());
            if (TargetObj != nullptr && (TargetObj->ObjectClass & CObject::ClassShip) == CObject::ClassShip)
                ((CShip*)TargetObj)->notify_set_target(this, false);
            Target.set(nullptr);
        }
        SubTargetIndex = SUBOBJ_ID_NONE;
    }

    return 0;
}

// ============================================================================
// CShip::open_bay
// ============================================================================

double CShip::open_bay()
{
    // FUN_062b2aa0. Returns the time until fully open.
    int const OldCounter = BayDoorCounter;
    BayDoorCounter = OldCounter + 1;
    if (OldCounter != 0)
    {
        // Already requested: Script (slot 0x14) - Stored (slot 0x40, fetched first), returned
        // unrounded. No Anim / handle test.
        float const Stored = DALib::Anim->GetStoredDuration(BayAnim.Index);
        float const Script = DALib::Anim->GetScriptDuration((void*)BayAnim.Index);
        return (double)Script - Stored;
    }

    if (BayState != BayClosing && BayState != BayClosed)
        return 0.0f;
    if (BayAnim.Index == -1)
    {
        BayState = BayOpen;
        return 0.0f;
    }

    BayState = BayOpening;
    DALib::Anim->EvalPlayback(BayAnim.Index, 0, -2.0f, 1.0f, nullptr, AsOpaqueDword(1.0f),
                              AsOpaqueDword(1.0f), nullptr);   // slot 0x2c
    AnimDB::Add(BayAnim.Index);
    float const Stored = DALib::Anim->GetStoredDuration(BayAnim.Index);
    float const Script = DALib::Anim->GetScriptDuration((void*)BayAnim.Index);
    float const Result = (float)((double)(Script) - Stored);

    if (Notify != nullptr)
    {
        unsigned char bOpen = 1;
        notify(INotify::BayStateChanged, &bOpen);
    }

    // Shields go down while the bay is open.
    CEquipTraverser Walk(EquipTypeShield);
    for (CEquip* Equip = EquipManager.Traverse(Walk); Equip != nullptr;
         Equip = EquipManager.Traverse(Walk))
    {
        Equip->Activate(false);
    }
    return Result;
}

// ============================================================================
// CShip::notify_set_target
// ============================================================================

void CShip::notify_set_target(CShip* TargetShip, bool bIsTarget)
{
    // FUN_062b24d0. TargetShip is the ship that started or stopped targeting this one; no null
    // test.
    if ((TargetShip->TypeBits & 0x30000) == 0)
        return;
    unsigned const TargeterId = TargetShip->Id;

    if (bIsTarget)
    {
        // The first cleared slot takes the targeter. No duplicate check.
        for (std::vector<FSimpleWatcher>::iterator It = Targeters.begin(); It != Targeters.end(); ++It)
        {
            if (It->get() == nullptr)
            {
                It->watch(TargetShip);
                return;
            }
        }
        // Vector insert FUN_062b54f0.
        FSimpleWatcher const Temp(TargetShip);
        Targeters.push_back(Temp);
        return;
    }

    // Every slot with the targeter's Id is cleared.
    for (std::vector<FSimpleWatcher>::iterator It = Targeters.begin(); It != Targeters.end(); ++It)
    {
        CSimple* const Existing = It->get();
        if (Existing != nullptr && Existing->Id == TargeterId)
            It->set(nullptr);
    }
}

// ============================================================================
// CShip::launch
// ============================================================================

void CShip::launch(IObjRW* DockObj, CEqObj* BaseObj, int DockPortIndex)
{
    // FUN_062b2a20. operator_new(0x78). The bay fields are not touched here.
    LaunchAction* Action = new LaunchAction(DockObj, BaseObj, DockPortIndex);
    Actions.add(Action);
    // FUN_062aff80, called unconditionally (also with a null allocation in retail).
    Action->Start();
}

// FUN_0632ed40: always 0.
int CShip::get_status(char* p1, int p2) const
{
    (void)p1;
    (void)p2;
    return 0;
}

bool CShip::is_player() const
{
    return bIsPlayer != 0;
}

// ============================================================================
// CShip setters
// ============================================================================

void CShip::set_throttle(float ThrottleValue)
{
    // FUN_062b16c0. Also activates an inactive engine.
    Throttle = ThrottleValue;

    CEquip* EngineEquip = const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeEngine));
    if (EngineEquip != nullptr && !EngineEquip->IsActive())
    {
        EngineEquip->Activate(true);
    }
}

void CShip::set_axis_throttle(Vector const& AxisThrottleValue)
{
    // Steering +0x1c (+0x24c)
    Steering.SetAxisThrottle(AxisThrottleValue);
}

void CShip::set_nudge_vec(Vector const& NudgeValue)
{
    // NudgeEngine +0x1c (+0x274)
    NudgeEngine.SetNudgeVec(NudgeValue);
}

void CShip::set_strafe_dir(StrafeDir StrafeDirValue)
{
    // StrafeEngine +0x18 (+0x298)
    StrafeEngine.SetStrafe((StrafeDir)StrafeDirValue);
}

void CShip::set_angular_drag_factor(float Factor)
{
    // FUN_06287b90 (+0x2c0)
    AngularDragFactor = Factor;
}

void CShip::set_thrust_power(float Power)
{
    // FUN_06288250 (+0x2a0)
    ThrustPower = Power;
}

void CShip::set_sub_target(unsigned short SubTargetId)
{
    // FUN_062b22f0 (+0x228)
    SubTargetIndex = SubTargetId;
}

void CShip::set_gun_stats_dirty()
{
    // +0x2c4
    GunStatsDirty = 1;
}

// ============================================================================
// CShip tradelane methods
// ============================================================================

void CShip::use_tradelane(bool bUseTradelane)
{
    // FUN_062b17a0 (+0x2bc)
    bUsingTradelane = bUseTradelane ? 1 : 0;
}

void CShip::stop_tradelane()
{
    // FUN_062b17e0. The 0x10 test is the null-LaneAction check, as in get_tradelane_*.
    IAction* Lane = Actions.find("LaneAction");
    if (Lane != nullptr && Lane != (IAction*)0x10)
    {
        // IAction is LaneAction's second base, after the 0x10-byte PhySys::Controller.
        LaneAction* LaneAct = static_cast<LaneAction*>(Lane);
        bUsingTradelane = 0;
        // FUN_062c2350(1): param 1 skips the behaviour dispatch (vtable slot 26) that
        // stop(0) makes on the tradelane target.
        LaneAct->stop(1);
    }
}

extern COMMON_API IBehaviorManager* __cdecl GetBehaviorManager(IObjRW* Obj);

void CShip::request_exit_tradelane()
{
    // FUN_062b1870. Actions is +0x2b0. Retail's null-LaneAction test after the -0x10 base
    // adjustment is spelled out on the address (a static_cast cannot yield null).
    IAction* const Lane = Actions.find("LaneAction");
    if (Lane == nullptr)
        return;
    if (Lane == (IAction*)0x10)
        return;
    static_cast<LaneAction*>(Lane)->request_exit();   // FUN_062c2300

    // Every follow follower with a behaviour manager gets the case-4 tradelane event (and a
    // warning is logged per follower). Payload byte +3 is never written.
    for (std::vector<FObjRWWatcher>::const_iterator It = FollowFollowers.begin(); It != FollowFollowers.end(); ++It)
    {
        IObjRW* const FollowerRW = It->get();
        if (FollowerRW == nullptr)
            continue;
        IBehaviorManager* const BehMgr = GetBehaviorManager(FollowerRW);
        if (BehMgr == nullptr)
            continue;

        FBehaviorTradelaneEvent Payload;
        Payload.bEnteringLane = 0;
        Payload.bExitRequested = 1;
        Payload.bFastExit = 0;
        Payload.Speed = 0.0f;
        Payload.Field_0x08 = 0;
        Payload.ShipId = Id;
        Payload.EntranceId = 0;
        Payload.ExitId = 0;
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", "E:\\FL\\Scratch\\Source\\Common\\CShip.cpp", 0x3c5,
              "request_exit_tradelane");

        FBehaviorManagerEvent Event;
        Event.Type = 4;
        Event.Data = &Payload;
        BehMgr->OnEvent(&Event);
    }
}

void CShip::set_tradelane_speed(float Speed)
{
    // +0x2b8
    TradelaneSpeed = Speed;
}

Vector const& CShip::get_follow_offset()
{
    // FUN_062b4f00 (+0x1e8)
    return FollowOffset;
}

void CShip::set_follow_offset(Vector const& Offset)
{
    // FUN_062b4ec0
    FollowOffset.x = Offset.x;
    FollowOffset.y = Offset.y;
    FollowOffset.z = Offset.z;
}

// ============================================================================
// CShip formation methods
// ============================================================================

void CShip::set_follow_leader(IObjRW* Leader)
{
    // FUN_062b4e90
    FollowLeader.watch(Leader);
}

FORMATION_RTYPE CShip::remove_follow_follower(IObjRW* Follower)
{
    // FUN_062b4e20. The slot is cleared, never erased.
    if (Follower != nullptr)
    {
        for (std::vector<FObjRWWatcher>::iterator It = FollowFollowers.begin();
             It != FollowFollowers.end(); ++It)
        {
            IObjRW* const Existing = It->get();
            if (Existing != nullptr && Existing->get_id() == Follower->get_id())
            {
                It->set(nullptr);
                return FORMATION_RTYPE_OK;
            }
        }
    }
    return FORMATION_RTYPE_NOT_FOUND;
}

FORMATION_RTYPE CShip::remove_formation_follower(IObjRW* Follower)
{
    // FUN_062b49b0, with recalculate_formation_speed inlined.
    if (Follower != nullptr)
    {
        for (std::vector<FObjRWWatcher>::iterator It = FormationFollowers.begin();
             It != FormationFollowers.end(); ++It)
        {
            IObjRW* const Existing = It->get();
            if (Existing != nullptr && Existing->get_id() == Follower->get_id())
            {
                It->set(nullptr);
                recalculate_formation_speed();
                return FORMATION_RTYPE_OK;
            }
        }
    }
    return FORMATION_RTYPE_NOT_FOUND;
}

void CShip::recalculate_formation_speed()
{
    // FUN_062b4790. The slowest follower's get_speed (IObjInspect slot 0xd8, any class); an
    // unordered speed replaces the minimum.
    float MinSpeed = 1.0e8f;
    for (std::vector<FObjRWWatcher>::const_iterator It = FormationFollowers.begin();
         It != FormationFollowers.end(); ++It)
    {
        IObjRW* const Follower = It->get();
        if (Follower != nullptr)
        {
            float const Speed = Follower->get_speed(1.0f, IObject::ThrustEquipEngine);
            if (!(MinSpeed < Speed))
                MinSpeed = Speed;
        }
    }
    FormationSpeed = MinSpeed;
}

namespace
{
    // The raw Watchable* a CEScanner::Scans entry watches.
    void* ScanEntryWatched(BaseWatcher const& Entry)
    {
        return (void*)Entry.target();
    }

    // The IObjRW a scanner slot watches (Watchable is IObjRW's base at +8), or null.
    IObjRW* ScanEntryOwner(BaseWatcher const& Entry)
    {
        return ObjRwFromWatchable(ScanEntryWatched(Entry));
    }

    // The candidate test next_enemy / previous_enemy inline: a CEqObj whose attitude towards
    // the ship is <= -0.6f (DAT_0639e548; unordered fails).
    bool IsHostileEqObj(IObjRW* Object, int MyVibe)
    {
        CObject const* const Obj = Object->cobject();
        if (Obj == nullptr || (Obj->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
            return false;
        int const TheirVibe = ((CEqObj const*)Obj)->VibeId;
        int const OwnVibe = MyVibe;
        float Attitude = 0.0f;
        if (Reputation::Vibe::AttitudeTowards(TheirVibe, OwnVibe, Attitude) != 0)
            return false;
        return Attitude <= -0.6f;
    }

    // Each difference rounded to float (FUN_0628b030), then the root of (y, z, x) unrounded.
    double RoundedDistance(Vector const& To, Vector const& From)
    {
        float const X = (float)((double)(To.x) - From.x);
        float const Y = (float)((double)(To.y) - From.y);
        float const Z = (float)((double)(To.z) - From.z);
        double const Sum = (double)(Y) * Y + (double)(Z) * Z +
                           (double)(X) * X;
        return sqrt(Sum);
    }

    // The second passes keep the differences unrounded; sum order (x, z, y).
    double UnroundedDistance(Vector const& To, Vector const& From)
    {
        double const Dx = (double)(To.x) - From.x;
        double const Dy = (double)(To.y) - From.y;
        double const Dz = (double)(To.z) - From.z;
        return sqrt((Dx * Dx + Dz * Dz) + Dy * Dy);
    }
}

IObjRW* CShip::next_enemy()
{
    // FUN_062b1bf0. Cycles to the nearest hostile scanned object farther than the current
    // target, else the nearest one at all.
    IObjRW* Best = nullptr;
    float MinAbove = 1.0e9f;
    float CurrentDist = -1.0f;

    IObjRW* const TargetObj = Target.get();
    if (TargetObj != nullptr && IsHostileEqObj(TargetObj, VibeId))
        CurrentDist = (float)(RoundedDistance(Target.get()->get_position(), WorldPosition));

    // A missing scanner skips the first pass only.
    CEScanner const* const Scanner =
        (CEScanner const*)EquipManager.FindFirst(EquipTypeScanner);

    // is_dying is slot 0x38.
    if (Scanner != nullptr)
    {
        for (int i = 0; i < Scanner->Scans.Count; ++i)
        {
            IObjRW* const Candidate = ScanEntryOwner(Scanner->Scans.Entries[i]);
            if (Candidate == nullptr || !IsHostileEqObj(Candidate, VibeId) || Candidate->is_dying())
                continue;
            double const Dist = RoundedDistance(Candidate->get_position(), WorldPosition);
            if (Dist < MinAbove && Dist > CurrentDist)
            {
                MinAbove = (float)Dist;
                Best = Candidate;
            }
        }
        if (Best != nullptr)
            return Best;
    }

    // Retail quirk kept: reached with a null scanner too, which then faults on its +0x824.
    float Nearest = 1.0e9f;
    for (int i = 0; i < Scanner->Scans.Count; ++i)
    {
        IObjRW* const Candidate = ScanEntryOwner(Scanner->Scans.Entries[i]);
        if (Candidate == nullptr || !IsHostileEqObj(Candidate, VibeId) || Candidate->is_dying())
            continue;
        double const Dist = UnroundedDistance(Candidate->get_position(), WorldPosition);
        if (Dist < Nearest)
        {
            Nearest = (float)(Dist);
            Best = Candidate;
        }
    }
    return Best;
}

IObjRW* CShip::previous_enemy()
{
    // FUN_062b1f70, next_enemy mirrored.
    IObjRW* Best = nullptr;
    float MaxBelow = -1.0f;
    float CurrentDist = 1.0e9f;

    IObjRW* const TargetObj = Target.get();
    if (TargetObj != nullptr && IsHostileEqObj(TargetObj, VibeId))
        CurrentDist = (float)(RoundedDistance(Target.get()->get_position(), WorldPosition));

    CEScanner const* const Scanner =
        (CEScanner const*)EquipManager.FindFirst(EquipTypeScanner);

    if (Scanner != nullptr)
    {
        for (int i = 0; i < Scanner->Scans.Count; ++i)
        {
            IObjRW* const Candidate = ScanEntryOwner(Scanner->Scans.Entries[i]);
            if (Candidate == nullptr || !IsHostileEqObj(Candidate, VibeId) || Candidate->is_dying())
                continue;
            double const Dist = RoundedDistance(Candidate->get_position(), WorldPosition);
            if (Dist > MaxBelow && Dist < CurrentDist)
            {
                MaxBelow = (float)Dist;
                Best = Candidate;
            }
        }
        if (Best != nullptr)
            return Best;
    }

    // Retail quirk kept: reached with a null scanner too, which then faults.
    float Farthest = -1.0f;
    for (int i = 0; i < Scanner->Scans.Count; ++i)
    {
        IObjRW* const Candidate = ScanEntryOwner(Scanner->Scans.Entries[i]);
        if (Candidate == nullptr || !IsHostileEqObj(Candidate, VibeId) || Candidate->is_dying())
            continue;
        double const Dist = UnroundedDistance(Candidate->get_position(), WorldPosition);
        if (Dist > Farthest)
        {
            Farthest = (float)(Dist);
            Best = Candidate;
        }
    }
    return Best;
}

IObjRW* CShip::next_target(bool bReverse)
{
    // FUN_062b1a40. The FindFirst result is used as a CEScanner without a cast check.
    CEScanner const* const Scanner =
        (CEScanner const*)EquipManager.FindFirst(EquipTypeScanner);
    if (Scanner == nullptr)
        return nullptr;
    int const Count = Scanner->Scans.Count;                           // +0x824
    if (Count < 1)
        return nullptr;

    int Current = -1;
    if (Target.get() != nullptr)
    {
        for (int i = 0; i < Count; ++i)
        {
            if (Target.get() == ScanEntryOwner(Scanner->Scans.Entries[i]))
            {
                Current = i;
                break;
            }
        }
    }

    // Count wrapping steps; skips the current slot, empty slots and objects with TypeBits 0
    // or 0x20000000.
    int const Step = bReverse ? -1 : 1;
    int Index = Current;
    for (int Steps = 0; Steps < Count; ++Steps)
    {
        Index += Step;
        if (Index < 0)
            Index = Count - 1;
        else if (Index >= Count)
            Index = 0;
        if (Index < 0 || Index >= Count || Index == Current)
            continue;

        IObjRW* const Candidate = ScanEntryOwner(Scanner->Scans.Entries[Index]);
        if (Candidate == nullptr)
            continue;
        // TypeBits (+0xe0) is read with no null test: a non-CSimple object faults, as in retail.
        CObject const* const Obj = Candidate->cobject();
        CSimple const* const Simple = (Obj != nullptr && (Obj->ObjectClass & 3) == 3)
            ? (CSimple const*)Obj : nullptr;
        if (Simple->TypeBits == 0 || Simple->TypeBits == 0x20000000)
            continue;
        return ScanEntryOwner(Scanner->Scans.Entries[Index]);
    }
    return nullptr;
}

bool CShip::leaving_system_update(unsigned& LeavingObjId)
{
    // FUN_062b4b10. LeavingObjId is the object that leaves the system; every watcher slot on it
    // is cleared.

    bool bFound = false;

    // get_id is IObjInspect slot 0x20.
    for (std::vector<FObjRWWatcher>::iterator It = FormationFollowers.begin();
         It != FormationFollowers.end(); ++It)
    {
        IObjRW* const Follower = It->get();
        if (Follower != nullptr && Follower->get_id() == LeavingObjId)
        {
            bFound = true;
            It->set(nullptr);
        }
    }

    for (std::vector<FObjRWWatcher>::iterator It = FollowFollowers.begin();
         It != FollowFollowers.end(); ++It)
    {
        IObjRW* const Follower = It->get();
        if (Follower != nullptr && Follower->get_id() == LeavingObjId)
        {
            It->set(nullptr);
            bFound = true;
        }
    }

    // Targeters by CSimple Id (+0xb0).
    for (std::vector<FSimpleWatcher>::iterator It = Targeters.begin(); It != Targeters.end(); ++It)
    {
        CSimple* const Targeter = It->get();
        if (Targeter != nullptr && Targeter->Id == LeavingObjId)
        {
            It->set(nullptr);
            bFound = true;
        }
    }

    return bFound;
}

Archetype::Ship const* CShip::shiparch() const
{
    // FUN_06262d50 (ICF fold)
    return (Archetype::Ship const*)Archetype;
}

// DAT_0639e574
float const CShip::JETTISON_CLEARANCE_TIME = 4.0f;

// ============================================================================
// CShip::get_renderable_subtarget
// ============================================================================

long CShip::get_renderable_subtarget() const
{
    // FUN_062b2310. Retail fetches cobject() twice; sub_obj_id_to_inst is vtable +0xf8.
    IObjRW* const TargetObj = Target.get();
    if (TargetObj == nullptr || SubTargetIndex == SUBOBJ_ID_NONE)
        return -1;
    CObject const* const Obj = TargetObj->cobject();
    if (Obj == nullptr || (Obj->ObjectClass & CObject::ClassEqObj) != CObject::ClassEqObj)
        return -1;
    CObject const* const EqObj = Target.get()->cobject();
    CEqObj const* const Eq = (EqObj != nullptr && (EqObj->ObjectClass & CObject::ClassEqObj) == CObject::ClassEqObj)
        ? (CEqObj const*)EqObj : nullptr;
    return Eq->sub_obj_id_to_inst(SubTargetIndex);
}
