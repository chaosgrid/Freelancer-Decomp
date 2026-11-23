// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include "common_exports.h"

// CEquip / CEquipManager — CEqObj::init / CShip::init slice.
// Layout: CEquip 0x20, CExternalEquip 0x28, CEquipManager 0x20 at CEqObj+0xe4,
// CECargo 0x2c, CEPower 0x20, CEEngine 0x58, CELauncher 0x1F4, CEGun 0x270,
// CEShieldGenerator 0x60, CEShield 0x88. Lists are heap std::list, not VC6 dummy nodes.
// See docs/re/common_physys.md.

#include "common_animdb.h"   // AnimDB::VChannel (CEGun::JointChannel's base)
#include "common_archetype.h"
#include "common_goods.h"
#include "common_misc.h"     // DamageEntry::SubObjFate (CAttachedEquip::SetFate)
#include "common_physys.h"
#include "common_pub.h"      // pub::AI::DirectiveCallback (LaunchAction base)

#include <cstddef>
#include <list>
#include <vector>

struct CEqObj;
class CEquip;
class CECargo;
class CECargoPod;
struct CObject;
struct CSimple;
struct CShip;
class CAttachedEquip;
struct IObjRW;
struct IObjInspect;
struct EquipStatus;  // common_iface.h (included there after common_cequip.h -- forward decl only)

// Retail nests this enum inside a global IObject, and the mangling says so:
// ?get_speed@IObjInspectImpl@@UBEMMW4ThrustEquipType@IObject@@@Z. Whether IObjInspect actually
// derives from IObject is not settled from the listing (Common.dll exports no IObject member at
// all), so the port declares only the scope the ABI needs.
struct IObject
{
    enum ThrustEquipType
    {
        ThrustEquipNormal = 0,
        ThrustEquipEngine = 1,
        ThrustEquipThruster = 2,
        ThrustEquipTradelane = 3,
        ThrustEquipCruise = 4,
        ThrustEquipCount
    };
};

// Spelled unqualified everywhere in the port; the alias keeps the mangling at IObject's scope,
// and the constants keep the enumerator names usable without the qualification.
typedef IObject::ThrustEquipType ThrustEquipType;
ThrustEquipType const ThrustEquipNormal = IObject::ThrustEquipNormal;
ThrustEquipType const ThrustEquipEngine = IObject::ThrustEquipEngine;
ThrustEquipType const ThrustEquipThruster = IObject::ThrustEquipThruster;
ThrustEquipType const ThrustEquipTradelane = IObject::ThrustEquipTradelane;
ThrustEquipType const ThrustEquipCruise = IObject::ThrustEquipCruise;
ThrustEquipType const ThrustEquipCount = IObject::ThrustEquipCount;

// FORMATION_RTYPE (global, returned by CShip::add_formation_follower) is declared
// once, in common_pub.h, which this header includes.

enum StrafeDir
{
    StrafeNone = 0,
    StrafeLeft = 1,
    StrafeRight = 2,
    StrafeUp = 3,
    StrafeDown = 4
};

// Retail's name for CShip+0x2ac (?get_bay_state@CShip@@QBE?AW4BayState@@XZ). The enumerator
// names are the port's, read off the transitions in CShip::update / open_bay / close_bay:
// open_bay moves 0/1 -> 3 (or straight to 2 without a bay animation), update moves 3 -> 2
// when the opening animation ends and 1 -> 0 when the closing one ends.
enum BayState
{
    BayClosed = 0,
    BayClosing = 1,
    BayOpen = 2,
    BayOpening = 3
};

enum EEquipType
{
    EquipTypeLight = 1,
    EquipTypeAttachedFX = 2,
    EquipTypeMineDropper = 0x20,
    EquipTypeCmDropper = 0x40,
    EquipTypeGun = 0x80,
    EquipTypeShield = 0x100,
    EquipTypeShieldGen = 0x200,
    EquipTypeThruster = 0x400,
    EquipTypeCargoPod = 0x800,
    EquipTypeCloak = 0x1000,
    EquipTypeCargo = 0x10000,
    EquipTypeEngine = 0x20000,
    EquipTypePower = 0x40000,
    EquipTypeScanner = 0x80000,
    EquipTypeTractor = 0x100000,
    EquipTypeRepairDroid = 0x200000,
    EquipTypeInternalFX = 0x400000,
    EquipTypeTradeLane = 0x800000,   // CETradeLaneEquip (ctor 06294ed7, cast 0629fca8)
    EquipTypeArmor = 0x1000000
};

// A DALib::Anim script handle (-1 = none) held as a member with a destructor:
// `if (Index != -1) { Anim slot 0x38 StopPlayback(Index); Anim slot 0x28 DestroyScript(Index);
// Index = -1; }` -- no AnimDB::Rem. Owners: CEInternalFX +0x20 (Unwind@06393be8 -> FUN_0629abd0;
// ~CEInternalFX / Destroy also run the same body inline before CEquip::Destroy), CShip +0x22c
// (Unwind@06394555 -> FUN_0629abd0; ~CShip inlines it at .text:062b0ada..062b0b0b), CSolar +0x1c0
// (Unwind@063947d8 -> FUN_0629abd0) and the values of CSolar's +0x1cc std::list (scalar
// deleting form FUN_062bb4d0, list erase FUN_062b94c0, list destructor FUN_062b8e30). Copies
// copy the dword (CShip .text:06287f72, CSolar .text:0629aae7). Release() is that body, which
// retail inlines wherever an owner releases the handle before its own destruction.
struct FAnimScriptRef
{
    int Index;                        // +0x00

    FAnimScriptRef()
        : Index(-1)
    {
    }
    FAnimScriptRef(FAnimScriptRef const& Other) = default;
    FAnimScriptRef& operator=(FAnimScriptRef const&) = delete;
    ~FAnimScriptRef();

    void Release();
};

static_assert(sizeof(FAnimScriptRef) == 0x04, "FAnimScriptRef sizeof 4");

// ============================================================================
// INotify — equipment/object notification listener interface
// ============================================================================
// Common.dll has no concrete implementation; it is an opaque listener type here
// (CEquip::ConnectServerEquip/ConnectClientEquip/NotifyDisconnecting/Notify). CEqObj has its
// own INotify* slot at +0x168 with its own connect/disconnect/notify.
//
// Slot 0 is notify(Event, void*): CEqObj::notify (`?notify@CEqObj@@UAEXW4Event@INotify@@PAX@Z`)
// overrides it, and every CEquip::Notify call dispatches through slot 0. No virtual
// destructor precedes it (retail never deletes through an INotify*).
struct INotify
{
    // Event codes seen at retail CEquip::Notify call sites. Codes named Event_0xNN have no
    // confirmed meaning; the numbering is not contiguous or CEquip-specific.
    enum Event
    {
        DockAnimEnded             = 0x0,  // CEqObj::update_docking_animations (062aabac / 062aac5f): a docking animation reached an end; Param = unsigned char* (1 = back at the start, 0 = at the end)
        BayStateChanged           = 0x2,  // CShip::open_bay (062b2b5b) / close_bay (062b2ca7): Param = unsigned char* (1 = opening)
        Event_0x5                 = 0x5,  // CAttachedEquip::Disconnect, FATE 2 (debris) with a debris object: Param = the CAttachedEquip*
        Event_0x6                 = 0x6,  // CAttachedEquip::Disconnect, FATE other than 1/2: Param = the CAttachedEquip*
        Event_0x7                 = 0x7,  // CArchGroup::Destroy, FATE 2 with a debris object: Param = the CArchGroup*
        Event_0x8                 = 0x8,  // CArchGroup::Destroy, FATE 3/4/5: Param = the CArchGroup*
        Event_0x9                 = 0x9,  // CArchGroup::SetHitPoints, a positive decrease: Param = the CArchGroup*
        EquipDestroying           = 0xa,  // CEquip::Destroy: Param = equipment Id (ushort, zero-extended)
        EquipActivating           = 0xb,  // CEquip::Activate(true) on a 0->1 edge: Param = nullptr
        EquipDeactivating         = 0xc,  // CEquip::Activate(false) on a 1->0 edge: Param = nullptr
        ArchGroupDestroyed        = 0xd,  // CEEngine::NotifyArchGroupDestroyed (06296120): nozzle arch group destroyed; Param = SubObjId
        EngineCruiseStateChanged  = 0xe,  // CEEngine::Update charge-threshold edge (Param =
                                           // &int, 2 while spooling up / 3 once fully charged)
                                           // and CEEngine::EngageCruise engage/disengage edges
                                           // (Param = &int, 0 normal disengage / 1 instant
                                           // disengage / 2 charge-already-elapsed instant-engage
                                           // / 3 zero-charge-time instant-engage)
        NotEnoughPower            = 0xf,  // CEThruster::Update (0629d384): power pool cannot cover thrust draw; Param = nullptr
        ShieldGeneratorSetChanged = 0x10, // CEShield::RemGen: dominant generator archetype changed; Param = nullptr
        ShieldOfflineStateChanged = 0x11, // CEShield Activate/Update/SetHitPoints/RemGen: Param = &(offline flag)
        ShieldWentOffline         = 0x12, // CEShield::SetHitPoints: relative health dropped to/below
                                           // OfflineThreshold while functioning; Param = nullptr
        ShieldCanReactivate       = 0x13, // CEShield::Update/SetHitPoints: an inactive-but-active
                                           // shield's relative health recovered above OfflineThreshold;
                                           // Param = nullptr
        Event_0x14                = 0x14, // CETractor::AddTarget verify failure: Param = &TractorFailureCode
        Event_0x15                = 0x15, // TractorArm::Update lost/invalid target: Param = the TractorArm*
        Event_0x17                = 0x17, // TractorArm::Update target reached the beam tip: Param = the TractorArm*
        Event_0x18                = 0x18, // TractorArm::Update consume timer elapsed: Param = the TractorArm*
        Event_0x19                = 0x19,
    };

    virtual void notify(Event Code, void* Param) = 0;
};

class COMMON_API CEquip
{
public:
    CEquip(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
           Archetype::Equipment const* Arch, bool bMissionFlag);
    virtual ~CEquip();

    // ??2/??3CEquip: class-scope allocation, a bare jump to msvcrt malloc/free
    // (listing 0x9d560 / 0x9d570). Every CE* object is created and freed through these.
    // noexcept: malloc returns null on failure, and a VC6 new-expression tests the result for
    // null before running the constructor (e.g. CSolar::alloc_equip 062b7f41..062b7f47).
    static void* operator new(size_t Size) noexcept;
    static void operator delete(void* Block);

    // Retail exports no copy assignment for CEquip or any class derived from it (the
    // copy constructors are exported). Deleting it keeps the compiler from generating
    // and exporting one, and from declaring move members.
    CEquip& operator=(CEquip const&) = delete;

    // Declared in retail's ??_7CEquip@@6B@ slot order (listing 0x0639bdcc..0x0639be20).
    virtual bool IsActive() const;
    virtual bool IsDestroyed() const;
    virtual bool IsFunctioning() const;
    virtual bool IsDisabled() const;
    virtual bool IsTemporary() const;
    virtual bool CanDelete() const;
    // Slot 7 (+0x1c): retail's base body is the shared ICF fold 0x062ed730, a bare `RET 4`.
    // CEEngine, CELightEquip and CEAttachedFX override it; CEqObj::destroy_equipment's
    // arch-group traversal calls it.
    virtual void NotifyArchGroupDestroyed(unsigned short SubObjId);
    virtual bool IsLootable() const;
    virtual bool Update(float Dt, unsigned Flags);
    virtual bool GetEquipDesc(EquipDesc& Desc) const;
    virtual void GetStatus(EquipStatus& OutStatus) const;
    virtual bool Activate(bool bOn);
    virtual void Destroy();
    virtual float GetMaxHitPoints() const;
    virtual float GetHitPoints() const;
    virtual void SetHitPoints(float Hit);
    // .text:0629af90: the quotient is left unrounded in ST0 (FDIV, RET) and every retail caller
    // consumes it there, so it is double here (export ?GetRelativeHealth@CEquip, same
    // class/method/arity).
    virtual double GetRelativeHealth() const;
    // 0x0632c0c0: base default always returns false (params unused). Overridden by
    // CExternalEquip::GetConnectionPosition (0x0629ee00).
    virtual bool GetConnectionPosition(Vector* OutPos, Matrix* OutMatrix) const;
    virtual bool EnableController();
    virtual bool DisableController();
    virtual bool IsControllerEnabled() const;

    unsigned GetType() const;
    unsigned short GetID() const;
    // Listing 0x3fb90 (ICF-shared by every leaf *Arch accessor): return [this+0xc].
    Archetype::Equipment const* EquipArch() const;
    // Listing 0x71ad0 (ICF-shared): return [this+0x4].
    CEqObj* GetOwner() const;
    char const* IdentifyEquipment() const;
    void ConnectServerEquip(INotify* NotifyParam);  // 0x3afd0
    void ConnectClientEquip(INotify* NotifyParam);  // 0xbd950
    void NotifyDisconnecting(INotify* NotifyParam); // 0x3afe0
    void Notify(INotify::Event Code, void* Param);  // 0x3b010

    CEqObj* Owner;                    // +0x04
    unsigned short Id;                // +0x08 (+0x0a: alignment, the ctor 0629ad70 leaves it)
    Archetype::Equipment* Arch;       // +0x0c
    unsigned char bActive;            // +0x10
    unsigned char bDestroyed;         // +0x11
    unsigned char bMission;           // +0x12 (+0x13: alignment, the ctor leaves it)
    unsigned Type;                    // +0x14
    INotify* NotifyA;                 // +0x18
    INotify* NotifyB;                 // +0x1c

private:
    // ??0CEquip@@AAE@XZ (listing 0x26d60): private default constructor.
    CEquip();
};

static_assert(sizeof(CEquip) == 0x20, "CEquip sizeof 0x20");
static_assert(offsetof(CEquip, Owner) == 0x04, "CEquip+0x04 owner");
static_assert(offsetof(CEquip, Arch) == 0x0c, "CEquip+0x0c arch");
static_assert(offsetof(CEquip, Type) == 0x14, "CEquip+0x14 type");

class COMMON_API CInternalEquip : public CEquip
{
public:
    CInternalEquip(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
                   Archetype::Equipment const* Arch, bool bMissionFlag);
    // 0x349e0. Adds no fields: CEquip(Other) plus this class's vtable.
    CInternalEquip(CInternalEquip const& Other);
    virtual ~CInternalEquip();

    virtual bool GetEquipDesc(EquipDesc& Desc) const override;

    // 0x3f320: type & 0x1ff0000.
    static CInternalEquip* __cdecl cast(CEquip* Equip);
    static CInternalEquip const* __cdecl cast(CEquip const* Equip);
};

// Listing CExternalEquip 0x28. Hardpoint CacheString +0x20, connection index +0x24 = -1.
// Connect: StringAlloc + GetParentConnector(true) = FindHardpoint(owner instance, hp).
// Cast: type & 0x1fe3.
class COMMON_API CExternalEquip : public CEquip
{
public:
    CExternalEquip(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
                   Archetype::Equipment const* Arch, bool bMissionFlag);
    // 0x32800. Memberwise copy, base level first (same shape as CAttachedEquip's).
    CExternalEquip(CExternalEquip const& Other);
    virtual ~CExternalEquip();

    virtual bool GetEquipDesc(EquipDesc& Desc) const override;
    // 0x3ed60. bDestroyed when IsConnected() (vtable+0x58), else true (an unconnected equip
    // counts as destroyed).
    virtual bool IsDestroyed() const override;
    virtual void Destroy() override;
    virtual bool Update(float Dt, unsigned Flags) override;
    // 0x0629ee00. Ghidra's decompile is corrupt here; ported from the listing.
    virtual bool GetConnectionPosition(Vector* OutPos, Matrix* OutMatrix) const override;
    // Virtuals introduced here, declared in retail's ??_7CExternalEquip@@6B@ slot order
    // (listing 0x0639c4f4..0x0639c510): IsConnected, Connect, GetParentHPName,
    // GetParentConnector, GetHardPointInfo, GetVelocity, GetCenterOfMass, Disconnect.
    virtual bool IsConnected() const;
    virtual bool Connect(char const* HpName);
    // 0x327f0 -- returns HardpointName by value. No override in retail.
    virtual CacheString GetParentHPName() const;
    // 0x3eed0 (?GetParentConnector@CExternalEquip@@UBEJ_N@Z, public virtual).
    virtual long GetParentConnector(bool bSearch) const;
    // 0x0629ef60, vtable+0x68. Queries the engine's hardpoint store for ConnectionIndex +
    // HardpointName on every call (no cache), so a moving parent is reflected.
    virtual bool GetHardPointInfo(HardpointInfo& Info) const;
    // 0x0629efe0, vtable+0x6c. Zeroes Out; when connected copies Owner->get_velocity()
    // (no null check on Owner). Retail quirk kept: always returns false.
    virtual bool GetVelocity(Vector& Out) const;
    // 0x0629f030, vtable+0x70. Zeroes Out; when connected, the connector instance's world
    // position via DALib::Engine vtable+0x90 on GetParentConnector(false). Retail quirk kept:
    // always returns false.
    virtual bool GetCenterOfMass(Vector& Out) const;
    static CExternalEquip* __cdecl cast(CEquip* Equip);
    static CExternalEquip const* __cdecl cast(CEquip const* Equip);

protected:
    // ?Disconnect@CExternalEquip@@MAEXXZ: protected virtual, last slot this class adds.
    virtual void Disconnect();

public:
    CacheString HardpointName;        // +0x20
    int ConnectionIndex;              // +0x24  -1
};

static_assert(sizeof(CExternalEquip) == 0x28, "CExternalEquip sizeof 0x28");
static_assert(offsetof(CExternalEquip, HardpointName) == 0x20, "CExternalEquip+0x20 hp");
static_assert(offsetof(CExternalEquip, ConnectionIndex) == 0x24, "CExternalEquip+0x24 index");

// Listing CAttachmentObj / CNonPhys 0x10 / CPhys 8. vtable+4 GetRootIndex, +0x10 Connect.
// Retail's own vftable ??_7CAttachmentObj@@6B@ (.rdata:0639c6b0) is the deleting dtor
// (LAB_06292b90) followed by five _purecall slots (LAB_06391fb0): the class is abstract and
// exports none of these five, only its special members and vftable.
class COMMON_API CAttachmentObj
{
public:
    virtual ~CAttachmentObj();
    virtual long GetRootIndex() const = 0;
    virtual CObject* GetPhysicsOwner() const = 0;
    virtual void EnableCollisions(bool bEnable) = 0;
    virtual bool Connect(CAttachedEquip* Equip) = 0;
    virtual CObject* Disconnect(CAttachedEquip* Equip, bool bMakeDebris) = 0;
};

class COMMON_API CNonPhysAttachment : public CAttachmentObj
{
public:
    CNonPhysAttachment();
    CNonPhysAttachment(const CNonPhysAttachment& Other);
    CNonPhysAttachment& operator=(const CNonPhysAttachment& Other);
    virtual ~CNonPhysAttachment();

    virtual long GetRootIndex() const;
    virtual CObject* GetPhysicsOwner() const;
    virtual void EnableCollisions(bool bEnable);
    virtual bool Connect(CAttachedEquip* Equip);
    virtual CObject* Disconnect(CAttachedEquip* Equip, bool bMakeDebris);

    long EngineInstanceId;            // +0x04  -1
    unsigned HpCrc;                   // +0x08
    unsigned char bCollisions;        // +0x0c
    // +0x0d: caches the is_shield_part-style byte (CObject PartMap word[2]) of this
    // attachment's PartMap entry across EnableCollisions(false)/(true) (.text:06293815 /
    // 0629386c). The copy ctor/operator= copy this byte but not +0x0e/+0x0f.
    unsigned char CachedShieldFlag;   // +0x0d
    // +0x0e..+0x0f: alignment padding; retail never writes or copies it.
};

static_assert(sizeof(CNonPhysAttachment) == 0x10, "CNonPhysAttachment sizeof 0x10");
static_assert(offsetof(CNonPhysAttachment, EngineInstanceId) == 0x04, "CNonPhys+0x04 instance");

class COMMON_API CPhysAttachment : public CAttachmentObj
{
public:
    CPhysAttachment();
    CPhysAttachment(const CPhysAttachment& Other);
    virtual ~CPhysAttachment();

    virtual long GetRootIndex() const;
    virtual CObject* GetPhysicsOwner() const;
    virtual void EnableCollisions(bool bEnable);
    virtual bool Connect(CAttachedEquip* Equip);
    virtual CObject* Disconnect(CAttachedEquip* Equip, bool bMakeDebris);

    CObject* EquipObject;             // +0x04
};

static_assert(sizeof(CPhysAttachment) == 8, "CPhysAttachment sizeof 8");
static_assert(offsetof(CPhysAttachment, EquipObject) == 0x04, "CPhys+0x04 CObject");

// Listing CAttachedEquip 0x4c. Ctor Arch->load (slot 3). Connect: CNonPhys if parent
// UserData+0x58 CRC list hits hp, else CPhys. GetRootIndex = attachment instance.
class COMMON_API CAttachedEquip : public CExternalEquip
{
public:
    CAttachedEquip(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
                   Archetype::Equipment const* Arch, bool bMissionFlag);
    // 0x32a70. Memberwise copy, base level first. No caller found in the retail listing.
    CAttachedEquip(CAttachedEquip const& Other);
    virtual ~CAttachedEquip();

    virtual bool Update(float Dt, unsigned Flags) override;
    virtual float GetHitPoints() const override;
    virtual void SetHitPoints(float Hit) override;
    virtual bool IsConnected() const override;
    virtual bool Connect(char const* HpName) override;
    // 0x334e0. Connected guard, then Attachment->GetPhysicsOwner()->
    // get_velocity(). Zeroes Out and returns false when not connected or physics-less.
    virtual bool GetVelocity(Vector& Out) const override;
    // Listing 0x33570. Zeroes Out; when connected fills the bound cache once (vtable+0x8c) and
    // returns BoundCenter transformed by the root instance's world transform
    // (IEngine::GetInstanceTransform + I3DMathEngine::TransformXform). Returns true when connected.
    virtual bool GetCenterOfMass(Vector& Out) const override;
    // Virtuals introduced here, declared in retail's ??_7CAttachedEquip@@6B@ slot order
    // (listing 0x0639c694..0x0639c6a8): GetToughness, GetRadius, IsInstOnEquip,
    // GetRootIndex, SetFate, ComputeBoundingSphere.
    // Listing 0x33200 (?GetToughness@CAttachedEquip@@UBEHXZ): return Arch->Toughness.
    virtual int GetToughness() const;
    // 0x06293650, vtable+0x7c. Same IsConnected guard and lazy bBoundsValid cache as
    // GetCenterOfMass; returns the cached BoundRadius (local space, not re-transformed).
    virtual bool GetRadius(float& OutRadius) const;
    // 0x062936a0, vtable+0x80. True when InstIndex is GetRootIndex() (vtable+0x84) or a
    // descendant of it.
    virtual bool IsInstOnEquip(long InstIndex) const;
    virtual long GetRootIndex() const;
    // 0x063275d0 (ICF fold `MOV [ECX+0x34],EAX; RET 4`) -- sets Fate.
    virtual void SetFate(DamageEntry::SubObjFate FateValue);
    CObject* GetPhysicsOwner() const;
    CObject* RetrieveDebrisObject();
    // Listing 0x3fb90 (ICF-shared): return [this+0xc].
    Archetype::AttachedEquipment const* AttachedEquipArch() const;
    static CAttachedEquip* __cdecl cast(CEquip* Equip);
    static CAttachedEquip const* __cdecl cast(CEquip const* Equip);

protected:
    // ?Disconnect@CAttachedEquip@@MAEXXZ (protected; overrides CExternalEquip's slot).
    virtual void Disconnect() override;
    // Listing 0x33740. Vtable+0x8c. IEngine::GetInstanceExtent(GetRootIndex(), 0, &OutRadius,
    // &OutCenter) (+0x88); the extent arguments are pushed before the 0-arg GetRootIndex vcall.
    virtual void ComputeBoundingSphere(float& OutRadius, Vector& OutCenter) const;

public:
    CAttachmentObj* Attachment;       // +0x28
    CObject* Debris;                  // +0x2c
    float HitPts;                     // +0x30
    int Fate;                         // +0x34  ctor 1
    // mutable: GetCenterOfMass/GetRadius (both const per retail's vtable signature) lazily
    // populate this cache on first read, matching retail's const-method-mutates-cache pattern.
    mutable unsigned char bBoundsValid; // +0x38 (+0x39..+0x3b: alignment)
    mutable float BoundRadius;          // +0x3c
    mutable Vector BoundCenter;         // +0x40
};

static_assert(sizeof(CAttachedEquip) == 0x4c, "CAttachedEquip sizeof 0x4c");
static_assert(offsetof(CAttachedEquip, Attachment) == 0x28, "CAttachedEquip+0x28 attachment");
static_assert(offsetof(CAttachedEquip, HitPts) == 0x30, "CAttachedEquip+0x30 hit_pts");
static_assert(offsetof(CAttachedEquip, BoundCenter) == 0x40, "CAttachedEquip+0x40 bound center");

class COMMON_API CECargo : public CInternalEquip
{
public:
    CECargo(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Equipment const* Arch,
            bool bMissionFlag);
    // 0x35380: memberwise copy through CInternalEquip(Other) plus CargoPod/HitPts/Count.
    CECargo(CECargo const& Other);
    virtual ~CECargo();

    virtual bool IsDestroyed() const override;
    virtual void Destroy() override;
    virtual float GetHitPoints() const override;
    virtual void SetHitPoints(float Hit) override;  // 0x35820
    virtual bool GetEquipDesc(EquipDesc& Desc) const override;
    // ??_7CECargo@@6B@ adds two slots after CEquip's: GetType (listing 0x35740, returns the
    // constant 0x10000 -- a virtual that hides the non-virtual CEquip::GetType) and Init.
    virtual unsigned GetType() const;
    virtual void Init(float HealthFrac, unsigned StackCount, CacheString const& Hardpoint);

    // Listing 0x3d880 (ICF-shared): return [this+0x20], the pod holding this stack.
    CECargoPod const* GetContainer() const;

    void AddToStack(unsigned Amount, float HealthFrac);
    void RemoveFromStack(unsigned Amount);
    float GetDecayDamagePerSecond() const;
    unsigned GetCount() const;
    void SetCount(unsigned Count);  // 0xdbae0
    static CECargo* __cdecl cast(CEquip* Equip);
    static CECargo const* __cdecl cast(CEquip const* Equip);

    CECargoPod* CargoPod;             // +0x20  linked pod (FillPod / EmptyPod)
    float HitPts;                     // +0x24
    unsigned Count;                   // +0x28
};

static_assert(sizeof(CECargo) == 0x2c, "CECargo sizeof 0x2c");
static_assert(offsetof(CECargo, Count) == 0x28, "CECargo+0x28 count");

class COMMON_API CEquipTraverser
{
public:
    // User-declared so that no implicit move members are generated and exported:
    // VC6 had none, and retail exports exactly the copy assignment.
    CEquipTraverser& operator=(CEquipTraverser const&) = default;

    CEquipTraverser();
    explicit CEquipTraverser(int TypeMask);
    CEquipTraverser(int TypeMask, bool bIncludeDestroyed);
    void Restart();

    unsigned TypeMask;                // +0x00
    unsigned char bIncludeDestroyed;  // +0x04 (+0x05..+0x07: alignment padding)
    int State;                       // +0x08  0 ext-start, 1 ext, 2 int-start, 3 int
    CEquip* Current;                  // +0x0c  last returned (port: not VC6 node*)
};

static_assert(sizeof(CEquipTraverser) == 0x10, "CEquipTraverser sizeof 0x10");

class COMMON_API CEquipManager
{
public:
    CEquipManager();
    ~CEquipManager();

    // 0x26fb0/0x270d0: retail's compiler-generated copy ctor/operator= copy the two lists
    // (element pointers by value, not the CEquip objects). No CALL site in the listing.
    CEquipManager(CEquipManager const& Other);
    CEquipManager& operator=(CEquipManager const& Other);

    bool Init(CEqObj* OwnerObj);
    bool AddNewEquipment(CEquip* Equip);
    void Clear();
    // 0x3b380/0x3b8c0: removes every CanDelete() equipment item from both lists
    // (decrementing DecayingCargoCount for decaying cargo before deleting it), Internal first
    // then External. Returns the total number of items removed.
    int CleanUp();
    int Size() const;
    bool HasDecayingCargo() const;
    // ?FindFirst@CEquipManager@@QBEPBVCEquip@@I@Z -- const, and it hands back a const CEquip.
    // Retail's own callers (get_scanlist at 062c03e0, for one) immediately treat the result as
    // mutable, so the constness buys nothing; it is reproduced because the ABI says so.
    CEquip const* FindFirst(unsigned TypeMask) const;
    // Listing 0x3b720, the same body as the const overload.
    CEquip* FindFirst(unsigned TypeMask);
    CEquip* Traverse(CEquipTraverser& Walk);
    // Listing 0x3b430: a bare JMP to the non-const Traverse.
    CEquip const* Traverse(CEquipTraverser& Walk) const;
    // 0x3b530/0x3b540: mirror of Traverse that walks both lists back-to-front and
    // visits Internal before External (i.e. the reverse of Traverse's own External-then-Internal
    // order). Retail's initial guard is ExternalCount==0 || InternalCount==0 (OR, not AND like
    // Traverse's own guard) -- reproduced verbatim even though it looks asymmetric.
    CEquip* ReverseTraverse(CEquipTraverser& Walk);
    // Listing 0x3b530: a bare JMP to the non-const ReverseTraverse.
    CEquip const* ReverseTraverse(CEquipTraverser& Walk) const;
    CEquip* FindByID(unsigned short EquipId);
    CEquip const* FindByID(unsigned short EquipId) const;
    // Listing 0x3b640 (both overloads share the body): walks external equipment
    // (mask 0x1fe3) and returns the first whose GetParentHPName() matches Hardpoint.
    CExternalEquip* FindByHardpoint(CacheString const& Hardpoint);
    CExternalEquip const* FindByHardpoint(CacheString const& Hardpoint) const;
    // 0x3b3a0: maps an engine instance id to the Id of the CAttachedEquip it belongs to
    // (TypeMask 0x1fe0 == kAttachedEquipMask, CAttachedEquip::IsInstOnEquip at vtable+0x80);
    // SUBOBJ_ID_NONE if Instance == -1 or nothing matches.
    unsigned short InstToSubObjID(long Instance) const;
    // 0x3b750: cross-checks a serialized EquipDescList against this manager's live
    // equipment (Id/ArchId/relative-health match, plus a discarded-result hardpoint stricmp for
    // external equips -- retail computes it but never checks it, reproduced verbatim), then walks
    // every live equip that reports a GetEquipDesc() and requires each to be present in the list.
    bool VerifyListSync(EquipDescList const& Other) const;

private:
    // Listing 0x3b8c0: removes every CanDelete() item from one list (decrementing
    // DecayingCargoCount for decaying cargo first) and returns how many were removed.
    int CleanUp(std::list<CEquip*>& List);
    // Listing 0x3b860: deletes every item of one list and empties it.
    static void __cdecl Clear(std::list<CEquip*>& List);

public:
    CEqObj* Owner;                    // +0x00
    unsigned ExternalAllocator;       // +0x04  retail VC6 list allocator byte
    std::list<CEquip*>* External;     // +0x08  retail dummy node*
    unsigned ExternalCount;           // +0x0c
    unsigned InternalAllocator;       // +0x10  retail VC6 list allocator byte
    std::list<CEquip*>* Internal;     // +0x14  retail dummy node*
    unsigned InternalCount;           // +0x18
    int DecayingCargoCount;           // +0x1c
};

static_assert(sizeof(CEquipManager) == 0x20, "CEquipManager sizeof 0x20");
static_assert(offsetof(CEquipManager, ExternalCount) == 0x0c, "CEquipManager+0x0c");
static_assert(offsetof(CEquipManager, InternalCount) == 0x18, "CEquipManager+0x18");

// Listing CEPower operator_new(0x20). Type 0x40000. Capacity from Power+0x70.
// Base is CInternalEquip: ??_7CEPower@@6B@ (.rdata:0639ca64) slot 10 is
// CInternalEquip::GetEquipDesc, and ~CEPower (.text:0629f3ea) restamps the CInternalEquip table.
class COMMON_API CEPower : public CInternalEquip
{
public:
    CEPower(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Power const* Arch,
            bool bMissionFlag);
    // 0x34a90. Adds no fields: CEquip(Other) plus this class's vtable.
    CEPower(CEPower const& Other);
    virtual ~CEPower();

    virtual bool Update(float Dt, unsigned Flags) override;
    float GetChargeRate() const;
    float GetCapacity() const;
    float GetThrustCapacity() const;
    float GetThrustChargeRate() const;
    static CEPower* __cdecl cast(CEquip* Equip);
    static CEPower const* __cdecl cast(CEquip const* Equip);

protected:
    // ?PowerArch@CEPower@@IBEPBUPower@Archetype@@XZ (listing 0x3fb90, ICF-shared).
    Archetype::Power const* PowerArch() const;
};

static_assert(sizeof(CEPower) == 0x20, "CEPower sizeof 0x20");

// Defined ahead of CEEngine and CEThruster, which derive from it (CEEngine at +0x20,
// CEThruster at +0x4c); CSteering/CNudgeEngine/CStrafeEngine further down derive from it too.
// CPhysControllerEquip — base equipment physics controller (0x3b090)
class COMMON_API CPhysControllerEquip : public PhySys::Controller
{
public:
    CPhysControllerEquip();
    CPhysControllerEquip(CPhysControllerEquip const& Other);
    virtual ~CPhysControllerEquip();
    CPhysControllerEquip& operator=(CPhysControllerEquip const& Other);

    // ??_7CPhysControllerEquip@@6B@ (listing 0x0639be44..0x0639be50): slot 7 EnableController
    // is _purecall (no ?EnableController@CPhysControllerEquip export), then DisableController,
    // IsControllerEnabled (listing 0x26e60: return [this+0x10]) and the protected ControlObject.
    virtual bool EnableController() = 0;
    virtual bool DisableController();
    virtual bool IsControllerEnabled() const;

protected:
    // ?ControlObject@CPhysControllerEquip@@MAE_NPAUCObject@@@Z (listing 0x3b090).
    virtual bool ControlObject(CObject* Object);

public:
    bool bControllerEnabled; // +0x10
};

// 0x14, not 0x18: the base ends at the bControllerEnabled byte. The ctor (.text:06286e40) writes
// only +0x10, the copy ctor (.text:06286e70) and operator= (.text:06280ad0) stop at +0x10, and
// CEEngine keeps its own byte at +0x34 == this base's +0x14 (.text:06295cb7, copy .text:06295ad8).
// The OwnerShip dword at +0x14 belongs to CSteering/CNudgeEngine/CStrafeEngine, whose ctors
// (.text:062bb5ed, 062ad22d, 062bb92d) zero it themselves.
static_assert(sizeof(CPhysControllerEquip) == 0x14, "CPhysControllerEquip sizeof 0x14");
static_assert(offsetof(CPhysControllerEquip, bControllerEnabled) == 0x10, "CPhysControllerEquip+0x10");

// Listing CEEngine operator_new(0x58). Type 0x20000.
// Two bases, as retail's two exported tables say: ??_7CEEngine@@6BCInternalEquip@@@ (.rdata
// 0639ce6c) and ??_7CEEngine@@6BCPhysControllerEquip@@@ (.rdata 0639ce3c). The ctor
// (.text:06295c8c-06295caa) constructs CInternalEquip at +0 and CPhysControllerEquip at +0x20
// (Controller() then [+0x30] = 0), and stamps both tables at .text:06295cdc/06295ce3.
// EnableController/DisableController/IsControllerEnabled override the same-named virtuals of
// both bases; slots 7..9 of the second table are the compiler's this-0x20 adjustor thunks
// (.text:06296960.. SUB ECX,0x20; JMP), which is exactly what an override of both produces.
// Slot 4 of the second table (.rdata:0639ce4c) is do_simulation_controller, the protected
// override declared below.
class COMMON_API CEEngine : public CInternalEquip, public CPhysControllerEquip
{
public:
    CEEngine(CShip* OwnerShip, unsigned short EquipId, Archetype::Engine const* Arch,
             bool bMissionFlag);
    // 0x35a40. Deep-copies the nozzle array (exact-fit allocation) and the embedded
    // Controller-region bytes; see the .cpp.
    CEEngine(CEEngine const& Other);
    virtual ~CEEngine();

    static CEEngine* __cdecl cast(CEquip* Equip);
    static CEEngine const* __cdecl cast(CEquip const* Equip);
    // Listing 0x3fb90 (ICF-shared): return [this+0xc].
    Archetype::Engine const* EngineArch() const;

    // 0x36600: formats "HpEngine0<Index>" via vsprintf+StringAlloc.
    static void __cdecl BuildNozzleHPName(int Index, CacheString& Out);

    // 0x35ee0: IsActive() && !IsDisabled() (+0x10) && !IsDestroyed() &&
    // IsControllerEnabled() (+0x54) && (has a nozzle OR Arch->bIndestructible).
    virtual bool IsFunctioning() const override;
    // 0x35f60. On deactivate, retail first calls EngageCruise(false, false); then
    // CEquip::Activate, then EnableController()/DisableController() (vtbl+0x4c/+0x50) on
    // success/failure respectively.
    virtual bool Activate(bool bOn) override;
    // .text:06295fa0..06296113: when functioning, the cruise charge timer and its
    // EngineCruiseStateChanged notifications, then (Flags bit 0 clear) PhySys::Controller::wakeup
    // when the ship throttle changed to a non-zero value; always CEquip::Update.
    virtual bool Update(float Dt, unsigned Flags) override;
    // .text:06296250..062963db. Non-virtual. Notify codes are ints 3/2/1/0 (Ghidra shows their
    // bit patterns as tiny floats); every state change ends in PhySys::Controller::wakeup.
    bool EngageCruise(bool Engage, bool Instant);
    // .text:062961a0: if IsActive() (vtbl+4), the virtual ControlObject(Owner) of the
    // CPhysControllerEquip base (vtbl+0x28 of the table at +0x20); else false.
    virtual bool EnableController() override;
    // .text:062961c0: ADD ECX,0x20; JMP CPhysControllerEquip::DisableController.
    virtual bool DisableController() override;
    // 0x36120, slot 7 override. Removes every nozzle record whose leading uint16 equals
    // SubObjId, calling Notify(ArchGroupDestroyed, SubObjId) per removal.
    virtual void NotifyArchGroupDestroyed(unsigned short SubObjId) override;

    float GetLinearDrag() const;
    // .text:062963e0: FLD Arch+0x74; FADD CRUISE_DRAG -- the sum is returned unrounded in ST0
    // (callers FADD onto it before one FSTP), hence double.
    double GetCruiseDrag() const;
    float GetThrust(float Throttle) const;
    float GetPowerDraw(float Throttle) const;
    float GetCruiseThrust(float LinearDrag) const;
    float GetCruisePowerDraw() const;
    bool IsCruiseEngaged() const;
    // .text:062961d0: MOV AL,[this+0x30] -- the CPhysControllerEquip base's bControllerEnabled.
    // Slot 21 of the CInternalEquip table, so CEquip*/CInternalEquip* callers (IsFunctioning's
    // own vtbl[0x54] call) reach it.
    virtual bool IsControllerEnabled() const override;
    float GetCruiseChargeTimeElapsed() const;
    float GetCruiseChargeTime() const;

protected:
    // .text:06296430 (ordinal 2442, protected virtual; `this` is the CPhysControllerEquip
    // subobject at +0x20). Per PSI: when functioning, picks the thrust force and power rate by
    // cruise state (normal throttle thrust while cruise is still charging; the ramped
    // cruise force once charged; plain throttle thrust otherwise), drains Power by
    // rate * dt (no push if that would go negative), and pushes along -Z of the ship.
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim,
                                          IVP_U_Vector<IVP_Core>* CoreList) override;

public:
    // .text:06295a30 (ordinal 1867, ICF-folded with accessory::get_materials):
    // `LEA EAX,[this+0x34]; RET` -- the exhaust nozzle vector.
    std::vector<ExhaustNozzleInfo> const& NozzleInfos() const { return Nozzles; }

    // +0x20 .. +0x33: the CPhysControllerEquip base (bControllerEnabled at +0x30).
    // +0x34: retail's VC6 std::vector<ExhaustNozzleInfo> (allocator byte +0x34, then
    // begin/end/capacity at +0x38/+0x3c/+0x40; element stride 0x3c == sizeof(ExhaustNozzleInfo)).
    // The ctor appends through FUN_06296640 (.text:06295d67), the dtor frees +0x38
    // (.text:06295e7d). Deviation: a modern std::vector, so the fields below sit at their
    // retail offsets only where the vector is 0x10 bytes; they are reached by name.
    std::vector<ExhaustNozzleInfo> Nozzles; // +0x34
    // +0x44: the ship throttle (CShip+0x29c) the last time Update woke the controller
    // (.text:062960d6..06296101: a changed, non-zero throttle calls wakeup() and is stored here).
    float LastWakeThrottle;
    unsigned char Opaque48[0x4];      // +0x48  never written by the ctor (.text:06295c40)
    float CruiseChargeRate;           // +0x4c  set by EngageCruise on charge-begin to
                                       // CRUISE_ACCEL_TIME * ThrustMultiplier(Arch+0x70) /
                                       // (ShipLinearDrag * CRUISING_SPEED), zeroed on disengage;
                                       // no reader found
    float CruiseChargeTimeElapsed;    // +0x50
    unsigned char bCruiseActive;      // +0x54  CShip::is_cruise_active (+0x55..+0x57 padding)
};

static_assert(offsetof(CEEngine, Nozzles) == 0x34, "CEEngine+0x34 nozzle vector");

// Listing CanFire returns. Fire itself is CanFire + zero +0x50 unless result==2.
enum FireResult
{
    FireDestroyed = 0,
    FireDisabled = 1,
    FireRefireDelay = 2,
    FireNoAmmo = 3,
    FireNoPower = 4,
    FireMuzzleCone = 5,
    FireCruise = 6,
    FireTradelane = 7,
    FireCloakActive = 8,
    FireOk = 9
};

// Listing ProjLaunchInfo 0x44: pos, 3x3, velocity, Seeker==2 lock. Spawn hook.
struct ProjLaunchInfo
{
    Vector Pos;                       // +0x00
    Matrix Orient;                    // +0x0c
    Vector Velocity;                  // +0x30
    CSimple* LockTarget;              // +0x3c  Watchable* - 0x9c
    unsigned short LockSubId;         // +0x40  CEGun+0x258 (+0x42: alignment)
};

static_assert(sizeof(ProjLaunchInfo) == 0x44, "ProjLaunchInfo sizeof 0x44");
static_assert(offsetof(ProjLaunchInfo, Velocity) == 0x30, "ProjLaunchInfo+0x30 vel");
static_assert(offsetof(ProjLaunchInfo, LockTarget) == 0x3c, "ProjLaunchInfo+0x3c lock");
static_assert(offsetof(ProjLaunchInfo, LockSubId) == 0x40, "ProjLaunchInfo+0x40 sub");

// Listing CELauncher 0x1F4. CAttachedEquip 0x4c + projectile +0x4c / refire +0x50 / barrels +0x54.
class COMMON_API CELauncher : public CAttachedEquip
{
public:
    // 0x369d0: memberwise copy through CAttachedEquip(Other) plus
    // Projectile/RefireElapsed/Barrels[8].
    CELauncher(CELauncher const& Other);
    virtual ~CELauncher();

    virtual bool Update(float Dt, unsigned Flags) override;
    virtual bool IsDisabled() const override;
    virtual bool Connect(char const* HpName) override;
    virtual void GetStatus(EquipStatus& OutStatus) const override;
    // Virtuals introduced here, in retail's ??_7CELauncher@@6B@ slot order (listing
    // 0x0639cf74..0x0639cfa0). The last six are protected in retail (MAE/MBE).
    virtual FireResult Fire(Vector const& Target);
    virtual void ConsumeFireResources();
    virtual void ComputeLaunchInfo(std::vector<ProjLaunchInfo>& Out, Vector const& Target) const;
    // Retail returns the product unrounded in ST0 (PowerNeedsMet subtracts it on the FPU).
    virtual double GetPowerDrawPerFire() const;
    virtual int GetAmmoCount() const;
    virtual bool AmmoNeedsMet() const;

    void ComputeProjectilePosAndVelocity_OneBarrel(Vector& Pos, Vector& Vel, int Barrel,
                                                   Vector const& Target) const;
    unsigned GetProjectilesPerFire() const;
    // ?GetBarrelInfo@CELauncher@@QBEPBUBarrel@1@I@Z -- Barrel is nested in CELauncher.
    // Listing barrel 0x34: hp instance, HardpointInfo 3x3 @ +0x04, pos @ +0x28.
    struct Barrel
    {
        int EngineIndex;              // +0x00  -1 unused
        Matrix Orient;                // +0x04
        Vector Position;              // +0x28
    };

    Barrel const* GetBarrelInfo(unsigned Index) const;
    Vector GetBarrelPosWS(unsigned Barrel) const;
    Vector GetBarrelDirWS(unsigned Barrel) const;
    Vector GetAvgBarrelPosWS() const;
    Vector GetAvgBarrelDirWS() const;
    static CELauncher* __cdecl cast(CEquip* Equip);
    static CELauncher const* __cdecl cast(CEquip const* Equip);
    // Listing 0x3fb90 (ICF-shared): return [this+0xc].
    Archetype::Launcher const* LauncherArch() const;
    // Listing 0x369c0 (ICF-shared): return [this+0x4c], the loaded projectile archetype.
    Archetype::Projectile const* ProjectileArch() const;

protected:
    // ??0CELauncher@@IAE@...: protected; only the concrete launchers construct one.
    CELauncher(unsigned TypeBits, CEqObj* OwnerObj, unsigned short EquipId,
               Archetype::Launcher const* Arch, bool bMissionFlag);

    virtual void Disconnect() override;
    virtual void ComputeLaunchInfo_OneBarrel(ProjLaunchInfo& Out, int Barrel,
                                             Vector const& Target) const;
    virtual FireResult CanFire(Vector const& Target) const;
    virtual bool PowerNeedsMet() const;
    virtual void DrawPower(int BarrelCount);
    virtual void DrawAmmoFromCargo(int BarrelCount);
    virtual bool RefireDelayElapsed() const;

public:
    Archetype::Equipment* Projectile; // +0x4c
    float RefireElapsed;                  // +0x50
    Barrel Barrels[8];                    // +0x54  8 * 0x34
};

static_assert(sizeof(CELauncher::Barrel) == 0x34, "CELauncher::Barrel sizeof 0x34");
static_assert(offsetof(CELauncher::Barrel, Position) == 0x28, "CELauncher::Barrel+0x28");
static_assert(sizeof(CELauncher) == 0x1F4, "CELauncher sizeof 0x1F4");
static_assert(offsetof(CELauncher, Projectile) == 0x4c, "CELauncher+0x4c projectile");
static_assert(offsetof(CELauncher, RefireElapsed) == 0x50, "CELauncher+0x50 refire");
static_assert(offsetof(CELauncher, Barrels) == 0x54, "CELauncher+0x54 barrels");

// Listing CEGun operator_new(0x270). Type 0x80. Fire JMP CELauncher::Fire; CanFire adds cone.
// Seeker==2 lock: BaseWatcher at +0x250 (Watchable* = CSimple+0x9c), subid +0x258,
// lock elapsed +0x26c vs Munition::TimeToLock. ComputeLaunchInfo_OneBarrel 0x37870.
// Update 0x37120: turret aim + AnimDB::Add + lock timer on +0x26c.
class COMMON_API CEGun : public CELauncher
{
public:
    // Retail RTTI ".?AUJointChannel@CEGun@@" (COL .rdata:063a7c10, base list JointChannel,
    // AnimDB::VChannel), vftable .rdata:0639d200 = { Tick 06299130, GetId 062a66a0 }. One per
    // turret joint at CEGun+0x1f4 (yaw) / +0x210 (pitch). The default ctor is inlined into
    // CEGun's ctors (.text:06296f78..06296fd7) and LoseJoints (.text:06298192..062981d0).
    struct JointChannel : public AnimDB::VChannel
    {
        JointChannel();

        // .text:06299130: slew Current toward Desired at Gun::TurnRate, push the angle into the
        // engine joint and read it back.
        virtual void Tick(float Dt) override;
        // .text:062a66a0 (ICF-folded `MOV EAX,[this+8]; RET`): the joint's engine instance.
        virtual int GetId() const override;

        // FUN_06299080 (__thiscall, RET 8): bind to an engine joint instance.
        void Bind(CEGun* NewOwner, long Instance);

        CEGun* Owner;                     // +0x04
        int EngineIndex;                  // +0x08  -1 none
        // +0x0c: 1 = the joint has a limited range, so the angle delta is used as-is; cleared by
        // Bind when |min-max| >= 2pi (a full-rotation joint), which makes Tick wrap the delta
        // into (-pi, pi].
        unsigned char bLimited;
        float Desired;                    // +0x10  aim angle written by Update
        float Current;                    // +0x14  engine joint angle (IEngine+0xcc read-back)
        // +0x18: Tick sets it when the read-back moved less than 1e-4 from the old Current
        // (.text:0629923f..06299261: |Current - ReadBack| FCOMP 1e-4, TEST AH,0x5 / JP taken to
        // the zero store when greater, equal or unordered), i.e. the joint has come to rest.
        unsigned char bSettled;
    };

    CEGun(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Gun const* Arch,
          bool bMissionFlag);
    // 0x36d00.
    CEGun(CEGun const& Other);
    virtual ~CEGun();

    virtual bool Update(float Dt, unsigned Flags) override;
    virtual bool Connect(char const* HpName) override;
    virtual FireResult Fire(Vector const& Target) override;
    virtual void ConsumeFireResources() override;
    // 0x370e0: if IsActive() changes and Owner is a CShip, mark
    // CShip::GunStatsDirty (Owner+0x2c4) so get_gun_stats recomputes.
    virtual bool Activate(bool bOn) override;
    // 0x36ce0: Projectile->Seeker != 0 (Archetype::Munition+0x90).
    bool is_guided() const;
    bool SetTarget(CSimple* Target, unsigned short SubId);
    void GetTarget(CSimple*& OutTarget, unsigned short& OutSubId) const;
    void SetTargetPoint(Vector const& Point);
    bool ComputeTgtLeadPosition(Vector& Out) const;
    bool CanSeeTargetPoint(Vector const& Point, float Angle) const;
    bool CanSeeTargetObject(float Angle) const;
    bool CanPointAt(Vector const& Point, float Angle) const;
    Vector GetTurretOrigin() const;
    bool IsJointInMotion() const;
    float GetMunitionRange() const;
    float GetSecondsTillLock() const;
    static bool __cdecl ComputeTimeToTgt(Vector const& RelPos, Vector const& RelVel, float Speed,
                                         float& OutTime);
    static CEGun* __cdecl cast(CEquip* Equip);
    static CEGun const* __cdecl cast(CEquip const* Equip);
    // Listing 0x3fb90 (ICF-shared): return [this+0xc].
    Archetype::Gun const* GunArch() const;
    // Listing 0x369c0 (ICF-shared): return [this+0x4c].
    Archetype::Munition const* MunitionArch() const;

protected:
    virtual void Disconnect() override;
    virtual void ComputeLaunchInfo_OneBarrel(ProjLaunchInfo& Out, int Barrel,
                                             Vector const& Target) const override;
    virtual FireResult CanFire(Vector const& Target) const override;

    // ?GetTargetObjectPos@CEGun@@IBE_NAAVVector@@@Z (protected).
    bool GetTargetObjectPos(Vector& Out) const;
    // Listing 0x38170 (?LoseJoints@CEGun@@IAEXXZ): drops both turret joints -- AnimDB::Rem
    // for a bound joint, then resets owner/instance/wrap/desired/current/in-motion.
    void LoseJoints();
    bool IsMovable() const;
    // 0x062980d0. Rediscovers Yaw/Pitch from GetRootIndex() (Connect()'s joint walk) and
    // always ends by recomputing TurretFrame. Its only reference in the listing is a data
    // table entry at 0x063b67ec, no CALL site.
    void LocateJoints();
    // 0x06297c70 (Ghidra's decompile is corrupt; see the .cpp).
    void ComputeTurretFrame();
    // .text:06297fc0 (?DecomposeJointMotion@CEGun@@IBEXABVVector@@AAM1@Z, ordinal 1349,
    // protected const). Expresses the world direction Dir in the frame
    // (connector orientation * TurretFrame) and returns its yaw atan2(x, -z) and pitch
    // atan2(y / hypot(x, z), 1).
    void DecomposeJointMotion(Vector const& Dir, float& OutYaw, float& OutPitch) const;

public:

    JointChannel Yaw;                     // +0x1f4
    JointChannel Pitch;                   // +0x210
    Matrix TurretFrame;                   // +0x22c  world turret basis in connector-local space;
                                           // ctor identity, filled by ComputeTurretFrame()
    // +0x250: a BaseWatcher on the target CSimple's Watchable (CSimple+0x9c). SetTarget
    // (.text:06298243), the copy ctor (06296e74) and the dtor (062970b7) all go through
    // BaseWatcher::set_pointer, so ~Watchable clears it when the target dies.
    BaseWatcher LockWatcher;              // +0x250  watched = target CSimple + 0x9c
    unsigned short LockSubId;             // +0x258 (+0x25a: alignment padding, never written)
    Vector TargetPoint;                  // +0x25c  SetTargetPoint: world - origin
    float TargetPointAge;                 // +0x268  ctor 0x7f7fffff; SetTargetPoint zeros
    float LockElapsed;                    // +0x26c  ConsumeFireResources zeros

protected:
    // Retail `protected: static float const CEGun::TARGET_POINT_VALID_TIME`, exported as
    // ordinal 2046 from .rdata 0x0639cedc = 2.0f. TargetPointAge (+0x268) accumulates dt and
    // is FCOMP'd against this at 06297174, 06297439, 06298285 and 062985d3 -- i.e. a stored
    // target point goes stale after two seconds. The port already used the bare literal at
    // those sites; naming it here also restores the exported symbol other modules link to.
    static float const TARGET_POINT_VALID_TIME;
};

static_assert(sizeof(CEGun::JointChannel) == 0x1C, "CEGun::JointChannel sizeof 0x1C");
static_assert(offsetof(CEGun::JointChannel, Owner) == 0x04, "JointChannel+4 owner");
static_assert(offsetof(CEGun::JointChannel, EngineIndex) == 0x08, "JointChannel+8 instance");
static_assert(offsetof(CEGun::JointChannel, bLimited) == 0x0c, "JointChannel+0xc limited");
static_assert(offsetof(CEGun::JointChannel, Desired) == 0x10, "JointChannel+0x10 desired");
static_assert(offsetof(CEGun::JointChannel, Current) == 0x14, "JointChannel+0x14 current");
static_assert(offsetof(CEGun::JointChannel, bSettled) == 0x18, "JointChannel+0x18 settled");
static_assert(sizeof(CEGun) == 0x270, "CEGun sizeof 0x270");
static_assert(offsetof(CEGun, Yaw) == 0x1f4, "CEGun+0x1f4 yaw joint");
static_assert(offsetof(CEGun, Pitch) == 0x210, "CEGun+0x210 pitch joint");
static_assert(offsetof(CEGun, TurretFrame) == 0x22c, "CEGun+0x22c turret frame");
static_assert(offsetof(CEGun, LockWatcher) == 0x250, "CEGun+0x250 lock watcher");
static_assert(offsetof(CEGun, LockSubId) == 0x258, "CEGun+0x258 lock subid");
static_assert(offsetof(CEGun, TargetPoint) == 0x25c, "CEGun+0x25c target point");
static_assert(offsetof(CEGun, TargetPointAge) == 0x268, "CEGun+0x268 target age");
static_assert(offsetof(CEGun, LockElapsed) == 0x26c, "CEGun+0x26c lock elapsed");

class CEShield;

// Listing CEShieldGenerator operator_new(0x60). Type 0x200. CAttachedEquip + LinkedShield +0x4c.
class COMMON_API CEShieldGenerator : public CAttachedEquip
{
public:
    CEShieldGenerator(CEqObj* OwnerObj, unsigned short EquipId,
                      Archetype::ShieldGenerator const* Arch, bool bMissionFlag);
    // 0x3bc50: memberwise copy, then LinkedShield as a raw pointer (not re-linked: the
    // shield's generator list still points at the original, as in CEShield's copy ctor)
    // plus Opaque50Value/LastUpdateTime; the +0x54 gap is not copied.
    CEShieldGenerator(CEShieldGenerator const& Other);
    virtual ~CEShieldGenerator();

    virtual bool IsFunctioning() const override;
    virtual bool Update(float Dt, unsigned Flags) override;
    virtual void Destroy() override;

    void LinkShield(CEShield* Shield);
    bool IsLinked() const;
    // 0x3cf70. Inverse of LinkShield: if still linked, removes this
    // generator from the CEShield's generator list, then clears LinkedShield.
    void UnLinkShield();
    static CEShieldGenerator* __cdecl cast(CEquip* Equip);
    static CEShieldGenerator const* __cdecl cast(CEquip const* Equip);
    // Listing 0x3fb90 (ICF-shared): return [this+0xc].
    Archetype::ShieldGenerator const* ShieldGenArch() const;

    CEShield* LinkedShield;            // +0x4c
    float Opaque50Value;               // +0x50: zeroed at construction, never read in retail
    unsigned char Pad54[4];            // +0x54 .. +0x57: gap; the copy ctor skips it too
    double LastUpdateTime;             // +0x58: Timing::GetGlobalTime() snapshot; Update()
                                        // computes its OWN Dt from (Now - LastUpdateTime)
                                        // instead of trusting the Dt parameter it's passed.
};

static_assert(sizeof(CEShieldGenerator) == 0x60, "CEShieldGenerator sizeof 0x60");
static_assert(offsetof(CEShieldGenerator, LinkedShield) == 0x4c, "CEShieldGenerator+0x4c");
static_assert(offsetof(CEShieldGenerator, Opaque50Value) == 0x50, "CEShieldGenerator+0x50");
static_assert(offsetof(CEShieldGenerator, LastUpdateTime) == 0x58, "CEShieldGenerator+0x58");

// Listing CEShield operator_new(0x88). Type 0x100. Built in link_shields, not alloc_equip.
// Base is CAttachedEquip: the ctor (0x3bd70) calls CAttachedEquip::CAttachedEquip, the copy
// ctor (0x3ba70) stamps the CEquip/CExternalEquip/CAttachedEquip tables in turn, and the
// dtor and Update/Connect/Disconnect call the CAttachedEquip versions. Vtable .rdata
// 0x0639d50c; its +0x44 slot is CEquip::GetRelativeHealth.
//
// CEShieldIntruderChecker: RTTI .?AVCEShieldIntruderChecker@@ (COL .rdata:063a7dc0, bases
// itself + PhySys::CObjectCB), vftable .rdata:0639d6a8 with two slots. Not exported. Retail
// code only ever destroys one (the vptr store at 0629bf7e/0629c160 is the inlined destructor
// in ~CEShield/CEShield::Destroy); no constructor or other writer of CEShield+0x80 exists in
// Common.dll (the ctor zeroes it, the copy ctor copies it), so the checker is never created
// in retail and every path below is dead there. Ported 1:1 anyway.
class CEShieldIntruderChecker : public PhySys::CObjectCB
{
public:
    // .text:0629cd10: INC [this+0xc] (an intruder entered the phantom).
    virtual void added(CObject* Obj) override;
    // .text:0629cd20: DEC [this+0xc] (an intruder left it).
    virtual void deleted(CObject* Obj) override;
    // FUN_0629cb70 (thiscall, RET 4): (re)build the phantom on the shield's geometry.
    void Attach(CEShield* Shield);

    unsigned Field04;   // +0x04  TODO: no reader or writer found in Common.dll
    void* Phantom;      // +0x08  PhySys phantom handle
    int IntruderCount;  // +0x0c  -1 while torn down, 0 after Attach, +1/-1 per intruder
};

static_assert(sizeof(CEShieldIntruderChecker) == 0x10, "CEShieldIntruderChecker 0x10");

class COMMON_API CEShield : public CAttachedEquip
{
public:
    CEShield(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Shield const* Arch,
             bool bMissionFlag);
    // 0x3ba70. Memberwise copy, base level first.
    CEShield(CEShield const& Other);
    virtual ~CEShield();

    virtual bool GetEquipDesc(EquipDesc& Desc) const;
    // 0x3bfe0: IsActive() && !IsDisabled() && !IsDestroyed() && bOnline.
    virtual bool IsFunctioning() const override;
    // 0x3c020. If not currently functioning but active and CanActivate() holds,
    // fires INotify::ShieldCanReactivate and brings the shield back online via
    // Activate_Internal(true), then forwards to CAttachedEquip::Update.
    virtual bool Update(float Dt, unsigned Flags) override;
    virtual void Destroy() override;
    // 0x3c190. CEquip::Activate(bOn) first; if now active, tries
    // CanActivate()/Activate_Internal(true); if now inactive and still online, deactivates via
    // Activate_Internal(false).
    virtual bool Activate(bool bOn) override;
    // 0x3c2d0. Clamps to MaxHitPoints, sets CurrentHitPoints, then either fires
    // ShieldWentOffline + Activate_Internal(false) (a functioning shield's relative health
    // dropped to/below OfflineThreshold; also arms the OfflineEndTime cooldown) or
    // ShieldCanReactivate + Activate_Internal(true) (an inactive-but-active shield recovered
    // above OfflineThreshold) -- the same online/offline pattern as Update/Activate.
    virtual void SetHitPoints(float Hit) override;
    // Returns CurrentHitPoints (COMDAT-folded with CEEngine::GetCruiseChargeTimeElapsed).
    virtual float GetHitPoints() const override;
    // 0x3c470. CAttachedEquip::Connect(), then flag_part_as_shield on the owner's part for
    // the root instance, sync the Attachment's collisions to bOnline, and publish the
    // shield's physical sphere as the root instance's engine extent
    // (IEngine::SetInstanceExtent; Ghidra's unaff_EBX there is the Radius local).
    virtual bool Connect(char const* HpName) override;
    // Listing 0x3ba00 (ICF-shared "xor al,al; ret"): a shield is never lootable.
    virtual bool IsLootable() const override;
    // Listing 0x8d730 (ICF-shared bare "ret 4"): a shield ignores sub-object fate.
    virtual void SetFate(DamageEntry::SubObjFate FateValue) override;
    static CEShield* __cdecl cast(CEquip* Equip);
    static CEShield const* __cdecl cast(CEquip const* Equip);
    virtual float GetMaxHitPoints() const override;
    float GetOfflineThreshold() const;
    float GetOfflineRebuildTime() const;  // 0x3ba50
    Archetype::ShieldGenerator const* ShieldGenArch() const;  // 0x3ba60
    // Listing 0x3fb90 (ICF-shared): return [this+0xc].
    Archetype::Shield const* ShieldArch() const;
    // 0x3c9b0: average, over Gens, of WeaponModDB_GetShieldMod(WeaponType.Id,
    // Gen->Arch->ShieldType.Id) -- see common_fuses.h. Defaults to 1.0 with no generators.
    // Returns double: retail leaves the average unrounded in ST0 (.text:0629ca29 FDIVR; RET).
    double GetWeaponModifier(ID_String const& WeaponType) const;

protected:
    // CEShieldGenerator::LinkShield/UnLinkShield/Destroy maintain the generator list through
    // the protected AddGen/RemGen below.
    friend class CEShieldGenerator;

    virtual void Disconnect() override;
    // ?AddGen@CEShield@@IAEXPAVCEShieldGenerator@@@Z / ?RemGen@... (protected).
    void AddGen(CEShieldGenerator* Gen);
    void RemGen(CEShieldGenerator* Gen);
    // 0x3ca50 (?CanActivate@CEShield@@IBE_NXZ). Out-of-line twin of the identical
    // condition inlined into the ctor/Update/Activate/SetHitPoints: not in the post-damage
    // cooldown window, relative health above OfflineThreshold, and (if ever allocated) the
    // intruder-checker phantom idle.
    bool CanActivate() const;
    // 0x3cad0 (?Activate_Internal@CEShield@@IAE_N_N@Z). Flips bOnline via
    // Attachment->EnableCollisions(bOn) + Notify(ShieldOfflineStateChanged, &bOnline); a no-op
    // if bOnline already equals bOn. Warns (does not crash) when called with no Attachment.
    bool Activate_Internal(bool bOn);

public:
    // Listing 0x3c950: if the checker exists, Attach(this) and set its phantom's collision
    // state to !bOnline.
    void AttachIntruderChecker();
    // Listing 0x3c980: if the checker exists, count = -1, destroy its phantom, phantom = 0.
    void DestroyIntruderChecker();
    // 0x3caa0 (?ShieldBatteryBoost@CEShield@@QAEXM@Z). If MaxHitPoints > 0, clears
    // the post-damage cooldown window (OfflineEndTime = 0) and adds Amount to CurrentHitPoints
    // via SetHitPoints (vtable+0x40), re-running the online/offline check.
    void ShieldBatteryBoost(float Amount);

    unsigned char bOnline;          // +0x4c (+0x4d..+0x4f: alignment)
    float CurrentHitPoints;         // +0x50 (CEShield::GetHitPoints() override)
    unsigned char Opaque54[4];      // +0x54 .. +0x57: gap, never touched
    double OfflineEndTime;          // +0x58, compared against Timing::GetGlobalTime()
    unsigned char MissionFlagCopy;  // +0x60: ctor's bMissionFlag argument, never read
    unsigned char Opaque61[3];
    std::vector<CEShieldGenerator*>* Gens; // +0x64 heap; retail VC6 vector at +0x64
    unsigned char Opaque68[8];      // +0x68 .. +0x6f: rest of retail's inline vector block
    // AddGen (0x3c5e0): the linked generator with the highest
    // Arch->Toughness (Archetype::AttachedEquipment+0x84). Retail stores the WINNING
    // GENERATOR'S ARCHETYPE pointer here, not the CEShieldGenerator* itself.
    Archetype::ShieldGenerator* DominantGenArch; // +0x70
    float OfflineThreshold;       // +0x74
    float OfflineRebuildTime;     // +0x78
    float MaxHitPoints;           // +0x7c
    // See CEShieldIntruderChecker above (never created by retail code).
    CEShieldIntruderChecker* IntruderChecker; // +0x80
};

static_assert(sizeof(CEShield) == 0x88, "CEShield sizeof 0x88");
static_assert(offsetof(CEShield, bOnline) == 0x4c, "CEShield+0x4c online flag");
static_assert(offsetof(CEShield, CurrentHitPoints) == 0x50, "CEShield+0x50 hit points");
static_assert(offsetof(CEShield, OfflineEndTime) == 0x58, "CEShield+0x58 offline end time");
static_assert(offsetof(CEShield, MissionFlagCopy) == 0x60, "CEShield+0x60 mission flag copy");
static_assert(offsetof(CEShield, Gens) == 0x64, "CEShield+0x64 gens vector");
static_assert(offsetof(CEShield, DominantGenArch) == 0x70, "CEShield+0x70 dominant gen arch");
static_assert(offsetof(CEShield, OfflineThreshold) == 0x74, "CEShield+0x74 offline threshold");
static_assert(offsetof(CEShield, OfflineRebuildTime) == 0x78, "CEShield+0x78 offline rebuild");
static_assert(offsetof(CEShield, MaxHitPoints) == 0x7c, "CEShield+0x7c max hit points");
static_assert(offsetof(CEShield, IntruderChecker) == 0x80, "CEShield+0x80 intruder checker");

// Two bases, per retail's ??_7CEThruster@@6BCAttachedEquip@@@ (.rdata 0639d744) and
// ??_7CEThruster@@6BCPhysControllerEquip@@@ (.rdata 0639d710): the ctor (.text:0629d159-
// 0629d174) builds CAttachedEquip at +0 and CPhysControllerEquip at +0x4c (Controller(), then
// [+0x5c] = 0), and stamps both tables at .text:0629d189/0629d18f.
class COMMON_API CEThruster : public CAttachedEquip, public CPhysControllerEquip
{
public:
    CEThruster(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Thruster const* Arch,
               bool bMissionFlag);
    virtual ~CEThruster();

    float GetThrust() const;
    float GetPowerDraw() const;
    bool CouldThrust() const;

    virtual bool Activate(bool bOn) override;

    // Override the same-named virtuals of both bases (slots 19..21 of the CAttachedEquip table,
    // slots 7..9 of the CPhysControllerEquip one as this-0x4c thunks, .text:0629d410..).
    virtual bool EnableController() override;
    virtual bool DisableController() override;

    virtual bool IsControllerEnabled() const override;

protected:
    // .text:0629d2d0 (ordinal 2443, protected virtual; slot 4 of the CPhysControllerEquip
    // table at +0x4c). Clears bCouldThrust; when functioning, drains Arch->PowerUsage * dt from
    // the ship's CShip::ThrustPower pool and pushes MaxThrust * dt along the owner's -Z, then sets
    // bCouldThrust -- or, when the pool cannot cover it, fires Notify(Event_0xF, 0).
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim,
                                          IVP_U_Vector<IVP_Core>* CoreList) override;

public:

    static CEThruster* __cdecl cast(CEquip* Equip);
    static CEThruster const* __cdecl cast(CEquip const* Equip);
    // Listing 0x3fb90 (ICF-shared): return [this+0xc].
    Archetype::Thruster const* ThrusterArch() const;

    // +0x4c .. +0x5f: the CPhysControllerEquip base (bControllerEnabled at +0x5c).
    bool bCouldThrust;                // +0x60  zeroed by the ctor (.text:0629d185) and by a
                                       // failed Activate (.text:0629d263); read by CouldThrust
                                       // (.text:0629d2c0); +0x61..+0x63: alignment
};

static_assert(sizeof(CEThruster) == 0x64, "CEThruster sizeof 0x64");

// CETractor::VerifyTarget return codes. Free enum (mangled W4TractorFailureCode@@); names are
// the port's, values from VerifyTarget's `return N` sites. AddTarget forwards the result and
// TractorArm::Update treats only 0 as a valid target.
enum TractorFailureCode
{
    TractorOk                = 0,
    TractorNoContents        = 1,  // target CLoot* null or its contents unit count (+0xf0) is 0
    TractorOutOfRange        = 2,  // beyond CETractor arch's range (+0xc -> Archetype::Tractor+0x70)
    TractorCargoFull         = 3,  // CShip::get_cargo_hold_remaining() < contents archetype Volume
    TractorShieldBattFull    = 4,  // ammo type 0x1b: CShip::get_shield_battery_capacity_remaining() == 0
    TractorRepairKitFull     = 5,  // ammo type 0x1a: CShip::get_repair_kit_capacity_remaining() == 0
    TractorAmmoFull          = 6,  // ammo type 0x1f/0x20/0x21: CShip::get_ammo_capacity_remaining() == 0
};

struct CLoot;
class TractorArm;

// CETractor layout from its ctor (0x3e030) and copy ctor (0x3d9a0): +0x20..+0x4F is a
// Transform (identity diagonal at +0x20/+0x30/+0x40), and +0x50 a VC6 std::vector<TractorArm>
// (allocator byte +0x50, begin/end/capacity +0x54/+0x58/+0x5c, stride 0x24). AddTarget grows
// it through FUN_0629e840. The byte the ctor writes at +0x50 is the allocator copy, not a
// flag. Deviation: a modern std::vector.
class COMMON_API CETractor : public CInternalEquip
{
public:
    CETractor(CShip* OwnerShip, unsigned short EquipId, Archetype::Tractor const* Arch,
              bool bMissionFlag);
    // 0x3d9a0. Retail inlines CInternalEquip(Other)'s field copy before copying
    // Transform/flag/arm array.
    CETractor(CETractor const& Other);
    virtual ~CETractor();

    // 0x3e440.
    virtual bool Update(float Dt, unsigned Flags) override;

    float GetRange() const;
    // 0x3e240.
    int CountActiveArms() const;
    // 0x3e270.
    TractorFailureCode AddTarget(CLoot* Target);
    // 0x3e3d0.
    void RemoveTarget(unsigned TargetId);
    // 0x3e4a0.
    TractorFailureCode VerifyTarget(CLoot const* Target) const;
    // 0x3e730. World-space position of the tractor beam's source
    // hardpoint (owner ship's world Transform composed with this equip's own HpXform).
    Vector GetSourcePos() const;
    // 0x3e7a0. Null if no arm currently holds a target with this id.
    TractorArm const* GetTractorArm(unsigned TargetId) const;
    static CETractor* __cdecl cast(CEquip* Equip);
    static CETractor const* __cdecl cast(CEquip const* Equip);
    // Listing 0x3fb90 (ICF-shared): return [this+0xc].
    Archetype::Tractor const* TractorArch() const;
    // .text:0629d990 (ordinal 1687, ICF-folded with CAsteroidField::get_axis_rotations):
    // `LEA EAX,[this+0x50]; RET` -- the arm vector itself.
    std::vector<TractorArm> const& GetTractorArms() const { return Arms; }

protected:
    // 0x3e660. Line-of-sight range check only (no actual raycast) --
    // same range test as VerifyTarget's first check, against an arbitrary target position.
    bool CanSeeTarget(CLoot const* Target) const;

    Transform HpXform;               // +0x20  beam source hardpoint, parent(ship)-space
    std::vector<TractorArm> Arms;    // +0x50  (TractorArm is completed right after CETractor)
    // The +0x50 offset check lives in the CETractor(CShip*, ...) ctor body in common_cetractor.cpp:
    // Arms is protected, and a member function added here only for the check would be exported
    // with the class (retail Common.dll exports no such member).
};

// TractorArm: one grab arm of a CETractor (tractor beam equipment), tracking a single CLoot
// pull in progress. Cluster: ctor(CETractor*) 0x3db80, copy-ctor 0x3d890, operator= 0x3d920,
// dtor 0x3dbd0, GetTarget 0x3d870, SetTarget 0x3ded0, TurnOff 0x3dea0, Update 0x3dc40,
// GetTipPos 0x3df10. Update/GetTipPos both compose the owner ship's world Transform with
// CETractor's HpXform through the 3DMathEngine's MulTransform (ResolveTractorSourceXform in
// common_cetractor.cpp); DAT_063fc22c is this cluster's cached Acquire3DMathEngine() slot.
class COMMON_API TractorArm
{
public:
    // From TractorArm::Update: 0 = idle (no target locked,
    // Update no-ops), 1 = pulling the target toward the beam tip, 2 = target reached the tip,
    // Timer counting down before consumption.
    enum Mode { MODE_0 = 0, MODE_1 = 1, MODE_2 = 2 };

    explicit TractorArm(CETractor* OwnerTractor);
    TractorArm(TractorArm const& Other);
    TractorArm& operator=(TractorArm const& Other);
    ~TractorArm();

    Mode GetMode() const { return (Mode)State; }
    // 0x0629d860 (ordinal 1778): on while State (+0x04) is anything but MODE_0.
    bool IsOn() const { return State != 0; }
    // 0x0629d880 (ordinal 1577, ICF-folded with CECargo::GetContainer): the stored ErrorCode.
    TractorFailureCode GetErrorCode() const { return ErrorCode; }
    // 0x3d870: the watched Watchable* minus CSimple::SelfWatchable's offset (0x9c), as in
    // CGuided::get_target.
    CLoot* GetTarget() const;
    void SetTarget(CLoot* Target);  // 0x3ded0
    void TurnOff();                 // 0x3dea0
    void Update(float Dt);          // 0x3dc40
    Vector GetTipPos() const;       // 0x3df10

    CETractor* Owner;             // +0x00
    unsigned State;               // +0x04  Mode
    BaseWatcher TargetWatch;      // +0x08  watches Target's CSimple::SelfWatchable
    Vector PullOffset;            // +0x10  accumulated pull toward beam tip (State == MODE_1)
    float Timer;                  // +0x1c  countdown after reaching the tip (State == MODE_2)
    TractorFailureCode ErrorCode; // +0x20

protected:
    // Retail `protected: static float const` exports (defined in common_tractorarm.cpp).
    // ACQUIRE_DELAY (ordinal 1174, 0x0639d80c = 1.0f) is moved into Timer (+0x1c) at 0629de02
    // when State becomes 2 -- the countdown before the towed CLoot is consumed.
    // GRAB_TOLERANCE_SQUARED (ordinal 1481, 0x0639d810 = 1.0f) has no reader in Common.dll.
    static float const ACQUIRE_DELAY;
    static float const GRAB_TOLERANCE_SQUARED;
};

static_assert(sizeof(TractorArm) == 0x24, "TractorArm sizeof 0x24");
static_assert(offsetof(TractorArm, TargetWatch) == 0x08, "TractorArm+0x08 target watcher");
static_assert(offsetof(TractorArm, PullOffset) == 0x10, "TractorArm+0x10 pull offset");
static_assert(offsetof(TractorArm, Timer) == 0x1c, "TractorArm+0x1c timer");
static_assert(offsetof(TractorArm, ErrorCode) == 0x20, "TractorArm+0x20 error code");

struct IObjDB;

// ScanList -- global scope in retail (UScanList@@, e.g. ?add@ScanList@@QAE_NPAUIObjRW@@@Z,
// ?get_scanlist@IObjInspectImpl@@UBEHAAPBUScanList@@I_N@Z). Layout from the ctor
// (.text:062c2610: 256 BaseWatchers of 8 bytes via the array-ctor helper FUN_06391ef0, then
// [+0x800] = 0, [+0x804] = 0x100) and every member below:
//   +0x000 BaseWatcher Entries[256], +0x800 int Count, +0x804 int Capacity. sizeof 0x808.
// Its only instance is CEScanner's own +0x24 member; IObjInspectImpl::get_scanlist hands out
// a pointer to it.
struct COMMON_API ScanList
{
    // ?add_in_range@ScanList@@QAEHABVVector@@MPAUIObjDB@@IPAUFilter@1@@Z -- a nested struct.
    // add_in_range calls slot 0 of it on every candidate (.text:062c27f8 / 062c28c9,
    // ECX = the filter, one pushed IObjRW*). CEScanner is the implementor
    // (??_7CEScanner@@6BFilter@ScanList@@@, slot 0 = ?add_obj@CEScanner@@MAE_NPAUIObjRW@@@Z).
    // Retail has no vftable of its own for it, so it is a pure interface.
    struct Filter
    {
        virtual bool add_obj(IObjRW* Obj) = 0;
    };

    ScanList();
    // ??0ScanList@@QAE@ABU0@@Z (.text:062948b0) and ??4ScanList@@QAEAAU0@ABU0@@Z
    // (.text:06294960): per-entry watcher copy, then Count and Capacity.
    ScanList(ScanList const& Other);
    ScanList& operator=(ScanList const& Other);

    bool add(IObjRW* Obj);
    void remove(IObjRW* Obj);
    void reset();
    int add_in_range(Vector const& Pos, float Range, IObjDB* Db, unsigned TypeMask, Filter* Filt);

    BaseWatcher Entries[256];   // +0x000 .. +0x7FF
    int Count;                  // +0x800
    int Capacity;               // +0x804  always 256 in practice (retail never resizes)
};

static_assert(offsetof(ScanList, Count) == 0x800, "ScanList+0x800 Count");
static_assert(offsetof(ScanList, Capacity) == 0x804, "ScanList+0x804 Capacity");
static_assert(sizeof(ScanList) == 0x808, "ScanList sizeof 0x808 (256 * sizeof(BaseWatcher) + 2 ints)");

// Listing CEScanner operator_new(0x840). Type 0x80000. Two bases, per retail's
// ??_7CEScanner@@6BCInternalEquip@@@ and ??_7CEScanner@@6BFilter@ScanList@@@ (.rdata 0639cac0):
// CInternalEquip at +0, the ScanList::Filter vptr at +0x20. Then the ScanList at +0x24, the
// mission byte at +0x82c, the cargo vector at +0x830 and the cached mask at +0x83c.
class COMMON_API CEScanner : public CInternalEquip, public ScanList::Filter
{
public:
    CEScanner(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Scanner const* Arch,
              bool bMissionFlag);
    CEScanner(CEScanner const& Other);
    virtual ~CEScanner();

    virtual bool Update(float Dt, unsigned Flags) override;

    void scan_for_types(unsigned TypeMask);
    void clear_cache();
    float GetRadarRange() const;
    float GetCargoScanRange() const;

    // Values from ScanCargo (0x3f8c0): out of range returns 0, insufficient power 1.
    // Callers check only ScanComplete.
    enum ScanResult
    {
        ScanTooFar = 0,
        ScanNotEnoughPower = 1,
        ScanComplete = 2,
        ScanNoTarget = 4
    };
    ScanResult ScanCargo(CEqObj* Target);

    Archetype::Scanner const* ScannerArch() const;
    static CEScanner* __cdecl cast(CEquip* Equip);
    static CEScanner const* __cdecl cast(CEquip const* Equip);

protected:
    // ?add_obj@CEScanner@@MAE_NPAUIObjRW@@@Z -- protected virtual, slot 0 of the
    // ScanList::Filter table (.text:0629f7c0).
    virtual bool add_obj(IObjRW* Obj) override;

public:
    // +0x20: the ScanList::Filter vptr.
    ScanList Scans;                   // +0x24 .. +0x82B
    unsigned char bMissionFlag;       // +0x82c (+0x82d..+0x82f: alignment)
    // VC6 std::vector<EquipDesc> for cargo scan results (3 pointers, matches EquipDescVector layout).
    EquipDesc* CargoVecBegin;         // +0x830
    EquipDesc* CargoVecEnd;           // +0x834
    EquipDesc* CargoVecCapacityEnd;   // +0x838
    unsigned CachedTypeMask;          // +0x83c
};

static_assert(sizeof(CEScanner) == 0x840, "CEScanner sizeof 0x840");

class COMMON_API CEArmor : public CInternalEquip
{
public:
    CEArmor(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Armor const* Arch,
            bool bMissionFlag);
    // 0x350b0: adds no fields -- plain CInternalEquip(Other).
    CEArmor(CEArmor const& Other);
    virtual ~CEArmor();
    Archetype::Armor const* ArmorArch() const;
    static CEArmor* __cdecl cast(CEquip* Equip);
    static CEArmor const* __cdecl cast(CEquip const* Equip);
};

static_assert(sizeof(CEArmor) == 0x20, "CEArmor sizeof 0x20");

class COMMON_API CECargoPod : public CAttachedEquip
{
public:
    CECargoPod(CEqObj* OwnerObj, unsigned short EquipId, Archetype::CargoPod const* Arch,
               bool bMissionFlag);
    // 0x35270: memberwise copy through CAttachedEquip(Other) plus Cargo.
    CECargoPod(CECargoPod const& Other);
    virtual ~CECargoPod();

    // 0x35980. Links the two objects: Cargo = Pod, and Pod->CargoPod = this.
    void FillPod(CECargo* Cargo);
    // 0x35990. Inverse of FillPod: if still linked, clears the CECargo's
    // CargoPod back-pointer to null, then clears this->Cargo to null.
    void EmptyPod();

    // 0x35950. A cargo pod is lootable iff it holds a CECargo with a non-zero stack count.
    virtual bool IsLootable() const override;
    // Listing 0x35970: a bare JMP to CExternalEquip::Destroy.
    virtual void Destroy() override;
    // Listing 0x369c0 (ICF-shared): return [this+0x4c], the cargo stack in the pod.
    CECargo const* GetContents() const;

    // 0x359a0.
    static CECargoPod* __cdecl cast(CEquip* Equip);
    static CECargoPod const* __cdecl cast(CEquip const* Equip);

    CECargo* Cargo;                   // +0x4c
};

static_assert(sizeof(CECargoPod) == 0x50, "CECargoPod sizeof 0x50");
static_assert(offsetof(CECargoPod, Cargo) == 0x4c, "CECargoPod+0x4c cargo");

class COMMON_API CEMineDropper : public CELauncher
{
public:
    CEMineDropper(CEqObj* OwnerObj, unsigned short EquipId, Archetype::MineDropper const* Arch,
                  bool bMissionFlag);
    // 0x36af0: adds no fields -- plain CELauncher(Other).
    CEMineDropper(CEMineDropper const& Other);
    virtual ~CEMineDropper();
    static CEMineDropper* __cdecl cast(CEquip* Equip);
    static CEMineDropper const* __cdecl cast(CEquip const* Equip);
    // Listing 0x3fb90 / 0x369c0 (ICF-shared, non-const in retail): [this+0xc] / [this+0x4c].
    Archetype::MineDropper const* MineDropperArch();
    Archetype::Mine const* MineArch();

protected:
    // 0x3a270: CELauncher::CanFire, then FireCruise when it said FireOk but the owner is a
    // CShip in cruise -- mines can't be dropped while cruising.
    virtual FireResult CanFire(Vector const& Target) const override;
};

static_assert(sizeof(CEMineDropper) == 500, "CEMineDropper sizeof 500");

class COMMON_API CECounterMeasureDropper : public CELauncher
{
public:
    CECounterMeasureDropper(CEqObj* OwnerObj, unsigned short EquipId,
                            Archetype::CounterMeasureDropper const* Arch, bool bMissionFlag);
    CECounterMeasureDropper(CECounterMeasureDropper const& Other);
    virtual ~CECounterMeasureDropper();

    // 0x3a320. If functioning, flag 0x20 isn't set on Flags, and
    // this launcher's archetype AiRangeSq (already squared on read) is positive: scans the
    // owning ship's own CEScanner (Owner->EquipManager.FindFirst(EquipTypeScanner)) tracked-
    // contact list for a same-owner missile/projectile within AiRangeSq of this equip's own
    // GetCenterOfMass(), and fires Notify(INotify::Event_0x19, nullptr) on the first hit -- see
    // the .cpp for the full IObjRW vtable-slot trace. Then the shared CELauncher::Update
    // epilogue (RefireElapsed accumulate + CAttachedEquip::Update).
    virtual bool Update(float Dt, unsigned Flags) override;

    // 0x3a4a0.
    static CECounterMeasureDropper* __cdecl cast(CEquip* Equip);
    static CECounterMeasureDropper const* __cdecl cast(CEquip const* Equip);
    // Listing 0x3fb90 / 0x369c0 (ICF-shared, non-const in retail): [this+0xc] / [this+0x4c].
    Archetype::CounterMeasureDropper const* CounterMeasureDropperArch();
    Archetype::CounterMeasure const* CounterMeasureArch();
};

static_assert(sizeof(CECounterMeasureDropper) == 500, "CECounterMeasureDropper sizeof 500");

class COMMON_API CELightEquip : public CExternalEquip
{
public:
    CELightEquip(CEqObj* OwnerObj, unsigned short EquipId, Archetype::Light const* Arch,
                 bool bMissionFlag);
    // 0x329b0. Same copy shape as CExternalEquip's (adds no fields).
    CELightEquip(CELightEquip const& Other);
    virtual ~CELightEquip();

    // 0x3f270.
    static CELightEquip* __cdecl cast(CEquip* Equip);
    static CELightEquip const* __cdecl cast(CEquip const* Equip);
    // Listing 0x3fb90 (ICF-shared): return [this+0xc].
    Archetype::Light const* LightArch() const;
    // Listing 0x3f110 (shared with CEAttachedFX): when connected and this light's connector
    // descends from the destroyed group's instance, disconnect and notify.
    virtual void NotifyArchGroupDestroyed(unsigned short SubObjId) override;
};

static_assert(sizeof(CELightEquip) == 0x28, "CELightEquip sizeof 0x28");

class COMMON_API CEAttachedFX : public CExternalEquip
{
public:
    CEAttachedFX(CEqObj* OwnerObj, unsigned short EquipId,
                 Archetype::AttachedFXEquip const* Arch, bool bMissionFlag);
    // 0x328f0. Same copy shape as CExternalEquip's (adds no fields).
    CEAttachedFX(CEAttachedFX const& Other);
    virtual ~CEAttachedFX();

    static CEAttachedFX* __cdecl cast(CEquip* Equip);
    static CEAttachedFX const* __cdecl cast(CEquip const* Equip);
    // Listing 0x3fb90 (ICF-shared): return [this+0xc].
    Archetype::AttachedFXEquip const* FXArch() const;
    // Listing 0x3f110 (shared with CELightEquip); see CELightEquip::NotifyArchGroupDestroyed.
    virtual void NotifyArchGroupDestroyed(unsigned short SubObjId) override;
};

static_assert(sizeof(CEAttachedFX) == 0x28, "CEAttachedFX sizeof 0x28");

// CEInternalFX — internal FX equip (asteroid-field ambient effects). Type bit 0x400000,
// inside CInternalEquip::cast's mask. Base is CInternalEquip: the vftable's GetEquipDesc slot
// is CInternalEquip::GetEquipDesc; the ctors and dtor inline CInternalEquip's trivial bodies.
class COMMON_API CEInternalFX : public CInternalEquip
{
public:
    CEInternalFX(CEqObj* OwnerObj, unsigned short EquipId,
                 Archetype::InternalFXEquip const* Arch, bool bMissionFlag);
    // 0x34ff0. CInternalEquip(Other) plus this class's vtable.
    CEInternalFX(CEInternalFX const& Other);
    virtual ~CEInternalFX();
    // Listing 0x3fe70. Releases Anim (AnimDB::Rem + FAnimScriptRef::Release, same as the
    // dtor) before chaining to the non-virtual CEquip::Destroy().
    virtual void Destroy();

    // 0x3fec0.
    static CEInternalFX* __cdecl cast(CEquip* Equip);
    static CEInternalFX const* __cdecl cast(CEquip const* Equip);
    // Listing 0x3fb90 (ICF-shared): return [this+0xc].
    Archetype::InternalFXEquip const* InternalFXArch() const;

    FAnimScriptRef Anim; // +0x20  ambient FX playback handle, -1 = none (FAnimScriptRef above)
};

static_assert(sizeof(CEInternalFX) == 0x24, "CEInternalFX sizeof 0x24");

// ShipGunStats is declared once, in common_cobject.h (beside CShip::get_gun_stats).
// The retail "engine object" wrapper (ctor 0xa98a0, sizeof 0x4c) is EngineObject in
// common_cobject.h.

// ============================================================================
// IAction — abstract action interface (ActionDB items) (0x27720)
// ============================================================================

// Retail ??_7IAction@@6B@ (.rdata:0639bf6c): [0] get_id (.text:0629ff10), [1] update =
// MSVCRT purecall (.text:06391fb0), [2] scalar deleting dtor (.text:06287780).
struct COMMON_API IAction
{
    IAction();
    IAction(IAction const& Other);
    IAction& operator=(IAction const& Other);

    static void* operator new(size_t Size);
    static void operator delete(void* Ptr);

    // Slot 0. .text:0629ff10: returns "" (.rdata:063997b0).
    virtual char const* get_id() const;

    // Slot 1, pure. ActionDB::update() calls it every tick; false removes the action.
    // LaneAction, LaunchAction and JumpInAction override it.
    virtual bool update(float Dt) = 0;

    // Slot 2.
    virtual ~IAction();

    // +0x04: ActionDB's singly-linked list link (ActionDB::add 0629ff3b, remove 0629fff7 /
    // 062a0007, update 0629ff66, find 062a0048, free 062a00d2). Zeroed by the ctor, copied by
    // the copy ctor and operator=.
    IAction* Next;
};

static_assert(sizeof(IAction) == 0x08, "IAction sizeof 0x08");
static_assert(offsetof(IAction, Next) == 0x04, "IAction+0x04 Next");

// ActionDB — singly-linked list of actions attached to ships/objects (0x3ff30)
struct COMMON_API ActionDB
{
public:
    ActionDB();
    ~ActionDB();
    ActionDB& operator=(ActionDB const& Other);

    void add(IAction* Action);
    void update(float Dt);
    bool remove(IAction* Action);
    IAction* find(char const* Name) const;
    int get_action_count() const;
    void free();

    IAction* Head; // +0x00
    IAction* Tail; // +0x04
};

static_assert(sizeof(ActionDB) == 0x08, "ActionDB sizeof 0x08");
static_assert(offsetof(ActionDB, Head) == 0x00, "ActionDB+0x00 Head");
static_assert(offsetof(ActionDB, Tail) == 0x04, "ActionDB+0x04 Tail");

// LaneRingTransform -- the lane frame at one ring, 0x34 bytes, LaneAction+0x28 (entrance) and
// +0x5c (exit). The ctor (062c1268-062c12b2) seeds an identity Orient and a zero Pos;
// LaneAction::BuildRingTransform (062c1880) fills all three fields.
struct LaneRingTransform
{
    IObjInspect const* Ring;  // +0x00: the ring object
    Matrix Orient;            // +0x04: the "HpRightLane" hardpoint orientation; turned 180 degrees
                              //        about Y when LaneAction::bHpLeftLane is zero
    Vector Pos;               // +0x28: the "HpRightLane" hardpoint position, or "HpLeftLane"'s
                              //        when bHpLeftLane is zero
};
static_assert(sizeof(LaneRingTransform) == 0x34, "LaneRingTransform sizeof 0x34");
static_assert(offsetof(LaneRingTransform, Orient) == 0x04, "LaneRingTransform+0x04 Orient");
static_assert(offsetof(LaneRingTransform, Pos) == 0x28, "LaneRingTransform+0x28 Pos");

// LaneAction — tradelane transit action controller (0x51960 / 0x62c1220)
// Not exported by retail (no LaneAction name in its export table).
class LaneAction : public PhySys::Controller, public IAction
{
public:
    LaneAction(IObjInspect const* InEntranceRing, IObjInspect const* InExitRing, IObjRW* InTradelaneObj,
               bool bFastExit, float Speed);
    virtual ~LaneAction();

    // The watched tradelane traveller: Watchable is IObjRW's third base (+8), so retail's
    // `head ? head - 8 : 0` is this downcast. Retail re-derives it at every use.
    IObjRW* TradelaneObj() const;

    virtual char const* get_id() const override { return "LaneAction"; }

    // 062c13f0, IAction slot 1 (reached through the IAction sub-object at +0x10): the action
    // stays in the owner's ActionDB while ExitState is nonzero.
    virtual bool update(float Dt) override;

    // 062c1490: the ctor's last step -- picks the start state and ramp, takes the traveller out
    // of collisions and control, registers this controller and captures the start pose.
    void InitializeApproach();

    // 062c1880: fills OutXform from the ring's lane hardpoints.
    void BuildRingTransform(IObjInspect const* Ring, LaneRingTransform& OutXform);

    // 062c1660: projects the traveller onto the A.Pos -> B.Pos segment. *OutProgress is the
    // fraction along it; *OutResult gets A.Orient and the projected point. Retail returns AL = 1,
    // which its only caller (062c1c0f) ignores.
    void ProjectOntoSegment(LaneRingTransform const& A, LaneRingTransform const& B, float* OutProgress, Transform* OutResult);

    // 062c19d0, PhySys::Controller slot 4 (RET 0x8): the per-tick lane drive. Reads
    // EventSim->delta_time only.
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim,
                                          IVP_U_Vector<IVP_Core>* CoreList) override;

    BaseWatcher TradelaneObjWatcher;  // +0x18  watches the tradelane object's Watchable (+8)
    IObjInspect const* EntranceRing;  // +0x20
    IObjInspect const* ExitRing;      // +0x24
    LaneRingTransform EntranceRingXform; // +0x28 .. +0x5b
    LaneRingTransform ExitRingXform;     // +0x5c .. +0x8f

    // +0x90, the lane state: 0 idle/stopped, 1 spinning up, 2 cruising, 3 last leg, 4 leaving
    // (request_exit, a cleared exit ring, the end of the lane or a departed formation leader),
    // 5 done (do_simulation_controller then calls stop(0) in the same tick).
    unsigned int ExitState;           // +0x90
    unsigned char bExitFlag;          // +0x94: set with state 4; turns the pose while leaving

    unsigned char Field95;            // +0x95: zeroed by the ctor (062c1370), no reader in
                                      // 062c1220..062c242a (LaneAction is not exported)
    // +0x96: 1 when the ring after the entrance ring (traverse_rings forward) is the exit
    // ring. It is the direction for every later traverse_rings, and BuildRingTransform takes
    // the "HpLeftLane" position and turns the frame 180 degrees when it is zero.
    unsigned char bHpLeftLane;
    unsigned char bRingApproachValid; // +0x97: the ctor's bool (go_tradelane's bFastExit);
                                      // InitializeApproach starts at state 2 instead of 1
    float Speed;                      // +0x98: the ctor's float (go_tradelane's); no reader in
                                      // 062c1220..062c242a

    // +0x9c: the pose beamed onto the traveller every tick. Outside state 4 it is the
    // projection onto the current leg (ProjectOntoSegment); in state 4 the traveller's own
    // transform, turned by bExitFlag.
    Transform LanePose;

    // +0xcc / +0xd8: the traveller's position and orientation when InitializeApproach ran; the
    // position advances with the lane every tick, and both feed the state-1 entry blend.
    Vector CapturedShipPos;
    Quaternion CapturedShipOrient;

    // +0xe8: seconds since construction. +0xec: speed fraction (lane speed = 2500 * it).
    // +0xf0: the fraction the leaving ramp ends at.
    float TransitElapsed;
    float TransitRamp;
    float TransitRampTarget;

    // +0xf4: TransitRamp at the first state-4 tick (seeded while it is still 0.0f); the leaving
    // ramp's rate is (it - TransitRampTarget * 0.4) per second.
    float ExitRampStart;

    // +0xf8: byte zeroed by the ctor (062c1376), no reader in 062c1220..062c242a;
    // +0xf9..+0xfb are never written.
    unsigned char Opaque_F8[4];

    // 062c2300: state 4 with bExitFlag set.
    void request_exit();

    // 062c2350: leaves the lane -- state 0, controller unregistered, the traveller's control
    // exclusion cleared and collisions enabled, IObjAffect::stop_tradelane unless
    // bSkipStopTradelane, throttle 1.0.
    void stop(char bSkipStopTradelane = 0);
};

static_assert(sizeof(LaneAction) == 0xfc, "LaneAction sizeof 0xfc");
static_assert(offsetof(LaneAction, EntranceRingXform) == 0x28, "LaneAction+0x28 EntranceRingXform");
static_assert(offsetof(LaneAction, ExitRingXform) == 0x5c, "LaneAction+0x5c ExitRingXform");
static_assert(offsetof(LaneAction, ExitState) == 0x90, "LaneAction+0x90 ExitState");
static_assert(offsetof(LaneAction, bExitFlag) == 0x94, "LaneAction+0x94 bExitFlag");
static_assert(offsetof(LaneAction, Field95) == 0x95, "LaneAction+0x95 Field95");
static_assert(offsetof(LaneAction, bHpLeftLane) == 0x96, "LaneAction+0x96 bHpLeftLane");
static_assert(offsetof(LaneAction, bRingApproachValid) == 0x97, "LaneAction+0x97 bRingApproachValid");
static_assert(offsetof(LaneAction, Speed) == 0x98, "LaneAction+0x98 Speed");
static_assert(offsetof(LaneAction, LanePose) == 0x9c, "LaneAction+0x9c LanePose");
static_assert(offsetof(LaneAction, CapturedShipPos) == 0xcc, "LaneAction+0xcc CapturedShipPos");
static_assert(offsetof(LaneAction, CapturedShipOrient) == 0xd8, "LaneAction+0xd8 CapturedShipOrient");
static_assert(offsetof(LaneAction, TransitElapsed) == 0xe8, "LaneAction+0xe8 TransitElapsed");
static_assert(offsetof(LaneAction, TransitRamp) == 0xec, "LaneAction+0xec TransitRamp");
static_assert(offsetof(LaneAction, TransitRampTarget) == 0xf0, "LaneAction+0xf0 TransitRampTarget");
static_assert(offsetof(LaneAction, ExitRampStart) == 0xf4, "LaneAction+0xf4 ExitRampStart");
static_assert(offsetof(LaneAction, Opaque_F8) == 0xf8, "LaneAction+0xf8 Opaque_F8");

// Retail exports these two as namespace-scope constants (`3MB` == `float const`):
//   ?launch_coast_time@LaunchActionVars@@3MB  .rdata:0639e568  0.5f   (ordinal 3235)
//   ?launch_decel_time@LaunchActionVars@@3MB  .rdata:0639e564  2.5f   (ordinal 3236)
// LaunchAction's Start/update/Step read them from .rdata (062b0103, 062b02b0/02b6,
// 062b05ca..062b0614).
namespace LaunchActionVars
{
    extern COMMON_API float const launch_coast_time;
    extern COMMON_API float const launch_decel_time;
}

// LaunchAction — ship launch action controller (ctor FUN_062afda0). Bases IAction (0x08) +
// PhySys::Controller (0x10) + pub::AI::DirectiveCallback (0x04) = 0x1c bytes; the ctor
// stamps DirectiveCallback's vptr at +0x18 like the other two.
// Not exported by retail; only the two LaunchActionVars constants above are.
class LaunchAction : public IAction, public PhySys::Controller, public pub::AI::DirectiveCallback
{
public:
    LaunchAction(IObjRW* DockObj, CEqObj* BaseObj, int LaunchType);
    virtual ~LaunchAction();

    virtual char const* get_id() const override { return "Launch"; }

    // Retail FUN_062aff80 -- the synchronous "Start" step run once immediately after
    // construction (CShip::launch calls it right after ActionDB::add). Either
    // performs the immediate physical launch (enables collisions/control exclusion,
    // computes LaunchPos/LaunchOrient via CEqObj::launch_pos, registers the physics
    // controller) or, when BaseObj's TypeBits say the dock point isn't ready for an
    // immediate launch, queues an AI DirectiveLaunchOp instead and returns.
    void Start();

    // 0x062b0270, IAction slot 1 (PTR_LAB_0639e5a4). Does work only when LaunchFlags & 0x20
    // (the immediate physical launch); on the AI-directive path it just returns
    // !bUpdateComplete. See common_launchaction.cpp.
    virtual bool update(float Dt) override;

    // 0x062b01f0, DirectiveCallback slot 0 (PTR_FUN_0639e57c). Called by IBehaviorManager
    // when the DirectiveLaunchOp queued by Start() finishes: clears the docked object's
    // control exclusion, notifies the dock's IObjAffect (slot 28) with BaseObj->Id and sets
    // bUpdateComplete -- the only way an AI-directive LaunchAction leaves its ActionDB.
    virtual void OnDirectiveComplete(int ResultWord11c, int ResultWord118) override;

    // LAB_062b0560, PhySys::Controller slot 4. While LaunchFlags == 0x20, advances
    // CoastElapsed by the event's delta_time, eases the docked ship out along LaunchDir
    // toward LaunchPos and beams it there every tick (as JumpInAction's does).
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim,
                                          IVP_U_Vector<IVP_Core>* CoreList) override;

    BaseWatcher DockObjWatcher;       // +0x1c  watches the launching object's Watchable (+8)
    CEqObj* BaseObj;                  // +0x24
    unsigned int LaunchFlags;         // +0x28  copy of BaseObj's CSimple::TypeBits (ctor); tested for bits 0x20 / 0x200100 in Start()
    int LaunchType;                   // +0x2c  forced to 1 by the ctor when LaunchFlags & 0x20
    // +0x30: advanced by do_simulation_controller; update() compares it with
    // launch_coast_time + launch_decel_time (3.0).
    float CoastElapsed;               // +0x30
    Vector LaunchPos;                 // +0x34  filled in by Start() via CEqObj::launch_pos plus a hardpoint offset
    Vector LaunchDir;                 // +0x40  cached forward column of LaunchOrient after Start() runs
    bool bNoDockingCamera;            // +0x4c  Start(): true when BaseObj's archetype has no docking camera
    bool bUpdateComplete;             // +0x4d  update(): true once the coast-elapsed threshold is reached or the dock watcher is gone; update() returns !bUpdateComplete
                                      // +0x4e .. +0x4f  alignment padding, never written
    Matrix LaunchOrient;              // +0x50  identity from the ctor, refined by CEqObj::launch_pos in Start()
    float CoastTimer;                 // +0x74  Start(): LaunchActionVars::launch_coast_time (0.5) * 80.0
};

static_assert(sizeof(LaunchAction) == 0x78, "LaunchAction sizeof 0x78");
static_assert(offsetof(LaunchAction, DockObjWatcher) == 0x1c, "LaunchAction+0x1c watcher");
static_assert(offsetof(LaunchAction, BaseObj) == 0x24, "LaunchAction+0x24 BaseObj");
static_assert(offsetof(LaunchAction, LaunchType) == 0x2c, "LaunchAction+0x2c LaunchType");
static_assert(offsetof(LaunchAction, LaunchPos) == 0x34, "LaunchAction+0x34 LaunchPos");
static_assert(offsetof(LaunchAction, LaunchOrient) == 0x50, "LaunchAction+0x50 LaunchOrient");
static_assert(offsetof(LaunchAction, CoastTimer) == 0x74, "LaunchAction+0x74 CoastTimer");

// JumpInAction — hyperspace jump-in action controller (0x52d40). Retail inlines the whole
// constructor into CShip::jump_in (no separate ctor call site).
// Its Controller vtable (PTR_core_is_going_to_be_deleted_event_0639e680) slot 4 is
// LAB_062afbe0, the do_simulation_controller override: an eased approach toward JumpPos
// over JumpSpeed seconds, not an instant teleport.
// Not exported by retail (no JumpInAction name in its export table).
class JumpInAction : public IAction, public PhySys::Controller
{
public:
    JumpInAction(IObjRW* JumpObj, Vector const& Pos, Matrix const& Orient, float Speed);
    virtual ~JumpInAction();

    virtual char const* get_id() const override { return "JumpIn"; }
    virtual bool update(float Dt) override;

    // LAB_062afbe0, PhySys::Controller slot 4; Dt is the event's delta_time.
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim,
                                          IVP_U_Vector<IVP_Core>* CoreList) override;

    BaseWatcher JumpObjWatcher;       // +0x18  watches the jumping object's Watchable (+8)
    float JumpElapsed;                // +0x20  elapsed-time accumulator for do_simulation_controller's ease-in
    float JumpSpeed;                  // +0x24  also the elapsed-time cap (seconds)
    float JumpApproachBudget;         // +0x28  JumpSpeed * 2000.0f; ease-out distance budget
    Matrix JumpOrient;                // +0x2c
    Vector JumpPos;                   // +0x50
    float Field5c;                    // +0x5c  retail constant 1500.0f; no reader found
    float Field60;                    // +0x60  retail constant 10.0f; no reader found
    bool bJumpComplete;               // +0x64  zeroed by the ctor, set by do_simulation_controller, polled by update(); +0x65..+0x67 padding
};

static_assert(sizeof(JumpInAction) == 0x68, "JumpInAction sizeof 0x68");
static_assert(offsetof(JumpInAction, JumpObjWatcher) == 0x18, "JumpInAction+0x18 watcher");
static_assert(offsetof(JumpInAction, JumpSpeed) == 0x24, "JumpInAction+0x24 JumpSpeed");
static_assert(offsetof(JumpInAction, JumpApproachBudget) == 0x28, "JumpInAction+0x28 JumpApproachBudget");
static_assert(offsetof(JumpInAction, JumpOrient) == 0x2c, "JumpInAction+0x2c JumpOrient");
static_assert(offsetof(JumpInAction, JumpPos) == 0x50, "JumpInAction+0x50 JumpPos");
static_assert(offsetof(JumpInAction, Field5c) == 0x5c, "JumpInAction+0x5c");
static_assert(offsetof(JumpInAction, Field60) == 0x60, "JumpInAction+0x60");

// CSteering — ship steering controller (pitch/yaw/roll torques) (0x5b5e0)
class COMMON_API CSteering : public CPhysControllerEquip
{
public:
    CSteering();
    CSteering(CSteering const& Other);
    virtual ~CSteering();
    CSteering& operator=(CSteering const& Other);

    virtual bool EnableController() override;

    bool Activate(bool bOn);
    bool IsActive() const;
    // .text:062bb610: the product is left unrounded in ST0 and every retail caller (CShip,
    // 062b3d25..062b4717) FDIVs it directly, hence double.
    double GetAxisSteeringTorque(unsigned Axis, float Factor) const;
    Vector const& GetAxisThrottle() const;
    void SetAxisThrottle(Vector const& Throttle);
    void SetOwner(CShip* Ship);

protected:
    // .text:062bb7c0 (ordinal 2446, protected virtual, slot 4). When IsControllerEnabled(),
    // bActive and AxisThrottle is non-zero: the throttle is taken into the ship's frame
    // (I3DMathEngine::InverseRotate), scaled per axis by Archetype::Ship::SteeringTorque{X,Y,Z},
    // rotated back out (I3DMathEngine::Rotate) and applied with PhySys::Controller::rotate.
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim,
                                          IVP_U_Vector<IVP_Core>* CoreList) override;

    // ?AXIS_CHANGE_THRESHOLD@CSteering@@1MB: protected static.
    static float const AXIS_CHANGE_THRESHOLD;

public:
    CShip* OwnerShip;        // +0x14
    bool bActive;            // +0x18  (+0x19..+0x1b alignment padding, never written)
    Vector AxisThrottle;     // +0x1c
};

static_assert(offsetof(CSteering, OwnerShip) == 0x14, "CSteering+0x14");
static_assert(sizeof(CSteering) == 0x28, "CSteering sizeof 0x28");
static_assert(offsetof(CSteering, bActive) == 0x18, "CSteering+0x18");
static_assert(offsetof(CSteering, AxisThrottle) == 0x1c, "CSteering+0x1c");

// CNudgeEngine — lightweight controller that applies a nudge vector to a ship (0x5b7e0)
class COMMON_API CNudgeEngine : public CPhysControllerEquip
{
public:
    CNudgeEngine();
    CNudgeEngine(CNudgeEngine const& Other);
    virtual ~CNudgeEngine();
    CNudgeEngine& operator=(CNudgeEngine const& Other);

    virtual bool EnableController() override;

    bool Activate(bool bOn);
    bool IsActive() const;
    Vector GetNudgeVec() const;
    void SetNudgeVec(Vector const& Vec);
    void SetOwner(CShip* Ship);

protected:
    // .text:062ad340 (ordinal 2444, protected virtual, slot 4). When IsControllerEnabled(),
    // bActive and CurrentNudge is non-zero (length squared compared with 0.0, not with
    // NUDGE_CHANGE_THRESHOLD): the nudge is rotated into world space (I3DMathEngine::Rotate by
    // the ship's transform), scaled by Archetype::Ship::NudgeForce and pushed.
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim,
                                          IVP_U_Vector<IVP_Core>* CoreList) override;

    // ?NUDGE_CHANGE_THRESHOLD@CNudgeEngine@@1MB: protected static.
    static float const NUDGE_CHANGE_THRESHOLD;

public:
    CShip* OwnerShip;        // +0x14
    bool bActive;            // +0x18  (+0x19..+0x1b alignment padding, never written)
    Vector CurrentNudge;     // +0x1c
};

static_assert(offsetof(CNudgeEngine, OwnerShip) == 0x14, "CNudgeEngine+0x14");

static_assert(sizeof(CNudgeEngine) == 0x28, "CNudgeEngine sizeof 0x28");
static_assert(offsetof(CNudgeEngine, bActive) == 0x18, "CNudgeEngine+0x18");
static_assert(offsetof(CNudgeEngine, CurrentNudge) == 0x1c, "CNudgeEngine+0x1c");

// CStrafeEngine — strafe thruster controller (0x5b920)
class COMMON_API CStrafeEngine : public CPhysControllerEquip
{
public:
    CStrafeEngine();
    CStrafeEngine(CStrafeEngine const& Other);
    virtual ~CStrafeEngine();
    CStrafeEngine& operator=(CStrafeEngine const& Other);

    virtual bool EnableController() override;

    StrafeDir GetStrafe() const;
    void SetStrafe(StrafeDir Dir);
    void SetOwner(CShip* Ship);

protected:
    // .text:062bb960 (ordinal 2447, protected virtual, slot 4). While CurrentStrafe is set:
    // drains Archetype::Ship::StrafePowerUsage * dt from CShip::ThrustPower (or, when the pool
    // cannot cover it, drops CurrentStrafe to StrafeNone) and pushes StrafeForce along -/+ the
    // ship's X column (Left/Right) or +/- its Y column (Up/Down).
    virtual void do_simulation_controller(IVP_Event_Sim* EventSim,
                                          IVP_U_Vector<IVP_Core>* CoreList) override;

public:
    CShip* OwnerShip;        // +0x14
    StrafeDir CurrentStrafe; // +0x18
};

static_assert(offsetof(CStrafeEngine, OwnerShip) == 0x14, "CStrafeEngine+0x14");

static_assert(sizeof(CStrafeEngine) == 0x1c, "CStrafeEngine sizeof 0x1c");
static_assert(offsetof(CStrafeEngine, CurrentStrafe) == 0x18, "CStrafeEngine+0x18");


// CETradeLaneEquip — tradelane jump equipment attached internally to ships.
// Type bit 0x800000. Inherits CInternalEquip layout. No extra fields beyond base.
class COMMON_API CETradeLaneEquip : public CInternalEquip
{
public:
    CETradeLaneEquip(CEqObj* OwnerObj, unsigned short EquipId,
                     Archetype::TradeLaneEquip const* Arch, bool bMissionFlag);
    // 0x34f40. CInternalEquip(Other) plus this class's vtable.
    CETradeLaneEquip(CETradeLaneEquip const& Other);
    virtual ~CETradeLaneEquip();

    static CETradeLaneEquip* __cdecl cast(CEquip* Equip);
    static CETradeLaneEquip const* __cdecl cast(CEquip const* Equip);

    Archetype::TradeLaneEquip const* TradeLaneArch() const;
};

static_assert(sizeof(CETradeLaneEquip) == 0x20, "CETradeLaneEquip sizeof 0x20");

// CERepairDroid — repair-droid internal equip. Type bit 0x200000, inside
// CInternalEquip::cast's mask. Base is CInternalEquip: the vftable's GetEquipDesc slot is
// CInternalEquip::GetEquipDesc; the ctor (0x3fbc0) and dtor inline CInternalEquip's trivial
// bodies, so they appear to call CEquip directly.
class COMMON_API CERepairDroid : public CInternalEquip
{
public:
    CERepairDroid(CEqObj* OwnerObj, unsigned short EquipId,
                  Archetype::RepairDroid const* Arch, bool bMissionFlag);
    // 0x34e00. CInternalEquip(Other) plus this class's vtable.
    CERepairDroid(CERepairDroid const& Other);
    virtual ~CERepairDroid();
    virtual float GetHitPoints() const;
    // 0x3fc50. Stores Hit; at or below 0.0f calls Activate(false) through vtable+0x30.
    virtual void SetHitPoints(float Hit);

    // 0x3fc80.
    static CERepairDroid* __cdecl cast(CEquip* Equip);
    static CERepairDroid const* __cdecl cast(CEquip const* Equip);
    // Listing 0x3fb90 (ICF-shared): return [this+0xc].
    Archetype::RepairDroid const* RepairDroidArch() const;

    float HitPts; // +0x20
};

static_assert(sizeof(CERepairDroid) == 0x24, "CERepairDroid sizeof 0x24");

class COMMON_API CECloakingDevice : public CAttachedEquip
{
public:
    CECloakingDevice(CEqObj* OwnerObj, unsigned short EquipId,
                     Archetype::CloakingDevice const* Arch, bool bMissionFlag);
    // 0x32fb0. Memberwise copy, base level first.
    CECloakingDevice(CECloakingDevice const& Other);
    virtual ~CECloakingDevice();

    virtual bool Update(float Dt, unsigned Flags) override;
    virtual bool Activate(bool bActive) override;
    // Returns double: retail leaves the fraction unrounded in ST0 (.text:0629461c / 0629464c).
    double cloak_percent() const;
    static CECloakingDevice* __cdecl cast(CEquip* Equip);
    static CECloakingDevice const* __cdecl cast(CEquip const* Equip);
    // Listing 0x3fb90 (ICF-shared): return [this+0xc].
    Archetype::CloakingDevice const* CloakArch() const;

    // 0x34520-0x347e0: a 2-state cross-fade engine. FadeTarget is
    // the state (0/1) Activate()'s most recent call is driving toward; BaselineState is the
    // last state the fade fully reached (== FadeTarget once the transition completes). While
    // they differ, a fade is in progress and cloak_percent()/Update() derive the 0..1 fraction
    // from CloakTimer against the archetype's CloakInTime (FadeTarget == 0 branch) or
    // CloakOutTime (FadeTarget != 0 branch). The meaning of state 0 vs 1 relative to
    // CEquip::bActive is not established beyond the branch structure.
    float CloakTimer;                 // +0x4c  elapsed time in the current fade
    unsigned char FadeTarget;         // +0x50  state Activate() is currently driving toward
    unsigned char BaselineState;      // +0x51  last state the fade fully settled at (+0x52/+0x53: alignment)
};

static_assert(sizeof(CECloakingDevice) == 0x54, "CECloakingDevice sizeof 0x54");
static_assert(offsetof(CECloakingDevice, CloakTimer) == 0x4c, "CECloakingDevice+0x4c timer");
static_assert(offsetof(CECloakingDevice, FadeTarget) == 0x50, "CECloakingDevice+0x50 state");
static_assert(offsetof(CECloakingDevice, BaselineState) == 0x51, "CECloakingDevice+0x51 target");
