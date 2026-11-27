// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "../include/common_server.h"
#include "../include/common_net.h"
#include "../include/common_physys.h"
#include "../include/common_casteroid.h" // CmnAsteroid::CAsteroidSystem (SrvAsteroidSystem AddRef/Release/get_sys_id)
#include "../include/common_server_playerdb.h"
#include "../include/common_ini.h"
#include "../include/common_misc.h"
#include "../include/common_universe.h"
#include "../include/common_reputation.h"
#include "../include/server_sysdb.h"
#include "../include/server_gameobject.h" // GameObject's +0x154..+0x160 slots (StarSystem::remove_player/unload)
#include "../include/common_cobjdb.h"  // IObjDB::GetZoneDb (SrvAsteroid_UpdateSystem, FUN_06d03480)
#include "../include/DALib.h"          // DALib::Data (SrvAsteroid's GetFileSysPath calls)
#include "../include/rpc_iclient.h"
#include "server_internal.h"
#include <windows.h>
#include <cstring>
#include <cmath>
#include <list>
#include <map>

// ===========================================================================
// SrvAsteroid -- the server's per-system asteroid bookkeeping. Retail source file
// "E:\FL\Scratch\Source\Server\Space\SrvAsteroid.cpp" (.rdata:06d6777c).
//
// Three levels, all owned by the Server.dll-static map DAT_06d8d980:
//   std::map<unsigned int, SrvAsteroidSystem>   one per loaded system
//     SrvAsteroidSystem::m_Fields               one FSrvAsteroidField per
//                                               CmnAsteroid::CAsteroidField
//       FSrvAsteroidField::Cubes                one FSrvAsteroidCube per cube
//                                               that reported changes
//         FSrvAsteroidCube::Objects             the server object standing in
//                                               for each live asteroid/mine
// The containers are std:: containers, not VC6 layouts; every member keeps its retail
// offset in a comment.
//
// The server objects are MedAsteroid (ctor FUN_06d04ba0, vtable .rdata:06d67894) and
// MedMine (ctor FUN_06d04c70, vtable .rdata:06d67aec), declared in server_gameobject.h;
// StarSystem makes them (create_med_asteroid / create_med_mine, FUN_06d09bb0 /
// FUN_06d09c80).
// ===========================================================================

namespace SrvAsteroid {
namespace {

// DAT_06d8d980 (static initializer LAB_06d03270, atexit FUN_06d032a0).
typedef std::map<unsigned int, SrvAsteroidSystem> FSrvAsteroidSystemMap;

FSrvAsteroidSystemMap GSrvAsteroidSystems;

} // namespace

// FUN_06d032b0: 2 when the system is already loaded (ref-counted), else 1 after loading.
int SrvAsteroid_LoadSystem(unsigned int SystemId, const char* Path) {
    FSrvAsteroidSystemMap::iterator It = GSrvAsteroidSystems.find(SystemId);
    if (It != GSrvAsteroidSystems.end()) {
        ++It->second.m_RefCount;
        return 2;
    }
    It = GSrvAsteroidSystems.insert(std::make_pair(SystemId, SrvAsteroidSystem())).first;
    ++It->second.m_RefCount;
    It->second.m_SystemId = SystemId;
    It->second.load(Path);
    return 1;
}

// FUN_06d03410: 0 when missing, 2 while still referenced, 1 after the erase.
int SrvAsteroid_UnloadSystem(unsigned int SystemId) {
    FSrvAsteroidSystemMap::iterator It = GSrvAsteroidSystems.find(SystemId);
    if (It == GSrvAsteroidSystems.end())
        return 0;
    if (--It->second.m_RefCount != 0)
        return 2;
    GSrvAsteroidSystems.erase(It);
    return 1;
}

// The value of a field entry's cube map, 0x24 bytes (offsets from FUN_06d04480).
struct FSrvAsteroidCube {
    CmnAsteroid::CAsteroidCube* Cube;              // +0x00
    unsigned int SystemId;                         // +0x04 (FUN_06d04ab0 reads it for SysMap)
    unsigned long CubeId;                          // +0x08
    int CubeSize;                                  // +0x0c
    // +0x10..+0x20: asteroid_marker::AsteroidId -> the server object (node+0x10).
    // Size at +0x20 is what FUN_06d04480's prune pass tests (node+0x30).
    std::map<unsigned long, GameObject*> Objects;

    FSrvAsteroidCube()
        : Cube(nullptr)
        , SystemId(0)
        , CubeId(0)
        , CubeSize(0xfa) {
    }

    // FUN_06d048f0 / FUN_06d046c0: destroy_notify on every object, in map order.
    ~FSrvAsteroidCube() {
        for (std::map<unsigned long, GameObject*>::iterator It = Objects.begin(); It != Objects.end(); ++It)
            It->second->destroy_notify();
    }

    void apply_marker(CmnAsteroid::asteroid_marker const* Marker);
};

// A field entry, operator_new(0x20).
struct FSrvAsteroidField {
    CmnAsteroid::CAsteroidField* Field;                   // +0x00
    std::map<unsigned long, FSrvAsteroidCube> Cubes;      // +0x04..+0x14, keyed by CubeId
    int CubeSize;                                         // +0x18
    unsigned int SystemId;                                // +0x1c

    FSrvAsteroidField()
        : Field(nullptr)
        , CubeSize(0xfa)
        , SystemId(0) {
    }

    // FUN_06d03fc0 (scalar deleting destructor FUN_06d038a0); the cube map's destructor
    // runs after this body, as in retail.
    ~FSrvAsteroidField() {
        if (Field != nullptr)
            Field->Release();
    }

    void set_field(CmnAsteroid::CAsteroidField* InField);
    void load(char const* Path);
    void update();
};

// FUN_06d03480 (cdecl). The argument is the StarSystem as an IObjDB; GetZoneDb gives its
// Universe::ISystem, whose id (NicknameId in this port) keys the map.
void SrvAsteroid_UpdateSystem(IObjDB* Db) {
    Universe::ISystem const* Info = Db->GetZoneDb();
    unsigned int SystemId = Info->NicknameId;
    FSrvAsteroidSystemMap::iterator It = GSrvAsteroidSystems.find(SystemId);
    if (It == GSrvAsteroidSystems.end())
        return;
    for (std::list<FSrvAsteroidField*>::iterator Field = It->second.m_Fields.begin();
         Field != It->second.m_Fields.end(); ++Field)
        (*Field)->update();
}

// FUN_06d03500 (cdecl), single player. No find first: an existing entry is kept and reused,
// and its fields are mapped again.
void SrvAsteroid_MapSystem(unsigned int SystemId) {
    CmnAsteroid::CAsteroidSystem* Cmn = CmnAsteroid::Find(SystemId);
    if (Cmn == nullptr)
        return;
    Cmn->AddRef();
    FSrvAsteroidSystemMap::iterator It =
        GSrvAsteroidSystems.insert(std::make_pair(SystemId, SrvAsteroidSystem())).first;
    SrvAsteroidSystem& System = It->second;
    ++System.m_RefCount;
    System.set_cmn_system(Cmn);
    System.map_asteroid_fields();
    Cmn->Release();
}

// Retail .text:06d03710 (ordinal 17).
SrvAsteroidSystem::SrvAsteroidSystem()
    : m_CmnSystem(nullptr)
    , m_SystemId(0)
    , m_RefCount(0) {
}

// Retail .text:06ce30d0 (ordinal 16): member-wise; CmnSystem without an AddRef, the entry
// pointers shallow.
SrvAsteroidSystem::SrvAsteroidSystem(const SrvAsteroidSystem& Other)
    : m_CmnSystem(Other.m_CmnSystem)
    , m_Fields(Other.m_Fields)
    , m_SystemId(Other.m_SystemId)
    , m_RefCount(Other.m_RefCount) {
}

// Retail .text:06d03740 (ordinal 22).
SrvAsteroidSystem::~SrvAsteroidSystem() {
    if (m_CmnSystem != nullptr)
        m_CmnSystem->Release();
    for (std::list<FSrvAsteroidField*>::iterator It = m_Fields.begin(); It != m_Fields.end(); ++It) {
        if (*It != nullptr)
            delete *It;
    }
    m_Fields.clear();
}

// Retail .text:06ce3170 (ordinal 32): CmnSystem copied without AddRef; retail's self check
// guards only the list assign.
SrvAsteroidSystem& SrvAsteroidSystem::operator=(const SrvAsteroidSystem& Other) {
    m_CmnSystem = Other.m_CmnSystem;
    m_Fields = Other.m_Fields;
    m_SystemId = Other.m_SystemId;
    m_RefCount = Other.m_RefCount;
    return *this;
}

namespace {

// .rdata:06d6777c -- the __FILE__ of SrvAsteroidSystem::load's FDUMP.
char const* const SrvAsteroidSource = "E:\\FL\\Scratch\\Source\\Server\\Space\\SrvAsteroid.cpp";

// FUN_06d03cf0 (cdecl); the same body is CopyBoundedString in server_savegame.cpp.
// SrvAsteroidSystem::load inlines the null arm itself.
unsigned int SrvAsteroid_CopyBounded(char* Dst, unsigned int Size, char const* Src) {
    unsigned int Length = static_cast<unsigned int>(strlen(Src));
    if (!(Length < Size))
        Length = Size - 1;
    memcpy(Dst, Src, Length);
    Dst[Length] = '\0';
    return Length;
}

} // namespace

// Retail .text:06d038c0 (ordinal 498): every [Asteroids] section with both a "file" and a
// "zone" becomes a field entry. A missing zone FDUMPs and retail then calls through it
// anyway.
void SrvAsteroidSystem::load(const char* Path) {
    INI_Reader Reader;
    char FullPath[0x100];
    GetFileSysPath(FullPath, Path, DALib::Data);
    if (!Reader.open(FullPath, false))
        return;

    while (Reader.read_header()) {
        if (!Reader.is_header("Asteroids"))
            continue;

        unsigned int FileLen = 0;
        char FileName[0x100];
        FileName[0] = '\0';
        unsigned int ZoneLen = 0;
        char ZoneName[0x40];
        ZoneName[0] = '\0';

        while (Reader.read_value()) {
            if (Reader.is_value("file")) {
                char const* Value = Reader.get_value_string();
                if (Value == nullptr) {
                    FileLen = 0;
                    FileName[0] = '\0';
                } else {
                    FileLen = SrvAsteroid_CopyBounded(FileName, 0x100, Value);
                }
            } else if (Reader.is_value("zone")) {
                char const* Value = Reader.get_value_string();
                if (Value == nullptr) {
                    ZoneLen = 0;
                    ZoneName[0] = '\0';
                } else {
                    ZoneLen = SrvAsteroid_CopyBounded(ZoneName, 0x40, Value);
                }
            }
        }

        if (FileLen == 0 || ZoneLen == 0)
            continue;

        CmnAsteroid::CAsteroidField* Field = CmnAsteroid::CreateAsteroidField(m_SystemId);
        m_Fields.push_back(new FSrvAsteroidField());
        FSrvAsteroidField* Entry = m_Fields.back();
        Entry->Field = Field;
        if (Field != nullptr) {
            Field->AddRef();
            Entry->CubeSize = Entry->Field->CubeSize;
        }
        Entry->SystemId = m_SystemId;

        Universe::IZone const* Zone = Universe::get_zone(m_SystemId, ZoneName);
        if (Zone == nullptr)
            FDUMP(kFdumpError, "%s(%d) : *** ERROR: AsteroidField linked to missing Zone (%s)",
                  SrvAsteroidSource, 0xa1, ZoneName);
        // Zone vtable +0x18, the shape id; retail discards the result. A null zone faults
        // here, as in retail.
        static_cast<void>(Zone->Shape);
        Field->set_zone(Zone);
        Entry->load(FileName);
    }
}

// Retail .text:06d03d40 (ordinal 520): FUN_06d04480 on every field entry.
void SrvAsteroidSystem::update() {
    for (std::list<FSrvAsteroidField*>::iterator It = m_Fields.begin(); It != m_Fields.end(); ++It)
        (*It)->update();
}

// Retail .text:06d03d70 (ordinal 514).
void SrvAsteroidSystem::set_sys_id(unsigned int SystemId) {
    m_SystemId = SystemId;
}

// Retail .text:06d03d80 (ordinal 511). The previous pointer is NOT released.
void SrvAsteroidSystem::set_cmn_system(CmnAsteroid::CAsteroidSystem* Sys) {
    m_CmnSystem = Sys;
    if (Sys == nullptr) {
        m_SystemId = 0;
        return;
    }
    Sys->AddRef();
    m_SystemId = m_CmnSystem->get_sys_id();
}

// Retail .text:06d03db0 (ordinal 504). No null check on CmnSystem.
void SrvAsteroidSystem::map_asteroid_fields() {
    CmnAsteroid::CAsteroidField* Field = m_CmnSystem->FindFirst();
    while (Field != nullptr) {
        Field->AddRef();
        m_Fields.push_back(new FSrvAsteroidField());
        FSrvAsteroidField* Entry = m_Fields.back();
        Entry->set_field(Field);
        Entry->SystemId = m_SystemId;
        Field->Release();
        Field = m_CmnSystem->FindNext();
    }
}

// Retail .text:06d03ed0 (ordinal 54).
int SrvAsteroidSystem::AddRef() {
    return ++m_RefCount;
}

// Retail .text:06d03ee0 (ordinal 358).
int SrvAsteroidSystem::Release() {
    return --m_RefCount;
}

// FUN_06d04140 (thiscall; the [Asteroids] "file" value), only caller SrvAsteroidSystem::load.
// [Field] cube_size / fill_dist / empty_cube_frequency are ftol'd ints; with a field set,
// the reader is rewound for CAsteroidField::load and the two values stored (the frequency
// as a float).
void FSrvAsteroidField::load(char const* Path) {
    int FillDist = 0;
    int EmptyFreq = 0;
    INI_Reader Reader;
    char FullPath[0x100];
    GetFileSysPath(FullPath, Path, DALib::Data);
    if (!Reader.open(FullPath, false))
        return;

    INI_Reader::State Start;
    Reader.get_state(Start);
    while (Reader.read_header()) {
        if (!Reader.is_header("Field"))
            continue;
        while (Reader.read_value()) {
            // x87: ftol straight on the unrounded ST0 (64-bit truncation, low dword stored).
            if (Reader.is_value("cube_size"))
                CubeSize = (int)(long long)Reader.get_value_float(0);
            else if (Reader.is_value("fill_dist"))
                FillDist = (int)(long long)Reader.get_value_float(0);
            else if (Reader.is_value("empty_cube_frequency"))
                EmptyFreq = (int)(long long)Reader.get_value_float(0);
        }
    }

    if (Field == nullptr)
        return;
    Reader.set_state(Start);
    Field->load(Reader, CubeSize);
    Field->FillDist = FillDist;
    Field->EmptyFreq = static_cast<float>(EmptyFreq);
}

// FUN_06d04480 (thiscall on the entry); callers SrvAsteroidSystem::update and FUN_06d03480.
// Each changed marker is applied to its cube record (created on first sight); then every
// record with no objects is erased.
void FSrvAsteroidField::update() {
    CmnAsteroid::asteroid_marker* Marker = Field->get_changes();
    while (Marker != nullptr) {
        FSrvAsteroidCube* Record;
        std::map<unsigned long, FSrvAsteroidCube>::iterator It = Cubes.find(Marker->CubeId);
        if (It == Cubes.end()) {
            It = Cubes.insert(std::make_pair(Marker->CubeId, FSrvAsteroidCube())).first;
            Record = &It->second;
            CmnAsteroid::CAsteroidCube* Cube = Field->find_cube(Marker->CubeId);
            if (Cube != nullptr) {
                Record->Cube = Cube;
                Record->CubeId = Cube->get_id();
                Record->CubeSize = CubeSize;
                Record->SystemId = SystemId;
            }
        } else {
            Record = &It->second;
        }
        Record->apply_marker(Marker);
        Marker = Marker->Next;
    }

    std::map<unsigned long, FSrvAsteroidCube>::iterator It = Cubes.begin();
    while (It != Cubes.end()) {
        if (It->second.Objects.size() == 0)
            It = Cubes.erase(It);
        else
            ++It;
    }
}

// FUN_06d04810. The previous field is not released.
void FSrvAsteroidField::set_field(CmnAsteroid::CAsteroidField* InField) {
    Field = InField;
    if (InField != nullptr) {
        InField->AddRef();
        CubeSize = Field->CubeSize;
    }
}

// FUN_06d04ab0 (thiscall); only caller FUN_06d04480. An inactive cube destroys the marker's
// object; an active one with no object creates a MedMine (armed with the archetype's detect
// radius, read through cobject() with no class narrowing) or a MedAsteroid, with no end
// check on the SysMap lookup. An active one with an object calls get_position and drops the
// result.
void FSrvAsteroidCube::apply_marker(CmnAsteroid::asteroid_marker const* Marker) {
    std::map<unsigned long, GameObject*>::iterator It = Objects.find(Marker->AsteroidId);
    if (!Marker->bCubeActive) {
        if (It != Objects.end()) {
            It->second->destroy_notify();
            Objects.erase(It);
        }
        return;
    }
    if (It == Objects.end()) {
        StarSystem& System = SysDB::SysMap.find(SystemId)->second;
        GameObject* Obj = nullptr;
        if (Marker->bMine) {
            MedMine* const Mine = System.create_med_mine(Marker->ObjId);
            Archetype::Asteroid const* const Arch =
                static_cast<Archetype::Asteroid const*>(Mine->cobject()->get_archetype());
            Mine->arm(Arch->DetectRadius, SystemId);
            Obj = Mine;
        } else {
            Obj = System.create_med_asteroid(Marker->ObjId);
        }
        Objects.insert(std::map<unsigned long, GameObject*>::value_type(Marker->AsteroidId, Obj));
        return;
    }
    It->second->get_position();
}

// ---------------------------------------------------------------------------
// MedAsteroid / MedMine. Declarations and the vtable dump in
// src/include/server_gameobject.h.
// ---------------------------------------------------------------------------

// FUN_06d04ba0 (vtables .rdata:06d67894 / 06d67814 / 06d67808).
MedAsteroid::MedAsteroid()
    : Field_0x60(0)
    , Field_0x64(nullptr)
{
}

// FUN_06d04c00 (scalar deleting destructor FUN_06d04bd0).
MedAsteroid::~MedAsteroid()
{
    if (Field_0x64 != nullptr)
        operator delete(Field_0x64);
}

// LAB_06d02cb0: returns false; the same body sits in 21 slots across Server.dll's wrappers.
bool MedAsteroid::create(Archetype::Root* Arch)
{
    (void)Arch;
    return false;
}

// LAB_06d02cb0 -- see MedAsteroid::create.
bool MedMine::create(Archetype::Root* Arch)
{
    (void)Arch;
    return false;
}

// LAB_06d04c30, shared by MedAsteroid and MedMine. Unlike Ship::find
// (GameObject::FindOfClass), advise runs only on a hit.
bool MedAsteroid::find(unsigned int const& Id)
{
    Object = CObject::Find(Id, CObject::ClassAsteroid);
    if (Object != nullptr)
        advise(true);
    return Object != nullptr;
}

// LAB_06d04c30 -- see MedAsteroid::find (the same body, ClassAsteroid 0x1003).
bool MedMine::find(unsigned int const& Id)
{
    Object = CObject::Find(Id, CObject::ClassAsteroid);
    if (Object != nullptr)
        advise(true);
    return Object != nullptr;
}

// Empty (.text:06d28880, ICF-folded with IServerImpl::Connect).
void MedAsteroid::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// Empty (.text:06d28880).
void MedAsteroid::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// Empty (.text:06d28880).
void MedAsteroid::explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage)
{
    (void)Blast;
    (void)Damage;
}

// Empty (.text:06ce1050). MedMine does NOT override this slot; it keeps GameObject's
// FUN_06ceee70.
void MedAsteroid::apply_damage_list(DamageList& Damage)
{
    (void)Damage;
}

// FUN_06d04c70 (vtables .rdata:06d67aec / 06d67a6c / 06d67a60, CObjectCB .rdata:06d67a54 at
// +0x60). Retail leaves +0x64, +0x68, +0x6c..+0x74 and +0x84 for arm() to write; the port
// zeroes them.
MedMine::MedMine()
    : Phantom(nullptr)
    , IntruderCount(0)
    , Explosion(nullptr)
    , RechargeTimer(1.0f)
    , RechargeTime(1.0f)
    , ExplosionOffset(0.0f)
{
    Direction.x = 0.0f;
    Direction.y = 0.0f;
    Direction.z = 0.0f;
}

// FUN_06d04ce0 (scalar deleting destructor FUN_06d04cb0). No null test before
// DestroyPhantom.
MedMine::~MedMine()
{
    PhySys::DestroyPhantom(Phantom);
    Phantom = nullptr;
}

// Empty (.text:06d28880).
void MedMine::damage_sub_obj(FSubObjDamage const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// Empty (.text:06d28880).
void MedMine::munition_hit(FMunitionImpact const& Hit, DamageList& Damage)
{
    (void)Hit;
    (void)Damage;
}

// Empty (.text:06d28880).
void MedMine::explosion_hit(ExplosionDamageEvent const& Blast, DamageList& Damage)
{
    (void)Blast;
    (void)Damage;
}

// Empty (.text:06d28880) -- MedMine's own +0x1bc (.rdata:06d67ca8).
void MedMine::Slot1bc(unsigned int Arg1, unsigned int Arg2)
{
    (void)Arg1;
    (void)Arg2;
}

// FUN_06d04d60: a phantom of the detect radius at the mine's position, listening through
// the CObjectCB sub-object (+0x60), attached to the raw CBase::Object; then the explosion,
// offset and recharge time from the archetype. Name[1..9] of the parms is never written
// (stack garbage in retail).
void MedMine::arm(float DetectRadius, unsigned int SystemId)
{
    IntruderCount = 0;

    PhySys::PhantomParms Parms;
    Parms.bPhysicalUnmoveable = 0;
    Parms.Flag05 = 0;
    Parms.Name[0] = '\0';
    Parms.Orientation = Matrix(1.0f, 0.0f, 0.0f,
                               0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 1.0f);
    Parms.SystemId = SystemId;
    Parms.Position = get_position();
    Parms.Listener = static_cast<PhySys::CObjectCB*>(this);

    PhySys::CreatePhantom(Parms, DetectRadius, Phantom);
    PhySys::AttachPhantom(Object, Phantom);
    PhySys::SetCollisionStatePhantom(Phantom, true);

    Archetype::Asteroid const* const Arch =
        static_cast<Archetype::Asteroid const*>(Object->get_archetype());
    Explosion = Archetype::GetExplosion(Arch->ExplosionArch);
    ExplosionOffset = Arch->ExplosionOffset;
    RechargeTime = Arch->RechargeTime;
    RechargeTimer = Arch->RechargeTime;
}

// LAB_06d04e50 -- MedMine's update (+0x16c). While an intruder is inside and the recharge
// timer has run out, the mine explodes (when the explosion has a radius and any damage
// above 0.0001, DAT_06d65190) and the mine-explosion packet goes out; a failed explosion
// test skips only the explode call, not the packet or the timer reset.
bool MedMine::update(float Dt)
{
    if (RechargeTimer < RechargeTime)
        RechargeTimer = Dt + RechargeTimer;

    if (IntruderCount != 0 && RechargeTimer >= RechargeTime) {
        ExplosionDamageEvent Blast;
        Blast.ExplodingObjId = get_id();
        Blast.InflictorId = get_id();
        Blast.Cause = DamageCause::Mine;
        Blast.Position = get_position();
        Blast.Explosion = Explosion;
        Blast.SkipCollisionGroup = nullptr;
        if (Explosion != nullptr && Explosion->Radius > 0.0001 &&
            (Explosion->Impulse > 0.0001 || Explosion->HullDamage > 0.0001 ||
             Explosion->EnergyDamage > 0.0001))
            OwnerSystem->explode(Blast);

        FMineExplosion Rec;
        Rec.Offset.x = ExplosionOffset * Direction.x;
        Rec.Offset.y = ExplosionOffset * Direction.y;
        Rec.Offset.z = ExplosionOffset * Direction.z;
        Rec.MineId = get_id();
        OwnerSystem->send_mine_explosion(Rec);
        RechargeTimer = 0.0f;
    }
    return !is_destroyed();
}

// LAB_06d04fb0 -- CObjectCB slot 0 (this + 0x60). A ship root counts as an intruder; the
// direction is from the mine to the ARGUMENT's own position (not its root), normalised with
// no zero test.
void MedMine::added(CObject* Obj)
{
    CObject* const Root = GetRoot(Obj);
    if (Root == nullptr)
        return;
    if ((static_cast<unsigned int>(Root->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return;

    Vector const& MinePos = get_position();
    Direction.x = Obj->WorldPosition.x - MinePos.x;
    Direction.y = Obj->WorldPosition.y - MinePos.y;
    Direction.z = Obj->WorldPosition.z - MinePos.z;

    float const Length = sqrtf((Direction.x * Direction.x + Direction.y * Direction.y) +
                               Direction.z * Direction.z);
    float const Inverse = 1.0f / Length;
    Direction.x = Inverse * Direction.x;
    Direction.y = Inverse * Direction.y;
    Direction.z = Inverse * Direction.z;

    ++IntruderCount;
}

// LAB_06d05070 -- CObjectCB slot 1.
void MedMine::deleted(CObject* Obj)
{
    CObject* const Root = GetRoot(Obj);
    if (Root == nullptr)
        return;
    if ((static_cast<unsigned int>(Root->ObjectClass) & CObject::ClassShip) != CObject::ClassShip)
        return;
    --IntruderCount;
}

} // namespace SrvAsteroid
