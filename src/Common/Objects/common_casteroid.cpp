// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "common_casteroid.h"
#include "common_ini.h"
#include "common_id.h"
#include "common_archetype.h"
#include "dacom.h"
#include "common_iface.h" // IObjInspect
#include "common_cobjdb.h"   // IObjDB::GetNext / GetZoneDb
#include "common_universe.h" // Universe::ISystem (CmnAsteroid::Update)
#include "common_csys.h"     // Acquire3DMathEngine (CAsteroidCube::init)
#include "x86math.h"         // I3DMathEngine::TransformMatrix / MulMatrix
#include "x87_trig.h"        // retail's inline FSIN / FCOS (compute_cube_orientation)

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <tuple>
#include <utility>

namespace CmnAsteroid
{

// ============================================================================
// Free functions
// ============================================================================

int __cdecl closest_cube_coord(int Coord, int CubeSize, int HalfSize)
{
    // FUN_062a2ff0
    if (Coord > 0)
        return (Coord / CubeSize) * CubeSize + HalfSize;
    return (Coord / CubeSize) * CubeSize - HalfSize;
}

namespace
{
    // The per-axis "nudge then ftol" idiom of compute_cube_id, compute_asteroid_id and
    // CAsteroidField::create_cube_id / closest_cube_pos: +0.1f (DAT_0639d20c) when greater
    // than zero, -0.1f for less, equal and unordered. The sum stays on the x87 stack; ftol
    // truncates to 64 bits and keeps the low 32.
    inline int NudgeTruncate(float V)
    {
        double const Nudged = (V > 0.0f) ? (double)V + (double)0.1f : (double)V - (double)0.1f;
        return (int)(long long)Nudged;
    }
}

unsigned long __cdecl compute_cube_id(Vector const& Pos)
{
    // FUN_062a6010
    long X = NudgeTruncate(Pos.x);
    long Y = NudgeTruncate(Pos.y);
    long Z = NudgeTruncate(Pos.z);

    unsigned GridX = (unsigned)std::abs(X) >> 7;
    unsigned GridY = (unsigned)std::abs(Y) >> 7;
    unsigned GridZ = (unsigned)std::abs(Z) >> 7;

    if (GridX >= 0x400 || GridY >= 0x200 || GridZ >= 0x400)
        return 0;

    if (X < 0)
        GridX |= 0x400;
    if (Z < 0)
        GridZ |= 0x400;
    if (Y < 0)
        GridY |= 0x200;

    return (GridZ << 10) | GridY | (GridX << 0x15);
}

namespace
{
    // Inlined CRT-rand()-style LCG. Retail reseeds its own holdrand (DAT_063fc290) from Seed on
    // every compute_cube_orientation call, so a local seed reproduces it without touching the
    // process-wide rand() stream.
    inline unsigned NextLcgRaw(unsigned& Seed)
    {
        Seed = Seed * 214013u + 2531011u;
        return (Seed >> 16) & 0x7fff;
    }

    // .data 063eb7f0: byte flag initialised to 1 and never written in the module; gates
    // CAsteroidSystem::update's observer scan (re-read per candidate). Its siblings 063eb7fc /
    // 063eb7fd (GAsteroidGridSearch / GAsteroidAheadSearch) are in common_casteroidfield.cpp.
    bool GAsteroidObserverScan = true;

    // DAT_063fc294 (header node DAT_063fc298): the system registry, built by the dynamic
    // initialiser LAB_062a3a90 (map ctor FUN_06311d40, atexit dtor LAB_062a3ac0).
    std::map<unsigned, CAsteroidSystem> GAsteroidSystems;
}

bool __cdecl compute_cube_orientation(unsigned long Seed, Matrix* InOutOrient, AxisRotations const& Rot, float Freq)
{
    // FUN_062a4010. Freq is the probability of an empty cube.
    unsigned RandState = Seed;
    unsigned WarmCount = (NextLcgRaw(RandState) >> 13) & 3;
    for (; WarmCount != 0; --WarmCount)
        RandState = RandState * 214013u + 2531011u;

    if (Freq >= 0.0f)
    {
        // Only an ordered "less" makes the cube empty; unordered continues.
        double const Prob = (double)NextLcgRaw(RandState) * (1.0f / 32768.0f);
        if (Prob < Freq)
            return false;
    }

    int AxisIndices[3];
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        int Idx = (int)((double)NextLcgRaw(RandState) * (4.0f / 32768.0f));
        if (Idx < 0) Idx = 0;
        if (Idx > 3) Idx = 3;
        AxisIndices[Axis] = Idx;
    }

    float const Angle0 = Rot.Angles[0][AxisIndices[0]];
    float const Angle1 = Rot.Angles[1][AxisIndices[1]];
    float const Angle2 = Rot.Angles[2][AxisIndices[2]];

    // Row-mixing rotations applied in place (the caller passes identity); the second axis uses
    // the opposite sign order, as in retail. FCOS is rounded to float; FSIN stays on the x87
    // stack until the row loop ends.
    double C = X87CosF((double)Angle0);
    double S = X87Sin((double)Angle0);
    for (int Col = 0; Col < 3; ++Col)
    {
        float const A = InOutOrient->E[1][Col];
        float const B = InOutOrient->E[2][Col];
        InOutOrient->E[1][Col] = (float)(A * C - B * S);
        InOutOrient->E[2][Col] = (float)(B * C + A * S);
    }

    C = X87CosF((double)Angle1);
    S = X87Sin((double)Angle1);
    for (int Col = 0; Col < 3; ++Col)
    {
        float const A = InOutOrient->E[0][Col];
        float const B = InOutOrient->E[2][Col];
        InOutOrient->E[0][Col] = (float)(A * C + B * S);
        InOutOrient->E[2][Col] = (float)(B * C - A * S);
    }

    C = X87CosF((double)Angle2);
    S = X87Sin((double)Angle2);
    for (int Col = 0; Col < 3; ++Col)
    {
        float const A = InOutOrient->E[0][Col];
        float const B = InOutOrient->E[1][Col];
        InOutOrient->E[0][Col] = (float)(A * C - B * S);
        InOutOrient->E[1][Col] = (float)(B * C + A * S);
    }

    return true;
}

// ============================================================================
// CAsteroidCube implementation
// ============================================================================

CAsteroidCube::CAsteroidCube(CAsteroidField* OwnerField)
    : CubeId(0)
    , Owner(OwnerField)
    , Size(0)
    , State(CubeStateUnknown)
    , StateTimer(0.0f)
    , SystemId(0)
    , bEmpty(false)
{
    // FUN_062a6150. Deviation: retail leaves CubeId/Orient/Pos/SystemId for init() and writes a
    // leftover register into the never-read +0x44 byte; the port zero-initialises.
    Pos.x = 0.0f;
    Pos.y = 0.0f;
    Pos.z = 0.0f;
    for (int Row = 0; Row < 3; ++Row)
        for (int Col = 0; Col < 3; ++Col)
            Orient.E[Row][Col] = (Row == Col) ? 1.0f : 0.0f;
}

CAsteroidCube::~CAsteroidCube()
{
    // FUN_062a6190. No null test on the asteroids.
    for (std::list<CObject*>::iterator It = Asteroids.begin(); It != Asteroids.end(); ++It)
        (*It)->Release();
    Asteroids.clear();
}

Vector CAsteroidCube::get_pos()
{
    // FUN_062a3020
    return Pos;
}

Matrix CAsteroidCube::get_orient()
{
    // FUN_062a3040
    return Orient;
}

unsigned long CAsteroidCube::get_id()
{
    // FUN_062a66a0 (+0x08)
    return CubeId;
}

CubeState CAsteroidCube::get_state()
{
    // FUN_062a6690 (+0x40)
    return State;
}

void CAsteroidCube::set_size(int SizeValue)
{
    // FUN_062a66b0 (+0x3c)
    Size = SizeValue;
}

void CAsteroidCube::set_system(unsigned SystemIdValue)
{
    // FUN_062a3060
    SystemId = SystemIdValue;
}

void CAsteroidCube::set_state(CubeState StateValue)
{
    // FUN_062a33e0
    State = StateValue;
}

void CAsteroidCube::init(Vector const& PosIn, unsigned long CubeIdValue,
                          AsteroidCubeArch* CubeArch,
                          AxisRotations const& Rot, float Freq)
{
    // FUN_062a6260
    CubeId = CubeIdValue;
    Pos = PosIn;
    StateTimer = 0.0f;
    for (int Row = 0; Row < 3; ++Row)
        for (int Col = 0; Col < 3; ++Col)
            Orient.E[Row][Col] = (Row == Col) ? 1.0f : 0.0f;

    // bEmpty (+0x54): no spawn.
    bEmpty = !compute_cube_orientation(CubeId, &Orient, Rot, Freq);
    if (bEmpty)
        return;

    // Per-archetype spawn loop (hand-traced from the listing; Ghidra has no .c for it).
    CFLIDMaker* Maker = CFLIDMaker::GetInstance();

    std::list<AsteroidCubeArchEntry> const& ArchList = CubeArch->ArchList;
    for (std::list<AsteroidCubeArchEntry>::const_iterator It = ArchList.begin(); It != ArchList.end(); ++It)
    {
        AsteroidCubeArchEntry const& Entry = *It;

        // Retail resets the record every iteration (FUN_062a6560), matching CreateParms'
        // default member initialisers; the CAsteroid-specific fields are set below.
        CAsteroid::CreateParms Parms;
        Parms.Pad58 = 0;
        // +0x60 CubeId is copied by CAsteroid::init to CAsteroid+0xec.
        Parms.AsteroidId = 0;
        Parms.CubeId = CubeId;

        // I3DMathEngine +0x2c: the rock's offset from the cube centre.
        Vector LocalOffset;
        Acquire3DMathEngine()->TransformMatrix((FVector&)LocalOffset, (FMatrix const&)Orient,
                                               (FVector const&)Entry.Extent);

        // The id is computed from the offset, before Pos is added.
        unsigned long AsteroidLocalId = 0;
        if (!compute_asteroid_id(LocalOffset, AsteroidLocalId))
            continue;

        // The rock's FLID nickname.
        char NameBuf[64];
        std::sprintf(NameBuf, "%i%i", (int)CubeId, (int)AsteroidLocalId);
        Parms.Id = Maker->CreateStaticID(NameBuf); // no null test on Maker

        Parms.OwnerField = Owner;
        // FUN_062887d0, a plain Vector add.
        Parms.Pos.x = Pos.x + LocalOffset.x;
        Parms.Pos.y = Pos.y + LocalOffset.y;
        Parms.Pos.z = Pos.z + LocalOffset.z;
        // I3DMathEngine +0x20 MulMatrix.
        Matrix RockOrient;
        Acquire3DMathEngine()->MulMatrix((FMatrix&)RockOrient, (FMatrix const&)Orient,
                                         (FMatrix const&)Entry.Orient);
        Parms.Orient = RockOrient;
        Parms.AsteroidId = AsteroidLocalId;
        Parms.bMine = Entry.bMine;
        Parms.bInstantKill = Entry.bInstantKill;

        // No null test on Alloc. CollisionGroupId (+0xf4) is set directly, not via CreateParms.
        // create_instance's result is discarded (the engine writes the id through
        // initialize_instance).
        CObject* NewObj = CObject::Alloc(CObject::ClassAsteroid);
        CAsteroid* NewAsteroid = (CAsteroid*)NewObj;
        NewAsteroid->CollisionGroupId = SystemId;
        NewAsteroid->open(Entry.Arch);
        Entry.Arch->create_instance(NewObj);
        NewAsteroid->init(Parms); // non-virtual CAsteroid::init

        Asteroids.push_back(NewObj);   // retail list at +0x48/+0x4c
    }
}

asteroid_marker* CAsteroidCube::get_changes()
{
    // FUN_062a65c0. Only Active or Far cubes report: one marker (operator_new(0x14)) per
    // asteroid, in list order, linked through Next.
    if (bEmpty)
        return nullptr;

    asteroid_marker* Head = nullptr;
    if (State != CubeStateActive && State != CubeStateFar)
        return Head;

    asteroid_marker* Prev = nullptr;
    for (std::list<CObject*>::iterator It = Asteroids.begin(); It != Asteroids.end(); ++It)
    {
        CAsteroid* const Rock = (CAsteroid*)*It;
        asteroid_marker* Marker = new asteroid_marker();
        Marker->CubeId = CubeId;
        Marker->AsteroidId = Rock->AsteroidId;
        Marker->ObjId = Rock->Id;
        Marker->bCubeActive = (State == CubeStateActive);
        Marker->bMine = Rock->bMine;
        Marker->bInstantKill = Rock->bInstantKill;
        if (Head == nullptr)
            Head = Marker;
        else
            Prev->Next = Marker;
        Prev = Marker;
    }
    return Head;
}

bool CAsteroidCube::compute_asteroid_id(Vector const& PosIn, unsigned long& OutId)
{
    // FUN_062a66c0. Does not touch *this. Packs abs(nudged coord) >> 1 per axis (10 bits, sign
    // in bit 9); OutId is written only on success.
    int const RoundedX = NudgeTruncate(PosIn.x);
    int const RoundedY = NudgeTruncate(PosIn.y);
    int const RoundedZ = NudgeTruncate(PosIn.z);

    unsigned IdX = (unsigned)std::abs(RoundedX) >> 1;
    unsigned IdY = (unsigned)std::abs(RoundedY) >> 1;
    unsigned IdZ = (unsigned)std::abs(RoundedZ) >> 1;

    if (IdX >= 0x200 || IdY >= 0x200 || IdZ >= 0x200)
        return false;

    if (RoundedX < 0)
        IdX |= 0x200;
    if (RoundedY < 0)
        IdY |= 0x200;
    if (RoundedZ < 0)
        IdZ |= 0x200;

    OutId = IdZ | (IdY << 10) | (IdX << 0x14);
    return true;
}

// ============================================================================
// CAsteroidSystem implementation
// ============================================================================

CAsteroidSystem::CAsteroidSystem()
    : RefCount(0)
    , SystemId(0)
{
    // FUN_062a4250. Deviation: retail leaves FindIter (+0x14) uninitialised; end() makes
    // FindNext before any FindFirst deterministic.
    FindIter = Fields.end();
}

CAsteroidSystem::~CAsteroidSystem()
{
    // FUN_062a42b0, with CAsteroidField::Release inlined.
    SystemId = 0;
    for (std::list<CAsteroidField*>::iterator It = Fields.begin(); It != Fields.end(); ++It)
        (*It)->Release();
}

CAsteroidField* CAsteroidSystem::create_field()
{
    // FUN_062a4410. operator new(0x80).
    Fields.push_back(new CAsteroidField());
    CAsteroidField* Field = Fields.back();
    ++Field->RefCount;
    Field->SystemId = SystemId;
    return Field;
}

void CAsteroidSystem::update(IObjDB* ObjDb, float Dt)
{
    // FUN_062a44c0
    for (std::list<CAsteroidField*>::iterator It = Fields.begin(); It != Fields.end(); ++It)
        (*It)->flush_changes();

    // Observer scan, re-tested against DAT_063eb7f0 before every candidate.
    {
        // The IObjDB::GetNext cursor, built once.
        struct FIterState
        {
            unsigned Unused0;
            unsigned Unused1;
            unsigned Mask;
        };
        FIterState Cursor = { 0, 0, 0x7f0000u };

        while (GAsteroidObserverScan)
        {
            IObjRW* Candidate = ObjDb->GetNext(&Cursor);
            if (Candidate == nullptr)
                break;

            // Objects on a trade lane are skipped.
            bool bUsingTradelane = false;
            (void)ObjInspectOf(Candidate)->is_using_tradelane(&bUsingTradelane);
            if (bUsingTradelane)
                continue;

            Vector Pos = ObjInspectOf(Candidate)->get_position();
            // Dir is the negated third column of the rotation.
            Matrix const& Rotation = ObjInspectOf(Candidate)->get_transform().Rotation;
            Vector Dir;
            Dir.x = -Rotation.E[0][2];
            Dir.y = -Rotation.E[1][2];
            Dir.z = -Rotation.E[2][2];

            // At most two nearby fields are populated per observer.
            int PopulatedCount = 0;
            for (std::list<CAsteroidField*>::iterator It = Fields.begin(); It != Fields.end(); ++It)
            {
                if (!(*It)->near_field(Pos))
                    continue;
                ++PopulatedCount;
                (*It)->populate_asteroids(Pos, Dir);
                if (PopulatedCount >= 2)
                    break;
            }
        }
    }

    for (std::list<CAsteroidField*>::iterator It = Fields.begin(); It != Fields.end(); ++It)
        (*It)->update(Dt);
}

int CAsteroidSystem::AddRef()
{
    // FUN_062a4600
    return ++RefCount;
}

int CAsteroidSystem::Release()
{
    // FUN_062a4610. No self-delete at zero; CmnAsteroid::Unload handles that.
    return --RefCount;
}

CAsteroidField* CAsteroidSystem::FindFirst()
{
    // FUN_062a4620
    FindIter = Fields.begin();
    if (FindIter == Fields.end())
        return nullptr;
    return *FindIter;
}

CAsteroidField* CAsteroidSystem::FindNext()
{
    // FUN_062a4640
    if (FindIter == Fields.end())
        return nullptr;
    ++FindIter;
    if (FindIter == Fields.end())
        return nullptr;
    return *FindIter;
}

// ============================================================================
// Global registry (CmnAsteroid::Load/Unload/Find/Update/CreateAsteroidField)
// ============================================================================

int __cdecl Load(unsigned SystemId)
{
    // FUN_062a3bb0. Returns 2 when already loaded, 1 when newly inserted (insert FUN_062a72f0).
    std::map<unsigned, CAsteroidSystem>::iterator It = GAsteroidSystems.find(SystemId);
    if (It != GAsteroidSystems.end())
    {
        ++It->second.RefCount;
        return 2;
    }
    It = GAsteroidSystems.emplace(std::piecewise_construct, std::forward_as_tuple(SystemId),
                                  std::forward_as_tuple()).first;
    ++It->second.RefCount;
    It->second.SystemId = SystemId;
    return 1;
}

int __cdecl Unload(unsigned SystemId)
{
    // FUN_062a3e80. 0 not found, 2 still referenced, 1 erased (FUN_062a7500).
    std::map<unsigned, CAsteroidSystem>::iterator It = GAsteroidSystems.find(SystemId);
    if (It == GAsteroidSystems.end())
        return 0;
    if (--It->second.RefCount != 0)
        return 2;
    GAsteroidSystems.erase(It);
    return 1;
}

void __cdecl Update(IObjDB* ObjDb, float Dt)
{
    // FUN_062a3ef0. Only the system the object database belongs to is updated.
    Universe::ISystem const* const System = ObjDb->GetZoneDb();
    unsigned const CurrentSystemId = System->GetId();
    std::map<unsigned, CAsteroidSystem>::iterator It = GAsteroidSystems.find(CurrentSystemId);
    if (It != GAsteroidSystems.end())
        It->second.update(ObjDb, Dt);
}

CAsteroidField* __cdecl CreateAsteroidField(unsigned SystemId)
{
    // FUN_062a3f60. An unregistered system is loaded and the caller gets no field this call.
    std::map<unsigned, CAsteroidSystem>::iterator It = GAsteroidSystems.find(SystemId);
    if (It == GAsteroidSystems.end())
    {
        Load(SystemId);
        return nullptr;
    }
    return It->second.create_field();
}

CAsteroidSystem* __cdecl Find(unsigned SystemId)
{
    // FUN_062a3fc0
    std::map<unsigned, CAsteroidSystem>::iterator It = GAsteroidSystems.find(SystemId);
    if (It == GAsteroidSystems.end())
        return nullptr;
    return &It->second;
}

}
