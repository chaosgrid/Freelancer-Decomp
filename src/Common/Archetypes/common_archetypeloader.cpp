// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Archetype table loaders -- retail Archetype.cpp (LoadSimples 0x062f1000 .. Free
// 0x062fa980 and the per-map teardowns up to 0x062fbe10) plus LoadExplosion 0x0630bd60 /
// FreeExplosions 0x0630c080. Declared in common_archetype.h (namespace Archetype).
#include "common_archetype.h"
#include "common_cliobj.h" // Archetype::ICliObjFactory
#include "common_id.h"     // ID_String, CreateID
#include "common_ini.h"    // INI_Reader
#include "common_misc.h"   // SinglePlayer, SUBOBJ_ID_NONE
#include "dacom.h"         // FDUMP

#include <map>
#include <utility>

namespace
{
    const char kArchetypeSource[] = "E:\\FL\\Scratch\\Source\\Common\\Archetype.cpp";

    // LoadEquipment's equipment sections, in retail's is_header order. FactoryTypeId is the
    // ICliObjFactory kind of each branch and also the record's get_class_type value.
    struct FEquipmentKind
    {
        char const* Name;
        int FactoryTypeId;
    };

    FEquipmentKind const kEquipmentKinds[] = {
        { "LootCrate", 8 },
        { "Commodity", 0xA },
        { "Power", 0xB },
        { "Engine", 0xC },
        { "Armor", 0x22 },
        { "Shield", 0xD },
        { "ShieldGenerator", 0xE },
        { "Thruster", 0xF },
        { "Gun", 0x11 },
        { "MineDropper", 0x12 },
        { "CounterMeasureDropper", 0x13 },
        { "Scanner", 0x14 },
        { "Light", 0x15 },
        { "Tractor", 0x16 },
        { "CargoPod", 9 },
        { "AttachedFX", 0x17 },
        { "InternalFX", 0x18 },
        { "TradeLane", 0x1D },
        { "RepairDroid", 0x19 },
        { "RepairKit", 0x1A },
        { "ShieldBattery", 0x1B },
        { "CloakingDevice", 0x1C },
        { "Munition", 0x1F },
        { "Mine", 0x20 },
        { "CounterMeasure", 0x21 },
    };

    FEquipmentKind const* MatchEquipmentKind(INI_Reader& Reader)
    {
        unsigned Count = sizeof(kEquipmentKinds) / sizeof(kEquipmentKinds[0]);
        for (unsigned Index = 0; Index < Count; ++Index)
        {
            if (Reader.is_header(kEquipmentKinds[Index].Name))
                return &kEquipmentKinds[Index];
        }
        return nullptr;
    }

    // operator new + the inlined constructor of each LoadEquipment branch, keyed by the factory
    // kind (unique per section).
    Archetype::Equipment* MakeEquipmentRecord(int FactoryTypeId, Archetype::ICliObj* Companion)
    {
        switch (FactoryTypeId)
        {
        case 0x08: return new Archetype::LootCrate(Companion);
        case 0x0A: return new Archetype::Commodity(Companion);
        case 0x0B: return new Archetype::Power(Companion);
        case 0x0C: return new Archetype::Engine(Companion);
        case 0x22: return new Archetype::Armor(Companion);
        case 0x0D: return new Archetype::Shield(Companion);
        case 0x0E: return new Archetype::ShieldGenerator(Companion);
        case 0x0F: return new Archetype::Thruster(Companion);
        case 0x11: return new Archetype::Gun(Companion);
        case 0x12: return new Archetype::MineDropper(Companion);
        case 0x13: return new Archetype::CounterMeasureDropper(Companion);
        case 0x14: return new Archetype::Scanner(Companion);
        case 0x15: return new Archetype::Light(Companion);
        case 0x16: return new Archetype::Tractor(Companion);
        case 0x09: return new Archetype::CargoPod(Companion);
        case 0x17: return new Archetype::AttachedFXEquip(Companion);
        case 0x18: return new Archetype::InternalFXEquip(Companion);
        case 0x1D: return new Archetype::TradeLaneEquip(Companion);
        case 0x19: return new Archetype::RepairDroid(Companion);
        case 0x1A: return new Archetype::RepairKit(Companion);
        case 0x1B: return new Archetype::ShieldBattery(Companion);
        case 0x1C: return new Archetype::CloakingDevice(Companion);
        case 0x1F: return new Archetype::Munition(Companion);
        case 0x20: return new Archetype::Mine(Companion);
        case 0x21:
        default:   return new Archetype::CounterMeasure(Companion); // the table holds no other id
        }
    }

    // The plain value loop of LoadEquipment and FUN_062f0dc0: read() for every key, and on false
    // SinglePlayer() with its result discarded. Equipment::read handles "inherit" itself and
    // [Simple] has none. Ships, Solars and Asteroids use their own loops below;
    // DynamicAsteroid's loop ignores read()'s result.
    void ReadRootValues(INI_Reader& Reader, Archetype::Root* Record)
    {
        while (Reader.read_value())
        {
            if (!Record->read(Reader))
                SinglePlayer();
        }
    }

    // Insert success tail shared by the loaders: the companion is bound to the record.
    void BindInsertedRecord(Archetype::Root* Record)
    {
        if (Record->Companion != nullptr)
            Record->Companion->Bind(Record);
    }

    // Duplicate tail: the live record is fetched the way ArchDB::Get does (dump at line 0x49 on
    // a miss) and redefined from the new one. On a miss -- a section without a nickname, which
    // the insert rejected -- retail dumps and then calls through a null pointer; the port makes
    // the same call, no null test.
    template <typename T>
    void RedefineLiveRecord(Archetype::ArchDB<T>& Db, T* Record)
    {
        T* Live = Db.Get(Record->Nickname);
        Live->redefine(*Record);
    }

    // [Motor] in LoadEquipment: read() results ignored, then the inline insert into DAT_063fca70
    // (same shape as FUN_062fbeb0). Not inserted: there is no reload test -- the live motor is
    // redefined and the new one freed. On a miss (nickname 0) retail calls through a null
    // pointer; the port makes the same call, no null test.
    void LoadMotorSection(INI_Reader& Reader)
    {
        Archetype::MotorData* Record = new Archetype::MotorData();
        while (Reader.read_value())
            Record->read(Reader);

        if (Archetype::GMotorDb.Insert(Record, Record->Nickname, "Motor"))
            return;

        Archetype::MotorData* Live = Archetype::GMotorDb.Get(Record->Nickname);
        Live->redefine(*Record);
        delete Record;
    }

    // FUN_062f0dc0, the [Simple] section shared by LoadSimples and LoadShips: companion kind 0,
    // inline insert into DAT_063fcae8. Not inserted: reload redefines the live record and
    // answers true, otherwise a warning and false; the new record is deleted either way.
    bool LoadSimpleSection(INI_Reader& Reader, bool bReload, Archetype::ICliObjFactory* Factory)
    {
        Archetype::ICliObj* Companion = nullptr;
        if (Factory != nullptr)
            Factory->Create(0, &Companion);
        Archetype::Root* Record = new Archetype::Root(Companion);

        ReadRootValues(Reader, Record);

        if (Archetype::GSimpleDb.Insert(Record, Record->Nickname, "Simple"))
        {
            BindInsertedRecord(Record);
            return true;
        }

        bool bResult = false;
        if (bReload)
        {
            RedefineLiveRecord(Archetype::GSimpleDb, Record);
            bResult = true;
        }
        else
        {
            FDUMP(0x100002,
                  "%s(%d) : *** WARNING: [Simple] nickname = %s value is not unique. Aborted second version.",
                  kArchetypeSource, 0x364, Record->Nickname.get_string());
        }
        delete Record;
        return bResult;
    }

    // Archetype::Free's per-record teardown, inlined for every Root map (also FUN_062fabb0 /
    // FUN_062fae00): release while still loaded, counting the turns; a non-zero count is
    // dumped with the nickname; then the scalar deleting dtor.
    void FreeRootRecord(Archetype::Root* Record, char const* Format, int Line)
    {
        if (Record == nullptr)
            return;

        int Dangling = 0;
        if (Record->EngineIndex != -1)
        {
            do
            {
                Record->Release();
                ++Dangling;
            } while (Record->EngineIndex != -1);

            if (Dangling != 0)
                FDUMP(0x100002, Format, kArchetypeSource, Line, Record->Nickname.get_string(), Dangling);
        }
        delete Record;
    }

    void FreeSolarRecord(Archetype::Solar* Entry)
    {
        FreeRootRecord(Entry,
                       "%s(%d) : *** WARNING: Archetype::Solar %s has %d dangling references.\n",
                       0x7ce);
    }

    void FreeShipRecord(Archetype::Ship* Entry)
    {
        // Retail format really has %x for the nickname string (and no newline); reproduced.
        FreeRootRecord(Entry,
                       "%s(%d) : *** WARNING: Archetype::Ship %x has %d dangling references.",
                       0x6d8);
    }

    void FreeEquipmentRecord(Archetype::Equipment* Entry)
    {
        FreeRootRecord(Entry,
                       "%s(%d) : *** WARNING: Archetype::Equipment %s has %d dangling references.\n",
                       0x112a);
    }

    void FreeSimpleRecord(Archetype::Root* Entry)
    {
        FreeRootRecord(Entry,
                       "%s(%d) : *** WARNING: Archetype::SimpleDB %s has %d dangling references.\n",
                       0x3c4);
    }

    void FreeAsteroidRecord(Archetype::Asteroid* Entry)
    {
        FreeRootRecord(Entry,
                       "%s(%d) : *** WARNING: Archetype::Asteroid %s has %d dangling references.",
                       0x12a8);
    }

    void FreeDynamicAsteroidRecord(Archetype::DynamicAsteroid* Entry)
    {
        FreeRootRecord(Entry,
                       "%s(%d) : *** WARNING: Archetype::DynamicAsteroid %s has %d dangling references.",
                       0x12bc);
    }

    // FUN_062facf0: every non-null MotorData is released with plain operator delete (MotorData
    // has a trivial destructor), then the map is emptied.
    void FreeMotorRecord(Archetype::MotorData* Entry)
    {
        delete Entry;
    }

    // The "inherit =" donor copy is inline in the [Ship], [Solar] and [Asteroid] value loops.
    // Each loop tests "inherit" before handing the line to read(), so read() (and the
    // companion's ReadUnknown) never sees that key.
    //
    // Shape of the copy, the same in all three loops (Companion is the factory-filled local,
    // also the record's +0x04):
    //   - Companion, when non-null: AddRef, then Inherit with the donor's companion (the Solar
    //     loop skips Inherit when the donor's companion is null).
    //   - Save the nickname.
    //   - Build a stack temp from the donor, copy the leaf fields into it, then run the base
    //     operator= from the temp into the record; the temp's destruction releases the
    //     companion it took from the donor.
    //   - Release the record's (donor) companion, then restore Companion and the nickname.
    // Deviation: the port runs the base operator= straight from the donor. That drops retail UB
    // (the temp's +0x04 is never initialised before Root::operator= releases it); the donor's
    // companion nets to +0 references either way.

    // LoadShips' value loop (062f2ae0). The donor lookup is ArchDB::Get(unsigned) inlined; no
    // donor -> the 'inherit' warning. Any other key goes to Ship::read, and on false to
    // SinglePlayer() with the result discarded.
    void ReadShipValues(INI_Reader& Reader, Archetype::Ship* Record, Archetype::ICliObj* Companion)
    {
        while (Reader.read_value())
        {
            if (!Reader.is_value("inherit"))
            {
                if (!Record->read(Reader))
                    SinglePlayer();
                continue;
            }

            ID_String DonorId;
            DonorId.Id = CreateID(Reader.get_value_string());
            Archetype::Ship* Donor = Archetype::GShipDb.Get(DonorId.Id);
            if (Donor == nullptr)
            {
                FDUMP(0x100002, "%s(%d) : *** WARNING: 'inherit' failed. couldn't find %s",
                      kArchetypeSource, 0x649, DonorId.get_string());
                continue;
            }

            ID_String const SavedNickname = Record->Nickname;
            if (Companion != nullptr)
            {
                Companion->AddRef();
                Companion->Inherit(Donor->Companion);
            }

            Record->EqObj::operator=(*Donor);
            Record->MsgIdPrefix = Donor->MsgIdPrefix.Data;         // FUN_062fb910
            Record->BayDoorAnim = Donor->BayDoorAnim;              // +0xdc..+0x12c
            Record->HpBaySurface = Donor->HpBaySurface;
            Record->HpBayExternal = Donor->HpBayExternal;
            Record->HpTractorSource = Donor->HpTractorSource;
            Record->IdsInfo1 = Donor->IdsInfo1;
            Record->IdsInfo2 = Donor->IdsInfo2;
            Record->IdsInfo3 = Donor->IdsInfo3;
            Record->ShipClass = Donor->ShipClass;
            Record->NumExhaustNozzles = Donor->NumExhaustNozzles;
            Record->HoldSize = Donor->HoldSize;
            Record->LinearDrag = Donor->LinearDrag;
            Record->AngularDragX = Donor->AngularDragX;
            Record->AngularDragY = Donor->AngularDragY;
            Record->AngularDragZ = Donor->AngularDragZ;
            Record->SteeringTorqueX = Donor->SteeringTorqueX;
            Record->SteeringTorqueY = Donor->SteeringTorqueY;
            Record->SteeringTorqueZ = Donor->SteeringTorqueZ;
            Record->NudgeForce = Donor->NudgeForce;
            Record->StrafeForce = Donor->StrafeForce;
            Record->StrafePowerUsage = Donor->StrafePowerUsage;
            Record->MaxBankAngle = Donor->MaxBankAngle;
            // +0x130: retail copies through the temp (FUN_062fb1e0 / FUN_062fb2a0).
            Record->LegalHps = Donor->LegalHps;
            Record->NanobotLimit = Donor->NanobotLimit;
            Record->ShieldBatteryLimit = Donor->ShieldBatteryLimit;

            if (Record->Companion != nullptr)
                Record->Companion->Release();
            Record->Companion = Companion;
            Record->Nickname = SavedNickname;
        }
    }

    // LoadSolar's value loop (062f3705). The lookup is ArchDB::Get(ID_String) inlined; no warning
    // follows its miss dump.
    void ReadSolarValues(INI_Reader& Reader, Archetype::Solar* Record, Archetype::ICliObj* Companion)
    {
        while (Reader.read_value())
        {
            if (!Reader.is_value("inherit"))
            {
                if (!Record->read(Reader))
                    SinglePlayer();
                continue;
            }

            ID_String DonorId;
            DonorId.Id = CreateID(Reader.get_value_string());
            Archetype::Solar* Donor = Archetype::GSolarDb.Get(DonorId);
            if (Donor == nullptr)
                continue;

            ID_String const SavedNickname = Record->Nickname;
            if (Companion != nullptr)
            {
                Companion->AddRef();
                if (Donor->Companion != nullptr)
                    Companion->Inherit(Donor->Companion);
            }

            Record->EqObj::operator=(*Donor);
            Record->bDestructible = Donor->bDestructible;          // +0x94..+0xa8
            Record->SolarRadius = Donor->SolarRadius;
            Record->Toughness = Donor->Toughness;
            Record->OpenAnim = Donor->OpenAnim;
            Record->JumpOutHp = Donor->JumpOutHp;
            Record->Loadout = Donor->Loadout;

            if (Record->Companion != nullptr)
                Record->Companion->Release();
            Record->Nickname = SavedNickname;
            Record->Companion = Companion;
        }
    }

    // LoadAsteroids' [Asteroid] / [AsteroidMine] value loop (062fa2c0). Lookup and miss as
    // LoadShips. Any other key goes to Asteroid::read with the result ignored (no SinglePlayer).
    void ReadAsteroidValues(INI_Reader& Reader, Archetype::Asteroid* Record, Archetype::ICliObj* Companion)
    {
        while (Reader.read_value())
        {
            if (!Reader.is_value("inherit"))
            {
                Record->read(Reader);
                continue;
            }

            ID_String DonorId;
            DonorId.Id = CreateID(Reader.get_value_string());
            Archetype::Asteroid* Donor = Archetype::GAsteroidDb.Get(DonorId.Id);
            if (Donor == nullptr)
            {
                FDUMP(0x100002, "%s(%d) : *** WARNING: 'inherit' failed. couldn't find %s",
                      kArchetypeSource, 0x125b, DonorId.get_string());
                continue;
            }

            ID_String const SavedNickname = Record->Nickname;
            if (Companion != nullptr)
            {
                Companion->AddRef();
                Companion->Inherit(Donor->Companion);
            }

            Record->Root::operator=(*Donor);
            Record->DetectRadius = Donor->DetectRadius;            // +0x5c..+0x64
            Record->ExplosionOffset = Donor->ExplosionOffset;
            Record->RechargeTime = Donor->RechargeTime;

            if (Record->Companion != nullptr)
                Record->Companion->Release();
            Record->Nickname = SavedNickname;
            Record->Companion = Companion;
        }
    }

    // [LOD] in every loader: the last inserted record's companion gets Lod(reader).
    void ApplyLodHeader(INI_Reader& Reader, Archetype::Root* Current)
    {
        if (Current == nullptr || Current->Companion == nullptr)
            return;
        Current->Companion->Lod(&Reader);
    }
}

int __cdecl Archetype::LoadEquipment(char const* Path, bool bReload, ICliObjFactory* Factory)
{
    // FUN_062f7c50. Returns the (motors + equipment) growth; the BEAM_ARCH handle is created at
    // the end even when the file did not open.
    unsigned Saved = GEquipmentDb.Size() + GMotorDb.Size();

    INI_Reader Reader;
    if (Reader.open(Path, false))
    {
        Root* Current = nullptr;
        while (Reader.read_header())
        {
            if (Reader.is_header("Motor"))
            {
                Current = nullptr;
                LoadMotorSection(Reader);
            }
            else if (Reader.is_header("Explosion"))
            {
                Current = nullptr;
                LoadExplosion(Reader, bReload);
            }
            else
            {
                FEquipmentKind const* Kind = MatchEquipmentKind(Reader);
                if (Kind != nullptr)
                {
                    ICliObj* Companion = nullptr;
                    if (Factory != nullptr)
                        Factory->Create(Kind->FactoryTypeId, &Companion);

                    Equipment* Record = MakeEquipmentRecord(Kind->FactoryTypeId, Companion);
                    ReadRootValues(Reader, Record);

                    // FUN_062fbbe0 with the literal "Equipment" for every section.
                    if (GEquipmentDb.Insert(Record, Record->Nickname, "Equipment"))
                    {
                        BindInsertedRecord(Record);
                        Current = Record;
                    }
                    else
                    {
                        // Only a reload redefines; no warning on a duplicate.
                        if (bReload)
                            RedefineLiveRecord(GEquipmentDb, Record);
                        delete Record;
                        Current = nullptr;
                    }
                }
                else if (Reader.is_header("LOD"))
                {
                    ApplyLodHeader(Reader, Current);
                }
            }
        }
    }

    Munition::LoadBeamArchIdx();
    return (int)(GEquipmentDb.Size() + GMotorDb.Size() - Saved);
}

int __cdecl Archetype::LoadSolar(char const* Path, bool bReload, ICliObjFactory* Factory)
{
    // FUN_062f35b0. Returns the Solar map growth (0 when the file does not open). Header order:
    // Solar, CollisionGroup, Explosion, LOD.
    unsigned Saved = GSolarDb.Size();

    INI_Reader Reader;
    if (!Reader.open(Path, false))
        return 0;

    SubObjectID::CollGroupIdMaker GroupIds;
    Solar* Current = nullptr;
    while (Reader.read_header())
    {
        if (Reader.is_header("Solar"))
        {
            ICliObj* Companion = nullptr;
            if (Factory != nullptr)
                Factory->Create(2, &Companion);

            Solar* Record = new Solar(Companion);              // inline ctor in retail
            GroupIds.Reset();
            ReadSolarValues(Reader, Record, Companion);
            if (GSolarDb.Insert(Record, Record->Nickname, "Solar"))
            {
                BindInsertedRecord(Record);
                Current = Record;
            }
            else
            {
                // No reload test and no warning: a duplicate always redefines.
                RedefineLiveRecord(GSolarDb, Record);
                delete Record;
                Current = nullptr;
            }
        }
        else if (Reader.is_header("CollisionGroup"))
        {
            // No SUBOBJ_ID_NONE test on the group id here, unlike Ships.
            if (Current == nullptr)
            {
                FDUMP(0x100001, "%s(%d) : *** ERROR: %s", kArchetypeSource, 0x780,
                      "Attempt to load a solar group into a solar that wasn't loaded.\n");
            }
            else
            {
                unsigned short const GroupId = GroupIds.CreateGroupID();
                ReadCollisionGroup(Reader, Current, GroupId);
            }
        }
        else if (Reader.is_header("Explosion"))
        {
            LoadExplosion(Reader, bReload);
        }
        else if (Reader.is_header("LOD"))
        {
            ApplyLodHeader(Reader, Current);
        }
    }

    return (int)(GSolarDb.Size() - Saved);
}

int __cdecl Archetype::LoadShips(char const* Path, bool bReload, ICliObjFactory* Factory)
{
    // FUN_062f29d0. Returns the Ship map growth (0 when the file does not open). Header order:
    // Ship, CollisionGroup, LOD, Simple, Explosion.
    unsigned Saved = GShipDb.Size();

    INI_Reader Reader;
    if (!Reader.open(Path, false))
        return 0;

    SubObjectID::CollGroupIdMaker GroupIds;
    Ship* Current = nullptr;
    while (Reader.read_header())
    {
        if (Reader.is_header("Ship"))
        {
            ICliObj* Companion = nullptr;
            if (Factory != nullptr)
                Factory->Create(3, &Companion);

            Ship* Record = new Ship(Companion);
            GroupIds.Reset();
            ReadShipValues(Reader, Record, Companion);
            if (GShipDb.Insert(Record, Record->Nickname, "Ship"))
            {
                BindInsertedRecord(Record);
                Current = Record;
            }
            else
            {
                if (bReload)
                    RedefineLiveRecord(GShipDb, Record);
                else
                    FDUMP(0x100002,
                          "%s(%d) : *** WARNING: [Ship] nickname = %s value is not unique. Aborted second version.",
                          kArchetypeSource, 0x66b, Record->Nickname.get_string());
                delete Record;
                Current = nullptr;
            }
        }
        else if (Reader.is_header("CollisionGroup"))
        {
            // Skipped without a current ship; SUBOBJ_ID_NONE (the 16-bit counter wrapped) warns
            // instead of making a group.
            if (Current != nullptr)
            {
                unsigned short const GroupId = GroupIds.CreateGroupID();
                if (GroupId == SUBOBJ_ID_NONE)
                {
                    FDUMP(0x100002,
                          "%s(%d) : *** WARNING: [Ship] nickname = %s: Exceeds maximum collision group",
                          kArchetypeSource, 0x67d, Current->Nickname.get_string());
                }
                else
                    ReadCollisionGroup(Reader, Current, GroupId);
            }
        }
        else if (Reader.is_header("LOD"))
        {
            ApplyLodHeader(Reader, Current);
        }
        else if (Reader.is_header("Simple"))
        {
            LoadSimpleSection(Reader, bReload, Factory);
        }
        else if (Reader.is_header("Explosion"))
        {
            LoadExplosion(Reader, bReload);
        }
    }

    return (int)(GShipDb.Size() - Saved);
}

int __cdecl Archetype::LoadAsteroids(char const* Path, ICliObjFactory* Factory)
{
    unsigned SavedAsteroids = GAsteroidDb.Size();
    unsigned SavedDynamic = GDynamicAsteroidDb.Size();

    INI_Reader Reader;
    if (!Reader.open(Path, false))
        return 0;

    // FUN_062fa0d0. Any other header is skipped. A record not inserted is deleted, nothing else.
    while (Reader.read_header())
    {
        if (Reader.is_header("Asteroid") || Reader.is_header("AsteroidMine"))
        {
            ICliObj* Companion = nullptr;
            if (Factory != nullptr)
                Factory->Create(4, &Companion);

            Asteroid* Record = new Asteroid(Companion);        // inline ctor in retail
            ReadAsteroidValues(Reader, Record, Companion);
            // "Asteroid" for both headers.
            if (GAsteroidDb.Insert(Record, Record->Nickname, "Asteroid"))
                BindInsertedRecord(Record);
            else
                delete Record;
        }
        else if (Reader.is_header("DynamicAsteroid"))
        {
            ICliObj* Companion = nullptr;
            if (Factory != nullptr)
                Factory->Create(5, &Companion);

            DynamicAsteroid* Record = new DynamicAsteroid(Companion);
            while (Reader.read_value())                        // result ignored
                Record->read(Reader);
            if (GDynamicAsteroidDb.Insert(Record, Record->Nickname, "DynamicAsteroid"))
                BindInsertedRecord(Record);
            else
                delete Record;
        }
    }

    return (int)((GAsteroidDb.Size() - SavedAsteroids) +
                 (GDynamicAsteroidDb.Size() - SavedDynamic));
}

int __cdecl Archetype::LoadSimples(char const* Path, bool bReload, ICliObjFactory* Factory)
{
    // FUN_062f1000. Returns the Simple map growth; anything but [Simple] / [Explosion] is
    // skipped.
    unsigned Saved = GSimpleDb.Size();

    INI_Reader Reader;
    if (Reader.open(Path, false))
    {
        while (Reader.read_header())
        {
            if (Reader.is_header("Simple"))
                LoadSimpleSection(Reader, bReload, Factory);
            else if (Reader.is_header("Explosion"))
                LoadExplosion(Reader, bReload);
        }
    }

    return (int)(GSimpleDb.Size() - Saved);
}

bool __cdecl Archetype::LoadExplosion(INI_Reader& Reader, bool bReload)
{
    // FUN_0630bd60. Only the first "nickname" counts (no zero test on the id); the record is
    // the map node's value whether or not the key was new. An existing key under reload is
    // reset. Every other key, once a record is held, goes to Explosion::read (result ignored).
    Explosion* Live = nullptr;
    while (Reader.read_value())
    {
        if (Reader.is_value("nickname"))
        {
            if (Live != nullptr)
                continue;

            ID_String RecordNickname;
            RecordNickname.Id = CreateID(Reader.get_value_string());
            std::pair<std::map<unsigned, Explosion>::iterator, bool> Inserted =
                GExplosionDb.insert(std::pair<unsigned const, Explosion>(
                    RecordNickname.Id, Explosion(RecordNickname)));
            Live = &Inserted.first->second;

            if (!Inserted.second && bReload)
            {
                Live->reset();
                Live->Nickname = RecordNickname;
            }
        }
        else if (Live != nullptr)
        {
            Live->read(Reader);
        }
    }

    return Live != nullptr;
}

void __cdecl Archetype::Free()
{
    // FUN_062fa980, then FUN_062facf0, FUN_062fabb0 and FUN_062fae00. The explosion map is left
    // to FreeExplosions.
    GSolarDb.ForEach(FreeSolarRecord);
    GSolarDb.Clear();
    GShipDb.ForEach(FreeShipRecord);
    GShipDb.Clear();
    GEquipmentDb.ForEach(FreeEquipmentRecord);
    GEquipmentDb.Clear();
    GMotorDb.ForEach(FreeMotorRecord);
    GMotorDb.Clear();
    Munition::FreeBeamArchIdx();
    GSimpleDb.ForEach(FreeSimpleRecord);
    GSimpleDb.Clear();
    GAsteroidDb.ForEach(FreeAsteroidRecord);
    GDynamicAsteroidDb.ForEach(FreeDynamicAsteroidRecord);
    GAsteroidDb.Clear();
    GDynamicAsteroidDb.Clear();
}

void __cdecl Archetype::FreeExplosions()
{
    // FUN_0630c080 (DAT_063fcf3c).
    GExplosionDb.clear();
}
