// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Archetype::Equipment and its non-weapon leaves (AttachedEquipment, Power, Engine, Armor, Shield,
// ShieldGenerator, Thruster, Scanner, Light, Tractor, CargoPod, FX equipment, RepairDroid,
// CloakingDevice, Commodity, RepairKit, ShieldBattery, TradeLaneEquip, LootCrate). Retail has
// one Archetype.cpp TU.

#include "common_archetype.h"
#include "Archetypes/common_archetype_internal.h"

#include "common_cobject.h" // CollisionGroupDesc (get_undamaged_collision_group_list)
#include "common_fuses.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_phyarch.h"
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "readfile.h"

#include <cstddef>
#include <list>
#include <map>
#include <math.h>
#include <memory>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace Archetype
{
    namespace
    {
        // Shared prefix copy for the "inherit=" donor copy (FUN_062f3ea0): the defaulted
        // Equipment::operator=, which runs Root::operator= first (Companion Release + AddRef,
        // not a raw store) and then the Equipment fields every leaf inlines. FUN_062f3ea0's
        // later Release of the donor Companion only balances because of that AddRef (a raw
        // copy destroys the donor's companion, e.g. light_equip.ini White -> SmallWhite ->
        // MediumWhite). No self test.
        void CopyEquipmentFields(Equipment& Dst, Equipment const& Src)
        {
            Dst.Equipment::operator=(Src);
        }
    }

    Equipment::Equipment(ICliObj* InCompanion)
        : Root(InCompanion)
        , Volume(0.0f)
        , bUseCount(true)
        , UnitsPerContainer(1)
        , bLootable(true)
    {
        TractoredExplosion.clear();
        HitPts = 1.0f;
    }

    bool Equipment::read(INI_Reader& Reader)
    {
        if (Root::read(Reader))
            return true;

        if (Reader.is_value("inherit"))
        {
            unsigned NicknameId = CreateID(Reader.get_value_string());
            Equipment* Donor = GEquipmentDb.Get(NicknameId);
            if (Donor != nullptr)
            {
                // FUN_062f3ea0, the donor copy; an immediately-invoked lambda so the port exports
                // no invented name. A dispatch on get_class_type() grouped by memory shape: retail
                // reuses one leaf's operator= for siblings of the same layout. Classes with no
                // case (Root/EqObj/Solar/Ship/Asteroid/DynamicAsteroid, bare Equipment/
                // AttachedEquipment/Projectile, and Armor, 0x22) copy no fields at all, as retail.
                [&](Equipment const& Src)
                {
                    if (get_class_type() != Src.get_class_type())
                        return;

                    // AddRef + Inherit the own companion from the donor's, with the pre-copy
                    // pointers.
                    ICliObj* OwnCompanion = Companion;
                    if (OwnCompanion != nullptr)
                    {
                        CliObjAddRef(OwnCompanion);
                        CliObjInherit(OwnCompanion, Src.Companion);
                    }
                    ID_String SavedNickname = Nickname;

                    switch (get_class_type())
                    {
                    case AClassLootCrate:
                    case AClassAttachedFX:
                        CopyEquipmentFields(*this, Src);
                        break;

                    case AClassCargoPod:
                    case AClassShield:
                        (AttachedEquipment&)*this = (AttachedEquipment const&)Src;
                        break;

                    case AClassCommodity:
                    {
                        CopyEquipmentFields(*this, Src);
                        Commodity& Dst = (Commodity&)*this;
                        Commodity const& LeafSrc = (Commodity const&)Src;
                        Dst.DecayPerSecond = LeafSrc.DecayPerSecond;
                        Dst.LootAppearance = LeafSrc.LootAppearance;
                        Dst.PodAppearance = LeafSrc.PodAppearance;
                        break;
                    }

                    case AClassPower:
                    {
                        CopyEquipmentFields(*this, Src);
                        Power& Dst = (Power&)*this;
                        Power const& LeafSrc = (Power const&)Src;
                        Dst.Capacity = LeafSrc.Capacity;
                        Dst.ChargeRate = LeafSrc.ChargeRate;
                        Dst.ThrustCapacity = LeafSrc.ThrustCapacity;
                        Dst.ThrustChargeRate = LeafSrc.ThrustChargeRate;
                        break;
                    }

                    case AClassEngine:
                    {
                        CopyEquipmentFields(*this, Src);
                        Engine& Dst = (Engine&)*this;
                        Engine const& LeafSrc = (Engine const&)Src;
                        Dst.ThrustMultiplier = LeafSrc.ThrustMultiplier;
                        Dst.LinearDrag = LeafSrc.LinearDrag;
                        Dst.PowerDrawFactor = LeafSrc.PowerDrawFactor;
                        Dst.CruisePowerUsage = LeafSrc.CruisePowerUsage;
                        Dst.CruiseChargeTime = LeafSrc.CruiseChargeTime;
                        Dst.bIndestructible = LeafSrc.bIndestructible;
                        Dst.ReverseFraction = LeafSrc.ReverseFraction;
                        break;
                    }

                    case AClassShieldGenerator:
                        // ShieldGenerator::operator='s body, inlined in retail.
                        (ShieldGenerator&)*this = (ShieldGenerator const&)Src;
                        break;

                    case AClassThruster:
                    {
                        (AttachedEquipment&)*this = (AttachedEquipment const&)Src;
                        Thruster& Dst = (Thruster&)*this;
                        Thruster const& LeafSrc = (Thruster const&)Src;
                        Dst.PowerUsage = LeafSrc.PowerUsage;
                        Dst.MaxThrust = LeafSrc.MaxThrust;
                        break;
                    }

                    case AClassLauncher:
                    {
                        // Retail's case 0x10 inlines the copy rather than calling Launcher::operator=;
                        // same result.
                        (AttachedEquipment&)*this = (AttachedEquipment const&)Src;
                        Launcher& Dst = (Launcher&)*this;
                        Launcher const& LeafSrc = (Launcher const&)Src;
                        Dst.DamagePerFire = LeafSrc.DamagePerFire;
                        Dst.PowerUsage = LeafSrc.PowerUsage;
                        Dst.RefireDelay = LeafSrc.RefireDelay;
                        Dst.MuzzleVelocity = LeafSrc.MuzzleVelocity;
                        Dst.ProjectileArchetype = LeafSrc.ProjectileArchetype;
                        Dst.UseAnimation = LeafSrc.UseAnimation;
                        break;
                    }

                    case AClassGun:
                        // Gun::operator='s body, inlined in retail.
                        (Gun&)*this = (Gun const&)Src;
                        break;

                    case AClassMineDropper:
                        (Launcher&)*this = (Launcher const&)Src;
                        break;

                    case AClassCounterMeasureDropper:
                    {
                        (Launcher&)*this = (Launcher const&)Src;
                        CounterMeasureDropper& Dst = (CounterMeasureDropper&)*this;
                        CounterMeasureDropper const& LeafSrc = (CounterMeasureDropper const&)Src;
                        Dst.AiRangeSq = LeafSrc.AiRangeSq;
                        break;
                    }

                    case AClassScanner:
                    {
                        CopyEquipmentFields(*this, Src);
                        Scanner& Dst = (Scanner&)*this;
                        Scanner const& LeafSrc = (Scanner const&)Src;
                        Dst.PowerThreshold = LeafSrc.PowerThreshold;
                        Dst.RadarRange = LeafSrc.RadarRange;
                        Dst.CargoScanRange = LeafSrc.CargoScanRange;
                        break;
                    }

                    case AClassLight:
                    {
                        CopyEquipmentFields(*this, Src);
                        Light& Dst = (Light&)*this;
                        Light const& LeafSrc = (Light const&)Src;
                        Dst.bAlwaysOn = LeafSrc.bAlwaysOn;
                        Dst.bDockingLight = LeafSrc.bDockingLight;
                        break;
                    }

                    case AClassTractor:
                    {
                        CopyEquipmentFields(*this, Src);
                        Tractor& Dst = (Tractor&)*this;
                        Tractor const& LeafSrc = (Tractor const&)Src;
                        Dst.MaxLength = LeafSrc.MaxLength;
                        Dst.ReachSpeed = LeafSrc.ReachSpeed;
                        break;
                    }

                    case AClassInternalFX:
                    {
                        CopyEquipmentFields(*this, Src);
                        InternalFXEquip& Dst = (InternalFXEquip&)*this;
                        InternalFXEquip const& LeafSrc = (InternalFXEquip const&)Src;
                        Dst.UseAnimation = LeafSrc.UseAnimation;
                        break;
                    }

                    case AClassRepairDroid:
                    {
                        CopyEquipmentFields(*this, Src);
                        RepairDroid& Dst = (RepairDroid&)*this;
                        RepairDroid const& LeafSrc = (RepairDroid const&)Src;
                        Dst.RepairRate = LeafSrc.RepairRate;
                        break;
                    }

                    case AClassRepairKit:
                    {
                        CopyEquipmentFields(*this, Src);
                        RepairKit& Dst = (RepairKit&)*this;
                        RepairKit const& LeafSrc = (RepairKit const&)Src;
                        Dst.LootAppearance = LeafSrc.LootAppearance;
                        break;
                    }

                    case AClassShieldBattery:
                    {
                        CopyEquipmentFields(*this, Src);
                        ShieldBattery& Dst = (ShieldBattery&)*this;
                        ShieldBattery const& LeafSrc = (ShieldBattery const&)Src;
                        Dst.LootAppearance = LeafSrc.LootAppearance;
                        break;
                    }

                    case AClassCloakingDevice:
                        (CloakingDevice&)*this = (CloakingDevice const&)Src;
                        break;

                    case AClassTradeLane:
                        CopyEquipmentFields(*this, Src);
                        break;

                    case AClassMunition:
                        (Munition&)*this = (Munition const&)Src;
                        break;

                    case AClassMine:
                        (Mine&)*this = (Mine const&)Src;
                        break;

                    case AClassCounterMeasure:
                        (CounterMeasure&)*this = (CounterMeasure const&)Src;
                        break;

                    default:
                        break;
                    }

                    // The shape copy left Companion as the donor with one extra AddRef: drop it
                    // and restore OwnCompanion (whose AddRef above balanced the copy's Release).
                    Nickname = SavedNickname;
                    CliObjRelease(Companion);
                    Companion = OwnCompanion;
                }(*Donor);
                return true;
            }

            // ArchDB::Get() has already dumped an ERROR; retail warns as well.
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: 'inherit' failed. couldn't find %s",
                  ArchetypeSource, 0x828, Reader.get_value_string());
            return true;
        }

        if (Reader.is_value("use_count"))
        {
            bUseCount = Reader.get_value_bool(0);
            return true;
        }

        if (Reader.is_value("units_per_container"))
        {
            UnitsPerContainer = Reader.get_value_int(0);
            return true;
        }

        // Dead in retail too: Root::read already takes "hit_pts".
        if (Reader.is_value("hit_pts"))
        {
            HitPts = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("volume"))
        {
            Volume = Reader.get_value_float(0);
            return true;
        }

        // The no-argument get_value_string (the whole value).
        if (Reader.is_value("tractored_explosion"))
        {
            TractoredExplosion.Id = CreateID(Reader.get_value_string());
            return true;
        }

        if (Reader.is_value("lootable"))
        {
            bLootable = Reader.get_value_bool(0);
            return true;
        }

        return false;
    }

    void Equipment::redefine(Root const& Other)
    {
        Root::redefine(Other);
        Equipment const* Donor = static_cast<Equipment const*>(&Other);
        if (Donor->bUseCount != bUseCount)
            bUseCount = Donor->bUseCount;
        if (Donor->UnitsPerContainer != UnitsPerContainer)
            UnitsPerContainer = Donor->UnitsPerContainer;
        if (Donor->Volume != Volume)
            Volume = Donor->Volume;
        if (Donor->bLootable != bLootable)
            bLootable = Donor->bLootable;
    }

    AClassType Equipment::get_class_type() const
    {
        return AClassEquipment;
    }

    // Base default (retail 0xc5390, folded with unrelated "return 0" accessors).
    // Overridden by many leaves; see each subclass for its own constant/logic.
    HpAttachmentType Equipment::get_hp_type() const
    {
        return HpTypeNone;
    }

    // 0x06261260 (ICF-shared).
    Equipment* Equipment::get_loot_appearance()
    {
        return this;
    }

    // 0x06325390 (ICF-shared).
    CargoPod* Equipment::get_pod_appearance()
    {
        return nullptr;
    }

    // 0x0629ba00 (ICF-shared).
    bool Equipment::is_attached() const
    {
        return false;
    }

    // 0x062f4720 (+0x74).
    Equipment* Commodity::get_loot_appearance()
    {
        if (LootAppearance != nullptr)
            return LootAppearance;
        return this;
    }

    // 0x062f4730 (+0x78).
    CargoPod* Commodity::get_pod_appearance()
    {
        return PodAppearance;
    }

    // 0x062f4e30, one body for both classes (both keep the pointer at +0x70).
    Equipment* RepairKit::get_loot_appearance()
    {
        if (LootAppearance != nullptr)
            return LootAppearance;
        return this;
    }

    Equipment* ShieldBattery::get_loot_appearance()
    {
        if (LootAppearance != nullptr)
            return LootAppearance;
        return this;
    }

    LootCrate::LootCrate(ICliObj* InCompanion)
        : Equipment(InCompanion)
    {
        // Listing 0x94460: ObjectType LOOT. LoadEquipment inlines this ctor.
        ObjectType = OBJ_LOOT;
    }

    AClassType LootCrate::get_class_type() const
    {
        return AClassLootCrate;
    }

    AttachedEquipment::AttachedEquipment(ICliObj* InCompanion)
        : Equipment(InCompanion)
        , ChildImpulse(0.0f)
        , ParentImpulse(0.0f)
        , Toughness(0)
    {
        SeparationExplosion.clear();
        DebrisType.clear();
        HpChild.clear();
    }

    bool AttachedEquipment::read(INI_Reader& Reader)
    {
        if (Equipment::read(Reader))
            return true;

        if (Reader.is_value("separation_explosion"))
        {
            SeparationExplosion.Id = CreateID(Reader.get_value_string(0));
            return true;
        }

        if (Reader.is_value("debris_type"))
        {
            DebrisType.Id = CreateID(Reader.get_value_string(0));
            return true;
        }

        if (Reader.is_value("child_impulse"))
        {
            ChildImpulse = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("parent_impulse"))
        {
            ParentImpulse = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("HP_child"))
        {
            HpChild.Value = StringAlloc(Reader.get_value_string(0), false);
            return true;
        }

        if (Reader.is_value("toughness"))
        {
            Toughness = Reader.get_value_int(0);
            return true;
        }

        return false;
    }

    void AttachedEquipment::redefine(Root const& Other)
    {
        Equipment::redefine(Other);
        AttachedEquipment const* Donor = static_cast<AttachedEquipment const*>(&Other);
        if (Donor->SeparationExplosion.Id != SeparationExplosion.Id)
            SeparationExplosion.Id = Donor->SeparationExplosion.Id;
        if (Donor->DebrisType.Id != DebrisType.Id)
            DebrisType.Id = Donor->DebrisType.Id;
        if (Donor->ChildImpulse != ChildImpulse)
            ChildImpulse = Donor->ChildImpulse;
        if (Donor->ParentImpulse != ParentImpulse)
            ParentImpulse = Donor->ParentImpulse;
        if (Donor->HpChild.Value != HpChild.Value)
            HpChild.Value = Donor->HpChild.Value;
    }

    AClassType AttachedEquipment::get_class_type() const
    {
        return AClassAttachedEquipment;
    }

    bool AttachedEquipment::is_attached() const
    {
        return true;
    }

    // Retail 0626df50 (also exported as CargoPod / Shield::operator=): Root::operator=, the
    // Equipment shape +0x5c..+0x6c, then dwords +0x70..+0x84. No self test.
    AttachedEquipment& AttachedEquipment::operator=(AttachedEquipment const& Other)
    {
        Equipment::operator=(Other);
        SeparationExplosion = Other.SeparationExplosion;
        DebrisType = Other.DebrisType;
        ChildImpulse = Other.ChildImpulse;
        ParentImpulse = Other.ParentImpulse;
        HpChild = Other.HpChild;
        Toughness = Other.Toughness;
        return *this;
    }


    Power::Power(ICliObj* InCompanion)
        : Equipment(InCompanion)
        , Capacity(0.0f)
        , ChargeRate(0.0f)
        , ThrustCapacity(0.0f)
        , ThrustChargeRate(0.0f)
    {
    }

    bool Power::read(INI_Reader& Reader)
    {
        if (Equipment::read(Reader))
            return true;

        if (Reader.is_value("capacity"))
        {
            Capacity = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("charge_rate"))
        {
            ChargeRate = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("thrust_capacity"))
        {
            ThrustCapacity = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("thrust_charge_rate"))
        {
            ThrustChargeRate = Reader.get_value_float(0);
            return true;
        }

        return false;
    }

    void Power::redefine(Root const& Other)
    {
        Equipment::redefine(Other);
        Power const* Donor = static_cast<Power const*>(&Other);
        if (Donor->Capacity != Capacity)
            Capacity = Donor->Capacity;
        if (Donor->ChargeRate != ChargeRate)
            ChargeRate = Donor->ChargeRate;
        if (Donor->ThrustCapacity != ThrustCapacity)
            ThrustCapacity = Donor->ThrustCapacity;
        if (Donor->ThrustChargeRate != ThrustChargeRate)
            ThrustChargeRate = Donor->ThrustChargeRate;
    }

    AClassType Power::get_class_type() const
    {
        return AClassPower;
    }

    // Retail 0x71740, folded with Solar::get_class_type and other generic-equipment
    // get_hp_type overrides -- all return the same unnamed constant 2.
    HpAttachmentType Power::get_hp_type() const
    {
        return HpTypeInternal;
    }

    Engine::Engine(ICliObj* InCompanion)
        : Equipment(InCompanion)
        , ThrustMultiplier(0.0f)
        , LinearDrag(0.0f)
        , PowerDrawFactor(0.0f)
        , CruisePowerUsage(0.0f)
        , CruiseChargeTime(0.0f)
        , bIndestructible(false)
        , ReverseFraction(0.0f)
    {
        Pad85[0] = 0;
        Pad85[1] = 0;
        Pad85[2] = 0;
    }

    // Common.dll.c 0x950b0.
    bool Engine::read(INI_Reader& Reader)
    {
        if (Equipment::read(Reader))
            return true;

        if (Reader.is_value("power_usage"))
        {
            PowerDrawFactor = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("cruise_power_usage"))
        {
            CruisePowerUsage = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("cruise_charge_time"))
        {
            CruiseChargeTime = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("max_force"))
        {
            ThrustMultiplier = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("linear_drag"))
        {
            LinearDrag = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("indestructible"))
        {
            bIndestructible = Reader.get_value_bool(0);
            return true;
        }

        if (!Reader.is_value("reverse_fraction"))
            return false;

        ReverseFraction = Reader.get_value_float(0);
        return true;
    }

    // Common.dll.c 0x951c0.
    void Engine::redefine(Root const& Other)
    {
        Equipment::redefine(Other);
        Engine const* Donor = static_cast<Engine const*>(&Other);
        if (Donor->PowerDrawFactor != PowerDrawFactor)
            PowerDrawFactor = Donor->PowerDrawFactor;
        if (Donor->CruisePowerUsage != CruisePowerUsage)
            CruisePowerUsage = Donor->CruisePowerUsage;
        if (Donor->CruiseChargeTime != CruiseChargeTime)
            CruiseChargeTime = Donor->CruiseChargeTime;
        if (Donor->ThrustMultiplier != ThrustMultiplier)
            ThrustMultiplier = Donor->ThrustMultiplier;
        if (Donor->LinearDrag != LinearDrag)
            LinearDrag = Donor->LinearDrag;
        if (Donor->ReverseFraction != ReverseFraction)
            ReverseFraction = Donor->ReverseFraction;
    }

    AClassType Engine::get_class_type() const
    {
        return AClassEngine;
    }

    HpAttachmentType Engine::get_hp_type() const
    {
        return HpTypeInternal;
    }

    Armor::Armor(ICliObj* InCompanion)
        : Equipment(InCompanion)
        , HitPtsScale(1.0f)
    {
    }

    // Common.dll.c 0x952d0.
    bool Armor::read(INI_Reader& Reader)
    {
        if (Equipment::read(Reader))
            return true;

        if (!Reader.is_value("hit_pts_scale"))
            return false;

        HitPtsScale = Reader.get_value_float(0);
        return true;
    }

    // Common.dll.c 0x946f0 -- shared code (Ghidra folds Armor/Commodity/RepairDroid::redefine
    // into one COMDAT since they all just compare-and-copy the float at +0x70).
    void Armor::redefine(Root const& Other)
    {
        Equipment::redefine(Other);
        Armor const* Donor = static_cast<Armor const*>(&Other);
        if (Donor->HitPtsScale != HitPtsScale)
            HitPtsScale = Donor->HitPtsScale;
    }

    AClassType Armor::get_class_type() const
    {
        return AClassArmor;
    }

    HpAttachmentType Armor::get_hp_type() const
    {
        return HpTypeInternal;
    }

    // Retail 0626dd20, also used by the "inherit=" donor copy for RepairDroid/RepairKit/
    // ShieldBattery (same Equipment + one-field shape). No self test.
    Armor& Armor::operator=(Armor const& Other)
    {
        Equipment::operator=(Other);
        HitPtsScale = Other.HitPtsScale;
        return *this;
    }

    Shield::Shield(ICliObj* InCompanion)
        : AttachedEquipment(InCompanion)
    {
        HitPts = 0.0f;
    }

    AClassType Shield::get_class_type() const
    {
        return AClassShield;
    }

    ShieldGenerator::ShieldGenerator(ICliObj* InCompanion)
        : AttachedEquipment(InCompanion)
        , HpType(HpTypeShieldGenerator)
        , RegenerationRate(0.0f)
        , MaxCapacity(0.0f)
        , ConstantPowerDraw(0.0f)
        , RebuildPowerDraw(0.0f)
        , OfflineThreshold(0.0f)
        , OfflineRebuildTime(0.0f)
        , HpShieldTypesBegin(nullptr)
        , HpShieldTypesEnd(nullptr)
        , HpShieldTypesCap(nullptr)
    {
        ShieldType.clear();
        memset(PadA8, 0, sizeof(PadA8));
    }

    ShieldGenerator::~ShieldGenerator()
    {
        // Common.dll.c 0xe5d0: free the dynamic array living at +0xac before Root's base
        // destructor runs.
        free(HpShieldTypesBegin);
        HpShieldTypesBegin = nullptr;
        HpShieldTypesEnd = nullptr;
        HpShieldTypesCap = nullptr;
    }


    ShieldGenerator::ShieldGenerator(ShieldGenerator const& Other)
        : AttachedEquipment(Other)
        , HpType(Other.HpType)
        , ShieldType(Other.ShieldType)
        , RegenerationRate(Other.RegenerationRate)
        , MaxCapacity(Other.MaxCapacity)
        , ConstantPowerDraw(Other.ConstantPowerDraw)
        , RebuildPowerDraw(Other.RebuildPowerDraw)
        , OfflineThreshold(Other.OfflineThreshold)
        , OfflineRebuildTime(Other.OfflineRebuildTime)
    {
        // Retail 0x0626e240: Root::Root(Other), the Equipment / AttachedEquipment / own
        // scalars (+0x5c..+0xa4) and the allocator byte at +0xa8 copied, then the array.
        memset(PadA8, 0, sizeof(PadA8));
        PadA8[0] = Other.PadA8[0];
        CopyConstructHpTypes(HpShieldTypesBegin, HpShieldTypesEnd, HpShieldTypesCap,
                             Other.HpShieldTypesBegin, Other.HpShieldTypesEnd);
    }

    ShieldGenerator& ShieldGenerator::operator=(ShieldGenerator const& Other)
    {
        // Retail 0x0626e3c0: no self test except around the array; +0xa8 is not copied.
        Equipment::operator=(Other);
        SeparationExplosion = Other.SeparationExplosion;
        DebrisType = Other.DebrisType;
        ChildImpulse = Other.ChildImpulse;
        ParentImpulse = Other.ParentImpulse;
        HpChild = Other.HpChild;
        Toughness = Other.Toughness;
        HpType = Other.HpType;
        ShieldType = Other.ShieldType;
        RegenerationRate = Other.RegenerationRate;
        MaxCapacity = Other.MaxCapacity;
        ConstantPowerDraw = Other.ConstantPowerDraw;
        RebuildPowerDraw = Other.RebuildPowerDraw;
        OfflineThreshold = Other.OfflineThreshold;
        OfflineRebuildTime = Other.OfflineRebuildTime;
        if (this != &Other)
        {
            AssignHpTypes(HpShieldTypesBegin, HpShieldTypesEnd, HpShieldTypesCap,
                          Other.HpShieldTypesBegin, Other.HpShieldTypesEnd);
        }
        return *this;
    }

    namespace
    {
        // Common.dll.c 0x95980 hp_shield_type push. VC6 vector<unsigned> push_back: grow by
        // doubling (1 if empty/size<2) when full, else append in place.
        void PushShieldHpType(ShieldGenerator& Gen, unsigned Value)
        {
            if (Gen.HpShieldTypesEnd == Gen.HpShieldTypesCap)
            {
                unsigned OldSize = Gen.HpShieldTypesBegin != nullptr
                                       ? (unsigned)(Gen.HpShieldTypesEnd - Gen.HpShieldTypesBegin)
                                       : 0;
                unsigned Growth = (Gen.HpShieldTypesBegin == nullptr || OldSize < 2) ? 1 : OldSize;
                unsigned NewCount = OldSize + Growth;
                unsigned* NewBuf = static_cast<unsigned*>(malloc(NewCount * sizeof(unsigned)));
                if (Gen.HpShieldTypesBegin != nullptr)
                    memcpy(NewBuf, Gen.HpShieldTypesBegin, OldSize * sizeof(unsigned));
                NewBuf[OldSize] = Value;
                free(Gen.HpShieldTypesBegin);
                Gen.HpShieldTypesBegin = NewBuf;
                Gen.HpShieldTypesEnd = NewBuf + OldSize + 1;
                Gen.HpShieldTypesCap = NewBuf + NewCount;
            }
            else
            {
                *Gen.HpShieldTypesEnd = Value;
                Gen.HpShieldTypesEnd += 1;
            }
        }
    }

    // Common.dll.c 0x95980.
    bool ShieldGenerator::read(INI_Reader& Reader)
    {
        if (AttachedEquipment::read(Reader))
            return true;

        if (Reader.is_value("hp_type"))
        {
            unsigned Value = 0;
            if (!TryLookupHpType(Reader.get_value_string(0), &Value))
            {
                HpType = HpTypeEliteShieldGenerator;
                return true;
            }
            HpType = Value;
            return true;
        }

        if (Reader.is_value("shield_type"))
        {
            ShieldType.Id = CreateID(Reader.get_value_string(0));
            return true;
        }

        if (Reader.is_value("regeneration_rate"))
        {
            RegenerationRate = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("max_capacity"))
        {
            MaxCapacity = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("constant_power_draw"))
        {
            ConstantPowerDraw = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("rebuild_power_draw"))
        {
            RebuildPowerDraw = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("offline_rebuild_time"))
        {
            OfflineRebuildTime = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("offline_threshold"))
        {
            OfflineThreshold = Reader.get_value_float(0);
            return true;
        }

        if (!Reader.is_value("hp_shield_type"))
            return false;

        unsigned Index = 0;
        while (!Reader.is_value_empty(Index))
        {
            unsigned Value = 0;
            if (TryLookupHpType(Reader.get_value_string(Index), &Value))
                PushShieldHpType(*this, Value);
            Index += 1;
        }
        return true;
    }

    // Common.dll.c 0x95d50. Notably HpType (+0x88) and MaxCapacity (+0x94) are NOT redefined.
    void ShieldGenerator::redefine(Root const& Other)
    {
        AttachedEquipment::redefine(Other);
        ShieldGenerator const* Donor = static_cast<ShieldGenerator const*>(&Other);
        if (Donor->ShieldType.Id != ShieldType.Id)
            ShieldType.Id = Donor->ShieldType.Id;
        if (Donor->RegenerationRate != RegenerationRate)
            RegenerationRate = Donor->RegenerationRate;
        if (Donor->ConstantPowerDraw != ConstantPowerDraw)
            ConstantPowerDraw = Donor->ConstantPowerDraw;
        if (Donor->RebuildPowerDraw != RebuildPowerDraw)
            RebuildPowerDraw = Donor->RebuildPowerDraw;
        if (Donor->OfflineRebuildTime != OfflineRebuildTime)
            OfflineRebuildTime = Donor->OfflineRebuildTime;
        if (Donor->OfflineThreshold != OfflineThreshold)
            OfflineThreshold = Donor->OfflineThreshold;
    }

    AClassType ShieldGenerator::get_class_type() const
    {
        return AClassShieldGenerator;
    }

    // Retail 0x2d50: return *(this+0x88) -- reads the (single) HpType field directly,
    // unlike get_hp_type_by_index which walks the dynamic HpShieldTypes array.
    HpAttachmentType ShieldGenerator::get_hp_type() const
    {
        return (HpAttachmentType)HpType;
    }

    Thruster::Thruster(ICliObj* InCompanion)
        : AttachedEquipment(InCompanion)
        , PowerUsage(0.0f)
        , MaxThrust(0.0f)
    {
    }

    AClassType Thruster::get_class_type() const
    {
        return AClassThruster;
    }

    // Retail 0xdec0, folded with CargoPod::get_class_type.
    HpAttachmentType Thruster::get_hp_type() const
    {
        return HpTypeThruster;
    }

    bool Thruster::read(INI_Reader& Reader)
    {
        // Common.dll.c 0x95f40
        if (AttachedEquipment::read(Reader))
            return true;

        if (Reader.is_value("power_usage"))
        {
            PowerUsage = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("max_force"))
        {
            MaxThrust = Reader.get_value_float(0);
            return true;
        }

        return false;
    }

    void Thruster::redefine(Root const& Other)
    {
        // Common.dll.c 0x95fb0
        AttachedEquipment::redefine(Other);
        Thruster const* Donor = static_cast<Thruster const*>(&Other);
        if (Donor->PowerUsage != PowerUsage)
            PowerUsage = Donor->PowerUsage;
        if (Donor->MaxThrust != MaxThrust)
            MaxThrust = Donor->MaxThrust;
    }

    Scanner::Scanner(ICliObj* InCompanion)
        : Equipment(InCompanion)
    {
        // Retail 062f48e0.
        PowerThreshold = 0.0f;
        RadarRange = 1000.0f;
        CargoScanRange = 0.0f;
    }

    // Common.dll.c 0x94960.
    bool Scanner::read(INI_Reader& Reader)
    {
        if (Equipment::read(Reader))
            return true;

        if (Reader.is_value("power_usage"))
        {
            PowerThreshold = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("range"))
        {
            RadarRange = Reader.get_value_float(0);
            return true;
        }

        if (!Reader.is_value("cargo_scan_range"))
            return false;

        CargoScanRange = Reader.get_value_float(0);
        return true;
    }

    // Common.dll.c 0x949e0.
    void Scanner::redefine(Root const& Other)
    {
        Equipment::redefine(Other);
        Scanner const* Donor = static_cast<Scanner const*>(&Other);
        if (Donor->PowerThreshold != PowerThreshold)
            PowerThreshold = Donor->PowerThreshold;
        if (Donor->RadarRange != RadarRange)
            RadarRange = Donor->RadarRange;
        if (Donor->CargoScanRange != CargoScanRange)
            CargoScanRange = Donor->CargoScanRange;
    }

    AClassType Scanner::get_class_type() const
    {
        return AClassScanner;
    }

    HpAttachmentType Scanner::get_hp_type() const
    {
        return HpTypeInternal;
    }

    Light::Light(ICliObj* InCompanion)
        : Equipment(InCompanion)
        , bAlwaysOn(true)
        , bDockingLight(false)
    {
        Pad72[0] = 0;
        Pad72[1] = 0;
    }

    // Common.dll.c 0x94aa0.
    bool Light::read(INI_Reader& Reader)
    {
        if (Equipment::read(Reader))
            return true;

        if (Reader.is_value("always_on"))
        {
            bAlwaysOn = Reader.get_value_bool(0);
            return true;
        }

        if (!Reader.is_value("docking_light"))
            return false;

        bDockingLight = Reader.get_value_bool(0);
        return true;
    }

    AClassType Light::get_class_type() const
    {
        return AClassLight;
    }

    Tractor::Tractor(ICliObj* InCompanion)
        : Equipment(InCompanion)
    {
        MaxLength = 0.0f;
        ReachSpeed = 100.0f;
    }

    // Common.dll.c 0x94b80
    bool Tractor::read(INI_Reader& Reader)
    {
        if (Equipment::read(Reader))
            return true;
        if (Reader.is_value("max_length"))
        {
            MaxLength = Reader.get_value_float(0);
            return true;
        }
        if (!Reader.is_value("reach_speed"))
            return false;
        ReachSpeed = Reader.get_value_float(0);
        return true;
    }

    // Common.dll.c 0x94be0
    void Tractor::redefine(Root const& Other)
    {
        Equipment::redefine(Other);
        Tractor const* Donor = static_cast<Tractor const*>(&Other);
        if (Donor->MaxLength != MaxLength)
            MaxLength = Donor->MaxLength;
        if (Donor->ReachSpeed != ReachSpeed)
            ReachSpeed = Donor->ReachSpeed;
    }

    AClassType Tractor::get_class_type() const
    {
        return AClassTractor;
    }

    HpAttachmentType Tractor::get_hp_type() const
    {
        return HpTypeInternal;
    }

    CargoPod::CargoPod(ICliObj* InCompanion)
        : AttachedEquipment(InCompanion)
    {
    }

    AClassType CargoPod::get_class_type() const
    {
        return AClassCargoPod;
    }

    // Retail 0xded0, folded with ShieldGenerator::get_class_type.
    HpAttachmentType CargoPod::get_hp_type() const
    {
        return HpTypeCargoPod;
    }

    AttachedFXEquip::AttachedFXEquip(ICliObj* InCompanion)
        : Equipment(InCompanion)
    {
    }

    AClassType AttachedFXEquip::get_class_type() const
    {
        return AClassAttachedFX;
    }

    // Retail 0626daa0, also used by the "inherit=" donor copy for LootCrate (same bare
    // Equipment shape). No self test.
    AttachedFXEquip& AttachedFXEquip::operator=(AttachedFXEquip const& Other)
    {
        Equipment::operator=(Other);
        return *this;
    }

    InternalFXEquip::InternalFXEquip(ICliObj* InCompanion)
        : Equipment(InCompanion)
    {
        UseAnimation.clear();
    }

    // Common.dll.c 0x94fb0.
    bool InternalFXEquip::read(INI_Reader& Reader)
    {
        if (Equipment::read(Reader))
            return true;

        if (!Reader.is_value("use_animation"))
            return false;

        UseAnimation.Value = StringAlloc(Reader.get_value_string(), false);
        return true;
    }

    // Common.dll.c 0x95000.
    void InternalFXEquip::redefine(Root const& Other)
    {
        Equipment::redefine(Other);
        InternalFXEquip const* Donor = static_cast<InternalFXEquip const*>(&Other);
        if (Donor->UseAnimation.Value != UseAnimation.Value)
            UseAnimation.Value = Donor->UseAnimation.Value;
    }

    AClassType InternalFXEquip::get_class_type() const
    {
        return AClassInternalFX;
    }

    RepairDroid::RepairDroid(ICliObj* InCompanion)
        : Equipment(InCompanion)
        , RepairRate(0.0f)
    {
    }

    // Common.dll.c 0x94ca0.
    bool RepairDroid::read(INI_Reader& Reader)
    {
        if (Equipment::read(Reader))
            return true;

        if (!Reader.is_value("repair_rate"))
            return false;

        RepairRate = Reader.get_value_float(0);
        return true;
    }

    // Common.dll.c 0x946f0 -- shared COMDAT with Armor::redefine/Commodity::redefine.
    void RepairDroid::redefine(Root const& Other)
    {
        Equipment::redefine(Other);
        RepairDroid const* Donor = static_cast<RepairDroid const*>(&Other);
        if (Donor->RepairRate != RepairRate)
            RepairRate = Donor->RepairRate;
    }

    AClassType RepairDroid::get_class_type() const
    {
        return AClassRepairDroid;
    }

    HpAttachmentType RepairDroid::get_hp_type() const
    {
        return HpTypeInternal;
    }

    CloakingDevice::CloakingDevice(ICliObj* InCompanion)
        : AttachedEquipment(InCompanion)
        , PowerUsage(0.0f)
        , CloakInTime(0.0f)
        , CloakOutTime(0.0f)
        , CloakInFx(0)
        , CloakOutFx(0)
    {
    }

    // Common.dll.c 0x956b0.
    bool CloakingDevice::read(INI_Reader& Reader)
    {
        if (AttachedEquipment::read(Reader))
            return true;

        if (Reader.is_value("power_usage"))
        {
            PowerUsage = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("cloakin_time"))
        {
            CloakInTime = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("cloakout_time"))
        {
            CloakOutTime = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("cloakin_fx"))
        {
            CloakInFx = CreateID(Reader.get_value_string());
            return true;
        }

        if (!Reader.is_value("cloakout_fx"))
            return false;

        CloakOutFx = CreateID(Reader.get_value_string());
        return true;
    }

    // Common.dll.c 0x957a0. Only PowerUsage is redefined; the other cloak fields are not.
    void CloakingDevice::redefine(Root const& Other)
    {
        AttachedEquipment::redefine(Other);
        CloakingDevice const* Donor = static_cast<CloakingDevice const*>(&Other);
        if (Donor->PowerUsage != PowerUsage)
            PowerUsage = Donor->PowerUsage;
    }

    AClassType CloakingDevice::get_class_type() const
    {
        return AClassCloakingDevice;
    }

    // Retail 0626e0a0, used by the "inherit=" donor copy. No self test.
    CloakingDevice& CloakingDevice::operator=(CloakingDevice const& Other)
    {
        AttachedEquipment::operator=(Other);
        PowerUsage = Other.PowerUsage;
        CloakInTime = Other.CloakInTime;
        CloakOutTime = Other.CloakOutTime;
        CloakInFx = Other.CloakInFx;
        CloakOutFx = Other.CloakOutFx;
        return *this;
    }

    Commodity::Commodity(ICliObj* InCompanion)
        : Equipment(InCompanion)
        , DecayPerSecond(0.0f)
        , LootAppearance(nullptr)
        , PodAppearance(nullptr)
    {
        ObjectType = OBJ_LOOT;
    }

    bool Commodity::read(INI_Reader& Reader)
    {
        if (Equipment::read(Reader))
            return true;

        if (Reader.is_value("decay_per_second"))
        {
            DecayPerSecond = Reader.get_value_float(0);
            return true;
        }

        if (ReadLootAppearance(Reader, LootAppearance))
            return true;

        if (Reader.is_value("pod_appearance"))
        {
            Equipment* Found = LookupLootAppearance(Reader.get_value_string());
            if (Found != nullptr && Found->get_class_type() == AClassCargoPod)
                PodAppearance = static_cast<CargoPod*>(Found);
            return true;
        }

        return false;
    }

    void Commodity::redefine(Root const& Other)
    {
        Equipment::redefine(Other);
        Commodity const* Donor = static_cast<Commodity const*>(&Other);
        if (Donor->DecayPerSecond != DecayPerSecond)
            DecayPerSecond = Donor->DecayPerSecond;
    }

    AClassType Commodity::get_class_type() const
    {
        return AClassCommodity;
    }

    // Retail 0x1024b0, folded with EqObj::get_class_type.
    HpAttachmentType Commodity::get_hp_type() const
    {
        return HpTypeCommodity;
    }

    float Commodity::get_decay_per_second() const
    {
        float Cap = HitPts * 0.25f;
        if (DecayPerSecond < Cap)
            return DecayPerSecond;
        return Cap;
    }

    RepairKit::RepairKit(ICliObj* InCompanion)
        : Equipment(InCompanion)
        , LootAppearance(nullptr)
    {
    }

    bool RepairKit::read(INI_Reader& Reader)
    {
        if (Equipment::read(Reader))
            return true;

        return ReadLootAppearance(Reader, LootAppearance);
    }

    AClassType RepairKit::get_class_type() const
    {
        return AClassRepairKit;
    }

    HpAttachmentType RepairKit::get_hp_type() const
    {
        return HpTypeInternal;
    }

    ShieldBattery::ShieldBattery(ICliObj* InCompanion)
        : Equipment(InCompanion)
        , LootAppearance(nullptr)
    {
    }

    bool ShieldBattery::read(INI_Reader& Reader)
    {
        if (Equipment::read(Reader))
            return true;

        return ReadLootAppearance(Reader, LootAppearance);
    }

    AClassType ShieldBattery::get_class_type() const
    {
        return AClassShieldBattery;
    }

    HpAttachmentType ShieldBattery::get_hp_type() const
    {
        return HpTypeInternal;
    }

    TradeLaneEquip::TradeLaneEquip(ICliObj* InCompanion)
        : Equipment(InCompanion)
    {
    }

    AClassType TradeLaneEquip::get_class_type() const
    {
        return AClassTradeLane;
    }


    int ShieldGenerator::get_number_of_hp_types() const
    {
        // 0x95e20: falls back to 0x22 (34) when the custom hp_shield_type list is empty.
        if (HpShieldTypesBegin == nullptr)
            return 0x22;
        int Count = (int)(HpShieldTypesEnd - HpShieldTypesBegin);
        return Count != 0 ? Count : 0x22;
    }

    HpAttachmentType ShieldGenerator::get_hp_type_by_index(int Index) const
    {
        // DAT_063ecc98, 34 dwords: the default hp-type table used when the shield generator
        // has no custom hp_type list (0x95e40).
        static HpAttachmentType const kDefaultShieldHpTypes[34] = {
            HpTypeShieldGenerator,          HpTypeFighterShieldGenerator,
            HpTypeEliteShieldGenerator,     HpTypeFreighterShieldGenerator,
            HpTypeFighterShieldSpecial1,    HpTypeFighterShieldSpecial2,
            HpTypeFighterShieldSpecial3,    HpTypeFighterShieldSpecial4,
            HpTypeFighterShieldSpecial5,    HpTypeFighterShieldSpecial6,
            HpTypeFighterShieldSpecial7,    HpTypeFighterShieldSpecial8,
            HpTypeFighterShieldSpecial9,    HpTypeFighterShieldSpecial10,
            HpTypeEliteShieldSpecial1,      HpTypeEliteShieldSpecial2,
            HpTypeEliteShieldSpecial3,      HpTypeEliteShieldSpecial4,
            HpTypeEliteShieldSpecial5,      HpTypeEliteShieldSpecial6,
            HpTypeEliteShieldSpecial7,      HpTypeEliteShieldSpecial8,
            HpTypeEliteShieldSpecial9,      HpTypeEliteShieldSpecial10,
            HpTypeFreighterShieldSpecial1,  HpTypeFreighterShieldSpecial2,
            HpTypeFreighterShieldSpecial3,  HpTypeFreighterShieldSpecial4,
            HpTypeFreighterShieldSpecial5,  HpTypeFreighterShieldSpecial6,
            HpTypeFreighterShieldSpecial7,  HpTypeFreighterShieldSpecial8,
            HpTypeFreighterShieldSpecial9,  HpTypeFreighterShieldSpecial10};

        unsigned Count = (HpShieldTypesBegin != nullptr)
                             ? (unsigned)(HpShieldTypesEnd - HpShieldTypesBegin)
                             : 0;
        if (Count == 0)
        {
            // Retail accepts 0..0x22; index 0x22 is one past the table and reads the next .data
            // dword, Munition::BEAM_ARCH_INDEX (063ecd20) -- reproduced.
            if (Index >= 0 && Index < 34)
                return kDefaultShieldHpTypes[Index];
            if (Index == 0x22)
                return (HpAttachmentType)Munition::BEAM_ARCH_INDEX;
            return HpTypeNone;
        }
        // Deviation: retail also accepts Index == Count (one past the end); the port stops at
        // Count like Gun::get_hp_type_by_index. See docs/re/deviations.md.
        if (Index >= 0 && (unsigned)Index < Count)
            return (HpAttachmentType)HpShieldTypesBegin[Index];
        return HpTypeNone;
    }
}
