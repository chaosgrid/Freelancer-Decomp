// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

// Common.dll Exclusion: AsteroidExclusionZone / NebulaExclusionZone and their managers, with the
// managers' VC6-vector and sort helpers (retail ExclusionZoneMgr.cpp).

#include "common_net.h"
#include "common_id.h" // CreateID for NebulaExclusionZone::set_zone_shell_name

#include "dacom.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

// ===========================================================================
// Exclusion namespace
// ===========================================================================

namespace Exclusion
{

// --- AsteroidExclusionZone ---

AsteroidExclusionZone::AsteroidExclusionZone()
    : ZonePtr(nullptr)
    , MaxDistSq(10000000.0f)   // 0x4b189680 == 1e7
    , bExcludeBillboards(0)
    , bExcludeDynamicAsteroids(0)
    , EmptyCubeFrequency(-1.0f)       // 0xbf800000
    , BillboardCount(-1.0f)
{
}

void AsteroidExclusionZone::update_dist(const Vector& Pos)
{
    // .text:062bc4a0.
    if (ZonePtr == nullptr)
        return;

    unsigned const Shape = ZonePtr->GetShape();
    Vector const ZonePos = ZonePtr->GetPos();

    float const Dx = Pos.x - ZonePos.x;
    float const Dy = Pos.y - ZonePos.y;
    float const Dz = Pos.z - ZonePos.z;
    float Len = 0.0f;
    double Radius = 0.0;

    if (Shape == Universe::ZoneShapeSphere)
    {
        // Summation order (z, y, x) as retail.
        Len = (float)(sqrt(((double)(Dz) * Dz + (double)(Dy) * Dy) +
                                      (double)(Dx) * Dx));
        Radius = ZonePtr->GetRadius();
    }
    else if (Shape == Universe::ZoneShapeEllipsoid)
    {
        // Summation order (z, x, y) differs from the sphere case, as retail. No zero-length
        // test.
        Len = (float)(sqrt(((double)(Dz) * Dz + (double)(Dx) * Dx) +
                                      (double)(Dy) * Dy));
        double const Inv = 1.0f / (double)(Len);
        Vector Dir;
        Dir.x = (float)(Dx * Inv);
        Dir.y = (float)(Dy * Inv);
        Dir.z = (float)(Dz * Inv);
        Radius = ZonePtr->GetRadiusAlong(Dir);
    }
    else
    {
        MaxDistSq = 0.0f;
        return;
    }

    // Unordered (NaN) takes the square branch, as retail.
    double const Dist = Len - Radius;
    if (Dist < 0.0)
        MaxDistSq = 0.0f;
    else
        MaxDistSq = (float)(Dist * Dist);
}

AsteroidExclusionZone::~AsteroidExclusionZone()
{
    // .text:062a8920 (empty)
}

Universe::IZone const* AsteroidExclusionZone::get_zone()
{
    // .text:06261d90
    return ZonePtr;
}

bool AsteroidExclusionZone::set_zone(Universe::IZone const* Zone, Universe::IZone const* Parent)
{
    // .text:062bc620. Parent is not tested; unordered compares false.
    ZonePtr = Zone;
    if (Zone == nullptr)
        return false;
    return Parent->GetImmersion(Zone->GetPos()) > 0.0f;
}

// --- NebulaExclusionZone ---

NebulaExclusionZone::NebulaExclusionZone()
    : ZonePtr(nullptr)
    // .text:062bcbc0: +0x04, the same 1e7 "far away" sentinel as AsteroidExclusionZone's.
    , MaxDistSq(10000000.0f)
    , FogFar(-1.0f)          // +0x08
{
    // Retail calls ID_String::clear on +0x0c twice (before and after the stores).
    ZoneShellName.clear();
}

NebulaExclusionZone::~NebulaExclusionZone()
{
    // .text:062a8920 (empty)
}

Universe::IZone const* NebulaExclusionZone::get_zone()
{
    // .text:06261d90
    return ZonePtr;
}

float NebulaExclusionZone::get_fog_far()
{
    // .text:06261da0
    return FogFar;
}

void NebulaExclusionZone::set_fog_far(float Far)
{
    // .text:0631d940
    FogFar = Far;
}

void NebulaExclusionZone::update_dist(const Vector& Pos)
{
    // .text:062bcbf0. The deltas stay in double; only the length is rounded to float.
    // Unordered takes the square branch.
    if (ZonePtr == nullptr)
        return;

    Vector const ZonePos = ZonePtr->GetPos();
    double const Dx = (double)(Pos.x) - ZonePos.x;
    double const Dy = (double)(Pos.y) - ZonePos.y;
    double const Dz = (double)(Pos.z) - ZonePos.z;
    float const Length = (float)(sqrt((Dz * Dz + Dx * Dx) + Dy * Dy));
    double const Dist = Length - (double)(ZonePtr->GetRadius());
    if (Dist < 0.0)
        MaxDistSq = 0.0f;
    else
        MaxDistSq = (float)(Dist * Dist);
}

bool NebulaExclusionZone::set_zone(Universe::IZone const* Zone, Universe::IZone const* Parent)
{
    // 062bcc90. Neither zone is null-checked.
    ZonePtr = Zone;

    if (Zone->GetShape() != Universe::ZoneShapeSphere)
    {
        FDUMP(kFdumpError,
            "%s(%d) : *** ERROR: DATA ERROR: nebula exclusion zones may only be SPHERE shaped\n %u is not a sphere zone",
            "E:\\FL\\Scratch\\Source\\Common\\ExclusionZoneMgr.cpp", 0x1e9, Zone->GetId());
    }

    // A float copy is kept for the return test; the 1.0 test runs on the unrounded x87 value.
    double const Immersion = Parent->GetImmersion(Zone->GetPos());
    float const Inside = (float)Immersion;

    // Below 1.0 or unordered reports.
    if (!(Immersion >= 1.0))
    {
        // Retail passes the two zone ids (uints) for the two %s -- undefined behaviour. The port
        // prints the same ids as ID_String text ("0x...") instead.
        ID_String ParentId;
        ParentId.Id = Parent->GetId();
        ID_String ZoneId;
        ZoneId.Id = Zone->GetId();
        char const* const ParentText = ParentId.get_string();
        char const* const ZoneText = ZoneId.get_string();
        FDUMP(kFdumpError,
            "%s(%d) : *** ERROR: DATA ERROR: nebula exclusion zone (%s) is not fully inside nebula zone (%s)\n",
            "E:\\FL\\Scratch\\Source\\Common\\ExclusionZoneMgr.cpp", 0x1f5, ZoneText, ParentText);
    }

    return Inside > 0.0f;
}

ID_String NebulaExclusionZone::get_zone_shell_name()
{
    // .text:062a2db0 (+0x0C)
    return ZoneShellName;
}

void NebulaExclusionZone::set_zone_shell_name(const char* Name)
{
    // .text:062bcd50
    ZoneShellName.Id = CreateID(Name);
}

// --- The managers' zone arrays ---
//
// Both managers hold a VC6 std::vector<Zone*> at +0x04 (allocator byte +0x04, first +0x08,
// last +0x0c, end +0x10). The helpers below are the vector members retail calls or inlines,
// on operator new / operator delete as VC6's allocator does.
namespace
{
    // vector<T*>::insert(last, 1, value) at .text:0628e250 (ICF-shared by every pointer vector),
    // as both push_back members call it. Growth is size + max(size, 1) slots.
    template <typename TZone>
    void ZoneArrayPushBack(TZone**& First, TZone**& Last, TZone**& End, TZone* Value)
    {
        if ((unsigned)(End - Last) < 1u)
        {
            unsigned const Size = (First == nullptr) ? 0u : (unsigned)(Last - First);
            unsigned const Grow = (First == nullptr || !(1u < Size)) ? 1u : Size;
            int Capacity = (int)(Size + Grow);
            if (Capacity < 0)
                Capacity = 0;
            TZone** const Block = (TZone**)(operator new((size_t)(Capacity) * sizeof(TZone*)));
            TZone** Out = Block;
            for (TZone** In = First; In != Last; ++In, ++Out)
                *Out = *In;
            *Out = Value;
            operator delete(First);
            End = Block + Capacity;
            Last = Block + (Size + 1u);
            First = Block;
            return;
        }
        *Last = Value;
        ++Last;
    }

    // The VC6 vector<T*> copy constructor both manager copy constructors inline (.text:062a2b9e
    // / 062a2dce). An empty source still allocates.
    template <typename TZone>
    void ZoneArrayCopyConstruct(TZone**& First, TZone**& Last, TZone**& End, TZone** OtherFirst, TZone** OtherLast)
    {
        int Count = (OtherFirst == nullptr) ? 0 : (int)(OtherLast - OtherFirst);
        if (Count < 0)
            Count = 0;
        First = (TZone**)(operator new((size_t)(Count) * sizeof(TZone*)));
        TZone** Out = First;
        for (TZone** In = OtherFirst; In != OtherLast; ++In, ++Out)
            *Out = *In;
        Last = Out;
        End = Out;
    }

    // The VC6 vector<T*>::operator= both manager assignments inline (.text:062a2c2b /
    // 062a2e4b). Self-assignment skips it. Only the pointers are copied; the zones are shared.
    template <typename TZone>
    void ZoneArrayAssign(TZone**& First, TZone**& Last, TZone**& End, TZone** OtherFirst, TZone** OtherLast)
    {
        unsigned const OtherSize = (OtherFirst == nullptr) ? 0u : (unsigned)(OtherLast - OtherFirst);
        unsigned const Size = (First == nullptr) ? 0u : (unsigned)(Last - First);
        if (!(OtherSize > Size))
        {
            TZone** Out = First;
            for (TZone** In = OtherFirst; In != OtherLast; ++In, ++Out)
                *Out = *In;
            Last = First + OtherSize;
            return;
        }

        unsigned const Capacity = (First == nullptr) ? 0u : (unsigned)(End - First);
        if (!(OtherSize > Capacity))
        {
            TZone** Out = First;
            TZone** In = OtherFirst;
            for (unsigned Index = 0; Index < Size; ++Index, ++In, ++Out)
                *Out = *In;
            for (; In != OtherLast; ++In, ++Out)
                *Out = *In;
            Last = First + OtherSize;
            return;
        }

        operator delete(First);
        int Count = (OtherFirst == nullptr) ? 0 : (int)(OtherLast - OtherFirst);
        if (Count < 0)
            Count = 0;
        First = (TZone**)(operator new((size_t)(Count) * sizeof(TZone*)));
        TZone** Out = First;
        for (TZone** In = OtherFirst; In != OtherLast; ++In, ++Out)
            *Out = *In;
        Last = Out;
        End = Out;
    }

    // The managers' destructors (.text:062bbd70 / 062bc6b0).
    template <typename TZone>
    void ZoneArrayDestroy(TZone**& First, TZone**& Last, TZone**& End)
    {
        for (TZone** It = First; It != Last; ++It)
        {
            if (*It != nullptr)
                delete *It;
        }
        Last = First;
        operator delete(First);
        First = nullptr;
        Last = nullptr;
        End = nullptr;
    }

    // The comparison every sort step makes (unordered compares false).
    template <typename TZone>
    bool ZoneCloser(TZone const* A, TZone const* B)
    {
        return A->MaxDistSq < B->MaxDistSq;
    }

    // VC6 std::sort on the zone arrays, inlined into both sort members with these out-of-line
    // pieces: _Unguarded_insert FUN_062bcf70, _Unguarded_partition FUN_062bcfa0 and the
    // recursive _Sort FUN_062bcd70 / FUN_062bce70 (one per zone type).
    template <typename TZone>
    void ZoneUnguardedInsert(TZone** Last, TZone* Value)
    {
        // FUN_062bcf70: shift right while Value sorts before the previous element.
        for (TZone** Prev = Last; ZoneCloser(Value, *--Prev); Last = Prev)
            *Last = *Prev;
        *Last = Value;
    }

    template <typename TZone>
    void ZoneInsertionSort(TZone** First, TZone** Last)
    {
        if (First == Last)
            return;
        for (TZone** It = First; ++It != Last;)
        {
            TZone* const Value = *It;
            if (ZoneCloser(Value, *First))
            {
                for (TZone** Move = It; Move != First; --Move)
                    *Move = *(Move - 1);
                *First = Value;
            }
            else
            {
                ZoneUnguardedInsert(It, Value);
            }
        }
    }

    template <typename TZone>
    TZone* ZoneMedian(TZone* X, TZone* Y, TZone* Z)
    {
        if (ZoneCloser(X, Y))
            return ZoneCloser(Y, Z) ? Y : (ZoneCloser(X, Z) ? Z : X);
        return ZoneCloser(X, Z) ? X : (ZoneCloser(Y, Z) ? Z : Y);
    }

    template <typename TZone>
    TZone** ZoneUnguardedPartition(TZone** First, TZone** Last, TZone* Pivot)
    {
        // FUN_062bcfa0.
        for (;; ++First)
        {
            for (; ZoneCloser(*First, Pivot); ++First)
            {
            }
            for (; ZoneCloser(Pivot, *--Last);)
            {
            }
            if (!(Last > First))
                return First;
            TZone* const Swap = *First;
            *First = *Last;
            *Last = Swap;
        }
    }

    template <typename TZone>
    void ZoneSortRange(TZone** First, TZone** Last)
    {
        // FUN_062bcd70 / FUN_062bce70: recurse into the side that is not longer.
        while ((Last - First) > 16)
        {
            TZone** const Mid = ZoneUnguardedPartition(
                First, Last, ZoneMedian(*First, *(First + (Last - First) / 2), *(Last - 1)));
            if ((Last - Mid) <= (Mid - First))
            {
                ZoneSortRange(Mid, Last);
                Last = Mid;
            }
            else
            {
                ZoneSortRange(First, Mid);
                First = Mid;
            }
        }
    }

    template <typename TZone>
    void ZoneSort(TZone** First, TZone** Last)
    {
        if ((Last - First) <= 16)
        {
            ZoneInsertionSort(First, Last);
            return;
        }
        ZoneSortRange(First, Last);
        ZoneInsertionSort(First, First + 16);
        for (TZone** It = First + 16; It != Last; ++It)
            ZoneUnguardedInsert(It, *It);
    }
}

// --- AsteroidExclusionZoneMgr ---

AsteroidExclusionZoneMgr::AsteroidExclusionZoneMgr()
    : ZonesAllocator(0)
    , Begin(nullptr)
    , End(nullptr)
    , CapacityEnd(nullptr)
    // +0x14 / +0x18 hold SQUARED distances (2000^2, 500^2); the setters square their argument.
    , MaxCliDist(4000000.0f)
    , MaxDynDist(250000.0f)
{
}

AsteroidExclusionZoneMgr::AsteroidExclusionZoneMgr(const AsteroidExclusionZoneMgr& Other)
    : ZonesAllocator(Other.ZonesAllocator)
{
    // .text:062a2b90 (base vftable 0639dcf0). The zone pointers are shared.
    ZoneArrayCopyConstruct(Begin, End, CapacityEnd, Other.Begin, Other.End);
    MaxCliDist = Other.MaxCliDist;
    MaxDynDist = Other.MaxDynDist;
}

AsteroidExclusionZoneMgr& AsteroidExclusionZoneMgr::operator=(const AsteroidExclusionZoneMgr& Other)
{
    // .text:062a2c20. The zones are shared, not copied, and the old ones are not freed.
    if (&ZonesAllocator != &Other.ZonesAllocator)
        ZoneArrayAssign(Begin, End, CapacityEnd, Other.Begin, Other.End);
    MaxCliDist = Other.MaxCliDist;
    MaxDynDist = Other.MaxDynDist;
    return *this;
}

AsteroidExclusionZoneMgr::~AsteroidExclusionZoneMgr()
{
    // .text:062bbd70.
    ZoneArrayDestroy(Begin, End, CapacityEnd);
}

bool AsteroidExclusionZoneMgr::is_excluded(const Vector& Pos, ExcludeObjectType Type, float* OutValue)
{
    // .text:062bbe70. Cases 0, 1 and 3 are inlined copies of the matching members in retail;
    // case 2 calls is_billboard_excluded.
    switch ((unsigned)(Type))
    {
    case EOT_CliAsteroid:
        return is_cliasteroid_excluded(Pos, OutValue);
    case EOT_CmnAsteroid:
        return is_cmnasteroid_excluded(Pos, OutValue);
    case EOT_Billboard:
        return is_billboard_excluded(Pos, OutValue);
    case EOT_DynamicAsteroid:
        return is_dynamic_asteroid_excluded(Pos);
    default:
        return false;
    }
}

void AsteroidExclusionZoneMgr::push_back(Universe::IZone const* Zone, Universe::IZone const* Parent)
{
    // .text:062bbde0: operator new(0x14). set_zone's result is dropped.
    AsteroidExclusionZone* const NewZone = new AsteroidExclusionZone();
    ZoneArrayPushBack(Begin, End, CapacityEnd, NewZone);
    back()->set_zone(Zone, Parent);
}

void AsteroidExclusionZoneMgr::sort(Vector const* Observer)
{
    // .text:062bbfe0. No null tests.
    for (AsteroidExclusionZone** It = Begin; It != End; ++It)
        (*It)->update_dist(*Observer);
    ZoneSort(Begin, End);
}

AsteroidExclusionZone* AsteroidExclusionZoneMgr::back()
{
    // .text:062bbe60 (shared with NebulaExclusionZoneMgr::back). No empty test.
    return *(End - 1);
}

int AsteroidExclusionZoneMgr::get_max_billboard_count()
{
    // .text:062bc3d0. -1 when none is above 0.
    int MaxCount = -1;
    for (AsteroidExclusionZone** It = Begin; It != End; ++It)
    {
        int const Count = (int)((long long)((*It)->BillboardCount));
        if ((float)(Count) > 0.0f && Count > MaxCount)
            MaxCount = Count;
    }
    return MaxCount;
}

void AsteroidExclusionZoneMgr::normalize_billboard_density(int TotalCount)
{
    // .text:062bc420. No zero test of TotalCount; the count is truncated before scaling.
    float const Inverse = (float)(1.0 / (double)(TotalCount));
    for (AsteroidExclusionZone** It = Begin; It != End; ++It)
    {
        AsteroidExclusionZone* const Zone = *It;
        int const Count = (int)((long long)(Zone->BillboardCount));
        if ((double)(Count) > 0.0)
            Zone->BillboardCount = (float)((double)(Count) * Inverse);
    }
}

void AsteroidExclusionZoneMgr::set_max_cliasteroid_exclusion_distance(float Dist)
{
    // RVA 0x42b70. Squared: compared directly against Zone->MaxDistSq.
    MaxCliDist = Dist * Dist;
}

void AsteroidExclusionZoneMgr::set_max_dynamicasteroid_exclusion_distance(float Dist)
{
    // Squared, as set_max_cliasteroid_exclusion_distance.
    MaxDynDist = Dist * Dist;
}

bool AsteroidExclusionZoneMgr::is_cliasteroid_excluded(const Vector& Pos, float* OutFrequency)
{
    // .text:062bc200. No null tests. The zones are sorted by distance, so the scan stops at the
    // first one beyond MaxCliDist. A negative or unordered EmptyCubeFrequency excludes
    // outright; otherwise the last containing zone's value is reported.
    *OutFrequency = -1.0f;
    for (AsteroidExclusionZone** It = Begin; It != End; ++It)
    {
        AsteroidExclusionZone* const Zone = *It;
        if (Zone->MaxDistSq > MaxCliDist)
            return false;
        if (Zone->ZonePtr->GetImmersion(Pos) != 0.0f)
        {
            if (!(Zone->EmptyCubeFrequency >= 0.0f))
                return true;
            *OutFrequency = Zone->EmptyCubeFrequency;
        }
    }
    return false;
}

bool AsteroidExclusionZoneMgr::is_cmnasteroid_excluded(const Vector& Pos, float* OutFrequency)
{
    // .text:062bc270. As is_cliasteroid_excluded without the distance cut-off.
    *OutFrequency = -1.0f;
    for (AsteroidExclusionZone** It = Begin; It != End; ++It)
    {
        AsteroidExclusionZone* const Zone = *It;
        if (Zone->ZonePtr->GetImmersion(Pos) != 0.0f)
        {
            if (!(Zone->EmptyCubeFrequency >= 0.0f))
                return true;
            *OutFrequency = Zone->EmptyCubeFrequency;
        }
    }
    return false;
}

bool AsteroidExclusionZoneMgr::is_billboard_excluded(const Vector& Pos, float* OutDensity)
{
    // .text:062bc2d0. The end of the scan returns false in every case; the found flag only
    // stops further writes to *OutDensity.
    bool bFound = false;
    for (AsteroidExclusionZone** It = Begin; It != End; ++It)
    {
        AsteroidExclusionZone* const Zone = *It;
        if (Zone->bExcludeBillboards)
        {
            if (Zone->ZonePtr->GetImmersion(Pos) != 0.0f)
                return true;
            continue;
        }
        float const Density = Zone->BillboardCount;
        if (bFound || !(Density > 0.0f))
            continue;
        if (Zone->ZonePtr->GetImmersion(Pos) != 0.0f)
        {
            bFound = true;
            *OutDensity = Density;
        }
    }
    return false;
}

bool AsteroidExclusionZoneMgr::is_dynamic_asteroid_excluded(const Vector& Pos)
{
    // .text:062bc370
    for (AsteroidExclusionZone** It = Begin; It != End; ++It)
    {
        AsteroidExclusionZone* const Zone = *It;
        if (Zone->MaxDistSq > MaxDynDist)
            return false;
        if (Zone->bExcludeDynamicAsteroids && Zone->ZonePtr->GetImmersion(Pos) != 0.0f)
            return true;
    }
    return false;
}

// --- NebulaExclusionZoneMgr ---

NebulaExclusionZoneMgr::NebulaExclusionZoneMgr()
    : ZonesAllocator(0)
    , Begin(nullptr)
    , End(nullptr)
    , CapacityEnd(nullptr)
    // .text:062bc660 never writes +0x14: retail leaves MaxDist indeterminate until
    // set_max_distance (UB); the port zeroes it.
    , MaxDist(0.0f)
{
    // The allocator byte at +0x04 is copied from an uninitialised stack byte in retail; the
    // value is never read.
}

NebulaExclusionZoneMgr::NebulaExclusionZoneMgr(const NebulaExclusionZoneMgr& Other)
    : ZonesAllocator(Other.ZonesAllocator)
{
    // .text:062a2dc0. The zone pointers are shared.
    ZoneArrayCopyConstruct(Begin, End, CapacityEnd, Other.Begin, Other.End);
    MaxDist = Other.MaxDist;
}

NebulaExclusionZoneMgr& NebulaExclusionZoneMgr::operator=(const NebulaExclusionZoneMgr& Other)
{
    // .text:062a2e40. The zones are shared, not copied, and the old ones are not freed.
    if (&ZonesAllocator != &Other.ZonesAllocator)
        ZoneArrayAssign(Begin, End, CapacityEnd, Other.Begin, Other.End);
    MaxDist = Other.MaxDist;
    return *this;
}

NebulaExclusionZoneMgr::~NebulaExclusionZoneMgr()
{
    // .text:062bc6b0.
    ZoneArrayDestroy(Begin, End, CapacityEnd);
}

bool NebulaExclusionZoneMgr::is_excluded(const Vector& Pos, ExcludeObjectType Type, float* OutValue)
{
    // .text:062bc840. OutValue is not touched; no null tests.
    (void)OutValue;
    if ((int)(Type) != 4)
        return false;
    for (NebulaExclusionZone** It = Begin; It != End; ++It)
    {
        if ((*It)->ZonePtr->GetImmersion(Pos) != 0.0f)
            return true;
    }
    return false;
}

void NebulaExclusionZoneMgr::push_back(Universe::IZone const* Zone, Universe::IZone const* Parent)
{
    // .text:062bc720: operator new(0x10). An unordered edge fraction reports too.
    NebulaExclusionZone* const NewZone = new NebulaExclusionZone();
    ZoneArrayPushBack(Begin, End, CapacityEnd, NewZone);
    bool const bInside = back()->set_zone(Zone, Parent);
    if (!(Zone->GetEdgeFraction() > 0.0f))
    {
        FDUMP(kFdumpError,
            "%s(%d) : *** ERROR: DATA ERROR: edge fraction must be added to exclusion zone %u inorder for transitions to work",
            "E:\\FL\\Scratch\\Source\\Common\\ExclusionZoneMgr.cpp", 0x170, Zone->GetId());
    }
    if (!bInside)
    {
        FDUMP(kFdumpError, "%s(%d) : *** ERROR: DATA ERROR: exclusion zone %u not inside nebula",
            "E:\\FL\\Scratch\\Source\\Common\\ExclusionZoneMgr.cpp", 0x176, Zone->GetId());
    }
}

void NebulaExclusionZoneMgr::sort(Vector const* Observer)
{
    // .text:062bc890. update_dist is inlined but NOT identical: the deltas are rounded to float
    // (Vector ctor FUN_0628b030) and summed in (y, z, x) order. Observer is not tested.
    Vector const& ObserverPos = *Observer;
    for (NebulaExclusionZone** It = Begin; It != End; ++It)
    {
        NebulaExclusionZone* const Zone = *It;
        if (Zone->ZonePtr == nullptr)
            continue;
        Vector const ZonePos = Zone->ZonePtr->GetPos();
        float const Dx = ObserverPos.x - ZonePos.x;
        float const Dy = ObserverPos.y - ZonePos.y;
        float const Dz = ObserverPos.z - ZonePos.z;
        float const Length = (float)(sqrt(((double)(Dy) * Dy + (double)(Dz) * Dz) +
                                                     (double)(Dx) * Dx));
        double const Dist = Length - (double)(Zone->ZonePtr->GetRadius());
        if (Dist < 0.0)
            Zone->MaxDistSq = 0.0f;
        else
            Zone->MaxDistSq = (float)(Dist * Dist);
    }
    ZoneSort(Begin, End);
}

NebulaExclusionZone* NebulaExclusionZoneMgr::back()
{
    // .text:062bbe60 (shared with AsteroidExclusionZoneMgr::back): no empty test.
    return *(End - 1);
}

bool NebulaExclusionZoneMgr::is_in_exclusion_zone(const Vector& Pos,
                                                NebulaExclusionZone** OutZone,
                                                float* OutImmersion, bool bNearOnly)
{
    // .text:062bcb40. No null tests. With bNearOnly the scan stops at the first zone beyond 10.0f
    // (float 0639e884); the threshold 1e-5f is float 0639b984.
    *OutZone = nullptr;
    *OutImmersion = 0.0f;
    for (NebulaExclusionZone** It = Begin; It != End; ++It)
    {
        NebulaExclusionZone* const Zone = *It;
        if (bNearOnly && Zone->MaxDistSq > 10.0f)
            return false;
        // Compared unrounded.
        double const Inside = Zone->ZonePtr->GetImmersion(Pos);
        if (Inside > 9.99999975e-06f)
        {
            *OutZone = Zone;
            *OutImmersion = (float)Inside;
            return true;
        }
    }
    return false;
}

void NebulaExclusionZoneMgr::set_max_distance(float Dist)
{
    // RVA 0x42b70, ICF-shared with set_max_cliasteroid_exclusion_distance: squared.
    MaxDist = Dist * Dist;
}

} // namespace Exclusion
