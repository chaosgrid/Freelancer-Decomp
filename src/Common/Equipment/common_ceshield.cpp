// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEShieldGenerator, CEShield, CEShieldIntruderChecker (retail CEShield.cpp). Declared in
// common_cequip.h; helpers shared with the sibling TUs are in common_cequip_internal.h
// (namespace CmnCequip).
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"

#include "common_cobject.h"
#include "common_csys.h"     // Acquire3DMathEngine
#include "common_fuses.h"    // WeaponModDB_GetShieldMod (CEShield::GetWeaponModifier)
#include "common_iface.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_physys.h"
#include "common_spatial.h"
#include "common_timing.h"   // Timing::GetGlobalTime
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "ivp_physics/ivp_real_object.h"
#include "ivp_physics/ivp_controller.h"

#include <cstdarg>
#include <cstdint>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Equipment/common_cequip_internal.h"

using namespace CmnCequip;

namespace
{
    const char kCEShieldSource[] = "E:\\FL\\Scratch\\Source\\Common\\CEShield.cpp";
}

CEShieldGenerator::CEShieldGenerator(CEqObj* OwnerObj, unsigned short EquipId,
                                     Archetype::ShieldGenerator const* ArchObj, bool bMissionFlag)
    : CAttachedEquip(EquipTypeShieldGen, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    // FUN_0629cd30: +0x54 is not written; the non-virtual CEquip::Activate.
    LinkedShield = nullptr;
    Opaque50Value = 0.0f;
    LastUpdateTime = Timing::GetGlobalTime();
    CEquip::Activate(true);
}

// RVA 0x3bc50. Retail quirk: LinkedShield is copied raw, not re-linked into the shield's
// generator list (as in CEShield's copy ctor).
CEShieldGenerator::CEShieldGenerator(CEShieldGenerator const& Other)
    : CAttachedEquip(Other)
{
    // The +0x54 gap is not copied.
    LinkedShield = Other.LinkedShield;
    Opaque50Value = Other.Opaque50Value;
    LastUpdateTime = Other.LastUpdateTime;
}

CEShieldGenerator::~CEShieldGenerator()
{
    // RVA 0x3cdb0: unlink, then the non-virtual CExternalEquip::Destroy().
    if (LinkedShield != nullptr)
    {
        LinkedShield->RemGen(this);
        LinkedShield = nullptr;
    }
    CExternalEquip::Destroy();
}

bool CEShieldGenerator::IsFunctioning() const
{
    // FUN_0629ce20: CEquip::IsFunctioning plus a linked shield.
    if (IsActive() && !IsDisabled() && !IsDestroyed() && LinkedShield != nullptr)
        return true;
    return false;
}

bool CEShieldGenerator::Update(float Dt, unsigned Flags)
{
    // FUN_0629ce60. Retail quirk: Dt is replaced by the real elapsed global time (read twice).
    (void)Dt;
    float RealDt = (float)(Timing::GetGlobalTime() - LastUpdateTime);
    LastUpdateTime = Timing::GetGlobalTime();

    if (IsFunctioning())
    {
        // NaN health does not recharge.
        bool bRecharging = LinkedShield->GetRelativeHealth() < 1.0f;
        Archetype::ShieldGenerator const* GenArch = (Archetype::ShieldGenerator const*)(Arch);
        // Kept on the x87 stack in retail.
        double Draw = (double)(RealDt) * GenArch->ConstantPowerDraw;
        if (bRecharging)
            Draw += (double)(RealDt) * GenArch->RebuildPowerDraw;
        // Not enough power (or NaN) skips both the draw and the regeneration.
        double Power = Owner->Power;
        if (Draw <= Power)
        {
            Owner->Power = (float)(Power - Draw);
            if (bRecharging)
            {
                float NewHitPoints = (float)(
                    (double)(RealDt) * GenArch->RegenerationRate + LinkedShield->GetHitPoints());
                LinkedShield->SetHitPoints(NewHitPoints);
            }
        }
    }
    return CAttachedEquip::Update(RealDt, Flags);
}

void CEShieldGenerator::Destroy()
{
    // RVA 0x3cf30, as the destructor.
    if (LinkedShield != nullptr)
    {
        LinkedShield->RemGen(this);
        LinkedShield = nullptr;
    }
    CExternalEquip::Destroy();
}

void CEShieldGenerator::LinkShield(CEShield* Shield)
{
    // FUN_0629cf50; no null test.
    LinkedShield = Shield;
    Shield->AddGen(this);
}

bool CEShieldGenerator::IsLinked() const
{
    // RVA 0x3bc40
    return LinkedShield != nullptr;
}

void CEShieldGenerator::UnLinkShield()
{
    // RVA 0x3cf70
    if (LinkedShield != nullptr)
    {
        LinkedShield->RemGen(this);
        LinkedShield = nullptr;
    }
}

CEShieldGenerator* __cdecl CEShieldGenerator::cast(CEquip* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeShieldGen)
        return nullptr;
    return (CEShieldGenerator*)(Equip);
}

CEShieldGenerator const* __cdecl CEShieldGenerator::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

CEShield::CEShield(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Shield const* ArchObj,
                   bool bMissionFlag)
    : CAttachedEquip(EquipTypeShield, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    // RVA 0x3bd70, in retail order.
    bOnline = 0;
    CurrentHitPoints = 0.0f;
    OfflineEndTime = 0.0;
    MissionFlagCopy = bMissionFlag ? 1 : 0;
    Gens = new std::vector<CEShieldGenerator*>();
    DominantGenArch = nullptr;
    OfflineThreshold = 0.0f;
    OfflineRebuildTime = 0.0f;
    MaxHitPoints = 0.0f;
    IntruderChecker = nullptr;

    // Retail overrides the inherited Fate (1) with 3; the meaning of 3 is not identified.
    Fate = 3;

    // Retail inlines CEShield::Activate(true) here.
    Activate(true);
}

CEShield::CEShield(CEShield const& Other)
    : CAttachedEquip(Other)
{
    // RVA 0x3ba70. Retail quirk: the generators are copied without re-linking them to the new
    // shield.
    bOnline = Other.bOnline;
    CurrentHitPoints = Other.CurrentHitPoints;
    OfflineEndTime = Other.OfflineEndTime;
    MissionFlagCopy = Other.MissionFlagCopy;
    Gens = new std::vector<CEShieldGenerator*>(*Other.Gens);
    DominantGenArch = Other.DominantGenArch;
    OfflineThreshold = Other.OfflineThreshold;
    OfflineRebuildTime = Other.OfflineRebuildTime;
    MaxHitPoints = Other.MaxHitPoints;
    IntruderChecker = Other.IntruderChecker;
}

CEShield::~CEShield()
{
    // FUN_0629bf00: CEShield::Destroy() inlined.
    CEShield::Destroy();
    delete Gens;
    Gens = nullptr;
}

void CEShield::Destroy()
{
    // FUN_0629c110. No null test of the generators.
    unsigned Index = 0;
    for (; Index < Gens->size(); ++Index)
        (*Gens)[Index]->LinkedShield = nullptr;
    Gens->clear();
    if (IntruderChecker != nullptr)
    {
        // Retail quirk: the inlined checker dtor calls DestroyPhantom only when the phantom is
        // already null.
        if (IntruderChecker->Phantom == nullptr)
            PhySys::DestroyPhantom(nullptr);
        delete IntruderChecker;
        // Retail quirk: the pointer is left dangling (a later ~CEShield would delete it again).
        // Unreachable: nothing in Common.dll creates a checker.
    }
    CExternalEquip::Destroy();
}

bool CEShield::GetEquipDesc(EquipDesc& Desc) const
{
    // RVA 0x3ba10 (an ICF-shared "return false"): unlike its siblings, no descriptor.
    (void)Desc;
    return false;
}

bool CEShield::IsFunctioning() const
{
    // FUN_0629bfe0: CEquip::IsFunctioning plus bOnline.
    return IsActive() && !IsDisabled() && !IsDestroyed() && bOnline != 0;
}

bool CEShield::CanActivate() const
{
    // FUN_0629ca50. NaN fails both tests.
    if (!(Timing::GetGlobalTime() >= OfflineEndTime))
        return false;
    if (!(GetRelativeHealth() > OfflineThreshold))
        return false;
    if (IntruderChecker != nullptr && IntruderChecker->IntruderCount != 0)
        return false;
    return true;
}

bool CEShield::Activate_Internal(bool bOn)
{
    // FUN_0629cad0
    if (bOnline != (unsigned char)(bOn))
    {
        if (Attachment != nullptr)
        {
            Attachment->EnableCollisions(bOn);
            if (IntruderChecker != nullptr)
                PhySys::SetCollisionStatePhantom(IntruderChecker->Phantom, !bOn);
            bOnline = bOn ? 1 : 0;
            Notify(INotify::ShieldOfflineStateChanged, &bOnline);
            return bOnline != 0;
        }
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kCEShieldSource, 0x195,
              "Tried to change change activation state on non-connected shield.");
    }
    return bOnline != 0;
}

bool CEShield::Activate(bool bOn)
{
    // RVA 0x3c190
    CEquip::Activate(bOn);
    if (IsActive())
    {
        if (CanActivate())
            Activate_Internal(true);
        return true;
    }
    if (bOnline != 0)
        Activate_Internal(false);
    return false;
}

bool CEShield::Update(float Dt, unsigned Flags)
{
    // RVA 0x3c020
    if (!IsFunctioning() && IsActive() && CanActivate())
    {
        Notify(INotify::ShieldCanReactivate, nullptr);
        Activate_Internal(true);
    }
    return CAttachedEquip::Update(Dt, Flags);
}

void CEShield::SetHitPoints(float Hit)
{
    // RVA 0x3c2d0
    if (MaxHitPoints < Hit)
        Hit = MaxHitPoints;
    CurrentHitPoints = Hit;

    if (IsFunctioning())
    {
        if (GetRelativeHealth() <= OfflineThreshold)
        {
            Notify(INotify::ShieldWentOffline, nullptr);
            OfflineEndTime = Timing::GetGlobalTime() + OfflineRebuildTime;
            Activate_Internal(false);
        }
    }
    else if (IsActive() && CanActivate())
    {
        Notify(INotify::ShieldCanReactivate, nullptr);
        Activate_Internal(true);
    }
}

float CEShield::GetHitPoints() const
{
    // RVA 0x3ba30 (ICF-shared with CEEngine::GetCruiseChargeTimeElapsed).
    return CurrentHitPoints;
}

bool CEShield::Connect(char const* HpName)
{
    // RVA 0x3c470
    if (!CAttachedEquip::Connect(HpName))
        return false;

    // inst_to_part is inlined in retail. No Attachment or owner test.
    CObject* PhysicsOwner = Attachment->GetPhysicsOwner();
    PhysicsOwner->flag_part_as_shield(PhysicsOwner->inst_to_part(Attachment->GetRootIndex()));
    Attachment->EnableCollisions(bOnline != 0);

    if (IntruderChecker != nullptr)
    {
        // AttachIntruderChecker inlined.
        IntruderChecker->Attach(this);
        PhySys::SetCollisionStatePhantom(IntruderChecker->Phantom, bOnline == 0);
    }

    // Publish the shield's physical sphere as the root instance's local-space extent. The
    // radius argument is the rounded float local (Ghidra shows it as "unaff_EBX"; the stack
    // trace confirms it). No null tests; math engine DAT_063fc204.
    long const Root = Attachment->GetRootIndex();
    CObject* RadiusOwner = Attachment->GetPhysicsOwner();
    Vector WorldCenter;
    float const Radius = RadiusOwner->get_physical_radius_r(WorldCenter);
    Transform const* InstXform = DALib::Engine->GetInstanceTransform(Root);
    I3DMathEngine* Math = Acquire3DMathEngine();
    Vector LocalCenter;
    Math->InverseTransform(LocalCenter, *InstXform, WorldCenter);
    DALib::Engine->SetInstanceExtent(Root, 1, Radius, &LocalCenter);

    return true;
}

void CEShield::Disconnect()
{
    // FUN_0629c5a0: DestroyIntruderChecker inlined.
    DestroyIntruderChecker();
    CAttachedEquip::Disconnect();
}

void CEShield::AttachIntruderChecker()
{
    // FUN_0629c950
    if (IntruderChecker != nullptr)
    {
        IntruderChecker->Attach(this);
        PhySys::SetCollisionStatePhantom(IntruderChecker->Phantom, bOnline == 0);
    }
}

void CEShield::DestroyIntruderChecker()
{
    // FUN_0629c980
    CEShieldIntruderChecker* Checker = IntruderChecker;
    if (Checker != nullptr)
    {
        Checker->IntruderCount = -1;
        PhySys::DestroyPhantom(Checker->Phantom);
        Checker->Phantom = nullptr;
    }
}

void CEShieldIntruderChecker::added(CObject* Obj)
{
    // FUN_0629cd10
    (void)Obj;
    ++IntruderCount;
}

void CEShieldIntruderChecker::deleted(CObject* Obj)
{
    // FUN_0629cd20
    (void)Obj;
    --IntruderCount;
}

void CEShieldIntruderChecker::Attach(CEShield* Shield)
{
    // FUN_0629cb70
    if (Phantom != nullptr)
    {
        IntruderCount = -1;
        PhySys::DestroyPhantom(Phantom);
        Phantom = nullptr;
    }
    if (Shield == nullptr)
        return;
    CObject* Body = Shield->GetPhysicsOwner();   // non-virtual in retail
    if (Body == nullptr)
        return;

    PhySys::PhantomParms Parms;
    Parms.SystemId = 0;
    Parms.bPhysicalUnmoveable = 0;
    Parms.Flag05 = 0;
    Parms.Position.x = 0.0f;
    Parms.Position.y = 0.0f;
    Parms.Position.z = 0.0f;
    MatrixSetIdentity(Parms.Orientation);
    Parms.Listener = nullptr;
    char const* Group = PhySys::GetCollisionGroup(Body);
    unsigned Byte = 0;
    do
    {
        Parms.Name[Byte] = Group[Byte];   // inline strcpy, unbounded as in retail
    } while (Group[Byte++] != '\0');
    Parms.SystemId = Body->PhysicsSystemId;
    Shield->GetConnectionPosition(&Parms.Position, &Parms.Orientation);
    Parms.Listener = this;
    Parms.Flag05 = 1;

    // The part is looked up by the CRC half of the root instance's name.
    FEngineName Name;
    DALib::Engine->GetInstanceName(&Name, Shield->GetRootIndex());
    PhyArch::Part const* Part = Shield->Arch->get_phy_part(Name.Crc);
    if (Part != nullptr)
    {
        PhySys::CreatePhantom(Parms, *Part, Phantom);
        PhySys::AttachPhantom(Body, Phantom);
        IntruderCount = 0;
        return;
    }
    FDUMP(kFdumpError,
          "%s(%d) : *** ERROR: Couldn't find shield geometry for shield intruder-checker: %s.",
          kCEShieldSource, 0x1e9, Body->Archetype->Nickname.get_string());
}

void CEShield::ShieldBatteryBoost(float Amount)
{
    // FUN_0629caa0: the argument is the new absolute hit points, not an increment.
    if (MaxHitPoints > 0.0f)
    {
        OfflineEndTime = 0.0;
        SetHitPoints(Amount);
    }
}

void CEShield::AddGen(CEShieldGenerator* Gen)
{
    // FUN_0629c5e0 (from the listing; Ghidra's output is corrupted). No Gen or Gen->Arch test.
    Archetype::ShieldGenerator* GenArch = (Archetype::ShieldGenerator*)Gen->Arch;

    // The toughest generator's archetype dominates (signed compare).
    if (DominantGenArch == nullptr || DominantGenArch->Toughness < GenArch->Toughness)
        DominantGenArch = GenArch;

    // A running average in one x87 expression, rounded once.
    int const ExistingCount = (int)Gens->size();
    OfflineThreshold = (float)(((double)ExistingCount * (double)OfflineThreshold +
                                (double)GenArch->OfflineThreshold) /
                               (double)(ExistingCount + 1));
    if (OfflineRebuildTime < GenArch->OfflineRebuildTime)
        OfflineRebuildTime = GenArch->OfflineRebuildTime;
    MaxHitPoints = GenArch->MaxCapacity + MaxHitPoints;

    // CEShield's own overrides (may bring the shield back online).
    SetHitPoints(GetHitPoints() + GenArch->MaxCapacity);

    Gens->push_back(Gen);

    // The virtual CEShield::Update(0, 0): may reactivate a shield starved for generators.
    Update(0.0f, 0);
}

void CEShield::RemGen(CEShieldGenerator* Gen)
{
    // RVA 0x3c820: the aggregates are rebuilt from the remaining generators (the threshold is
    // an average). No Gen, element or element Arch test.
    Archetype::ShieldGenerator* OldDominantGenArch = DominantGenArch;

    DominantGenArch = nullptr;
    OfflineThreshold = 0.0f;
    OfflineRebuildTime = 0.0f;
    MaxHitPoints = 0.0f;

    for (std::vector<CEShieldGenerator*>::iterator It = Gens->begin(); It != Gens->end(); )
    {
        if (*It == Gen)
        {
            // Every duplicate is removed too.
            It = Gens->erase(It);
            continue;
        }

        // Each float sum is rounded by its own store.
        Archetype::ShieldGenerator* RemArch = (Archetype::ShieldGenerator*)(*It)->Arch;
        if (DominantGenArch == nullptr || DominantGenArch->Toughness < RemArch->Toughness)
            DominantGenArch = RemArch;
        OfflineThreshold = RemArch->OfflineThreshold + OfflineThreshold;  // running sum; averaged below
        if (OfflineRebuildTime < RemArch->OfflineRebuildTime)
            OfflineRebuildTime = RemArch->OfflineRebuildTime;
        MaxHitPoints = RemArch->MaxCapacity + MaxHitPoints;
        ++It;
    }

    // Notify when the dominant archetype's nickname changed. The old one is dereferenced with
    // no null test (it cannot be null while a generator remains).
    if (DominantGenArch != nullptr && DominantGenArch->Nickname.Id != OldDominantGenArch->Nickname.Id)
    {
        Notify(INotify::ShieldGeneratorSetChanged, nullptr);
    }

    unsigned const RemainingCount = (unsigned)Gens->size();
    if (RemainingCount != 0)
        OfflineThreshold = (float)((double)OfflineThreshold / (double)RemainingCount);

    // Note: CurrentHitPoints is not clamped to a lowered MaxHitPoints unless it reaches 0.
    if (MaxHitPoints == 0.0f)
        SetHitPoints(0.0f);

    Update(0.0f, 0);
}

CEShield* __cdecl CEShield::cast(CEquip* Equip)
{
    if (Equip == nullptr || Equip->GetType() != EquipTypeShield)
        return nullptr;
    return (CEShield*)(Equip);
}

CEShield const* __cdecl CEShield::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

float CEShield::GetMaxHitPoints() const
{
    // RVA 0x3ba20
    return MaxHitPoints;
}

float CEShield::GetOfflineThreshold() const
{
    // RVA 0x3ba40
    return OfflineThreshold;
}

float CEShield::GetOfflineRebuildTime() const
{
    // RVA 0x3ba50
    return OfflineRebuildTime;
}

Archetype::ShieldGenerator const* CEShield::ShieldGenArch() const
{
    // RVA 0x3ba60: the dominant (toughest) generator's archetype.
    return DominantGenArch;
}

double CEShield::GetWeaponModifier(ID_String const& WeaponType) const
{
    // FUN_0629c9b0: the float-summed mean of FUN_06341c70 over the generators, left unrounded
    // in ST0 (hence double); 1.0 with none. No Gen / Arch test.
    float Sum = 0.0f;
    unsigned const Count = (unsigned)Gens->size();
    for (unsigned Index = 0; Index < Count; ++Index)
    {
        Archetype::ShieldGenerator const* GenArch = (*Gens)[Index]->ShieldGenArch();
        Sum = WeaponModDB_GetShieldMod(WeaponType.Id, GenArch->ShieldType.Id) + Sum;
    }
    if (Count == 0)
        return 1.0f;
    return (double)Sum / (double)Count;
}

Archetype::ShieldGenerator const* CEShieldGenerator::ShieldGenArch() const
{
    return (Archetype::ShieldGenerator const*)(Arch);
}

Archetype::Shield const* CEShield::ShieldArch() const
{
    return (Archetype::Shield const*)(Arch);
}

bool CEShield::IsLootable() const
{
    // RVA 0x3ba00 (ICF-shared).
    return false;
}

void CEShield::SetFate(DamageEntry::SubObjFate FateValue)
{
    // RVA 0x8d730 (an ICF-shared empty body).
    (void)FateValue;
}
