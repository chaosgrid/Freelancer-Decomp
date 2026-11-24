// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CETractor (TractorArm itself is in common_tractorarm.cpp). Declared in common_cequip.h;
// shared helpers are in common_cequip_internal.h (namespace CmnCequip).
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"

#include "common_cobject.h"
#include "common_csys.h"     // Acquire3DMathEngine
#include "common_fuses.h"
#include "common_iface.h"
#include "common_ini.h"
#include "common_misc.h"
#include "common_physys.h"
#include "common_spatial.h"
#include "common_timing.h"
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
// Owner ship's world Transform (EngineObject Orientation @ +0x08 followed by WorldPosition @
// +0x2c) composed with the hardpoint-local HpXform via I3DMathEngine::MulTransform (slot 0x1c).
// FUN_0629de50 and the identical inline code in FUN_0629e730/0629e660/0629df20. Shared by
// CETractor::GetSourcePos/CanSeeTarget/VerifyTarget and, via GetSourcePos, TractorArm
// (which cannot reach the protected HpXform). Retail tests neither the owner nor the engine.
Transform ResolveTractorSourceXform(CEqObj const* Owner, Transform const& LocalXform)
{
    Transform const* OwnerXform =
        (Transform const*)((unsigned char const*)Owner + offsetof(EngineObject, Orientation));

    Transform Result;
    Acquire3DMathEngine()->MulTransform((FTransform&)Result, (FTransform const&)*OwnerXform,
                                        (FTransform const&)LocalXform);   // +0x1C
    return Result;
}
}  // namespace

CETractor::CETractor(CShip* OwnerObj, unsigned short EquipId, Archetype::Tractor const* ArchObj,
                     bool bMissionFlagIn)
    : CInternalEquip(EquipTypeTractor, OwnerObj, EquipId, ArchObj, bMissionFlagIn)
    , Arms()
{
    static_assert(offsetof(CETractor, Arms) == 0x50, "CETractor+0x50 arm vector");

    // FUN_0629e030. Identity unless Owner is a ship (ObjectClass & 0x503) with a HpTractorSource
    // hardpoint, which then supplies the ship-space Transform. Retail does not test the archetype
    // or hardpoint name; on failure it rewrites the same identity.
    HpXform.Rotation = Matrix(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    HpXform.Translation = Vector{0.0f, 0.0f, 0.0f};

    if (OwnerObj != nullptr && ((unsigned)OwnerObj->ObjectClass & 0x503) == 0x503)
    {
        Archetype::Ship const* ShipArch = (Archetype::Ship const*)OwnerObj->Archetype;
        long DiscardHandle = 0;
        HardpointInfo Info;
        if (FindHardpoint_PS(OwnerObj->EngineInstanceId, ShipArch->HpTractorSource.Value,
                             DiscardHandle, Info))
        {
            HpXform.Rotation = Info.Orient;
            HpXform.Translation = Info.Position;
        }
    }
}

CETractor::CETractor(CETractor const& Other)
    : CInternalEquip(Other)
    , HpXform(Other.HpXform)
    , Arms(Other.Arms)
{
    // FUN_0629d9a0. Retail's inlined vector<TractorArm> copy is TractorArm(TractorArm const&)
    // per arm. Quirk kept: each copied arm's Owner still points at the source CETractor.
}

CETractor::~CETractor()
{
    // FUN_0629e170. CEquip::Destroy, then the inlined ~vector<TractorArm> (the Arms member's
    // destructor here), which runs before ~CInternalEquip as in retail.
    CEquip::Destroy();
}

// FUN_0629e440. IsFunctioning is vtable+0xc, Activate vtable+0x30.
bool CETractor::Update(float Dt, unsigned Flags)
{
    if (IsFunctioning())
    {
        int ActiveCount = 0;
        for (std::vector<TractorArm>::iterator It = Arms.begin(); It != Arms.end(); ++It)
        {
            It->Update(Dt);
            if (It->GetMode() != TractorArm::MODE_OFF)
                ++ActiveCount;
        }
        if (ActiveCount == 0)
            Activate(false);
    }
    return CEquip::Update(Dt, Flags);
}

// FUN_0629e790
float CETractor::GetRange() const
{
    return ((Archetype::Tractor const*)EquipArch())->MaxLength;
}

int CETractor::CountActiveArms() const
{
    // FUN_0629e240
    int Count = 0;
    for (std::vector<TractorArm>::const_iterator It = Arms.begin(); It != Arms.end(); ++It)
        if (It->GetMode() != TractorArm::MODE_OFF)
            ++Count;
    return Count;
}

TractorFailureCode CETractor::AddTarget(CLoot* Target)
{
    // FUN_0629e270. On a verify failure Notify(TractorTargetRejected) gets the address of the
    // failure code (retail reuses the dead param_1 slot for it), not the CLoot.
    TractorFailureCode Code = VerifyTarget(Target);
    if (Code != TractorOk)
    {
        Notify(INotify::TractorTargetRejected, &Code);
        return Code;
    }

    TractorArm* Slot = nullptr;
    for (std::vector<TractorArm>::iterator It = Arms.begin(); It != Arms.end(); ++It)
    {
        if (It->GetMode() == TractorArm::MODE_OFF)
        {
            Slot = &*It;
            break;
        }
    }

    if (Slot == nullptr)
    {
        // Retail's VC6 vector insert helper FUN_0629e840.
        Arms.insert(Arms.end(), 1, TractorArm(this));
        Slot = &Arms.back();
    }

    Slot->SetTarget(Target);

    if (!IsActive())
        Activate(true);

    return TractorOk;
}

void CETractor::RemoveTarget(unsigned TargetId)
{
    // FUN_0629e3d0
    int RemainingActive = 0;
    for (std::vector<TractorArm>::iterator It = Arms.begin(); It != Arms.end(); ++It)
    {
        if (It->GetMode() == TractorArm::MODE_OFF)
            continue;
        CLoot* Target = It->GetTarget();
        if (Target == nullptr || Target->get_id() != TargetId)
        {
            ++RemainingActive;
        }
        else
        {
            It->TurnOff();
        }
    }
    if (RemainingActive == 0)
        Activate(false);
}

TractorFailureCode CETractor::VerifyTarget(CLoot const* Target) const
{
    // FUN_0629e4a0
    if (Target == nullptr || Target->Units == 0)
        return TractorNoContents;

    Vector const SourcePos = GetSourcePos();
    Vector const Delta{Target->WorldPosition.x - SourcePos.x, Target->WorldPosition.y - SourcePos.y,
                        Target->WorldPosition.z - SourcePos.z};
    // Range 0 means unlimited. Float deltas, x87 sum order (y^2 + z^2) + x^2; out of range only
    // when Range^2 is strictly below (unordered passes).
    float const Range = GetRange();
    if (Range != 0.0f)
    {
        double const DistSq = (double)Delta.y * Delta.y + (double)Delta.z * Delta.z +
                              (double)Delta.x * Delta.x;
        if ((double)Range * Range < DistSq)
            return TractorOutOfRange;
    }

    // Retail calls the accessors below on ShipOwner and reads the contents archetype with no
    // null test.
    CShip* ShipOwner = nullptr;
    if (Owner != nullptr && ((unsigned)Owner->ObjectClass & 0x503) == 0x503)
        ShipOwner = (CShip*)Owner;

    Archetype::Equipment const* ContentsArch = Target->contents_arch();   // [Target+0xec]
    // Only a positive volume is tested; full only when the unrounded remainder is strictly below.
    float const Volume = ContentsArch->Volume;
    if (Volume > 0.0f && ShipOwner->get_cargo_hold_remaining() < Volume)
        return TractorCargoFull;

    switch (ContentsArch->get_class_type())
    {
    case Archetype::AClassRepairKit:
        if (ShipOwner->get_repair_kit_capacity_remaining() == 0)
            return TractorRepairKitFull;
        break;
    case Archetype::AClassShieldBattery:
        if (ShipOwner->get_shield_battery_capacity_remaining() == 0)
            return TractorShieldBattFull;
        break;
    case Archetype::AClassMunition:
    case Archetype::AClassMine:
    case Archetype::AClassCounterMeasure:
        if (ShipOwner->get_ammo_capacity_remaining(ContentsArch) == 0)
            return TractorAmmoFull;
        break;
    default:
        break;
    }

    return TractorOk;
}

Vector CETractor::GetSourcePos() const
{
    // FUN_0629e730
    return ResolveTractorSourceXform(Owner, HpXform).Translation;
}

bool CETractor::CanSeeTarget(CLoot const* Target) const
{
    // FUN_0629e660. A range check only, no line-of-sight raycast. Unlike VerifyTarget the
    // differences stay unrounded in double and the sum order is (dz^2 + dy^2) + dx^2.
    Vector const SourcePos = GetSourcePos();
    double const Dx = (double)Target->WorldPosition.x - SourcePos.x;
    double const Dy = (double)Target->WorldPosition.y - SourcePos.y;
    double const Dz = (double)Target->WorldPosition.z - SourcePos.z;
    float const Range = GetRange();
    if (Range != 0.0f && (double)Range * Range < Dz * Dz + Dy * Dy + Dx * Dx)
        return false;
    return true;
}

TractorArm const* CETractor::GetTractorArm(unsigned TargetId) const
{
    // FUN_0629e7a0
    for (std::vector<TractorArm>::const_iterator It = Arms.begin(); It != Arms.end(); ++It)
    {
        CLoot* Target = It->GetTarget();
        if (Target != nullptr && Target->get_id() == TargetId)
            return &*It;
    }
    return nullptr;
}

Archetype::Tractor const* CETractor::TractorArch() const
{
    return (Archetype::Tractor const*)(Arch);
}

CETractor* __cdecl CETractor::cast(CEquip* Equip)
{
    // FUN_0629e7e0
    if (Equip == nullptr || Equip->GetType() != EquipTypeTractor)
        return nullptr;
    return (CETractor*)(Equip);
}

CETractor const* __cdecl CETractor::cast(CEquip const* Equip)
{
    return cast(const_cast<CEquip*>(Equip));
}
