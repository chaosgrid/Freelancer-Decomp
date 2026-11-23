// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_universe.h"
#include "Universe/common_universe_internal.h"

#include "common_archetype.h"
#include "common_cobjdb.h"
#include "common_csys.h"
#include "common_id.h"
#include "common_ini.h"
#include "common_misc.h" // Geometry::Sphere / Frustum, SphereCull
#include "common_net.h"  // CommReferrable (SYSTEM's second base)
#include "common_reputation.h" // Reputation::Vibe static store (FreeStatic / Begin / EndStaticLoad)
#include "common_zone.h"   // ZONE, the concrete IZone

#include "DALib.h"
#include "dacom.h"
#include "readfile.h"

#include <list>
#include <map>
#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <vector>

extern double DaysPerSecond;

using CmnUniverse::BASE;
using CmnUniverse::GSystemObjectPool;
using CmnUniverse::GSystemObjectPoolOwner;
using CmnUniverse::LoadSystemObjectPool;
using CmnUniverse::SYSTEM;

namespace
{
    const char kSystemsSource[] = "E:\\FL\\Scratch\\Source\\Common\\Systems.cpp";
    const unsigned long kPathChars = 0x104;
    const unsigned kValidIdMask = 0x3fffffff;

    int GUniverseRefCount = 0;
    IFileSystem* GUniverseDirFs = nullptr;


    // The universe object DAT_0658a828: the system map at +0x00, the base map at +0x14, the
    // zone map at +0x28 and the gate map at +0x3c. Retail keeps the records by value in the
    // map nodes, copied from a stack record through a {id, record} pair (FUN_06337250).
    std::map<unsigned, SYSTEM> GSystems;
    std::map<unsigned, BASE> GBases;
    std::map<unsigned, ZONE> GZones;
    std::map<unsigned, unsigned> GGateSystems;

    // DAT_0658a824 / DAT_0658a820: the GetFirst/GetNext walks. Retail keeps a bare node
    // pointer that starts as 0 and is written only by those four functions, so a GetNext
    // before any GetFirst dereferences it; a value-initialised iterator is the same null node.
    std::map<unsigned, SYSTEM>::iterator GSystemWalk;
    std::map<unsigned, BASE>::iterator GBaseWalk;
}

namespace CmnUniverse
{
    // The shared [Object] record pool of ISystem slots 15/16: DAT_0658a994..DAT_0658a9a0, a
    // VC6 vector of 0x118-byte records. Its static constructor LAB_06336330 and atexit
    // destructor LAB_06336360 are this std::vector's own construction and destruction.
    std::vector<Universe::FSystemObject> GSystemObjectPool;
    // DAT_0658a9b8: the SYSTEM whose file the pool currently holds. Retail never resets it.
    SYSTEM const* GSystemObjectPoolOwner = nullptr;
}

namespace
{
    // Stack slots of FUN_06337250 that outlive one INI section, because retail never
    // re-initialises them per section: the stack SYSTEM's position (+0x74; a [System] without
    // pos / real_pos keeps the previous block's) and the [Object] nickname (an object without a
    // nickname key uses the previous object's). Their first values are whatever the stack held
    // (undefined behaviour in retail); the port starts both at zero.
    struct FUniverseLoadCarry
    {
        Vector SystemPos;
        unsigned ObjectNickname;
    };

    void LoadSystemFile(SYSTEM& System, char const* RelFile, FUniverseLoadCarry& Carry);

    void ReleaseUniverseDirFs()
    {
        if (GUniverseDirFs == nullptr)
            return;

        GUniverseDirFs->Release(); // +0x08
        GUniverseDirFs = nullptr;
    }

    // The four erases Startup, FUN_06337250 and Shutdown make, in this order. Strings are
    // StringCache pointers and are never freed. Port: the walk iterators are reset too; retail
    // leaves them pointing into the erased nodes (undefined).
    void ClearUniverseTables()
    {
        GSystemWalk = GSystems.end();
        GBaseWalk = GBases.end();
        GSystems.clear();
        GBases.clear();
        GZones.clear();
        GGateSystems.clear();
    }

    // Inlined in Startup: a DOS file system rooted at the universe directory. DALib::Dacom is
    // not null-checked. The Desc layout is asserted in readfile.h.
    void CreateUniverseDirFs(char const* Directory)
    {
        FFileSystemDesc Desc;
        memset(&Desc, 0, sizeof(Desc));
        Desc.Size = 0x34;
        Desc.ComponentName = "FileSystem";
        Desc.Implementation = "DOS";
        Desc.Path = Directory;
        Desc.Access = 0x80000000;
        Desc.ShareMode = 1;
        Desc.Creation = 3;
        Desc.Flags = 0x8000080;
        Desc.FindHandle = 0xFFFFFFFF;

        void* Object = nullptr;
        GENRESULT Result = DALib::Dacom->CreateInstance((AGGDESC*)&Desc, &Object);
        GUniverseDirFs = (Result == GR_OK) ? (IFileSystem*)Object : nullptr;
    }

    bool IdIsValid(unsigned NicknameId)
    {
        return (NicknameId & kValidIdMask) != 0;
    }

    void ReadTimeBlock(INI_Reader& Reader)
    {
        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("seconds_per_day"))
            {
                // Divides by the unrounded x87 value.
                DaysPerSecond = 1.0 / Reader.get_value_float(0);
                break;
            }
        } while (Reader.read_value());
    }

    // FUN_06337250's [System] value loop. The "file" key parses that system's own ini on the
    // spot, so its zones and connections are loaded with whatever nickname the block has set by
    // then as their system id.
    void ReadSystemValues(INI_Reader& Reader, SYSTEM& Record, FUniverseLoadCarry& Carry)
    {
        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("nickname"))
            {
                char const* Text = Reader.get_value_string();
                Record.NicknameId = CreateID(Text);
                Record.Nickname = StringAlloc(Text, false);
            }
            else if (Record.CommReferrable::read_comm_ini_data(Reader))
            {
                // msg_id_prefix -> CommReferrable::MsgIdPrefix.
            }
            else if (Reader.is_value("file"))
            {
                // +0x70
                Record.File = StringAlloc(Reader.get_value_string(), false);
                LoadSystemFile(Record, Record.File, Carry);
            }
            else if (Reader.is_value("strid_name"))
            {
                Record.StridName = Reader.get_value_int(0);
            }
            else if (Reader.is_value("ids_info"))
            {
                Record.IdsInfo = Reader.get_value_int(0);
            }
            else if (Reader.is_value("visit"))
            {
                Record.Visit = (unsigned char)Reader.get_value_int(0);
            }
            else if (Reader.is_value("NavMapScale"))
            {
                Record.NavMapScale = Reader.get_value_float(0);
            }
            else if (Reader.is_value("links"))
            {
                // Recognised; the value is not read.
            }
            else if (Reader.is_value("pos"))
            {
                // z is 0.0 (DAT_0639c430) when value 2 is empty.
                Record.Pos.x = Reader.get_value_float(0);
                Record.Pos.y = Reader.get_value_float(1);
                if (Reader.is_value_empty(2))
                    Record.Pos.z = 0.0f;
                else
                    Record.Pos.z = Reader.get_value_float(2);
            }
            else if (Reader.is_value("real_pos"))
            {
                Record.Pos.x = Reader.get_value_float(0);
                Record.Pos.y = Reader.get_value_float(1);
                Record.Pos.z = Reader.get_value_float(2);
            }
        } while (Reader.read_value());
    }

    // FUN_06337250 after the [System] value loop. A unique insert (FUN_0633dea0, no overwrite);
    // the pair is destroyed before the collision error. The msg_id_prefix check runs either
    // way.
    void InsertSystem(SYSTEM& Record)
    {
        if (!IdIsValid(Record.NicknameId))
        {
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: Failed to insert system \"%s\" into system DB due to invalid ID.\n",
                  kSystemsSource, 0xb39, Record.Nickname);
        }
        else
        {
            bool bInserted = false;
            {
                std::pair<unsigned, SYSTEM> const Pair(Record.GetId(), Record);
                bInserted = GSystems.insert(Pair).second;
            }
            if (!bInserted)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kSystemsSource, 0xb34,
                      "Failed to insert new system into system DB due to ID collision.\n");
            }
        }

        // A system without msg_id_prefix is an error unless it is one of the listed
        // story/tutorial systems (exact, case-sensitive match). "St03" is listed twice in
        // retail.
        if (Record.CommReferrable::GetMsgIdPrefix().Length == 0)
        {
            static char const* const kSystemsWithoutPrefix[] = {
                "St01", "St02", "St03", "St02b", "St02c", "St03", "St03b", "Tut", "Ew05",
                "FP7_system", "Ew06",
            };
            char const* Name = Record.Nickname;
            bool bListed = false;
            unsigned Index = 0;
            for (; Index < sizeof(kSystemsWithoutPrefix) / sizeof(kSystemsWithoutPrefix[0]); ++Index)
            {
                if (strcmp(Name, kSystemsWithoutPrefix[Index]) == 0)
                {
                    bListed = true;
                    break;
                }
            }
            if (!bListed)
            {
                FDUMP(0x100001,
                      "%s(%d) : *** ERROR: System \"%s\" has no msg_id_prefix information, which it "
                      "will need to be referred to by space GCS - See TAYBRO",
                      kSystemsSource, 0xb49, Name);
            }
        }
    }

    struct FZoneShapeName
    {
        char const* Name;
        unsigned Value;
    };

    unsigned ParseZoneShape(char const* Text)
    {
        // 063362d0, table 063edb80.
        static FZoneShapeName const kShapes[] = {
            {"sphere", Universe::ZoneShapeSphere},
            {"ellipsoid", Universe::ZoneShapeEllipsoid},
            {"BOX", Universe::ZoneShapeBox},
            {"CYLINDER", Universe::ZoneShapeCylinder},
            {"RING", Universe::ZoneShapeRing},
            {"MESH", Universe::ZoneShapeMesh},
        };

        if (Text == nullptr)
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: invalid ZoneShape \'%s\'\n", kSystemsSource,
                  0x24c, "");
            return Universe::ZoneShapeInvalid;
        }

        unsigned Index = 0;
        for (; Index < 6; ++Index)
        {
            if (_stricmp(Text, kShapes[Index].Name) == 0)
                return kShapes[Index].Value;
        }

        FDUMP(0x100002, "%s(%d) : *** WARNING: invalid ZoneShape \'%s\'\n", kSystemsSource, 0x24c,
              Text);
        return Universe::ZoneShapeInvalid;
    }

    // FUN_06337250 / FUN_06336750 without the universe directory file system. Retail's copy
    // into the 0x108-byte stack buffer is unbounded (undefined behaviour for a longer name);
    // the port stops at the buffer.
    void CopyRelPath(char* Dest, char const* RelFile)
    {
        unsigned Index = 0;
        for (;;)
        {
            char Copied = RelFile[Index];
            Dest[Index] = Copied;
            if (Copied == '\0')
                return;
            ++Index;
            if (Index >= kPathChars - 1)
            {
                Dest[Index] = '\0';
                return;
            }
        }
    }

    // FUN_06337250's [Zone] value loop; the caller sets the system id.
    void ReadZoneValues(INI_Reader& Reader, ZONE& Zone)
    {
        Universe::IZone* const Record = &Zone;
        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("nickname") || Reader.is_value("name"))
            {
                // Only the id (ZONE +0x04); the text is not kept.
                Record->NicknameId = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("pos"))
            {
                Record->Pos.x = Reader.get_value_float(0);
                Record->Pos.y = Reader.get_value_float(1);
                Record->Pos.z = Reader.get_value_float(2);
            }
            else if (Reader.is_value("rotate"))
            {
                Vector Angles;
                Angles.x = Reader.get_value_float(0);
                Angles.y = Reader.get_value_float(1);
                Angles.z = Reader.get_value_float(2);
                Record->Orientation = EulerMatrix(Angles);
            }
            else if (Reader.is_value("shape"))
            {
                Record->Shape = ParseZoneShape(Reader.get_value_string(0));
            }
            else if (Reader.is_value("size"))
            {
                Record->Size.x = Reader.get_value_float(0);
                if (Reader.is_value_empty(1))
                    Record->Size.y = 0.0f;
                else
                    Record->Size.y = Reader.get_value_float(1);
                if (Reader.is_value_empty(2))
                    Record->Size.z = 0.0f;
                else
                    Record->Size.z = Reader.get_value_float(2);
            }
            else if (Reader.is_value("mesh"))
            {
                // The result is dropped.
                Archetype::Load(Reader.get_value_string(0), &Record->MeshHandle, nullptr);
            }
            else if (Reader.is_value("edge_fraction"))
            {
                // Compared unrounded; unordered clamps to 1.0.
                double const Value = Reader.get_value_float(0);
                if (Value < 0.0)
                    Record->EdgeFraction = 0.0f;
                else if (Value < 1.0)
                    Record->EdgeFraction = (float)Value;
                else
                    Record->EdgeFraction = 1.0f;
            }
            else if (Reader.is_value("property_flags"))
            {
                Record->PropertyFlags |= (unsigned)Reader.get_value_int(0);
            }
            else if (Reader.is_value("property_fog_color"))
            {
                // 1/255 is the double DAT_0639a1b8.
                double const kInv255 = 0.00392156862745098;
                Record->FogColor.x = (float)(Reader.get_value_float(0) * kInv255);
                Record->FogColor.y = (float)(Reader.get_value_float(1) * kInv255);
                Record->FogColor.z = (float)(Reader.get_value_float(2) * kInv255);
            }
            else if (Reader.is_value("drag_modifier"))
            {
                // The error test uses the UNROUNDED value (unordered passes); the later tests
                // read the stored float.
                double const Unrounded = Reader.get_value_float(0);
                float const Value = (float)Unrounded;
                if (Unrounded < 1.0)
                {
                    char const* const FileName = Reader.get_file_name();
                    ID_String ZoneId;
                    ZoneId.Id = Record->NicknameId;
                    FDUMP(0x100001,
                          "%s(%d) : *** ERROR: drag modifier should be >= 1.0  in zone (%s) in file (%s)",
                          kSystemsSource, 0x9cd, ZoneId.get_string(), FileName);
                }
                if (Value < 1.0f)
                    Record->DragModifier = 1.0f;
                else
                    Record->DragModifier = Value;
                if (Value > 1.0f)
                    Record->PropertyFlags |= 0x80000;
            }
            else if (Reader.is_value("damage"))
            {
                // The flag test uses the unrounded value.
                double const Value = Reader.get_value_float(0);
                Record->Damage = (float)Value;
                if (Value > 0.0)
                    Record->PropertyFlags |= 0x40000;
            }
            else if (Reader.is_value("interference"))
            {
                // The flag test uses the unrounded value; unordered sets it.
                double const Value = Reader.get_value_float(0);
                Record->Interference = (float)Value;
                if (Value != 0.0)
                    Record->PropertyFlags |= 0x100000;
            }
            else if (Reader.is_value("music"))
            {
                Record->MusicId = CreateID(Reader.get_value_string());
                Record->PropertyFlags |= 0x400000;
            }
            else if (Reader.is_value("spacedust"))
            {
                // CacheString at +0x70.
                Record->Spacedust = StringAlloc(Reader.get_value_string(), false);
                Record->PropertyFlags |= 0x200000;
            }
            else if (Reader.is_value("spacedust_maxparticles"))
            {
                Record->SpacedustMaxParticles = Reader.get_value_int(0);
            }
            else if (Reader.is_value("faction"))
            {
                unsigned int const FactionId = MakeId(Reader.get_value_string(0));
                float const Weight = Reader.get_value_float(1);
                Record->Population.Items.push_back(std::pair<unsigned int, float>(FactionId, Weight));
            }
            else if (Reader.is_value("path_label"))
            {
                Record->bPathLabel = true;
            }
            else if (Reader.is_value("visit"))
            {
                // Through vt+0x90, which ORs in 0x20.
                Record->SetVisit((unsigned char)Reader.get_value_int(0));
            }
            else if (Reader.is_value("ids_name"))
            {
                Record->IdsName = Reader.get_value_int(0);
            }
            else if (Reader.is_value("ids_info"))
            {
                Record->IdsInfo = Reader.get_value_int(0);
            }
        } while (Reader.read_value());
    }

    // FUN_06337250 after the [Zone] value loop. Returns the map's zone, or null when it was not
    // inserted (empty id, or a duplicate: no overwrite, no error), which decides whether it goes
    // onto its SYSTEM's zone list. Retail quirk kept: the pair's destructor (FUN_06339010)
    // releases the mesh handle the map node still holds, and the stack ZONE's destructor does
    // again -- a zone mesh is released twice for the one Archetype::Load.
    Universe::IZone* InsertZone(ZONE const& Record)
    {
        if (Record.NicknameId == 0)
            return nullptr;

        std::pair<std::map<unsigned, ZONE>::iterator, bool> Inserted;
        {
            std::pair<unsigned, ZONE> const Pair(Record.NicknameId, Record);
            Inserted = GZones.insert(Pair);
        }
        if (!Inserted.second)
            return nullptr;
        return &Inserted.first->second;
    }

    unsigned LookupObjectBaseId(char const* Nickname)
    {
        // 0633bb20 (also inlined at 06336666).
        unsigned NicknameId = CreateID(Nickname);
        if (GBases.find(NicknameId) == GBases.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_base(%s) failed.\n", kSystemsSource, 0xbff,
                  Nickname);
            return 0;
        }
        return NicknameId;
    }

    unsigned SolarGateTypeFromObjectType(unsigned ObjectType)
    {
        // Inlined at 06338131.
        if ((ObjectType & 0x40) != 0)
            return Universe::SolarGateJumpGate;
        if ((ObjectType & 0x800) != 0)
            return Universe::SolarGateJumpHole;
        if ((ObjectType & 0x400) != 0)
            return Universe::SolarGateAirlock;
        return Universe::SolarGateNone;
    }

    // FUN_06336380: read one [Object] section of a system file into a pool record. The key
    // order below is retail's is_value order.
    void ReadSystemObject(Universe::FSystemObject& Record, INI_Reader& Reader)
    {
        memset(&Record, 0, sizeof(Record));
        Record.Visit = 0;
        bool bStar = false;
        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("nickname"))
            {
                // TString assignment FUN_06306020 (null -> empty).
                Record.Nickname = Reader.get_value_string();
            }
            else if (Reader.is_value("next_ring"))
            {
                Record.NextRing = Reader.get_value_string();
            }
            else if (Reader.is_value("prev_ring"))
            {
                Record.PrevRing = Reader.get_value_string();
            }
            else if (Reader.is_value("pos"))
            {
                // Value 1 is read first, then 0, then 2.
                float Y = Reader.get_value_float(1);
                float X = Reader.get_value_float(0);
                float Z = Reader.get_value_float(2);
                Record.Pos.x = X;
                Record.Pos.y = Y;
                Record.Pos.z = Z;
            }
            else if (Reader.is_value("archetype"))
            {
                ID_String Name;
                Name.Id = CreateID(Reader.get_value_string());
                Record.ArchetypeId = Archetype::GetAsteroidID(Name);
            }
            else if (Reader.is_value("ids_name"))
            {
                Record.IdsName = Reader.get_value_int(0);
            }
            else if (Reader.is_value("ids_info") || Reader.is_value("ids_info_card") ||
                     Reader.is_value("infocard_ids"))
            {
                Record.IdsInfo = Reader.get_value_int(0);
            }
            else if (Reader.is_value("goto"))
            {
                Record.Goto = CreateID(Reader.get_value_string(0));
            }
            else if (Reader.is_value("dock_with"))
            {
                Record.DockWith = CreateID(Reader.get_value_string(0));
            }
            else if (Reader.is_value("reputation"))
            {
                Record.Reputation = Reader.get_value_string(0);
            }
            else if (Reader.is_value("visit"))
            {
                Record.Visit = (unsigned char)Reader.get_value_int(0);
            }
            else if (Reader.is_value("base"))
            {
                Record.BaseId = LookupObjectBaseId(Reader.get_value_string());
            }
            else if (Reader.is_value("star"))
            {
                bStar = true;
            }
        } while (Reader.read_value());

        if (bStar)
            Record.Visit = 1;
    }

    void BuildSystemFilePath(char* FullPath, char const* RelFile)
    {
        // Inlined in FUN_06337250 and FUN_06336750. No absolute-path test (unlike
        // GetFileSysPath).
        if (GUniverseDirFs != nullptr)
        {
            char Root[kPathChars];
            GUniverseDirFs->GetFileSysPath(Root, kPathChars);
            _makepath(FullPath, nullptr, Root, RelFile, nullptr);
        }
        else
        {
            CopyRelPath(FullPath, RelFile);
        }
    }

}

namespace CmnUniverse
{
    // FUN_06336750: refill the shared object pool from one system file. Returns false when
    // the file does not open (the pool is left empty).
    bool LoadSystemObjectPool(char const* RelFile)
    {
        GSystemObjectPool.clear();

        char FullPath[kPathChars];
        BuildSystemFilePath(FullPath, RelFile);

        INI_Reader Reader;
        if (!Reader.open(FullPath, false))
            return false;

        if (GSystemObjectPool.capacity() < 300)
            GSystemObjectPool.reserve(300);

        while (Reader.read_header())
        {
            if (!Reader.is_header("Object"))
                continue;
            // The zero record is the function static DAT_0658a878.
            GSystemObjectPool.push_back(Universe::FSystemObject());
            ReadSystemObject(GSystemObjectPool.back(), Reader);
        }
        return true;
    }
}

namespace
{
    // FUN_06337250: one [Object] section of a system file during the universe load. Only the
    // keys below are read here; the full record is FUN_06336380's job.
    void ReadSystemFileObject(INI_Reader& Reader, SYSTEM& System, FUniverseLoadCarry& Carry)
    {
        unsigned NicknameId = 0;
        unsigned BaseId = 0;
        int IdsName = 0;
        unsigned ArchetypeId = 0;
        unsigned GotoId = 0;
        unsigned char Visit = 0;
        bool bSawGoto = false;
        Vector Pos;
        Pos.x = -0.1f;
        Pos.y = -0.1f;
        Pos.z = -0.1f;

        // No value at all: the carried nickname is not written back.
        if (!Reader.read_value())
            return;

        // The nickname the previous [Object] left behind.
        unsigned CarriedNickname = Carry.ObjectNickname;
        do
        {
            if (Reader.is_value("nickname"))
            {
                NicknameId = CreateID(Reader.get_value_string(0));
                CarriedNickname = NicknameId;
            }
            else if (Reader.is_value("base"))
            {
                BaseId = LookupObjectBaseId(Reader.get_value_string());
            }
            else if (Reader.is_value("ids_name"))
            {
                IdsName = Reader.get_value_int(0);
            }
            else if (Reader.is_value("archetype"))
            {
                ArchetypeId = CreateID(Reader.get_value_string(0));
            }
            else if (Reader.is_value("pos"))
            {
                Pos.x = Reader.get_value_float(0);
                Pos.y = Reader.get_value_float(1);
                Pos.z = Reader.get_value_float(2);
            }
            else if (Reader.is_value("visit"))
            {
                Visit = (unsigned char)Reader.get_value_int(0);
            }
            else if (Reader.is_value("goto"))
            {
                // Keyed by the nickname read so far in this object, else the carried one;
                // inserted without overwrite (FUN_062fbad0).
                GotoId = CreateID(Reader.get_value_string(0));
                GGateSystems.insert(std::pair<unsigned, unsigned>(CarriedNickname, GotoId));
                bSawGoto = true;
            }
        } while (Reader.read_value());

        Carry.ObjectNickname = CarriedNickname;

        if (bSawGoto)
        {
            // Connection record onto SYSTEM+0x54 (FUN_0633e770).
            ID_String ArchName;
            ArchName.Id = ArchetypeId;
            unsigned GateType = Universe::SolarGateNone;
            Archetype::Solar* SolarArch = Archetype::GetSolarByName(ArchName);
            if (SolarArch != nullptr)
                GateType = SolarGateTypeFromObjectType(SolarArch->ObjectType);

            Universe::FSystemConnection Connection;
            Connection.GotoSystemId = GotoId;
            Connection.Object.Id = NicknameId;
            Connection.ObjectId = CarriedNickname;
            Connection.Pos = Pos;
            Connection.Visit = Visit;
            Connection.GateType = GateType;
            System.Connections.push_back(Connection);
        }

        // An object that names a base updates that base.
        if (BaseId == 0 || !IdIsValid(BaseId))
            return;
        std::map<unsigned, BASE>::iterator Found = GBases.find(BaseId);
        if (Found == GBases.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_base(%#X) failed.\n", kSystemsSource, 0xbf3,
                  BaseId);
            return;
        }
        // Only when the base's strid_name equals this object's ids_name; slot 25 (+0x64) takes
        // the carried nickname and slot 26 (+0x68) the visit byte.
        Universe::IBase& Base = Found->second;
        if (Base.GetStridName() != IdsName)
            return;
        Base.SetSpaceObjectId(CarriedNickname);
        Base.SetSpaceObjectVisit(Visit);
    }

    // The system-file half of FUN_06337250: every [Object] vibe is allocated first
    // (FUN_062b8de0), then the file is re-read. A file that does not open is skipped silently.
    void LoadSystemFile(SYSTEM& System, char const* RelFile, FUniverseLoadCarry& Carry)
    {
        char FullPath[kPathChars];
        BuildSystemFilePath(FullPath, RelFile);

        INI_Reader Reader;
        if (!Reader.open(FullPath, false))
            return;

        Reputation::SolarReps::LoadObjectVibesFromSystemIni(Reader);
        Reader.reset();

        while (Reader.read_header())
        {
            if (Reader.is_header("Zone"))
            {
                // The reader state is taken at the header, before any value is read.
                Universe::FSystemZone Entry;
                Reader.get_state(Entry.State);

                // The system id is the SYSTEM's nickname id so far (+0x4c).
                ZONE Stack;
                Stack.SystemId = System.NicknameId;
                ReadZoneValues(Reader, Stack);
                Universe::IZone* const Inserted = InsertZone(Stack);
                if (Inserted != nullptr)
                {
                    Entry.Zone = Inserted;
                    System.Zones.push_back(Entry);      // FUN_0633e920
                }
            }
            else if (Reader.is_header("Music"))
            {
                if (!Reader.read_value())
                    continue;
                do
                {
                    if (Reader.is_value("space"))
                        System.SpaceMusicId = CreateID(Reader.get_value_string());
                } while (Reader.read_value());
            }
            else if (Reader.is_header("Object"))
            {
                ReadSystemFileObject(Reader, System, Carry);
            }
        }
    }

    void ReadBaseValues(INI_Reader& Reader, Universe::IBase* Record)
    {
        if (!Reader.read_value())
            return;

        do
        {
            if (Reader.is_value("nickname"))
            {
                char const* Text = Reader.get_value_string();
                Record->NicknameId = CreateID(Text);
                Record->Nickname = StringAlloc(Text, false);
            }
            else if (Reader.is_value("strid_name"))
            {
                Record->StridName = Reader.get_value_int(0);
            }
            else if (Reader.is_value("file"))
            {
                Record->File = StringAlloc(Reader.get_value_string(), false);
            }
            else if (Reader.is_value("system"))
            {
                Record->SystemId = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("BGCS_base_run_by"))
            {
                Record->BgcsBaseRunBy = Reader.get_value_string();
            }
            else if (Reader.is_value("ship_sml_01"))
            {
                Record->ShipSml[0] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_sml_02"))
            {
                Record->ShipSml[1] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_sml_03"))
            {
                Record->ShipSml[2] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_mdm_01"))
            {
                Record->ShipMdm[0] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_mdm_02"))
            {
                Record->ShipMdm[1] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_mdm_03"))
            {
                Record->ShipMdm[2] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_lrg_01"))
            {
                Record->ShipLrg[0] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_lrg_02"))
            {
                Record->ShipLrg[1] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("ship_lrg_03"))
            {
                Record->ShipLrg[2] = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("terrain_tiny"))
            {
                Record->TerrainTiny = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("terrain_sml"))
            {
                Record->TerrainSml = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("terrain_mdm"))
            {
                Record->TerrainMdm = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("terrain_lrg"))
            {
                Record->TerrainLrg = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("terrain_dyna_01"))
            {
                Record->TerrainDyna01 = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("terrain_dyna_02"))
            {
                Record->TerrainDyna02 = CreateID(Reader.get_value_string());
            }
            else if (Reader.is_value("autosave_forbidden"))
            {
                Record->bAutosaveForbidden = Reader.get_value_bool(0);
            }
        } while (Reader.read_value());
    }

    // FUN_06337250 after the [Base] value loop: a unique insert (FUN_0633ead0, no overwrite).
    void InsertBase(BASE const& Record)
    {
        if (!IdIsValid(Record.NicknameId))
        {
            FDUMP(0x100001,
                  "%s(%d) : *** ERROR: Failed to insert base \"%s\" into system DB due to invalid ID.\n",
                  kSystemsSource, 0xbc0, Record.Nickname);
            return;
        }

        bool bInserted = false;
        {
            std::pair<unsigned, BASE> const Pair(Record.GetId(), Record);
            bInserted = GBases.insert(Pair).second;
        }
        if (!bInserted)
        {
            FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kSystemsSource, 0xbbb,
                  "Failed to insert new base into system DB due to ID collision.\n");
        }
    }

    // FUN_06337250 (thiscall on the universe object DAT_0658a828). Returns the system map's
    // size, 0 when the file does not open. It also drives the reputation Vibe static store, so
    // every Vibe allocated while the universe loads is a static entry.
    unsigned LoadUniverseIni(char const* FullPath)
    {
        ClearUniverseTables();
        Reputation::Vibe::FreeStatic();          // thunk_FUN_06320550

        FUniverseLoadCarry Carry;
        Carry.SystemPos.x = 0.0f;
        Carry.SystemPos.y = 0.0f;
        Carry.SystemPos.z = 0.0f;
        Carry.ObjectNickname = 0;

        INI_Reader Reader;
        if (!Reader.open(FullPath, false))
            return (unsigned)GSystems.size();

        Reputation::Vibe::BeginStaticLoad();     // FUN_06320430

        while (Reader.read_header())
        {
            if (Reader.is_header("Time"))
            {
                ReadTimeBlock(Reader);
            }
            else if (Reader.is_header("System"))
            {
                // The stack SYSTEM's +0x74 position still holds the previous block's (see
                // FUniverseLoadCarry).
                SYSTEM Stack;
                Stack.Pos = Carry.SystemPos;
                ReadSystemValues(Reader, Stack, Carry);
                InsertSystem(Stack);
                Carry.SystemPos = Stack.Pos;
            }
            else if (Reader.is_header("Base"))
            {
                BASE Stack;
                ReadBaseValues(Reader, &Stack);
                InsertBase(Stack);
            }
        }

        // FUN_06320530, only on the opened path.
        Reputation::Vibe::EndStaticLoad();

        return (unsigned)GSystems.size();
    }
}

namespace Archetype
{
    unsigned __cdecl GetAsteroidID(ID_String const& Name)
    {
        return Name.Id;
    }

    unsigned __cdecl GetDynamicAsteroidID(ID_String const& Name)
    {
        return Name.Id;
    }

    unsigned __cdecl GetEquipmentID(ID_String const& Name)
    {
        return Name.Id;
    }

    unsigned __cdecl GetShipID(ID_String const& Name)
    {
        return Name.Id;
    }

    unsigned __cdecl GetSimpleID(ID_String const& Name)
    {
        return Name.Id;
    }

    unsigned __cdecl GetSolarID(ID_String const& Name)
    {
        return Name.Id;
    }

}

namespace Universe
{
    bool __cdecl Startup(char const* RelPath)
    {
        // 06337060. Refcount DAT_0658a9bc.
        if (GUniverseRefCount == 0)
        {
            ClearUniverseTables();
            Reputation::Vibe::FreeStatic();
            ReleaseUniverseDirFs();

            char FullPath[kPathChars];
            GetFileSysPath(FullPath, RelPath, DALib::Data);

            char Drive[kPathChars];
            char Dir[kPathChars];
            char Directory[kPathChars];
            _splitpath(FullPath, Drive, Dir, nullptr, nullptr);
            _makepath(Directory, Drive, Dir, nullptr, nullptr);

            CreateUniverseDirFs(Directory);

            unsigned SystemCount = LoadUniverseIni(FullPath);
            if (SystemCount == 0)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: could not create universe '%s'\n",
                      kSystemsSource, 0xc2d, FullPath);
            }
        }

        GUniverseRefCount += 1;
        return GUniverseDirFs != nullptr;
    }

    void __cdecl Shutdown()
    {
        // 0633bd80
        GUniverseRefCount -= 1;
        if (GUniverseRefCount != 0)
            return;

        ClearUniverseTables();
        Reputation::Vibe::FreeStatic();
        ReleaseUniverseDirFs();
    }

    unsigned __cdecl num_systems()
    {
        return (unsigned)GSystems.size();
    }

    unsigned __cdecl get_system_id(char const* Nickname)
    {
        unsigned NicknameId = CreateID(Nickname);
        if (GSystems.find(NicknameId) == GSystems.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_system(%s) failed.\n", kSystemsSource, 0xbe4,
                  Nickname);
            return 0;
        }
        return NicknameId;
    }

    ISystem const* __cdecl get_system(unsigned NicknameId)
    {
        if (!IdIsValid(NicknameId))
            return nullptr;

        std::map<unsigned, SYSTEM>::iterator Found = GSystems.find(NicknameId);
        if (Found == GSystems.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_system(%#X) failed.\n", kSystemsSource, 0xbd8,
                  NicknameId);
            return nullptr;
        }
        return &Found->second;
    }

    void __cdecl get_filename(char* Dest, char const* RelPath)
    {
        // 0633c650 (directory file system DAT_0658a9b4). No argument checks.
        if (GUniverseDirFs != nullptr)
        {
            char Root[kPathChars];
            GUniverseDirFs->GetFileSysPath(Root, kPathChars);
            _makepath(Dest, nullptr, Root, RelPath, nullptr);
            return;
        }
        strcpy(Dest, RelPath);
    }

    // 0633c030 / 0633c0a0: post-increment walks -- the element under the walk is returned
    // and the walk moves to its in-order successor before the return.
    ISystem* __cdecl GetFirstSystem()
    {
        GSystemWalk = GSystems.begin();
        if (GSystemWalk == GSystems.end())
            return nullptr;
        SYSTEM* const Current = &GSystemWalk->second;
        ++GSystemWalk;
        return Current;
    }

    ISystem* __cdecl GetNextSystem()
    {
        if (GSystemWalk == GSystems.end())
            return nullptr;
        SYSTEM* const Current = &GSystemWalk->second;
        ++GSystemWalk;
        return Current;
    }

    unsigned __cdecl num_bases()
    {
        return (unsigned)GBases.size();
    }

    unsigned __cdecl get_base_id(char const* Nickname)
    {
        unsigned NicknameId = CreateID(Nickname);
        if (GBases.find(NicknameId) == GBases.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_base(%s) failed.\n", kSystemsSource, 0xbff,
                  Nickname);
            return 0;
        }
        return NicknameId;
    }

    IBase* __cdecl get_base(unsigned NicknameId)
    {
        if (!IdIsValid(NicknameId))
            return nullptr;

        std::map<unsigned, BASE>::iterator Found = GBases.find(NicknameId);
        if (Found == GBases.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_base(%#X) failed.\n", kSystemsSource, 0xbf3,
                  NicknameId);
            return nullptr;
        }
        return &Found->second;
    }

    // 0633c230 / 0633c2a0: the same post-increment walks over the base map.
    IBase* __cdecl GetFirstBase()
    {
        GBaseWalk = GBases.begin();
        if (GBaseWalk == GBases.end())
            return nullptr;
        BASE* const Current = &GBaseWalk->second;
        ++GBaseWalk;
        return Current;
    }

    IBase* __cdecl GetNextBase()
    {
        if (GBaseWalk == GBases.end())
            return nullptr;
        BASE* const Current = &GBaseWalk->second;
        ++GBaseWalk;
        return Current;
    }

    IZone const* __cdecl get_zone(unsigned NicknameId)
    {
        // 0633c310. No 0x3fffffff mask test.
        std::map<unsigned, ZONE>::iterator Found = GZones.find(NicknameId);
        if (Found == GZones.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_zone(%d) failed", kSystemsSource, 0xc8e,
                  NicknameId);
            return nullptr;
        }
        return &Found->second;
    }

    IZone const* __cdecl get_zone(unsigned SystemId, char const* Nickname)
    {
        // 0633c380. SystemId is unused: the lookup is on the global zone map.
        (void)SystemId;
        char const* Name = Nickname;
        unsigned NicknameId = CreateID(Nickname);
        std::map<unsigned, ZONE>::iterator Found = GZones.find(NicknameId);
        if (Found == GZones.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_zone(%s) failed", kSystemsSource, 0xc9c,
                  Name);
            return nullptr;
        }
        return &Found->second;
    }

    IZone* __cdecl get_non_const_zone(unsigned SystemId, char const* Nickname)
    {
        (void)SystemId;
        char const* Name = Nickname;
        unsigned NicknameId = CreateID(Nickname);
        std::map<unsigned, ZONE>::iterator Found = GZones.find(NicknameId);
        if (Found == GZones.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_non_const_zone(%s) failed", kSystemsSource,
                  0xcaa, Name);
            return nullptr;
        }
        return &Found->second;
    }

    IZone* __cdecl get_non_const_zone(unsigned SystemId, unsigned NicknameId)
    {
        (void)SystemId;
        std::map<unsigned, ZONE>::iterator Found = GZones.find(NicknameId);
        if (Found == GZones.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_non_const_zone(%d) failed", kSystemsSource,
                  0xcb7, NicknameId);
            return nullptr;
        }
        return &Found->second;
    }

    IZone const* __cdecl first_zone(unsigned SystemId)
    {
        // 0633c4f0 (zone list SYSTEM+0x80).
        std::map<unsigned, SYSTEM>::iterator Found = GSystems.find(SystemId);
        if (Found == GSystems.end())
            return nullptr;
        if (Found->second.Zones.size() == 0)
            return nullptr;
        return Found->second.Zones.front().Zone;
    }

    IZone const* __cdecl next_zone(IZone const* Current)
    {
        // 0633c550. The entry is found by zone id, not by pointer.
        if (Current == nullptr)
            return nullptr;

        std::map<unsigned, SYSTEM>::iterator Found = GSystems.find(Current->GetSystem());
        if (Found == GSystems.end())
            return nullptr;

        std::list<FSystemZone> const& Zones = Found->second.Zones;
        std::list<FSystemZone>::const_iterator It = Zones.begin();
        std::list<FSystemZone>::const_iterator End = Zones.end();
        for (; It != End; ++It)
        {
            if (It->Zone == nullptr)
                continue;
            if (It->Zone->GetId() != Current->GetId())
                continue;
            ++It;
            if (It == End)
                return nullptr;
            return It->Zone;
        }
        return nullptr;
    }

    unsigned __cdecl get_gate_system(unsigned NicknameId)
    {
        // 0633c5e0. Map DAT_0658a868: object id -> goto system.
        std::map<unsigned, unsigned>::iterator Found = GGateSystems.find(NicknameId);
        if (Found == GGateSystems.end())
        {
            FDUMP(0x100002, "%s(%d) : *** WARNING: get_gate_system(%d) failed", kSystemsSource,
                  0xd15, NicknameId);
            return 0;
        }
        return Found->second;
    }
}

// NewSpatial / FreeSpatial and the spatial partitions live in common_spatial.cpp.
