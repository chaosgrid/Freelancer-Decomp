// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CAttachmentObj, CNonPhysAttachment, CPhysAttachment. Declared in common_cequip.h; helpers
// shared with the sibling TUs are in common_cequip_internal.h (namespace CmnCequip). Engine
// instances are IEngine `void*` handles; the attachments keep them as `long` ids, as retail does.
#include "common_cequip.h"
#include "x86math.h"

#include "common_cobject.h"
#include "common_csys.h"     // Acquire3DMathEngine
#include "common_ini.h"
#include "common_physys.h"
#include "DALib.h"
#include "dacom.h"

#include <stdlib.h>
#include "Equipment/common_cequip_internal.h"

using namespace CmnCequip;

namespace
{
    // FUN_0628f700 (VC6 vector _Insert_n); every call site inserts one element at the end.
    void AppendPartMapEntry(CObject* Owner, unsigned PartId, long InstanceId, bool bShieldPart)
    {
        FPartMapEntry Entry;
        Entry.PartId = PartId;
        Entry.EngineInstanceId = InstanceId;
        Entry.bShieldPart = bShieldPart ? 1 : 0;
        Owner->PartMap.push_back(Entry);
    }

    // Inlined in EnableCollisions(false): the first entry for InstanceId is erased and its
    // shield byte handed back; false when there is none. No null check on Owner.
    bool RemovePartMapEntryByInstance(CObject* Owner, long InstanceId, unsigned char& OutFlag)
    {
        for (std::vector<FPartMapEntry>::iterator It = Owner->PartMap.begin(); It != Owner->PartMap.end(); ++It)
        {
            if (It->EngineInstanceId == InstanceId)
            {
                OutFlag = It->bShieldPart;
                Owner->PartMap.erase(It);
                return true;
            }
        }
        return false;
    }

    // The 0x48-byte hardpoint record IHardpoint::GetHardpoint copies out, as FUN_062940c0
    // reads it: +0x00 joint type (2 = revolute), +0x04 position, +0x10 orientation,
    // +0x34 rotation axis, +0x40/+0x44 angle limits.
    struct FHardpointData
    {
        unsigned Type;      // +0x00
        Vector Position;    // +0x04
        Matrix Orient;      // +0x10
        Vector Axis;        // +0x34
        float Min;          // +0x40
        float Max;          // +0x44
    };
    static_assert(sizeof(FHardpointData) == 0x48, "FHardpointData 0x48");

    // Inlined twice in FUN_062940c0. Retail quirk: the range is wrapped against 360 (degrees,
    // DAT_0639c9d8) in steps of 2pi (DAT_0639c9e0). Kept on the x87 stack, so double here.
    double WrapLimitRange(double Range)
    {
        while (Range < 0.0)
            Range += 6.2831854820251465;
        while (Range >= 360.0)
            Range -= 6.2831854820251465;
        return Range;
    }

    // FUN_062940c0 (cdecl). Connects ChildInst's ChildHp to
    // ParentInst's ParentHp. When both hardpoints are revolute (type 2) it builds a revolute
    // joint (narrower of the two limit ranges, orientation = parent * child, axis = child axis in
    // that frame) through IEngine::CreateJoint; otherwise IHardpoint::ConnectHardpoints. Returns
    // the child's IEngine::GetInstanceRelative block, or null when either archetype or
    // hardpoint lookup fails. Both archetype handles are released, also when they are -1.
    unsigned long* ConnectAttachedHardpoints(long ParentInst, char const* ParentHp, long ChildInst,
                                             char const* ChildHp)
    {
        unsigned long* Result = nullptr;
        void* ParentHandle = DALib::Engine->GetInstanceArchetype((void*)ParentInst);
        if (ParentHandle != (void*)-1)
        {
            FHardpointKey ParentKey;
            ParentKey.Name = ParentHp;
            ParentKey.Crc = DACOM_CRC::GetCRC32(ParentHp);
            FHardpointData ParentData;
            // Retail tests only AL.
            if ((unsigned char)DALib::Hardpoint->GetHardpoint(ParentHandle, &ParentKey, &ParentData) != 0)
            {
                void* ChildHandle = DALib::Engine->GetInstanceArchetype((void*)ChildInst);
                if (ChildHandle != (void*)-1)
                {
                    FHardpointKey ChildKey;
                    ChildKey.Name = ChildHp;
                    ChildKey.Crc = DACOM_CRC::GetCRC32(ChildHp);
                    FHardpointData ChildData;
                    if ((unsigned char)DALib::Hardpoint->GetHardpoint(ChildHandle, &ChildKey, &ChildData) != 0)
                    {
                        if (ChildData.Type == 2 && ParentData.Type == 2)
                        {
                            FJointRelative Joint;
                            Joint.Type = 2;
                            Joint.Field_0x54 = 0;  // retail: unwritten stack (see FJointRelative)
                            Joint.Field_0x58 = 0;
                            Joint.ParentPos = ParentData.Position;
                            Joint.ChildPos = ChildData.Position;
                            I3DMathEngine* Math = Acquire3DMathEngine();  // DAT_063fc114
                            Matrix Combined;
                            Math->MulMatrix((FMatrix&)Combined, (FMatrix const&)ParentData.Orient,
                                            (FMatrix const&)ChildData.Orient);
                            Joint.Orient = Combined;
                            Vector Axis;
                            Math->TransformMatrix((FVector&)Axis, (FMatrix const&)Joint.Orient,
                                                  (FVector const&)ChildData.Axis);
                            Joint.Axis = Axis;

                            // A range >= 2pi opens the limits to [-pi, pi]; a NaN range takes
                            // the wrap path.
                            double ParentRange = (double)ParentData.Max - ParentData.Min;
                            if (ParentRange >= 6.2831855f)
                            {
                                ParentData.Min = -3.1415927f;
                                ParentData.Max = 3.1415927f;
                            }
                            else
                            {
                                ParentRange = WrapLimitRange(ParentRange);
                            }
                            double ChildRange = (double)ChildData.Max - ChildData.Min;
                            if (ChildRange >= 6.2831855f)
                            {
                                ChildData.Min = -3.1415927f;
                                ChildData.Max = 3.1415927f;
                            }
                            else
                            {
                                ChildRange = WrapLimitRange(ChildRange);
                            }
                            // The narrower range's limits (the child's on a tie or NaN).
                            if (ParentRange < ChildRange)
                            {
                                Joint.Min = ParentData.Min;
                                Joint.Max = ParentData.Max;
                            }
                            else
                            {
                                Joint.Min = ChildData.Min;
                                Joint.Max = ChildData.Max;
                            }
                            DALib::Engine->CreateJoint((void*)ParentInst, (void*)ChildInst,
                                                       (unsigned long const*)&Joint);
                        }
                        else
                        {
                            FHardpointKey ChildConnectKey;
                            ChildConnectKey.Name = ChildHp;
                            ChildConnectKey.Crc = DACOM_CRC::GetCRC32(ChildHp);
                            FHardpointKey ParentConnectKey;
                            ParentConnectKey.Name = ParentHp;
                            ParentConnectKey.Crc = DACOM_CRC::GetCRC32(ParentHp);
                            DALib::Hardpoint->ConnectHardpoints((void*)ParentInst, &ParentConnectKey,
                                                                (void*)ChildInst, &ChildConnectKey);
                        }
                        Result = DALib::Engine->GetInstanceRelative((void*)ChildInst);
                    }
                }
                DALib::Engine->ReleaseHandle(ChildHandle);
            }
        }
        DALib::Engine->ReleaseHandle(ParentHandle);
        return Result;
    }
}

CAttachmentObj::~CAttachmentObj()
{
}

// GetRootIndex / GetPhysicsOwner / EnableCollisions / Connect / Disconnect are pure in retail
// (_purecall slots 1-5 of .rdata:0639c6b0) and have no CAttachmentObj body or export.

CNonPhysAttachment::CNonPhysAttachment()
{
    // FUN_062937a0. Deviation: retail leaves CachedShieldFlag (+0xd) unwritten; the port zeroes it.
    EngineInstanceId = -1;
    HpCrc = 0;
    bCollisions = 0;
    CachedShieldFlag = 0;
}

CNonPhysAttachment::CNonPhysAttachment(const CNonPhysAttachment& Other)
{
    // FUN_06292c00
    EngineInstanceId = Other.EngineInstanceId;
    HpCrc = Other.HpCrc;
    bCollisions = Other.bCollisions;
    CachedShieldFlag = Other.CachedShieldFlag;
}

CNonPhysAttachment& CNonPhysAttachment::operator=(const CNonPhysAttachment& Other)
{
    // FUN_06292c30; no self test.
    EngineInstanceId = Other.EngineInstanceId;
    HpCrc = Other.HpCrc;
    bCollisions = Other.bCollisions;
    CachedShieldFlag = Other.CachedShieldFlag;
    return *this;
}

CNonPhysAttachment::~CNonPhysAttachment()
{
    // FUN_06292bf0, ICF-folded with ~CAttachmentObj. The engine instance is not destroyed: on a
    // FATE_STAY disconnect it stays a child of the owner's instance (with its PartMap entry) and
    // goes away with the owner's instance tree.
}

long CNonPhysAttachment::GetRootIndex() const
{
    return EngineInstanceId;
}

CObject* CNonPhysAttachment::GetPhysicsOwner() const
{
    // FUN_062937c0: only -1 means "no instance".
    if (EngineInstanceId == -1)
        return nullptr;
    return (CObject*)DALib::Engine->GetInstanceUserData((void*)EngineInstanceId);
}

void CNonPhysAttachment::EnableCollisions(bool bEnable)
{
    // RVA 0x337e0: nothing unless the state changes and there is an instance.
    if ((unsigned char)bEnable == bCollisions)
        return;
    if (EngineInstanceId == -1)
        return;

    CObject* Owner = GetPhysicsOwner();
    if (bEnable)
    {
        // The entry comes back with the cached shield byte.
        AppendPartMapEntry(Owner, HpCrc, EngineInstanceId, CachedShieldFlag != 0);
    }
    else
    {
        // With no entry the cache keeps its value.
        unsigned char RemovedFlag = 0;
        if (RemovePartMapEntryByInstance(Owner, EngineInstanceId, RemovedFlag))
            CachedShieldFlag = RemovedFlag;
    }
    bCollisions = bEnable ? 1 : 0;
}

bool CNonPhysAttachment::Connect(CAttachedEquip* Equip)
{
    // FUN_062938c0
    Archetype::AttachedEquipment* Arch = (Archetype::AttachedEquipment*)Equip->Arch;
    EngineInstanceId = Arch->create_instance(nullptr);
    DALib::Engine->UpdateInstance((void*)EngineInstanceId, 0, nullptr);
    if (EngineInstanceId == -1)
        return false;

    char const* ChildHp = Arch->HpChild.Value;
    if (ConnectAttachedHardpoints(Equip->GetParentConnector(false),
                                  Equip->GetParentHPName().Value,
                                  EngineInstanceId, ChildHp) == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: CAttachedEquip: Failed to connect %s on %s to %s (Ignorable)",
              kCAttachedEquipSource, 0x1d0, Arch->HpChild.Value, Arch->DaArchetype.Value,
              Equip->GetParentHPName().Value);
        return false;
    }

    HpCrc = DACOM_CRC::GetCRC32(Equip->GetParentHPName().Value);
    void* ParentUser = DALib::Engine->GetInstanceUserData((void*)Equip->GetParentConnector(false));
    DALib::Engine->SetInstanceUserData((void*)EngineInstanceId, ParentUser);

    AppendPartMapEntry((CObject*)ParentUser, HpCrc, EngineInstanceId, false);

    void* Child = DALib::Engine->GetRelatedInstance((void*)EngineInstanceId, 0, (void*)-1);
    while (Child != (void*)-1)
    {
        DALib::Engine->SetInstanceUserData(Child, ParentUser);
        Child = DALib::Engine->GetRelatedInstance((void*)EngineInstanceId, 0, Child);
    }
    bCollisions = 1;
    return true;
}

CObject* CNonPhysAttachment::Disconnect(CAttachedEquip* Equip, bool bMakeDebris)
{
    // FUN_06293a60
    CObject* Debris = nullptr;
    if (bMakeDebris)
    {
        void* Inst = (void*)EngineInstanceId;
        CObject* ParentObj = (CObject*)DALib::Engine->GetInstanceUserData(Inst);
        if (ParentObj != nullptr)
        {
            // Erase the first PartMap entry whose PartId (hardpoint CRC) is HpCrc.
            for (std::vector<FPartMapEntry>::iterator It = ParentObj->PartMap.begin();
                 It != ParentObj->PartMap.end(); ++It)
            {
                if (It->PartId == HpCrc)
                {
                    ParentObj->PartMap.erase(It);
                    break;
                }
            }
        }

        void* ParentInst = DALib::Engine->GetInstanceParent(Inst);
        DALib::Engine->DestroyJoint(ParentInst, Inst);
        Debris = CObject::Alloc(CObject::ClassEquipmentObj);
        Debris->open(Equip->Arch);
        IEngineInstance* const DebrisInstance = Debris;
        DALib::Engine->SetInstanceTransform(Inst, DebrisInstance);

        // Retail stores the CreateParms defaults and then overwrites them: what reaches
        // BuildIVP_Real is ZERO linear and angular damping (verified in the raw bytes), radix
        // 0x18 for the name. Pad27 is not written by retail.
        PhySys::CreateParms Parms;
        Parms.Mass = Equip->Arch->Mass;
        Parms.LinearDamping = 0.0f;
        Parms.AngularDamping[0] = 0.0f;
        Parms.AngularDamping[1] = 0.0f;
        Parms.AngularDamping[2] = 0.0f;
        Parms.SystemId = ParentObj->PhysicsSystemId;
        Parms.Type = 2;
        Parms.bPhysicalUnmoveable = 0;
        Parms.bEnableCollisions = 1;
        Parms.Name[0] = '\0';
        _itoa((int)Equip->Owner->Id, Parms.Name, 0x18);
        Parms.bHasInertia = 0;
        Parms.Pad27 = 0;
        Parms.Inertia.x = 0.0f;
        Parms.Inertia.y = 0.0f;
        Parms.Inertia.z = 0.0f;
        if (Equip->Arch->bHasRotationInertia)
        {
            Parms.bHasInertia = 1;
            Parms.Inertia.x = Equip->Arch->RotationInertiaX;
            Parms.Inertia.y = Equip->Arch->RotationInertiaY;
            Parms.Inertia.z = Equip->Arch->RotationInertiaZ;
        }
        PhySys::BuildIVP_Real(Debris, Parms);

        if (Debris->BodyPtr != nullptr)
        {
            Vector ParentVel = PhySys::GetVelocity(ParentObj);
            PhySys::AddToVelocity(Debris, ParentVel);
        }
        else
        {
            Archetype::Equipment* Arch = Equip->Arch;
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: CAttachedEquip: Failed to create physics for [%s]: %s (Ignorable)",
                  kCAttachedEquipSource, 0x224, Arch->Nickname.get_string(), Arch->DaArchetype.Value);
            Debris->Release();
            Debris = nullptr;
        }
    }
    else
    {
        // FUN_0628a410
        DetachInstanceInPlace(GetRootIndex());
    }

    EngineInstanceId = -1;
    HpCrc = 0;
    bCollisions = 0;
    return Debris;
}

CPhysAttachment::CPhysAttachment()
{
    EquipObject = nullptr;
}

CPhysAttachment::CPhysAttachment(const CPhysAttachment& Other)
{
    // RVA 0x32cc0: a shallow pointer copy, no AddRef.
    EquipObject = Other.EquipObject;
}

CPhysAttachment::~CPhysAttachment()
{
    if (EquipObject != nullptr)
    {
        EquipObject->Release();
        EquipObject = nullptr;
    }
}

long CPhysAttachment::GetRootIndex() const
{
    if (EquipObject == nullptr)
        return -1;
    return EquipObject->EngineInstanceId;
}

CObject* CPhysAttachment::GetPhysicsOwner() const
{
    return EquipObject;
}

void CPhysAttachment::EnableCollisions(bool bEnable)
{
    // RVA 0x33d60: only on a change of the physics collision state.
    if (EquipObject == nullptr)
        return;
    if (PhySys::GetCollisionState(EquipObject) != bEnable)
        PhySys::SetCollisionState(EquipObject, bEnable);
}

bool CPhysAttachment::Connect(CAttachedEquip* Equip)
{
    // FUN_06293d90
    CEqObj* ParentObj = Equip->Owner;
    Archetype::AttachedEquipment* Arch = (Archetype::AttachedEquipment*)Equip->Arch;

    // create_instance writes the instance through IEngineInstance; the returned id is not stored.
    EquipObject = CObject::Alloc(CObject::ClassEquipmentObj);
    EquipObject->open(Arch);
    Arch->create_instance(EquipObject);
    DALib::Engine->UpdateInstance((void*)EquipObject->EngineInstanceId, 0, nullptr);

    if (EquipObject == nullptr)
    {
        // Unreachable (EquipObject was dereferenced above). Retail then calls Release on the
        // null pointer.
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: CAttachedEquip: Object (%s) failed to create attachment instance (Ignorable)\n",
              kCAttachedEquipSource, 0x2ab, ParentObj->Archetype->Nickname.get_string());
        EquipObject->Release();
        EquipObject = nullptr;
        return false;
    }

    // Retail evaluation order.
    char const* ChildHp = Arch->HpChild.Value;
    long const Inst = EquipObject->EngineInstanceId;
    char const* ParentHp = Equip->GetParentHPName().Value;
    long const ParentInst = Equip->GetParentConnector(false);
    if (ConnectAttachedHardpoints(ParentInst, ParentHp, Inst, ChildHp) == nullptr)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: CAttachedEquip: Failed to connect %s on %s to %s (Ignorable)",
              kCAttachedEquipSource, 0x2a5, Arch->HpChild.Value, Arch->DaArchetype.Value,
              Equip->GetParentHPName().Value);
        EquipObject->Release();
        EquipObject = nullptr;
        return false;
    }

    // Collisions start off (enabled after Attach below).
    PhySys::CreateParms Parms;
    Parms.Mass = 1.0f;
    Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
    Parms.AngularDamping[0] = PhySys::DEFAULT_ANGULAR_DAMPING.x;
    Parms.AngularDamping[1] = PhySys::DEFAULT_ANGULAR_DAMPING.y;
    Parms.AngularDamping[2] = PhySys::DEFAULT_ANGULAR_DAMPING.z;
    Parms.SystemId = ParentObj->PhysicsSystemId;
    Parms.Type = 2;
    Parms.bPhysicalUnmoveable = 0;
    Parms.bEnableCollisions = 0;
    Parms.Name[0] = '\0';
    _itoa((int)ParentObj->Id, Parms.Name, 0x18);
    Parms.bHasInertia = 0;
    Parms.Pad27 = 0;
    Parms.Inertia.x = 0.0f;
    Parms.Inertia.y = 0.0f;
    Parms.Inertia.z = 0.0f;
    PhySys::BuildIVP_Real(EquipObject, Parms);

    if (EquipObject->BodyPtr != nullptr)
    {
        PhySys::Attach(ParentObj, EquipObject);
        EquipObject->enable_collisions_r(true);
        EquipObject->AddRef();
        return true;
    }

    FDUMP(0x100001, "%s(%d) : *** ERROR: CAttachedEquip: Failed to create physics for [%s]: %s (Ignorable)",
          kCAttachedEquipSource, 0x29d, Arch->Nickname.get_string(), Arch->DaArchetype.Value);
    EquipObject->Release();
    EquipObject = nullptr;
    return false;
}

CObject* CPhysAttachment::Disconnect(CAttachedEquip* Equip, bool bMakeDebris)
{
    // FUN_06293fd0
    CObject* Debris = nullptr;
    if (bMakeDebris)
    {
        Archetype::Equipment* Arch = Equip->Arch;
        PhySys::DetachParms Parms;
        Parms.LinearDamping = PhySys::DEFAULT_LINEAR_DAMPING;
        Parms.AngularDamping = PhySys::DEFAULT_ANGULAR_DAMPING;
        Parms.Name[0] = '\0';
        Parms.Mass = Arch->Mass;
        Parms.bHasInertia = 0;
        Parms.Inertia.x = 0.0f;
        Parms.Inertia.y = 0.0f;
        Parms.Inertia.z = 0.0f;
        if (Arch->bHasRotationInertia)
        {
            Parms.bHasInertia = 1;
            Parms.Inertia.x = Arch->RotationInertiaX;
            Parms.Inertia.y = Arch->RotationInertiaY;
            Parms.Inertia.z = Arch->RotationInertiaZ;
        }
        Debris = DetachInstance(GetRootIndex(), Parms);   // FUN_0628a050
    }
    else
    {
        DetachInstanceInPlace(GetRootIndex());           // FUN_0628a410
    }

    EquipObject->Release();
    EquipObject = nullptr;
    return Debris;
}
