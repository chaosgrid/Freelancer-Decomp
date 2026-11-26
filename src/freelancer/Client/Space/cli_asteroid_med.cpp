// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "Space/cli_asteroid_med.h"

#include "Space/cli_asteroid.h"
#include "Space/Effects/client_explosion.h"   // Action_Explode (FUN_005361e0)
#include "Space/client_objdb.h"       // ClientObjDb_RegisterGameobj (FUN_00540c30)
#include "common_server.h"      // IServer::SPBadLandsObjCollision (slot 54)
#include "common_misc.h"        // SinglePlayer
#include "dacom.h"              // FDUMP
#include "Space/player_spawn.h"       // GetPlayerConnectionId (DAT_00673344)
#include "Network/server_connect.h"     // GetLocalServerInterface (DAT_0067ecd0)

#include <cstdint>
#include <new>
#include <utility>

namespace
{
    // .rdata:005dc47c -- CliAsteroid.cpp's FDUMP file argument.
    char const kCliAsteroidSourceFile[] = "E:\\FL\\Scratch\\Source\\Client\\Space\\CliAsteroid.cpp";

    static_assert(sizeof(FCliAsteroidCubeMap) <= sizeof(FCliAsteroid::CubeMapStorage),
                  "the cube map must fit the retail 0x14 bytes at FCliAsteroid+0x08");
    static_assert(alignof(FCliAsteroidCubeMap) <= 8, "FCliAsteroid+0x08 is 8-aligned");
}

// ===========================================================================
// MedAsteroid
// ===========================================================================

FCliMedAsteroidGameobj::FCliMedAsteroidGameobj()
    : FClientGameobj(EClientGameobjKind::MedAsteroid)
{
    // bWatchListed (+0xa8) is not written here; ApplyEvent stores the instant-kill byte into it
    // after PostCreateInit (slot 92) has already read it.
}

FCliMedAsteroidGameobj::~FCliMedAsteroidGameobj()
{
}

bool FCliMedAsteroidGameobj::find(unsigned const& Id)
{
    // The exe imports the reference overload of CObject::Find and passes the slot's own
    // reference argument through.
    Object = CObject::Find(Id, CObject::ClassAsteroid);
    if (Object != nullptr)
        advise(true);
    return Object != nullptr;
}

void FCliMedAsteroidGameobj::OnSurfaceCollisionHit(void const* Contact, void const* B, FClientGameobj* Other,
                                     void const* D)
{
    // Retail also tests Other's CBase (+0xc), never null for a live Other. class & 0x503 is a
    // CShip.
    if (bWatchListed != 0 && Other != nullptr && Other->GetObject() != nullptr &&
        (static_cast<unsigned>(Other->GetObject()->ObjectClass) & CObject::ClassShip) == CObject::ClassShip)
    {
        Explode(static_cast<Vector const*>(Contact));
        return;
    }
    // Retail tail-jumps to GameObject's FUN_00539cd0.
    FClientGameobj::OnSurfaceCollisionHit(Contact, B, Other, D);
}

void FCliMedAsteroidGameobj::ReportCollisionToServer(void const* A, void const* B, FClientGameobj* Other,
                                     void const* D)
{
    if (bWatchListed == 0)
    {
        FClientGameobj::ReportCollisionToServer(A, B, Other, D);
        return;
    }

    if (Other->get_id() == 0)
        return;

    // In multiplayer only an Other the local player owns reports -- its class & 3 object's
    // OwnerPlayer (+0xb4), or the player connection id (FUN_0054bb00) when the narrow fails.
    if (!SinglePlayer())
    {
        CObject* const OtherObj = Other->GetObject();
        unsigned Owner = 0;
        if (OtherObj != nullptr && (static_cast<unsigned>(OtherObj->ObjectClass) & CObject::ClassSimple) == CObject::ClassSimple)
            Owner = static_cast<CSimple const*>(OtherObj)->OwnerPlayer;
        else
            Owner = GetPlayerConnectionId();
        if (Owner != GetPlayerConnectionId())
            return;
    }

    // No null test of the server interface. Retail leaves the word at +0x0a as the stack held
    // it; the port zeroes it.
    SSPBadLandsObjCollisionInfo Info;
    Info.BadLandsObjId = get_id();
    Info.ObjectId = Other->get_id();
    Info.SubObjId = static_cast<unsigned short>(reinterpret_cast<uintptr_t>(D));
    Info.Pad_0x0a = 0;
    static_cast<IServer*>(GetLocalServerInterface())
        ->SPBadLandsObjCollision(Info, GetPlayerConnectionId());
}

void FCliMedAsteroidGameobj::Explode(Vector const* Pos)
{
    if (Actions.get_action_count() != 0)
        return;

    if (ExplosionArch == nullptr)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kCliAsteroidSourceFile, 0x7a5,
              "asteroid_mine told to explode but has no explosion archetype");
        return;
    }
    if (ExplosionArch->Process != Archetype::Explosion::ProcessNone)
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kCliAsteroidSourceFile, 0x79c,
              "Explosion process for asteroid should be set to none");
        return;
    }
    // The pointer itself, no stack copy (unlike DynamicAsteroid's Explode).
    Vector const* const Where = (Pos != nullptr) ? Pos : &get_position();
    Actions.add(Action_Explode::Create(ExplosionArch, this, Where));
}

unsigned short* FCliMedAsteroidGameobj::Slot1bc(unsigned short* Out, unsigned A, unsigned B,
                                               unsigned C, unsigned D)
{
    (void)A;
    (void)B;
    (void)C;
    (void)D;
    return CliAsteroidSlot1bcBody(Out);
}

unsigned short* CliAsteroidSlot1bcBody(unsigned short* Out)
{
    Out[0] = 0;
    Out[1] = 0xffff;
    return Out;
}

// ===========================================================================
// The cube value
// ===========================================================================

FCliAsteroidCube::FCliAsteroidCube()
    : CubeId(0)
    , CubeSize(0xfa)
{
}

FCliAsteroidCube::~FCliAsteroidCube()
{
    for (std::map<unsigned long, FCliMedAsteroidGameobj*>::iterator It = Asteroids.begin();
         It != Asteroids.end(); ++It)
    {
        It->second->MarkForRemoval(true);
    }
}

void FCliAsteroidCube::ApplyEvent(CmnAsteroid::asteroid_marker const* Event)
{
    std::map<unsigned long, FCliMedAsteroidGameobj*>::iterator const It =
        Asteroids.find(Event->AsteroidId);

    if (Event->bCubeActive) // +0x0c
    {
        if (It != Asteroids.end())
        {
            FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kCliAsteroidSourceFile, 0x72d,
                  "asteroid cube told to create asteroid that already exists");
            return;
        }
        FCliMedAsteroidGameobj* const Med = new FCliMedAsteroidGameobj();
        Asteroids.insert(std::pair<unsigned long const, FCliMedAsteroidGameobj*>(
            Event->AsteroidId, Med));
        Med->find(Event->ObjId);
        Med->PostCreateInit();
        // The instant-kill byte is stored after PostCreateInit, as retail.
        Med->bWatchListed = Event->bInstantKill ? 1 : 0;
        ClientObjDb_RegisterGameobj(Med, false);
        return;
    }

    if (It == Asteroids.end())
    {
        FDUMP(kFdumpWarning, "%s(%d) : *** WARNING: %s", kCliAsteroidSourceFile, 0x739,
              "asteroid cube told to delete asteroid that does not exist");
        return;
    }
    It->second->MarkForRemoval(true);
    Asteroids.erase(It);
}

// ===========================================================================
// The field's cube map (FCliAsteroid +0x08)
// ===========================================================================

FCliAsteroidCubeMap& CliAsteroidCubeMap(FCliAsteroid* Field)
{
    return *reinterpret_cast<FCliAsteroidCubeMap*>(Field->CubeMapStorage);
}

void CliAsteroidCubeMapConstruct(FCliAsteroid* Field)
{
    // Retail's allocator bytes (+0x08, +0x09), the byte at +0x14 and the head node -- one
    // std::map here.
    new (Field->CubeMapStorage) FCliAsteroidCubeMap();
}

void CliAsteroidCubeMapDestruct(FCliAsteroid* Field)
{
    // Each pair's destructor (FUN_00522480) runs the value's FUN_00524af0.
    CliAsteroidCubeMap(Field).~FCliAsteroidCubeMap();
}
