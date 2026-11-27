// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Content.dll TU64: the zone enumerator that fills SystemInfo's zones. The rest of TU64 is
// shared STL instantiations (ported as std containers at their use site) and import thunks
// (CommReferrable slots of the SystemInfo vtable).

#include "Universe/SystemInfo.h"

#include "Core/ContentInternal.h"
#include "Core/ContentStaticIds.h"   // TU64 static-init thunks
#include "common_ini.h"
#include "common_server.h"

#include <cstring>

namespace
{

    I3DMathEngine* GetZoneMath()
    {
        return ContentMathEngine();
    }
}

// FUN_06fa7ff0.
bool FSystemZone::GetPathEnds(Vector* OutFirst, Vector* OutSecond)
{
    Vector Size;
    pub::Zone::GetSize(ZoneId, Size);
    Vector const Position = pub::Zone::GetPos(ZoneId);
    Matrix Orientation;
    pub::Zone::GetOrientation(ZoneId, Orientation);

    // The zone transform and (0, y/2, 0); 0.5f = DAT_06fb605c.
    Transform Placement;
    for (int Row = 0; Row < 3; ++Row)
    {
        for (int Column = 0; Column < 3; ++Column)
            Placement.Rotation.E[Row][Column] = Orientation.E[Row][Column];
    }
    Placement.Translation.x = Position.x;
    Placement.Translation.y = Position.y;
    Placement.Translation.z = Position.z;

    Vector Offset;
    Offset.x = 0.0f;
    Offset.y = Size.y * 0.5f;
    Offset.z = 0.0f;
    Vector Transformed;
    GetZoneMath()->TransformXform(Transformed, Placement, Offset);     // slot +0x28
    OutFirst->x = Transformed.x;
    OutFirst->y = Transformed.y;
    OutFirst->z = Transformed.z;

    // (0, y * -0.5f, 0), DAT_06fbd6c4.
    Offset.x = 0.0f;
    Offset.y = Size.y * -0.5f;
    Offset.z = 0.0f;
    GetZoneMath()->TransformXform(Transformed, Placement, Offset);
    OutSecond->x = Transformed.x;
    OutSecond->y = Transformed.y;
    OutSecond->z = Transformed.z;
    return true;
}

// FUN_06fa8120.
void FSystemZone::SetPath(FPatrolPath* InPath)
{
    Path = InPath;
}

// FUN_06fa8130.
FPatrolPath* FSystemZone::GetPath()
{
    return Path;
}

// FUN_06fa7fb0: the result code of GetPopulation is not read.
weighted_vector<unsigned int> const* FSystemZone::GetPopulationFactions()
{
    weighted_vector<unsigned int> const* Factions;
    pub::Zone::GetPopulation(ZoneId, Factions);
    return Factions;
}

// FUN_06fa7fd0.
float FSystemZone::GetTradelaneAttack() const
{
    if (bUnpopulated)
        return 0.0f;
    return Population.TradelaneAttack;
}

// FUN_06fa7e40. VignetteType (+0x14) is not copied, as retail.
FSystemZone& FSystemZone::operator=(FSystemZone const& Other)
{
    ZoneId = Other.ZoneId;
    Nickname = Other.Nickname;
    System = Other.System;
    Path = Other.Path;
    bTradelane = Other.bTradelane;
    bUnpopulated = Other.bUnpopulated;
    bLawful = Other.bLawful;
    bUnlawful = Other.bUnlawful;
    Population = Other.Population;
    return *this;
}

// FUN_06fa77c0.
void FSystemZoneEnumerator::OnZone(unsigned int const* ZoneId, INI_Reader* Reader)
{
    FSystemZone Zone;
    Zone.ZoneId = *ZoneId;
    Zone.Nickname.clear();
    Zone.System = System;

    FZonePathLabel PathLabel;
    PathLabel.Label.clear();

    if (Zone.Parse(Reader, &PathLabel, bLoadEncounters))
    {
        Zones->insert(Zones->end(), 1, Zone);
        if (PathLabels != nullptr && PathLabel.ZoneId != 0)
            PathLabels->insert(PathLabels->end(), 1, PathLabel);
    }
}

namespace
{
    // Append Value unless Indices already holds it.
    void AddUniqueIndex(std::vector<int>* Indices, int Value)
    {
        for (std::vector<int>::iterator It = Indices->begin(); It != Indices->end(); ++It)
        {
            if (*It == Value)
                return;
        }
        Indices->insert(Indices->end(), Value);
    }
}

// FUN_06fa7980.
bool FSystemZone::Parse(INI_Reader* Reader, FZonePathLabel* PathLabel, bool bLoadEncounters)
{
    while (Reader->read_value())
    {
        if (Reader->is_value("nickname"))
        {
            Nickname.Id = CreateID(Reader->get_value_string());
            if (strstr(Reader->get_value_string(), "Tradelane") != nullptr)
            {
                ZoneShape Shape;
                pub::Zone::GetShape(ZoneId, Shape);
                if (Shape == ZONE_SHAPE_BOX)
                {
                    Vector Size;
                    pub::Zone::GetSize(ZoneId, Size);
                    // DAT_06fb4438 = 10.0f; unordered skips. The product stays on the x87
                    // stack (not rounded to float).
                    if (static_cast<double>(Size.x) * 10.0 < static_cast<double>(Size.z) &&
                        static_cast<double>(Size.y) * 10.0 < static_cast<double>(Size.z))
                        bTradelane = true;
                }
            }
            else if (strstr(Reader->get_value_string(), "vignette") != nullptr)
            {
                bLawful = true;
            }
        }
        else if (Reader->is_value("path_label"))
        {
            // Label, then unique path indices from value 1 on.
            PathLabel->ZoneId = ZoneId;
            PathLabel->Label.Id = CreateID(Reader->get_value_string(0));
            AddUniqueIndex(&PathLabel->Indices, Reader->get_value_int(1));
            unsigned int Index = 2;
            while (!Reader->is_value_empty(Index))
            {
                AddUniqueIndex(&PathLabel->Indices, Reader->get_value_int(Index));
                ++Index;
            }
        }
        else if (Reader->is_value("usage"))
        {
            unsigned int Index = 0;
            while (!Reader->is_value_empty(Index))
            {
                char const* Usage = Reader->get_value_string(Index);
                if (_stricmp(Usage, "trade") == 0)
                    PathLabel->Usage |= kPathUsageTrade;
                else if (_stricmp(Usage, "patrol") == 0)
                    PathLabel->Usage |= kPathUsagePatrol;
                ++Index;
            }
        }
        else if (Reader->is_value("mission_eligible"))
        {
            PathLabel->bMissionEligible = Reader->get_value_bool(0);
        }
        else if (Reader->is_value("faction"))
        {
            // Recognised and ignored.
        }
        else if (Reader->is_value("density"))
        {
            Population.Density = Reader->get_value_int(0);
        }
        else if (Reader->is_value("repop_time"))
        {
            // A value <= 0 becomes 1.0 (NaN stays).
            Population.RepopTime = Reader->get_value_float(0);
            if (Population.RepopTime <= 0.0f)
                Population.RepopTime = 1.0f;
        }
        else if (Reader->is_value("mission_type"))
        {
            // bUnlawful is not reset first.
            bLawful = false;
            unsigned int Index = 0;
            while (!Reader->is_value_empty(Index))
            {
                if (_stricmp(Reader->get_value_string(Index), "unlawful") == 0)
                    bUnlawful = true;
                else if (_stricmp(Reader->get_value_string(Index), "lawful") == 0)
                    bLawful = true;
                ++Index;
            }
            if (!bUnlawful && !bLawful)
                bLawful = true;
        }
        else if (Reader->is_value("vignette_type"))
        {
            if (_stricmp(Reader->get_value_string(0), "field") == 0)
                VignetteType = kVignetteZoneField;
            else if (_stricmp(Reader->get_value_string(0), "exclusion") == 0)
                VignetteType = kVignetteZoneExclusion;
            else
                VignetteType = kVignetteZoneOpen;
        }
        else if (bLoadEncounters)
        {
            // The return value is not used.
            Population.ParseValue(Reader, System, this);
        }
    }

    if (bLoadEncounters)
        bUnpopulated = !Population.bHasPopulationKeys && PathLabel->ZoneId == 0;

    if (bUnpopulated && PathLabel->ZoneId == 0 && !bLawful && !bUnlawful)
        return false;
    return true;
}
