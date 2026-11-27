// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail E:\FL\Scratch\Source\Client\Space\CliAsteroid.cpp -- the client asteroid field's cube
// map (FCliAsteroid +0x08) and the MedAsteroid gameobj each live CmnAsteroid asteroid of a
// field cube gets on the client.
//
//   MedAsteroid : GameObject   ctor inlined in FUN_00524cb0, 0xac bytes
//                              IObjRW 005dca14 (112 slots), IObjAffect 005dc994 (GameObject's
//                              entries), CBase 005dc98c -> FUN_00524e90 (body FUN_00524ec0 =
//                              the vtables reset, then GameObject's FUN_005394b0)
// RTTI .?AVMedAsteroid@CliAsteroid@@ (COL 005ee020). It adds no data: operator new(0xac) is
// GameObject's size, and bWatchListed (+0xa8) carries the CmnAsteroid bInstantKill flag.
// Slots other than 91, 104, 107, 108 and the new 111 are GameObject's (vtable 005de7ec).

#include "Space/client_gameobj.h"
#include "common_casteroid.h"
#include "common_csys.h" // Matrix, Vector

#include <map>

struct FCliAsteroid;

class FCliMedAsteroidGameobj : public FClientGameobj
{
public:
    // Inlined in FUN_00524cb0: FUN_005392d0(8), then MedAsteroid's three vtables.
    FCliMedAsteroidGameobj();
    // FUN_00524ec0 (this = the CBase sub-object), deleting wrapper FUN_00524e90: only the
    // vtables reset and the base destructor.
    ~FCliMedAsteroidGameobj() override;

    // 91 +0x16c FUN_00524ee0: CObject::Find(Id, CAsteroid 0x1003) into Object,
    // CBase::advise(true) only when found; returns Object != 0.
    bool find(unsigned const& Id) override;
    // 104 +0x1a0 LAB_00524f20: an instant-kill asteroid (bWatchListed) touched by a ship
    // ((class & 0x503) == 0x503) explodes at the contact point; anything else goes to
    // GameObject's handler with the same arguments.
    void OnSurfaceCollisionHit(void const* Contact, void const* B, FClientGameobj* Other,
                 void const* D) override;
    // 107 +0x1ac LAB_00524f80: an ordinary asteroid reports through GameObject's handler; an
    // instant-kill one sends IServer::SPBadLandsObjCollision -- in multiplayer only for
    // objects the local player owns.
    void ReportCollisionToServer(void const* A, void const* B, FClientGameobj* Other, void const* D) override;
    // 108 +0x1b0 FUN_00525040: explode once (no action pending) with the explosion archetype,
    // whose process must be 2; FDUMP warnings otherwise.
    void Explode(Vector const* Pos) override;
    // 111 +0x1bc LAB_005250f0: Out[0] = 0, Out[1] = 0xffff; returns Out. The four arguments
    // after Out are not read. No freelancer.exe call site reaches it through an asteroid (the
    // vt+0x1bc callers are Solar's).
    virtual unsigned short* Slot1bc(unsigned short* Out, unsigned A, unsigned B, unsigned C,
                                    unsigned D);
};
static_assert(sizeof(FCliMedAsteroidGameobj) == 0xac, "MedAsteroid sizeof 0xac (operator new(0xac))");

// LAB_005250f0 -- the slot-111 body MedAsteroid and DynamicAsteroid share.
unsigned short* CliAsteroidSlot1bcBody(unsigned short* Out);

// The value of the field's cube map: one CmnAsteroid cube as the client last saw it, and the
// MedAsteroid gameobjs of its live asteroids. Retail value 0x50 bytes (node +0x10 .. +0x5f).
struct FCliAsteroidCube
{
    // Inlined in FCliAsteroid::Update: the inner map (FUN_004b6800), CubeId = 0, CubeSize = 0xfa;
    // Cube, Orient and Pos are left as the stack held them.
    FCliAsteroidCube();
    // FUN_00526a40 -- memberwise.
    FCliAsteroidCube(FCliAsteroidCube const& Other) = default;
    // FUN_00524af0: every asteroid still in the map gets MarkForRemoval(true) in map order (the
    // ObjDB reap deletes them later), then the inner map is destroyed.
    ~FCliAsteroidCube();

    FCliAsteroidCube& operator=(FCliAsteroidCube const&) = delete;

    // FUN_00524cb0 -- one CmnAsteroid change event for this cube: an active cube creates the
    // MedAsteroid for a new asteroid id, an inactive one releases it (FDUMP warnings for a
    // duplicate / an unknown id).
    void ApplyEvent(CmnAsteroid::asteroid_marker const* Event);

    CmnAsteroid::CAsteroidCube* Cube;   // +0x00  find_cube
    unsigned long CubeId;               // +0x04  CAsteroidCube::get_id
    Matrix Orient;                      // +0x08  the cube's +0x0c (9 dwords)
    Vector Pos;                         // +0x2c  the cube's +0x30
    int CubeSize;                       // +0x38  the field's +0x20
    // +0x3c: retail VC6 std::map<unsigned long, MedAsteroid*> (node 0x18: key +0x0c, value
    // +0x10). The port keeps a std::map of its own (no retail ABI).
    std::map<unsigned long, FCliMedAsteroidGameobj*> Asteroids;
};

// FCliAsteroid +0x08 -- retail VC6 std::map<unsigned long (cube id), value 0x50>, head at
// +0x0c, size at +0x18. The port constructs a std::map in the same 0x14 bytes.
typedef std::map<unsigned long, FCliAsteroidCube> FCliAsteroidCubeMap;

// The map in FCliAsteroid::CubeMapStorage.
FCliAsteroidCubeMap& CliAsteroidCubeMap(FCliAsteroid* Field);
// The field destructor's FUN_00527310 and inline head free -- erase every cube (each value's
// destructor runs), then the map's own destructor. FUN_00520500 is the EH-unwind copy of the
// same body.
void CliAsteroidCubeMapDestruct(FCliAsteroid* Field);
