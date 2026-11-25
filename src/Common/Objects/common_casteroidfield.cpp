// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// CmnAsteroid::CAsteroidField (retail cluster 0x062a3270-0x062a6100). Declared in
// common_casteroid.h.
#include "common_casteroid.h"
#include "common_ini.h"
#include "common_id.h"
#include "common_archetype.h"
#include "dacom.h"
#include "common_iface.h" // IObjInspect
#include "common_cobjdb.h"
#include "common_universe.h" // Universe::get_zone / get_non_const_zone
#include "x87_trig.h"        // retail's inline FSIN / FCOS (CAsteroidField::load)

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <tuple>
#include <utility>

namespace CmnAsteroid
{

namespace
{
    // .data 063eb7fc / 063eb7fd: byte flags initialised to 1 and never written in the module;
    // they gate populate_asteroids' grid and look-ahead candidates. 063eb7f0
    // (GAsteroidObserverScan) is in common_casteroid.cpp.
    bool GAsteroidGridSearch = true;
    bool GAsteroidAheadSearch = true;
}

// ============================================================================
// CAsteroidField implementation
// ============================================================================

CAsteroidField::CAsteroidField()
    : RefCount(0)
    , Zone(nullptr)
    , Changes(nullptr)
    , ExclusionMgr(nullptr)
    , SystemId(0)
    , CubeSize(0xfa)
    , FillDist(1000)
    , EmptyFreq(0.0f)
{
    // FUN_062a4660. ArchList +0x00, RefCount +0x10, cube map +0x14, Zone / Changes /
    // ExclusionMgr +0x28 / +0x2c / +0x30, LootZones +0x34. Deviation: retail leaves SystemId
    // (+0x40) uninitialised (create_field sets it).
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Rotations.Angles[Axis][0] = 0.0f;
        Rotations.Angles[Axis][1] = 1.5707963f;
        Rotations.Angles[Axis][2] = 3.14159265f;
        Rotations.Angles[Axis][3] = 4.71238898f;
    }
}

CAsteroidField::~CAsteroidField()
{
    // FUN_062a47a0, in retail order; then the member destructors.
    Cubes.clear();
    LootZones.clear();
    flush_changes();
    if (ExclusionMgr != nullptr)
        delete ExclusionMgr;
}

void CAsteroidField::load(INI_Reader& Reader, int CubeSizeParm)
{
    // FUN_062a4940. The second parameter is the cube size (same stores as set_cube_size).
    CubeSize = CubeSizeParm;
    ArchCubeSize = CubeSizeParm;

    static const float kDegToRad = 0.017453292f;

    if (!Reader.read_header())
        return;

    do
    {
        if (Reader.is_header("Cube"))
        {
            if (Reader.read_value())
            {
                do
                {
                    // The unrounded value times DAT_0639d210, one rounding to float.
                    if (Reader.is_value("xaxis_rotation"))
                    {
                        for (unsigned Index = 0; Index < 4; ++Index)
                            Rotations.Angles[0][Index] = (float)(Reader.value_num(Index) * kDegToRad);
                    }
                    else if (Reader.is_value("yaxis_rotation"))
                    {
                        for (unsigned Index = 0; Index < 4; ++Index)
                            Rotations.Angles[1][Index] = (float)(Reader.value_num(Index) * kDegToRad);
                    }
                    else if (Reader.is_value("zaxis_rotation"))
                    {
                        for (unsigned Index = 0; Index < 4; ++Index)
                            Rotations.Angles[2][Index] = (float)(Reader.value_num(Index) * kDegToRad);
                    }
                    else if (Reader.is_value("asteroid"))
                    {
                        // The archetype is used without a null test.
                        ID_String NameId;
                        NameId.Id = CreateID(Reader.get_value_string(0));
                        Archetype::Asteroid* const Arch =
                            Archetype::GetAsteroid(Archetype::GetAsteroidID(NameId));
                        Arch->load();
                        if (Arch->Companion != nullptr)
                            Archetype::CliObjLoad(Arch->Companion, 0);

                        // See the entry's comment in common_casteroid.h for the release that
                        // balances Root::load.
                        ArchList.push_back(AsteroidCubeArchEntry());
                        AsteroidCubeArchEntry& Entry = ArchList.back();
                        Entry.Arch = Arch;

                        // Values 1 and 2 are rounded to float, value 3 stays unrounded until
                        // after its clamp to [-1, 1] (unordered keeps the value). Times
                        // CubeSize * 0.5 (double DAT_0639df88), one rounding.
                        float ExtentX = Reader.get_value_float(1);
                        float ExtentY = Reader.get_value_float(2);
                        double ExtentZ = Reader.value_num(3);
                        if (ExtentX < -1.0f)
                            ExtentX = -1.0f;
                        else if (ExtentX > 1.0f)
                            ExtentX = 1.0f;
                        if (ExtentY < -1.0f)
                            ExtentY = -1.0f;
                        else if (ExtentY > 1.0f)
                            ExtentY = 1.0f;
                        if (ExtentZ < -1.0)
                            ExtentZ = -1.0;
                        else if (ExtentZ > 1.0)
                            ExtentZ = 1.0;
                        double const HalfCube = (double)ArchCubeSize * 0.5;
                        Entry.Extent.x = (float)(HalfCube * ExtentX);
                        Entry.Extent.y = (float)(HalfCube * ExtentY);
                        Entry.Extent.z = (float)(HalfCube * (float)ExtentZ);

                        // Retail reads them in the order 5, 4, 6.
                        float AngleX = 0.0f;
                        float AngleY = 0.0f;
                        float AngleZ = 0.0f;
                        if (Reader.is_value_empty(4) || Reader.is_value_empty(5) ||
                            Reader.is_value_empty(6))
                        {
                            FDUMP(kFdumpError, "%s(%d) : *** ERROR: %s",
                                  "E:\\FL\\Scratch\\Source\\Common\\CAsteroidSystem.cpp", 0x167,
                                  "invalid rotation specified for an asteroid");
                        }
                        else
                        {
                            AngleY = Reader.get_value_float(5);
                            AngleX = Reader.get_value_float(4);
                            AngleZ = Reader.get_value_float(6);
                        }

                        // Identity, then three row rotations. cos is rounded to float, sin
                        // and R stay unrounded.
                        Matrix& EntryOrient = Entry.Orient;
                        for (int Row = 0; Row < 3; ++Row)
                            for (int Col = 0; Col < 3; ++Col)
                                EntryOrient.E[Row][Col] = (Row == Col) ? 1.0f : 0.0f;
                        {
                            // Rows 1 and 2 by AngleX.
                            double const R = (double)AngleX * kDegToRad;
                            double const C = X87CosF(R);
                            double const S = X87Sin(R);
                            for (int Col = 0; Col < 3; ++Col)
                            {
                                double const A = EntryOrient.E[1][Col];
                                double const B = EntryOrient.E[2][Col];
                                EntryOrient.E[1][Col] = (float)(C * A - S * B);
                                EntryOrient.E[2][Col] = (float)(A * S + C * B);
                            }
                        }
                        {
                            // Rows 0 and 2 by AngleY.
                            double const R = (double)AngleY * kDegToRad;
                            double const C = X87CosF(R);
                            double const S = X87Sin(R);
                            for (int Col = 0; Col < 3; ++Col)
                            {
                                double const A = EntryOrient.E[0][Col];
                                double const B = EntryOrient.E[2][Col];
                                EntryOrient.E[0][Col] = (float)(C * A + S * B);
                                EntryOrient.E[2][Col] = (float)(C * B - A * S);
                            }
                        }
                        {
                            // Rows 0 and 1 by AngleZ.
                            double const R = (double)AngleZ * kDegToRad;
                            double const C = X87CosF(R);
                            double const S = X87Sin(R);
                            for (int Col = 0; Col < 3; ++Col)
                            {
                                double const A = EntryOrient.E[0][Col];
                                double const B = EntryOrient.E[1][Col];
                                EntryOrient.E[0][Col] = (float)(C * A - S * B);
                                EntryOrient.E[1][Col] = (float)(A * S + C * B);
                            }
                        }

                        Entry.bMine = false;
                        Entry.bInstantKill = false;
                        if (!Reader.is_value_empty(7))
                        {
                            char const* Flag = Reader.get_value_string(7);
                            if (std::strcmp(Flag, "mine") == 0)
                                Entry.bMine = true;
                            else if (std::strcmp(Flag, "instant_kill") == 0)
                                Entry.bInstantKill = true;
                        }
                    }
                } while (Reader.read_value());
            }
        }
        else if (Reader.is_header("Exclusion Zones"))
        {
            if (ExclusionMgr == nullptr)
                ExclusionMgr = new Exclusion::AsteroidExclusionZoneMgr();

            if (Reader.read_value())
            {
                do
                {
                    if (Reader.is_value("exclusion"))
                    {
                        // Retail calls the const get_zone overload here (get_non_const_zone is
                        // used for the LootableZone "zone" key below).
                        char const* Name = Reader.get_value_string(0);
                        Universe::IZone const* Found = Universe::get_zone(SystemId, Name);
                        if (Found == nullptr)
                        {
                            FDUMP(kFdumpError,
                                  "%s(%d) : *** ERROR: AsteroidField linked to missing Exclusion Zone (%s) in file named (%s)",
                                  "E:\\FL\\Scratch\\Source\\Common\\CAsteroidSystem.cpp", 0x18c, Name,
                                  Reader.get_file_name());
                        }
                        else
                        {
                            ExclusionMgr->push_back(Found, Zone);
                        }
                    }
                    else if (Reader.is_value("exclude_billboards"))
                    {
                        // ftol of the unrounded value; back()+0x08.
                        int const Count = (int)Reader.value_num(0);
                        if (Count > 0)
                            ExclusionMgr->back()->bExcludeBillboards = 1;
                    }
                    else if (Reader.is_value("billboard_count"))
                    {
                        // back()+0x10
                        int const Count = (int)Reader.value_num(0);
                        if (Count > 0)
                            ExclusionMgr->back()->BillboardCount = (float)Count;
                    }
                    else if (Reader.is_value("exclude_dynamic_asteroids"))
                    {
                        // back()+0x09, only for sphere or ellipsoid zones. No null tests.
                        int const Count = (int)Reader.value_num(0);
                        if (Count > 0)
                        {
                            Exclusion::AsteroidExclusionZone* const Back = ExclusionMgr->back();
                            unsigned const Shape = Back->ZonePtr->GetShape();
                            if (Shape == Universe::ZoneShapeSphere || Shape == Universe::ZoneShapeEllipsoid)
                                Back->bExcludeDynamicAsteroids = 1;
                        }
                    }
                    else if (Reader.is_value("empty_cube_frequency"))
                    {
                        // The unrounded value is tested; below 0 or unordered is skipped.
                        // back()+0x0c.
                        double const Freq = Reader.value_num(0);
                        if (Freq >= 0.0)
                            ExclusionMgr->back()->EmptyCubeFrequency = (float)Freq;
                    }
                } while (Reader.read_value());
            }
        }
        else if (Reader.is_header("LootableZone"))
        {
            // The entry (operator new(0x28)) is allocated before the value loop and handed to
            // the zone below, whose SetLootableInfo frees the previous one with free(), hence
            // malloc. Retail quirks kept: an entry no zone takes leaks, and a failed allocation
            // crashes on the later stores.
            LootableZoneEntry* const Entry = (LootableZoneEntry*)std::malloc(sizeof(LootableZoneEntry));
            if (Entry != nullptr)
            {
                Entry->AsteroidLootContainerId.Id = 0;
                Entry->DynamicLootContainerId.Id = 0;
                Entry->AsteroidLootCommodityId.Id = 0;
                Entry->DynamicLootCommodityId.Id = 0;
                Entry->AsteroidLootCountMin = 1;
                Entry->AsteroidLootCountMax = 1;
                Entry->DynamicLootCountMin = 1;
                Entry->DynamicLootCountMax = 1;
                Entry->AsteroidLootDifficulty = 100;
                Entry->DynamicLootDifficulty = 1;
            }

            bool bHasDynamicLoot = false;
            Universe::IZone* NamedZone = nullptr;

            if (Reader.read_value())
            {
                do
                {
                    if (Reader.is_value("dynamic_loot_container"))
                    {
                        Entry->DynamicLootContainerId.Id = CreateID(Reader.get_value_string(0));
                        bHasDynamicLoot = true;
                    }
                    else if (Reader.is_value("dynamic_loot_commodity"))
                    {
                        Entry->DynamicLootCommodityId.Id = CreateID(Reader.get_value_string(0));
                        bHasDynamicLoot = true;
                    }
                    else if (Reader.is_value("zone"))
                    {
                        // A later "zone" line overwrites it, null included.
                        NamedZone = Universe::get_non_const_zone(SystemId, Reader.get_value_string(0));
                    }
                    else if (Reader.is_value("dynamic_loot_count"))
                    {
                        // +0x18 / +0x1c
                        int Min = (int)Reader.value_num(0);
                        int Max = (int)Reader.value_num(1);
                        if (Min < 1)
                            Min = 1;
                        if (Max < 1)
                            Max = 1;
                        if (Max < Min)
                            Min = Max;
                        Entry->DynamicLootCountMin = Min;
                        Entry->DynamicLootCountMax = Max;
                    }
                    else if (Reader.is_value("dynamic_loot_difficulty"))
                    {
                        // +0x24
                        int Difficulty = (int)Reader.value_num(0);
                        if (Difficulty < 1)
                            Difficulty = 1;
                        else if (Difficulty > 100)
                            Difficulty = 100;
                        Entry->DynamicLootDifficulty = Difficulty;
                    }
                    // Retail also tests "asteroid_loot_container" / "_commodity" / "_count" /
                    // "_difficulty" but stores nothing for them (see LootableZoneEntry).
                } while (Reader.read_value());

                if (bHasDynamicLoot)
                {
                    if (NamedZone != nullptr)
                    {
                        NamedZone->SetLootableInfo(Entry);   // slot +0x8c
                        LootZones.push_back(NamedZone);
                    }
                    else if (Zone != nullptr)
                    {
                        // The field's own zone (+0x28) takes it; not added to LootZones.
                        Zone->SetLootableInfo(Entry);
                    }
                    else
                    {
                        FDUMP(kFdumpError,
                              "%s(%d) : *** ERROR: loot zone information attached to asteroid field (%s) that has no zone",
                              "E:\\FL\\Scratch\\Source\\Common\\CAsteroidSystem.cpp", 0x246,
                              Reader.get_file_name());
                    }
                }
            }
        }
    } while (Reader.read_header());
}

namespace
{
    // The nudge-then-ftol idiom (see NudgeTruncate in common_casteroid.cpp). Takes double
    // because populate_asteroids nudges one coordinate that is still unrounded on the x87 stack.
    inline int TruncWithEpsilon(double V)
    {
        double const Nudged = (V > 0.0) ? V + (double)0.1f : V - (double)0.1f;
        return (int)(long long)Nudged;
    }
}

unsigned long CAsteroidField::create_cube_id(Vector const& Pos)
{
    // FUN_062a3270. The +/-0.1 nudge guards exact grid boundaries; it is not a round-to-nearest.
    int const HalfSize = (int)(long long)(((double)CubeSize + 1.0) * 0.5);

    Vector Snapped;
    Snapped.x = (float)closest_cube_coord(TruncWithEpsilon(Pos.x), CubeSize, HalfSize);
    Snapped.y = (float)closest_cube_coord(TruncWithEpsilon(Pos.y), CubeSize, HalfSize);
    Snapped.z = (float)closest_cube_coord(TruncWithEpsilon(Pos.z), CubeSize, HalfSize);
    return compute_cube_id(Snapped);
}

Vector CAsteroidField::closest_cube_pos(Vector const& Pos)
{
    // FUN_062a5ee0. create_cube_id's snap without the hash.
    int const HalfSize = (int)(long long)(((double)CubeSize + 1.0) * 0.5);

    Vector Snapped;
    Snapped.x = (float)closest_cube_coord(TruncWithEpsilon(Pos.x), CubeSize, HalfSize);
    Snapped.y = (float)closest_cube_coord(TruncWithEpsilon(Pos.y), CubeSize, HalfSize);
    Snapped.z = (float)closest_cube_coord(TruncWithEpsilon(Pos.z), CubeSize, HalfSize);
    return Snapped;
}

void CAsteroidField::update(float Dt)
{
    // FUN_062a52a0. Far only when StateTimer is strictly above 3.0f (DAT_0639dfd4); unordered
    // is Near.
    for (std::map<unsigned long, CAsteroidCube>::iterator It = Cubes.begin(); It != Cubes.end(); ++It)
    {
        CAsteroidCube& Cube = It->second;
        if (Cube.State == CubeStateUnknown)
        {
            Cube.StateTimer += Dt;
            Cube.State = (Cube.StateTimer > 3.0f) ? CubeStateFar : CubeStateNear;
        }
    }
}

float CAsteroidField::get_immersion(Vector const& Pos)
{
    // FUN_062a53a0. Tail jump into IZoneImmersion::GetImmersion (slot 0).
    if (Zone == nullptr)
        return 1.0f;
    return Zone->GetImmersion(Pos);
}

bool CAsteroidField::near_field(Vector const& Pos)
{
    // FUN_062a53c0
    if (Zone == nullptr)
        return true;

    float const Margin = (float)((double)(CubeSize + FillDist) * (double)1.74f);   // DAT_0639dfdc

    Vector const& Center = Zone->GetPos();
    Vector Delta;
    Delta.x = Pos.x - Center.x;
    Delta.y = Pos.y - Center.y;
    Delta.z = Pos.z - Center.z;

    // DistSq (sum order x, z, y) is stored to a float but compared unrounded.
    double const Reach = (double)Zone->GetRadius() + (double)Margin;
    double const DistSqWide = (double)Delta.x * Delta.x + (double)Delta.z * Delta.z +
                              (double)Delta.y * Delta.y;
    float const DistSq = (float)DistSqWide;
    if (DistSqWide > Reach * Reach)
        return false;

    // The root is compared unrounded with 1e-4f (DAT_0639dfd8; unordered gives 0 excess), but
    // Dir uses the float Dist. GetRadiusAlong is slot +0x40.
    double const DistWide = std::sqrt((double)DistSq);
    float const Dist = (float)DistWide;
    double Excess;
    if (DistWide > (double)0.0001f)
    {
        double const InvDist = 1.0 / (double)Dist;
        Vector Dir;
        Dir.x = (float)(Delta.x * InvDist);
        Dir.y = (float)(Delta.y * InvDist);
        Dir.z = (float)(Delta.z * InvDist);
        Excess = (double)Dist - (double)Zone->GetRadiusAlong(Dir);
    }
    else
    {
        Excess = 0.0;
    }

    // Unordered is false.
    return Excess <= (double)Margin;
}

float CAsteroidField::near_field_fraction(Vector const& Pos, float Dist)
{
    // FUN_062a54e0
    if (Zone == nullptr)
        return 1.0f;

    // Non-positive or unordered Dist takes the default (DAT_0639dfe0 = 1.2f).
    if (!(Dist > 0.0f))
        Dist = (float)((double)(FillDist + CubeSize) * (double)1.2f);

    Vector const& Center = Zone->GetPos();
    Vector Delta;
    Delta.x = Pos.x - Center.x;
    Delta.y = Pos.y - Center.y;
    Delta.z = Pos.z - Center.z;

    double const Reach = (double)Zone->GetRadius() + (double)Dist;
    double const DistSqWide = (double)Delta.x * Delta.x + (double)Delta.z * Delta.z +
                              (double)Delta.y * Delta.y;
    float const DistSq = (float)DistSqWide;
    if (DistSqWide > Reach * Reach)
        return 0.0f;

    // Unlike near_field: no small-length test, and the reciprocal is of the unrounded root.
    double const LenWide = std::sqrt((double)DistSq);
    float const Len = (float)LenWide;
    double const InvLen = 1.0 / LenWide;
    Vector Dir;
    Dir.x = (float)(Delta.x * InvLen);
    Dir.y = (float)(Delta.y * InvLen);
    Dir.z = (float)(Delta.z * InvLen);
    double const Excess = (double)Len - (double)Zone->GetRadiusAlong(Dir);

    // Unordered gives 0. Retail leaves the result unrounded in ST0; its only consumer
    // (freelancer.exe 0052241f) stores it as a float at once.
    if (!(Excess <= (double)Dist))
        return 0.0f;
    return (float)(1.0 - Excess / (double)Dist);
}

void CAsteroidField::flush_changes()
{
    // FUN_062a5600. Far cubes are erased (FUN_062a6c60), the others reset to Unknown; then the
    // cached marker chain (+0x2c) is freed (recursive dtor FUN_062a56e0).
    for (std::map<unsigned long, CAsteroidCube>::iterator It = Cubes.begin(); It != Cubes.end();)
    {
        if (It->second.State == CubeStateFar)
        {
            It = Cubes.erase(It);
        }
        else
        {
            It->second.State = CubeStateUnknown;
            ++It;
        }
    }
    if (Changes != nullptr)
        delete Changes;
    Changes = nullptr;
}

asteroid_marker* CAsteroidField::get_changes()
{
    // FUN_062a5300. Builds and caches (+0x2c) the concatenation of every cube's markers in tree
    // order.
    if (Changes != nullptr)
        return Changes;

    asteroid_marker* Tail = nullptr;
    for (std::map<unsigned long, CAsteroidCube>::iterator It = Cubes.begin(); It != Cubes.end(); ++It)
    {
        asteroid_marker* CubeChanges = It->second.get_changes();
        if (CubeChanges == nullptr)
            continue;
        if (Changes == nullptr)
        {
            Changes = CubeChanges;
            Tail = CubeChanges;
        }
        else
        {
            while (Tail->Next != nullptr)
                Tail = Tail->Next;
            Tail->Next = CubeChanges;
        }
    }
    return Changes;
}

CAsteroidCube* CAsteroidField::find_cube(unsigned long CubeIdValue)
{
    // FUN_062a6100
    std::map<unsigned long, CAsteroidCube>::iterator It = Cubes.find(CubeIdValue);
    if (It == Cubes.end())
        return nullptr;
    return &It->second;
}

void CAsteroidField::populate_asteroids_internal(Vector const& Pos, Vector const& Candidate, float DistSqThreshold)
{
    // FUN_062a5ab0 (no Ghidra .c). An exclusion zone's "empty_cube_frequency" (returned through
    // is_excluded's out value) overrides EmptyFreq when it is >= 0 (NaN does not). An existing
    // cube that is not Active/Near is marked Near when within the threshold; a new cube is
    // created when within the threshold and the zone immersion is nonzero.
    float FreqOverride = EmptyFreq;
    bool bNotExcluded = true;
    if (ExclusionMgr != nullptr)
    {
        float OutDist = -1.0f;
        bNotExcluded = !ExclusionMgr->is_excluded(Candidate, Exclusion::EOT_CmnAsteroid, &OutDist);
        if (bNotExcluded && OutDist >= 0.0f)
            FreqOverride = OutDist;
    }

    // The cube id is computed before either bail-out is tested.
    unsigned long const CubeIdValue = compute_cube_id(Candidate);
    if (CubeIdValue == 0)
        return;
    if (!bNotExcluded)
        return;

    CAsteroidCube* Existing = find_cube(CubeIdValue);
    if (Existing != nullptr)
    {
        if (Existing->State == CubeStateActive || Existing->State == CubeStateNear)
            return;

        // FUN_06288800 rounds each component to float; the sum (z, y, x order) is not rounded.
        Vector Delta;
        Delta.x = Pos.x - Candidate.x;
        Delta.y = Pos.y - Candidate.y;
        Delta.z = Pos.z - Candidate.z;
        double DistSq = (double)Delta.z * Delta.z + (double)Delta.y * Delta.y;
        DistSq += (double)Delta.x * Delta.x;
        // Strictly below; NaN leaves the cube alone.
        if (DistSq < (double)DistSqThreshold)
        {
            Existing->State = CubeStateNear;
            Existing->StateTimer = 0.0f;
        }
        return;
    }

    Vector Delta;
    Delta.x = Pos.x - Candidate.x;
    Delta.y = Pos.y - Candidate.y;
    Delta.z = Pos.z - Candidate.z;
    double DistSq = (double)Delta.z * Delta.z + (double)Delta.y * Delta.y;
    DistSq += (double)Delta.x * Delta.x;
    if (!(DistSq < (double)DistSqThreshold))
        return;

    // No null test on Zone. Only an exact 0.0f bails; NaN builds the cube.
    if (Zone->GetImmersion(Candidate) == 0.0f)
        return;

    // Retail builds a temporary cube, copies it into a (CubeId, cube) pair (copy ctor
    // FUN_062a3070) and inserts that (FUN_062a6a50); building in place gives the same node.
    CAsteroidCube& NewCube = Cubes.emplace(std::piecewise_construct,
        std::forward_as_tuple(CubeIdValue), std::forward_as_tuple(this)).first->second;
    NewCube.State = CubeStateActive;
    NewCube.Size = CubeSize;
    NewCube.SystemId = SystemId;
    NewCube.init(Candidate, CubeIdValue, this, Rotations, FreqOverride);
}

void CAsteroidField::populate_asteroids(Vector const& Pos, Vector const& Dir)
{
    // FUN_062a5710. Pos and Dir are an observer's position and negated forward axis. Populates
    // the 2x2x2 grid cubes around Pos, then one cube ahead along Dir. The grid threshold uses
    // 0.99 (double DAT_0639dfe8).
    Vector const Grid = closest_cube_pos(Pos);
    double const ScaledSize = (double)CubeSize * 0.99;
    float const DistSqThresholdGrid = (float)(ScaledSize * ScaledSize);

    if (GAsteroidGridSearch)                                     // DAT_063eb7fc
    {
        // Unordered gives -CubeSize.
        int const OffsetX = (Pos.x > Grid.x) ? CubeSize : -CubeSize;
        int const OffsetY = (Pos.y > Grid.y) ? CubeSize : -CubeSize;
        int const OffsetZ = (Pos.z > Grid.z) ? CubeSize : -CubeSize;

        // z outer, y middle, x inner (retail order).
        for (int Cz = 0; Cz < 2; ++Cz)
        {
            for (int Cy = 0; Cy < 2; ++Cy)
            {
                for (int Cx = 0; Cx < 2; ++Cx)
                {
                    Vector Candidate;
                    Candidate.x = (Cx != 0) ? (float)((double)OffsetX + Grid.x) : Grid.x;
                    Candidate.y = (Cy != 0) ? (float)((double)OffsetY + Grid.y) : Grid.y;
                    Candidate.z = (Cz != 0) ? (float)((double)OffsetZ + Grid.z) : Grid.z;
                    populate_asteroids_internal(Pos, Candidate, DistSqThresholdGrid);
                }
            }
        }
    }

    if (GAsteroidAheadSearch)                                    // DAT_063eb7fd
    {
        // x87 rounding differs per axis, as in retail: x rounds once, y keeps the final sum
        // unrounded, z rounds every step. 1.25f is DAT_0639dfe4.
        double const C = (double)CubeSize;
        float const AheadX = (float)(C * Dir.x * 1.25 + Pos.x);
        float const ScaledY = (float)(C * Dir.y);
        double const AheadYWide = (double)ScaledY * 1.25 + Pos.y;
        float const ScaledZ = (float)(C * Dir.z);
        float const ScaledZ125 = (float)((double)ScaledZ * 1.25);
        float const AheadZ = (float)((double)ScaledZ125 + Pos.z);

        int const SnapX = TruncWithEpsilon(AheadX);
        int const SnapY = TruncWithEpsilon(AheadYWide);
        int const SnapZ = TruncWithEpsilon(AheadZ);
        int const HalfSize = (int)(long long)((C + 1.0) * 0.5);
        Vector Candidate;
        Candidate.x = (float)closest_cube_coord(SnapX, CubeSize, HalfSize);
        Candidate.y = (float)closest_cube_coord(SnapY, CubeSize, HalfSize);
        Candidate.z = (float)closest_cube_coord(SnapZ, CubeSize, HalfSize);

        populate_asteroids_internal(Pos, Candidate, 1000000.0f);
    }
}

int CAsteroidField::get_cube_size()
{
    // FUN_062a33b0
    return CubeSize;
}

void CAsteroidField::set_system(unsigned SystemIdValue)
{
    // FUN_062a33e0 (+0x40)
    SystemId = SystemIdValue;
}

AxisRotations& CAsteroidField::get_axis_rotations()
{
    // FUN_0629d990 (+0x50)
    return Rotations;
}

Exclusion::AsteroidExclusionZoneMgr* CAsteroidField::get_exclusion_mgr()
{
    // FUN_063275a0 (+0x30)
    return ExclusionMgr;
}

Universe::IZone const* CAsteroidField::get_lootable_zone(Vector const& Pos) const
{
    // FUN_062a5e80. The first LootZones entry (+0x38) with nonzero immersion at Pos.
    for (std::list<Universe::IZone*>::const_iterator It = LootZones.begin();
         It != LootZones.end(); ++It)
    {
        if ((*It)->GetImmersion(Pos) != 0.0f)
            return *It;
    }

    // Zone slot 0x7c (FUN_06262dd0, +0x84) is the loot entry installed by slot 0x8c. Retail
    // dereferences Zone unconditionally.
    if (Zone->GetLootableInfo() != nullptr)
        return Zone;
    return nullptr;
}

void CAsteroidField::set_cube_size(int Size)
{
    // FUN_062a33a0 (+0x44 and +0x0c)
    CubeSize = Size;
    ArchCubeSize = Size;
}

void CAsteroidField::set_fill_dist(int Dist)
{
    // FUN_062a33c0
    FillDist = Dist;
}

void CAsteroidField::set_empty_freq(float Freq)
{
    // FUN_062a33d0
    EmptyFreq = Freq;
}

void CAsteroidField::set_zone(Universe::IZone const* InZone)
{
    // FUN_0633bae0 (ICF-folded with CECargo::SetCount), +0x28
    Zone = const_cast<Universe::IZone*>(InZone);
}

int CAsteroidField::AddRef()
{
    // FUN_062a5e40
    return ++RefCount;
}

int CAsteroidField::Release()
{
    // FUN_062a5e50
    int NewCount = --RefCount;
    if (NewCount == 0)
        delete this;
    return NewCount;
}

}
