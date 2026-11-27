// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail E:\FL\Scratch\Source\Client\Space\CliAsteroid.cpp -- [DynamicAsteroids]: the manager
// record a field keeps per section (FCliDynamicAsteroid, cli_asteroid.h, 0xe8) and the
// DynamicAsteroid gameobjs it scatters, fades and recycles around the camera.
//
//   DynamicAsteroid : GameObject   ctor inlined in FUN_00526650, 0xc8 bytes
//                                  IObjRW 005dccdc (112 slots), IObjAffect 005dcc5c (GameObject's
//                                  entries), CBase 005dcc50 -> FUN_00525110 (body FUN_00525140)
// RTTI .?AVDynamicAsteroid@CliAsteroid@@. Slots other than 34 (get_type), 90, 91, 101, 108 and
// the new 111 are GameObject's (vtable 005de7ec).

#include "Space/cli_asteroid.h"
#include "Space/client_gameobj.h"
#include "common_id.h"

struct FSpaceCamera;

class FCliDynamicAsteroidGameobj : public FClientGameobj
{
public:
    // Inlined in FUN_00526650 after FUN_005392d0(9). Alpha (+0xb0) is not written.
    explicit FCliDynamicAsteroidGameobj(FCliDynamicAsteroid* Owner);
    // FUN_00525140 (this = the CBase sub-object), deleting wrapper FUN_00525110: the
    // companion's particle, sound and light released, then GameObject's destructor.
    ~FCliDynamicAsteroidGameobj() override;

    // IObjInspect 34 +0x88 LAB_00525690: *OutType = 0x20000000, returns 0.
    int get_type(uint& OutType) const override;

    // 90 +0x168 FUN_005251d0: the object of class 0x4001 opened on Arch (no narrowing, no null
    // test), +0xb4 = the archetype's nickname.
    void create(Archetype::Root* Arch) override;
    // 91 +0x16c FUN_0053f330: returns false (Beam's and Debris's body).
    bool find(unsigned const& Id) override;
    // 101 +0x194 LAB_00525480: Other->OnSurfaceCollisionHit(A, D, this, SUBOBJ_ID_NONE); then
    // an active asteroid hit by a munition ((class & 7) == 7) explodes at A and, when the shot
    // is the player's and the owner's zone is lootable, may drop loot (IServer::MineAsteroid).
    void OnCollision(FClientCollisionRecord const* A, void const* B, FClientGameobj* Other, void const* D) override;
    // 108 +0x1b0 LAB_005255e0: deactivate, collisions off, the companion's effect dropped,
    // then the explosion (process must be 2) at Pos or get_position().
    void Explode(Vector const* Pos) override;
    // 111 +0x1bc LAB_005250f0: MedAsteroid's body (CliAsteroidSlot1bcBody).
    virtual unsigned short* Slot1bc(unsigned short* Out, unsigned A, unsigned B, unsigned C,
                                    unsigned D);

    // FUN_00525200 -- beam the object to Pos with an identity orientation and give it a random
    // velocity (up to MaxVelocity per axis, square-biased) and a random angular velocity (up to
    // MaxAngularVelocity); the particle's intensity starts at 0. Called from
    // FCliDynamicAsteroid::Update.
    void Place(Vector const* Pos, float MaxVelocity, float MaxAngularVelocity);

    // +0xa8 -- the owning manager. Retail stores the dword over GameObject's bWatchListed byte
    // and its three pad bytes; the port keeps that storage.
    FCliDynamicAsteroid* GetOwner() const;
    void SetOwner(FCliDynamicAsteroid* Owner);

    // ---- data ---------------------------------------------------------------------------
    unsigned char bActive;   // +0xac  set when placed; Explode clears it
    unsigned char bPlaced;   // +0xad  inside the placement sphere since the last placement
    unsigned char Padae[2];
    float Alpha;             // +0xb0  the update's fade, the draw's material alpha
    ID_String Nickname;      // +0xb4  slot 90: the archetype's nickname
    void* Companion[4];      // +0xb8  effect companion {arch, ISoundSource*, particle, light}
};
static_assert(offsetof(FCliDynamicAsteroidGameobj, bActive) == 0xac, "DynamicAsteroid +0xac");
static_assert(offsetof(FCliDynamicAsteroidGameobj, Alpha) == 0xb0, "DynamicAsteroid +0xb0");
static_assert(offsetof(FCliDynamicAsteroidGameobj, Nickname) == 0xb4, "DynamicAsteroid +0xb4");
static_assert(offsetof(FCliDynamicAsteroidGameobj, Companion) == 0xb8, "DynamicAsteroid +0xb8");
static_assert(sizeof(FCliDynamicAsteroidGameobj) == 0xc8,
              "DynamicAsteroid sizeof 0xc8 (operator new(0xc8))");
