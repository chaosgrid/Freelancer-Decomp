// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CArchGroup, CArchGroupManager and CArchGrpTraverser (retail CArchGroupManager.cpp).
#include "common_cobject.h"
#include "x86math.h"

#include "common_id.h"
#include "common_physys.h"
#include "common_misc.h" // DamageList, DamageEntry

#include "DALib.h"
#include "dacom.h"

#include <cmath>
#include <list>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    const char kCArchGroupSource[] = "E:\\FL\\Scratch\\Source\\Common\\CArchGroupManager.cpp";
}

CArchGroup::CArchGroup(Archetype::CollisionGroup const& GroupArch, CEqObj* const OwnerObj)
{
    Owner = OwnerObj;
    Arch = &GroupArch;
    HitPts = (float)GroupArch.HitPts;
    Field10 = 1;
    State = STATE_ALIVE;
    Fate = 0;
    DestroyedObj = nullptr;
    // +0x20..+0x30; the padding +0x21..+0x23 is left alone.
    bBoundsValid = false;
    BoundRadius = 0.0f;
    BoundCenter.x = 0.0f;
    BoundCenter.y = 0.0f;
    BoundCenter.z = 0.0f;

    // The root instance of the group: the child of the owner's instance named by Arch's "obj".
    RootIndex = FindChild(OwnerObj->EngineInstanceId, GroupArch.ObjName);

    LoadDamageObjs();
}

CArchGroup::~CArchGroup()
{
    // FUN_062a01e0. DestroyedObj is +0x1c.
    if (DestroyedObj != nullptr)
        DestroyedObj->Release();
    FreeDamageObjs();
}

void CArchGroup::LoadDamageObjs()
{
    // FUN_062a0e00. Parent list (Arch+0x40), then group list (Arch+0x4c). The list heads are
    // not tested (the CollisionGroup ctor always allocates them).
    std::vector<Archetype::DamageObjInfo>::const_iterator It = Arch->DmgObjs.Items->begin();
    for (; It != Arch->DmgObjs.Items->end(); ++It)
    {
        Archetype::Root* const Simple = Archetype::GetSimple(It->Arch.Id);
        if (Simple != nullptr)
            Simple->load();
    }
    for (It = Arch->GroupDmgObjs.Items->begin(); It != Arch->GroupDmgObjs.Items->end(); ++It)
    {
        Archetype::Root* const Simple = Archetype::GetSimple(It->Arch.Id);
        if (Simple != nullptr)
            Simple->load();
    }
}

void CArchGroup::FreeDamageObjs()
{
    // FUN_062a0e70
    std::vector<Archetype::DamageObjInfo>::const_iterator It = Arch->DmgObjs.Items->begin();
    for (; It != Arch->DmgObjs.Items->end(); ++It)
    {
        Archetype::Root* const Simple = Archetype::GetSimple(It->Arch.Id);
        if (Simple != nullptr)
            Simple->Release();
    }
    for (It = Arch->GroupDmgObjs.Items->begin(); It != Arch->GroupDmgObjs.Items->end(); ++It)
    {
        Archetype::Root* const Simple = Archetype::GetSimple(It->Arch.Id);
        if (Simple != nullptr)
            Simple->Release();
    }
}

void CArchGroup::AttachDamageObjs(CObject* Target, Archetype::DamageObjInfoList const& List) const
{
    // FUN_062a0c10. Retail tests neither the list head nor GetSimpleByName's result.
    std::vector<Archetype::DamageObjInfo>::const_iterator It = List.Items->begin();
    for (; It != List.Items->end(); ++It)
    {
        Archetype::DamageObjInfo const& Info = *It;
        CacheString const& HpName = Info.Hardpoint;

        long Handle = -1;
        HardpointInfo HpInfo;
        if (!FindHardpoint_PS(Target->EngineInstanceId, Info.Hardpoint.Value, Handle, HpInfo))
            continue;
        if (Owner->is_damaged_obj_attached(HpName))
            continue;
        Owner->attaching_damaged_obj(HpName);

        Archetype::Root* Simple = Archetype::GetSimpleByName(Info.Arch);
        Simple->load();
        if (Simple->Companion != nullptr)
            Simple->Companion->Load(0);

        CObject* Obj = CObject::Alloc(CObject::ClassObject);
        Simple->create_instance(Obj);
        Obj->open(Simple);

        FHardpointKey ChildKey;
        ChildKey.Name = "DpConnect";
        ChildKey.Crc = DACOM_CRC::GetCRC32("DpConnect");
        FHardpointKey ParentKey;
        ParentKey.Name = Info.Hardpoint.Value;
        ParentKey.Crc = DACOM_CRC::GetCRC32(Info.Hardpoint.Value);
        if (DALib::Hardpoint->ConnectHardpoints((void*)Handle, &ParentKey,
                                                (void*)Obj->EngineInstanceId, &ChildKey) == 0)
        {
            DALib::Engine->UpdateInstance((void*)Obj->EngineInstanceId, 0, nullptr);

            PhySys::CreateParms Phy;
            Phy.Mass = Simple->Mass;
            Phy.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
            Phy.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
            Phy.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
            Phy.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
            Phy.SystemId = Owner->PhysicsSystemId;
            Phy.Type = PhySys::ANIM_TECHNIQUE_MERGED;
            Phy.bPhysicalUnmoveable = 0;
            Phy.bEnableCollisions = 1;
            Phy.bHasInertia = 0;
            Phy.Inertia.x = 0.0f;
            Phy.Inertia.y = 0.0f;
            Phy.Inertia.z = 0.0f;
            strcpy(Phy.Name, PhySys::GetCollisionGroup(Target));
            PhySys::BuildIVP_Real(Obj, Phy);
            PhySys::Attach(Target, Obj);
        }
        Simple->Release();
    }
}

void CArchGroup::SetRelativeHealth(float Frac)
{
    // FUN_062a0270. The int max is exact on the x87; one rounding to float.
    HitPts = (float)((double)Arch->HitPts * Frac);
}

double CArchGroup::GetRelativeHealth() const
{
    // FUN_062a0260. No zero test (a zero max gives inf or NaN); returned unrounded in ST0.
    return (double)HitPts / Arch->HitPts;
}

void CArchGroup::Kill(DamageEntry::SubObjFate FateValue)
{
    State = STATE_KILLED;
    Fate = (int)FateValue;
}

DamageEntry::SubObjFate CArchGroup::GetFate() const
{
    // FUN_062a0160. Fate is an int because Destroy also tests 4 and 5, which SubObjFate does
    // not name.
    return (DamageEntry::SubObjFate)Fate;
}

void CArchGroup::Destroy()
{
    // FUN_062a0450. Only a separable group (Arch+0x14) still on a live root instance does
    // anything beyond State = STATE_DESTROYED.
    if (Arch->bSeparable && RootIndex != -1)
    {
        AttachDamageObjs(Owner, ParentDamageObjs());                      // Arch+0x40
        // IEngine +0xd4; the result is not used.
        (void)DALib::Engine->GetInstanceParent((void*)RootIndex);

        if (Fate == 2)
        {
            // Impulses scaled by Arch+0x18 / Arch+0x1c (float products).
            Vector ParentPush;
            Vector ChildPush;
            Vector Point;
            ComputeExplosiveImpulses(RootIndex, ParentPush, ChildPush, Point);
            ParentPush.x = ParentPush.x * Arch->ParentImpulse;
            ParentPush.y = ParentPush.y * Arch->ParentImpulse;
            ParentPush.z = ParentPush.z * Arch->ParentImpulse;
            ChildPush.x = ChildPush.x * Arch->ChildImpulse;
            ChildPush.y = ChildPush.y * Arch->ChildImpulse;
            ChildPush.z = ChildPush.z * Arch->ChildImpulse;

            // Retail stores the default dampings first and then overwrites them with zeros.
            // Name is itoa(Owner->Id) in radix 0x18 (sic).
            PhySys::DetachParms Parms;
            Parms.LinearDamping = 0.0f;
            Parms.AngularDamping.x = 0.0f;
            Parms.AngularDamping.y = 0.0f;
            Parms.AngularDamping.z = 0.0f;
            Parms.Name[0] = '\0';
            Parms.Mass = Arch->Mass;
            Parms.bHasInertia = 0;
            Parms.Inertia.x = 0.0f;
            Parms.Inertia.y = 0.0f;
            Parms.Inertia.z = 0.0f;
            if (Arch->bHasRotationInertia)
            {
                Parms.Inertia.x = Arch->RotationInertiaX;
                Parms.Inertia.y = Arch->RotationInertiaY;
                Parms.Inertia.z = Arch->RotationInertiaZ;
                Parms.bHasInertia = 1;
            }
            _itoa((int)Owner->Id, Parms.Name, 0x18);

            // FUN_0628a050 splits the group's instance off into a debris object.
            DestroyedObj = DetachInstance(RootIndex, Parms);
            if (DestroyedObj != nullptr)
            {
                AttachDamageObjs(DestroyedObj, GroupDamageObjs());         // Arch+0x4c
                if (DestroyedObj->BodyPtr != nullptr)
                    PhySys::AddImpulseAtPoint(DestroyedObj, ChildPush, Point);
                if (Owner->BodyPtr != nullptr)
                    PhySys::AddImpulseAtPoint(Owner, ParentPush, Point);
                Owner->notify(INotify::ArchGroupSeparated, this);
            }
        }
        else if (Fate == 3 || Fate == 4 || Fate == 5)
        {
            Owner->notify(INotify::ArchGroupDetachedInPlace, this);
            DetachInstanceInPlace(RootIndex);
        }
        RootIndex = -1;
    }
    State = STATE_DESTROYED;
}

bool CArchGroup::IsDead() const
{
    // FUN_062a0130. Killed (2) or destroyed (3).
    return State == STATE_KILLED || State == STATE_DESTROYED;
}

bool CArchGroup::IsDestroyed() const
{
    // FUN_062a0150
    return State == STATE_DESTROYED;
}

void CArchGroup::GetCollisionGroupDesc(CollisionGroupDesc& Out) const
{
    // FUN_062a0280. Extended-precision divide, one rounding. No null test.
    Out.Id = Arch->Id;
    Out.Health = (float)((double)HitPts / Arch->HitPts);
}

CacheString const& CArchGroup::Name() const
{
    // FUN_062a06b0. CollisionGroup's "obj" CacheString at +0x08. No null check on Arch.
    return *(CacheString const*)&Arch->ObjName;
}

unsigned short CArchGroup::GetID() const
{
    // FUN_062a0120
    return Arch->Id;
}

long CArchGroup::GetRootIndex() const
{
    // FUN_062a02a0
    if (State == STATE_DESTROYED)
        return -1;
    return RootIndex;
}

bool CArchGroup::IsInstInGroup(long InstIndex) const
{
    // FUN_062a02b0
    if (State == STATE_DESTROYED)
        return false;
    return IsDescendant(RootIndex, InstIndex);
}

bool CArchGroup::GetVelocity(Vector& Out) const
{
    // FUN_062a0300. The velocity of the CObject riding on the root instance; no null test of
    // the user data.
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    if (State == STATE_DESTROYED || RootIndex == -1)
        return false;
    Out = PhySys::GetVelocity((CObject const*)DALib::Engine->GetInstanceUserData((void*)RootIndex));
    return true;
}

namespace
{
    // The 16-byte element GetBoundingSphere collects (vector insert FUN_062a0ee0).
    struct FArchGroupChildSphere
    {
        FVector Center;
        float Radius;
    };

    static_assert(sizeof(FArchGroupChildSphere) == 0x10, "CArchGroup child-sphere element stride 0x10");
}

void CArchGroup::GetBoundingSphere(float& OutRadius, Vector& OutCenter) const
{
    // FUN_062a07a0 (ported from the listing; Ghidra's .c is corrupt). An AABB in the root's
    // parent frame over the root's and every group descendant's extent sphere; its midpoint is
    // the centre, half its widest span the first radius, which then grows to cover the root
    // sphere and every child sphere. The centre is returned in the root's local frame. Terms
    // retail keeps on the FPU stack are double; each float store is a rounding.

    // A destroyed group enumerates from the invalid handle.
    long const RootInst = (State == STATE_DESTROYED) ? -1 : RootIndex;

    // Retail does not pre-set the outputs; every GetExtra path writes both.
    float RootExtent = 0.0f;
    FVector RootLocal = { 0.0f, 0.0f, 0.0f };
    DALib::Engine->GetInstanceExtent((void*)RootInst, 1, &RootExtent, &RootLocal.X);

    // The root transform is copied; the tail reuses the copy.
    FTransform const RootXform =
        *(FTransform const*)DALib::Engine->GetInstanceTransform((void*)RootInst);
    I3DMathEngine* const Math = Acquire3DMathEngine();

    FVector RootCenter;
    Math->TransformXform(RootCenter, RootXform, RootLocal);   // slot +0x28

    float MinX = (float)((double)RootCenter.X - RootExtent);
    float MaxX = (float)((double)RootCenter.X + RootExtent);
    float MinY = (float)((double)RootCenter.Y - RootExtent);
    float MaxY = (float)((double)RootCenter.Y + RootExtent);
    float MinZ = (float)((double)RootCenter.Z - RootExtent);
    float MaxZ = (float)((double)RootCenter.Z + RootExtent);

    std::vector<FArchGroupChildSphere> ChildSpheres;

    for (long Child = (long)DALib::Engine->GetRelatedInstance((void*)RootInst, 0,
                                                              G_InvalidEngineHandle);
         Child != -1;
         Child = (long)DALib::Engine->GetRelatedInstance((void*)RootInst, 0, (void*)Child))
    {
        // Only instances the owner maps to this group (16-bit compare).
        if (Owner->inst_to_subobj_id(Child) != Arch->Id)
            continue;

        // The radius output is pre-zeroed, the centre output is not.
        float ChildExtent = 0.0f;
        FVector ChildLocal;
        DALib::Engine->GetInstanceExtent((void*)Child, 1, &ChildExtent, &ChildLocal.X);

        FTransform const* const ChildXform =
            (FTransform const*)DALib::Engine->GetInstanceTransform((void*)Child);
        FVector ChildCenter;
        Math->TransformXform(ChildCenter, *ChildXform, ChildLocal);

        FArchGroupChildSphere Entry;
        Entry.Center = ChildCenter;
        Entry.Radius = ChildExtent;
        ChildSpheres.push_back(Entry);

        // Folds on the unrounded candidate; equal or unordered stores it.
        double const CandMinX = (double)ChildCenter.X - ChildExtent;
        if (!(MinX < CandMinX))
            MinX = (float)CandMinX;
        double const CandMaxX = (double)ChildCenter.X + ChildExtent;
        if (!(MaxX > CandMaxX))
            MaxX = (float)CandMaxX;
        double const CandMinY = (double)ChildCenter.Y - ChildExtent;
        if (!(MinY < CandMinY))
            MinY = (float)CandMinY;
        double const CandMaxY = (double)ChildCenter.Y + ChildExtent;
        if (!(MaxY > CandMaxY))
            MaxY = (float)CandMaxY;
        double const CandMinZ = (double)ChildCenter.Z - ChildExtent;
        if (!(MinZ < CandMinZ))
            MinZ = (float)CandMinZ;
        double const CandMaxZ = (double)ChildCenter.Z + ChildExtent;
        if (!(MaxZ > CandMaxZ))
            MaxZ = (float)CandMaxZ;
    }

    // 0.5f is DAT_0639c13c. Retail keeps the X/Y sums unrounded but rounds the Z sum to float
    // before halving it.
    OutCenter.x = (float)(((double)MaxX + MinX) * 0.5);
    OutCenter.y = (float)(((double)MaxY + MinY) * 0.5);
    float const SumZ = (float)((double)MaxZ + MinZ);
    OutCenter.z = (float)((double)SumZ * 0.5);

    // SpanX stays unrounded, SpanY and SpanZ are rounded; NaN takes the later span. The
    // unrounded half is kept for the next compare.
    double const SpanX = (double)MaxX - MinX;
    float const SpanY = (float)((double)MaxY - MinY);
    float const SpanZ = (float)((double)MaxZ - MinZ);
    double MaxSpan = (SpanX > SpanY) ? SpanX : (double)SpanY;
    if (!(MaxSpan > SpanZ))
        MaxSpan = SpanZ;
    double const HalfSpan = MaxSpan * 0.5;
    OutRadius = (float)HalfSpan;

    // Grow to cover the root sphere (compared with the unrounded half, sum order dz, dy, dx).
    {
        double const Dx = (double)OutCenter.x - RootCenter.X;
        double const Dy = (double)OutCenter.y - RootCenter.Y;
        double const Dz = (double)OutCenter.z - RootCenter.Z;
        double const Reach = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx) + RootExtent;
        if (Reach > HalfSpan)
            OutRadius = (float)Reach;
    }

    // Grow to cover every child sphere.
    std::vector<FArchGroupChildSphere>::const_iterator It = ChildSpheres.begin();
    for (; It != ChildSpheres.end(); ++It)
    {
        double const Dx = (double)It->Center.X - OutCenter.x;
        double const Dy = (double)It->Center.Y - OutCenter.y;
        double const Dz = (double)It->Center.Z - OutCenter.z;
        double const Reach = std::sqrt((Dz * Dz + Dy * Dy) + Dx * Dx) + It->Radius;
        if (Reach > OutRadius)
            OutRadius = (float)Reach;
    }

    // InverseTransform (slot +0x3c) into the root's local frame; the radius is not transformed.
    FVector const ParentCenter = { OutCenter.x, OutCenter.y, OutCenter.z };
    FVector LocalCenter;
    Math->InverseTransform(LocalCenter, RootXform, ParentCenter);
    OutCenter.x = LocalCenter.X;
    OutCenter.y = LocalCenter.Y;
    OutCenter.z = LocalCenter.Z;
}

bool CArchGroup::GetCenterOfMass(Vector& Out) const
{
    // FUN_062a0360
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    if (State == STATE_DESTROYED || RootIndex == -1)
        return false;

    // The flag is set before the call, so a re-entrant call cannot recurse through here.
    if (!bBoundsValid)
    {
        bBoundsValid = true;
        GetBoundingSphere(BoundRadius, BoundCenter);
    }

    // BoundCenter is cached in the root's local frame, so it goes back out through the root
    // transform (IEngine +0xa0, math singleton DAT_063fc26c slot +0x28). No null tests.
    FTransform const* const Xform =
        (FTransform const*)DALib::Engine->GetInstanceTransform((void*)RootIndex);
    I3DMathEngine* const Math = Acquire3DMathEngine();

    FVector World;
    Math->TransformXform(World, *Xform, (FVector const&)BoundCenter);
    Out.x = World.X;
    Out.y = World.Y;
    Out.z = World.Z;
    return true;
}

bool CArchGroup::GetRadius(float& Out) const
{
    // FUN_062a0400
    Out = 0.0f;
    if (State == STATE_DESTROYED || RootIndex == -1)
        return false;

    if (!bBoundsValid)
    {
        bBoundsValid = true;
        GetBoundingSphere(BoundRadius, BoundCenter);
    }

    Out = BoundRadius;
    return true;
}

int CArchGroup::InstDepthInGroup(long InstIndex) const
{
    // FUN_062a0690
    if (State == STATE_DESTROYED)
        return -1;
    return Hierarchy::GetDepth(RootIndex, InstIndex);
}

double CArchGroup::GetMaxHitPoints() const
{
    // FUN_062a0200. The int is returned exactly in ST0.
    return Arch->HitPts;
}

float CArchGroup::GetHitPoints() const
{
    return HitPts;
}

void CArchGroup::SetHitPoints(float NewHitPts)
{
    // FUN_062a0210. Notifies (CEqObj vtable +0xc8) only on a strict decrease to a positive
    // value; unordered does not notify.
    float const OldHitPts = HitPts;
    HitPts = NewHitPts;
    if (NewHitPts > 0.0f && NewHitPts < OldHitPts)
        Owner->notify(INotify::ArchGroupDamaged, this);
}

unsigned CArchGroup::GetCollisionGroupType() const
{
    // FUN_062a06c0
    return Arch->Type;
}

// FUN_062a02f0
CObject* CArchGroup::RetreiveDestroyedObj()
{
    CObject* Obj = DestroyedObj;
    DestroyedObj = nullptr;
    return Obj;
}

// The CArchGroup accessors .text:062a06d0..062a0796 are all `MOV EAX,[this+4]` (Arch) and one
// load or LEA off it, without a null test.

// .text:062a06d0: byte [Arch+0x14].
bool CArchGroup::Separable() const
{
    return Arch->bSeparable;
}

// .text:062a06e0: byte [Arch+0x15].
bool CArchGroup::DestroysParent() const
{
    return Arch->bDestroyParent;
}

// .text:062a06f0: byte [Arch+0x16].
bool CArchGroup::IsRootHealthProxy() const
{
    return Arch->bRootHealthProxy;
}

// .text:062a0710: LEA [Arch+0x38].
ID_String const& CArchGroup::DebrisTypeName() const
{
    return Arch->DebrisType;
}

// .text:062a0720: FLD [Arch+0x18].
float CArchGroup::ExplosionParentImpulse() const
{
    return Arch->ParentImpulse;
}

// .text:062a0730: FLD [Arch+0x1c].
float CArchGroup::ExplosionGroupImpulse() const
{
    return Arch->ChildImpulse;
}

// .text:062a0770: [Arch+0x64].
unsigned CArchGroup::LinkedEquipmentType() const
{
    return Arch->LinkedEquipType;
}

// .text:062a0780: FLD [Arch+0x68].
float CArchGroup::LinkedEquipmentDamage() const
{
    return Arch->LinkedEquipHealth;
}

// .text:062a0790: FLD [Arch+0x3c].
float CArchGroup::ExplosionResistance() const
{
    return Arch->ExplosionResistance;
}

Archetype::DamageObjInfoList const& CArchGroup::ParentDamageObjs() const
{
    // .text:062a0740: LEA [Arch+0x40].
    return Arch->DmgObjs;
}

Archetype::DamageObjInfoList const& CArchGroup::GroupDamageObjs() const
{
    // .text:062a0750: LEA [Arch+0x4c].
    return Arch->GroupDmgObjs;
}

Archetype::FuseIgnitionList const& CArchGroup::FuseIgnitions() const
{
    // .text:062a0760: LEA [Arch+0x58].
    return Arch->Fuses;
}

// .text:062a0700: LEA [Arch+0x34].
ID_String const& CArchGroup::ExplosionTypeName() const
{
    return Arch->SeparationExplosion;
}

CArchGroupManager::CArchGroupManager()
    : Owner(nullptr)
    , TreeAllocator(0)
    , TreeCompare(0)
    , Groups(nullptr)
    , TreeNilSlot(0)
    , bTreeMulti(false)
    , Count(0)
{
    // FUN_06280b60. Deviation: retail stores uninitialised stack bytes into +0x04/+0x05.
    Groups = new std::map<unsigned short, CArchGroup*>();
}

CArchGroupManager::~CArchGroupManager()
{
    // FUN_062a1230. Clear()'s steps, a warning when the saved Count (+0x14) was nonzero, then
    // the map's own destructor (`delete Groups` here).
    unsigned OldCount = Count;
    Clear();
    if (OldCount != 0)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s",
              kCArchGroupSource, 0x12, "CArchGroupList: Destructing with ArchGroups still present");
    }
    delete Groups;
    Groups = nullptr;
    Count = 0;
}

// FUN_06280bd0. The CArchGroup* values are copied shallowly.
CArchGroupManager::CArchGroupManager(CArchGroupManager const& Other)
    : Owner(Other.Owner)
    , TreeAllocator(Other.TreeAllocator)
    , TreeCompare(Other.TreeCompare)
    , Groups(new std::map<unsigned short, CArchGroup*>(*Other.Groups))
    , TreeNilSlot(0)
    , bTreeMulti(Other.bTreeMulti)
    , Count(Other.Count)
{
}

// FUN_06280d30. Owner is copied even on self-assignment. The old nodes are freed without
// deleting their CArchGroup values; only the key_compare byte is copied (not the allocator
// byte or _Multi).
CArchGroupManager& CArchGroupManager::operator=(CArchGroupManager const& Other)
{
    Owner = Other.Owner;
    if (this != &Other)
    {
        Groups->clear();
        TreeCompare = Other.TreeCompare;
        *Groups = *Other.Groups;
        Count = Other.Count;
    }
    return *this;
}

bool CArchGroupManager::Init(CEqObj* OwnerObj)
{
    if (Owner == nullptr)
        Owner = OwnerObj;
    return Owner != nullptr;
}

bool CArchGroupManager::AddNewArchGroup(CArchGroup* Group)
{
    // FUN_062a17f0. No null test on Group. A rejected group is not freed (it leaks, as in
    // retail).
    unsigned short const GroupId = Group->GetID();
    if (Groups->find(GroupId) != Groups->end())
    {
        FDUMP(kFdumpWarning,
              "%s(%d) : *** WARNING: CArchGroupList: Insertion failed: Group SUBOBJ_ID[%d] already on list",
              kCArchGroupSource, 0xce, (unsigned)GroupId);
        return false;
    }
    // Retail's Count (+0x14) is the map's own size; the port keeps it in step.
    (*Groups)[GroupId] = Group;
    Count = (unsigned)Groups->size();
    return true;
}

int CArchGroupManager::Clear()
{
    // FUN_062a1680. Returns the old Count.
    int const OldCount = (int)Count;
    for (std::map<unsigned short, CArchGroup*>::iterator It = Groups->begin(); It != Groups->end(); ++It)
    {
        CArchGroup* const Group = It->second;
        if (Group != nullptr)
            delete Group;
    }
    Groups->clear();
    Count = 0;
    return OldCount;
}

int CArchGroupManager::NumLiving() const
{
    // FUN_062a17a0. Groups not Killed (2) or Destroyed (3).
    int LivingCount = 0;
    CArchGrpTraverser Traverser;
    for (CArchGroup const* Group = Traverse(Traverser); Group != nullptr; Group = Traverse(Traverser))
    {
        if (Group->State != CArchGroup::STATE_KILLED && Group->State != CArchGroup::STATE_DESTROYED)
            ++LivingCount;
    }
    return LivingCount;
}

bool CArchGroupManager::DeleteArchGroup(unsigned short GroupId)
{
    // FUN_062a18b0. Unlike DestroyDeadGroups, this deletes the group and erases its entry.
    std::map<unsigned short, CArchGroup*>::iterator It = Groups->find(GroupId);
    if (It == Groups->end())
    {
        FDUMP(kFdumpWarning,
              "%s(%d) : *** WARNING: CArchGroupList: Removal failed: Group SUBOBJ_ID[%d] not on list",
              kCArchGroupSource, 0xe7, (unsigned)GroupId);
        return false;
    }

    CArchGroup* const Group = It->second;
    if (Group != nullptr)
        delete Group;
    Groups->erase(It);
    Count = (unsigned)Groups->size();
    return true;
}

bool CArchGroupManager::DestroyDeadGroups()
{
    // FUN_062a1a50. Values are dereferenced without a null test; retail's two state tests
    // reduce to State == STATE_KILLED. flush_animations is CEqObj vtable +0xcc.
    for (std::map<unsigned short, CArchGroup*>::iterator It = Groups->begin(); It != Groups->end(); ++It)
    {
        if (It->second->State != CArchGroup::STATE_KILLED)
            continue;
        Owner->flush_animations();
        CArchGroup* const Group = It->second;
        DestroyLowerGroups(*Group);
        Group->Destroy();
    }
    return true;
}

bool CArchGroupManager::DestroyGroups(DamageList const& List, bool Param2)
{
    // FUN_062a1960. Param2 selects between two pushes of the same value, so it has no effect.
    (void)Param2;

    for (std::list<DamageEntry>::const_iterator EntryIt = List.Entries.begin(); EntryIt != List.Entries.end(); ++EntryIt)
    {
        if (!SubObjectID::IsArchGroupID(EntryIt->SubObjId))
            continue;
        if (EntryIt->Fate == DamageEntry::SubObjFate::None)
            continue;
        std::map<unsigned short, CArchGroup*>::iterator Found = Groups->find(EntryIt->SubObjId);
        if (Found == Groups->end())
            continue;
        CArchGroup* const Group = Found->second;
        if (Group == nullptr || Group->State == CArchGroup::STATE_KILLED || Group->State == CArchGroup::STATE_DESTROYED)
            continue;
        Group->Kill(EntryIt->Fate);
    }

    // DestroyDeadGroups, inlined.
    for (std::map<unsigned short, CArchGroup*>::iterator It = Groups->begin(); It != Groups->end(); ++It)
    {
        if (It->second->State != CArchGroup::STATE_KILLED)
            continue;
        Owner->flush_animations();
        CArchGroup* const Group = It->second;
        DestroyLowerGroups(*Group);
        Group->Destroy();
    }
    return true;
}

void CArchGroupManager::DestroyDeadGroup(CArchGroup& Group)
{
    // FUN_062a1c10
    DestroyLowerGroups(Group);
    Group.Destroy();
}

void CArchGroupManager::DestroyLowerGroups(CArchGroup const& DeadGroup)
{
    // FUN_062a1ac0. Every other group under DeadGroup's instance is either dead (recurse, then
    // Destroy()) or still alive, an "UnDead lower group" that retail logs and deletes. Values
    // are dereferenced without a null test.
    std::map<unsigned short, CArchGroup*>::iterator It = Groups->begin();
    while (It != Groups->end())
    {
        CArchGroup* const Entry = It->second;
        if (DeadGroup.GetID() != Entry->GetID() && DeadGroup.IsInstInGroup(Entry->GetRootIndex()))
        {
            if (Entry->State == CArchGroup::STATE_KILLED || Entry->State == CArchGroup::STATE_DESTROYED)
            {
                DestroyLowerGroups(*Entry);
                Entry->Destroy();
            }
            else
            {
                char const* const EntryName = Entry->Name().Value;
                char const* const DeadName = DeadGroup.Name().Value;
                FDUMP(kFdumpWarning,
                      "%s(%d) : *** WARNING: DestroyLowerGroups(%s): Deleting UnDead lower group[%s].",
                      kCArchGroupSource, 0x146, DeadName, EntryName);
                unsigned const EntryId = Entry->GetID();
                long const EntryRoot = Entry->GetRootIndex();
                FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: Dead DUMP: Inst:%d\t ID:%d",
                      kCArchGroupSource, 0x148, EntryRoot, EntryId);

                // Retail decrements the iterator (_Dec), deletes and erases, then the loop's
                // _Inc resumes after the erased node. Retail quirk kept: when the erased node
                // was the first one, _Dec yields the head and _Inc from it lands in the
                // rightmost node's subtree, skipping everything in between. The port resumes
                // at the rightmost node; retail resumes at its left child when it has one
                // (not visible through std::map). If the erased first node is the root, retail
                // reads the freed node (UB) and reaches the rightmost node too. DeadGroup's
                // own node keeps the map non-empty.
                bool const bWasFirst = (It == Groups->begin());
                std::map<unsigned short, CArchGroup*>::iterator const Erased = It;
                if (!bWasFirst)
                    --It;
                if (Entry != nullptr)
                    delete Entry;
                Groups->erase(Erased);
                Count = (unsigned)Groups->size();
                if (bWasFirst)
                {
                    It = Groups->end();
                    --It;
                    continue;
                }
            }
        }
        ++It;
    }
}

CArchGroup* CArchGroupManager::FindByID(unsigned short GroupId)
{
    // FUN_062a1540 (serves both overloads)
    std::map<unsigned short, CArchGroup*>::iterator It = Groups->find(GroupId);
    if (It == Groups->end())
        return nullptr;
    return It->second;
}

CArchGroup const* CArchGroupManager::FindByID(unsigned short GroupId) const
{
    return const_cast<CArchGroupManager*>(this)->FindByID(GroupId);
}

int CArchGroupManager::Size() const
{
    // FUN_06263bb0 (ICF fold)
    return (int)Count;
}

bool CArchGroupManager::IsOnMap(unsigned short GroupId, bool CheckDestroyed) const
{
    // FUN_062a1460. With CheckDestroyed, whether the group is Destroyed (no null test).
    std::map<unsigned short, CArchGroup*>::const_iterator It = Groups->find(GroupId);
    if (It == Groups->end())
        return false;
    if (CheckDestroyed)
        return It->second->State == CArchGroup::STATE_DESTROYED;
    return true;
}

CArchGroup* CArchGroupManager::FindByName(CacheString const& Name)
{
    // FUN_062a14e0. First living group whose interned name pointer matches. No null test on the
    // value.
    CArchGroup* Result = nullptr;
    for (std::map<unsigned short, CArchGroup*>::iterator It = Groups->begin();
         It != Groups->end() && Result == nullptr; ++It)
    {
        CArchGroup* const Group = It->second;
        if (Group->Name().Value == Name.Value && Group->State != CArchGroup::STATE_KILLED && Group->State != CArchGroup::STATE_DESTROYED)
            Result = Group;
    }
    return Result;
}

CArchGroup const* CArchGroupManager::FindByName(CacheString const& Name) const
{
    return const_cast<CArchGroupManager*>(this)->FindByName(Name);
}

// FUN_06280ec0
CArchGrpTraverser::CArchGrpTraverser()
    : AtStart(true)
{
}

// FUN_06280ed0
void CArchGrpTraverser::Restart()
{
    AtStart = true;
}

CArchGroup* CArchGroupManager::Traverse(CArchGrpTraverser& Traverser)
{
    // FUN_062a15a0. Deviation: the port keeps the current key and steps with upper_bound;
    // retail keeps the node and increments it, which differs only if that node was erased
    // between calls (retail then increments a freed node).
    if (!Traverser.AtStart)
    {
        std::map<unsigned short, CArchGroup*>::iterator Next = Groups->upper_bound(Traverser.CurrentKey);
        if (Next != Groups->end())
        {
            Traverser.CurrentKey = Next->first;
            return Next->second;
        }
        Traverser.AtStart = true;
    }
    else if (Count != 0)
    {
        Traverser.AtStart = false;
        std::map<unsigned short, CArchGroup*>::iterator First = Groups->begin();
        Traverser.CurrentKey = First->first;
        return First->second;
    }
    return nullptr;
}

// FUN_062a15f0, a JMP thunk to the non-const overload.
CArchGroup const* CArchGroupManager::Traverse(CArchGrpTraverser& Traverser) const
{
    return const_cast<CArchGroupManager*>(this)->Traverse(Traverser);
}

unsigned short CArchGroupManager::InstToSubObjID(long InstIndex) const
{
    // FUN_062a1600. The group holding InstIndex at the smallest depth; the walk stops at depth 0.
    // No null test on the value.
    unsigned short Result = SUBOBJ_ID_NONE;
    if (InstIndex == -1)
        return Result;

    int BestDepth = 0x7fff;
    for (std::map<unsigned short, CArchGroup*>::const_iterator It = Groups->begin();
         It != Groups->end() && BestDepth > 0; ++It)
    {
        CArchGroup const* const Group = It->second;
        int const Depth = Group->InstDepthInGroup(InstIndex);
        if (Depth != -1 && Depth < BestDepth)
        {
            BestDepth = Depth;
            Result = Group->GetID();
        }
    }
    return Result;
}

// Export ordinal 1835, .rdata 0x0639d9dc. Never read in Common.dll; exported for symbol parity.
int const CArchGroupManager::MAX_GROUP_DEPTH = 0x7FFF;
