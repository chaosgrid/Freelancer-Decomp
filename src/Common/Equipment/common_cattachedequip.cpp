// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CAttachedEquip (retail CAttachedEquip.cpp). Declared in common_cequip.h; helpers shared with
// the sibling TUs are in common_cequip_internal.h (namespace CmnCequip).
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"

#include "common_cobject.h"
#include "common_csys.h"     // Acquire3DMathEngine
#include "common_fuses.h"
#include "common_iface.h"
#include "common_ini.h"
#include "common_misc.h"     // ComputeExplosiveImpulses (Disconnect)
#include "common_phyarch.h"  // PhyArch::Part (Connect)
#include "common_physys.h"
#include "common_spatial.h"
#include "common_timing.h"
#include "common_universe.h"
#include "DALib.h"
#include "dacom.h"
#include "ivp_physics/ivp_real_object.h"
#include "ivp_physics/ivp_controller.h"

#include <algorithm>
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
    const int kFateStay = 1;
    const int kFateDebris = 2;
}

CAttachedEquip::CAttachedEquip(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
                               Archetype::Equipment const* ArchObj, bool bMissionFlag)
    : CExternalEquip(TypeBits, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    Attachment = nullptr;
    Debris = nullptr;
    HitPts = 0.0f;
    Fate = kFateStay;
    bBoundsValid = 0;
    BoundRadius = 0.0f;
    BoundCenter.x = 0.0f;
    BoundCenter.y = 0.0f;
    BoundCenter.z = 0.0f;
    // Arch->load() with no null test, then the non-virtual CEquip::GetMaxHitPoints. The pad
    // bytes +0x39..+0x3b are not written.
    Arch->load();
    HitPts = CEquip::GetMaxHitPoints();
}

CAttachedEquip::CAttachedEquip(CAttachedEquip const& Other)
    : CExternalEquip(Other)
    , Attachment(Other.Attachment)
    , Debris(Other.Debris)
    , HitPts(Other.HitPts)
    , Fate(Other.Fate)
    , bBoundsValid(Other.bBoundsValid)
    , BoundRadius(Other.BoundRadius)
    , BoundCenter(Other.BoundCenter)
{
    // RVA 0x32a70: a raw field copy, no AddRef on Attachment/Debris/Arch. Only the +0x38 byte
    // is copied; the pad bytes +0x39..+0x3b are not.
}

CAttachedEquip::~CAttachedEquip()
{
    // FUN_06293160
    if (Debris != nullptr)
    {
        FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kCAttachedEquipSource, 0x22,
              "Deleting equipment with un-retrieved debris object!");
        Debris->Release();
        Debris = nullptr;
    }
    if (Attachment != nullptr)
        delete Attachment;
    Arch->Release();
}

int CAttachedEquip::GetToughness() const
{
    // RVA 0x33200; no null check.
    return ((Archetype::AttachedEquipment const*)Arch)->Toughness;
}

bool CAttachedEquip::Update(float Dt, unsigned Flags)
{
    // FUN_06293210: destroyed with un-retrieved debris -> true.
    if (IsDestroyed() && Debris != nullptr)
        return true;
    return CExternalEquip::Update(Dt, Flags);
}

float CAttachedEquip::GetHitPoints() const
{
    return HitPts;
}

void CAttachedEquip::SetHitPoints(float Hit)
{
    HitPts = Hit;
}

// RVA 0xc75d0
void CAttachedEquip::SetFate(DamageEntry::SubObjFate FateValue)
{
    Fate = (int)(FateValue);
}

bool CAttachedEquip::IsConnected() const
{
    if (!CExternalEquip::IsConnected() || Attachment == nullptr)
        return false;
    return Attachment->GetRootIndex() != -1;
}

bool CAttachedEquip::Connect(char const* HpName)
{
    // FUN_062932a0. No null tests on HpName, the parent's user data or its part.
    if (!CExternalEquip::Connect(HpName))
        return false;

    unsigned long const HpCrc = DACOM_CRC::GetCRC32(HpName);
    CObject* const Parent = (CObject*)(
        DALib::Engine->GetInstanceUserData((void*)(ConnectionIndex)));
    // A parent part whose hardpoint id list holds the CRC makes this a non-physical attachment.
    PhyArch::Part const* const Part = Parent->PhyPart;
    if (Part != nullptr &&
        std::find(Part->HardpointIds.begin(), Part->HardpointIds.end(), HpCrc) !=
            Part->HardpointIds.end())
    {
        Attachment = new CNonPhysAttachment();
    }
    else
    {
        Attachment = new CPhysAttachment();
    }
    return Attachment->Connect(this);
}

void CAttachedEquip::Disconnect()
{
    // FUN_06293380
    if (IsConnected())
    {
        if (Fate == kFateDebris)
        {
            Vector ParentPush;
            Vector ChildPush;
            Vector Point;
            ComputeExplosiveImpulses(GetRootIndex(), ParentPush, ChildPush, Point);
            Archetype::AttachedEquipment const* AttachedArch =
                (Archetype::AttachedEquipment const*)(Arch);
            ParentPush.x = ParentPush.x * AttachedArch->ParentImpulse; // Arch+0x7c
            ParentPush.y = ParentPush.y * AttachedArch->ParentImpulse;
            ParentPush.z = ParentPush.z * AttachedArch->ParentImpulse;
            ChildPush.x = ChildPush.x * AttachedArch->ChildImpulse;    // Arch+0x78
            ChildPush.y = ChildPush.y * AttachedArch->ChildImpulse;
            ChildPush.z = ChildPush.z * AttachedArch->ChildImpulse;
            Debris = Attachment->Disconnect(this, true);
            if (Debris != nullptr)
            {
                if (Debris->BodyPtr != nullptr)
                    PhySys::AddImpulseAtPoint(Debris, ChildPush, Point);
                Owner->notify(INotify::EquipSeparated, this);
            }
            if (Owner->BodyPtr != nullptr)
                PhySys::AddImpulseAtPoint(Owner, ParentPush, Point);
        }
        else if (Fate != kFateStay)
        {
            Owner->notify(INotify::EquipDetachedInPlace, this);
            Attachment->Disconnect(this, false);
            if (Fate != 3 && Fate != 4)
                FDUMP(0x100002, "%s(%d) : *** WARNING: %s", kCAttachedEquipSource, 0xd1,
                      "Disconnecting equipment with FATE_UNKNOWN, using FATE_DISAPPEAR");
        }
        if (Attachment != nullptr)
            delete Attachment;
        Attachment = nullptr;
    }
    CExternalEquip::Disconnect();
}

long CAttachedEquip::GetRootIndex() const
{
    if (!IsConnected())
        return -1;
    return Attachment->GetRootIndex();
}

bool CAttachedEquip::GetVelocity(Vector& Out) const
{
    // RVA 0x334e0
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    if (!IsConnected())
        return false;
    CObject* Owner = Attachment->GetPhysicsOwner();
    if (Owner == nullptr)
        return false;
    Out = Owner->get_velocity();
    return true;
}

void CAttachedEquip::ComputeBoundingSphere(float& OutRadius, Vector& OutCenter) const
{
    // FUN_06293740: the root instance's extent.
    long Root = GetRootIndex();
    DALib::Engine->GetInstanceExtent((void*)(Root), 0, &OutRadius,
                                     &OutCenter.x);
}

bool CAttachedEquip::GetCenterOfMass(Vector& Out) const
{
    // FUN_06293570: the lazily cached local center through the root instance's world transform
    // (math engine singleton DAT_063fc114).
    Vector World;
    World.x = 0.0f;
    World.y = 0.0f;
    World.z = 0.0f;
    Out = World;
    if (!IsConnected())
        return false;
    if (!bBoundsValid)
    {
        bBoundsValid = 1;
        ComputeBoundingSphere(BoundRadius, BoundCenter);
    }
    long Root = Attachment->GetRootIndex();
    float* Xform = DALib::Engine->GetInstanceTransform((void*)(Root));
    Acquire3DMathEngine()->TransformXform((FVector&)(World),
                                          *(FTransform const*)Xform,
                                          (FVector const&)(BoundCenter));
    Out = World;
    return true;
}

bool CAttachedEquip::GetRadius(float& OutRadius) const
{
    // FUN_06293650: the same lazy cache; the radius is not transformed.
    if (!IsConnected())
        return false;
    if (!bBoundsValid)
    {
        bBoundsValid = 1;
        ComputeBoundingSphere(BoundRadius, BoundCenter);
    }
    OutRadius = BoundRadius;
    return true;
}

CObject* CAttachedEquip::GetPhysicsOwner() const
{
    if (!IsConnected())
        return nullptr;
    return Attachment->GetPhysicsOwner();
}

bool CAttachedEquip::IsInstOnEquip(long InstIndex) const
{
    // RVA 0x336a0: the virtual GetRootIndex is called again for IsDescendant.
    if (InstIndex == -1)
        return false;
    if (InstIndex == GetRootIndex())
        return true;
    return IsDescendant(GetRootIndex(), InstIndex);
}

CObject* CAttachedEquip::RetrieveDebrisObject()
{
    CObject* Out = Debris;
    Debris = nullptr;
    return Out;
}

CAttachedEquip* __cdecl CAttachedEquip::cast(CEquip* Equip)
{
    if (Equip == nullptr || (Equip->GetType() & kAttachedEquipMask) == 0)
        return nullptr;
    return (CAttachedEquip*)(Equip);
}

CAttachedEquip const* __cdecl CAttachedEquip::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

// ============================================================
// Archetype accessors and small retail members restored by name
// ============================================================

// Every leaf *Arch accessor is one ICF-folded body (RVA 0x3fb90): the base Arch field, typed as
// the leaf archetype.
Archetype::AttachedEquipment const* CAttachedEquip::AttachedEquipArch() const
{
    return (Archetype::AttachedEquipment const*)(Arch);
}
