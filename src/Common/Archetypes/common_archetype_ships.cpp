// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Archetype::DamageObjInfo(List), ShieldLink, CollisionGroup, EqObj, Solar, Ship (retail
// Archetype.cpp).

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
        // PTR_s_berth_063eca00, the 8-entry table EqObj::read's "docking_sphere" case consults.
        // A miss yields type 0 (EqObj::read masks the value with FUN_062fbb90's result).
        struct FDockTypeEntry
        {
            char const* Text;
            unsigned Value;
        };

        FDockTypeEntry const* DockTypeTable(unsigned* OutCount)
        {
            static FDockTypeEntry const kTable[] = {
                {"berth", 1},
                {"moor_small", 3},
                {"moor_medium", 4},
                {"moor_large", 5},
                {"ring", 6},
                {"pad", 6},
                {"jump", 7},
                {"airlock", 8},
            };
            if (OutCount != nullptr)
                *OutCount = sizeof(kTable) / sizeof(kTable[0]);
            return kTable;
        }

        // FUN_062fbb90 (thiscall on the table, RET 0xc: Name, &Out, Count): a null entry name is
        // skipped, otherwise DACOM.DLL stricmp(Name, entry) == 0 stores the value and returns
        // true; no match -> false with Out untouched. No guard on Name or Out.
        bool TryLookupDockType(char const* Name, unsigned* OutValue)
        {
            unsigned Count = 0;
            FDockTypeEntry const* Table = DockTypeTable(&Count);
            for (unsigned Index = 0; Index < Count; ++Index)
            {
                if (Table[Index].Text == nullptr)
                    continue;
                if (CallDacomStricmp(Name, Table[Index].Text) == 0)
                {
                    *OutValue = Table[Index].Value;
                    return true;
                }
            }
            return false;
        }
    }

    namespace
    {
        struct FNamedType
        {
            char const* Name;
            unsigned Type;
        };

        // PTR_DAT_063ec968, 19 entries.
        const FNamedType kCollisionGroupTypes[] = {
            {"Fin", 2},
            {"Top_Fin", 4},
            {"Port_Fin", 8},
            {"Starboard_Fin", 0x10},
            {"Port_Wing", 0x20},
            {"Starboard_Wing", 0x40},
            {"Middle_Wing", 0x80},
            {"Engine", 0x100},
            {"Port_Engine", 0x200},
            {"Middle_Engine", 0x400},
            {"Starboard_Engine", 0x800},
            {"Top_Engine", 0x1000},
            {"Bottom_Engine", 0x2000},
            {"Spoiler", 0x4000},
            {"Tail", 0x8000},
            {"Port_Side_Panel", 0x10000},
            {"Starboard_Side_Panel", 0x20000},
            {"Port_Arm", 0x40000},
            {"Starboard_Arm", 0x80000}
        };

        // PTR_s_scanner_063ec8f8, 14 entries. Values are EEquipType bits.
        const FNamedType kLinkedEquipTypes[] = {
            {"scanner", 0x80000},
            {"engine", 0x20000},
            {"weapon", 0x80},
            {"shield", 0x100},
            {"thruster", 0x400},
            {"power", 0x40000},
            {"light", 1},
            {"tractor", 0x100000},
            {"attached_fx", 2},
            {"internal_fx", 0x400000},
            {"tradelane", 0x800000},
            {"repairdroid", 0x200000},
            {"cloaking_device", 0x1000},
            {"armor", 0x1000000}
        };

        // FUN_062f9b10's two table walks ("type", "linked_equip"), DACOM stricmp.
        unsigned LookupNamedType(char const* Name, FNamedType const* Table, unsigned Count,
                                 unsigned DefaultValue)
        {
            unsigned Index = 0;
            for (; Index < Count; ++Index)
            {
                if (Table[Index].Name != nullptr && CallDacomStricmp(Name, Table[Index].Name) == 0)
                    return Table[Index].Type;
            }
            return DefaultValue;
        }

        // The per-entry test of DamageObjInfoList::IsValid and of FUN_062f9b10's two post-loop
        // walks: an empty hardpoint string (no null test) or zero low 30 id bits.
        bool DamageObjUnmatched(DamageObjInfo const& Info)
        {
            if (Info.Hardpoint.Value[0] == '\0')
                return true;
            if ((Info.Arch.Id & 0x3fffffffu) == 0)
                return true;
            return false;
        }

        // FUN_062f9700's two shapes: a last entry with an empty hardpoint (*_hp) or an empty id
        // (*_obj) is filled in, otherwise a new entry is appended. Retail reads the value after
        // the test; the order does not matter.
        void FillOrPushHp(std::vector<DamageObjInfo>& List, char const* Hp)
        {
            if (!List.empty() && List.back().Hardpoint.Value[0] == '\0')
            {
                List.back().Hardpoint.Value = Hp;
                return;
            }
            DamageObjInfo Info;
            Info.Hardpoint.Value = Hp;
            Info.Arch.clear();
            List.push_back(Info);
        }

        void FillOrPushObj(std::vector<DamageObjInfo>& List, unsigned ArchId)
        {
            if (!List.empty() && List.back().Arch.IsEmpty())
            {
                List.back().Arch.Id = ArchId;
                return;
            }
            DamageObjInfo Info;
            Info.Hardpoint.clear();
            Info.Arch.Id = ArchId;
            List.push_back(Info);
        }

        // FUN_062f9700 (cdecl: reader, dmg list, group-dmg list). Every value is the whole
        // value string. The retail list keeps its own size at +0x08; the port mirrors it in
        // Count.
        bool ReadDamagePair(INI_Reader& Reader, CollisionGroup* Group)
        {
            if (Reader.is_value("dmg_hp"))
            {
                FillOrPushHp(*Group->DmgObjs.Items, StringAlloc(Reader.get_value_string(), false));
                Group->DmgObjs.Count = (unsigned)Group->DmgObjs.Items->size();
                return true;
            }
            if (Reader.is_value("dmg_obj"))
            {
                FillOrPushObj(*Group->DmgObjs.Items, CreateID(Reader.get_value_string()));
                Group->DmgObjs.Count = (unsigned)Group->DmgObjs.Items->size();
                return true;
            }
            if (Reader.is_value("group_dmg_hp"))
            {
                FillOrPushHp(*Group->GroupDmgObjs.Items, StringAlloc(Reader.get_value_string(), false));
                Group->GroupDmgObjs.Count = (unsigned)Group->GroupDmgObjs.Items->size();
                return true;
            }
            if (Reader.is_value("group_dmg_obj"))
            {
                FillOrPushObj(*Group->GroupDmgObjs.Items, CreateID(Reader.get_value_string()));
                Group->GroupDmgObjs.Count = (unsigned)Group->GroupDmgObjs.Items->size();
                return true;
            }
            return false;
        }

        void WarnUnmatchedDamage(CollisionGroup* Group, std::vector<DamageObjInfo>* List,
                                 char const* Fmt, int Line)
        {
            unsigned Index = 0;
            for (; Index < List->size(); ++Index)
            {
                if (!DamageObjUnmatched((*List)[Index]))
                    continue;
                FDUMP(0x100002, Fmt, ArchetypeSource, Line, Group->ObjName);
                List->clear();
                return;
            }
        }
    }

    // Retail 0xf8a0. Hardpoint.clear() stores the StringCache pool's shared empty string.
    DamageObjInfo::DamageObjInfo()
    {
        Hardpoint.clear();
        Arch.clear();
    }

    // Retail 0x20ee0 (the same bytes also serve operator=).
    DamageObjInfo::DamageObjInfo(DamageObjInfo const& Other)
        : Hardpoint(Other.Hardpoint)
        , Arch(Other.Arch)
    {
    }

    // Retail 0xc6c0. Pad06 is not written by retail.
    ShieldLink::ShieldLink()
    {
        ShieldArch.clear();
        EquipId = SUBOBJ_ID_NONE;
        Hardpoint.clear();
    }

    // The four comparisons (retail 0xf840 / 0xf820 / 0xf860 / 0xf880) look at Arch.Id only,
    // unsigned; Hardpoint is not considered.
    bool DamageObjInfo::operator<(DamageObjInfo const& Other) const
    {
        return Arch.Id < Other.Arch.Id;
    }

    bool DamageObjInfo::operator==(DamageObjInfo const& Other) const
    {
        return Arch.Id == Other.Arch.Id;
    }

    bool DamageObjInfo::operator!=(DamageObjInfo const& Other) const
    {
        return Arch.Id != Other.Arch.Id;
    }

    bool DamageObjInfo::operator>(DamageObjInfo const& Other) const
    {
        return Other.Arch.Id < Arch.Id;
    }

    // Retail 0xf8f0. Retail's std::list (head at +0x04, size at +0x08) is a vector here; the
    // allocator byte is an uninitialised stack byte in retail.
    DamageObjInfoList::DamageObjInfoList()
        : Items(new std::vector<DamageObjInfo>())
        , Count(0)
    {
    }

    // Retail 0xf920 (the same bytes as list<CollisionGroupDesc>'s copy ctor).
    DamageObjInfoList::DamageObjInfoList(DamageObjInfoList const& Other)
        : Items(new std::vector<DamageObjInfo>(*Other.Items))
        , Count((unsigned)Other.Items->size())
    {
        Pad0[0] = Other.Pad0[0];
    }

    // Retail 0x468e0.
    DamageObjInfoList::~DamageObjInfoList()
    {
        delete Items;
        Items = nullptr;
        Count = 0;
    }

    // Retail 0xf9b0; returns at once on self-assignment. The vector assignment gives the same
    // element-wise result as retail's node overwrite / erase / append.
    DamageObjInfoList& DamageObjInfoList::operator=(DamageObjInfoList const& Other)
    {
        if (this != &Other)
        {
            *Items = *Other.Items;
            Count = (unsigned)Items->size();
        }
        return *this;
    }

    // Retail 0xf8c0: every entry needs a non-empty hardpoint and a nonzero low-30-bit id; an
    // empty list is valid.
    bool DamageObjInfoList::IsValid()
    {
        for (DamageObjInfo const& Info : *Items)
        {
            if (DamageObjUnmatched(Info))
                return false;
        }
        return true;
    }

    // FuseIgnitionList's four members are the same list code inlined in CollisionGroup's ctor,
    // copy ctor and operator= (see the class comment).
    FuseIgnitionList::FuseIgnitionList()
        : Items(new std::vector<FGroupFuse>())
        , Count(0)
    {
    }

    FuseIgnitionList::FuseIgnitionList(FuseIgnitionList const& Other)
        : Items(new std::vector<FGroupFuse>(*Other.Items))
        , Count((unsigned)Other.Items->size())
    {
        Pad0[0] = Other.Pad0[0];
    }

    FuseIgnitionList::~FuseIgnitionList()
    {
        delete Items;
        Items = nullptr;
        Count = 0;
    }

    FuseIgnitionList& FuseIgnitionList::operator=(FuseIgnitionList const& Other)
    {
        if (this != &Other)
        {
            *Items = *Other.Items;
            Count = (unsigned)Items->size();
        }
        return *this;
    }

    CollisionGroup::CollisionGroup()
        : Next(nullptr)
        , Id(0)
        , ObjName("")
        , Type(1)
        , HitPts(0)
        , bSeparable(false)
        , bDestroyParent(false)
        , bRootHealthProxy(false)
        , ParentImpulse(0.0f)
        , ChildImpulse(0.0f)
        , Mass(1.0f)
        , bHasRotationInertia(false)
        , RotationInertiaX(0.0f)
        , RotationInertiaY(0.0f)
        , RotationInertiaZ(0.0f)
        , ExplosionResistance(1.0f)
        , LinkedEquipType(0)
        , LinkedEquipHealth(0.0f)
    {
        // ObjName (+0x08) gets the StringCache pool's shared empty string, not a literal.
        Id = SUBOBJ_ID_NONE;
        CacheString EmptyName;
        EmptyName.clear();
        ObjName = EmptyName.Value;
        SeparationExplosion.clear();
        DebrisType.clear();
    }

    CollisionGroup::CollisionGroup(CollisionGroup const& Other)
        // Retail 0xfa70. Retail's three std::list members are vectors here; same contents.
        : Next(Other.Next)
        , Id(Other.Id)
        , ObjName(Other.ObjName)
        , Type(Other.Type)
        , HitPts(Other.HitPts)
        , bSeparable(Other.bSeparable)
        , bDestroyParent(Other.bDestroyParent)
        , bRootHealthProxy(Other.bRootHealthProxy)
        , ParentImpulse(Other.ParentImpulse)
        , ChildImpulse(Other.ChildImpulse)
        , Mass(Other.Mass)
        , bHasRotationInertia(Other.bHasRotationInertia)
        , RotationInertiaX(Other.RotationInertiaX)
        , RotationInertiaY(Other.RotationInertiaY)
        , RotationInertiaZ(Other.RotationInertiaZ)
        , SeparationExplosion(Other.SeparationExplosion)
        , DebrisType(Other.DebrisType)
        , ExplosionResistance(Other.ExplosionResistance)
        // Each list's size is the number of elements copied, not Other's size field.
        , DmgObjs(Other.DmgObjs)
        , GroupDmgObjs(Other.GroupDmgObjs)
        , Fuses(Other.Fuses)
        , LinkedEquipType(Other.LinkedEquipType)
        , LinkedEquipHealth(Other.LinkedEquipHealth)
    {
    }

    CollisionGroup::~CollisionGroup()
    {
        // The three lists free themselves (DamageObjInfoList::~ 0x468e0 for the first two).
    }

    CollisionGroup& CollisionGroup::operator=(CollisionGroup const& Other)
    {
        // Retail 0xfbc0. Retail reuses the list nodes in place; the vector assignment gives the
        // same contents. Retail also copies the intrusive Next pointer (reproduced; no call site
        // is known). No top-level self test; each list has its own.
        Next = Other.Next;
        Id = Other.Id;
        ObjName = Other.ObjName;
        Type = Other.Type;
        HitPts = Other.HitPts;
        bSeparable = Other.bSeparable;
        bDestroyParent = Other.bDestroyParent;
        bRootHealthProxy = Other.bRootHealthProxy;
        ParentImpulse = Other.ParentImpulse;
        ChildImpulse = Other.ChildImpulse;
        Mass = Other.Mass;
        bHasRotationInertia = Other.bHasRotationInertia;
        RotationInertiaX = Other.RotationInertiaX;
        RotationInertiaY = Other.RotationInertiaY;
        RotationInertiaZ = Other.RotationInertiaZ;
        SeparationExplosion = Other.SeparationExplosion;
        DebrisType = Other.DebrisType;
        ExplosionResistance = Other.ExplosionResistance;
        DmgObjs = Other.DmgObjs;
        GroupDmgObjs = Other.GroupDmgObjs;
        Fuses = Other.Fuses;
        LinkedEquipType = Other.LinkedEquipType;
        LinkedEquipHealth = Other.LinkedEquipHealth;
        return *this;
    }

    // FUN_062f9b10, cdecl(reader, owner, id). Both callers (LoadShips, LoadSolar) pass a
    // non-null owner. "obj", "type", "separation_explosion" and "debris_type" read the whole
    // value string; "fuse" and "linked_equip" the indexed one.
    void ReadCollisionGroup(INI_Reader& Reader, EqObj* Owner, unsigned short GroupId)
    {
        CollisionGroup* Group = new CollisionGroup();
        Group->Id = GroupId;

        while (Reader.read_value())
        {
            if (Reader.is_value("obj"))
            {
                Group->ObjName = StringAlloc(Reader.get_value_string(), false);
            }
            else if (Reader.is_value("type"))
            {
                Group->Type = LookupNamedType(Reader.get_value_string(), kCollisionGroupTypes,
                                              sizeof(kCollisionGroupTypes) / sizeof(kCollisionGroupTypes[0]),
                                              1);
            }
            else if (Reader.is_value("hit_pts"))
            {
                Group->HitPts = Reader.get_value_int(0);
            }
            else if (Reader.is_value("separable"))
            {
                Group->bSeparable = true;
            }
            else if (Reader.is_value("destroy_parent"))
            {
                if (Reader.is_value_empty(0))
                    Group->bDestroyParent = true;
                else
                    Group->bDestroyParent = Reader.get_value_bool(0);
            }
            else if (Reader.is_value("root_health_proxy"))
            {
                Group->bRootHealthProxy = Reader.get_value_bool(0);
            }
            else if (Reader.is_value("parent_impulse"))
            {
                Group->ParentImpulse = Reader.get_value_float(0);
            }
            else if (Reader.is_value("child_impulse"))
            {
                Group->ChildImpulse = Reader.get_value_float(0);
            }
            else if (Reader.is_value("mass"))
            {
                Group->Mass = Reader.get_value_float(0);
            }
            else if (Reader.is_value("rotation_inertia"))
            {
                Group->RotationInertiaX = Reader.get_value_float(0);
                Group->RotationInertiaY = Reader.get_value_float(1);
                Group->RotationInertiaZ = Reader.get_value_float(2);
                Group->bHasRotationInertia = true;
            }
            else if (Reader.is_value("separation_explosion"))
            {
                Group->SeparationExplosion.Id = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("debris_type"))
            {
                Group->DebrisType.Id = CreateID(Reader.get_value_string());
            }
            else if (ReadDamagePair(Reader, Group))
            {
            }
            else if (Reader.is_value("explosion_resistance"))
            {
                Group->ExplosionResistance = Reader.get_value_float(0);
            }
            else if (Reader.is_value("fuse"))
            {
                FGroupFuse Fuse;
                Fuse.Fuse.Id = CreateID(Reader.get_value_string(0));
                Fuse.Value1 = Reader.get_value_float(1);
                Fuse.Value2 = Reader.get_value_float(2);
                Group->Fuses.Items->push_back(Fuse);
                Group->Fuses.Count = (unsigned)Group->Fuses.Items->size();
            }
            else if (Reader.is_value("linked_equip"))
            {
                Group->LinkedEquipType =
                    LookupNamedType(Reader.get_value_string(0), kLinkedEquipTypes,
                                    sizeof(kLinkedEquipTypes) / sizeof(kLinkedEquipTypes[0]), 0);
                Group->LinkedEquipHealth = Reader.get_value_float(1);
            }
        }

        WarnUnmatchedDamage(Group, Group->DmgObjs.Items,
                            "%s(%d) : *** WARNING: Unmatched dmg_obj or dmg_hp values for Group[%s].  No damage objects will be attached.",
                            0x11de);
        Group->DmgObjs.Count = (unsigned)Group->DmgObjs.Items->size();
        WarnUnmatchedDamage(Group, Group->GroupDmgObjs.Items,
                            "%s(%d) : *** WARNING: Unmatched group_dmg_obj or group_dmg_hp values for Group[%s].  No damage objects will be attached.",
                            0x11e3);
        Group->GroupDmgObjs.Count = (unsigned)Group->GroupDmgObjs.Items->size();

        if (Owner->CollisionGroups == nullptr)
        {
            Owner->CollisionGroups = Group;
            return;
        }
        CollisionGroup* Tail = Owner->CollisionGroups;
        while (Tail->Next != nullptr)
            Tail = Tail->Next;
        Tail->Next = Group;
    }

    EqObj::EqObj(ICliObj* InCompanion)
        : Root(InCompanion)
        , CollisionGroups(nullptr)
        , ShieldIdNext(0xfff0)
        , ShieldLinks(nullptr)
        , Fuses(nullptr)
        , bNomad(false)
        , bDockingCamera(true)
    {
        memset(Pad62, 0, sizeof(Pad62));
        memset(Pad6c, 0, sizeof(Pad6c));
        memset(Pad78, 0, sizeof(Pad78));
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
        memset(DockingSpheresPad, 0, sizeof(DockingSpheresPad));
#endif

        ShieldLinks = new std::vector<ShieldLink>();
        Fuses = new std::vector<FGroupFuse>();
    }

    // Retail 0x91460: Root(0), empty sub-objects, then EqObj::operator= inline. Deviation:
    // retail never writes bNomad (+0x80) and bDockingCamera (+0x81), so they keep whatever the
    // allocation held; the port gives them the default ctor's values (false / true).
    EqObj::EqObj(EqObj const& Other)
        : Root(nullptr)
        , CollisionGroups(nullptr)
        , ShieldIdNext(0xfff0)
        , ShieldLinks(new std::vector<ShieldLink>())
        , Fuses(new std::vector<FGroupFuse>())
        , bNomad(false)
        , bDockingCamera(true)
    {
        memset(Pad62, 0, sizeof(Pad62));
        memset(Pad6c, 0, sizeof(Pad6c));
        memset(Pad78, 0, sizeof(Pad78));
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
        memset(DockingSpheresPad, 0, sizeof(DockingSpheresPad));
#endif
        operator=(Other);
    }

    EqObj::~EqObj()
    {
        CollisionGroup* Group = CollisionGroups;
        while (Group != nullptr)
        {
            CollisionGroup* Next = Group->Next;
            delete Group;
            Group = Next;
        }
        CollisionGroups = nullptr;
        delete ShieldLinks;
        ShieldLinks = nullptr;
        delete Fuses;
        Fuses = nullptr;
    }

    // Backs Ship/Solar "inherit=" (LoadShips 0x062f2d43 / LoadSolar 0x062f383d). Root part,
    // the docking-sphere vector (retail inlines vector::operator=), and a fresh copy of the
    // CollisionGroup chain (operator new(0x6c) + copy ctor per node). Like retail, an existing
    // destination chain is not freed first (both callers assign into a fresh EqObj).
    // ShieldIdNext, ShieldLinks, Fuses, bNomad and bDockingCamera are not copied by retail
    // either. Returns EqObj const&, matching retail's mangled return type.
    EqObj const& EqObj::operator=(EqObj const& Other)
    {
        Root::operator=(Other);

        DockingSpheres = Other.DockingSpheres;

        // The donor's head is read before *this is written, so self-assignment walks safely.
        CollisionGroup* SrcHead = Other.CollisionGroups;
        CollisionGroup* NewHead = nullptr;
        CollisionGroup** Tail = &NewHead;
        for (CollisionGroup const* Cur = SrcHead; Cur != nullptr; Cur = Cur->Next)
        {
            CollisionGroup* NewNode = new CollisionGroup(*Cur);
            NewNode->Next = nullptr;
            *Tail = NewNode;
            Tail = &NewNode->Next;
        }
        CollisionGroups = NewHead;

        return *this;
    }

    // 0x062f1c60, a tail jump to Root::redefine.
    void EqObj::redefine(Root const& Other)
    {
        Root::redefine(Other);
    }

    bool EqObj::read(INI_Reader& Reader)
    {
        if (Root::read(Reader))
            return true;

        if (Reader.is_value("docking_camera"))
        {
            bDockingCamera = Reader.get_value_bool(0);
            return true;
        }

        if (Reader.is_value("docking_sphere"))
        {
            // Lookup FUN_062fbb90, push helper FUN_062fc710. The hardpoint name is lower-cased
            // in place.
            DockingSphere Sphere;

            char const* TypeName = Reader.get_value_string(0);
            unsigned FoundType = 0;
            Sphere.Type = TryLookupDockType(TypeName, &FoundType) ? FoundType : 0;

            char* HardpointAlloc = StringAlloc(Reader.get_value_string(1), false);
            _strlwr(HardpointAlloc);
            Sphere.HardpointName = HardpointAlloc;

            Sphere.Radius = Reader.get_value_float(2);

            if (!Reader.is_value_empty(3))
                Sphere.AnimName = StringAlloc(Reader.get_value_string(3), false);

            DockingSpheres.push_back(Sphere);
            return true;
        }

        if (Reader.is_value("fuse"))
        {
            // Parsed as ReadCollisionGroup's "fuse" case; stored in the Fuses vector (+0x74).
            FGroupFuse Fuse;
            Fuse.Fuse.Id = CreateID(Reader.get_value_string(0));
            Fuse.Value1 = Reader.get_value_float(1);
            Fuse.Value2 = Reader.get_value_float(2);
            Fuses->push_back(Fuse);
            return true;
        }

        if (Reader.is_value("shield_link"))
        {
            ShieldLink Link;
            Link.ShieldArch.clear();
            Link.EquipId = 0;
            Link.Pad06 = 0;
            Link.Hardpoint.clear();
            char const* ArchName = Reader.get_value_string(0);
            Link.ShieldArch.Id = CreateID(ArchName);
            ++ShieldIdNext;
            Link.EquipId = ShieldIdNext;
            Link.Hardpoint.Value = StringAlloc(Reader.get_value_string(1), false);
            unsigned ValueIndex = 2;
            while (!Reader.is_value_empty(ValueIndex))
            {
                CacheString HpName;
                HpName.Value = StringAlloc(Reader.get_value_string(ValueIndex), false);
                Link.GeneratorHps.push_back(HpName);
                ValueIndex += 1;
            }
            ShieldLinks->push_back(Link);
            return true;
        }

        if (Reader.is_value("nomad"))
        {
            bNomad = Reader.get_value_bool(0);
            return true;
        }

        return false;
    }

    AClassType EqObj::get_class_type() const
    {
        return AClassEqObj;
    }

    CollisionGroup const* EqObj::get_group_by_id(unsigned short GroupId) const
    {
        CollisionGroup* Group = CollisionGroups;
        while (Group != nullptr)
        {
            if (Group->Id == GroupId)
                return Group;
            Group = Group->Next;
        }
        return nullptr;
    }

    CollisionGroup const* EqObj::get_group_by_name(CacheString const& Name) const
    {
        CollisionGroup* Group = CollisionGroups;
        while (Group != nullptr)
        {
            // Interned StringCache pointers are compared, not their text.
            if (Group->ObjName == Name.Value)
                return Group;
            Group = Group->Next;
        }
        return nullptr;
    }

    bool EqObj::traverse_groups(CollisionGroup const*& Cursor) const
    {
        if (Cursor == nullptr)
        {
            Cursor = CollisionGroups;
            return Cursor != nullptr;
        }
        Cursor = Cursor->Next;
        return Cursor != nullptr;
    }

    // Common.dll.c 0x91f00. See header comment: archetype-level list, every group
    // reported at full (1.0f) health since the archetype carries no per-instance damage.
    bool EqObj::get_undamaged_collision_group_list(std::list<CollisionGroupDesc>& OutDescs) const
    {
        OutDescs.clear();
        for (CollisionGroup const* Group = CollisionGroups; Group != nullptr; Group = Group->Next)
        {
            CollisionGroupDesc Desc;
            Desc.Id = Group->Id;
            Desc.Pad02 = 0;
            Desc.Health = 1.0f;
            OutDescs.push_back(Desc);
        }
        return true;
    }

    bool EqObj::dock_terminal_valid(int Index) const
    {
        // Retail 0xc950: Index != -1, a non-null end, and Index below the element count.
        if (Index == -1 || DockingSpheres.empty())
            return false;
        return (size_t)Index < DockingSpheres.size();
    }

    Solar::Solar(ICliObj* InCompanion)
        : EqObj(InCompanion)
        , bDestructible(false)
        , SolarRadius(500.0f)
        , Toughness(0)
    {
        OpenAnim.clear();
        JumpOutHp.clear();
        Loadout.clear();
    }

    Solar::~Solar()
    {
        // Retail 0626d230 is `JMP EqObj::~EqObj`: Solar's own members are all scalars.
    }

    bool Solar::read(INI_Reader& Reader)
    {
        // FUN_062f3470. No "inherit" case: LoadSolar handles that key before calling read(). The
        // three string keys read the whole value string, not the indexed one.
        if (EqObj::read(Reader))
            return true;

        if (Reader.is_value("toughness"))
        {
            Toughness = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("destructible"))
        {
            bDestructible = Reader.get_value_bool(0);
            return true;
        }

        if (Reader.is_value("open_anim"))
        {
            OpenAnim.Value = StringAlloc(Reader.get_value_string(), false);
            return true;
        }

        if (Reader.is_value("loadout"))
        {
            Loadout.Id = CreateID(Reader.get_value_string());
            return true;
        }

        if (Reader.is_value("jump_out_hp"))
        {
            JumpOutHp.Value = StringAlloc(Reader.get_value_string(), false);
            return true;
        }

        if (Reader.is_value("solar_radius"))
        {
            SolarRadius = Reader.get_value_float(0);
            return true;
        }

        return false;
    }

    void Solar::redefine(Root const& Other)
    {
        Root::redefine(Other);
        Solar const* Donor = (Solar const*)&Other;
        if (Donor->Toughness != Toughness)
            Toughness = Donor->Toughness;
    }

    AClassType Solar::get_class_type() const
    {
        // Retail 0x71740.
        return AClassSolar;
    }

    // .rdata:063a08ac (exported ?MAX_EXHAUST_NOZZLES@Ship@Archetype@@2HB): int 9.
    int const Ship::MAX_EXHAUST_NOZZLES = 9;

    Ship::Ship(ICliObj* InCompanion)
        : EqObj(InCompanion)
        , CommReferrable()
        , IdsInfo1(0)
        , IdsInfo2(0)
        , IdsInfo3(0)
        , ShipClass(0)
        , NumExhaustNozzles(9)
        , HoldSize(0.0f)
        , LinearDrag(100.0f)
        , AngularDragX(0.75f)
        , AngularDragY(0.75f)
        , AngularDragZ(0.75f)
        , SteeringTorqueX(2000.0f)
        , SteeringTorqueY(2000.0f)
        , SteeringTorqueZ(2000.0f)
        , NudgeForce(0.0f)
        , StrafeForce(0.0f)
        , StrafePowerUsage(0.0f)
        , MaxBankAngle(0.0f)
        , LegalHps()                        // retail zeroes the vector header at +0x130
        , NanobotLimit(-1)
        , ShieldBatteryLimit(-1)
    {
        BayDoorAnim.clear();
        HpBaySurface.clear();
        HpBayExternal.clear();
        // Defaults to "HpHeadlight", not empty.
        HpTractorSource.Value = StringAlloc("HpHeadlight", false);
    }

    Ship::~Ship()
    {
        // Retail 0xd000: +0x130 is a vector of 0x14-byte elements each owning an inner
        // vector<CacheString>; ~vector frees the same buffers. The pool strings are not freed
        // by retail either. Retail's apparent `MetaList<>::operator=(element+4, elem)` call over
        // each inner range sits where VC6 inlines vector<CacheString>::_Destroy and has no
        // observable effect (inferred from the shape, not read byte by byte).
        LegalHps.clear();
    }

    bool Ship::read(INI_Reader& Reader)
    {
        // FUN_062f21b0. No "inherit" case: LoadShips handles that key before calling read(). The
        // four CacheString keys read the whole value string; hp_type uses the indexed one.
        if (EqObj::read(Reader))
            return true;

        if (Reader.is_value("bay_door_anim"))
        {
            BayDoorAnim.Value = StringAlloc(Reader.get_value_string(), false);
            return true;
        }

        if (Reader.is_value("HP_bay_surface"))
        {
            HpBaySurface.Value = StringAlloc(Reader.get_value_string(), false);
            return true;
        }

        if (Reader.is_value("HP_bay_external"))
        {
            HpBayExternal.Value = StringAlloc(Reader.get_value_string(), false);
            return true;
        }

        if (Reader.is_value("HP_tractor_source"))
        {
            HpTractorSource.Value = StringAlloc(Reader.get_value_string(), false);
            return true;
        }

        if (Reader.is_value("num_exhaust_nozzles"))
        {
            NumExhaustNozzles = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("hold_size"))
        {
            HoldSize = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("linear_drag"))
        {
            LinearDrag = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("angular_drag"))
        {
            AngularDragX = Reader.get_value_float(0);
            AngularDragY = Reader.get_value_float(1);
            AngularDragZ = Reader.get_value_float(2);
            return true;
        }

        if (Reader.is_value("steering_torque"))
        {
            SteeringTorqueX = Reader.get_value_float(0);
            SteeringTorqueY = Reader.get_value_float(1);
            SteeringTorqueZ = Reader.get_value_float(2);
            return true;
        }

        if (Reader.is_value("nudge_force"))
        {
            NudgeForce = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("strafe_force"))
        {
            StrafeForce = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("strafe_power_usage"))
        {
            StrafePowerUsage = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("ids_info1"))
        {
            IdsInfo1 = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("ids_info2"))
        {
            IdsInfo2 = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("ids_info3"))
        {
            IdsInfo3 = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("ship_class"))
        {
            ShipClass = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("nanobot_limit"))
        {
            NanobotLimit = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("shield_battery_limit"))
        {
            ShieldBatteryLimit = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("max_bank_angle"))
        {
            // The value stays unrounded; clamped to 180.0f (DAT_063a15b4, unordered kept), times
            // DAT_0639d210, rounded once to float.
            double Degrees = Reader.value_num(0);
            if (Degrees > 180.0)
                Degrees = 180.0;
            MaxBankAngle = (float)(Degrees * (double)0.017453292f);
            return true;
        }

        // A virtual call on the CommReferrable subobject (+0x94).
        if (read_comm_ini_data(Reader))
            return true;

        // hp_type = <class name>, <hardpoint>, ... An unknown class (or value 0) swallows the
        // line; otherwise one LegalHpTypes entry with its hardpoint names.
        if (Reader.is_value("hp_type"))
        {
            unsigned Value = 0;
            if (!TryLookupHpType(Reader.get_value_string(0), &Value) || Value == 0)
                return true;
            HpAttachmentType const Type = (HpAttachmentType)Value;

            LegalHps.push_back(LegalHpTypes());
            LegalHpTypes& Entry = LegalHps.back();
            Entry.Type = Type;

            for (int Index = 1; !Reader.is_value_empty(Index); ++Index)
            {
                CacheString Hp;
                Hp.Value = StringAlloc(Reader.get_value_string(Index), false);
                Entry.Hardpoints.push_back(Hp);
            }
            return true;
        }

        return false;
    }

    // FUN_062f29a0. Returns the inner vector even when it is empty; only "no element of this
    // class" yields null. Server.dll's reason-3 cheat check depends on that distinction.
    std::vector<CacheString> const* Ship::get_legal_hps(HpAttachmentType Type) const
    {
        for (unsigned int Index = 0; Index < LegalHps.size(); ++Index)
        {
            if (LegalHps[Index].Type == Type)
                return &LegalHps[Index].Hardpoints;
        }
        return nullptr;
    }

    // Common.dll.c 0x927f0. Only copies HpBaySurface/HpBayExternal/HpTractorSource and the
    // physics scalars (hold_size .. max_bank_angle); BayDoorAnim/IdsInfo1-3/ShipClass/
    // NumExhaustNozzles/StrafePowerUsage are NOT touched by retail's redefine either.
    void Ship::redefine(Root const& Other)
    {
        Root::redefine(Other);
        Ship const* Donor = (Ship const*)&Other;
        if (Donor->HpBaySurface.Value != HpBaySurface.Value)
            HpBaySurface.Value = Donor->HpBaySurface.Value;
        if (Donor->HpBayExternal.Value != HpBayExternal.Value)
            HpBayExternal.Value = Donor->HpBayExternal.Value;
        if (Donor->HpTractorSource.Value != HpTractorSource.Value)
            HpTractorSource.Value = Donor->HpTractorSource.Value;
        if (Donor->HoldSize != HoldSize)
            HoldSize = Donor->HoldSize;
        if (Donor->LinearDrag != LinearDrag)
            LinearDrag = Donor->LinearDrag;
        if (Donor->AngularDragX != AngularDragX || Donor->AngularDragY != AngularDragY
            || Donor->AngularDragZ != AngularDragZ)
        {
            AngularDragX = Donor->AngularDragX;
            AngularDragY = Donor->AngularDragY;
            AngularDragZ = Donor->AngularDragZ;
        }
        if (Donor->SteeringTorqueX != SteeringTorqueX || Donor->SteeringTorqueY != SteeringTorqueY
            || Donor->SteeringTorqueZ != SteeringTorqueZ)
        {
            SteeringTorqueX = Donor->SteeringTorqueX;
            SteeringTorqueY = Donor->SteeringTorqueY;
            SteeringTorqueZ = Donor->SteeringTorqueZ;
        }
        if (Donor->NudgeForce != NudgeForce)
            NudgeForce = Donor->NudgeForce;
        if (Donor->StrafeForce != StrafeForce)
            StrafeForce = Donor->StrafeForce;
        if (Donor->MaxBankAngle != MaxBankAngle)
            MaxBankAngle = Donor->MaxBankAngle;
    }

    AClassType Ship::get_class_type() const
    {
        // Retail 0xc9a0.
        return AClassShip;
    }
}

// ===========================================================================
// Explicit STL instantiation exported like retail's. Retail Common.dll exports all 55
// members of its VC6 std::list<CollisionGroupDesc>; FreeLancer.exe, Server.dll and
// rpclocal.dll import them by name. The port exports the modern instantiation; the
// importing declaration is in common_archetype.h.
// ===========================================================================
template class COMMON_API std::list<CollisionGroupDesc>;
