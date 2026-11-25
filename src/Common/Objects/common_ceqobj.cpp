// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEqObj (retail CEqObj.cpp) and CEqObj::DockAnimInfo.
#include "common_cobject.h"
#include "x86math.h"

#include "common_cobjdb.h"
#include "common_goods.h"
#include "common_physys.h"
#include "common_animdb.h"  // AnimDB::Add/Rem (common_animdb.cpp)
#include "common_iface.h"
#include "common_universe.h"
#include "common_misc.h"    // DamageList, DamageEntry

#include "DALib.h"
#include "x87_trig.h"       // retail's inline FSIN / FCOS (CEqObj::launch_pos)

#include <cmath>
#include <list>
#include <new>      // placement new (get_equip_desc_list)
#include <string.h>

namespace
{
    const char kCEqObjSource[] = "E:\\FL\\Scratch\\Source\\Common\\CEqObj.cpp";

    // 0x1ee0: CAttachedEquip's family (0x1fe0) without EquipTypeShield, the walk mask of the
    // get_total_* family.
    const int kAttachedNoShieldMask = EquipTypeMineDropper | EquipTypeCmDropper | EquipTypeGun |
                                      EquipTypeShieldGen | EquipTypeThruster | EquipTypeCargoPod |
                                      EquipTypeCloak;
    static_assert(kAttachedNoShieldMask == 0x1ee0, "the 0x1ee0 traverser mask");
}

CEqObj::CEqObj(Class ObjectClass)

    : CSimple(ObjectClass)
{
    // FUN_062a9010
    EquipManager.Init(this);
    VibeId = -1;
    // The accessory ids (+0x118..+0x134) are left unwritten, as retail does.
    SpaceCostume.PartIds[0] = 0;
    SpaceCostume.PartIds[1] = 0;
    SpaceCostume.PartIds[2] = 0;
    SpaceCostume.PartIds[3] = 0;
    SpaceCostume.AccessoryCount = 0;
    VoiceId = 0;
    CloakPercent = 0.0f;
    ArchGroups.Init(this);
    bWeaponsOffline = 0;
    DockingBaseId = 0;
    DockingPointBaseId = 0;
    // Only the client connects an INotify listener (Server.dll never calls connect), so a
    // server-side CEqObj keeps it null and notify() is a no-op.
    Notify = nullptr;
    bExplosionDmgCached = false;
    ExplosionDmgRadius = 0.0f;
    ExplosionDmgCenter.x = 0.0f;
    ExplosionDmgCenter.y = 0.0f;
    ExplosionDmgCenter.z = 0.0f;
    ControlExclusion = 0;
    BehaviorMgr = nullptr;
    Power = 0.0f;
    MaxPower = 0.0f;
    BehaviorIndex = -1;
}

// FUN_062872c0. Deviation: a plain member-wise copy. Retail's version has no callers in
// Common.dll, freelancer.exe or Server.dll (exported only because the class is), so its
// exact ArchGroups/EquipManager copying was not reproduced.
CEqObj::CEqObj(CEqObj const& Other)
    : CSimple(Other)
    , EquipManager(Other.EquipManager)
    , VibeId(Other.VibeId)
    , SpaceCostume(Other.SpaceCostume)
    , VoiceId(Other.VoiceId)
    , CloakPercent(Other.CloakPercent)
    , ArchGroups(Other.ArchGroups)
    , bWeaponsOffline(Other.bWeaponsOffline)
    , DockingBaseId(Other.DockingBaseId)
    , DockingPointBaseId(Other.DockingPointBaseId)
    , Notify(Other.Notify)
    , bExplosionDmgCached(Other.bExplosionDmgCached)
    , ExplosionDmgRadius(Other.ExplosionDmgRadius)
    , ExplosionDmgCenter(Other.ExplosionDmgCenter)
    , DockingPoints(Other.DockingPoints)
    , ControlExclusion(Other.ControlExclusion)
    , BehaviorMgr(Other.BehaviorMgr)
    , Power(Other.Power)
    , MaxPower(Other.MaxPower)
    , BehaviorIndex(Other.BehaviorIndex)
{
}

CEqObj::~CEqObj()
{
    // FUN_062a9190
    PhySys::SetCollisionState(this, false);

    CEquipTraverser Trav;
    for (CEquip* Item = EquipManager.Traverse(Trav); Item != nullptr; Item = EquipManager.Traverse(Trav))
        Item->DisableController();   // CEquip vtable +0x50

    if (BehaviorMgr != nullptr)
    {
        delete BehaviorMgr;   // ~IBehaviorManager FUN_062d2530
        BehaviorMgr = nullptr;
    }

    EquipManager.Clear();
    ArchGroups.Clear();
}

void CEqObj::load_equip_and_cargo(EquipDescVector const& Items, bool bClear)
{
    if (bClear)
        clear_equip_and_cargo();

    EquipDesc* It = Items.Begin;
    EquipDesc* End = Items.End;
    for (; It != End; ++It)
        add_item(*It);
}

void CEqObj::clear_equip_and_cargo()
{
    EquipManager.Clear();
}

bool CEqObj::add_item(EquipDesc const& Desc)
{
    if (Desc.bEquipped)
        return add_equipped_item(Desc);
    return add_cargo_item(Desc);
}

bool CEqObj::add_equipped_item(EquipDesc const& Desc)
{
    // FUN_062a9700
    Archetype::Equipment* const Arch = Archetype::GetEquipment(Desc.ArchId);
    if (Arch == nullptr)
    {
        // The object's archetype is read without a null test.
        FDUMP(kFdumpWarning,
              "%s(%d) : *** WARNING: Object(%s) missing equipment archetype(%X)\n",
              kCEqObjSource, 0x127, Archetype->Nickname.get_string(), Desc.ArchId);
        return false;
    }

    CEquip* const Equip = alloc_equip(Desc.Id, Arch, Desc.bMission);   // vtable +0x104
    if (Equip == nullptr)
        return false;

    Equip->SetHitPoints(Desc.Health * Arch->HitPts);
    if (!Desc.is_internal())
    {
        CExternalEquip* const Ext = CExternalEquip::cast(Equip);
        if (Ext != nullptr)
        {
            Ext->Connect(Desc.Hardpoint);                              // vtable +0x5c
        }
        else
        {
            FDUMP(kFdumpWarning,
                  "%s(%d) : *** WARNING: Object(%s) tried to connect internal type(%s) to "
                  "hardpoint(%s). Ignorable.",
                  kCEqObjSource, 0x116, Archetype->Nickname.get_string(),
                  Arch->Nickname.get_string(), Desc.Hardpoint);
        }
    }
    EquipManager.AddNewEquipment(Equip);
    return true;
}

bool CEqObj::add_cargo_item(EquipDesc const& Desc)
{
    // FUN_062a9820
    Archetype::Equipment* const Arch = Archetype::GetEquipment(Desc.ArchId);
    if (Arch == nullptr)
    {
        FDUMP(kFdumpWarning,
              "%s(%d) : *** WARNING: Object(%s) missing equipment archetype(%X)\n",
              kCEqObjSource, 0x158, Archetype->Nickname.get_string(), Desc.ArchId);
        return false;
    }

    // CECargo is 0x2c bytes; Init is vtable +0x5c.
    CECargo* const Cargo = new CECargo(this, Desc.Id, Arch, Desc.bMission);
    Cargo->Init(Desc.Health, (unsigned)Desc.Count, Desc.get_hardpoint());
    EquipManager.AddNewEquipment(Cargo);
    return true;
}

bool CEqObj::sync_cargo(EquipDescList const& List)
{
    // FUN_062a9920. First pass: every existing cargo equip takes Count and HitPoints from the
    // List entry with its Id, or Count 0.
    CEquipTraverser Trav(EquipTypeCargo);
    CEquip* Item = EquipManager.Traverse(Trav);
    while (Item != nullptr)
    {
        EquipDesc const* const Desc = List.find_equipment_item(Item->Id);
        CECargo* const Cargo = (CECargo*)Item;
        if (Desc != nullptr)
        {
            Cargo->SetCount((unsigned)Desc->Count);
            Item->SetHitPoints(Item->GetMaxHitPoints() * Desc->Health);
        }
        else
        {
            Cargo->SetCount(0);
        }
        Item = EquipManager.Traverse(Trav);
    }

    // Second pass: a new CECargo for every non-equipped entry without an equip, as
    // add_cargo_item builds one but with no null test on GetEquipment.
    for (std::list<EquipDesc>::const_iterator It = List.Items.begin(); It != List.Items.end(); ++It)
    {
        EquipDesc const& Desc = *It;
        if (Desc.bEquipped)
            continue;
        if (EquipManager.FindByID(Desc.Id) != nullptr)
            continue;
        Archetype::Equipment* const Arch = Archetype::GetEquipment(Desc.ArchId);
        CECargo* const Cargo = new CECargo(this, Desc.Id, Arch, Desc.bMission);
        Cargo->Init(Desc.Health, (unsigned)Desc.Count, Desc.get_hardpoint());
        EquipManager.AddNewEquipment(Cargo);
    }
    return true;
}

CEquip* CEqObj::alloc_equip(unsigned short EquipId, Archetype::Equipment* Arch,
                            bool bMissionFlag)
{
    // FUN_062a9a90. No null test on Arch.
    int const ClassType = (int)Arch->get_class_type();

    switch (ClassType)
    {
    case Archetype::AClassCargoPod:
        return new CECargoPod(this, EquipId, (Archetype::CargoPod const*)Arch, bMissionFlag);
    case Archetype::AClassPower:
        return new CEPower(this, EquipId, (Archetype::Power const*)Arch, bMissionFlag);
    case Archetype::AClassShieldGenerator:
        return new CEShieldGenerator(this, EquipId, (Archetype::ShieldGenerator const*)Arch, bMissionFlag);
    case Archetype::AClassGun:
        return new CEGun(this, EquipId, (Archetype::Gun const*)Arch, bMissionFlag);
    case Archetype::AClassMineDropper:
        return new CEMineDropper(this, EquipId, (Archetype::MineDropper const*)Arch, bMissionFlag);
    case Archetype::AClassCounterMeasureDropper:
        return new CECounterMeasureDropper(this, EquipId, (Archetype::CounterMeasureDropper const*)Arch, bMissionFlag);
    case Archetype::AClassScanner:
        return new CEScanner(this, EquipId, (Archetype::Scanner const*)Arch, bMissionFlag);
    case Archetype::AClassLight:
        return new CELightEquip(this, EquipId, (Archetype::Light const*)Arch, bMissionFlag);
    case Archetype::AClassAttachedFX:
        return new CEAttachedFX(this, EquipId, (Archetype::AttachedFXEquip const*)Arch, bMissionFlag);
    case Archetype::AClassInternalFX:
        return new CEInternalFX(this, EquipId, (Archetype::InternalFXEquip const*)Arch, bMissionFlag);
    case Archetype::AClassRepairDroid:
        return new CERepairDroid(this, EquipId, (Archetype::RepairDroid const*)Arch, bMissionFlag);
    case Archetype::AClassCloakingDevice:
        return new CECloakingDevice(this, EquipId, (Archetype::CloakingDevice const*)Arch, bMissionFlag);
    case Archetype::AClassArmor:
        return new CEArmor(this, EquipId, (Archetype::Armor const*)Arch, bMissionFlag);
    default:
        break;
    }

    FDUMP(kFdumpWarning,
          "%s(%d) : *** WARNING: alloc_equip: Archetype[%s] not supported/equippable!",
          kCEqObjSource, 0x1cf, Arch->Nickname.get_string());
    return nullptr;
}

void CEqObj::link_shields()
{
    // FUN_062abbe0. Shield links at Archetype+0x68/+0x6c; no null test on Archetype.
    Archetype::EqObj const* const Eq = eqobjarch();
    std::vector<Archetype::ShieldLink> const& Links = *Eq->ShieldLinks;
    for (std::vector<Archetype::ShieldLink>::const_iterator LinkIt = Links.begin(); LinkIt != Links.end(); ++LinkIt)
    {
        Archetype::ShieldLink const& Link = *LinkIt;
        CEShield* Shield = nullptr;
        for (std::vector<CacheString>::const_iterator HpIt = Link.GeneratorHps.begin();
             HpIt != Link.GeneratorHps.end(); ++HpIt)
        {
            CEShieldGenerator* const Gen = CEShieldGenerator::cast(EquipManager.FindByHardpoint(*HpIt));
            if (Gen == nullptr || Gen->LinkedShield != nullptr)        // +0x4c
                continue;
            if (Shield == nullptr)
            {
                Archetype::Equipment* const ShieldArch = Archetype::GetEquipmentByName(Link.ShieldArch);
                if (ShieldArch == nullptr)
                {
                    // The whole function returns.
                    FDUMP(kFdumpError,
                          "%s(%d) : *** ERROR: Failed to create shield(%s) on hardpoint(%s). "
                          "(Ignorable)",
                          kCEqObjSource, 0x579, Link.ShieldArch.get_string(), Link.Hardpoint.Value);
                    return;
                }
                Shield = new CEShield(this, Link.EquipId, (Archetype::Shield const*)ShieldArch, false);
                Shield->Connect(Link.Hardpoint.Value);
                EquipManager.AddNewEquipment(Shield);
            }
            Gen->LinkShield(Shield);
        }
    }
}

void CEqObj::load_arch_groups(std::list<CollisionGroupDesc> const& Descs)
{
    // FUN_062a9420. Neither the archetype nor get_group_by_id's result is tested. CArchGroup is
    // 0x34 bytes; only an exact 0.0 health kills the group.
    Archetype::EqObj* const Eq = eqobjarch();
    for (std::list<CollisionGroupDesc>::const_iterator It = Descs.begin(); It != Descs.end(); ++It)
    {
        Archetype::CollisionGroup const* const GroupArch = Eq->get_group_by_id(It->Id);
        CArchGroup* const Group = new CArchGroup(*GroupArch, this);
        Group->SetRelativeHealth(It->Health);
        if (It->Health == 0.0f)
            Group->Kill(DamageEntry::SubObjFate::HealthProxy);
        ArchGroups.AddNewArchGroup(Group);
    }
    ArchGroups.DestroyDeadGroups();
}

void CEqObj::clear_arch_groups()
{
    // FUN_062a94f0
    ArchGroups.Clear();
}

void CEqObj::get_collision_group_description(std::list<CollisionGroupDesc>& OutDescs) const
{
    // FUN_062a9500
    CArchGrpTraverser Traverser;
    for (CArchGroup const* Group = ArchGroups.Traverse(Traverser); Group != nullptr;
         Group = ArchGroups.Traverse(Traverser))
    {
        CollisionGroupDesc Desc;
        Group->GetCollisionGroupDesc(Desc);
        OutDescs.push_back(Desc);
    }
}

void CEqObj::get_equip_desc_list(EquipDescVector& OutList) const
{
    // FUN_062a9f00. One descriptor is reused for every equip.
    EquipDesc Desc;

    // Inlined VC6 reserve(size() + ExternalCount (+0xf0) + InternalCount (+0xfc)), to exactly
    // that many elements.
    unsigned const OldSize = (OutList.Begin != nullptr) ? (unsigned)(OutList.End - OutList.Begin) : 0;
    unsigned const Wanted = EquipManager.InternalCount + EquipManager.ExternalCount + OldSize;
    unsigned const Capacity = (OutList.Begin != nullptr) ? (unsigned)(OutList.Cap - OutList.Begin) : 0;
    if (Capacity < Wanted)
    {
        int AllocCount = (int)Wanted;
        if (AllocCount < 0) // the allocator's own clamp
            AllocCount = 0;
        EquipDesc* const NewBuf = (EquipDesc*)::operator new(AllocCount * sizeof(EquipDesc));
        EquipDesc* Dst = NewBuf;
        for (EquipDesc const* Src = OutList.Begin; Src != OutList.End; ++Src, ++Dst)
            new (Dst) EquipDesc(*Src);
        ::operator delete(OutList.Begin);
        OutList.Cap = NewBuf + Wanted;
        OutList.End = NewBuf + ((OutList.Begin != nullptr) ? (OutList.End - OutList.Begin) : 0);
        OutList.Begin = NewBuf;
    }

    // 062a9fce-062aa17d: default traverser over every equip; vt+0x28 GetEquipDesc(Desc) true
    // -> the inlined VC6 insert(end(), 1, Desc).
    CEquipManager& MutableEquipManager = const_cast<CEquipManager&>(EquipManager);
    CEquipTraverser Walk;
    for (CEquip const* Eq = MutableEquipManager.Traverse(Walk); Eq != nullptr;
         Eq = MutableEquipManager.Traverse(Walk))
    {
        if (!Eq->GetEquipDesc(Desc))
            continue;
        if ((unsigned)(OutList.Cap - OutList.End) < 1)
        {
            // VC6 growth: size() + max(size(), 1).
            unsigned const Size = (OutList.Begin != nullptr) ? (unsigned)(OutList.End - OutList.Begin) : 0;
            unsigned const Grown = Size + (Size > 1 ? Size : 1);
            int AllocCount = (int)Grown;
            if (AllocCount < 0)
                AllocCount = 0;
            EquipDesc* const NewBuf = (EquipDesc*)::operator new(AllocCount * sizeof(EquipDesc));
            EquipDesc* Dst = NewBuf;
            for (EquipDesc const* Src = OutList.Begin; Src != OutList.End; ++Src, ++Dst)
                new (Dst) EquipDesc(*Src);
            new (Dst) EquipDesc(Desc);
            ::operator delete(OutList.Begin);
            OutList.Cap = NewBuf + Grown;
            OutList.End = NewBuf + (Size + 1);
            OutList.Begin = NewBuf;
        }
        else
        {
            new (OutList.End) EquipDesc(Desc);
            ++OutList.End;
        }
    }
}

void CEqObj::init_docking_points(unsigned int BaseId)
{
    // FUN_062aadc0
    DockingPointBaseId = BaseId;                                       // +0x164

    // The vector is not cleared: entries are appended. No null test on the archetype.
    Archetype::EqObj const* const Arch = eqobjarch();
    DockingPoints.reserve(Arch->DockingSpheres.size());

    // A non-empty animation name is started with IAnimation2 slot 9 PlayScript; an empty one
    // gives channel -1.
    for (std::vector<Archetype::EqObj::DockingSphere>::const_iterator It = Arch->DockingSpheres.begin();
         It != Arch->DockingSpheres.end(); ++It)
    {
        Archetype::EqObj::DockingSphere const& Sphere = *It;
        int AnimChannel = -1;
        if (Sphere.AnimName.c_str()[0] != '\0')
        {
            AnimChannel = DALib::Anim->PlayScript(AsOpaqueDword(Arch->get_script_index()),
                                                 AsOpaqueDword(this->EngineInstanceId),
                                                 Sphere.AnimName.c_str(), nullptr, nullptr);
            if (AnimChannel == -1)
            {
                FDUMP(kFdumpError,
                      "%s(%d) : *** ERROR: failed to find animation (%s) in object (%s)\n",
                      "E:\\FL\\Scratch\\Source\\Common\\CEqObj.cpp",
                      0x359,
                      Sphere.AnimName.c_str(),
                      this->Archetype->Nickname.get_string());
            }
        }

        DockAnimInfo Entry;
        Entry.bActive = false;
        Entry.AnimChannel = AnimChannel;
        Entry.LastDuration = 0.0f;
        Entry.ArchPoint = &Sphere;
        DockingPoints.push_back(Entry);
    }
}

void CEqObj::init(CreateParms const& Parms)
{
    CSimple::init(Parms);

    VibeId = (int)Parms.VibeId;
    SpaceCostume.PartIds[0] = Parms.SpaceCostume.PartIds[0];
    SpaceCostume.PartIds[1] = Parms.SpaceCostume.PartIds[1];
    SpaceCostume.PartIds[2] = Parms.SpaceCostume.PartIds[2];
    SpaceCostume.PartIds[3] = Parms.SpaceCostume.PartIds[3];

    // No Vibe::Alloc here; player/NPC Alloc is a separate Common helper.

    // Deviation: retail stores the count (+0x138) unclamped and copies without a bound; a count
    // of 9 or 10 rewrites +0x138 / +0x13c with the same values, anything larger copies the
    // creation parameters' containers over CEqObj fields. The port stops at the 8-entry array.
    SpaceCostume.AccessoryCount = Parms.SpaceCostume.AccessoryCount;
    int const AccessoryCount = (int)SpaceCostume.AccessoryCount;
    for (int AccessoryIndex = 0; AccessoryIndex < AccessoryCount && AccessoryIndex < 8; ++AccessoryIndex)
        SpaceCostume.AccessoryIds[AccessoryIndex] = Parms.SpaceCostume.AccessoryIds[AccessoryIndex];

    VoiceId = Parms.VoiceId;                          // Parms +0x98 -> +0x13c

    load_arch_groups(Parms.ArchGroupDescs);           // Parms +0xac

    load_equip_and_cargo(Parms.Equipment, true);      // virtual +0xa4

    if (EquipManager.FindFirst(EquipTypeShieldGen) != nullptr)
        link_shields();

    // MaxPower +0x19c, Power +0x198. No cast and no class test on the power equips.
    MaxPower = 0.0f;
    CEquipTraverser PowerWalk(EquipTypePower);
    for (CEquip* PowerEquip = EquipManager.Traverse(PowerWalk); PowerEquip != nullptr;
         PowerEquip = EquipManager.Traverse(PowerWalk))
    {
        float const Add = PowerEquip->IsDestroyed()
                              ? 0.0f
                              : ((Archetype::Power const*)PowerEquip->Arch)->Capacity;
        MaxPower = Add + MaxPower;
    }
    Power = MaxPower;
}

CEqObj::DockAnimInfo::DockAnimInfo()
{
    // FUN_06287270. Padding bytes +1..+3 untouched.
    bActive = false;
    AnimChannel = -1;
    LastDuration = 0.0f;
    ArchPoint = nullptr;
}

bool CEqObj::is_targetable() const
{
    // FUN_06287240. CloakPercent (+0x140) NaN counts as targetable.
    if ((FlagsA0 & 1) == 0)
        return false;
    return !(CloakPercent >= 1.0f);
}

Archetype::EqObj* CEqObj::eqobjarch() const
{
    // FUN_06262d50 (ICF fold)
    return (Archetype::EqObj*)Archetype;
}

bool CEqObj::is_damaged_obj_attached(CacheString const& Name) const
{
    // FUN_062a9590. The set at +0xbc, keyed by the Value pointer.
    return AttachedDamageObjs.find(Name) != AttachedDamageObjs.end();
}

void CEqObj::attaching_damaged_obj(CacheString const& Name)
{
    // FUN_062a95f0
    AttachedDamageObjs.insert(Name);
}

unsigned CEqObj::get_name() const
{
    // FUN_062aa420. The Vibe name (VibeId +0x104), not an archetype lookup; returns the first
    // FmtStr's mad-lib id. Same call as CShip::get_pilot_name.
    FmtStr NameFmtA;
    FmtStr NameFmtB;
    wchar_t const* WideName = nullptr;
    Reputation::Vibe::GetName(VibeId, NameFmtA, NameFmtB, WideName);
    return NameFmtA.MadLibId;
}

float CEqObj::get_cloak_percentage() const
{
    return CloakPercent;
}

int CEqObj::is_cloaked() const
{
    // CloakPercent is a 0..1 fade fraction; 1.0 is fully cloaked (same threshold as
    // CELauncher::get_fire_error).
    return (CloakPercent >= 1.0f) ? 1 : 0;
}

float CEqObj::attitude_towards(CEqObj const* Other) const
{
    // FUN_062aa640. No null test on Other; 0.0f when the reputation call fails.
    float Attitude = 0.0f;
    int const Result = Reputation::Vibe::AttitudeTowards(this->VibeId, Other->VibeId, Attitude);
    if (Result != 0)
        return 0.0f;
    return Attitude;
}

void CEqObj::attitude_towards_symmetrical(float& OutAtt, CEqObj const* Other,
                                          float& OutReverse) const
{
    // FUN_062aa600. No null test on Other; both outputs are zeroed only when the call fails.
    int const Result = Reputation::Vibe::AttitudeTowardsSymmetrical(
        this->VibeId, Other->VibeId, OutAtt, OutReverse);
    if (Result != 0)
    {
        OutAtt = 0.0f;
        OutReverse = 0.0f;
    }
}

void CEqObj::disconnect(IObjRW* Watcher)
{
    // FUN_062aa3d0. Deletes the behavior manager (+0x194) when its +0xF0 watcher holds Watcher.
    if (BehaviorMgr == nullptr)
        return;
    if (ObjRwFromWatchable(BehaviorMgr->WatchablePtr) != Watcher)
        return;
    delete BehaviorMgr;
    BehaviorMgr = nullptr;
}

void CEqObj::unmake_physical()
{
    // FUN_062aa2f0. The shield walk stops at the first non-shield.
    CEquipTraverser Trav(EquipTypeShield);
    for (CEShield* Shield = CEShield::cast(EquipManager.Traverse(Trav)); Shield != nullptr;
         Shield = CEShield::cast(EquipManager.Traverse(Trav)))
    {
        Shield->DestroyIntruderChecker();
    }
    CSimple::unmake_physical();
}

void CEqObj::remake_physical(PhySys::CreateParms const& Parms, float Radius)
{
    // FUN_062aa360
    CObject::remake_physical(Parms, Radius);
    CEquipTraverser Trav(EquipTypeShield);
    for (CEShield* Shield = CEShield::cast(EquipManager.Traverse(Trav)); Shield != nullptr;
         Shield = CEShield::cast(EquipManager.Traverse(Trav)))
    {
        Shield->AttachIntruderChecker();
    }
}

int CEqObj::update(float Dt, unsigned Flags)
{
    // FUN_062aa520
    CObject::update(Dt, Flags);

    // Unordered health does not count as destroyed.
    bool bWeaponsOfflineDestroyed = false;
    if (bWeaponsOffline != 0)
    {
        double const RelHealth = CSimple::get_relative_health();
        bWeaponsOfflineDestroyed = (RelHealth <= 0.0);
    }

    // Mask 0xff3efffc. Update is vtable +0x24.
    CEquipTraverser Trav(-0xc10004);
    CEquip* Item = EquipManager.Traverse(Trav);
    while (Item != nullptr)
    {
        Item->Update(Dt, Flags);
        Item = EquipManager.Traverse(Trav);
    }

    // FUN_062d2b10. Retail brackets it with two calls to 062a8920, a bare RET.
    if (BehaviorMgr != nullptr && !bWeaponsOfflineDestroyed && (Flags & NO_BEHAVIOR) == 0 &&
        (ControlExclusion & 1) == 0)
    {
        BehaviorMgr->UpdateBehaviorTick(Dt);
    }

    update_docking_animations(Dt);
    return 0;
}

ObjActivateResult CEqObj::activate(bool bOn, unsigned DockIndex)
{
    // FUN_062ab130. Retail bounds DockIndex against the archetype's docking spheres
    // (Archetype+0x84), not against DockingPoints (+0x184), which init_docking_points builds
    // 1:1 from them.
    if (DockIndex == 0xffffffffu)
        return ObjActivateFailed;
    Archetype::EqObj const* Arch = eqobjarch();
    if (DockIndex >= Arch->DockingSpheres.size())
        return ObjActivateFailed;
    bool WasOn = DockingPoints[DockIndex].bActive;
    DockingPoints[DockIndex].bActive = bOn;
    return (bOn == WasOn) ? ObjActivateNoChange : ObjActivateChanged;
}

bool CEqObj::get_activate_state(std::vector<bool>& OutStates)
{
    // FUN_062ab190. Appends every docking point's bActive; true if any is active. Retail
    // inlines vector<bool>'s push_back.
    bool bAnyActive = false;
    for (DockAnimInfo const& Entry : DockingPoints)
    {
        bool bActive = Entry.bActive;
        OutStates.push_back(bActive);
        if (bActive)
            bAnyActive = true;
    }
    return bAnyActive;
}

bool CEqObj::get_sub_obj_velocity(unsigned short SubObjId, Vector& Out) const
{
    // FUN_062ab330. Out is the object's own velocity (vtable +0x54) on every path; a
    // sub-object's velocity replaces it only on success.
    Out = get_velocity();
    Vector Local;
    if (SubObjectID::IsEquipID(SubObjId))
    {
        CEquip const* Eq = EquipManager.FindByID(SubObjId);
        if (Eq == nullptr)
            return false;
        CExternalEquip const* External = CExternalEquip::cast(Eq);
        if (External == nullptr)
            return false;
        if (!External->GetVelocity(Local))                       // vtable +0x6c
            return false;
    }
    else
    {
        if (!SubObjectID::IsArchGroupID(SubObjId))
            return false;
        CArchGroup* Group = const_cast<CArchGroupManager&>(ArchGroups).FindByID(SubObjId);
        if (Group == nullptr)
            return false;
        if (!Group->GetVelocity(Local))
            return false;
    }
    Out = Local;
    return true;
}

bool CEqObj::get_sub_obj_center_of_mass(unsigned short SubObjId, Vector& Out) const
{
    // FUN_062ab3f0. CExternalEquip::GetCenterOfMass is vtable +0x70. Out is left untouched on
    // every failure path.
    if (SubObjectID::IsEquipID(SubObjId))
    {
        CEquip const* Eq = EquipManager.FindByID(SubObjId);
        if (Eq == nullptr)
            return false;
        CExternalEquip* External = CExternalEquip::cast(const_cast<CEquip*>(Eq));
        if (External == nullptr)
            return false;
        Vector Local{ 0.0f, 0.0f, 0.0f };
        if (!External->GetCenterOfMass(Local))
            return false;
        Out = Local;
        return true;
    }
    if (SubObjectID::IsArchGroupID(SubObjId))
    {
        CArchGroup* Group = const_cast<CArchGroupManager&>(ArchGroups).FindByID(SubObjId);
        if (Group == nullptr)
            return false;
        Vector Local{ 0.0f, 0.0f, 0.0f };
        if (!Group->GetCenterOfMass(Local))
            return false;
        Out = Local;
        return true;
    }
    return false;
}

bool CEqObj::get_sub_obj_max_hit_pts(unsigned short SubObjId, float& Out) const
{
    // FUN_062ab820
    Out = 0.0f;
    if (SubObjectID::IsEquipID(SubObjId))
    {
        CEquip const* Eq = EquipManager.FindByID(SubObjId);
        if (Eq == nullptr)
            return false;
        Out = Eq->GetMaxHitPoints();
        return true;
    }
    if (SubObjectID::IsArchGroupID(SubObjId))
    {
        CArchGroup* Group = const_cast<CArchGroupManager&>(ArchGroups).FindByID(SubObjId);
        if (Group == nullptr)
            return false;
        Out = (float)Group->GetMaxHitPoints();
        return true;
    }
    return false;
}

bool CEqObj::get_sub_obj_relative_health(unsigned short SubObjId, float& Out) const
{
    // FUN_062ab8a0
    Out = 0.0f;
    if (SubObjectID::IsEquipID(SubObjId))
    {
        CEquip const* Eq = EquipManager.FindByID(SubObjId);
        if (Eq == nullptr)
            return false;
        Out = (float)Eq->GetRelativeHealth();
        return true;
    }
    if (SubObjectID::IsArchGroupID(SubObjId))
    {
        CArchGroup* Group = const_cast<CArchGroupManager&>(ArchGroups).FindByID(SubObjId);
        if (Group == nullptr)
            return false;
        Out = (float)Group->GetRelativeHealth();
        return true;
    }
    if (SubObjId == 1)
    {
        Out = (float)CSimple::get_relative_health();
        return true;
    }
    return false;
}

long CEqObj::sub_obj_id_to_inst(unsigned short SubObjId) const
{
    // FUN_062ab990
    if (SubObjectID::IsEquipID(SubObjId))
    {
        CEquip const* Eq = EquipManager.FindByID(SubObjId);
        if (Eq == nullptr)
            return -1;
        CAttachedEquip* const Attached = CAttachedEquip::cast(const_cast<CEquip*>(Eq));
        if (Attached == nullptr)
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kCEqObjSource, 0x4ee,
                  "Tried to get inst for non-attached equipment.");
            return -1;
        }
        // vtable +0x84 (slot 0639c6a0)
        return Attached->GetRootIndex();
    }
    if (SubObjectID::IsArchGroupID(SubObjId))
    {
        CArchGroup* Group = const_cast<CArchGroupManager&>(ArchGroups).FindByID(SubObjId);
        if (Group == nullptr)
            return -1;
        return Group->GetRootIndex();
    }
    if (SubObjId == 1)
        return EngineInstanceId;
    return -1;
}

IBehaviorManager* CEqObj::create_behavior_interface(IObjRW* Owner, int GraphId)
{
    // FUN_062ac240. operator_new(0x1cc) + ctor FUN_062d2220 when +0x194 is null; then
    // FUN_062d2830(ObjectDb +0xAC) and set_state_graph on both paths.
    if (BehaviorMgr == nullptr)
        BehaviorMgr = new IBehaviorManager(Owner);
    ConnectBehaviorManagerObjectDb(BehaviorMgr, ObjectDb);
    BehaviorMgr->set_state_graph(GraphId, false);
    return BehaviorMgr;
}

void CEqObj::enable_controllers()
{
    // FUN_062aa250. Retail calls vtable +0x4c (EnableController) on every equip. The port's
    // CEquip declaration order differs from retail's vtable, so the named virtual is called.
    CEquipTraverser Trav; // all types
    for (CEquip* Eq = EquipManager.Traverse(Trav); Eq != nullptr; Eq = EquipManager.Traverse(Trav))
        Eq->EnableController();
}

void CEqObj::disable_controllers()
{
    // FUN_062aa2a0. Retail vtable +0x50; see enable_controllers.
    CEquipTraverser Trav;
    for (CEquip* Eq = EquipManager.Traverse(Trav); Eq != nullptr; Eq = EquipManager.Traverse(Trav))
        Eq->DisableController();
}

bool CEqObj::launch_pos(Vector& OutPos, Matrix& OutOrient, int HardpointIndex) const
{
    // FUN_062aa980 (traced from the listing). HardpointIndex selects one of the archetype's
    // docking spheres (Archetype+0x88/+0x8c, 0x10-byte stride, name at +4; no null test on the
    // archetype); an invalid index takes the fallback below.
    Archetype::EqObj* Arch = eqobjarch();
    if (HardpointIndex != -1 && (unsigned)HardpointIndex < (unsigned)Arch->DockingSpheres.size())
    {
        char const* HpName = Arch->DockingSpheres[(unsigned)HardpointIndex].HardpointName.c_str();
        HardpointPosition(this->EngineInstanceId, HpName, &OutPos, &OutOrient);

        // A quaternion about column 1 of the hardpoint orientation by A = DAT_0639e2d8 (float
        // pi/2 as a double). The scale is rounded to float for FUN_06288830 (Vector * float).
        // No zero-length test.
        double const kSpreadAngle = 1.5707963705062866;
        Vector const Col1{ OutOrient.E[0][1], OutOrient.E[1][1], OutOrient.E[2][1] };
        float const W = X87CosF(kSpreadAngle);
        double const Len = sqrt(((double)Col1.z * Col1.z + (double)Col1.x * Col1.x) + (double)Col1.y * Col1.y);
        float const Scale = (float)(X87Sin(kSpreadAngle) / Len);
        struct FQuat
        {
            float W, X, Y, Z;
        };
        FQuat const Quat{ W, Scale * Col1.x, Scale * Col1.y, Scale * Col1.z };

        // QuaternionToMatrix +0x54, MulMatrix +0x20; no null test on the engine.
        I3DMathEngine* Math = Acquire3DMathEngine();
        Matrix SpreadMatrix;
        Math->QuaternionToMatrix((FMatrix&)SpreadMatrix, (FQuaternion const&)Quat);
        Matrix Product;
        Math->MulMatrix((FMatrix&)Product, (FMatrix const&)SpreadMatrix, (FMatrix const&)OutOrient);
        OutOrient = Product;
        return true;
    }

    // Fallback: the object's own pose, rows 0 and 2 rotated by A = DAT_0639e2d0 (float pi as a
    // double); each element is rounded once.
    OutPos = this->WorldPosition;
    OutOrient = this->Orientation;

    double const kFallbackAngle = 3.1415927410125732;
    double const CosA = X87Cos(kFallbackAngle);
    double const SinA = X87Sin(kFallbackAngle);
    for (int Col = 0; Col < 3; ++Col)
    {
        double const Row0 = OutOrient.E[0][Col];
        double const Row2 = OutOrient.E[2][Col];
        OutOrient.E[0][Col] = (float)(SinA * Row2 + Row0 * CosA);
        OutOrient.E[2][Col] = (float)(CosA * Row2 - Row0 * SinA);
    }
    return false;
}

bool CEqObj::get_explosion_dmg_bounding_sphere(float& OutRadius, Vector& OutCenter) const
{
    // FUN_062abd50. The sphere is cached in object space on first use; the centre is
    // transformed by the current pose (this+8 as a Transform) on every call.
    if (!bExplosionDmgCached)
    {
        bExplosionDmgCached = true;
        compute_explosion_dmg_bounding_sphere(ExplosionDmgRadius, ExplosionDmgCenter);
    }
    OutRadius = ExplosionDmgRadius;

    // Math engine slot +0x28, never null-tested.
    Vector World;
    Acquire3DMathEngine()->TransformXform((FVector&)World, (FTransform const&)Orientation,
                                          (FVector const&)ExplosionDmgCenter);
    OutCenter = World;
    return true;
}

void CEqObj::compute_explosion_dmg_bounding_sphere(float& OutRadius, Vector& OutCenter) const
{
    // FUN_062abde0. Same scheme as CArchGroup::GetBoundingSphere, over the root group's
    // instances. Gathered in world space; only the final centre goes back to object space.
    // No null tests (DALib::Engine, the math engine).
    struct FChildSphere
    {
        Vector Center;   // +0x00 world centre of a root-group child
        float Radius;    // +0x0c its extent
    };

    I3DMathEngine* const Math = Acquire3DMathEngine();     // DAT_063fc2e4 cache
    FTransform const& Xform = (FTransform const&)Orientation;   // this+0x08
    void* const Instance = (void*)EngineInstanceId;

    // IEngine +0x88 extent; the AABB starts as the root sphere.
    float RootRadius;
    Vector RootLocal;
    DALib::Engine->GetInstanceExtent(Instance, 1, &RootRadius, (float*)&RootLocal);
    Vector RootWorld;
    Math->TransformXform((FVector&)RootWorld, Xform, (FVector const&)RootLocal);
    float MinX = RootWorld.x - RootRadius;
    float MaxX = RootWorld.x + RootRadius;
    float MinY = RootWorld.y - RootRadius;
    float MaxY = RootWorld.y + RootRadius;
    float MinZ = RootWorld.z - RootRadius;
    float MaxZ = RootWorld.z + RootRadius;

    // Every related instance (IEngine +0xd8) in the root group. Bounds fold on the unrounded
    // value; equal or NaN replaces.
    std::vector<FChildSphere> Children;
    for (long Child = (long)DALib::Engine->GetRelatedInstance(Instance, 0, G_InvalidEngineHandle);
         Child != -1;
         Child = (long)DALib::Engine->GetRelatedInstance(Instance, 0, (void*)Child))
    {
        if (inst_to_subobj_id(Child) != SUBOBJ_ID_ROOT)
            continue;

        float ChildRadius;
        Vector ChildLocal;
        DALib::Engine->GetInstanceExtent((void*)Child, 1, &ChildRadius, (float*)&ChildLocal);
        FTransform const& ChildXform =
            *(FTransform const*)DALib::Engine->GetInstanceTransform((void*)Child);
        FChildSphere Sphere;
        Math->TransformXform((FVector&)Sphere.Center, ChildXform, (FVector const&)ChildLocal);
        Sphere.Radius = ChildRadius;
        Children.push_back(Sphere);

        double const LowX = (double)(Sphere.Center.x) - Sphere.Radius;
        if (!(MinX < LowX))
            MinX = (float)(LowX);
        double const HighX = (double)(Sphere.Center.x) + Sphere.Radius;
        if (!(MaxX > HighX))
            MaxX = (float)(HighX);
        double const LowY = (double)(Sphere.Center.y) - Sphere.Radius;
        if (!(MinY < LowY))
            MinY = (float)(LowY);
        double const HighY = (double)(Sphere.Center.y) + Sphere.Radius;
        if (!(MaxY > HighY))
            MaxY = (float)(HighY);
        double const LowZ = (double)(Sphere.Center.z) - Sphere.Radius;
        if (!(MinZ < LowZ))
            MinZ = (float)(LowZ);
        double const HighZ = (double)(Sphere.Center.z) + Sphere.Radius;
        if (!(MaxZ > HighZ))
            MaxZ = (float)(HighZ);
    }

    // The x and y sums stay unrounded, the z sum is rounded first.
    double const SumX = (double)(MaxX) + MinX;
    double const SumY = (double)(MaxY) + MinY;
    float const SumZ = (float)((double)(MaxZ) + MinZ);
    OutCenter.x = (float)(SumX * 0.5f);
    OutCenter.y = (float)(SumY * 0.5f);
    OutCenter.z = (float)((double)(SumZ) * 0.5f);

    // Widest span: wx unrounded, wy and wz rounded; NaN takes the right-hand value.
    double const WidthX = (double)(MaxX) - MinX;
    float const WidthY = (float)((double)(MaxY) - MinY);
    float const WidthZ = (float)((double)(MaxZ) - MinZ);
    double const BestXY = (WidthX > WidthY) ? WidthX : (double)(WidthY);
    double const Widest = (BestXY > WidthZ) ? BestXY : (double)(WidthZ);
    double const Half = Widest * 0.5f;
    OutRadius = (float)(Half);

    // Grow to cover the root sphere, then every child sphere.
    {
        double const Dx = (double)(OutCenter.x) - RootWorld.x;
        double const Dy = (double)(OutCenter.y) - RootWorld.y;
        double const Dz = (double)(OutCenter.z) - RootWorld.z;
        double const Reach = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx) + RootRadius;
        if (Reach > Half)
            OutRadius = (float)(Reach);
    }

    for (std::vector<FChildSphere>::const_iterator It = Children.begin(); It != Children.end(); ++It)
    {
        double const Dx = (double)(It->Center.x) - OutCenter.x;
        double const Dy = (double)(It->Center.y) - OutCenter.y;
        double const Dz = (double)(It->Center.z) - OutCenter.z;
        double const Reach = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx) + It->Radius;
        if (Reach > OutRadius)
            OutRadius = (float)(Reach);
    }

    // InverseTransform (+0x3c) back to object space.
    Vector LocalCenter;
    Math->InverseTransform((FVector&)LocalCenter, Xform, (FVector const&)OutCenter);
    OutCenter = LocalCenter;
}

IBehaviorManager* CEqObj::get_behavior_interface()
{
    // FUN_062ac230
    return BehaviorMgr;
}

void CEqObj::set_control_exclusion(unsigned int Flags)
{
    // FUN_062ac2e0
    ControlExclusion = Flags;
}

bool CEqObj::is_control_excluded(unsigned int Flags) const
{
    return (ControlExclusion & Flags) != 0;
}

unsigned CEqObj::get_base() const
{
    return DockingBaseId;
}

bool CEqObj::is_base() const
{
    // FUN_06287670 (+0x160)
    return DockingBaseId != 0;
}

unsigned int const& CEqObj::get_dock_target() const
{
    return DockingPointBaseId;
}

bool CEqObj::is_dock() const
{
    // FUN_06287680 (+0x164)
    return DockingPointBaseId != 0;
}

float CEqObj::get_power() const
{
    return Power;
}

// FUN_062876c0
void CEqObj::set_power(float NewPower)
{
    Power = NewPower;
}

float CEqObj::get_max_power() const
{
    return MaxPower;
}

float CEqObj::get_power_ratio() const
{
    // FUN_062876e0
    if (MaxPower <= 0.0f)
        return 0.0f;
    return Power / MaxPower;
}

int CEqObj::get_vibe() const
{
    return VibeId;
}

int CEqObj::enumerate_sub_objs() const
{
    // FUN_062aba80. Reads +0x158 (ArchGroups.Count), +0xfc (InternalCount), +0xf0
    // (ExternalCount).
    return (int)ArchGroups.Count + (int)EquipManager.InternalCount + (int)EquipManager.ExternalCount;
}

unsigned CEqObj::get_base_name() const
{
    // FUN_062ab100. The base's strid_name (IBase slot 2), not its nickname id; no null test on
    // get_base.
    if ((DockingBaseId & 0x3fffffff) == 0)
        return 0;
    return (unsigned)Universe::get_base(DockingBaseId)->GetStridName();
}

float CEqObj::get_total_hit_pts() const
{
    // FUN_062ab4c0. Own hit points + the attached equipment but the shield + living arch groups.
    float Total = get_hit_pts();

    CEquipManager& MutableEquipManager = const_cast<CEquipManager&>(EquipManager);
    CEquipTraverser Trav(kAttachedNoShieldMask);
    for (CEquip const* Eq = MutableEquipManager.Traverse(Trav); Eq != nullptr; Eq = MutableEquipManager.Traverse(Trav))
        Total += Eq->GetHitPoints();

    CArchGrpTraverser GroupTrav;
    for (CArchGroup const* Group = ArchGroups.Traverse(GroupTrav); Group != nullptr;
         Group = ArchGroups.Traverse(GroupTrav))
    {
        if (Group->State != CArchGroup::STATE_KILLED && Group->State != CArchGroup::STATE_DESTROYED)
            Total += Group->GetHitPoints();
    }
    return Total;
}

float CEqObj::get_total_max_hit_pts() const
{
    // FUN_062ab570
    float Total = get_max_hit_pts();

    CEquipManager& MutableEquipManager = const_cast<CEquipManager&>(EquipManager);
    CEquipTraverser Trav(kAttachedNoShieldMask);
    for (CEquip const* Eq = MutableEquipManager.Traverse(Trav); Eq != nullptr; Eq = MutableEquipManager.Traverse(Trav))
        Total += Eq->GetMaxHitPoints();

    CArchGrpTraverser GroupTrav;
    for (CArchGroup const* Group = ArchGroups.Traverse(GroupTrav); Group != nullptr;
         Group = ArchGroups.Traverse(GroupTrav))
    {
        if (Group->State != CArchGroup::STATE_KILLED && Group->State != CArchGroup::STATE_DESTROYED)
            Total = (float)(Total + Group->GetMaxHitPoints());
    }
    return Total;
}

float CEqObj::get_total_relative_health() const
{
    // FUN_062ab620. A NaN Max still divides; the quotient is returned in ST0.
    float Hit = get_hit_pts();
    float Max = get_max_hit_pts();

    CEquipManager& MutableEquipManager = const_cast<CEquipManager&>(EquipManager);
    CEquipTraverser Trav(kAttachedNoShieldMask);
    for (CEquip const* Eq = MutableEquipManager.Traverse(Trav); Eq != nullptr; Eq = MutableEquipManager.Traverse(Trav))
    {
        Hit += Eq->GetHitPoints();
        Max += Eq->GetMaxHitPoints();
    }

    CArchGrpTraverser GroupTrav;
    for (CArchGroup const* Group = ArchGroups.Traverse(GroupTrav); Group != nullptr;
         Group = ArchGroups.Traverse(GroupTrav))
    {
        if (Group->State != CArchGroup::STATE_KILLED && Group->State != CArchGroup::STATE_DESTROYED)
        {
            Hit += Group->GetHitPoints();
            Max = (float)(Max + Group->GetMaxHitPoints());
        }
    }
    if (Max < 0.0f || Max == 0.0f)
        return 0.0f;
    return Hit / Max;
}

bool CEqObj::get_sub_obj_hit_pts(unsigned short SubObjId, float& OutHitPts) const
{
    // FUN_062ab720
    OutHitPts = 0.0f;
    if (SubObjectID::IsEquipID(SubObjId))
    {
        CEquip const* Eq = EquipManager.FindByID(SubObjId);
        if (Eq != nullptr)
        {
            OutHitPts = Eq->GetHitPoints();
            return true;
        }
    }
    else if (SubObjectID::IsArchGroupID(SubObjId))
    {
        CArchGroup const* Group = const_cast<CArchGroupManager&>(ArchGroups).FindByID(SubObjId);
        if (Group != nullptr)
        {
            OutHitPts = Group->GetHitPoints();
            return true;
        }
    }
    return false;
}

bool CEqObj::set_sub_obj_hit_pts(unsigned short SubObjId, float HitPts)
{
    // FUN_062ab7a0
    if (SubObjectID::IsEquipID(SubObjId))
    {
        CEquip* Eq = EquipManager.FindByID(SubObjId);
        if (Eq != nullptr)
        {
            Eq->SetHitPoints(HitPts);
            return true;
        }
    }
    else if (SubObjectID::IsArchGroupID(SubObjId))
    {
        CArchGroup* Group = ArchGroups.FindByID(SubObjId);
        if (Group != nullptr)
        {
            Group->SetHitPoints(HitPts);
            return true;
        }
    }
    return false;
}

unsigned short CEqObj::inst_to_subobj_id(long Instance) const
{
    // FUN_062ab940. Falls back to 1 (the root) when no equip or group claims the instance.
    if (Instance != EngineInstanceId)
    {
        unsigned short SubObjId = EquipManager.InstToSubObjID(Instance);
        if (SubObjId != 0)
            return SubObjId;
        SubObjId = ArchGroups.InstToSubObjID(Instance);
        if (SubObjId != 0)
            return SubObjId;
    }
    return 1;
}

void CEqObj::flush_animations()
{
    // FUN_062aad30. DestroyScript (IAnimation2 slot 10), not StopPlayback as in
    // CShip::flush_animations. No null test on DALib::Anim. The vector keeps its capacity.
    for (std::vector<DockAnimInfo>::iterator It = DockingPoints.begin(); It != DockingPoints.end(); ++It)
    {
        if (It->AnimChannel != -1)
        {
            AnimDB::Rem(It->AnimChannel);
            DALib::Anim->DestroyScript((void*)It->AnimChannel);
        }
    }
    DockingPoints.clear();
}

void CEqObj::update_docking_animations(float Dt)
{
    // FUN_062aab50. Dt is never read. Anim slot 0x40 (GetStoredDuration) and slot 0x14 (see the
    // note in CShip::open_bay). No null test on DALib::Anim.
    (void)Dt;

    for (std::vector<DockAnimInfo>::iterator It = DockingPoints.begin(); It != DockingPoints.end(); ++It)
    {
        DockAnimInfo& Entry = *It;
        if (Entry.AnimChannel == -1)
            continue;

        float const Duration = DALib::Anim->GetStoredDuration(Entry.AnimChannel);
        float const Position = DALib::Anim->GetScriptDuration((void*)Entry.AnimChannel);

        if (Entry.bActive)
        {
            // NaN skips. notify is vtable +0xc8.
            if (Duration < Position)
            {
                if (Duration == 0.0f)
                {
                    unsigned char Flag = 1;
                    notify(INotify::DockAnimEnded, &Flag);
                    DALib::Anim->EvalPlayback(Entry.AnimChannel, 0, 0.0f, 1.0f, nullptr, AsOpaqueDword(1.0f),
                                              AsOpaqueDword(1.0f), nullptr);
                    AnimDB::Add(Entry.AnimChannel);
                }
                else if (Duration <= Entry.LastDuration)
                {
                    DALib::Anim->EvalPlayback(Entry.AnimChannel, 0, -2.0f, 1.0f, nullptr, AsOpaqueDword(1.0f),
                                              AsOpaqueDword(1.0f), nullptr);
                }
            }
        }
        else if (Duration > 0.0f)
        {
            if (Duration >= Position)
            {
                unsigned char Flag = 0;
                notify(INotify::DockAnimEnded, &Flag);
                DALib::Anim->EvalPlayback(Entry.AnimChannel, 1, -1.0f, 1.0f, nullptr, AsOpaqueDword(1.0f),
                                          AsOpaqueDword(1.0f), nullptr);
                AnimDB::Add(Entry.AnimChannel);
            }
            else if (Duration >= Entry.LastDuration)
            {
                DALib::Anim->EvalPlayback(Entry.AnimChannel, 1, -2.0f, 1.0f, nullptr, AsOpaqueDword(1.0f),
                                          AsOpaqueDword(1.0f), nullptr);
            }
        }

        Entry.LastDuration = Duration;
    }
}

void CEqObj::destroy_equipment(DamageList const& Damage, bool bParam2)
{
    // FUN_062abaa0
    for (DamageEntry const& Entry : Damage.Entries)
    {
        if (Entry.Fate == DamageEntry::SubObjFate::None)
            continue;

        if (SubObjectID::IsEquipID(Entry.SubObjId))
        {
            CEquip* Equip = EquipManager.FindByID(Entry.SubObjId);
            if (Equip == nullptr)
                continue;

            // Cause 0x14. Event 4 for cargo, else 3, with the SubObjId as the context.
            if (Damage.Cause == DamageCause::LootDecay)
                notify((INotify::Event)(Equip->Type == EquipTypeCargo ? 4 : 3), (void*)(unsigned)Entry.SubObjId);


            CAttachedEquip* Attached = CAttachedEquip::cast(Equip);
            if (Attached != nullptr)
            {
                if (Entry.Fate == DamageEntry::SubObjFate::Separated && !bParam2)
                {
                    Attached->SetFate(DamageEntry::SubObjFate::HealthProxy);
                    Equip->Destroy();
                    continue;
                }
                Attached->SetFate(Entry.Fate);
            }
            Equip->Destroy();
        }
        else if (SubObjectID::IsArchGroupID(Entry.SubObjId))
        {
            // vtable +0x1c; empty in the base CEquip (see NotifyArchGroupDestroyed).
            CEquipTraverser Walk;
            CEquip* Item = EquipManager.Traverse(Walk);
            while (Item != nullptr)
            {
                Item->NotifyArchGroupDestroyed(Entry.SubObjId);
                Item = EquipManager.Traverse(Walk);
            }
        }
    }
}

void CEqObj::destroy_sub_objs(DamageList const& List, bool Param2)
{
    // FUN_062aba50
    destroy_equipment(List, Param2);
    ArchGroups.DestroyGroups(List, Param2);
}

// FUN_062aa190 (the IObjDB overload).
void CEqObj::connect(IObjDB* ObjDb)
{
    CSimple::connect(ObjDb);
    // Tail jump to FUN_062d2830 on the behavior manager.
    if (BehaviorMgr != nullptr)
        ConnectBehaviorManagerObjectDb(BehaviorMgr, ObjectDb);
}

// FUN_062aa1c0 (the IObjDB overload).
void CEqObj::disconnect(IObjDB* ObjDb)
{
    if (ObjectDb != nullptr && ObjectDb == ObjDb)
    {
        if (BehaviorMgr != nullptr)
            ConnectBehaviorManagerObjectDb(BehaviorMgr, nullptr);
        CSimple::disconnect(ObjDb);

        // The first scanner's scan and cargo results are emptied (storage kept). Retail uses
        // the FindFirst result without a type re-check.
        CEquip* Eq = const_cast<CEquip*>(EquipManager.FindFirst(EquipTypeScanner));
        if (Eq != nullptr)
        {
            CEScanner* const Scanner = (CEScanner*)Eq;
            Scanner->Scans.reset();
            Scanner->CargoVecEnd = Scanner->CargoVecBegin;
        }
    }
}

// FUN_062a92a0
void CEqObj::connect(INotify* NotifyParam)
{
    Notify = NotifyParam;
}

void CEqObj::disconnect(INotify* NotifyParam)
{
    Notify = nullptr;
}

void CEqObj::notify(INotify::Event EventParam, void* Context)
{
    // FUN_062a92c0. INotify slot 0, as in CEquip::Notify but with a single listener.
    if (Notify != nullptr)
        Notify->notify(EventParam, Context);
}
