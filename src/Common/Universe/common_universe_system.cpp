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

// ============================================================================
// SYSTEM -- Universe::ISystem's implementation (vftable .rdata:063a5144)
// ============================================================================

namespace CmnUniverse
{
    // The zone slots SYSTEM calls go through Universe::IZone's vtable (common_universe.h):
    //   zone vt+0x10 GetPos, vt+0x1c GetProperties, vt+0x30 GetRadius,
    //   vt+0x34 InZone (SYSTEM slots 10-12 always pass Margin 0), vt+0x54 HasPhysicalEffect.

    // 0x60ad78ec, the starting "best radius" of slots 10-12.
    float const kNoZoneRadius = 1.0e20f;

    // Inlined in FUN_06337250 (the stack SYSTEM, .text:06337376).
    SYSTEM::SYSTEM()
        : Spatial(nullptr)
        , SpaceMusicId(0)
    {
        NicknameId = 0;         // +0x4c
        reinterpret_cast<CacheString*>(&Nickname)->clear(); // +0x50
        reinterpret_cast<CacheString*>(&File)->clear();     // +0x70
        StridName = 0;          // +0x68
        IdsInfo = 0;            // +0x6c
        Visit = 0;              // +0x64
        NavMapScale = 1.0f;     // +0x90
        // +0x74 (pos) is not written by retail's construction; LoadUniverseIni gives the stack
        // SYSTEM the previous block's position (FUniverseLoadCarry).
    }

    // FUN_0633aee0
    SYSTEM::~SYSTEM()
    {
        if (Spatial != nullptr)
        {
            FreeSpatial(Spatial);
            Spatial = nullptr;
        }
    }

    // Slot 0, 062969c0 (ICF-folded getter).
    unsigned SYSTEM::GetId() const
    {
        return NicknameId;
    }

    // Slot 1, 0633afa0: a this-adjusting thunk.
    TString<64> const& SYSTEM::GetMsgIdPrefix() const
    {
        return CommReferrable::GetMsgIdPrefix();
    }

    // Slot 2, 063390f0 (ICF-folded getter).
    char const* SYSTEM::GetNickname() const
    {
        return Nickname;
    }

    // Slot 3, 0633aff0
    int SYSTEM::GetStridName() const
    {
        return StridName;
    }

    // Slot 4, 0633b000
    int SYSTEM::GetIdsInfo() const
    {
        return IdsInfo;
    }

    // Slot 5, 0629ba60 (ICF-folded getter).
    char const* SYSTEM::GetFile() const
    {
        return File;
    }

    // Slot 6, 0633b020
    std::list<Universe::FSystemZone> const* SYSTEM::GetZones() const
    {
        return &Zones;
    }

    ISpatialPartition* SYSTEM::BuildZoneTree() const
    {
        if (Spatial == nullptr)
        {
            Spatial = NewSpatial("BSP");
            std::list<Universe::FSystemZone>::const_iterator It = Zones.begin();
            std::list<Universe::FSystemZone>::const_iterator End = Zones.end();
            for (; It != End; ++It)
            {
                Universe::IZone const* Zone = It->Zone;
                FSpatialEntry Entry;
                Entry.Radius = Zone->GetRadius();               // radius first, as retail
                Entry.Position = Zone->GetPos();
                Entry.Object = const_cast<Universe::IZone*>(Zone);
                SpatialAddObject(Spatial, &Entry);
            }
        }
        return Spatial;
    }

    // Slot 7, 0633b030: build the zone tree on first use and return it.
    ISpatialPartition* SYSTEM::GetZoneTree() const
    {
        return BuildZoneTree();
    }

    // Slot 8, 0633b0c0: property flags at a sphere. Zones whose flags carry any of 0x7fc0 are
    // exclusive -- only the smallest-radius one of them counts (ties keep the first); all other
    // zones' flags are OR-ed together.
    unsigned SYSTEM::GetSphereProperties(Geometry::Sphere const* Query) const
    {
        ISpatialPartition* Tree = BuildZoneTree();
        FSpatialQueryResult Result;
        Result.Capacity = 0x800;
        Result.Count = 0;
        SpatialFill(Tree, Query, &Result);

        unsigned Combined = 0;
        unsigned ExclusiveFlags = 0;
        float ExclusiveRadius = 0.0f;              // only read once ExclusiveFlags != 0
        unsigned Index = 0;
        for (; Index < Result.Count; ++Index)
        {
            Universe::IZone const* Zone = static_cast<Universe::IZone const*>(Result.Items[Index]);
            if (SphereCull(*Query, Zone))
                continue;
            unsigned Flags = Zone->GetProperties();
            if ((Flags & Universe::kZonePropObjectZone) == 0)
            {
                Combined |= Flags;
                continue;
            }
            if (ExclusiveFlags == 0)
            {
                ExclusiveFlags = Flags;
                ExclusiveRadius = Zone->GetRadius();
            }
            else if (Zone->GetRadius() < ExclusiveRadius)
            {
                ExclusiveFlags = Flags;
                ExclusiveRadius = Zone->GetRadius();
            }
        }
        return Combined | ExclusiveFlags;
    }

    // Slot 9, 0633b230
    unsigned SYSTEM::GetNestedProperties(Geometry::Sphere const* Query) const
    {
        ISpatialPartition* Tree = BuildZoneTree();
        FSpatialQueryResult Result;
        Result.Capacity = 0x800;
        Result.Count = 0;
        SpatialFill(Tree, Query, &Result);

        unsigned Combined = 0;
        unsigned Index = 0;
        for (; Index < Result.Count; ++Index)
        {
            Universe::IZone const* Zone = static_cast<Universe::IZone const*>(Result.Items[Index]);
            if (!SphereCull(*Query, Zone))
                Combined |= Zone->GetProperties();
        }
        return Combined;
    }

    // Slot 10, 0633b350: the smallest-radius zone containing Point.
    Universe::IZone const* SYSTEM::GetSmallestZoneAt(Vector const* Point) const
    {
        BuildZoneTree();
        if (Spatial == nullptr)
            return nullptr;

        Geometry::Sphere Query;
        Query.Center = *Point;
        Query.Radius = 0.1f;
        FSpatialQueryResult Result;
        Result.Capacity = 0x800;
        Result.Count = 0;
        if (SpatialFill(Spatial, &Query, &Result) == 0)
            return nullptr;

        Universe::IZone const* Best = nullptr;
        float BestRadius = kNoZoneRadius;
        unsigned Index = 0;
        for (; Index < Result.Count; ++Index)
        {
            Universe::IZone const* Zone = static_cast<Universe::IZone const*>(Result.Items[Index]);
            if (!Zone->InZone(*Point, 0.0f))
                continue;
            float Radius = Zone->GetRadius();
            if (Radius < BestRadius)
            {
                BestRadius = Radius;
                Best = Zone;
            }
        }
        return Best;
    }

    // Slot 11, 0633b4b0: as slot 10 for physical zones; the radius test comes before the
    // containment test.
    Universe::IZone const* SYSTEM::GetSmallestPhysicalZoneAt(Vector const* Point) const
    {
        Universe::IZone const* Best = nullptr;
        float BestRadius = kNoZoneRadius;
        BuildZoneTree();
        if (Spatial == nullptr)
            return Best;

        Geometry::Sphere Query;
        Query.Center = *Point;
        Query.Radius = 0.1f;
        FSpatialQueryResult Result;
        Result.Capacity = 0x800;
        Result.Count = 0;
        if (SpatialFill(Spatial, &Query, &Result) == 0)
            return Best;

        unsigned Index = 0;
        for (; Index < Result.Count; ++Index)
        {
            Universe::IZone const* Zone = static_cast<Universe::IZone const*>(Result.Items[Index]);
            if (!Zone->HasPhysicalEffect())
                continue;
            float Radius = Zone->GetRadius();
            if (!(Radius < BestRadius))
                continue;
            if (Zone->InZone(*Point, 0.0f))
            {
                Best = Zone;
                BestRadius = Radius;
            }
        }
        return Best;
    }

    // Slot 12, 0633b620: as slot 11 with a property filter and a query radius of 1.0f.
    Universe::IZone const* SYSTEM::GetSmallestZoneWithPropertiesAt(Vector const* Point, unsigned Mask) const
    {
        Universe::IZone const* Best = nullptr;
        float BestRadius = kNoZoneRadius;
        BuildZoneTree();
        if (Spatial == nullptr)
            return Best;

        Geometry::Sphere Query;
        Query.Center = *Point;
        Query.Radius = 1.0f;
        FSpatialQueryResult Result;
        Result.Capacity = 0x800;
        Result.Count = 0;
        if (SpatialFill(Spatial, &Query, &Result) == 0)
            return Best;

        unsigned Index = 0;
        for (; Index < Result.Count; ++Index)
        {
            Universe::IZone const* Zone = static_cast<Universe::IZone const*>(Result.Items[Index]);
            if ((Mask & Zone->GetProperties()) == 0)
                continue;
            float Radius = Zone->GetRadius();
            if (!(Radius < BestRadius))
                continue;
            if (Zone->InZone(*Point, 0.0f))
            {
                Best = Zone;
                BestRadius = Radius;
            }
        }
        return Best;
    }

    // Slot 13, 0633b790
    int SYSTEM::GetConnectionCount() const
    {
        return (int)Connections.size();
    }

    // Slot 14, 0633b7b0. Retail quirk kept: false on every path, even after the copy.
    bool SYSTEM::GetConnection(unsigned Index, Universe::FSystemConnection* Out) const
    {
        if (Index < Connections.size())
            *Out = Connections[Index];
        return false;
    }

    // Slot 15, 0633b7f0: reloads the shared object pool unless it already holds this system.
    int SYSTEM::GetObjectCount() const
    {
        if (this != GSystemObjectPoolOwner)
        {
            LoadSystemObjectPool(File);
            GSystemObjectPoolOwner = this;
        }
        return (int)GSystemObjectPool.size();
    }

    // Slot 16, 0633b840. Slot 15 is called through the vtable, so it reloads the pool when
    // needed.
    bool SYSTEM::GetSystemObject(unsigned Index, Universe::FSystemObject* Out) const
    {
        if (Index >= (unsigned)GetObjectCount())
            return false;
        if (Index >= GSystemObjectPool.size())
            return false;

        Universe::FSystemObject const& Src = GSystemObjectPool[Index];
        Out->Nickname = Src.Nickname.Data;
        Out->Pos = Src.Pos;
        Out->ArchetypeId = Src.ArchetypeId;
        Out->IdsName = Src.IdsName;
        Out->IdsInfo = Src.IdsInfo;
        Out->Reputation = Src.Reputation.Data;
        Out->DockWith = Src.DockWith;
        Out->Goto = Src.Goto;
        Out->BaseId = Src.BaseId;
        Out->NextRing = Src.NextRing.Data;
        Out->PrevRing = Src.PrevRing.Data;
        Out->Visit = Src.Visit;
        return true;
    }

    // Slot 17, 0633b010
    Vector const* SYSTEM::GetPos() const
    {
        return &Pos;
    }

    // Slot 18, 0633afb0
    unsigned char SYSTEM::GetVisit() const
    {
        return Visit;
    }

    // Slot 19, 0633afe0
    void SYSTEM::SetVisit(unsigned char NewVisit)
    {
        Visit = NewVisit;
    }

    // Slot 20, 0633afc0
    float SYSTEM::GetNavMapScale() const
    {
        return NavMapScale;
    }

    // Slot 21, 0633afd0
    unsigned SYSTEM::GetSpaceMusic() const
    {
        return SpaceMusicId;
    }
}

