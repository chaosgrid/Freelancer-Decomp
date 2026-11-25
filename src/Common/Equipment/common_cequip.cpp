// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CEquip, CInternalEquip, CExternalEquip, CEquipTraverser, CEquipManager.
#include "common_cequip.h"
#include "x86math.h"

#include "common_animdb.h"

#include "common_cobject.h"
#include "common_csys.h"
#include "common_fuses.h"
#include "common_iface.h"    // EquipStatus, EquipStatusFlag* (CEquip::GetStatus)
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
    const char kCExternalEquipSource[] = "E:\\FL\\Scratch\\Source\\Common\\CExternalEquip.cpp";
    const unsigned kInternalListMask = 0xc10003u;
    const unsigned kExternalListMask = 0xff3efffcu;
    const unsigned kExternalEquipMask = 0x1fe3u;
    const unsigned kInternalEquipMask = 0x1ff0000u;

    // DAT_063fc198: CEquip::IdentifyEquipment's 0x40-byte result buffer.
    char GIdentifyEquipmentBuf[0x40];

    // Deviation: retail keeps the list node in CEquipTraverser+0xc and steps node->next; the
    // port keeps the CEquip* and rescans from begin() past it -- the same element while the
    // list is unchanged between calls. If `After` was erased, retail steps through the freed
    // node; the port ends that list instead. The filters are retail's.
    CEquip* NextMatching(std::list<CEquip*>* List, CEquip* After, unsigned TypeMask,
                         bool bIncludeDestroyed)
    {
        bool bSkip = After != nullptr;
        std::list<CEquip*>::iterator It = List->begin();
        for (; It != List->end(); ++It)
        {
            CEquip* Equip = *It;
            if (bSkip)
            {
                if (Equip == After)
                    bSkip = false;
                continue;
            }
            if (Equip == nullptr)
                continue;
            if ((TypeMask & Equip->GetType()) == 0)
                continue;
            if (!bIncludeDestroyed && Equip->IsDestroyed())
                continue;
            return Equip;
        }
        return nullptr;
    }

    // NextMatching back to front, for CEquipManager::ReverseTraverse (RVA 0x3b530 / 0x3b540).
    CEquip* PrevMatching(std::list<CEquip*>* List, CEquip* Before, unsigned TypeMask,
                        bool bIncludeDestroyed)
    {
        bool bSkip = Before != nullptr;
        std::list<CEquip*>::reverse_iterator It = List->rbegin();
        for (; It != List->rend(); ++It)
        {
            CEquip* Equip = *It;
            if (bSkip)
            {
                if (Equip == Before)
                    bSkip = false;
                continue;
            }
            if (Equip == nullptr)
                continue;
            if ((TypeMask & Equip->GetType()) == 0)
                continue;
            if (!bIncludeDestroyed && Equip->IsDestroyed())
                continue;
            return Equip;
        }
        return nullptr;
    }
}

namespace CmnCequip
{
    // The Watchable a BaseWatcher currently watches (null when the target died), as the raw
    // pointer the action classes then step back 8 bytes from to reach the IObjRW.
    void* WatchedTarget(BaseWatcher const& Watcher)
    {
        return (void*)Watcher.target();
    }
}

CEquip::CEquip(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
               Archetype::Equipment const* ArchObj, bool bMissionFlag)
{
    Owner = OwnerObj;
    Id = EquipId;
    // Retail takes the archetype as const (PBU in every CE* constructor's mangling) and later
    // calls non-const members on it (load/Release/create_instance) through the same field.
    Arch = const_cast<Archetype::Equipment*>(ArchObj);
    bActive = 0;
    bDestroyed = 0;
    bMission = bMissionFlag ? 1 : 0;
    Type = TypeBits;
    NotifyA = nullptr;
    NotifyB = nullptr;
}

CEquip::CEquip()
{
    // RVA 0x26d60. bDestroyed (+0x11) is left untouched.
    Owner = nullptr;
    Id = SUBOBJ_ID_NONE;
    Arch = nullptr;
    bActive = 0;
    bMission = 0;
    Type = 0;
    NotifyA = nullptr;
    NotifyB = nullptr;
}

CEquip::~CEquip()
{
}

void* CEquip::operator new(size_t Size) noexcept
{
    // RVA 0x9d560: msvcrt malloc.
    return malloc(Size);
}

void CEquip::operator delete(void* Block)
{
    // RVA 0x9d570: msvcrt free.
    free(Block);
}

// RVA 0x3afd0 (ICF-shared).
void CEquip::ConnectServerEquip(INotify* NotifyParam)
{
    NotifyA = NotifyParam;
}

// RVA 0xbd950 (ICF-shared).
void CEquip::ConnectClientEquip(INotify* NotifyParam)
{
    NotifyB = NotifyParam;
}

void CEquip::NotifyDisconnecting(INotify* NotifyParam)
{
    // RVA 0x3afe0: clears the first matching sink only.
    if (NotifyA == NotifyParam) {
        NotifyA = nullptr;
        return;
    }
    if (NotifyB == NotifyParam) {
        NotifyB = nullptr;
    }
}

// RVA 0x3b010
void CEquip::Notify(INotify::Event Code, void* Param)
{
    if (NotifyA != nullptr)
        NotifyA->notify(Code, Param);
    if (NotifyB != nullptr)
        NotifyB->notify(Code, Param);
}

bool CEquip::IsActive() const
{
    return bActive != 0;
}

bool CEquip::IsDestroyed() const
{
    return bDestroyed != 0;
}

bool CEquip::IsFunctioning() const
{
    // FUN_06286d20. Vtable slot 4 is IsDisabled, not IsControllerEnabled: their base bodies
    // are ICF-folded (0629ba00), but CEThruster overrides only IsControllerEnabled, so the
    // wrong one makes every registered thruster "not functioning".
    if (IsActive()) {
        if (!IsDisabled() && !IsDestroyed())
            return true;
    }
    return false;
}

bool CEquip::Activate(bool bOn)
{
    // RVA 0x3aed0: notifies only on a real transition, before flipping bActive.
    if (bOn) {
        if (bActive == 0) {
            Notify(INotify::EquipActivating, nullptr);
        }
        bActive = 1;
    } else {
        if (bActive != 0) {
            Notify(INotify::EquipDeactivating, nullptr);
        }
        bActive = 0;
    }
    return IsActive();
}

void CEquip::Destroy()
{
    // RVA 0x3af40
    bDestroyed = 1;
    Notify(INotify::EquipDestroying, (void*)(uintptr_t)Id);
}

float CEquip::GetMaxHitPoints() const
{
    // FUN_0629af70; no Arch null test.
    return Arch->HitPts;
}

void CEquip::SetHitPoints(float Hit)
{
    // RVA 0x8d730 (an ICF-shared empty body).
    (void)Hit;
}

bool CEquip::IsDisabled() const
{
    return false;
}

bool CEquip::EnableController()
{
    return false;
}

bool CEquip::DisableController()
{
    // RVA 0x3ba00
    return false;
}

bool CEquip::IsControllerEnabled() const
{
    return false;
}

bool CEquip::Update(float Dt, unsigned Flags)
{
    (void)Dt;
    (void)Flags;
    return !IsDestroyed();
}

float CEquip::GetHitPoints() const
{
    // RVA 0x3af80: a tail jump to the virtual GetMaxHitPoints; no destroyed check.
    return GetMaxHitPoints();
}

double CEquip::GetRelativeHealth() const
{
    // FUN_0629af90: the quotient is left unrounded in ST0; a NaN max divides.
    float const MaxHit = GetMaxHitPoints();
    if (MaxHit == 0.0f)
        return 0.0f;
    return (double)GetHitPoints() / (double)MaxHit;
}

bool CEquip::IsTemporary() const
{
    return bMission != 0;
}

bool CEquip::CanDelete() const
{
    // RVA 0x3adc0: equipment on a solar can never be deleted.
    if (Owner != nullptr && (Owner->ObjectClass & CObject::ClassSolar) == CObject::ClassSolar)
        return false;
    return IsDestroyed();
}

bool CEquip::IsLootable() const
{
    // FUN_0629ade0: the archetype allows it (no Arch test) and it maps to a Good.
    if (Arch->bLootable)
        return Arch2Good(Arch->get_id()) != 0;
    return false;
}

void CEquip::GetStatus(EquipStatus& OutStatus) const
{
    // FUN_0629ae80, in retail order.
    if (IsDestroyed())
        OutStatus.Flags |= EquipStatusFlagDestroyed;
    else
        OutStatus.Flags &= ~EquipStatusFlagDestroyed;

    if (IsActive())
        OutStatus.Flags |= EquipStatusFlagActive;
    else
        OutStatus.Flags &= ~EquipStatusFlagActive;

    if (IsDisabled())
        OutStatus.Flags |= EquipStatusFlagDisabled;
    else
        OutStatus.Flags &= ~EquipStatusFlagDisabled;
}

bool CEquip::GetConnectionPosition(Vector* OutPos, Matrix* OutMatrix) const
{
    // RVA 0xcc0c0
    (void)OutPos;
    (void)OutMatrix;
    return false;
}

void CEquip::NotifyArchGroupDestroyed(unsigned short SubObjId)
{
    // RVA 0x8d730 (an ICF-shared empty body).
    (void)SubObjId;
}

bool CEquip::GetEquipDesc(EquipDesc& Desc) const
{
    // RVA 0x3ae10, in retail call order; no Arch null test. Ghidra names the setters after
    // unrelated ICF-shared bodies (DamageList, CPlayerTradeOffer...); they are EquipDesc's own.
    if (IsDestroyed())
        return false;
    Desc.set_id(Id);
    Desc.set_status(GetRelativeHealth());
    Desc.set_arch_id(Arch->get_id());
    Desc.Count = 1;
    Desc.set_equipped(true);
    Desc.bMission = bMission;
    return true;
}

unsigned CEquip::GetType() const
{
    return Type;
}

unsigned short CEquip::GetID() const
{
    return Id;
}

Archetype::Equipment const* CEquip::EquipArch() const
{
    // RVA 0x3fb90
    return Arch;
}

CEqObj* CEquip::GetOwner() const
{
    // RVA 0x71ad0
    return Owner;
}

char const* CEquip::IdentifyEquipment() const
{
    // FUN_0629b040; no null guards. Deviation: retail's sprintf can overrun the 0x40-byte
    // buffer for a long nickname; the port truncates.
    GIdentifyEquipmentBuf[0] = '\0';
    _snprintf(GIdentifyEquipmentBuf, sizeof(GIdentifyEquipmentBuf), "ID(0x%X) EQ_ID(0x%X) EQ_ARCH(%s)",
              Owner->Id, (unsigned)(Id), Arch->Nickname.get_string());
    GIdentifyEquipmentBuf[sizeof(GIdentifyEquipmentBuf) - 1] = '\0';
    return GIdentifyEquipmentBuf;
}

CInternalEquip::CInternalEquip(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
                               Archetype::Equipment const* ArchObj, bool bMissionFlag)
    : CEquip(TypeBits, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
}

CInternalEquip::CInternalEquip(CInternalEquip const& Other)
    : CEquip(Other)
{
}

CInternalEquip::~CInternalEquip()
{
}

bool CInternalEquip::GetEquipDesc(EquipDesc& Desc) const
{
    // RVA 0x3f300
    bool Result = CEquip::GetEquipDesc(Desc);
    Desc.make_internal();
    return Result;
}

CInternalEquip* __cdecl CInternalEquip::cast(CEquip* Equip)
{
    // RVA 0x3f320
    if (Equip == nullptr || (Equip->GetType() & kInternalEquipMask) == 0)
        return nullptr;
    return (CInternalEquip*)(Equip);
}

CInternalEquip const* __cdecl CInternalEquip::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

CExternalEquip::CExternalEquip(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
                               Archetype::Equipment const* ArchObj, bool bMissionFlag)
    : CEquip(TypeBits, OwnerObj, EquipId, ArchObj, bMissionFlag)
{
    HardpointName.clear();
    ConnectionIndex = -1;
}

CExternalEquip::CExternalEquip(CExternalEquip const& Other)
    : CEquip(Other)
    , HardpointName(Other.HardpointName)
    , ConnectionIndex(Other.ConnectionIndex)
{
    // RVA 0x32800
}

CExternalEquip::~CExternalEquip()
{
}

bool CExternalEquip::GetEquipDesc(EquipDesc& Desc) const
{
    // RVA 0x3ed30
    bool Result = CEquip::GetEquipDesc(Desc);
    Desc.set_hardpoint(HardpointName);
    return Result;
}

bool CExternalEquip::IsDestroyed() const
{
    // RVA 0x3ed60: an unconnected equip counts as destroyed.
    if (!IsConnected())
        return true;
    return bDestroyed != 0;
}

bool CExternalEquip::IsConnected() const
{
    // RVA 0x3ed80
    return GetParentConnector(false) != kInvalidEngineInstance;
}

long CExternalEquip::GetParentConnector(bool bSearch) const
{
    // FUN_0629eed0: a search miss warns and returns the hit as FindHardpoint left it.
    if (!bSearch)
        return ConnectionIndex;
    long HitInst = -1;
    if (!FindHardpoint(Owner->EngineInstanceId, HardpointName.Value, HitInst))
    {
        FDUMP(kFdumpWarning,
              "%s(%d) : *** WARNING: CExternalEquip: Object (%s) missing hardpoint (%s) for item (%s)\n",
              kCExternalEquipSource, 0x8e, Owner->Archetype->Nickname.get_string(),
              HardpointName.Value, Arch->Nickname.get_string());
    }
    return HitInst;
}

bool CExternalEquip::GetHardPointInfo(HardpointInfo& Info) const
{
    // FUN_0629ef60: a flat lookup on the connector's archetype (no child recursion, unlike
    // FindHardpoint_OS). GetParentConnector is called twice; no null or handle tests.
    if (GetParentConnector(false) == kInvalidEngineInstance)
        return false;

    void* Handle = DALib::Engine->GetInstanceArchetype((void*)(GetParentConnector(false)));

    FHardpointKey Key;
    Key.Name = HardpointName.Value;
    Key.Crc = DACOM_CRC::GetCRC32(HardpointName.Value);
    unsigned long Hit = DALib::Hardpoint->GetHardpoint(Handle, &Key, &Info);

    DALib::Engine->ReleaseHandle(Handle);

    return Hit != 0;
}

bool CExternalEquip::GetConnectionPosition(Vector* OutPos, Matrix* OutMatrix) const
{
    // FUN_0629ee00 (reconstructed from the listing; Ghidra's output is corrupted). The
    // hardpoint through the connector's world transform; the matrix operand is the transform's
    // leading 3x3. The outputs are optional; no null tests on the engine, the transform or the
    // math engine (DAT_063fc23c).
    if (!IsConnected())
        return false;

    HardpointInfo Buf;
    if (!GetHardPointInfo(Buf))
        return false;

    Transform const* XformPtr = (Transform const*)(
        DALib::Engine->GetInstanceTransform((void*)(ConnectionIndex)));

    if (OutPos != nullptr)
    {
        FVector Pos;
        Acquire3DMathEngine()->TransformXform(Pos, *(FTransform const*)XformPtr,
                                              (FVector const&)(Buf.Position));
        *(FVector*)OutPos = Pos;
    }

    if (OutMatrix != nullptr)
    {
        FMatrix Rot;
        Acquire3DMathEngine()->MulMatrix(Rot, *(FMatrix const*)XformPtr,
                                         (FMatrix const&)(Buf.Orient));
        *(FMatrix*)OutMatrix = Rot;
    }

    return true;
}

bool CExternalEquip::GetVelocity(Vector& Out) const
{
    // FUN_0629efe0. Retail quirk: always returns false. No Owner null test.
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    if (IsConnected())
        Out = Owner->get_velocity();
    return false;
}

bool CExternalEquip::GetCenterOfMass(Vector& Out) const
{
    // FUN_0629f030: the connector's world position. Retail quirk: always returns false.
    Out.x = 0.0f;
    Out.y = 0.0f;
    Out.z = 0.0f;
    if (IsConnected())
    {
        long const ConnIdx = GetParentConnector(false);
        float const* Pos = DALib::Engine->GetInstancePosition((void*)(ConnIdx));
        Out.x = Pos[0];
        Out.y = Pos[1];
        Out.z = Pos[2];
    }
    return false;
}

bool CExternalEquip::Connect(char const* HpName)
{
    // RVA 0x3eda0
    HardpointName.Value = StringAlloc(HpName, false);
    ConnectionIndex = (int)(GetParentConnector(true));
    if (ConnectionIndex == kInvalidEngineInstance)
    {
        Disconnect();
        return false;
    }
    return true;
}

void CExternalEquip::Disconnect()
{
    ConnectionIndex = (int)(kInvalidEngineInstance);
}

void CExternalEquip::Destroy()
{
    CEquip::Destroy();
    Disconnect();
}

CacheString CExternalEquip::GetParentHPName() const
{
    // RVA 0x327f0
    return HardpointName;
}

bool CExternalEquip::Update(float Dt, unsigned Flags)
{
    // RVA 0x3ed20
    return CEquip::Update(Dt, Flags);
}

CExternalEquip* __cdecl CExternalEquip::cast(CEquip* Equip)
{
    if (Equip == nullptr || (Equip->GetType() & kExternalEquipMask) == 0)
        return nullptr;
    return (CExternalEquip*)(Equip);
}

CExternalEquip const* __cdecl CExternalEquip::cast(CEquip const* Equip)
{
    // Retail folds the const overload onto the same body as the non-const one.
    return cast(const_cast<CEquip*>(Equip));
}

// The three ctors (0629b970 / 0629b990 / 0629b9b0) write +0, byte +4 and +8 only. Retail
// leaves +0xc unwritten; the port zeroes Current (it is only read in states 1 and 3, after
// Traverse has set it).
CEquipTraverser::CEquipTraverser()
{
    TypeMask = 0xFFFFFFFFu;
    bIncludeDestroyed = 0;
    State = 0;
    Current = nullptr;
}

CEquipTraverser::CEquipTraverser(int TypeMaskValue)
{
    TypeMask = (unsigned)TypeMaskValue;
    bIncludeDestroyed = 0;
    State = 0;
    Current = nullptr;
}

CEquipTraverser::CEquipTraverser(int TypeMaskValue, bool bIncludeDestroyedValue)
{
    TypeMask = (unsigned)TypeMaskValue;
    bIncludeDestroyed = bIncludeDestroyedValue ? 1 : 0;
    State = 0;
    Current = nullptr;
}

void CEquipTraverser::Restart()
{
    State = 0;
}


CEquipManager::CEquipManager()
    : Owner(nullptr)
    , ExternalAllocator(0)
    , External(nullptr)
    , ExternalCount(0)
    , InternalAllocator(0)
    , Internal(nullptr)
    , InternalCount(0)
    , DecayingCargoCount(0)
{
    External = new std::list<CEquip*>();
    Internal = new std::list<CEquip*>();
}

CEquipManager::CEquipManager(CEquipManager const& Other)
    : Owner(Other.Owner)
    , ExternalAllocator(Other.ExternalAllocator)
    , External(new std::list<CEquip*>(*Other.External))
    , ExternalCount(Other.ExternalCount)
    , InternalAllocator(Other.InternalAllocator)
    , Internal(new std::list<CEquip*>(*Other.Internal))
    , InternalCount(Other.InternalCount)
    , DecayingCargoCount(Other.DecayingCargoCount)
{
}

CEquipManager& CEquipManager::operator=(CEquipManager const& Other)
{
    // RVA 0x270d0: VC6 list assignment (std::list::operator= here); the allocator bytes are not
    // copied, and a self-assignment is a no-op.
    if (this == &Other)
        return *this;

    Owner = Other.Owner;
    *External = *Other.External;
    ExternalCount = Other.ExternalCount;
    *Internal = *Other.Internal;
    InternalCount = Other.InternalCount;
    DecayingCargoCount = Other.DecayingCargoCount;
    return *this;
}

CEquipManager::~CEquipManager()
{
    Clear();
    delete External;
    External = nullptr;
    ExternalCount = 0;
    delete Internal;
    Internal = nullptr;
    InternalCount = 0;
}

bool CEquipManager::Init(CEqObj* OwnerObj)
{
    if (Owner == nullptr)
        Owner = OwnerObj;
    return Owner != nullptr;
}

bool CEquipManager::AddNewEquipment(CEquip* Equip)
{
    // FUN_0629b210: no Equip null test; a decaying cargo stack (NaN counts) bumps the decay
    // count. Always true.
    if ((Equip->GetType() & kInternalListMask) == 0)
    {
        External->push_back(Equip);
        ExternalCount += 1;
    }
    else
    {
        Internal->push_back(Equip);
        InternalCount += 1;
    }

    if (Equip->GetType() == EquipTypeCargo &&
        ((CECargo*)Equip)->GetDecayDamagePerSecond() != 0.0f)
        DecayingCargoCount += 1;
    return true;
}

void CEquipManager::Clear()
{
    // RVA 0x3b2b0: the static Clear(list&) inlined, internal list first.
    Clear(*Internal);
    InternalCount = 0;
    Clear(*External);
    ExternalCount = 0;
    DecayingCargoCount = 0;
}

void __cdecl CEquipManager::Clear(std::list<CEquip*>& List)
{
    // RVA 0x3b860: null each slot and delete its equipment, then unlink every node.
    std::list<CEquip*>::iterator It = List.begin();
    for (; It != List.end(); ++It)
    {
        CEquip* Equip = *It;
        *It = nullptr;
        delete Equip;
    }
    List.clear();
}

int CEquipManager::CleanUp()
{
    // RVA 0x3b380. In retail the counts are the lists' own size fields; the port keeps them
    // separate and adjusts them here.
    int InternalRemoved = CleanUp(*Internal);
    int ExternalRemoved = CleanUp(*External);
    InternalCount -= (unsigned)(InternalRemoved);
    ExternalCount -= (unsigned)(ExternalRemoved);
    return InternalRemoved + ExternalRemoved;
}

int CEquipManager::CleanUp(std::list<CEquip*>& List)
{
    // RVA 0x3b8c0: unlink, then delete, every deletable item.
    int Removed = 0;
    std::list<CEquip*>::iterator It = List.begin();
    while (It != List.end())
    {
        CEquip* Equip = *It;
        if (!Equip->CanDelete())
        {
            ++It;
            continue;
        }
        if (Equip->GetType() == EquipTypeCargo &&
            ((CECargo*)Equip)->GetDecayDamagePerSecond() != 0.0f)
            DecayingCargoCount -= 1;
        It = List.erase(It);
        delete Equip;
        ++Removed;
    }
    return Removed;
}

int CEquipManager::Size() const
{
    return (int)(InternalCount + ExternalCount);
}

bool CEquipManager::HasDecayingCargo() const
{
    // RVA 0x26fa0
    return DecayingCargoCount > 0;
}

CEquip* CEquipManager::FindFirst(unsigned TypeMask)
{
    // RVA 0x3b720 (both overloads).
    CEquipTraverser Walk((int)(TypeMask));
    return Traverse(Walk);
}

CEquip const* CEquipManager::Traverse(CEquipTraverser& Walk) const
{
    // RVA 0x3b430
    return const_cast<CEquipManager*>(this)->Traverse(Walk);
}

CEquip const* CEquipManager::ReverseTraverse(CEquipTraverser& Walk) const
{
    // RVA 0x3b530
    return const_cast<CEquipManager*>(this)->ReverseTraverse(Walk);
}

CEquip const* CEquipManager::FindFirst(unsigned TypeMask) const
{
    // Retail declares this const although Traverse is not.
    CEquipTraverser Walk((int)(TypeMask));
    return const_cast<CEquipManager*>(this)->Traverse(Walk);
}

CEquip* CEquipManager::Traverse(CEquipTraverser& Walk)
{
    if (ExternalCount == 0 && InternalCount == 0)
        return nullptr;

    for (;;)
    {
        switch (Walk.State)
        {
        case 0:
            if (ExternalCount == 0 || (Walk.TypeMask & kExternalListMask) == 0)
            {
                Walk.State = 2;
                Walk.Current = nullptr;
                continue;
            }
            Walk.State = 1;
            Walk.Current = nullptr;
            break;
        case 1:
            break;
        case 2:
            if (InternalCount == 0 || (Walk.TypeMask & kInternalListMask) == 0)
            {
                Walk.State = 0;
                Walk.Current = nullptr;
                return nullptr;
            }
            Walk.State = 3;
            Walk.Current = nullptr;
            break;
        case 3:
            break;
        default:
            // Retail returns without resetting the state.
            return nullptr;
        }

        std::list<CEquip*>* List = (Walk.State == 1) ? External : Internal;
        CEquip* Found = NextMatching(List, Walk.Current, Walk.TypeMask,
                                     Walk.bIncludeDestroyed != 0);
        if (Found != nullptr)
        {
            Walk.Current = Found;
            return Found;
        }

        if (Walk.State == 1)
        {
            Walk.State = 2;
            Walk.Current = nullptr;
            continue;
        }
        Walk.State = 0;
        Walk.Current = nullptr;
        return nullptr;
    }
}

CEquip* CEquipManager::ReverseTraverse(CEquipTraverser& Walk)
{
    // RVA 0x3b540. Retail quirk: the guard is OR, not Traverse's AND, so a manager with only
    // one list never reverse-traverses.
    if (ExternalCount == 0 || InternalCount == 0)
        return nullptr;

    for (;;)
    {
        switch (Walk.State)
        {
        case 0:
            if (InternalCount == 0 || (Walk.TypeMask & kInternalListMask) == 0)
            {
                Walk.State = 2;
                Walk.Current = nullptr;
                continue;
            }
            Walk.State = 3;
            Walk.Current = nullptr;
            break;
        case 3:
            break;
        case 2:
            if (ExternalCount == 0 || (Walk.TypeMask & kExternalListMask) == 0)
            {
                Walk.State = 0;
                Walk.Current = nullptr;
                return nullptr;
            }
            Walk.State = 1;
            Walk.Current = nullptr;
            break;
        case 1:
            break;
        default:
            // Retail returns without resetting the state.
            return nullptr;
        }

        std::list<CEquip*>* List = (Walk.State == 3) ? Internal : External;
        CEquip* Found = PrevMatching(List, Walk.Current, Walk.TypeMask,
                                     Walk.bIncludeDestroyed != 0);
        if (Found != nullptr)
        {
            Walk.Current = Found;
            return Found;
        }

        if (Walk.State == 3)
        {
            Walk.State = 2;
            Walk.Current = nullptr;
            continue;
        }
        Walk.State = 0;
        Walk.Current = nullptr;
        return nullptr;
    }
}

CExternalEquip* CEquipManager::FindByHardpoint(CacheString const& Hardpoint)
{
    // RVA 0x3b640. Retail neither casts nor null-checks the names.
    CEquipTraverser Walk((int)(kExternalEquipMask));
    for (;;)
    {
        CEquip* Equip = Traverse(Walk);
        if (Equip == nullptr)
            return nullptr;
        CExternalEquip* Ext = (CExternalEquip*)(Equip);
        if (CallDacomStricmp(Ext->GetParentHPName().Value, Hardpoint.Value) == 0)
            return Ext;
    }
}

CExternalEquip const* CEquipManager::FindByHardpoint(CacheString const& Hardpoint) const
{
    // RVA 0x3b640 (shared).
    return const_cast<CEquipManager*>(this)->FindByHardpoint(Hardpoint);
}

CEquip* CEquipManager::FindByID(unsigned short EquipId)
{
    // FUN_0629b6d0: external list first; no null test of the payloads.
    std::list<CEquip*>::iterator It = External->begin();
    for (; It != External->end(); ++It)
    {
        if ((*It)->Id == EquipId)
            return *It;
    }
    for (It = Internal->begin(); It != Internal->end(); ++It)
    {
        if ((*It)->Id == EquipId)
            return *It;
    }
    return nullptr;
}

CEquip const* CEquipManager::FindByID(unsigned short EquipId) const
{
    // RVA 0x3b6c0
    return const_cast<CEquipManager*>(this)->FindByID(EquipId);
}

unsigned short CEquipManager::InstToSubObjID(long Instance) const
{
    // FUN_0629b3a0: the first attached equip whose instance tree holds Instance.
    if (Instance == -1)
        return SUBOBJ_ID_NONE;

    CEquipTraverser Walk((int)(kAttachedEquipMask));
    CEquip* Equip = const_cast<CEquipManager*>(this)->Traverse(Walk);
    while (Equip != nullptr)
    {
        if (((CAttachedEquip*)Equip)->IsInstOnEquip(Instance))
            return Equip->Id;
        Equip = const_cast<CEquipManager*>(this)->Traverse(Walk);
    }
    return SUBOBJ_ID_NONE;
}

bool CEquipManager::VerifyListSync(EquipDescList const& Other) const
{
    // RVA 0x3b750, phase 1: every desc in Other must have a live matching equip.
    for (EquipDesc const& Desc : Other.Items)
    {
        CEquip const* Equip = FindByID(Desc.Id);
        if (Equip == nullptr)
            return false;
        // No Arch null test.
        if (Desc.ArchId != Equip->Arch->Nickname.Id)
            return false;
        // Unrounded against 0.001 (DAT_0639c9f0); NaN fails.
        double const HealthDiff =
            fabs((double)Desc.Health - Equip->GetRelativeHealth());
        if (!(HealthDiff < 0.001))
            return false;
        // Retail computes this stricmp but never checks its result -- reproduced verbatim.
        CExternalEquip const* Ext = CExternalEquip::cast(const_cast<CEquip*>(Equip));
        if (Ext != nullptr)
            CallDacomStricmp(Desc.Hardpoint, Ext->GetParentHPName().Value);
    }

    // Phase 2: every live equip that reports a GetEquipDesc() must appear somewhere in Other.
    CEquipTraverser Walk((int)(0xFFFFFFFFu));
    for (;;)
    {
        CEquip const* Equip = const_cast<CEquipManager*>(this)->Traverse(Walk);
        if (Equip == nullptr)
            return true;
        EquipDesc TempDesc;
        bool bHasDesc = Equip->GetEquipDesc(TempDesc);
        if (bHasDesc && Other.find_equipment_item(Equip->Id) == nullptr)
            return false;
    }
}
