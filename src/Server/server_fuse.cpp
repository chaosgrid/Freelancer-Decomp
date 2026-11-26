// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Server.dll fuse database and fuse-action classes -- retail .text:06cfc650-06cffc97.
// See src/include/server_fuse.h for what is and is not ported.

#include "../include/server_fuse.h"

#include "../include/common_ini.h"
#include "../include/common_cequip.h"      // CEquipTraverser, CExternalEquip (DestroyHardpoint)
#include "../include/common_goods.h"       // FindHardpoint_PS / HardpointToWorld (ImpulseAction)
#include "../include/server_gameobject.h"  // EqObj / GameObject slots every Trigger calls
#include "../include/server_sysdb.h"       // StarSystem::find_object (FUN_06d0c8d0)
#include "../include/server_createinfo.h"  // Server_Acquire3DMathEngine (FUN_06d5ff70)
#include "../include/x86math.h"            // I3DMathEngine::Rotate (TumbleAction)

#include <cstdlib> // rand (retail: MSVCRT.DLL::rand)
#include <cstring> // _stricmp (retail: DACOM.DLL::stricmp)

namespace
{

// The DamageList every damage Trigger builds inline, filled the same way everywhere: cause
// 0x15, the source as inflictor, and its owner player when the source is a ship in the
// owner's system. The CSimple read has no null test (a non-CSimple source faults, in retail
// and here).
void InitFuseDamageList(DamageList& Damage, EqObj* Owner, unsigned SourceId)
{
    unsigned int InflictorOwner = 0;
    Damage.Cause = DamageCause::Fuse;
    Damage.InflictorId = SourceId;
    if (Owner != nullptr && Owner->OwnerSystem != nullptr)
    {
        GameObject* Source = Owner->OwnerSystem->find_object(SourceId, OBJ_SHIP_MASK);
        if (Source != nullptr)
            InflictorOwner = Source->csimple()->OwnerPlayer;
    }
    Damage.InflictorOwner = InflictorOwner;
}

// TumbleAction's random interpolation, inlined four times: Low + rand() * 2^-15 * (High -
// Low). x87: evaluated in double, stored as a float.
float RandomBetween(int Roll, float Low, float High)
{
    double const Scaled = static_cast<double>(Roll) * (1.0 / 32768.0);
    return static_cast<float>(Scaled * (static_cast<double>(High) - static_cast<double>(Low)) +
                              static_cast<double>(Low));
}
} // namespace

// ---------------------------------------------------------------------------
// ServerFuse
// ---------------------------------------------------------------------------

ServerFuse::ServerFuse(ID_String const& Nickname, float Lifetime)
    : Fuse(Nickname, Lifetime)
    , Owner(nullptr)
    , bHasDestroyRoot(false)
{
}

ServerFuse::ServerFuse(ServerFuse const& Other)
    : Fuse(Other)
    , Owner(Other.Owner)
    , bHasDestroyRoot(Other.bHasDestroyRoot)
{
}

// .text:06cec7c0.
ServerFuse::~ServerFuse()
{
}

// FUN_06cfc650.
bool ServerFuse::Read(INI_Reader& Reader)
{
    if (!ReadFuseValues(Reader))
        return false;
    if (!Reader.read_header())
        return true;

    for (;;)
    {
        // The headers are tested in retail's order.
        ServerFuseAction* Action = nullptr;
        if (Reader.is_header("ignite_fuse"))
        {
            Action = new IgniteFuseAction();
        }
        else if (Reader.is_header("destroy_root"))
        {
            // Set before the allocation.
            bHasDestroyRoot = true;
            Action = new DestroyRootAction();
        }
        else if (Reader.is_header("damage_root"))
        {
            Action = new DamageRootAction();
        }
        else if (Reader.is_header("destroy_group"))
        {
            Action = new DestroyGroupAction();
        }
        else if (Reader.is_header("damage_group"))
        {
            Action = new DamageGroupAction();
        }
        else if (Reader.is_header("destroy_hp_attachment"))
        {
            Action = new DestroyHardpointAttachmentAction();
        }
        else if (Reader.is_header("damage_hp_attachment"))
        {
            Action = new DamageHardpointAttachmentAction();
        }
        else if (Reader.is_header("impulse"))
        {
            Action = new ImpulseAction();
        }
        else if (Reader.is_header("tumble"))
        {
            Action = new TumbleAction();
        }
        else if (Reader.is_header("make_invincible"))
        {
            Action = new MakeInvincibleAction();
        }
        else if (Reader.is_header("dump_cargo"))
        {
            Action = new DumpCargoAction();
        }
        else if (Reader.is_header("fuse"))
        {
            // The next fuse starts here; the caller reads it.
            return true;
        }

        if (Action != nullptr)
        {
            // Unlike the exe reader (FUN_00529e10), a failed read does NOT end the fuse: the
            // insert is skipped and the action leaks (retail).
            if (Action->Read(Reader))
            {
                Actions.insert(std::multimap<float, FuseAction*>::value_type(
                    Action->GetTriggerTime(), Action));
                if (Action->ShouldRandomize())
                    bNeedsRandomize = true;
            }
        }

        if (!Reader.read_header())
            return true;
    }
}

// FUN_06cfd1e0: the fuse and every action get the owner.
void ServerFuse::SetOwner(EqObj* NewOwner)
{
    Owner = NewOwner;
    for (std::multimap<float, FuseAction*>::iterator It = Actions.begin(); It != Actions.end(); ++It)
        static_cast<ServerFuseAction*>(It->second)->Owner = NewOwner;
}

// ---------------------------------------------------------------------------
// ServerFuseAction
// ---------------------------------------------------------------------------

ServerFuseAction::ServerFuseAction()
    : Owner(nullptr)
{
}

// FUN_06cfd1b0 is ~FuseAction inlined; the base destructor does the same here.
ServerFuseAction::~ServerFuseAction()
{
}

// ---------------------------------------------------------------------------
// IgniteFuseAction
// ---------------------------------------------------------------------------

IgniteFuseAction::IgniteFuseAction()
    : FuseT(0.0f)
{
    FuseId.clear();
}

// FUN_06cfcea0.
FuseAction* IgniteFuseAction::Clone() const
{
    IgniteFuseAction* Copy = new IgniteFuseAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

// FUN_06cfefd0.
bool IgniteFuseAction::Read(INI_Reader& Reader)
{
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("fuse"))
            FuseId.Id = CreateID(Reader.get_value_string());
        else if (Reader.is_value("fuse_t"))
            FuseT = (float)Reader.get_value_float(0);
    }
    return true;
}

// FUN_06cff150, shared with DestroyGroupAction and DestroyHardpointAttachmentAction.
void IgniteFuseAction::CopyArchProperties(IgniteFuseAction const& Other)
{
    FuseAction::CopyArchProperties(Other);
    FuseId = Other.FuseId;
    FuseT = Other.FuseT;
}

// FUN_06cff050. The light_fuse result is unused; FuseAction::Trigger is inlined in retail.
void IgniteFuseAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    if (!IsTriggered())
        Owner->light_fuse(SourceId, FuseId, SubObjId, FuseT, -1.0f);
    FuseAction::Trigger(SourceId, SubObjId);
}

// LAB_06cff0e0. Unlike the base body, this clears bTriggered; unlight_fuse runs whether or
// not the sub-object was present.
void IgniteFuseAction::UnTrigger(unsigned short SubObjId)
{
    FuseAction::UnTrigger(SubObjId);
    bTriggered = false;
    Owner->unlight_fuse(FuseId, SubObjId, FuseT);
}

// ---------------------------------------------------------------------------
// DestroyRootAction
// ---------------------------------------------------------------------------

// No fields of its own.
DestroyRootAction::DestroyRootAction()
{
}

// LAB_06cfcb30.
FuseAction* DestroyRootAction::Clone() const
{
    DestroyRootAction* Copy = new DestroyRootAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

// FUN_06cfe040: ReadFuseActionValue's result is not tested.
bool DestroyRootAction::Read(INI_Reader& Reader)
{
    while (Reader.read_value())
        ReadFuseActionValue(Reader);
    return true;
}

// FUN_06cfe070. FuseAction::Trigger is inlined in retail.
void DestroyRootAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    if (!IsTriggered())
        Owner->destroy(DESTROY_FUSE, SourceId);
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// DamageRootAction
// ---------------------------------------------------------------------------

DamageRootAction::DamageRootAction()
    : Hitpoints(0)
    , bCanKill(true)
{
}

// LAB_06cfcb90.
FuseAction* DamageRootAction::Clone() const
{
    DamageRootAction* Copy = new DamageRootAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

// FUN_06cfe0f0.
bool DamageRootAction::Read(INI_Reader& Reader)
{
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("damage_type"))
        {
            // DACOM's stricmp (case-insensitive, unlike DamageGroupAction's compare).
            if (_stricmp(Reader.get_value_string(), "absolute") == 0)
                bCanKill = false;
            else
                bCanKill = true;
        }
        else if (Reader.is_value("hitpoints"))
        {
            Hitpoints = Reader.get_value_int(0);
        }
    }
    return true;
}

// FUN_06cfe330.
void DamageRootAction::CopyArchProperties(DamageRootAction const& Other)
{
    FuseAction::CopyArchProperties(Other);
    Hitpoints = Other.Hitpoints;
    bCanKill = Other.bCanKill;
}

// LAB_06cfe180. FuseAction::Trigger is inlined in retail.
void DamageRootAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    if (!IsTriggered())
    {
        DamageList Damage;
        InitFuseDamageList(Damage, Owner, SourceId);
        Owner->inflict_hull_damage(static_cast<float>(Hitpoints), bCanKill, Damage);
        Owner->apply_damage_list(Damage);
    }
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// DestroyGroupAction
// ---------------------------------------------------------------------------

DestroyGroupAction::DestroyGroupAction()
    : Fate(0)
{
    GroupName.clear();
}

// FUN_06cfcc00.
FuseAction* DestroyGroupAction::Clone() const
{
    DestroyGroupAction* Copy = new DestroyGroupAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

// FUN_06cfe370.
bool DestroyGroupAction::Read(INI_Reader& Reader)
{
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("group_name"))
        {
            GroupName.Value = StringAlloc(Reader.get_value_string(), false);
        }
        else if (Reader.is_value("fate"))
        {
            // A stack TString<32> (clamped to 31 characters), compared byte-exactly. Any
            // other string leaves Fate unchanged.
            TString<32> FateName;
            FateName = Reader.get_value_string();
            if (FateName == "debris")
                Fate = 2;
            else if (FateName == "disappear")
                Fate = 3;
        }
    }
    return true;
}

// FUN_06cff150 (ICF with IgniteFuseAction's).
void DestroyGroupAction::CopyArchProperties(DestroyGroupAction const& Other)
{
    FuseAction::CopyArchProperties(Other);
    GroupName = Other.GroupName;
    Fate = Other.Fate;
}

// LAB_06cfe490. FuseAction::Trigger is inlined in retail.
void DestroyGroupAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    if (!IsTriggered())
    {
        // "random" (DAT_06d8d8ac) picks a living group and overwrites the name for good.
        if (_stricmp(GroupName.Value, GRandomGroupName) == 0)
        {
            // ceqobj() is not null-tested.
            CArchGroupManager& Groups = Owner->ceqobj()->ArchGroups;
            int const Living = Groups.NumLiving();
            if (Living > 0)
            {
                int const Target = (rand() * Living) / 0x8000;
                int Counter = 0;
                CArchGrpTraverser Walk;
                CArchGroup* Group = Groups.Traverse(Walk);
                while (Group != nullptr)
                {
                    // The counter is bumped BEFORE the compare, so the first living group is
                    // number 1 and a Target of 0 never matches: the name then stays "random"
                    // (retail quirk).
                    if (!Group->IsDead())
                    {
                        ++Counter;
                        if (Counter == Target)
                        {
                            // A raw pointer copy in retail.
                            GroupName = Group->Name();
                            break;
                        }
                    }
                    Group = Groups.Traverse(Walk);
                }
            }
            else
            {
                GroupName.clear();
            }
        }

        // An empty name skips the damage.
        if (GroupName.Value[0] != '\0')
        {
            DamageList Damage;
            InitFuseDamageList(Damage, Owner, SourceId);
            Owner->destroy_group_by_name(GroupName.Value,
                                         static_cast<DamageEntry::SubObjFate>(Fate), Damage);
            Owner->apply_damage_list(Damage);
        }
    }
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// DamageGroupAction
// ---------------------------------------------------------------------------

DamageGroupAction::DamageGroupAction()
    : Hitpoints(0)
    , bCanKill(true)
{
    GroupName.clear();
}

// FUN_06cfcca0.
FuseAction* DamageGroupAction::Clone() const
{
    DamageGroupAction* Copy = new DamageGroupAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

// FUN_06cfe6c0.
bool DamageGroupAction::Read(INI_Reader& Reader)
{
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("group_name"))
        {
            GroupName.Value = StringAlloc(Reader.get_value_string(), false);
        }
        else if (Reader.is_value("damage_type"))
        {
            // A byte-exact compare, NOT the stricmp DamageRootAction uses.
            TString<32> DamageType;
            DamageType = Reader.get_value_string();
            if (DamageType == "absolute")
                bCanKill = false;
            else
                bCanKill = true;
        }
        else if (Reader.is_value("hitpoints"))
        {
            Hitpoints = Reader.get_value_int(0);
        }
    }
    return true;
}

// FUN_06cfe950.
void DamageGroupAction::CopyArchProperties(DamageGroupAction const& Other)
{
    FuseAction::CopyArchProperties(Other);
    GroupName = Other.GroupName;
    Hitpoints = Other.Hitpoints;
    bCanKill = Other.bCanKill;
}

// LAB_06cfe7d0. No "random" pick here -- the name goes out as read.
void DamageGroupAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    if (!IsTriggered())
    {
        DamageList Damage;
        InitFuseDamageList(Damage, Owner, SourceId);
        Owner->inflict_group_damage(GroupName.Value, static_cast<float>(Hitpoints), bCanKill,
                                    Damage);
        Owner->apply_damage_list(Damage);
    }
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// DestroyHardpointAttachmentAction
// ---------------------------------------------------------------------------

DestroyHardpointAttachmentAction::DestroyHardpointAttachmentAction()
    : Fate(0)
{
    Hardpoint.clear();
}

// FUN_06cfcd50.
FuseAction* DestroyHardpointAttachmentAction::Clone() const
{
    DestroyHardpointAttachmentAction* Copy = new DestroyHardpointAttachmentAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

// FUN_06cfe9a0.
bool DestroyHardpointAttachmentAction::Read(INI_Reader& Reader)
{
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("hardpoint"))
        {
            Hardpoint.Value = StringAlloc(Reader.get_value_string(), false);
        }
        else if (Reader.is_value("fate"))
        {
            // Byte-exact compares, as in DestroyGroupAction, plus "loot".
            TString<32> FateName;
            FateName = Reader.get_value_string();
            if (FateName == "debris")
                Fate = 2;
            else if (FateName == "disappear")
                Fate = 3;
            else if (FateName == "loot")
                Fate = 5;
        }
    }
    return true;
}

// FUN_06cff150 (ICF).
void DestroyHardpointAttachmentAction::CopyArchProperties(DestroyHardpointAttachmentAction const& Other)
{
    FuseAction::CopyArchProperties(Other);
    Hardpoint = Other.Hardpoint;
    Fate = Other.Fate;
}

// LAB_06cfeae0. FuseAction::Trigger is inlined in retail.
void DestroyHardpointAttachmentAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    if (!IsTriggered())
    {
        // "random" (DAT_06d8d8a8) picks an attached equip's hardpoint.
        if (_stricmp(Hardpoint.Value, GRandomHardpointName) == 0)
        {
            // ceqobj() is not null-tested. CEquipTraverser(0x1fe0): attached equips.
            CEquipManager& Equips = Owner->ceqobj()->EquipManager;
            int Count = 0;
            CEquipTraverser Walk(kEquipMaskAttached);
            // First pass: count the live equips.
            CEquip* Equip = Equips.Traverse(Walk);
            while (Equip != nullptr)
            {
                if (!Equip->IsDestroyed())
                    ++Count;
                Equip = Equips.Traverse(Walk);
            }

            if (Count != 0)
            {
                int const Target = (rand() * Count) / 0x8000;
                Count = 0;
                Walk.Restart();
                // Second pass: EVERY equip bumps the counter, destroyed ones included, before
                // the compare -- so a destroyed hardpoint can be picked and a Target of 0
                // never matches (the name then stays "random"). Retail quirk.
                Equip = Equips.Traverse(Walk);
                while (Equip != nullptr)
                {
                    ++Count;
                    if (Count == Target)
                    {
                        // A raw pointer copy in retail; the 0x1fe0 traverser yields only
                        // attached (external) equips.
                        Hardpoint = static_cast<CExternalEquip*>(Equip)->GetParentHPName();
                        break;
                    }
                    Equip = Equips.Traverse(Walk);
                }
            }
            else
            {
                Hardpoint.clear();
            }
        }

        // An empty name skips the damage.
        if (Hardpoint.Value[0] != '\0')
        {
            DamageList Damage;
            InitFuseDamageList(Damage, Owner, SourceId);
            Owner->destroy_equip_by_hardpoint(Hardpoint.Value,
                                              static_cast<DamageEntry::SubObjFate>(Fate), Damage);
            Owner->apply_damage_list(Damage);
        }
    }
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// DamageHardpointAttachmentAction
// ---------------------------------------------------------------------------

DamageHardpointAttachmentAction::DamageHardpointAttachmentAction()
    : Hitpoints(0)
    , bCanKill(true)
{
    Hardpoint.clear();
}

// FUN_06cfcdf0.
FuseAction* DamageHardpointAttachmentAction::Clone() const
{
    DamageHardpointAttachmentAction* Copy = new DamageHardpointAttachmentAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

// FUN_06cfed40.
bool DamageHardpointAttachmentAction::Read(INI_Reader& Reader)
{
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("hardpoint"))
        {
            Hardpoint.Value = StringAlloc(Reader.get_value_string(), false);
        }
        else if (Reader.is_value("damage_type"))
        {
            // Byte-exact compare.
            TString<32> DamageType;
            DamageType = Reader.get_value_string();
            if (DamageType == "absolute")
                bCanKill = false;
            else
                bCanKill = true;
        }
        else if (Reader.is_value("hitpoints"))
        {
            Hitpoints = Reader.get_value_int(0);
        }
    }
    return true;
}

// FUN_06cfe950 (ICF with DamageGroupAction's).
void DamageHardpointAttachmentAction::CopyArchProperties(DamageHardpointAttachmentAction const& Other)
{
    FuseAction::CopyArchProperties(Other);
    Hardpoint = Other.Hardpoint;
    Hitpoints = Other.Hitpoints;
    bCanKill = Other.bCanKill;
}

// LAB_06cfee50 -- DamageGroupAction's body with inflict_equip_damage (+0x1cc). No "random"
// pick.
void DamageHardpointAttachmentAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    if (!IsTriggered())
    {
        DamageList Damage;
        InitFuseDamageList(Damage, Owner, SourceId);
        Owner->inflict_equip_damage(Hardpoint.Value, static_cast<float>(Hitpoints), bCanKill,
                                    Damage);
        Owner->apply_damage_list(Damage);
    }
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// ImpulseAction
// ---------------------------------------------------------------------------

ImpulseAction::ImpulseAction()
    : Force(0.0f)
    , Radius(0.0f)
    , Damage(0.0f)
{
    Hardpoint.clear();
    PosOffset.x = 0.0f;
    PosOffset.y = 0.0f;
    PosOffset.z = 0.0f;
}

// FUN_06cfcf60.
FuseAction* ImpulseAction::Clone() const
{
    ImpulseAction* Copy = new ImpulseAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

// FUN_06cff170.
bool ImpulseAction::Read(INI_Reader& Reader)
{
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("hardpoint"))
        {
            Hardpoint.Value = StringAlloc(Reader.get_value_string(), false);
        }
        else if (Reader.is_value("force"))
        {
            Force = (float)Reader.get_value_float(0);
        }
        else if (Reader.is_value("radius"))
        {
            Radius = (float)Reader.get_value_float(0);
        }
        else if (Reader.is_value("damage"))
        {
            Damage = (float)Reader.get_value_float(0);
        }
        else if (Reader.is_value("pos_offset"))
        {
            // All three read before any is stored.
            float const X = (float)Reader.get_value_float(0);
            float const Y = (float)Reader.get_value_float(1);
            float const Z = (float)Reader.get_value_float(2);
            PosOffset.x = X;
            PosOffset.y = Y;
            PosOffset.z = Z;
        }
    }
    return true;
}

// FUN_06cff550. The hardpoint IS copied here, unlike in the exe-side impulse action.
void ImpulseAction::CopyArchProperties(ImpulseAction const& Other)
{
    FuseAction::CopyArchProperties(Other);
    Hardpoint = Other.Hardpoint;
    Force = Other.Force;
    Radius = Other.Radius;
    Damage = Other.Damage;
    PosOffset = Other.PosOffset;
}

// LAB_06cff290 (vftable .rdata:06d66c78 +0x1c). SourceId is not read by the body -- the
// blast is the owner's own.
void ImpulseAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    if (!IsTriggered())
    {
        // Bails when the lookup fails or yields no system; the object out-param is never read.
        unsigned int OwnerId = Owner->get_id();
        GameObject* Found = nullptr;
        StarSystem* System = nullptr;
        if (Server_FindObject(OwnerId, Found, System) && System != nullptr)
        {
            long HpHandle = -1;
            HardpointInfo HpInfo;
            if (FindHardpoint_PS(Owner->get_index(), Hardpoint.Value, HpHandle, HpInfo))
            {
                HardpointToWorld(Owner->get_index(), HpInfo);

                Vector Offset;
                Server_MathEngine()->TransposeTransform(Offset, HpInfo.Orient, PosOffset);

                // x87: a float sum rounded from the 53-bit register equals the plain float sum.
                Vector BlastPos;
                BlastPos.x = static_cast<float>(static_cast<double>(Offset.x) + HpInfo.Position.x);
                BlastPos.y = static_cast<float>(static_cast<double>(Offset.y) + HpInfo.Position.y);
                BlastPos.z = static_cast<float>(static_cast<double>(Offset.z) + HpInfo.Position.z);

                // A stack explosion archetype named "FuseImpulse".
                Archetype::Explosion Blast(ID_String{CreateID("FuseImpulse")});
                Blast.Radius = Radius;
                Blast.Impulse = Force;
                Blast.HullDamage = Damage;

                // The inlined ctor's defaults (cause 4), then get_id once more and cause 0x15
                // over the 4.
                ExplosionDamageEvent Event;
                Event.ExplodingObjId = 0;
                Event.InflictorId = 0;
                Event.Cause = DamageCause::Explosion;
                Event.Explosion = nullptr;
                Event.SkipCollisionGroup = nullptr;
                unsigned int const BlastOwnerId = Owner->get_id();
                Event.ExplodingObjId = BlastOwnerId;
                Event.InflictorId = BlastOwnerId;
                Event.Position = BlastPos;
                Event.Cause = DamageCause::Fuse;
                Event.Explosion = &Blast;

                System->explode(Event);
            }
        }
    }
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// TumbleAction
// ---------------------------------------------------------------------------

TumbleAction::TumbleAction()
    : AngDragScale(1.0f)
    , ThrottleLow(0.0f)
    , ThrottleHigh(0.0f)
    , bEngineKill(false)
{
    TurnThrottleLow.x = 0.0f;
    TurnThrottleLow.y = 0.0f;
    TurnThrottleLow.z = 0.0f;
    TurnThrottleHigh.x = 1.0f;
    TurnThrottleHigh.y = 1.0f;
    TurnThrottleHigh.z = 1.0f;
}

// LAB_06cfd010.
FuseAction* TumbleAction::Clone() const
{
    TumbleAction* Copy = new TumbleAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

// FUN_06cff5a0.
bool TumbleAction::Read(INI_Reader& Reader)
{
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("ang_drag_scale"))
        {
            AngDragScale = (float)Reader.get_value_float(0);
        }
        else if (Reader.is_value("turn_throttle_x"))
        {
            TurnThrottleLow.x = (float)Reader.get_value_float(0);
            TurnThrottleHigh.x = (float)Reader.get_value_float(1);
        }
        else if (Reader.is_value("turn_throttle_y"))
        {
            TurnThrottleLow.y = (float)Reader.get_value_float(0);
            TurnThrottleHigh.y = (float)Reader.get_value_float(1);
        }
        else if (Reader.is_value("turn_throttle_z"))
        {
            TurnThrottleLow.z = (float)Reader.get_value_float(0);
            TurnThrottleHigh.z = (float)Reader.get_value_float(1);
        }
        else if (Reader.is_value("throttle"))
        {
            ThrottleLow = (float)Reader.get_value_float(0);
            ThrottleHigh = (float)Reader.get_value_float(1);
        }
        else if (Reader.is_value("engine_kill"))
        {
            bEngineKill = Reader.get_value_bool(0);
        }
    }
    return true;
}

// FUN_06cff940. The two turn-throttle vectors are NOT copied, so a clone keeps the
// constructor's (0,0,0) / (1,1,1) -- retail.
void TumbleAction::CopyArchProperties(TumbleAction const& Other)
{
    FuseAction::CopyArchProperties(Other);
    AngDragScale = Other.AngDragScale;
    ThrottleLow = Other.ThrottleLow;
    ThrottleHigh = Other.ThrottleHigh;
    bEngineKill = Other.bEngineKill;
}

// LAB_06cff6c0. Retail's null tests on the Owner -> CBase conversion come after Owner is
// already dereferenced, so they are not guards and have no counterpart here.
void TumbleAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    if (!IsTriggered())
    {
        // Players never tumble (the CSimple narrowing is not null-tested).
        if (Owner->csimple()->OwnerPlayer == 0)
        {
            // Ships only.
            CObject* Object = Owner->cobj();
            if (Object != nullptr &&
                (static_cast<unsigned int>(Object->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
            {
                // A ship driven by a behaviour manager is left alone.
                CEqObj* Cq = Owner->ceqobj();
                if (Cq->get_behavior_interface() == nullptr)
                {
                    CShip* Ship = static_cast<CShip*>(Owner->cobj());
                    Ship->AngularDragFactor = AngDragScale;

                    // Three random turn throttles, in x, y, z order.
                    Vector Axis;
                    Axis.x = RandomBetween(rand(), TurnThrottleLow.x, TurnThrottleHigh.x);
                    Axis.y = RandomBetween(rand(), TurnThrottleLow.y, TurnThrottleHigh.y);
                    Axis.z = RandomBetween(rand(), TurnThrottleLow.z, TurnThrottleHigh.z);

                    // Rotated into the ship's frame (I3DMathEngine +0x30).
                    Matrix const& Orientation = Owner->get_orientation();
                    Vector Rotated;
                    Server_MathEngine()->Rotate(Rotated,
                                            reinterpret_cast<Transform const&>(Orientation),
                                            Axis);

                    // Results unused.
                    Vector AxisThrottle;
                    AxisThrottle.x = Rotated.x;
                    AxisThrottle.y = Rotated.y;
                    AxisThrottle.z = Rotated.z;
                    static_cast<IObjAffect*>(Owner)->set_axis_throttle(AxisThrottle);

                    float const Throttle = RandomBetween(rand(), ThrottleLow, ThrottleHigh);
                    static_cast<IObjAffect*>(Owner)->set_throttle(Throttle);

                    // Deactivate the first engine (no null test); its id goes by address from
                    // a zero-extended dword local.
                    if (bEngineKill)
                    {
                        unsigned int EngineId = Cq->EquipManager.FindFirst(EquipTypeEngine)->Id;
                        unsigned short const* EngineIdPtr =
                            reinterpret_cast<unsigned short const*>(&EngineId);
                        static_cast<IObjAffect*>(Owner)->toggle_item(EngineIdPtr, false, 0);
                    }
                }
            }
        }
    }
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// MakeInvincibleAction
// ---------------------------------------------------------------------------

MakeInvincibleAction::MakeInvincibleAction()
    : bTurnOn(true)
{
}

// LAB_06cfd0a0.
FuseAction* MakeInvincibleAction::Clone() const
{
    MakeInvincibleAction* Copy = new MakeInvincibleAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

// FUN_06cff970.
bool MakeInvincibleAction::Read(INI_Reader& Reader)
{
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("turn_on"))
            bTurnOn = Reader.get_value_bool(0);
    }
    return true;
}

// FUN_06cffa80.
void MakeInvincibleAction::CopyArchProperties(MakeInvincibleAction const& Other)
{
    FuseAction::CopyArchProperties(Other);
    bTurnOn = Other.bTurnOn;
}

// LAB_06cff9d0. Type 4 is the invincibility list key; turning off sets MaxHpLoss to 1.0f.
// Bytes +0x0a/+0x0b are stack residue in retail, zeroed here.
void MakeInvincibleAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    if (!IsTriggered())
    {
        FInvincibility Command;
        Command.Type = 4;
        Command.bProtect = bTurnOn;
        Command.bFromPlayers = bTurnOn;
        Command.Pad06[0] = 0;
        Command.Pad06[1] = 0;
        Command.MaxHpLoss = bTurnOn ? 0.0f : 1.0f;
        Owner->set_invincible(Command);
    }
    FuseAction::Trigger(SourceId, SubObjId);
}

// ---------------------------------------------------------------------------
// DumpCargoAction
// ---------------------------------------------------------------------------

DumpCargoAction::DumpCargoAction()
{
    OriginHardpoint.clear();
}

// FUN_06cfd110.
FuseAction* DumpCargoAction::Clone() const
{
    DumpCargoAction* Copy = new DumpCargoAction();
    Copy->CopyArchProperties(*this);
    return Copy;
}

// FUN_06cffaa0.
bool DumpCargoAction::Read(INI_Reader& Reader)
{
    while (Reader.read_value())
    {
        if (ReadFuseActionValue(Reader))
            continue;
        if (Reader.is_value("origin_hardpoint"))
            OriginHardpoint.Value = StringAlloc(Reader.get_value_string(0), false);
    }
    return true;
}

// FUN_06cffc80.
void DumpCargoAction::CopyArchProperties(DumpCargoAction const& Other)
{
    FuseAction::CopyArchProperties(Other);
    OriginHardpoint = Other.OriginHardpoint;
}

// LAB_06cffb10. FuseAction::Trigger is inlined in retail.
void DumpCargoAction::Trigger(unsigned SourceId, unsigned short SubObjId)
{
    if (!IsTriggered())
    {
        DamageList Damage;
        InitFuseDamageList(Damage, Owner, SourceId);
        Owner->dump_cargo(OriginHardpoint.Value, Damage);
        Owner->apply_damage_list(Damage);
    }
    FuseAction::Trigger(SourceId, SubObjId);
}

// "random" -- the CRT dynamic initializers LAB_06cfe350 / LAB_06cfe980.
char const* GRandomGroupName = StringAlloc("random", false);     // DAT_06d8d8ac
char const* GRandomHardpointName = StringAlloc("random", false); // DAT_06d8d8a8

// ---------------------------------------------------------------------------
// ServerFuseDB
// ---------------------------------------------------------------------------

// DAT_06d8d870. Its CRT init / atexit pair is the std::map's own construction and
// destruction; neither deletes the ServerFuse values.
std::map<unsigned, ServerFuse*> ServerFuseDB::Fuses;

// FUN_06cfd390.
std::map<unsigned, ServerFuse*>& ServerFuseDB::Get()
{
    return Fuses;
}

// FUN_06cfd3a0: every non-null prototype deleted in key order, then the map cleared.
void ServerFuseDB::Clear()
{
    for (std::map<unsigned, ServerFuse*>::iterator It = Fuses.begin(); It != Fuses.end(); ++It)
    {
        if (It->second != nullptr)
            delete It->second;
    }
    Fuses.clear();
}

// FUN_06cfd4b0: one LoadFile per path in FuseDB::m_FuseINIFiles.
void ServerFuseDB::LoadAll()
{
    for (std::list<TString<260>>::iterator It = m_FuseINIFiles.begin(); It != m_FuseINIFiles.end(); ++It)
        LoadFile(It->Data);
}

// FUN_06cfd4e0.
void ServerFuseDB::LoadFile(char const* Path)
{
    INI_Reader Reader;
    if (!Reader.open(Path, false))
        return;
    Reader.read_header(); // result unused
    while (!Reader.is_end())
    {
        if (Reader.is_header("fuse"))
        {
            ServerFuse* NewFuse = new ServerFuse(ID_String{CreateID("")}, -1.0f);
            // A failed Read, or a nickname already in the map, leaks NewFuse (retail).
            if (NewFuse->Read(Reader))
                Fuses.insert(std::map<unsigned, ServerFuse*>::value_type(NewFuse->Name().Id, NewFuse));
        }
        else
        {
            Reader.read_header();
        }
    }
}
