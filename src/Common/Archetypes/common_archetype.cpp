// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

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

        // PTR_s_hp_gun_063eca40 (.data:063eca40..063ecc37, stride 8, 63 entries): name/value
        // pairs for HpAttachmentType 4..0x42. Munition miss -> 4 (hp_gun); Gun hp_gun_type
        // miss -> skip the push (not default 4).
        struct FHpTypeEntry
        {
            char const* Text;
            unsigned Value;
        };

        FHpTypeEntry const* HpTypeTable(unsigned* OutCount)
        {
            static FHpTypeEntry const kTable[] = {
                {"hp_gun", 4},
                {"hp_shield_generator", 5},
                {"hp_fighter_shield_generator", 6},
                {"hp_elite_shield_generator", 7},
                {"hp_freighter_shield_generator", 8},
                {"hp_thruster", 9},
                {"hp_torpedo", 0xa},
                {"hp_mine_dropper", 0xb},
                {"hp_turret", 0xd},
                {"hp_countermeasure_dropper", 0xc},
                {"hp_cargo_pod", 0xe},
                {"hp_gun_special_1", 0xf},
                {"hp_gun_special_2", 0x10},
                {"hp_gun_special_3", 0x11},
                {"hp_gun_special_4", 0x12},
                {"hp_gun_special_5", 0x13},
                {"hp_gun_special_6", 0x14},
                {"hp_gun_special_7", 0x15},
                {"hp_gun_special_8", 0x16},
                {"hp_gun_special_9", 0x17},
                {"hp_gun_special_10", 0x18},
                {"hp_turret_special_1", 0x19},
                {"hp_turret_special_2", 0x1a},
                {"hp_turret_special_3", 0x1b},
                {"hp_turret_special_4", 0x1c},
                {"hp_turret_special_5", 0x1d},
                {"hp_turret_special_6", 0x1e},
                {"hp_turret_special_7", 0x1f},
                {"hp_turret_special_8", 0x20},
                {"hp_turret_special_9", 0x21},
                {"hp_turret_special_10", 0x22},
                {"hp_torpedo_special_1", 0x24},
                {"hp_torpedo_special_2", 0x23},
                {"hp_fighter_shield_special_1", 0x25},
                {"hp_fighter_shield_special_2", 0x26},
                {"hp_fighter_shield_special_3", 0x27},
                {"hp_fighter_shield_special_4", 0x28},
                {"hp_fighter_shield_special_5", 0x29},
                {"hp_fighter_shield_special_6", 0x2a},
                {"hp_fighter_shield_special_7", 0x2b},
                {"hp_fighter_shield_special_8", 0x2c},
                {"hp_fighter_shield_special_9", 0x2d},
                {"hp_fighter_shield_special_10", 0x2e},
                {"hp_elite_shield_special_1", 0x2f},
                {"hp_elite_shield_special_2", 0x30},
                {"hp_elite_shield_special_3", 0x31},
                {"hp_elite_shield_special_4", 0x32},
                {"hp_elite_shield_special_5", 0x33},
                {"hp_elite_shield_special_6", 0x34},
                {"hp_elite_shield_special_7", 0x35},
                {"hp_elite_shield_special_8", 0x36},
                {"hp_elite_shield_special_9", 0x37},
                {"hp_elite_shield_special_10", 0x38},
                {"hp_freighter_shield_special_1", 0x39},
                {"hp_freighter_shield_special_2", 0x3a},
                {"hp_freighter_shield_special_3", 0x3b},
                {"hp_freighter_shield_special_4", 0x3c},
                {"hp_freighter_shield_special_5", 0x3d},
                {"hp_freighter_shield_special_6", 0x3e},
                {"hp_freighter_shield_special_7", 0x3f},
                {"hp_freighter_shield_special_8", 0x40},
                {"hp_freighter_shield_special_9", 0x41},
                {"hp_freighter_shield_special_10", 0x42},
            };
            if (OutCount != nullptr)
                *OutCount = sizeof(kTable) / sizeof(kTable[0]);
            return kTable;
        }
    }

    // Helpers shared with the other Archetype TUs (common_archetype_ships / _equipment /
    // _weapons / _misc.cpp); declared in common_archetype_internal.h.
    namespace CmnArchetype
    {
        Equipment* LookupLootAppearance(char const* Name)
        {
            ID_String Key;
            Key.Id = CreateID(Name);
            return GetEquipmentByName(Key);
        }

        // The linear walk every hp-type key inlines (Ship::read, Gun::read, Munition::read):
        // DACOM.DLL stricmp against each name, first match wins.
        bool TryLookupHpType(char const* Name, unsigned* OutValue)
        {
            unsigned Count = 0;
            FHpTypeEntry const* Table = HpTypeTable(&Count);
            unsigned Index = 0;
            for (; Index < Count; ++Index)
            {
                if (CallDacomStricmp(Name, Table[Index].Text) == 0)
                {
                    *OutValue = Table[Index].Value;
                    return true;
                }
            }
            return false;
        }

        bool ReadLootAppearance(INI_Reader& Reader, Equipment*& Out)
        {
            if (!Reader.is_value("loot_appearance"))
                return false;
            Out = LookupLootAppearance(Reader.get_value_string());
            return true;
        }

        // The hp-type arrays of ShieldGenerator (+0xa8) and Gun (+0xbc) are retail VC6
        // vector<unsigned>s {allocator byte, first, last, end}; the port keeps the three
        // pointers as a malloc'd block (the owners' destructors free it).

        // The VC6 vector copy constructor (inlined in the ShieldGenerator and Gun copy ctors):
        // a block of exactly size() elements -- an empty source still gets a block.
        void CopyConstructHpTypes(unsigned*& Begin, unsigned*& End, unsigned*& Cap,
                                  unsigned const* SrcBegin, unsigned const* SrcEnd)
        {
            unsigned const Count = SrcBegin == nullptr ? 0u : (unsigned)(SrcEnd - SrcBegin);
            Begin = (unsigned*)malloc(Count * sizeof(unsigned));
            unsigned Index = 0;
            for (; Index < Count; ++Index)
                Begin[Index] = SrcBegin[Index];
            End = Begin + Count;
            Cap = End;
        }

        // The VC6 vector operator= (inlined in ShieldGenerator / Gun operator=); the caller skips
        // it for the same vector. Within capacity the elements are overwritten in place (retail's
        // two in-place cases leave the same state); otherwise a block of exactly n is allocated.
        void AssignHpTypes(unsigned*& Begin, unsigned*& End, unsigned*& Cap,
                           unsigned const* SrcBegin, unsigned const* SrcEnd)
        {
            unsigned const Count = SrcBegin == nullptr ? 0u : (unsigned)(SrcEnd - SrcBegin);
            unsigned const Capacity = Begin == nullptr ? 0u : (unsigned)(Cap - Begin);
            if (Count > Capacity)
            {
                free(Begin);
                Begin = (unsigned*)malloc(Count * sizeof(unsigned));
                Cap = Begin + Count;
            }
            unsigned Index = 0;
            for (; Index < Count; ++Index)
                Begin[Index] = SrcBegin[Index];
            End = Begin + Count;
        }

        // Slot 0/1, for Equipment::InheritFrom's companion AddRef/Inherit/Release sequence (the
        // paired calls FUN_062f3ea0 makes around its CliObjInherit call).
        void CliObjAddRef(ICliObj* Companion)
        {
            if (Companion == nullptr)
                return;

            Companion->AddRef();
        }

        void CliObjRelease(ICliObj* Companion)
        {
            if (Companion == nullptr)
                return;

            Companion->Release();
        }
    }

    std::map<unsigned, unsigned short> GLargeToSmall;
    std::vector<unsigned> GSmallToLarge;

    void RegisterLargeId(unsigned LargeId)
    {
        std::map<unsigned, unsigned short>::iterator Existing = GLargeToSmall.find(LargeId);
        if (Existing != GLargeToSmall.end())
            return;

        GSmallToLarge.push_back(LargeId);
        unsigned short SmallId = (unsigned short)GSmallToLarge.size();
        GLargeToSmall.insert(std::pair<unsigned, unsigned short>(LargeId, SmallId));
    }

    // ArchDB::Insert's zero-id arm (FUN_062fbeb0 / FUN_062fbbe0).
    void ArchDBDumpNoNickname(char const* KindName)
    {
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Loaded '%s' has no nickname .. not adding to database.\n",
              ArchetypeSource, 0x84, KindName);
    }

    // ArchDB::Get(unsigned)'s miss dump.
    void ArchDBDumpMiss(unsigned NicknameId)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: ArchDB::Get(%u) failed",
              ArchetypeSource, 0x3c, NicknameId);
    }

    // ArchDB::Get(ID_String const&)'s miss dump.
    void ArchDBDumpMiss(ID_String const& Name)
    {
        FDUMP(0x100001, "%s(%d) : *** ERROR: ArchDB::Get(%s) failed. ID=%d",
              ArchetypeSource, 0x49, Name.get_string(), Name.Id);
    }

    void ClearAllArchDBs()
    {
        GSimpleDb.Clear();
        GShipDb.Clear();
        GSolarDb.Clear();
        GEquipmentDb.Clear();
        GAsteroidDb.Clear();
        GDynamicAsteroidDb.Clear();
        GMotorDb.Clear();
        GExplosionDb.clear();
    }

    ArchDB<Root> GSimpleDb(true);
    ArchDB<Ship> GShipDb(true);
    ArchDB<Solar> GSolarDb(true);
    ArchDB<Equipment> GEquipmentDb(true);  // FUN_062fbbe0 registers small ids too
    ArchDB<Asteroid> GAsteroidDb(true);
    ArchDB<DynamicAsteroid> GDynamicAsteroidDb(true);
    ArchDB<MotorData> GMotorDb(true);

    unsigned short __cdecl LargeIDToSmallID(unsigned LargeId)
    {
        std::map<unsigned, unsigned short>::const_iterator Found = GLargeToSmall.find(LargeId);
        if (Found == GLargeToSmall.end())
            return 0;
        return Found->second;
    }

    unsigned __cdecl SmallIDToLargeID(unsigned short SmallId)
    {
        if (SmallId == 0)
            return 0;

        unsigned Count = (unsigned)GSmallToLarge.size();
        if (SmallId > Count)
            return 0;

        return GSmallToLarge[SmallId - 1];
    }

    namespace
    {
        struct FObjectTypeEntry
        {
            char const* Name;
            unsigned Bits;
        };

        // PTR_DAT_063ec810, 29 entries, stride 8, loop bound 0xe8 (DecodeType 0x8f3d0).
        FObjectTypeEntry const GObjectTypes[] = {
            { "NONE", 0 },
            { "MOON", 1 },
            { "PLANET", 2 },
            { "SUN", 4 },
            { "ASTEROID", 0x20000000 },
            { "BLACKHOLE", 8 },
            { "SATELLITE", 0x10 },
            { "DOCKING_RING", 0x20 },
            { "JUMP_GATE", 0x40 },
            { "JUMP_HOLE", 0x800 },
            { "WEAPONS_PLATFORM", 0x1000 },
            { "DESTROYABLE_DEPOT", 0x2000 },
            { "NON_TARGETABLE", 0x4000 },
            { "MISSION_SATELLITE", 0x8000 },
            { "AIRLOCK_GATE", 0x400 },
            { "TRADELANE_RING", 0x80 },
            { "STATION", 0x100 },
            { "WAYPOINT", 0x200 },
            { "FIGHTER", 0x10000 },
            { "FREIGHTER", 0x20000 },
            { "GUNBOAT", 0x40000 },
            { "CRUISER", 0x80000 },
            { "TRANSPORT", 0x100000 },
            { "CAPITAL", 0x200000 },
            { "MINING", 0x400000 },
            { "GUIDED", 0x1000000 },
            { "MINE", 0x4000000 },
            { "LOOT", 0x10000000 },
            { "BULLET", 0x2000000 },
        };

        bool CliObjReadUnknown(ICliObj* Companion, INI_Reader& Reader)
        {
            if (Companion == nullptr)
                return false;

            return Companion->ReadUnknown(&Reader);
        }

        void CliObjRedefineCompanion(ICliObj* Companion, ICliObj* Donor)
        {
            if (Companion == nullptr)
                return;

            Companion->Redefine(Donor);
        }
    }

}  // namespace Archetype

// .text:062ef3d0, ?DecodeType@@YAIPBD@Z -- a global export. No match warns and answers 0; no
// null test of Name in retail. Keys Collision::Load's damage-by-object-type map
// (common_physys.cpp) and the archetype `type =` handler.
unsigned __cdecl DecodeType(char const* Name)
{
    unsigned Count = sizeof(Archetype::GObjectTypes) / sizeof(Archetype::GObjectTypes[0]);
    for (unsigned Index = 0; Index < Count; ++Index)
    {
        if (Archetype::GObjectTypes[Index].Name == nullptr)
            continue;
        // DACOM.DLL stricmp, not the CRT one.
        if (CallDacomStricmp(Name, Archetype::GObjectTypes[Index].Name) == 0)
            return Archetype::GObjectTypes[Index].Bits;
    }

    FDUMP(0x100002, "%s(%d) : *** WARNING: unknown OBJECT_TYPE = '%s'\n",
          Archetype::ArchetypeSource, 0xe1, Name);
    return 0;
}

namespace Archetype
{

    void CliObjLod(ICliObj* Companion, INI_Reader* Reader)
    {
        if (Companion == nullptr || Reader == nullptr)
            return;

        Companion->Lod(Reader);
    }

    void CliObjBind(ICliObj* Companion, Root* Record)
    {
        if (Companion == nullptr || Record == nullptr)
            return;

        Companion->Bind(Record);
    }

    void CliObjFreeResources(ICliObj* Companion)
    {
        if (Companion == nullptr)
            return;

        Companion->FreeResources();
    }

    void CliObjInherit(ICliObj* Companion, ICliObj* Donor)
    {
        if (Companion == nullptr)
            return;

        Companion->Inherit(Donor);
    }

    unsigned long CliObjLoad(ICliObj* Companion, int LoadMode)
    {
        if (Companion == nullptr)
            return 0;

        return Companion->Load(LoadMode);
    }

    Root::Root(ICliObj* InCompanion)
        : Companion(InCompanion)
        , ObjectType(0)
        , IdsName(0)
        , IdsInfo(0)
        , HitPts(100.0f)
        , Mass(1.0f)
        , ExplosionResistance(1.0f)
        , RotationInertiaX(0.0f)
        , RotationInertiaY(0.0f)
        , RotationInertiaZ(0.0f)
        , bHasRotationInertia(false)
        , bPhantomPhysics(false)
        , LoadCount(0)
        , EngineIndex(-1)
        , AnimIndex(-1)
    {
        Nickname.clear();
        DaArchetype.clear();
        ExplosionArch.clear();
    }

    // FUN_062f01f0: the three string members cleared, an empty surfaces map, then
    // operator=(Other). Deviation: retail never initialises Companion (+0x04), so operator='s
    // Release of the old companion runs on garbage; the port starts it at null. The only
    // callers are the leaf copy constructors, reachable only through the export table.
    Root::Root(Root const& Other)
    {
        Companion = nullptr;
        Nickname.clear();
        DaArchetype.clear();
        ExplosionArch.clear();
        operator=(Other);
    }

    Root::~Root()
    {
        if (Companion != nullptr)
        {
            Companion->Release();
            Companion = nullptr;
        }
    }

    bool Root::read(INI_Reader& Reader)
    {
        if (Reader.is_value("nickname"))
        {
            Nickname.Id = CreateID(Reader.get_value_string(0));
            return true;
        }

        if (Reader.is_value("DA_archetype") || Reader.is_value("attachment_archetype"))
        {
            DaArchetype.Value = StringAlloc(Reader.get_value_string(0), true);
            return true;
        }

        if (Reader.is_value("type"))
        {
            ObjectType = ::DecodeType(Reader.get_value_string(0));
            return true;
        }

        if (Reader.is_value("ids_name"))
        {
            IdsName = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("ids_info"))
        {
            IdsInfo = Reader.get_value_int(0);
            return true;
        }

        if (Reader.is_value("explosion_arch"))
        {
            ExplosionArch.Id = CreateID(Reader.get_value_string(0));
            return true;
        }

        if (Reader.is_value("explosion_resistance"))
        {
            ExplosionResistance = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("hit_pts"))
        {
            HitPts = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("mass"))
        {
            Mass = Reader.get_value_float(0);
            return true;
        }

        if (Reader.is_value("rotation_inertia"))
        {
            RotationInertiaX = Reader.get_value_float(0);
            RotationInertiaY = Reader.get_value_float(1);
            RotationInertiaZ = Reader.get_value_float(2);
            bHasRotationInertia = true;
            return true;
        }

        if (Reader.is_value("phantom_physics"))
        {
            bPhantomPhysics = Reader.get_value_bool(0);
            return true;
        }

        if (Reader.is_value("mission_property"))
            return true;

        return CliObjReadUnknown(Companion, Reader);
    }

    void Root::redefine(Root const& Other)
    {
        CliObjRedefineCompanion(Companion, Other.Companion);

        if (Other.IdsName != IdsName)
            IdsName = Other.IdsName;
        if (Other.IdsInfo != IdsInfo)
            IdsInfo = Other.IdsInfo;
        if (Other.HitPts != HitPts)
            HitPts = Other.HitPts;
        if (Other.ExplosionArch.Id != ExplosionArch.Id)
            ExplosionArch.Id = Other.ExplosionArch.Id;
        if (Other.ExplosionResistance != ExplosionResistance)
            ExplosionResistance = Other.ExplosionResistance;
        if (Other.Mass != Mass)
            Mass = Other.Mass;
        if (Other.RotationInertiaX != RotationInertiaX
            || Other.RotationInertiaY != RotationInertiaY
            || Other.RotationInertiaZ != RotationInertiaZ)
        {
            RotationInertiaX = Other.RotationInertiaX;
            RotationInertiaY = Other.RotationInertiaY;
            RotationInertiaZ = Other.RotationInertiaZ;
        }
    }

    // Reachable from LoadShips/LoadSolar's "inherit=" donor copy via EqObj::operator=. No
    // self-assignment guard in retail; every step is self-assign-safe. Companion is released,
    // assigned and AddRef'd; PhyParts is retail's inlined map operator=; EngineIndex/AnimIndex
    // (+0x40/+0x44) are copied as plain ints (shared lookup indices, not owned handles); the
    // rest is a raw copy.
    Root const& Root::operator=(Root const& Other)
    {
        CliObjRelease(Companion);
        Companion = Other.Companion;
        CliObjAddRef(Companion);

        PhyParts = Other.PhyParts;

        Nickname = Other.Nickname;
        DaArchetype = Other.DaArchetype;
        ObjectType = Other.ObjectType;
        IdsName = Other.IdsName;
        IdsInfo = Other.IdsInfo;
        HitPts = Other.HitPts;
        Mass = Other.Mass;
        ExplosionArch = Other.ExplosionArch;
        ExplosionResistance = Other.ExplosionResistance;
        RotationInertiaX = Other.RotationInertiaX;
        RotationInertiaY = Other.RotationInertiaY;
        RotationInertiaZ = Other.RotationInertiaZ;
        bHasRotationInertia = Other.bHasRotationInertia;
        bPhantomPhysics = Other.bPhantomPhysics;
        LoadCount = Other.LoadCount;
        EngineIndex = Other.EngineIndex;
        AnimIndex = Other.AnimIndex;

        return *this;
    }

    bool __cdecl Load(IFileSystem* Filesys, long* EngineIndex, int* AnimIndex)
    {
        // FUN_062efd80. No test of Filesys or of DALib::Engine; DALib::Anim and both out
        // pointers are tested.
        IAnimation2* Anim = DALib::Anim;
        if (Anim != nullptr && AnimIndex != nullptr && *AnimIndex == -1)
        {
            *AnimIndex = (int)(ptrdiff_t)Anim->OpenScript(Filesys);
        }

        if (EngineIndex != nullptr)
        {
            // The file system's own path, named with its CRC.
            char PathBuf[0x100];
            Filesys->GetFileSysPath(PathBuf, 0x100);
            FEngineArchetypeName Name;
            Name.Text = PathBuf;
            Name.Crc = DACOM_CRC::GetCRC32(PathBuf);
            *EngineIndex = (long)DALib::Engine->FindArchetype(&Name);
            if (*EngineIndex == -1)
            {
                // Retail takes the CRC again.
                Name.Text = PathBuf;
                Name.Crc = DACOM_CRC::GetCRC32(PathBuf);
                *EngineIndex = (long)DALib::Engine->CreateArchetypeHandle(&Name, Filesys);
            }
        }
        return true;
    }

    bool __cdecl Load(char const* Path, long* EngineIndex, int* AnimIndex)
    {
        // FUN_062efe40. No test of Path.
        if (EngineIndex != nullptr && AnimIndex == nullptr)
        {
            // An engine-only request first tries the already-loaded archetype by its path.
            FEngineArchetypeName Name;
            Name.Text = Path;
            Name.Crc = DACOM_CRC::GetCRC32(Path);
            *EngineIndex = (long)DALib::Engine->FindArchetype(&Name);
            if (*EngineIndex != -1)
                return true;
        }

        // A 0x34-byte "FileSystem" descriptor with no implementation name: GENERIC_READ,
        // FILE_SHARE_READ, OPEN_EXISTING, flags 0x8000080.
        FFileSystemDesc Desc;
        memset(&Desc, 0, sizeof(Desc));
        Desc.Size = 0x34;
        Desc.ComponentName = "FileSystem";
        Desc.Path = Path;
        Desc.Access = 0x80000000;
        Desc.ShareMode = 1;
        Desc.Creation = 3;
        Desc.Flags = 0x8000080;
        Desc.FindHandle = 0xFFFFFFFF;

        IFileSystem* Filesys = nullptr;
        if (DALib::Data->CreateInstance((AGGDESC*)&Desc, (void**)&Filesys) != 0)
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: failed to open archetype (%s) file system\n",
                  ArchetypeSource, 0x1c3, Path);
            return false;
        }

        // Retail inlines Load(IFileSystem*, ...) here.
        Load(Filesys, EngineIndex, AnimIndex);

        Filesys->Release();
        return true;
    }

    bool Root::load()
    {
        bool bLoaded = false;
        if (EngineIndex == -1)
        {
            const char* Path = DaArchetype.Value;
            if (Path[0] != '\0')
            {
                bLoaded = Load(Path, &EngineIndex, &AnimIndex);
                if (!bLoaded)
                {
                    FDUMP(0x100002, "%s(%d) : *** WARNING: missing Archetype file %s\n",
                          ArchetypeSource, 0x2b0, Path);
                    LoadCount += 1;
                    return false;
                }
                if (EngineIndex != -1)
                {
                    char Drive[4];
                    char Dir[260];
                    char Name[256];
                    char Ext[256];
                    char SurPath[260];
                    Drive[0] = '\0';
                    Dir[0] = '\0';
                    Name[0] = '\0';
                    Ext[0] = '\0';
                    SurPath[0] = '\0';
                    _splitpath(Path, Drive, Dir, Name, Ext);
                    // DACOM.DLL stricmp, not the CRT one.
                    if (CallDacomStricmp(Ext, ".sph") != 0)
                    {
                        _makepath(SurPath, Drive, Dir, Name, "sur");
                        PhyArch::LoadSurfaces(SurPath, PhyParts);
                    }
                }
                bLoaded = true;
            }
        }

        LoadCount += 1;
        return bLoaded;
    }

    // Retail 0x90be0. Unlike load(), does not call the engine mesh loader (Load()); only
    // (re)loads the .sur surfaces map for an already-loaded mesh.
    bool Root::init_physical_representation()
    {
        if (EngineIndex == -1)
            return false;

        char const* Path = DaArchetype.Value;
        char Drive[4] = {0};
        char Dir[260] = {0};
        char Name[256] = {0};
        char Ext[256] = {0};
        char SurPath[260] = {0};
        _splitpath(Path, Drive, Dir, Name, Ext);
        // DACOM.DLL stricmp, not the CRT one.
        if (CallDacomStricmp(Ext, ".sph") == 0)
            return true;

        _makepath(SurPath, Drive, Dir, Name, "sur");
        return PhyArch::LoadSurfaces(SurPath, PhyParts);
    }

    // Retail 0x90c90. Frees the +0x48 surfaces map only -- does not touch EngineIndex/
    // AnimIndex/Companion (unlike free_resources()).
    void Root::free_physical_representation()
    {
        PhyParts.clear();
    }

    AClassType Root::get_class_type() const
    {
        return AClassRoot;
    }

    bool Root::is_loaded() const
    {
        return EngineIndex != -1;
    }

    PhyArch::Part const* Root::get_phy_part(unsigned PartId) const
    {
        // Retail 0x90d70.
        std::map<unsigned int, PhyArch::Part>::const_iterator It = PhyParts.find(PartId);
        if (It == PhyParts.end())
            return nullptr;
        return &It->second;
    }

    long Root::create_instance(IEngineInstance* EngineInstance) const
    {
        // FUN_062f05d0. No engine or handle test.
        if (EngineIndex != -1)
        {
            return (long)DALib::Engine->CreateInstance((void*)EngineIndex, EngineInstance);
        }

        // The CacheString always holds a pointer (clear() hands out the pool's empty string);
        // retail reads its first byte directly.
        char const* Path = DaArchetype.Value;
        if (Path[0] == '\0')
        {
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: Cannot create_instance(). Archetype (%s) has no file assigned. NOT IGNORABLE!\n",
                  ArchetypeSource, 0x218, Nickname.get_string());
            return -1;
        }
        FDUMP(0x100001,
              "%s(%d) : *** ERROR: Cannot create_instance(). Archetype (%s) file isn't loaded/valid: %s.  NOT IGNORABLE!\n",
              ArchetypeSource, 0x21e, Nickname.get_string(), Path);
        return -1;
    }

    void Root::AddRef()
    {
        ++LoadCount;
    }

    int Root::GetRef() const
    {
        return LoadCount;
    }

    int Root::Release()
    {
        // FUN_062f0d50: returns the decremented count.
        int const Count = LoadCount - 1;
        LoadCount = Count;
        if (Count == 0)
            free_resources();
        return Count;
    }

    void Root::free_resources()
    {
        // FUN_062f0af0. DALib::Anim itself is not tested.
        if (AnimIndex != -1)
        {
            DALib::Anim->CloseScript((void*)(ptrdiff_t)AnimIndex);
            AnimIndex = -1;
        }

        if (EngineIndex != -1)
            DALib::Engine->ReleaseHandle((void*)EngineIndex);

        PhyParts.clear();

        // The mesh index is reset unconditionally.
        EngineIndex = -1;

        CliObjFreeResources(Companion);
    }

    // Typed Get wrappers — FDUMP on miss via ArchDB::Get.
    Ship* __cdecl GetShip(unsigned NicknameId)
    {
        return GShipDb.Get(NicknameId);
    }

    Ship* __cdecl GetShipByName(ID_String const& Name)
    {
        return GShipDb.Get(Name);
    }

    Solar* __cdecl GetSolar(unsigned NicknameId)
    {
        return GSolarDb.Get(NicknameId);
    }

    Solar* __cdecl GetSolarByName(ID_String const& Name)
    {
        return GSolarDb.Get(Name);
    }

    Equipment* __cdecl GetEquipment(unsigned NicknameId)
    {
        return GEquipmentDb.Get(NicknameId);
    }

    Equipment* __cdecl GetEquipmentByName(ID_String const& Name)
    {
        return GEquipmentDb.Get(Name);
    }

    Root* __cdecl GetSimple(unsigned NicknameId)
    {
        return GSimpleDb.Get(NicknameId);
    }

    Root* __cdecl GetSimpleByName(ID_String const& Name)
    {
        return GSimpleDb.Get(Name);
    }

    Asteroid* __cdecl GetAsteroid(unsigned NicknameId)
    {
        return GAsteroidDb.Get(NicknameId);
    }

    Asteroid* __cdecl GetAsteroidByName(ID_String const& Name)
    {
        return GAsteroidDb.Get(Name);
    }

    DynamicAsteroid* __cdecl GetDynamicAsteroid(unsigned NicknameId)
    {
        return GDynamicAsteroidDb.Get(NicknameId);
    }

    DynamicAsteroid* __cdecl GetDynamicAsteroidByName(ID_String const& Name)
    {
        return GDynamicAsteroidDb.Get(Name);
    }

    MotorData const* __cdecl GetMotor(unsigned NicknameId)
    {
        return GMotorDb.Get(NicknameId);
    }

    Explosion* __cdecl GetExplosion(ID_String const& Name)
    {
        // Retail 0630bd10: map find on DAT_063fcf3c; a miss returns null silently, a hit the
        // address of the Explosion held in the node.
        std::map<unsigned, Explosion>::iterator Found = GExplosionDb.find(Name.Id);
        if (Found == GExplosionDb.end())
            return nullptr;
        return &Found->second;
    }

    // Enum* (062f3310 Ships, 062f3dc0 Solars, 062f9690 Equipment, 062f12e0 Simples,
    // 062fa8a0 Asteroids, 062fa910 DynamicAsteroids): one body each, differing only in the map.
    void __cdecl EnumShips(ArchEnumerator* Enumerator)
    {
        GShipDb.Enumerate(Enumerator);
    }

    void __cdecl EnumSolars(ArchEnumerator* Enumerator)
    {
        GSolarDb.Enumerate(Enumerator);
    }

    void __cdecl EnumEquipment(ArchEnumerator* Enumerator)
    {
        GEquipmentDb.Enumerate(Enumerator);
    }

    void __cdecl EnumSimples(ArchEnumerator* Enumerator)
    {
        GSimpleDb.Enumerate(Enumerator);
    }

    void __cdecl EnumAsteroids(ArchEnumerator* Enumerator)
    {
        GAsteroidDb.Enumerate(Enumerator);
    }

    void __cdecl EnumDynamicAsteroids(ArchEnumerator* Enumerator)
    {
        GDynamicAsteroidDb.Enumerate(Enumerator);
    }

    // Archetype::GetId 062efba0: jump table on ClassType (cases 0,2,3,4,5,6). Cases 0/2/3/4/6
    // inline the FUN_062fb890 walk on their map (Simple 063fcae8, Solar 063fcaac, Ship 063fcac0,
    // Asteroid 063fca98, Equipment 063fcad4); case 5 calls 062fb890 on DynamicAsteroid 063fca84.
    // Case 1 and anything above 6 return 0xFFFFFFFF (062efd5b OR EAX,-1).
    unsigned __cdecl GetId(AClassType ClassType, unsigned Index)
    {
        switch (ClassType)
        {
        case AClassRoot:             return GSimpleDb.KeyAt(Index);
        case AClassSolar:            return GSolarDb.KeyAt(Index);
        case AClassShip:             return GShipDb.KeyAt(Index);
        case AClassAsteroid:         return GAsteroidDb.KeyAt(Index);
        case AClassDynamicAsteroid:  return GDynamicAsteroidDb.KeyAt(Index);
        case AClassEquipment:        return GEquipmentDb.KeyAt(Index);
        default:                     return 0xFFFFFFFFu;
        }
    }

    // Archetype::GetIdCount 062efb40: the map size [map+0x10] for cases 0/2/3/4/5/6 (Simple
    // 063fcaf8, Solar 063fcabc, Ship 063fcad0, Asteroid 063fcaa8, DynamicAsteroid 063fca94,
    // Equipment 063fcae4); case 1 and anything above 6 return 0 (062efb74 XOR EAX,EAX).
    unsigned __cdecl GetIdCount(AClassType ClassType)
    {
        switch (ClassType)
        {
        case AClassRoot:             return GSimpleDb.Size();
        case AClassSolar:            return GSolarDb.Size();
        case AClassShip:             return GShipDb.Size();
        case AClassAsteroid:         return GAsteroidDb.Size();
        case AClassDynamicAsteroid:  return GDynamicAsteroidDb.Size();
        case AClassEquipment:        return GEquipmentDb.Size();
        default:                     return 0;
        }
    }

    int __cdecl GetArchMaxHitPts(unsigned LargeId)
    {
        // Common.dll.c 0x99570 (raw asm authoritative -- ftol confirms this
        // truncates the float HitPts to int, not the float-returning signature
        // the .c pseudocode's misleading "int" cast implied at first glance).
        // Retail checks exactly three DBs in this order -- Ship, Solar,
        // Equipment (NOT Asteroid/Simple/DynamicAsteroid) -- each via
        // ArchDB::Get (which FDUMPs an error on its own miss) and falls
        // through to the next DB rather than returning nullptr, and defaults
        // to 1 (not 0) if all three miss or resolve to a null record.
        Root* Record = GShipDb.Get(LargeId);
        if (Record == nullptr)
            Record = GSolarDb.Get(LargeId);
        if (Record == nullptr)
            Record = GEquipmentDb.Get(LargeId);

        return Record != nullptr ? (int)Record->HitPts : 1;
    }
}

// (ArchetypeLoadFromPath, a client-only port helper that used to be exported from here, now
// lives in freelancer.exe: src/freelancer/Client/client_mesh_load.cpp. Retail Common.dll
// exports no such function.)
